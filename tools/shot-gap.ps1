param(
    [string]$Exe = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\短剧更名工具.exe',
    [string]$Demo = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\_demo\The CEO Heiress'
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class V {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[V]::SetProcessDPIAware() | Out-Null

if (Test-Path $Demo) { Remove-Item $Demo -Recurse -Force }
New-Item -ItemType Directory -Path $Demo -Force | Out-Null
foreach ($n in @(1,2,3,8,9,10,11)) {
    [System.IO.File]::WriteAllBytes((Join-Path $Demo "$n.mp4"), [byte[]](0..255))
}

function Shot($hwnd, $path) {
    $r = New-Object V+RECT
    [V]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap($w, $hh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [V]::PrintWindow($hwnd, $hdc, 2)   # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "  saved $path ($w x $hh) printwindow=$ok"
}

function RunOne($keep, $out) {
    $proc = Start-Process -FilePath $Exe -PassThru
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250; $proc.Refresh()
        if ($proc.HasExited) { Write-Host "exited"; return }
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
    }
    [V]::ShowWindow($h, 5) | Out-Null
    [V]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null   # HWND_TOPMOST, NOSIZE|NOMOVE
    [V]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 700

    [V]::SendMessageStr([V]::GetDlgItem($h,1004), 0x000C, [IntPtr]::Zero, '') | Out-Null
    [V]::SendMessageStr([V]::GetDlgItem($h,1001), 0x000C, [IntPtr]::Zero, $Demo) | Out-Null
    Start-Sleep -Milliseconds 250
    [V]::SendMessage($h, 0x0111, [IntPtr]1018, [V]::GetDlgItem($h,1018)) | Out-Null   # 识别剧名
    Start-Sleep -Milliseconds 500

    $ck = [V]::GetDlgItem($h, 1020)
    [V]::SendMessage($ck, 0x00F1, [IntPtr]$(if ($keep) {1} else {0}), [IntPtr]::Zero) | Out-Null
    [V]::SendMessage($h, 0x0111, [IntPtr]1020, $ck) | Out-Null
    Start-Sleep -Milliseconds 400

    [V]::SendMessage($h, 0x0111, [IntPtr](1008 -bor (1 -shl 16)), [V]::GetDlgItem($h,1008)) | Out-Null
    Start-Sleep -Milliseconds 900

    [V]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 500
    Shot $h $out
    [V]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null   # HWND_NOTOPMOST
    $proc | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

RunOne $false 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\shot-gap-off.png'
RunOne $true  'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\shot-gap-on.png'
Write-Host 'done'
