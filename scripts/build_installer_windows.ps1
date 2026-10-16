[CmdletBinding()]
param(
  [ValidateSet('Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [string]$RuntimeLibraryPath='',
  [string]$ISCCPath='',
  [switch]$OfflineOnly,
  [switch]$Clean
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_pipeline.ps1')
. (Join-Path $PSScriptRoot 'windows_paths.ps1')
. (Join-Path $PSScriptRoot 'windows_artifact_state.ps1')
. (Join-Path $PSScriptRoot 'windows_native.ps1')
. (Join-Path $PSScriptRoot 'windows_inno_setup.ps1')

try {
  $Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
  $Version=Get-SuchVersion -SourceRoot $Root
  $Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
  # Build and verify the current source before acquiring external installer
  # tooling. An old staged.json file cannot attest to this source revision.
  Invoke-SuchWindowsVerifiedBuild -Config $Config -Arch $Arch -Generator $Generator -Jobs $Jobs -RuntimeLibraryPath $RuntimeLibraryPath -RequireRuntime -OfflineOnly:$OfflineOnly -Clean:$Clean
  Invoke-SuchWindowsStageStep -Config $Config -Arch $Arch -RequireRuntime
  $stage=Read-SuchStageManifest -Workspace $Ws
  if ([string]::IsNullOrWhiteSpace([string]$stage.runtime_path)) { throw 'A verified Fix5 runtime is mandatory for setup packaging.' }
  $stageBin=Join-Path $Ws.InstallDir 'bin'
  foreach($binary in @('Such.exe','SuchCLI.exe','SuchRuntimePrivate.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageBin $binary) -PathType Leaf)) { throw "Staged payload missing: $binary" }
  }
  Assert-SuchWindowsRuntimeSha256 -SourceRoot $Root -RuntimePath (Join-Path $stageBin 'SuchRuntimePrivate.dll')
  foreach($binary in @('Such.exe','SuchCLI.exe')) {
    $pe=(Get-Item -LiteralPath (Join-Path $stageBin $binary)).VersionInfo
    if (($pe.ProductVersion -notin @($Version, "$Version.0")) -or ($pe.FileVersion -notin @($Version, "$Version.0"))) {
      throw ("Windows PE metadata mismatch ({0}): FileVersion={1} ProductVersion={2}, expected {3}" -f $binary,$pe.FileVersion,$pe.ProductVersion,$Version)
    }
  }
  # Only provision the Inno 7 toolchain after source/build/runtime validation.
  $iscc=Resolve-SuchInnoCompiler -SourceRoot $Root -ExplicitPath $ISCCPath -OfflineOnly:$OfflineOnly
  $outputDir=Join-Path $Root 'dist\installers'
  New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
  $setup=Join-Path $outputDir ("Such_v{0}_Windows_x64_Setup.exe" -f $Version)
  Remove-Item -LiteralPath $setup -Force -ErrorAction SilentlyContinue
  $compileArgs=@(
    "/DAppVersion=$Version",
    "/DStageBin=$stageBin",
    "/DSourceRoot=$Root",
    "/DInstallerOutput=$outputDir",
    (Join-Path $Root 'installer\Such.iss')
  )
  Invoke-SuchNative -FilePath $iscc -Arguments $compileArgs -Step 'Inno Setup compiler'
  if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "Inno Setup reported success without a setup EXE: $setup" }
  $hash=Get-SuchFileSha256 -Path $setup
  Write-Host "[SUCH:SETUP] PASS: $setup"
  Write-Host "SHA-256: $hash"
} catch {
  Stop-SuchPipelineWithError -Entry 'installer' -ErrorRecord $_
  exit 1
}
