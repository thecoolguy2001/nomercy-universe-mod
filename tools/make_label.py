#!/usr/bin/env python3
"""Build a No Mercy menu label (TEX, CI4, grey ramp) from letters cut out of
the game's own labels, so the new text matches the game's font exactly.

  make_label.py <rom> "Universe Mode" <out.tex> [preview.png]

Letters are cut from existing label assets at columns that hold only
outline/transparent pixels (index <= 3). Capital U does not exist in any
label, so it is built from the game's lowercase u stretched to cap height.
"""
import struct
import sys

import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nm_assets as A  # noqa: E402

# label asset index -> its text (letters only matter, spaces skipped)
SOURCES = {
    0x448C: "Survival",
    0x448D: "Championship",
    0x448E: "Smackdown Mall",
    0x448F: "Options",
    0x448A: "Guest Referee",
    0x448B: "Ladder Match",
    0x4488: "Pay Per View",
    0x4489: "King of the Ring",
    0x4487: "Royal Rumble",
    0x4491: "Ironman Match",
}
GAP_MAX = 3      # column counts as a gap if every pixel is <= this
TOP, BOTTOM = 4, 26
CAP_TOP, X_TOP, BASE = 6, 9, 23   # cap height, x-height, baseline rows (Survival)


def pixels(rom, idx):
    d = A.load(rom, idx)
    w, h, bpp, _, ncol = struct.unpack('>HHBBH', d[4:12])
    px = d[16 + 2 * ncol:]
    grid = [[(px[(y * w + x) // 2] >> (4 if x % 2 == 0 else 0)) & 15 for x in range(w)] for y in range(h)]
    return w, h, grid, d[:16 + 2 * ncol]


def split(grid, w):
    """Column runs that contain letter pixels (value > GAP_MAX)."""
    runs, start = [], None
    for x in range(w):
        solid = any(grid[y][x] > GAP_MAX for y in range(TOP, BOTTOM))
        if solid and start is None:
            start = x
        if not solid and start is not None:
            runs.append((start, x))
            start = None
    if start is not None:
        runs.append((start, w))
    return runs


# Hand-picked cut columns [x0, x1) in the game's own labels (outline
# columns included so neighbouring letters can share them).
CUTS = {
    'u': (0x448C, 23, 35),   # Survival
    'v': (0x448C, 41, 52),
    'i': (0x448C, 51, 57),
    'e': (0x448B, 45, 55),   # Ladder Match
    'r': (0x448B, 54, 64),
    'd': (0x448B, 24, 35),
    'M': (0x448B, 69, 85),
    'o': (0x448F, 49, 60),   # Options
    'n': (0x448F, 59, 70),
    's': (0x448F, 70, 81),
}


def glyph_bank(rom):
    cache, bank = {}, {}
    for ch, (idx, x0, x1) in CUTS.items():
        if idx not in cache:
            cache[idx] = pixels(rom, idx)[2]
        g = cache[idx]
        bank[ch] = [[g[y][x] for y in range(32)] for x in range(x0, x1)]
    return bank


def stretch_to_caps(cols):
    """Make a capital from a lowercase glyph: rows X_TOP..BASE -> CAP_TOP..BASE."""
    out = []
    for col in cols:
        new = col[:]
        for y in range(CAP_TOP - 1, BASE + 2):
            new[y] = 0
        src_top, src_bot = X_TOP - 1, BASE + 1      # include outline rows
        dst_top, dst_bot = CAP_TOP - 1, BASE + 1
        for y in range(dst_top, dst_bot + 1):
            sy = src_top + round((y - dst_top) * (src_bot - src_top) / (dst_bot - dst_top))
            new[y] = col[sy]
        out.append(new)
    return out


def compose(bank, text, width=128, height=32, space=5):
    cols = []
    for ch in text:
        if ch == ' ':
            cols += [[0] * height for _ in range(space)]
            continue
        if ch in bank:
            g = bank[ch]
        elif ch == 'U' and 'u' in bank:
            g = stretch_to_caps(bank['u'])
        else:
            raise SystemExit(f'no glyph for {ch!r}')
        if cols:   # letters share one outline column, like the originals
            last, first = cols[-1], g[0]
            cols[-1] = [max(a, b) for a, b in zip(last, first)]
            g = g[1:]
        cols += [c[:] for c in g]
    if len(cols) > width:
        # squeeze horizontally to fit, keeping the outline (nearest neighbour)
        n = len(cols)
        cols = [cols[min(n - 1, round(x * n / width))] for x in range(width)]
    pad = (width - len(cols)) // 2
    cols = [[0] * height] * pad + cols + [[0] * height] * (width - pad - len(cols))
    return [[cols[x][y] for x in range(width)] for y in range(height)]


def to_tex(header_template, grid, width, height):
    hdr = bytearray(header_template)
    struct.pack_into('>HH', hdr, 4, width, height)
    px = bytearray()
    for y in range(height):
        for x in range(0, width, 2):
            px.append(grid[y][x] << 4 | grid[y][x + 1])
    return bytes(hdr) + bytes(px)


def preview(path, grid, scale=3):
    h, w = len(grid), len(grid[0])
    rgb = bytearray()
    for y in range(h * scale):
        for x in range(w * scale):
            v = grid[y // scale][x // scale]
            rgb += bytes((v * 17,) * 3) if v else b'\x20\x20\x50'
    A.write_png(path, w * scale, h * scale, bytes(rgb))


if __name__ == '__main__':
    rom = open(sys.argv[1], 'rb').read()
    text, out = sys.argv[2], sys.argv[3]
    bank = glyph_bank(rom)
    print('glyphs:', ''.join(sorted(bank)))
    grid = compose(bank, text)
    _, _, _, header = pixels(rom, 0x448D)       # 128x32 header + palette
    open(out, 'wb').write(to_tex(header, grid, 128, 32))
    if len(sys.argv) > 4:
        preview(sys.argv[4], grid)
