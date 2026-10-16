[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [string]$RuntimeLibraryPath='',
  [switch]$RequireRuntime,
  [switch]$OfflineOnly,
  [switch]$Clean,
  [switch]$SkipAudit
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_pipeline.ps1')
try {
  Invoke-SuchWindowsVerifiedBuild -Config $Config -Arch $Arch -Generator $Generator -Jobs $Jobs -RuntimeLibraryPath $RuntimeLibraryPath -RequireRuntime:$RequireRuntime -OfflineOnly:$OfflineOnly -Clean:$Clean -SkipAudit:$SkipAudit
  Write-Host 'Public Windows verification PASS'
} catch {
  Stop-SuchPipelineWithError -Entry 'verify' -ErrorRecord $_
  exit 1
}
