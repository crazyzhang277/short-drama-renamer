param(
    [string]$Exe = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\src\renamer.exe',
    [string]$Demo = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\_dup\Test Drama'
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Q {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[Q]::SetProcessDPIAware() | Out-Null

if (Test-Path $Demo) { Remove-Item $Demo -Recurse -Force }
New-Item -ItemType Directory -Path $Demo -Force | Out-Null
foreach ($n in @('1','2','EP08','第8集','9')) {
    [System.IO.File]::WriteAllBytes((Join-Path $Demo "$n.mp4"), [byte[]](0..255))
}
Write-Host "原始: " + ((Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object { $_.BaseName }) -join ', ')

$proc = Start-Process -FilePath $Exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250; $proc.Refresh()
    if ($proc.HasExited) { Write-Host "exited"; exit 1 }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
Start-Sleep -Milliseconds 500

[Q]::SendMessageStr([Q]::GetDlgItem($h,1001), 0x000C, [IntPtr]::Zero, $Demo) | Out-Null
[Q]::SendMessageStr([Q]::GetDlgItem($h,1004), 0x000C, [IntPtr]::Zero, 'Test Drama') | Out-Null
[Q]::SendMessageStr([Q]::GetDlgItem($h,1005), 0x000C, [IntPtr]::Zero, '1') | Out-Null
[Q]::SendMessageStr([Q]::GetDlgItem($h,1006), 0x000C, [IntPtr]::Zero, '2') | Out-Null

$ck = [Q]::GetDlgItem($h, 1020)
[Q]::SendMessage($ck, 0x00F1, [IntPtr]1, [IntPtr]::Zero) | Out-Null
[Q]::SendMessage($h, 0x0111, [IntPtr]1020, $ck) | Out-Null
Start-Sleep -Milliseconds 600

$tag = [Q]::GetDlgItem($h, 1008)
[Q]::SendMessage($tag, 0x014E, [IntPtr]0, [IntPtr]::Zero) | Out-Null
[Q]::SendMessage($h, 0x0111, [IntPtr](1008 -bor (1 -shl 16)), $tag) | Out-Null
Start-Sleep -Milliseconds 900

[Q]::PostMessage($h, 0x0111, [IntPtr]1012, [Q]::GetDlgItem($h,1012)) | Out-Null
Start-Sleep -Milliseconds 1200

$d = [IntPtr]::Zero
for ($i = 0; $i -lt 20; $i++) {
    $d = [Q]::FindWindowW('#32770', $null)
    if ($d -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 250
}
if ($d -ne [IntPtr]::Zero) {
    $sb = New-Object System.Text.StringBuilder 1024
    [Q]::GetWindowTextW($d, $sb, 1024) | Out-Null
    Write-Host "弹窗标题: $($sb.ToString())"
    # 读正文
    $stat = [Q]::GetDlgItem($d, 0xFFFF)
    $sb2 = New-Object System.Text.StringBuilder 1024
    [Q]::GetWindowTextW($stat, $sb2, 1024) | Out-Null
    Write-Host "弹窗内容:"
    ($sb2.ToString() -split "`n") | ForEach-Object { Write-Host "   $_" }
    [Q]::PostMessage($d, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
} else {
    Write-Host "!! 没有弹出冲突提示"
}

Write-Host "`n改名之后（应该原封不动）:"
Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object { Write-Host "  $($_.Name)" }
$proc | Stop-Process -Force
