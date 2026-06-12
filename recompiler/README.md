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

Control flow lifts too — intra-function branches become labels/`goto`, conditional
branches become `if (cond) goto`, calls go through the dispatch table, and `bx lr` /
`pop {pc}` become `return`. SonicN's `memset` (`sub_100E76E4`) lifts to:

```c
void func_100e76e4(ngage_cpu_t* c) {
    c->r[3] = (c->r[2]);                                   // mov r3, r2
    c->r[2] = c->r[2] - 0x1u;                              // sub r2, r2, #1
    ngage_sub_flags(c, c->r[3], 0x0u);                     // cmp r3, #0
    if ((ngage_zf(c) || ngage_nf(c) != ngage_vf(c))) { return; }   // bxle lr
L_100e76f4:;
    ngage_w8(c, c->r[0], (uint8_t)c->r[1]); c->r[0] += 1;  // strb r1, [r0], #1
    c->r[3] = (c->r[2]);                                   // mov r3, r2
    c->r[2] = c->r[2] - 0x1u;                              // sub r2, r2, #1
    ngage_sub_flags(c, c->r[3], 0x0u);                     // cmp r3, #0
    if ((!ngage_zf(c) && ngage_nf(c) == ngage_vf(c))) { goto L_100e76f4; }  // bgt loop
    return;                                                // bx lr
}
```

Both compile clean under `clang -Wall` and **execute correctly**: the memset fills
exactly N bytes, post-increments `r0`, and the `count==0`/`bxle` early-return path
works. Calls through `ngage_call()` resolve via the dispatch table
(`runtime/src/dispatch.c`). PC-relative literal loads are **folded to constants**.

## Coverage (whole binary, honest)

Measured over **all 2,621** SonicN functions / **223,868** instructions:

| | |
|---|---|
| Instructions lifted | **99.94%** (223,727 / 223,868) |
| Functions fully lifted (0 stubs) | **97.8%** (2,564) |
| Lifter exceptions | **0** |

Handled: data processing (MOV/MVN/ADD/SUB/RSB/AND/ORR/EOR/BIC, imm/reg/imm-shifted),
CMP/CMN/TST/TEQ, the S-bit, MUL/MLA/SMULL/UMULL, standalone shifts, LDR/STR/LDR{H,B}/
STR{H,B}/LDRSB/LDRSH with `[Rn,#imm]` / `[Rn,Rm]` / `[pc,#imm]` literals / pre- &
post-index writeback, PUSH/POP/LDM/STM (ia/ib/da/db), per-instruction condition codes,
and the full control-flow set (branches → labels, calls → dispatch, returns).

Anything else emits a loud `ngage_unimplemented(...)` stub, so coverage is *measured*,
not guessed. The 141 residual stubs are all **register-amount shifts**
(`orr r0, r1, r2, lsl r3`) and `RRX` — the next thing to implement.

> Generated C and `functions.json` are derived from a game image and are **gitignored** —
> bring your own dump and produce them locally.
