param(
    [string]$Exe  = (Join-Path (Split-Path -Parent $PSScriptRoot) '短剧更名工具.exe'),
    [string]$Root = (Join-Path (Split-Path -Parent $PSScriptRoot) '_demo\stats'),
    [string]$Shot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'shot-stats.png'),
    [string]$FFmpeg = 'C:\ffmpeg\bin\ffmpeg.exe'
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="GetWindowTextW")] public static extern int GetText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageSB(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[W]::SetProcessDPIAware() | Out-Null

# ---- 造测试素材（时长固定，便于断言）
$ok = 0; $bad = 0
function Check($cond, $name) {
    if ($cond) { Write-Host "  [OK]   $name"; $script:ok++ }
    else       { Write-Host "  [FAIL] $name"; $script:bad++ }
}

$dirA = Join-Path $Root '原剧'
$dirB = Join-Path $Root '海外'
foreach ($d in @($dirA, $dirB)) {
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item -ItemType Directory -Path $d -Force | Out-Null
}
$plan = @(
    @{ Dir = $dirA; Name = '01.mp4'; Sec = 61 },
    @{ Dir = $dirA; Name = '02.mp4'; Sec = 45 },
    @{ Dir = $dirA; Name = '03.mp4'; Sec = 50 },
    @{ Dir = $dirB; Name = '01.mp4'; Sec = 62 },
    @{ Dir = $dirB; Name = '02.mp4'; Sec = 46 },
    @{ Dir = $dirB; Name = '03.mp4'; Sec = 47 }
)
foreach ($p in $plan) {
    $dst = Join-Path $p.Dir $p.Name
    & $FFmpeg -hide_banner -loglevel error -y -f lavfi -i "color=c=black:s=64x64:r=5" -t $p.Sec `
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p $dst
    if (-not (Test-Path $dst)) { Write-Host "ffmpeg 生成失败: $dst"; exit 1 }
}

function Text($h, $id) {
    $c = [W]::GetDlgItem($h, $id)
    $sb = New-Object System.Text.StringBuilder 8192
    # GetWindowText 读不到别的进程里的编辑框，得发 WM_GETTEXT
    [W]::SendMessageSB($c, 0x000D, [IntPtr]8192, $sb) | Out-Null
    return $sb.ToString()
}
function SetText($h, $id, $s) {
    [W]::SendMessageStr([W]::GetDlgItem($h, $id), 0x000C, [IntPtr]::Zero, $s) | Out-Null
}
function Click($h, $id) {
    [W]::SendMessage($h, 0x0111, [IntPtr]$id, [W]::GetDlgItem($h, $id)) | Out-Null
}
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
[W]::ShowWindow($h, 5) | Out-Null
[W]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700

# ---- 点页签「剧集统计」：TabRect(1) = x 140..256, y 62..102（96dpi 基准）
$dpi = 96
try { $d = [W]::GetDpiForWindow($h); if ($d -ge 48) { $dpi = $d } } catch { }
$sx = $dpi / 96.0
$x = [int](198 * $sx)
$y = [int](82 * $sx)
$lp = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
[W]::SendMessage($h, 0x0201, [IntPtr]1, $lp) | Out-Null   # WM_LBUTTONDOWN
Start-Sleep -Milliseconds 500

Check (-not [W]::IsWindowVisible([W]::GetDlgItem($h, 1001))) '切页后改名页的文件夹框已隐藏'
Check ([W]::IsWindowVisible([W]::GetDlgItem($h, 1025))) '统计页的原剧文件夹框可见'

SetText $h 1025 $dirA
SetText $h 1027 $dirB
SetText $h 1029 '测试剧'
SetText $h 1030 '别名'
SetText $h 1033 'Test Drama'
Start-Sleep -Milliseconds 300
Click $h 1037   # 统计信息
Start-Sleep -Milliseconds 2500

$ep1 = Text $h 1031; $dur1 = Text $h 1032
$ep2 = Text $h 1034; $dur2 = Text $h 1035
$sp  = Text $h 1036
$rep = (Text $h 1039) -replace "`r`n", "`n"

Write-Host ("  集数 {0} / {1}，时长 {2} / {3}" -f $ep1, $ep2, $dur1, $dur2)
Write-Host ("  特殊情况：{0}" -f $sp)
Check ($ep1 -eq '3' -and $ep2 -eq '3') '两个文件夹各统计到 3 集'
Check ($dur1 -eq '2:36') "原剧总时长 2:36（实际 $dur1）"
Check ($dur2 -eq '2:35') "海外总时长 2:35（实际 $dur2）"
Check ($sp -eq '剧集共3集，其中第1集满1分钟，其余2集均不满一分钟，为46-47秒之间') '特殊情况文案'

$wantLast = '特殊情况: ' + $sp
$want = @(
    '原剧:',
    '测试剧（dy：别名）',
    '集数: 3',
    '时长: 2:36',
    '',
    '海外转译后:',
    'Test Drama',
    '集数: 3',
    '时长: 2:35',
    $wantLast
) -join "`n"
Check ($rep -eq $want) '报送文案格式与微信模板一致'
if ($rep -ne $want) {
    Write-Host "---- 实际 ----"; Write-Host $rep
    Write-Host "---- 期望 ----"; Write-Host $want
    Write-Host ("长度 {0} vs {1}" -f $rep.Length, $want.Length)
    $n = [Math]::Min($rep.Length, $want.Length)
    for ($i = 0; $i -lt $n; $i++) {
        if ($rep[$i] -ne $want[$i]) {
            Write-Host ("第一个不同的位置 {0}：实际 0x{1:X4} 期望 0x{2:X4}" -f $i, [int]$rep[$i], [int]$want[$i])
            break
        }
    }
}

# 手改集数后文案应跟着变（EN_CHANGE 联动）
SetText $h 1034 '53'
Start-Sleep -Milliseconds 400
$rep2 = (Text $h 1039) -replace "`r`n", "`n"
$pat = "*海外转译后:*`nTest Drama`n集数: 53*"
if ($rep2 -notlike $pat) { Write-Host "---- 改后 ----"; Write-Host $rep2 }
Check ($rep2 -like $pat) '手改集数后文案自动更新'

# 截图前改回真实统计值，别让截图上是测试用的 53
SetText $h 1034 $ep2
Start-Sleep -Milliseconds 400

Shot $h $Shot

Start-Sleep -Milliseconds 200
$proc | Stop-Process -Force
Write-Host ""
if ($bad -eq 0) { Write-Host "=== 全部通过（$ok 项）==="; exit 0 }
Write-Host "=== 有失败（$bad 项，通过 $ok 项）==="
exit 1
