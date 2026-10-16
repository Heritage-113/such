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

. (Join-Path $PSScriptRoot 'windows_paths.ps1')
. (Join-Path $PSScriptRoot 'windows_native.ps1')
. (Join-Path $PSScriptRoot 'windows_artifact_state.ps1')

$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$CMake=Resolve-SuchWindowsTool -Name cmake
if ([string]::IsNullOrWhiteSpace($CMake)) { throw 'cmake.exe is unavailable. Run bootstrap_windows.cmd first.' }
$ResolvedGenerator=Resolve-SuchWindowsGenerator -CMakePath $CMake -Requested $Generator
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
Write-SuchPathDiagnostics -Workspace $Ws
Write-Host "CMake           : $CMake"
Write-Host "Generator       : $ResolvedGenerator"
Write-Host "Architecture    : $Arch"
Write-Host "Configuration   : $Config"

if ($Clean.IsPresent) {
  Remove-Item -LiteralPath $Ws.BuildDir -Recurse -Force -ErrorAction SilentlyContinue
  Clear-SuchWindowsArtifactState -Workspace $Ws
} elseif (-not (Test-SuchCMakeCacheMatches -BuildDir $Ws.BuildDir -Generator $ResolvedGenerator -Arch $Arch -SourcePath $Ws.CMakeSource)) {
  Write-Warning 'Stale/incompatible CMake cache detected. Recreating only the Windows build directory.'
  Remove-Item -LiteralPath $Ws.BuildDir -Recurse -Force -ErrorAction SilentlyContinue
  Clear-SuchWindowsArtifactState -Workspace $Ws
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

$bin=Join-Path $Ws.BuildDir $Config
foreach($required in @('Such.exe','SuchCLI.exe','SuchRuntimeStub.dll')) {
  $path=Join-Path $bin $required
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Build output missing: $path" }
}

# The Public build is valid without a Private runtime. Production installation
# and release still require a pinned, independently verified Fix5 DLL. Accept
# the same explicit runtime path used by the verify/install entry points.
$productionRuntime=$null
if ($Arch -eq 'x64' -or -not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)) {
  $productionRuntime=Resolve-SuchWindowsRuntime -SourceRoot $Root -ExplicitPath $RuntimeLibraryPath
}
$buildRuntime=Join-Path $bin 'SuchRuntimePrivate.dll'
if ($null -eq $productionRuntime) {
  # An earlier build may have mirrored the DLL. Never leave a stale, unverified
  # production runtime beside freshly built Public binaries.
  Remove-Item -LiteralPath $buildRuntime -Force -ErrorAction SilentlyContinue
  if ($RequireRuntime.IsPresent) {
    throw 'Production runtime required. Supply an approved DLL in runtime/windows-x64/SuchRuntimePrivate.dll or use -RuntimeLibraryPath <full DLL path>.'
  }
  Write-Host 'Public-only build: production runtime absent (ABI stub is built for verification; installation requires Fix5 DLL).'
} else {
  Assert-SuchWindowsRuntimeArchitecture -RuntimePath $productionRuntime -Arch $Arch
  if ($Arch -eq 'x64') {
    Assert-SuchWindowsRuntimeSha256 -SourceRoot $Root -RuntimePath $productionRuntime
  }
  Copy-Item -LiteralPath $productionRuntime -Destination $buildRuntime -Force
  if ((Get-SuchFileSha256 -Path $buildRuntime) -ne (Get-SuchFileSha256 -Path $productionRuntime)) {
    throw "Build runtime mirror verification failed: $buildRuntime"
  }
}

# Any successful rebuild invalidates previous test/stage attestations.
Clear-SuchWindowsArtifactState -Workspace $Ws
Write-Host "Windows build step PASS: $($Ws.BuildDir)"
