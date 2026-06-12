# NGageRecomp

**A static recompiler for Nokia N-Gage games — turn 2003-era Symbian ARM binaries into native, portable, modern executables.**

> The N-Gage was a taco-shaped oddity that nobody asked for and everybody remembers. A phone that played real games. Sidetalkin'. Hot-swap MMC cards you had to remove the battery to reach. It flopped commercially and then quietly turned out to be *right* — a connected handheld with downloadable titles, a decade before that was normal. This project gives its small, strange library a second life by **statically recompiling** games into code that runs natively on whatever you've got.

---

## Why the N-Gage is a *good* recomp target

Static recompilation (lift the original machine code to C, compile it natively, link it against a hand-written runtime) has had a renaissance — N64, PS1, and others. The N-Gage is, counterintuitively, one of the **friendliest** targets out there:

| Property | N-Gage | Why it helps |
|---|---|---|
| CPU | ARM925T @ 104 MHz, **ARMv4T** | Clean, fully-documented ISA. No exotic coprocessors, no microcode. |
| Graphics | **None.** 100% software rendering into a framebuffer | This is the big one. There is *no GPU to high-level-emulate* — the single hardest part of N64/PS2 recomp simply does not exist here. |
| Executable format | Symbian **E32Image** (ARMv4) | A relocatable, well-understood format with documented headers, import and relocation tables. |
| Screen | 176×208, 12-bit colour | Tiny, fixed, predictable. |

The CPU is the easy part. **The real work is Symbian.** N-Gage games run on Symbian OS 6.1 / Series 60 and call constantly into the OS — `EUSER`, the window server, the font & bitmap server, the file server. NGageRecomp's job is to lift the game's own ARM code *and* provide a **High-Level Emulation (HLE) layer** that answers those calls natively.

We don't start from scratch on the HLE: the open-source [EKA2L1](https://github.com/EKA2L1/EKA2L1) Symbian emulator is an invaluable reference (and, in places, a reusable one) for the exact API surface a game touches.

## How it works

```
   sonicn.app (ARMv4 E32Image)
            │
            ▼
   ┌──────────────────┐     reads E32Image header, code/data
   │  recompiler/     │     sections, import + relocation tables
   │  (static ARM→C)  │ ──► emits one C function per original function
   └──────────────────┘
            │  generated C
            ▼
   ┌──────────────────┐     ARM register/flag/memory semantics
   │  runtime/        │  +  Symbian HLE (EUSER, window server,
   │  (NGageRuntime)  │     fbserv, file server, sound, input)
   └──────────────────┘
            │
            ▼
     native executable  (Windows / Linux / macOS / handhelds)
```

- **`recompiler/`** — the offline tool. Parses an `E32Image`, recovers functions, and lifts each ARMv4 instruction to portable C. Architecturally modeled on the N64Recomp pipeline (per-function translation + a tightly-typed runtime context).
- **`runtime/` (NGageRuntime)** — the part that ships *with* each game. ARM CPU state (registers, CPSR flags, memory), plus the growing Symbian HLE layer. Per-game tweaks live in small TOML config files.
- **`config/`** — per-title recompiler configuration (entry points, section overrides, function hints).

## Status

🚧 **Early scaffolding.** This repo is the framework; individual game ports live in their own repos (e.g. the first target, [`sonicn-ngage`](https://github.com/sp00nznet/sonicn-ngage)). See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and [`docs/SYMBIAN-HLE.md`](docs/SYMBIAN-HLE.md) for the plan and the honest list of unknowns.

## Toolchain

- **IDA Professional 9.1** (headless idalib) — the analysis front end. Its EPOC loader
  parses the `E32Image`, recovers functions, demangles the import ordinals, and
  Hex-Rays decompiles ARM as an oracle. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §0.
- **Ghidra 12** — fallback / cross-check.
- **EKA2L1** — open-source N-Gage emulator, used as a reference oracle and the de-facto
  spec for the Symbian HLE functions. Harness in [`harness/`](harness/).

## Roadmap

- [x] Confirm IDA loads an N-Gage `.app` and recovers functions + imports *(SonicN: 2,621 funcs, 233 imports)*
- [x] IDA bridge (`extract.py`) + **ARMv4 lifter** (`lift.py`) — functions lift to C, compile under `clang -Wall`, and round-trip correctly
- [x] Control flow (branches → labels/`goto`, calls → dispatch, returns), stack ops, writeback, multiply — **99.94% of instructions, 97.8% of all 2,621 functions, zero exceptions**; verified by executing a lifted `memset` + dispatch call
- [x] NGageRuntime core: register/flag/memory model + guest→native dispatch (`runtime/`)
- [ ] Register-amount shifts / `RRX` (the 141 residual stubs)
- [ ] Symbian HLE bring-up set (process/heap, file-server reads, framebuffer present, key input) — unblocked: the firmware system DLLs give the exact ordinal→signature map
- [ ] First frame on screen from a real game
- [ ] Per-game config format + docs
- [ ] Second title → prove the framework generalizes

## Legal

NGageRecomp ships **no copyrighted game code or assets**. It is a tool. You supply your own legally-obtained game dump. Recompiled output is a derivative of *your* copy and is yours to run.

## Prior art & thanks

- [N64: Recompiled](https://github.com/N64Recomp/N64Recomp) — the static-recomp pattern this follows
- [EKA2L1](https://github.com/EKA2L1/EKA2L1) — the reference for Symbian/N-Gage HLE
- Everyone who ever sidetalked unironically. 🌮📞
