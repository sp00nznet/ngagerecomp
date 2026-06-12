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
- **`gen_register.py`** — registers all lifted functions at their guest addresses so the
  game can call itself (`game_register.c`).
- **`extract_imports.py` + `gen_hle.py`** — dump the import table and wire each slot to an
  HLE shim or a named stub (`imports.json` → `hle_generated.c`).
- **`gen_image.py`** — pack the image's data segments (vtables/const pools/jump tables)
  into `segments.bin` for the runtime image loader, so guest data reads resolve.

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
| Instructions lifted | **100.0%** (0 stubs) |
| Functions fully lifted | **100%** (2,621) |
| Whole corpus (247k lines) compiles `clang -Wall` | **clean** (0 errors, 0 warnings) |

Handled: data processing (MOV/MVN/ADD/SUB/RSB/AND/ORR/EOR/BIC, imm/reg/imm-shifted/
reg-shifted), CMP/CMN/TST/TEQ, the S-bit, MUL/MLA/SMULL/UMULL, standalone shifts,
LDR/STR/LDR{H,B}/STR{H,B}/LDRSB/LDRSH with `[Rn,#imm]` / `[Rn,Rm]` / `[pc,#imm]`
literals / pre- & post-index writeback, PUSH/POP/LDM/STM (ia/ib/da/db), the ABI register
aliases (ip/fp/sl/sb), per-instruction condition codes, and the full control-flow set
(branches → labels, calls → dispatch, returns).

> **Compiling the whole corpus is the real gate** — the coverage % counts emitted
> instructions, but only `clang -Wall` proves the C is *valid*. Two bugs the % missed
> and the compile caught: unmapped `ip`/`fp` aliases, and `goto` into non-contiguous
> function chunks. Both fixed.

**Known simplification:** `ldr pc, [pc, rN, lsl #2]` jump tables currently lower to a
runtime `ngage_call(...)` dispatch rather than a static C `switch`. They compile and are
honest (an unresolved target logs, never crashes silently), but switch lowering from the
constant jump-table data is a TODO before those code paths run for real.

> Generated C and `functions.json` are derived from a game image and are **gitignored** —
> bring your own dump and produce them locally.

## Correctness notes

Running real game code is the lifter's true test — it surfaces bugs the per-instruction
compile can't. Three found and fixed this way, each affecting *any* game:

1. **`LDM` clobbering its own base** (`ldm r9,{r9,r10}`): loading `r9` first wrecks the
   base before later element addresses. Fix: snapshot the base into a temp `_b` and address
   every element off it.
2. **Scaled index dropped** (`ldr r0,[r6,r5,lsl #2]`): Capstone reports the index shift on
   the *operand* (`op.shift`), not `mem.lshift` — reading the wrong field silently produced
   `r6 + r5` instead of `r6 + r5*4`. Affects all array indexing.
3. **ARMv4 indirect call mis-lifted as a tail return** (`mov lr, pc; bx ip`, incl. the
   conditional `bxne` form): `bx reg` was emitted as `…; return;`, skipping the epilogue
   that restores `r4`–`r11`. Fix: a `bx reg` preceded by `mov lr, pc` is call-and-continue.
4. **Jump tables** (`ldr pc, [pc, rN, lsl #2]`, incl. `ldrls`): these switch dispatches were
   routed through `ngage_call` (wrong — the targets are *local* labels) with a stray
   `return`. Now lowered properly: the constant target table is read from the image and
   emitted as a C `switch` with local `goto`s. 85 tables across SonicN.

Two debug builds find these fast (see `docs/SYMBIAN-HLE.md`):
- `-DNGAGE_MEM_GUARD` — wild guest accesses report their address + call stack.
- `-DNGAGE_ABI_CHECK` — flags any function that returns with `r4`–`r11`/`sp` altered (how
  bug #3 was pinpointed to the exact function).
