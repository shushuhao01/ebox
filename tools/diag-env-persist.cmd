@echo off
rem =====================================================================
rem  diag-env-persist.cmd  -  read-only evidence collector (pure CMD)
rem  No PowerShell required. Uses only dir / reg / findstr.
rem
rem  usage: diag-env-persist.cmd [EnvIndex] [Label] [EnvDataRoot] [OutFile]
rem    EnvIndex     default 13
rem    Label        after-login / after-reboot / after-env-start / after-rescan
rem    EnvDataRoot  default F:\eBoxData\Env   (F:\eBoxData is auto-fixed)
rem    OutFile      default %USERPROFILE%\Desktop\ebox-env<idx>-persist-<label>.txt
rem
rem  This script changes NOTHING. It only reads files and registry keys.
rem =====================================================================
setlocal EnableExtensions
set "IDX=%~1"
if "%IDX%"=="" set "IDX=13"
set "TAG=%~2"
if "%TAG%"=="" set "TAG=snap"
set "ENVDATA=%~3"
if "%ENVDATA%"=="" set "ENVDATA=F:\eBoxData\Env"
if not exist "%ENVDATA%\data" if exist "%ENVDATA%\Env\data" set "ENVDATA=%ENVDATA%\Env"
set "ENVROOT=%ENVDATA%\%IDX%"
set "OUT=%~4"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\ebox-env%IDX%-persist-%TAG%.txt"
set "LOG=%ENVDATA%\data\logs\env_%IDX%.log"
set "ADMIN=no"
whoami /groups 2>nul | findstr /C:"S-1-5-32-544" >nul && set "ADMIN=yes-group"

rem Documents is often redirected (D:\Backup\Documents ...). Read the real path.
set "DOCS=%USERPROFILE%\Documents"
for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders" /v Personal 2^>nul ^| findstr /I "Personal"') do set "DOCS=%%B"
call set "DOCS=%DOCS%"

> "%OUT%" echo ================================================================
>>"%OUT%" echo  eBox environment persistence evidence (read-only)
>>"%OUT%" echo ================================================================
>>"%OUT%" echo time        : %DATE% %TIME%
>>"%OUT%" echo computer    : %COMPUTERNAME%
>>"%OUT%" echo user        : %USERNAME%
>>"%OUT%" echo adminGroup  : %ADMIN%
>>"%OUT%" echo envIndex    : %IDX%
>>"%OUT%" echo envDataRoot : %ENVDATA%
>>"%OUT%" echo envRoot     : %ENVROOT%
>>"%OUT%" echo documents   : %DOCS%
>>"%OUT%" echo label       : %TAG%
>>"%OUT%" echo.

rem ---------------------------------------------------------------- [1] env root
>>"%OUT%" echo --- [1] ENV ROOT -------------------------------------------------
if not exist "%ENVROOT%\" goto :noenvroot
>>"%OUT%" echo top-level: size / last-write-time
dir /a /-c "%ENVROOT%" >>"%OUT%" 2>&1
>>"%OUT%" echo total:
rem Chinese Windows prints a localized file-count line; take the
rem second-to-last line of a recursive dir (last line = free space).
set "TOTAL="
set "LASTLINE="
for /f "delims=" %%L in ('dir /a /s /-c "%ENVROOT%" 2^>nul') do call :roll "%%L"
if not defined TOTAL set "TOTAL=(no summary line)"
>>"%OUT%" echo %TOTAL%
goto :envrootdone
:noenvroot
>>"%OUT%" echo ENV_ROOT_NOT_FOUND
>>"%OUT%" echo existing dirs under "%ENVDATA%" :
dir /b /ad "%ENVDATA%" >>"%OUT%" 2>&1
:envrootdone
>>"%OUT%" echo.

rem ------------------------------------------- [1b] where did WXWork write inside env
>>"%OUT%" echo --- [1b] WXWORK WRITES INSIDE ENV (top level, newest first) ----
if not exist "%ENVROOT%\" goto :sec1bdone
for /d %%U in ("%ENVROOT%\Users\*") do call :probe_env_wx "%%~fU"
:sec1bdone
>>"%OUT%" echo.

rem ------------------------------------------------------- [2] WXWork in env
>>"%OUT%" echo --- [2] WXWork DIRECTORIES INSIDE ENV ----------------------------
if not exist "%ENVROOT%\" goto :sec2done
dir /a /s /b /ad "%ENVROOT%" | findstr /I "WXWork" >>"%OUT%" 2>&1
:sec2done
>>"%OUT%" echo.

rem --------------------------------------------------- [3] key state files
>>"%OUT%" echo --- [3] KEY STATE FILES INSIDE ENV -------------------------------
if not exist "%ENVROOT%\" goto :sec3done
>>"%OUT%" echo  Config.cfg :
dir /a /s /b /-c "%ENVROOT%\Config.cfg" >>"%OUT%" 2>&1
>>"%OUT%" echo  Local State :
dir /a /s /b /-c "%ENVROOT%\Local State" >>"%OUT%" 2>&1
:sec3done
>>"%OUT%" echo.

rem ------------------------------------------------------------ [4] host side
>>"%OUT%" echo --- [4] HOST REAL DIRECTORIES (should NOT grow while env runs) ---
if not exist "%DOCS%\WXWork\" goto :nohostwx
>>"%OUT%" echo host WXWork (top level) :
dir /a /-c "%DOCS%\WXWork" >>"%OUT%" 2>&1
>>"%OUT%" echo host Config.cfg :
dir /a /-c "%DOCS%\WXWork\Global\Config.cfg" >>"%OUT%" 2>&1
goto :hostdone
:nohostwx
>>"%OUT%" echo host WXWork : NOT_FOUND under %DOCS%
:hostdone
if not exist "%APPDATA%\Tencent\WXWork\" goto :noappdata
>>"%OUT%" echo APPDATA\Tencent\WXWork (top level) :
dir /a /-c "%APPDATA%\Tencent\WXWork" >>"%OUT%" 2>&1
>>"%OUT%" echo APPDATA\Tencent\WXWork total :
set "TOTAL="
set "LASTLINE="
for /f "delims=" %%L in ('dir /a /s /-c "%APPDATA%\Tencent\WXWork" 2^>nul') do call :roll "%%L"
if not defined TOTAL set "TOTAL=(no summary line)"
>>"%OUT%" echo %TOTAL%
goto :appdatadone
:noappdata
>>"%OUT%" echo APPDATA\Tencent\WXWork : NOT_FOUND
:appdatadone
>>"%OUT%" echo.

rem ------------------------------------------- [4b] host WXWork data roots
>>"%OUT%" echo --- [4b] HOST WXWORK DATA ROOTS (Config.cfg / Local State) -----
for %%R in ("%DOCS%\WXWork" "%APPDATA%\Tencent\WXWork" "%LOCALAPPDATA%\Tencent\WXWork" "%ProgramData%\Tencent\WXWork") do call :probe_root "%%~R"
>>"%OUT%" echo.

rem ------------------------------------------- [4c] host WXWork registry
>>"%OUT%" echo --- [4c] HOST REGISTRY HKCU\Software\Tencent\WXWork ------------
reg query "HKCU\Software\Tencent\WXWork" >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    NOT_FOUND: host has no HKCU\Software\Tencent\WXWork
>>"%OUT%" echo  GUID subkey (device id used for auto-login):
reg query "HKCU\Software\Tencent\WXWork\GUID" /s >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    NOT_FOUND: host has no WXWork\GUID
>>"%OUT%" echo.

rem --------------------------------------------------------------- [5] hive files
>>"%OUT%" echo --- [5] REGISTRY HIVE FILES IN ENV ROOT --------------------------
if not exist "%ENVROOT%\" goto :sec5done
>>"%OUT%" echo  16-hex hive / *.tmp / *.bad.* (name, size, last write time) :
dir /a-d /-c "%ENVROOT%" >>"%OUT%" 2>&1
:sec5done
>>"%OUT%" echo.

rem ---------------------------------------------------------- [6] registry
>>"%OUT%" echo --- [6] REGISTRY STORES ------------------------------------------
>>"%OUT%" echo  HKU\eBox_Env_%IDX%\Software\Tencent\WXWork :
reg query "HKU\eBox_Env_%IDX%\Software\Tencent\WXWork" >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    NOT_LOADED / NOT_FOUND / access denied (try administrator)
>>"%OUT%" echo  HKCU\Software\eBox\EnvRegistry\%IDX% :
reg query "HKCU\Software\eBox\EnvRegistry\%IDX%" >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    not present
>>"%OUT%" echo.

rem --------------------------------------------------------------- [7] env log
>>"%OUT%" echo --- [7] ENV LOG (lines containing hive / error / fail) ----------
>>"%OUT%" echo  log file : %LOG%
if not exist "%LOG%" goto :nolog
for %%A in ("%LOG%") do >>"%OUT%" echo  log size : %%~zA bytes  mtime=%%~tA
findstr /I /C:"hive" /C:"error" /C:"fail" "%LOG%" > "%TEMP%\eboxdiag_log.txt" 2>&1
for %%A in ("%TEMP%\eboxdiag_log.txt") do if %%~zA==0 (>>"%OUT%" echo   NO_MATCH - no hive/error/fail lines in this log) else (type "%TEMP%\eboxdiag_log.txt" >>"%OUT%" 2>&1)
del "%TEMP%\eboxdiag_log.txt" >nul 2>&1
goto :logdone
:nolog
>>"%OUT%" echo LOG_NOT_FOUND
:logdone
>>"%OUT%" echo.
>>"%OUT%" echo ================================================================
>>"%OUT%" echo DONE. Please send this txt file back.
>>"%OUT%" echo saved to: %OUT%

echo.
echo [saved] %OUT%
endlocal
exit /b 0

rem ------------------------------------------------------- sub: probe one root
:probe_root
>>"%OUT%" echo  %~1
if exist "%~1\Global\Config.cfg" (dir /a /-c "%~1\Global\Config.cfg" >>"%OUT%" 2>&1) else (>>"%OUT%" echo    Config.cfg  : NOT_FOUND)
if exist "%~1\Local State" (dir /a /-c "%~1\Local State" >>"%OUT%" 2>&1) else (>>"%OUT%" echo    Local State : NOT_FOUND)
goto :eof

rem ---------------------------------------------- sub: WXWork dirs of one env user
:probe_env_wx
if not exist "%~1\Documents\WXWork\" goto :pew2
>>"%OUT%" echo  Documents\WXWork :
dir /a /-c /o-d "%~1\Documents\WXWork" >>"%OUT%" 2>&1
:pew2
if not exist "%~1\Documents\WXWork\Global\" goto :pew3
>>"%OUT%" echo  Documents\WXWork\Global :
dir /a /-c /o-d "%~1\Documents\WXWork\Global" >>"%OUT%" 2>&1
:pew3
if not exist "%~1\appdata\roaming\Tencent\WXWork\" goto :pew4
>>"%OUT%" echo  appdata\roaming\Tencent\WXWork :
dir /a /-c /o-d "%~1\appdata\roaming\Tencent\WXWork" >>"%OUT%" 2>&1
:pew4
if not exist "%~1\appdata\local\Tencent\WXWork\" goto :pew5
>>"%OUT%" echo  appdata\local\Tencent\WXWork :
dir /a /-c /o-d "%~1\appdata\local\Tencent\WXWork" >>"%OUT%" 2>&1
:pew5
goto :eof

rem ------------------------------------------------- sub: keep last two dir lines
:roll
set "TOTAL=%LASTLINE%"
set "LASTLINE=%~1"
goto :eof
