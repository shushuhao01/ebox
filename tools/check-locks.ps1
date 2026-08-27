# check-locks.ps1 - Static check for dangerous operations inside lock critical sections
#
# Purpose: scan C++ sources for file I/O / user callbacks / RPC / Sleep inside lock
# critical sections, preventing regression of the v2.9.9 Env::m_mutex lock-convoy hang
# (see logs/eBox-hang diagnosis report).
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\check-locks.ps1
# Exit code: 0 = pass; 1 = suspicious hits found (file:line printed).
#
# Note: heuristic check (dangerous call within N lines after a lock declaration).
# May produce false positives; extend $allowPatterns to whitelist.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$scanDirs = @(
    (Join-Path $root 'eBox'),
    (Join-Path $root 'MemoryDll'),
    (Join-Path $root 'common')
)
$extensions = @('.cpp', '.ixx', '.h', '.hpp')

# Dangerous calls forbidden inside a lock critical section
$dangerPatterns = @(
    'fs::exists', 'fs::create_directories', 'fs::copy_file', 'fs::remove',
    'fs::rename', 'fs::file_size', 'weakly_canonical', 'std::ifstream', 'std::ofstream',
    'CreateFileW', 'CreateFileA', 'WriteFile', 'ReadFile', 'DeleteFileW',
    'm_notify\s*\(', 'm_envChangeNotify\s*\(',
    'RegOpenKey', 'RegCreateKey', 'RegSaveKey', 'RegLoadKey',
    '::Sleep\s*\(', 'MsgWaitForMultipleObjects', 'WinHttp'
)

# Whitelist: lines matching these are skipped even if a danger pattern hits
$allowPatterns = @(
    '^\s*//',        # comment line
    '^\s*\*',        # block-comment line
    'outside lock',  # explicit out-of-lock marker
    'check-locks'    # self reference
)

$lockRegex = [regex]'std::(unique_lock|lock_guard|scoped_lock|shared_lock)\s*(<[^>]+>)?\s+\w+'
$scopeLines = 12

# Indentation of a line = count of leading whitespace chars (tab counts as 1)
function Get-Indent([string] $s)
{
    $n = 0
    while ($n -lt $s.Length -and ($s[$n] -eq ' ' -or $s[$n] -eq "`t")) { ++$n }
    return $n
}

$hits = @()
foreach ($dir in $scanDirs)
{
    if (-not (Test-Path $dir)) { continue }
    $files = Get-ChildItem -Path $dir -Recurse -File | Where-Object { $extensions -contains $_.Extension }
    foreach ($file in $files)
    {
        $lines = [IO.File]::ReadAllLines($file.FullName)
        for ($i = 0; $i -lt $lines.Count; ++$i)
        {
            if (-not $lockRegex.IsMatch($lines[$i])) { continue }
            $lockIndent = Get-Indent $lines[$i]
            $end = [Math]::Min($i + $scopeLines, $lines.Count - 1)
            for ($j = $i; $j -le $end; ++$j)
            {
                $line = $lines[$j]
                # Stop at scope end: a bare closing brace at indent <= lock declaration
                # means the critical section (or enclosing function) has ended.
                if ($j -gt $i -and $line -match '^\s*\}' -and (Get-Indent $line) -le $lockIndent) { break }
                foreach ($dp in $dangerPatterns)
                {
                    if ($line -match $dp)
                    {
                        $allowed = $false
                        foreach ($ap in $allowPatterns)
                        {
                            if ($line -match $ap) { $allowed = $true; break }
                        }
                        if (-not $allowed)
                        {
                            $hits += [PSCustomObject]@{
                                File = $file.FullName.Substring($root.Length + 1)
                                Line = $j + 1
                                Pattern = $dp
                                Text = $line.Trim()
                            }
                        }
                    }
                }
            }
        }
    }
}

if ($hits.Count -eq 0)
{
    Write-Host '[check-locks] PASS: no dangerous operation inside locks.' -ForegroundColor Green
    exit 0
}

Write-Host ("[check-locks] FAIL: {0} suspicious hit(s):" -f $hits.Count) -ForegroundColor Red
$hits | ForEach-Object {
    Write-Host ("  {0}:{1}  [{2}]  {3}" -f $_.File, $_.Line, $_.Pattern, $_.Text) -ForegroundColor Yellow
}
Write-Host 'Verify whether these calls are inside a lock; if false positive, extend $allowPatterns.' -ForegroundColor Red
exit 1
