# Toolchain-only owner: resolve or securely provision Inno Setup 7 without
# elevating, registering services, or modifying the Such runtime.
Set-StrictMode -Version Latest

function Read-SuchInnoSetupAssets {
  param([Parameter(Mandatory=$true)][string]$SourceRoot)
  $manifest=Join-Path $SourceRoot 'scripts/inno_setup7_assets.txt'
  if(-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Inno Setup asset manifest is missing: $manifest"
  }
  $items=@{}
  foreach($raw in [System.IO.File]::ReadAllLines($manifest)) {
    $line=$raw.Trim()
    if($line -eq '' -or $line.StartsWith('#')) { continue }
    $split=$line.IndexOf('=')
    if($split -le 0) { throw "Invalid Inno Setup manifest line: $line" }
    $key=$line.Substring(0,$split)
    if($items.ContainsKey($key)) { throw "Duplicate Inno Setup manifest key: $key" }
    $items[$key]=$line.Substring($split+1)
  }
  $ver=[string]$items['InnoSetupVersion']
  $name=[string]$items['InnoSetupInstaller']
  $url=[string]$items['InnoSetupUrl']
  $sha=[string]$items['InnoSetupSha256']
  if($ver -notmatch '^7\.[0-9]+\.[0-9]+$' -or $name -ne "innosetup-$ver-x64.exe" -or
     $url -ne ('https://github.com/jrsoftware/issrc/releases/download/is-{0}/{1}' -f ($ver -replace '\.','_'),$name) -or
     $sha -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Inno Setup asset manifest does not describe a pinned official x64 v7 release.'
  }
  return [pscustomobject]@{Version=$ver;Installer=$name;Url=$url;Sha256=$sha.ToLowerInvariant()}
}

function Test-SuchInnoSetup7Compiler {
  param([string]$Path)
  if([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
  if([System.IO.Path]::GetFileName($Path) -ine 'ISCC.exe') { return $false }
  try {
    # ISCC can have 0.0.0.0 PE metadata. Query the compiler engine itself.
    $result=Invoke-SuchNativeCapture -FilePath $Path -Arguments @('--version') -Step 'Inno Setup compiler version'
    return $result.StdOut.Trim() -match '^7\.[0-9]+\.[0-9]+$'
  } catch { return $false }
}

function Find-SuchInnoSetup7Compiler {
  param([Parameter(Mandatory=$true)][string]$SourceRoot)
  $candidates=New-Object 'System.Collections.Generic.List[string]'
  $candidates.Add((Join-Path $SourceRoot '.cache/toolchains/InnoSetup7/ISCC.exe'))
  $local=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
  if(-not [string]::IsNullOrWhiteSpace($local)) {
    $candidates.Add((Join-Path $local 'Programs/Inno Setup 7/ISCC.exe'))
    $candidates.Add((Join-Path $local 'Programs/Inno Setup 7 (64-bit)/ISCC.exe'))
  }
  foreach($base in @($env:ProgramFiles,${env:ProgramFiles(x86)})) {
    if(-not [string]::IsNullOrWhiteSpace($base)) {
      $candidates.Add((Join-Path $base 'Inno Setup 7/ISCC.exe'))
      $candidates.Add((Join-Path $base 'Inno Setup 7 (64-bit)/ISCC.exe'))
    }
  }
  $existing=Get-Command 'ISCC.exe' -All -ErrorAction SilentlyContinue
  foreach($item in @($existing)) {
    if($null -ne $item -and -not [string]::IsNullOrWhiteSpace([string]$item.Source)) {
      $candidates.Add([string]$item.Source)
    }
  }
  foreach($candidate in $candidates) {
    if(Test-SuchInnoSetup7Compiler -Path $candidate) { return [System.IO.Path]::GetFullPath($candidate) }
  }
  return $null
}

function Resolve-SuchInnoCompiler {
  param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [string]$ExplicitPath='',
    [switch]$OfflineOnly
  )
  if(-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
    $path=[System.IO.Path]::GetFullPath($ExplicitPath)
    if(-not (Test-SuchInnoSetup7Compiler -Path $path)) {
      throw "-ISCCPath must point to the Inno Setup 7 ISCC.exe compiler: $path"
    }
    return $path
  }
  $found=Find-SuchInnoSetup7Compiler -SourceRoot $SourceRoot
  if($null -ne $found) { return $found }
  if($OfflineOnly.IsPresent) {
    throw 'Inno Setup 7 is absent and -OfflineOnly forbids download. Install Inno Setup 7 or pass -ISCCPath.'
  }
  $asset=Read-SuchInnoSetupAssets -SourceRoot $SourceRoot
  $cache=Join-Path $SourceRoot '.cache/toolchains/InnoSetup7'
  $downloadDir=Join-Path $SourceRoot '.cache/downloads'
  $installer=Join-Path $downloadDir $asset.Installer
  New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
  New-Item -ItemType Directory -Force -Path $cache | Out-Null
  if(Test-Path -LiteralPath $installer -PathType Leaf) {
    $actual=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
    if($actual -ne $asset.Sha256) {
      Remove-Item -LiteralPath $installer -Force
      Write-Host '[SUCH:INNO] Discarded an invalid cached Inno Setup installer.' -ForegroundColor Yellow
    }
  }
  if(-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    $temporary="$installer.download"
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    Write-Host "[SUCH:INNO] Downloading official Inno Setup $($asset.Version) x64..."
    try {
      Invoke-WebRequest -Uri $asset.Url -OutFile $temporary -MaximumRedirection 5 -ErrorAction Stop
      $digest=(Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
      if($digest -ne $asset.Sha256) { throw "Downloaded Inno Setup SHA-256 mismatch: $digest" }
      Move-Item -LiteralPath $temporary -Destination $installer -Force
    } finally {
      Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
  }
  $digest=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
  if($digest -ne $asset.Sha256) { throw 'Inno Setup installer SHA-256 verification failed.' }
  $signature=Get-AuthenticodeSignature -LiteralPath $installer
  if($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
     $signature.SignerCertificate.Subject -notmatch '(^|,)\s*(CN|O)=Pyrsys B\.V\.($|,)') {
    throw 'Inno Setup installer does not have a valid Pyrsys B.V. Authenticode signature.'
  }
  Write-Host "[SUCH:INNO] Verified Inno Setup $($asset.Version) (SHA-256 and Authenticode)."
  Write-Host '[SUCH:INNO] Installing compiler for current user (no elevation)...'
  # Inno Setup itself supports these command-line options. /DIR keeps the tool
  # inside ignored source-local cache, leaving global Inno Setup installs intact.
  $args=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER',("/DIR=$cache"))
  Invoke-SuchNative -FilePath $installer -Arguments $args -Step 'Inno Setup 7 provisioning'
  $iscc=Join-Path $cache 'ISCC.exe'
  if(-not (Test-SuchInnoSetup7Compiler -Path $iscc)) {
    throw "Inno Setup 7 provisioned but compiler missing or wrong version at $iscc"
  }
  Write-Host "[SUCH:INNO] Ready: $iscc"
  return $iscc
}
