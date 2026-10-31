@echo off
setlocal EnableExtensions
cd /d "%~dp0"
echo ================================================================
echo  eBox environment persistence evidence  (READ-ONLY)
echo  Run this ON THE FAILURE MACHINE. It changes nothing.
echo ================================================================
echo.
set "IDX="
set /p IDX=Env index [13]:
if "%IDX%"=="" set IDX=13
set "TAG="
set /p TAG=Label [after-login / after-reboot / after-env-start / after-rescan]:
if "%TAG%"=="" set TAG=snap
echo.
where powershell >nul 2>nul
if errorlevel 1 goto use_cmd
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0diag-env-persist.ps1" -EnvIndex %IDX% -EnvDataRoot F:\eBoxData\Env -Label %TAG%
if not errorlevel 1 goto hive
echo.
echo [warn] PowerShell collector failed, using the CMD collector instead...
:use_cmd
call "%~dp0diag-env-persist.cmd" %IDX% %TAG% F:\eBoxData\Env
:hive
echo.
echo ----------------------------------------------------------------
echo  REGISTRY HIVE DUMP (the decisive evidence)
echo  Requirements: run as ADMINISTRATOR. eBox may stay open.
echo  If eBox is running, the LIVE hive is read; if it is closed,
echo  a temp copy is loaded instead. Either way nothing is modified.
echo ----------------------------------------------------------------
set "YN="
set /p YN=Dump env registry hive now? (y/N):
if /I not "%YN%"=="y" goto done
whoami /groups 2>nul | findstr /C:"S-1-5-32-544" >nul
if errorlevel 1 (
  echo [warn] not elevated - the hive dump will fail. Right-click this file
  echo        and pick "Run as administrator", then choose y.
)
call "%~dp0diag-hive.cmd" %IDX% F:\eBoxData\Env
:done
echo.
echo ----------------------------------------------------------------
echo Done. Result txt files are on the Desktop:
echo   ebox-env%IDX%-persist-%TAG%.txt
echo   ebox-hive-dump.txt   (if the hive dump was selected)
echo Please send those files back.
echo ----------------------------------------------------------------
pause
