@echo off
rem =====================================================================
rem  diag-hive.cmd  -  dump an eBox environment registry hive (READ-ONLY)
rem
rem  usage: diag-hive.cmd [EnvIndex] [EnvDataRoot] [OutFile]
rem    EnvIndex     default 13
rem    EnvDataRoot  default F:\eBoxData\Env   (F:\eBoxData is auto-fixed)
rem    OutFile      default %USERPROFILE%\Desktop\ebox-hive-dump.txt
rem
rem  MUST be run as administrator.
rem
rem  Two modes, picked automatically:
rem    - hive LOADED (eBox running): reads the LIVE hive through the
rem      registry API (reg query / reg export). No copy, no load.
rem    - hive NOT loaded (eBox closed): copies hive + .LOG1/.LOG2 to
rem      %TEMP%, loads the COPY as HKLM\eBoxDiagHive, dumps it, unloads
rem      it and deletes the copy.
rem  The original environment files are never modified.
rem =====================================================================
setlocal EnableExtensions
set "IDX=%~1"
if "%IDX%"=="" set "IDX=13"
set "ENVDATA=%~2"
if "%ENVDATA%"=="" set "ENVDATA=F:\eBoxData\Env"
if not exist "%ENVDATA%\data" if exist "%ENVDATA%\Env\data" set "ENVDATA=%ENVDATA%\Env"
set "ENVROOT=%ENVDATA%\%IDX%"
set "OUT=%~3"
if "%OUT%"=="" set "OUT=%USERPROFILE%\Desktop\ebox-hive-dump.txt"
set "WORK=%TEMP%\eboxhivediag_%IDX%"

> "%OUT%" echo ================================================================
>>"%OUT%" echo  eBox environment registry hive dump (read-only copy)
>>"%OUT%" echo ================================================================
>>"%OUT%" echo time        : %DATE% %TIME%
>>"%OUT%" echo computer    : %COMPUTERNAME%
>>"%OUT%" echo user        : %USERNAME%
>>"%OUT%" echo envIndex    : %IDX%
>>"%OUT%" echo envRoot     : %ENVROOT%
>>"%OUT%" echo outFile     : %OUT%
>>"%OUT%" echo.

if not exist "%ENVROOT%\" goto :noenv

rem ---- pick the hive file: pure 16-hex name in env root (excludes .LOG1/.LOG2)
set "HIVE="
for /f "delims=" %%F in ('dir /b /a-d "%ENVROOT%" 2^>nul ^| findstr /R /C:"^[0-9A-Fa-f][0-9A-Fa-f]*$"') do set "HIVE=%ENVROOT%\%%F"
if not defined HIVE goto :nohive

>>"%OUT%" echo hive file   : %HIVE%
for %%A in ("%HIVE%") do >>"%OUT%" echo hive size   : %%~zA bytes  mtime=%%~tA
if exist "%HIVE%.LOG1" for %%A in ("%HIVE%.LOG1") do >>"%OUT%" echo hive LOG1   : %%~zA bytes  mtime=%%~tA
if exist "%HIVE%.LOG2" for %%A in ("%HIVE%.LOG2") do >>"%OUT%" echo hive LOG2   : %%~zA bytes  mtime=%%~tA
>>"%OUT%" echo.

rem ---- a mounted hive file cannot be copied (COPY_FAILED), so when eBox has
rem      it loaded we read the LIVE hive instead of copying it.
reg query "HKU\eBox_Env_%IDX%" >nul 2>&1
if not errorlevel 1 goto :live

rem ---- hive is not mounted: copy hive + transaction logs, then load the copy
if exist "%WORK%" rmdir /s /q "%WORK%" >nul 2>&1
mkdir "%WORK%" >nul 2>&1
copy /y "%HIVE%" "%WORK%\hivecopy" >nul 2>&1
if exist "%HIVE%.LOG1" copy /y "%HIVE%.LOG1" "%WORK%\hivecopy.LOG1" >nul 2>&1
if exist "%HIVE%.LOG2" copy /y "%HIVE%.LOG2" "%WORK%\hivecopy.LOG2" >nul 2>&1
if not exist "%WORK%\hivecopy" goto :copyfail

reg unload "HKLM\eBoxDiagHive" >nul 2>&1
reg load "HKLM\eBoxDiagHive" "%WORK%\hivecopy" >"%WORK%\load.txt" 2>&1
if errorlevel 1 goto :loadfail

>>"%OUT%" echo --- [A] Software\Tencent\WXWork inside the hive --------------
reg query "HKLM\eBoxDiagHive\Software\Tencent\WXWork" /s >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    NOT_FOUND: the hive has no Software\Tencent\WXWork
>>"%OUT%" echo.
>>"%OUT%" echo --- [B] Software\Tencent inside the hive ----------------------
reg query "HKLM\eBoxDiagHive\Software\Tencent" /s >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    NOT_FOUND: the hive has no Software\Tencent
>>"%OUT%" echo.
>>"%OUT%" echo --- [C] FULL hive dump ------------------------------------------
reg query "HKLM\eBoxDiagHive" /s >>"%OUT%" 2>&1

reg unload "HKLM\eBoxDiagHive" >nul 2>&1
goto :cleanup

:live
>>"%OUT%" echo NOTE: HKU\eBox_Env_%IDX% is LOADED (eBox is running).
>>"%OUT%" echo       Reading the LIVE hive directly: no copy, no load.
>>"%OUT%" echo.
>>"%OUT%" echo --- [A] Software\Tencent\WXWork inside the hive --------------
reg query "HKU\eBox_Env_%IDX%\Software\Tencent\WXWork" /s >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    NOT_FOUND: the hive has no Software\Tencent\WXWork
>>"%OUT%" echo.
>>"%OUT%" echo --- [B] Software\Tencent inside the hive ----------------------
reg query "HKU\eBox_Env_%IDX%\Software\Tencent" /s >>"%OUT%" 2>&1
if errorlevel 1 >>"%OUT%" echo    NOT_FOUND: the hive has no Software\Tencent
>>"%OUT%" echo.
>>"%OUT%" echo --- [C] FULL hive dump ------------------------------------------
reg query "HKU\eBox_Env_%IDX%" /s >>"%OUT%" 2>&1
>>"%OUT%" echo.
>>"%OUT%" echo --- [D] hive export (.reg) --------------------------------------
reg export "HKU\eBox_Env_%IDX%" "%OUT%.reg" /y >nul 2>&1
if exist "%OUT%.reg" (
    >>"%OUT%" echo exported : %OUT%.reg
) else (
    >>"%OUT%" echo EXPORT_FAILED: cannot export HKU\eBox_Env_%IDX%
)
goto :cleanup

:loadfail
>>"%OUT%" echo.
>>"%OUT%" echo LOAD_FAILED - the copy could not be mounted.
>>"%OUT%" echo reg load output:
type "%WORK%\load.txt" >>"%OUT%" 2>&1
>>"%OUT%" echo.
>>"%OUT%" echo Common causes: not running as administrator,
>>"%OUT%" echo              or the hive is corrupt / in use by eBox.
goto :cleanup

:copyfail
>>"%OUT%" echo COPY_FAILED - cannot copy the hive to %WORK%
goto :cleanup

:cleanup
reg unload "HKLM\eBoxDiagHive" >nul 2>&1
if exist "%WORK%" rmdir /s /q "%WORK%" >nul 2>&1
>>"%OUT%" echo.
>>"%OUT%" echo ================================================================
>>"%OUT%" echo DONE. Please send this txt file back.
>>"%OUT%" echo saved to: %OUT%
echo.
echo [saved] %OUT%
endlocal
exit /b 0

:noenv
>>"%OUT%" echo ENV_ROOT_NOT_FOUND: %ENVROOT%
>>"%OUT%" echo existing dirs under %ENVDATA% :
dir /b /ad "%ENVDATA%" >>"%OUT%" 2>&1
echo.
echo [error] env root not found: %ENVROOT%
endlocal
exit /b 1

:nohive
>>"%OUT%" echo NO_HIVE_FILE_FOUND in %ENVROOT%
dir /a /-c "%ENVROOT%" >>"%OUT%" 2>&1
echo.
echo [error] no hive file found in %ENVROOT%
endlocal
exit /b 1
