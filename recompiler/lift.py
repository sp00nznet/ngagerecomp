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
                      CS_MODE_LITTLE_ENDIAN, CS_GRP_JUMP, CS_GRP_CALL)
from capstone.arm import (ARM_OP_REG, ARM_OP_IMM, ARM_OP_MEM, ARM_REG_PC,
                          ARM_REG_LR, ARM_SFT_LSL, ARM_SFT_LSR, ARM_SFT_ASR,
                          ARM_SFT_ROR, ARM_SFT_LSL_REG, ARM_SFT_LSR_REG,
                          ARM_SFT_ASR_REG, ARM_SFT_ROR_REG)

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

# Two-letter ARM condition suffixes — stripped from a mnemonic to get the base op
# (capstone appends them: movne, strhne, ldreq, ...).
CC_SUFFIX = {"eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl",
             "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al"}


class Unsupported(Exception):
    """Raised by operand rendering when the first cut can't represent something."""


# ARM register name -> index (incl. ABI aliases capstone emits: ip=r12, fp=r11, ...).
_REG_NUM = {"sp": 13, "lr": 14, "pc": 15, "ip": 12, "fp": 11, "sl": 10, "sb": 9}


def regnum(name):
    if name in _REG_NUM:
        return _REG_NUM[name]
    if name and name[0] == "r":
        try:
            return int(name[1:])
        except ValueError:
            return None
    return None


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
        self._call_continue = set()   # bx-reg addrs that are calls (mov lr,pc; bx reg)
        self._jumptables = {}         # ldr-pc switch addrs -> (index reg, [targets])

    # --- operand rendering -------------------------------------------------
    def reg(self, ins, rid, pc):
        if rid == ARM_REG_PC:
            return f"{pc + 8:#x}u"          # ARM: PC reads as instruction addr + 8
        name = ins.reg_name(rid)
        n = regnum(name)
        if n is None:
            raise Unsupported()             # unknown register -> stub the instruction
        return f"c->r[{n}]"

    def src(self, ins, op, pc):
        """Render a data-processing source operand (reg/imm/shifted reg)."""
        if op.type == ARM_OP_IMM:
            return f"{op.imm & 0xffffffff:#x}u"
        if op.type == ARM_OP_REG:
            base = self.reg(ins, op.reg, pc)
            st, sv = op.shift.type, op.shift.value
            if st and sv:
                if st == ARM_SFT_LSL:
                    return f"({base} << {sv})"
                if st == ARM_SFT_LSR:
                    return f"({base} >> {sv})"
                if st == ARM_SFT_ASR:
                    return f"((uint32_t)((int32_t){base} >> {sv}))"
                if st == ARM_SFT_ROR:
                    return f"(({base} >> {sv}) | ({base} << {32 - sv}))"
                # register-amount shifts: amount is register id `sv`
                rs = {ARM_SFT_LSL_REG: "ngage_lsl", ARM_SFT_LSR_REG: "ngage_lsr",
                      ARM_SFT_ASR_REG: "ngage_asr", ARM_SFT_ROR_REG: "ngage_ror"}.get(st)
                if rs:
                    return f"{rs}({base}, {self.reg(ins, sv, pc)})"
                raise Unsupported()   # RRX (rotate through carry): rare
            return base
        raise Unsupported()

    # --- control-flow helpers ---------------------------------------------
    @staticmethod
    def _guard(cc, stmt):
        return f"if ({CC[cc]}) {{ {stmt} }}" if cc else stmt

    def _reglist(self, ins, ops):
        """[(num, 'rN')] sorted ascending by register number."""
        out = [(regnum(ins.reg_name(op.reg)), ins.reg_name(op.reg)) for op in ops]
        out.sort(key=lambda x: (x[0] is None, x[0]))
        return out

    # --- instruction emit --------------------------------------------------
    def emit(self, ins, next_addr, headset):
        self.stats["insns"] += 1
        cc = ins.cc if ins.cc in CC else None
        m = ins.mnemonic
        base = m.split('.')[0]
        cmt = f"  // {m} {ins.op_str}"

        # ---- calls (bl / blx): set lr, dispatch, fall through ----
        if ins.group(CS_GRP_CALL):
            op = ins.operands[0]
            setlr = f"c->r[14] = {next_addr:#x}u; "
            tgt = (f"{op.imm & 0xffffffff:#x}u" if op.type == ARM_OP_IMM
                   else self.reg(ins, op.reg, ins.address))
            self.stats["lifted"] += 1
            return "    " + self._guard(cc, f"{setlr}ngage_call(c, {tgt});") + cmt

        # ---- jumps / returns (b / bcc / bx) ----
        if ins.group(CS_GRP_JUMP):
            op = ins.operands[0]
            if op.type == ARM_OP_REG:                      # bx <reg>
                if op.reg == ARM_REG_LR:
                    stmt = "return;"
                elif ins.address in self._call_continue:
                    # ARMv4 indirect CALL idiom (mov lr,pc; bx reg): call, then continue
                    # to the epilogue — NOT a tail return.
                    stmt = f"ngage_call(c, {self.reg(ins, op.reg, ins.address)});"
                else:
                    stmt = f"ngage_call(c, {self.reg(ins, op.reg, ins.address)}); return;"
            else:                                          # b/bcc <imm>
                tgt = op.imm & 0xffffffff
                stmt = (f"goto L_{tgt:08x};" if tgt in headset
                        else f"ngage_call(c, {tgt:#x}u); return;")
            self.stats["lifted"] += 1
            return "    " + self._guard(cc, stmt) + cmt

        # ---- jump table (ldr pc, [pc, rN, lsl #2]) -> switch with local gotos ----
        if ins.address in self._jumptables:
            idx, targets = self._jumptables[ins.address]
            cases = " ".join(f"case {i}: goto L_{t:08x};" for i, t in enumerate(targets))
            self.stats["lifted"] += 1
            return "    " + self._guard(cc, f"switch (c->r[{idx}]) {{ {cases} }}") + cmt

        # ---- everything else ----
        # Strip the 2-char condition suffix so "movne"/"strhne" map to mov/strh,
        # then the flag-setting 's' so "subs"/"ands"/"lsrs" map to sub/and/lsr.
        if cc and len(base) > 2 and base[-2:] in CC_SUFFIX:
            base = base[:-2]
        if ins.update_flags and len(base) > 3 and base.endswith("s"):
            base = base[:-1]
        try:
            body = self._body(ins, base, ins)
        except Unsupported:
            body = None
        if body is None:
            self.stats["stubbed"] += 1
            return (f'    ngage_unimplemented(c, {ins.address:#x}, '
                    f'"{m} {ins.op_str}");  /* TODO */')
        self.stats["lifted"] += 1
        if cc:
            return "    " + self._guard(cc, body) + cmt
        return f"    {body}{cmt}"

    def _body(self, ins, base, d):
        ops = d.operands
        pc = ins.address
        # ---- stack / block transfer ----
        if base == "push":
            return self._push(ins, ops)
        if base == "pop":
            return self._pop(ins, ops)
        if base.startswith("ldm") or base.startswith("stm"):
            return self._ldm_stm(ins, base, ops)
        # ---- multiply ----
        if base == "mul":
            dst = self.reg(ins, ops[0].reg, pc)
            s = f"{dst} = {self.src(ins, ops[1], pc)} * {self.src(ins, ops[2], pc)};"
            if d.update_flags:
                s += f" ngage_set_nz(c, {dst});"
            return s
        if base == "mla":
            dst = self.reg(ins, ops[0].reg, pc)
            s = (f"{dst} = {self.src(ins, ops[1], pc)} * {self.src(ins, ops[2], pc)} "
                 f"+ {self.src(ins, ops[3], pc)};")
            if d.update_flags:
                s += f" ngage_set_nz(c, {dst});"
            return s
        if base in ("smull", "umull"):
            lo = self.reg(ins, ops[0].reg, pc)
            hi = self.reg(ins, ops[1].reg, pc)
            a, b = self.src(ins, ops[2], pc), self.src(ins, ops[3], pc)
            prod = (f"((int64_t)(int32_t){a} * (int64_t)(int32_t){b})" if base == "smull"
                    else f"((uint64_t){a} * (uint64_t){b})")
            return f"{{ uint64_t _p = (uint64_t){prod}; {lo} = (uint32_t)_p; {hi} = (uint32_t)(_p >> 32); }}"
        if base in ("smlal", "umlal"):    # {hi:lo} += a*b (64-bit signed/unsigned accumulate)
            lo = self.reg(ins, ops[0].reg, pc)
            hi = self.reg(ins, ops[1].reg, pc)
            a, b = self.src(ins, ops[2], pc), self.src(ins, ops[3], pc)
            prod = (f"((int64_t)(int32_t){a} * (int64_t)(int32_t){b})" if base == "smlal"
                    else f"((uint64_t){a} * (uint64_t){b})")
            return (f"{{ uint64_t _a = (uint64_t){lo} | ((uint64_t){hi} << 32); "
                    f"_a += (uint64_t){prod}; {lo} = (uint32_t)_a; {hi} = (uint32_t)(_a >> 32); }}")
        # ---- standalone shifts (capstone normalizes mov+shift to lsl/lsr/asr/ror) ----
        if base in ("lsl", "lsr", "asr", "ror"):
            dst = self.reg(ins, ops[0].reg, pc)
            val = self.src(ins, ops[1], pc)   # the shift is carried on ops[1].shift
            s = f"{dst} = {val};"
            if d.update_flags:
                s += f" ngage_set_nz(c, {dst});"
            return s
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
        if base in ("ldr", "ldrh", "ldrb", "ldrsb", "ldrsh", "str", "strh", "strb"):
            return self._mem(ins, base, d, pc)
        return None

    def _mem(self, ins, base, d, pc):
        ops = d.operands
        reg_op, mem_op = ops[0], ops[1]
        mem = mem_op.mem
        pc_dest = base.startswith("ldr") and reg_op.reg == ARM_REG_PC
        # PC-relative literal load -> fold the constant from the image (non-PC dest only).
        if base == "ldr" and mem.base == ARM_REG_PC and mem.index == 0 and not pc_dest:
            addr = pc + 8 + mem.disp
            val = self.mem.r32(addr)
            if val is not None:
                dst = self.reg(ins, reg_op.reg, pc)
                return f"{dst} = {val:#x}u;  /* literal @ {addr:#x} */"
        b = self.reg(ins, mem.base, pc)
        wb = d.writeback
        post = len(ops) >= 3   # post-indexed: offset is a trailing operand, access at base

        if post:
            access = b
            o3 = ops[2]
            if o3.type == ARM_OP_IMM:
                amt = o3.imm
                update = f"{b} += {amt};" if amt >= 0 else f"{b} -= {-amt};"
            else:
                idx = self.reg(ins, o3.reg, pc)
                sign = "-" if getattr(o3, "subtracted", False) else "+"
                update = f"{b} {sign}= {idx};"
        else:
            addr = b
            if mem.index:
                idx = self.reg(ins, mem.index, pc)
                # Scaled index shift is on the OPERAND (mem_op.shift), not mem.lshift.
                st, sv = mem_op.shift.type, mem_op.shift.value
                if not (st and sv) and getattr(mem, "lshift", 0):
                    st, sv = ARM_SFT_LSL, mem.lshift
                if st and sv:
                    if st == ARM_SFT_LSL:
                        idx = f"({idx} << {sv})"
                    elif st == ARM_SFT_LSR:
                        idx = f"({idx} >> {sv})"
                    elif st == ARM_SFT_ASR:
                        idx = f"((uint32_t)((int32_t){idx} >> {sv}))"
                    elif st == ARM_SFT_ROR:
                        idx = f"(({idx} >> {sv}) | ({idx} << {32 - sv}))"
                    else:
                        raise Unsupported()
                sign = "-" if getattr(mem_op, "subtracted", False) else "+"
                addr = f"{addr} {sign} {idx}"
            if mem.disp:
                addr = f"{addr} + {mem.disp}" if mem.disp >= 0 else f"{addr} - {-mem.disp}"
            access = addr
            update = f"{b} = {addr};" if wb else None   # pre-index: base <- accessed addr

        sz = {"ldr": 32, "str": 32, "ldrh": 16, "strh": 16, "ldrb": 8, "strb": 8,
              "ldrsb": 8, "ldrsh": 16}[base]
        rfn = {32: "ngage_r32", 16: "ngage_r16", 8: "ngage_r8"}[sz]
        if pc_dest:
            # ldr pc, [...] — computed/indirect jump (jump table or fn-ptr load).
            # TODO: lower constant jump tables to a C switch; for now dispatch + return.
            pre = (update + " ") if (wb and update) else ""
            return f"{pre}ngage_call(c, {rfn}(c, {access})); return;"
        reg = self.reg(ins, reg_op.reg, pc)
        if base.startswith("ldr"):
            if base in ("ldrsb", "ldrsh"):           # sign-extend to 32 bits
                sext = {8: "(int8_t)", 16: "(int16_t)"}[sz]
                stmt = f"{reg} = (uint32_t)(int32_t){sext}{rfn}(c, {access});"
            else:
                stmt = f"{reg} = {rfn}(c, {access});"
        else:
            wfn = {32: "ngage_w32", 16: "ngage_w16", 8: "ngage_w8"}[sz]
            cast = {32: "(uint32_t)", 16: "(uint16_t)", 8: "(uint8_t)"}[sz]
            stmt = f"{wfn}(c, {access}, {cast}{reg});"
        if wb and update:
            stmt = f"{stmt} {update}"   # access first, then update the base register
        return stmt

    # --- block transfer ----------------------------------------------------
    def _push(self, ins, ops):
        regs = self._reglist(ins, ops)
        n = len(regs)
        s = [f"c->r[13] -= {4 * n};"]
        for i, (num, _) in enumerate(regs):
            s.append(f"ngage_w32(c, c->r[13] + {4 * i}, c->r[{num}]);")
        return " ".join(s)

    def _pop(self, ins, ops):
        regs = self._reglist(ins, ops)
        n = len(regs)
        has_pc = any(num == 15 for num, _ in regs)
        s = []
        for i, (num, _) in enumerate(regs):
            if num == 15:
                s.append(f"/* pc <- [sp+{4 * i}] => return */")
            else:
                s.append(f"c->r[{num}] = ngage_r32(c, c->r[13] + {4 * i});")
        s.append(f"c->r[13] += {4 * n};")
        if has_pc:
            s.append("return;")
        return " ".join(s)

    def _ldm_stm(self, ins, base, ops):
        load = base.startswith("ldm")
        mode = base[3:5] if len(base) >= 5 else "ia"   # ia/ib/da/db (capstone-normalized)
        if mode not in ("ia", "ib", "da", "db"):
            mode = "ia"
        b = self.reg(ins, ops[0].reg, ins.address)
        writeback = ins.writeback
        regs = self._reglist(ins, ops[1:])
        n = len(regs)
        low = {"ia": 0, "ib": 4, "db": -4 * n, "da": -4 * n + 4}[mode]
        has_pc = any(num == 15 for num, _ in regs)
        # Snapshot the base: in real LDM/STM every element uses the ORIGINAL base, but a
        # loaded register may BE the base (e.g. `ldm r9,{r9,r10}`), which would otherwise
        # clobber it mid-sequence. The temp `_b` keeps all addresses on the original base.
        s = ["{ uint32_t _b = " + b + ";"]
        for i, (num, _) in enumerate(regs):
            off = low + 4 * i
            addr = "_b" if off == 0 else (f"_b + {off}" if off > 0 else f"_b - {-off}")
            if load:
                if num == 15:
                    s.append(f"/* pc <- [{addr}] => return */")
                else:
                    s.append(f"c->r[{num}] = ngage_r32(c, {addr});")
            else:
                s.append(f"ngage_w32(c, {addr}, c->r[{num}]);")
        if writeback:
            s.append(f"{b} = _b {'+' if low >= 0 else '-'} {4 * n};")
        s.append("}")
        if load and has_pc:
            s.append("return;")
        return " ".join(s)

    # --- whole function ----------------------------------------------------
    def lift_func(self, fn):
        code = bytes.fromhex(fn["bytes"])
        start = fn["start"]
        md = self.md_t if fn["thumb"] else self.md
        # IDA's code instruction heads — so we decode only code, skipping any
        # embedded literal-pool / data words.
        heads = fn.get("heads") or [start + o for o in range(0, len(code), 4)]

        # Decode exactly one instruction at each in-range head. A function can have
        # non-contiguous chunks; heads outside our byte window aren't ours to lift.
        decoded = []  # (addr, ins)
        for h in heads:
            off = h - start
            if off < 0 or off >= len(code):
                continue
            ins = next(md.disasm(code[off:off + 4], h), None)
            if ins is not None:
                decoded.append((h, ins))
        # Only addresses we actually decoded can be local goto targets; anything else
        # (other chunks, other functions) routes through the dispatch table.
        headset = {addr for addr, _ in decoded}

        # Detect the ARMv4 indirect-CALL idiom `mov lr, pc; bx/blx reg`: such a `bx reg`
        # is a call that continues, not a tail return. Works for conditional forms too
        # (e.g. `bxne`), so the base mnemonic is taken with the cc suffix stripped.
        def base_mnem(ins):
            m = ins.mnemonic.split('.')[0]
            if ins.cc in CC and len(m) > 2 and m[-2:] in CC_SUFFIX:
                m = m[:-2]
            return m
        self._call_continue = set()
        for i in range(len(decoded) - 1):
            a = decoded[i][1]
            if (base_mnem(a) == "mov" and len(a.operands) == 2
                    and a.operands[0].type == ARM_OP_REG and a.operands[1].type == ARM_OP_REG
                    and regnum(a.reg_name(a.operands[0].reg)) == 14   # lr
                    and a.operands[1].reg == ARM_REG_PC):
                b = decoded[i + 1][1]
                if (base_mnem(b) in ("bx", "blx")
                        and b.operands and b.operands[0].type == ARM_OP_REG):
                    self._call_continue.add(b.address)

        # Pass 1: intra-function branch targets become labels; lower jump tables.
        labels = set()
        self._jumptables = {}        # insn addr -> (index reg num, [local target addrs])
        for addr, ins in decoded:
            if ins.group(CS_GRP_JUMP) and not ins.group(CS_GRP_CALL):
                op = ins.operands[0]
                if op.type == ARM_OP_IMM and (op.imm & 0xffffffff) in headset:
                    labels.add(op.imm & 0xffffffff)
                continue
            # `ldr pc, [pc, rN, lsl #2]` — a switch (mnemonic may carry a cc, e.g. ldrls).
            # Read the constant table of word targets that follows; each becomes a goto.
            lm = ins.mnemonic.split('.')[0]
            if ins.cc in CC and len(lm) > 3 and lm[-2:] in CC_SUFFIX:
                lm = lm[:-2]
            if lm == "ldr" and len(ins.operands) >= 2:
                d, mo = ins.operands[0], ins.operands[1]
                if (d.type == ARM_OP_REG and d.reg == ARM_REG_PC
                        and mo.type == ARM_OP_MEM and mo.mem.base == ARM_REG_PC and mo.mem.index):
                    tbl = addr + 8 + mo.mem.disp
                    idx = regnum(ins.reg_name(mo.mem.index))
                    targets = []
                    for k in range(256):
                        t = self.mem.r32(tbl + k * 4)
                        if t is None or t not in headset:
                            break
                        targets.append(t)
                    if idx is not None and targets:
                        self._jumptables[addr] = (idx, targets)
                        labels.update(targets)

        # Pass 2: emit.
        name = "func_%08x" % start
        lines = [
            f"/* {fn['name']}  @ {start:#010x}  ({fn['size']} bytes, "
            f"{'Thumb' if fn['thumb'] else 'ARM'}) */",
            f"void {name}(ngage_cpu_t* c) {{",
        ]
        for i, (addr, ins) in enumerate(decoded):
            if addr in labels:
                lines.append(f"L_{addr:08x}:;")
            next_addr = decoded[i + 1][0] if i + 1 < len(decoded) else fn["end"]
            lines.append(self.emit(ins, next_addr, headset))
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
