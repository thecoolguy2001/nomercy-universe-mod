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
Next: find the copy the loader reads (candidates above), or write the picks
before the final confirm.

## Not found yet
- Match result (winner, method) after the bell.
- Where match type / arena / player mode are committed for the match.
- Wrestler ids beyond page 1, unlock flags, menu code for Single Play.
