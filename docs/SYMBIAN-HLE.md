# Symbian HLE

The CPU is the easy part. **Symbian is the project.** An N-Gage game is a Series 60 application running on Symbian OS 6.1; it doesn't touch hardware directly — it talks to OS servers over Symbian's client/server IPC and links against system DLLs by **ordinal**.

To run a recompiled game natively, every import it resolves must be answered by a native implementation. This document tracks that surface.

## How imports work in an `E32Image`

The import table lists, per dependency DLL, the **ordinals** the game needs. There are no names in the executable — ordinal N of `EUSER.DLL` is whatever the matching SDK's `.def` file says it is. So the HLE effort is:

1. Read the import table → list of `(DLL, ordinal)` pairs the game actually uses.
2. Map each ordinal back to its Symbian SDK function signature (via the Series 60 v1 SDK `.def` files).
3. Implement that function natively (or stub it, loudly, until something needs it).

This is tractable precisely because we only have to implement **what a given game imports**, not all of Symbian.

## The likely bring-up set for SonicN

A 2D action-platformer port realistically needs a small core:

| Subsystem | Symbian DLL(s) | What the game wants |
|---|---|---|
| Process / heap / descriptors | `EUSER` | `RHeap` alloc, `TDes`/`TBuf` string ops, cleanup stack, active scheduler |
| File I/O | `EFSRV` | open & read the `*.bin` asset files (`action_char*.bin`, `etcdata.bin`, `snd*.bin`) |
| Bitmaps | `FBSCLI` (font/bitmap server) | decode `images.mbm` / `volume.mbm` (Symbian Multi-BitMap) |
| Screen | window server (`WS32`) / direct screen access | get a framebuffer and present it |
| Input | window server | N-Gage D-pad + keys (`5`=action, soft keys) |
| Sound | N-Gage / Series 60 audio | play `snd*.bin` streams |

The honest expectation: input + file I/O + framebuffer present is enough to get a **first frame and a controllable Sonic**; sound and the long tail of `EUSER` come after.

## Strategy

- **Import-driven, demand-paged.** Implement an ordinal the first time the game faults on its stub. Every stub logs `UNIMPLEMENTED EUSER ordinal 123` so progress is measured by what's left, not guessed.
- **Lean on EKA2L1.** Its HLE has already mapped huge chunks of this surface; we mirror semantics rather than rediscover them.
- **MBM and asset formats are documented** (Symbian Multi-BitMap), so the asset decoders are normal data-format work, not reverse engineering.

## Tracking

| Status | Meaning |
|---|---|
| ⬜ not started | |
| 🟨 stubbed | returns a safe default, logs on call |
| 🟩 implemented | real behavior, exercised by the game |

(Per-ordinal table will be filled in once the SonicN import table is dumped — see the game repo.)
