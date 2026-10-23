param(
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) '短剧更名工具.exe'),
    [string]$Demo = (Join-Path (Split-Path -Parent $PSScriptRoot) '_demo\Dignity Reborn')
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class E {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
}
"@
[E]::SetProcessDPIAware() | Out-Null

if (Test-Path $Demo) { Remove-Item $Demo -Recurse -Force }
New-Item -ItemType Directory -Path $Demo -Force | Out-Null
foreach ($n in @('随便 3', '随便 1', '随便 10', '随便 2')) {
    [System.IO.File]::WriteAllBytes((Join-Path $Demo "$n.mp4"), [byte[]](0..255))
}
[System.IO.File]::WriteAllText((Join-Path $Demo '说明.txt'), 'x')

$proc = Start-Process -FilePath $Exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { Write-Host "exited $($proc.ExitCode)"; exit 1 }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
Start-Sleep -Milliseconds 500

[E]::SendMessageStr([E]::GetDlgItem($h,1004), 0x000C, [IntPtr]::Zero, '') | Out-Null
[E]::SendMessageStr([E]::GetDlgItem($h,1001), 0x000C, [IntPtr]::Zero, $Demo) | Out-Null
Start-Sleep -Milliseconds 250
[E]::SendMessage($h, 0x0111, [IntPtr]1018, [E]::GetDlgItem($h,1018)) | Out-Null
Start-Sleep -Milliseconds 500

# 标签 = 索引 2 (True heiress)
$tag = [E]::GetDlgItem($h, 1008)
[E]::SendMessage($tag, 0x014E, [IntPtr]2, [IntPtr]::Zero) | Out-Null
[E]::SendMessage($h, 0x0111, [IntPtr](1008 -bor (1 -shl 16)), $tag) | Out-Null
Start-Sleep -Milliseconds 900

Write-Host "--- 改名之前 ---"
Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object { Write-Host "  $($_.Name)" }

# 点执行改名（会弹确认框 -> 自动点「是」）
[E]::SendMessage($h, 0x0111, [IntPtr]1012, [E]::GetDlgItem($h,1012)) | Out-Null
Start-Sleep -Milliseconds 900

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class D {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
"@
$dlg = [D]::FindWindowW('#32770', $null)
if ($dlg -ne [IntPtr]::Zero) {
    $yes = [D]::GetDlgItem($dlg, 6)   # IDYES
    [D]::PostMessage($dlg, 0x0111, [IntPtr]6, $yes) | Out-Null
    Write-Host "(已点确认框的「是」)"
} else {
    Write-Host "(没有确认框，可能直接执行了)"
}
Start-Sleep -Milliseconds 1500

# 关掉可能的「完成」提示框
for ($k = 0; $k -lt 4; $k++) {
    $d2 = [D]::FindWindowW('#32770', $null)
    if ($d2 -eq [IntPtr]::Zero) { break }
    [D]::PostMessage($d2, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 400
}

Write-Host "--- 改名之后 ---"
Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object { Write-Host "  $($_.Name)" }
$undo = Join-Path $Demo '.rename-tool'
if (Test-Path $undo) {
    Write-Host "--- 撤销记录 ---"
    Get-ChildItem $undo -File | ForEach-Object { Write-Host "  $($_.Name)" }
}

# 撤销
[E]::SendMessage($h, 0x0111, [IntPtr]1013, [E]::GetDlgItem($h,1013)) | Out-Null
Start-Sleep -Milliseconds 800
for ($k = 0; $k -lt 4; $k++) {
    $d2 = [D]::FindWindowW('#32770', $null)
    if ($d2 -eq [IntPtr]::Zero) { break }
    [D]::PostMessage($d2, 0x0111, [IntPtr]6, [D]::GetDlgItem($d2,6)) | Out-Null
    Start-Sleep -Milliseconds 500
}
Start-Sleep -Milliseconds 800
Write-Host "--- 撤销之后 ---"
Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object { Write-Host "  $($_.Name)" }

$proc | Stop-Process -Force
