[CmdletBinding()]
param(
  [ValidateSet('Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [string]$RuntimeLibraryPath='',
  [switch]$OfflineOnly,
  [switch]$Clean,
  [switch]$SkipAudit
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_pipeline.ps1')
. (Join-Path $PSScriptRoot 'windows_paths.ps1')
. (Join-Path $PSScriptRoot 'windows_artifact_state.ps1')

try {
  $Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
  $Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
  $forceVerify=$Clean.IsPresent -or -not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)
  $stageReady=$false
  $verifiedReady=$false
  if(-not $forceVerify){
    $stageReady=Test-SuchStageManifest -Workspace $Ws
    if($stageReady){
      $existingStage=Read-SuchStageManifest -Workspace $Ws
      if([string]::IsNullOrWhiteSpace([string]$existingStage.runtime_path)){$stageReady=$false}
    }
    if(-not $stageReady){
      $verifiedReady=Test-SuchVerificationManifest -Workspace $Ws
      if($verifiedReady){
        $existingVerification=Read-SuchVerificationManifest -Workspace $Ws
        if([string]::IsNullOrWhiteSpace([string]$existingVerification.runtime_path)){$verifiedReady=$false}
      }
    }
  }

  if($stageReady){
    Write-Host '[SUCH:RELEASE] using existing verified staged artifact.' -ForegroundColor Cyan
  } elseif($verifiedReady){
    Write-Host '[SUCH:RELEASE] using existing verified build; staging only.' -ForegroundColor Cyan
    Invoke-SuchWindowsStageStep -Config $Config -Arch $Arch -RequireRuntime
  } else {
    Write-Host '[SUCH:RELEASE] no reusable verified artifact; running verification pipeline.' -ForegroundColor Cyan
    Invoke-SuchWindowsVerifiedBuild -Config $Config -Arch $Arch -Generator $Generator -Jobs $Jobs -RuntimeLibraryPath $RuntimeLibraryPath -RequireRuntime -OfflineOnly:$OfflineOnly -Clean:$Clean -SkipAudit:$SkipAudit
    Invoke-SuchWindowsStageStep -Config $Config -Arch $Arch -RequireRuntime
  }
  Invoke-SuchWindowsReleaseStep -Config $Config -Arch $Arch
  Write-Host 'Public Windows release PASS'
} catch {
  Stop-SuchPipelineWithError -Entry 'release' -ErrorRecord $_
  exit 1
}
