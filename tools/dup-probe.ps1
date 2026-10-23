param(
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) '短剧更名工具.exe'),
    [string]$Demo = (Join-Path (Split-Path -Parent $PSScriptRoot) '_dup\Test Drama')
)

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class P {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, [In, Out] ref RECT r);
  public static string Txt(IntPtr h) { var sb = new StringBuilder(2048); GetWindowTextW(h, sb, 2048); return sb.ToString(); }
}
[StructLayout(LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
"@
[P]::SetProcessDPIAware() | Out-Null

if (Test-Path $Demo) { Remove-Item $Demo -Recurse -Force }
New-Item -ItemType Directory -Path $Demo -Force | Out-Null
foreach ($n in @('1','2','EP08','第8集','9')) {
    [System.IO.File]::WriteAllBytes((Join-Path $Demo "$n.mp4"), [byte[]](0..255))
}

$proc = Start-Process -FilePath $Exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250; $proc.Refresh()
    if ($proc.HasExited) { Write-Host "exited"; exit 1 }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
Start-Sleep -Milliseconds 600

[P]::SendMessageStr([P]::GetDlgItem($h,1001), 0x000C, [IntPtr]::Zero, $Demo) | Out-Null
[P]::SendMessageStr([P]::GetDlgItem($h,1004), 0x000C, [IntPtr]::Zero, 'Test Drama') | Out-Null
[P]::SendMessageStr([P]::GetDlgItem($h,1005), 0x000C, [IntPtr]::Zero, '1') | Out-Null
[P]::SendMessageStr([P]::GetDlgItem($h,1006), 0x000C, [IntPtr]::Zero, '2') | Out-Null

$ck = [P]::GetDlgItem($h, 1020)
Write-Host "checkbox exists: $($ck -ne [IntPtr]::Zero)  text=[$([P]::Txt($ck))]"
[P]::SendMessage($ck, 0x00F1, [IntPtr]1, [IntPtr]::Zero) | Out-Null
[P]::SendMessage($h, 0x0111, [IntPtr]1020, $ck) | Out-Null
Start-Sleep -Milliseconds 800

$tag = [P]::GetDlgItem($h, 1008)
[P]::SendMessage($tag, 0x014E, [IntPtr]0, [IntPtr]::Zero) | Out-Null
[P]::SendMessage($h, 0x0111, [IntPtr](1008 -bor (1 -shl 16)), $tag) | Out-Null
Start-Sleep -Milliseconds 1200

$list = [P]::GetDlgItem($h, 1003)
$cnt = [P]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)   # LVM_GETITEMCOUNT
Write-Host "列表行数 = $cnt"
Write-Host "状态栏 = [$([P]::Txt([P]::GetDlgItem($h,1017)))]"

# 截图看列表里的新名
Add-Type -AssemblyName System.Drawing
$r = New-Object RECT
[P]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.Right - $r.Left), ($r.Bottom - $r.Top)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$bmp.Save((Join-Path (Split-Path -Parent $PSScriptRoot) '_dup-before.png'))
$g.Dispose(); $bmp.Dispose()
Write-Host "已截图 _dup-before.png"

[P]::SendMessage($h, 0x0111, [IntPtr]1012, [P]::GetDlgItem($h,1012)) | Out-Null
$d = [IntPtr]::Zero
for ($i = 0; $i -lt 24; $i++) {
    Start-Sleep -Milliseconds 250
    $d = [P]::FindWindowW('#32770', $null)
    if ($d -ne [IntPtr]::Zero) { break }
}
if ($d -ne [IntPtr]::Zero) {
    Write-Host "弹窗标题 = [$([P]::Txt($d))]"
    Write-Host "弹窗正文 ="
    [P]::Txt([P]::GetDlgItem($d, 0xFFFF)) -split "`n" | ForEach-Object { Write-Host "   $_" }
    [P]::PostMessage($d, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400
} else {
    Write-Host "!! 没有弹窗"
}

Write-Host "`n改名之后:"
Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object { Write-Host "  $($_.Name)" }
$proc | Stop-Process -Force
