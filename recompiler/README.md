# recompiler/

The offline static recompiler: `E32Image` in, native C out.

## Planned structure

- `src/e32image.*` — parse the Symbian `E32Image` header, UIDs, code/data sections, import table, relocation tables. Inflate compressed images.
- `src/arm/` — ARMv4T decoder + lifter (one C function emitted per recovered function).
- `src/recover.*` — function discovery (reloc/import-driven + linear sweep, with per-game hints).
- `src/emit.*` — C emitter and the generated-file layout.
- `src/main.*` — CLI: `ngagerecomp <game.app> <config.toml> -o out/`

> Nothing here is implemented yet — this is the agreed structure so generated code and the runtime ABI stay in lockstep. See `../docs/ARCHITECTURE.md`.
