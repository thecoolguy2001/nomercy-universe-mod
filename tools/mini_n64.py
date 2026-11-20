#!/usr/bin/env python3
"""Minimal N64 boot harness for testing the Universe payload without an
emulator. NOT a general N64 emulator: it implements the 32-bit MIPS integer
instructions our payload uses, RDRAM, cart ROM, PI DMA, VI registers and a
PIF controller. It runs the patched ROM from the entry point, feeds scripted
button presses, saves screenshots of the framebuffer, and stops when the
payload hands control back to No Mercy (jumps to 0x80000400 again).

Usage:
  mini_n64.py <rom.z64> <outdir> [script]  [--state in.bin] [--save-state out.bin]
                                           [--no-expansion]

script: comma-separated steps, e.g. "shot:home,A,DOWN,shot:menu,START"
  BUTTON      press and release (A B Z START UP DOWN LEFT RIGHT L R)
  shot:NAME   save <outdir>/NAME.png
--state/--save-state carry RDRAM across runs to simulate a console reset.
"""
import struct
import sys
import zlib

M32 = 0xFFFFFFFF
BUTTONS = {"A": 0x8000, "B": 0x4000, "Z": 0x2000, "START": 0x1000, "UP": 0x0800,
           "DOWN": 0x0400, "LEFT": 0x0200, "RIGHT": 0x0100, "L": 0x0020, "R": 0x0010}


def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


class Halt(Exception):
    pass


class N64:
    def __init__(self, rom, ram_size):
        self.rom = rom
        self.ram = bytearray(ram_size)
        self.r = [0] * 32
        self.hi = self.lo = 0
        self.pc = 0x80000400
        self.npc = self.pc + 4
        self.steps = 0
        self.vi = {}
        self.vi_line = 0
        self.pi = {}
        self.si_dram = 0
        self.pif = bytearray(64)
        self.buttons = 0
        self.pad_reads = 0
        self.on_pad_read = None
        self.started_payload = False

    # ---- memory ----
    def phys(self, addr):
        if 0x80000000 <= addr < 0xC0000000:
            return addr & 0x1FFFFFFF
        raise Halt(f"unmapped address {addr:08X} at pc {self.pc:08X}")

    def read(self, addr, size):
        p = self.phys(addr)
        if p < len(self.ram):
            return int.from_bytes(self.ram[p:p + size], "big")
        if p < 0x00800000:
            return 0  # no expansion pak: reads as 0
        if 0x10000000 <= p < 0x10000000 + len(self.rom):
            o = p - 0x10000000
            return int.from_bytes(self.rom[o:o + size], "big")
        if 0x04400000 <= p < 0x04400038:
            if p == 0x04400010:                   # VI_V_CURRENT
                self.vi_line = (self.vi_line + 8) % 525
                return self.vi_line
            return self.vi.get(p, 0)
        if 0x04600000 <= p < 0x04600034:
            return 0 if p == 0x04600010 else self.pi.get(p, 0)
        if 0x04800000 <= p < 0x0480001C:
            return 0                              # SI never busy
        raise Halt(f"read from unhandled {p:08X} at pc {self.pc:08X}")

    def write(self, addr, size, val):
        p = self.phys(addr)
        val &= (1 << (8 * size)) - 1
        if p < len(self.ram):
            self.ram[p:p + size] = val.to_bytes(size, "big")
            return
        if p < 0x00800000:
            return  # no expansion pak: writes are lost
        if 0x04400000 <= p < 0x04400038:
            self.vi[p] = val
            return
        if 0x04600000 <= p < 0x04600034:
            self.pi[p] = val
            if p == 0x0460000C:                   # PI_WR_LEN: cart -> RDRAM
                dram = self.pi.get(0x04600000, 0) & 0x00FFFFFF
                cart = self.pi.get(0x04600004, 0) - 0x10000000
                n = val + 1
                self.ram[dram:dram + n] = self.rom[cart:cart + n]
            return
        if 0x04800000 <= p < 0x0480001C:
            if p == 0x04800000:
                self.si_dram = val & 0x00FFFFFF
            elif p == 0x04800010:                 # write 64 bytes to PIF RAM
                self.pif[:] = self.ram[self.si_dram:self.si_dram + 64]
                self.pif_run()
            elif p == 0x04800004:                 # read 64 bytes from PIF RAM
                self.ram[self.si_dram:self.si_dram + 64] = self.pif
            return
        raise Halt(f"write to unhandled {p:08X} at pc {self.pc:08X}")

    def pif_run(self):
        # Controller 1 only: expect FF 01 04 01 then 4 response bytes.
        if self.pif[0:4] != b"\xff\x01\x04\x01":
            raise Halt("unexpected PIF command block " + self.pif[:8].hex())
        self.pad_reads += 1
        if self.on_pad_read:
            self.on_pad_read(self)
        self.pif[4:8] = struct.pack(">HBB", self.buttons, 0, 0)

    # ---- CPU ----
    def run(self, limit=400_000_000):
        r = self.r
        while self.steps < limit:
            pc = self.pc
            if pc == 0x80060000:
                self.started_payload = True
            if pc == 0x80000400 and self.started_payload:
                raise Halt("handoff")
            ins = self.read(pc, 4)
            self.pc = self.npc
            self.npc = self.pc + 4
            self.steps += 1
            op = ins >> 26
            rs = (ins >> 21) & 31
            rt = (ins >> 16) & 31
            imm = ins & 0xFFFF
            simm = sx16(imm)
            if op == 0:
                rd = (ins >> 11) & 31
                sa = (ins >> 6) & 31
                fn = ins & 63
                a, b = r[rs], r[rt]
                if fn == 0x00: v = (b << sa) & M32
                elif fn == 0x02: v = b >> sa
                elif fn == 0x03: v = (s32(b) >> sa) & M32
                elif fn == 0x04: v = (b << (a & 31)) & M32
                elif fn == 0x06: v = b >> (a & 31)
                elif fn == 0x07: v = (s32(b) >> (a & 31)) & M32
                elif fn == 0x08: self.npc = a; continue
                elif fn == 0x09: v = (pc + 8) & M32; self.npc = a
                elif fn == 0x10: v = self.hi
                elif fn == 0x12: v = self.lo
                elif fn in (0x18, 0x19):
                    prod = s32(a) * s32(b) if fn == 0x18 else a * b
                    self.lo = prod & M32
                    self.hi = (prod >> 32) & M32
                    continue
                elif fn in (0x1A, 0x1B):
                    if b == 0:
                        continue
                    if fn == 0x1A:
                        q = int(s32(a) / s32(b))
                        m = s32(a) - q * s32(b)
                    else:
                        q, m = divmod(a, b)
                    self.lo, self.hi = q & M32, m & M32
                    continue
                elif fn in (0x20, 0x21): v = (a + b) & M32
                elif fn in (0x22, 0x23): v = (a - b) & M32
                elif fn == 0x24: v = a & b
                elif fn == 0x25: v = a | b
                elif fn == 0x26: v = a ^ b
                elif fn == 0x27: v = ~(a | b) & M32
                elif fn == 0x2A: v = int(s32(a) < s32(b))
                elif fn == 0x2B: v = int(a < b)
                else: raise Halt(f"unknown SPECIAL {fn:02X} at {pc:08X}")
                if rd: r[rd] = v
                continue
            if op == 1:                                   # REGIMM
                a = s32(r[rs])
                if rt == 0 and a < 0 or rt == 1 and a >= 0:
                    self.npc = (pc + 4 + (simm << 2)) & M32
                elif rt not in (0, 1):
                    raise Halt(f"unknown REGIMM {rt} at {pc:08X}")
                continue
            if op == 2 or op == 3:
                if op == 3:
                    r[31] = (pc + 8) & M32
                self.npc = (pc & 0xF0000000) | ((ins & 0x03FFFFFF) << 2)
                continue
            if 4 <= op <= 7:
                a, b = r[rs], r[rt]
                take = (a == b if op == 4 else a != b if op == 5 else
                        s32(a) <= 0 if op == 6 else s32(a) > 0)
                if take:
                    self.npc = (pc + 4 + (simm << 2)) & M32
                continue
            if op == 0x10:                                # COP0
                if rs == 0 and ((ins >> 11) & 31) == 9:   # mfc0 Count
                    if rt: r[rt] = (self.steps * 2) & M32
                continue
            if op == 0x2F:                                # cache
                continue
            a = r[rs]
            if op in (8, 9): v = (a + simm) & M32
            elif op == 0x0A: v = int(s32(a) < simm)
            elif op == 0x0B: v = int(a < (simm & M32))
            elif op == 0x0C: v = a & imm
            elif op == 0x0D: v = a | imm
            elif op == 0x0E: v = a ^ imm
            elif op == 0x0F: v = (imm << 16) & M32
            elif op in (0x20, 0x21, 0x23, 0x24, 0x25):
                addr = (a + simm) & M32
                if op == 0x20: v = self.read(addr, 1); v = (v - 256 if v & 0x80 else v) & M32
                elif op == 0x21: v = self.read(addr, 2); v = (v - 65536 if v & 0x8000 else v) & M32
                elif op == 0x23: v = self.read(addr, 4)
                elif op == 0x24: v = self.read(addr, 1)
                else: v = self.read(addr, 2)
            elif op in (0x28, 0x29, 0x2B):
                addr = (a + simm) & M32
                self.write(addr, {0x28: 1, 0x29: 2, 0x2B: 4}[op], r[rt])
                continue
            else:
                raise Halt(f"unknown opcode {op:02X} at {pc:08X}")
            if rt:
                r[rt] = v
        raise Halt("step limit")

    def screenshot(self, path):
        origin = self.vi.get(0x04400004, 0) & 0x00FFFFFF
        width = self.vi.get(0x04400008, 320) or 320
        rows = []
        for y in range(240):
            row = bytearray(b"\x00")
            base = origin + y * width * 2
            for x in range(320):
                px = int.from_bytes(self.ram[base + 2 * x:base + 2 * x + 2], "big")
                row += bytes((((px >> 11) & 31) << 3, ((px >> 6) & 31) << 3, ((px >> 1) & 31) << 3))
            rows.append(bytes(row))
        raw = b"".join(rows)

        def chunk(t, d):
            c = struct.pack(">I", len(d)) + t + d
            return c + struct.pack(">I", zlib.crc32(t + d) & M32)
        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 2, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
        open(path, "wb").write(png)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = sys.argv[1:]
    rom = open(args[0], "rb").read()
    outdir = args[1]
    script = [s for s in (args[2] if len(args) > 2 else "").split(",") if s]
    expansion = "--no-expansion" not in opts
    m = N64(rom, 0x800000 if expansion else 0x400000)

    if "--state" in opts:
        m.ram[:] = open(opts[opts.index("--state") + 1], "rb").read()[:len(m.ram)]
    # what the boot code (IPL3) does before jumping to the game
    m.ram[0x400:0x400 + 0x100000] = rom[0x1000:0x101000]
    m.ram[0x318:0x31C] = struct.pack(">I", len(m.ram))

    queue = list(script)
    state = {"release": False}

    def on_pad(machine):
        if machine.pad_reads <= 2:      # first reads: nothing held
            machine.buttons = 0
            return
        if state["release"]:
            machine.buttons = 0
            state["release"] = False
            return
        while queue and queue[0].startswith("shot:"):
            name = queue.pop(0)[5:]
            machine.screenshot(f"{outdir}/{name}.png")
            print(f"  screenshot {name}.png (pad read {machine.pad_reads})")
        if queue:
            machine.buttons = BUTTONS[queue.pop(0)]
            state["release"] = True
        else:
            machine.buttons = 0
            raise Halt("script finished")

    m.on_pad_read = on_pad
    try:
        m.run()
    except Halt as h:
        print(f"stopped: {h} after {m.steps} instructions")
        if str(h) == "handoff":
            ok = m.ram[0x400:0x460] == open("build/n64/orig_entry.bin", "rb").read()
            print("  original entry code restored:", ok)
            print("  sp at handoff points to:", hex(m.r[29]))
    if "--save-state" in opts:
        open(opts[opts.index("--save-state") + 1], "wb").write(m.ram)


if __name__ == "__main__":
    main()
