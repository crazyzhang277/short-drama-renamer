Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class EW {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static List<string> Dump(IntPtr h) {
    var list = new List<string>();
    EnumChildWindows(h, (c, p) => {
      RECT r; GetWindowRect(c, out r);
      var cn = new StringBuilder(128); GetClassName(c, cn, 128);
      var tx = new StringBuilder(256); GetWindowText(c, tx, 256);
      list.Add(string.Format("{0,5} | {1,-22} | {2,4},{3,4} - {4,4},{5,4} | {6,4}x{7,-4} | {8}",
        GetDlgCtrlID(c), cn.ToString(), r.Left, r.Top, r.Right, r.Bottom,
        r.Right-r.Left, r.Bottom-r.Top, tx.ToString()));
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
"@
[EW]::SetProcessDPIAware() | Out-Null

$exe = 'C:\Users\EDY\Documents\deepseek-harness\自动更名工具\src\renamer.exe'
$proc = Start-Process -FilePath $exe -PassThru
$h = [IntPtr]::Zero
for ($i=0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 250
  $proc.Refresh()
  if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
}
if ($h -eq [IntPtr]::Zero) { "no window"; $proc | Stop-Process -Force; exit 1 }
Start-Sleep -Milliseconds 700
[EW]::Dump($h) | ForEach-Object { $_ }
$proc | Stop-Process -Force
