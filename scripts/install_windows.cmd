@echo off
setlocal EnableExtensions
set "PSHOST="
where pwsh.exe >nul 2>nul && set "PSHOST=pwsh.exe"
if not defined PSHOST if exist "%ProgramFiles%\PowerShell\7\pwsh.exe" set "PSHOST=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not defined PSHOST if exist "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" set "PSHOST=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not defined PSHOST (
  echo [FATAL] PowerShell was not found. 1>&2
  exit /b 9009
)
"%PSHOST%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_windows.ps1" %*
set "RC=%ERRORLEVEL%"
exit /b %RC%
