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
function Start-Game([string]$Arguments) {
    $p = New-Object System.Diagnostics.Process
    $p.StartInfo.FileName = $script:exe
    $p.StartInfo.Arguments = $Arguments
    # Deliberately unrelated to the executable and bundled data directories.
    $p.StartInfo.WorkingDirectory = $work.FullName
    $p.StartInfo.UseShellExecute = $false
    $p.StartInfo.CreateNoWindow = $true
    $p.StartInfo.RedirectStandardOutput = $true
    $p.StartInfo.RedirectStandardError = $true
    [void]$p.Start()
    $script:active = $p
    return [pscustomobject]@{
        process=$p; stdout=$p.StandardOutput.ReadToEndAsync(); stderr=$p.StandardError.ReadToEndAsync()
    }
}
function Finish-Game($App, [string]$Name, [int]$ExpectedExit=0) {
    $finished = $App.process.WaitForExit(30000)
    if (-not $finished) { $App.process.Kill(); $App.process.WaitForExit() }
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
    $app = Start-Game ('--save-dir "'+$Saves+'" --smoke-test "'+$output+'" '+$Extra)
    return Finish-Game $app $Name
}

try {
    $before = @{}
    Get-ChildItem -LiteralPath (Join-Path $game 'data') -File -Recurse | ForEach-Object {
        $before[$_.FullName.Substring($game.Length)] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    }
    Assert-True ($before.Count -eq 8) 'Expected exactly eight bundled data files.'
    $results += Finish-Game (Start-Game '--version') 'version'
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
    }
    $json = $report | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText((Join-Path $work.FullName 'validation.json'), $json, $utf8)
    $json
} finally {
    if ($null -ne $active -and -not $active.HasExited) { $active.Kill(); $active.WaitForExit() }
}
