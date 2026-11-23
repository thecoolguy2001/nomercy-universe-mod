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

## Arenas (2026-11-21)
- ARENA tab cursor 0x80159E7D; copies 0x80159656, 0x8015971E and the
  match loader's 0x800AA99F (written by the director). Arenas: 0 Raw is
  War, 1 No Mercy, 2 SmackDown!, 3 King of the Ring, 4 SummerSlam,
  5 Survivor Series, 6 Royal Rumble, 7 WrestleMania, 8 Backlash,
  9 Armageddon (8/9 not listed on the tab but load fine when written).
- Preview pictures: table 0x80149558 = 6 asset ids (1-based) per arena
  (Raw 0x4641.., No Mercy 0x4665.., SmackDown 0x463B.., ...); each a 64x32
  CI8 TEX with its own 256-colour palette. Drawn by 0x80125B98: tile
  rectangles at 0x8014C268 (16 bytes: x0,y0,s0,t0,x1,y1,s1,t1), tiles 0-2
  = left column top to bottom, 3-5 = right column, each 64x32 stretched to
  72x36 (144x108 picture).
- The mod stores all 60 tiles (tools/make_arenas.py) at the end of the ROM,
  0x1FDA800 (moved from 0x1FD0000 on 2026-11-23 so the payload at
  0x1FB0000 has 0x2A800 bytes); the boot loader DMAs them to 0x80480000;
  the cover draws the show's arena.
- Display list buffers 0x80389880 / 0x8038C7F0 (0x2F70 bytes each); menus
  use < 2 KB.

## Exhibition match types (plan step 1, 2026-11-22)
- MATCH tab items: 0 Single, 1 Tag, 2 Triple Threat, 3 Handicap, 4 Cage
  (Cage = 1 on 1). A on the tab commits: copies 0x80159654 / 0x8015971A /
  0x80159798, u32 0x800AAAAC = 1 << item, u16 0x800AAAB2 = wrestlers to pick
  (2/4/3/3/2), and resets the PLAYER choice (START from the MATCH tab then
  does nothing for a changed type). The director highlights the item, presses
  A, sets the PLAYER value on the PLAYER tab (screen 06) and presses START.
- PLAYER value 0x80159FB5 = position in the type's full list (greyed
  multiplayer entries included). 1P / Watch: Single 1/3, Tag 5/7, Triple
  3/6, Handicap 3/5, Cage 1/3. Slot +3 before picks: 1 for the player's
  wrestler (and the CPU partner in Tag / Handicap 1P), else 0.
- Slots in pick order: Tag 0-1 vs 2-3; Triple one each; Handicap the pair
  (0-1) vs the lone wrestler (2). Pickers 26 / 27 (screens 1A / 1B) take
  turns; Tag partners are picked by the same picker back to back with no
  picker init (0x801124D0 only runs when the other picker starts), so the
  director moves the cursor for the partner (R/L to the page via u16
  0x800AAAB6, then picker cell +4 at 0x8015B810 / 0x8015B948).
- Results per slot 0x8015AF45 + 0x358*slot: 4 = won (both members of a
  winning team), 3 / 1 = lost, 0 = not involved in the finish (triple).
- Tag belt: SETUP_BELT 1 + champion bytes +2 / +3 (0x800A53E2 + 0x34) shows
  the "Tag Team Championship" intro.

## RULES, Multi Play modes, Guest Referee (plan step 2, 2026-11-22)
- Multi Play items (22-byte records at 0x8014D3E0, byte 19 = mode): 0
  Exhibition 0x02, ..., 4 Guest Referee 0x0D, 5 Ladder 0x0E, 6 Ironman 0x0F.
  Writing the mode into the Universe item opens that mode; its MATCH /
  PLAYER tabs use the same screens (07 / 06) and variables.
- Match-type word u32 0x800AAAAC: Exhibition 1 << item (Cage 0x10);
  Ladder 0x40 | format bit, Guest Referee 0x80 | (1 Single, 4 Triple,
  8 Handicap), Ironman 0x100 | format bit.
- PLAYER values: Ladder / Ironman as Exhibition (1P / Watch: Single 1/3,
  Tag 5/7, Triple 3/6, Handicap 3/5). Guest Referee: one choice with one
  controller, "REF. CPU vs CPU" (Single 3, Triple 6, Handicap 5); the
  referee is the first pick (Superstar Select labels it "1P Referee"),
  slot 0 +3 = 1.
- RULES live row 0x800931D8 (12 bytes): 0 rules slot (0 Single, 1 Tag,
  2 Triple, 3 Handicap, 4 Cage, 5 Ladder, 6-8 Guest Referee Single /
  Triple / Handicap, 9-12 Ironman Single / Tag / Triple / Handicap,
  13 = type bit 0x200), 1 time limit (0 5 min, 1 10, 2 15, 3 30, 4 60,
  5 none), 2 count out (0 10 counts, 1 20, 2 hardcore, 3 none), 3 pin,
  4 submission, 5 TKO, 6 rope break, 7 DQ, 8 bloodshed, 9 interference
  (0 yes, 1 no), 10 tag help time (0 5 s, 1 10 s, 2 20, 3 30, 4 60,
  5 no tag). Defaults: Exhibition types 05 03 00 00 01 00 01 00 00 (no
  limit, no count-out, no DQ); Ironman 04 01 00 00 01 00 00 00 00 (60 min,
  20 counts, DQ on), tag help 10 s for Ironman Tag. Cage and Ladder show
  only Time Limit and Bloodshed; Guest Referee has no Interference row,
  tag types show Tag Help Time instead of Interference.
- Opening the RULES tab (0x801353BC) fills the live row from the match
  type's saved rules (0x80134CA8 -> 0x800E40F0, unpacks into 0x800AB574).
  Nothing else writes it, so a match started without opening the tab uses
  whatever the live row holds.
- A on the loading screen first turns the match rules 0x800AB574 into the
  match settings (0x80137414 -> code at 0x801375B8..0x80137A7C: flags
  u32 0x800AAE80, time limit 0x800A139C from table 0x801515BC[TL], ...),
  then commits: 0x80135640(1) copies the live row to 0x800AB574 and, for
  0x800AAAA8 < 2, packs it into the save data as that match type's saved
  rules: jal 0x800E4A98(slot) at 0x8013576C -> (*0x800F7634) + 0x6E28 +
  4 * slot (14 slots, e.g. 0x802EBBF8, in the menu heap that the match
  reuses for decompressed data). A few frames later the save code
  (0x80004848: compare with the copy at 0x800634D8, queue 0x78-byte
  chunks at 0x80063CD8) writes the change to the cartridge.
  So a match started without opening RULES uses the stale 0x800AB574,
  and a new live row only applies to the next match.
- Universe: writes its row into both 0x800931D8 and 0x800AB574 before the
  match, makes the jal at 0x8013576C a nop while it runs a match (checked
  words around it: 0x24040004 before, nop after), and puts the player's
  two rows back when it returns to Universe. The saved rules are never
  touched. Verified: Ironman with time limit 0 counts down from 5:00.

## Match end: result bytes, falls, finish (2026-11-22)
- Per slot (stride 0x358): result 0x8015AF45 (4 won, 1/3 lost). Only
  valid on the match screen (2A): the game clears the result bytes a few
  seconds after the bell (still on 2A), and the result screen (1D) reuses
  the area.
- Ironman falls (the HUD counters), matched against HUD screenshots of a
  2-2 match: u32 per slot at 0x8015A738 (slot 0 low byte 0x8015A73B,
  slot 1 0x8015A73F); also bytes 0x80159ED0 (slot 0) / 0x80159ED3 (slot 1).
  Tag / triple ironman layout not checked yet.
- Ironman: a fall sets the pinned wrestler's result byte to 1 for a moment
  and adds 1 to the scorer's falls; nobody gets 4 until the end. A 0-0
  time-up ends with all result bytes 0. The clock counts down.
- Finish flags u32 0x800A261C, set at the bell, still there on the result
  screen: 0x2000 submission, 0x4000 pin, 0x8000 draw (time limit),
  0x10000 TKO, 0x20000 DQ, 0x80000 / 0x100000 / 0x200000 / 0x400000
  count out, 0x800 / 0x40000 / 0x1000000 / 0x2000000 no caption. The result
  screen calls 0x8010B3F8(flags) -> u32 0x801597C0 = caption + 1; caption
  text id 0xF1AF + value through 0x800DEC70 (ids 0xF0CE.. -> pointer table
  0x800B8458): table 0x800F4B18 = Draw, Submit, Pin, Draw, TKO, Count out
  x5, TKO, "", DQ, "", "". Seen: pin 0x4412, time-limit draw 0x9400,
  ladder climbed 0x40412 (bit 0x40000).
- Match record 0x800A99F0 + 12 * (s16 0x800A9CB4 - 1): u16 minutes, u16
  seconds, ..., u16 flags at +10 (same value as 0x800A261C).
- Universe records every match when the result screen appears: result
  bytes and falls as last seen on the match screen, finish from the flags,
  no winner = draw.

## Royal Rumble mode (plan step 3, 2026-11-22)
- Multi Play item 1 = Royal Rumble, mode 0x04 (item 2 Pay Per View 0x06,
  item 3 King of the Ring 0x08). Tabs: ORDER (screen 07; 0 Random,
  1 Select, 2 Team Battle), NUMBER (screen 09), PLAYER (06), ARENA (05),
  RULES (15). Match-type word bit 0x200, rules slot 13; default RULES row
  0D 05 02 00 00 01 00 01 00 00 00 01 (count out row: Lose / Hardcore / No).
- NUMBER: the tab object (pointer at 0x8015A36C) byte +0x4E = competitors / 4
  (4..40, steps of 4; +0x4C / +0x4D follow it); A on the tab (0x8012FF24)
  sets u16 0x800AAABC = 4 * it, START copies it to the pick count 0x800AAAB2.
  Writing 10 there gives a 40-man Rumble ("1/40" on Superstar Select).
- PLAYER values: 3 / 4 / 5 / 6 = 1P as entrant 1 / 2 / 3 / 4 (rest CPU),
  7 = Watch.
- ORDER Select: every entrant picked on Superstar Select (duplicates
  allowed). ORDER Random: the first four are picked, the game fills the rest.
- Entry order: u16 wrestler ids at 0x800BEA00 (2 * competitors bytes); the
  first four are also the setup slots 0x800AAAD0. Filled when the picks are
  done; read during the match.
- The setup slots' ids alone do not change who wrestles: overwriting them
  after the picks changed the models but the names stayed the picked ones
  (loading screen and entrance captions), so the first four must be real
  picks (Universe presets the cursors).
- Superstar Select page table 0x80047150 [page][12]: pages 0-4 SUPERSTARS
  1-5 (45 men), 5 LADIES, 6 HIDDEN 1, 7-9 numbers 65-82 (locked on a fresh
  save; R skips them). Wrestler records (4 attires x 20 bytes): +0 id,
  +8 u16 weight - 100, +0x0A / +0x0C name text ids.
- Entrants 5+ come in only when someone is eliminated (four in the ring at
  most); the HUD's top number counts those not eliminated yet. CPU Rumbles
  are slow (a 4-man one went ~15 game minutes).
- Writing the entry order works: with 0x800BEA08.. = Austin, Undertaker,
  Kane, Mankind, Steve Austin came in as entrant 5 (original ROM test).
  A run with hidden number 59 as entrant 5 froze at the first elimination
  (locked hidden wrestlers may not load - checking).
- Per place (stride 0x358): u16 id of who is in it at 0x8015AF50 (result
  byte + 0x0B); in a Rumble it changes as entrants replace the eliminated.
  The 4-man Rumble ended with result bytes 1 / 4 and finish flags 0x4012
  (pin); the winner is whoever holds the place with the 4.
- A result byte of 4 also appears mid-Rumble on whoever made an
  elimination (urr8: 02 / 04 after the first elimination); the winner is the
  place whose byte turns 4 last (urr4b end: 03 04 03 03). Universe tracks
  new 4s and reads that place's id.
- The Royal Rumble's result screen is 0x1C (not 0x1D); Exit there goes to
  the Multi Play page (02) like the others.

## King of the Ring mode (plan step 4, 2026-11-22; not used)
- Multi Play item 3, mode 0x08. Tabs: MATCH (Simple Match, Tag Match, Cage
  Match, Ladder: Single, Ladder: Tag), NUMBER ("Number of Superstars",
  3-16, default 16), ARENA, RULES (Time Limit, Bloodshed); no PLAYER tab.
- START: Superstar Select for every entrant ("1/16"), then a bracket
  screen (0x0A) with CPU / MAN per entrant and Decision / All Player /
  All CPU; the game then plays the whole bracket.
- The user chose match-by-match tournaments instead (plan updated):
  Universe books the bracket and each match is a normal Exhibition single.

## The roster (plan step 5, 2026-11-23)
- Wrestler profiles (names), menu heap: number n at 0x8031A398 +
  0x138 * (n - 1), 4 attires x 0x4E bytes: +0 full name, +0x15 short name,
  +0x48 u16 weight - 100 (500+ = not given: women, non-wrestlers, Crash).
  Superstar Select draws the name from there. Numbers 1-49 men (49 attire 2
  Funaki), 50 Chyna, 51 Stephanie, 52 Tori, 53 Terri / Debra, 54 Trish,
  55 Lita, 56 Ivory / Jacqueline, 57 Linda, 58 The Kat, 59 Mae Young /
  Fabulous Moolah, 60 Ho, 61 Vince, 62 Shane, 63 Earl Hebner / Howard Finkel
  / Paul Bearer / Michael Cole, 64 Jim Ross / Jerry Lawler / Pat Patterson
  / Gerald Brisco (attires 1-4); 65-82 edit slots (empty).
- Attire ("Type"): u8 0x800AAAB9 (set before a picker starts, follows
  C-Right / C-Left); a pick stores it at setup slot + 2.
- Superstar Select pages, live records at 0x800AAEC0 + 56 * page: 9 x
  (u16 id, u16 number), u16 count at +0x24, name at +0x26. 0-4 SUPERSTARS,
  5 LADIES (51 52 54 58 53 55 56), 6 HIDDEN 1 (61 62 57 63 64 59 60),
  7 HIDDEN 2 (9 7 8 25 46), 8-9 SUPERSTARS 6-7 (empty), 10-11 EDIT 1-2.
  Live page u16 0x800AAAB6. R / L skip pages where nobody can be picked
  (0x801386EC(page)); 0x801385FC(id) is the per-wrestler unlock test
  (hidden wrestlers locked on a fresh save). Locked wrestlers still load
  as Royal Rumble entrants written into the entry list.
- Champions table 0x800A53E0 + 0x34 * belt: +2 champion's number, +3 tag
  partner; all zero on a fresh save.

## Allies and rivals (plan step 5, 2026-11-23)
- Smackdown Mall > Superstar Options > Edit a Superstar > ALLY/ENEMY:
  Rival 1 (50%), Rival 2 (30%), Rival 3 (20%), Accompanied by ("Not able
  to edit" on the built-in wrestlers). Superstar Options also has Change
  Stable and Stables Name (stable data not located yet).
- Data: bit-packed records, 0xD8 bytes per wrestler, number n at
  0x80320A60 + 0xD8 * (n - 1) (menu heap, right after the profiles),
  unpacked MSB-first by 0x8000644C (bit reader 0x800061E4) into a
  structure where u16 +0x28 / +0x2A / +0x2C are rival 1-3 and +0x2E the
  "accompanied by", all as wrestler ids (0 = none). Field getter 0x80007948.
  Decoded for all 64 (e.g. The Rock: Triple H, Chris Benoit, Undertaker;
  Edge with Christian, the Hardys / Dudleys / Too Cool with each other,
  Steven Richards with Bull Buchanan, Eddie Guerrero with Chyna, Triple H
  with Stephanie, X-Pac with Tori, Test / Val Venis / Albert with Trish).
- Stables: Superstar Options "Change Stable" is the Superstar Select grid
  (L / R pages) for moving wrestlers between pages, and "Stables Name"
  renames pages: the game's "stables" are the select pages (default names
  SUPERSTARS 1-7, LADIES, HIDDEN 1-2, EDIT 1-2), not factions - page 1
  holds The Rock, Triple H and Steve Austin together. No faction data.
- Bit layout of the packed record (MSB first): rival 1-3 are 12-bit
  wrestler ids at bits 149, 161, 173; "accompanied by" at bit 185 (12
  bits); 247 bits are read in all.
- Universe use (step 5): host/roster.c REL[] is this data for numbers
  1-64; rivalry heat = 15 + (weight a gives b + weight b gives a) / 2
  with weights 50 / 30 / 20 (mutual Rival 1 = 65); mutual "accompanied by"
  = ally 90, one-sided 75, same companion 50 (+20 with a rival in common);
  men at ally 70+ form the starting tag teams.

## Rivals and allies in the match (plan step 7, 2026-11-23)
- Built-in wrestler n's 0xD8-byte block: *(u32 *)0x80047F34 + 0xD8 * n
  (0x803208D0 in menus and while a match loads; edit slots use
  *(u32 *)0x80047F3C). 0x8000AF08(id) -> index, 0x8000B5F0(index) -> block.
  The relation record (rival 1-3, accompanied by; see above) is the
  block's last 0x20 bytes, +0xB8 (so The Rock's is 0x80320A60).
- Match load (A on the loading screen) copies the whole block of each
  wrestler into the setup slot (0x80099514 + 0x114 * slot, 0x8000BB68) and
  unpacks the relation record from the block itself (bit reader
  0x800061E4, ~650-1650 frames later, one wrestler after another) - so
  values written before the loading screen reach the match.
- The relation bits match the ROM (records at ROM 0x28EB7E, 0xD8 apart);
  the flash save holds none of them (searched every save file).
- The resident writes Universe rivals / companion at director start and
  restores the bytes at director end (only if they are still the written
  ones). Verified: written on Superstar Select, all 64 records equal the
  game's again after backing out.
- "Accompanied by" does not bring anyone out at the entrance (Eddie
  Guerrero / Chyna in the game's data, Mr. Ass / Road Dogg written): its
  in-match use is the game's interference logic (not observed in short
  test matches).

## The Universe save (2026-11-23)
- No Mercy's flash save is its own log-structured file system (main code
  0x80003D64-0x80004F4C, ROM = RAM - 0x7FFFF400): 128-byte pages = u16
  previous page, u16 logical page (0x4000 = superblock), u32 checksum (XOR
  of the page's words), 120 data bytes. The save image (RAM pointer
  0x80062CC0, size 0x80062CC4 = 29040 bytes = 242 logical pages, up to 320)
  is written page by page; a superblock (magic 0x00080721, the game's two
  8-byte IDs, u64 sequence at +8) ends each save. Pages form a ring: write
  pointer 0x80064510, chain head 0x8006450C / tail 0x8006450E; a sector is
  erased when first written; incremental saves (0x80004918, changed pages
  only - an unchanged image writes nothing) go on while within 4 sectors of
  the head, else a full rewrite (0x80004618) from the sector after the tail
  (up to 4 tries). Mount (0x800043E8) scans every page for the newest
  superblock and follows the chain back (0x80004254). A blank flash is
  formatted (0x80004C68: osFlashChipErase, then a superblock). Init
  0x80004BB4 (osFlashInit once: flag 0x800436E0), save 0x80004E6C.
  Helpers: 0x80003E90 page -> sector, 0x80003EA4 sector -> first page
  ((s*128) % 1023), 0x80003EE4 sector distance from the head (8 sectors).
- osFlash* (one caller each, all in that module): init 0x8002FF60, status
  0x80030070, chip erase 0x80030170, sector erase 0x800302B0, write buffer
  0x80030400, write array 0x800304A0, read array 0x800305F0. The game's IO
  message 0x800644D0, queue 0x800644F0 (message 0x80064508).
- The game's flash use is the whole chip (a ring over pages 0-1021), so the
  ROM shrinks it to sectors 0-5 (pages 0-767; tools/build_rom.py
  FLASH_PATCHES: the five 0x3FE limits -> 0x300, sector start % 767, 6
  sectors, incremental within 3 sectors so a full rewrite (243 pages) still
  fits in 2, format erases sectors 0-5 via the resident instead of the chip)
  and the Universe has sectors 6-7: two copies in turn (n64/resident/
  usave.c), data pages then a header page (magic 'NMUV', sequence, length,
  FNV-1a), through the game's own osFlash routines. Verified: a fresh save
  formats and saves in pages 0-243; ~330 forced game saves (test hook
  mailbox 0x3C) wrapped the 768-page ring with full rewrites and no
  failure, never touched pages 768+, and the game mounted the newest
  superblock after a reboot; the Universe (and its card) loads back.
- The ROM also has the Controller Pak file system (osPfs*, 0x80038C70-
  0x8003BFFF, kept by a stub at 0x80005E78; never used by the game), the
  Rumble Pak code (osMotorInit 0x80039A74) and Transfer Pak code
  (0x80005000-0x80005530).

## Created wrestlers (2026-11-23)
- Smackdown Mall > Superstar Options > Create a Superstar > Save Data
  Select (Data 1-18) > the editor (tabs Profile/Music, Appearance, Moves,
  Fighting Style, Parameter, Ally/Enemy); START = Decision, then "save this
  superstar?" (cursor on No). Appearance > Body > A opens the body list
  with a Male/Female selector (D-pad right asks "change superstar gender?";
  the editor resets).
- Data n is game number 64 + n (65-82), picked from the EDIT 1 / EDIT 2
  Superstar Select pages (ids 0x0A01+). Its profile is in the same array
  (0x8031A398 + 0x138 * (n - 1)): an empty slot has an empty name (the
  default name is "??????"); +0x48 u16 weight - 100.
- Gender: profile +0x31 (per attire, 0x4E apart) & 0x1F is the body type:
  0-12 male, 13-21 female - true for every built-in character too.
- The edit wrestlers' 0xD8-byte blocks (*(u32 *)0x80047F3C) hold a template
  until used; their ALLY/ENEMY is not rewritten by Universe (built-ins only).
