[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [switch]$RequireRuntime
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'windows_paths.ps1')
. (Join-Path $PSScriptRoot 'windows_artifact_state.ps1')

$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
$verification=Read-SuchVerificationManifest -Workspace $Ws
if ($RequireRuntime.IsPresent -and [string]::IsNullOrWhiteSpace([string]$verification.runtime_path)) {
  throw 'A production runtime is required, but the verified-build manifest contains no tested runtime.'
}

# Staging is intentionally toolchain-free. Verification already bound the exact
# build outputs by SHA-256, so stage copies only those attested files and checks
# their hashes again. install/release therefore do not depend on CMake or MSVC.
Remove-Item -LiteralPath $Ws.InstallDir -Recurse -Force -ErrorAction SilentlyContinue
$stageBin=Join-Path $Ws.InstallDir 'bin'
New-Item -ItemType Directory -Force -Path $stageBin | Out-Null
$gui=Join-Path $stageBin 'Such.exe'
$cli=Join-Path $stageBin 'SuchCLI.exe'
Copy-Item -LiteralPath ([string]$verification.gui_path) -Destination $gui -Force
Copy-Item -LiteralPath ([string]$verification.cli_path) -Destination $cli -Force

$runtime=''
if (-not [string]::IsNullOrWhiteSpace([string]$verification.runtime_path)) {
  $testedRuntime=[string]$verification.runtime_path
  if ((Get-SuchFileSha256 -Path $testedRuntime) -ne [string]$verification.runtime_sha256) {
    throw 'Production runtime changed after verification and before staging.'
  }
  $runtime=Join-Path $stageBin 'SuchRuntimePrivate.dll'
  Copy-Item -LiteralPath $testedRuntime -Destination $runtime -Force
  if ((Get-SuchFileSha256 -Path $runtime) -ne [string]$verification.runtime_sha256) {
    throw 'Staged production runtime differs from the runtime that passed verification.'
  }
  Write-Host 'Verified production runtime staged beside Such.exe and SuchCLI.exe.'
}

if ((Get-SuchFileSha256 -Path $gui) -ne [string]$verification.gui_sha256) { throw 'Staged Such.exe differs from the verified build output.' }
if ((Get-SuchFileSha256 -Path $cli) -ne [string]$verification.cli_sha256) { throw 'Staged SuchCLI.exe differs from the verified build output.' }

[void](Write-SuchStageManifest -Workspace $Ws -VerificationManifest $verification -GuiPath $gui -CliPath $cli -RuntimePath $runtime)
Write-Host "Windows stage step PASS: $($Ws.InstallDir)"
