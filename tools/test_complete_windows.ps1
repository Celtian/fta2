# Validate a complete package on an actual Windows desktop with isolated saves.
param(
    [Parameter(Mandatory=$true)][string]$Package,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$OriginalSave
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding
$utf8 = New-Object System.Text.UTF8Encoding($false)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh output directory.' }
$work = New-Item -ItemType Directory -Path $OutputDirectory
Expand-Archive -LiteralPath $Package -DestinationPath $work.FullName
$directories = @(Get-ChildItem -LiteralPath $work.FullName -Directory)
if ($directories.Count -ne 1) { throw 'Expected a single package directory.' }
$game = $directories[0].FullName
$exe = Join-Path $game 'bin\fta2-native.exe'
$active = $null
$results = @()

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Start-Game([string]$Arguments, [bool]$Automated=$false, [bool]$Launcher=$false) {
    $p = New-Object System.Diagnostics.Process
    $p.StartInfo.FileName = $script:exe
    if ($Launcher) { $p.StartInfo.FileName = Join-Path $script:game 'Faery Tale Adventure II.exe' }
    $p.StartInfo.Arguments = $Arguments
    # Deliberately unrelated to the executable and bundled data directories.
    $p.StartInfo.WorkingDirectory = $work.FullName
    $p.StartInfo.UseShellExecute = $false
    $p.StartInfo.CreateNoWindow = $true
    $p.StartInfo.RedirectStandardOutput = $true
    $p.StartInfo.RedirectStandardError = $true
    if ($Launcher) {
        # The launcher check injects window messages rather than physical raw input.
        $p.StartInfo.EnvironmentVariables['SDL_WINDOWS_RAW_KEYBOARD'] = '0'
    }
    if ($Automated) {
        # Keep desktop cursor/window events out of injected-input scenarios.
        $p.StartInfo.EnvironmentVariables['SDL_VIDEODRIVER'] = 'dummy'
        $p.StartInfo.EnvironmentVariables['SDL_AUDIODRIVER'] = 'dummy'
    }
    [void]$p.Start()
    $script:active = $p
    $stdout = $p.StandardOutput.ReadToEndAsync()
    $stderr = $p.StandardError.ReadToEndAsync()
    $parent = $null
    if ($Launcher -and $Arguments.StartsWith('--save-dir')) {
        $parent = $p
        for ($i=0; $i -lt 100 -and -not $parent.HasExited; $i++) {
            $child = Get-CimInstance Win32_Process -Filter ('ParentProcessId='+$parent.Id) |
                Where-Object { $_.Name -eq 'fta2-native.exe' } | Select-Object -First 1
            if ($child) { $p = Get-Process -Id $child.ProcessId; $null = $p.Handle; break }
            Start-Sleep -Milliseconds 100
        }
        Assert-True ($p.Id -ne $parent.Id) 'Launcher did not create the game process.'
        Assert-True ($p.Path -eq $script:exe) 'Launcher started a different installation.'
        $script:active = $p
    }
    return [pscustomobject]@{
        process=$p; stdout=$stdout; stderr=$stderr; launcher=$parent
    }
}
function Finish-Game($App, [string]$Name, [int]$ExpectedExit=0) {
    $finished = $App.process.WaitForExit(300000)
    if (-not $finished) { $App.process.Kill(); $App.process.WaitForExit() }
    if ($App.launcher) {
        Assert-True ($App.launcher.WaitForExit(30000)) 'Launcher did not finish with its child.'
        Assert-True ($App.launcher.ExitCode -eq $ExpectedExit) 'Launcher did not return the child exit code.'
    }
    $result = [pscustomobject]@{
        name=$Name; finished=$finished; exit_code=$App.process.ExitCode
        stdout=$App.stdout.Result.ToString(); stderr=$App.stderr.Result.ToString()
    }
    [IO.File]::WriteAllText((Join-Path $work.FullName ($Name+'.err')), $result.stderr, $utf8)
    $script:active = $null
    Assert-True ($finished -and $result.exit_code -eq $ExpectedExit) ('Failed: '+$Name+' '+$result.stderr)
    return $result
}
function Wait-GameWindow($App) {
    for ($i=0; $i -lt 100 -and -not $App.process.HasExited; $i++) {
        $App.process.Refresh()
        Assert-True (-not $App.process.MainWindowTitle.StartsWith('Select the Faery')) 'Unexpected folder picker.'
        if ($App.process.MainWindowTitle -eq 'Faery Tale Adventure II') { return }
        Start-Sleep -Milliseconds 100
    }
    throw 'Expected game/error window was not shown.'
}
function Read-DataPath([string]$Saves) {
    return ((Get-Content -Encoding UTF8 -LiteralPath (Join-Path $Saves 'config.ini') |
        Where-Object { $_.StartsWith('data_dir=') }) -replace '^data_dir=', '').Replace('/', '\')
}
function Run-Probe([string]$Name, [string]$Saves, [string]$Extra='') {
    $output = Join-Path $work.FullName ($Name+'-output')
    $app = Start-Game ('--save-dir "'+$Saves+'" --smoke-test "'+$output+'" '+$Extra) $true
    return Finish-Game $app $Name
}

Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public class Fta2LauncherIcon {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct Info {
  public IntPtr icon; public int index; public uint attributes;
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string name;
  [MarshalAs(UnmanagedType.ByValTStr, SizeConst=80)] public string type;
 }
 [DllImport("shell32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SHGetFileInfo(string path,uint attr,out Info info,uint size,uint flags);
 [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon);
 [DllImport("user32.dll", SetLastError=true)] public static extern bool PostMessage(IntPtr window,uint message,IntPtr key,IntPtr flags);
 public static string Pixels(string path) {
  Info info; SHGetFileInfo(path,0,out info,(uint)Marshal.SizeOf(typeof(Info)),0x101);
  if(info.icon==IntPtr.Zero) throw new Exception("Cannot extract shell icon");
  try {
   using(var icon=Icon.FromHandle(info.icon)) using(var bitmap=icon.ToBitmap()) {
    var pixels=new byte[bitmap.Width*bitmap.Height*4]; int at=0;
    for(int y=0;y<bitmap.Height;y++) for(int x=0;x<bitmap.Width;x++) {
     var c=bitmap.GetPixel(x,y); pixels[at++]=c.R;pixels[at++]=c.G;pixels[at++]=c.B;pixels[at++]=c.A;
    }
    return Convert.ToBase64String(pixels);
   }
  } finally { DestroyIcon(info.icon); }
 }
}
'@ -ReferencedAssemblies System.Drawing

function Test-Launcher([string]$Name) {
    $launcher = Join-Path $script:game 'Faery Tale Adventure II.exe'
    Assert-True (Test-Path -LiteralPath $launcher) 'Root launcher is missing.'
    Assert-True ([Fta2LauncherIcon]::Pixels($launcher) -eq [Fta2LauncherIcon]::Pixels($script:exe)) 'Launcher icon differs from the game.'
    $saves = Join-Path $work.FullName ($Name+' saves '+[char]0x017e+' '+[char]0x010d)
    $app = Start-Game ('--save-dir "'+$saves+'"') $false $true
    Wait-GameWindow $app
    Assert-True ((Read-DataPath $saves) -eq (Join-Path $script:game 'data')) 'Launcher argument forwarding or bundled discovery failed.'
    Start-Sleep -Milliseconds 300
    $screenshots = Join-Path $script:game 'screenshots'
    $beforeCount = @(Get-ChildItem -LiteralPath $screenshots -Filter '*.png' -ErrorAction SilentlyContinue).Count
    # Dispatch native window messages without changing the desktop keyboard state.
    for ($press=1; $press -le 2; $press++) {
        foreach ($message in @(0x100, 0x100, 0x101)) {
            $flags = [IntPtr]0x01370001
            if ($message -eq 0x101) { $flags = [IntPtr]([long]0xc1370001 -band 0xffffffffL) }
            Assert-True ([Fta2LauncherIcon]::PostMessage($app.process.MainWindowHandle, $message,
                [IntPtr]0x2c, $flags)) 'Cannot dispatch Print Screen.'
        }
        for ($i=0; $i -lt 100; $i++) {
            $files = @(Get-ChildItem -LiteralPath $screenshots -Filter '*.png' -ErrorAction SilentlyContinue)
            if ($files.Count -eq $beforeCount+$press -and $files[-1].Length -gt 33) { break }
            Start-Sleep -Milliseconds 100
        }
        Assert-True ($files.Count -eq $beforeCount+$press) 'Print Screen count or package-root path is incorrect.'
    }
    foreach ($file in $files) {
        $image = [Drawing.Image]::FromFile($file.FullName)
        try { Assert-True ($image.Width -eq 640 -and $image.Height -eq 480) 'Screenshot includes window borders or has wrong dimensions.' }
        finally { $image.Dispose() }
    }
    [void]$app.process.CloseMainWindow()
    return Finish-Game $app $Name
}

try {
    $before = @{}
    Get-ChildItem -LiteralPath (Join-Path $game 'data') -File -Recurse | ForEach-Object {
        $before[$_.FullName.Substring($game.Length)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    Assert-True ($before.Count -eq 8) 'Expected exactly eight bundled data files.'
    $results += Finish-Game (Start-Game '--version') 'version'
    $results += Finish-Game (Start-Game '--version' $false $true) 'launcher-version'
    $results += Finish-Game (Start-Game '--unknown-launcher-test' $false $true) 'launcher-invalid-argument' 1
    $results += Test-Launcher 'root-launcher'
    $saves = Join-Path $work.FullName 'clean-saves'
    $app = Start-Game ('--save-dir "'+$saves+'"')
    Wait-GameWindow $app
    Start-Sleep -Milliseconds 300
    [void]$app.process.CloseMainWindow()
    $results += Finish-Game $app 'clean-startup'
    Assert-True ((Read-DataPath $saves) -eq (Join-Path $game 'data')) 'Bundled path was not selected.'

    # The stored path becomes stale when moving the package.
    $moved = Join-Path $work.FullName ('Moved game - '+[char]0x017e+' '+[char]0x010d)
    Move-Item -LiteralPath $game -Destination $moved
    $game = $moved
    $script:exe = Join-Path $game 'bin\fta2-native.exe'
    $results += Test-Launcher 'moved-root-launcher'
    if ($OriginalSave) {
        Copy-Item -LiteralPath $OriginalSave -Destination (Join-Path $saves 'fta2.002')
    }
    $results += Run-Probe 'moved-unicode' $saves
    Assert-True ((Read-DataPath $saves) -eq (Join-Path $game 'data')) 'Moved package used stale data.'
    $states = @(Get-Content -Encoding UTF8 -LiteralPath (Join-Path $work.FullName 'moved-unicode-output\state.jsonl') |
        ForEach-Object { $_ | ConvertFrom-Json })
    Assert-True ($states[-1].phase -eq 'finished') 'Gameplay probe did not finish.'
    $audio = Get-Content -Encoding UTF8 -LiteralPath (Join-Path $work.FullName 'moved-unicode-output\audio.json') -Raw | ConvertFrom-Json
    Assert-True ($audio.music_nonzero_samples -gt 0 -and $audio.effects_nonzero_samples -gt 0) 'Audio generation failed.'
    $saved = $states | Where-Object { $_.phase -eq 'saved' }
    $restored = $states | Where-Object { $_.phase -eq 'restored' }
    foreach ($key in @('actor','u','v','z','rng')) {
        Assert-True ($saved.$key -eq $restored.$key) ('Save restoration mismatch: '+$key)
    }
    if ($OriginalSave) {
        Assert-True (@($states | Where-Object { $_.phase -eq 'original-save-loaded' }).Count -eq 1) 'Original save import failed.'
    }

    $external = Join-Path $work.FullName 'external-data'
    Copy-Item -LiteralPath (Join-Path $game 'data') -Destination $external -Recurse
    $staleSaves = Join-Path $work.FullName 'remembered-saves'
    [void](New-Item -ItemType Directory -Path $staleSaves)
    [IO.File]::WriteAllText((Join-Path $staleSaves 'config.ini'), ('data_dir='+$external+"`n"), $utf8)
    $results += Run-Probe 'bundled-over-remembered' $staleSaves
    Assert-True ((Read-DataPath $staleSaves) -eq (Join-Path $game 'data')) 'Remembered data overrode bundled data.'

    $archive = Join-Path $game 'data\FTA.HRS'
    $validArchive = [IO.File]::ReadAllBytes($archive)
    $damagedArchive = $validArchive.Clone()
    $damagedArchive[0] = 0
    [IO.File]::WriteAllBytes($archive, $damagedArchive)
    # A valid remembered installation must not mask a damaged bundle.
    [IO.File]::WriteAllText((Join-Path $staleSaves 'config.ini'), ('data_dir='+$external+"`n"), $utf8)
    $app = Start-Game ('--save-dir "'+$staleSaves+'" --smoke-test "'+(Join-Path $work.FullName 'damaged-output')+'"')
    $failure = Finish-Game $app 'damaged-archive' 1
    Assert-True ($failure.stderr.Contains('Invalid game archive FTA.HRS')) 'Archive error did not identify the file.'
    $results += $failure

    $app = Start-Game ('--save-dir "'+$staleSaves+'"')
    Wait-GameWindow $app
    [void]$app.process.CloseMainWindow()
    $results += Finish-Game $app 'visible-damaged-error' 1

    $overrideSaves = Join-Path $work.FullName 'override-saves'
    $results += Run-Probe 'explicit-override' $overrideSaves ('--data-dir "'+$external+'"')
    Assert-True ((Read-DataPath $overrideSaves) -eq $external) 'Explicit data path was not preferred.'
    [IO.File]::WriteAllBytes($archive, $validArchive)

    $source = Join-Path $game 'data\FTA2.EXE'
    $held = Join-Path $game 'data\FTA2.held'
    Move-Item -LiteralPath $source -Destination $held
    $app = Start-Game ('--save-dir "'+$staleSaves+'" --smoke-test "'+(Join-Path $work.FullName 'missing-output')+'"')
    $failure = Finish-Game $app 'missing-resource-source' 1
    Assert-True ($failure.stderr.Contains('missing: FTA2.EXE')) 'Missing resource source was not reported.'
    $results += $failure
    Move-Item -LiteralPath $held -Destination $source
    foreach ($name in $before.Keys) {
        Assert-True ((Get-FileHash -LiteralPath ($game+$name) -Algorithm SHA256).Hash -eq $before[$name]) 'Bundled data changed.'
    }
    $report = [pscustomobject]@{
        windows=[Environment]::OSVersion.VersionString; tests=$results; bundled_files_unchanged=$before.Count
        relocated_unicode_path=$true; unrelated_working_directory=$true; data_override_used_for_startup=$false
        original_save_import=[bool]$OriginalSave
        root_launcher_before_after_move=$true; launcher_icon_matches=$true; unicode_arguments_forwarded=$true
        native_printscreen_package_root_before_after_move=$true; screenshot_repeat_ignored=$true
    }
    $json = $report | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $work.FullName 'validation.json'), $json, $utf8)
    $json
} finally {
    if ($null -ne $active -and -not $active.HasExited) { $active.Kill(); $active.WaitForExit() }
}
