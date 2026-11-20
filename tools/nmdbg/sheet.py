#!/usr/bin/env python3
"""Contact sheet of nmdbg PNGs (filter-0 RGB). sheet.py out.png cols img1.png img2.png ..."""
import struct, sys, zlib
def load(p):
    d=open(p,'rb').read(); i=8; idat=b''; w=h=0
    while i<len(d):
        n=struct.unpack('>I',d[i:i+4])[0]; t=d[i+4:i+8]; c=d[i+8:i+8+n]
        if t==b'IHDR': w,h=struct.unpack('>II',c[:8])
        if t==b'IDAT': idat+=c
        i+=12+n
    raw=zlib.decompress(idat); s=w*3+1
    return w,h,[raw[y*s+1:(y+1)*s] for y in range(h)]
out,cols=sys.argv[1],int(sys.argv[2]); files=sys.argv[3:]
tw,th=160,120
rows=(len(files)+cols-1)//cols
W,H=cols*tw,rows*th
img=[bytearray(W*3) for _ in range(H)]
for k,f in enumerate(files):
    w,h,px=load(f); ox,oy=(k%cols)*tw,(k//cols)*th
    for y in range(th):
        sy=y*h//th; row=px[sy]
        for x in range(tw):
            sx=x*w//tw*3; img[oy+y][(ox+x)*3:(ox+x)*3+3]=row[sx:sx+3]
raw=b''.join(b'\x00'+bytes(r) for r in img)
def ch(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
open(out,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b''))
