<#
.SYNOPSIS
  Run an N-Gage game in EKA2L1 headless-ish as a REFERENCE ORACLE for the recomp.

.DESCRIPTION
  EKA2L1 is the ground truth: it boots real Symbian and runs the game, so we can
  trace the exact Symbian API / window-server / fbs call sequence and memory state
  that the recompiled build must reproduce. This script encodes the working recipe.

  One-time device install (already done in this tree):
    1. The N-Gage S60v1 firmware ships as a ready-made EKA2L1 device package
       (Data/devices.yml + Data/roms/NEM-4/SYM.ROM + Data/drives/z/NEM-4/...).
       Extract its Data/ into  <EKA2L1>/data/  (data-storage: data in config.yml).
    2. `eka2l1_qt.exe --listdevices` then shows NEM-4 (Nokia N-Gage) + RH-29 (QD).

  Per-game:
    - Install the game's `system/` tree onto BOTH the C: and E: guest drives
      (<EKA2L1>/data/drives/{c,e}/system/...). N-Gage titles run from E: but read
      assets/saves from C: (e.g. C:\SYSTEM\apps\<id>\*.bin).
    - Launch with `--runng` (auto-detects the single N-Gage game on E:).

  Instrumentation (pick any):
    - IPC/service trace: set log-ipc / log-svc true in config.yml (coarse, no args).
    - Lua hooks: drop a script in <EKA2L1>/scripts/ (auto-loaded). Use
      events.registerIpcHook('!Windowserver', opcode, when, fn) for draw calls,
      events.registerBreakpointHook('<image>.app', <rebased addr>, 0, <uid3>, fn)
      for the game's own functions, cpu.getReg/mem.readDword inside the callback.
    - GDB stub: set enable-gdb-stub true (port 24689), attach IDA's remote GDB.

.EXAMPLE
  $env:EKA2L1_HOME = "$env:RECOMP_ROOT\ngage\emu\eka2l1"
  ./run-ngage-oracle.ps1 -Game "$env:RECOMP_ROOT\ngage\snakes-ngage\game" -Seconds 50 -Trace
#>
param(
  [Parameter(Mandatory=$true)][string]$Game,
  [string]$Eka2l1Home = $env:EKA2L1_HOME,
  [int]$Seconds = 50,
  [switch]$Trace,                          # enable log-ipc / log-svc
  [string]$LogOut = "oracle.log"
)
$ErrorActionPreference = 'Stop'
if (-not $Eka2l1Home) { throw "Set -Eka2l1Home or `$env:EKA2L1_HOME (folder with eka2l1_qt.exe)" }
$exe = Join-Path $Eka2l1Home 'eka2l1_qt.exe'
if (-not (Test-Path $exe))  { throw "eka2l1_qt.exe not found in $Eka2l1Home" }
if (-not (Test-Path $Game)) { throw "game folder not found: $Game" }
$src = Join-Path $Game 'system'
if (-not (Test-Path $src))  { throw "expected '$Game\system' (the game-card tree)" }

# Install the game onto C: and E: guest drives.
foreach ($d in @('c','e')) {
  $dest = Join-Path $Eka2l1Home "data\drives\$d\system"
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  Copy-Item -Recurse -Force "$src\*" $dest
  Write-Host "installed game -> data\drives\$d\system"
}

# Optional tracing.
$cfg = Join-Path $Eka2l1Home 'config.yml'
if ($Trace -and (Test-Path $cfg)) {
  (Get-Content $cfg) `
    -replace '^log-ipc: false','log-ipc: true' `
    -replace '^log-svc: false','log-svc: true' | Set-Content $cfg -Encoding utf8
  Write-Host "tracing enabled (log-ipc / log-svc)"
}

$log = Join-Path $Eka2l1Home $LogOut
Write-Host "launching --runng (capturing $Seconds s to $log)"
$p = Start-Process -FilePath $exe -ArgumentList '--runng' -WorkingDirectory $Eka2l1Home `
       -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { $p.Kill() }
Write-Host "done. trace: $log  (filter noise: codeseg / 'Invalid ordinal')"
