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
$PreflightRoot=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'audit_windows.ps1') -SourceRoot $PreflightRoot
. (Join-Path $PSScriptRoot 'windows_paths.ps1')

function Invoke-GuiSmoke {
  param(
    [Parameter(Mandatory=$true)][string]$GuiPath,
    [Parameter(Mandatory=$true)][string]$Label
  )
  $p=Start-Process -FilePath $GuiPath -ArgumentList '--smoke' -PassThru
  if (-not $p.WaitForExit(15000)) {
    try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
    throw "$Label timed out."
  }
  if ($p.ExitCode -ne 0) { throw "$Label failed with exit $($p.ExitCode)" }
}

$buildParams=@{Config=$Config;Arch=$Arch;Generator=$Generator;Jobs=$Jobs}
if ($Clean.IsPresent) { $buildParams.Clean=$true }
if ($RequireRuntime.IsPresent) { $buildParams.RequireRuntime=$true }
if (-not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)) { $buildParams.RuntimeLibraryPath=$RuntimeLibraryPath }
& (Join-Path $PSScriptRoot 'build_windows.ps1') @buildParams

$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
$bin=Join-Path $Ws.BuildDir $Config
$stageBin=Join-Path $Ws.InstallDir 'bin'
$stub=Join-Path $bin 'SuchRuntimeStub.dll'
$prod=Join-Path $bin 'SuchRuntimePrivate.dll'
$stageProd=Join-Path $stageBin 'SuchRuntimePrivate.dll'
$gui=Join-Path $bin 'Such.exe'
$cli=Join-Path $bin 'SuchCLI.exe'
foreach ($p in @($stub,$gui,$cli)) {
  if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing build output: $p" }
}
if ($RequireRuntime.IsPresent -and -not (Test-Path -LiteralPath $prod -PathType Leaf)) {
  throw "Missing required production runtime output: $prod"
}

$old=$env:SUCH_RUNTIME_LIBRARY
try {
  # Contract smoke: public RuntimeClient must still match the stable ABI.
  $env:SUCH_RUNTIME_LIBRARY=$stub
  & $cli 'report'
  if ($LASTEXITCODE -ne 0) { throw "CLI ABI stub smoke failed with exit $LASTEXITCODE" }
  Invoke-GuiSmoke -GuiPath $gui -Label 'GUI ABI stub smoke'

  # Product smoke: whenever a production runtime is staged, exercise that exact
  # binary. Release verification (-RequireRuntime) therefore can never pass on
  # the stub alone.
  if (Test-Path -LiteralPath $prod -PathType Leaf) {
    Assert-SuchWindowsRuntimeArchitecture -RuntimePath $prod -Arch $Arch
    if ($RequireRuntime.IsPresent) { Assert-SuchWindowsRuntimeSha256 -SourceRoot $Root -RuntimePath $prod }
    if (-not (Test-Path -LiteralPath $stageProd -PathType Leaf)) { throw "Production runtime missing from install staging: $stageProd" }
    $buildHash=(Get-FileHash -LiteralPath $prod -Algorithm SHA256).Hash
    $stageHash=(Get-FileHash -LiteralPath $stageProd -Algorithm SHA256).Hash
    if ($buildHash -ne $stageHash) { throw 'Staged Windows runtime differs from verified build runtime.' }

    $env:SUCH_RUNTIME_LIBRARY=$prod
    & $cli 'report'
    if ($LASTEXITCODE -ne 0) { throw "CLI production-runtime smoke failed with exit $LASTEXITCODE" }
    Invoke-GuiSmoke -GuiPath $gui -Label 'GUI production-runtime smoke'
    Write-Host 'Windows production-runtime smoke PASS'
  }
}
finally {
  if ($null -eq $old) { Remove-Item Env:SUCH_RUNTIME_LIBRARY -ErrorAction SilentlyContinue }
  else { $env:SUCH_RUNTIME_LIBRARY=$old }
}
Write-Host 'Public Windows verification PASS'
