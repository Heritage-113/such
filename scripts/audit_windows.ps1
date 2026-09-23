[CmdletBinding()]
param(
  [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
  $SourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
} else {
  $SourceRoot = [System.IO.Path]::GetFullPath($SourceRoot)
}

function Fail-Audit {
  param([Parameter(Mandatory=$true)][string]$Message)
  throw "Windows source audit failed: $Message"
}

# Parse every PowerShell script before invoking CMake. This specifically catches
# interpolation errors such as "$value:" before a shared helper is dot-sourced.
# It works in both Windows PowerShell 5.1 and PowerShell 7 and needs no Python.
$psFiles = Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'scripts') -Filter '*.ps1' -File
foreach ($file in $psFiles) {
  $tokens = $null
  $errors = $null
  [void][System.Management.Automation.Language.Parser]::ParseFile(
    $file.FullName,
    [ref]$tokens,
    [ref]$errors)
  if (@($errors).Count -ne 0) {
    $details = ($errors | ForEach-Object { "line $($_.Extent.StartLineNumber): $($_.Message)" }) -join '; '
    Fail-Audit "$($file.Name): $details"
  }
}

# Windows PowerShell 5.1 compatibility: nested .NET types must be fully qualified.
foreach ($file in $psFiles) {
  $text = Get-Content -LiteralPath $file.FullName -Raw
  if ($text -match '\[Environment\+SpecialFolder\]') {
    Fail-Audit "$($file.Name): use the fully qualified System.Environment+SpecialFolder type."
  }
  if ($text -match '\b[0-9]+u\b') {
    Fail-Audit "$($file.Name): unsigned integer literal suffixes are unsupported by Windows PowerShell 5.1."
  }
}

# Build/install scripts must not depend directly on APPDATA environment variables.
# Use Windows known-folder APIs instead so stripped/custom shells do not crash.
foreach ($file in $psFiles) {
  if ($file.Name -eq 'audit_windows.ps1') { continue }
  $text = Get-Content -LiteralPath $file.FullName -Raw
  if ($text -match '\$env:(?:APPDATA|LOCALAPPDATA)') {
    Fail-Audit "$($file.Name): direct APPDATA/LOCALAPPDATA environment access is forbidden; use a known-folder resolver."
  }
}

# Such is an on-demand application, not a resident service. Product scripts must
# never register logon/startup hooks, scheduled tasks, or Windows services. The
# audit script itself is excluded because it contains the detection patterns.
$backgroundRegistrationPatterns = @(
  @{ Pattern = 'CurrentVersion\\(?:Run|RunOnce)'; Why = 'Run/RunOnce autorun registration is forbidden.' },
  @{ Pattern = '(?:^|[\\/])Startup(?:[\\/]|$)'; Why = 'Startup-folder autorun registration is forbidden.' },
  @{ Pattern = '\b(?:Register-ScheduledTask|New-ScheduledTask|schtasks(?:\.exe)?\s+/(?:Create|Change))\b'; Why = 'Scheduled-task registration is forbidden.' },
  @{ Pattern = '\b(?:New-Service|Set-Service|sc(?:\.exe)?\s+(?:create|config|start)|CreateService)\b'; Why = 'Windows service registration/start is forbidden.' },
  @{ Pattern = '\bStart-Job\b'; Why = 'Detached PowerShell background jobs are forbidden in product scripts.' }
)
foreach ($file in $psFiles) {
  if ($file.Name -eq 'audit_windows.ps1') { continue }
  $text = Get-Content -LiteralPath $file.FullName -Raw
  foreach ($rule in $backgroundRegistrationPatterns) {
    if ([regex]::IsMatch($text, $rule.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
      Fail-Audit "$($file.Name): $($rule.Why)"
    }
  }
}
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'scripts') -Filter '*.cmd' -File)) {
  $text = Get-Content -LiteralPath $file.FullName -Raw
  if ($text -match '(?i)\bschtasks(?:\.exe)?\s+/(?:Create|Change)\b|\bsc(?:\.exe)?\s+(?:create|config|start)\b') {
    Fail-Audit "$($file.Name): background service/task registration is forbidden."
  }
}

# Windows entrypoint scripts must expose actionable exception diagnostics.
foreach ($entryName in @('build_windows.ps1','install_windows.ps1','doctor_windows.ps1','verify_windows.ps1','release_windows.ps1','uninstall_windows.ps1')) {
  $entryPath = Join-Path (Join-Path $SourceRoot 'scripts') $entryName
  $entryText = Get-Content -LiteralPath $entryPath -Raw
  if ($entryText -notmatch '\[SUCH:FATAL\]') {
    Fail-Audit "${entryName}: missing top-level exception diagnostics."
  }
}

$requiredRelative = @(
  'CMakeLists.txt',
  'platform/windows/Win32Frontend.cpp',
  'src/runtime/RuntimeClient.cpp',
  'tests/fake_runtime.cpp',
  'tests/public_contracts.cpp',
  'tests/runtime_client_smoke.cpp',
  'include/such/runtime/RuntimeABI.h'
)
foreach ($relative in $requiredRelative) {
  $path = Join-Path $SourceRoot $relative
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    Fail-Audit "missing required file: $relative"
  }
}

$win = Get-Content -LiteralPath (Join-Path $SourceRoot 'platform/windows/Win32Frontend.cpp') -Raw
$runtime = Get-Content -LiteralPath (Join-Path $SourceRoot 'src/runtime/RuntimeClient.cpp') -Raw
$cmake = Get-Content -LiteralPath (Join-Path $SourceRoot 'CMakeLists.txt') -Raw
$tests = ((Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'tests') -Filter '*.cpp' -File | ForEach-Object {
  Get-Content -LiteralPath $_.FullName -Raw
}) -join "`n")

$forbidden = @(
  @{ Text = $win; Pattern = '\bSIID_FAVORITES\b'; Why = 'SIID_FAVORITES is not a valid SHSTOCKICONID.' },
  @{ Text = $win; Pattern = '\b(?:SetPointerCapture|ReleasePointerCapture)\b'; Why = 'Do not use nonexistent classic HWND pointer-capture APIs.' },
  @{ Text = $win; Pattern = 'std::(?:max|min)\s*\(\s*(?:client|row|search|suggested)\.(?:left|right|top|bottom)'; Why = 'Normalize RECT/LONG geometry before STL min/max.' },
  @{ Text = $tests; Pattern = '#define\s+CHECK[^\r\n]*do\s*\{'; Why = 'Avoid MSVC C4127/C2220 constant-condition CHECK macros under /WX.' },
  @{ Text = $tests; Pattern = 'CHECK\s*\(\s*design::k'; Why = 'Compile-time design invariants must use static_assert.' },
  @{ Text = $runtime; Pattern = 'reinterpret_cast\s*<[^>]+>\s*\(\s*GetProcAddress'; Why = 'Do not directly cast FARPROC under MSVC /W4 /WX.' },
  @{ Text = $runtime; Pattern = 'reinterpret_cast\s*<\s*Fn\s*>\s*\(\s*load_symbol'; Why = 'Bind runtime symbols without C4191-prone direct function-pointer casts.' },
  @{ Text = ($win + "`n" + $tests + "`n" + $cmake); Pattern = '\bsuch_v038\b'; Why = 'Active v1.1.7 sources must not regress target names to v0.3.8.' },
  @{ Text = $cmake; Pattern = 'test_v061_frontend\.cpp'; Why = 'Stale pre-split test path must not return.' }
)
foreach ($rule in $forbidden) {
  if ([regex]::IsMatch($rule.Text, $rule.Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
    Fail-Audit $rule.Why
  }
}

if ($win -notmatch 'MoveWindow\s*\(\s*gSearch\s*,\s*searchX\s*,\s*searchY\s*,\s*searchW\s*,\s*searchH\s*,\s*TRUE\s*\)') {
  Fail-Audit 'Expected the search EDIT control to use inset coordinates inside the parent-owned search surface.'
}
if ($cmake -notmatch '/W4' -or $cmake -notmatch '/WX') {
  Fail-Audit 'MSVC strict warning policy (/W4 and /WX) is missing.'
}
if ($cmake -match '/wd4127') {
  Fail-Audit 'C4127 must be fixed in source, not globally suppressed.'
}
if ($cmake -notmatch 'WINDOWS_EXPORT_ALL_SYMBOLS\s+ON') {
  Fail-Audit 'Windows test runtime DLL must export the ABI symbols used by GetProcAddress.'
}
if ($win -notmatch 'HeritageSuchV110Window') {
  Fail-Audit 'Win32 window class has not been promoted to the v1.1 identity.'
}

$pathsScript = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_paths.ps1') -Raw
if (-not $pathsScript.Contains("GetFullPath('C:\Heritage\Such')")) {
  Fail-Audit 'Windows install root must resolve to C:\Heritage\Such.'
}
$installScript = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/install_windows.ps1') -Raw
if ($installScript -match "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall") {
  Fail-Audit 'Machine-wide install must not register uninstall state under HKCU.'
}
if ($win -notmatch '#include\s+<such/Version\.h>') {
  Fail-Audit 'Win32 product title/version must come from generated such/Version.h.'
}

# Source-boundary checks apply only to source-owned material. VCS metadata,
# injected production runtimes, CI metadata, and build outputs are intentionally
# outside this audit's responsibility.
$ignoredRoots = @('.git', '.github', '.runtime', '.secure', 'build', 'dist')
$forbiddenNames = @('third_party', 'topodb', 'search.topodb')
foreach ($entry in Get-ChildItem -LiteralPath $SourceRoot -Recurse -Force) {
  $relative = $entry.FullName.Substring($SourceRoot.Length).TrimStart([char[]]'\\/')
  if ([string]::IsNullOrWhiteSpace($relative)) { continue }
  $firstPart = ($relative -split '[\\/]', 2)[0]
  if ($ignoredRoots -contains $firstPart) { continue }
  foreach ($name in $forbiddenNames) {
    if ($relative.IndexOf($name, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
      Fail-Audit "private-backend material leaked into public source tree: $relative"
    }
  }
  if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    Fail-Audit "reparse point/symlink is not allowed in public source tree: $relative"
  }
}

Write-Host 'Windows source audit PASS: scripts, current split tests, ABI loading, DLL exports, and public boundary checked.'
