@echo off
rem ============================================================
rem  All-Drive Junk Cleaner (全盘垃圾清理工具 v2.2)
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
try { $Host.UI.RawUI.WindowTitle = '全盘垃圾清理工具 v2.3' } catch {}

# ---------------- 工具函数 (全部兼容 PowerShell 2.0) ----------------
function Format-Size {
    param([double]$b)
    if ($b -ge 1GB) { return ('{0:N2} GB' -f ($b / 1GB)) }
    if ($b -ge 1MB) { return ('{0:N1} MB' -f ($b / 1MB)) }
    if ($b -ge 1KB) { return ('{0:N0} KB' -f ($b / 1KB)) }
    return ('{0:N0} B' -f $b)
}

function Width-Cjk {
    # 计算字符串在控制台的显示宽度(中文等全角字符按 2 列, ASCII 按 1 列)。
    # 这是修复"重影"的关键: 控制台按显示宽折行, 若按字符数截断/补齐, 含中文的行
    # 实际显示宽会超过窗口宽 → 折行 → 光标定位行号错位 → 重绘残留叠加(重影)。
    param([string]$s)
    if (-not $s) { return 0 }
    $w = 0
    foreach ($ch in $s.ToCharArray()) { if ([int]$ch -gt 255) { $w += 2 } else { $w += 1 } }
    return $w
}

function Fit-Cjk {
    # 按显示宽截断字符串到 $w 列(超宽时截断并加 ".." 占位), 保证恰好占 1 物理行不折行。
    param([string]$s, [int]$w)
    if (-not $s) { $s = '' }
    if ((Width-Cjk $s) -le $w) { return $s }
    if ($w -le 2) { return ('.' * [Math]::Max(0, $w)) }
    $acc = 0
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $s.ToCharArray()) {
        $cw = 2; if ([int]$ch -le 255) { $cw = 1 }
        if (($acc + $cw) -gt ($w - 2)) { break }   # 预留 2 列给省略号
        [void]$sb.Append($ch)
        $acc += $cw
    }
    [void]$sb.Append('..')
    return $sb.ToString()
}

function Pad-Cjk {
    # 按显示宽右侧补空格到 $w 列; 超宽时截断(Fit-Cjk), 保证输出恰好占 $w 显示列。
    param([string]$s, [int]$w)
    if (-not $s) { $s = '' }
    $s = Fit-Cjk $s $w
    $len = Width-Cjk $s
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

function Test-ConsoleUsable {
    # 检测是否有可用控制台句柄(输出被重定向/管道捕获时句柄无效,Console API 会抛异常)。
    # 无控制台时进度绘制整体跳过,避免异常;真实交互窗口正常显示。
    if ($null -ne $script:ConsoleUsable) { return $script:ConsoleUsable }
    $script:ConsoleUsable = $false
    try { [void][Console]::WindowWidth; $script:ConsoleUsable = $true } catch {}
    return $script:ConsoleUsable
}

function Draw-Bar {
    # 生成 ASCII 进度条(不依赖 Write-Progress,避免其文本重叠 bug)
    param([int]$Pct, [int]$Width = 20)
    $p = $Pct; if ($p -gt 100) { $p = 100 }; if ($p -lt 0) { $p = 0 }
    $filled = [int]([double]$p / 100 * $Width)
    return (('#' * $filled) + ('-' * ($Width - $filled)))
}

function Draw-ProgressBars {
    # 自绘进度区: 用控制台光标定位整块重绘。每次先覆盖上次绘制的所有行,再逐行整宽填充写新内容,
    # 行数变化/文字变短都不会残留旧字符,彻底解决重叠(替代 Write-Progress)。
    # 首次调用记录起点行与开始时间; 每槽一行 @{Label;Count;Found;Current;Done;Total};
    # 末尾总进度行附带实时扫描速度。Clear-Progress 用空格清除并复位光标。
    param([string]$Title, $Slots, [int]$Done, [int]$Total)
    if (-not (Test-ConsoleUsable)) { return }
    if ($null -eq $script:ProgressTop) {
        try { $script:ProgressTop = [Console]::CursorTop } catch { $script:ProgressTop = 0 }
        $script:ProgressStart = [DateTime]::UtcNow
        $script:ProgressPrevDone = -1
        $script:ProgressLastLines = 0
    }
    $w = 79
    try { $w = [Console]::WindowWidth - 1 } catch {}
    if ($w -lt 40) { $w = 40 }
    $row = [int]$script:ProgressTop
    $lines = New-Object System.Collections.ArrayList
    if ($Title) { [void]$lines.Add(('  ' + $Title)) }
    $totProc = 0
    if ($Slots) {
        $shown = 0
        foreach ($s in $Slots) {
            if ($shown -ge 10) { break }
            $bar = ''
            $pctTxt = '  --'
            $pct = -1
            if ($null -ne $s.DoneFlags) {
                # 整盘百分比 = 已完成分片 / 总分片(全部完成才是 100%,避免根层快分片提前显示 100%)
                $tot = 0; $dn = 0
                foreach ($f in @($s.DoneFlags)) { $tot++; if ($f) { $dn++ } }
                if ($tot -gt 0) { $pct = [int](100 * $dn / $tot) }
            } elseif ($s.Done) { $pct = 100 }
            elseif ($s.Total -gt 0) { $pct = [int](100 * [double]$s.Count / $s.Total) }
            if ($pct -ge 0) { $bar = Draw-Bar $pct 16; $pctTxt = ('{0,3}%' -f $pct) }
            $cur = [string]$s.Current
            if ($cur.Length -gt 44) { $cur = '..' + $cur.Substring($cur.Length - 42) }
            if ($bar) { $bar = $bar + ' ' }
            if (($null -ne $s.Mode) -and ($s.Mode -eq 'size')) {
                # 统计大小型槽: Found=已统计 MB, 显示当前扫描路径
                [void]$lines.Add(('  [{0}] {1}{2} | 已统计 {3} MB | {4}' -f $s.Label, $bar, $pctTxt, $s.Found, $cur))
            } else {
                [void]$lines.Add(('  [{0}] {1}{2} | 已处理 {3} | 发现 {4} | {5}' -f $s.Label, $bar, $pctTxt, $s.Count, $s.Found, $cur))
            }
            $totProc += [int]$s.Count
            $shown++
        }
    }
    $pctAll = 0
    if ($Total -gt 0) { $pctAll = [int](100 * [double]$Done / $Total) }
    $speedTxt = ''
    if (($null -ne $script:ProgressStart) -and ($totProc -gt 0)) {
        $el = ([DateTime]::UtcNow - $script:ProgressStart).TotalSeconds
        if ($el -gt 0) { $speedTxt = (' | {0} 项/秒' -f ([int]($totProc / $el))) }
    }
    [void]$lines.Add(('  总进度: {0}/{1} ({2}%){3}' -f $Done, $Total, $pctAll, $speedTxt))
    # 先覆盖上一次绘制的所有行(空白),行数变化也不会残留重叠
    try {
        for ($y = $row; $y -lt ($row + $script:ProgressLastLines); $y++) {
            [Console]::SetCursorPosition(0, $y)
            Write-Host (' ' * $w) -NoNewline
        }
    } catch {}
    $y = $row
    foreach ($ln in $lines) {
        # 按显示宽截断+补空格到 $w 列(中文占 2 列), 确保每行恰好 1 物理行不折行;
        # 否则含中文的行显示宽超窗口 → 折行 → 行号错位 → 重绘残留叠加(重影)。
        $s = Fit-Cjk ([string]$ln) $w
        try { [Console]::SetCursorPosition(0, $y) } catch { break }
        Write-Host ($s + (' ' * ([Math]::Max(0, $w - (Width-Cjk $s))))) -NoNewline
        $y++
    }
    $script:ProgressLastLines = $lines.Count
    $script:ProgressEnd = $y - 1
}

function Set-Progress {
    # 自绘单行/两行总进度(替代 Write-Progress)。标题绘制在首行(ProgressTop), 状态行在其下方;
    # 每行整宽填充, 文字变短也不残留; Clear-Progress 从 ProgressTop 起整块清除(含标题行)。
    param([string]$Status, [int]$Percent = -1, [string]$Title = '')
    if (-not (Test-ConsoleUsable)) { return }
    if ($null -eq $script:ProgressTop) {
        try { $script:ProgressTop = [Console]::CursorTop } catch { $script:ProgressTop = 0 }
    }
    $w = 79; try { $w = [Console]::WindowWidth - 1 } catch {}
    if ($w -lt 40) { $w = 40 }
    $row = [int]$script:ProgressTop
    if ($Title) {
        # 按显示宽截断/补齐(中文占 2 列), 防止含中文的行折行导致光标行号错位(重影)
        $t = Fit-Cjk ('  ' + $Title) $w
        try { [Console]::SetCursorPosition(0, $row) } catch {}
        Write-Host ($t + (' ' * ([Math]::Max(0, $w - (Width-Cjk $t))))) -NoNewline
        $row++
    }
    $bar = Draw-Bar $Percent 20
    $st = [string]$Status
    $line = Fit-Cjk ('  {0} {1,3}%  {2}' -f $bar, $Percent, $st) $w
    try { [Console]::SetCursorPosition(0, $row) } catch {}
    Write-Host ($line + (' ' * ([Math]::Max(0, $w - (Width-Cjk $line))))) -NoNewline
    $script:ProgressEnd = $row
}

function Clear-Progress {
    # 清除自绘进度区并复位光标(含标题行,整块清除)
    if (-not (Test-ConsoleUsable)) {
        $script:ProgressTop = $null; $script:ProgressEnd = $null
        $script:ProgressLastLines = 0; $script:ProgressStart = $null; $script:ProgressPrevDone = -1
        return
    }
    if ($null -eq $script:ProgressTop) { return }
    $startY = [int]$script:ProgressTop
    $endY = [int]$script:ProgressEnd
    $w = 79; try { $w = [Console]::WindowWidth } catch {}
    for ($y = $startY; $y -le $endY; $y++) {
        try { [Console]::SetCursorPosition(0, $y) } catch { continue }
        Write-Host ((' ' * $w)) -NoNewline
    }
    try { [Console]::SetCursorPosition(0, $startY) } catch {}
    $script:ProgressTop = $null
    $script:ProgressEnd = $null
    $script:ProgressLastLines = 0
    $script:ProgressStart = $null
    $script:ProgressPrevDone = -1
}

function Test-IsUnder {
    # 判断 Path 是否等于或位于 Bases 中任一目录之下(不区分大小写),用于跨运行空间并行时的 eBox 环境保护。
    # 两侧都经 GetFullPath 规范化,避免大小写/尾部分隔符/8.3 短名差异导致误判。
    param([string]$Path, [string[]]$Bases)
    if (-not $Path) { return $false }
    $p = ''
    try { $p = [System.IO.Path]::GetFullPath($Path) } catch { $p = $Path }
    foreach ($b in $Bases) {
        if (-not $b) { continue }
        $bb = ''
        try { $bb = [System.IO.Path]::GetFullPath($b) } catch { $bb = $b }
        if ($p -ieq $bb) { return $true }
        if ($p.StartsWith(($bb + '\'), [StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Invoke-Parallel {
    # PS2.0 兼容的多线程并行执行器(基于 RunspacePool)。
    # 将 FunctionNames 中列出的函数注入工作运行空间后,对每个 InputObject 并发调用 ScriptBlock。
    # 结果按输入顺序返回;主线程轮询等待,可选 -Counter 共享计数器实现实时进度(不阻塞在单个任务上)。
    param(
        [object[]]$InputObjects,
        [scriptblock]$ScriptBlock,
        [int]$Throttle = 4,
        [string]$ProgressActivity = '',
        [string[]]$FunctionNames = @(),
        [int[]]$Counter = $null,
        [int]$RefreshMs = 250,
        [object]$ProgressSlots = $null
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
            if ($null -ne $Counter) { [void]$ps.AddArgument($Counter) }
            $jobs += ,@($ps, $ps.BeginInvoke())
        }
        $doneFlags = New-Object 'bool[]' $list.Count
        $resultsArr = New-Object 'object[]' $list.Count
        $done = 0
        while ($done -lt $list.Count) {
            for ($i = 0; $i -lt $list.Count; $i++) {
                if ($doneFlags[$i]) { continue }
                if ($jobs[$i][1].IsCompleted) {
                    try { $out = $jobs[$i][0].EndInvoke($jobs[$i][1]) } catch { $out = @() }
                    $resultsArr[$i] = @($out)
                    $jobs[$i][0].Dispose()
                    $doneFlags[$i] = $true
                    $done++
                }
            }
            if ($ProgressActivity) {
                if ($null -ne $ProgressSlots) {
                    Draw-ProgressBars -Title $ProgressActivity -Slots $ProgressSlots -Done $done -Total $list.Count
                } else {
                    $pct = 0
                    if ($list.Count -gt 0) { $pct = [int](100 * $done / $list.Count) }
                    Set-Progress -Status ('{0}/{1}' -f $done, $list.Count) -Percent $pct -Title $ProgressActivity
                }
            }
            if ($done -lt $list.Count) { Start-Sleep -Milliseconds $RefreshMs }
        }
        $results = New-Object System.Collections.ArrayList
        foreach ($r in $resultsArr) { foreach ($o in @($r)) { [void]$results.Add($o) } }
        if ($ProgressActivity) { Clear-Progress }
        # 直接返回扁平结果数组(不要用一元逗号包裹,否则调用处 @() 会得到"1 个元素=整个结果数组",
        # 导致 $scanResAll[$idx] 只能取到 [0], 其余为 null, 扫描结果解析为空)
        return $results.ToArray()
    } finally {
        $pool.Close()
    }
}

function Get-DirSize {
    # 可选 $Slot: 传入进度槽(hashtable)时,每遍历 200 个目录节流刷新槽的 Current(当前路径),
    # 让"正在统计大小"这类长耗时阶段画面持续有动静,避免被误判为卡死。不传时行为与旧版一致。
    param([string]$p, $Slot = $null)
    $sum = [double]0
    $n = 0
    $stack = New-Object System.Collections.Stack
    $stack.Push($p)
    while ($stack.Count -gt 0) {
        $d = [string]$stack.Pop()
        $n++
        if (($null -ne $Slot) -and (($n % 200) -eq 0)) { $Slot.Current = $d }
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

function New-ScanSlices {
    # 将磁盘根目录切成扫描片: 根片(只扫根层) + 各非系统一级子目录片(完整 DFS)。
    # 目的: 每个片是一个独立任务, 可被多个 Runspace 并行扫描, 避免"整盘单线程"拖慢。
    param([string]$Base, [string[]]$IgnoreBases)
    $sysTop = @('$recycle.bin','system volume information','windows','program files','program files (x86)',
        'programdata','boot','recovery','$windows.~bt','$windows.~ws','$getcurrent','esd','windows.old','perflogs')
    $rootNames = @('WeChat Files','xwechat_files','WXWork','Tencent Files','DingTalk','DingtalkData')
    $junkDirNames = @('cache','caches','temp','tmp','logs','log','crashpad','crashreports','crashdumps',
        'minidumps','cache_data','code cache','gpucache','media cache','inetcache','cacheddata',
        'cachedprofilesdata','cachedextensions','cachedextensionvsixs','squirreltemp','cache2','shadercache',
        'grshadercache','dawncache','service worker','serviceworkercachestorage','update cache','updatecache')
    $slices = New-Object System.Collections.ArrayList
    [void]$slices.Add(@{ Base = $Base; MaxDepth = 1 })   # 根片: 只扫根层,判定直接子目录
    try {
        $rootDi = New-Object System.IO.DirectoryInfo($Base)
        foreach ($sub in $rootDi.GetDirectories()) {
            try { if ($sub.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
            $sn = $sub.Name.ToLower()
            if ($sysTop -contains $sn) { continue }
            if ($rootNames -contains $sub.Name) { continue }   # 应用数据根由根片识别,不必再深扫
            if ($junkDirNames -contains $sn) { continue }      # 垃圾目录由根片识别,不必再深扫
            if (Test-IsUnder $sub.FullName $IgnoreBases) { continue }
            [void]$slices.Add(@{ Base = $sub.FullName; MaxDepth = 10 })
        }
    } catch {}
    return $slices.ToArray()
}

function Scan-DriveSlice {
    # 一次遍历同时收集: 应用数据根 / 缓存垃圾目录 / 散落垃圾文件 / 虚拟机目录。
    # 替代旧版 Scan-AppRoots + Scan-JunkDirs + Scan-JunkFiles 的三遍整盘重复遍历, 大幅提速。
    # -Slot: 共享进度槽(hashtable 引用)。同一盘的多个分片共享一个槽,这里按"累计增量"写入
    # (每次把本片新增的处理数累加到槽上), 这样进度数字只会持续上涨,不会因多片回写而回跳。
    # 每 100 个目录节流更新一次, 避免多线程写共享内存的缓存竞争, 同时保证进度条实时有动静。
    param([string]$Base, [int]$MaxDepth, [string[]]$IgnoreBases, [hashtable]$Slot = $null, [int]$SlotIdx = -1)
    $rootNames = @('WeChat Files','xwechat_files','WXWork','Tencent Files','DingTalk','DingtalkData')
    $junkDirNames = @('cache','caches','temp','tmp','logs','log','crashpad','crashreports','crashdumps',
        'minidumps','cache_data','code cache','gpucache','media cache','inetcache','cacheddata',
        'cachedprofilesdata','cachedextensions','cachedextensionvsixs','squirreltemp','cache2','shadercache',
        'grshadercache','dawncache','service worker','serviceworkercachestorage','update cache','updatecache')
    $skipDirs = @('$recycle.bin','system volume information','windows','winsxs','node_modules','.git',
        'program files','program files (x86)','programdata','boot','system32','syswow64','recovery',
        'system','assembly','found.000')
    $junkExt = @('.tmp','.temp','.dmp','.chk','.gid','.crdownload','.partial')
    $junkNames = @('thumbs.db')
    $roots = New-Object System.Collections.ArrayList
    $junk = New-Object System.Collections.ArrayList
    $files = New-Object System.Collections.ArrayList
    $vmx = New-Object System.Collections.ArrayList
    $n = 0
    $foundLocal = 0
    $lastThrottle = 0
    $slotFoundLast = 0
    $stack = New-Object System.Collections.Stack
    $stack.Push(@($Base, 0))
    while ($stack.Count -gt 0) {
        $cur = $stack.Pop()
        $dir = [string]$cur[0]; $depth = [int]$cur[1]
        if (($depth -ge $MaxDepth) -and ($depth -gt 0)) { continue }
        $n++
        if (($null -ne $Slot) -and (($n - $lastThrottle) -ge 100)) {
            $Slot.Count = [int]$Slot.Count + ($n - $lastThrottle)
            $lastThrottle = $n
            if (($foundLocal - $slotFoundLast) -gt 0) { $Slot.Found = [int]$Slot.Found + ($foundLocal - $slotFoundLast); $slotFoundLast = $foundLocal }
            $Slot.Current = $dir
        }
        $di = $null
        try { $di = New-Object System.IO.DirectoryInfo($dir) } catch { continue }
        $hasVm = $false
        try {
            foreach ($sub in $di.GetDirectories()) {
                try { if ($sub.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
                if (Test-IsUnder $sub.FullName $IgnoreBases) { continue }
                $sn = $sub.Name.ToLower()
                if ($skipDirs -contains $sn) { continue }
                if ($rootNames -contains $sub.Name) { [void]$roots.Add($sub.FullName); $foundLocal++; continue }
                if ($junkDirNames -contains $sn) { [void]$junk.Add($sub.FullName); $foundLocal++; continue }
                if (($depth + 1) -lt $MaxDepth) { $stack.Push(@($sub.FullName, ($depth + 1))) }
            }
        } catch {}
        try {
            foreach ($f in $di.GetFiles()) {
                $ext = $f.Extension.ToLower()
                if (($junkExt -contains $ext) -or ($junkNames -contains $f.Name.ToLower())) { [void]$files.Add($f); $foundLocal++ }
                if (($ext -eq '.vmx') -or ($ext -eq '.vbox')) { $hasVm = $true }
            }
        } catch {}
        if ($hasVm) { [void]$vmx.Add($di.FullName) }
    }
    if ($null -ne $Slot) {
        if ($n -gt $lastThrottle) { $Slot.Count = [int]$Slot.Count + ($n - $lastThrottle) }
        if (($foundLocal - $slotFoundLast) -gt 0) { $Slot.Found = [int]$Slot.Found + ($foundLocal - $slotFoundLast) }
        # 只标记本分片完成; 整盘百分比由 Draw-ProgressBars 汇总全部 DoneFlags 计算,
        # 避免"第一个快分片(根层)完成就把整盘显示成 100%"的错误。
        if (($SlotIdx -ge 0) -and ($null -ne $Slot.DoneFlags)) {
            try { $Slot.DoneFlags[$SlotIdx] = $true } catch {}
        }
        $Slot.Current = ''
    }
    return @{ Roots = $roots.ToArray(); JunkDirs = $junk.ToArray(); Files = $files.ToArray(); VmxDirs = $vmx.ToArray() }
}

function Remove-DirContents {
    # 删除目录内所有内容但保留目录本身。
    # 提速: 用 robocopy /MIR 空目录镜像清空(robocopy 原生多线程,远快于逐文件串行删除)。
    # 被占用的文件会保留, 再用 Remove-Item 兜底删除一次。
    param([string]$dir)
    $fail = 0
    if (-not $dir) { return 0 }
    $d = $dir.TrimEnd('\')
    if ($d.Length -le 3) { return 0 }
    if (-not (Test-Path -LiteralPath $dir)) { return 0 }
    $empty = Join-Path $env:TEMP ('ebox_empty_' + [System.Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $empty -Force -ErrorAction SilentlyContinue | Out-Null
        & robocopy.exe $empty $dir /MIR /R:1 /W:1 /NFL /NDL /NJH /NJS /NP 2>$null | Out-Null
        $rc = $LASTEXITCODE
        if ($rc -ge 8) { $fail = 1 }
    } catch { $fail++ }
    finally {
        Remove-Item -LiteralPath $empty -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $dir) {
        $items = @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)
        foreach ($it in $items) {
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
Write-Host '          全 盘 垃 圾 清 理 工 具  v2.2' -ForegroundColor Cyan
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
        # 提权重启时保留 /auto(由 eBox 应用内确认框替代回车确认,避免二次确认)
        $argLine = '/c "{0}"' -f $env:SELF
        if ($env:EBOX_AUTO -eq '1') { $argLine = '/c "{0}" /auto' -f $env:SELF }
        Start-Process -FilePath 'cmd.exe' -ArgumentList $argLine -Verb RunAs -ErrorAction Stop
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
$parallelDegree = 8
try {
    $cpu = [int]$env:NUMBER_OF_PROCESSORS
    if ($cpu -gt 0) {
        if ($cpu -gt 8) { $parallelDegree = [Math]::Min(12, $cpu) } else { $parallelDegree = $cpu }
    }
} catch {}
$scanWorker = {
    param($task)
    return (Scan-DriveSlice -Base $task['Base'] -MaxDepth $task['MaxDepth'] -IgnoreBases $task['IgnoreBases'] -Slot $task['Slot'] -SlotIdx $task['SlotIdx'])
}
$scanFns = @('Scan-DriveSlice', 'Test-IsUnder')

# ---- 并行扫描: 按磁盘子目录分片,多线程并行(应用根/缓存目录/垃圾文件/虚拟机 一次遍历) ----
$scanDriveList = New-Object System.Collections.ArrayList
$scanSliceCounts = New-Object System.Collections.ArrayList
$scanSlots = New-Object System.Collections.ArrayList
$scanTasks = New-Object System.Collections.ArrayList
foreach ($dv in $drives) {
    $scanBase = "$dv`:\"
    if (-not (Test-Path -LiteralPath $scanBase)) {
        Write-Host ('        {0} 盘不可访问,跳过。' -f $dv) -ForegroundColor DarkGray
        continue
    }
    [void]$scanDriveList.Add($dv)
    # 每盘一个共享进度槽(该盘所有分片任务都写入它),进度条按盘显示一行
    # DoneFlags: 每分片一格, 分片完成置 true; 整盘百分比=已完成分片/总分片, 全部完成才 100%
    $slices = @(New-ScanSlices -Base $scanBase -IgnoreBases $envBases)
    $doneFlags = New-Object 'object[]' $slices.Count
    for ($k = 0; $k -lt $slices.Count; $k++) { $doneFlags[$k] = $false }
    $slot = @{ Label = $dv; Count = 0; Found = 0; Current = ''; Done = $false; Total = 0; DoneFlags = $doneFlags }
    [void]$scanSlots.Add($slot)
    [void]$scanSliceCounts.Add($slices.Count)
    $sid = 0
    foreach ($s in $slices) {
        [void]$scanTasks.Add(@{ Base = $s['Base']; MaxDepth = $s['MaxDepth']; IgnoreBases = $envBases; Slot = $slot; SlotIdx = $sid })
        $sid++
    }
}

$scanResAll = @()
if ($scanTasks.Count -gt 0) {
    Write-Host ''
    Write-Host ('  正在并行扫描 {0} 个磁盘(大磁盘需数分钟,请耐心等待)...' -f $scanDriveList.Count) -ForegroundColor Cyan
    $scanResAll = @(Invoke-Parallel -InputObjects @($scanTasks) -ScriptBlock $scanWorker -FunctionNames $scanFns -Throttle $parallelDegree -ProgressActivity '正在扫描所有磁盘' -ProgressSlots @($scanSlots) -RefreshMs 200)
}

# ---- 逐盘处理扫描结果(顺序执行,开销小) ----
$scanCount = 0
$idx = 0
for ($di = 0; $di -lt $scanDriveList.Count; $di++) {
    $dv = $scanDriveList[$di]
    $scanBase = "$dv`:\"
    $scanCount++
    $sliceCount = $scanSliceCounts[$di]
    Write-Host ''
    Write-Host ('  ------ 扫描 {0} 盘 ------  [{1}/{2}]' -f $dv, $scanCount, $scanDriveList.Count) -ForegroundColor DarkYellow

    # ---- 通用: 根目录临时文件夹 ----
    foreach ($p in @("$dv`:\Temp", "$dv`:\Tmp", "$dv`:\Cache")) { Add-Cat 'root_temp' $p }

    # ---- 通用: 回收站 ----
    $rb = "$dv`:\`$RECYCLE.BIN"
    if (Test-Path -LiteralPath $rb) { Add-Cat 'recycle' $rb }

    # 汇总该盘所有分片结果
    $roots = New-Object System.Collections.ArrayList
    $junkDirs = New-Object System.Collections.ArrayList
    $junkFiles = New-Object System.Collections.ArrayList
    for ($s = 0; $s -lt $sliceCount; $s++) {
        $res = $scanResAll[$idx]; $idx++
        foreach ($r in @($res['Roots'])) { [void]$roots.Add($r) }
        foreach ($jd in @($res['JunkDirs'])) { [void]$junkDirs.Add($jd) }
        foreach ($jf in @($res['Files'])) { [void]$junkFiles.Add($jf) }
        foreach ($v in @($res['VmxDirs'])) { [void]$global:QJVmxDirs.Add($v) }
    }
    $roots = @($roots | Sort-Object -Unique)
    $junkDirs = @($junkDirs | Sort-Object -Unique)
    if ($roots.Count -gt 0) {
        Write-Host ('        应用数据根目录 {0} 个:' -f $roots.Count) -ForegroundColor DarkGray
        foreach ($r in $roots) { Write-Host ('          - ' + $r) -ForegroundColor DarkGray }
    }

    Add-AppRootCategories $roots

    if ($junkDirs.Count -gt 0) {
        Write-Host ('        缓存/垃圾目录 {0} 个:' -f $junkDirs.Count) -ForegroundColor DarkGray
        foreach ($jd in $junkDirs) { Write-Host ('          - ' + $jd) -ForegroundColor DarkGray }
        foreach ($jd in $junkDirs) { Add-Cat 'disk_cache' $jd }
    }

    if ($junkFiles.Count -gt 0) {
        Write-Host ('        临时/垃圾文件 {0} 个:' -f $junkFiles.Count) -ForegroundColor DarkGray
        $shown = 0
        foreach ($jf in $junkFiles) {
            if ($shown -lt 5) { Write-Host ('          - ' + $jf.FullName) -ForegroundColor DarkGray; $shown++ }
        }
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
    Clear-Progress
}

# ---------------- eBox 多开环境数据: 缓存 + 聊天记录(按环境分组) ----------------
# 白名单与应用"环境清理"按钮完全一致, 且严格限制为垃圾目录:
#  缓存: qtCef/WXWorkCefCache/着色器/浏览器指标/字典 等整目录;
#        Default profile 下的 Cache/Code Cache/GPU缓存/Service Worker 等子缓存
#  聊天记录: 各数字企业目录下的 Data 消息库 / Index 搜索索引
# 绝不触碰(以下任何一项都不会被加入清理列表):
#  注册表 hive / Cookies / Local Storage / Preferences / 企业配置 / 设备指纹 / 登录状态
# 未安装 eBox 或没有对应环境目录时自动跳过。
$global:EBXEnvList = New-Object System.Collections.ArrayList

function Add-EnvWxworkCaches {
    # 白名单收集某个 wxwork 目录内的缓存/聊天记录目录到输出容器(只收集路径,不删任何文件)
    param([string]$WxDir, $CacheOut, $ChatOut)
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
            if ($whole -contains $sn) { [void]$CacheOut.Add($s.FullName); continue }
            if ($sn -eq 'default') {
                $ds = $null
                try { $ds = (New-Object System.IO.DirectoryInfo($s.FullName)).GetDirectories() } catch { continue }
                foreach ($d in $ds) {
                    $dn = $d.Name.ToLower()
                    if ($defSub -contains $dn) { [void]$CacheOut.Add($d.FullName) }
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
                    if (($dn -eq 'data') -or ($dn -eq 'index')) { [void]$ChatOut.Add($d.FullName) }
                }
            }
        }
    }
}

function Invoke-RegWithTimeout {
    # 带超时执行 reg.exe 子命令, 防止 hive 被占用/杀软拦截时 reg.exe 挂起导致整个扫描"卡死"。
    # 返回: @{ ExitCode; Output(行数组,仅查询时有值) }; 超时强杀进程并返回 ExitCode=-1。
    # (原实现 & reg.exe 同步等待无超时; PowerShell 对挂起的外部进程无法解除等待)
    param([string]$ArgumentLine, [int]$TimeoutMs = 15000, [bool]$Capture = $false)
    $outFile = $null; $errFile = $null
    $p = $null
    try {
        if ($Capture) {
            $outFile = [IO.Path]::GetTempFileName()
            $errFile = [IO.Path]::GetTempFileName()
            $p = Start-Process -FilePath 'reg.exe' -ArgumentList $ArgumentLine -NoNewWindow -PassThru `
                -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        } else {
            $p = Start-Process -FilePath 'reg.exe' -ArgumentList $ArgumentLine -NoNewWindow -PassThru
        }
        try { $null = $p.Handle } catch {}
        # 关键: 立即缓存进程句柄。否则进程快速退出后句柄未建立, WaitForExit 返回 true 但 ExitCode 读出为空,
        # load 会被误判失败(环境名全部降级)。已实测 PS5.1 复现并验证此修复。
        if (-not $p.WaitForExit($TimeoutMs)) {
            try { $p.Kill() } catch {}
            try { $p.WaitForExit(3000) } catch {}
            return @{ ExitCode = -1; Output = @() }   # -1 = 超时
        }
        $code = $p.ExitCode
        $lines = @()
        if ($Capture -and (Test-Path -LiteralPath $outFile)) {
            # 控制台代码页 65001 下 reg.exe 重定向输出为 UTF-8, 与原 [Console]::OutputEncoding 方案一致
            try { $lines = @(Get-Content -LiteralPath $outFile -Encoding UTF8 -ErrorAction SilentlyContinue) } catch { $lines = @() }
        }
        return @{ ExitCode = $code; Output = $lines }
    } catch {
        return @{ ExitCode = -2; Output = @() }
    } finally {
        if ($null -ne $p) { try { $p.Dispose() } catch {} }
        if ($outFile) { Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue }
        if ($errFile) { Remove-Item -LiteralPath $errFile -Force -ErrorAction SilentlyContinue }
    }
}

function Get-EBoxEnvNameMap {
    # 读取 eBox 环境注册表 hive 文件(<Env>\data\eBox 或 eBox_<用户名>), 建立 目录index -> 显示名 映射。
    # eBox 改名只改显示名(Name REG_SZ), 环境目录名(index)不变; hive 内 Env\<flagName>\Index(REG_DWORD) 与目录名一致。
    # 用 reg.exe load 挂载读取(管理员权限); eBox 正在运行占用 hive 或权限不足时返回空表, 调用方降级用目录名。
    param([string]$DataDir)
    $key = [string]$DataDir
    if ($null -eq $script:EnvNameMapCache) { $script:EnvNameMapCache = @{} }
    if ($script:EnvNameMapCache.ContainsKey($key)) { return $script:EnvNameMapCache[$key] }
    $map = @{}
    if ((-not $DataDir) -or (-not (Test-Path -LiteralPath $DataDir))) { return $map }
    $hive = Join-Path $DataDir 'eBox'
    if (-not (Test-Path -LiteralPath $hive)) {
        $cands = @(Get-ChildItem -LiteralPath $DataDir -Filter 'eBox_*' -Force -ErrorAction SilentlyContinue | Where-Object { -not $_.PSIsContainer })
        if ($cands.Count -gt 0) { $hive = $cands[0].FullName } else { return $map }
    }
    $mount = 'eBoxClean' + ([Guid]::NewGuid().ToString('N').Substring(0, 8))
    try { [void](Invoke-RegWithTimeout -ArgumentLine ('delete "HKU\' + $mount + '" /f') -TimeoutMs 5000) } catch {}
    $loaded = $false
    try {
        # load 挂载带超时(15s): eBox 正在运行占用 hive 时正常立即失败; 杀软拦截/损坏 hive 挂起时超时跳过
        $r = Invoke-RegWithTimeout -ArgumentLine ('load "HKU\' + $mount + '" "' + $hive + '"') -TimeoutMs 15000
        $loaded = ($r.ExitCode -eq 0)
    } catch {}
    if (-not $loaded) { $global:EBoxEnvNameReadFailed = $true; return $map }
    # 用 reg.exe query 读取(独立进程,不占用本进程注册表句柄,保证 unload 成功、不残留挂载);
    # query 带超时(60s): 超大/损坏 hive 挂起时强杀并放弃读取(降级用目录名,不影响后续清理)
    try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch {}
    try {
        $r = Invoke-RegWithTimeout -ArgumentLine ('query "HKU\' + $mount + '\Env" /s') -TimeoutMs 60000 -Capture $true
        $out = @($r.Output)
        $curIdx = -1
        foreach ($line in $out) {
            $s = [string]$line
            # 子键头(含根键)行: "HKEY_USERS\<mount>\Env[<子键>]"; 进入新块先结算上一子键
            if ($s -match '^\s*HKEY_USERS\\') {
                if (($curIdx -ge 0) -and (-not $map.ContainsKey($curIdx))) { $map[$curIdx] = '' }
                $curIdx = -1
                continue
            }
            $m = [regex]::Match($s, '^\s+(\S+)\s+(\S+)\s*(.*)$')
            if (-not $m.Success) { continue }
            $vn = $m.Groups[1].Value.ToLower()
            $vt = $m.Groups[2].Value.ToLower()
            $vd = $m.Groups[3].Value.Trim()
            if (($vt -eq 'reg_dword') -and ($vn -eq 'index')) {
                # 读到新的 Index: 上一子键若有 Index 但无 Name, 也先入 map(留空, 由调用方回退默认名)
                if (($curIdx -ge 0) -and (-not $map.ContainsKey($curIdx))) { $map[$curIdx] = '' }
                $hex = $vd
                if ($hex.StartsWith('0x')) { $hex = $hex.Substring(2) }
                try { $curIdx = [Convert]::ToInt32($hex, 16) } catch { $curIdx = -1 }
            }
            elseif (($vt -eq 'reg_sz') -and ($vn -eq 'name')) {
                if (($curIdx -ge 0) -and $vd) { $map[$curIdx] = $vd }
            }
        }
        # 结尾: 最后一个子键若有 Index 但无 Name, 同样入 map(留空)
        if (($curIdx -ge 0) -and (-not $map.ContainsKey($curIdx))) { $map[$curIdx] = '' }
    } catch {}
    finally {
        # unload 带超时+重试: 句柄延迟释放导致偶发失败, 重试 2 次仍失败时强制删除残留挂载键, 防止 hive 挂载残留
        if ($loaded) {
            $unloaded = $false
            for ($try = 0; $try -lt 3; $try++) {
                try { Start-Sleep -Milliseconds 400 } catch {}
                $r = Invoke-RegWithTimeout -ArgumentLine ('unload "HKU\' + $mount + '"') -TimeoutMs 10000
                if ($r.ExitCode -eq 0) { $unloaded = $true; break }
            }
            if (-not $unloaded) {
                try { [void](Invoke-RegWithTimeout -ArgumentLine ('delete "HKU\' + $mount + '" /f') -TimeoutMs 5000) } catch {}
            }
        }
    }
    $script:EnvNameMapCache[$key] = $map
    return $map
}

function Collect-EBoxEnvData {
    # 每个环境目录生成一条记录 @{Display;Name;Dir;Base;CacheDirs;ChatDirs;Size}; 无垃圾目录的环境自动跳过。
    # 目录名=index; 显示名优先取 hive 中的 Name(改名后的名称), 读不到时用"环境{index}";
    # Display 形如"环境 3"或"环境 工作号A (目录 3)"; 仅处理数字目录, 跳过 data 目录与 *_to_delete 残留。
    # 进度显示: 环境内是企业微信完整数据(可达数万目录), 遍历需数秒~数分钟;
    # 原版此阶段无任何输出, 控制台停留在上一阶段清单 → 用户误以为卡死。现按环境+目录计数节流刷新进度。
    # 先统计总环境数(仅枚举一级数字目录, 开销可忽略)
    $totalEnvs = 0
    foreach ($base in $global:EBoxEnvBases) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        try {
            foreach ($d in (New-Object System.IO.DirectoryInfo($base)).GetDirectories()) {
                $dn = $d.Name
                if (($dn -ne 'data') -and (-not $dn.EndsWith('_to_delete')) -and ($dn -match '^\d+$')) { $totalEnvs++ }
            }
        } catch {}
    }
    if ($totalEnvs -eq 0) { return }
    $envNo = 0
    foreach ($base in $global:EBoxEnvBases) {
        if (-not (Test-Path -LiteralPath $base)) { continue }
        $nameMap = Get-EBoxEnvNameMap -DataDir (Join-Path $base 'data')
        $envDirs = $null
        try { $envDirs = (New-Object System.IO.DirectoryInfo($base)).GetDirectories() } catch { continue }
        foreach ($envDir in $envDirs) {
            $dirName = $envDir.Name
            if ($dirName -eq 'data') { continue }
            if ($dirName.EndsWith('_to_delete')) { continue }
            if (-not ($dirName -match '^\d+$')) { continue }
            $envNo++
            Set-Progress -Title '正在收集 eBox 环境垃圾目录(环境内文件多时需几分钟)' -Status ('第 {0}/{1} 个环境(目录 {2})...' -f $envNo, $totalEnvs, $dirName) -Percent ([int](100 * ($envNo - 1) / $totalEnvs))
            $index = [int]$dirName
            $displayName = $null
            if ($nameMap.ContainsKey($index)) { $displayName = $nameMap[$index] }
            if (-not $displayName) { $displayName = ('环境' + $dirName) }
            if ($displayName -eq ('环境' + $dirName)) { $disp = ('环境 ' + $dirName) }
            else { $disp = ('环境 ' + $displayName + ' (目录 ' + $dirName + ')') }
            $cacheOut = New-Object System.Collections.ArrayList
            $chatOut = New-Object System.Collections.ArrayList
            $stack = New-Object System.Collections.Stack
            $stack.Push($envDir.FullName)
            $walked = 0
            while ($stack.Count -gt 0) {
                $dir = [string]$stack.Pop()
                $walked++
                if (($walked % 200) -eq 0) {
                    # 遍历进行中节流刷新(让进度区持续有动静, 避免大环境长时间无输出被误判卡死)
                    Set-Progress -Title '正在收集 eBox 环境垃圾目录(环境内文件多时需几分钟)' -Status ('第 {0}/{1} 个环境(目录 {2}), 已遍历 {3} 个目录...' -f $envNo, $totalEnvs, $dirName, $walked) -Percent ([int](100 * ($envNo - 1) / $totalEnvs))
                }
                $subs = $null
                try { $subs = (New-Object System.IO.DirectoryInfo($dir)).GetDirectories() } catch { continue }
                foreach ($s in $subs) {
                    try { if ($s.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { continue } } catch { continue }
                    if ($s.Name.ToLower() -eq 'wxwork') { Add-EnvWxworkCaches $s.FullName $cacheOut $chatOut; continue }
                    $stack.Push($s.FullName)
                }
            }
            if (($cacheOut.Count -gt 0) -or ($chatOut.Count -gt 0)) {
                [void]$global:EBXEnvList.Add(@{ Display = $disp; Name = $displayName; Dir = $dirName; Base = $envDir.FullName; CacheDirs = $cacheOut; ChatDirs = $chatOut; Size = [double]0 })
            }
        }
    }
    Clear-Progress
}
Collect-EBoxEnvData

# 按环境显示垃圾清单并统计大小(仅缓存+聊天记录; 没有环境/没有对应文件时整段跳过)
if ($global:EBXEnvList.Count -gt 0) {
    Write-Host ''
    Write-Host ('  eBox 多开环境垃圾(按环境, 共 {0} 个环境):' -f $global:EBXEnvList.Count) -ForegroundColor DarkYellow
    if ($global:EBoxEnvNameReadFailed) {
        Write-Host '    提示: 未能读取环境显示名(eBox 可能正在运行), 已按目录序号显示。' -ForegroundColor DarkGray
    }
    # 每个环境一个进度槽: 实时显示"已统计 N MB + 当前路径"。环境内聊天数据常达 GB 级,
    # 单环境统计需 30 秒~1 分钟, 原版只显示 0/26 计数不动 → 用户误判卡死。
    $envSlots = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $global:EBXEnvList.Count; $i++) {
        [void]$envSlots.Add(@{ Label = [string]$global:EBXEnvList[$i].Display; Mode = 'size'; Count = 0; Found = 0; Current = '排队中...'; Done = $false; DoneFlags = $null; Total = 0 })
        $global:EBXEnvList[$i]['Slot'] = $envSlots[$i]
    }
    $envSizeWorker = {
        param($e)
        $s = [double]0
        $slot = $null
        if ($e.ContainsKey('Slot')) { $slot = $e['Slot'] }
        foreach ($p in $e['CacheDirs']) {
            $s += Get-DirSize $p $slot
            if ($null -ne $slot) { $slot.Found = [int]($s / 1MB); $slot.Current = $p }
        }
        foreach ($p in $e['ChatDirs']) {
            $s += Get-DirSize $p $slot
            if ($null -ne $slot) { $slot.Found = [int]($s / 1MB); $slot.Current = $p }
        }
        if ($null -ne $slot) { $slot.Done = $true; $slot.Current = '' }
        return @{ Size = $s }
    }
    $envSizes = @(Invoke-Parallel -InputObjects @($global:EBXEnvList) -ScriptBlock $envSizeWorker -FunctionNames @('Get-DirSize') -Throttle $parallelDegree -ProgressActivity '正在统计环境垃圾大小(数据大时单环境需 30 秒~1 分钟)' -ProgressSlots @($envSlots))
    for ($i = 0; $i -lt $global:EBXEnvList.Count; $i++) { $global:EBXEnvList[$i].Size = $envSizes[$i]['Size'] }
    foreach ($e in $global:EBXEnvList) {
        if ($e.Size -gt 0) {
            Write-Host ('    [{0}] 缓存 {1} 目录, 聊天记录 {2} 目录, 共 {3}' -f $e.Display, $e.CacheDirs.Count, $e.ChatDirs.Count, (Format-Size $e.Size)) -ForegroundColor DarkGray
        } else {
            Write-Host ('    [{0}] 缓存 {1} 目录, 聊天记录 {2} 目录, 无可清理内容' -f $e.Display, $e.CacheDirs.Count, $e.ChatDirs.Count) -ForegroundColor DarkGray
        }
    }
}

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
    @{K='root_temp';    G='通用';     N='磁盘根目录临时文件夹';     Note='Temp/Tmp/Cache 目录的内容';          Sel=$true}
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

# eBox 多开环境: 每个环境一个清理目标(仅缓存+聊天记录白名单目录, 不影响登录状态)
foreach ($e in $global:EBXEnvList) {
    $envPaths = @()
    foreach ($p in @($e.CacheDirs)) { $envPaths += $p }
    foreach ($p in @($e.ChatDirs)) { $envPaths += $p }
    $envPaths = @($envPaths | Where-Object { $_ } | Sort-Object -Unique)
    if ($envPaths.Count -eq 0) { continue }
    [void]$targets.Add((New-Obj @{
        Group='eBox环境'; Name=($e.Display + ' (缓存+聊天记录)'); Paths=$envPaths; Files=@(); Mode='Contents';
        Selected=$true; Note='仅缓存与聊天记录, 不影响登录状态'; Size=[double]$e.Size; TakeOwn=$false
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
Write-Host '  [3/3] 正在统计各项目大小(内容多时需几分钟,请耐心等待)...' -ForegroundColor Cyan
$toSize = @($targets | Where-Object { $_.Size -le 0 })
if ($toSize.Count -gt 0) {
    # 每个待统计目标一个进度槽(同环境统计): 实时显示已统计 MB 与当前路径, 避免大目标长时间无动静被误判卡死
    $tgtSlots = New-Object System.Collections.ArrayList
    $sizeTasks = New-Object System.Collections.ArrayList
    foreach ($t in $toSize) {
        [void]$tgtSlots.Add(@{ Label = ([string]$t.Group + '·' + [string]$t.Name); Mode = 'size'; Count = 0; Found = 0; Current = '排队中...'; Done = $false; DoneFlags = $null; Total = 0 })
    }
    for ($i = 0; $i -lt $toSize.Count; $i++) { [void]$sizeTasks.Add(@{ T = $toSize[$i]; Slot = $tgtSlots[$i] }) }
    $sizeWorker = {
        param($task)
        $t = $task['T']
        $slot = $task['Slot']
        $s = [double]0
        foreach ($p in $t.Paths) {
            $s += Get-DirSize $p $slot
            if ($null -ne $slot) { $slot.Found = [int]($s / 1MB); $slot.Current = $p }
        }
        foreach ($f in $t.Files) { try { $s += $f.Length } catch {} }
        if ($null -ne $slot) { $slot.Done = $true; $slot.Current = '' }
        return @{ Size = $s }
    }
    $sizes = @(Invoke-Parallel -InputObjects @($sizeTasks) -ScriptBlock $sizeWorker -FunctionNames @('Get-DirSize') -Throttle $parallelDegree -ProgressActivity '正在统计大小' -ProgressSlots @($tgtSlots))
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
try { $tw = [Console]::WindowWidth - 1 } catch { $tw = 79 }
if ($tw -lt 40) { $tw = 40 }
$noteW = $tw - 61   # 说明列可用显示宽 = 行宽 - (编号8+分类10+项目30+大小10+间隔3)
if ($noteW -lt 8) { $noteW = 8 }
foreach ($t in $targets) {
    $i++
    $allSum += $t.Size
    if ($t.Selected) { $selSum += $t.Size }
    Write-Host -NoNewline ('  {0,3}   ' -f $i)
    Write-Host -NoNewline (Pad-Cjk $t.Group 10)
    Write-Host -NoNewline (Pad-Cjk $t.Name 30)
    Write-Host -NoNewline ('{0,10}' -f (Format-Size $t.Size))
    # 说明列按剩余显示宽截断: 超宽行在控制台折行,会与下一行视觉叠错(用户反馈的"重影")
    Write-Host ('   ' + (Fit-Cjk ([string]$t.Note) $noteW)) -ForegroundColor DarkGray
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
Write-Host '  开始清理(内容多时需几分钟,请耐心等待)...' -ForegroundColor Cyan
$driveFree = @{}
foreach ($dv in $drives) { $driveFree[$dv] = (Get-PSDrive -Name $dv -ErrorAction SilentlyContinue).Free }
$driveFreed = @{}
foreach ($dv in $drives) { $driveFreed[$dv] = [double]0 }
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
    $drvF = ''
    if ($t.Paths.Count -gt 0) { $drvF = $t.Paths[0].Substring(0, 1) }
    if ($drvF -and $driveFreed.ContainsKey($drvF)) { $driveFreed[$drvF] += $freed }
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
        $drvF = ''
        if ($t.Paths.Count -gt 0) { $drvF = $t.Paths[0].Substring(0, 1) }
        if ($drvF -and $driveFreed.ContainsKey($drvF)) { $driveFreed[$drvF] += $freed }
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
    Write-Host (Fit-Cjk $line ($tw - 1))   # 整行按显示宽截断,防折行叠错
}
Write-Host ('  ' + ('-' * 60)) -ForegroundColor DarkGray
Write-Host ('  本次共释放空间: ' + (Format-Size $totalFreed)) -ForegroundColor Green
foreach ($dv in $drives) {
    $before = $driveFree[$dv]
    $after = (Get-PSDrive -Name $dv -ErrorAction SilentlyContinue).Free
    if (($null -ne $before) -and ($null -ne $after)) {
        Write-Host ('  {0} 盘可用: {1}  ->  {2}   (本盘释放 {3})' -f $dv, (Format-Size $before), (Format-Size $after), (Format-Size $driveFreed[$dv])) -ForegroundColor Green
    }
}
if ($totalFail -gt 0) {
    Write-Host ('  有 ' + $totalFail + ' 个项目被程序占用未能删除,重启电脑后再次运行即可。') -ForegroundColor Yellow
}
Write-Host ''
Read-Host '  按回车键退出'
exit
