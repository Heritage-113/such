[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [string]$Generator='Auto',
  [ValidateRange(1,64)][int]$Jobs=1,
  [switch]$Clean,
  [switch]$DesktopShortcut,
  [switch]$SkipBuild,
  [switch]$SkipFonts,
  [switch]$RequireFonts,
  [string]$FontSourceDir='',
  [string]$RuntimeLibraryPath='',
  [switch]$RequireRuntime
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
$windowsPowerShell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$powerShellHostPath=if(Test-Path -LiteralPath $windowsPowerShell -PathType Leaf){$windowsPowerShell}else{[System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName}
if (-not (Test-SuchWindowsAdministrator)) {
  Write-Host '[SUCH] Requesting Administrator rights for the machine-wide C:\Heritage\Such installation.' -ForegroundColor Yellow
  function Quote-PayloadLiteral([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
  $elevated=@('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,
    '-Config',$Config,'-Arch',$Arch,'-Generator',$Generator,'-Jobs',"$Jobs")
  if($Clean.IsPresent){$elevated+='-Clean'}
  if($DesktopShortcut.IsPresent){$elevated+='-DesktopShortcut'}
  if($SkipBuild.IsPresent){$elevated+='-SkipBuild'}
  if($SkipFonts.IsPresent){$elevated+='-SkipFonts'}
  if($RequireFonts.IsPresent){$elevated+='-RequireFonts'}
  if($RequireRuntime.IsPresent){$elevated+='-RequireRuntime'}
  if(-not[string]::IsNullOrWhiteSpace($FontSourceDir)){$elevated+=@('-FontSourceDir',$FontSourceDir)}
  if(-not[string]::IsNullOrWhiteSpace($RuntimeLibraryPath)){$elevated+=@('-RuntimeLibraryPath',$RuntimeLibraryPath)}
  $payloadArgs=($elevated | ForEach-Object { Quote-PayloadLiteral $_ }) -join ','
  $payload=@"
`$installArgs=@($payloadArgs)
& $(Quote-PayloadLiteral $powerShellHostPath) @installArgs
`$rc=`$LASTEXITCODE
if(`$rc -eq 0){Write-Host '';Write-Host '[SUCH] Installation and CLI PATH registration completed.' -ForegroundColor Green}
else{Write-Host '';Write-Host ("[SUCH] Installation failed with exit code {0}." -f `$rc) -ForegroundColor Red}
Write-Host 'Open a new PowerShell window and run: such version'
[void](Read-Host 'Press Enter to close this Administrator console')
exit `$rc
"@
  $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($payload))
  $child=Start-Process -FilePath $powerShellHostPath -ArgumentList @('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded) -WorkingDirectory $env:SystemRoot -WindowStyle Normal -Verb RunAs -Wait -PassThru
  exit $child.ExitCode
}
function New-Shortcut([string]$Path,[string]$Target,[string]$WorkingDirectory) {
  $shell=New-Object -ComObject WScript.Shell
  $s=$shell.CreateShortcut($Path); $s.TargetPath=$Target; $s.WorkingDirectory=$WorkingDirectory
  $s.Description='Such — local file search'; $s.IconLocation="$Target,0"; $s.Save()
}
function Get-AutoFontSources([string]$SourceRoot) {
  $found=New-Object System.Collections.Generic.List[string]; $seen=@{}
  function Add-Candidate([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) { return }
    $full=[System.IO.Path]::GetFullPath($Path); $key=$full.ToLowerInvariant()
    if (-not $seen.ContainsKey($key)) { $seen[$key]=$true; [void]$found.Add($full) }
  }
  if (-not [string]::IsNullOrWhiteSpace($FontSourceDir)) { Add-Candidate $FontSourceDir; return @($found) }
  Add-Candidate (Join-Path $SourceRoot 'font-bundles')
  foreach ($base in @($SourceRoot,(Split-Path -Parent $SourceRoot),(Get-Location).Path)) {
    foreach ($name in @('Gowun-Batang-master.zip','continuous.zip','KOPUBWORLD_OTF_FONTS2026.zip')) { Add-Candidate (Join-Path $base $name) }
  }
  return @($found)
}
$Root=Get-SuchSourceRoot -ScriptRoot $PSScriptRoot
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
if (-not $SkipBuild.IsPresent) {
  $params=@{Config=$Config;Arch=$Arch;Generator=$Generator;Jobs=$Jobs}; if($Clean.IsPresent){$params.Clean=$true}; if(-not[string]::IsNullOrWhiteSpace($RuntimeLibraryPath)){$params.RuntimeLibraryPath=$RuntimeLibraryPath}; if($RequireRuntime.IsPresent){$params.RequireRuntime=$true}
  & (Join-Path $PSScriptRoot 'build_windows.ps1') @params
  if($LASTEXITCODE -ne 0){throw "Windows build failed with exit $LASTEXITCODE"}
}
$stageBin=Join-Path $Ws.InstallDir 'bin'
$gui=Join-Path $stageBin 'Such.exe'; $cli=Join-Path $stageBin 'SuchCLI.exe'
foreach($b in @($gui,$cli)){ if(-not(Test-Path -LiteralPath $b)){ throw "Missing staged binary: $b" } }
$installDir=Get-SuchWindowsInstallRoot
$cliCommandDir=Join-Path $installDir 'cli'
$startMenuDir=Join-Path (Get-SuchWindowsStartMenuPrograms) 'Heritage Inc.'
$managedRoots=@($installDir) + @(Get-SuchWindowsLegacyInstallRoots)
Stop-SuchWindowsProcessesInRoots -Roots $managedRoots
New-Item -ItemType Directory -Force -Path $installDir,$cliCommandDir,$startMenuDir|Out-Null
Copy-Item -LiteralPath $gui -Destination (Join-Path $installDir 'Such.exe') -Force
Copy-Item -LiteralPath $cli -Destination (Join-Path $installDir 'SuchCLI.exe') -Force
Copy-Item -LiteralPath $cli -Destination (Join-Path $cliCommandDir 'such.exe') -Force
foreach($helper in @('uninstall_windows.ps1','uninstall_windows.cmd','uninstall_user_fonts_windows.ps1','windows_paths.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $helper) -Destination (Join-Path $installDir $helper) -Force}
$runtime=$null
$stagedRuntime=Join-Path $stageBin 'SuchRuntimePrivate.dll'
if(Test-Path -LiteralPath $stagedRuntime -PathType Leaf){
  $runtime=$stagedRuntime
}else{
  $runtime=Resolve-SuchWindowsRuntime -SourceRoot $Root -ExplicitPath $RuntimeLibraryPath
}
if($null -ne $runtime){
  Assert-SuchWindowsRuntimeArchitecture -RuntimePath $runtime -Arch $Arch
  Copy-Item -LiteralPath $runtime -Destination (Join-Path $installDir 'SuchRuntimePrivate.dll') -Force
  Write-Host 'SuchRuntimePrivate.dll installed beside the executables.'
}else{if($RequireRuntime.IsPresent){throw 'Required Windows production runtime is missing from staged output.'}; Write-Warning 'No production runtime was found. Put it at .runtime\windows-x64\SuchRuntimePrivate.dll (or pass -RuntimeLibraryPath) and rerun install_windows.cmd.'}

foreach($source in @($gui,$cli)){
  $destination=Join-Path $installDir (Split-Path -Leaf $source)
  if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash){
    throw "Installed binary verification failed: $destination"
  }
}
$cliCommand=Join-Path $cliCommandDir 'such.exe'
if((Get-FileHash -LiteralPath $cli -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $cliCommand -Algorithm SHA256).Hash){
  throw "Installed CLI command verification failed: $cliCommand"
}
if($null -ne $runtime){
  $installedRuntime=Join-Path $installDir 'SuchRuntimePrivate.dll'
  if((Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $installedRuntime -Algorithm SHA256).Hash){
    throw "Installed runtime verification failed: $installedRuntime"
  }
}

$appExe=Join-Path $installDir 'Such.exe'; $startMenuLink=Join-Path $startMenuDir 'Such.lnk'
New-Shortcut $startMenuLink $appExe $installDir
if($DesktopShortcut.IsPresent){New-Shortcut (Join-Path (Get-SuchWindowsDesktop) 'Such.lnk') $appExe $installDir}
$appPathKey='HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe'; New-Item -Path $appPathKey -Force|Out-Null; Set-Item -Path $appPathKey -Value $appExe; New-ItemProperty -Path $appPathKey -Name Path -Value $installDir -PropertyType String -Force|Out-Null
Add-SuchWindowsCommandPath -Directory $cliCommandDir
$uninstallKey='HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch'; New-Item -Path $uninstallKey -Force|Out-Null
$uninstallScript=Join-Path $installDir 'uninstall_windows.ps1'
$uninstallCmd='"{0}" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "{1}"' -f $powerShellHostPath,$uninstallScript
foreach($kv in @(@('DisplayName','Such'),@('DisplayVersion','1.1.7'),@('Publisher','Heritage Inc.'),@('InstallLocation',$installDir),@('DisplayIcon',"$appExe,0"),@('UninstallString',$uninstallCmd))){New-ItemProperty -Path $uninstallKey -Name $kv[0] -Value $kv[1] -PropertyType String -Force|Out-Null}
New-ItemProperty -Path $uninstallKey -Name QuietUninstallString -Value $uninstallCmd -PropertyType String -Force|Out-Null
New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force|Out-Null; New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force|Out-Null
$fontManifest=Join-Path $installDir 'font-install-manifest.json'; $fontSources=@()
if(-not$SkipFonts.IsPresent){$fontSources=@(Get-AutoFontSources $Root); if($fontSources.Count -gt 0){& (Join-Path $PSScriptRoot 'install_user_fonts_windows.ps1') -Source $fontSources -ManifestPath $fontManifest}elseif($RequireFonts.IsPresent){throw 'No font bundles found.'}else{Write-Warning 'No local font bundle discovered.'}}
Remove-SuchWindowsLegacyArtifacts
Write-Host "Such v1.1.7 installed: $installDir"
Write-Host "CLI command installed: $cliCommand (open a new terminal, then run: such)"
