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

# Only the PowerShell host materializer must parse/run under Windows PowerShell
# 5.1. Main Such Windows workflows are executed by the pinned PowerShell 7 host.
$hostBridgeText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_powershell_host.ps1') -Raw
if($hostBridgeText -match '\.ArgumentList\b' -or $hostBridgeText -match '\[Environment\+SpecialFolder\]' -or $hostBridgeText -match '\b[0-9]+u\b'){
  Fail-Audit 'windows_powershell_host.ps1 drifted beyond the Windows PowerShell 5.1 bootstrap bridge contract.'
}
$installStepText = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_install.ps1') -Raw
if ($installStepText -match '(?i)-Verb\s+RunAs|EncodedCommand|Administrator console') {
  Fail-Audit 'windows_step_install.ps1: current-user installation must not elevate or open an Administrator console.'
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

# Windows script architecture. Public entries use one orchestration library;
# atomic steps never invoke sibling steps or public workflows.
$versionFile = Join-Path $SourceRoot 'VERSION'
if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { Fail-Audit 'missing VERSION authority.' }
$ProductVersion = ([System.IO.File]::ReadAllText($versionFile)).Trim()
if ($ProductVersion -notmatch '^\d+\.\d+\.\d+$') { Fail-Audit "invalid VERSION value: $ProductVersion" }

$requiredWindowsScripts = @(
  'windows_entry_common.ps1','windows_pipeline.ps1','windows_native.ps1','windows_paths.ps1','windows_artifact_state.ps1',
  'windows_powershell_host.ps1','windows_powershell_host.cmd','windows_powershell_assets.txt',
  'windows_step_build.ps1','windows_step_test.ps1','windows_step_stage.ps1','windows_step_install.ps1','windows_step_release.ps1',
  'build_windows.ps1','verify_windows.ps1','install_windows.ps1','release_windows.ps1','qualify_windows.ps1'
)
foreach ($scriptName in $requiredWindowsScripts) {
  $scriptPath = Join-Path (Join-Path $SourceRoot 'scripts') $scriptName
  if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) { Fail-Audit "missing Windows pipeline script: $scriptName" }
}

$atomicNames=@('windows_step_build.ps1','windows_step_test.ps1','windows_step_stage.ps1','windows_step_install.ps1','windows_step_release.ps1')
foreach($stepName in $atomicNames){
  $stepText=Get-Content -LiteralPath (Join-Path $SourceRoot "scripts/$stepName") -Raw
  if($stepText -match '(?m)^\s*trap\s*\{' -or $stepText -match '(?m)^\s*exit\s+\d+') { Fail-Audit "${stepName}: atomic steps must fail upward, not exit/trap." }
  foreach($sibling in $atomicNames){ if($sibling -ne $stepName -and $stepText.Contains($sibling)){ Fail-Audit "${stepName}: atomic step invokes sibling $sibling" } }
  foreach($public in @('build_windows.ps1','verify_windows.ps1','install_windows.ps1','release_windows.ps1','qualify_windows.ps1')){
    $publicPattern = if($public -eq 'install_windows.ps1'){'(?<!un)install_windows\.ps1'}else{[regex]::Escape($public)}
    if($stepText -match $publicPattern){ Fail-Audit "${stepName}: atomic step invokes public workflow $public" }
  }
}

$pipelineText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_pipeline.ps1') -Raw
foreach($required in @('audit_windows.ps1','bootstrap_windows.ps1','windows_step_build.ps1','windows_step_test.ps1','windows_step_stage.ps1','windows_step_install.ps1','windows_step_release.ps1')){
  if(-not $pipelineText.Contains($required)){ Fail-Audit "windows_pipeline.ps1 missing atomic owner: $required" }
}
foreach($entryName in @('build_windows.ps1','verify_windows.ps1','install_windows.ps1','release_windows.ps1','qualify_windows.ps1')){
  $entryText=Get-Content -LiteralPath (Join-Path $SourceRoot "scripts/$entryName") -Raw
  if(-not $entryText.Contains('windows_pipeline.ps1')){ Fail-Audit "${entryName}: public workflow bypasses windows_pipeline.ps1" }
  if($entryText -match 'windows_step_(?:build|test|stage|install|release)\.ps1'){ Fail-Audit "${entryName}: public workflow embeds atomic step path instead of pipeline helper." }
}

$stepBuildText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_build.ps1') -Raw
$stepTestText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_test.ps1') -Raw
$stepStageText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_stage.ps1') -Raw
$stepInstallText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_install.ps1') -Raw
$stepReleaseText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_release.ps1') -Raw

$installEntryText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/install_windows.ps1') -Raw
$releaseEntryText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/release_windows.ps1') -Raw
foreach($entryContract in @(@('install_windows.ps1',$installEntryText),@('release_windows.ps1',$releaseEntryText))){
  if(-not $entryContract[1].Contains('Test-SuchStageManifest') -or -not $entryContract[1].Contains('Test-SuchVerificationManifest')){
    Fail-Audit ("{0}: Windows install/release must reuse valid staged/verified artifacts before rebuilding." -f $entryContract[0])
  }
}
foreach($psFile in @(Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'scripts') -Filter '*.ps1' -File)){
  if($psFile.Name -eq 'audit_windows.ps1'){continue}
  $lines=Get-Content -LiteralPath $psFile.FullName
  $codeOnly=($lines | Where-Object { -not $_.TrimStart().StartsWith('#') }) -join "`n"
  if($codeOnly.Contains('$LASTEXITCODE')){ Fail-Audit ("{0}: do not depend on process-global `$LASTEXITCODE; capture native exit status at the call boundary." -f $psFile.Name) }
}
if($stepBuildText -notmatch "'--build'" -or $stepBuildText -match "'--test-dir'|'--install'|HKCU:"){ Fail-Audit 'windows_step_build.ps1 must configure/build only.' }
if($stepTestText -notmatch "'--test-dir'" -or $stepTestText -notmatch '/inside SUCH_INSIDE_SENTINEL_918273' -or $stepTestText -match "'--build'|'--install'|HKCU:"){ Fail-Audit 'windows_step_test.ps1 must test only.' }
if($stepStageText -notmatch 'Copy-Item' -or $stepStageText -match "cmake\.exe|windows_native\.ps1|'--build'|'--test-dir'|'--install'|HKCU:"){ Fail-Audit 'windows_step_stage.ps1 must be toolchain-free.' }
if($stepInstallText -match 'bootstrap_windows|ctest\.exe|cmake\.exe|--build|--test-dir'){ Fail-Audit 'windows_step_install.ps1 must install staged artifacts only.' }
if($stepReleaseText -match 'bootstrap_windows|ctest\.exe|cmake\.exe|--build|--test-dir'){ Fail-Audit 'windows_step_release.ps1 must package staged artifacts only.' }

# All public .cmd entries delegate host selection/provisioning to one utility.
$cmdEntries=@{
  'bootstrap_windows.cmd'='bootstrap_windows_entry.ps1';
  'build_windows.cmd'='build_windows.ps1';
  'doctor_windows.cmd'='doctor_windows.ps1';
  'install_windows.cmd'='install_windows.ps1';
  'prepare_windows_offline_toolchain.cmd'='prepare_windows_offline_toolchain.ps1';
  'qualify_windows.cmd'='qualify_windows.ps1';
  'release_windows.cmd'='release_windows.ps1';
  'uninstall_windows.cmd'='uninstall_windows.ps1';
  'verify_windows.cmd'='verify_windows.ps1'
}
foreach($cmdName in $cmdEntries.Keys){
  $cmdText=Get-Content -LiteralPath (Join-Path $SourceRoot "scripts/$cmdName") -Raw
  if($cmdText -notmatch 'windows_powershell_host\.cmd' -or $cmdText -notmatch [regex]::Escape($cmdEntries[$cmdName]) -or $cmdText -match 'WindowsPowerShell\\v1\.0'){
    Fail-Audit "${cmdName}: centralized PowerShell host contract drift."
  }
  if($cmdText -notmatch 'exit /b %RC%'){Fail-Audit "${cmdName}: launcher exit propagation drift."}
}
$hostCmdText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_powershell_host.cmd') -Raw
if($hostCmdText -notmatch 'windows_powershell_assets\.txt' -or $hostCmdText -notmatch 'windows_powershell_host\.ps1' -or $hostCmdText -notmatch 'PowerShellVersion='){
  Fail-Audit 'windows_powershell_host.cmd: host provisioning contract drift.'
}

$powerShellAssetPath=Join-Path $SourceRoot 'scripts/windows_powershell_assets.txt'
$powerShellAsset=@{}
foreach($raw in @(Get-Content -LiteralPath $powerShellAssetPath)){
  $line=$raw.Trim()
  if([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')){continue}
  $split=$line.IndexOf('=')
  if($split -le 0){Fail-Audit 'windows_powershell_assets.txt: invalid metadata line.'}
  $powerShellAsset[$line.Substring(0,$split)]=$line.Substring($split+1)
}
$powerShellVersion=[string]$powerShellAsset['PowerShellVersion']
if($powerShellVersion -notmatch '^\d+\.\d+\.\d+$'){Fail-Audit 'windows_powershell_assets.txt: invalid PowerShellVersion.'}
foreach($assetSpec in @(@('X64','x64'),@('Arm64','arm64'),@('X86','x86'))){
  $label=$assetSpec[0];$arch=$assetSpec[1]
  $zipName=[string]$powerShellAsset[('PowerShell{0}Zip' -f $label)]
  $sha=[string]$powerShellAsset[('PowerShell{0}Sha256' -f $label)]
  $url=[string]$powerShellAsset[('PowerShell{0}Url' -f $label)]
  $expectedName='PowerShell-{0}-win-{1}.zip' -f $powerShellVersion,$arch
  $expectedUrl='https://github.com/PowerShell/PowerShell/releases/download/v{0}/{1}' -f $powerShellVersion,$expectedName
  if($zipName -ne $expectedName){Fail-Audit ("windows_powershell_assets.txt: asset name drift for {0}." -f $arch)}
  if($sha -notmatch '^[0-9A-Fa-f]{64}$'){Fail-Audit ("windows_powershell_assets.txt: invalid SHA-256 for {0}." -f $arch)}
  if($url -ne $expectedUrl){Fail-Audit ("windows_powershell_assets.txt: URL drift for {0}." -f $arch)}
}
$hostMaterializerText=Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_powershell_host.ps1') -Raw
foreach($token in @('Get-FileHash','Expand-Archive','Invoke-WebRequest','windows_powershell_assets.txt')){
  if(-not $hostMaterializerText.Contains($token)){Fail-Audit ("windows_powershell_host.ps1: materializer contract drift ({0})." -f $token)}
}
foreach($token in @('windows_powershell_host.cmd','windows_powershell_host.ps1','windows_powershell_assets.txt','uninstall_windows.cmd')){
  if(-not $stepInstallText.Contains($token)){Fail-Audit ("windows_step_install.ps1: PowerShell host install contract drift ({0})." -f $token)}
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
$cliEntry = Get-Content -LiteralPath (Join-Path $SourceRoot 'apps/such_cli.cpp') -Raw
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
  @{ Text = ($win + "`n" + $tests + "`n" + $cmake); Pattern = '\bsuch_v038\b'; Why = 'Active sources must not regress target names to v0.3.8.' },
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
$bootstrapScriptText = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/bootstrap_windows.ps1') -Raw
if ($bootstrapScriptText -notmatch 'Microsoft\.VisualStudio\.Workload\.VCTools' -or
    $bootstrapScriptText -notmatch 'cmake-3\.31\.12-windows-x86_64\.zip' -or
    $bootstrapScriptText -notmatch '--noWeb') {
  Fail-Audit 'Windows dependency bootstrap must preserve pinned CMake, MSVC VCTools, and offline Visual Studio layout support.'
}
if ($cmake -match '/wd4127') {
  Fail-Audit 'C4127 must be fixed in source, not globally suppressed.'
}
if ($cmake -notmatch 'WINDOWS_EXPORT_ALL_SYMBOLS\s+ON') {
  Fail-Audit 'Windows test runtime DLL must export the ABI symbols used by GetProcAddress.'
}
# Validate the generated version authority, not a literal class string in C++.
# The latter contradicted version de-hardcoding and rejected valid builds.
$versionTemplatePath=Join-Path $SourceRoot 'cmake/Version.h.in'
if(-not (Test-Path -LiteralPath $versionTemplatePath -PathType Leaf)) {
  Fail-Audit 'Generated VERSION header template is missing.'
}
$versionTemplate=Get-Content -LiteralPath $versionTemplatePath -Raw
$expectedClassTemplate='HeritageSuchV@PROJECT_VERSION_MAJOR@_@PROJECT_VERSION_MINOR@_@PROJECT_VERSION_PATCH@Window'
if(-not $versionTemplate.Contains($expectedClassTemplate) -or
    $win -notmatch 'such::version::kWindowClassName' -or
    $win -notmatch '#include\s+[<"]such/Version\.h[>"]' -or
    $cmake -notmatch 'configure_file\s*\(\s*cmake/Version\.h\.in') {
  Fail-Audit 'Win32 window class must use the generated VERSION-based authority.'
}
if($win -match 'HeritageSuchV\d+_\d+_\d+Window') {
  Fail-Audit 'Win32 window class must not be hardcoded to a release.'
}

$testScriptText = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_test.ps1') -Raw
if ($testScriptText -notmatch '/inside SUCH_INSIDE_SENTINEL_918273') {
  Fail-Audit 'Windows strict test step must exercise /inside against the production runtime.'
}
if ($win -notmatch '/drive  Choose indexed drive or folder') {
  Fail-Audit 'Windows empty search cue must expose the /drive root-selection command.'
}
if ($win -notmatch 'IFileOpenDialog' -or $win -notmatch 'FOS_PICKFOLDERS') {
  Fail-Audit '/drive must use the modern filesystem folder/drive picker.'
}
if ($win -notmatch 'WM_IME_STARTCOMPOSITION' -or $win -notmatch 'WM_IME_ENDCOMPOSITION' -or
    $win -notmatch 'gSearchImeComposing' -or $win -notmatch 'kMsgRefreshAfterIme') {
  Fail-Audit 'Win32 search input must preserve native IME composition and refresh after commit.'
}
if ($cliEntry -notmatch 'int\s+wmain\s*\(' -or $cliEntry -notmatch 'WideCharToMultiByte' -or
    $cliEntry -notmatch 'CP_UTF8') {
  Fail-Audit 'Windows SuchCLI must convert wide command-line arguments to UTF-8 before entering the public core.'
}
if ($win -notmatch 'launch_index_worker_tracked' -or $win -notmatch 'poll_index_worker' -or $win -notmatch 'reload_runtime_after_index_worker') {
  Fail-Audit 'External index workers must be tracked and the GUI runtime reloaded after completion.'
}
if ($win -notmatch 'gRuntime\.reset\(\);[\s\S]{0,1600}launch_index_worker_tracked') {
  Fail-Audit 'GUI runtime must be released before the external index worker opens persistent state.'
}

$pathsScript = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_paths.ps1') -Raw
if ($pathsScript -notmatch 'Get-SuchWindowsBuildBase' -or
    $pathsScript -notmatch 'Get-SuchWindowsToolchainRoot' -or
    $pathsScript -notmatch 'Get-SuchWindowsOfflineRoot') {
  Fail-Audit 'Windows build/toolchain/offline path ownership must remain centralized in windows_paths.ps1.'
}
$sourceBuildPattern = 'Join-Path\s+\$SourceRoot\s+''build'''
if ($pathsScript -notmatch $sourceBuildPattern) {
  Fail-Audit 'Windows generated build state must use the repository-local build directory.'
}
if ($pathsScript -match 'SUCH_WINDOWS_WORK_ROOT|ItemType\s+Junction|UserProfile[\s\S]{0,300}''SW''') {
  Fail-Audit 'Windows build orchestration must not depend on an external SW workspace or source junction.'
}
if (-not $pathsScript.Contains('LocalApplicationData') -or -not $pathsScript.Contains("'Programs\Such'")) {
  Fail-Audit 'Windows install root must resolve from LocalApplicationData to Programs\Such.'
}
if ($pathsScript -match "SetEnvironmentVariable\('Path',[^\r\n]*'Machine'\)") {
  Fail-Audit 'Such must update only the current-user PATH.'
}
$installStep = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_install.ps1') -Raw
$installRuntimePattern = 'Join-Path\s+\$installDir\s+''SuchRuntimePrivate\.dll'''
$cliRuntimePattern = 'Join-Path\s+\$cliCommandDir\s+''SuchRuntimePrivate\.dll'''
if ($installStep -notmatch $installRuntimePattern -or
    $installStep -notmatch $cliRuntimePattern) {
  Fail-Audit 'Windows install must place the private runtime beside both installed GUI/primary CLI and PATH CLI copies.'
}
$buildStep = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/windows_step_build.ps1') -Raw
if ($buildStep -notmatch 'Join-Path\s+\$bin\s+''SuchRuntimePrivate\.dll''' -or
    $buildStep -notmatch 'Assert-SuchWindowsRuntimeSha256') {
  Fail-Audit 'Windows x64 build output must mirror and hash-check the private runtime beside the binaries.'
}
if ($installStep -match '(?i)-Verb\s+RunAs|HKLM:\\') {
  Fail-Audit 'Current-user installation must not request elevation or write HKLM.'
}
if ($installStep -notmatch "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\HeritageSuch") {
  Fail-Audit 'Current-user uninstall registration must live under HKCU.'
}
if ($installStep -notmatch "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\Such.exe") {
  Fail-Audit 'Current-user App Paths registration must live under HKCU.'
}

$uninstallScriptText = Get-Content -LiteralPath (Join-Path $SourceRoot 'scripts/uninstall_windows.ps1') -Raw
if (-not $pathsScript.Contains('Get-SuchWindowsLegacyStateRoots') -or
    -not $pathsScript.Contains("Join-Path `$localData 'Such'") -or
    -not $pathsScript.Contains("Join-Path `$roamingData 'Such'") -or
    -not $uninstallScriptText.Contains('Get-SuchWindowsLegacyStateRoots')) {
  Fail-Audit 'Uninstall must remove legacy LocalAppData/RoamingAppData Such state roots.'
}

if (-not $pathsScript.Contains('Get-SuchWindowsLegacyMachineInstallRoots') -or
    -not $pathsScript.Contains("'C:\Heritage\Such'") -or
    -not $pathsScript.Contains('Get-SuchWindowsLegacyMachineShortcutPaths') -or
    -not $pathsScript.Contains('Get-SuchWindowsLegacyMachineCommandPaths') -or
    -not $pathsScript.Contains('Get-SuchWindowsLegacyMachineRegistryPaths') -or
    -not $uninstallScriptText.Contains('LegacyMachineCleanupOnly') -or
    -not $uninstallScriptText.Contains('-Verb RunAs') -or
    -not $uninstallScriptText.Contains("SetEnvironmentVariable('Path',(`$filtered -join ';'),'Machine')")) {
  Fail-Audit 'Uninstall must clean the known machine-wide v1.1.x install, shortcuts, HKLM registration, and Machine PATH entry.'
}

$runtimeClientText = Get-Content -LiteralPath (Join-Path $SourceRoot 'src/runtime/RuntimeClient.cpp') -Raw
$rootServicePath = Join-Path $SourceRoot 'src/runtime/IndexRootService.cpp'
if (-not (Test-Path -LiteralPath $rootServicePath -PathType Leaf)) {
  Fail-Audit 'IndexRootService.cpp is required for root-policy orchestration.'
}
$rootServiceText = Get-Content -LiteralPath $rootServicePath -Raw
if ($runtimeClientText -match 'plan_default_index_policy|recursive_directory_iterator') {
  Fail-Audit 'RuntimeClient must remain a raw ABI adapter; root policy belongs to IndexRootService.'
}
if ($rootServiceText -notmatch 'plan_default_index_policy_set\(policy_requests,\s*false') {
  Fail-Audit 'Root mutations must compile bounded root-set policy and must not pre-walk a drive.'
}
if ($rootServiceText -match 'recursive_directory_iterator') {
  Fail-Audit 'IndexRootService must never recursively enumerate the selected storage.'
}
if ($win -notmatch '#include\s+<such/Version\.h>') {
  Fail-Audit 'Win32 product title/version must come from generated such/Version.h.'
}

# Source-boundary checks apply only to source-owned material. VCS metadata,
# injected production runtimes, CI metadata, and generated build/toolchain state
# are intentionally outside this audit's responsibility. Traverse explicitly so
# historical in-repository VS layouts are pruned before recursion; Get-ChildItem
# -Recurse would still walk those multi-GB/deep paths even if individual results
# were ignored later.
$ignoredRoots = @('.git', '.github', '.secure', '.toolchain', 'build', 'dist')
$forbiddenNames = @('topodb', 'search.topodb')
# Permit only the packaged license/notice files; reparse points remain forbidden.
$allowedLicensePaths = @(
  'runtime\linux-x64\licenses\TopoDB-Apache-2.0-LICENSE.txt',
  'runtime\linux-x64\licenses\TopoDB-NOTICE.txt'
)
$legacyVsLayoutRelative = 'third_party\windows\offline\vs2022-buildtools'
$directoryStack = New-Object 'System.Collections.Generic.Stack[string]'
$directoryStack.Push($SourceRoot)
while ($directoryStack.Count -gt 0) {
  $auditDirectory = $directoryStack.Pop()
  foreach ($entry in @(Get-ChildItem -LiteralPath $auditDirectory -Force -ErrorAction Stop)) {
    $relative = $entry.FullName.Substring($SourceRoot.Length).TrimStart([char[]]'\/')
    if ([string]::IsNullOrWhiteSpace($relative)) { continue }
    $firstPart = ($relative -split '[\/]', 2)[0]
    if ($ignoredRoots -contains $firstPart) { continue }

    $normalizedRelative = $relative.Replace('/', '\')
    if ($normalizedRelative.Equals($legacyVsLayoutRelative, [System.StringComparison]::OrdinalIgnoreCase) -or
        $normalizedRelative.StartsWith(($legacyVsLayoutRelative + '\'), [System.StringComparison]::OrdinalIgnoreCase)) {
      continue
    }

    foreach ($name in $forbiddenNames) {
      if ($relative.IndexOf($name, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 -and
          ($entry.PSIsContainer -or $allowedLicensePaths -notcontains $normalizedRelative)) {
        Fail-Audit "private-backend material leaked into public source tree: $relative"
      }
    }
    if (($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
      Fail-Audit "reparse point/symlink is not allowed in public source tree: $relative"
    }
    if ($entry.PSIsContainer) {
      $directoryStack.Push($entry.FullName)
    }
  }
}

Write-Host 'Windows source audit PASS: scripts, current split tests, ABI loading, DLL exports, and public boundary checked.'
