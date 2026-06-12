"""
extract_imports.py — dump the import address table of an N-Gage .app.

Each imported Symbian function has a slot in the image's import address table (IAT).
The game calls it indirectly: `ldr r12, =slot; ldr r12, [r12]; blx r12`. For HLE we
make each slot point to itself and register that address -> a native shim, so the
indirect call dispatches to our implementation.

Output: imports.json = [{slot, dll, name, ordinal}], consumed by gen_hle.py.

Usage:  py -3.11 extract_imports.py <app> [imports.json]
"""
import idapro  # must be first
import sys, json
import ida_nalt


def main():
    app = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "imports.json"
    print(f"[*] Opening {app} (headless)...", file=sys.stderr)
    idapro.open_database(app, run_auto_analysis=True)

    imports = []
    nmods = ida_nalt.get_import_module_qty()
    for i in range(nmods):
        dll = ida_nalt.get_import_module_name(i) or f"mod{i}"

        def cb(ea, name, ordinal, _dll=dll):
            imports.append({"slot": ea, "dll": _dll.upper(),
                            "name": name or "", "ordinal": ordinal})
            return True

        ida_nalt.enum_import_names(i, cb)

    with open(out, "w") as fh:
        json.dump(imports, fh)
    print(f"[*] {len(imports)} imports across {nmods} modules -> {out}", file=sys.stderr)
    idapro.close_database(save=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
