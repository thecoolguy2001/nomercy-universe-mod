/*
 * Demo roster for the proof of concept: one brand, 16 wrestlers, 2 titles.
 * Overall ratings are placeholders, NOT read from the ROM yet. game_slot is
 * U_NONE until each wrestler is mapped to their No Mercy roster slot.
 */
#include "demo_roster.h"

static const struct {
    const char *name;
    uint8_t align, overall;
} ROSTER[] = {
    {"The Rock",       U_FACE, 92},
    {"Stone Cold",     U_FACE, 93},
    {"Triple H",       U_HEEL, 91},
    {"Undertaker",     U_FACE, 89},
    {"Kurt Angle",     U_HEEL, 88},
    {"Chris Jericho",  U_FACE, 86},
    {"Kane",           U_HEEL, 85},
    {"Chris Benoit",   U_HEEL, 86},
    {"Big Show",       U_HEEL, 80},
    {"Eddie Guerrero", U_HEEL, 82},
    {"Rikishi",        U_FACE, 78},
    {"Edge",           U_FACE, 77},
    {"Christian",      U_HEEL, 75},
    {"Jeff Hardy",     U_FACE, 76},
    {"Matt Hardy",     U_FACE, 74},
    {"X-Pac",          U_HEEL, 73},
};

void demo_universe(Universe *u, uint32_t seed)
{
    unsigned i;
    u_init(u, seed);
    for (i = 0; i < sizeof ROSTER / sizeof ROSTER[0]; i++)
        u_add_wrestler(u, ROSTER[i].name, U_NONE, U_BRAND_RAW,
                       ROSTER[i].align, ROSTER[i].overall);
    u_add_title(u, "WWF Title", U_BRAND_RAW, 2);           /* Triple H */
    u_add_title(u, "IC Title", U_BRAND_RAW, 5); /* Chris Jericho */
    u_add_rivalry(u, 0, 4, 60);                            /* Rock vs Angle */
}
