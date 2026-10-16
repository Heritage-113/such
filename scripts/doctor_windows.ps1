[CmdletBinding()]
param(
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [switch]$OfflineOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_entry_common.ps1')
try {
  $Root=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
  . (Join-Path $PSScriptRoot 'windows_paths.ps1')
  Write-Host "PowerShell       : $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition))"
  Write-Host "Source root      : $Root"
  Write-Host "Architecture     : $Arch"
  Write-Host "Offline-only     : $($OfflineOnly.IsPresent)"
  $cmake=Resolve-SuchWindowsTool -Name cmake
  $ctest=Resolve-SuchWindowsTool -Name ctest
  $ready=$true
  if([string]::IsNullOrWhiteSpace($cmake)){Write-Warning 'CMake is not prepared.';$ready=$false}else{Write-Host "CMake            : $cmake"}
  if([string]::IsNullOrWhiteSpace($ctest)){Write-Warning 'CTest is not prepared.';$ready=$false}else{Write-Host "CTest            : $ctest"}
  if($ready){try{$resolved=Resolve-SuchWindowsGenerator -CMakePath $cmake -Requested $Generator;Write-Host "Generator        : $resolved"}catch{Write-Warning $_.Exception.Message;$ready=$false}}
  $ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config Release -NoCreate
  Write-SuchPathDiagnostics -Workspace $ws
  Write-Host "Toolchain root   : $(Get-SuchWindowsToolchainRoot)"
  Write-Host "Offline root     : $(Get-SuchWindowsOfflineRoot)"
  Write-Host "VS offline layout: $(Get-SuchWindowsOfflineVsLayout)"
  if(-not $ready){
    $hint=if($OfflineOnly.IsPresent){'scripts\bootstrap_windows.cmd -OfflineOnly'}else{'scripts\bootstrap_windows.cmd'}
    throw "Windows prerequisites are not ready. Run: $hint"
  }
  Write-Host 'Windows build preflight PASS (diagnostic-only)'
} catch {
  Stop-SuchPipelineWithError -Entry 'doctor' -ErrorRecord $_
  exit 1
}
