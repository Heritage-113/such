[CmdletBinding()]
param(
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [switch]$OfflineOnly,
  [switch]$SkipBuildToolsInstall
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_entry_common.ps1')
try {
  $args=@{Arch=$Arch}
  if($OfflineOnly.IsPresent){$args.OfflineOnly=$true}
  if($SkipBuildToolsInstall.IsPresent){$args.SkipBuildToolsInstall=$true}
  Invoke-SuchPipelineStep -Name 'bootstrap' -ScriptPath (Join-Path $PSScriptRoot 'bootstrap_windows.ps1') -Arguments $args
} catch {
  Stop-SuchPipelineWithError -Entry 'bootstrap' -ErrorRecord $_
  exit 1
}
