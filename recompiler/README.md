# recompiler/

`E32Image` → C. IDA recovers the functions; this stage lifts their ARM to C.

## Pipeline

```
sonicn.app ──(extract.py / IDA idalib)──▶ functions.json ──(lift.py / Capstone)──▶ generated C
                 functions + segment bytes                      one C fn per ARM fn
```

- **`extract.py`** — IDA bridge (needs an IDA license). Opens the `.app` via IDA's
  EPOC loader and dumps each function's address, size, ARM/Thumb mode, raw bytes, and a
  leaf flag, plus the loaded segment bytes (so the lifter can resolve PC-relative
  literal-pool reads). Output: `functions.json`.
  ```
  py -3.11 extract.py <app> functions.json [--max-size N]
  ```
- **`lift.py`** — the lifter (pure Python + Capstone, **no IDA license needed**). Decodes
  ARM and emits C against `ngage_cpu_t` (see `../runtime/include/`).
  ```
  py -3.11 lift.py functions.json --func sub_1000BD34
  py -3.11 lift.py functions.json --addr 0x1000bd34 --out generated.c
  py -3.11 lift.py functions.json --self-test          # lift a known leaf set
  ```

## What it emits

A leaf function lifts to plain, readable C. Real output for SonicN's `sub_1000BD34`:

```c
void func_1000bd34(ngage_cpu_t* c) {
    c->r[2] = 0x29au;  /* literal @ 0x1000bd44 */          // ldr r2, [pc, #8]
    c->r[3] = (0x4u);                                      // mov r3, #4
    ngage_w16(c, c->r[0] + c->r[2], (uint16_t)c->r[3]);    // strh r3, [r0, r2]
    return;                                                // bx lr
}
```

This compiles clean under `clang -Wall` and round-trips correctly (the store lands at
`r0 + 0x29a`). PC-relative literal loads are **folded to constants** from the image.

## First-cut coverage (honest)

Measured over 1,270 SonicN functions ≤200 bytes:

| | |
|---|---|
| Instructions lifted | **70.2%** (10,463 / 14,904) |
| Functions fully lifted (0 stubs) | **27%** (341) |

Handled now: data processing (MOV/MVN/ADD/SUB/RSB/AND/ORR/EOR/BIC, imm/reg/shifted),
CMP/CMN/TST/TEQ, the S-bit, LDR/STR/LDRH/STRH/LDRB/STRB (`[Rn,#imm]`, `[Rn,Rm]`,
`[pc,#imm]` literals), per-instruction condition codes, and `BX` return.

Anything else emits a loud `ngage_unimplemented(...)` stub, so coverage is *measured*,
not guessed. Next up, in priority order (these are the actual top stubbed mnemonics):

1. **Control flow** — `b` / `bl` / `beq`… : basic-block splitting, intra-function
   branches as labels/`goto`, and calls via the guest→native dispatch table.
2. **Stack** — `push` / `pop` / `ldm` / `stm`.
3. Standalone `lsl` / `lsr` / `asr`, and skipping literal-pool data IDA already marks
   (those show up as bogus `andeq` decodes today).

> Generated C and `functions.json` are derived from a game image and are **gitignored** —
> bring your own dump and produce them locally.
