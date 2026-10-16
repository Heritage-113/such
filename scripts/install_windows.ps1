[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [switch]$DesktopShortcut,
  [switch]$SkipFonts,
  [switch]$RequireFonts,
  [string]$FontSourceDir='',
  [string]$RuntimeLibraryPath='',
  [switch]$RequireRuntime,
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
  $EffectiveRequireRuntime=$RequireRuntime.IsPresent -or $Arch -eq 'x64'

  # Windows install is intentionally artifact-first. A valid staged payload can
  # be installed without touching CMake/MSVC. A verified build only needs the
  # toolchain-free stage step. Rebuild/test is fallback work, never the default.
  $forceVerify=$Clean.IsPresent -or -not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)
  $stageReady=$false
  $verifiedReady=$false
  if(-not $forceVerify){
    $stageReady=Test-SuchStageManifest -Workspace $Ws
    if($stageReady -and $EffectiveRequireRuntime){
      $existingStage=Read-SuchStageManifest -Workspace $Ws
      if([string]::IsNullOrWhiteSpace([string]$existingStage.runtime_path)){$stageReady=$false}
    }
    if(-not $stageReady){
      $verifiedReady=Test-SuchVerificationManifest -Workspace $Ws
      if($verifiedReady -and $EffectiveRequireRuntime){
        $existingVerification=Read-SuchVerificationManifest -Workspace $Ws
        if([string]::IsNullOrWhiteSpace([string]$existingVerification.runtime_path)){$verifiedReady=$false}
      }
    }
  }

  if($stageReady){
    Write-Host '[SUCH:INSTALL] using existing verified staged artifact.' -ForegroundColor Cyan
  } elseif($verifiedReady){
    Write-Host '[SUCH:INSTALL] using existing verified build; staging only.' -ForegroundColor Cyan
    Invoke-SuchWindowsStageStep -Config $Config -Arch $Arch -RequireRuntime:$EffectiveRequireRuntime
  } else {
    Write-Host '[SUCH:INSTALL] no reusable verified artifact; running verification pipeline.' -ForegroundColor Cyan
    Invoke-SuchWindowsVerifiedBuild -Config $Config -Arch $Arch -Generator $Generator -Jobs $Jobs -RuntimeLibraryPath $RuntimeLibraryPath -RequireRuntime:$EffectiveRequireRuntime -OfflineOnly:$OfflineOnly -Clean:$Clean -SkipAudit:$SkipAudit
    Invoke-SuchWindowsStageStep -Config $Config -Arch $Arch -RequireRuntime:$EffectiveRequireRuntime
  }

  Invoke-SuchWindowsInstallStep -Config $Config -Arch $Arch -DesktopShortcut:$DesktopShortcut -SkipFonts:$SkipFonts -RequireFonts:$RequireFonts -RequireRuntime:$EffectiveRequireRuntime -FontSourceDir $FontSourceDir
  Write-Host 'Public Windows install PASS'
} catch {
  Stop-SuchPipelineWithError -Entry 'install' -ErrorRecord $_
  exit 1
}
