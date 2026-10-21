Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
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

  public const uint WM_COMMAND = 0x0111;
  public const uint WM_SETTEXT = 0x000C;
  public const uint WM_GETTEXT = 0x000D;
  public const uint LVM_GETITEMCOUNT = 0x1004;
  public const uint LVM_GETITEMTEXTW = 0x1073;
  public const uint LVM_GETHEADER = 0x101F;
  public const uint HDM_GETITEMCOUNT = 0x1200;

  public static IntPtr Find(IntPtr parent, int id) {
    IntPtr found = IntPtr.Zero;
    EnumChildWindows(parent, (c, p) => {
      if (GetDlgCtrlID(c) == id) { found = c; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  public static string Text(IntPtr h) {
    var sb = new StringBuilder(1024);
    GetWindowText(h, sb, 1024);
    return sb.ToString();
  }

  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct LVITEM {
    public uint mask; public int iItem; public int iSubItem;
    public uint state; public uint stateMask;
    public IntPtr pszText; public int cchTextMax;
    public int iImage; public IntPtr lParam;
    public int iIndent; public int iGroupId; public uint cColumns; public IntPtr puColumns;
    public IntPtr piColFmt; public int iGroup;
  }

  public static string SubItem(IntPtr lv, int item, int sub) {
    var buf = new StringBuilder(512);
    var it = new LVITEM();
    it.mask = 0x1; // LVIF_TEXT
    it.iItem = item; it.iSubItem = sub;
    it.pszText = Marshal.StringToHGlobalUni(new string('\0', 512));
    it.cchTextMax = 512;
    SendMessage(lv, LVM_GETITEMTEXTW, (IntPtr)item, ref it);
    string s = Marshal.PtrToStringUni(it.pszText);
    Marshal.FreeHGlobal(it.pszText);
    return s;
  }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, ref LVITEM l);
}
"@
[UI]::SetProcessDPIAware() | Out-Null

$exe = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\src\renamer.exe'
$work = Join-Path $env:TEMP 'rn-ui-test'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory $work | Out-Null
foreach ($n in @('随便 3.mp4','随便 1.mp4','随便 10.mp4','随便 2.mp4')) {
  Set-Content -Path (Join-Path $work $n) -Value 'x' -NoNewline
}

$proc = Start-Process -FilePath $exe -PassThru
$h = [IntPtr]::Zero
for ($i=0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 250; $proc.Refresh(); if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h=$proc.MainWindowHandle; break } }
if ($h -eq [IntPtr]::Zero) { "no window"; exit 1 }
Start-Sleep -Milliseconds 600

$eFolder = [UI]::Find($h, 1001)
$eTitle  = [UI]::Find($h, 1004)
$eStart  = [UI]::Find($h, 1005)
$ePad    = [UI]::Find($h, 1006)
$lv      = [UI]::Find($h, 1003)

"found: folder=$eFolder title=$eTitle start=$eStart pad=$ePad list=$lv"
[UI]::SendMessage($eFolder, [UI]::WM_SETTEXT, [IntPtr]::Zero, $work) | Out-Null
[UI]::SendMessage($eTitle,  [UI]::WM_SETTEXT, [IntPtr]::Zero, 'Reborn Heiress') | Out-Null
[UI]::SendMessage($eStart,  [UI]::WM_SETTEXT, [IntPtr]::Zero, '1') | Out-Null
[UI]::SendMessage($ePad,    [UI]::WM_SETTEXT, [IntPtr]::Zero, '2') | Out-Null
Start-Sleep -Milliseconds 200

# 选择标签 combo -> 第 3 项（True heiress）
$cb = [UI]::Find($h, 1008)
[UI]::SendMessage($cb, 0x014E, [IntPtr]3, [IntPtr]::Zero) | Out-Null   # CB_SETCURSEL

# 点击预览 (1011)
[UI]::PostMessage($h, [UI]::WM_COMMAND, [IntPtr]1011, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800

$cnt = [UI]::SendMessage($lv, [UI]::LVM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
"list rows = $cnt"
$hd = [UI]::SendMessage($lv, [UI]::LVM_GETHEADER, [IntPtr]::Zero, [IntPtr]::Zero)
"columns = " + [UI]::SendMessage($hd, [UI]::HDM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero)
for ($i = 0; $i -lt $cnt; $i++) {
  $a = [UI]::SubItem($lv, $i, 0)
  $b = [UI]::SubItem($lv, $i, 1)
  $c = [UI]::SubItem($lv, $i, 2)
  "  [{0}] {1} | {2} -> {3}" -f $i, $a, $b, $c
}
"status = [" + [UI]::Text([UI]::Find($h, 1017)) + "]"
"alive = " + (-not $proc.HasExited)
$proc.Refresh(); "alive2 = " + (-not $proc.HasExited)
"title-bar = [" + $proc.MainWindowTitle + "]"

$proc | Stop-Process -Force
Remove-Item $work -Recurse -Force
