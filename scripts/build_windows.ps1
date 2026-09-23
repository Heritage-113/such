[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [string]$RuntimeLibraryPath='',
  [switch]$RequireRuntime,
  [switch]$Clean
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

function Invoke-SuchNative {
  param(
    [Parameter(Mandatory=$true)][string]$FilePath,
    [Parameter(Mandatory=$true)][string[]]$Arguments,
    [Parameter(Mandatory=$true)][string]$Step
  )
  Write-Host ("[RUN] {0} {1}" -f $FilePath, ($Arguments -join ' '))
  $startInfo=[System.Diagnostics.ProcessStartInfo]::new()
  $startInfo.FileName=$FilePath
  $startInfo.UseShellExecute=$false
  foreach($argument in $Arguments){[void]$startInfo.ArgumentList.Add($argument)}
  $pathValue=$env:PATH
  $startInfo.Environment.Clear()
  foreach($item in Get-ChildItem Env:){
    if($item.Name -ieq 'PATH'){continue}
    $startInfo.Environment[$item.Name]=$item.Value
  }
  $startInfo.Environment['Path']=$pathValue
  $startInfo.Environment['MSBUILDDISABLENODEREUSE']='1'
  $process=[System.Diagnostics.Process]::Start($startInfo)
  $process.WaitForExit()
  $exitCode=$process.ExitCode
  if ($exitCode -ne 0) { throw "$Step failed with exit $exitCode" }
}

$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot

$CMake=Resolve-SuchWindowsTool -Name cmake
if ([string]::IsNullOrWhiteSpace($CMake)) {
  throw 'cmake.exe was not found. Install CMake or Visual Studio/Build Tools with Desktop development with C++.'
}
$CTest=Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $CTest -PathType Leaf)) { $CTest=Resolve-SuchWindowsTool -Name ctest }
if ([string]::IsNullOrWhiteSpace($CTest)) { throw 'ctest.exe was not found beside cmake.exe or in an installed CMake/Visual Studio toolchain.' }
$ResolvedGenerator=Resolve-SuchWindowsGenerator -CMakePath $CMake -Requested $Generator
Write-Host "CMake           : $CMake"
Write-Host "CTest           : $CTest"
Write-Host "Generator       : $ResolvedGenerator"
Write-Host "Architecture    : $Arch"
Write-Host "Configuration   : $Config"

$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
Write-SuchPathDiagnostics -Workspace $Ws

if ($Clean.IsPresent) {
  Remove-Item -LiteralPath $Ws.BuildDir,$Ws.InstallDir -Recurse -Force -ErrorAction SilentlyContinue
} elseif (-not (Test-SuchCMakeCacheMatches -BuildDir $Ws.BuildDir -Generator $ResolvedGenerator -Arch $Arch -SourcePath $Ws.CMakeSource)) {
  Write-Warning 'Stale/incompatible CMake cache detected. Recreating the Windows build directory automatically.'
  Remove-Item -LiteralPath $Ws.BuildDir -Recurse -Force -ErrorAction SilentlyContinue
}

$cmakeArgs=@(
  '-S',$Ws.CMakeSource,
  '-B',$Ws.BuildDir,
  '-G',$ResolvedGenerator,
  '-A',$Arch,
  '-DSUCH_BUILD_GUI=ON',
  '-DSUCH_BUILD_CLI=ON',
  '-DSUCH_BUILD_TESTS=ON',
  '-DSUCH_STRICT_WARNINGS=ON',
  '-DBUILD_TESTING=ON'
)
Invoke-SuchNative -FilePath $CMake -Arguments $cmakeArgs -Step 'CMake configure'
Invoke-SuchNative -FilePath $CMake -Arguments @('--build',$Ws.BuildDir,'--config',$Config,'--parallel',"$Jobs") -Step 'CMake build'
Invoke-SuchNative -FilePath $CTest -Arguments @('--test-dir',$Ws.BuildDir,'-C',$Config,'--output-on-failure') -Step 'CTest'
Invoke-SuchNative -FilePath $CMake -Arguments @('--install',$Ws.BuildDir,'--config',$Config,'--prefix',$Ws.InstallDir) -Step 'Install staging'

$runtime=Resolve-SuchWindowsRuntime -SourceRoot $Root -ExplicitPath $RuntimeLibraryPath
if ($null -ne $runtime) {
  Assert-SuchWindowsRuntimeArchitecture -RuntimePath $runtime -Arch $Arch
  if ($RequireRuntime.IsPresent) {
    if ($Arch -ne 'x64') { throw 'Such v1.1.7 release runtime is available for Windows x64 only.' }
    Assert-SuchWindowsRuntimeSha256 -SourceRoot $Root -RuntimePath $runtime
  }
  $buildBin=Join-Path $Ws.BuildDir $Config
  $stageBin=Join-Path $Ws.InstallDir 'bin'
  New-Item -ItemType Directory -Force -Path $buildBin,$stageBin | Out-Null
  Copy-Item -LiteralPath $runtime -Destination (Join-Path $buildBin 'SuchRuntimePrivate.dll') -Force
  Copy-Item -LiteralPath $runtime -Destination (Join-Path $stageBin 'SuchRuntimePrivate.dll') -Force
  Write-Host 'Private runtime staged beside Such.exe and SuchCLI.exe.'
} else {
  if ($RequireRuntime.IsPresent) { throw 'Required Windows production runtime is missing. Expected .runtime\windows-x64\SuchRuntimePrivate.dll or -RuntimeLibraryPath.' }
  Write-Host 'Production runtime: not present (frontend-only build).'
}


Write-Host "Public Windows build complete: $($Ws.BuildDir)"
