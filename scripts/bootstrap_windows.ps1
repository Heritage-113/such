[CmdletBinding()]
param(
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [switch]$OfflineOnly,
  [switch]$SkipBuildToolsInstall
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'windows_paths.ps1')
$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$ToolchainRoot=Get-SuchWindowsToolchainRoot
$CMakeVersion='3.31.12'
$CMakeArchiveName='cmake-3.31.12-windows-x86_64.zip'
$CMakeExpectedSha256='0c4baa40f28b3f8225eb3fdf6946c987b4fe901403b4eaf2fbbd9378100aaa0c'
$CMakeUrl=('https://cmake.org/files/v3.31/{0}' -f $CMakeArchiveName)
$BundledCMakeArchive=Join-Path $Root ('third_party\windows\bootstrap\{0}' -f $CMakeArchiveName)
$OfflineRoot=Get-SuchWindowsOfflineRoot
$OfflineCMakeArchive=Join-Path $OfflineRoot $CMakeArchiveName
$CMakeDownload=Join-Path $ToolchainRoot $CMakeArchiveName
$CMakeExtractRoot=Join-Path $ToolchainRoot ('cmake-{0}' -f $CMakeVersion)
$CMakeExe=Join-Path $CMakeExtractRoot ('cmake-{0}-windows-x86_64\bin\cmake.exe' -f $CMakeVersion)
$BuildToolsOnlineUrl='https://aka.ms/vs/17/release/vs_buildtools.exe'
$BuildToolsCache=Join-Path $ToolchainRoot 'vs_BuildTools.exe'
$OfflineLayout=Get-SuchWindowsOfflineVsLayout
$BuildToolsInstallPath=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools'

function Get-SuchVsWherePath {
  $pf86=${env:ProgramFiles(x86)}
  if ([string]::IsNullOrWhiteSpace($pf86)) { return $null }
  $candidate=Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
  return $null
}

function Get-SuchVs2022CppInstallPath {
  $vswhere=Get-SuchVsWherePath
  if ([string]::IsNullOrWhiteSpace($vswhere)) { return $null }
  $required=if($Arch -eq 'ARM64'){'Microsoft.VisualStudio.Component.VC.Tools.ARM64'}else{'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'}
  try {
    $found=@(& $vswhere -latest -products * -version '[17.0,18.0)' -requires $required -property installationPath 2>$null | Select-Object -First 1)
    if($found.Count -eq 0 -or [string]::IsNullOrWhiteSpace([string]$found[0])){return $null}
    return [System.IO.Path]::GetFullPath(([string]$found[0]).Trim())
  } catch { return $null }
}

function Test-SuchVs2022CppToolchain {
  return -not [string]::IsNullOrWhiteSpace((Get-SuchVs2022CppInstallPath))
}

function Invoke-SuchDownload {
  param([Parameter(Mandatory=$true)][string]$Uri,[Parameter(Mandatory=$true)][string]$Destination)
  if($OfflineOnly.IsPresent){throw ('Offline bootstrap asset is missing: {0}' -f $Destination)}
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
  [System.Net.ServicePointManager]::SecurityProtocol=[System.Net.SecurityProtocolType]::Tls12
  Write-Host ('Downloading: {0}' -f $Uri)
  Invoke-WebRequest -Uri $Uri -OutFile $Destination -UseBasicParsing
}

function Assert-SuchSha256 {
  param([Parameter(Mandatory=$true)][string]$Path,[Parameter(Mandatory=$true)][string]$Expected)
  $actual=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
  if($actual -ne $Expected.ToLowerInvariant()){throw ('SHA-256 mismatch for {0}. Expected {1}, actual {2}' -f $Path,$Expected,$actual)}
}

function Test-SuchExistingCMake {
  $candidate=Resolve-SuchWindowsTool -Name cmake
  if([string]::IsNullOrWhiteSpace($candidate)){return $null}
  try {
    $line=(& $candidate --version 2>$null | Select-Object -First 1)
    $m=[regex]::Match([string]$line,'(\d+)\.(\d+)\.(\d+)')
    if(-not $m.Success){return $null}
    $version=[System.Version](('{0}.{1}.{2}' -f $m.Groups[1].Value,$m.Groups[2].Value,$m.Groups[3].Value))
    if($version -lt [System.Version]'3.20.0'){return $null}
    return $candidate
  } catch { return $null }
}

function Ensure-SuchCMake {
  $existing=Test-SuchExistingCMake
  if(-not [string]::IsNullOrWhiteSpace($existing)){
    Write-Host ('CMake ready      : {0}' -f $existing)
    return
  }
  if(Test-Path -LiteralPath $CMakeExe -PathType Leaf){Write-Host ('Portable CMake   : {0}' -f $CMakeExe); return}

  $archive=$null
  foreach($candidate in @($BundledCMakeArchive,$OfflineCMakeArchive,$CMakeDownload)){
    if(Test-Path -LiteralPath $candidate -PathType Leaf){$archive=$candidate;break}
  }
  if($null -eq $archive){
    Invoke-SuchDownload -Uri $CMakeUrl -Destination $CMakeDownload
    $archive=$CMakeDownload
  }
  Assert-SuchSha256 -Path $archive -Expected $CMakeExpectedSha256
  Remove-Item -LiteralPath $CMakeExtractRoot -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $CMakeExtractRoot | Out-Null
  Expand-Archive -LiteralPath $archive -DestinationPath $CMakeExtractRoot -Force
  if(-not (Test-Path -LiteralPath $CMakeExe -PathType Leaf)){throw ('Portable CMake extraction did not produce cmake.exe: {0}' -f $CMakeExe)}
  Write-Host ('Portable CMake   : {0}' -f $CMakeExe)
}

function Get-SuchOfflineBuildToolsBootstrapper {
  foreach($name in @('vs_BuildTools.exe','vs_setup.exe')){
    $candidate=Join-Path $OfflineLayout $name
    if(Test-Path -LiteralPath $candidate -PathType Leaf){return $candidate}
  }
  return $null
}

function Install-SuchBuildTools {
  if($SkipBuildToolsInstall.IsPresent){return}
  $installed=Get-SuchVs2022CppInstallPath
  if(-not [string]::IsNullOrWhiteSpace($installed)){
    Write-Host ('MSVC 2022 ready  : {0}' -f $installed)
    return
  }

  $bootstrapper=Get-SuchOfflineBuildToolsBootstrapper
  $offline=-not [string]::IsNullOrWhiteSpace($bootstrapper)
  if(-not $offline){
    if($OfflineOnly.IsPresent){throw ('VS2022 C++ Build Tools are missing and no offline layout exists at {0}' -f $OfflineLayout)}
    if(-not (Test-Path -LiteralPath $BuildToolsCache -PathType Leaf)){Invoke-SuchDownload -Uri $BuildToolsOnlineUrl -Destination $BuildToolsCache}
    $bootstrapper=$BuildToolsCache
  }

  $signature=Get-AuthenticodeSignature -FilePath $bootstrapper
  if($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid){throw ('Visual Studio bootstrapper signature is not valid: {0}' -f $signature.Status)}

  $arguments=@('--quiet','--wait','--norestart','--installPath',('"{0}"' -f $BuildToolsInstallPath),'--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended')
  if($Arch -eq 'ARM64'){$arguments += @('--add','Microsoft.VisualStudio.Component.VC.Tools.ARM64')}
  if($offline){$arguments += '--noWeb'}else{$arguments += '--nocache'}
  Write-Host 'Installing Visual Studio 2022 C++ Build Tools. One UAC approval may be required.'
  $process=Start-Process -FilePath $bootstrapper -ArgumentList ($arguments -join ' ') -Verb RunAs -Wait -PassThru
  if($null -eq $process){throw 'Visual Studio Build Tools installer could not be started.'}
  if($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010){throw ('Visual Studio Build Tools installer failed with exit {0}' -f $process.ExitCode)}
  if(-not (Test-SuchVs2022CppToolchain)){throw 'VS2022 installer completed, but the requested C++ toolchain is still not discoverable.'}
}

New-Item -ItemType Directory -Force -Path $ToolchainRoot,$OfflineRoot | Out-Null
Write-Host ('Toolchain cache   : {0}' -f $ToolchainRoot)
Write-Host ('Offline root     : {0}' -f $OfflineRoot)
Write-Host ('VS2022 layout    : {0}' -f $OfflineLayout)
Ensure-SuchCMake
Install-SuchBuildTools
Write-Host 'Windows dependency bootstrap PASS.'
