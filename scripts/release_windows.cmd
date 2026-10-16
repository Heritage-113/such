@echo off
setlocal EnableExtensions
set "SUCH_PS_ENTRY=%~dp0release_windows.ps1"
call "%~dp0windows_powershell_host.cmd" %*
set "RC=%ERRORLEVEL%"
call "%~dp0windows_cmd_finish.cmd" "%~nx0" "%RC%"
exit /b %RC%
