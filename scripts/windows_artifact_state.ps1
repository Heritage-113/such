Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_paths.ps1')

function Get-SuchFileSha256 {
  param([Parameter(Mandatory=$true)][string]$Path)
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Artifact is missing: $Path" }
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-SuchVerificationManifestPath {
  param([Parameter(Mandatory=$true)]$Workspace)
  return (Join-Path $Workspace.StateDir 'verified.json')
}

function Get-SuchStageManifestPath {
  param([Parameter(Mandatory=$true)]$Workspace)
  return (Join-Path $Workspace.StateDir 'staged.json')
}

function Clear-SuchWindowsArtifactState {
  param([Parameter(Mandatory=$true)]$Workspace)
  foreach($path in @((Get-SuchVerificationManifestPath -Workspace $Workspace),(Get-SuchStageManifestPath -Workspace $Workspace))) {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
  }
}

function Write-SuchVerificationManifest {
  param(
    [Parameter(Mandatory=$true)]$Workspace,
    [Parameter(Mandatory=$true)][string]$Arch,
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$GuiPath,
    [Parameter(Mandatory=$true)][string]$CliPath,
    [Parameter(Mandatory=$true)][string]$StubPath,
    [string]$RuntimePath='',
    [switch]$RuntimeRequired
  )

  New-Item -ItemType Directory -Force -Path $Workspace.StateDir | Out-Null
  $runtimeFull=''
  $runtimeHash=''
  if (-not [string]::IsNullOrWhiteSpace($RuntimePath)) {
    $runtimeFull=[System.IO.Path]::GetFullPath($RuntimePath)
    $runtimeHash=Get-SuchFileSha256 -Path $runtimeFull
  }

  $manifest=[ordered]@{
    schema=1
    version=(Get-SuchVersion -SourceRoot $Workspace.SourceRoot)
    source_root=[System.IO.Path]::GetFullPath($Workspace.SourceRoot)
    cmake_source=[System.IO.Path]::GetFullPath($Workspace.CMakeSource)
    build_dir=[System.IO.Path]::GetFullPath($Workspace.BuildDir)
    arch=$Arch
    config=$Config
    gui_path=[System.IO.Path]::GetFullPath($GuiPath)
    gui_sha256=(Get-SuchFileSha256 -Path $GuiPath)
    cli_path=[System.IO.Path]::GetFullPath($CliPath)
    cli_sha256=(Get-SuchFileSha256 -Path $CliPath)
    stub_path=[System.IO.Path]::GetFullPath($StubPath)
    stub_sha256=(Get-SuchFileSha256 -Path $StubPath)
    runtime_required=[bool]$RuntimeRequired.IsPresent
    runtime_path=$runtimeFull
    runtime_sha256=$runtimeHash
    verified_utc=[DateTime]::UtcNow.ToString('o')
  }
  $path=Get-SuchVerificationManifestPath -Workspace $Workspace
  $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $path -Encoding UTF8
  Remove-Item -LiteralPath (Get-SuchStageManifestPath -Workspace $Workspace) -Force -ErrorAction SilentlyContinue
  Write-Host "Verification manifest: $path"
  return $path
}

function Read-SuchVerificationManifest {
  param([Parameter(Mandatory=$true)]$Workspace)
  $path=Get-SuchVerificationManifestPath -Workspace $Workspace
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Verified-build manifest is missing. Run verify_windows.cmd first: $path"
  }
  $manifest=(Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
  if ([string]$manifest.version -ne (Get-SuchVersion -SourceRoot $Workspace.SourceRoot)) { throw "Verified-build manifest version mismatch: $($manifest.version)" }
  if (-not [string]::Equals([System.IO.Path]::GetFullPath([string]$manifest.source_root),[System.IO.Path]::GetFullPath([string]$Workspace.SourceRoot),[System.StringComparison]::OrdinalIgnoreCase)) { throw 'Verified-build manifest source-root mismatch.' }
  if ([string]$manifest.arch -ne [string]$Workspace.Arch) { throw "Verified-build manifest architecture mismatch." }
  if ([string]$manifest.config -ne [string]$Workspace.Config) { throw "Verified-build manifest configuration mismatch." }
  if ((Get-SuchFileSha256 -Path ([string]$manifest.gui_path)) -ne [string]$manifest.gui_sha256) { throw 'Verified Such.exe has changed since testing.' }
  if ((Get-SuchFileSha256 -Path ([string]$manifest.cli_path)) -ne [string]$manifest.cli_sha256) { throw 'Verified SuchCLI.exe has changed since testing.' }
  if ((Get-SuchFileSha256 -Path ([string]$manifest.stub_path)) -ne [string]$manifest.stub_sha256) { throw 'Verified SuchRuntimeStub.dll has changed since testing.' }
  if (-not [string]::IsNullOrWhiteSpace([string]$manifest.runtime_path)) {
    if ((Get-SuchFileSha256 -Path ([string]$manifest.runtime_path)) -ne [string]$manifest.runtime_sha256) { throw 'Verified production runtime has changed since testing.' }
  }
  return $manifest
}

function Write-SuchStageManifest {
  param(
    [Parameter(Mandatory=$true)]$Workspace,
    [Parameter(Mandatory=$true)]$VerificationManifest,
    [Parameter(Mandatory=$true)][string]$GuiPath,
    [Parameter(Mandatory=$true)][string]$CliPath,
    [string]$RuntimePath=''
  )
  New-Item -ItemType Directory -Force -Path $Workspace.StateDir | Out-Null
  $verifiedPath=Get-SuchVerificationManifestPath -Workspace $Workspace
  $runtimeFull=''
  $runtimeHash=''
  if (-not [string]::IsNullOrWhiteSpace($RuntimePath)) {
    $runtimeFull=[System.IO.Path]::GetFullPath($RuntimePath)
    $runtimeHash=Get-SuchFileSha256 -Path $runtimeFull
  }
  $manifest=[ordered]@{
    schema=1
    version=(Get-SuchVersion -SourceRoot $Workspace.SourceRoot)
    source_root=[System.IO.Path]::GetFullPath($Workspace.SourceRoot)
    install_dir=[System.IO.Path]::GetFullPath($Workspace.InstallDir)
    arch=[string]$Workspace.Arch
    config=[string]$Workspace.Config
    verification_manifest_sha256=(Get-SuchFileSha256 -Path $verifiedPath)
    gui_path=[System.IO.Path]::GetFullPath($GuiPath)
    gui_sha256=(Get-SuchFileSha256 -Path $GuiPath)
    cli_path=[System.IO.Path]::GetFullPath($CliPath)
    cli_sha256=(Get-SuchFileSha256 -Path $CliPath)
    runtime_path=$runtimeFull
    runtime_sha256=$runtimeHash
    staged_utc=[DateTime]::UtcNow.ToString('o')
  }
  $path=Get-SuchStageManifestPath -Workspace $Workspace
  $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $path -Encoding UTF8
  Write-Host "Stage manifest       : $path"
  return $path
}

function Read-SuchStageManifest {
  param([Parameter(Mandatory=$true)]$Workspace)
  $verification=Read-SuchVerificationManifest -Workspace $Workspace
  $path=Get-SuchStageManifestPath -Workspace $Workspace
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Staged-artifact manifest is missing. Run install_windows.cmd or release_windows.cmd to stage the already verified build: $path"
  }
  $manifest=(Get-Content -LiteralPath $path -Raw | ConvertFrom-Json)
  if ([string]$manifest.version -ne (Get-SuchVersion -SourceRoot $Workspace.SourceRoot)) { throw "Staged-artifact manifest version mismatch: $($manifest.version)" }
  if (-not [string]::Equals([System.IO.Path]::GetFullPath([string]$manifest.source_root),[System.IO.Path]::GetFullPath([string]$Workspace.SourceRoot),[System.StringComparison]::OrdinalIgnoreCase)) { throw 'Staged-artifact manifest source-root mismatch.' }
  if ([string]$manifest.arch -ne [string]$Workspace.Arch -or [string]$manifest.config -ne [string]$Workspace.Config) { throw 'Staged-artifact manifest build identity mismatch.' }
  $verificationPath=Get-SuchVerificationManifestPath -Workspace $Workspace
  if ((Get-SuchFileSha256 -Path $verificationPath) -ne [string]$manifest.verification_manifest_sha256) { throw 'Stage was produced from a different verification manifest.' }
  if ((Get-SuchFileSha256 -Path ([string]$manifest.gui_path)) -ne [string]$manifest.gui_sha256) { throw 'Staged Such.exe changed after staging.' }
  if ((Get-SuchFileSha256 -Path ([string]$manifest.cli_path)) -ne [string]$manifest.cli_sha256) { throw 'Staged SuchCLI.exe changed after staging.' }
  if (-not [string]::IsNullOrWhiteSpace([string]$manifest.runtime_path)) {
    if ((Get-SuchFileSha256 -Path ([string]$manifest.runtime_path)) -ne [string]$manifest.runtime_sha256) { throw 'Staged production runtime changed after staging.' }
  }
  return $manifest
}


function Test-SuchVerificationManifest {
  param([Parameter(Mandatory=$true)]$Workspace)
  try {
    [void](Read-SuchVerificationManifest -Workspace $Workspace)
    return $true
  } catch {
    return $false
  }
}

function Test-SuchStageManifest {
  param([Parameter(Mandatory=$true)]$Workspace)
  try {
    [void](Read-SuchStageManifest -Workspace $Workspace)
    return $true
  } catch {
    return $false
  }
}
