[CmdletBinding()]
param(
  [switch]$KeepFonts,
  [switch]$PurgeSecurityState,
  [switch]$LegacyMachineCleanupOnly
)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest

trap {
  Write-Host ''
  Write-Host ('[SUCH:FAIL][uninstall] ' + $_.Exception.Message) -ForegroundColor Red
  if ($_.InvocationInfo -and $_.InvocationInfo.PositionMessage) { Write-Host $_.InvocationInfo.PositionMessage -ForegroundColor DarkRed }
  if (-not [string]::IsNullOrWhiteSpace($_.ScriptStackTrace)) { Write-Host $_.ScriptStackTrace -ForegroundColor DarkRed }
  exit 1
}
. (Join-Path $PSScriptRoot 'windows_paths.ps1')

function Test-SuchLegacyMachineArtifacts {
  param([switch]$IncludeSecurityState)

  foreach($root in @(Get-SuchWindowsLegacyMachineInstallRoots)) {
    if(Test-Path -LiteralPath $root){ return $true }
  }
  foreach($shortcut in @(Get-SuchWindowsLegacyMachineShortcutPaths)) {
    if(Test-Path -LiteralPath $shortcut){ return $true }
  }
  foreach($registryPath in @(Get-SuchWindowsLegacyMachineRegistryPaths)) {
    if(Test-Path -LiteralPath $registryPath){ return $true }
  }

  $machinePath=[System.Environment]::GetEnvironmentVariable('Path','Machine')
  $machineParts=@($machinePath -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  foreach($legacyCommandPath in @(Get-SuchWindowsLegacyMachineCommandPaths)) {
    $legacyFull=[System.IO.Path]::GetFullPath($legacyCommandPath).TrimEnd([char[]]'\/')
    foreach($part in $machineParts) {
      if($part.TrimEnd([char[]]'\/').Equals($legacyFull,[System.StringComparison]::OrdinalIgnoreCase)){ return $true }
    }
  }

  if($IncludeSecurityState.IsPresent -and (Test-Path -LiteralPath (Get-SuchWindowsLegacyMachineSecurityStateRoot))){
    return $true
  }
  return $false
}

function Remove-SuchLegacyMachineArtifacts {
  param([switch]$PreserveFonts,[switch]$PurgeLegacySecurityState)

  if(-not(Test-SuchWindowsAdministrator)){
    throw 'Legacy machine-wide Such cleanup requires Administrator rights.'
  }

  $legacyRoots=@(Get-SuchWindowsLegacyMachineInstallRoots)
  Stop-SuchWindowsProcessesInRoots -Roots $legacyRoots

  if(-not$PreserveFonts.IsPresent){
    foreach($root in $legacyRoots){
      $manifest=Join-Path $root 'font-install-manifest.json'
      $fontUninstaller=Join-Path $root 'uninstall_user_fonts_windows.ps1'
      if((Test-Path -LiteralPath $manifest -PathType Leaf) -and (Test-Path -LiteralPath $fontUninstaller -PathType Leaf)){
        try {
          & $fontUninstaller -ManifestPath $manifest
        } catch {
          Write-Warning ("Legacy font cleanup failed for {0}: {1}" -f $root,$_.Exception.Message)
        }
      }
    }
  }

  foreach($shortcut in @(Get-SuchWindowsLegacyMachineShortcutPaths)){
    Remove-Item -LiteralPath $shortcut -Force -ErrorAction SilentlyContinue
  }
  foreach($registryPath in @(Get-SuchWindowsLegacyMachineRegistryPaths)){
    Remove-Item -LiteralPath $registryPath -Recurse -Force -ErrorAction SilentlyContinue
  }

  $machinePath=[System.Environment]::GetEnvironmentVariable('Path','Machine')
  $machineParts=@($machinePath -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
  $legacyCommandKeys=@(Get-SuchWindowsLegacyMachineCommandPaths | ForEach-Object {
    [System.IO.Path]::GetFullPath($_).TrimEnd([char[]]'\/')
  })
  $filtered=@($machineParts | Where-Object {
    $candidate=$_.TrimEnd([char[]]'\/')
    $matchesLegacy=$false
    foreach($legacyKey in $legacyCommandKeys){
      if($candidate.Equals($legacyKey,[System.StringComparison]::OrdinalIgnoreCase)){
        $matchesLegacy=$true
        break
      }
    }
    -not $matchesLegacy
  })
  if(($filtered -join ';') -ne ($machineParts -join ';')){
    [System.Environment]::SetEnvironmentVariable('Path',($filtered -join ';'),'Machine')
    Send-SuchWindowsEnvironmentChanged
  }

  if($PurgeLegacySecurityState.IsPresent){
    Remove-Item -LiteralPath (Get-SuchWindowsLegacyMachineSecurityStateRoot) -Recurse -Force -ErrorAction SilentlyContinue
  }

  foreach($root in $legacyRoots){
    if(Test-Path -LiteralPath $root){
      Write-Host "Removing legacy machine-wide Such installation: $root"
      Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction Stop
    }
  }

  $commonVendorDir=Join-Path (Get-SuchWindowsKnownFolder -Name 'CommonPrograms') 'Heritage Inc.'
  if((Test-Path -LiteralPath $commonVendorDir -PathType Container) -and
     @(Get-ChildItem -LiteralPath $commonVendorDir -Force -ErrorAction SilentlyContinue).Count -eq 0){
    Remove-Item -LiteralPath $commonVendorDir -Force -ErrorAction SilentlyContinue
  }
}

function Quote-SuchProcessArgument([string]$Value){ return ('"{0}"' -f $Value.Replace('"','\"')) }

if($LegacyMachineCleanupOnly.IsPresent){
  $preserveLegacyFonts=$KeepFonts.IsPresent
  $purgeLegacySecurity=$PurgeSecurityState.IsPresent
  Remove-SuchLegacyMachineArtifacts -PreserveFonts:$preserveLegacyFonts -PurgeLegacySecurityState:$purgeLegacySecurity
  Write-Host 'Legacy machine-wide Such cleanup complete.'
  exit 0
}

$installDir=Get-SuchWindowsInstallRoot; $fontManifest=Join-Path $installDir 'font-install-manifest.json'
$cliCommandDir=Join-Path $installDir 'cli'
$managedRoots=@($installDir) + @(Get-SuchWindowsLegacyInstallRoots) + @(Get-SuchWindowsLegacyMachineInstallRoots)
Stop-SuchWindowsProcessesInRoots -Roots $managedRoots
if(-not$KeepFonts.IsPresent -and (Test-Path -LiteralPath $fontManifest)) {
  & (Join-Path $installDir 'uninstall_user_fonts_windows.ps1') -ManifestPath $fontManifest
}
$programs=Get-SuchWindowsKnownFolder -Name 'Programs'
Remove-Item -LiteralPath (Join-Path $programs 'Heritage Inc.\Such.lnk') -Force -ErrorAction SilentlyContinue
Remove-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch' -Recurse -Force -ErrorAction SilentlyContinue
Remove-SuchWindowsCommandPath -Directory $cliCommandDir
$desktop=Join-Path (Get-SuchWindowsKnownFolder -Name 'Desktop') 'Such.lnk'; Remove-Item -LiteralPath $desktop -Force -ErrorAction SilentlyContinue
Remove-SuchWindowsLegacyArtifacts
foreach($stateRoot in @(Get-SuchWindowsLegacyStateRoots)) {
  if(Test-Path -LiteralPath $stateRoot){
    Write-Host "Removing Such user state: $stateRoot"
    Remove-Item -LiteralPath $stateRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}
if($PurgeSecurityState.IsPresent){
  Remove-Item -LiteralPath (Get-SuchWindowsSecurityStateRoot) -Recurse -Force -ErrorAction SilentlyContinue
}
$userProgramsVendorDir=Join-Path $programs 'Heritage Inc.'
if((Test-Path -LiteralPath $userProgramsVendorDir -PathType Container) -and
   @(Get-ChildItem -LiteralPath $userProgramsVendorDir -Force -ErrorAction SilentlyContinue).Count -eq 0){
  Remove-Item -LiteralPath $userProgramsVendorDir -Force -ErrorAction SilentlyContinue
}

# Historical v1.1.x releases installed machine-wide under C:\Heritage\Such and
# registered HKLM/Common Start Menu artifacts. Normal current-version uninstall remains
# current-user-only; request elevation only when those known legacy artifacts
# are actually present.
$preserveLegacyFonts=$KeepFonts.IsPresent
$purgeLegacySecurity=$PurgeSecurityState.IsPresent
if(Test-SuchLegacyMachineArtifacts -IncludeSecurityState:$purgeLegacySecurity){
  if(Test-SuchWindowsAdministrator){
    Remove-SuchLegacyMachineArtifacts -PreserveFonts:$preserveLegacyFonts -PurgeLegacySecurityState:$purgeLegacySecurity
  } else {
    Write-Host '[SUCH] Legacy machine-wide Such artifacts detected. Requesting Administrator rights for legacy cleanup only.' -ForegroundColor Yellow
    $hostPath=[System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
    $legacyArgs=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',(Quote-SuchProcessArgument $PSCommandPath),'-LegacyMachineCleanupOnly')
    if($KeepFonts.IsPresent){ $legacyArgs += '-KeepFonts' }
    if($PurgeSecurityState.IsPresent){ $legacyArgs += '-PurgeSecurityState' }
    try {
      $legacyWorker=Start-Process -FilePath $hostPath -Verb RunAs -ArgumentList ($legacyArgs -join ' ') -WindowStyle Hidden -Wait -PassThru
      if($null -eq $legacyWorker -or $legacyWorker.ExitCode -ne 0){
        Write-Warning 'Legacy machine-wide Such cleanup did not complete successfully.'
      }
    } catch {
      Write-Warning ('Legacy machine-wide Such cleanup was skipped: ' + $_.Exception.Message)
    }
  }
}

# This script may itself be running from the installation directory. Use a
# temporary PowerShell cleanup worker that waits for this process and retries;
# a single delayed rmdir silently left partial installations when files were
# briefly locked by Explorer, antivirus, or a still-closing frontend.
$cleanupRoot=Join-Path ([System.IO.Path]::GetTempPath()) 'Heritage\SuchUninstall'
New-Item -ItemType Directory -Force -Path $cleanupRoot | Out-Null
$cleanupScript=Join-Path $cleanupRoot ("cleanup-{0}.ps1" -f [Guid]::NewGuid().ToString('N'))
$cleanupLog=Join-Path $cleanupRoot 'last-uninstall.log'
$cleanupBody=@'
param([string]$Target,[int]$WaitForPid,[string]$LogPath,[string]$SelfPath)
$ErrorActionPreference='SilentlyContinue'
Wait-Process -Id $WaitForPid -Timeout 30 -ErrorAction SilentlyContinue
for($attempt=1;$attempt -le 30;$attempt++){
  Remove-Item -LiteralPath $Target -Recurse -Force -ErrorAction SilentlyContinue
  if(-not(Test-Path -LiteralPath $Target)){
    "Such uninstall complete: $Target" | Set-Content -LiteralPath $LogPath -Encoding UTF8
    Remove-Item -LiteralPath $SelfPath -Force -ErrorAction SilentlyContinue
    exit 0
  }
  Start-Sleep -Milliseconds 500
}
"Such uninstall could not remove: $Target" | Set-Content -LiteralPath $LogPath -Encoding UTF8
exit 1
'@
Set-Content -LiteralPath $cleanupScript -Value $cleanupBody -Encoding UTF8
$hostPath=[System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
function Quote-CleanupArgument([string]$Value){return ('"{0}"' -f $Value.Replace('"','\"'))}
$cleanupArgs=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',(Quote-CleanupArgument $cleanupScript),
  '-Target',(Quote-CleanupArgument $installDir),'-WaitForPid',"$PID",'-LogPath',(Quote-CleanupArgument $cleanupLog),
  '-SelfPath',(Quote-CleanupArgument $cleanupScript))
$worker=Start-Process -FilePath $hostPath -ArgumentList ($cleanupArgs -join ' ') -WindowStyle Hidden -PassThru
if($null -eq $worker){throw 'Could not start the final uninstall cleanup worker.'}
Write-Host "Such uninstall prepared. Final cleanup worker pid=$($worker.Id), log=$cleanupLog"
