[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')]
  [string]$Config = 'Release',
  [ValidateSet('x64','Win32','ARM64')]
  [string]$Arch = 'x64',
  [string]$Generator = 'Visual Studio 17 2022',
  [ValidateRange(1,64)]
  [int]$Jobs = 1,
  [switch]$Clean
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Params = @{ Config = $Config; Arch = $Arch; Generator = $Generator; Jobs = $Jobs }
if ($Clean.IsPresent) { $Params['Clean'] = $true }
& (Join-Path $PSScriptRoot 'verify_windows.ps1') @Params
