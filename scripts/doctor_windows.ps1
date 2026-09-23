[CmdletBinding()]
param(
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$Generator='Auto'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

trap {
  Write-Host ''
  Write-Host ('[SUCH:FATAL] ' + $_.Exception.Message) -ForegroundColor Red
  if ($_.InvocationInfo -and $_.InvocationInfo.PositionMessage) { Write-Host $_.InvocationInfo.PositionMessage -ForegroundColor DarkRed }
  if (-not [string]::IsNullOrWhiteSpace($_.ScriptStackTrace)) { Write-Host $_.ScriptStackTrace -ForegroundColor DarkRed }
  exit 1
}
$PreflightRoot=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'audit_windows.ps1') -SourceRoot $PreflightRoot
. (Join-Path $PSScriptRoot 'windows_paths.ps1')
$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
Write-Host "PowerShell       : $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition))"
Write-Host "Source root      : $Root"
$cmake=Resolve-SuchWindowsTool -Name cmake
if([string]::IsNullOrWhiteSpace($cmake)){throw 'cmake.exe not found.'}
$ctest=Resolve-SuchWindowsTool -Name ctest
if([string]::IsNullOrWhiteSpace($ctest)){throw 'ctest.exe not found.'}
$resolved=Resolve-SuchWindowsGenerator -CMakePath $cmake -Requested $Generator
Write-Host "CMake            : $cmake"
Write-Host "CTest            : $ctest"
Write-Host "Generator        : $resolved"
Write-Host "Architecture     : $Arch"
$ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config Release
Write-SuchPathDiagnostics -Workspace $ws
$runtime=Resolve-SuchWindowsRuntime -SourceRoot $Root
if($null -ne $runtime){
  Assert-SuchWindowsRuntimeArchitecture -RuntimePath $runtime -Arch $Arch
  Write-Host "Runtime          : $runtime"
}else{
  Write-Host 'Runtime          : not present (frontend-only build is still valid)'
}
Write-Host 'Windows build preflight PASS'
