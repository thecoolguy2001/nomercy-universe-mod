# No Mercy Universe Mode — Design

## Goal
A Universe mode that runs **inside WWF No Mercy (US v1.0)**: weekly shows,
PPVs, rosters, automatic booking, title histories, rankings, rivalries,
storylines, play-or-sim matches, persistent progression. The game's own
wrestling engine plays the matches; we only add the "management layer".

## One engine, two homes
`engine/universe.c` is plain C with no libc, no malloc, no floats, fixed
arrays, and a hand-packed big-endian save (1510 bytes, version 2).
- Host: `make test` (216k checks) and the `build/universe` CLI.
- N64: `make rom` cross-compiles the same file with clang/lld into the ROM.

## How the ROM build works (`make rom`, `make install`)
| Piece | Where |
|---|---|
| Loader stub (80 bytes, `n64/stub.S`) | replaces entry code at ROM 0x1000 / RAM 0x80000400 |
| Payload (~35 KB: engine + UI + font) | ROM 0x1FB0000, in the 0xFF padding that starts at 0x1FA6B70 |
| Payload RAM window | 0x80060000–0x80090000 code/data, 0x80090000 framebuffer, stack top 0x800BE000 — all inside the game's BSS (0x80056710–0x800BEE90) |
| Persistent universe | 0x807F0000 (Expansion Pak RAM), FNV-checksummed, survives reset |
| Boot checksum | CIC-6102, recomputed by `tools/build_rom.py` |

Boot flow: stub DMAs the payload into the BSS area → Universe screen →
on exit the payload copies the **original** entry code back to 0x80000400
and jumps there; it clears the BSS (wiping the payload) and boots No Mercy
untouched. No Expansion Pak needed to run it; without one, progress is lost
when the game starts.

Played matches: Universe screen → pick match → START → set it up in
Exhibition → play → **reset** → the screen asks who won and how.

`tools/mini_n64.py` is a small MIPS test harness (not an emulator) that
runs the patched ROM's boot path with scripted buttons and saves
screenshots, used to verify the payload without an emulator.

## Rules
- Calendar: Raw, SmackDown each week; PPV after SmackDown every 4th week
  (Royal Rumble … Armageddon, 12-PPV cycle).
- Booking: singles titles (PPV: always defended; TV: 1 in 3 title match,
  else champion vs contender non-title) → tag titles → rivalries (PPV
  feuds get No DQ / Steel Cage / Ladder by heat) → one tag match → rest by
  ranking, face vs heel preferred. Vacant titles: top two contenders.
- Titles change only on pinfall/submission/escape/ladder; count-out/DQ =
  retained (a defense). Belts move to the new champion's brand.
- Rivalries: start after upsets/cheap finishes (50%); cheap finishes add
  heat; a clean win ends them at a PPV or after 4 shows; decay 10/show.
- Injuries: 4% per match, 1–3 weeks, counted in weeks.
- Roster: draft (titles follow), turn, rating, release (vacates titles,
  disbands teams), rehire.

## Roadmap
| Phase | Work | Status |
|---|---|---|
| 1 | Engine + weekly show + tests | done |
| 2 | Brands, PPVs, tag teams/titles, stipulations, roster management | done |
| 3 | Universe screen inside the ROM (boot-time), play/sim/report loop | done (needs emulator test) |
| 4 | Full No Mercy roster incl. unlockables; real roster slot ids | next |
| 5 | Unlock everything in the modded ROM | needs RE: unlock flags in save/RAM + a hook after save load |
| 6 | Menu music (Undertaker theme) on the Universe screen | needs RE of the game's audio format + an audio driver |
| 7 | Auto set-up of matches and auto-read of results (no manual reporting) | needs RE of match setup/result RAM |
| 8 | Universe entry in the game's own menus; universe in cart save | needs RE of menu code and save layout |

## Open risks
- Untested on a real emulator yet (only in the harness). Most likely
  sticking points: emulator soft-reset clearing RAM (then results can't be
  reported), or the VI mode on a particular video plugin.
- Unknown whether No Mercy touches 0x807F0000 when an Expansion Pak is
  present (it appears not to read the memory size at all).
