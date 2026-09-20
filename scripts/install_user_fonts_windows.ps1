[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)]
  [string[]]$Source,
  [Parameter(Mandatory=$true)]
  [string]$ManifestPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
  throw 'LOCALAPPDATA is required for per-user font installation.'
}

$fontDir = Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts'
$fontReg = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Fonts'
New-Item -ItemType Directory -Force -Path $fontDir | Out-Null
New-Item -Path $fontReg -Force | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SuchFontNative {
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern int AddFontResourceExW(string name, uint flags, IntPtr reserved);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr SendMessageTimeoutW(IntPtr hWnd, uint Msg, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
}
'@

$FR_PRIVATE_NONE = 0u
$HWND_BROADCAST = [IntPtr]0xffff
$WM_FONTCHANGE = 0x001D
$SMTO_ABORTIFHUNG = 0x0002

function Get-ExistingManifestEntries {
  param([string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) { return @() }
  try {
    $raw = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ([string]::IsNullOrWhiteSpace($raw)) { return @() }
    $parsed = $raw | ConvertFrom-Json
    if ($null -eq $parsed.entries) { return @() }
    return @($parsed.entries)
  } catch {
    Write-Warning "Ignoring unreadable previous font manifest: $Path"
    return @()
  }
}

function Get-ExistingPropertyValue {
  param([string]$Path, [string]$Name)
  try {
    $item = Get-ItemProperty -LiteralPath $Path -ErrorAction Stop
    $prop = $item.PSObject.Properties[$Name]
    if ($null -ne $prop) { return [string]$prop.Value }
  } catch {}
  return $null
}

function Expand-FontSource {
  param(
    [string]$InputPath,
    [string]$ScratchRoot
  )

  $full = [System.IO.Path]::GetFullPath($InputPath)
  if (-not (Test-Path -LiteralPath $full)) {
    Write-Warning "Font source not found, skipped: $full"
    return @()
  }

  $item = Get-Item -LiteralPath $full
  if ($item.PSIsContainer) {
    $files = New-Object System.Collections.Generic.List[System.IO.FileInfo]
    foreach ($font in @(Get-ChildItem -LiteralPath $full -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @('.ttf','.otf') })) {
      [void]$files.Add($font)
    }
    foreach ($archive in @(Get-ChildItem -LiteralPath $full -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -ieq '.zip' })) {
      $archiveDir = Join-Path $ScratchRoot ([Guid]::NewGuid().ToString('N'))
      New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
      Expand-Archive -LiteralPath $archive.FullName -DestinationPath $archiveDir -Force
      foreach ($font in @(Get-ChildItem -LiteralPath $archiveDir -Recurse -File | Where-Object { $_.Extension -in @('.ttf','.otf') })) {
        [void]$files.Add($font)
      }
    }
    return @($files)
  }

  if ($item.Extension -in @('.ttf','.otf')) { return @($item) }
  if ($item.Extension -ieq '.zip') {
    $archiveDir = Join-Path $ScratchRoot ([Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
    Expand-Archive -LiteralPath $item.FullName -DestinationPath $archiveDir -Force
    return @(Get-ChildItem -LiteralPath $archiveDir -Recurse -File | Where-Object { $_.Extension -in @('.ttf','.otf') })
  }

  Write-Warning "Unsupported font source type, skipped: $full"
  return @()
}

$manifestFull = [System.IO.Path]::GetFullPath($ManifestPath)
$manifestDir = Split-Path -Parent $manifestFull
New-Item -ItemType Directory -Force -Path $manifestDir | Out-Null

$previous = @(Get-ExistingManifestEntries -Path $manifestFull)
$previousByDestination = @{}
foreach ($entry in $previous) {
  if ($null -ne $entry.destination) {
    $previousByDestination[[string]$entry.destination.ToLowerInvariant()] = $entry
  }
}

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ('SuchFontInstall-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
try {
  $candidates = New-Object System.Collections.Generic.List[System.IO.FileInfo]
  foreach ($src in $Source) {
    foreach ($font in @(Expand-FontSource -InputPath $src -ScratchRoot $scratch)) {
      [void]$candidates.Add($font)
    }
  }

  # One face per basename. Prefer OTF when both OTF and TTF versions are supplied.
  $selected = @{}
  foreach ($font in $candidates) {
    $key = $font.BaseName.ToLowerInvariant()
    if (-not $selected.ContainsKey($key)) {
      $selected[$key] = $font
    } elseif ($font.Extension -ieq '.otf' -and $selected[$key].Extension -ieq '.ttf') {
      $selected[$key] = $font
    }
  }

  $entries = New-Object System.Collections.Generic.List[object]
  $installed = 0
  $reused = 0
  $skipped = 0

  foreach ($key in @($selected.Keys | Sort-Object)) {
    $font = $selected[$key]
    $dest = Join-Path $fontDir $font.Name
    $sourceHash = (Get-FileHash -LiteralPath $font.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $ownedFile = $false

    if (Test-Path -LiteralPath $dest) {
      $existingHash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
      if ($existingHash -ne $sourceHash) {
        Write-Warning "Font filename collision; existing user font preserved and candidate skipped: $dest"
        $skipped++
        continue
      }
      $previousEntry = $previousByDestination[$dest.ToLowerInvariant()]
      if ($null -ne $previousEntry -and [bool]$previousEntry.ownedFile -and [string]$previousEntry.sha256 -eq $sourceHash) {
        $ownedFile = $true
      }
      $reused++
    } else {
      Copy-Item -LiteralPath $font.FullName -Destination $dest
      $ownedFile = $true
      $installed++
    }

    $kind = if ($font.Extension -ieq '.otf') { 'OpenType' } else { 'TrueType' }
    $regName = "$($font.BaseName) ($kind)"
    $currentReg = Get-ExistingPropertyValue -Path $fontReg -Name $regName
    $ownedRegistry = $false

    if ($null -eq $currentReg) {
      New-ItemProperty -LiteralPath $fontReg -Name $regName -Value $dest -PropertyType String -Force | Out-Null
      $ownedRegistry = $true
    } elseif ([string]::Equals($currentReg, $dest, [System.StringComparison]::OrdinalIgnoreCase)) {
      foreach ($old in $previous) {
        if ([string]$old.registryName -eq $regName -and [bool]$old.ownedRegistry) {
          $ownedRegistry = $true
          break
        }
      }
    } else {
      Write-Warning "Font registry-name collision preserved: $regName -> $currentReg"
    }

    [void][SuchFontNative]::AddFontResourceExW($dest, $FR_PRIVATE_NONE, [IntPtr]::Zero)

    $entries.Add([pscustomobject]@{
      fileName      = $font.Name
      destination   = $dest
      registryName  = $regName
      registryValue = $dest
      sha256         = $sourceHash
      ownedFile      = $ownedFile
      ownedRegistry  = $ownedRegistry
    }) | Out-Null

    Write-Host ("Font ready: {0}{1}" -f $font.Name, $(if ($ownedFile) { ' [Such-managed]' } else { ' [pre-existing]' }))
  }

  $manifest = [ordered]@{
    schemaVersion = 1
    product       = 'Such'
    version       = '1.0.0'
    installedAt   = (Get-Date).ToString('o')
    entries       = @($entries)
  }
  $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestFull -Encoding UTF8

  $broadcastResult = [UIntPtr]::Zero
  [void][SuchFontNative]::SendMessageTimeoutW($HWND_BROADCAST, $WM_FONTCHANGE, [UIntPtr]::Zero, [IntPtr]::Zero, $SMTO_ABORTIFHUNG, 1000, [ref]$broadcastResult)

  Write-Host "Font installation complete: new=$installed reused=$reused skipped=$skipped manifest=$manifestFull"
  exit 0
} finally {
  Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}
