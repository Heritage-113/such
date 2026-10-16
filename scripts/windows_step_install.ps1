[CmdletBinding()]
param(
  [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')][string]$Config='Release',
  [ValidateSet('x64','Win32','ARM64')][string]$Arch='x64',
  [switch]$DesktopShortcut,
  [switch]$SkipFonts,
  [switch]$RequireFonts,
  [switch]$RequireRuntime,
  [string]$FontSourceDir=''
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'windows_paths.ps1')
. (Join-Path $PSScriptRoot 'windows_artifact_state.ps1')

function New-Shortcut([string]$ShortcutPath,[string]$TargetExe,[string]$WorkingDirectory) {
  $shell=New-Object -ComObject WScript.Shell
  $shortcut=$shell.CreateShortcut($ShortcutPath)
  $shortcut.TargetPath=$TargetExe
  $shortcut.WorkingDirectory=$WorkingDirectory
  $shortcut.Description='Such — local file search'
  $shortcut.IconLocation="$TargetExe,0"
  $shortcut.Save()
}

function Get-AutoFontSources([string]$SourceRoot) {
  $found=New-Object System.Collections.Generic.List[string]
  $seen=@{}
  function Test-FontPayload([string]$CandidatePath) {
    if ([string]::IsNullOrWhiteSpace($CandidatePath) -or -not (Test-Path -LiteralPath $CandidatePath)) { return $false }
    $item=Get-Item -LiteralPath $CandidatePath -ErrorAction Stop
    if(-not $item.PSIsContainer){ return $item.Extension -in @('.ttf','.otf','.zip') }
    $payload=Get-ChildItem -LiteralPath $item.FullName -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @('.ttf','.otf','.zip') } | Select-Object -First 1
    return $null -ne $payload
  }
  function Add-Candidate([string]$CandidatePath) {
    if (-not (Test-FontPayload $CandidatePath)) { return }
    $full=[System.IO.Path]::GetFullPath($CandidatePath)
    $key=$full.ToLowerInvariant()
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
$Version=Get-SuchVersion -SourceRoot $Root
$Ws=Get-SuchWindowsWorkspace -SourceRoot $Root -Arch $Arch -Config $Config
$stageManifest=Read-SuchStageManifest -Workspace $Ws
$gui=[string]$stageManifest.gui_path
$cli=[string]$stageManifest.cli_path
$runtime=[string]$stageManifest.runtime_path
if (($RequireRuntime.IsPresent -or $Arch -eq 'x64') -and [string]::IsNullOrWhiteSpace($runtime)) { throw 'Windows x64 installation requires SuchRuntimePrivate.dll in the verified staged artifact.' }

$installDir=Get-SuchWindowsInstallRoot
$cliCommandDir=Join-Path $installDir 'cli'
$startMenuDir=Join-Path (Get-SuchWindowsStartMenuPrograms) 'Heritage Inc.'
$managedRoots=@($installDir) + @(Get-SuchWindowsLegacyInstallRoots)
Stop-SuchWindowsProcessesInRoots -Roots $managedRoots
New-Item -ItemType Directory -Force -Path $installDir,$cliCommandDir,$startMenuDir | Out-Null

Copy-Item -LiteralPath $gui -Destination (Join-Path $installDir 'Such.exe') -Force
Copy-Item -LiteralPath $cli -Destination (Join-Path $installDir 'SuchCLI.exe') -Force
Copy-Item -LiteralPath $cli -Destination (Join-Path $cliCommandDir 'such.exe') -Force
foreach($helper in @('uninstall_windows.ps1','uninstall_windows.cmd','uninstall_user_fonts_windows.ps1','windows_paths.ps1','windows_powershell_host.cmd','windows_cmd_finish.cmd','windows_powershell_host.ps1','windows_powershell_assets.txt')) {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $helper) -Destination (Join-Path $installDir $helper) -Force
}
if (-not [string]::IsNullOrWhiteSpace($runtime)) {
  # GUI and primary CLI live in $installDir; the PATH command is a second CLI
  # copy under cli\, so it needs the same sibling runtime contract as well.
  Copy-Item -LiteralPath $runtime -Destination (Join-Path $installDir 'SuchRuntimePrivate.dll') -Force
  Copy-Item -LiteralPath $runtime -Destination (Join-Path $cliCommandDir 'SuchRuntimePrivate.dll') -Force
}

$installedGui=Join-Path $installDir 'Such.exe'
$installedCli=Join-Path $installDir 'SuchCLI.exe'
$cliCommand=Join-Path $cliCommandDir 'such.exe'
if ((Get-SuchFileSha256 -Path $installedGui) -ne [string]$stageManifest.gui_sha256) { throw "Installed binary verification failed: $installedGui" }
if ((Get-SuchFileSha256 -Path $installedCli) -ne [string]$stageManifest.cli_sha256) { throw "Installed binary verification failed: $installedCli" }
if ((Get-SuchFileSha256 -Path $cliCommand) -ne [string]$stageManifest.cli_sha256) { throw "Installed CLI command verification failed: $cliCommand" }
if (-not [string]::IsNullOrWhiteSpace($runtime)) {
  $installedRuntime=Join-Path $installDir 'SuchRuntimePrivate.dll'
  $installedCliRuntime=Join-Path $cliCommandDir 'SuchRuntimePrivate.dll'
  foreach ($runtimeCopy in @($installedRuntime,$installedCliRuntime)) {
    if (-not (Test-Path -LiteralPath $runtimeCopy -PathType Leaf)) { throw "Installed runtime missing: $runtimeCopy" }
    if ((Get-SuchFileSha256 -Path $runtimeCopy) -ne [string]$stageManifest.runtime_sha256) { throw "Installed runtime verification failed: $runtimeCopy" }
  }
}

$startMenuLink=Join-Path $startMenuDir 'Such.lnk'
New-Shortcut $startMenuLink $installedGui $installDir
if($DesktopShortcut.IsPresent){New-Shortcut (Join-Path (Get-SuchWindowsDesktop) 'Such.lnk') $installedGui $installDir}

$appPathKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe'
New-Item -Path $appPathKey -Force | Out-Null
Set-Item -Path $appPathKey -Value $installedGui
New-ItemProperty -Path $appPathKey -Name Path -Value $installDir -PropertyType String -Force | Out-Null
Add-SuchWindowsCommandPath -Directory $cliCommandDir

$uninstallKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch'
New-Item -Path $uninstallKey -Force | Out-Null
$uninstallLauncher=Join-Path $installDir 'uninstall_windows.cmd'
$commandProcessor=if(-not[string]::IsNullOrWhiteSpace($env:ComSpec)){$env:ComSpec}else{Join-Path $env:SystemRoot 'System32\cmd.exe'}
$uninstallCmd='"{0}" /d /c ""{1}""' -f $commandProcessor,$uninstallLauncher
foreach($kv in @(@('DisplayName','Such'),@('DisplayVersion',$Version),@('Publisher','Heritage Inc.'),@('InstallLocation',$installDir),@('DisplayIcon',"$installedGui,0"),@('UninstallString',$uninstallCmd))) {
  New-ItemProperty -Path $uninstallKey -Name $kv[0] -Value $kv[1] -PropertyType String -Force | Out-Null
}
New-ItemProperty -Path $uninstallKey -Name QuietUninstallString -Value $uninstallCmd -PropertyType String -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null

$fontManifest=Join-Path $installDir 'font-install-manifest.json'
if(-not $SkipFonts.IsPresent){
  $fontSources=@(Get-AutoFontSources $Root)
  if($fontSources.Count -gt 0){
    & (Join-Path $PSScriptRoot 'install_user_fonts_windows.ps1') -Source $fontSources -ManifestPath $fontManifest
  }elseif($RequireFonts.IsPresent){
    throw 'No font bundles found.'
  }else{
    Write-Warning 'No local font bundle discovered.'
  }
}
Remove-SuchWindowsLegacyArtifacts
Write-Host "Windows install step PASS: $installDir"
Write-Host "CLI command installed: $cliCommand (open a new terminal, then run: such)"
