"""
gen_image.py — pack the image's loaded segments into a binary blob.

The lifted *code* runs natively, but it reads the image's *data* (vtables, jump tables,
string/const pools, static objects) from guest memory by address. Those bytes must be
present in the flat memory window at runtime. extract.py already dumped the relocated
segment bytes; this writes them as segments.bin for the runtime loader (image.c).

Format: u32 count, then per segment { u32 start, u32 len, u8 bytes[len] }, little-endian.

Usage:  py -3.11 gen_image.py functions_all.json [segments.bin]
"""
import json, sys, struct

src = sys.argv[1] if len(sys.argv) > 1 else "functions_all.json"
out = sys.argv[2] if len(sys.argv) > 2 else "segments.bin"
segs = json.load(open(src))["segments"]

with open(out, "wb") as f:
    f.write(struct.pack("<I", len(segs)))
    total = 0
    for s in segs:
        data = bytes.fromhex(s["bytes"])
        f.write(struct.pack("<II", s["start"], len(data)))
        f.write(data)
        total += len(data)
print(f"[*] {out}: {len(segs)} segments, {total} bytes "
      f"({', '.join('%s@%#x' % (s['name'], s['start']) for s in segs)})", file=sys.stderr)
