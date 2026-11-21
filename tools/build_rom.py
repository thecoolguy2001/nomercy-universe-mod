#!/usr/bin/env python3
"""Build the Universe Mode ROM from an untouched No Mercy (US v1.0) ROM.

  build_rom.py extract-entry <rom> <out.bin>
      Save the game's original 96-byte entry code (the payload restores it).

  build_rom.py patch <rom> <stub.bin> <payload.bin> <out.z64>
      Put the payload in the ROM's unused padding, replace the entry code
      with the loader stub, and recompute the CIC-6102 boot checksum.
"""
import hashlib
import sys

US_V10_SHA1 = "3566d9b5723291937582453ad0453ce517cfc358"
ENTRY_ROM = 0x1000          # entry code in ROM (loaded to 0x80000400)
ENTRY_LEN = 0x60            # up to the boot function at 0x80000460
PAYLOAD_ROM = 0x1FB0000     # inside the 0xFF padding that starts at 0x1FA6B70
PADDING_START = 0x1FA6B70
ROM_SIZE = 0x2000000
# osContGetReadData (RAM 0x800346A8) -> jump to the resident module
HOOK_ROM = 0x1000 + 0x342A8
HOOK_ORIG = bytes.fromhex("27bdfff0" "00804821")   # addiu sp,sp,-0x10 ; move t1,a0
HOOK_NEW = bytes.fromhex("08104000" "00000000")    # j 0x80410000 ; nop
# Asset lookup (RAM 0x80003BD0) -> resident asset hook
ASSET_HOOK_ROM = 0x1000 + 0x37D0
ASSET_HOOK_ORIG = bytes.fromhex("30a2ffff" "2c430001")
ASSET_HOOK_NEW = bytes.fromhex("08104002" "00000000")    # j 0x80410008 ; nop
# play_song (RAM 0x80017CF4) -> resident music hook
SONG_HOOK_ROM = 0x1000 + 0x178F4
SONG_HOOK_ORIG = bytes.fromhex("3c028005" "8c428588")
SONG_HOOK_NEW = bytes.fromhex("08104004" "00000000")     # j 0x80410010 ; nop
# Menu overlay (code segment stored at ROM = RAM - 0x80083AE0): the per-frame
# draw_text call at RAM 0x800DE6FC -> resident text hook
TEXT_HOOK_ROM = 0x5AC1C
TEXT_HOOK_ORIG = bytes.fromhex("0c037708")                # jal 0x800DDC20
TEXT_HOOK_NEW = bytes.fromhex("0c104006")                 # jal 0x80410018
# New label asset (packed) goes in the unused ROM padding before the payload
LABEL_ROM = 0x1FA8000
# Single Play entry of the main-menu table (overlay stored uncompressed;
# RAM 0x8014F4E0): (0x8014D47C, 2 items) -> (0x80409000, 3 items)
SP_TABLE_ROM = 0xF5C80
SP_TABLE_ORIG = bytes.fromhex("8014d47c" "02000000")
SP_TABLE_NEW = bytes.fromhex("80409000" "03000000")
# First 4 words of the original entry: BSS clear of 0x80056710, 0x68780 bytes
EXPECTED_ENTRY = bytes.fromhex("3c088005 25086710 3c090007 25298780".replace(" ", ""))


def load_rom(path):
    data = open(path, "rb").read()
    sha1 = hashlib.sha1(data).hexdigest()
    if sha1 != US_V10_SHA1:
        sys.exit(f"{path}: not the expected No Mercy US v1.0 ROM (sha1 {sha1})")
    if data[ENTRY_ROM:ENTRY_ROM + 16] != EXPECTED_ENTRY:
        sys.exit("entry code does not match the expected layout")
    return bytearray(data)


def cic6102_crc(rom):
    m = 0xFFFFFFFF
    t1 = t2 = t3 = t4 = t5 = t6 = 0xF8CA4DDC
    for i in range(0x1000, 0x101000, 4):
        d = int.from_bytes(rom[i:i + 4], "big")
        if (t6 + d) & m < t6:
            t4 = (t4 + 1) & m
        t6 = (t6 + d) & m
        t3 ^= d
        r = ((d << (d & 0x1F)) | (d >> (32 - (d & 0x1F)))) & m
        t5 = (t5 + r) & m
        t2 = t2 ^ r if t2 > d else t2 ^ t6 ^ d
        t1 = (t1 + (t5 ^ d)) & m
    return (t6 ^ t4 ^ t3) & m, (t5 ^ t2 ^ t1) & m


def extract_entry(rom_path, out_path):
    rom = load_rom(rom_path)
    open(out_path, "wb").write(rom[ENTRY_ROM:ENTRY_ROM + ENTRY_LEN])


def patch(rom_path, stub_path, payload_path, out_path, label_path=None):
    rom = load_rom(rom_path)
    stub = open(stub_path, "rb").read()
    payload = open(payload_path, "rb").read()

    if len(stub) > ENTRY_LEN:
        sys.exit(f"stub is {len(stub)} bytes, max {ENTRY_LEN}")
    if PAYLOAD_ROM + len(payload) > ROM_SIZE:
        sys.exit(f"payload is {len(payload)} bytes, max {ROM_SIZE - PAYLOAD_ROM}")
    if any(b != 0xFF for b in rom[PADDING_START:ROM_SIZE]):
        sys.exit("ROM padding region is not empty")

    crc_before = cic6102_crc(rom)
    if crc_before != (int.from_bytes(rom[16:20], "big"), int.from_bytes(rom[20:24], "big")):
        sys.exit("original checksum does not verify; refusing to patch")

    if rom[HOOK_ROM:HOOK_ROM + 8] != HOOK_ORIG:
        sys.exit("osContGetReadData does not match the expected instructions")
    rom[HOOK_ROM:HOOK_ROM + 8] = HOOK_NEW

    if rom[ASSET_HOOK_ROM:ASSET_HOOK_ROM + 8] != ASSET_HOOK_ORIG:
        sys.exit("asset lookup does not match the expected instructions")
    rom[ASSET_HOOK_ROM:ASSET_HOOK_ROM + 8] = ASSET_HOOK_NEW

    if rom[SONG_HOOK_ROM:SONG_HOOK_ROM + 8] != SONG_HOOK_ORIG:
        sys.exit("play_song does not match the expected instructions")
    rom[SONG_HOOK_ROM:SONG_HOOK_ROM + 8] = SONG_HOOK_NEW

    if rom[TEXT_HOOK_ROM:TEXT_HOOK_ROM + 4] != TEXT_HOOK_ORIG:
        sys.exit("menu text call does not match the expected instruction")
    rom[TEXT_HOOK_ROM:TEXT_HOOK_ROM + 4] = TEXT_HOOK_NEW

    if rom[SP_TABLE_ROM:SP_TABLE_ROM + 8] != SP_TABLE_ORIG:
        sys.exit("Single Play menu table entry does not match")
    rom[SP_TABLE_ROM:SP_TABLE_ROM + 8] = SP_TABLE_NEW

    if label_path:
        label = open(label_path, "rb").read()
        if LABEL_ROM < PADDING_START or LABEL_ROM + len(label) > PAYLOAD_ROM:
            sys.exit("label asset does not fit in the free ROM space")
        rom[LABEL_ROM:LABEL_ROM + len(label)] = label

    padded = payload + b"\xFF" * (-len(payload) % 8)
    rom[PAYLOAD_ROM:PAYLOAD_ROM + len(padded)] = padded
    rom[ENTRY_ROM:ENTRY_ROM + ENTRY_LEN] = stub + b"\x00" * (ENTRY_LEN - len(stub))

    c1, c2 = cic6102_crc(rom)
    rom[16:20] = c1.to_bytes(4, "big")
    rom[20:24] = c2.to_bytes(4, "big")
    open(out_path, "wb").write(rom)
    print(f"{out_path}: payload {len(payload)} bytes at ROM 0x{PAYLOAD_ROM:X}, "
          f"stub {len(stub)} bytes, CRC {c1:08X} {c2:08X}")
    print(f"sha1 {hashlib.sha1(rom).hexdigest()}")


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "extract-entry":
        extract_entry(sys.argv[2], sys.argv[3])
    elif len(sys.argv) in (6, 7) and sys.argv[1] == "patch":
        patch(*sys.argv[2:])
    else:
        sys.exit(__doc__)
