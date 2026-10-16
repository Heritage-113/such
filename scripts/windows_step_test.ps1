[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$RuntimeLibraryPath='',
  [switch]$RequireRuntime
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'windows_paths.ps1')
. (Join-Path $PSScriptRoot 'windows_native.ps1')
. (Join-Path $PSScriptRoot 'windows_artifact_state.ps1')

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
    foreach($proc in $leftovers) { try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {} }
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

$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$Version=Get-SuchVersion -SourceRoot $Root
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
$CTest=Resolve-SuchWindowsTool -Name ctest
if ([string]::IsNullOrWhiteSpace($CTest)) { throw 'ctest.exe is unavailable. Run bootstrap_windows.cmd first.' }

$bin=Join-Path $Ws.BuildDir $Config
$stub=Join-Path $bin 'SuchRuntimeStub.dll'
$gui=Join-Path $bin 'Such.exe'
$cli=Join-Path $bin 'SuchCLI.exe'
foreach ($path in @($stub,$gui,$cli)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing build output. Run build_windows.cmd first: $path" }
}

Invoke-SuchNative -FilePath $CTest -Arguments @('--test-dir',$Ws.BuildDir,'-C',$Config,'--output-on-failure') -Step 'CTest'

$oldRuntime=$env:SUCH_RUNTIME_LIBRARY
$oldState=$env:SUCH_STATE_DIR
$runtime=$null
try {
  $env:SUCH_RUNTIME_LIBRARY=$stub
  [void](Invoke-SuchNativeCapture -FilePath $cli -Arguments @('report') -Step 'CLI ABI stub smoke')
  Invoke-GuiSmoke -GuiPath $gui -Label 'GUI ABI stub smoke'

  $runtime=$null
  if($Arch -eq 'x64' -or -not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)){
    $runtime=Resolve-SuchWindowsRuntime -SourceRoot $Root -ExplicitPath $RuntimeLibraryPath
  }
  if ($null -eq $runtime) {
    if ($RequireRuntime.IsPresent) { throw 'Required Windows production runtime is missing.' }
    Write-Host 'Production runtime smoke: skipped (runtime not present)'
  } else {
    Assert-SuchWindowsRuntimeArchitecture -RuntimePath $runtime -Arch $Arch
    if ($RequireRuntime.IsPresent -and $Arch -ne 'x64') {
      throw ("Such v{0} release runtime is available for Windows x64 only." -f $Version)
    }
    # Even optional production-runtime smoke must not attest an unapproved DLL.
    if ($Arch -eq 'x64') {
      Assert-SuchWindowsRuntimeSha256 -SourceRoot $Root -RuntimePath $runtime
    }

    $verificationState=Join-Path $Ws.StateDir 'runtime-smoke'
    Remove-Item -LiteralPath $verificationState -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $verificationState | Out-Null
    $env:SUCH_STATE_DIR=$verificationState
    $env:SUCH_RUNTIME_LIBRARY=$runtime

    [void](Invoke-SuchNativeCapture -FilePath $cli -Arguments @('report') -Step 'CLI production-runtime smoke')

    $insideRoot=Join-Path $Ws.StateDir 'inside-root'
    Remove-Item -LiteralPath $insideRoot -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $insideRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $insideRoot 'content_probe.txt') -Value 'alpha SUCH_INSIDE_SENTINEL_918273 warranty omega' -Encoding UTF8
    [void](Invoke-SuchNativeCapture -FilePath $cli -Arguments @(("/drive {0}" -f $insideRoot)) -Step 'Windows /inside fixture indexing')
    $insideResult=Invoke-SuchNativeCapture -FilePath $cli -Arguments @('/inside SUCH_INSIDE_SENTINEL_918273') -Step 'Windows /inside production-runtime query'
    $insideOutput=[string]$insideResult.StdOut
    if ($insideOutput -notmatch 'content_probe\.txt' -or $insideOutput -notmatch 'SUCH_INSIDE_SENTINEL_918273') {
      throw 'Windows /inside production-runtime result mismatch.'
    }
    Write-Host 'Windows /inside production-runtime search PASS'

    Invoke-GuiSmoke -GuiPath $gui -Label 'GUI production-runtime smoke'
    Write-Host 'Windows production-runtime smoke PASS'
  }
}
finally {
  if ($null -eq $oldRuntime) { Remove-Item Env:SUCH_RUNTIME_LIBRARY -ErrorAction SilentlyContinue } else { $env:SUCH_RUNTIME_LIBRARY=$oldRuntime }
  if ($null -eq $oldState) { Remove-Item Env:SUCH_STATE_DIR -ErrorAction SilentlyContinue } else { $env:SUCH_STATE_DIR=$oldState }
}

$runtimePathForManifest=if($null -eq $runtime){''}else{[string]$runtime}
[void](Write-SuchVerificationManifest -Workspace $Ws -Arch $Arch -Config $Config -GuiPath $gui -CliPath $cli -StubPath $stub -RuntimePath $runtimePathForManifest -RuntimeRequired:$RequireRuntime)
Write-Host 'Windows test step PASS'
