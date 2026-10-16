@echo off
setlocal EnableExtensions
if not defined SUCH_PS_ENTRY (
  echo [SUCH:FAIL][launcher] SUCH_PS_ENTRY is not defined. 1>&2
  exit /b 87
)
set "ASSET_FILE=%~dp0windows_powershell_assets.txt"
if not exist "%ASSET_FILE%" (
  echo [SUCH:FAIL][launcher] PowerShell asset authority is missing: "%ASSET_FILE%" 1>&2
  exit /b 2
)
set "PWSH_VERSION="
for /f "tokens=1,* delims==" %%A in ('findstr /B /C:"PowerShellVersion=" "%ASSET_FILE%"') do set "PWSH_VERSION=%%B"
if not defined PWSH_VERSION (
  echo [SUCH:FAIL][launcher] PowerShellVersion is missing from "%ASSET_FILE%". 1>&2
  exit /b 2
)
set "HOSTARCH=x64"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "HOSTARCH=arm64"
if /I "%PROCESSOR_ARCHITEW6432%"=="ARM64" set "HOSTARCH=arm64"
if /I "%PROCESSOR_ARCHITECTURE%"=="x86" if "%PROCESSOR_ARCHITEW6432%"=="" set "HOSTARCH=x86"
set "TOOLCHAIN_ROOT=%USERPROFILE%\STC"
if defined SUCH_TOOLCHAIN_ROOT set "TOOLCHAIN_ROOT=%SUCH_TOOLCHAIN_ROOT%"
set "PWSH=%TOOLCHAIN_ROOT%\powershell\%PWSH_VERSION%\win-%HOSTARCH%\pwsh.exe"
rem Prefer an already installed matching PowerShell 7 host. This avoids
rem failing in Windows PowerShell 5.1 when GetFolderPath(UserProfile) is empty.
where pwsh.exe >nul 2>nul
if not errorlevel 1 (
  for /f "usebackq delims=" %%V in (`pwsh.exe -NoLogo -NoProfile -Command "$PSVersionTable.PSVersion.ToString()" 2^>nul`) do if "%%V"=="%PWSH_VERSION%" set "PWSH=pwsh.exe"
)
if /I "%PWSH%"=="pwsh.exe" goto :run_host
set "BOOTSTRAP_HOST=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%BOOTSTRAP_HOST%" (
  where pwsh.exe >nul 2>nul || (
    echo [SUCH:FAIL][launcher] No bootstrap PowerShell host was found. 1>&2
    exit /b 9009
  )
  set "BOOTSTRAP_HOST=pwsh.exe"
)
set "HOST_OFFLINE="
for %%A in (%*) do if /I "%%~A"=="-OfflineOnly" set "HOST_OFFLINE=-OfflineOnly"
"%BOOTSTRAP_HOST%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0windows_powershell_host.ps1" -HostArch "%HOSTARCH%" %HOST_OFFLINE%
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%PWSH%" (
  echo [SUCH:FAIL][launcher] Provisioned PowerShell host is missing: "%PWSH%" 1>&2
  exit /b 2
)
:run_host
"%PWSH%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SUCH_PS_ENTRY%" %*
set "RC=%ERRORLEVEL%"
exit /b %RC%
