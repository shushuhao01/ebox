# diag-env-persist.ps1 - 只读取证脚本：判断企业微信登录态是否真的写进了 eBox 环境
#
# 背景：环境 hive 已确认加载、日志无报错，但重启电脑后仍要求扫码。
# 本脚本不改动任何数据，只收集"环境目录 / 宿主机目录 / 环境注册表 / 环境日志"四类证据，
# 用来回答两个问题：
#   1) 企业微信运行期的写入（Global\Config.cfg 等登录态文件）到底落在环境目录里，
#      还是落在了宿主机真实目录（说明文件重定向没生效）；
#   2) 环境 hive / 用户注册表里到底有没有企业微信的数据。
#
# 用法（在【故障机】上运行，采集更完整可右键"以管理员身份运行"）：
#   powershell -ExecutionPolicy Bypass -File diag-env-persist.ps1 -EnvIndex 13 -Label after-reboot
# 默认环境数据根目录为 F:\eBoxData\Env（可用 -EnvDataRoot 覆盖）。
#
# 建议采集三次并对比：after-login（登录成功但还没重启）/ after-reboot（重启后、还没启动环境）/
# after-rescan（启动环境并重新扫码之后）。

param(
    [int]$EnvIndex = 13,
    [string]$EnvRoot = '',
    [string]$EnvDataRoot = 'F:\eBoxData\Env',
    [string]$OutFile = '',
    # 采集时刻标记：建议 after-login / after-reboot / after-rescan，便于对比三次输出
    [string]$Label = ''
)

$ErrorActionPreference = 'Continue'

# 规范化路径：EnvDataRoot 指 ...\Env 这一层（日志在 <EnvDataRoot>\data\logs，环境目录是 <EnvDataRoot>\<索引>）
if (-not $PSBoundParameters.ContainsKey('EnvDataRoot') -and -not [string]::IsNullOrWhiteSpace($EnvRoot)) {
    $EnvDataRoot = Split-Path -Parent $EnvRoot
}
# 容错：用户可能只给到 F:\eBoxData，其下才是 Env，自动下钻一层
if (-not (Test-Path -LiteralPath (Join-Path $EnvDataRoot 'data'))) {
    $maybeEnv = Join-Path $EnvDataRoot 'Env'
    if (Test-Path -LiteralPath (Join-Path $maybeEnv 'data')) {
        $EnvDataRoot = $maybeEnv
    }
}
if ([string]::IsNullOrWhiteSpace($EnvRoot)) {
    $EnvRoot = Join-Path $EnvDataRoot "$EnvIndex"
}
if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $tag = if ([string]::IsNullOrWhiteSpace($Label)) { 'snap' } else { $Label }
    $OutFile = Join-Path ([Environment]::GetFolderPath('Desktop')) ("ebox-env{0}-persist-{1}-{2}.txt" -f $EnvIndex, $tag, (Get-Date -Format 'yyyyMMdd-HHmmss'))
}

$script:Lines = New-Object System.Collections.Generic.List[string]

function Say([string]$text) {
    $script:Lines.Add($text) | Out-Null
    Write-Host $text
}

function Get-DirStat([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $files = Get-ChildItem -LiteralPath $path -Recurse -File -Force -ErrorAction SilentlyContinue
    if (-not $files) {
        return [pscustomobject]@{ Count = 0; Bytes = 0; Newest = $null; NewestName = '' }
    }
    $sum = ($files | Measure-Object -Property Length -Sum).Sum
    $newest = $files | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    return [pscustomobject]@{ Count = @($files).Count; Bytes = $sum; Newest = $newest.LastWriteTime; NewestName = $newest.FullName }
}

function Format-Stat($stat) {
    if ($null -eq $stat) { return '(not found)' }
    if ($stat.Count -eq 0) { return 'empty dir' }
    return ('{0} files, {1:N2} MB, newest={2:yyyy-MM-dd HH:mm:ss} {3}' -f $stat.Count, ($stat.Bytes / 1MB), $stat.Newest, $stat.NewestName)
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

Say '================================================================'
Say ' eBox environment persistence evidence (read-only)'
Say '================================================================'
Say ("time        : {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
Say ("computer    : {0}" -f $env:COMPUTERNAME)
Say ("user        : {0}" -f $env:USERNAME)
Say ("isAdmin     : {0}" -f $isAdmin)
Say ("envIndex    : {0}" -f $EnvIndex)
Say ("envDataRoot : {0}" -f $EnvDataRoot)
Say ("envRoot     : {0}" -f $EnvRoot)
Say ("label       : {0}" -f $(if ([string]::IsNullOrWhiteSpace($Label)) { '(none)' } else { $Label }))
Say ''

# ---------------------------------------------------------------- [1] env root
Say '--- [1] ENV ROOT -------------------------------------------------'
if (-not (Test-Path -LiteralPath $EnvRoot)) {
    Say 'ENV_ROOT_NOT_FOUND'
    # 目录不存在时把实际存在的环境列出来，避免在错的机器/错的编号上白跑一次
    $envParent = $EnvDataRoot
    if (Test-Path -LiteralPath $envParent) {
        Say ("  existing env dirs under {0}:" -f $envParent)
        Get-ChildItem -LiteralPath $envParent -Directory -Force -ErrorAction SilentlyContinue |
            ForEach-Object { Say ("    {0}" -f $_.Name) }
    } else {
        Say ("  {0} not found either - wrong env data root?" -f $envParent)
    }
} else {
    Say ("env total   : {0}" -f (Format-Stat (Get-DirStat $EnvRoot)))
    Say 'top-level:'
    Get-ChildItem -LiteralPath $EnvRoot -Force -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.PSIsContainer) {
            Say ("  [dir ] {0,-28} {1}" -f $_.Name, (Format-Stat (Get-DirStat $_.FullName)))
        } else {
            Say ("  [file] {0,-28} {1,10} bytes  mtime={2:yyyy-MM-dd HH:mm:ss}" -f $_.Name, $_.Length, $_.LastWriteTime)
        }
    }
}
Say ''

# ------------------------------------------- [1b] what WXWork wrote inside env
Say '--- [1b] WXWORK WRITES INSIDE ENV (newest 25 files) --------------'
if (Test-Path -LiteralPath $EnvRoot) {
    $recent = @(Get-ChildItem -LiteralPath $EnvRoot -File -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'WXWork|Tencent' } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 25)
    if ($recent.Count -eq 0) {
        Say '  NO_WXWORK_FILES_INSIDE_ENV'
    } else {
        foreach ($f in $recent) {
            Say ("  {0:yyyy-MM-dd HH:mm:ss} {1,10} bytes  {2}" -f $f.LastWriteTime, $f.Length, $f.FullName)
        }
    }
} else {
    Say '  ENV_ROOT_NOT_FOUND'
}
Say ''

# ------------------------------------------------------- [2] WXWork in env
Say '--- [2] WXWork DIRECTORIES INSIDE ENV ----------------------------'
$wxDirs = @()
if (Test-Path -LiteralPath $EnvRoot) {
    $wxDirs = @(Get-ChildItem -LiteralPath $EnvRoot -Directory -Recurse -Depth 6 -Filter 'WXWork' -Force -ErrorAction SilentlyContinue)
}
if ($wxDirs.Count -eq 0) {
    Say 'NO_WXWORK_DIR_INSIDE_ENV  (redirect of Documents/AppData may be broken)'
} else {
    foreach ($d in $wxDirs) {
        Say ("  {0}" -f $d.FullName)
        Say ("    {0}" -f (Format-Stat (Get-DirStat $d.FullName)))
        foreach ($sub in @('Global', 'Profiles', 'Default')) {
            $p = Join-Path $d.FullName $sub
            if (Test-Path -LiteralPath $p) { Say ("      {0,-10} {1}" -f $sub, (Format-Stat (Get-DirStat $p))) }
        }
    }
}
Say ''

# --------------------------------------------------- [3] key state files
Say '--- [3] KEY STATE FILES INSIDE ENV -------------------------------'
$keyNames = @('Config.cfg', 'Local State')
foreach ($n in $keyNames) {
    $hits = @()
    if (Test-Path -LiteralPath $EnvRoot) {
        $hits = @(Get-ChildItem -LiteralPath $EnvRoot -File -Recurse -Depth 8 -Force -Filter $n -ErrorAction SilentlyContinue)
    }
    if ($hits.Count -eq 0) {
        Say ("  {0,-14} : NOT_FOUND" -f $n)
    } else {
        foreach ($f in $hits) {
            Say ("  {0,-14} : {1,9} bytes  mtime={2:yyyy-MM-dd HH:mm:ss}  {3}" -f $n, $f.Length, $f.LastWriteTime, $f.FullName)
            if ($n -eq 'Local State') {
                try {
                    $txt = Get-Content -LiteralPath $f.FullName -Raw -ErrorAction Stop
                    if ($txt -match '"machine_id"\s*:\s*"([0-9a-fA-F]+)"') { Say ("    machine_id  = {0}" -f $Matches[1]) }
                } catch { }
            }
        }
    }
}
Say ''

# ------------------------------------------------------------ [4] host side
Say '--- [4] HOST REAL DIRECTORIES (should NOT grow while env runs) ---'
$docs = [Environment]::GetFolderPath('MyDocuments')
Say ("  MyDocuments : {0}" -f $docs)
$hostWx = Join-Path $docs 'WXWork'
if (Test-Path -LiteralPath $hostWx) {
    Say ("  host WXWork : {0}" -f (Format-Stat (Get-DirStat $hostWx)))
    $cfg = Join-Path $hostWx 'Global\Config.cfg'
    if (Test-Path -LiteralPath $cfg) {
        $i = Get-Item -LiteralPath $cfg
        Say ("  Config.cfg  : {0,9} bytes  mtime={1:yyyy-MM-dd HH:mm:ss}" -f $i.Length, $i.LastWriteTime)
    } else {
        Say '  Config.cfg  : NOT_FOUND'
    }
    Get-ChildItem -LiteralPath $hostWx -File -Recurse -Depth 1 -Force -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 5 | ForEach-Object {
            Say ("    recent: {0:yyyy-MM-dd HH:mm:ss} {1,9} bytes {2}" -f $_.LastWriteTime, $_.Length, $_.FullName)
        }
} else {
    Say '  host WXWork : NOT_FOUND'
}
$roamingWx = Join-Path $env:APPDATA 'Tencent\WXWork'
if (Test-Path -LiteralPath $roamingWx) {
    Say ("  APPDATA WXWork : {0}" -f (Format-Stat (Get-DirStat $roamingWx)))
    Get-ChildItem -LiteralPath $roamingWx -File -Recurse -Depth 2 -Force -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 5 | ForEach-Object {
            Say ("    recent: {0:yyyy-MM-dd HH:mm:ss} {1,9} bytes {2}" -f $_.LastWriteTime, $_.Length, $_.FullName)
        }
} else {
    Say '  APPDATA WXWork : NOT_FOUND'
}
Say ''

# ------------------------------------------- [4b] host WXWork data roots
Say '--- [4b] HOST WXWORK DATA ROOTS (where Global\Config.cfg lives) ----'
$roots = @(
    (Join-Path $docs 'WXWork'),
    (Join-Path $env:APPDATA 'Tencent\WXWork'),
    (Join-Path $env:LOCALAPPDATA 'Tencent\WXWork'),
    (Join-Path $env:ProgramData 'Tencent\WXWork')
)
foreach ($r in $roots) {
    $cfg = Join-Path $r 'Global\Config.cfg'
    if (Test-Path -LiteralPath $cfg) {
        $i = Get-Item -LiteralPath $cfg -Force
        Say ("  FOUND    {0}  ({1} bytes, mtime={2:yyyy-MM-dd HH:mm:ss})" -f $cfg, $i.Length, $i.LastWriteTime)
    } elseif (Test-Path -LiteralPath $r) {
        Say ("  no cfg   {0}" -f $r)
    } else {
        Say ("  absent   {0}" -f $r)
    }
}
Say ''

# ------------------------------------------- [4c] host WXWork registry
Say '--- [4c] HOST WXWORK REGISTRY (HKCU\Software\Tencent\WXWork) -------'
if (Test-Path -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\Tencent\WXWork') {
    (Get-Item -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\Tencent\WXWork').GetValueNames() |
        ForEach-Object { Say ("  value {0} = {1}" -f $_, (Get-ItemPropertyValue -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\Tencent\WXWork' -Name $_)) }
    if (Test-Path -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\Tencent\WXWork\GUID') {
        Say '  GUID subkey:'
        (Get-Item -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\Tencent\WXWork\GUID').GetValueNames() |
            ForEach-Object { Say ("    {0} = {1}" -f $_, (Get-ItemPropertyValue -LiteralPath 'Registry::HKEY_CURRENT_USER\Software\Tencent\WXWork\GUID' -Name $_)) }
    } else {
        Say '  NOT_FOUND: no WXWork\GUID (device id used for auto-login)'
    }
} else {
    Say '  NOT_FOUND: host has no HKCU\Software\Tencent\WXWork'
}
Say ''

# --------------------------------------------------------------- [5] hive files
Say '--- [5] REGISTRY HIVE FILES IN ENV ROOT --------------------------'
if (Test-Path -LiteralPath $EnvRoot) {
    Get-ChildItem -LiteralPath $EnvRoot -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^[0-9A-Fa-f]{16}$' -or $_.Name -like '*.tmp' -or $_.Name -like '*.tmp.new' -or $_.Name -like '*.bad.*' } |
        ForEach-Object {
            Say ("  {0,-32} {1,9} bytes  mtime={2:yyyy-MM-dd HH:mm:ss}" -f $_.Name, $_.Length, $_.LastWriteTime)
        }
}
Say ''

# ---------------------------------------------------------- [6] registry
Say '--- [6] REGISTRY STORES ------------------------------------------'
$hiveSub = "eBox_Env_$EnvIndex"
$hiveKey = "Registry::HKEY_USERS\$hiveSub"
if (Test-Path -LiteralPath $hiveKey) {
    Say ("  HKU\{0} : LOADED" -f $hiveSub)
    foreach ($probe in @('Software\Tencent\WXWork', 'Software\Tencent')) {
        $p = "$hiveKey\$probe"
        if (Test-Path -LiteralPath $p) {
            $sub = @(Get-ChildItem -LiteralPath $p -Recurse -ErrorAction SilentlyContinue).Count
            $vals = (Get-Item -LiteralPath $p).ValueCount
            Say ("    {0} : values={1} subkeys={2}" -f $probe, $vals, $sub)
        } else {
            Say ("    {0} : NOT_FOUND" -f $probe)
        }
    }
} else {
    Say ("  HKU\{0} : NOT_LOADED (normal when eBox is not running)" -f $hiveSub)
}
$userStore = "Registry::HKEY_CURRENT_USER\Software\eBox\EnvRegistry\$EnvIndex"
if (Test-Path -LiteralPath $userStore) {
    $sub = @(Get-ChildItem -LiteralPath $userStore -Recurse -ErrorAction SilentlyContinue).Count
    Say ("  HKCU\Software\eBox\EnvRegistry\{0} : EXISTS (user-store mode), subkeys={1}" -f $EnvIndex, $sub)
} else {
    Say ("  HKCU\Software\eBox\EnvRegistry\{0} : not present" -f $EnvIndex)
}
Say ''

# --------------------------------------------------------------- [7] env log
Say '--- [7] ENV LOG (registry / error lines, last 60) ----------------'
$logPath = Join-Path $EnvDataRoot "data\logs\env_$EnvIndex.log"
Say ("  log file : {0}" -f $logPath)
if (Test-Path -LiteralPath $logPath) {
    # 注意：变量名不能叫 $lines，PowerShell 变量名大小写不敏感，会覆盖收集用的 $script:Lines
    $logHits = @(Get-Content -LiteralPath $logPath -Encoding UTF8 -ErrorAction SilentlyContinue |
        Where-Object { $_ -match '注册表|错误|失败|hive' } | Select-Object -Last 60)
    if ($logHits.Count -eq 0) { Say '  (no registry/error lines)' } else { $logHits | ForEach-Object { Say ("  {0}" -f $_) } }
} else {
    Say '  LOG_NOT_FOUND'
}
Say ''
Say '================================================================'
Say 'DONE. Please send this txt file back.'
Say ("saved to: {0}" -f $OutFile)

try {
    $script:Lines | Out-File -LiteralPath $OutFile -Encoding UTF8
    Write-Host ("`n[saved] {0}" -f $OutFile)
} catch {
    Write-Host ("`n[ERROR] cannot write {0}: {1}" -f $OutFile, $_.Exception.Message)
}
