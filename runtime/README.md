# runtime/ — NGageRuntime

Ships with every recompiled game. Two halves:

- `include/ngage_cpu.h`, `src/cpu.*` — ARM register/flag/memory model, guest→native dispatch, helper ops (barrel shifter, flag math, unaligned loads) that generated C calls.
- `src/hle/` — the Symbian High-Level Emulation layer, one source file per subsystem (`euser`, `efsrv`, `fbserv`, `wserv`, `sound`, `input`). See `../docs/SYMBIAN-HLE.md`.

The runtime exposes a stable ABI that the recompiler's emitted C targets, so the two evolve together.

> Scaffold only — see the header for the agreed CPU context shape.
