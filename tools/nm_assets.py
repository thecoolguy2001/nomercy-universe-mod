#!/usr/bin/env python3
"""No Mercy (US 1.0) asset archive tools.

Asset table: ROM 0x16C3238, 19459 big-endian u32 entries. Entry & ~1 is the
offset of the asset from the archive base (ROM 0x1BD1B0); the low bit marks
it as compressed. A compressed asset is: u32 decompressed size, then an
LZSS stream (4 KB ring, zero-filled, write index starts at 4078, flag byte
LSB first, 1 = literal, 0 = 12-bit offset / 4-bit length+3).

TEX assets: "TEX\\0", u16 width, u16 height, u8 bpp (4), u8 2, u16 colours,
4 zero bytes, RGBA5551 palette, then CI pixels (high nibble first).

  nm_assets.py show <index> <out.png>         decode a TEX asset to PNG
"""
import struct
import sys
import zlib

TABLE = 0x16C3238
COUNT = 19459
BASE = 0x1BD1B0


def entry(rom, idx):
    return struct.unpack('>I', rom[TABLE + 4 * idx:TABLE + 4 * idx + 4])[0]


def lzss_decompress(src, pos, size):
    ring = bytearray(4096)
    r = 4078
    out = bytearray()
    flags = 0
    while len(out) < size:
        flags >>= 1
        if not flags & 0x100:
            flags = src[pos] | 0xFF00
            pos += 1
        if flags & 1:
            c = src[pos]
            pos += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) & 4095
        else:
            b1, b2 = src[pos], src[pos + 1]
            pos += 2
            off = b1 | (b2 & 0xF0) << 4
            for k in range((b2 & 0x0F) + 3):
                c = ring[(off + k) & 4095]
                out.append(c)
                ring[r] = c
                r = (r + 1) & 4095
    return bytes(out[:size]), pos


def load(rom, idx):
    e = entry(rom, idx)
    at = BASE + (e & ~1)
    if e & 1:
        size = struct.unpack('>I', rom[at:at + 4])[0]
        data, _ = lzss_decompress(rom, at + 4, size)
        return data
    nxt = BASE + (entry(rom, idx + 1) & ~1)
    return rom[at:nxt]


def tex_to_rgb(data):
    if data[:4] != b'TEX\0':
        raise ValueError('not a TEX asset')
    w, h, bpp, _, ncol = struct.unpack('>HHBBH', data[4:12])
    pal = []
    for i in range(ncol):
        c = struct.unpack('>H', data[16 + 2 * i:18 + 2 * i])[0]
        pal.append(bytes(((c >> 11 & 31) << 3, (c >> 6 & 31) << 3, (c >> 1 & 31) << 3))
                   if c & 1 else b'\x30\x30\x30')
    px = data[16 + 2 * ncol:]
    rgb = bytearray()
    if bpp == 4:
        for b in px[:w * h // 2]:
            rgb += pal[b >> 4] + pal[b & 15]
    else:
        for b in px[:w * h]:
            rgb += pal[b]
    return w, h, bytes(rgb)


def write_png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    ch = lambda t, x: struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))


def lzss_compress(data):
    """Greedy LZSS matching lzss_decompress (ring 4096, start 4078, len 3..18)."""
    ring = bytearray(4096)
    r = 4078
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        flag_pos = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if i >= n:
                break
            best_len, best_off = 0, 0
            max_len = min(18, n - i)
            for off in range(4096):
                # simulate the decoder: copying from ring[off+k] while writing at r+k
                k = 0
                while k < max_len:
                    src = (off + k) & 4095
                    # bytes written earlier in this same copy are visible to later k
                    dist = (src - r) & 4095
                    c = data[i + dist] if dist < k else ring[src]
                    if c != data[i + k]:
                        break
                    k += 1
                if k > best_len:
                    best_len, best_off = k, off
                    if k == max_len:
                        break
            if best_len >= 3:
                out.append(best_off & 0xFF)
                out.append((best_off >> 4) & 0xF0 | (best_len - 3))
                for k in range(best_len):
                    ring[r] = data[i + k]
                    r = (r + 1) & 4095
                i += best_len
            else:
                flags |= 1 << bit
                out.append(data[i])
                ring[r] = data[i]
                r = (r + 1) & 4095
                i += 1
        out[flag_pos] = flags
    return bytes(out)


def pack_asset(data):
    """u32 decompressed size + LZSS stream (the archive's compressed form)."""
    return struct.pack('>I', len(data)) + lzss_compress(data)


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == 'show':
        rom = open(sys.argv[2], 'rb').read()
        w, h, rgb = tex_to_rgb(load(rom, int(sys.argv[3], 0)))
        write_png(sys.argv[4], w, h, rgb)
        print(f'{w}x{h}')
    else:
        sys.exit('usage: nm_assets.py show <rom> <index> <out.png>')
