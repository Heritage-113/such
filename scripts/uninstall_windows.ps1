[CmdletBinding()]
param([switch]$KeepFonts,[switch]$PurgeSecurityState)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest

trap {
  Write-Host ''
  Write-Host ('[SUCH:FATAL] ' + $_.Exception.Message) -ForegroundColor Red
  if ($_.InvocationInfo -and $_.InvocationInfo.PositionMessage) { Write-Host $_.InvocationInfo.PositionMessage -ForegroundColor DarkRed }
  if (-not [string]::IsNullOrWhiteSpace($_.ScriptStackTrace)) { Write-Host $_.ScriptStackTrace -ForegroundColor DarkRed }
  exit 1
}
. (Join-Path $PSScriptRoot 'windows_paths.ps1')
if(-not (Test-SuchWindowsAdministrator)){
  Write-Host '[SUCH] Requesting Administrator rights to uninstall C:\Heritage\Such.' -ForegroundColor Yellow
  $windowsPowerShell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $hostPath=if(Test-Path -LiteralPath $windowsPowerShell -PathType Leaf){$windowsPowerShell}else{[System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName}
  $scriptArg=('"{0}"' -f $PSCommandPath.Replace('"','\"'))
  $elevated=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',$scriptArg)
  if($KeepFonts.IsPresent){$elevated+='-KeepFonts'}
  if($PurgeSecurityState.IsPresent){$elevated+='-PurgeSecurityState'}
  $child=Start-Process -FilePath $hostPath -ArgumentList ($elevated -join ' ') -WorkingDirectory $env:SystemRoot -Verb RunAs -Wait -PassThru
  exit $child.ExitCode
}
$installDir=Get-SuchWindowsInstallRoot; $fontManifest=Join-Path $installDir 'font-install-manifest.json'
$cliCommandDir=Join-Path $installDir 'cli'
$managedRoots=@($installDir) + @(Get-SuchWindowsLegacyInstallRoots)
Stop-SuchWindowsProcessesInRoots -Roots $managedRoots
if(-not$KeepFonts.IsPresent -and (Test-Path -LiteralPath $fontManifest)) {
  & (Join-Path $installDir 'uninstall_user_fonts_windows.ps1') -ManifestPath $fontManifest
}
$programs=Get-SuchWindowsKnownFolder -Name 'CommonPrograms'
Remove-Item -LiteralPath (Join-Path $programs 'Heritage Inc.\Such.lnk') -Force -ErrorAction SilentlyContinue
Remove-Item -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch' -Recurse -Force -ErrorAction SilentlyContinue
Remove-SuchWindowsCommandPath -Directory $cliCommandDir
$desktop=Join-Path (Get-SuchWindowsKnownFolder -Name 'CommonDesktopDirectory') 'Such.lnk'; Remove-Item -LiteralPath $desktop -Force -ErrorAction SilentlyContinue
Remove-SuchWindowsLegacyArtifacts
if($PurgeSecurityState.IsPresent){
  $programData=Get-SuchWindowsKnownFolder -Name 'CommonApplicationData'
  Remove-Item -LiteralPath (Join-Path $programData 'Heritage\Such\Security') -Recurse -Force -ErrorAction SilentlyContinue
}
$commonVendorDir=Join-Path $programs 'Heritage Inc.'
if((Test-Path -LiteralPath $commonVendorDir -PathType Container) -and
   @(Get-ChildItem -LiteralPath $commonVendorDir -Force -ErrorAction SilentlyContinue).Count -eq 0){
  Remove-Item -LiteralPath $commonVendorDir -Force -ErrorAction SilentlyContinue
}
$userVendorDir=Join-Path (Get-SuchWindowsKnownFolder -Name 'Programs') 'Heritage Inc.'
if((Test-Path -LiteralPath $userVendorDir -PathType Container) -and
   @(Get-ChildItem -LiteralPath $userVendorDir -Force -ErrorAction SilentlyContinue).Count -eq 0){
  Remove-Item -LiteralPath $userVendorDir -Force -ErrorAction SilentlyContinue
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
