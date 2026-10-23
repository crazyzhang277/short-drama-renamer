# 安装 C++ 编译环境（MinGW-w64 GCC）—— 便携解压，不需要管理员权限
# 用法： pwsh -NoProfile -File .\install-toolchain.ps1
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$base = 'https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/mingw64/'
$root = Join-Path $env:LOCALAPPDATA 'short-drama-renamer'
$pkgs = Join-Path $root '_pkgs'
New-Item -ItemType Directory -Force $root | Out-Null
New-Item -ItemType Directory -Force $pkgs | Out-Null

Write-Host "== 1/4 拉取软件包索引 =="
$html = (Invoke-WebRequest -Uri $base -UseBasicParsing -TimeoutSec 60).Content
$all  = [regex]::Matches($html, 'href="(mingw-w64-x86_64-[^"]*?\.pkg\.tar\.zst)"') |
        ForEach-Object { $_.Groups[1].Value }

function Get-VerKey([string]$ver) {
    $parts = [regex]::Matches($ver, '\d+') | ForEach-Object { $_.Value.PadLeft(10, '0') }
    return ($parts -join '.')
}

$map = @{}
foreach ($f in $all) {
    $m = [regex]::Match($f, '^(?<n>mingw-w64-x86_64-.+)-(?<v>\d[^-]*)-(?<r>\d+)-any\.pkg\.tar\.zst$')
    if (-not $m.Success) { continue }
    $n   = $m.Groups['n'].Value
    $ver = "$($m.Groups['v'].Value)-$($m.Groups['r'].Value)"
    $rec = [pscustomobject]@{ name = $n; ver = $ver; key = (Get-VerKey $ver); file = $f }
    if (-not $map.ContainsKey($n)) { $map[$n] = New-Object System.Collections.ArrayList }
    [void]$map[$n].Add($rec)
}
Write-Host ("   索引：" + $map.Count + " 个包名")

function Get-Latest([string]$name) {
    if (-not $map.ContainsKey($name)) { return $null }
    $list = @($map[$name])
    if ($list.Count -eq 0) { return $null }
    return ($list | Sort-Object -Property key | Select-Object -Last 1)
}
function Get-Exact([string]$name, [string]$ver) {
    if ($ver -and $map.ContainsKey($name)) {
        $hit = @($map[$name]) | Where-Object { $_.ver -eq $ver } | Select-Object -First 1
        if ($hit) { return $hit }
    }
    return Get-Latest $name
}

Write-Host "== 2/4 解析依赖并下载 =="
$queue = New-Object System.Collections.Queue
$resolved = @{}
$start = Get-Exact 'mingw-w64-x86_64-gcc' '15.2.0-14'
if (-not $start) { $start = Get-Latest 'mingw-w64-x86_64-gcc' }
Write-Host ("   根包：" + $start.file)
$queue.Enqueue($start)

$downloaded = @()
$missing = @()
while ($queue.Count -gt 0) {
    $p = $queue.Dequeue()
    if ($resolved.ContainsKey($p.file)) { continue }
    $resolved[$p.file] = $true

    $local = Join-Path $pkgs $p.file
    if (-not (Test-Path $local)) {
        Write-Host ("   下载 " + $p.file)
        & curl.exe -L --fail -s -o $local ($base + $p.file)
        if ($LASTEXITCODE -ne 0) { Write-Host ("   !! 失败 " + $p.file); continue }
    }
    $downloaded += $local

    $info = & tar.exe -xOf $local .PKGINFO 2>$null
    foreach ($line in $info) {
        if ($line -notmatch '^depend\s*=\s*(.+)$') { continue }
        $dep = $Matches[1].Trim()
        if ($dep -notlike 'mingw-w64-x86_64-*') { continue }
        $dn = $dep; $dv = $null
        if ($dep -match '^(?<n>.+?)(?:>=|<=|=|>|<)(?<v>\d[^\s]*)$') { $dn = $Matches['n']; $dv = $Matches['v'] }
        $hit = if ($dv) { Get-Exact $dn $dv } else { Get-Latest $dn }
        if ($hit) { $queue.Enqueue($hit) } else { $missing += $dn }
    }
}
if ($missing.Count -gt 0) { Write-Host ("   缺失依赖：" + ($missing -join ', ')) }
Write-Host ("   已下载 " + $downloaded.Count + " 个包")

Write-Host "== 3/4 解压 =="
foreach ($f in $downloaded) {
    & tar.exe -xf $f -C $root --exclude '*.md' --exclude '.BUILDINFO' --exclude '.MTREE' --exclude '.PKGINFO' --exclude '.INSTALL' 2>$null
}

Write-Host "== 4/4 验证 =="
$gpp = Join-Path $root 'mingw64\bin\g++.exe'
if (Test-Path $gpp) {
    & $gpp --version | Select-Object -First 1
    Write-Host ""
    Write-Host "编译环境就绪：$root\mingw64\bin"
    Write-Host "接下来运行：  .\build.bat"
} else {
    Write-Host "!! 安装失败：找不到 g++.exe"
    exit 1
}
