Set-StrictMode -Version Latest
$script:SuchWindowsPathsScriptRoot = $PSScriptRoot

function Get-SuchVersion {
    param([string]$SourceRoot='')
    if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
        $SourceRoot = [System.IO.Path]::GetFullPath((Join-Path $script:SuchWindowsPathsScriptRoot '..'))
    }
    $versionFile = Join-Path $SourceRoot 'VERSION'
    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
        throw "Such VERSION file is missing: $versionFile"
    }
    $version = ([System.IO.File]::ReadAllText($versionFile)).Trim()
    if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') {
        throw "Invalid Such VERSION value: $version"
    }
    return $version
}

function Get-SuchVersionTag {
    param([string]$SourceRoot='')
    $version = Get-SuchVersion -SourceRoot $SourceRoot
    return ('v' + ($version -replace '\.', ''))
}

function Get-SuchWindowsKnownFolder {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet('UserProfile','LocalApplicationData','ApplicationData','ProgramFiles','CommonApplicationData','Programs','CommonPrograms','Desktop','CommonDesktopDirectory')]
        [string]$Name
    )

    try {
        $specialFolder = switch ($Name) {
            'UserProfile'          { [System.Environment+SpecialFolder]::UserProfile }
            'LocalApplicationData' { [System.Environment+SpecialFolder]::LocalApplicationData }
            'ApplicationData'      { [System.Environment+SpecialFolder]::ApplicationData }
            'ProgramFiles'         { [System.Environment+SpecialFolder]::ProgramFiles }
            'CommonApplicationData'{ [System.Environment+SpecialFolder]::CommonApplicationData }
            'Programs'             { [System.Environment+SpecialFolder]::Programs }
            'CommonPrograms'       { [System.Environment+SpecialFolder]::CommonPrograms }
            'Desktop'              { [System.Environment+SpecialFolder]::Desktop }
            'CommonDesktopDirectory' { [System.Environment+SpecialFolder]::CommonDesktopDirectory }
            default                { throw "Unsupported Windows known folder: $Name" }
        }
        $folder = [System.Environment]::GetFolderPath($specialFolder)
    } catch {
        throw ("Windows known-folder resolution failed for {0}: {1}" -f $Name, $_.Exception.Message)
    }
    if ([string]::IsNullOrWhiteSpace($folder)) {
        throw "Windows known-folder resolution returned an empty path for $Name"
    }
    return [System.IO.Path]::GetFullPath($folder)
}

function Get-SuchWindowsInstallRoot {
    $localData = Get-SuchWindowsKnownFolder -Name 'LocalApplicationData'
    return [System.IO.Path]::GetFullPath((Join-Path $localData 'Programs\Such'))
}

function Send-SuchWindowsEnvironmentChanged {
    if (-not ('SuchEnvironmentNative' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SuchEnvironmentNative {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam,
        uint flags, uint timeout, out UIntPtr result);
}
'@
    }
    $result = [UIntPtr]::Zero
    [void][SuchEnvironmentNative]::SendMessageTimeout(
        [IntPtr]0xffff, 0x001A, [UIntPtr]::Zero, 'Environment',
        0x0002, 5000, [ref]$result)
}

function Add-SuchWindowsCommandPath {
    param([Parameter(Mandatory=$true)][string]$Directory)
    $full = [System.IO.Path]::GetFullPath($Directory).TrimEnd([char[]]'\/')
    $userPath = [System.Environment]::GetEnvironmentVariable('Path', 'User')
    $parts = @($userPath -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if (-not ($parts | Where-Object { $_.TrimEnd([char[]]'\/').Equals($full, [System.StringComparison]::OrdinalIgnoreCase) })) {
        [System.Environment]::SetEnvironmentVariable('Path', (($full) + ';' + ($parts -join ';')), 'User')
        Send-SuchWindowsEnvironmentChanged
    }
}

function Remove-SuchWindowsCommandPath {
    param([Parameter(Mandatory=$true)][string]$Directory)
    $full = [System.IO.Path]::GetFullPath($Directory).TrimEnd([char[]]'\/')
    $userPath = [System.Environment]::GetEnvironmentVariable('Path', 'User')
    $parts = @($userPath -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        -not $_.TrimEnd([char[]]'\/').Equals($full, [System.StringComparison]::OrdinalIgnoreCase)
    })
    [System.Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
    Send-SuchWindowsEnvironmentChanged
}


function Get-SuchWindowsStartMenuPrograms {
    return (Get-SuchWindowsKnownFolder -Name 'Programs')
}

function Get-SuchWindowsDesktop {
    return (Get-SuchWindowsKnownFolder -Name 'Desktop')
}

function Get-SuchWindowsLegacyInstallRoots {
    return @()
}

function Get-SuchWindowsLegacyShortcutPaths {
    return @()
}

function Get-SuchWindowsLegacyStateRoots {
    $localData = Get-SuchWindowsKnownFolder -Name 'LocalApplicationData'
    $roamingData = Get-SuchWindowsKnownFolder -Name 'ApplicationData'
    return @(
        (Join-Path $localData 'Such'),
        (Join-Path $roamingData 'Such')
    )
}


# Historical v1.1.x machine-wide installers used these exact locations. Keep
# this list explicit: uninstall cleanup must never broaden into heuristic
# deletion outside Such-owned paths.
function Get-SuchWindowsLegacyMachineInstallRoots {
    return @([System.IO.Path]::GetFullPath('C:\Heritage\Such'))
}

function Get-SuchWindowsLegacyMachineShortcutPaths {
    $commonPrograms = Get-SuchWindowsKnownFolder -Name 'CommonPrograms'
    $commonDesktop = Get-SuchWindowsKnownFolder -Name 'CommonDesktopDirectory'
    return @(
        (Join-Path $commonPrograms 'Heritage Inc.\Such.lnk'),
        (Join-Path $commonDesktop 'Such.lnk')
    )
}

function Get-SuchWindowsLegacyMachineCommandPaths {
    return @([System.IO.Path]::GetFullPath('C:\Heritage\Such\cli'))
}

function Get-SuchWindowsLegacyMachineRegistryPaths {
    return @(
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch'
    )
}

function Get-SuchWindowsLegacyMachineSecurityStateRoot {
    $programData = Get-SuchWindowsKnownFolder -Name 'CommonApplicationData'
    return (Join-Path $programData 'Heritage\Such\Security')
}

function Test-SuchWindowsAdministrator {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object System.Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Stop-SuchWindowsProcessesInRoots {
    param([Parameter(Mandatory=$true)][string[]]$Roots)
    $normalized = @($Roots | ForEach-Object {
        [System.IO.Path]::GetFullPath($_).TrimEnd([char[]]'\/') + [System.IO.Path]::DirectorySeparatorChar
    })
    foreach ($process in @(Get-Process -ErrorAction SilentlyContinue)) {
        $inside = $false
        try {
            $processPath = $process.Path
            if ([string]::IsNullOrWhiteSpace($processPath)) { continue }
            $full = [System.IO.Path]::GetFullPath($processPath)
            foreach ($root in $normalized) {
                if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { $inside=$true; break }
            }
            if ($inside) {
                Write-Host ("Stopping installed Such process: {0} ({1})" -f $process.Id, $full)
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
                try { Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue } catch {}
            }
        } catch {
            if ($inside) { throw }
        }
    }
}

function Remove-SuchWindowsLegacyArtifacts {
    foreach ($shortcut in @(Get-SuchWindowsLegacyShortcutPaths)) {
        Remove-Item -LiteralPath $shortcut -Force -ErrorAction SilentlyContinue
    }
    foreach ($root in @(Get-SuchWindowsLegacyInstallRoots)) {
        if (Test-Path -LiteralPath $root -PathType Container) {
            $looksLikeSuch = (Test-Path -LiteralPath (Join-Path $root 'Such.exe') -PathType Leaf) -or
                             (Test-Path -LiteralPath (Join-Path $root 'uninstall_windows.ps1') -PathType Leaf)
            if ($looksLikeSuch) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction Stop }
        }
    }
}

function Get-SuchWindowsSecurityStateRoot {
    $localData = Get-SuchWindowsKnownFolder -Name 'LocalApplicationData'
    return (Join-Path $localData 'Heritage\Such\Security')
}

function Get-SuchWindowsUserFontDir {
    $local = Get-SuchWindowsKnownFolder -Name 'LocalApplicationData'
    return (Join-Path $local 'Microsoft\Windows\Fonts')
}

function Get-SuchSourceRoot {
    param([Parameter(Mandatory=$true)][string]$ScriptRoot)
    return [System.IO.Path]::GetFullPath((Join-Path $ScriptRoot '..'))
}


function Get-SuchWindowsBuildBase {
    param([Parameter(Mandatory=$true)][string]$SourceRoot)
    # Windows build state is repository-local by contract. Do not redirect it
    # to a user-profile SW workspace, temp directory, or junction-backed tree.
    return [System.IO.Path]::GetFullPath((Join-Path $SourceRoot 'build'))
}

function Get-SuchWindowsToolchainRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:SUCH_TOOLCHAIN_ROOT)) {
        return [System.IO.Path]::GetFullPath($env:SUCH_TOOLCHAIN_ROOT)
    }
    $profile = Get-SuchWindowsKnownFolder -Name 'UserProfile'
    return [System.IO.Path]::GetFullPath((Join-Path $profile 'STC'))
}

function Get-SuchWindowsOfflineRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:SUCH_WINDOWS_OFFLINE_ROOT)) {
        return [System.IO.Path]::GetFullPath($env:SUCH_WINDOWS_OFFLINE_ROOT)
    }
    $profile = Get-SuchWindowsKnownFolder -Name 'UserProfile'
    return [System.IO.Path]::GetFullPath((Join-Path $profile 'STC\offline'))
}

function Get-SuchWindowsOfflineVsLayout {
    if (-not [string]::IsNullOrWhiteSpace($env:SUCH_VS_OFFLINE_LAYOUT)) {
        return [System.IO.Path]::GetFullPath($env:SUCH_VS_OFFLINE_LAYOUT)
    }
    return (Join-Path (Get-SuchWindowsOfflineRoot) 'VS17')
}

function Resolve-SuchWindowsTool {
    param(
        [Parameter(Mandatory=$true)][ValidateSet('cmake','ctest')][string]$Name
    )

    $exe = "{0}.exe" -f $Name
    $candidates = New-Object System.Collections.Generic.List[string]
    $toolchainRoot = Get-SuchWindowsToolchainRoot
    $portableRoot = Join-Path $toolchainRoot 'cmake-3.31.12\cmake-3.31.12-windows-x86_64\bin'
    [void]$candidates.Add((Join-Path $portableRoot $exe))

    $command = Get-Command $exe -ErrorAction SilentlyContinue
    if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Path)) {
        [void]$candidates.Add($command.Path)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        [void]$candidates.Add((Join-Path $env:ProgramFiles ("CMake\bin\{0}" -f $exe)))
        [void]$candidates.Add((Join-Path $env:ProgramFiles ("PowerShell\7\{0}" -f $exe)))
    }

    $pf86 = ${env:ProgramFiles(x86)}
    if (-not [string]::IsNullOrWhiteSpace($pf86)) {
        $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $relative = "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\{0}" -f $exe
            try {
                $found = @(& $vswhere -all -products * -find $relative 2>$null)
                foreach ($toolPath in $found) {
                    if (-not [string]::IsNullOrWhiteSpace($toolPath)) { [void]$candidates.Add($toolPath.Trim()) }
                }
            } catch {
                # Candidate probing below provides the deterministic failure path.
            }
        }
    }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    return $null
}

function Get-SuchCMakeGenerators {
    param([Parameter(Mandatory=$true)][string]$CMakePath)
    try {
        $json = (& $CMakePath -E capabilities 2>$null | Out-String)
        # Do not consult $LASTEXITCODE here. It is process-global state in Windows
        # PowerShell 5.1 and can retain a stale value from an earlier native call.
        # Valid capability JSON is the authority for generator discovery.
        if ([string]::IsNullOrWhiteSpace($json)) { return @() }
        $cap = $json | ConvertFrom-Json
        if ($null -eq $cap.generators) { return @() }
        return @($cap.generators | ForEach-Object { [string]$_.name } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    } catch {
        return @()
    }
}

function Resolve-SuchWindowsGenerator {
    param(
        [Parameter(Mandatory=$true)][string]$CMakePath,
        [string]$Requested=''
    )
    $available = @(Get-SuchCMakeGenerators -CMakePath $CMakePath)

    if (-not [string]::IsNullOrWhiteSpace($Requested) -and $Requested -ne 'Auto') {
        if ($available.Count -gt 0 -and $available -notcontains $Requested) {
            throw "Requested CMake generator is not supported by this CMake: $Requested`nAvailable generators:`n  $($available -join "`n  ")"
        }
        return $Requested
    }

    # Such's Windows qualification authority is VS2022/MSVC. Do not let a newer
    # side-by-side Visual Studio (for example VS18) hijack generator selection.
    $pf86 = ${env:ProgramFiles(x86)}
    $vs2022 = $null
    if (-not [string]::IsNullOrWhiteSpace($pf86)) {
        $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            try {
                $found = @(& $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
                if ($found.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace([string]$found[0])) { $vs2022 = ([string]$found[0]).Trim() }
            } catch {}
        }
    }
    if ([string]::IsNullOrWhiteSpace($vs2022)) {
        throw 'Visual Studio 2022 C++ toolchain was not detected. Run bootstrap_windows.cmd first.'
    }

    $generator='Visual Studio 17 2022'
    if ($available.Count -gt 0 -and $available -notcontains $generator) {
        throw "CMake does not expose the required VS2022 generator '$generator'. Run bootstrap_windows.cmd to prepare the pinned CMake toolchain."
    }
    return $generator
}

function Test-SuchCMakeCacheMatches {
    param(
        [Parameter(Mandatory=$true)][string]$BuildDir,
        [Parameter(Mandatory=$true)][string]$Generator,
        [Parameter(Mandatory=$true)][string]$Arch,
        [Parameter(Mandatory=$true)][string]$SourcePath
    )
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { return $true }
    try {
        $text = [System.IO.File]::ReadAllText($cache)
        $genMatch = [regex]::Match($text, '(?m)^CMAKE_GENERATOR:INTERNAL=(.+)$')
        $platMatch = [regex]::Match($text, '(?m)^CMAKE_GENERATOR_PLATFORM:INTERNAL=(.*)$')
        $homeMatch = [regex]::Match($text, '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$')
        if ($genMatch.Success -and $genMatch.Groups[1].Value.Trim() -ne $Generator) { return $false }
        if ($platMatch.Success -and -not [string]::IsNullOrWhiteSpace($platMatch.Groups[1].Value) -and $platMatch.Groups[1].Value.Trim() -ne $Arch) { return $false }
        if ($homeMatch.Success) {
            $cached = [System.IO.Path]::GetFullPath($homeMatch.Groups[1].Value.Trim()).TrimEnd([char[]]'\/')
            $expected = [System.IO.Path]::GetFullPath($SourcePath).TrimEnd([char[]]'\/')
            if (-not $cached.Equals($expected, [System.StringComparison]::OrdinalIgnoreCase)) { return $false }
        }
        return $true
    } catch {
        return $false
    }
}

function Resolve-SuchWindowsRuntime {
    param(
        [Parameter(Mandatory=$true)][string]$SourceRoot,
        [string]$ExplicitPath=''
    )

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        try { $explicitFull = [System.IO.Path]::GetFullPath($ExplicitPath) }
        catch { throw "Invalid runtime library path: $ExplicitPath" }
        if (-not (Test-Path -LiteralPath $explicitFull -PathType Leaf)) {
            throw "Runtime library not found: $explicitFull"
        }
        return $explicitFull
    }

    # Windows-only package authority: visible runtime payload first.
    # Compatibility fallback for older source layouts.
    [void]$candidates.Add((Join-Path $SourceRoot 'runtime\windows-x64\SuchRuntimePrivate.dll'))
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        try { $full = [System.IO.Path]::GetFullPath($candidate) } catch { continue }
        if (Test-Path -LiteralPath $full -PathType Leaf) { return $full }
    }
    return $null
}


function Get-SuchPeArchitecture {
    param([Parameter(Mandatory=$true)][string]$PePath)

    $stream = [System.IO.File]::Open($PePath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 64) { throw "Not a valid PE image: $PePath" }
        $reader = New-Object System.IO.BinaryReader($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Missing MZ header: $PePath" }
            $stream.Position = 0x3C
            $peOffset = $reader.ReadUInt32()
            if ($peOffset + 6 -gt $stream.Length) { throw "Invalid PE header offset: $PePath" }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) { throw "Missing PE signature: $PePath" }
            $machine = $reader.ReadUInt16()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }

    if ($machine -eq 0x014c) { return 'Win32' }
    if ($machine -eq 0x8664) { return 'x64' }
    if ($machine -eq 0xAA64) { return 'ARM64' }
    return ('0x{0:X4}' -f $machine)
}

function Assert-SuchWindowsRuntimeArchitecture {
    param(
        [Parameter(Mandatory=$true)][string]$RuntimePath,
        [Parameter(Mandatory=$true)][string]$Arch
    )
    $actual = Get-SuchPeArchitecture -PePath $RuntimePath
    if ($actual -ne $Arch) {
        throw ("SuchRuntimePrivate.dll architecture mismatch. Requested {0}, DLL is {1}: {2}" -f $Arch, $actual, $RuntimePath)
    }
    Write-Host ("Private runtime  : {0} ({1})" -f $RuntimePath, $actual)
}


function Get-SuchWindowsWorkspace {
    param(
        [Parameter(Mandatory=$true)][string]$SourceRoot,
        [Parameter(Mandatory=$true)][string]$Arch,
        [Parameter(Mandatory=$true)][string]$Config,
        [switch]$NoCreate
    )

    $sourceRootFull = [System.IO.Path]::GetFullPath($SourceRoot)
    $base = Get-SuchWindowsBuildBase -SourceRoot $sourceRootFull
    $archKey = $Arch.ToLowerInvariant()
    $configKey = $Config.ToLowerInvariant()

    # CMake/MSBuild build tree and all transient attestation state live under
    # the checkout. There is no external SW workspace and no source junction.
    $buildDir = Join-Path $base ("windows-{0}" -f $archKey)
    $installDir = Join-Path $base ("stage\windows-{0}\{1}" -f $archKey,$configKey)
    $consumerDir = Join-Path $base ("consumer\windows-{0}\{1}" -f $archKey,$configKey)
    $stateDir = Join-Path $base ("state\windows-{0}\{1}" -f $archKey,$configKey)

    if (-not $NoCreate.IsPresent) {
        foreach ($dir in @($base,$buildDir,$installDir,$consumerDir,$stateDir)) {
            New-Item -ItemType Directory -Force -Path $dir -ErrorAction Stop | Out-Null
        }
    }

    return [pscustomobject]@{
        SourceRoot = $sourceRootFull
        Arch = $Arch
        Config = $Config
        CMakeSource = $sourceRootFull
        Base = $base
        VersionRoot = $base
        BuildDir = $buildDir
        InstallDir = $installDir
        ConsumerDir = $consumerDir
        StateDir = $stateDir
        SourceLink = ''
    }
}

function Write-SuchPathDiagnostics {
    param([Parameter(Mandatory=$true)]$Workspace)
    Write-Host ('Source root     : {0} ({1} chars)' -f $Workspace.SourceRoot, $Workspace.SourceRoot.Length)
    Write-Host ('CMake -S        : {0} ({1} chars)' -f $Workspace.CMakeSource, $Workspace.CMakeSource.Length)
    Write-Host ('Build directory : {0} ({1} chars)' -f $Workspace.BuildDir, $Workspace.BuildDir.Length)
    Write-Host ('Install dir     : {0} ({1} chars)' -f $Workspace.InstallDir, $Workspace.InstallDir.Length)
    Write-Host ('State dir       : {0} ({1} chars)' -f $Workspace.StateDir, $Workspace.StateDir.Length)
}

function Get-SuchExpectedRuntimeSha256 {
    param(
        [Parameter(Mandatory=$true)][string]$SourceRoot,
        [Parameter(Mandatory=$true)][ValidateSet('windows-x64','linux-x64')][string]$Platform,
        [Parameter(Mandatory=$true)][string]$FileName
    )
    $manifest = Join-Path $SourceRoot 'runtime\RUNTIME_SHA256.txt'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Runtime SHA-256 manifest is missing: $manifest"
    }
    foreach ($line in [System.IO.File]::ReadAllLines($manifest)) {
        $parts = @($line -split '\s+') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        if ($parts.Count -ge 3 -and $parts[0] -eq $Platform -and $parts[1] -eq $FileName) {
            return $parts[2].ToLowerInvariant()
        }
    }
    throw "Runtime SHA-256 entry not found for $Platform/$FileName"
}

function Assert-SuchWindowsRuntimeSha256 {
    param(
        [Parameter(Mandatory=$true)][string]$SourceRoot,
        [Parameter(Mandatory=$true)][string]$RuntimePath
    )
    $expected = Get-SuchExpectedRuntimeSha256 -SourceRoot $SourceRoot -Platform 'windows-x64' -FileName 'SuchRuntimePrivate.dll'
    $actual = (Get-FileHash -LiteralPath $RuntimePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) {
        throw ("SuchRuntimePrivate.dll SHA-256 mismatch. Expected {0}, actual {1}: {2}" -f $expected, $actual, $RuntimePath)
    }
    Write-Host ("Release runtime SHA-256 PASS: {0}" -f $actual)
}
