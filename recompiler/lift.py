"""
lift.py — ARMv4 -> C static recompiler (first cut).

Reads the JSON produced by extract.py (functions + segment bytes), decodes each
function's ARM code with Capstone, and emits one C function per ARM function that
operates on `ngage_cpu_t` (see runtime/include/ngage_cpu.h, ngage_runtime.h).

Pure Python + Capstone — no IDA license needed at this stage.

Scope of this first cut (everything else is emitted as a loud, traceable stub):
  - data processing: MOV/MVN/ADD/SUB/RSB/AND/ORR/EOR/BIC (imm, reg, reg<<imm shift)
  - comparisons:     CMP/CMN/TST/TEQ (flag-setting)
  - the S-bit (flag-setting) on data processing
  - load/store:      LDR/STR/LDRH/STRH/LDRB/STRB with [Rn,#imm], [Rn,Rm], [pc,#imm]
  - PC-relative literal-pool loads are folded to constants from the image
  - control:         BX LR / BX Rm (return), unconditional/condition-suffixed
  - per-instruction conditional execution (EQ, NE, CS/HS, ...)

Usage:
    py -3.11 lift.py functions.json --func sub_1000BD34 [--out out.c]
    py -3.11 lift.py functions.json --addr 0x1000bd34
    py -3.11 lift.py functions.json --self-test     # lift the known target set
"""
import json, sys, argparse
from capstone import (Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB,
                      CS_MODE_LITTLE_ENDIAN, CS_OPT_DETAIL)
from capstone.arm import (ARM_OP_REG, ARM_OP_IMM, ARM_OP_MEM, ARM_REG_PC,
                          ARM_REG_LR, ARM_SFT_LSL, ARM_SFT_LSR, ARM_SFT_ASR,
                          ARM_SFT_ROR)

# Capstone ARM condition-code ids -> C boolean expression over flag accessors.
CC = {
    1:  "ngage_zf(c)",                              # EQ
    2:  "!ngage_zf(c)",                             # NE
    3:  "ngage_cf(c)",                              # HS/CS
    4:  "!ngage_cf(c)",                             # LO/CC
    5:  "ngage_nf(c)",                              # MI
    6:  "!ngage_nf(c)",                             # PL
    7:  "ngage_vf(c)",                              # VS
    8:  "!ngage_vf(c)",                             # VC
    9:  "(ngage_cf(c) && !ngage_zf(c))",            # HI
    10: "(!ngage_cf(c) || ngage_zf(c))",            # LS
    11: "(ngage_nf(c) == ngage_vf(c))",             # GE
    12: "(ngage_nf(c) != ngage_vf(c))",             # LT
    13: "(!ngage_zf(c) && ngage_nf(c) == ngage_vf(c))",  # GT
    14: "(ngage_zf(c) || ngage_nf(c) != ngage_vf(c))",   # LE
}
SHIFT_C = {ARM_SFT_LSL: "<<", ARM_SFT_LSR: ">>", ARM_SFT_ASR: ">>", ARM_SFT_ROR: None}


class Mem:
    """Sparse image: resolve a virtual address to bytes from the dumped segments."""
    def __init__(self, segments):
        self.segs = [(s["start"], s["end"], bytes.fromhex(s["bytes"])) for s in segments]

    def r32(self, va):
        for start, end, data in self.segs:
            if start <= va and va + 4 <= end:
                o = va - start
                return int.from_bytes(data[o:o + 4], "little")
        return None


class Lifter:
    def __init__(self, mem):
        self.mem = mem
        self.md = Cs(CS_ARCH_ARM, CS_MODE_ARM + CS_MODE_LITTLE_ENDIAN)
        self.md.detail = True
        self.md_t = Cs(CS_ARCH_ARM, CS_MODE_THUMB + CS_MODE_LITTLE_ENDIAN)
        self.md_t.detail = True
        self.stats = {"insns": 0, "lifted": 0, "stubbed": 0}

    # --- operand rendering -------------------------------------------------
    def reg(self, ins, rid, pc):
        if rid == ARM_REG_PC:
            return f"0x{pc + 8:#x}".replace("0x0x", "0x")  # ARM: PC reads as addr+8
        name = ins.reg_name(rid)
        n = int(name[1:]) if name and name[0] == 'r' else \
            {"sp": 13, "lr": 14, "pc": 15}.get(name, None)
        if n is None:
            return f"/*?{name}*/0"
        return f"c->r[{n}]"

    def src(self, ins, op, pc):
        """Render a data-processing source operand (reg/imm/shifted reg)."""
        if op.type == ARM_OP_IMM:
            return f"{op.imm & 0xffffffff:#x}u"
        if op.type == ARM_OP_REG:
            base = self.reg(ins, op.reg, pc)
            if op.shift.type and op.shift.value:
                opc = SHIFT_C.get(op.shift.type)
                if opc is None:                      # ROR
                    s = op.shift.value
                    return f"(({base} >> {s}) | ({base} << {32 - s}))"
                if op.shift.type == ARM_SFT_ASR:
                    return f"((uint32_t)((int32_t){base} >> {op.shift.value}))"
                return f"({base} {opc} {op.shift.value})"
            return base
        return "/*?op*/0"

    # --- instruction emit --------------------------------------------------
    def emit(self, ins):
        d = ins  # this binding exposes .operands/.cc/.update_flags/.writeback on the insn
        m = ins.mnemonic
        base = m.split('.')[0]
        cc = d.cc if d.cc in CC else None
        body = self._body(ins, base, d)
        if body is None:
            self.stats["stubbed"] += 1
            return (f'    ngage_unimplemented(c, {ins.address:#x}, '
                    f'"{m} {ins.op_str}");  /* TODO */')
        self.stats["lifted"] += 1
        line = f"    {body}  // {m} {ins.op_str}"
        if cc:
            return f"    if ({CC[cc]}) {{ {body} }}  // {m} {ins.op_str}"
        return line

    def _body(self, ins, base, d):
        ops = d.operands
        pc = ins.address
        # ---- return ----
        if base == "bx":
            if d.operands and d.operands[0].reg == ARM_REG_LR:
                return "return;"
            return "return;"  # bx <reg>: leaf model treats as return (TODO: tail dispatch)
        # ---- moves ----
        if base in ("mov", "mvn"):
            dst = self.reg(ins, ops[0].reg, pc)
            val = self.src(ins, ops[1], pc)
            expr = f"~({val})" if base == "mvn" else f"({val})"
            s = f"{dst} = {expr};"
            if d.update_flags:
                s += f" ngage_set_nz(c, {dst});"
            return s
        # ---- arithmetic with full flags ----
        if base in ("add", "sub", "rsb"):
            dst = self.reg(ins, ops[0].reg, pc)
            a = self.src(ins, ops[1], pc)
            b = self.src(ins, ops[2], pc)
            if base == "rsb":
                a, b = b, a
            if d.update_flags:
                fn = "ngage_add_flags" if base == "add" else "ngage_sub_flags"
                if base == "add":
                    return f"{dst} = {fn}(c, {a}, {b}, 0);"
                return f"{dst} = {fn}(c, {a}, {b});"
            op = "+" if base == "add" else "-"
            return f"{dst} = {a} {op} {b};"
        # ---- logical ----
        if base in ("and", "orr", "eor", "bic"):
            dst = self.reg(ins, ops[0].reg, pc)
            a = self.src(ins, ops[1], pc)
            b = self.src(ins, ops[2], pc)
            cop = {"and": "&", "orr": "|", "eor": "^", "bic": "& ~"}[base]
            s = f"{dst} = {a} {cop} ({b});" if base == "bic" else f"{dst} = {a} {cop} {b};"
            if d.update_flags:
                s += f" ngage_set_nz(c, {dst});"
            return s
        # ---- comparisons (always set flags) ----
        if base in ("cmp", "cmn", "tst", "teq"):
            a = self.src(ins, ops[0], pc)
            b = self.src(ins, ops[1], pc)
            if base == "cmp":
                return f"ngage_sub_flags(c, {a}, {b});"
            if base == "cmn":
                return f"ngage_add_flags(c, {a}, {b}, 0);"
            if base == "tst":
                return f"ngage_set_nz(c, {a} & {b});"
            return f"ngage_set_nz(c, {a} ^ {b});"
        # ---- load/store ----
        if base in ("ldr", "ldrh", "ldrb", "str", "strh", "strb"):
            return self._mem(ins, base, d, pc)
        return None

    def _mem(self, ins, base, d, pc):
        ops = d.operands
        reg_op, mem_op = ops[0], ops[1]
        mem = mem_op.mem
        # PC-relative literal load -> fold the constant from the image.
        if base == "ldr" and mem.base == ARM_REG_PC and mem.index == 0:
            addr = pc + 8 + mem.disp
            val = self.mem.r32(addr)
            if val is not None:
                dst = self.reg(ins, reg_op.reg, pc)
                return f"{dst} = {val:#x}u;  /* literal @ {addr:#x} */"
        # address expression
        b = self.reg(ins, mem.base, pc)
        addr = b
        if mem.index:
            idx = self.reg(ins, mem.index, pc)
            if getattr(mem, "lshift", 0):
                idx = f"({idx} << {mem.lshift})"
            sign = "-" if getattr(mem_op, "subtracted", False) else "+"
            addr = f"{addr} {sign} {idx}"
        if mem.disp:
            addr = f"{addr} + {mem.disp}" if mem.disp >= 0 else f"{addr} - {-mem.disp}"
        if d.writeback:
            return None  # pre/post-index writeback not handled yet -> stub
        sz = {"ldr": 32, "str": 32, "ldrh": 16, "strh": 16, "ldrb": 8, "strb": 8}[base]
        reg = self.reg(ins, reg_op.reg, pc)
        if base.startswith("ldr"):
            cast = {32: "ngage_r32", 16: "ngage_r16", 8: "ngage_r8"}[sz]
            return f"{reg} = {cast}(c, {addr});"
        wfn = {32: "ngage_w32", 16: "ngage_w16", 8: "ngage_w8"}[sz]
        cast = {32: "(uint32_t)", 16: "(uint16_t)", 8: "(uint8_t)"}[sz]
        return f"{wfn}(c, {addr}, {cast}{reg});"

    # --- whole function ----------------------------------------------------
    def lift_func(self, fn):
        code = bytes.fromhex(fn["bytes"])
        md = self.md_t if fn["thumb"] else self.md
        name = "func_%08x" % fn["start"]
        lines = [
            f"/* {fn['name']}  @ {fn['start']:#010x}  ({fn['size']} bytes, "
            f"{'Thumb' if fn['thumb'] else 'ARM'}) */",
            f"void {name}(ngage_cpu_t* c) {{",
        ]
        for ins in md.disasm(code, fn["start"]):
            self.stats["insns"] += 1
            lines.append(self.emit(ins))
        lines.append("}")
        return "\n".join(lines)


def find(funcs, addr=None, name=None):
    for f in funcs:
        if addr is not None and f["start"] == addr:
            return f
        if name is not None and f["name"].lower() == name.lower():
            return f
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json")
    ap.add_argument("--func")
    ap.add_argument("--addr")
    ap.add_argument("--out")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    data = json.load(open(args.json))
    funcs, segs = data["functions"], data["segments"]
    lifter = Lifter(Mem(segs))

    header = ('#include "ngage_cpu.h"\n#include "ngage_runtime.h"\n')
    targets = []
    if args.self_test:
        for nm in ("sub_1000BD34", "sub_1000BD48", "sub_1000BD5C", "nullsub_1"):
            f = find(funcs, name=nm)
            if f:
                targets.append(f)
    elif args.func:
        targets = [find(funcs, name=args.func)]
    elif args.addr:
        targets = [find(funcs, addr=int(args.addr, 0))]
    else:
        ap.error("need --func, --addr, or --self-test")
    if not targets or targets[0] is None:
        print("function not found", file=sys.stderr)
        return 1

    out = [header] + [lifter.lift_func(f) for f in targets]
    text = "\n\n".join(out) + "\n"
    if args.out:
        open(args.out, "w").write(text)
        print(f"wrote {args.out}", file=sys.stderr)
    else:
        print(text)
    s = lifter.stats
    print(f"// insns={s['insns']} lifted={s['lifted']} stubbed={s['stubbed']}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
