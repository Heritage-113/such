Set-StrictMode -Version Latest

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
    return [System.IO.Path]::GetFullPath('C:\Heritage\Such')
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
    $machinePath = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
    $parts = @($machinePath -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if (-not ($parts | Where-Object { $_.TrimEnd([char[]]'\/').Equals($full, [System.StringComparison]::OrdinalIgnoreCase) })) {
        [System.Environment]::SetEnvironmentVariable('Path', (($full) + ';' + ($parts -join ';')), 'Machine')
        Send-SuchWindowsEnvironmentChanged
    }
}

function Remove-SuchWindowsCommandPath {
    param([Parameter(Mandatory=$true)][string]$Directory)
    $full = [System.IO.Path]::GetFullPath($Directory).TrimEnd([char[]]'\/')
    $machinePath = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
    $parts = @($machinePath -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        -not $_.TrimEnd([char[]]'\/').Equals($full, [System.StringComparison]::OrdinalIgnoreCase)
    })
    [System.Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'Machine')
    Send-SuchWindowsEnvironmentChanged
}


function Get-SuchWindowsStartMenuPrograms {
    return (Get-SuchWindowsKnownFolder -Name 'CommonPrograms')
}

function Get-SuchWindowsDesktop {
    return (Get-SuchWindowsKnownFolder -Name 'CommonDesktopDirectory')
}

function Get-SuchWindowsLegacyInstallRoots {
    $local = Get-SuchWindowsKnownFolder -Name 'LocalApplicationData'
    return @([System.IO.Path]::GetFullPath((Join-Path $local 'Programs\Such')))
}

function Get-SuchWindowsLegacyShortcutPaths {
    $userPrograms = Get-SuchWindowsKnownFolder -Name 'Programs'
    $userDesktop = Get-SuchWindowsKnownFolder -Name 'Desktop'
    return @(
        (Join-Path $userPrograms 'Heritage Inc.\Such.lnk'),
        (Join-Path $userDesktop 'Such.lnk')
    )
}

function Stop-SuchWindowsProcessesInRoots {
    param([Parameter(Mandatory=$true)][string[]]$Roots)
    $normalized = @($Roots | ForEach-Object {
        [System.IO.Path]::GetFullPath($_).TrimEnd([char[]]'\/') + [System.IO.Path]::DirectorySeparatorChar
    })
    foreach ($process in @(Get-Process -ErrorAction SilentlyContinue)) {
        $inside = $false
        try {
            $path = $process.Path
            if ([string]::IsNullOrWhiteSpace($path)) { continue }
            $full = [System.IO.Path]::GetFullPath($path)
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
    Remove-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\App Paths\Such.exe' -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\HeritageSuch' -Recurse -Force -ErrorAction SilentlyContinue
    foreach ($root in @(Get-SuchWindowsLegacyInstallRoots)) {
        if (Test-Path -LiteralPath $root -PathType Container) {
            $looksLikeSuch = (Test-Path -LiteralPath (Join-Path $root 'Such.exe') -PathType Leaf) -or
                             (Test-Path -LiteralPath (Join-Path $root 'uninstall_windows.ps1') -PathType Leaf)
            if ($looksLikeSuch) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction Stop }
        }
    }
}

function Test-SuchWindowsAdministrator {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object System.Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-SuchWindowsAdministrator {
    if (-not (Test-SuchWindowsAdministrator)) {
        throw 'Such v1.1 installs under Program Files and requires Administrator. Re-run install_windows.cmd or uninstall_windows.cmd as Administrator.'
    }
}

function Get-SuchWindowsSecurityStateRoot {
    $programData = Get-SuchWindowsKnownFolder -Name 'CommonApplicationData'
    return (Join-Path $programData 'Heritage\Such\Security')
}

function Get-SuchWindowsUserFontDir {
    $local = Get-SuchWindowsKnownFolder -Name 'LocalApplicationData'
    return (Join-Path $local 'Microsoft\Windows\Fonts')
}

function Get-SuchSourceRoot {
    param([Parameter(Mandatory=$true)][string]$ScriptRoot)
    return [System.IO.Path]::GetFullPath((Join-Path $ScriptRoot '..'))
}

function Resolve-SuchWindowsTool {
    param(
        [Parameter(Mandatory=$true)][ValidateSet('cmake','ctest')][string]$Name
    )

    $exe = "{0}.exe" -f $Name
    $command = Get-Command $exe -ErrorAction SilentlyContinue
    if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Path)) { return $command.Path }

    $candidates = New-Object System.Collections.Generic.List[string]
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
                foreach ($path in $found) {
                    if (-not [string]::IsNullOrWhiteSpace($path)) { [void]$candidates.Add($path.Trim()) }
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
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($json)) { return @() }
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

    $vsMajor = $null
    if (-not [string]::IsNullOrWhiteSpace($env:VisualStudioVersion)) {
        $m = [regex]::Match($env:VisualStudioVersion, '^(\d+)')
        if ($m.Success) { $vsMajor = [int]$m.Groups[1].Value }
    }

    if ($null -eq $vsMajor) {
        $pf86 = ${env:ProgramFiles(x86)}
        if (-not [string]::IsNullOrWhiteSpace($pf86)) {
            $vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
            if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
                try {
                    $version = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2>$null | Select-Object -First 1)
                    if (-not [string]::IsNullOrWhiteSpace($version)) {
                        $m = [regex]::Match($version.Trim(), '^(\d+)')
                        if ($m.Success) { $vsMajor = [int]$m.Groups[1].Value }
                    }
                } catch {}
            }
        }
    }

    if ($null -eq $vsMajor) {
        throw 'No Visual Studio C++ toolchain was detected. Install Visual Studio/Build Tools with Desktop development with C++.'
    }

    if ($available.Count -eq 0) {
        if ($vsMajor -eq 17) { return 'Visual Studio 17 2022' }
        throw "Visual Studio major version $vsMajor is installed, but CMake generator discovery failed. Pass -Generator explicitly or install a newer CMake."
    }

    $prefix = "Visual Studio $vsMajor "
    $matches = @($available | Where-Object { $_.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase) })
    if ($matches.Count -eq 0) {
        throw "Installed Visual Studio major version $vsMajor is not supported by this CMake. Update CMake.`nAvailable generators:`n  $($available -join "`n  ")"
    }
    return ($matches | Select-Object -First 1)
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

    [void]$candidates.Add((Join-Path $SourceRoot '.runtime\windows-x64\SuchRuntimePrivate.dll'))
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        try { $full = [System.IO.Path]::GetFullPath($candidate) } catch { continue }
        if (Test-Path -LiteralPath $full -PathType Leaf) { return $full }
    }
    return $null
}


function Get-SuchPeArchitecture {
    param([Parameter(Mandatory=$true)][string]$Path)

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 64) { throw "Not a valid PE image: $Path" }
        $reader = New-Object System.IO.BinaryReader($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Missing MZ header: $Path" }
            $stream.Position = 0x3C
            $peOffset = $reader.ReadUInt32()
            if ($peOffset + 6 -gt $stream.Length) { throw "Invalid PE header offset: $Path" }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) { throw "Missing PE signature: $Path" }
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
    $actual = Get-SuchPeArchitecture -Path $RuntimePath
    if ($actual -ne $Arch) {
        throw ("SuchRuntimePrivate.dll architecture mismatch. Requested {0}, DLL is {1}: {2}" -f $Arch, $actual, $RuntimePath)
    }
    Write-Host ("Private runtime  : {0} ({1})" -f $RuntimePath, $actual)
}


function Get-SuchWindowsWorkspace {
    param(
        [Parameter(Mandatory=$true)][string]$SourceRoot,
        [Parameter(Mandatory=$true)][string]$Arch,
        [Parameter(Mandatory=$true)][string]$Config
    )

    $base = $env:SUCH_BUILD_ROOT
    $usesSourceBuildRoot = [string]::IsNullOrWhiteSpace($base)
    if ($usesSourceBuildRoot) { $base = Join-Path $SourceRoot 'build' }
    $base = [System.IO.Path]::GetFullPath($base)

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $sourceBytes = [System.Text.Encoding]::UTF8.GetBytes($SourceRoot.ToLowerInvariant())
        $sourceHash = $sha256.ComputeHash($sourceBytes)
    } finally {
        $sha256.Dispose()
    }
    $sourceId = -join ($sourceHash[0..3] | ForEach-Object { $_.ToString('x2') })
    $versionRoot = if ($usesSourceBuildRoot) { $base } else { Join-Path $base ("v110-{0}" -f $sourceId) }
    $configSuffix = if ($Config -eq 'Release') { '' } else { '-' + $Config.ToLowerInvariant() }
    $buildDir = Join-Path $versionRoot ("windows-{0}{1}" -f $Arch.ToLowerInvariant(), $configSuffix)
    $installDir = Join-Path $versionRoot ("stage-windows-{0}{1}" -f $Arch.ToLowerInvariant(), $configSuffix)
    $consumerDir = Join-Path $versionRoot ("consumer-windows-{0}{1}" -f $Arch.ToLowerInvariant(), $configSuffix)
    $sourceLink = Join-Path $versionRoot 'src'

    New-Item -ItemType Directory -Force -Path $versionRoot | Out-Null

    # Use the real source path unless it is long enough to risk MSBuild/FileTracker.
    # This removes the junction from the normal code path, which is more reliable on
    # locked-down Windows installations and network/external drives.
    $cmakeSource = $SourceRoot
    if ($SourceRoot.Length -ge 120) {
        try {
            if (Test-Path -LiteralPath $sourceLink) {
                $item = Get-Item -LiteralPath $sourceLink -Force
                if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) {
                    throw "Refusing to remove non-reparse path at short-source location: $sourceLink"
                }
                Remove-Item -LiteralPath $sourceLink -Force -ErrorAction Stop
            }
            New-Item -ItemType Junction -Path $sourceLink -Target $SourceRoot -ErrorAction Stop | Out-Null
            $cmakeSource = $sourceLink
        }
        catch {
            Write-Warning "Could not create short source junction '$sourceLink': $($_.Exception.Message)"
            Write-Warning 'Falling back to the original source path. Extracting Such closer to the drive root is recommended.'
        }
    }

    return [pscustomobject]@{
        SourceRoot = $SourceRoot
        CMakeSource = $cmakeSource
        Base = $base
        VersionRoot = $versionRoot
        BuildDir = $buildDir
        InstallDir = $installDir
        ConsumerDir = $consumerDir
        SourceLink = $sourceLink
    }
}

function Write-SuchPathDiagnostics {
    param([Parameter(Mandatory=$true)]$Workspace)
    Write-Host ('Source root     : {0} ({1} chars)' -f $Workspace.SourceRoot, $Workspace.SourceRoot.Length)
    Write-Host ('CMake -S        : {0} ({1} chars)' -f $Workspace.CMakeSource, $Workspace.CMakeSource.Length)
    Write-Host ('Build directory : {0} ({1} chars)' -f $Workspace.BuildDir, $Workspace.BuildDir.Length)
    Write-Host ('Install dir     : {0} ({1} chars)' -f $Workspace.InstallDir, $Workspace.InstallDir.Length)
}

function Get-SuchExpectedRuntimeSha256 {
    param(
        [Parameter(Mandatory=$true)][string]$SourceRoot,
        [Parameter(Mandatory=$true)][ValidateSet('windows-x64','linux-x64')][string]$Platform,
        [Parameter(Mandatory=$true)][string]$FileName
    )
    $manifest = Join-Path $SourceRoot 'runtime\RUNTIME_SHA256_v1.1.7.txt'
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
