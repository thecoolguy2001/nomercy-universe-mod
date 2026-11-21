# Reverse-engineering findings (No Mercy US v1.0)

Found with the headless debug emulator in /root/n64tools (Mupen64Plus core +
cxd4 LLE RSP + angrylion RDP with a headless, scriptable output plugin:
`/root/n64tools/nmdbg/run.sh <rom> <script> <outdir>`). Frame numbers are
VI counts from power-on with the original ROM.

## Boot
- Entry 0x80000400: clears BSS 0x80056710 (+0x68780), sp = 0x800A1310,
  jumps to 0x80000460. CIC-6102.

## Video (Mupen64Plus)
- Screen only refreshes when VI_V_INTR < VI_V_SYNC (vi_controller.c).
- GLideN64 (M64Plus FZ default) does not show CPU-written framebuffers;
  Angrylion/ParaLLEl do. Universe screen needs RDP drawing for GLideN64.

## Menu route to an Exhibition match (scripted, original ROM)
1500/1800/2100 START (skip intro, title) → 2400 A = Multi Play > Exhibition
→ MATCH tab (2710 A = Single) → PLAYER tab (1P vs CPU / CPU vs 1P / Watch)
→ ARENA tab (Raw is War, No Mercy, SmackDown, KOTR, SummerSlam, Survivor
Series, Royal Rumble, WrestleMania) → RULES → START "Decision" → Superstar
Select (3x3 grid per page, L/R pages) → P1 A, CPU A → match loads.

## RAM addresses
| Address | Meaning | Confidence |
|---|---|---|
| 0x8015A0ED | MATCH tab cursor (0 Single, 1 Tag, 2 Triple Threat...) | high |
| 0x800AAAB5, 0x8015B815 | Superstar Select grid cursor (page 1: 0-8) | high |
| 0x80159722 | P1 chosen wrestler (grid index; 0 Rock, 1 ?, 3 Undertaker, 5 Mankind) | high |
| 0x80159724 | CPU/P2 chosen wrestler | high |
| 0x80159723 / 0x80159725 | 0x1A / 0x1B after each pick (constant so far: costume? control type?) | unknown |
| 0x80045849, 0x8015B94D | also hold the CPU pick | medium |

Writing 0x80159722/24 at frame 4060 (after both picks confirmed) did NOT
change who wrestled — the match loader had already copied the picks.

**Working method to choose wrestlers:** write the grid cursor just before
each confirm press. P1: 0x8015B815 + 0x800AAAB5 before P1's A. CPU:
0x8015B94D + 0x800AAAB5 + 0x80045849 before the CPU's A. Verified:
7 vs 4 = Kurt Angle vs Kane, 2 vs 6 = Steve Austin vs Chris Jericho.

Superstars page 1 grid: 0 The Rock, 1 ?, 2 Steve Austin, 3 Undertaker,
4 Kane, 5 Mankind, 6 Chris Jericho, 7 Kurt Angle, 8 ?.
("Type N" under the name changes with the pick — probably attire.)

## Not found yet
- Finish METHOD (meaning of loser codes 1 vs 3), DQ/count-out/submission codes.
- Where match type / arena / player mode are committed for the match.
- Wrestler ids beyond page 1, unlock flags, menu code for Single Play.

## Match results (in progress)
Runs: wa3 Rock beat Kane (Watch, ended ~f17000, 2:47), we Mankind beat
Angle (Watch, pinfall at 2:21, ~f15500), wd P1 Rock idle vs CPU Kane
(ended ~f36000, 7:57, winner not confirmed), wb3 Austin v Jericho (no
finish by 7:27). Exhibition Watch matches loop replays after the bell.

| Address | Observation | Status |
|---|---|---|
| 0x800436C2 | 0 during every match; after the bell: 1 (wa3), 2 (wd), 0 (we pinfall) | candidate for finish METHOD — needs dense screenshots to confirm |
| 0x8015A5F6, 0x8015B232 | follow a wrestler id but also change mid-match | probably camera/focus, not winner |
| 0x8015B1D7, 0x8015A896, 0x8015A932 | change at the end but also during play | unresolved |
Winner byte not identified yet. Next: shorter controlled matches (e.g.
low time limit via RULES, or P1 forced loss) with screenshots every 30
frames around the bell.

## Match clock and result — FOUND
| Address | Meaning |
|---|---|
| 0x8015AB18 (u32) | match clock minutes |
| 0x8015AB1C (u32) | match clock seconds; clock freezes at the bell |
| 0x8015AF45 | side 1 (P1 slot) result: 0 during match, **4 = won**, 1 or 3 = lost |
| 0x8015B29D | side 2 result, same codes (slot stride 0x358; more slots likely for tag/triple) |

Verified on 9 finished matches (5 with pre/post-bell snapshots from the
`endtrig` debugger command, 4 from periodic dumps) and 1 unfinished match
(stays 0/0). Loser code 1 vs 3 not explained yet (both seen on pinfalls
per the on-screen banner; idle-P1 match gave 4/3).

Debugger: `endtrig` script command saves pre.bin/post.bin around the bell
and end_NN.png screenshots, then quits. Emulation is NOT deterministic
between runs (same inputs gave different matches), so experiments must
capture what they need in a single run.

## In-game hook — WORKING (stage 1)
- osContGetReadData = 0x800346A8 (single caller: pad update at 0x800058A8,
  pads at 0x80064880, 4 x 6 bytes). __osContPifRam 0x800A0200,
  __osMaxControllers 0x800A2614.
- No Mercy only probes 8 bytes at 0x80400000 / 0x80600000; rest of
  Expansion RAM is free. Resident module at 0x80410000, mailbox 0x80408000.
- ROM patches osContGetReadData to "j 0x80410000"; loader copies the
  resident image at boot (or restores the original 2 instructions if no
  Expansion Pak). Old boot screen only when Z is held at power-on.
- Verified in debugger: hook called ~every 2-3 VI frames, menus respond
  normally through it (reached Exhibition > PLAYER tab).

## Main menu wheel / Single Play menu — FOUND
- Main menu wheel pages: Multi Play (7 items), Single Play (Championship,
  Survival), Commissioner (Smackdown Mall, Options). L/R turns the wheel.
- Menu overlay is stored UNCOMPRESSED in ROM: RAM 0x8014C000 = ROM 0xF27A0
  (RAM = ROM + 0x80059860 for this overlay).
- Menu table (pairs of item-list pointer, count<<24) around 0x8014F4C8.
  Single Play entry at RAM 0x8014F4E0 / ROM 0xF5C80 = (0x8014D47C, 2).
  Multi Play (0x8014D3E0, 7), Commissioner (0x8014D4A8, 2).
- Menu item = 22 bytes: s16 x, s16 y, 04 00, up, down, left, right,
  u16 label image id (0x85xx: Exhibition 8501 .. Ironman 8507,
  Championship 8509, Survival 850A, Smackdown Mall 850C, Options 850D),
  u16 help id (0xF6xx), byte, 00, FFFF, 04, mode opened (Exhibition 02,
  Championship 0C, Survival 03), 08 00.
- Single Play cursor: u16 at 0x80159C0C (record at 0x80159C00 holds a
  pointer to the menu-table entry at +8).
- Labels are rendered once when the menu is built (changing the id later
  does nothing). Label ids are not ASCII strings anywhere — likely images.

**Working:** ROM patch points the Single Play entry at (0x80409000, 3);
the boot loader writes 3 items there (third = Universe Mode, currently
reusing the Championship label and opening Survival). Drawn and navigable.
Requires the Expansion Pak.

## Match director — in test
Resident hook accepts CMD_WATCH via mailbox (0x80408014: cmd, p1, p2),
drives Exhibition menus from the MATCH tab, picks wrestlers, reaches the
match (verified: Undertaker picked, match started, state=IN_MATCH).

## Assets and the Universe Mode label — WORKING
- Asset table: ROM 0x16C3238, 19459 u32 entries, copied to RAM (pointer at
  0x80062CB0); unpacked sizes table pointer at 0x80062CB4.
  Entry & ~1 = offset from archive base ROM 0x1BD1B0; bit 0 = compressed.
- Compressed asset = u32 unpacked size + LZSS (4 KB ring zero-filled,
  start 4078, flag byte LSB-first, 1 = literal, ref = 12-bit pos + len-3).
  tools/nm_assets.py decodes/encodes; verified byte-exact on menu labels.
- Asset lookup 0x80003BD0 (a0 = record, a1 = 1-based id) fills
  {ROM start, ROM end = next entry, unpacked size, packed length}; the
  length comes from the NEXT entry, so assets can't just be moved.
- Menu labels are TEX assets (CI4 grey ramp, 128x32 / 96x32). Label ids:
  0x8501-07 Multi Play items, 0x8508 Single Play title, 0x8509
  Championship, 0x850A Survival, 0x850B Commissioner, 0x850C/D Smackdown
  Mall/Options, 0x850E Records (asset 0x4491, unused by any menu),
  0x8510 World Heavyweight, ...
- Universe label (text "Universe", 96x32 like Survival): tools/make_label.py cuts the game's own letters
  (Survival, Ladder Match, Options; capital U from lowercase u), packed
  at ROM 0x1FA8000. ROM patches 0x80003BD0 -> j 0x80410008; resident hook
  serves asset 0x4491 from there. Third Single Play item uses id 0x850E.

## On-screen text (in progress)
- Menu/footer text is ASCII in the overlay data: e.g. 0x800F3E90
  "$4Instructions", "$6Random", "$1Option", "$eDecision" ($x = button
  icon escape), match types, championship names, options words.
- String pointer table at 0x800F4794 (Instructions, Random, Option,
  Decision, ..., Watch at 0x800F47D4).
- Text pipeline (main code, always resident) found with read watchpoints:
  * 0x800086C0  char -> value ('0'-'9','a'-'z','A'-'Z' = 0..35), used
    for the $x escapes.
  * 0x80008724  string signature: record {+0 str ptr, +0x12 flags,
    +0x14 length, +0x18 checksum}.
  * 0x80009F18  lookup in a 128-slot rendered-text cache (slot 0x1C bytes,
    table pointer at 0x800931B4) by (str, style, length, checksum).
  * 0x80009FFC  get text texture: a0 = string | flags (high bits),
    a1 = style; renders new text via 0x80009760. Called by menu overlay
    code at 0x800E0880, which then adds the slot to the overlay's own
    draw list (0x800E07BC / 0x800E08EC).
- Font text is used for footers ("$4Instructions"), Superstar Select
  names and the match intro; the main-menu and Exhibition tab item lists
  are pre-drawn label images (no reads of "Single Match" on the MATCH tab).

## Debugger with breakpoints
NMDBG_DEBUG=1 run.sh ... < /dev/null uses a DEBUGGER=1 core
(/root/n64tools/mupen64plus-core-dbg) launched with --debug in the pure
interpreter; the video plugin replaces ui-console's debugger callbacks.
Script: `bpx ADDR` (virtual), `bpr/bpw ADDR LEN` (converted to PHYSICAL
addresses - mupen memory bps match physical), `bplimit N`. Each hit logs
pc, ra, a0-a3, v0, sp and resumes. ~3x slower than normal runs.
