# Native Windows window-state checks; only the test game's own HWND is controlled.
param(
    [Parameter(Mandatory=$true)][string]$Package,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class WindowStateUI {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; public Point(int x,int y) { X=x;Y=y; } }
 [StructLayout(LayoutKind.Sequential)] public struct MonitorInfo { public int Size; public Rect Monitor,Work; public uint Flags; }
 [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtrW(IntPtr h,int index);
 [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h,uint flags);
 [DllImport("user32.dll")] public static extern bool GetMonitorInfoW(IntPtr monitor,ref MonitorInfo info);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(Point point);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 public static void ActivateTestWindow(IntPtr h) {
  // Temporarily raise only the test window so an always-on-top host cannot intercept the click.
  SetWindowPos(h,new IntPtr(-1),0,0,0,0,0x0013);
  var r=Bounds(h);var point=new Point((r[0]+r[2])/2,(r[1]+r[3])/2);
  Point saved;GetCursorPos(out saved);
  try {
   if (WindowFromPoint(point)!=h) throw new Exception("Test window is not visible at its activation point: "+TaskbarDiagnostic(h));
   SetCursorPos(point.X,point.Y);mouse_event(2,0,0,0,UIntPtr.Zero);mouse_event(4,0,0,0,UIntPtr.Zero);
   System.Threading.Thread.Sleep(100);
  } finally { SetWindowPos(h,new IntPtr(-2),0,0,0,0,0x0013);SetCursorPos(saved.X,saved.Y); }
 }
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,System.Text.StringBuilder name,int count);
 public static string ClassName(IntPtr h) { var name=new System.Text.StringBuilder(256);GetClassNameW(h,name,256);return name.ToString(); }
 public static string TaskbarDiagnostic(IntPtr h) { var m=Monitor(h);var at=WindowFromPoint(new Point((m.Monitor.Left+m.Monitor.Right)/2,(m.Work.Bottom+m.Monitor.Bottom)/2));return "game="+h+" foreground="+GetForegroundWindow()+" foreground_class="+ClassName(GetForegroundWindow())+" point_owner="+at+" class="+ClassName(at)+" bounds="+String.Join(",",Bounds(h)); }
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect rect);
 public static int[] Bounds(IntPtr h) { Rect r;GetWindowRect(h,out r);return new[]{r.Left,r.Top,r.Right,r.Bottom}; }
 public static MonitorInfo Monitor(IntPtr h) { var m=new MonitorInfo();m.Size=Marshal.SizeOf(m);GetMonitorInfoW(MonitorFromWindow(h,2),ref m);return m; }
 public static bool HasCaption(IntPtr h) { return (GetWindowLongPtrW(h,-16).ToInt64() & 0x00C00000)!=0; }
 public static bool Fullscreen(IntPtr h) { Rect r;GetWindowRect(h,out r);var m=Monitor(h).Monitor;return !IsIconic(h) && !HasCaption(h) && r.Left==m.Left && r.Top==m.Top && r.Right==m.Right && r.Bottom==m.Bottom; }
 public static bool CoversTaskbarArea(IntPtr h) {
  var m=Monitor(h);var r=m.Monitor;var w=m.Work;
  if (w.Bottom<r.Bottom && WindowFromPoint(new Point((r.Left+r.Right)/2,(w.Bottom+r.Bottom)/2))!=h) return false;
  if (w.Top>r.Top && WindowFromPoint(new Point((r.Left+r.Right)/2,(w.Top+r.Top)/2))!=h) return false;
  if (w.Left>r.Left && WindowFromPoint(new Point((w.Left+r.Left)/2,(r.Top+r.Bottom)/2))!=h) return false;
  if (w.Right<r.Right && WindowFromPoint(new Point((w.Right+r.Right)/2,(r.Top+r.Bottom)/2))!=h) return false;
  return true;
 }
 public static int[] Size(IntPtr h) { Rect r;GetWindowRect(h,out r);return new[]{r.Right-r.Left,r.Bottom-r.Top}; }
}
'@
# Compare physical desktop coordinates even when Windows display scaling is enabled.
[void][WindowStateUI]::SetThreadDpiAwarenessContext([IntPtr](-4))
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh output directory.' }
[void](New-Item -ItemType Directory -Path $OutputDirectory)
Expand-Archive -LiteralPath $Package -DestinationPath $OutputDirectory
$root=@(Get-ChildItem -LiteralPath $OutputDirectory -Directory)[0].FullName
$p=New-Object System.Diagnostics.Process
$p.StartInfo.FileName=Join-Path $root 'bin\fta2-ce.exe'
$p.StartInfo.Arguments='--save-dir "'+(Join-Path $OutputDirectory 'saves')+'"'
$p.StartInfo.WorkingDirectory=$OutputDirectory
$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true
$p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
# Allow deterministic messages to this HWND without injecting global keystrokes.
$p.StartInfo.EnvironmentVariables['SDL_WINDOWS_RAW_KEYBOARD']='0'
$h=[IntPtr]::Zero
function Wait-Fullscreen([bool]$Expected) {
    for ($i=0;$i -lt 50 -and -not $p.HasExited;$i++) {
        if (([WindowStateUI]::Fullscreen($h) -eq $Expected) -and
            ($Expected -or ([WindowStateUI]::HasCaption($h) -and -not [WindowStateUI]::IsIconic($h)))) { return }
        Start-Sleep -Milliseconds 100
    }
    throw ('Unexpected fullscreen state; expected '+$Expected)
}
function Press-Shortcut([bool]$RightAlt=$false,[bool]$Keypad=$false,[bool]$Hold=$false) {
    $altScan=[long]0x00380001
    $enterScan=[long]0x201C0001
    if ($RightAlt) { $altScan=$altScan -bor 0x01000000 }
    if ($Keypad) { $enterScan=$enterScan -bor 0x01000000 }
    [void][WindowStateUI]::PostMessage($h,0x0104,[IntPtr]0x12,[IntPtr]$altScan)
    [void][WindowStateUI]::PostMessage($h,0x0104,[IntPtr]0x0D,[IntPtr]$enterScan)
    if (-not $Hold) { Release-Shortcut $RightAlt $Keypad }
}
function Release-Shortcut([bool]$RightAlt=$false,[bool]$Keypad=$false) {
    $altScan=[long]0x00380001
    $enterScan=[long]0x201C0001
    if ($RightAlt) { $altScan=$altScan -bor 0x01000000 }
    if ($Keypad) { $enterScan=$enterScan -bor 0x01000000 }
    [void][WindowStateUI]::PostMessage($h,0x0105,[IntPtr]0x0D,[IntPtr]($enterScan -bor 3221225472))
    [void][WindowStateUI]::PostMessage($h,0x0105,[IntPtr]0x12,[IntPtr]($altScan -bor 3221225472))
}
try {
    [void]$p.Start();$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
    for ($i=0;$i -lt 100 -and -not $p.HasExited;$i++) {
        $p.Refresh()
        if ($p.MainWindowTitle -eq 'Faery Tale Adventure II: Community Edition') { $h=$p.MainWindowHandle;break }
        Start-Sleep -Milliseconds 100
    }
    if ($h -eq [IntPtr]::Zero) { throw 'Game window did not appear.' }
    # WSL-launched processes do not inherit Explorer's foreground activation grant.
    # Click only a point verified to belong to this window, then restore the cursor.
    [WindowStateUI]::ActivateTestWindow($h)
    if ([WindowStateUI]::GetForegroundWindow() -ne $h) { throw 'Test window did not receive foreground focus.' }
    if (([WindowStateUI]::GetWindowLongPtrW($h,-20).ToInt64() -band 8) -ne 0) { throw 'Test activation left the window always on top.' }
    Wait-Fullscreen $true
    $startupBounds=[WindowStateUI]::Bounds($h)
    $monitor=[WindowStateUI]::Monitor($h)
    Start-Sleep -Milliseconds 500
    if (-not [WindowStateUI]::CoversTaskbarArea($h)) { throw ('Taskbar area is not covered by the game: '+[WindowStateUI]::TaskbarDiagnostic($h)) }
    Press-Shortcut -Hold $true
    Wait-Fullscreen $false
    # Hold Enter: subsequent native key-down messages must not toggle again.
    [void][WindowStateUI]::PostMessage($h,0x0104,[IntPtr]0x0D,[IntPtr]0x601C0001)
    Start-Sleep -Milliseconds 300
    if ([WindowStateUI]::Fullscreen($h)) { throw 'Repeated Alt+Enter toggled again.' }
    Release-Shortcut
    Press-Shortcut
    Wait-Fullscreen $true
    if (-not [WindowStateUI]::CoversTaskbarArea($h)) { throw 'Taskbar area is not covered after Alt+Enter.' }
    Press-Shortcut
    Wait-Fullscreen $false
    [void][WindowStateUI]::SetWindowPos($h,[IntPtr]::Zero,0,0,900,650,0x0016)
    Start-Sleep -Milliseconds 300
    $before=[WindowStateUI]::Size($h)
    Press-Shortcut
    Wait-Fullscreen $true
    Press-Shortcut -RightAlt $true -Keypad $true
    Wait-Fullscreen $false
    $after=[WindowStateUI]::Size($h)
    if ($before[0] -ne $after[0] -or $before[1] -ne $after[1]) { throw 'Restored size was not preserved.' }
    [void]$p.CloseMainWindow()
    if (-not $p.WaitForExit(10000) -or $p.ExitCode -ne 0) { throw 'Game did not close cleanly.' }
    $report=[pscustomobject]@{
        windows=[Environment]::OSVersion.VersionString; startup_fullscreen=$true; caption_absent=$true; covers_full_monitor=$true; covers_taskbar_area=$true
        startup_bounds=$startupBounds; monitor_bounds=@($monitor.Monitor.Left,$monitor.Monitor.Top,$monitor.Monitor.Right,$monitor.Monitor.Bottom)
        work_area_bounds=@($monitor.Work.Left,$monitor.Work.Top,$monitor.Work.Right,$monitor.Work.Bottom)
        alt_enter_windowed=$true; alt_enter_fullscreen=$true; repeated_key_ignored=$true
        resized_window_then_alt_enter=$true; right_alt_keypad_enter=$true; restored_size=$after
        native_message_injection='Own HWND only; SDL raw keyboard disabled for the test'
        activation='Temporarily raised for a verified click; normal stacking and cursor position restored before assertions'
        always_on_top=$false
        exit_code=$p.ExitCode
    }
    $json=$report | ConvertTo-Json -Depth 3
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'window-validation.json'),$json)
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'game.err'),$stderr.Result.ToString())
    $json
} finally {
    if ($p.Id -and -not $p.HasExited) { $p.Kill();$p.WaitForExit() }
}
