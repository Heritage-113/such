Set-StrictMode -Version Latest

function Invoke-SuchPipelineStep {
  param(
    [Parameter(Mandatory=$true)][string]$Name,
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [hashtable]$Arguments=@{}
  )
  if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) {
    throw "[$Name] script is missing: $ScriptPath"
  }
  Write-Host ("[SUCH:{0}] BEGIN" -f $Name.ToUpperInvariant()) -ForegroundColor Cyan
  try {
    & $ScriptPath @Arguments
  } catch {
    throw ("[{0}] {1}" -f $Name,$_.Exception.Message)
  }
  Write-Host ("[SUCH:{0}] PASS" -f $Name.ToUpperInvariant()) -ForegroundColor Green
}

function Stop-SuchPipelineWithError {
  param(
    [Parameter(Mandatory=$true)][string]$Entry,
    [Parameter(Mandatory=$true)]$ErrorRecord
  )
  Write-Host ''
  Write-Host ("[SUCH:FAIL][{0}] {1}" -f $Entry,$ErrorRecord.Exception.Message) -ForegroundColor Red
  if ($ErrorRecord.InvocationInfo -and $ErrorRecord.InvocationInfo.PositionMessage) {
    Write-Host $ErrorRecord.InvocationInfo.PositionMessage -ForegroundColor DarkRed
  }
  if (-not [string]::IsNullOrWhiteSpace($ErrorRecord.ScriptStackTrace)) {
    Write-Host $ErrorRecord.ScriptStackTrace -ForegroundColor DarkRed
  }
}
