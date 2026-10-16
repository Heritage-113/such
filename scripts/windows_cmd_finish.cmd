@echo off
rem Do not pause scripted/CI invocations, including PowerShell and build agents.
rem Direct Explorer launch normally runs cmd.exe /c "...launcher.cmd".
setlocal EnableExtensions EnableDelayedExpansion
if defined CI exit /b 0
if /I "!SUCH_NONINTERACTIVE!"=="1" exit /b 0
if /I "!SUCH_KEEP_OPEN!"=="0" exit /b 0
if /I "!SUCH_KEEP_OPEN!"=="1" goto :keep
set "invocation=!CMDCMDLINE!"
set "entry=%~1"
if "!invocation:%entry%=!"=="!invocation!" exit /b 0
:keep
echo.
echo [SUCH] %~1 finished, exit code %~2. Press any key to close this window.
pause >nul
exit /b 0
