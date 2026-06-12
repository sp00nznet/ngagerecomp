"""
extract.py — IDA (idalib) bridge for NGageRecomp.

Opens an N-Gage .app headlessly via IDA's EPOC loader and dumps each function's
address, size, ARM/Thumb mode, raw bytes, and a leaf flag (no BL/BLX calls out)
to JSON. That JSON is the input to the pure-Python lifter (lift.py), which needs
no IDA license to run.

Usage:
    py -3.11 extract.py <path-to.app> [out.json] [--max-size N]

Requires: IDA Professional with idalib activated, and a Python with `idapro`.
`import idapro` MUST be first.
"""
import idapro  # noqa: E402  (must be first)
import sys, json
import ida_funcs, ida_bytes, idautils, idc, ida_segment


def is_thumb(ea: int) -> bool:
    # The 'T' segment register is 1 in Thumb regions, 0 in ARM.
    t = idc.get_sreg(ea, "T")
    return t == 1


def func_calls_out(start: int, end: int) -> int:
    """Count call instructions (BL/BLX) in [start, end)."""
    n = 0
    ea = start
    while ea < end and ea != idc.BADADDR:
        mnem = idc.print_insn_mnem(ea).upper()
        if mnem.startswith("BL"):  # BL, BLX
            n += 1
        ea = idc.next_head(ea, end)
    return n


def main():
    if len(sys.argv) < 2:
        print("usage: extract.py <app> [out.json] [--max-size N]", file=sys.stderr)
        return 2
    app = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else "functions.json"
    max_size = None
    if "--max-size" in sys.argv:
        max_size = int(sys.argv[sys.argv.index("--max-size") + 1])

    print(f"[*] Opening {app} (headless)...", file=sys.stderr)
    idapro.open_database(app, run_auto_analysis=True)

    funcs = []
    for ea in idautils.Functions():
        f = ida_funcs.get_func(ea)
        if not f:
            continue
        size = f.end_ea - f.start_ea
        if max_size and size > max_size:
            continue
        b = ida_bytes.get_bytes(f.start_ea, size) or b""
        # Real instruction heads (code only) so the lifter can skip embedded
        # literal-pool / data words instead of mis-decoding them as code.
        heads = [h for h in idautils.FuncItems(f.start_ea)
                 if ida_bytes.is_code(ida_bytes.get_flags(h))]
        funcs.append({
            "name": idc.get_func_name(ea),
            "start": f.start_ea,
            "end": f.end_ea,
            "size": size,
            "thumb": is_thumb(f.start_ea),
            "calls_out": func_calls_out(f.start_ea, f.end_ea),
            "heads": heads,
            "bytes": b.hex(),
        })

    funcs.sort(key=lambda x: x["size"])

    # Dump loaded segments so the lifter can resolve PC-relative literal-pool reads
    # and any static data access against the real image bytes.
    segs = []
    for i in range(ida_segment.get_segm_qty()):
        s = ida_segment.getnseg(i)
        if not s:
            continue
        size = s.end_ea - s.start_ea
        data = ida_bytes.get_bytes(s.start_ea, size)
        if not data:
            continue  # uninitialized (.bss) — skip; runtime zero-fills
        segs.append({
            "name": ida_segment.get_segm_name(s),
            "start": s.start_ea,
            "end": s.end_ea,
            "perm": s.perm,  # bit0=exec? IDA: SEGPERM_EXEC=1, WRITE=2, READ=4
            "bytes": data.hex(),
        })

    with open(out, "w") as fh:
        json.dump({"functions": funcs, "segments": segs}, fh)
    print(f"[*] {len(segs)} segments dumped.", file=sys.stderr)
    leaves = [f for f in funcs if f["calls_out"] == 0]
    print(f"[*] {len(funcs)} functions dumped to {out} ({len(leaves)} leaves).", file=sys.stderr)
    print("[*] smallest leaves:", file=sys.stderr)
    for f in [x for x in funcs if x["calls_out"] == 0][:12]:
        mode = "T" if f["thumb"] else "A"
        print(f"    {f['start']:#010x}  {f['size']:4d}B  [{mode}]  {f['name']}", file=sys.stderr)

    idapro.close_database(save=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
