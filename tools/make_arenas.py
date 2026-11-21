#!/usr/bin/env python3
"""Extract No Mercy's 10 arena preview pictures (ARENA tab) for the resident.

  make_arenas.py <rom> <out.bin>

Each picture is 6 TEX assets (64x32 CI8, own 256-colour RGBA5551 palette),
2 across x 3 down = 128x96, asset ids 0x4635 + 6*picture (1-based).
Output: for each picture, for each tile: 512-byte palette + 2048 pixels.
Picture order: SummerSlam, SmackDown, Raw, WrestleMania, Survivor Series,
Backlash, Royal Rumble, Armageddon, No Mercy, King of the Ring.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nm_assets  # noqa: E402

FIRST_ID = 0x4635
PICTURES = 10


def main(rom_path, out_path):
    rom = open(rom_path, 'rb').read()
    out = bytearray()
    for i in range(PICTURES * 6):
        d = nm_assets.load(rom, FIRST_ID + i - 1)
        w, h, bpp, _, ncol = struct.unpack('>HHBBH', d[4:12])
        if d[:4] != b'TEX\0' or (w, h, bpp, ncol) != (64, 32, 8, 256):
            sys.exit(f'asset 0x{FIRST_ID + i:X} is not a 64x32 CI8 arena tile')
        out += d[16:16 + 512 + 2048]
    open(out_path, 'wb').write(out)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
