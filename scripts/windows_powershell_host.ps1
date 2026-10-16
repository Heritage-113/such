[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('x64','x86','arm64')][string]$HostArch,
  [switch]$OfflineOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

# This file is the only Windows PowerShell 5.1 compatibility bridge in the
# Windows launcher path. Product/build workflows execute under the provisioned
# PowerShell 7 host after this helper returns.
$AssetAuthority=Join-Path $PSScriptRoot 'windows_powershell_assets.txt'
if(-not(Test-Path -LiteralPath $AssetAuthority -PathType Leaf)){
  throw ('PowerShell asset authority is missing: {0}' -f $AssetAuthority)
}

$asset=@{}
foreach($line in [System.IO.File]::ReadAllLines($AssetAuthority)){
  $trimmed=$line.Trim()
  if([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith('#')){continue}
  $split=$trimmed.IndexOf('=')
  if($split -le 0){throw ('Invalid PowerShell asset metadata line: {0}' -f $trimmed)}
  $asset[$trimmed.Substring(0,$split)]=$trimmed.Substring($split+1)
}

$required=@('PowerShellVersion')
switch($HostArch){
  'x64'   {$required+=@('PowerShellX64Zip','PowerShellX64Sha256','PowerShellX64Url');$prefix='PowerShellX64'}
  'arm64' {$required+=@('PowerShellArm64Zip','PowerShellArm64Sha256','PowerShellArm64Url');$prefix='PowerShellArm64'}
  'x86'   {$required+=@('PowerShellX86Zip','PowerShellX86Sha256','PowerShellX86Url');$prefix='PowerShellX86'}
}
foreach($key in $required){if(-not $asset.ContainsKey($key) -or [string]::IsNullOrWhiteSpace([string]$asset[$key])){throw ('Missing PowerShell asset metadata: {0}' -f $key)}}

$version=[string]$asset['PowerShellVersion']
$archiveName=[string]$asset[$prefix+'Zip']
$expectedSha=([string]$asset[$prefix+'Sha256']).ToLowerInvariant()
$url=[string]$asset[$prefix+'Url']

$profile=[System.Environment]::GetFolderPath([System.Environment+SpecialFolder]::UserProfile)
if([string]::IsNullOrWhiteSpace($profile)){throw 'Could not resolve the current user profile for the Such PowerShell host cache.'}
$toolchainRoot=if(-not[string]::IsNullOrWhiteSpace($env:SUCH_TOOLCHAIN_ROOT)){[System.IO.Path]::GetFullPath($env:SUCH_TOOLCHAIN_ROOT)}else{[System.IO.Path]::GetFullPath((Join-Path $profile 'STC'))}
$downloadRoot=Join-Path $toolchainRoot 'downloads'
$offlineRoot=if(-not[string]::IsNullOrWhiteSpace($env:SUCH_WINDOWS_OFFLINE_ROOT)){[System.IO.Path]::GetFullPath($env:SUCH_WINDOWS_OFFLINE_ROOT)}else{Join-Path $toolchainRoot 'offline'}
$installRoot=Join-Path (Join-Path (Join-Path $toolchainRoot 'powershell') $version) ('win-{0}' -f $HostArch)
$pwsh=Join-Path $installRoot 'pwsh.exe'
$marker=Join-Path $installRoot '.such-powershell-sha256'

function Write-Status([string]$Message){[Console]::Error.WriteLine('[SUCH:PWSH] '+$Message)}
function Get-Sha256([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Test-VerifiedHost {
  if(-not(Test-Path -LiteralPath $pwsh -PathType Leaf)){return $false}
  if(-not(Test-Path -LiteralPath $marker -PathType Leaf)){return $false}
  $stored=([System.IO.File]::ReadAllText($marker)).Trim().ToLowerInvariant()
  return $stored -eq $expectedSha
}

if(Test-VerifiedHost){Write-Status ('PowerShell {0} ready: {1}' -f $version,$pwsh);exit 0}

$sourceRoot=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bundledArchive=Join-Path $sourceRoot ('third_party\windows\bootstrap\{0}' -f $archiveName)
$sidecarArchive=Join-Path $PSScriptRoot $archiveName
$offlineArchive=Join-Path $offlineRoot $archiveName
$cachedArchive=Join-Path $downloadRoot $archiveName
$archive=$null
foreach($candidate in @($bundledArchive,$sidecarArchive,$offlineArchive,$cachedArchive)){
  if(-not(Test-Path -LiteralPath $candidate -PathType Leaf)){continue}
  $actual=Get-Sha256 $candidate
  if($actual -ne $expectedSha){
    if($candidate -eq $cachedArchive){Remove-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue;continue}
    throw ('PowerShell archive SHA-256 mismatch: {0}. Expected {1}, actual {2}' -f $candidate,$expectedSha,$actual)
  }
  $archive=$candidate
  break
}

if($null -eq $archive){
  if($OfflineOnly.IsPresent){
    throw ('PowerShell {0} is not cached for offline use. Supply {1} in third_party\windows\bootstrap, beside the launcher, or under {2}.' -f $version,$archiveName,$offlineRoot)
  }
  New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
  $partial=$cachedArchive+'.partial'
  Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
  [System.Net.ServicePointManager]::SecurityProtocol=[System.Net.SecurityProtocolType]::Tls12
  Write-Status ('Downloading PowerShell {0} ({1})' -f $version,$HostArch)
  Invoke-WebRequest -Uri $url -OutFile $partial -UseBasicParsing
  $actual=Get-Sha256 $partial
  if($actual -ne $expectedSha){
    Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
    throw ('Downloaded PowerShell SHA-256 mismatch. Expected {0}, actual {1}' -f $expectedSha,$actual)
  }
  Move-Item -LiteralPath $partial -Destination $cachedArchive -Force
  $archive=$cachedArchive
}

$parent=Split-Path -Parent $installRoot
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$temp=Join-Path $parent ('.extract-'+[Guid]::NewGuid().ToString('N'))
try{
  New-Item -ItemType Directory -Force -Path $temp | Out-Null
  Expand-Archive -LiteralPath $archive -DestinationPath $temp -Force
  $tempPwsh=Join-Path $temp 'pwsh.exe'
  if(-not(Test-Path -LiteralPath $tempPwsh -PathType Leaf)){throw ('PowerShell archive did not contain pwsh.exe: {0}' -f $archive)}
  [System.IO.File]::WriteAllText((Join-Path $temp '.such-powershell-sha256'),$expectedSha,[System.Text.UTF8Encoding]::new($false))
  Remove-Item -LiteralPath $installRoot -Recurse -Force -ErrorAction SilentlyContinue
  Move-Item -LiteralPath $temp -Destination $installRoot
}catch{
  Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
  throw
}
if(-not(Test-VerifiedHost)){throw ('PowerShell host materialization failed: {0}' -f $pwsh)}
Write-Status ('PowerShell {0} provisioned: {1}' -f $version,$pwsh)
