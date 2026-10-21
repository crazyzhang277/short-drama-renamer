Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class UI {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string n);

  public static IntPtr Find(IntPtr parent, int id) {
    IntPtr found = IntPtr.Zero;
    EnumChildWindows(parent, (c, p) => {
      if (GetDlgCtrlID(c) == id) { found = c; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public static string Text(IntPtr h) {
    var sb = new StringBuilder(2048);
    GetWindowText(h, sb, 2048);
    return sb.ToString();
  }
}
"@
[UI]::SetProcessDPIAware() | Out-Null

$exe  = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\短剧更名工具.exe'
$work = Join-Path $env:TEMP 'rn-e2e'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
foreach ($n in @('随便 3.mp4','随便 1.mp4','随便 10.mp4','随便 2.mp4','说明.txt')) {
  Set-Content -Path (Join-Path $work $n) -Value 'x' -NoNewline
}

function Wait-Dialog([string]$title, [int]$ms = 8000) {
  $deadline = (Get-Date).AddMilliseconds($ms)
  while ((Get-Date) -lt $deadline) {
    $d = [UI]::FindWindow('#32770', $title)
    if ($d -ne [IntPtr]::Zero) { return $d }
    Start-Sleep -Milliseconds 120
  }
  return [IntPtr]::Zero
}

$proc = Start-Process -FilePath $exe -PassThru
$h = [IntPtr]::Zero
for ($i=0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 250; $proc.Refresh(); if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h=$proc.MainWindowHandle; break } }
if ($h -eq [IntPtr]::Zero) { "no window"; exit 1 }
Start-Sleep -Milliseconds 600

[UI]::SendMessage([UI]::Find($h,1001), 0x000C, [IntPtr]::Zero, $work) | Out-Null
[UI]::SendMessage([UI]::Find($h,1004), 0x000C, [IntPtr]::Zero, 'Reborn Heiress') | Out-Null
[UI]::SendMessage([UI]::Find($h,1005), 0x000C, [IntPtr]::Zero, '1') | Out-Null
[UI]::SendMessage([UI]::Find($h,1006), 0x000C, [IntPtr]::Zero, '2') | Out-Null
[UI]::SendMessage([UI]::Find($h,1008), 0x014E, [IntPtr]3, [IntPtr]::Zero) | Out-Null   # 标签 = True heiress
Start-Sleep -Milliseconds 200

# --- 预览
[UI]::PostMessage($h, 0x0111, [IntPtr]1011, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
"status after preview = [" + [UI]::Text([UI]::Find($h,1017)) + "]"
"alive = " + (-not $proc.HasExited)

# --- 执行改名
[UI]::PostMessage($h, 0x0111, [IntPtr]1012, [IntPtr]::Zero) | Out-Null
$d = Wait-Dialog '确认改名'
"confirm dialog = $d"
if ($d -ne [IntPtr]::Zero) { [UI]::PostMessage($d, 0x0111, [IntPtr]6, [IntPtr]::Zero) | Out-Null }  # IDYES
$d2 = Wait-Dialog '成功'
"success dialog = $d2"
if ($d2 -ne [IntPtr]::Zero) { [UI]::PostMessage($d2, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null }  # IDOK
Start-Sleep -Milliseconds 700

"--- 改名后目录 ---"
Get-ChildItem $work -File | Sort-Object Name | ForEach-Object { "  " + $_.Name }
"--- .rename-tool ---"
Get-ChildItem (Join-Path $work '.rename-tool') -File -ErrorAction SilentlyContinue | ForEach-Object { "  " + $_.Name }
$undo = Get-ChildItem (Join-Path $work '.rename-tool') -Filter 'undo-*.txt' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($undo) { "--- undo 内容 ---"; Get-Content -Encoding UTF8 $undo.FullName | ForEach-Object { "  " + $_ } }

# --- 撤销
[UI]::PostMessage($h, 0x0111, [IntPtr]1013, [IntPtr]::Zero) | Out-Null
$u = Wait-Dialog '确认撤销'
"undo confirm = $u"
if ($u -ne [IntPtr]::Zero) { [UI]::PostMessage($u, 0x0111, [IntPtr]6, [IntPtr]::Zero) | Out-Null }
$u2 = Wait-Dialog '撤销'
"undo result = $u2"
if ($u2 -ne [IntPtr]::Zero) { [UI]::PostMessage($u2, 0x0111, [IntPtr]1, [IntPtr]::Zero) | Out-Null }
Start-Sleep -Milliseconds 700

"--- 撤销后目录 ---"
Get-ChildItem $work -File | Sort-Object Name | ForEach-Object { "  " + $_.Name }

$proc | Stop-Process -Force
Remove-Item $work -Recurse -Force
