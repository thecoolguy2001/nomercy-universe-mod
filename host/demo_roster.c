/*
 * Demo universe: two brands of 12, four tag teams, four titles.
 * Shared by the host CLI, the tests and the N64 build.
 * Overall ratings are placeholders, NOT read from the ROM yet. game_slot is
 * U_NONE until each wrestler is mapped to their No Mercy roster slot.
 */
#include "demo_roster.h"

#define R U_BRAND_RAW
#define S U_BRAND_SMACKDOWN

static const struct {
    const char *name;
    uint8_t brand, align, overall;
} ROSTER[] = {
    /* 0 */ {"The Rock",       R, U_FACE, 92},
    /* 1 */ {"Triple H",       R, U_HEEL, 91},
    /* 2 */ {"Kurt Angle",     R, U_HEEL, 88},
    /* 3 */ {"Kane",           R, U_HEEL, 85},
    /* 4 */ {"Bull Buchanan",  R, U_HEEL, 80},
    /* 5 */ {"Rikishi",        R, U_FACE, 78},
    /* 6 */ {"Edge",           R, U_HEEL, 77},
    /* 7 */ {"Christian",      R, U_HEEL, 75},
    /* 8 */ {"Bubba Ray",      R, U_FACE, 76},
    /* 9 */ {"D-Von",          R, U_FACE, 74},
    /*10 */ {"Val Venis",      R, U_HEEL, 73},
    /*11 */ {"X-Pac",          R, U_HEEL, 73},
    /*12 */ {"Stone Cold",     S, U_FACE, 93},
    /*13 */ {"Undertaker",     S, U_FACE, 89},
    /*14 */ {"Chris Jericho",  S, U_FACE, 86},
    /*15 */ {"Chris Benoit",   S, U_HEEL, 86},
    /*16 */ {"Eddie Guerrero", S, U_HEEL, 82},
    /*17 */ {"Tazz",           S, U_HEEL, 78},
    /*18 */ {"Jeff Hardy",     S, U_FACE, 76},
    /*19 */ {"Matt Hardy",     S, U_FACE, 74},
    /*20 */ {"Road Dogg",      S, U_HEEL, 74},
    /*21 */ {"Billy Gunn",     S, U_HEEL, 75},
    /*22 */ {"Test",           S, U_FACE, 72},
    /*23 */ {"Mankind",        S, U_FACE, 84},
};

/* Names exactly as No Mercy shows them on Superstar Select, by wrestler id
 * (used to pick the right wrestler when a match is played in the game). */
static const char *const GAME_NAMES[] = {
    "THE ROCK", "TRIPLE H", "KURT ANGLE", "KANE", "BULL BUCHANAN", "RIKISHI",
    "EDGE", "CHRISTIAN", "BUH BUH RAY DUDLEY", "D-VON DUDLEY", "VAL VENIS", "X-PAC",
    "STEVE AUSTIN", "UNDERTAKER", "CHRIS JERICHO", "CHRIS BENOIT", "EDDIE GUERRERO",
    "TAZZ", "JEFF HARDY", "MATT HARDY", "ROAD DOGG", "MR. ASS", "TEST", "MANKIND",
};

/* No Mercy's own wrestler number for each roster id (1-based index into the
 * game's wrestler table at 0x800471C4; also used by the champions table). */
static const uint8_t GAME_INDEX[] = {
    1, 10, 19, 4, 47, 26, 29, 30, 36, 35, 24, 11,
    2, 3, 18, 14, 17, 20, 32, 31, 13, 12, 21, 6,
};

/* Fill-ins (plan: non-regulars who only fill Royal Rumble spots). Only
 * Chyna's game number is confirmed so far; the McMahons, Paul Bearer, Jim
 * Ross and the others are hidden wrestlers whose numbers come with the full
 * roster (plan step 5). */
static const struct {
    const char *name;
    uint8_t game_index, overall;
} FILLINS[] = {
    {"Chyna", 50, 74},
};
#define N_ROSTER (sizeof ROSTER / sizeof ROSTER[0])

uint8_t demo_game_index(uint8_t id)
{
    if (id < sizeof GAME_INDEX) return GAME_INDEX[id];
    if (id - N_ROSTER < sizeof FILLINS / sizeof FILLINS[0]) return FILLINS[id - N_ROSTER].game_index;
    return 0;
}

const char *demo_game_name(uint8_t id)
{
    return id < sizeof GAME_NAMES / sizeof GAME_NAMES[0] ? GAME_NAMES[id] : "";
}

void demo_universe(Universe *u, uint32_t seed)
{
    unsigned i;
    int ec, dud, hardys, nao;

    u_init(u, seed);
    for (i = 0; i < sizeof ROSTER / sizeof ROSTER[0]; i++)
        u_add_wrestler(u, ROSTER[i].name, U_NONE, ROSTER[i].brand,
                       ROSTER[i].align, ROSTER[i].overall);

    ec     = u_add_team(u, "Edge&Christian", 6, 7);
    dud    = u_add_team(u, "Dudley Boyz", 8, 9);
    hardys = u_add_team(u, "Hardy Boyz", 18, 19);
    nao    = u_add_team(u, "New Age Outlaws", 20, 21);
    (void)dud; (void)hardys; (void)nao;

    for (i = 0; i < sizeof FILLINS / sizeof FILLINS[0]; i++)
        u_add_fillin(u, FILLINS[i].name, U_NONE, FILLINS[i].overall);

    u_add_title(u, "WWF Title", R, U_TITLE_SINGLES, 1);        /* Triple H */
    u_add_title(u, "IC Title", S, U_TITLE_SINGLES, 14);        /* Chris Jericho */
    u_add_title(u, "Tag Team Titles", R, U_TITLE_TAG, (uint8_t)ec);
    u_add_title(u, "European Title", S, U_TITLE_SINGLES, 16);  /* Eddie Guerrero */

    u_add_rivalry(u, 0, 2, 60);   /* The Rock vs Kurt Angle */
    u_add_rivalry(u, 12, 1, 50);  /* Stone Cold vs Triple H: cross-brand, PPV only */
}
