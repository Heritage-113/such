[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [switch]$OfflineOnly,
  [switch]$Clean,
  [switch]$SkipAudit
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_pipeline.ps1')
try {
  Invoke-SuchWindowsAuditStep -SkipAudit:$SkipAudit
  Invoke-SuchWindowsBootstrapStep -Arch $Arch -OfflineOnly:$OfflineOnly
  Invoke-SuchWindowsBuildStep -Config $Config -Arch $Arch -Generator $Generator -Jobs $Jobs -Clean:$Clean
  Write-Host 'Public Windows build PASS'
} catch {
  Stop-SuchPipelineWithError -Entry 'build' -ErrorRecord $_
  exit 1
}
