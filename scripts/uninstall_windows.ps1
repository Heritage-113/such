[CmdletBinding()]
param([switch]$KeepFonts)
$ErrorActionPreference='Stop'; Set-StrictMode -Version Latest
if([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)-or[string]::IsNullOrWhiteSpace($env:APPDATA)){throw 'LOCALAPPDATA and APPDATA are required.'}
$installDir=Join-Path $env:LOCALAPPDATA 'Programs\Such'; $fontManifest=Join-Path $installDir 'font-install-manifest.json'
if(-not$KeepFonts.IsPresent -and (Test-Path -LiteralPath $fontManifest)) { & (Join-Path $installDir 'uninstall_user_fonts_windows.ps1') -ManifestPath $fontManifest }
Remove-Item -LiteralPath (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Heritage Inc.\Such.lnk') -Force -ErrorAction SilentlyContinue
Remove-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe' -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch' -Recurse -Force -ErrorAction SilentlyContinue
$desktop=Join-Path ([Environment]::GetFolderPath('Desktop')) 'Such.lnk'; Remove-Item -LiteralPath $desktop -Force -ErrorAction SilentlyContinue
Write-Host "Such uninstall prepared. Removing $installDir"
Start-Process -FilePath cmd.exe -ArgumentList @('/d','/c',"ping 127.0.0.1 -n 2 >nul & rmdir /s /q `"$installDir`"") -WindowStyle Hidden
