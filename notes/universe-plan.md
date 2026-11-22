# Universe Mode plan (agreed with the user, 2026-11-21)

This is the agreed plan. Follow it exactly; ask before changing anything in it.

## Matches = format + stipulation
- Formats: Single, Tag, Triple Threat, Handicap (2 on 1).
- Stipulations: Normal, Cage, Ladder, Ironman, Guest Referee.
- Any combination the game supports (check each mode's allowed formats while
  reverse-engineering; only book supported combinations; report the list to
  the user).
- Special matches: Royal Rumble (40-man), King of the Ring tournament.
- Game modes used: Exhibition (Single, Tag, Triple Threat, Handicap, Cage);
  Multi Play (Royal Rumble, Ladder Match, Ironman Match, Guest Referee,
  King of the Ring). Multi Play "Pay Per View" is NOT used.
- Card shows format + stipulation + title, e.g. "Tag Team Ladder Match, Tag
  Titles", "Triple Threat Ironman", and why it is booked (see Rankings).
- Play / Watch for every type, same feel as singles now: straight to
  character select with cursors on the right wrestlers, B backs out to
  Universe, Exit returns to Universe, Rematch replaces the result.

## Booking rules
- Weekly Raw and SmackDown: mostly Single and Tag, sometimes Triple Threat,
  Handicap or Cage; grown from rivalries and rankings. Raw leans slightly
  to big rivalry / World title main events, SmackDown slightly to
  secondary titles and tags (a tilt, not a rule).
- Royal Rumble PPV: 40-man Royal Rumble (winner gets a World title shot at
  WrestleMania) + title defences + ONE high-rivalry match.
- No Way Out: Cage only for the hottest rivalry, 50% chance.
- WrestleMania: biggest card, World title main event (singles or Triple
  Threat), a Ladder match for a title.
- Backlash, Fully Loaded, No Mercy, Armageddon: Cage possible but low
  chance (about 1 in 5 per show), never guaranteed.
- Judgment Day: Ironman only when a champion has a very high (near max)
  rivalry; otherwise none.
- Guest Referee: only when a third wrestler is a rival of every wrestler in
  the match; that wrestler is the referee. Any PPV.
- King of the Ring PPV: the whole 16-man tournament (8 first round, 4
  quarterfinals, 2 semifinals, final; later rounds fill from winners as
  they are played/simmed) + ONE title defence + ONE high-rivalry match.
  Winner crowned King, big momentum boost.
- SummerSlam: Ladder only for the hottest rivalry, 50% chance.
- Unforgiven: Triple Threat World title + a Handicap match.
- Survivor Series: card of regular 2-on-2 tag matches (no elimination),
  rival teams against each other, + a title match.
- Stipulation rules apply to whatever format the match is (tag rivalry ->
  Tag Ladder, three-way feud -> Triple Threat version, tag champions in a
  near-max rivalry at Judgment Day -> Tag Ironman).

## Roster and titles
- Every wrestler in the base game (read from the game's wrestler table),
  including women with the Women's title and a women's division (women's
  singles on Raw/SmackDown, Women's title defences).
- All 7 game belts: World, Intercontinental, European, Tag, Hardcore,
  Light Heavyweight, Women's (title matches put the real belt on the line).
- NO brand split (2000): one shared roster; any title on either show;
  the longer a title goes undefended the more likely a defence; World title
  gets the most main events.
- Non-regular wrestlers (Vince, Shane, Stephanie, Paul Bearer, Jim Ross,
  ...) are NOT in the normal Universe; they only fill Royal Rumble spots
  when injuries leave fewer than 40 healthy wrestlers. Chyna is in that
  fill-in group and is the only woman who can enter the Rumble.

## Rankings
- Roster ranking points from results: wins worth more vs higher-ranked,
  PPV wins double, losses cost points (upset losses more), streak bonus;
  weekly decay; injured wrestlers drop. Separate men / women / tag teams.
- Title contenders: top 5 per belt from eligible wrestlers (champion
  excluded). World: all men. IC / European: men, World champion excluded.
  Hardcore: men. Light Heavyweight: light wrestlers (game weight, about
  220 lbs or less). Women's: women. Tag: tag team ranking.
- Booking: title defence vs #1 contender or a top-5 rival of the champion;
  #1 Contender matches (#2 vs #3, winner becomes #1); regular matches pair
  wrestlers close in ranking or rivals; upsets move people fast; PPV main
  events from the World title picture and hottest rivalries.
- Card shows why: "World Title - Champion vs #1 Contender",
  "#1 Contender Match (Intercontinental)", "Rivalry".
- Rankings screen in Universe (L/R: men, women, tag teams, each belt with
  champion + top 5), styled like the Universe screen.

## Injuries
- Show weeks out: results "Injured: Kane, out 3 weeks"; an Injuries list
  (weeks left, counts down weekly). Injured wrestlers are never booked.

## Rivalries and allies (built on No Mercy's own system)
- Find where No Mercy stores each wrestler's ally / rival data (slots,
  partner field, strength) and report what is there before building on it.
- New Universe starts from the game's data: game rivals -> starting
  rivalries (first rival hotter); game allies -> ally levels; strong pairs
  / partners -> tag teams; factions -> high ally levels among members.
- Ally level per pair: grows with teaming, common rivals, run-in saves;
  fades when apart.
- High allies not in a tag team may form one ("A & B"), entering the tag
  ranking at the bottom with a debut tag match.
- Tag teams low in the tag ranking for several weeks (especially losing
  streaks) may split; a split may become a low-level rivalry; ally level
  resets. Tag champions never split.
- Before every match the Universe writes each wrestler's hottest rivals and
  strongest allies into the game's profile data (so the game's own run-ins
  / interference follow the story); the originals are restored afterwards
  (Exhibition and the save stay untouched).

## Order of work
1. Exhibition types: Tag, Triple Threat, Handicap, Cage (with the
   format + stipulation match model in the engine).
2. Ladder, Ironman, Guest Referee in every format the game supports.
3. Royal Rumble (40-man, fill-in rule).
4. King of the Ring (16-man card).
5. Full roster (women, all 7 belts, no brand split) with starting
   rivalries / allies / tag teams from the game's data.
6. Rankings, Rankings screen, Injuries list.
7. Alliances over time, tag teams forming / breaking up, rivals and allies
   written into the game before each match.
8. All booking rules above.
Test each step in the debug emulator, install, tell the user what to try.

## Supported combinations (found 2026-11-22)
- Exhibition (mode 0x02): Single, Tag, Triple Threat, Handicap, Cage (Cage
  is 1 on 1 only).
- Multi Play Ladder Match (mode 0x0E): Single, Tag, Triple Threat, Handicap.
- Multi Play Ironman Match (mode 0x0F): Single, Tag, Triple Threat, Handicap.
- Multi Play Guest Referee (mode 0x0D): Single, Triple Threat, Handicap (no Tag).
- So: Cage only for singles; Guest Referee never for tag matches.
