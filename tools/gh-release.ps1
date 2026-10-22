#requires -Version 7.0
<#
  打一个 tag + Release，并把 dist 里的单文件 exe 当附件传上去。
  用法：
    $env:GITHUB_TOKEN = 'ghp_xxx'
    pwsh -NoProfile -File tools\gh-release.ps1 -Tag v1.1.0
#>
param(
    [string]$Token   = $env:GITHUB_TOKEN,
    [string]$Repo    = 'short-drama-renamer',
    [string]$Tag     = 'v1.1.0',
    [string]$Root    = (Split-Path -Parent $PSScriptRoot),
    [string]$Asset   = 'dist\短剧更名工具.exe',
    [string]$AssetName = 'short-drama-renamer.exe',
    [string]$Notes   = '新增「统计总时长」：选中文件夹一次算出所有视频的合计时长。'
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Token)) { throw '缺少 token：设置 $env:GITHUB_TOKEN 或传 -Token' }
$Api = 'https://api.github.com'
$Hdr = @{
    Authorization          = "Bearer $Token"
    Accept                 = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
    'User-Agent'           = 'short-drama-renamer-uploader'
}

$me   = Invoke-RestMethod -Uri "$Api/user" -Headers $Hdr
$full = "$($me.login)/$Repo"
Write-Host "登录身份: $($me.login)   仓库: $full"

$head = Invoke-RestMethod -Uri "$Api/repos/$full/git/ref/heads/main" -Headers $Hdr
$sha  = $head.object.sha
Write-Host "main = $sha"

try {
    Invoke-RestMethod -Uri "$Api/repos/$full/git/refs" -Method Post -Headers $Hdr -Body (
        @{ ref = "refs/tags/$Tag"; sha = $sha } | ConvertTo-Json) | Out-Null
    Write-Host "已建 tag: $Tag"
} catch {
    Write-Host "tag $Tag 已存在，沿用"
}

$rel = Invoke-RestMethod -Uri "$Api/repos/$full/releases" -Method Post -Headers $Hdr -Body (
    @{ tag_name = $Tag; name = $Tag; body = $Notes; draft = $false; prerelease = $false } |
    ConvertTo-Json) 
Write-Host "已建 Release: $($rel.html_url)"

$file = Join-Path $Root $Asset
if (-not (Test-Path $file)) { throw "找不到附件: $file" }
$upload = "https://uploads.github.com/repos/$full/releases/$($rel.id)/assets?name=$AssetName"
$assetObj = Invoke-RestMethod -Uri $upload -Method Post -Headers $Hdr `
    -ContentType 'application/octet-stream' -InFile $file
Write-Host "已上传附件: $($assetObj.name)  $($assetObj.size) 字节"
Write-Host "下载: $($assetObj.browser_download_url)"
