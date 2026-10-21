param(
    [string]$Exe = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\src\renamer.exe',
    [string]$Demo = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\_demo\Dignity Reborn'
)

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class V {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
"@
[V]::SetProcessDPIAware() | Out-Null

if (Test-Path $Demo) { Remove-Item $Demo -Recurse -Force }
New-Item -ItemType Directory -Path $Demo -Force | Out-Null
foreach ($n in @('随便 3', '随便 1', '随便 10', '随便 2')) {
    [System.IO.File]::WriteAllBytes((Join-Path $Demo "$n.mp4"), [byte[]](0..255))
}
[System.IO.File]::WriteAllText((Join-Path $Demo '说明.txt'), 'x')

function Txt($h) {
    $sb = New-Object System.Text.StringBuilder 4096
    [V]::GetWindowTextW($h, $sb, 4096) | Out-Null
    return $sb.ToString()
}
function Cls($h) {
    $sb = New-Object System.Text.StringBuilder 256
    [V]::GetClassNameW($h, $sb, 256) | Out-Null
    return $sb.ToString()
}

$proc = Start-Process -FilePath $Exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    $proc.Refresh()
    if ($proc.HasExited) { Write-Host "exited $($proc.ExitCode)"; exit 1 }
    if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
$script:pid2 = [uint32]$proc.Id
Write-Host "main=$h pid=$($script:pid2)"
Start-Sleep -Milliseconds 500

[V]::SendMessageStr([V]::GetDlgItem($h,1004), 0x000C, [IntPtr]::Zero, '') | Out-Null
[V]::SendMessageStr([V]::GetDlgItem($h,1001), 0x000C, [IntPtr]::Zero, $Demo) | Out-Null
Start-Sleep -Milliseconds 200
Write-Host "状态1 = [$((Txt ([V]::GetDlgItem($h,1017))))]"

$gb = [V]::GetDlgItem($h, 1018)
[V]::SendMessage($h, 0x0111, [IntPtr]1018, $gb) | Out-Null
Start-Sleep -Milliseconds 500
Write-Host "识别后剧名 = [$((Txt ([V]::GetDlgItem($h,1004))))]"

$lv = [V]::GetDlgItem($h, 1003)
$cnt = [int][V]::SendMessage($lv, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host "自动预览后列表行数 = $cnt"
Write-Host "状态2 = [$((Txt ([V]::GetDlgItem($h,1017))))]"

# 切标签 -> 索引 3 (True heiress)
$tag = [V]::GetDlgItem($h, 1008)
[V]::SendMessage($tag, 0x014E, [IntPtr]3, [IntPtr]::Zero) | Out-Null
[V]::SendMessage($h, 0x0111, [IntPtr](1008 -bor (1 -shl 16)), $tag) | Out-Null
Start-Sleep -Milliseconds 900
Write-Host "切标签后状态 = [$((Txt ([V]::GetDlgItem($h,1017))))]"

# 切排序 -> 修改时间
$srt = [V]::GetDlgItem($h, 1007)
[V]::SendMessage($srt, 0x014E, [IntPtr]2, [IntPtr]::Zero) | Out-Null
[V]::SendMessage($h, 0x0111, [IntPtr](1007 -bor (1 -shl 16)), $srt) | Out-Null
Start-Sleep -Milliseconds 900
Write-Host "切排序后状态 = [$((Txt ([V]::GetDlgItem($h,1017))))]"

# 打开教程
$gh = [V]::GetDlgItem($h, 1020)
[V]::SendMessage($h, 0x0111, [IntPtr]1020, $gh) | Out-Null
Start-Sleep -Milliseconds 800

$script:found = @()
$cb = [V+EnumProc]{
    param($w, $l)
    $p = [uint32]0
    [V]::GetWindowThreadProcessId($w, [ref]$p) | Out-Null
    if ($p -eq $script:pid2) {
        $script:found += [pscustomobject]@{ H=$w; C=(Cls $w); T=(Txt $w); Vis=[V]::IsWindowVisible($w) }
    }
    return $true
}
[V]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Write-Host "`n本进程顶层窗口:"
$script:found | ForEach-Object { Write-Host ("  {0} vis={1} class={2} title={3}" -f $_.H, $_.Vis, $_.C, $_.T) }

$tut = ($script:found | Where-Object { $_.C -eq 'RenamerTutorialWnd' } | Select-Object -First 1)
if ($tut) {
    Write-Host "`n教程窗口内控件:"
    $cb2 = [V+EnumProc]{
        param($w, $l)
        Write-Host ("  id={0} class={1} len={2}" -f [V]::GetDlgCtrlID($w), (Cls $w), (Txt $w).Length)
        return $true
    }
    [V]::EnumChildWindows($tut.H, $cb2, [IntPtr]::Zero) | Out-Null
} else {
    Write-Host "`n!! 教程窗口没有创建出来"
}

$proc | Stop-Process -Force
