[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)]
  [string]$ManifestPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $ManifestPath)) {
  Write-Host 'No Such-managed font manifest found; no fonts removed.'
  exit 0
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SuchFontUninstallNative {
    [DllImport("gdi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool RemoveFontResourceExW(string name, uint flags, IntPtr reserved);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr SendMessageTimeoutW(IntPtr hWnd, uint Msg, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
}
'@

$fontReg = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Fonts'
$manifest = (Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8) | ConvertFrom-Json
$removed = 0
$preserved = 0

foreach ($entry in @($manifest.entries)) {
  $dest = [string]$entry.destination
  $regName = [string]$entry.registryName
  $hash = [string]$entry.sha256

  if ([bool]$entry.ownedRegistry) {
    try {
      $item = Get-ItemProperty -LiteralPath $fontReg -ErrorAction Stop
      $prop = $item.PSObject.Properties[$regName]
      if ($null -ne $prop -and [string]::Equals([string]$prop.Value, $dest, [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-ItemProperty -LiteralPath $fontReg -Name $regName -Force -ErrorAction SilentlyContinue
      }
    } catch {}
  }

  if ([bool]$entry.ownedFile -and (Test-Path -LiteralPath $dest)) {
    $currentHash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($currentHash -eq $hash) {
      [void][SuchFontUninstallNative]::RemoveFontResourceExW($dest, 0u, [IntPtr]::Zero)
      Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
      $removed++
    } else {
      Write-Warning "Preserved modified font file: $dest"
      $preserved++
    }
  } else {
    $preserved++
  }
}

$broadcastResult = [UIntPtr]::Zero
[void][SuchFontUninstallNative]::SendMessageTimeoutW([IntPtr]0xffff, 0x001D, [UIntPtr]::Zero, [IntPtr]::Zero, 0x0002, 1000, [ref]$broadcastResult)
Write-Host "Such-managed font cleanup complete: removed=$removed preserved=$preserved"
