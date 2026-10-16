Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'windows_entry_common.ps1')

function Invoke-SuchWindowsAuditStep {
  param([switch]$SkipAudit)
  if ($SkipAudit.IsPresent) { return }
  $root=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
  Invoke-SuchPipelineStep -Name 'audit' -ScriptPath (Join-Path $PSScriptRoot 'audit_windows.ps1') -Arguments @{SourceRoot=$root}
}

function Invoke-SuchWindowsBootstrapStep {
  param([Parameter(Mandatory=$true)][string]$Arch,[switch]$OfflineOnly)
  $args=@{Arch=$Arch}
  if($OfflineOnly.IsPresent){$args.OfflineOnly=$true}
  Invoke-SuchPipelineStep -Name 'bootstrap' -ScriptPath (Join-Path $PSScriptRoot 'bootstrap_windows.ps1') -Arguments $args
}

function Invoke-SuchWindowsBuildStep {
  param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$Arch,
    [Parameter(Mandatory=$true)][string]$Generator,
    [Parameter(Mandatory=$true)][int]$Jobs,
    [string]$RuntimeLibraryPath='',
    [switch]$RequireRuntime,
    [switch]$Clean
  )
  $args=@{Config=$Config;Arch=$Arch;Generator=$Generator;Jobs=$Jobs}
  if(-not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)){$args.RuntimeLibraryPath=$RuntimeLibraryPath}
  if($RequireRuntime.IsPresent){$args.RequireRuntime=$true}
  if($Clean.IsPresent){$args.Clean=$true}
  Invoke-SuchPipelineStep -Name 'build' -ScriptPath (Join-Path $PSScriptRoot 'windows_step_build.ps1') -Arguments $args
}

function Invoke-SuchWindowsTestStep {
  param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$Arch,
    [string]$RuntimeLibraryPath='',
    [switch]$RequireRuntime
  )
  $args=@{Config=$Config;Arch=$Arch}
  if($RequireRuntime.IsPresent){$args.RequireRuntime=$true}
  if(-not [string]::IsNullOrWhiteSpace($RuntimeLibraryPath)){$args.RuntimeLibraryPath=$RuntimeLibraryPath}
  Invoke-SuchPipelineStep -Name 'test' -ScriptPath (Join-Path $PSScriptRoot 'windows_step_test.ps1') -Arguments $args
}

function Invoke-SuchWindowsVerifiedBuild {
  param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$Arch,
    [Parameter(Mandatory=$true)][string]$Generator,
    [Parameter(Mandatory=$true)][int]$Jobs,
    [string]$RuntimeLibraryPath='',
    [switch]$RequireRuntime,
    [switch]$OfflineOnly,
    [switch]$Clean,
    [switch]$SkipAudit
  )
  Invoke-SuchWindowsAuditStep -SkipAudit:$SkipAudit
  Invoke-SuchWindowsBootstrapStep -Arch $Arch -OfflineOnly:$OfflineOnly
  Invoke-SuchWindowsBuildStep -Config $Config -Arch $Arch -Generator $Generator -Jobs $Jobs -RuntimeLibraryPath $RuntimeLibraryPath -RequireRuntime:$RequireRuntime -Clean:$Clean
  Invoke-SuchWindowsTestStep -Config $Config -Arch $Arch -RuntimeLibraryPath $RuntimeLibraryPath -RequireRuntime:$RequireRuntime
}

function Invoke-SuchWindowsStageStep {
  param([Parameter(Mandatory=$true)][string]$Config,[Parameter(Mandatory=$true)][string]$Arch,[switch]$RequireRuntime)
  $args=@{Config=$Config;Arch=$Arch}
  if($RequireRuntime.IsPresent){$args.RequireRuntime=$true}
  Invoke-SuchPipelineStep -Name 'stage' -ScriptPath (Join-Path $PSScriptRoot 'windows_step_stage.ps1') -Arguments $args
}

function Invoke-SuchWindowsInstallStep {
  param(
    [Parameter(Mandatory=$true)][string]$Config,
    [Parameter(Mandatory=$true)][string]$Arch,
    [switch]$DesktopShortcut,
    [switch]$SkipFonts,
    [switch]$RequireFonts,
    [switch]$RequireRuntime,
    [string]$FontSourceDir=''
  )
  $args=@{Config=$Config;Arch=$Arch}
  if($DesktopShortcut.IsPresent){$args.DesktopShortcut=$true}
  if($SkipFonts.IsPresent){$args.SkipFonts=$true}
  if($RequireFonts.IsPresent){$args.RequireFonts=$true}
  if($RequireRuntime.IsPresent){$args.RequireRuntime=$true}
  if(-not [string]::IsNullOrWhiteSpace($FontSourceDir)){$args.FontSourceDir=$FontSourceDir}
  Invoke-SuchPipelineStep -Name 'install' -ScriptPath (Join-Path $PSScriptRoot 'windows_step_install.ps1') -Arguments $args
}

function Invoke-SuchWindowsReleaseStep {
  param([Parameter(Mandatory=$true)][string]$Config,[Parameter(Mandatory=$true)][string]$Arch)
  Invoke-SuchPipelineStep -Name 'release' -ScriptPath (Join-Path $PSScriptRoot 'windows_step_release.ps1') -Arguments @{Config=$Config;Arch=$Arch}
}
