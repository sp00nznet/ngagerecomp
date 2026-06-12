"""
gen_hle.py — generate the HLE wiring for a game from its import table.

Reads imports.json (from extract_imports.py) and emits hle_generated.c:
  - ngage_hle_init(c): for every import slot, write a self-pointer into guest memory
    and register the slot address with the dispatch table.
  - implemented imports -> their hand-written shim (runtime/src/hle/*.c)
  - everything else    -> a named stub that logs the exact Symbian function when hit,
    so an unimplemented call says WHAT is missing, not just an address.

Usage:  py -3.11 gen_hle.py imports.json [hle_generated.c]
"""
import json, sys, re

# Stripped import name -> implemented shim. Grow this as shims land in runtime/src/hle/.
IMPLEMENTED = {
    "memcpy": "hle_memcpy",
    "memset": "hle_memset",
    "FillZ__3MemPvi": "hle_Mem_FillZ",
}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "imports.json"
    out = sys.argv[2] if len(sys.argv) > 2 else "hle_generated.c"
    imps = json.load(open(src))

    lines = ['#include "ngage_cpu.h"', '#include "ngage_runtime.h"',
             '#include "ngage_hle.h"', '']
    stubs, regs, n_impl = [], [], 0
    for i, imp in enumerate(imps):
        name = imp["name"]
        key = name[6:] if name.startswith("__imp_") else name   # strip __imp_
        slot = imp["slot"]
        shim = IMPLEMENTED.get(key)
        if shim:
            n_impl += 1
        else:
            fn = f"hle_stub_{i}"
            label = re.sub(r'["\\]', "", f'{imp["dll"]}:{name}')
            stubs.append(f'static void {fn}(ngage_cpu_t* c){{ '
                         f'ngage_unimplemented(c, {slot:#x}u, "{label}"); }}')
            shim = fn
        regs.append((slot, shim))

    lines += stubs
    lines += ['', '/* Point each import slot at itself and route it to a shim. */',
              'void ngage_hle_init(ngage_cpu_t* c) {']
    for slot, shim in regs:
        lines.append(f'    ngage_w32(c, {slot:#x}u, {slot:#x}u); '
                     f'ngage_register({slot:#x}u, {shim});')
    lines.append('}')

    open(out, "w").write("\n".join(lines) + "\n")
    print(f"[*] {out}: {len(imps)} imports  ({n_impl} implemented, {len(stubs)} stubbed)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
