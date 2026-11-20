# No Mercy Universe Mode — Design

## Goal
A Universe mode that runs **inside WWF No Mercy (US v1.0)**: weekly shows,
PPVs, rosters, automatic booking, title histories, rankings, rivalries,
storylines, play-or-sim matches, persistent progression. The game's own
wrestling engine plays the matches; we only add the "management layer".

## Core idea: one engine, two homes
`engine/universe.c` is plain C with no libc, no malloc, no floats, fixed
arrays, and a hand-packed big-endian save format. That means:

- **Now:** it builds on the tablet and is tested by `make test`; the
  `build/universe` CLI stands in for the in-game menus.
- **Later:** the same file is cross-compiled for the N64's MIPS CPU and
  injected into the ROM. No rewrite.

## Weekly loop (same shape in the CLI and in the game)
```
u_book_show()            build the card from rankings/titles/rivalries
for each match:
    play it  -> game runs the match -> u_set_result(winner, method)
    or sim   -> u_sim_match()
u_apply_show()           records, momentum, titles, rivalries, injuries, week++
u_save()                 1178-byte fixed blob
```

## Current rules (PoC)
- **Ranking score** = overall + momentum + 2 × (wins − losses).
- **Booking:** each champion defends (vs rival if any, else #1 contender not
  tied up in a rivalry) → active rivalries by heat → rest paired by rank,
  preferring face vs heel. Up to 6 matches; most important match last.
- **Sim:** win chance 50% ± rating difference, clamped to 15–85%.
  Finish: 70% pin, 18% submission, 7% count-out, 5% DQ.
- **Titles** change only on pinfall/submission; count-out/DQ = retained
  (counted as a defense). History of 8 reigns per title.
- **Rivalries:** start 50% of the time after an upset or cheap finish;
  cheap finishes add heat; a clean win after 3+ weeks ends it; heat
  decays 15/week when not featured.
- **Injuries:** 4% chance loser misses 1–3 weeks.
- Deterministic: the RNG state lives in the save, same seed = same universe.

## Roadmap
| Phase | Work | Where |
|---|---|---|
| 1 ✅ | Engine + one weekly show, tests, CLI | tablet |
| 2 | Map demo roster to real No Mercy slots; read real names/stats from ROM | tablet |
| 3 | Engine features: PPV every 4th week, tag matches, multiple brands, draft/roster editing | tablet |
| 4 | Reverse engineering: RAM addresses for match setup (wrestlers, type, title) and match result (winner, method) | needs debugger |
| 5 | Code injection: MIPS toolchain, free ROM space, hook, call engine | tablet + emulator |
| 6 | In-game UI: Universe menu, card screen, play/sim choice | |
| 7 | Save integration: store the 1178-byte blob in cart save memory | |

## Open risks / questions
- **Debugging on Android:** phase 4 needs memory search/watchpoints. Options:
  RetroArch cheat search on the tablet (limited), or a PC emulator with a
  debugger (Project64 debugger, ares) for the heavy lifting.
- **Save space:** how much of No Mercy's cart save is free is unknown;
  1178 bytes may need shrinking (smaller name fields, IDs instead of names).
- **Code space / RAM:** need free ROM space and a safe RAM region for the
  engine (~1.2 KB state + code). Expansion Pak use is an option to check.
- **Interim fallback:** until phase 5, the CLI can act as a companion: book
  the card, set the matches up in Exhibition, enter results with `--play`.
