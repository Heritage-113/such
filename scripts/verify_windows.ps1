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

function Assert-NoResidentExecutable {
  param(
    [Parameter(Mandatory=$true)][string]$ExecutablePath,
    [Parameter(Mandatory=$true)][string]$Label
  )
  $expected=[System.IO.Path]::GetFullPath($ExecutablePath)
  Start-Sleep -Milliseconds 250
  $leftovers=@()
  foreach($proc in @(Get-Process -ErrorAction SilentlyContinue)) {
    try {
      $candidate=$proc.Path
      if(-not [string]::IsNullOrWhiteSpace($candidate) -and
         [string]::Equals([System.IO.Path]::GetFullPath($candidate),$expected,[System.StringComparison]::OrdinalIgnoreCase)) {
        $leftovers += $proc
      }
    } catch {}
  }
  if($leftovers.Count -ne 0) {
    foreach($proc in $leftovers) {
      try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    }
    $ids=($leftovers | ForEach-Object { $_.Id }) -join ','
    throw "$Label left a resident Such process after smoke exit (pid=$ids)."
  }
}

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
  Assert-NoResidentExecutable -ExecutablePath $GuiPath -Label $Label
}

$buildParams=@{Config=$Config;Arch=$Arch;Generator=$Generator;Jobs=$Jobs}
if ($Clean.IsPresent) { $buildParams.Clean=$true }
if ($RequireRuntime.IsPresent) { $buildParams.RequireRuntime=$true }
if (-not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)) { $buildParams.RuntimeLibraryPath=$RuntimeLibraryPath }
& (Join-Path $PSScriptRoot 'build_windows.ps1') @buildParams
if($LASTEXITCODE -ne 0){throw "Windows build failed with exit $LASTEXITCODE"}

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
if ($RequireRuntime.IsPresent -and -not (Test-Path -LiteralPath $prod -PathType Leaf)) { throw "Missing required production runtime output: $prod" }

$old=$env:SUCH_RUNTIME_LIBRARY
$oldState=$env:SUCH_STATE_DIR
try {
  # Contract smoke: public RuntimeClient must still match the stable ABI.
  $env:SUCH_RUNTIME_LIBRARY=$stub
  & $cli 'report'
  if ($LASTEXITCODE -ne 0) { throw "CLI ABI stub smoke failed with exit $LASTEXITCODE" }
  Invoke-GuiSmoke -GuiPath $gui -Label 'GUI ABI stub smoke'

  # Product smoke: whenever a production runtime is staged, exercise that exact
  # binary. Release verification (-RequireRuntime) therefore can never pass on
  # the stub alone.
  $runProductionSmoke = $RequireRuntime.IsPresent -or -not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)
  if ($runProductionSmoke) {
    if (-not (Test-Path -LiteralPath $prod -PathType Leaf)) {
      throw "Production runtime requested for smoke verification but missing: $prod"
    }
    Assert-SuchWindowsRuntimeArchitecture -RuntimePath $prod -Arch $Arch
    if ($RequireRuntime.IsPresent) { Assert-SuchWindowsRuntimeSha256 -SourceRoot $Root -RuntimePath $prod }
    if (-not (Test-Path -LiteralPath $stageProd -PathType Leaf)) { throw "Production runtime missing from install staging: $stageProd" }
    $buildHash=(Get-FileHash -LiteralPath $prod -Algorithm SHA256).Hash
    $stageHash=(Get-FileHash -LiteralPath $stageProd -Algorithm SHA256).Hash
    if ($buildHash -ne $stageHash) { throw 'Staged Windows runtime differs from verified build runtime.' }

    $verificationState=Join-Path $Ws.VersionRoot 'verify-runtime-state'
    Remove-Item -LiteralPath $verificationState -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $verificationState | Out-Null
    $env:SUCH_STATE_DIR=$verificationState
    $env:SUCH_RUNTIME_LIBRARY=$prod
    & $cli 'report'
    if ($LASTEXITCODE -ne 0) { throw "CLI production-runtime smoke failed with exit $LASTEXITCODE" }
    Invoke-GuiSmoke -GuiPath $gui -Label 'GUI production-runtime smoke'
    Write-Host 'Windows production-runtime smoke PASS'
  } elseif (Test-Path -LiteralPath $prod -PathType Leaf) {
    Write-Host 'Production runtime smoke skipped; pass -RequireRuntime for strict release verification.'
  }
}
finally {
  if ($null -eq $old) { Remove-Item Env:SUCH_RUNTIME_LIBRARY -ErrorAction SilentlyContinue }
  else { $env:SUCH_RUNTIME_LIBRARY=$old }
  if ($null -eq $oldState) { Remove-Item Env:SUCH_STATE_DIR -ErrorAction SilentlyContinue }
  else { $env:SUCH_STATE_DIR=$oldState }
}
Write-Host 'Public Windows verification PASS'
