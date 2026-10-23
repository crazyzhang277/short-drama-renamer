param(
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) '短剧更名工具.exe'),
    [string]$Out = (Join-Path (Split-Path -Parent $PSScriptRoot) 'shot.png'),
    [string]$DropDir = ''
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[W]::SetProcessDPIAware() | Out-Null

$proc = Start-Process -FilePath $Exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { Write-Host "process exited code=$($proc.ExitCode)"; exit 1 }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
if ($h -eq [IntPtr]::Zero) { Write-Host 'no window'; $proc | Stop-Process -Force; exit 1 }

[W]::ShowWindow($h, 5) | Out-Null
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500

if ($DropDir -ne '') {
    # 向「视频文件夹」编辑框写入路径 (IDC_EDIT_FOLDER = 1001)
    $edit = [W]::FindWindowEx($h, [IntPtr]::Zero, 'Edit', $null)
}

$r = New-Object W+RECT
[W]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top

$bmp = New-Object System.Drawing.Bitmap($w, $hh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $hh)))
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

Start-Sleep -Milliseconds 200
$proc | Stop-Process -Force
Write-Host "saved $Out ($w x $hh)"
