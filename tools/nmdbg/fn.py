#!/usr/bin/env python3
"""Print the function containing a RAM address from boot.asm (main code).
fn.py <hex address> [lines]"""
import re, sys
addr = int(sys.argv[1], 16); count = int(sys.argv[2]) if len(sys.argv) > 2 else 60
lines = open('/root/n64tools/boot.asm').read().split('\n')
idx = {}
for i, l in enumerate(lines):
    m = re.match(r'\s+([0-9a-f]+):\s+(.*)', l)
    if m: idx[int(m.group(1), 16) + 0x80000400] = i
i = idx[addr]
while i > 0 and not re.search(r'addiu\s+\$sp, \$sp, -', lines[i]): i -= 1
for l in lines[i:i + count]:
    m = re.match(r'\s+([0-9a-f]+):\s+(.*)', l)
    if m: print(f'{int(m.group(1), 16) + 0x80000400:08x}  {m.group(2).strip()}')
