# harness/

Two harnesses support the port: one to **understand** the game, one to **run** it for reference.

## 1. `ida-dump.ps1` — analysis front end

Thin wrapper around IDA Professional 9.1 (headless `idalib`) for the queries we run
constantly against a `.app`:

```powershell
./ida-dump.ps1 <path-to.app> info                 # entry points, function count
./ida-dump.ps1 <path-to.app> imports              # Symbian HLE worklist (demangled)
./ida-dump.ps1 <path-to.app> funcs [substr]       # function inventory
./ida-dump.ps1 <path-to.app> decompile <name|0xADDR>
./ida-dump.ps1 <path-to.app> disasm   <name|0xADDR>
```

Set `$env:IDA_TOOLKIT` to the folder containing `tools/analyze.py` (an idalib script
runner). Output feeds the recompiler and the per-game `HLE-IMPORTS` worklist.

## 2. `run-eka2l1.ps1` — reference oracle

Launches **[EKA2L1](https://github.com/EKA2L1/EKA2L1)** (open-source N-Gage / Symbian
emulator) on the game so we have ground truth to compare the recomp against — correct
framebuffer, timing, and HLE behavior.

```powershell
$env:EKA2L1_HOME = "D:\path\to\eka2l1"      # folder with eka2l1_qt.exe
./run-eka2l1.ps1 -Game "D:\...\sonicn-ngage\game"
```

> **Firmware note.** EKA2L1 boots Symbian, which needs **N-Gage device firmware you
> dump from your own hardware** (or install via EKA2L1's supported flow). This repo
> ships none and won't fetch any. Without firmware the launcher still opens EKA2L1 so
> you can complete device setup once, manually.

EKA2L1's real long-term value here is its **source code**: the C++ implementations of
EUSER / EFSRV / FBSCLI / window-server semantics are the spec our native HLE mirrors.
