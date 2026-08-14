@echo off
rem ============================================================
rem  All-Drive Junk Cleaner (全盘垃圾清理工具 v2.1)
rem  Auto-detect all fixed drives and clean them all.
rem  Compatible: Windows 7 / 8 / 8.1 / 10 / 11 (PowerShell 2.0+)
rem  This file must be saved as UTF-8 (no BOM).
rem ============================================================
chcp 65001 >nul 2>&1
setlocal
set "SELF=%~f0"
rem ============================================================
rem  eBox 集成: 传入 /auto 时跳过回车确认(由 eBox 应用内确认框替代)
rem ============================================================
if /i "%~1"=="/auto" set "EBOX_AUTO=1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$b=[IO.File]::ReadAllBytes($env:SELF);$u=New-Object System.Text.UTF8Encoding($false,$true);try{$c=$u.GetString($b)}catch{$c=[Text.Encoding]::GetEncoding(936).GetString($b)};$m=([string][char]35)+'PSCODE';$i=$c.IndexOf($m);if($i -lt 0){$i=$c.IndexOf(([string][char]35)+'P'+'SCODE')};$code=$c.Substring($i);$tmp=[IO.Path]::GetTempFileName()+'.ps1';[IO.File]::WriteAllText($tmp,$code,(New-Object System.Text.UTF8Encoding($true)));try{& $tmp}finally{Remove-Item $tmp -Force -ErrorAction SilentlyContinue}"
if errorlevel 1 pause
endlocal
exit /b

#PSCODE
# ==================== 以下为 PowerShell 主程序(由上方批处理引导执行) ====================
$ErrorActionPreference = 'Continue'
try { $Host.UI.RawUI.WindowTitle = '全盘垃圾清理工具 v2.0' } catch {}

# ---------------- 工具函数 (全部兼容 PowerShell 2.0) ----------------
function Format-Size {
    param([double]$b)
    if ($b -ge 1GB) { return ('{0:N2} GB' -f ($b / 1GB)) }
    if ($b -ge 1MB) { return ('{0:N1} MB' -f ($b / 1MB)) }
    if ($b -ge 1KB) { return ('{0:N0} KB' -f ($b / 1KB)) }
    return ('{0:N0} B' -f $b)
}

function Pad-Cjk {
    param([string]$s, [int]$w)
    if (-not $s) { $s = '' }
    $len = 0
    foreach ($ch in $s.ToCharArray()) { if ([int]$ch -gt 255) { $len += 2 } else { $len += 1 } }
    if ($len -ge $w) { return $s }
    return ($s + (' ' * ($w - $len)))
}

function New-Obj {
    # 替代 PS3.0+ 的 [pscustomobject],兼容 PS2.0
    param([hashtable]$Props)
    $o = New-Object PSObject
    foreach ($k in $Props.Keys) { $o | Add-Member -MemberType NoteProperty -Name $k -Value $Props[$k] }
    return $o
}

function Test-IsUnder {
    # 判断 Path 是否等于或位于 Bases 中任一目录之下(不区分大小写),用于跨运行空间并行时的 eBox 环境保护
    param([string]$Path, [string[]]$Bases)
    if (-not $Path) { return $false }
    foreach ($b in $Bases) {
        if (-not $b) { continue }
        if ($Path -ieq $b) { return $true }
        if ($Path.StartsWith(($b + '\'), [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Invoke-Parallel {
    # PS2.0 兼容的多线程并行执行器(基于 RunspacePool)。
    # 将 FunctionNames 中列出的函数注入工作运行空间后,对每个 InputObject 并发调用 ScriptBlock。
    # 结果按输入顺序返回;可选 -ProgressActivity 在每个任务完成后更新主线程进度条(短文本,避免重叠)。
    param(
        [object[]]$InputObjects,
        [scriptblock]$ScriptBlock,
        [int]$Throttle = 4,
        [string]$ProgressActivity = '',
        [string[]]$FunctionNames = @()
    )
    $list = @($InputObjects)
    if ($list.Count -eq 0) { return @() }
    $iss = [System.Management.Automation.Runspaces.InitialSessionState]::CreateDefault()
    foreach ($fn in $FunctionNames) {
        $cmd = Get-Command $fn -ErrorAction SilentlyContinue
        if ($null -ne $cmd) {
            $sb = [scriptblock]::Create($cmd.Definition)
            $fe = New-Object System.Management.Automation.Runspaces.SessionStateFunctionEntry($fn, $sb)
            $iss.Commands.Add($fe)
        }
    }
    $poolSize = $Throttle
    if ($poolSize -lt 1) { $poolSize = 1 }
    if ($poolSize -gt $list.Count) { $poolSize = $list.Count }
    $pool = [RunspaceFactory]::CreateRunspacePool(1, $poolSize, $iss, $Host)
    try {
        $pool.Open()
        $jobs = @()
        foreach ($obj in $list) {
            $ps = [PowerShell]::Create()
            $ps.RunspacePool = $pool
            [void]$ps.AddScript($ScriptBlock.ToString())
            [void]$ps.AddArgument($obj)
            $jobs += ,@($ps, $ps.BeginInvoke())
        }
        $results = New-Object System.Collections.ArrayList
        $done = 0
        foreach ($j in $jobs) {
            $out = $j[0].EndInvoke($j[1])
            $done++
            if ($ProgressActivity) {
                $pct = [int](100 * $done / $jobs.Count)
                Write-Progress -Activity $ProgressActivity -Status ('{0}/{1}' -f $done, $jobs.Count) -PercentComplete $pct
            }
            foreach ($o in $out) { [void]$results.Add($o) }
            $j[0].Dispose()
        }
        if ($ProgressActivity) { Write-Progress -Activity $ProgressActivity -Completed }
        return ,$results.ToArray()
    } finally {
        $pool.Close()
    }
}

function Get-DirSize {
    param([string]$p)
    $sum = [double]0
    $stack = New-Object System.Collections.Stack
    $stack.Push($p)
    while ($stack.Count -gt 0) {
        $d = [string]$stack.Pop()
        $di = $null
        try { $di = New-Object System.IO.DirectoryInfo($d) } catch { continue }
        try {
            foreach ($s in $di.GetDirectories()) {
                try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
                $stack.Push($s.FullName)
            }
        } catch {}
        try { foreach ($f in $di.GetFiles()) { $sum += $f.Length } } catch {}
    }
    return $sum
}

function Get-SubDirs {
    # 替代 PS3.0+ 的 Get-ChildItem -Directory
    param([string]$Path)
    return @(Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue | Where-Object { $_.PSIsContainer })
}

function Get-SubFiles {
    param([string]$Path)
    return @(Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue | Where-Object { -not $_.PSIsContainer })
}

function Get-DirsDepth {
    # 替代 PS5.0+ 的 Get-ChildItem -Depth
    param([string]$Base, [int]$MaxDepth)
    $result = New-Object System.Collections.ArrayList
    $stack = New-Object System.Collections.Stack
    $stack.Push(@($Base, 0))
    while ($stack.Count -gt 0) {
        $cur = $stack.Pop()
        $dir = [string]$cur[0]; $depth = [int]$cur[1]
        if ($depth -ge $MaxDepth) { continue }
        $subs = $null
        try { $subs = (New-Object System.IO.DirectoryInfo($dir)).GetDirectories() } catch { continue }
        foreach ($s in $subs) {
            try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
            [void]$result.Add($s.FullName)
            $stack.Push(@($s.FullName, ($depth + 1)))
        }
    }
    return $result.ToArray()
}

function Scan-AppRoots {
    # 在 Base 下按目录名(不区分大小写)搜索应用数据根目录,限制深度,并顺带收集虚拟机目录(纯函数,供并行调用)
    param([string]$Base)
    $rootNames = @('WeChat Files', 'xwechat_files', 'WXWork', 'Tencent Files', 'DingTalk', 'DingtalkData')
    $skip = @('$RECYCLE.BIN', 'System Volume Information', 'Windows', 'node_modules', '.git', 'WinSxS')
    $found = New-Object System.Collections.ArrayList
    $vmx = New-Object System.Collections.ArrayList
    $stack = New-Object System.Collections.Stack
    $stack.Push(@($Base, 0))
    while ($stack.Count -gt 0) {
        $cur = $stack.Pop()
        $dir = [string]$cur[0]; $depth = [int]$cur[1]
        $subs = $null
        try { $subs = (New-Object System.IO.DirectoryInfo($dir)).GetDirectories() } catch { continue }
        foreach ($s in $subs) {
            try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
            try { if (($s.GetFiles('*.vmx').Length -gt 0) -or ($s.GetFiles('*.vbox').Length -gt 0)) { [void]$vmx.Add($s.FullName) } } catch {}
            if ($rootNames -contains $s.Name) { [void]$found.Add($s.FullName); continue }
            if ($skip -contains $s.Name) { continue }
            if (($depth + 1) -lt 6) { $stack.Push(@($s.FullName, ($depth + 1))) }
        }
    }
    return @{ Roots = $found.ToArray(); VmxDirs = $vmx.ToArray() }
}

function Scan-JunkFiles {
    # 全盘搜索临时/垃圾文件(纯函数,供并行调用;进度由主线程统一显示)
    param([string]$Base, [string[]]$IgnoreBases)
    $junkExt = @('.tmp', '.temp', '.dmp', '.chk', '.gid', '.crdownload', '.partial')
    $junkNames = @('thumbs.db')
    $skip = @('$RECYCLE.BIN', 'System Volume Information', '.git')
    $files = New-Object System.Collections.ArrayList
    $stack = New-Object System.Collections.Stack
    $stack.Push($Base)
    while ($stack.Count -gt 0) {
        $dir = [string]$stack.Pop()
        $di = $null
        try { $di = New-Object System.IO.DirectoryInfo($dir) } catch { continue }
        try {
            foreach ($sub in $di.GetDirectories()) {
                if ($skip -contains $sub.Name) { continue }
                try { if ($sub.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
                if (Test-IsUnder $sub.FullName $IgnoreBases) { continue }
                $stack.Push($sub.FullName)
            }
        } catch {}
        try {
            foreach ($f in $di.GetFiles()) {
                $ext = $f.Extension.ToLower()
                if (($junkExt -contains $ext) -or ($junkNames -contains $f.Name.ToLower())) { [void]$files.Add($f) }
            }
        } catch {}
    }
    return @{ Files = $files.ToArray() }
}

function Scan-JunkDirs {
    # 全盘扫描所有目录, 按目录名识别缓存/临时/日志/崩溃目录(不区分大小写,纯函数,供并行调用)
    # 不局限特定应用路径, 任何目录名符合垃圾特征的都会被收集
    param([string]$Base, [string[]]$IgnoreBases)
    $junkDirNames = @('cache', 'caches', 'temp', 'tmp', 'logs', 'log', 'crashpad', 'crashreports',
        'crashdumps', 'minidumps', 'cache_data', 'code cache', 'gpucache', 'media cache', 'inetcache',
        'cacheddata', 'cachedprofilesdata', 'cachedextensions', 'cachedextensionvsixs', 'squirreltemp',
        'cache2', 'shadercache', 'grshadercache', 'dawncache', 'service worker', 'serviceworkercachestorage',
        'update cache', 'updatecache')
    $skipDirs = @('$recycle.bin', 'system volume information', 'windows', 'winsxs', 'node_modules',
        '.git', 'program files', 'program files (x86)', 'programdata', 'boot', 'system32', 'syswow64',
        'recovery', 'system', 'temp$', 'assembly', 'found.000')
    $found = New-Object System.Collections.ArrayList
    $stack = New-Object System.Collections.Stack
    $stack.Push(@($Base, 0))
    while ($stack.Count -gt 0) {
        $cur = $stack.Pop()
        $dir = [string]$cur[0]; $depth = [int]$cur[1]
        $subs = $null
        try { $subs = (New-Object System.IO.DirectoryInfo($dir)).GetDirectories() } catch { continue }
        foreach ($s in $subs) {
            try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
            if (Test-IsUnder $s.FullName $IgnoreBases) { continue }
            $sn = $s.Name.ToLower()
            if ($skipDirs -contains $sn) { continue }
            if ($junkDirNames -contains $sn) { [void]$found.Add($s.FullName); continue }
            if (($depth + 1) -lt 10) { $stack.Push(@($s.FullName, ($depth + 1))) }
        }
    }
    return @{ Dirs = $found.ToArray() }
}

function Remove-DirContents {
    # 删除目录内所有内容但保留目录本身
    param([string]$dir)
    $fail = 0
    if (-not $dir) { return 0 }
    if ($dir.TrimEnd('\').Length -le 3) { return 0 }
    $items = @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)
    foreach ($it in $items) {
        $done = $false
        try {
            if ($it.PSIsContainer) {
                [System.IO.Directory]::Delete($it.FullName, $true)
            } else {
                try { $it.Attributes = [System.IO.FileAttributes]::Normal } catch {}
                [System.IO.File]::Delete($it.FullName)
            }
            $done = $true
        } catch {}
        if (-not $done) {
            try { Remove-Item -LiteralPath $it.FullName -Recurse -Force -ErrorAction Stop } catch { $fail++ }
        }
    }
    return $fail
}

$global:QJCat = @{}
function Add-Cat {
    param([string]$key, [string]$path)
    if (-not $path) { return }
    if (Test-Path -LiteralPath $path -PathType Container) {
        if (-not $global:QJCat.ContainsKey($key)) { $global:QJCat[$key] = @() }
        $global:QJCat[$key] += $path
    }
}

$global:QJCatFiles = @{}
function Add-CatFile {
    param([string]$key, [string]$path)
    if (-not $path) { return }
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        if (-not $global:QJCatFiles.ContainsKey($key)) { $global:QJCatFiles[$key] = @() }
        $global:QJCatFiles[$key] += (New-Object System.IO.FileInfo($path))
    }
}

function Add-ChromiumCaches {
    param([string]$key, [string]$userData)
    if (-not (Test-Path -LiteralPath $userData)) { return }
    foreach ($n in @('ShaderCache', 'GrShaderCache', 'GraphiteDawnCache')) { Add-Cat $key (Join-Path $userData $n) }
    foreach ($prof in @(Get-SubDirs $userData)) {
        if (($prof.Name -ne 'Default') -and ($prof.Name -notlike 'Profile*') -and ($prof.Name -notlike 'Guest*')) { continue }
        foreach ($n in @('Cache', 'Code Cache', 'GPUCache', 'Media Cache')) {
            Add-Cat $key (Join-Path $prof.FullName $n)
        }
    }
}

function Add-AppRootCategories {
    # 根据应用数据根目录(微信/企业微信/QQ/钉钉)把缓存/图片/视频/文件等加入分类(主线程执行,写共享分类)
    param([object[]]$Roots)
    foreach ($root in $Roots) {
        if (-not $root) { continue }
        $leaf = Split-Path $root -Leaf
        if ($leaf -ieq 'WeChat Files') {
            # 微信 3.x
            Add-Cat 'wx_applet' (Join-Path $root 'Applet')
            Add-Cat 'wx_applet' (Join-Path $root 'WMPF')
            foreach ($acct in @(Get-SubDirs $root)) {
                $fs = Join-Path $acct.FullName 'FileStorage'
                if (-not (Test-Path -LiteralPath $fs)) { continue }
                Add-Cat 'wx_cache'   (Join-Path $fs 'Cache')
                Add-Cat 'wx_image'   (Join-Path $fs 'Image')
                Add-Cat 'wx_image'   (Join-Path $fs 'MsgAttach')
                Add-Cat 'wx_video'   (Join-Path $fs 'Video')
                Add-Cat 'wx_file'    (Join-Path $fs 'File')
                Add-Cat 'wx_sns'     (Join-Path $fs 'Sns')
                Add-Cat 'wx_emotion' (Join-Path $fs 'CustomEmotion')
            }
        }
        elseif ($leaf -ieq 'xwechat_files') {
            # 微信 4.x
            Add-Cat 'wx_applet' (Join-Path $root 'wmpf')
            foreach ($acct in @(Get-SubDirs $root)) {
                $an = $acct.FullName
                $isAcct = (Test-Path -LiteralPath (Join-Path $an 'msg')) -or (Test-Path -LiteralPath (Join-Path $an 'db_storage'))
                if (-not $isAcct) { continue }
                Add-Cat 'wx_cache' (Join-Path $an 'cache')
                Add-Cat 'wx_cache' (Join-Path $an 'temp')
                Add-Cat 'wx_image' (Join-Path $an 'msg\attach')
                Add-Cat 'wx_video' (Join-Path $an 'msg\video')
                Add-Cat 'wx_file'  (Join-Path $an 'msg\file')
            }
        }
        elseif (($leaf -ieq 'WXWork') -or ($leaf -ieq 'WeCom')) {
            # 企业微信
            Add-Cat 'wxwork_log'   (Join-Path $root 'Global\Log')
            Add-Cat 'wxwork_cache' (Join-Path $root 'Update')
            foreach ($acct in @(Get-SubDirs $root)) {
                if (($acct.Name -ieq 'Global') -or ($acct.Name -ieq 'Update')) { continue }
                $cacheDir = Join-Path $acct.FullName 'Cache'
                if (Test-Path -LiteralPath $cacheDir) {
                    foreach ($sub in @(Get-SubDirs $cacheDir)) {
                        switch -Wildcard ($sub.Name.ToLower()) {
                            'image*' { Add-Cat 'wxwork_image' $sub.FullName }
                            'video*' { Add-Cat 'wxwork_video' $sub.FullName }
                            'file*'  { Add-Cat 'wxwork_file'  $sub.FullName }
                            default  { Add-Cat 'wxwork_cache' $sub.FullName }
                        }
                    }
                }
                Add-Cat 'wxwork_log' (Join-Path $acct.FullName 'Log')
            }
        }
        elseif ($leaf -ieq 'Tencent Files') {
            # QQ / TIM
            foreach ($acct in @(Get-SubDirs $root)) {
                if ($acct.Name -notmatch '^\d{4,}$') { continue }
                $an = $acct.FullName
                Add-Cat 'qq_cache' (Join-Path $an 'Image')
                Add-Cat 'qq_cache' (Join-Path $an 'Audio')
                Add-Cat 'qq_video' (Join-Path $an 'Video')
                Add-Cat 'qq_recv'  (Join-Path $an 'FileRecv')
                $nt = Join-Path $an 'nt_qq\nt_data'
                if (Test-Path -LiteralPath $nt) {
                    Add-Cat 'qq_cache' (Join-Path $nt 'Pic')
                    Add-Cat 'qq_cache' (Join-Path $nt 'Emoji')
                    Add-Cat 'qq_video' (Join-Path $nt 'Video')
                    Add-Cat 'qq_log'   (Join-Path $nt 'log')
                    Add-Cat 'qq_recv'  (Join-Path $nt 'File')
                }
            }
        }
        elseif (($leaf -ieq 'DingTalk') -or ($leaf -ieq 'DingtalkData')) {
            # 钉钉: 只清理日志/缓存/临时
            foreach ($sub in @(Get-DirsDepth -Base $root -MaxDepth 2)) {
                if (($sub.Name -match '^(?i)(log|logs|holmeslog|temp|tmp)$') -or ($sub.Name -match '(?i)cache')) {
                    Add-Cat 'dd_cache' $sub.FullName
                }
            }
        }
    }
}

# ---------------- 检测所有固定磁盘 ----------------
$drives = @()
Get-WmiObject Win32_LogicalDisk -Filter "DriveType=3" -ErrorAction SilentlyContinue | ForEach-Object { $drives += $_.DeviceID.TrimEnd(':') }
if ($drives.Count -eq 0) {
    Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue | Where-Object { $_.Free -ne $null } | ForEach-Object { $drives += $_.Name }
}
$drives = @($drives | Where-Object { $_ } | Sort-Object -Unique)
if ($drives.Count -eq 0) { $drives = @('C') }

# ---------------- eBox 多开环境数据目录 (保护关键文件 + 专用清理) ----------------
# 环境数据根目录: 注册表 HKCU\Software\eBox\DataDir(兼容旧键 Software\2Box) + 默认候选位置。
# 环境关键文件(登录状态/注册表 hive/企业配置/设备指纹)绝不删除;
# 环境内的缓存与聊天记录由专用白名单精确清理(与应用"环境清理"按钮行为一致)。
$global:EBoxEnvBases = New-Object System.Collections.ArrayList
function Init-EBoxEnvBases {
    foreach ($sub in @('Software\eBox', 'Software\2Box')) {
        try {
            $v = (Get-ItemProperty ("HKCU:\" + $sub) -ErrorAction Stop).DataDir
            if ($v) {
                $p = [string]$v
                if (Test-Path -LiteralPath $p) { [void]$global:EBoxEnvBases.Add((Join-Path $p 'Env')) }
            }
        } catch {}
    }
    foreach ($p in @('C:\eBoxData', 'D:\eBoxData', 'C:\2BoxData', 'D:\2BoxData')) {
        $e = Join-Path $p 'Env'
        if ((Test-Path -LiteralPath $e) -and (-not $global:EBoxEnvBases.Contains($e))) { [void]$global:EBoxEnvBases.Add($e) }
    }
}
Init-EBoxEnvBases

# ---------------- 欢迎信息 ----------------
try { Clear-Host } catch {}
Write-Host ''
Write-Host '  =============================================' -ForegroundColor Cyan
Write-Host '          全 盘 垃 圾 清 理 工 具  v2.0' -ForegroundColor Cyan
Write-Host '  =============================================' -ForegroundColor Cyan
Write-Host ('  检测到本地磁盘: ' + ($drives -join '  ')) -ForegroundColor Green
Write-Host ''
Write-Host '  本次清理将自动执行:'
Write-Host '    * 自动关闭正在运行的应用(微信/企业微信/QQ/钉钉/浏览器等)' -ForegroundColor Yellow
Write-Host '    * 清理系统临时文件、更新缓存、预读取、错误报告、崩溃转储'
Write-Host '    * 清理浏览器缓存、开发工具缓存、应用缓存、缩略图缓存'
Write-Host '    * 清理微信/企业微信/QQ/钉钉的聊天记录缓存、图片、视频、文件'
Write-Host '    * 清理 eBox 多开环境内的缓存与聊天记录(不影响登录状态)'
Write-Host '    * 清理 *.tmp/*.dmp/Thumbs.db 等散落垃圾文件'
Write-Host '    * 清空所有磁盘的回收站'
Write-Host ''
Write-Host '  *** 以上均为永久删除(不经过回收站),不可恢复! ***' -ForegroundColor Red
Write-Host '  *** 建议先保存正在编辑的文档并退出不必要的程序 ***' -ForegroundColor Red
Write-Host ''
Write-Host '  请确认: 回车键 = 开始清理   |   ESC键 = 取消退出' -ForegroundColor Cyan

# ---------------- 回车/ESC 确认 ----------------
$key = $null
if ($env:EBOX_AUTO -ne '1') {
    try { $key = [Console]::ReadKey($true) } catch { $null = Read-Host '按回车键开始清理...' }
    if (($null -ne $key) -and ($key.Key -eq [ConsoleKey]::Escape)) {
        Write-Host '  已取消,未做任何修改。' -ForegroundColor Yellow
        Read-Host '  按回车键退出'
        exit
    }
}

# ---------------- 管理员权限 ----------------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    try {
        Start-Process -FilePath 'cmd.exe' -ArgumentList ('/c "{0}"' -f $env:SELF) -Verb RunAs -ErrorAction Stop
        exit
    } catch {
        Write-Host '  未获得管理员权限,将以普通权限继续(部分内容可能清理不彻底)。' -ForegroundColor Yellow
    }
}

# ---------------- 第一步: 扫描所有磁盘 ----------------
Write-Host ''
Write-Host '  [1/3] 正在扫描所有磁盘中的垃圾文件与缓存...' -ForegroundColor Cyan
$global:QJVmxDirs = New-Object System.Collections.ArrayList
$appInfos = @()
$scanCount = 0
$envBases = @($global:EBoxEnvBases)
$parallelDegree = 4
try { $cpu = [int]$env:NUMBER_OF_PROCESSORS; if (($cpu -gt 0) -and ($cpu -lt $parallelDegree)) { $parallelDegree = $cpu } } catch {}
$scanWorker = {
    param($task)
    if ($task['Kind'] -eq 'AppRoots') { return (Scan-AppRoots -Base $task['Base']) }
    elseif ($task['Kind'] -eq 'JunkDirs') { return (Scan-JunkDirs -Base $task['Base'] -IgnoreBases $task['IgnoreBases']) }
    elseif ($task['Kind'] -eq 'JunkFiles') { return (Scan-JunkFiles -Base $task['Base'] -IgnoreBases $task['IgnoreBases']) }
}
$scanFns = @('Scan-AppRoots', 'Scan-JunkDirs', 'Scan-JunkFiles', 'Test-IsUnder')

foreach ($dv in $drives) {
    $scanBase = "$dv`:\"
    $scanCount++
    Write-Host ''
    Write-Host ('  ------ 扫描 {0} 盘 ------  [{1}/{2}]' -f $dv, $scanCount, $drives.Count) -ForegroundColor DarkYellow
    if (-not (Test-Path -LiteralPath $scanBase)) {
        Write-Host ('        {0} 盘不可访问,跳过。' -f $dv) -ForegroundColor DarkGray
        continue
    }

    # ---- 通用: 根目录临时文件夹 ----
    foreach ($p in @("$dv`:\Temp", "$dv`:\Tmp", "$dv`:\Cache")) { Add-Cat 'root_temp' $p }

    # ---- 通用: 回收站 ----
    $rb = "$dv`:\`$RECYCLE.BIN"
    if (Test-Path -LiteralPath $rb) { Add-Cat 'recycle' $rb }

    # ---- 并行扫描: 应用数据根目录 / 缓存目录 / 散落垃圾文件 ----
    $tasks = @(
        @{ Base = $scanBase; Kind = 'AppRoots'; IgnoreBases = $envBases },
        @{ Base = $scanBase; Kind = 'JunkDirs'; IgnoreBases = $envBases },
        @{ Base = $scanBase; Kind = 'JunkFiles'; IgnoreBases = $envBases }
    )
    $scanRes = @(Invoke-Parallel -InputObjects $tasks -ScriptBlock $scanWorker -FunctionNames $scanFns -Throttle $parallelDegree -ProgressActivity ('正在扫描 {0} 盘' -f $dv))

    $appRootsRes = $scanRes[0]
    $roots = @($appRootsRes['Roots'])
    foreach ($v in @($appRootsRes['VmxDirs'])) { [void]$global:QJVmxDirs.Add($v) }
    foreach ($r in $roots) { Write-Host ('        应用数据: ' + $r) -ForegroundColor DarkGray }

    Add-AppRootCategories $roots

    $junkDirs = @($scanRes[1]['Dirs'])
    if ($junkDirs.Count -gt 0) {
        Write-Host ('        发现 {0} 个缓存/垃圾目录。' -f $junkDirs.Count) -ForegroundColor DarkGray
        foreach ($jd in $junkDirs) { Add-Cat 'disk_cache' $jd }
    }

    $junkFiles = @($scanRes[2]['Files'])
    if ($junkFiles.Count -gt 0) {
        Write-Host ('        发现 {0} 个临时/垃圾文件。' -f $junkFiles.Count) -ForegroundColor DarkGray
        if (-not $global:QJCatFiles.ContainsKey('junk_files')) { $global:QJCatFiles['junk_files'] = @() }
        foreach ($jf in $junkFiles) { $global:QJCatFiles['junk_files'] += $jf }
    }

    # ---- 仅 C 盘: 系统级垃圾 + 浏览器 + 开发工具 + 应用缓存 ----
    if ($dv -eq 'C') {
        Write-Host '        扫描系统级垃圾...' -ForegroundColor DarkGray
        $userDirs = @(Get-SubDirs 'C:\Users' | Where-Object { $_.Name -ne 'Public' })

        foreach ($u in $userDirs) { Add-Cat 'sys_usertemp' (Join-Path $u.FullName 'AppData\Local\Temp') }
        Add-Cat 'sys_wintemp' 'C:\Windows\Temp'
        Add-Cat 'sys_prefetch' 'C:\Windows\Prefetch'

        Add-Cat 'sys_update' 'C:\Windows\SoftwareDistribution\Download'
        Add-Cat 'sys_update' 'C:\Windows\ServiceProfiles\NetworkService\AppData\Local\Microsoft\Windows\DeliveryOptimization\Cache'

        foreach ($p in @('C:\Windows.old', 'C:\$Windows.~BT', 'C:\$Windows.~WS', 'C:\$GetCurrent', 'C:\ESD')) { Add-Cat 'sys_old' $p }

        foreach ($u in $userDirs) {
            foreach ($p in @('AppData\Local\D3DSCache', 'AppData\Local\NVIDIA\DXCache', 'AppData\Local\NVIDIA\GLCache', 'AppData\Local\AMD\DxCache', 'AppData\Local\AMD\DxcCache')) {
                Add-Cat 'sys_gpucache' (Join-Path $u.FullName $p)
            }
        }

        Add-Cat 'log_dump' 'C:\Windows\Minidump'
        Add-Cat 'log_dump' 'C:\Windows\LiveKernelReports'
        Add-CatFile 'log_dump' 'C:\Windows\MEMORY.DMP'
        foreach ($u in $userDirs) { Add-Cat 'log_dump' (Join-Path $u.FullName 'AppData\Local\CrashDumps') }
        Add-Cat 'log_wer' 'C:\ProgramData\Microsoft\Windows\WER\ReportQueue'
        Add-Cat 'log_wer' 'C:\ProgramData\Microsoft\Windows\WER\ReportArchive'
        Add-Cat 'log_wer' 'C:\ProgramData\Microsoft\Windows\WER\Temp'
        foreach ($u in $userDirs) { Add-Cat 'log_wer' (Join-Path $u.FullName 'AppData\Local\Microsoft\Windows\WER') }

        Add-Cat 'log_winlogs' 'C:\Windows\Logs'
        Add-Cat 'log_iis' 'C:\inetpub\logs\LogFiles'

        foreach ($u in $userDirs) {
            $la = Join-Path $u.FullName 'AppData\Local'
            Add-ChromiumCaches 'br_cache' (Join-Path $la 'Google\Chrome\User Data')
            Add-ChromiumCaches 'br_cache' (Join-Path $la 'Microsoft\Edge\User Data')
            Add-ChromiumCaches 'br_cache' (Join-Path $la 'BraveSoftware\Brave-Browser\User Data')
            Add-ChromiumCaches 'br_cache' (Join-Path $la '360ChromeX\Chrome\User Data')
            Add-ChromiumCaches 'br_cache' (Join-Path $la '360Chrome\Chrome\User Data')
            Add-ChromiumCaches 'br_cache' (Join-Path $la 'Tencent\QQBrowser\User Data')
            $ffProfiles = Join-Path $la 'Mozilla\Firefox\Profiles'
            if (Test-Path -LiteralPath $ffProfiles) {
                foreach ($prof in @(Get-SubDirs $ffProfiles)) { Add-Cat 'br_cache' (Join-Path $prof.FullName 'cache2') }
            }
        }

        foreach ($u in $userDirs) {
            $exp = Join-Path $u.FullName 'AppData\Local\Microsoft\Windows\Explorer'
            if (Test-Path -LiteralPath $exp) {
                foreach ($f in @(Get-SubFiles $exp)) {
                    if ($f.Name -match '^(?i)(thumbcache|iconcache)_.*\.db$') { Add-CatFile 'thumb' $f.FullName }
                }
            }
            Add-CatFile 'thumb' (Join-Path $u.FullName 'AppData\Local\IconCache.db')
        }

        foreach ($u in $userDirs) {
            foreach ($p in @('AppData\Local\pip\cache', 'AppData\Local\npm-cache', 'AppData\Roaming\npm-cache', 'AppData\Local\Yarn\Cache', 'AppData\Local\NuGet\v3-cache', 'AppData\Local\go-build', 'AppData\Local\pnpm-cache', '.gradle\caches', '.cargo\registry\cache')) {
                Add-Cat 'dev_cache' (Join-Path $u.FullName $p)
            }
        }

        # 虚拟机日志(仅日志/转储,不碰虚拟磁盘)
        foreach ($u in $userDirs) {
            foreach ($vmBaseRel in @('Documents\Virtual Machines', 'Documents\My Virtual Machines', 'OneDrive\Documents\Virtual Machines', 'VirtualBox VMs')) {
                $vb = Join-Path $u.FullName $vmBaseRel
                if (-not (Test-Path -LiteralPath $vb)) { continue }
                foreach ($vm in @(Get-SubDirs $vb)) {
                    foreach ($f in @(Get-SubFiles $vm.FullName)) {
                        if (($f.Name -match '^(?i)vmware.*\.log$') -or ($f.Extension -ieq '.dmp') -or ($f.Name -match '^(?i)vmmcores')) { Add-CatFile 'vm_logs' $f.FullName }
                    }
                    Add-Cat 'vm_logs' (Join-Path $vm.FullName 'Logs')
                }
            }
            $vbHome = Join-Path $u.FullName '.VirtualBox'
            if (Test-Path -LiteralPath $vbHome) {
                foreach ($f in @(Get-SubFiles $vbHome)) {
                    if ($f.Name -match '(?i)\.log(\.\d+)?$') { Add-CatFile 'vm_logs' $f.FullName }
                }
            }
        }
        if (Test-Path -LiteralPath 'C:\ProgramData\VMware') {
            foreach ($sub in @(Get-DirsDepth -Base 'C:\ProgramData\VMware' -MaxDepth 2)) {
                if ($sub.Name -match '^(?i)(logs?|caches?)$') { Add-Cat 'vm_logs' $sub.FullName }
            }
        }

        # C 盘上的微信/企业微信/QQ (文档目录或自定义保存路径)
        $appRoots = @()
        foreach ($u in $userDirs) {
            foreach ($docBase in @((Join-Path $u.FullName 'Documents'), (Join-Path $u.FullName 'OneDrive\Documents'), $u.FullName)) {
                foreach ($n in @('WeChat Files', 'xwechat_files', 'WXWork', 'WeCom', 'Tencent Files', 'DingTalk', 'DingtalkData')) {
                    $cand = Join-Path $docBase $n
                    if (Test-Path -LiteralPath $cand) { $appRoots += $cand }
                }
            }
        }
        try {
            $v = (Get-ItemProperty 'HKCU:\Software\Tencent\WeChat' -ErrorAction Stop).FileSavePath
            if ($v -and ($v -ne 'MyDocument:') -and ($v -like 'C:*')) {
                foreach ($n in @('WeChat Files', 'xwechat_files')) {
                    $cand = Join-Path $v $n
                    if (Test-Path -LiteralPath $cand) { $appRoots += $cand }
                }
            }
        } catch {}
        $appRoots = @($appRoots | Sort-Object -Unique)
        foreach ($candRoot in $appRoots) {
            if ($roots -contains $candRoot) { continue }
            Add-AppRootCategories @($candRoot)
        }

        # 各应用在 AppData 的缓存/日志 (IDE/编辑器/常用软件)
        $cacheNames = @('cache', 'caches', 'cache_data', 'code cache', 'gpucache', 'shadercache', 'grshadercache',
            'dawncache', 'dawngraphitecache', 'dawnwebgpucache', 'cacheddata', 'cachedprofilesdata',
            'cachedextensions', 'cachedextensionvsixs', 'cachestorage', 'crashpad', 'crashreports', 'crashes',
            'logs', 'log', 'temp', 'tmp', 'squirreltemp', 'inetcache', 'media cache', 'minidumps', 'sentry')
        $scanSkip = @('packages', 'windowsapps', 'package cache', 'application data', 'history')
        $covered = New-Object System.Collections.ArrayList
        foreach ($k in @($global:QJCat.Keys)) { foreach ($p in $global:QJCat[$k]) { [void]$covered.Add($p.TrimEnd('\')) } }
        $appCache = @{}
        foreach ($u in $userDirs) {
            foreach ($baseRel in @('AppData\Local', 'AppData\Roaming')) {
                $base = Join-Path $u.FullName $baseRel
                if (-not (Test-Path -LiteralPath $base)) { continue }
                $baseLen = $base.Length
                $stack = New-Object System.Collections.Stack
                $stack.Push(@($base, 0))
                while ($stack.Count -gt 0) {
                    $cur = $stack.Pop()
                    $dir = [string]$cur[0]; $depth = [int]$cur[1]
                    $subs = $null
                    try { $subs = (New-Object System.IO.DirectoryInfo($dir)).GetDirectories() } catch { continue }
                    foreach ($s in $subs) {
                        try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
                        $nl = $s.Name.ToLower()
                        if ($scanSkip -contains $nl) { continue }
                        if ($cacheNames -contains $nl) {
                            $fp = $s.FullName
                            $dup = $false
                            foreach ($cv in $covered) {
                                if (($fp -ieq $cv) -or $fp.StartsWith(($cv + '\'), [StringComparison]::OrdinalIgnoreCase) -or $cv.StartsWith(($fp + '\'), [StringComparison]::OrdinalIgnoreCase)) { $dup = $true; break }
                            }
                            if (-not $dup) {
                                $rel = $fp.Substring($baseLen + 1)
                                $seg = $rel.Split('\')[0]
                                if (-not $appCache.ContainsKey($seg)) { $appCache[$seg] = @() }
                                $appCache[$seg] += $fp
                                [void]$covered.Add($fp)
                            }
                            continue
                        }
                        if (($depth + 1) -le 4) { $stack.Push(@($s.FullName, ($depth + 1))) }
                    }
                }
            }
        }
        $appInfos = @()
        if ($appCache.Count -gt 0) {
            $appTasks = @()
            foreach ($k in @($appCache.Keys)) { $appTasks += @{ App = $k; Paths = @($appCache[$k]) } }
            $appWorker = { param($t) $sz = [double]0; foreach ($p in $t['Paths']) { $sz += Get-DirSize $p }; return @{ App = $t['App']; Size = $sz } }
            $appSizes = @(Invoke-Parallel -InputObjects $appTasks -ScriptBlock $appWorker -FunctionNames @('Get-DirSize') -Throttle $parallelDegree -ProgressActivity '正在统计应用缓存大小')
            foreach ($r in $appSizes) {
                if ($r['Size'] -gt 0) { $appInfos += (New-Obj @{ App = $r['App']; Paths = @($appCache[$r['App']]); Size = $r['Size'] }) }
            }
            Write-Host ('        发现 {0} 个应用产生了缓存/日志。' -f $appInfos.Count) -ForegroundColor DarkGray
        }
    }
}

# ---------------- 虚拟机垃圾清理 (所有磁盘上发现的虚拟机) ----------------
$vmDirs = @($global:QJVmxDirs | Sort-Object -Unique)
if ($vmDirs.Count -gt 0) {
    Write-Host ''
    Write-Host ('  发现 {0} 个虚拟机目录,收集其中的日志/转储/临时垃圾...' -f $vmDirs.Count) -ForegroundColor DarkYellow
    $vmCnt = 0
    foreach ($vmDir in $vmDirs) {
        $vmCnt++
        $s = '{0}/{1}' -f $vmCnt, $vmDirs.Count
        $pct = [int](100 * $vmCnt / $vmDirs.Count)
        Write-Progress -Activity '收集虚拟机垃圾文件' -Status $s -PercentComplete $pct
        Write-Host ('        虚拟机: ' + $vmDir) -ForegroundColor DarkGray
        # 目录内及一级子目录的日志/转储/临时文件 (绝不碰 .vmdk/.vdi/.vmx/.nvram/.vmem/.vmss/.vmsn)
        foreach ($f in @(Get-SubFiles $vmDir)) {
            if ($f.Name -match '(?i)^(vmware.*\.log|.*\.log(\.\d+)?$|vmmcores.*|.*\.dmp$|.*\.core$|.*\.core\.\d+$)') {
                Add-CatFile 'vm_logs' $f.FullName
            }
        }
        Add-Cat 'vm_logs' (Join-Path $vmDir 'Logs')
        foreach ($sub in @(Get-SubDirs $vmDir)) {
            if ($sub.Name -match '(?i)^(logs?|.*\.lck)$') {
                # 锁目录(.lck)仅在虚拟机未运行时清理内容, 日志目录直接清
                Add-Cat 'vm_logs' $sub.FullName
            }
            if ($sub.Name -match '(?i)^(cache|caches|tmp|temp)$') { Add-Cat 'vm_logs' $sub.FullName }
        }
    }
    Write-Progress -Activity '收集虚拟机垃圾文件' -Completed
}

# ---------------- eBox 多开环境数据: 缓存 + 聊天记录 ----------------
# 白名单与应用"环境清理"按钮完全一致:
#  缓存: qtCef/WXWorkCefCache/着色器/浏览器指标/字典 等整目录;
#        Default profile 下的 Cache/Code Cache/GPU缓存/Service Worker 等子缓存
#  聊天记录: 各数字企业目录下的 Data 消息库 / Index 搜索索引
# 绝不触碰: 注册表 hive / Cookies / Local Storage / Preferences / 企业配置 / 设备指纹
$global:EBXEnvCacheDirs = New-Object System.Collections.ArrayList
$global:EBXEnvChatDirs = New-Object System.Collections.ArrayList

function Add-EnvWxworkCaches {
    param([string]$WxDir)
    $whole = @('qtcef','wxworkcefcache','shadercache','grshadercache','graphitedawncache','browsermetrics','segmentation_platform','dictionaries')
    $defSub = @('cache','code cache','gpucache','dawncache','dawnwebgpucache','dawnwebgpu','shadercache','grshadercache','graphitedawncache','service worker','shared dictionary')
    $stack = New-Object System.Collections.Stack
    $stack.Push($WxDir)
    while ($stack.Count -gt 0) {
        $dir = [string]$stack.Pop()
        $subs = $null
        try { $subs = (New-Object System.IO.DirectoryInfo($dir)).GetDirectories() } catch { continue }
        foreach ($s in $subs) {
            try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
            $sn = $s.Name.ToLower()
            if ($whole -contains $sn) { [void]$global:EBXEnvCacheDirs.Add($s.FullName); continue }
            if ($sn -eq 'default') {
                $ds = $null
                try { $ds = (New-Object System.IO.DirectoryInfo($s.FullName)).GetDirectories() } catch { continue }
                foreach ($d in $ds) {
                    $dn = $d.Name.ToLower()
                    if ($defSub -contains $dn) { [void]$global:EBXEnvCacheDirs.Add($d.FullName) }
                }
                continue
            }
            $stack.Push($s.FullName)
        }
    }
    # 聊天记录: 数字企业目录下的一级 Data/Index 目录
    $enterprise = $null
    try { $enterprise = (New-Object System.IO.DirectoryInfo($WxDir)).GetDirectories() } catch {}
    if ($enterprise) {
        foreach ($e in $enterprise) {
            $en = $e.Name
            if (($en.Length -ge 5) -and ($en -match '^\d+$')) {
                $eds = $null
                try { $eds = (New-Object System.IO.DirectoryInfo($e.FullName)).GetDirectories() } catch { continue }
                foreach ($d in $eds) {
                    $dn = $d.Name.ToLower()
                    if (($dn -eq 'data') -or ($dn -eq 'index')) { [void]$global:EBXEnvChatDirs.Add($d.FullName) }
                }
            }
        }
    }
}

function Collect-EBoxEnvData {
    foreach ($base in $global:EBoxEnvBases) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        $envDirs = $null
        try { $envDirs = (New-Object System.IO.DirectoryInfo($base)).GetDirectories() } catch { continue }
        foreach ($envDir in $envDirs) {
            $stack = New-Object System.Collections.Stack
            $stack.Push($envDir.FullName)
            while ($stack.Count -gt 0) {
                $dir = [string]$stack.Pop()
                $subs = $null
                try { $subs = (New-Object System.IO.DirectoryInfo($dir)).GetDirectories() } catch { continue }
                foreach ($s in $subs) {
                    try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
                    if ($s.Name.ToLower() -eq 'wxwork') { Add-EnvWxworkCaches $s.FullName; continue }
                    $stack.Push($s.FullName)
                }
            }
        }
    }
}
Collect-EBoxEnvData
if ($global:EBXEnvCacheDirs.Count -gt 0) { $global:QJCat['ebox_env_cache'] = @($global:EBXEnvCacheDirs | Sort-Object -Unique) }
if ($global:EBXEnvChatDirs.Count -gt 0) { $global:QJCat['ebox_env_chat'] = @($global:EBXEnvChatDirs | Sort-Object -Unique) }

# ---------------- 组装清理项目列表 ----------------
$meta = @(
    @{K='sys_usertemp'; G='系统';     N='用户临时文件夹(Temp)';     Note='使用中的文件自动跳过';                 Sel=$true},
    @{K='sys_wintemp';  G='系统';     N='Windows临时文件夹';        Note='安全';                                Sel=$true},
    @{K='sys_update';   G='系统';     N='Windows更新缓存';          Note='需要时会重新下载';                    Sel=$true},
    @{K='sys_prefetch'; G='系统';     N='预读取缓存(Prefetch)';     Note='安全,程序首次启动稍慢';              Sel=$true},
    @{K='sys_gpucache'; G='系统';     N='显卡着色器缓存';           Note='安全,自动重建';                      Sel=$true},
    @{K='sys_old';      G='系统';     N='旧系统/升级残留';          Note='删除后无法回退旧系统';               Sel=$true; TakeOwn=$true},
    @{K='log_dump';     G='日志';     N='崩溃转储文件';             Note='安全';                              Sel=$true},
    @{K='log_wer';      G='日志';     N='Windows错误报告';          Note='安全';                              Sel=$true},
    @{K='log_winlogs';  G='日志';     N='Windows日志文件';          Note='使用中的自动跳过';                  Sel=$true},
    @{K='log_iis';      G='日志';     N='IIS服务器日志';            Note='排查网站问题时才需要';               Sel=$false},
    @{K='br_cache';     G='浏览器';   N='网页缓存(Chrome/Edge等)';  Note='安全,网页重新加载';                  Sel=$true},
    @{K='thumb';        G='用户缓存'; N='缩略图/图标缓存';          Note='安全,系统自动重建';                  Sel=$true},
    @{K='dev_cache';    G='开发缓存'; N='pip/npm/Gradle等构建缓存'; Note='下次构建时重新下载';                 Sel=$true},
    @{K='vm_logs';      G='虚拟机';   N='VMware/VBox日志与转储';    Note='仅日志,不影响虚拟机数据';            Sel=$true},
    @{K='wx_cache';     G='微信';     N='缓存与临时文件';           Note='安全,微信自动重建';                  Sel=$true},
    @{K='wx_image';     G='微信';     N='聊天图片缓存';             Note='清理后历史图片无法查看';             Sel=$true},
    @{K='wx_video';     G='微信';     N='聊天视频';                 Note='清理后历史视频无法查看';             Sel=$true},
    @{K='wx_file';      G='微信';     N='聊天文件';                 Note='聊天中收发的文件将被删除';           Sel=$true},
    @{K='wx_sns';       G='微信';     N='朋友圈缓存';               Note='安全,可重新加载';                    Sel=$true},
    @{K='wx_emotion';   G='微信';     N='表情缓存';                 Note='自定义表情可重新下载';               Sel=$true},
    @{K='wx_applet';    G='微信';     N='小程序/视频号缓存';        Note='安全,可重新加载';                    Sel=$true},
    @{K='wxwork_cache'; G='企业微信'; N='缓存与更新包';             Note='安全,程序自动重建';                  Sel=$true},
    @{K='wxwork_log';   G='企业微信'; N='日志文件';                 Note='安全';                              Sel=$true},
    @{K='wxwork_image'; G='企业微信'; N='聊天图片缓存';             Note='清理后历史图片无法查看';             Sel=$true},
    @{K='wxwork_video'; G='企业微信'; N='聊天视频';                 Note='清理后历史视频无法查看';             Sel=$true},
    @{K='wxwork_file';  G='企业微信'; N='聊天文件';                 Note='聊天中收发的文件将被删除';           Sel=$true},
    @{K='qq_cache';     G='QQ/TIM';   N='缓存(图片/语音/表情)';     Note='清理后历史图片语音无法查看';         Sel=$true},
    @{K='qq_video';     G='QQ/TIM';   N='聊天视频';                 Note='清理后历史视频无法查看';             Sel=$true},
    @{K='qq_log';       G='QQ/TIM';   N='日志';                     Note='安全';                              Sel=$true},
    @{K='qq_recv';      G='QQ/TIM';   N='接收保存的文件';           Note='属于您保存的文件,默认不清理';        Sel=$false},
    @{K='dd_cache';     G='钉钉';     N='缓存与日志';               Note='安全';                              Sel=$true},
    @{K='disk_cache';   G='全盘缓存'; N='全盘扫描发现的缓存/临时/日志目录'; Note='不局限应用路径,凡目录名符合垃圾特征'; Sel=$true},
    @{K='root_temp';    G='通用';     N='磁盘根目录临时文件夹';     Note='Temp/Tmp/Cache 目录的内容';          Sel=$true},
    @{K='ebox_env_cache'; G='eBox环境'; N='多开环境缓存(CEF/着色器)'; Note='与应用"环境清理"一致,不影响登录';    Sel=$true},
    @{K='ebox_env_chat';  G='eBox环境'; N='多开环境聊天记录(消息库/索引)'; Note='清理后历史聊天记录无法查看,不影响登录'; Sel=$true}
)

$targets = New-Object System.Collections.ArrayList

# 回收站 (每个磁盘)
foreach ($dv in $drives) {
    $rb = "$dv`:\`$RECYCLE.BIN"
    if (Test-Path -LiteralPath $rb) {
        $rsz = Get-DirSize $rb
        if ($rsz -gt 0) {
            [void]$targets.Add((New-Obj @{
                Group='回收站'; Name=("$dv 盘回收站(彻底清空)"); Paths=@($rb); Files=@();
                Mode='RecycleBin'; Selected=$true; Note='永久删除,无法恢复'; Size=$rsz; TakeOwn=$false
            }))
        }
    }
}

foreach ($m in $meta) {
    $paths = @()
    $files = @()
    if ($global:QJCat.ContainsKey($m.K)) { $paths = @($global:QJCat[$m.K] | Sort-Object -Unique) }
    if ($global:QJCatFiles.ContainsKey($m.K)) { $files = @($global:QJCatFiles[$m.K]) }
    $clean = @()
    foreach ($p in $paths) {
        $nested = $false
        foreach ($q in $paths) {
            if (($p -ne $q) -and $p.StartsWith(($q.TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) { $nested = $true; break }
        }
        if (-not $nested) { $clean += $p }
    }
    if (($clean.Count -eq 0) -and ($files.Count -eq 0)) { continue }
    $to = $false
    if ($m.ContainsKey('TakeOwn')) { $to = $m.TakeOwn }
    [void]$targets.Add((New-Obj @{
        Group=$m.G; Name=$m.N; Paths=$clean; Files=$files; Mode='Contents';
        Selected=$m.Sel; Note=$m.Note; Size=[double]0; TakeOwn=$to
    }))
}

# 各应用缓存
if ($appInfos.Count -gt 0) {
    $bigApps = @($appInfos | Where-Object { $_.Size -ge 200MB } | Sort-Object -Property Size -Descending)
    $smallApps = @($appInfos | Where-Object { $_.Size -lt 200MB })
    foreach ($a in $bigApps) {
        [void]$targets.Add((New-Obj @{
            Group='应用缓存'; Name=($a.App + ' 缓存/日志'); Paths=$a.Paths; Files=@(); Mode='Contents';
            Selected=$true; Note='安全,应用自动重建(首次启动稍慢)'; Size=$a.Size; TakeOwn=$false
        }))
    }
    if ($smallApps.Count -gt 0) {
        $smPaths = @(); $smSize = [double]0
        foreach ($a in $smallApps) { $smPaths += $a.Paths; $smSize += $a.Size }
        [void]$targets.Add((New-Obj @{
            Group='应用缓存'; Name=('其他应用缓存/日志({0}个应用)' -f $smallApps.Count); Paths=$smPaths; Files=@(); Mode='Contents';
            Selected=$true; Note='安全,应用自动重建'; Size=$smSize; TakeOwn=$false
        }))
    }
}

# 散落垃圾文件
if ($global:QJCatFiles.ContainsKey('junk_files') -and @($global:QJCatFiles['junk_files']).Count -gt 0) {
    $jf = @($global:QJCatFiles['junk_files'])
    $jsz = [double]0
    foreach ($f in $jf) { try { $jsz += $f.Length } catch {} }
    [void]$targets.Add((New-Obj @{
        Group='通用'; Name=('临时/垃圾文件({0}个)' -f $jf.Count); Paths=@(); Files=$jf;
        Mode='Files'; Selected=$true; Note='*.tmp/*.dmp/未完成下载等'; Size=$jsz; TakeOwn=$false
    }))
}

# 虚拟机日志文件
if ($global:QJCatFiles.ContainsKey('vm_logs') -and @($global:QJCatFiles['vm_logs']).Count -gt 0) {
    $vf = @($global:QJCatFiles['vm_logs'])
    $vsz = [double]0
    foreach ($f in $vf) { try { $vsz += $f.Length } catch {} }
    [void]$targets.Add((New-Obj @{
        Group='虚拟机'; Name=('VMware/VBox日志与转储({0}个)' -f $vf.Count); Paths=@(); Files=$vf;
        Mode='Files'; Selected=$true; Note='仅日志/转储,不影响虚拟机数据'; Size=$vsz; TakeOwn=$false
    }))
}

# ---------------- 显示发现的垃圾目录(可视化) ----------------
Write-Host ''
Write-Host '  [2/3] 扫描到的垃圾目录/文件清单(按分类):' -ForegroundColor Cyan
$catNames = @{
    'recycle'     = '回收站'
    'sys_usertemp'= '用户临时文件'
    'sys_wintemp' = 'Windows临时'
    'sys_update'  = 'Windows更新缓存'
    'sys_prefetch'= '预读取缓存'
    'sys_gpucache'= '显卡着色器缓存'
    'sys_old'     = '旧系统残留'
    'log_dump'    = '崩溃转储'
    'log_wer'     = '错误报告'
    'log_winlogs' = 'Windows日志'
    'log_iis'     = 'IIS日志'
    'br_cache'    = '浏览器缓存'
    'thumb'       = '缩略图缓存'
    'dev_cache'   = '开发缓存'
    'vm_logs'     = '虚拟机日志'
    'wx_cache'    = '微信缓存'
    'wx_image'    = '微信图片'
    'wx_video'    = '微信视频'
    'wx_file'     = '微信文件'
    'wx_sns'      = '朋友圈缓存'
    'wx_emotion'  = '表情缓存'
    'wx_applet'   = '小程序缓存'
    'wxwork_cache'= '企业微信缓存'
    'wxwork_log'  = '企业微信日志'
    'wxwork_image'= '企业微信图片'
    'wxwork_video'= '企业微信视频'
    'wxwork_file' = '企业微信文件'
    'qq_cache'    = 'QQ缓存'
    'qq_video'    = 'QQ视频'
    'qq_log'      = 'QQ日志'
    'qq_recv'     = 'QQ接收文件'
    'dd_cache'    = '钉钉缓存'
    'disk_cache'  = '全盘缓存/临时/日志目录'
    'root_temp'   = '根目录临时'
    'ebox_env_cache' = 'eBox环境缓存'
    'ebox_env_chat'  = 'eBox环境聊天记录'
}
$foundAny = $false
foreach ($k in @($global:QJCat.Keys)) {
    $paths = @($global:QJCat[$k] | Sort-Object -Unique)
    if ($paths.Count -eq 0) { continue }
    $name = $k
    if ($catNames.ContainsKey($k)) { $name = $catNames[$k] }
    Write-Host ('    [' + $name + ']') -ForegroundColor Yellow
    foreach ($p in $paths) { Write-Host ('        ' + $p) -ForegroundColor DarkGray }
    $foundAny = $true
}
foreach ($k in @($global:QJCatFiles.Keys)) {
    $files = @($global:QJCatFiles[$k])
    if ($files.Count -eq 0) { continue }
    $name = $k
    if ($catNames.ContainsKey($k)) { $name = $catNames[$k] }
    Write-Host ('    [' + $name + '] ' + $files.Count + ' 个文件') -ForegroundColor Yellow
    foreach ($f in ($files | Select-Object -First 10)) { Write-Host ('        ' + $f.FullName) -ForegroundColor DarkGray }
    if ($files.Count -gt 10) { Write-Host ('        ... 等共 ' + $files.Count + ' 个文件') -ForegroundColor DarkGray }
    $foundAny = $true
}
if (-not $foundAny) { Write-Host '    (未发现垃圾文件)' -ForegroundColor DarkGray }

# ---------------- 第三步: 统计大小 ----------------
Write-Host ''
Write-Host '  [3/3] 正在统计各项目大小(内容多时需要一些时间)...' -ForegroundColor Cyan
$toSize = @($targets | Where-Object { $_.Size -le 0 })
if ($toSize.Count -gt 0) {
    $sizeWorker = { param($t) $s = [double]0; foreach ($p in $t.Paths) { $s += Get-DirSize $p }; foreach ($f in $t.Files) { try { $s += $f.Length } catch {} }; return @{ Size = $s } }
    $sizes = @(Invoke-Parallel -InputObjects $toSize -ScriptBlock $sizeWorker -FunctionNames @('Get-DirSize') -Throttle $parallelDegree -ProgressActivity '正在统计大小')
    for ($i = 0; $i -lt $toSize.Count; $i++) { $toSize[$i].Size = $sizes[$i]['Size'] }
}
$targets = @($targets | Where-Object { $_.Size -gt 0 })

if ($targets.Count -eq 0) {
    Write-Host ''
    Write-Host '  未发现可清理的内容,磁盘很干净!' -ForegroundColor Green
    Read-Host '  按回车键退出'
    exit
}

# ---------------- 显示扫描结果(可视化) ----------------
try { Clear-Host } catch {}
Write-Host ''
Write-Host '  ================ 扫描结果 ================' -ForegroundColor Cyan
Write-Host ''
Write-Host ('  编号  ' + (Pad-Cjk '分类' 10) + (Pad-Cjk '项目' 30) + ('{0,10}' -f '大小') + '   说明') -ForegroundColor Gray
Write-Host ('  ' + ('-' * 92)) -ForegroundColor DarkGray
$i = 0
$selSum = [double]0
$allSum = [double]0
foreach ($t in $targets) {
    $i++
    $allSum += $t.Size
    if ($t.Selected) { $selSum += $t.Size }
    Write-Host -NoNewline ('  {0,3}   ' -f $i)
    Write-Host -NoNewline (Pad-Cjk $t.Group 10)
    Write-Host -NoNewline (Pad-Cjk $t.Name 30)
    Write-Host -NoNewline ('{0,10}' -f (Format-Size $t.Size))
    Write-Host ('   ' + $t.Note) -ForegroundColor DarkGray
}
Write-Host ('  ' + ('-' * 92)) -ForegroundColor DarkGray
Write-Host ('  将清理: ' + (Format-Size $selSum) + '  /  可清理总计: ' + (Format-Size $allSum)) -ForegroundColor Cyan

# ---------------- 关闭占用程序(进程可视化) ----------------
$procMap = @{
    '浏览器'   = @('chrome', 'msedge', 'firefox', 'brave', '360se', '360ChromeX', 'QQBrowser')
    '微信'     = @('WeChat', 'WeChatApp', 'WeChatAppEx', 'Weixin', 'WeChatPlayer')
    '企业微信' = @('WXWork', 'WXWorkWeb', 'wwbizsdk')
    'QQ/TIM'   = @('QQ', 'TIM', 'QQNT', 'QQProtect')
    '钉钉'     = @('DingTalk')
}
$running = @()
foreach ($g in @('浏览器','微信','企业微信','QQ/TIM','钉钉')) {
    if (-not $procMap.ContainsKey($g)) { continue }
    foreach ($pn in $procMap[$g]) { $running += @(Get-Process -Name $pn -ErrorAction SilentlyContinue) }
}
$running = @($running | Where-Object { $_ })
if ($running.Count -gt 0) {
    Write-Host ''
    Write-Host ('  [3/3] 检测到 {0} 个应用正在运行,将自动关闭:' -f $running.Count) -ForegroundColor Yellow
    Write-Host ('  ' + ('-' * 50)) -ForegroundColor DarkGray
    foreach ($p in ($running | Sort-Object ProcessName -Unique)) {
        $mem = [double]0
        try { $mem = $p.WorkingSet64 } catch {}
        Write-Host ('    {0,-20} PID {1,-6} {2}' -f $p.ProcessName, $p.Id, (Format-Size $mem)) -ForegroundColor DarkGray
    }
    Write-Host ''
    Write-Host '  按回车键关闭上述程序并开始清理, ESC 键取消' -ForegroundColor Cyan
    $key2 = $null
    if ($env:EBOX_AUTO -ne '1') {
        try { $key2 = [Console]::ReadKey($true) } catch { $null = Read-Host '按回车键继续...' }
        if (($null -ne $key2) -and ($key2.Key -eq [ConsoleKey]::Escape)) {
            Write-Host '  已取消,未做任何修改。' -ForegroundColor Yellow
            Read-Host '  按回车键退出'
            exit
        }
    }
    foreach ($p in $running) { try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {} }
    Start-Sleep -Seconds 2
}

# ---------------- 执行清理 ----------------
Write-Host ''
Write-Host '  开始清理...' -ForegroundColor Cyan
$driveFree = @{}
foreach ($dv in $drives) { $driveFree[$dv] = (Get-PSDrive -Name $dv -ErrorAction SilentlyContinue).Free }
$totalFreed = [double]0
$totalFail = 0
$report = New-Object System.Collections.ArrayList

$selected = @($targets | Where-Object { $_.Selected })
$recycleTargets = @($selected | Where-Object { $_.Mode -eq 'RecycleBin' })
$cleanTargets = @($selected | Where-Object { $_.Mode -ne 'RecycleBin' })
$ti = 0

# 回收站目标串行处理(需按盘符调用 Clear-RecycleBin,数量少,串行即可保证安全)
foreach ($t in $recycleTargets) {
    $ti++
    Write-Host -NoNewline ('    [{0}/{1}] 清理 [{2}] {3} ... ' -f $ti, $selected.Count, $t.Group, $t.Name)
    $fail = 0
    $freed = [double]0
    foreach ($p in $t.Paths) {
        $drvL = $p.Substring(0,1)
        try { Clear-RecycleBin -DriveLetter $drvL -Force -ErrorAction Stop } catch {}
        $fail += Remove-DirContents $p
    }
    if ($fail -eq 0) { $freed = $t.Size } else {
        $left = [double]0
        foreach ($p in $t.Paths) { if (Test-Path -LiteralPath $p) { $left += Get-DirSize $p } }
        $freed = [double]($t.Size - $left); if ($freed -lt 0) { $freed = [double]0 }
    }
    $totalFreed += $freed
    $totalFail += $fail
    [void]$report.Add((New-Obj @{ Group=$t.Group; Name=$t.Name; Freed=$freed; Fail=$fail }))
    if ($fail -gt 0) {
        Write-Host ('释放 ' + (Format-Size $freed) + ',有 ' + $fail + ' 项被占用未删除') -ForegroundColor Yellow
    } else {
        Write-Host ('已释放 ' + (Format-Size $freed)) -ForegroundColor Green
    }
}

# 其余目标并行清理(每个目标内部串行处理其路径,不同目标之间互不依赖,可安全并发)
if ($cleanTargets.Count -gt 0) {
    $cleanWorker = {
        param($t)
        $fail = 0
        $freed = [double]0
        if ($t.Mode -eq 'Files') {
            foreach ($f in $t.Files) {
                try {
                    try { [System.IO.File]::SetAttributes($f.FullName, [System.IO.FileAttributes]::Normal) } catch {}
                    [System.IO.File]::Delete($f.FullName)
                    $freed += $f.Length
                } catch { $fail++ }
            }
        }
        else {
            foreach ($p in $t.Paths) { $fail += Remove-DirContents $p }
            if (($fail -gt 0) -and $t.TakeOwn) {
                foreach ($p in $t.Paths) {
                    if (Test-Path -LiteralPath $p) {
                        try { & takeown.exe /F $p /R /D Y 2>$null | Out-Null } catch {}
                        try { & icacls.exe $p /grant '*S-1-5-32-544:F' /T /C /Q 2>$null | Out-Null } catch {}
                    }
                }
                $fail = 0
                foreach ($p in $t.Paths) { $fail += Remove-DirContents $p }
            }
            if ($t.TakeOwn) {
                foreach ($p in $t.Paths) {
                    try { if (Test-Path -LiteralPath $p) { [System.IO.Directory]::Delete($p, $true) } } catch {}
                }
            }
            foreach ($f in $t.Files) {
                try {
                    try { [System.IO.File]::SetAttributes($f.FullName, [System.IO.FileAttributes]::Normal) } catch {}
                    [System.IO.File]::Delete($f.FullName)
                } catch { $fail++ }
            }
            if ($fail -eq 0) {
                $freed = $t.Size
            } else {
                $left = [double]0
                foreach ($p in $t.Paths) { if (Test-Path -LiteralPath $p) { $left += Get-DirSize $p } }
                foreach ($f in $t.Files) { try { if (Test-Path -LiteralPath $f.FullName) { $left += $f.Length } } catch {} }
                $freed = [double]($t.Size - $left); if ($freed -lt 0) { $freed = [double]0 }
            }
        }
        return @{ Freed = $freed; Fail = $fail }
    }

    $cleanResults = @(Invoke-Parallel -InputObjects $cleanTargets -ScriptBlock $cleanWorker -FunctionNames @('Remove-DirContents','Get-DirSize') -Throttle $parallelDegree -ProgressActivity '正在清理')
    for ($i = 0; $i -lt $cleanTargets.Count; $i++) {
        $t = $cleanTargets[$i]
        $r = $cleanResults[$i]
        $freed = [double]$r['Freed']
        $fail = [int]$r['Fail']
        $ti++
        $totalFreed += $freed
        $totalFail += $fail
        [void]$report.Add((New-Obj @{ Group=$t.Group; Name=$t.Name; Freed=$freed; Fail=$fail }))
        if ($fail -gt 0) {
            Write-Host ('    [{0}/{1}] 清理 [{2}] {3}: 释放 {4},有 {5} 项被占用未删除' -f $ti, $selected.Count, $t.Group, $t.Name, (Format-Size $freed), $fail) -ForegroundColor Yellow
        } else {
            Write-Host ('    [{0}/{1}] 清理 [{2}] {3}: 已释放 {4}' -f $ti, $selected.Count, $t.Group, $t.Name, (Format-Size $freed)) -ForegroundColor Green
        }
    }
}

# ---------------- 结果报告 ----------------
Write-Host ''
Write-Host '  ==================== 清理结果汇总 ====================' -ForegroundColor Cyan
foreach ($r in ($report | Sort-Object Freed -Descending)) {
    $line = '  ' + (Pad-Cjk ('[' + $r.Group + '] ' + $r.Name) 42) + ('{0,12}' -f (Format-Size $r.Freed))
    if ($r.Fail -gt 0) { $line += ('   (' + $r.Fail + ' 项被占用)') }
    Write-Host $line
}
Write-Host ('  ' + ('-' * 60)) -ForegroundColor DarkGray
Write-Host ('  本次共释放空间: ' + (Format-Size $totalFreed)) -ForegroundColor Green
foreach ($dv in $drives) {
    $before = $driveFree[$dv]
    $after = (Get-PSDrive -Name $dv -ErrorAction SilentlyContinue).Free
    if (($null -ne $before) -and ($null -ne $after)) {
        Write-Host ('  {0} 盘可用空间: {1}  ->  {2}' -f $dv, (Format-Size $before), (Format-Size $after))
    }
}
if ($totalFail -gt 0) {
    Write-Host ('  有 ' + $totalFail + ' 个项目被程序占用未能删除,重启电脑后再次运行即可。') -ForegroundColor Yellow
}
Write-Host ''
Read-Host '  按回车键退出'
exit
