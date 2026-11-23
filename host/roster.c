/*
 * The full Universe roster (notes/universe-plan.md step 5), from No Mercy's
 * own data: wrestler numbers, attires ("Type": some numbers hold two
 * characters) and weights are read from the game's wrestler profiles
 * (notes/re-findings.md). Alignment and ability are Universe's own
 * (late 2000). Starting rivalries, allies and tag teams come from the
 * game's ALLY/ENEMY data (REL below).
 *
 * Regulars: the 50 men and the women's division. Fill-ins (Royal Rumble
 * only, when fewer than 40 regulars are healthy): Chyna, the only woman
 * allowed in the Rumble, and the non-wrestlers the Rumble's entry list can
 * name (attire 0 of their game number). Stephanie and Linda McMahon and the
 * Ho never wrestle in Universe.
 */
#include "roster.h"

enum { M = U_MALE, F = U_FEMALE };
enum { FA = U_FACE, HE = U_HEEL };
enum { REG = U_ROLE_REGULAR, FIL = U_ROLE_FILLIN };

static const struct {
    const char *name;
    uint8_t num, attire, gender, role, align, overall;
    uint16_t weight;          /* game weight (lbs), 0 = not given */
} R[] = {
    {"The Rock",        1, 0, M, REG, FA, 93, 275},
    {"Steve Austin",    2, 0, M, REG, FA, 93, 252},
    {"Undertaker",      3, 0, M, REG, FA, 90, 328},
    {"Kane",            4, 0, M, REG, HE, 87, 326},
    {"Steven Richards", 5, 0, M, REG, HE, 70, 238},
    {"Mankind",         6, 0, M, REG, FA, 85, 287},
    {"Cactus Jack",     7, 0, M, REG, FA, 84, 287},
    {"Mick Foley",      8, 0, M, REG, FA, 81, 287},
    {"Shawn Michaels",  9, 0, M, REG, FA, 88, 227},
    {"Triple H",       10, 0, M, REG, HE, 92, 246},
    {"X-Pac",          11, 0, M, REG, HE, 78, 212},
    {"Mr. Ass",        12, 0, M, REG, FA, 77, 270},
    {"Road Dogg",      13, 0, M, REG, FA, 76, 236},
    {"Chris Benoit",   14, 0, M, REG, HE, 86, 220},
    {"Dean Malenko",   15, 0, M, REG, HE, 78, 212},
    {"Perry Saturn",   16, 0, M, REG, HE, 76, 234},
    {"Eddie Guerrero", 17, 0, M, REG, HE, 83, 220},
    {"Chris Jericho",  18, 0, M, REG, FA, 87, 225},
    {"Kurt Angle",     19, 0, M, REG, HE, 89, 220},
    {"Tazz",           20, 0, M, REG, FA, 78, 240},
    {"Test",           21, 0, M, REG, FA, 76, 287},
    {"Godfather",      22, 0, M, REG, HE, 68, 320},
    {"D'Lo Brown",     23, 0, M, REG, HE, 72, 286},
    {"Val Venis",      24, 0, M, REG, HE, 76, 240},
    {"Ken Shamrock",   25, 0, M, REG, FA, 81, 235},
    {"Rikishi",        26, 0, M, REG, HE, 80, 401},
    {"G.M. Sexay",     27, 0, M, REG, FA, 70, 213},
    {"Scotty",         28, 0, M, REG, FA, 71, 209},
    {"Edge",           29, 0, M, REG, HE, 79, 240},
    {"Christian",      30, 0, M, REG, HE, 77, 215},
    {"Matt Hardy",     31, 0, M, REG, FA, 78, 227},
    {"Jeff Hardy",     32, 0, M, REG, FA, 79, 222},
    {"Faarooq",        33, 0, M, REG, FA, 74, 270},
    {"Bradshaw",       34, 0, M, REG, FA, 77, 290},
    {"D-Von",          35, 0, M, REG, FA, 76, 240},
    {"Buh Buh Ray",    36, 0, M, REG, FA, 78, 275},
    {"Hardcore Holly", 37, 0, M, REG, FA, 72, 242},
    {"Crash",          38, 0, M, REG, FA, 68,   0},
    {"Big Boss Man",   39, 0, M, REG, HE, 72, 315},
    {"Albert",         40, 0, M, REG, HE, 72, 325},
    {"Al Snow",        41, 0, M, REG, FA, 72, 234},
    {"Steve Blackman", 42, 0, M, REG, FA, 72, 245},
    {"British Bulldog",43, 0, M, REG, HE, 75, 270},
    {"Mark Henry",     44, 0, M, REG, FA, 70, 400},
    {"Viscera",        45, 0, M, REG, HE, 66, 554},
    {"Andre the Giant",46, 0, M, REG, FA, 80, 562},
    {"Bull Buchanan",  47, 0, M, REG, HE, 74, 297},
    {"Essa Rios",      48, 0, M, REG, FA, 69, 195},
    {"Taka Michinoku", 49, 0, M, REG, FA, 67, 187},
    {"Funaki",         49, 1, M, REG, FA, 64, 194},
    {"Tori",           52, 0, F, REG, HE, 64, 0},
    {"Terri",          53, 0, F, REG, FA, 60, 0},
    {"Debra",          53, 1, F, REG, FA, 58, 0},
    {"Trish Stratus",  54, 0, F, REG, HE, 72, 0},
    {"Lita",           55, 0, F, REG, FA, 74, 0},
    {"Ivory",          56, 0, F, REG, HE, 73, 0},
    {"Jacqueline",     56, 1, F, REG, FA, 70, 0},
    {"The Kat",        58, 0, F, REG, FA, 62, 0},
    {"Mae Young",      59, 0, F, REG, FA, 52, 0},
    {"Fabulous Moolah",59, 1, F, REG, FA, 60, 0},
    {"Chyna",          50, 0, F, FIL, FA, 76, 0},
    {"Vince McMahon",  61, 0, M, FIL, HE, 60, 0},
    {"Shane McMahon",  62, 0, M, FIL, HE, 66, 0},
    {"Earl Hebner",    63, 0, M, FIL, FA, 40, 0},
    {"Jim Ross",       64, 0, M, FIL, FA, 40, 0},
};
#define N_R (int)(sizeof R / sizeof R[0])

static int id_of(const Universe *u, uint8_t num, uint8_t attire)
{
    int i;
    for (i = 0; i < u->n_wrestlers; i++)
        if (u->w[i].game_slot == num && u->w[i].game_attire == attire) return i;
    return -1;
}

/* No Mercy's own relations (Edit a Superstar > ALLY/ENEMY), decoded from
 * the game's packed wrestler records (notes/re-findings.md): Rival 1-3
 * (the game weights them 50 / 30 / 20 %) and "Accompanied by", as game
 * numbers (attire 0), indexed by game number - 1. The built-in wrestlers'
 * relations can't be edited in the game, so this table is what every save
 * has. */
static const struct { uint8_t rival[3], with; } REL[64] = {
    /*  1 */ {{10, 14, 3}, 0},
    /*  2 */ {{61, 10, 1}, 0},
    /*  3 */ {{10, 1, 4}, 0},
    /*  4 */ {{10, 11, 52}, 0},
    /*  5 */ {{0, 0, 0}, 47},
    /*  6 */ {{10, 41, 0}, 0},
    /*  7 */ {{10, 0, 0}, 0},
    /*  8 */ {{10, 0, 0}, 0},
    /*  9 */ {{3, 10, 61}, 0},
    /* 10 */ {{1, 2, 3}, 51},
    /* 11 */ {{4, 36, 0}, 52},
    /* 12 */ {{36, 35, 0}, 0},
    /* 13 */ {{36, 26, 1}, 0},
    /* 14 */ {{18, 26, 1}, 0},
    /* 15 */ {{17, 16, 50}, 0},
    /* 16 */ {{14, 15, 17}, 0},
    /* 17 */ {{15, 0, 0}, 50},
    /* 18 */ {{10, 50, 19}, 0},
    /* 19 */ {{18, 20, 26}, 0},
    /* 20 */ {{19, 10, 0}, 0},
    /* 21 */ {{35, 36, 32}, 54},
    /* 22 */ {{23, 17, 50}, 60},
    /* 23 */ {{22, 0, 0}, 0},
    /* 24 */ {{26, 37, 18}, 54},
    /* 25 */ {{0, 0, 0}, 0},
    /* 26 */ {{24, 14, 10}, 0},
    /* 27 */ {{29, 30, 0}, 28},
    /* 28 */ {{29, 30, 15}, 27},
    /* 29 */ {{27, 28, 26}, 30},
    /* 30 */ {{27, 28, 26}, 29},
    /* 31 */ {{29, 30, 54}, 32},
    /* 32 */ {{29, 30, 54}, 31},
    /* 33 */ {{0, 0, 0}, 0},
    /* 34 */ {{0, 0, 0}, 0},
    /* 35 */ {{11, 13, 52}, 36},
    /* 36 */ {{11, 13, 52}, 35},
    /* 37 */ {{14, 18, 24}, 0},
    /* 38 */ {{64, 37, 34}, 0},
    /* 39 */ {{47, 0, 0}, 0},
    /* 40 */ {{31, 32, 27}, 54},
    /* 41 */ {{0, 0, 0}, 0},
    /* 42 */ {{0, 0, 0}, 0},
    /* 43 */ {{0, 0, 0}, 0},
    /* 44 */ {{35, 36, 0}, 0},
    /* 45 */ {{0, 0, 0}, 0},
    /* 46 */ {{0, 0, 0}, 0},
    /* 47 */ {{39, 3, 0}, 5},
    /* 48 */ {{55, 0, 0}, 0},
    /* 49 */ {{15, 0, 0}, 0},
    /* 50 */ {{15, 18, 0}, 17},
    /* 51 */ {{18, 56, 0}, 10},
    /* 52 */ {{36, 4, 0}, 0},
    /* 53 */ {{50, 58, 0}, 0},
    /* 54 */ {{55, 0, 0}, 24},
    /* 55 */ {{54, 48, 0}, 0},
    /* 56 */ {{51, 0, 0}, 0},
    /* 57 */ {{51, 61, 62}, 0},
    /* 58 */ {{53, 63, 63}, 0},
    /* 59 */ {{35, 36, 53}, 0},
    /* 60 */ {{38, 0, 0}, 0},
    /* 61 */ {{1, 2, 3}, 0},
    /* 62 */ {{1, 3, 2}, 0},
    /* 63 */ {{10, 0, 0}, 0},
    /* 64 */ {{38, 0, 0}, 0},
};

/* Starting teams the game's data doesn't pair (the game has no faction
 * data: its "stables" are just the Superstar Select pages). */
static const struct { const char *name; uint8_t a, aa, b, ba; } OWN_TEAMS[] = {
    {"APA",             33, 0, 34, 0},
    {"New Age Outlaws", 13, 0, 12, 0},
    {"Kaientai",        49, 0, 49, 1},
};

/* Names for the teams the game's data pairs (by first member's number) */
static const struct { uint8_t num; const char *name; } TEAM_NAMES[] = {
    {29, "Edge&Christian"}, {31, "Hardy Boyz"}, {35, "Dudley Boyz"},
    {27, "Too Cool"}, {5, "Right to Censor"}, {21, "T & A"},
};

#define ALLY_PARTNER   90   /* both name each other as "Accompanied by" */
#define ALLY_VALET     75   /* one names the other */
#define ALLY_SHARED    50   /* both accompanied by the same person */
#define ALLY_COMMON    20   /* ... and a rival in common */
#define ALLY_OWN_TEAM  80

static const uint8_t RIVAL_WEIGHT[3] = {50, 30, 20};

/* Belts in No Mercy's order (title index = game belt), their division and
 * default champions (late 2000) for a save whose own champions table is
 * empty. Tag titles: the team of that wrestler. */
static const struct {
    const char *name;
    uint8_t kind, division, num, attire;
} BELTS[] = {
    {"WWF Title",       U_TITLE_SINGLES, U_DIV_MEN,   19, 0},   /* Kurt Angle */
    {"Tag Team Titles", U_TITLE_TAG,     U_DIV_MEN,   29, 0},   /* Edge & Christian (Edge's team) */
    {"IC Title",        U_TITLE_SINGLES, U_DIV_MEN,   12, 0},   /* Mr. Ass */
    {"European Title",  U_TITLE_SINGLES, U_DIV_MEN,   41, 0},   /* Al Snow */
    {"Hardcore Title",  U_TITLE_SINGLES, U_DIV_MEN,   42, 0},   /* Steve Blackman */
    {"Lt Heavyweight",  U_TITLE_SINGLES, U_DIV_LIGHT, 15, 0},   /* Dean Malenko */
    {"Women's Title",   U_TITLE_SINGLES, U_DIV_WOMEN, 56, 0},   /* Ivory */
};

/* Universe id of a game number (attire 0); Mankind / Cactus Jack / Mick
 * Foley are one man (the first of his personas). -1 = not in the universe. */
static int who(const Universe *u, uint8_t num)
{
    int id = num ? id_of(u, num, 0) : -1, i;
    if (id < 0 || !u->w[id].persona) return id;
    for (i = 0; i < id; i++)
        if (u->w[i].persona == u->w[id].persona) return i;
    return id;
}

static int regular(const Universe *u, int id)
{
    return id >= 0 && u->w[id].role == U_ROLE_REGULAR;
}

static int team_of(const Universe *u, int id)
{
    int i;
    for (i = 0; i < u->n_teams; i++)
        if (u->tm[i].a == id || u->tm[i].b == id) return i;
    return -1;
}

static int common_rival(int n, int m)
{
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            if (REL[n - 1].rival[i] && REL[n - 1].rival[i] == REL[m - 1].rival[j]) return 1;
    return 0;
}

/* Allies from "Accompanied by"; tag teams from the strong pairs of men */
static void game_allies(Universe *u)
{
    int n, m, i;
    for (n = 1; n <= 64; n++) {
        int w = REL[n - 1].with, a = who(u, (uint8_t)n), b = who(u, (uint8_t)w);
        if (!w || a < 0 || b < 0 || a == b) continue;
        u_add_ally(u, (uint8_t)a, (uint8_t)b,
                   REL[w - 1].with == n ? ALLY_PARTNER : ALLY_VALET);
    }
    for (n = 1; n <= 64; n++)
        for (m = n + 1; m <= 64; m++) {
            int a = who(u, (uint8_t)n), b = who(u, (uint8_t)m);
            if (!REL[n - 1].with || REL[n - 1].with != REL[m - 1].with) continue;
            if (a < 0 || b < 0 || a == b) continue;
            u_add_ally(u, (uint8_t)a, (uint8_t)b,
                       ALLY_SHARED + (common_rival(n, m) ? ALLY_COMMON : 0));
        }
    for (i = 0; i < U_MAX_ALLIES; i++) {                /* strong pairs of men */
        const UAlly *x = &u->al[i];
        const char *name = 0;
        char buf[U_NAME_LEN];
        unsigned k;
        if (x->level < U_ALLY_CLOSE || !regular(u, x->a) || !regular(u, x->b)) continue;
        if (u->w[x->a].gender != U_MALE || u->w[x->b].gender != U_MALE) continue;
        if (team_of(u, x->a) >= 0 || team_of(u, x->b) >= 0) continue;
        for (k = 0; k < sizeof TEAM_NAMES / sizeof TEAM_NAMES[0]; k++)
            if (TEAM_NAMES[k].num == u->w[x->a].game_slot ||
                TEAM_NAMES[k].num == u->w[x->b].game_slot)
                name = TEAM_NAMES[k].name;
        if (!name) {                                    /* "A & B" */
            const char *p[2] = {u->w[x->a].name, u->w[x->b].name};
            int o = 0, j;
            for (j = 0; j < 2; j++) {
                const char *c = p[j];
                if (j) { buf[o++] = '&'; }
                while (*c && *c != ' ' && o < U_NAME_LEN - 1) buf[o++] = *c++;
            }
            buf[o] = 0;
            name = buf;
        }
        u_add_team(u, name, x->a, x->b);
    }
}

/* Rivalries from the game's Rival 1-3: each side adds its weight for the
 * other (a mutual first rival is hottest), hottest added first. Rivals
 * outside the regular roster or of the other gender, and allies, are
 * left out. */
static void game_rivalries(Universe *u)
{
    static uint8_t heat[U_MAX_WRESTLERS][U_MAX_WRESTLERS];  /* side weights */
    int n, k, a, b, best;
    for (a = 0; a < U_MAX_WRESTLERS; a++)
        for (b = 0; b < U_MAX_WRESTLERS; b++) heat[a][b] = 0;
    for (n = 1; n <= 64; n++) {
        a = who(u, (uint8_t)n);
        if (!regular(u, a)) continue;
        for (k = 0; k < 3; k++) {
            b = who(u, REL[n - 1].rival[k]);
            if (!regular(u, b) || a == b || u->w[a].gender != u->w[b].gender) continue;
            if (u_ally_level(u, (uint8_t)a, (uint8_t)b) >= U_ALLY_CLOSE) continue;
            if (RIVAL_WEIGHT[k] > heat[a][b]) heat[a][b] = RIVAL_WEIGHT[k];
        }
    }
    do {
        int ba = 0, bb = 0;
        best = 0;
        for (a = 0; a < u->n_wrestlers; a++)
            for (b = a + 1; b < u->n_wrestlers; b++)
                if (heat[a][b] + heat[b][a] > best) {
                    best = heat[a][b] + heat[b][a];
                    ba = a; bb = b;
                }
        if (best) {
            u_add_rivalry(u, (uint8_t)ba, (uint8_t)bb, (uint8_t)(15 + best / 2));
            heat[ba][bb] = heat[bb][ba] = 0;
        }
    } while (best);
}

void roster_universe(Universe *u, uint32_t seed)
{
    unsigned i;
    u_init(u, seed);
    for (i = 0; i < (unsigned)N_R; i++) {
        int id = R[i].role == FIL
               ? u_add_fillin(u, R[i].name, R[i].num, R[i].overall)
               : u_add_wrestler(u, R[i].name, R[i].num, U_BRAND_RAW, R[i].align, R[i].overall);
        if (id < 0) continue;
        u->w[id].align = R[i].align;
        u_set_game_details(u, (uint8_t)id, R[i].attire, R[i].gender, R[i].weight);
    }
    for (i = 6; i <= 8; i++) {             /* Mankind, Cactus Jack, Mick Foley: one man */
        int id = id_of(u, (uint8_t)i, 0);
        if (id >= 0) u_set_persona(u, (uint8_t)id, 1);
    }
    game_allies(u);
    for (i = 0; i < sizeof OWN_TEAMS / sizeof OWN_TEAMS[0]; i++) {
        int a = id_of(u, OWN_TEAMS[i].a, OWN_TEAMS[i].aa), b = id_of(u, OWN_TEAMS[i].b, OWN_TEAMS[i].ba);
        if (a < 0 || b < 0) continue;
        u_add_ally(u, (uint8_t)a, (uint8_t)b, ALLY_OWN_TEAM);
        u_add_team(u, OWN_TEAMS[i].name, (uint8_t)a, (uint8_t)b);
    }
    for (i = 0; i < sizeof BELTS / sizeof BELTS[0]; i++) {
        int h = BELTS[i].kind == U_TITLE_TAG ? team_of(u, id_of(u, BELTS[i].num, BELTS[i].attire))
                                             : id_of(u, BELTS[i].num, BELTS[i].attire);
        int t = u_add_title(u, BELTS[i].name, U_BRAND_RAW, BELTS[i].kind,
                            h >= 0 ? (uint8_t)h : U_NONE);
        if (t >= 0) u_set_division(u, (uint8_t)t, BELTS[i].division);
    }
    game_rivalries(u);
}
