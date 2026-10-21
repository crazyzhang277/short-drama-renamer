#requires -Version 7.0
<#
  用 GitHub REST API 上传本目录到仓库，不需要本机装 git。
  用法：
    $env:GITHUB_TOKEN = 'ghp_xxx'
    pwsh -NoProfile -File tools\gh-upload.ps1 -Repo short-drama-renamer -Visibility public
#>
param(
    [string]$Token   = $env:GITHUB_TOKEN,
    [string]$Repo    = 'short-drama-renamer',
    [ValidateSet('public','private')][string]$Visibility = 'public',
    [string]$Root    = (Split-Path -Parent $PSScriptRoot),
    [string]$Branch  = 'main',
    [string]$Message = '短剧视频批量更名工具 v1.0（C++ Win32 桌面版）'
)

$ErrorActionPreference = 'Stop'
$Api = 'https://api.github.com'
$Hdr = @{
    Authorization          = "Bearer $Token"
    Accept                 = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
    'User-Agent'           = 'short-drama-renamer-uploader'
}

function Invoke-Api {
    param([string]$Method, [string]$Path, $Body)
    $req = @{ Uri = "$Api$Path"; Method = $Method; Headers = $Hdr }
    if ($null -ne $Body) {
        $json = $Body | ConvertTo-Json -Depth 20 -Compress
        $req.Body        = [Text.Encoding]::UTF8.GetBytes($json)
        $req.ContentType = 'application/json; charset=utf-8'
    }
    Invoke-RestMethod @req
}

if ([string]::IsNullOrWhiteSpace($Token)) { throw '缺少 token：设置 $env:GITHUB_TOKEN 或传 -Token' }

# ---------------------------------------------------------------- 1. 验证身份
$me = Invoke-Api GET '/user'
Write-Host "登录身份: $($me.login)"

# ---------------------------------------------------------------- 2. 建仓库
$created = $false
try {
    $body = @{
        name        = $Repo
        private     = ($Visibility -eq 'private')
        auto_init   = $false
        description = 'Batch renamer for short-drama videos — native Win32 GUI, single exe'
        has_issues  = $true
        has_wiki    = $false
    }
    $repoObj = Invoke-Api POST '/user/repos' $body
    $created = $true
    Write-Host "已创建仓库: $($repoObj.full_name)  ($Visibility)"
} catch {
    $code = $_.Exception.Response.StatusCode.value__
    if ($code -eq 422) {
        Write-Host "仓库已存在，改为向现有仓库推送: $($me.login)/$Repo"
        $repoObj = Invoke-Api GET "/repos/$($me.login)/$Repo"
    } else {
        throw
    }
}

$owner = $repoObj.owner.login
$full  = "$owner/$Repo"

# ---------------------------------------------------------------- 2.5 空仓库先垫一个初始提交
# GitHub 的 git/blobs 接口在空仓库上会返回 409 Git Repository is empty，
# 所以先用 contents 接口写入一个占位文件，把 main 分支建出来。
$needBoot = $false
try {
    Invoke-Api GET "/repos/$full/git/ref/heads/$Branch" | Out-Null
} catch {
    $needBoot = $true
}
if ($needBoot) {
    $seed = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("# $Repo`n"))
    Invoke-Api PUT "/repos/$full/contents/.bootstrap" @{
        message = 'chore: bootstrap'
        content = $seed
        branch  = $Branch
    } | Out-Null
    Write-Host "已垫初始提交，main 分支就绪"
}

# ---------------------------------------------------------------- 3. 收集文件
$skipDir  = @('.git', '.rename-tool', '_demo', '_gap', '_dup', 'node_modules')
$skipFile = @('settings.ini', '_page-draft.md', 'Thumbs.db', 'desktop.ini', '.DS_Store')

$files = Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object {
    $rel = $_.FullName.Substring($Root.Length).TrimStart('\', '/')
    $parts = $rel -split '[\\/]'
    if ($parts | Where-Object { $skipDir -contains $_ }) { return $false }
    if ($skipFile -contains $_.Name) { return $false }
    # .gitignore 里排除的编译中间产物
    if ($rel -eq 'src\renamer.exe' -or $rel -eq 'src/renamer.exe') { return $false }
    if ($rel -match '^src[\\/](app\.res|selftest\.log)$') { return $false }
    if ($_.Extension -eq '.o') { return $false }
    return $true
}

Write-Host "待上传 $($files.Count) 个文件"

# ---------------------------------------------------------------- 4. 逐个建 blob
$tree = @()
$i = 0
foreach ($f in $files) {
    $i++
    $rel  = $f.FullName.Substring($Root.Length).TrimStart('\', '/') -replace '\\', '/'
    $b64  = [Convert]::ToBase64String([IO.File]::ReadAllBytes($f.FullName))
    $blob = Invoke-Api POST "/repos/$full/git/blobs" @{ content = $b64; encoding = 'base64' }
    $tree += @{ path = $rel; mode = '100644'; type = 'blob'; sha = $blob.sha }
    Write-Host ("  [{0}/{1}] {2}  ({3:N0} B)" -f $i, $files.Count, $rel, $f.Length)
}

# ---------------------------------------------------------------- 5. 建 tree
$treeObj = Invoke-Api POST "/repos/$full/git/trees" @{ tree = $tree }
Write-Host "tree: $($treeObj.sha)"

# ---------------------------------------------------------------- 6. 建 commit
$parent = $null
try {
    $refObj = Invoke-Api GET "/repos/$full/git/ref/heads/$Branch"
    $parent = $refObj.object.sha
} catch { }

$commitBody = @{ message = $Message; tree = $treeObj.sha }
if ($parent) { $commitBody.parents = @($parent) }
$commit = Invoke-Api POST "/repos/$full/git/commits" $commitBody
Write-Host "commit: $($commit.sha)"

# ---------------------------------------------------------------- 7. 建/更新 ref
if ($parent) {
    Invoke-Api PATCH "/repos/$full/git/refs/heads/$Branch" @{ sha = $commit.sha; force = $true } | Out-Null
} else {
    Invoke-Api POST "/repos/$full/git/refs" @{ ref = "refs/heads/$Branch"; sha = $commit.sha } | Out-Null
}

# ---------------------------------------------------------------- 8. 设默认分支
Invoke-Api PATCH "/repos/$full" @{ default_branch = $Branch } | Out-Null

Write-Host ''
Write-Host "完成: https://github.com/$full"
