[CmdletBinding()]
param(
  [string]$LayoutPath='',
  [switch]$IncludeArm64
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_entry_common.ps1')
try {
  . (Join-Path $PSScriptRoot 'windows_paths.ps1')
  $ToolchainRoot=Get-SuchWindowsToolchainRoot
  $OfflineRoot=Get-SuchWindowsOfflineRoot
  if([string]::IsNullOrWhiteSpace($LayoutPath)){$LayoutPath=Get-SuchWindowsOfflineVsLayout}
  $LayoutPath=[System.IO.Path]::GetFullPath($LayoutPath)
  if($LayoutPath.Length -ge 64){throw ('VS offline layout root must be short (<64 chars): {0}. Set SUCH_WINDOWS_OFFLINE_ROOT=C:\STC or pass -LayoutPath C:\STC\VS17.' -f $LayoutPath)}

  $cmakeName='cmake-3.31.12-windows-x86_64.zip'
  $cmakeHash='0c4baa40f28b3f8225eb3fdf6946c987b4fe901403b4eaf2fbbd9378100aaa0c'
  $cmakePath=Join-Path $OfflineRoot $cmakeName
  $bootstrapDir=Join-Path $ToolchainRoot 'bootstrap'
  $vsBootstrap=Join-Path $bootstrapDir 'vs_BuildTools.exe'
  New-Item -ItemType Directory -Force -Path $OfflineRoot,$bootstrapDir | Out-Null
  [System.Net.ServicePointManager]::SecurityProtocol=[System.Net.SecurityProtocolType]::Tls12
  if(-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)){
    Invoke-WebRequest -Uri ('https://cmake.org/files/v3.31/{0}' -f $cmakeName) -OutFile $cmakePath -UseBasicParsing
  }
  $actual=(Get-FileHash -LiteralPath $cmakePath -Algorithm SHA256).Hash.ToLowerInvariant()
  if($actual -ne $cmakeHash){throw ('CMake archive SHA-256 mismatch. Expected {0}, actual {1}' -f $cmakeHash,$actual)}
  if(-not (Test-Path -LiteralPath $vsBootstrap -PathType Leaf)){
    Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_buildtools.exe' -OutFile $vsBootstrap -UseBasicParsing
  }
  $signature=Get-AuthenticodeSignature -FilePath $vsBootstrap
  if($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid){throw ('VS2022 bootstrapper signature is not valid: {0}' -f $signature.Status)}
  $args=@('--layout',('"{0}"' -f $LayoutPath),'--lang','en-US','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended')
  if($IncludeArm64.IsPresent){$args += @('--add','Microsoft.VisualStudio.Component.VC.Tools.ARM64')}
  $p=Start-Process -FilePath $vsBootstrap -ArgumentList ($args -join ' ') -Verb RunAs -Wait -PassThru
  if($null -eq $p){throw 'VS2022 offline layout process could not be started.'}
  if($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010){throw ('VS2022 offline layout failed with exit {0}' -f $p.ExitCode)}
  Write-Host ('Offline CMake      : {0}' -f $cmakePath)
  Write-Host ('Offline VS layout  : {0}' -f $LayoutPath)
  Write-Host 'Windows offline toolchain preparation PASS.'
} catch {
  Stop-SuchPipelineWithError -Entry 'offline-toolchain' -ErrorRecord $_
  exit 1
}
