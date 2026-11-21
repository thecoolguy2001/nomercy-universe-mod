#!/usr/bin/env python3
"""Decode CI4/CI8 textures seen in an nmdbg [tex] log from a RAM dump.
textures.py log.txt ram.bin outdir"""
import os, re, struct, sys, zlib
log, ram, out = sys.argv[1:4]
d = open(ram, 'rb').read()
os.makedirs(out, exist_ok=True)
lines = [l.split(None, 2)[2] for l in open(log) if l.startswith('[tex]')]
seen = {}
tlut = None; timg = None; texels = None
for l in lines:
    m = re.match(r'SETTIMG fmt=(\d) siz=(\d) width=(\d+) addr=([0-9A-F]+)', l)
    if m:
        fmt, siz, addr = int(m[1]), int(m[2]), int(m[4], 16)
        if fmt == 0 and siz == 2: tlut = addr
        elif fmt == 2: timg = addr
        continue
    m = re.match(r'LOADBLOCK tile=7 texels=(\d+)', l)
    if m: texels = int(m[1]); continue
    m = re.match(r'SETTILE fmt=2 siz=(\d) line=(\d+) tmem=000 tile=0', l)
    if m and timg is not None and tlut is not None and texels:
        siz, line = int(m[1]), int(m[2])
        nbytes = texels * 2
        w = line * 8 * (2 if siz == 0 else 1)
        if w and nbytes % (line * 8) == 0:
            seen[timg] = (tlut, siz, w, nbytes // (line * 8), nbytes)
        timg = None
def png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    ch = lambda t, x: struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
for addr, (tl, siz, w, h, nb) in sorted(seen.items()):
    ncol = 16 if siz == 0 else 256
    pal = []
    for i in range(ncol):
        c = struct.unpack('>H', d[tl + 2*i: tl + 2*i + 2])[0]
        pal.append(bytes(((c >> 11 & 31) << 3, (c >> 6 & 31) << 3, (c >> 1 & 31) << 3)) if c & 1 else b'\x10\x10\x30')
    px = bytearray()
    data = d[addr:addr + nb]
    for b in data:
        if siz == 0: px += pal[b >> 4] + pal[b & 15]
        else: px += pal[b]
    png(f'{out}/tex_{addr:06X}_{w}x{h}_ci{4 if siz==0 else 8}.png', w, h, bytes(px))
    print(f'{0x80000000+addr:08X} palette {0x80000000+tl:08X} CI{4 if siz==0 else 8} {w}x{h} ({nb} bytes)')
