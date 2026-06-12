# Architecture

NGageRecomp follows the proven **static recompilation** shape: do the hard analysis *offline*, emit plain C, and link it against a small native runtime. Nothing is interpreted at runtime — the game's ARM code becomes the program.

## The pipeline

### 0. Tooling — IDA does the analysis front end

We don't hand-roll an `E32Image` parser or a function-recovery pass. **IDA
Professional 9.1** has a built-in EPOC/E32Image loader; run headlessly (idalib) it
gives us, for free:

- the parsed image (sections, relocations, entry points),
- **function boundaries** (2,621 for SonicN),
- the **import table with ordinals demangled** to Symbian SDK signatures, and
- **Hex-Rays decompilation** of any ARM function — a ready-made oracle to check the
  lift against.

So the recompiler's job narrows to: take IDA's function list + bytes, **lift each
function's ARM to C**, and link against the runtime. IDA is the front end; we own the
lifter and the runtime. (Ghidra 12 is a fallback / cross-check.)

For reference behavior we keep **EKA2L1** (open-source Symbian/N-Gage emulator) on
hand as both a runtime oracle and the de-facto spec for the HLE functions.

### 1. Front end — `E32Image` parsing (`recompiler/`)

> Superseded for v1 by IDA (§0) — kept as the format reference. The header we care
about (EPOC release 6 / EKA1, which the original N-Gage uses):

| Field | SonicN value | Meaning |
|---|---|---|
| `iUid1` | `0x10000079` | `KDynamicLibraryUid` — a Series 60 `.app` is a polymorphic DLL |
| `iUid2` | `0x100039CE` | `KUidApp` |
| `iUid3` | `0x101FB882` | the application's own UID |
| `iSignature` | `'EPOC'` | image magic |
| `iCpu` | `0x2000` | `ECpuArmV4` |

From the header we recover: the **code section**, the **const/data section**, the **import table** (which ordinals from which DLLs the game needs), the **relocation tables** (code + data), and the **entry point**. Compressed images (deflate / the `BytePair` scheme) must be inflated first; SonicN's `.app` is uncompressed, which is why it's the first target.

### 2. Lifter — ARMv4T → C (`recompiler/`)

Each recovered function is translated instruction-by-instruction into a C function operating on an explicit CPU context:

```c
typedef struct {
    uint32_t r[16];     // r0–r15 (r13=sp, r14=lr, r15=pc)
    uint32_t cpsr;      // N Z C V flags + mode bits we care about
    uint8_t* mem;       // flat guest address space
} ngage_cpu_t;
```

ARM specifics the lifter must respect:
- **Conditional execution** on (almost) every instruction (`EQ`, `NE`, `CS`, …).
- The **barrel shifter** as an operand modifier (`LSL/LSR/ASR/ROR/RRX`).
- **Flag-setting `S` variants** — only model flags that downstream code reads.
- **Thumb** (ARMv4**T**): SonicN may contain Thumb regions; the lifter tracks ARM/Thumb state per region.
- **Indirect branches** (`BX`, `LDR pc, …`, jump tables) — resolved via a guest→native address lookup in the runtime.

The translation unit granularity is one C function per recovered function, matching the N64Recomp approach, which keeps generated files reviewable and lets us hand-patch individual functions when the lifter can't prove something.

### 3. Back end — NGageRuntime (`runtime/`)

Ships with every recompiled game. Two halves:

- **CPU support**: register/flag/memory model, the guest→native dispatch table, and helpers the generated C calls (shifts, flag math, unaligned access).
- **Symbian HLE**: every import the game resolves against a system DLL becomes a native function. This is where the bulk of the long-tail work lives — see [`SYMBIAN-HLE.md`](SYMBIAN-HLE.md).

## Why not just interpret / JIT (i.e. emulate)?

EKA2L1 already does excellent *dynamic* emulation. NGageRecomp is deliberately different: **ahead-of-time** translation produces a standalone native binary per game, with no interpreter loop, which opens the door to per-game fixes, mods, widescreen/hi-res hacks, and trivial portability to platforms where shipping a full emulator is awkward. The trade is up-front engineering per title — exactly the trade N64Recomp made.

## Open questions (being honest)

- How much **Thumb** is in the SonicN `.app`, and how cleanly do ARM/Thumb regions separate?
- Function recovery without symbols: how far does linear-sweep + the reloc/import tables get us before we need heuristics?
- HLE depth: SonicN is a near-direct port of *Sonic Advance* — how much does it lean on Symbian beyond framebuffer + input + file I/O + sound?
- Self-modifying code / overlays: present? (Probably not for this title, but must be checked.)
