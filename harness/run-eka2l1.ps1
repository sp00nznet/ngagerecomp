<#
.SYNOPSIS
  Launch EKA2L1 (N-Gage / Symbian emulator) as a reference oracle for a game.

.DESCRIPTION
  Opens EKA2L1 pointed at the game folder so its behavior can be compared against the
  recompiled build. EKA2L1 boots Symbian and therefore needs N-Gage device firmware
  you provide yourself (dumped from your own hardware, or installed via EKA2L1's setup).
  This script does not fetch firmware.

.EXAMPLE
  $env:EKA2L1_HOME = "D:\recomp\ngage\emu\eka2l1"
  ./run-eka2l1.ps1 -Game "D:\recomp\ngage\sonicn-ngage\game"
#>
param(
  [string]$Game,
  [string]$Eka2l1Home = $env:EKA2L1_HOME
)
$ErrorActionPreference = 'Stop'

if (-not $Eka2l1Home) { throw "Set -Eka2l1Home or `$env:EKA2L1_HOME to the EKA2L1 folder (with eka2l1_qt.exe)" }
$exe = Join-Path $Eka2l1Home 'eka2l1_qt.exe'
if (-not (Test-Path $exe)) { throw "eka2l1_qt.exe not found in $Eka2l1Home" }

if ($Game -and -not (Test-Path $Game)) { throw "game folder not found: $Game" }

Write-Host "Launching EKA2L1: $exe"
if ($Game) {
  Write-Host "Game folder: $Game"
  Write-Host "First run: complete N-Gage device/firmware setup in the EKA2L1 UI, then"
  Write-Host "install/mount the game from this folder. Subsequent runs remember it."
}
# EKA2L1 is GUI-first; launch detached so this script returns.
Start-Process -FilePath $exe
Write-Host "EKA2L1 started (detached)."
