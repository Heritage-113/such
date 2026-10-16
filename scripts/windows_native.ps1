Set-StrictMode -Version Latest

function ConvertTo-SuchNativeArgument {
  param([AllowEmptyString()][string]$Value)
  if ($null -eq $Value) { $Value='' }
  if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }

  $builder=New-Object System.Text.StringBuilder
  [void]$builder.Append('"')
  $backslashCount=0
  foreach($character in $Value.ToCharArray()) {
    if ($character -eq '\') {
      $backslashCount++
      continue
    }
    if ($character -eq '"') {
      if ($backslashCount -gt 0) { [void]$builder.Append([char]92, ($backslashCount * 2)) }
      [void]$builder.Append([char]92)
      [void]$builder.Append('"')
      $backslashCount=0
      continue
    }
    if ($backslashCount -gt 0) {
      [void]$builder.Append([char]92, $backslashCount)
      $backslashCount=0
    }
    [void]$builder.Append($character)
  }
  if ($backslashCount -gt 0) { [void]$builder.Append([char]92, ($backslashCount * 2)) }
  [void]$builder.Append('"')
  return $builder.ToString()
}

function Invoke-SuchNative {
  param(
    [Parameter(Mandatory=$true)][string]$FilePath,
    [Parameter(Mandatory=$true)][string[]]$Arguments,
    [Parameter(Mandatory=$true)][string]$Step,
    [hashtable]$EnvironmentOverrides=@{}
  )

  if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
    throw "$Step executable not found: $FilePath"
  }

  $nativeArguments=@($Arguments | ForEach-Object { ConvertTo-SuchNativeArgument -Value $_ })
  Write-Host ("[RUN] {0} {1}" -f $FilePath, ($nativeArguments -join ' '))

  $startInfo=New-Object System.Diagnostics.ProcessStartInfo
  $startInfo.FileName=$FilePath
  $startInfo.UseShellExecute=$false
  $startInfo.Arguments=($nativeArguments -join ' ')

  # ProcessStartInfo inherits the current process environment when UseShellExecute is false.
  # Do not clear/rebuild it: Windows carries special environment variables whose names
  # may begin with '=' (for example drive-current-directory state), and reconstructing
  # the table is both unnecessary and fragile on Windows PowerShell 5.1.
  $environmentVariables=$startInfo.EnvironmentVariables
  $environmentVariables['MSBUILDDISABLENODEREUSE']='1'
  foreach($key in $EnvironmentOverrides.Keys) {
    $environmentVariables[[string]$key]=[string]$EnvironmentOverrides[$key]
  }

  $process=[System.Diagnostics.Process]::Start($startInfo)
  if ($null -eq $process) { throw "$Step could not start: $FilePath" }
  $process.WaitForExit()
  $exitCode=$process.ExitCode
  $process.Dispose()
  if ($exitCode -ne 0) { throw "$Step failed with exit $exitCode" }
}


function Invoke-SuchNativeCapture {
  param(
    [Parameter(Mandatory=$true)][string]$FilePath,
    [Parameter(Mandatory=$true)][string[]]$Arguments,
    [Parameter(Mandatory=$true)][string]$Step,
    [hashtable]$EnvironmentOverrides=@{}
  )

  if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
    throw "$Step executable not found: $FilePath"
  }

  $nativeArguments=@($Arguments | ForEach-Object { ConvertTo-SuchNativeArgument -Value $_ })
  Write-Host ("[RUN] {0} {1}" -f $FilePath, ($nativeArguments -join ' '))

  $startInfo=New-Object System.Diagnostics.ProcessStartInfo
  $startInfo.FileName=$FilePath
  $startInfo.UseShellExecute=$false
  $startInfo.RedirectStandardOutput=$true
  $startInfo.RedirectStandardError=$true
  $startInfo.CreateNoWindow=$true
  $startInfo.Arguments=($nativeArguments -join ' ')

  $environmentVariables=$startInfo.EnvironmentVariables
  $environmentVariables['MSBUILDDISABLENODEREUSE']='1'
  foreach($key in $EnvironmentOverrides.Keys) {
    $environmentVariables[[string]$key]=[string]$EnvironmentOverrides[$key]
  }

  $process=[System.Diagnostics.Process]::Start($startInfo)
  if ($null -eq $process) { throw "$Step could not start: $FilePath" }
  try {
    $stdoutTask=$process.StandardOutput.ReadToEndAsync()
    $stderrTask=$process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout=$stdoutTask.Result
    $stderr=$stderrTask.Result
    $exitCode=$process.ExitCode
  } finally {
    $process.Dispose()
  }

  if ($exitCode -ne 0) {
    $detail=if([string]::IsNullOrWhiteSpace($stderr)){''}else{"`n$stderr"}
    throw ("{0} failed with exit {1}{2}" -f $Step,$exitCode,$detail)
  }

  return [pscustomobject]@{
    ExitCode=$exitCode
    StdOut=$stdout
    StdErr=$stderr
  }
}
