[CmdletBinding()]
param(
  [ValidateSet('Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [string]$RuntimeLibraryPath='',
  [switch]$Clean
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$PreflightRoot=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'audit_windows.ps1') -SourceRoot $PreflightRoot
. (Join-Path $PSScriptRoot 'windows_paths.ps1')
$verify=@{Config=$Config;Arch=$Arch;Generator=$Generator;Jobs=$Jobs;RequireRuntime=$true}
if($Clean.IsPresent){$verify.Clean=$true}
if(-not[string]::IsNullOrWhiteSpace($RuntimeLibraryPath)){$verify.RuntimeLibraryPath=$RuntimeLibraryPath}
& (Join-Path $PSScriptRoot 'verify_windows.ps1') @verify
$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
$runtime=Join-Path $Ws.InstallDir 'bin\SuchRuntimePrivate.dll'
if(-not(Test-Path -LiteralPath $runtime -PathType Leaf)){throw "Release runtime missing from install staging: $runtime"}
$artifactDir=Join-Path $Root 'dist\artifacts'
$artifact=Join-Path $artifactDir 'Such_v1.0.0_Windows_x64_Public.zip'
New-Item -ItemType Directory -Force -Path $artifactDir|Out-Null
Remove-Item -LiteralPath $artifact -Force -ErrorAction SilentlyContinue
$packRoot=Join-Path $Ws.VersionRoot 'release-pack-windows-x64'
Remove-Item -LiteralPath $packRoot -Recurse -Force -ErrorAction SilentlyContinue
$payload=Join-Path $packRoot 'Such-v1.0.0-Windows-x64'
New-Item -ItemType Directory -Force -Path $payload|Out-Null
Copy-Item -Path (Join-Path $Ws.InstallDir '*') -Destination $payload -Recurse -Force
Compress-Archive -Path $payload -DestinationPath $artifact -CompressionLevel Optimal -Force
Write-Host "Public Windows release artifact: $artifact"
