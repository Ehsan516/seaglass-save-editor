#!/usr/bin/env python3
"""
probe_rom.py -- diagnose why offset auto-detection fails on a specific ROM.

Reuses the exact game-charset encoding from seaglass_save.py and searches the
ROM directly (independent of the SeaglassSave class, no save file needed) for
each of the four anchors the auto-detector looks for, reporting what it finds
so we can see exactly which one is missing/wrong instead of guessing.

Usage:  python probe_rom.py path\to\rom.gba
"""
import sys
from seaglass_save import _REV, decode_str

def enc(txt):
    out = bytearray()
    for ch in txt:
        if ch not in _REV:
            return None
        out.append(_REV[ch])
    return bytes(out)

def find_all(rom, sub, limit=20):
    e = enc(sub)
    if e is None:
        print(f"  (couldn't encode {sub!r} in the game charset)")
        return []
    out = []
    pos = 0
    while len(out) < limit:
        i = rom.find(e, pos)
        if i < 0:
            break
        out.append(i)
        pos = i + 1
    return out

def find_all_terminated(rom, sub, limit=20):
    e = enc(sub)
    if e is None:
        return []
    e = e + b"\xFF"
    out = []
    pos = 0
    while len(out) < limit:
        i = rom.find(e, pos)
        if i < 0:
            break
        out.append(i)
        pos = i + 1
    return out

def main(path):
    with open(path, "rb") as f:
        rom = f.read()
    print(f"ROM: {path}")
    print(f"ROM size: {len(rom):,} bytes ({len(rom)/1024/1024:.1f} MB)\n")

    print("-- Species anchor: Bulbasaur -> Ivysaur (+stride) -> Venusaur (+2*stride) --")
    positions = find_all(rom, "Bulbasaur")
    print(f"  'Bulbasaur' found at {len(positions)} location(s): {[hex(p) for p in positions]}")
    found_valid = False
    for b in positions:
        window = rom[b:b + 0x200 + 16]
        iv_enc = enc("Ivysaur")
        i = window.find(iv_enc)
        if i > 0:
            stride = i
            in_range = 0x80 <= stride <= 0x200
            venusaur = decode_str(rom[b + 2 * stride: b + 2 * stride + 8])
            print(f"  @ {hex(b)}: 'Ivysaur' at stride {hex(stride)} "
                  f"({'in range' if in_range else 'OUT OF 0x80-0x200 RANGE'}), "
                  f"then decodes to {venusaur!r}")
            if in_range and venusaur == "Venusaur":
                print(f"    ==> VALID species anchor at {hex(b)}, stride {hex(stride)}")
                found_valid = True
        else:
            print(f"  @ {hex(b)}: no 'Ivysaur' within 0x200 bytes")
    if not found_valid:
        print("  ==> NO VALID SPECIES ANCHOR FOUND")
    print()

    print("-- Move anchor: 'Karate Chop' + terminator (move 2) --")
    kc = find_all_terminated(rom, "Karate Chop")
    print(f"  found at: {[hex(p) for p in kc]}" if kc else "  NOT FOUND")
    print()

    print("-- Item anchor: 'Master Ball' + terminator (item 4) --")
    mb = find_all_terminated(rom, "Master Ball")
    print(f"  found at: {[hex(p) for p in mb]}" if mb else "  NOT FOUND")
    print()

    print("-- Ability anchor: 'Overgrow' + terminator (id 65) --")
    og = find_all_terminated(rom, "Overgrow")
    print(f"  found at: {[hex(p) for p in og]}" if og else "  NOT FOUND")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: python probe_rom.py path\\to\\rom.gba")
        sys.exit(1)
    main(sys.argv[1])
