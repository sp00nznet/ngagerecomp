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
./run-eka2l1.ps1 -Game "<recomp-root>\ngage\sonicn-ngage\game"
```

> **Firmware note.** EKA2L1 boots Symbian, which needs **N-Gage device firmware you
> dump from your own hardware** (or install via EKA2L1's supported flow). This repo
> ships none and won't fetch any. Without firmware the launcher still opens EKA2L1 so
> you can complete device setup once, manually.

EKA2L1's real long-term value here is its **source code**: the C++ implementations of
EUSER / EFSRV / FBSCLI / window-server semantics are the spec our native HLE mirrors.

## 3. `run-ngage-oracle.ps1` + `oracle-trace.lua` — the live oracle (WORKING)

EKA2L1 runs the real game, so it's ground truth for the call sequence / memory state
the recomp must reproduce. Verified end-to-end: **Snakes boots and runs in EKA2L1**
(engine `6r45_1.app`, UID3 `0x101fd3db`; screen device created; clean, no panic).

**Device install (one-time, headless).** The N-Gage S60v1 firmware is a ready-made
EKA2L1 device package (`Data/devices.yml` + `Data/roms/NEM-4/SYM.ROM` +
`Data/drives/z/NEM-4/...`). Extract its `Data/` into `<EKA2L1>/data/` (config.yml
`data-storage: data`). Then `--listdevices` shows **NEM-4** (N-Gage) and **RH-29**
(N-Gage QD); `--device NEM-4` selects it.

**Per-game + run.** `run-ngage-oracle.ps1 -Game <gamedir> -Trace` installs the game's
`system/` onto the **C: and E:** guest drives (titles run from E: but read assets/saves
from C:) and launches `--runng` (auto-detects the single N-Gage game on E:).

**Instrumentation:**
- **IPC/service trace** — `log-ipc` / `log-svc` in config.yml (coarse, no args; first pass).
- **Lua hooks** (`scripts/*.lua`, auto-loaded) — `events.registerIpcHook('!Windowserver',…)`
  for draw calls (load-address independent), `events.registerBreakpointHook(image, addr, 0,
  uid3, fn)` for the game's own functions (rebase: `loadbase + (idaAddr - 0x10000000)`),
  plus `cpu.getReg`/`mem.readDword`. Template: `oracle-trace.lua`.
- **GDB stub** — `enable-gdb-stub: true` (port 24689); attach IDA's remote GDB backend
  for single-step / breakpoints against the live emulated ARM.

This de-risks bring-up: instead of guessing HLE behavior, diff against EKA2L1.
