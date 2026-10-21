param(
    [string]$Exe = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\src\renamer.exe',
    [string]$Demo = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\_demo\Dignity Reborn',
    [string]$OutMain = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\shot.png',
    [string]$OutTut = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\shot-tutorial.png'
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="GetWindowTextW")] public static extern int GetText(IntPtr h, StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[W]::SetProcessDPIAware() | Out-Null

if (Test-Path $Demo) { Remove-Item $Demo -Recurse -Force }
New-Item -ItemType Directory -Path $Demo -Force | Out-Null
foreach ($n in @('随便 3', '随便 1', '随便 10', '随便 2')) {
    [System.IO.File]::WriteAllBytes((Join-Path $Demo "$n.mp4"), [byte[]](0..255))
}
[System.IO.File]::WriteAllText((Join-Path $Demo '说明.txt'), 'x')

function Shot($hwnd, $path) {
    $r = New-Object W+RECT
    [W]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap($w, $hh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "  saved $path ($w x $hh)"
}

$proc = Start-Process -FilePath $Exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { Write-Host "exited code=$($proc.ExitCode)"; exit 1 }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
$script:pid2 = [uint32]$proc.Id
[W]::ShowWindow($h, 5) | Out-Null
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700

[W]::SendMessageStr([W]::GetDlgItem($h,1004), 0x000C, [IntPtr]::Zero, '') | Out-Null
[W]::SendMessageStr([W]::GetDlgItem($h,1001), 0x000C, [IntPtr]::Zero, $Demo) | Out-Null
Start-Sleep -Milliseconds 250
[W]::SendMessage($h, 0x0111, [IntPtr]1018, [W]::GetDlgItem($h,1018)) | Out-Null   # 识别剧名
Start-Sleep -Milliseconds 600
[W]::SendMessage($h, 0x0111, [IntPtr](1008 -bor (1 -shl 16)), [W]::GetDlgItem($h,1008)) | Out-Null  # 标签 CBN_SELCHANGE
Start-Sleep -Milliseconds 900

[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
Shot $h $OutMain

[W]::SendMessage($h, 0x0111, [IntPtr]1021, [W]::GetDlgItem($h,1021)) | Out-Null   # 使用教程
Start-Sleep -Milliseconds 1000

$script:tut = [IntPtr]::Zero
$cb = [W+EnumProc]{
    param($w, $l)
    $p = [uint32]0
    [W]::GetWindowThreadProcessId($w, [ref]$p) | Out-Null
    if ($p -eq $script:pid2) {
        $sb = New-Object System.Text.StringBuilder 512
        [W]::GetText($w, $sb, 512) | Out-Null
        if ($sb.ToString() -like '使用教程*') { $script:tut = $w }
    }
    return $true
}
[W]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null

if ($script:tut -eq [IntPtr]::Zero) {
    Write-Host '  tutorial window NOT found'
} else {
    [W]::ShowWindow($script:tut, 5) | Out-Null
    [W]::SetForegroundWindow($script:tut) | Out-Null
    Start-Sleep -Milliseconds 500
    Shot $script:tut $OutTut
}

Start-Sleep -Milliseconds 200
$proc | Stop-Process -Force
Write-Host 'done'
