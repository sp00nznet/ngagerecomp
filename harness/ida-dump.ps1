<#
.SYNOPSIS
  Run a headless IDA (idalib) query against an N-Gage .app and print the result.

.DESCRIPTION
  Thin wrapper over an idalib script runner (tools/analyze.py). Point $env:IDA_TOOLKIT
  at the folder that contains tools/analyze.py. Requires IDA Professional with idalib
  activated and a Python that has the `idapro` package.

.EXAMPLE
  $env:IDA_TOOLKIT = "<path-to-ida-recomp-toolkit>"
  ./ida-dump.ps1 "<game-dir>\game\system\apps\sonicn\sonicn.app" imports
  ./ida-dump.ps1 "<game-dir>\sonicn.app" decompile 0x100163bc
#>
param(
  [Parameter(Mandatory, Position = 0)] [string]$App,
  [Parameter(Mandatory, Position = 1)]
  [ValidateSet('info','imports','funcs','strings','decompile','disasm')]
  [string]$Cmd,
  [Parameter(Position = 2)] [string]$Arg,
  [string]$Python = 'py'
)
$ErrorActionPreference = 'Stop'

$toolkit = $env:IDA_TOOLKIT
if (-not $toolkit) { throw "Set `$env:IDA_TOOLKIT to the folder containing tools/analyze.py" }
$analyze = Join-Path $toolkit 'tools/analyze.py'
if (-not (Test-Path $analyze)) { throw "analyze.py not found at $analyze" }
if (-not (Test-Path $App))     { throw "binary not found: $App" }

$pyArgs = @('-3.11', $analyze, $App, $Cmd)
if ($Arg) { $pyArgs += $Arg }

# analyze.py writes a progress banner to stderr; in Windows PowerShell that would
# otherwise surface as a terminating NativeCommandError. Let it flow as normal output.
$ErrorActionPreference = 'Continue'
& $Python @pyArgs 2>&1 | ForEach-Object {
  if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.ToString() } else { $_ }
}
