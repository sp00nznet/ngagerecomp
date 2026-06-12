"""
gen_register.py — emit the dispatch registration for all lifted functions.

The lifter names each function `func_<addr>` and routes intra-game calls through
ngage_call(addr). For those to resolve, every function must register its guest entry
address. This emits game_register.c: extern decls + ngage_game_register() that wires
them all into the dispatch table.

Usage:  py -3.11 gen_register.py functions_all.json [game_register.c]
"""
import json, sys

src = sys.argv[1] if len(sys.argv) > 1 else "functions_all.json"
out = sys.argv[2] if len(sys.argv) > 2 else "game_register.c"
fns = json.load(open(src))["functions"]

decls, regs = [], []
for f in fns:
    name = "func_%08x" % f["start"]
    decls.append(f"void {name}(ngage_cpu_t*);")
    regs.append(f"    ngage_register({f['start']:#x}u, {name});")

lines = ['#include "ngage_cpu.h"', '#include "ngage_runtime.h"', '']
lines += decls
lines += ['', 'void ngage_game_register(void) {']
lines += regs
lines += ['}']
open(out, "w").write("\n".join(lines) + "\n")
print(f"[*] {out}: registered {len(fns)} functions", file=sys.stderr)
