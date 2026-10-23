param(
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) '短剧更名工具.exe'),
    [string]$Dir = (Join-Path $env:TEMP 'dur-test'),
    [string]$ShotMain = (Join-Path (Split-Path -Parent $PSScriptRoot) 'shot-duration.png'),
    [string]$ShotDlg  = (Join-Path (Split-Path -Parent $PSScriptRoot) 'shot-duration-dialog.png')
)
# 点「统计总时长」按钮（IDC_BTN_DURATION = 1024），读出弹窗文字，截图，再关掉弹窗
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class D {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static string Text(IntPtr h) {
    var sb = new StringBuilder(2048); GetWindowText(h, sb, 2048); return sb.ToString();
  }
  public static string DialogText(IntPtr dlg) {
    var sb = new StringBuilder();
    EnumChildWindows(dlg, (c, p) => { var s = Text(c); if (s.Length > 0) sb.AppendLine(s); return true; }, IntPtr.Zero);
    return sb.ToString();
  }
}
"@
[D]::SetProcessDPIAware() | Out-Null

function Capture([IntPtr]$h, [string]$path) {
    $r = New-Object D+RECT
    [D]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap($w, $hh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
    $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

$proc = Start-Process -FilePath $Exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250; $proc.Refresh()
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
if ($h -eq [IntPtr]::Zero) { 'no window'; exit 1 }
[D]::ShowWindow($h, 5) | Out-Null
[D]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500

# 视频文件夹 = IDC_EDIT_FOLDER(1001)
$edit = [D]::GetDlgItem($h, 1001)
if ($edit -eq [IntPtr]::Zero) { 'no folder edit'; $proc | Stop-Process -Force; exit 1 }
[D]::SendMessage($edit, 0x000C, [IntPtr]::Zero, $Dir) | Out-Null
Start-Sleep -Milliseconds 300
Capture $h $ShotMain

# 统计总时长 = IDC_BTN_DURATION(1024)
[D]::PostMessage($h, 0x0111, [IntPtr]1024, [IntPtr]::Zero) | Out-Null

$dlg = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 200
    $dlg = [D]::FindWindow('#32770', '统计总时长')
    if ($dlg -ne [IntPtr]::Zero) { break }
}
if ($dlg -eq [IntPtr]::Zero) { 'no dialog'; $proc | Stop-Process -Force; exit 1 }
Start-Sleep -Milliseconds 400
Capture $dlg $ShotDlg
'---- dialog text ----'
[D]::DialogText($dlg).TrimEnd()
'---------------------'
[D]::PostMessage($dlg, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null   # IDOK
Start-Sleep -Milliseconds 500
$proc | Stop-Process -Force
'closed'
