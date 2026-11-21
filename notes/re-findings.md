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

## Music — FOUND and WORKING
- All cartridge reads go through __osEPiRawStartDma 0x8002EC40 (called by
  the PI manager at 0x8002F350). Sound banks: "N64 PtrTablesV2" +
  "N64 WaveTables" at ROM 0x16F3300/0x16F6F70, 0x1858090/0x185EE10,
  0x1967470/0x19698E0; samples are streamed in 512-byte DMAs.
- play_song(a0 = song, a1 = mode, a2 = volume) at 0x80017CF4 (song < 0x50;
  current song u16 at 0x800485A4; per-song table at 0x80048178). Screens
  call it every frame or two with their song; it ignores repeats.
- Songs seen: 0x00 silence/intro, 0x0B title, 0x0E main menu, 0x10
  Exhibition, 0x12 Smackdown Mall, 0x29 The Undertaker (entrance),
  0x41 Chris Jericho (entrance). Menus use mode 4, volume 0x50.
- Hook: ROM patches 0x80017CF4 -> j 0x80410010; while mailbox
  music_override (0x80408024) is set, requests for songs 1..0x1F (menu
  music) are replaced by it; 0, -1 and wrestler themes (>= 0x20) pass.
  The resident sets it to 0x29 while Universe is highlighted on the
  Single Play page. Verified: requests 0x0E -> 0x29 -> 0x0E as the cursor
  moves onto and off Universe.
- Single Play menu record 0x80159C00: +0x08 table-entry ptr (0x8014F4E0),
  +0x0C u16 cursor, +0x18 u32 wheel page (0 Single, 1 Commissioner, 0x10
  Multi), +0x1C pointer.

## In-game Universe screen — WORKING
- Menu overlay CODE is stored at ROM = RAM - 0x80083AE0 (its data/tables
  use RAM - 0x80059860). The per-frame footer text call at 0x800DE6FC
  (jal 0x800DDC20, ROM 0x5AC1C) is patched to jal 0x80410018; the
  resident wrapper draws the footer then Universe text into the same
  display list.
- Menu text routines (menu overlay): draw_text 0x800DDC20 (right-aligned)
  and draw_text_centered 0x800DDDE8, args (Gfx **holder, str, x, y,
  scale 255=100%, style, flags); footer uses style 0x30, flags 0x100.
  Coordinates are in the 480x240 frame buffer (VI scales to 640).
- Button icon escapes: $0 A, $1 B, $2 L, $3 R, $4 Z, $5 stick,
  $6-$9 C-up/down/left/right, $a-$d D-pad up/down/left/right, $e START.
- Panel: FILL-cycle fill rect (render mode must be 0 in FILL mode or the
  RDP hangs - angrylion reports "RDP crashed"), then restore the game's
  text mode EF08ACEF/00504240 and combine FC5196A3/112CFE7F.
- Universe mode: A on the Universe item enters (input swallowed, menu
  underneath frozen), B leaves; Undertaker theme (song 0x29) only while
  inside the mode. The universe engine runs in the resident module.

## Play / Watch from Universe (in progress)
- Setting the Universe item's mode byte (0x8040903F) to 0x02 and pressing A
  opens Exhibition's MATCH tab directly from Single Play.
- Superstar Select: grid cursor value is per page (0-8); page variable
  0x800AAAB7; cursor writes only work on some pages, so navigation is by
  D-pad/R presses. Six pages for P1, more (LADIES) for the CPU side.
- 0x802EFB28 is NOT a highlight variable - it is a text-cache slot (writes
  come from 0x8000A07C/0x8000A29C). Instead the resident hooks
  get_text_image 0x80009FFC (ROM 0xABFC -> j 0x80410020) and recognises the
  highlighted wrestler when his full name string is drawn (the bottom
  label animates in: allow ~40 frames per move).
- Wrestler attire table in RAM (0x8031A398, 4 records of 0x4E per
  wrestler, full name at +0, short name at +0x15) gives the game's
  internal order: Rock, Austin, Undertaker, Kane, Steven Richards,
  Mankind, Cactus Jack, Mick Foley, HBK, Triple H, X-Pac, Mr. Ass, Road
  Dogg, Benoit, Malenko, Saturn, Guerrero, Jericho, Angle, Tazz, ...
  (Big Show is not in No Mercy.)
- Verified: Z on a card match -> Exhibition -> Watch -> both wrestlers
  picked by name (Val Venis, X-Pac) -> match starts.
- After a Watch match: replays loop until START; START -> white
  transition -> main menu wheel (Multi Play page). Director phase
  D_RETURN presses START, waits until the menu footer is being drawn
  (footer hook = "a menu is on screen"; the Single Play record keeps stale
  values during matches), steers the wheel (R from Multi, L from
  Commissioner, D-down to cursor 2) and reopens the Universe screen.
- Debugger: `endtrig keep` keeps running after the bell and rebases script
  commands with frame >= 90000 to "bell + (frame - 90000)".

## Real match setup (in progress, replacing the menu-driving director)
User feedback (2026-11-21): grid search too slow; Watch sometimes became
1P vs CPU (timed D-pad presses); title matches not set (BELT tab never
used). Goal: write the match setup directly instead of driving menus.
- 0x80159720 picks are NOT read by the match loader (read watchpoints
  over 0x80159600-0x801597FF from confirm to intro only show menu reads).
- At each Superstar Select confirm, fixed-memory slots go 255 -> value:
  0x800AAAD0 (P1: Angle 3, Austin 0), 0x800AAADC/0x800AAADD (CPU: Kane
  0/4, Jericho 3/1). Related: 0x800A556F, 0x800AA0CE/D2, 0x8015B81F/23.
  Looks like a per-player selection table - next: decode its meaning and
  find what the match loader reads (bpr on 0x800AAAC0-0x800AAAFF).
- Debugger: NMDBG_EA=1 decodes the load/store at each memory-breakpoint
  hit and logs unique (pc, effective address) pairs as [ea] lines.
- SOLVED (director v2, no grid search):
  - Setup slots 0x800AAAD0 + 12*i: +0 u16 wrestler id (0xFFxx = not
    picked; Superstar Select empties both on entry). Ids come from the
    game's table: n = 1..64 (internal order, Rock 1 ... Jim Ross 64),
    id = *(u16*)*(u32*)(0x800471C4 + 16*n), e.g. Rock 0x0001, HHH 0x0101,
    Jericho 0x0301, Angle 0x0302, Rikishi 0x0401, Bubba 0x0504.
  - Participant list 0x800A5568 + 4*i (u32 id) must be written too, else
    side 2 keeps the grid pick.
  - PLAYER tab choice: 0x8015971C / 0x80159655 / 0x80159FB5 (1 1P vs CPU,
    2 CPU vs 1P, 3 Watch). The tab's default is Watch, so the old fixed
    D-down presses turned Watch into 1P vs CPU. Writing these bytes
    before A on the PLAYER tab gives the right Superstar Select labels.
  - Title: s32 0x800AB5B4 = belt (-1 none): 0 World Heavyweight, 1 Tag,
    2 Intercontinental, 3 European, 4 Hardcore, 5 Light Heavyweight,
    6 Women's. The intro caption (code at 0x80117244) then reads e.g.
    "World Heavyweight Championship" with the WWF logo. Champions table
    0x800A53E0 + 0x34*belt, +2 = champion's wrestler number (empty on a
    fresh save, which is why the BELT tab is greyed out). The director
    restores it after the match.
  - Match flags u32 0x800AAAB4 select the normal caption (0x8011726C).
- Post-match (2026-11-21): after the bell the game shows replays (until
  START), then the result screen (Exit / Rematch). Exit loads the main
  menu on the Multi Play page. u8 0x80045845 is the screen id
  (0x2A match, 0x1D result screen, 2 Multi Play page, 3 Single Play page);
  changing 2 -> 3 as soon as Exit sets it makes the menu open on Single
  Play, with the cursor still on Universe. 0x80159C14/18 are the menu
  slide-in animation (0x88/0x10 while sliding), not a page number.
  Director: entrances, replays and the result screen are left to the
  player; D_RETURN swaps the screen id and opens Universe once the menu is
  up (D_STEER, under a full-screen cover, is only a fallback).

## Straight to Superstar Select (2026-11-21)
User request: no Exhibition tabs; Superstar Select opens on the card's
wrestlers, the player confirms (or changes) them, then the loading screen.
- Screen ids (u16 0x80045844, low byte used): 07 MATCH tab, 06 PLAYER,
  05 ARENA, 15 RULES, 29 loading transition, 1A Superstar Select, 1B second
  pick confirmed, 19 "Single Match / A VS B" (waits for A), 2A match,
  1D result screen (Quit / Rematch, shows time and method e.g. "Pin").
- START ("Decision") works on the MATCH tab: Universe item (mode byte =
  Exhibition) -> screen 07 -> START -> 29 -> 1A in about 60 frames.
  Poking 1A or 29 into the screen id from the main menu does NOT work
  (No Data grid / stuck): the transition manager (0x800ECB00.., sub-state
  0x800A0248, target screen u16 0x801596C0) loads the screen's data.
- Watch vs 1P at Superstar Select: slot byte 0x800AAAD3 (slot 0 +3) is the
  controller before the picks: 1 human, 0 CPU (slot 1 +3 = 0). The PLAYER
  tab sets it; the 0x8015971C.. bytes alone only change the tab display.
- Superstar Select cursor: picker objects at 0x80159860 + 0x138*k, k = 26
  side 1, 27 side 2 (+4 s16 cell, +0x14 pointer to 0x88-byte cell records,
  record +0x62 = wrestler id). Picker init 0x801124D0 (a0 = k) starts on
  page u16 0x800AAAB6 / cell s16 0x800AAAB4. The game rewrites 0x800AAAB4
  every frame from the active cursor (0x801139C8), so side 2 would start
  where side 1 confirmed. The resident patches 0x801124D0 (menu overlay,
  RAM only; 6 words checked first) to j 0x80410028 while the director needs
  it, and sets page/cell for the side being initialised.
- Page layout table 0x80047150: [page][12] wrestler numbers, cells 0-8
  (pages 0-4 = SUPERSTARS 1-5; later entries use another layout).
- Cover fix: the footer text hook isn't called during the first frames of a
  freshly loaded menu (first Exhibition visit after boot showed the MATCH
  tab for ~6 frames). 0x80002DE4(a0, a1 = DL position, ...) writes every
  frame's final FullSync/EndDisplayList (F3DEX2 0xDF) and sends the gfx
  task; ROM 0x39E4 -> j 0x80410030, the resident appends the full-screen
  cover there while it is on.
- Backing out of Superstar Select (B) sets the transition target u16
  0x801596C0 to 07 (MATCH tab) and runs screen 29. Changing the target to
  03 lands on the Single Play page; the director then reopens Universe
  under the cover. Exit after a match is covered the same way (cover on
  from the 2 -> 3 swap until Universe is open).
