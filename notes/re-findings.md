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
