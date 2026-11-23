/*
 * The full Universe roster (notes/universe-plan.md step 5), from No Mercy's
 * own data: wrestler numbers, attires ("Type": some numbers hold two
 * characters) and weights are read from the game's wrestler profiles
 * (notes/re-findings.md). Alignment and ability are Universe's own
 * (late 2000).
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

/* Interim starting tag teams and rivalries (late 2000) until they come
 * from the game's ally / rival data (plan step 5, after reporting it). */
static const struct { const char *name; uint8_t a, aa, b, ba; } TEAMS[] = {
    {"Edge&Christian",  29, 0, 30, 0},
    {"Dudley Boyz",     36, 0, 35, 0},
    {"Hardy Boyz",      31, 0, 32, 0},
    {"New Age Outlaws", 13, 0, 12, 0},
    {"APA",             33, 0, 34, 0},
    {"Too Cool",        27, 0, 28, 0},
    {"Holly Cousins",   37, 0, 38, 0},
    {"T & A",           21, 0, 40, 0},
    {"Kaientai",        49, 0, 49, 1},
    {"Radicalz",        16, 0, 15, 0},
    {"Right to Censor", 47, 0, 22, 0},
};

static const struct { uint8_t a, b, heat; } FEUDS[] = {
    {2, 10, 60},    /* Steve Austin vs Triple H */
    {1, 19, 55},    /* The Rock vs Kurt Angle */
    {3, 4, 45},     /* Undertaker vs Kane */
    {18, 14, 40},   /* Chris Jericho vs Chris Benoit */
};

/* Belts in No Mercy's order (title index = game belt), their division and
 * default champions (late 2000) for a save whose own champions table is
 * empty. Tag titles: team index. */
static const struct {
    const char *name;
    uint8_t kind, division, num, attire;
} BELTS[] = {
    {"WWF Title",       U_TITLE_SINGLES, U_DIV_MEN,   19, 0},   /* Kurt Angle */
    {"Tag Team Titles", U_TITLE_TAG,     U_DIV_MEN,    0, 0},   /* Edge & Christian */
    {"IC Title",        U_TITLE_SINGLES, U_DIV_MEN,   12, 0},   /* Mr. Ass */
    {"European Title",  U_TITLE_SINGLES, U_DIV_MEN,   41, 0},   /* Al Snow */
    {"Hardcore Title",  U_TITLE_SINGLES, U_DIV_MEN,   42, 0},   /* Steve Blackman */
    {"Lt Heavyweight",  U_TITLE_SINGLES, U_DIV_LIGHT, 15, 0},   /* Dean Malenko */
    {"Women's Title",   U_TITLE_SINGLES, U_DIV_WOMEN, 56, 0},   /* Ivory */
};

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
    for (i = 0; i < sizeof TEAMS / sizeof TEAMS[0]; i++) {
        int a = id_of(u, TEAMS[i].a, TEAMS[i].aa), b = id_of(u, TEAMS[i].b, TEAMS[i].ba);
        if (a >= 0 && b >= 0) u_add_team(u, TEAMS[i].name, (uint8_t)a, (uint8_t)b);
    }
    for (i = 0; i < sizeof BELTS / sizeof BELTS[0]; i++) {
        int h = BELTS[i].kind == U_TITLE_TAG ? BELTS[i].num
                                             : id_of(u, BELTS[i].num, BELTS[i].attire);
        int t = u_add_title(u, BELTS[i].name, U_BRAND_RAW, BELTS[i].kind,
                            h >= 0 ? (uint8_t)h : U_NONE);
        if (t >= 0) u_set_division(u, (uint8_t)t, BELTS[i].division);
    }
    for (i = 0; i < sizeof FEUDS / sizeof FEUDS[0]; i++) {
        int a = id_of(u, FEUDS[i].a, 0), b = id_of(u, FEUDS[i].b, 0);
        if (a >= 0 && b >= 0) u_add_rivalry(u, (uint8_t)a, (uint8_t)b, FEUDS[i].heat);
    }
}
