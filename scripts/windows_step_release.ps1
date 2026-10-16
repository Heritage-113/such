[CmdletBinding()]
param(
  [ValidateSet('Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64')][string]$Arch='x64'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'windows_paths.ps1')
. (Join-Path $PSScriptRoot 'windows_artifact_state.ps1')

$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$Version=Get-SuchVersion -SourceRoot $Root
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
$stageManifest=Read-SuchStageManifest -Workspace $Ws
if ([string]::IsNullOrWhiteSpace([string]$stageManifest.runtime_path)) {
  throw 'Public Windows release requires a verified production runtime in the staged payload.'
}

$artifactDir=Join-Path $Root 'dist\artifacts'
$artifact=Join-Path $artifactDir ("Such_v{0}_Windows_x64_Public.zip" -f $Version)
New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
Remove-Item -LiteralPath $artifact -Force -ErrorAction SilentlyContinue

# Package from repository-local build state; publish the completed archive to dist.
$shortArtifact=Join-Path $Ws.Base ("Such_v{0}_Windows_x64_Public.zip" -f $Version)
Remove-Item -LiteralPath $shortArtifact -Force -ErrorAction SilentlyContinue
$packRoot=Join-Path $Ws.Base 'release-pack-windows-x64'
Remove-Item -LiteralPath $packRoot -Recurse -Force -ErrorAction SilentlyContinue
$payload=Join-Path $packRoot ("Such-v{0}-Windows-x64" -f $Version)
New-Item -ItemType Directory -Force -Path $payload | Out-Null
Copy-Item -Path (Join-Path $Ws.InstallDir '*') -Destination $payload -Recurse -Force
Compress-Archive -Path $payload -DestinationPath $shortArtifact -CompressionLevel Optimal -Force
if (-not (Test-Path -LiteralPath $shortArtifact -PathType Leaf)) { throw "Release archive was not created: $shortArtifact" }
Copy-Item -LiteralPath $shortArtifact -Destination $artifact -Force
if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Release archive publish failed: $artifact" }
Write-Host "Windows release step PASS: $artifact"
