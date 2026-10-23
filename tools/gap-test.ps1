param(
    [string]$Exe = (Join-Path (Split-Path -Parent $PSScriptRoot) '短剧更名工具.exe'),
    [string]$Demo = (Join-Path (Split-Path -Parent $PSScriptRoot) '_gap\Test Drama')
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class G {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr SendMessageStr(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[G]::SetProcessDPIAware() | Out-Null

function MakeDemo {
    if (Test-Path $Demo) { Remove-Item $Demo -Recurse -Force }
    New-Item -ItemType Directory -Path $Demo -Force | Out-Null
    foreach ($n in @(1,2,3,8,9,10,11)) {
        [System.IO.File]::WriteAllBytes((Join-Path $Demo "$n.mp4"), [byte[]](0..255))
    }
}

function RunCase($keepEp, $label) {
    MakeDemo
    Write-Host "`n=== $label ===" -ForegroundColor Cyan
    Write-Host "原始文件名: " + ((Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object { $_.BaseName }) -join ', ')

    $proc = Start-Process -FilePath $Exe -PassThru
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        $proc.Refresh()
        if ($proc.HasExited) { Write-Host "exited $($proc.ExitCode)"; return }
        if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $h = $proc.MainWindowHandle; break }
    }
    Start-Sleep -Milliseconds 500

    [G]::SendMessageStr([G]::GetDlgItem($h,1001), 0x000C, [IntPtr]::Zero, $Demo) | Out-Null
    [G]::SendMessageStr([G]::GetDlgItem($h,1004), 0x000C, [IntPtr]::Zero, 'Test Drama') | Out-Null
    [G]::SendMessageStr([G]::GetDlgItem($h,1005), 0x000C, [IntPtr]::Zero, '1') | Out-Null
    [G]::SendMessageStr([G]::GetDlgItem($h,1006), 0x000C, [IntPtr]::Zero, '2') | Out-Null

    # 复选框状态 (IDC_CHECK_KEEPEP = 1020)
    $ck = [G]::GetDlgItem($h, 1020)
    [G]::SendMessage($ck, 0x00F1, [IntPtr]$(if ($keepEp) {1} else {0}), [IntPtr]::Zero) | Out-Null
    [G]::SendMessage($h, 0x0111, [IntPtr](1020 -bor (0 -shl 16)), $ck) | Out-Null
    Start-Sleep -Milliseconds 600

    # 选标签
    $tag = [G]::GetDlgItem($h, 1008)
    [G]::SendMessage($tag, 0x014E, [IntPtr]0, [IntPtr]::Zero) | Out-Null
    [G]::SendMessage($h, 0x0111, [IntPtr](1008 -bor (1 -shl 16)), $tag) | Out-Null
    Start-Sleep -Milliseconds 900

    [G]::SendMessage($h, 0x0111, [IntPtr]1012, [G]::GetDlgItem($h,1012)) | Out-Null
    Start-Sleep -Milliseconds 1200
    for ($k = 0; $k -lt 4; $k++) {
        $d = [G]::FindWindowW('#32770', $null)
        if ($d -eq [IntPtr]::Zero) { break }
        [G]::PostMessage($d, 0x0111, [IntPtr]6, [G]::GetDlgItem($d,6)) | Out-Null
        Start-Sleep -Milliseconds 500
    }
    Start-Sleep -Milliseconds 900

    Get-ChildItem $Demo -File | Sort-Object Name | ForEach-Object {
        Write-Host ("  " + ($_.Name -replace ' #short drama #mini drama #micro drama #Billionaire boss', ''))
    }
    $proc | Stop-Process -Force
    Start-Sleep -Milliseconds 400
}

RunCase $false '不勾「沿用原集数」（默认）'
RunCase $true  '勾上「沿用原集数」'
