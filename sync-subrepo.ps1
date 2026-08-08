# ============================================================
# eBox 主仓库 -> 授权平台子仓库同步脚本
# ------------------------------------------------------------
# 用法（在仓库根目录执行）：
#   powershell -ExecutionPolicy Bypass -File .\sync-subrepo.ps1
#
# 作用：把主仓库 license-server/ 目录同步推送到子仓库
#       https://github.com/shushuhao01/eBox-online（main 分支）
#
# 原理：git subtree split 生成 license-server 的独立历史分支，
#       再 force push 到子仓库 main，实现"覆盖式同步"——
#       子仓库始终等于主仓库 license-server/ 的最新内容。
#
# 前置：主仓库已完成提交（license-server 已在 git 历史中）；
#       子仓库需可推送（已配置 GitHub 凭据）。
# ============================================================
param(
    [string]$Prefix = "license-server",
    [string]$SubrepoUrl = "https://github.com/shushuhao01/eBox-online.git",
    [string]$SubrepoBranch = "main",
    [string]$TmpBranch = "subrepo-sync"
)

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

# 辅助：安全删除本地临时分支（不存在时不报错）
function Remove-SyncBranch {
    param([string]$Branch)
    git rev-parse --verify "refs/heads/$Branch" 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) { git branch -D $Branch | Out-Null }
}

# ---------- 1. 检查主仓库未提交改动 ----------
Write-Host "==> 1/3 检查 license-server/ 是否有未提交改动" -ForegroundColor Cyan
$uncommitted = git status --porcelain -- $Prefix
if ($LASTEXITCODE -ne 0) { throw "git status 失败" }
if ($uncommitted) {
    Write-Host "[警告] license-server/ 有未提交改动，为避免同步旧内容请先提交主仓库：" -ForegroundColor Yellow
    Write-Host $uncommitted
    throw "请先 git add + git commit 主仓库改动，再运行本脚本"
}

# ---------- 2. subtree split ----------
Write-Host "==> 2/3 git subtree split 生成独立历史分支" -ForegroundColor Cyan
Remove-SyncBranch $TmpBranch
git subtree split --prefix=$Prefix -b $TmpBranch
if ($LASTEXITCODE -ne 0) { throw "subtree split 失败" }

# ---------- 3. force push 到子仓库 ----------
Write-Host "==> 3/3 推送子仓库 $SubrepoUrl ($SubrepoBranch)" -ForegroundColor Cyan
try {
    git push $SubrepoUrl "${TmpBranch}:${SubrepoBranch}" --force
    if ($LASTEXITCODE -ne 0) { throw "推送失败" }
} finally {
    Remove-SyncBranch $TmpBranch
}

Write-Host ""
Write-Host "✅ 同步完成：主仓库 license-server/ -> $SubrepoUrl ($SubrepoBranch)" -ForegroundColor Green
Write-Host "   服务器更新：cd /www/wwwroot/license-server && bash update.sh" -ForegroundColor DarkGray
