/*
 * No Mercy Universe engine - core state and rules.
 *
 * Designed to run unchanged in two places:
 *   1. On a host machine for development and automated tests.
 *   2. Inside the N64 ROM (n64/), called from the boot-time Universe screen.
 *
 * Rules for this file and universe.c so they stay N64-friendly:
 *   - No malloc, no stdio, no libc calls. Fixed-size arrays only.
 *   - No floating point. Integer math only (deterministic everywhere).
 *   - Save data is packed byte-by-byte in big-endian order, never by
 *     dumping structs, so host and N64 saves are identical.
 */
#ifndef UNIVERSE_H
#define UNIVERSE_H

#include <stdint.h>

#define U_MAX_WRESTLERS  32
#define U_MAX_TEAMS      8
#define U_MAX_TITLES     6
#define U_MAX_REIGNS     8   /* title history kept per title (ring buffer) */
#define U_MAX_RIVALRIES  8
#define U_MAX_MATCHES    8
#define U_WEEKLY_MATCHES 6
#define U_PPV_MATCHES    8
#define U_PPV_EVERY      4   /* a PPV closes every 4th week */
#define U_NAME_LEN       16  /* includes terminating zero */

#define U_NONE           0xFF
#define U_OPEN_WEEK      0xFFFF  /* reign still in progress */

enum { U_BRAND_RAW = 0, U_BRAND_SMACKDOWN = 1, U_BRAND_COUNT = 2 };
enum { U_FACE = 0, U_HEEL = 1 };
enum { U_SHOW_WEEKLY = 0, U_SHOW_PPV = 1 };
enum { U_TITLE_SINGLES = 0, U_TITLE_TAG = 1 };
/* A match is a format plus a stipulation (notes/universe-plan.md). */
enum {
    U_FMT_SINGLE = 0,       /* 1 vs 1 */
    U_FMT_TAG,              /* 2 vs 2 (teams) */
    U_FMT_TRIPLE,           /* 1 vs 1 vs 1 */
    U_FMT_HANDICAP,         /* 1 (side 0) vs 2 (side 1) */
    U_FMT_COUNT
};

/* Stipulations map onto match types No Mercy already has. */
enum {
    U_STIP_NORMAL = 0,
    U_STIP_CAGE,
    U_STIP_LADDER,
    U_STIP_IRONMAN,
    U_STIP_REFEREE,         /* guest referee: UMatch.referee */
    U_STIP_NODQ,            /* no count-outs, no disqualifications */
    U_STIP_COUNT
};

enum {
    U_WIN_PINFALL = 0,
    U_WIN_SUBMISSION,
    U_WIN_COUNTOUT,
    U_WIN_DQ,
    U_WIN_SPECIAL,      /* escaped the cage / climbed the ladder */
    U_WIN_FALLS,        /* Ironman: most falls (UMatch.falls) */
    U_WIN_DRAW,         /* no winner (UMatch.winner = U_NONE): time limit,
                           double count-out, Ironman level on falls */
    U_WIN_METHOD_COUNT
};

enum { U_SRC_SIMULATED = 0, U_SRC_PLAYED = 1 };

typedef struct {
    char     name[U_NAME_LEN];
    uint8_t  game_slot;     /* No Mercy roster slot; U_NONE until mapped */
    uint8_t  brand;
    uint8_t  align;         /* U_FACE / U_HEEL */
    uint8_t  overall;       /* 0..100 base ability */
    int8_t   momentum;      /* -100..100, changes with results */
    uint8_t  injured_weeks; /* 0 = healthy */
    uint8_t  active;        /* 0 = released / retired */
    uint16_t wins;
    uint16_t losses;
} UWrestler;

typedef struct {
    char     name[U_NAME_LEN];
    uint8_t  a, b;          /* member wrestler ids */
    uint8_t  active;
    uint16_t wins;
    uint16_t losses;
} UTeam;

typedef struct {
    uint8_t  holder;        /* wrestler id, or team id for tag titles */
    uint8_t  defenses;
    uint16_t won_week;
    uint16_t lost_week;     /* U_OPEN_WEEK while current */
} UReign;

typedef struct {
    char     name[U_NAME_LEN];
    uint8_t  brand;
    uint8_t  kind;          /* U_TITLE_SINGLES / U_TITLE_TAG */
    uint8_t  active;
    uint8_t  vacant;
    uint8_t  reign_count;   /* total reigns ever (history wraps at U_MAX_REIGNS) */
    UReign   history[U_MAX_REIGNS];
} UTitle;

typedef struct {
    uint8_t a, b;           /* wrestler ids (rivalries are singles feuds) */
    uint8_t intensity;      /* 0 = inactive, 100 = red hot */
    uint8_t weeks;          /* shows the rivalry has been featured on */
} URivalry;

typedef struct {
    /* booking (filled by u_book_show) */
    uint8_t format;         /* U_FMT_* */
    uint8_t stip;           /* U_STIP_* */
    uint8_t side[3][2];     /* wrestler ids, U_NONE where unused: single and
                               triple use side[x][0]; tag both of each side;
                               handicap side 0 alone, side 1 two wrestlers */
    uint8_t team[3];        /* team ids in tag matches, else U_NONE */
    uint8_t referee;        /* guest referee (U_STIP_REFEREE), else U_NONE */
    uint8_t title;          /* title index or U_NONE */
    uint8_t rivalry;        /* rivalry index or U_NONE */
    /* result (filled by u_sim_match or u_set_result) */
    uint8_t done;
    uint8_t winner;         /* winning side: 0..u_match_sides()-1, U_NONE for a draw */
    uint8_t method;
    uint8_t source;
    uint8_t falls[3];       /* Ironman: falls scored by each side */
    /* consequences (filled by u_apply_show, for display) */
    uint8_t title_changed;
    uint8_t rivalry_started;
    uint8_t rivalry_ended;
    uint8_t injured;        /* wrestler id or U_NONE */
    uint8_t injury_weeks;
} UMatch;

typedef struct {
    uint16_t week;
    uint8_t  kind;          /* U_SHOW_WEEKLY / U_SHOW_PPV */
    uint8_t  brand;         /* U_NONE for a PPV (both brands) */
    uint8_t  count;
    UMatch   m[U_MAX_MATCHES]; /* card order: m[count-1] is the main event */
} UShow;

typedef struct {
    uint16_t  week;
    uint8_t   slot;         /* 0 = Raw, 1 = SmackDown, 2 = PPV */
    uint32_t  rng;
    uint8_t   n_wrestlers;
    uint8_t   n_teams;
    uint8_t   n_titles;
    UWrestler w[U_MAX_WRESTLERS];
    UTeam     tm[U_MAX_TEAMS];
    UTitle    t[U_MAX_TITLES];
    URivalry  r[U_MAX_RIVALRIES];
} Universe;

/* Fixed save size so it can be given a fixed region of save memory. */
#define U_SAVE_HEADER_BYTES   14
#define U_SAVE_WRESTLER_BYTES (U_NAME_LEN + 11)
#define U_SAVE_TEAM_BYTES     (U_NAME_LEN + 7)
#define U_SAVE_TITLE_BYTES    (U_NAME_LEN + 5 + U_MAX_REIGNS * 6)
#define U_SAVE_SIZE (U_SAVE_HEADER_BYTES \
                     + U_MAX_WRESTLERS * U_SAVE_WRESTLER_BYTES \
                     + U_MAX_TEAMS * U_SAVE_TEAM_BYTES \
                     + U_MAX_TITLES * U_SAVE_TITLE_BYTES \
                     + U_MAX_RIVALRIES * 4 + 2)

/* ---- setup / roster management ---- */
void    u_init(Universe *u, uint32_t seed);
int     u_add_wrestler(Universe *u, const char *name, uint8_t game_slot,
                       uint8_t brand, uint8_t align, uint8_t overall);
int     u_add_team(Universe *u, const char *name, uint8_t a, uint8_t b);
int     u_add_title(Universe *u, const char *name, uint8_t brand, uint8_t kind,
                    uint8_t holder);
int     u_add_rivalry(Universe *u, uint8_t a, uint8_t b, uint8_t intensity);
int     u_set_brand(Universe *u, uint8_t id, uint8_t brand);   /* titles follow */
int     u_set_align(Universe *u, uint8_t id, uint8_t align);
int     u_set_overall(Universe *u, uint8_t id, uint8_t overall);
int     u_set_active(Universe *u, uint8_t id, uint8_t active); /* vacates titles */

/* ---- queries ---- */
uint8_t u_title_holder(const Universe *u, uint8_t title);
int     u_score(const UWrestler *w);
int     u_team_score(const Universe *u, uint8_t team);
int     u_rankings(const Universe *u, uint8_t brand, uint8_t *out, int max);
int     u_team_rankings(const Universe *u, uint8_t brand, uint8_t *out, int max);
uint32_t u_rand(Universe *u, uint32_t n);
void    u_next_show(const Universe *u, uint8_t *kind, uint8_t *brand);
const char *u_show_name(const Universe *u);  /* "Raw Is War", "WrestleMania"... */
const char *u_stip_name(uint8_t stip);
const char *u_format_name(uint8_t format);
int     u_match_sides(const UMatch *m);      /* 3 for a triple threat, else 2 */
/* Text for a side ("Rock", "Edge & Christian", "Kane & X-Pac"), and for a
 * whole match: "A vs B vs C", or with winner set "A def. B & C". Always
 * zero-terminated within cap; returns the length. */
int     u_side_text(const Universe *u, const UMatch *m, int side, char *buf, int cap);
int     u_match_text(const Universe *u, const UMatch *m, int result, char *buf, int cap);
const char *u_method_name(uint8_t method, uint8_t stip);
int     u_method_allowed(uint8_t stip, uint8_t method);
/* Format + stipulation combinations No Mercy can stage (notes/universe-plan.md):
 * Cage is 1 on 1 only, Guest Referee is never a tag match. */
int     u_stip_supported(uint8_t format, uint8_t stip);

/* ---- show loop: book -> (play or sim each match) -> apply ---- */
void    u_book_show(Universe *u, UShow *s);
void    u_sim_match(Universe *u, UMatch *m);
int     u_set_result(UMatch *m, uint8_t winning_side, uint8_t method);
int     u_set_draw(UMatch *m);
/* falls[k] for each of the u_match_sides() sides; level at the top = draw */
int     u_set_ironman_result(UMatch *m, const uint8_t *falls);
void    u_apply_show(Universe *u, UShow *s);

/* ---- persistence: bytes written / 0 on success, negative on error ---- */
int     u_save(const Universe *u, uint8_t *buf, int cap);
int     u_load(Universe *u, const uint8_t *buf, int len);

#endif
