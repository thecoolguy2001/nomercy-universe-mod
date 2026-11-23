/* No Mercy Universe engine - see universe.h for the design rules. */
#include "universe.h"

/* Ranking points (notes/universe-plan.md, Rankings) */
#define U_PTS_START   3     /* a new wrestler / team: 3 x ability */
#define U_PTS_WIN     20
#define U_PTS_LOSS    10
#define U_PTS_GAP     10    /* + 1 per this many points the other side was ahead */
#define U_PTS_GAP_MAX 20
#define U_PTS_STREAK  3     /* per win in a row before this one ... */
#define U_PTS_STREAK_MAX 5  /* ... counting up to this many */
#define U_PTS_ALONE   10    /* the lone wrestler beating a handicap pair */
#define U_PTS_RUMBLE  40    /* Royal Rumble winner */
#define U_PTS_MAX     9999


/* ---------- small helpers (no libc) ---------- */

static void u_zero(void *p, unsigned n)
{
    unsigned char *c = (unsigned char *)p;
    while (n--) *c++ = 0;
}

static void u_set_name(char *dst, const char *src)
{
    int i = 0;
    for (; i < U_NAME_LEN - 1 && src[i]; i++) dst[i] = src[i];
    for (; i < U_NAME_LEN; i++) dst[i] = 0;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* xorshift32: tiny, fast, deterministic, state fits in the save. */
uint32_t u_rand(Universe *u, uint32_t n)
{
    uint32_t x = u->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    u->rng = x;
    return n ? x % n : x;
}

/* ---------- calendar ---------- */

static const char *const PPV_NAMES[12] = {
    "Royal Rumble", "No Way Out", "WrestleMania", "Backlash",
    "Judgment Day", "King of the Ring", "Fully Loaded", "SummerSlam",
    "Unforgiven", "No Mercy", "Survivor Series", "Armageddon",
};

void u_next_show(const Universe *u, uint8_t *kind, uint8_t *brand)
{
    if (u->slot >= 2) {
        *kind = U_SHOW_PPV;
        *brand = U_NONE;
    } else {
        *kind = U_SHOW_WEEKLY;
        *brand = u->slot;
    }
}

int u_ppv_index(const Universe *u)
{
    return (u->week / U_PPV_EVERY + 11) % 12;
}

const char *u_show_name(const Universe *u)
{
    if (u->slot == U_BRAND_RAW) return "Raw Is War";
    if (u->slot == U_BRAND_SMACKDOWN) return "SmackDown!";
    return PPV_NAMES[u_ppv_index(u)];
}

static void advance_calendar(Universe *u)
{
    if (u->slot == 0) {
        u->slot = 1;
    } else if (u->slot == 1 && u->week % U_PPV_EVERY == 0) {
        u->slot = 2;
    } else {
        u->slot = 0;
        u->week++;
    }
}

const char *u_stip_name(uint8_t stip)
{
    static const char *const N[U_STIP_COUNT] = {
        "", "Cage", "Ladder", "Ironman", "Guest Referee", "No DQ"};
    return stip < U_STIP_COUNT ? N[stip] : "";
}

const char *u_format_name(uint8_t format)
{
    static const char *const N[U_FMT_COUNT] = {
        "Single", "Tag Team", "Triple Threat", "Handicap", "Royal Rumble"};
    return format < U_FMT_COUNT ? N[format] : "";
}

int u_match_sides(const UMatch *m)
{
    if (m->format == U_FMT_RUMBLE) return m->n_entrants;
    return m->format == U_FMT_TRIPLE ? 3 : 2;
}

static int append(char *buf, int n, int cap, const char *t)
{
    while (*t && n < cap - 1) buf[n++] = *t++;
    buf[n] = 0;
    return n;
}

int u_side_text(const Universe *u, const UMatch *m, int side, char *buf, int cap)
{
    int n = 0;
    if (cap <= 0) return 0;
    buf[0] = 0;
    if (m->format == U_FMT_RUMBLE)
        return side >= 0 && side < m->n_entrants
            ? append(buf, 0, cap, u->w[m->entrant[side]].name) : 0;
    if (side < 0 || side >= 3) return 0;
    if (m->side[side][0] == U_NONE) return append(buf, 0, cap, "TBD");  /* later round */
    if (m->format == U_FMT_TAG && m->team[side] != U_NONE)
        return append(buf, 0, cap, u->tm[m->team[side]].name);
    if (m->side[side][0] != U_NONE) n = append(buf, n, cap, u->w[m->side[side][0]].name);
    if (m->side[side][1] != U_NONE) {
        n = append(buf, n, cap, " & ");
        n = append(buf, n, cap, u->w[m->side[side][1]].name);
    }
    return n;
}

int u_match_text(const Universe *u, const UMatch *m, int result, char *buf, int cap)
{
    char part[2 * U_NAME_LEN + 4];
    int n = 0, k, first = 1, sides = u_match_sides(m);
    if (cap <= 0) return 0;
    buf[0] = 0;
    if (m->format == U_FMT_RUMBLE) {
        char num[4] = {0, 0, 0, 0};
        if (result && m->done && m->winner < m->n_entrants) {
            n = append(buf, 0, cap, u->w[m->entrant[m->winner]].name);
            return append(buf, n, cap, " wins the Royal Rumble");
        }
        num[0] = (char)('0' + m->n_entrants / 10 % 10);
        num[1] = (char)('0' + m->n_entrants % 10);
        n = append(buf, 0, cap, "Royal Rumble - ");
        n = append(buf, n, cap, num[0] == '0' ? num + 1 : num);
        return append(buf, n, cap, " men");
    }
    if (result && m->done && m->winner == U_NONE) {      /* a draw */
        for (k = 0; k < sides; k++) {
            if (k) n = append(buf, n, cap, " vs ");
            u_side_text(u, m, k, part, sizeof part);
            n = append(buf, n, cap, part);
        }
        return append(buf, n, cap, " - Draw");
    }
    if (result && m->done) {
        u_side_text(u, m, m->winner, part, sizeof part);
        n = append(buf, n, cap, part);
        n = append(buf, n, cap, " def. ");
        for (k = 0; k < sides; k++) {
            if (k == m->winner) continue;
            if (!first) n = append(buf, n, cap, " and ");
            u_side_text(u, m, k, part, sizeof part);
            n = append(buf, n, cap, part);
            first = 0;
        }
        return n;
    }
    for (k = 0; k < sides; k++) {
        if (k) n = append(buf, n, cap, " vs ");
        u_side_text(u, m, k, part, sizeof part);
        n = append(buf, n, cap, part);
    }
    return n;
}

const char *u_method_name(uint8_t method, uint8_t stip)
{
    static const char *const N[] = {"pinfall", "submission", "count-out", "DQ"};
    if (method == U_WIN_SPECIAL)
        return stip == U_STIP_LADDER ? "climbing the ladder" : "escaping the cage";
    if (method == U_WIN_FALLS) return "most falls";
    if (method == U_WIN_DRAW) return "draw";
    if (method == U_WIN_RUMBLE) return "last man in the ring";
    return method < U_WIN_SPECIAL ? N[method] : "?";
}

/* How a match can end. Any match the player plays can end in a draw. */
int u_method_allowed(uint8_t stip, uint8_t method)
{
    if (method == U_WIN_DRAW) return stip < U_STIP_COUNT;
    switch (stip) {
    case U_STIP_NORMAL:      /* (U_WIN_RUMBLE: Royal Rumble format only) */
        return method <= U_WIN_DQ || method == U_WIN_RUMBLE;
    case U_STIP_REFEREE: return method <= U_WIN_DQ;
    case U_STIP_NODQ:    return method == U_WIN_PINFALL || method == U_WIN_SUBMISSION;
    case U_STIP_CAGE:    return method == U_WIN_PINFALL || method == U_WIN_SUBMISSION ||
                                method == U_WIN_SPECIAL;
    case U_STIP_LADDER:  return method == U_WIN_SPECIAL;
    case U_STIP_IRONMAN: return method == U_WIN_FALLS;
    }
    return 0;
}

int u_stip_supported(uint8_t format, uint8_t stip)
{
    if (format >= U_FMT_COUNT || stip >= U_STIP_COUNT) return 0;
    if (format == U_FMT_RUMBLE) return stip == U_STIP_NORMAL;
    if (stip == U_STIP_CAGE) return format == U_FMT_SINGLE;
    if (stip == U_STIP_REFEREE) return format != U_FMT_TAG;
    return 1;
}

/* ---------- setup ---------- */

void u_init(Universe *u, uint32_t seed)
{
    u_zero(u, sizeof *u);
    u->rng = seed ? seed : 0x4E4D5531u; /* xorshift must not start at 0 */
    u->week = 1;
    u->rumble_winner = U_NONE;
    u->king = U_NONE;
}

int u_add_wrestler(Universe *u, const char *name, uint8_t game_slot,
                   uint8_t brand, uint8_t align, uint8_t overall)
{
    UWrestler *w;
    if (u->n_wrestlers >= U_MAX_WRESTLERS || brand >= U_BRAND_COUNT) return -1;
    w = &u->w[u->n_wrestlers];
    u_zero(w, sizeof *w);
    u_set_name(w->name, name);
    w->game_slot = game_slot;
    w->brand = brand;
    w->align = align ? U_HEEL : U_FACE;
    w->overall = (uint8_t)clamp(overall, 0, 100);
    w->points = (uint16_t)(U_PTS_START * w->overall);
    w->active = 1;
    return u->n_wrestlers++;
}

int u_add_fillin(Universe *u, const char *name, uint8_t game_slot, uint8_t overall)
{
    int id = u_add_wrestler(u, name, game_slot, U_BRAND_RAW, U_FACE, overall);
    if (id >= 0) u->w[id].role = U_ROLE_FILLIN;
    return id;
}

int u_set_game_details(Universe *u, uint8_t id, uint8_t attire, uint8_t gender,
                       uint16_t weight)
{
    if (id >= u->n_wrestlers || attire > 3 || gender > U_FEMALE) return -1;
    u->w[id].game_attire = attire;
    u->w[id].gender = gender;
    u->w[id].weight = weight;
    return 0;
}

int u_set_persona(Universe *u, uint8_t id, uint8_t persona)
{
    if (id >= u->n_wrestlers) return -1;
    u->w[id].persona = persona;
    return 0;
}

int u_set_champion(Universe *u, uint8_t title, uint8_t holder)
{
    UTitle *t;
    if (title >= u->n_titles) return -1;
    t = &u->t[title];
    if (holder >= (t->kind == U_TITLE_TAG ? u->n_teams : u->n_wrestlers) || t->reign_count > 1)
        return -1;
    t->vacant = 0;
    t->reign_count = 1;
    t->history[0].holder = holder;
    t->history[0].defenses = 0;
    t->history[0].won_week = u->week;
    t->history[0].lost_week = U_OPEN_WEEK;
    return 0;
}

int u_set_no_world(Universe *u, uint8_t title, uint8_t on)
{
    if (title >= u->n_titles) return -1;
    u->t[title].no_world = on ? 1 : 0;
    return 0;
}

int u_set_division(Universe *u, uint8_t title, uint8_t division)
{
    if (title >= u->n_titles || division > U_DIV_LIGHT) return -1;
    u->t[title].division = division;
    return 0;
}

int u_add_team(Universe *u, const char *name, uint8_t a, uint8_t b)
{
    UTeam *t;
    if (u->n_teams >= U_MAX_TEAMS || a == b || a >= u->n_wrestlers ||
        b >= u->n_wrestlers)
        return -1;
    t = &u->tm[u->n_teams];
    u_zero(t, sizeof *t);
    u_set_name(t->name, name);
    t->a = a;
    t->b = b;
    t->active = 1;
    t->points = (uint16_t)(U_PTS_START * (u->w[a].overall + u->w[b].overall) / 2);
    return u->n_teams++;
}

int u_add_title(Universe *u, const char *name, uint8_t brand, uint8_t kind,
                uint8_t holder)
{
    UTitle *t;
    uint8_t limit = kind == U_TITLE_TAG ? u->n_teams : u->n_wrestlers;
    if (u->n_titles >= U_MAX_TITLES || kind > U_TITLE_TAG) return -1;
    if (holder != U_NONE && holder >= limit) return -1;
    t = &u->t[u->n_titles];
    u_zero(t, sizeof *t);
    u_set_name(t->name, name);
    t->brand = brand;
    t->kind = kind;
    t->active = 1;
    t->division = U_DIV_MEN;
    t->last_week = u->week;
    if (holder == U_NONE) {
        t->vacant = 1;
    } else {
        t->reign_count = 1;
        t->history[0].holder = holder;
        t->history[0].won_week = u->week;
        t->history[0].lost_week = U_OPEN_WEEK;
    }
    return u->n_titles++;
}

int u_add_rivalry(Universe *u, uint8_t a, uint8_t b, uint8_t intensity)
{
    int i;
    if (a == b || a >= u->n_wrestlers || b >= u->n_wrestlers || !intensity)
        return -1;
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        if (u->r[i].intensity == 0) {
            u->r[i].a = a;
            u->r[i].b = b;
            u->r[i].intensity = intensity > 100 ? 100 : intensity;
            u->r[i].weeks = 0;
            return i;
        }
    }
    return -1;
}

/* ---------- queries ---------- */

int u_add_ally(Universe *u, uint8_t a, uint8_t b, uint8_t level)
{
    int i, free = -1;
    if (a == b || a >= u->n_wrestlers || b >= u->n_wrestlers || !level) return -1;
    for (i = 0; i < U_MAX_ALLIES; i++) {
        UAlly *x = &u->al[i];
        if (x->level && ((x->a == a && x->b == b) || (x->a == b && x->b == a))) {
            if (level > x->level) x->level = (uint8_t)clamp(level, 1, 100);
            return i;
        }
        if (!x->level && free < 0) free = i;
    }
    if (free < 0) return -1;
    u->al[free].a = a;
    u->al[free].b = b;
    u->al[free].level = (uint8_t)clamp(level, 1, 100);
    return free;
}

int u_ally_level(const Universe *u, uint8_t a, uint8_t b)
{
    int i;
    for (i = 0; i < U_MAX_ALLIES; i++) {
        const UAlly *x = &u->al[i];
        if (x->level && ((x->a == a && x->b == b) || (x->a == b && x->b == a))) return x->level;
    }
    return 0;
}

static UReign *current_reign(UTitle *t)
{
    return t->reign_count ? &t->history[(t->reign_count - 1) % U_MAX_REIGNS] : 0;
}

uint8_t u_title_holder(const Universe *u, uint8_t title)
{
    const UTitle *t;
    if (title >= u->n_titles) return U_NONE;
    t = &u->t[title];
    if (t->vacant || !t->reign_count) return U_NONE;
    return t->history[(t->reign_count - 1) % U_MAX_REIGNS].holder;
}

static int is_singles_champion(const Universe *u, uint8_t id)
{
    int i;
    for (i = 0; i < u->n_titles; i++)
        if (u->t[i].active && u->t[i].kind == U_TITLE_SINGLES &&
            u_title_holder(u, (uint8_t)i) == id)
            return 1;
    return 0;
}

static int is_tag_champion(const Universe *u, uint8_t team)
{
    int i;
    for (i = 0; i < u->n_titles; i++)
        if (u->t[i].active && u->t[i].kind == U_TITLE_TAG &&
            u_title_holder(u, (uint8_t)i) == team)
            return 1;
    return 0;
}

int u_score(const UWrestler *w)
{
    return w->overall + w->momentum + 2 * ((int)w->wins - (int)w->losses);
}

int u_team_score(const Universe *u, uint8_t team)
{
    const UTeam *t = &u->tm[team];
    return (u_score(&u->w[t->a]) + u_score(&u->w[t->b])) / 2 +
           3 * ((int)t->wins - (int)t->losses);
}

static int team_on_brand(const Universe *u, uint8_t team, uint8_t brand)
{
    const UTeam *t = &u->tm[team];
    if (!t->active || !u->w[t->a].active || !u->w[t->b].active) return 0;
    if (u->w[t->a].brand != u->w[t->b].brand) return 0; /* split by a draft */
    return brand == U_NONE || u->w[t->a].brand == brand;
}

/* Ranking order: points, then ability */
static int rank_key(const UWrestler *w)
{
    return (int)w->points * 128 + w->overall;
}

static int team_key(const Universe *u, uint8_t team)
{
    const UTeam *t = &u->tm[team];
    return (int)t->points * 128 + (u->w[t->a].overall + u->w[t->b].overall) / 2;
}

/* Active regular wrestlers of a brand (U_NONE = all), best first. */
int u_rankings(const Universe *u, uint8_t brand, uint8_t *out, int max)
{
    int n = 0, i, j;
    for (i = 0; i < u->n_wrestlers && n < max; i++) {
        if (!u->w[i].active || u->w[i].role != U_ROLE_REGULAR) continue;
        if (brand != U_NONE && u->w[i].brand != brand) continue;
        out[n++] = (uint8_t)i;
    }
    for (i = 1; i < n; i++) { /* insertion sort, stable by id */
        uint8_t id = out[i];
        int s = rank_key(&u->w[id]);
        for (j = i; j > 0 && rank_key(&u->w[out[j - 1]]) < s; j--)
            out[j] = out[j - 1];
        out[j] = id;
    }
    return n;
}

int u_eligible(const Universe *u, uint8_t id, uint8_t division)
{
    const UWrestler *w;
    if (id >= u->n_wrestlers) return 0;
    w = &u->w[id];
    if (division == U_DIV_WOMEN) return w->gender == U_FEMALE;
    if (w->gender != U_MALE) return 0;
    return division != U_DIV_LIGHT || (w->weight && w->weight <= U_LIGHT_LBS);
}

int u_division_rankings(const Universe *u, uint8_t division, uint8_t *out, int max)
{
    uint8_t all[U_MAX_WRESTLERS];
    int n = u_rankings(u, U_NONE, all, U_MAX_WRESTLERS), i, k = 0;
    for (i = 0; i < n && k < max; i++)
        if (u_eligible(u, all[i], division)) out[k++] = all[i];
    return k;
}

int u_team_rankings(const Universe *u, uint8_t brand, uint8_t *out, int max)
{
    int n = 0, i, j;
    for (i = 0; i < u->n_teams && n < max; i++)
        if (team_on_brand(u, (uint8_t)i, brand)) out[n++] = (uint8_t)i;
    for (i = 1; i < n; i++) {
        uint8_t id = out[i];
        int s = team_key(u, id);
        for (j = i; j > 0 && team_key(u, out[j - 1]) < s; j--)
            out[j] = out[j - 1];
        out[j] = id;
    }
    return n;
}

int u_contenders(const Universe *u, uint8_t title, uint8_t *out, int max)
{
    uint8_t all[U_MAX_WRESTLERS];
    const UTitle *t;
    uint8_t h, world;
    int n, i, k = 0;
    if (title >= u->n_titles) return 0;
    t = &u->t[title];
    h = u_title_holder(u, title);
    if (t->kind == U_TITLE_TAG) {
        n = u_team_rankings(u, U_NONE, all, U_MAX_TEAMS);
        for (i = 0; i < n && k < max; i++) {
            const UTeam *tm = &u->tm[all[i]];
            if (all[i] != h && !u->w[tm->a].injured_weeks && !u->w[tm->b].injured_weeks)
                out[k++] = all[i];
        }
        return k;
    }
    world = u->n_titles && u->t[0].kind == U_TITLE_SINGLES ? u_title_holder(u, 0) : U_NONE;
    n = u_division_rankings(u, t->division, all, U_MAX_WRESTLERS);
    for (i = 0; i < n && k < max; i++) {
        if (all[i] == h || u->w[all[i]].injured_weeks) continue;
        if (t->no_world && all[i] == world) continue;
        out[k++] = all[i];
    }
    return k;
}

int u_injuries(const Universe *u, uint8_t *out, int max)
{
    int n = 0, i, j;
    for (i = 0; i < u->n_wrestlers && n < max; i++)
        if (u->w[i].injured_weeks && u->w[i].active && u->w[i].role == U_ROLE_REGULAR)
            out[n++] = (uint8_t)i;
    for (i = 1; i < n; i++) {
        uint8_t id = out[i];
        for (j = i; j > 0 && u->w[out[j - 1]].injured_weeks < u->w[id].injured_weeks; j--)
            out[j] = out[j - 1];
        out[j] = id;
    }
    return n;
}

static int find_rivalry(const Universe *u, uint8_t a, uint8_t b)
{
    int i;
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (r->intensity && ((r->a == a && r->b == b) || (r->a == b && r->b == a)))
            return i;
    }
    return -1;
}

/* Close allies (tag partners, valets) are not booked as opponents. */
static int allies(const Universe *u, uint8_t a, uint8_t b)
{
    return u_ally_level(u, a, b) >= U_ALLY_CLOSE;
}

static int in_rivalry(const Universe *u, uint8_t id)
{
    int i;
    for (i = 0; i < U_MAX_RIVALRIES; i++)
        if (u->r[i].intensity && (u->r[i].a == id || u->r[i].b == id)) return 1;
    return 0;
}

/* ---------- roster management ---------- */

static void vacate(Universe *u, UTitle *t)
{
    UReign *cur = current_reign(t);
    if (t->vacant) return;
    if (cur) cur->lost_week = u->week;
    t->vacant = 1;
}

int u_set_brand(Universe *u, uint8_t id, uint8_t brand)
{
    int i;
    if (id >= u->n_wrestlers || brand >= U_BRAND_COUNT) return -1;
    u->w[id].brand = brand;
    for (i = 0; i < u->n_titles; i++) {  /* a drafted champion takes the belt */
        UTitle *t = &u->t[i];
        uint8_t h = u_title_holder(u, (uint8_t)i);
        if (h == U_NONE) continue;
        if (t->kind == U_TITLE_SINGLES && h == id) t->brand = brand;
        if (t->kind == U_TITLE_TAG && (u->tm[h].a == id || u->tm[h].b == id) &&
            team_on_brand(u, h, brand))
            t->brand = brand;
    }
    return 0;
}

int u_set_align(Universe *u, uint8_t id, uint8_t align)
{
    if (id >= u->n_wrestlers) return -1;
    u->w[id].align = align ? U_HEEL : U_FACE;
    return 0;
}

int u_set_overall(Universe *u, uint8_t id, uint8_t overall)
{
    if (id >= u->n_wrestlers) return -1;
    u->w[id].overall = (uint8_t)clamp(overall, 0, 100);
    return 0;
}

int u_set_active(Universe *u, uint8_t id, uint8_t active)
{
    int i;
    if (id >= u->n_wrestlers) return -1;
    u->w[id].active = active ? 1 : 0;
    for (i = 0; i < u->n_teams; i++) {
        UTeam *tm = &u->tm[i];
        if (tm->a == id || tm->b == id)
            tm->active = u->w[tm->a].active && u->w[tm->b].active;
    }
    if (active) return 0;
    for (i = 0; i < u->n_titles; i++) {
        UTitle *t = &u->t[i];
        uint8_t h = u_title_holder(u, (uint8_t)i);
        if (h == U_NONE) continue;
        if ((t->kind == U_TITLE_SINGLES && h == id) ||
            (t->kind == U_TITLE_TAG && (u->tm[h].a == id || u->tm[h].b == id)))
            vacate(u, t);
    }
    for (i = 0; i < U_MAX_RIVALRIES; i++)
        if (u->r[i].a == id || u->r[i].b == id) u->r[i].intensity = 0;
    return 0;
}

/* ---------- booking ---------- */

typedef struct {
    Universe *u;
    UShow    *s;
    uint8_t   max;
    uint8_t   used[U_MAX_WRESTLERS];
} Booker;

static UMatch *new_match(Booker *b, uint8_t format, uint8_t title, uint8_t stip)
{
    UMatch *m;
    int k;
    if (b->s->count >= b->max) return 0;
    m = &b->s->m[b->s->count++];
    u_zero(m, sizeof *m);
    m->format = format;
    m->stip = stip;
    m->title = title;
    m->rivalry = U_NONE;
    m->referee = U_NONE;
    m->winner = U_NONE;
    m->injured = U_NONE;
    for (k = 0; k < 3; k++) {
        m->team[k] = U_NONE;
        m->side[k][0] = m->side[k][1] = U_NONE;
    }
    return m;
}

static UMatch *book_singles(Booker *b, uint8_t x, uint8_t y, uint8_t title,
                            uint8_t riv, uint8_t stip)
{
    UMatch *m = new_match(b, U_FMT_SINGLE, title, stip);
    if (!m) return 0;
    m->side[0][0] = x;
    m->side[1][0] = y;
    m->rivalry = riv;
    b->used[x] = b->used[y] = 1;
    return m;
}

static void book_tag(Booker *b, uint8_t ta, uint8_t tb, uint8_t title, uint8_t stip)
{
    UMatch *m = new_match(b, U_FMT_TAG, title, stip);
    const UTeam *A = &b->u->tm[ta], *B = &b->u->tm[tb];
    if (!m) return;
    m->team[0] = ta;
    m->team[1] = tb;
    m->side[0][0] = A->a; m->side[0][1] = A->b;
    m->side[1][0] = B->a; m->side[1][1] = B->b;
    b->used[A->a] = b->used[A->b] = b->used[B->a] = b->used[B->b] = 1;
}

static void book_triple(Booker *b, uint8_t x, uint8_t y, uint8_t z, uint8_t title,
                        uint8_t stip)
{
    UMatch *m = new_match(b, U_FMT_TRIPLE, title, stip);
    if (!m) return;
    m->side[0][0] = x;
    m->side[1][0] = y;
    m->side[2][0] = z;
    b->used[x] = b->used[y] = b->used[z] = 1;
}

/* x alone against y and z */
static void book_handicap(Booker *b, uint8_t x, uint8_t y, uint8_t z, uint8_t stip)
{
    UMatch *m = new_match(b, U_FMT_HANDICAP, U_NONE, stip);
    if (!m) return;
    m->side[0][0] = x;
    m->side[1][0] = y;
    m->side[1][1] = z;
    b->used[x] = b->used[y] = b->used[z] = 1;
}

static int team_free(const Booker *b, uint8_t team)
{
    const UTeam *t = &b->u->tm[team];
    return team_on_brand(b->u, team, U_NONE) && !b->used[t->a] && !b->used[t->b];
}

/* One roster, two weekly shows: a feud on Raw rests on that week's
 * SmackDown (PPVs may have it again) */
static int rested(const Booker *b, const URivalry *r)
{
    return b->s->kind == U_SHOW_WEEKLY && b->s->brand == U_BRAND_SMACKDOWN &&
           r->last_show == 4u * b->u->week + U_BRAND_RAW;
}

#define U_WOMEN_MATCHES 2   /* women's matches per card at most */

static int women_matches(const Booker *b)
{
    int i, n = 0;
    for (i = 0; i < b->s->count; i++) {
        uint8_t id = b->s->m[i].side[0][0];
        if (id != U_NONE && b->u->w[id].gender == U_FEMALE) n++;
    }
    return n;
}

/* Two rivals whose feud is resting this show (see rested()) */
static int resting_pair(const Booker *b, uint8_t x, uint8_t y)
{
    int ri = find_rivalry(b->u, x, y);
    return ri >= 0 && rested(b, &b->u->r[ri]);
}

/* A regular match from a ranking: the free wrestler who has waited longest
 * for a match, against the free wrestler closest to him in the ranking
 * (opposite alignment preferred, within 4 places), never an ally or a feud
 * resting this show. 1 = booked, 0 = he was left out (only allies or
 * resting rivals free), -1 = nobody free. */
static int book_regular(Booker *b, const uint8_t *rank, int n)
{
    const Universe *u = b->u;
    int i, a = -1, d, k, pass, pick = -1;
    for (i = 0; i < n; i++)
        if (!b->used[rank[i]] && (a < 0 || u->w[rank[i]].last_show < u->w[rank[a]].last_show))
            a = i;
    if (a < 0) return -1;
    for (pass = 0; pass < 2 && pick < 0; pass++)
        for (d = 1; d < n && pick < 0 && (pass || d <= 4); d++)
            for (k = 0; k < 2 && pick < 0; k++) {
                int j = k ? a - d : a + d;
                if (j < 0 || j >= n || b->used[rank[j]]) continue;
                if (!pass && u->w[rank[j]].align == u->w[rank[a]].align) continue;
                if (allies(u, rank[a], rank[j]) || resting_pair(b, rank[a], rank[j])) continue;
                pick = j;
            }
    if (pick < 0) {
        b->used[rank[a]] = 1;
        return 0;
    }
    return book_singles(b, rank[a], rank[pick], U_NONE, U_NONE, U_STIP_NORMAL) ? 1 : -1;
}

/* Hottest rivalry whose two members are both still unbooked. If id is not
 * U_NONE, the rivalry must involve that wrestler. */
static int best_open_rivalry(const Booker *b, uint8_t id)
{
    const Universe *u = b->u;
    int i, best = -1, women_full = women_matches(b) >= U_WOMEN_MATCHES;
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (!r->intensity || b->used[r->a] || b->used[r->b]) continue;
        if (u->w[r->a].gender != u->w[r->b].gender) continue;  /* (no intergender matches) */
        if (women_full && u->w[r->a].gender == U_FEMALE) continue;
        if (rested(b, r)) continue;
        if (id != U_NONE && r->a != id && r->b != id) continue;
        if (best < 0 || r->intensity > u->r[best].intensity) best = i;
    }
    return best;
}

/* Royal Rumble entrants: every healthy regular except the World champion
 * (best-ranked first if there are more than U_RUMBLE_MAX); fill-ins only
 * when fewer than U_RUMBLE_MAX regulars are available; then a multiple of 4
 * (No Mercy's Rumble sizes) in random entry order. */
static void book_rumble(Booker *b, const uint8_t *rank, int n_rank)
{
    Universe *u = b->u;
    uint8_t pool[U_MAX_WRESTLERS], fill[U_MAX_WRESTLERS];
    uint8_t champ = u->n_titles && u->t[0].kind == U_TITLE_SINGLES ? u_title_holder(u, 0) : U_NONE;
    int np = 0, nf = 0, i, n;
    UMatch *m;
    for (i = 0; i < n_rank && np < U_RUMBLE_MAX; i++)
        if (rank[i] != champ && !u->w[rank[i]].injured_weeks) pool[np++] = rank[i];
    for (i = 0; i < u->n_wrestlers; i++)
        if (u->w[i].role == U_ROLE_FILLIN && u->w[i].active && !u->w[i].injured_weeks)
            fill[nf++] = (uint8_t)i;
    while (np < U_RUMBLE_MAX && nf) {         /* random fill-ins */
        int k = (int)u_rand(u, (uint32_t)nf);
        pool[np++] = fill[k];
        fill[k] = fill[--nf];
    }
    n = np - np % 4;
    if (n < 4 || !(m = new_match(b, U_FMT_RUMBLE, U_NONE, U_STIP_NORMAL))) return;
    for (i = n - 1; i > 0; i--) {             /* entry order */
        int k = (int)u_rand(u, (uint32_t)(i + 1));
        uint8_t t = pool[i];
        pool[i] = pool[k];
        pool[k] = t;
    }
    m->n_entrants = (uint8_t)n;
    for (i = 0; i < n; i++) m->entrant[i] = pool[i];
}

/* The King of the Ring PPV (notes/universe-plan.md): one World title
 * defence, one hot rivalry, and the 16-man tournament from the best-ranked
 * wrestlers left (seeded 1 v 16, 8 v 9, ...; 8 men if fewer are available).
 * Card order: first round, quarterfinals, the rivalry, semifinals, the title
 * match, the final as the main event. */
static uint8_t feud_stip(Booker *b, const URivalry *r, int title_match);

static void book_kotr(Booker *b, const uint8_t *rank, int n)
{
    static const uint8_t SEED16[16] = {0, 15, 7, 8, 4, 11, 3, 12, 2, 13, 5, 10, 6, 9, 1, 14};
    static const uint8_t SEED8[8] = {0, 7, 3, 4, 2, 5, 1, 6};
    Universe *u = b->u;
    uint8_t champ = u->n_titles && u->t[0].kind == U_TITLE_SINGLES ? u_title_holder(u, 0) : U_NONE;
    uint8_t ent[16], title[2] = {U_NONE, U_NONE}, feud[2] = {U_NONE, U_NONE};
    int i, ne = 0, size, first, ri = -1;
    UMatch *m;

    if (champ != U_NONE && !b->used[champ]) {            /* the title defence */
        for (i = 0; i < n; i++)
            if (rank[i] != champ && !b->used[rank[i]]) break;
        if (i < n) {
            title[0] = champ;
            title[1] = rank[i];
            b->used[champ] = b->used[rank[i]] = 1;
        }
    }
    {                                                     /* the hottest feud */
        int k;
        for (k = 0; k < U_MAX_RIVALRIES; k++) {
            const URivalry *r = &u->r[k];
            if (!r->intensity || b->used[r->a] || b->used[r->b]) continue;
            if (ri < 0 || r->intensity > u->r[ri].intensity) ri = k;
        }
        if (ri >= 0) {
            feud[0] = u->r[ri].a;
            feud[1] = u->r[ri].b;
            b->used[feud[0]] = b->used[feud[1]] = 1;
        }
    }
    for (i = 0; i < n && ne < 16; i++)
        if (!b->used[rank[i]]) ent[ne++] = rank[i];
    size = ne >= 16 ? 16 : ne >= 8 ? 8 : 0;
    first = size == 16 ? 0 : 8;                           /* bracket position of round 1 */

    for (i = 0; i < size / 2; i++) {                      /* first round, seeded */
        uint8_t x = ent[size == 16 ? SEED16[2 * i] : SEED8[2 * i]];
        uint8_t y = ent[size == 16 ? SEED16[2 * i + 1] : SEED8[2 * i + 1]];
        if ((m = book_singles(b, x, y, U_NONE, U_NONE, U_STIP_NORMAL)) != 0)
            m->kotr = (uint8_t)(1 + first + i);
    }
    for (i = first + size / 2; size && i < U_KOTR_MATCHES; i++) {  /* later rounds: TBD */
        if (i == 12 && feud[0] != U_NONE)
            book_singles(b, feud[0], feud[1], U_NONE, (uint8_t)ri, feud_stip(b, &u->r[ri], 0));
        if (i == 14 && title[0] != U_NONE)
            book_singles(b, title[0], title[1], 0, U_NONE, U_STIP_NORMAL);
        if ((m = new_match(b, U_FMT_SINGLE, U_NONE, U_STIP_NORMAL)) != 0) m->kotr = (uint8_t)(1 + i);
    }
    if (!size) {                                          /* (not enough men) */
        if (feud[0] != U_NONE)
            book_singles(b, feud[0], feud[1], U_NONE, (uint8_t)ri, feud_stip(b, &u->r[ri], 0));
        if (title[0] != U_NONE) book_singles(b, title[0], title[1], 0, U_NONE, U_STIP_NORMAL);
    }
}

/* Interim stipulations, so every match type turns up until the booking
 * step of notes/universe-plan.md (step 8) brings the real rules: weekly TV
 * hot feuds are sometimes a Cage or No DQ match; PPV hot feuds get Ladder
 * (title matches), Ironman, Cage or No DQ. */
static uint8_t feud_stip(Booker *b, const URivalry *r, int title_match)
{
    uint32_t roll = u_rand(b->u, 100);
    if (b->s->kind != U_SHOW_PPV) {
        if (r->intensity < 50) return U_STIP_NORMAL;
        return roll < 12 ? U_STIP_CAGE : roll < 24 ? U_STIP_NODQ : U_STIP_NORMAL;
    }
    if (r->intensity >= 70) {
        if (title_match)
            return roll < 30 ? U_STIP_LADDER : roll < 55 ? U_STIP_IRONMAN
                 : roll < 75 ? U_STIP_CAGE : roll < 90 ? U_STIP_NODQ : U_STIP_NORMAL;
        return roll < 35 ? U_STIP_CAGE : roll < 70 ? U_STIP_NODQ
             : roll < 85 ? U_STIP_IRONMAN : U_STIP_NORMAL;
    }
    return roll < 25 ? U_STIP_NODQ : U_STIP_NORMAL;
}

/* Interim Guest Referee (until rivalries can involve three wrestlers, steps
 * 7-8): a PPV feud match without another stipulation sometimes gets the
 * best-ranked free wrestler as its referee - never a champion (they defend)
 * or someone whose own feud is still to be booked. */
static void maybe_referee(Booker *b, UMatch *m, const uint8_t *rank, int n)
{
    int j, k;
    if (!m || b->s->kind != U_SHOW_PPV || m->stip != U_STIP_NORMAL ||
        !u_stip_supported(m->format, U_STIP_REFEREE) || u_rand(b->u, 100) >= 30)
        return;
    for (j = 0; j < n; j++) {
        uint8_t id = rank[j];
        int champ = is_singles_champion(b->u, id);
        for (k = 0; k < b->u->n_teams && !champ; k++)
            champ = is_tag_champion(b->u, (uint8_t)k) &&
                    (b->u->tm[k].a == id || b->u->tm[k].b == id);
        if (!b->used[id] && !champ && best_open_rivalry(b, id) < 0) {
            m->stip = U_STIP_REFEREE;
            m->referee = id;
            b->used[id] = 1;
            return;
        }
    }
}

/*
 * Booking priorities:
 *   1. Singles titles. PPV: every title is defended. Weekly TV: a title
 *      match 1 time in 3, otherwise the champion faces the contender in a
 *      non-title match to build towards the PPV. Vacant titles are fought
 *      over by the top two contenders.
 *   2. Tag titles, the same way.
 *   3. Active rivalries, hottest first (PPV feuds get stipulations).
 *   4. One extra tag match if two more teams are free.
 *   5. Everyone else paired by ranking, preferring face vs heel.
 * The first match booked is the most important, so the card is reversed
 * at the end to put it last (main event).
 */
/* A weekly title defence gets likelier the longer the title has gone
 * without one (the World title most of all); PPVs defend every title. */
static int title_due(Universe *u, uint8_t t, int ppv)
{
    const UTitle *ti = &u->t[t];
    int weeks = u->week > ti->last_week ? u->week - ti->last_week : 0;
    int chance = clamp(15 + 15 * weeks, 15, 85) + (t == 0 ? 10 : 0);
    return ppv || (int)u_rand(u, 100) < chance;
}

/* The best-ranked free contender who doesn't hold a singles title */
static int free_contender(const Booker *b, const uint8_t *con, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (!b->used[con[i]] && !is_singles_champion(b->u, con[i])) return con[i];
    return -1;
}

/* How big a match is: the card runs from the least to the most important
 * (the main event last). The World title first, then the Royal Rumble's own
 * main event, the tag and secondary titles, hot feuds by their heat and
 * star power, the
 * rest by the wrestlers' ability. */
static int prominence(const Universe *u, const UMatch *m)
{
    int k, sum = 0, cnt = 0;
    if (m->format == U_FMT_RUMBLE) return 1000;
    if (m->title == 0) return 500;
    if (m->title != U_NONE)
        return u->t[m->title].kind == U_TITLE_TAG ? 220 : clamp(270 - 20 * m->title, 100, 250);
    for (k = 0; k < 6; k++)
        if (m->side[k / 2][k % 2] != U_NONE) {
            const UWrestler *w = &u->w[m->side[k / 2][k % 2]];
            sum += w->gender == U_FEMALE ? w->overall / 2 : w->overall;
            cnt++;
        }
    if (cnt) sum /= cnt;
    /* feuds by heat, then star power (the biggest names' feud main-events) */
    if (m->rivalry != U_NONE) return 30 + 2 * u->r[m->rivalry].intensity + sum;
    return sum;
}

/* One man's characters (Mankind, Cactus Jack, Mick Foley): only one of
 * them may be booked on a show */
static int holds_a_title(const Universe *u, uint8_t id)
{
    int t;
    if (is_singles_champion(u, id)) return 1;
    for (t = 0; t < u->n_teams; t++)
        if ((u->tm[t].a == id || u->tm[t].b == id) && is_tag_champion(u, (uint8_t)t)) return 1;
    return 0;
}

static void one_persona(Booker *b)
{
    Universe *u = b->u;
    int i, j;
    for (i = 0; i < u->n_wrestlers; i++) {
        int free = 0, keep = -1;
        if (!u->w[i].persona || b->used[i]) continue;
        for (j = i; j < u->n_wrestlers; j++)
            if (u->w[j].persona == u->w[i].persona && !b->used[j]) {
                free++;
                if (keep < 0 && holds_a_title(u, (uint8_t)j)) keep = j;   /* champions first */
            }
        if (free < 2) continue;
        for (j = i; keep < 0 && j < u->n_wrestlers; j++)                  /* then a feud */
            if (u->w[j].persona == u->w[i].persona && !b->used[j] && in_rivalry(u, (uint8_t)j))
                keep = j;
        if (keep < 0) {
            int pick = (int)u_rand(u, (uint32_t)free);
            for (j = i; j < u->n_wrestlers; j++)
                if (u->w[j].persona == u->w[i].persona && !b->used[j] && pick-- == 0) keep = j;
        }
        for (j = i; j < u->n_wrestlers; j++)
            if (u->w[j].persona == u->w[i].persona && j != keep) b->used[j] = 1;
    }
}

void u_book_show(Universe *u, UShow *s)
{
    Booker b;
    uint8_t men[U_MAX_WRESTLERS], women[U_MAX_WRESTLERS];
    uint8_t trank[U_MAX_TEAMS];
    int nm, nw, nt, i, j, t, ppv, women_booked = 0;

    u_zero(s, sizeof *s);
    s->week = u->week;
    u_next_show(u, &s->kind, &s->brand);   /* (Raw / SmackDown: names only) */
    ppv = s->kind == U_SHOW_PPV;
    b.u = u;
    b.s = s;
    b.max = ppv ? U_PPV_MATCHES : U_WEEKLY_MATCHES;
    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        const UWrestler *w = &u->w[i];
        b.used[i] = (uint8_t)(i >= u->n_wrestlers || !w->active ||
                              w->role != U_ROLE_REGULAR || w->injured_weeks > 0);
    }
    one_persona(&b);
    nm = u_division_rankings(u, U_DIV_MEN, men, U_MAX_WRESTLERS);
    nw = u_division_rankings(u, U_DIV_WOMEN, women, U_MAX_WRESTLERS);
    nt = u_team_rankings(u, U_NONE, trank, U_MAX_TEAMS);

    /* The Royal Rumble PPV's main event (booked first: the card is
     * reversed at the end). Its entrants may also wrestle earlier. */
    if (ppv && u_ppv_index(u) == 0) book_rumble(&b, men, nm);

    if (ppv && u_ppv_index(u) == 5) {                    /* King of the Ring */
        b.max = U_MAX_MATCHES;
        book_kotr(&b, men, nm);
        return;                                           /* (already in card order) */
    }

    /* Singles titles, World first (it gets the most main events) */
    for (t = 0; t < u->n_titles; t++) {
        UTitle *ti = &u->t[t];
        uint8_t h = u_title_holder(u, (uint8_t)t);
        uint8_t con[U_MAX_WRESTLERS];                     /* the title's contenders */
        int nc;
        uint8_t stip = t == 4 ? U_STIP_NODQ : U_STIP_NORMAL;   /* Hardcore: No DQ */
        if (!ti->active || ti->kind != U_TITLE_SINGLES) continue;
        nc = u_contenders(u, (uint8_t)t, con, U_MAX_WRESTLERS);
        if (h == U_NONE) {                                /* vacant: #1 vs #2 */
            int x = free_contender(&b, con, nc), y;
            if (x < 0) continue;
            b.used[x] = 1;
            y = free_contender(&b, con, nc);
            b.used[x] = 0;
            if (y >= 0) book_singles(&b, (uint8_t)x, (uint8_t)y, (uint8_t)t, U_NONE,
                                     ppv ? U_STIP_LADDER : stip);
            if (ti->division == U_DIV_WOMEN && y >= 0) women_booked = 1;
            continue;
        }
        if (b.used[h]) continue;
        /* WrestleMania: the World champion faces the Royal Rumble winner */
        if (ppv && t == 0 && u_ppv_index(u) == 2 && u->rumble_winner < u->n_wrestlers &&
            u->rumble_winner != h && !b.used[u->rumble_winner]) {
            int ri = find_rivalry(u, h, u->rumble_winner);
            book_singles(&b, h, u->rumble_winner, (uint8_t)t,
                         ri >= 0 ? (uint8_t)ri : U_NONE, U_STIP_NORMAL);
            continue;
        }
        if (!title_due(u, (uint8_t)t, ppv)) continue;  /* (his feud may still be booked) */
        {
            /* the champion's hottest feud with a top-5 contender (unless
             * the rival holds a singles title too), else the #1 contender */
            int ri = -1, k;
            for (k = 0; k < U_MAX_RIVALRIES; k++) {
                const URivalry *r = &u->r[k];
                uint8_t opp = r->a == h ? r->b : r->a;
                int top = 0, c;
                for (c = 0; c < nc && c < U_CONTENDERS; c++) top |= con[c] == opp;
                if (!r->intensity || (r->a != h && r->b != h) || b.used[opp] ||
                    is_singles_champion(u, opp) || !top || rested(&b, r))
                    continue;
                if (ri < 0 || r->intensity > u->r[ri].intensity) ri = k;
            }
            if (ri >= 0) {
                URivalry *r = &u->r[ri];
                uint8_t opp = r->a == h ? r->b : r->a;
                uint8_t fs = feud_stip(&b, r, 1);
                maybe_referee(&b, book_singles(&b, h, opp, (uint8_t)t, (uint8_t)ri,
                                               fs == U_STIP_NORMAL ? stip : fs), men, nm);
            } else {
                int c = free_contender(&b, con, nc);
                if (c < 0) continue;
                book_singles(&b, h, (uint8_t)c, (uint8_t)t, U_NONE, stip);
            }
            if (ti->division == U_DIV_WOMEN) women_booked = 1;
        }
    }

    for (t = 0; t < u->n_titles; t++) {
        UTitle *ti = &u->t[t];
        uint8_t h = u_title_holder(u, (uint8_t)t);
        int x = -1, y = -1;
        if (!ti->active || ti->kind != U_TITLE_TAG) continue;
        if (h != U_NONE && !team_free(&b, h)) continue;
        if (h != U_NONE && !title_due(u, (uint8_t)t, ppv)) continue;
        if (h != U_NONE) x = h;
        for (j = 0; j < nt; j++) {
            if (!team_free(&b, trank[j]) || trank[j] == x) continue;
            if (x < 0) x = trank[j];
            else { y = trank[j]; break; }
        }
        if (x < 0 || y < 0) continue;
        {
            /* interim (step 8 brings the real rules): PPV tag title
             * matches sometimes get a stipulation */
            uint8_t stip = U_STIP_NORMAL;
            uint32_t roll = u_rand(u, 100);
            if (h == U_NONE && ppv) stip = U_STIP_LADDER;
            else if (ppv) stip = roll < 20 ? U_STIP_LADDER : roll < 35 ? U_STIP_IRONMAN
                               : roll < 50 ? U_STIP_NODQ : U_STIP_NORMAL;
            book_tag(&b, (uint8_t)x, (uint8_t)y, (uint8_t)t, stip);
        }
    }

    for (i = 0; s->count < b.max && (ppv || i < U_WEEKLY_FEUDS); i++) {
        int ri = best_open_rivalry(&b, U_NONE);
        if (ri < 0) break;
        if (u->w[u->r[ri].a].gender == U_FEMALE) women_booked = 1;
        maybe_referee(&b, book_singles(&b, u->r[ri].a, u->r[ri].b, U_NONE, (uint8_t)ri,
                                       feud_stip(&b, &u->r[ri], 0)), men, nm);
    }

    /* The women's division: a match on every show */
    if (!women_booked && s->count < b.max)
        while (book_regular(&b, women, nw) == 0) {}

    for (i = 0; i < nt && s->count < b.max; i++) {
        if (!team_free(&b, trank[i]) || is_tag_champion(u, trank[i])) continue;
        for (j = i + 1; j < nt; j++) {
            if (team_free(&b, trank[j]) && !is_tag_champion(u, trank[j])) {
                book_tag(&b, trank[i], trank[j], U_NONE, U_STIP_NORMAL);
                break;
            }
        }
        break; /* one extra tag match per show */
    }

    /* Weekly TV sometimes has a Triple Threat or a Handicap match among
     * wrestlers still free (nearby in the rankings). */
    if (!ppv && s->count < b.max) {
        uint8_t f[3];
        int nf = 0, roll = (int)u_rand(u, 100);
        uint8_t stip = u_rand(u, 100) < 15 ? U_STIP_NODQ : U_STIP_NORMAL;  /* interim */
        for (i = 0; i < nm && nf < 3; i++) {
            if (b.used[men[i]]) continue;
            for (j = 0; j < nf && !allies(u, f[j], men[i]); j++) {}
            if (j == nf) f[nf++] = men[i];
        }
        if (nf == 3 && roll < 30) {
            book_triple(&b, f[0], f[1], f[2], U_NONE, stip);
        } else if (nf == 3 && roll < 50) {
            /* the best of the three alone against the other two */
            book_handicap(&b, f[0], f[1], f[2], stip);
        }
    }

    while (s->count < b.max && book_regular(&b, men, nm) >= 0) {}

    for (i = 1; i < s->count; i++) {      /* stable: least important first */
        UMatch tmp = s->m[i];
        int p = prominence(u, &tmp);
        for (j = i; j > 0 && prominence(u, &s->m[j - 1]) > p; j--) s->m[j] = s->m[j - 1];
        s->m[j] = tmp;
    }
}

/* ---------- results ---------- */

static int side_rating(const Universe *u, const UMatch *m, int side)
{
    int k, sum = 0, cnt = 0;
    for (k = 0; k < 2; k++) {
        uint8_t id = m->side[side][k];
        if (id == U_NONE) continue;
        sum += u->w[id].overall + u->w[id].momentum / 4;
        cnt++;
    }
    if (!cnt) return 0;
    sum /= cnt;
    if (m->format == U_FMT_TAG) {
        const UTeam *t = &u->tm[m->team[side]];
        sum += clamp((int)t->wins - (int)t->losses, -10, 10);
    }
    if (m->format == U_FMT_HANDICAP && side == 1) sum += 12;  /* two on one */
    return sum;
}

static uint8_t sim_winner(Universe *u, const UMatch *m)
{
    if (u_match_sides(m) == 3) {
        int r[3], wsum = 0, k, avg, roll;
        for (k = 0; k < 3; k++) r[k] = side_rating(u, m, k);
        avg = (r[0] + r[1] + r[2]) / 3;
        for (k = 0; k < 3; k++) wsum += r[k] = clamp(33 + r[k] - avg, 10, 60);
        roll = (int)u_rand(u, (uint32_t)wsum);
        for (k = 0; k < 2; k++) {
            if (roll < r[k]) return (uint8_t)k;
            roll -= r[k];
        }
        return 2;
    }
    {
        int p = clamp(50 + side_rating(u, m, 0) - side_rating(u, m, 1), 15, 85);
        return (int)u_rand(u, 100) < p ? 0 : 1;
    }
}

/* ---------- tournaments (King of the Ring) ---------- */

int u_match_ready(const UMatch *m)
{
    int k;
    if (m->format == U_FMT_RUMBLE) return m->n_entrants > 0;
    for (k = 0; k < u_match_sides(m); k++) {
        if (m->side[k][0] == U_NONE) return 0;
        if ((m->format == U_FMT_TAG || (m->format == U_FMT_HANDICAP && k == 1)) &&
            m->side[k][1] == U_NONE)
            return 0;
    }
    return 1;
}

const char *u_round_name(const UMatch *m)
{
    if (!m->kotr) return "";
    if (m->kotr <= 8) return "First Round";
    if (m->kotr <= 12) return "Quarterfinal";
    if (m->kotr <= 14) return "Semifinal";
    return "Final";
}

/* Bracket position p (0-14): later rounds take the winners of positions
 * 2(p-8) and 2(p-8)+1. A side is known once its feeder has a winner. */
void u_show_update(UShow *s)
{
    int at[U_KOTR_MATCHES], i, k;
    for (i = 0; i < U_KOTR_MATCHES; i++) at[i] = -1;
    for (i = 0; i < s->count; i++)
        if (s->m[i].kotr) at[s->m[i].kotr - 1] = i;
    for (i = 8; i < U_KOTR_MATCHES; i++) {
        UMatch *m;
        if (at[i] < 0 || s->m[at[i]].done) continue;
        if (at[2 * (i - 8)] < 0 && at[2 * (i - 8) + 1] < 0) continue;  /* an 8-man first round */
        m = &s->m[at[i]];
        for (k = 0; k < 2; k++) {
            int f = at[2 * (i - 8) + k];
            const UMatch *fm = f >= 0 ? &s->m[f] : 0;
            m->side[k][0] = fm && fm->done && fm->winner < 2 ? fm->side[fm->winner][0] : U_NONE;
        }
    }
}

void u_sim_show(Universe *u, UShow *s)
{
    int i, more = 1;
    while (more) {                     /* later rounds become ready */
        more = 0;
        for (i = 0; i < s->count; i++)
            if (!s->m[i].done && u_match_ready(&s->m[i])) {
                u_sim_match(u, &s->m[i]);
                u_show_update(s);
                more = 1;
            }
    }
}

/* Royal Rumble: the better and the later an entrant, the likelier the win */
static uint8_t sim_rumble(Universe *u, const UMatch *m)
{
    uint32_t w[U_RUMBLE_MAX], sum = 0, roll;
    int i;
    for (i = 0; i < m->n_entrants; i++) {
        const UWrestler *x = &u->w[m->entrant[i]];
        int r = clamp(x->overall + x->momentum / 4 - 45, 2, 60);
        w[i] = (uint32_t)(r * r) * (uint32_t)(m->n_entrants + 2 * i);
        sum += w[i];
    }
    roll = u_rand(u, sum);
    for (i = 0; i < m->n_entrants - 1; i++) {
        if (roll < w[i]) return (uint8_t)i;
        roll -= w[i];
    }
    return (uint8_t)(m->n_entrants - 1);
}

void u_sim_match(Universe *u, UMatch *m)
{
    uint32_t roll;

    if (!u_match_ready(m)) return;
    if (m->format == U_FMT_RUMBLE) {
        m->winner = m->n_entrants ? sim_rumble(u, m) : U_NONE;
        m->method = U_WIN_RUMBLE;
        m->source = U_SRC_SIMULATED;
        m->done = 1;
        return;
    }

    m->winner = sim_winner(u, m);
    roll = u_rand(u, 100);
    switch (m->stip) {
    case U_STIP_CAGE:
        m->method = roll < 45 ? U_WIN_PINFALL : roll < 60 ? U_WIN_SUBMISSION
                                                          : U_WIN_SPECIAL;
        break;
    case U_STIP_LADDER:
        m->method = U_WIN_SPECIAL;
        break;
    case U_STIP_IRONMAN: {
        uint8_t wf = (uint8_t)(1 + u_rand(u, 5));
        int k;
        m->method = U_WIN_FALLS;
        for (k = 0; k < u_match_sides(m); k++)
            m->falls[k] = k == m->winner ? wf : (uint8_t)u_rand(u, wf);
        break;
    }
    case U_STIP_NODQ:
        m->method = roll < 72 ? U_WIN_PINFALL : U_WIN_SUBMISSION;
        break;
    default:
        m->method = roll < 70 ? U_WIN_PINFALL : roll < 88 ? U_WIN_SUBMISSION
                  : roll < 95 ? U_WIN_COUNTOUT : U_WIN_DQ;
    }
    m->source = U_SRC_SIMULATED;
    m->done = 1;
}

/* Record the result of a match the player actually played. */
int u_set_result(UMatch *m, uint8_t winning_side, uint8_t method)
{
    if (winning_side >= u_match_sides(m) || method == U_WIN_FALLS ||
        method == U_WIN_DRAW || (m->format == U_FMT_RUMBLE) != (method == U_WIN_RUMBLE) ||
        !u_match_ready(m))
        return -1;
    if (!u_method_allowed(m->stip, method)) return -1;
    m->winner = winning_side;
    m->method = method;
    m->source = U_SRC_PLAYED;
    m->done = 1;
    return 0;
}

/* A match the player played ended without a winner */
int u_set_draw(UMatch *m)
{
    /* (Rumbles and tournament matches always have a winner) */
    if (m->stip >= U_STIP_COUNT || m->format == U_FMT_RUMBLE || m->kotr || !u_match_ready(m))
        return -1;
    m->winner = U_NONE;
    m->method = U_WIN_DRAW;
    m->source = U_SRC_PLAYED;
    m->done = 1;
    return 0;
}

/* An Ironman match the player played: falls scored by each side. The side
 * with the most falls wins; level at the top is a draw. */
int u_set_ironman_result(UMatch *m, const uint8_t *falls)
{
    int k, n = u_match_sides(m), best = 0, level = 0;
    if (m->stip != U_STIP_IRONMAN || !u_match_ready(m)) return -1;
    for (k = 1; k < n; k++) {
        if (falls[k] > falls[best]) { best = k; level = 0; }
        else if (falls[k] == falls[best]) level = 1;
    }
    for (k = 0; k < 3; k++) m->falls[k] = k < n ? falls[k] : 0;
    if (level) return u_set_draw(m);
    m->winner = (uint8_t)best;
    m->method = U_WIN_FALLS;
    m->source = U_SRC_PLAYED;
    m->done = 1;
    return 0;
}

static void add_momentum(UWrestler *w, int d)
{
    w->momentum = (int8_t)clamp(w->momentum + d, -100, 100);
}

static int side_score(const Universe *u, const UMatch *m, int side)
{
    int k, sum = 0, cnt = 0;
    if (m->format == U_FMT_TAG) return u_team_score(u, m->team[side]);
    for (k = 0; k < 2; k++)
        if (m->side[side][k] != U_NONE) {
            sum += u_score(&u->w[m->side[side][k]]);
            cnt++;
        }
    return cnt ? sum / cnt : 0;
}

/* A draw: nobody wins or loses, a champion keeps the title, the feud is
 * not settled (it heats up); injuries can still happen. */
static void apply_draw(Universe *u, UMatch *m, uint8_t *touched)
{
    int ri = m->rivalry, n = u_match_sides(m), L, k, cnt = 0;
    uint8_t ids[6];

    if (m->title != U_NONE) {
        UTitle *t = &u->t[m->title];
        UReign *cur = current_reign(t);
        t->last_week = u->week;
        if (!t->vacant && cur && cur->defenses < 255) cur->defenses++;
    }
    if (ri == U_NONE && m->format == U_FMT_SINGLE)
        ri = find_rivalry(u, m->side[0][0], m->side[1][0]);
    if (ri != U_NONE && ri >= 0 && u->r[ri].intensity) {
        URivalry *r = &u->r[ri];
        touched[ri] = 1;
        r->last_show = (uint16_t)(4u * u->week + u->slot);
        if (r->weeks < 255) r->weeks++;
        r->intensity = (uint8_t)clamp(r->intensity + 15, 1, 100);
    }
    for (L = 0; L < n; L++)
        for (k = 0; k < 2; k++)
            if (m->side[L][k] != U_NONE) ids[cnt++] = m->side[L][k];
    if (cnt && u_rand(u, 100) < 4) {
        uint8_t id = ids[u_rand(u, (uint32_t)cnt)];
        u->w[id].injured_weeks = (uint8_t)(1 + u_rand(u, 3));
        m->injured = id;
        m->injury_weeks = u->w[id].injured_weeks;
    }
}

/* ---- ranking points ---- */

static void score(uint16_t *points, int8_t *streak, int delta, int won)
{
    *points = (uint16_t)clamp((int)*points + delta, 0, U_PTS_MAX);
    if (won) *streak = (int8_t)(*streak > 0 ? clamp(*streak + 1, 1, 100) : 1);
    else     *streak = (int8_t)(*streak < 0 ? clamp(*streak - 1, -100, -1) : -1);
}

/* a side's ranking points: the team's, or its wrestlers' average */
static int side_points(const Universe *u, const UMatch *m, int side)
{
    int k, sum = 0, cnt = 0;
    if (m->format == U_FMT_TAG) return u->tm[m->team[side]].points;
    for (k = 0; k < 2; k++)
        if (m->side[side][k] != U_NONE) {
            sum += u->w[m->side[side][k]].points;
            cnt++;
        }
    return cnt ? sum / cnt : 0;
}

static int win_points(int gap, int streak, int ppv, int alone)
{
    int p = U_PTS_WIN + clamp(gap / U_PTS_GAP, 0, U_PTS_GAP_MAX)
          + U_PTS_STREAK * clamp(streak, 0, U_PTS_STREAK_MAX) + (alone ? U_PTS_ALONE : 0);
    return ppv ? 2 * p : p;
}

/* Winners gain (more against a higher-ranked side, double at a PPV, a
 * streak bonus), losers pay (more when they were the higher-ranked side).
 * Tag matches score the teams. */
static void rank_points(Universe *u, const UShow *s, const UMatch *m, int W)
{
    int n = u_match_sides(m), sp[3], L, k, best = -1;
    int ppv = s->kind == U_SHOW_PPV;
    int alone = m->format == U_FMT_HANDICAP && W == 0;
    for (L = 0; L < n; L++) sp[L] = side_points(u, m, L);
    for (L = 0; L < n; L++)
        if (L != W && (best < 0 || sp[L] > best)) best = sp[L];
    for (L = 0; L < n; L++) {
        int loss = -(U_PTS_LOSS + clamp((sp[L] - sp[W]) / U_PTS_GAP, 0, U_PTS_GAP_MAX));
        if (m->format == U_FMT_TAG) {
            UTeam *t = &u->tm[m->team[L]];
            score(&t->points, &t->streak,
                  L == W ? win_points(best - sp[W], t->streak, ppv, 0) : loss, L == W);
            continue;
        }
        for (k = 0; k < 2; k++) {
            UWrestler *w;
            if (m->side[L][k] == U_NONE) continue;
            w = &u->w[m->side[L][k]];
            score(&w->points, &w->streak,
                  L == W ? win_points(best - sp[W], w->streak, ppv, alone) : loss, L == W);
        }
    }
}

/* Every week points decay by a tenth; injured wrestlers lose another tenth */
static void decay_points(Universe *u)
{
    int i;
    for (i = 0; i < u->n_wrestlers; i++) {
        UWrestler *w = &u->w[i];
        w->points = (uint16_t)(w->points - w->points / 10);
        if (w->injured_weeks) w->points = (uint16_t)(w->points - w->points / 10);
    }
    for (i = 0; i < u->n_teams; i++)
        u->tm[i].points = (uint16_t)(u->tm[i].points - u->tm[i].points / 10);
}

/* The Royal Rumble winner earns a World title shot at WrestleMania (a
 * fill-in winner gets the glory but no shot). Nobody else gets a loss. */
static void apply_rumble(Universe *u, UMatch *m)
{
    uint8_t id;
    if (m->winner >= m->n_entrants) return;
    id = m->entrant[m->winner];
    u->w[id].wins++;
    score(&u->w[id].points, &u->w[id].streak, U_PTS_RUMBLE, 1);
    add_momentum(&u->w[id], 15);
    if (u->w[id].role == U_ROLE_REGULAR) u->rumble_winner = id;
    if (u_rand(u, 100) < 4) {
        uint8_t hurt = m->entrant[u_rand(u, m->n_entrants)];
        if (u->w[hurt].role == U_ROLE_REGULAR && hurt != id) {
            u->w[hurt].injured_weeks = (uint8_t)(1 + u_rand(u, 3));
            m->injured = hurt;
            m->injury_weeks = u->w[hurt].injured_weeks;
        }
    }
}

static void apply_match(Universe *u, const UShow *s, UMatch *m, uint8_t *touched)
{
    int W = m->winner, n = u_match_sides(m), L, k, ri, best_loser = -1;
    int decisive = m->method == U_WIN_PINFALL || m->method == U_WIN_SUBMISSION ||
                   m->method == U_WIN_SPECIAL || m->method == U_WIN_FALLS;
    int tag = m->format == U_FMT_TAG;
    int upset;
    uint8_t champ_id;
    uint8_t losers[4];
    int nl = 0;

    if (m->format == U_FMT_RUMBLE) {
        apply_rumble(u, m);
        return;
    }
    if (m->method == U_WIN_DRAW || W >= n) {
        apply_draw(u, m, touched);
        return;
    }
    champ_id = tag ? m->team[W] : m->side[W][0];
    for (L = 0; L < n; L++)
        if (L != W && (best_loser < 0 || side_score(u, m, L) > side_score(u, m, best_loser)))
            best_loser = L;
    upset = side_score(u, m, W) + 10 < side_score(u, m, best_loser);
    rank_points(u, s, m, W);

    for (L = 0; L < n; L++) {
        for (k = 0; k < 2; k++) {
            uint8_t id = m->side[L][k];
            if (id == U_NONE) continue;
            if (L == W) {
                u->w[id].wins++;
                add_momentum(&u->w[id], (upset ? 14 : 8) - (tag ? 2 : 0));
            } else {
                u->w[id].losses++;
                add_momentum(&u->w[id], tag ? -4 : -6);
                if (nl < 4) losers[nl++] = id;
            }
        }
    }
    if (tag) {
        u->tm[m->team[W]].wins++;
        u->tm[m->team[1 - W]].losses++;
    }

    /* Titles only change hands decisively (not by count-out or DQ). */
    if (m->title != U_NONE) {
        UTitle *t = &u->t[m->title];
        t->last_week = u->week;
        UReign *cur = current_reign(t);
        if (!t->vacant && cur && (cur->holder == champ_id || !decisive)) {
            if (cur->defenses < 255) cur->defenses++;
        } else if (decisive) {
            UReign *nr;
            if (!t->vacant && cur) cur->lost_week = u->week;
            t->vacant = 0;
            t->reign_count++;
            if (t->reign_count == 0) t->reign_count = U_MAX_REIGNS; /* keep ring valid */
            nr = current_reign(t);
            nr->holder = champ_id;
            nr->defenses = 0;
            nr->won_week = u->week;
            nr->lost_week = U_OPEN_WEEK;
            m->title_changed = 1;
            /* the belt goes to the new champion's brand (matters at PPVs) */
            t->brand = u->w[m->side[W][0]].brand;
            for (k = 0; k < 2; k++)
                if (m->side[W][k] != U_NONE) add_momentum(&u->w[m->side[W][k]], 10);
        }
    }

    /* Rivalries: cheap finishes add heat, a clean win settles it at a PPV
     * or after 4 shows. In singles, upsets and cheap finishes start new ones. */
    ri = m->rivalry;
    if (ri == U_NONE && m->format == U_FMT_SINGLE)
        ri = find_rivalry(u, m->side[0][0], m->side[1][0]);
    if (ri != U_NONE && ri >= 0 && u->r[ri].intensity) {
        URivalry *r = &u->r[ri];
        touched[ri] = 1;
        r->last_show = (uint16_t)(4u * u->week + u->slot);
        if (r->weeks < 255) r->weeks++;
        if (!decisive) {
            r->intensity = (uint8_t)clamp(r->intensity + 20, 1, 100);
        } else if (s->kind == U_SHOW_PPV || r->weeks >= 4) {
            r->intensity = 0;
            m->rivalry_ended = 1;
        } else {
            r->intensity = (uint8_t)clamp(r->intensity + 10, 1, 100);
        }
    } else if (m->format == U_FMT_SINGLE) {
        uint8_t a = m->side[0][0], b = m->side[1][0];
        if ((upset || !decisive) && !in_rivalry(u, a) && !in_rivalry(u, b) &&
            u_rand(u, 100) < 50) {
            int nr = u_add_rivalry(u, a, b, 30);
            if (nr >= 0) {
                touched[nr] = 1;
                m->rivalry_started = 1;
            }
        }
    }

    /* King of the Ring: the final's winner is crowned */
    if (m->kotr == U_KOTR_MATCHES && !tag) {
        u->king = m->side[W][0];
        add_momentum(&u->w[u->king], 20);
    }

    if (nl && u_rand(u, 100) < 4) {
        uint8_t id = losers[u_rand(u, (uint32_t)nl)];
        u->w[id].injured_weeks = (uint8_t)(1 + u_rand(u, 3));
        m->injured = id;
        m->injury_weeks = u->w[id].injured_weeks;
    }
}

void u_apply_show(Universe *u, UShow *s)
{
    uint8_t touched[U_MAX_RIVALRIES];
    int i, wrestlemania = s->kind == U_SHOW_PPV && u_ppv_index(u) == 2;

    u_zero(touched, sizeof touched);
    if (u->slot == 0) {   /* a new week: points decay, injuries heal by one week */
        decay_points(u);
        for (i = 0; i < u->n_wrestlers; i++)
            if (u->w[i].injured_weeks) u->w[i].injured_weeks--;
    }

    for (i = 0; i < s->count; i++) {
        UMatch *m = &s->m[i];
        int k;
        if (!m->done) continue;
        for (k = 0; k < 6; k++)
            if (m->side[k / 2][k % 2] != U_NONE)
                u->w[m->side[k / 2][k % 2]].last_show = (uint16_t)(4u * u->week + u->slot);
        for (k = 0; m->format == U_FMT_RUMBLE && k < m->n_entrants; k++)
            u->w[m->entrant[k]].last_show = (uint16_t)(4u * u->week + u->slot);
        apply_match(u, s, m, touched);
    }

    /* Rivalries that weren't on this show cool off (one roster: every
     * show could have had them) */
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        URivalry *r = &u->r[i];
        if (!r->intensity || touched[i]) continue;
        r->intensity = (uint8_t)clamp(r->intensity - 5, 0, 100);
    }
    if (wrestlemania) u->rumble_winner = U_NONE;   /* the shot is used (or lost) */

    advance_calendar(u);
}

/* ---------- save / load ---------- */

typedef struct {
    uint8_t *p;
    int n, cap, err;
} UWriter;

static void w8(UWriter *w, uint32_t v)
{
    if (w->n >= w->cap) { w->err = 1; return; }
    w->p[w->n++] = (uint8_t)v;
}
static void w16(UWriter *w, uint32_t v) { w8(w, v >> 8); w8(w, v); }
static void w32(UWriter *w, uint32_t v) { w16(w, v >> 16); w16(w, v); }
static void wname(UWriter *w, const char *s)
{
    int i;
    for (i = 0; i < U_NAME_LEN; i++) w8(w, (uint8_t)s[i]);
}

typedef struct {
    const uint8_t *p;
    int n;
} UReader;

static uint8_t r8(UReader *r) { return r->p[r->n++]; }
static uint16_t r16(UReader *r) { uint16_t hi = r8(r); return (uint16_t)(hi << 8 | r8(r)); }
static uint32_t r32(UReader *r) { uint32_t hi = r16(r); return hi << 16 | r16(r); }
static void rname(UReader *r, char *s)
{
    int i;
    for (i = 0; i < U_NAME_LEN; i++) s[i] = (char)r8(r);
    s[U_NAME_LEN - 1] = 0;
}

static uint16_t fletcher16(const uint8_t *p, int n)
{
    uint16_t a = 0, b = 0;
    while (n--) {
        a = (uint16_t)((a + *p++) % 255);
        b = (uint16_t)((b + a) % 255);
    }
    return (uint16_t)(b << 8 | a);
}

int u_save(const Universe *u, uint8_t *buf, int cap)
{
    UWriter w;
    int i, k;

    w.p = buf;
    w.n = 0;
    w.cap = cap;
    w.err = 0;
    w8(&w, 'N'); w8(&w, 'M'); w8(&w, 'U'); w8(&w, U_SAVE_VERSION);
    w16(&w, u->week);
    w8(&w, u->slot);
    w32(&w, u->rng);
    w8(&w, u->rumble_winner);
    w8(&w, u->king);
    w8(&w, u->n_wrestlers);
    w8(&w, u->n_teams);
    w8(&w, u->n_titles);
    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        const UWrestler *x = &u->w[i];
        wname(&w, x->name);
        w8(&w, x->game_slot); w8(&w, x->brand); w8(&w, x->align);
        w8(&w, x->overall); w8(&w, (uint8_t)x->momentum);
        w8(&w, x->injured_weeks); w8(&w, x->active); w8(&w, x->role);
        w8(&w, x->gender); w8(&w, x->game_attire); w16(&w, x->weight); w8(&w, x->persona);
        w16(&w, x->wins); w16(&w, x->losses);
        w16(&w, x->points); w8(&w, (uint8_t)x->streak); w16(&w, x->last_show);
    }
    for (i = 0; i < U_MAX_TEAMS; i++) {
        const UTeam *t = &u->tm[i];
        wname(&w, t->name);
        w8(&w, t->a); w8(&w, t->b); w8(&w, t->active);
        w16(&w, t->wins); w16(&w, t->losses);
        w16(&w, t->points); w8(&w, (uint8_t)t->streak);
    }
    for (i = 0; i < U_MAX_TITLES; i++) {
        const UTitle *t = &u->t[i];
        wname(&w, t->name);
        w8(&w, t->brand); w8(&w, t->kind); w8(&w, t->active);
        w8(&w, t->vacant); w8(&w, t->reign_count);
        w8(&w, t->division); w16(&w, t->last_week); w8(&w, t->no_world);
        for (k = 0; k < U_MAX_REIGNS; k++) {
            const UReign *rg = &t->history[k];
            w8(&w, rg->holder); w8(&w, rg->defenses);
            w16(&w, rg->won_week); w16(&w, rg->lost_week);
        }
    }
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        w8(&w, r->a); w8(&w, r->b); w8(&w, r->intensity); w8(&w, r->weeks);
        w16(&w, r->last_show);
    }
    for (i = 0; i < U_MAX_ALLIES; i++) {
        const UAlly *x = &u->al[i];
        w8(&w, x->a); w8(&w, x->b); w8(&w, x->level);
    }
    if (w.err) return -1;
    w16(&w, fletcher16(buf, w.n));
    return w.err ? -1 : w.n;
}

static int load_fail(Universe *u, int code)
{
    u_zero(u, sizeof *u);
    return code;
}

int u_load(Universe *u, const uint8_t *buf, int len)
{
    UReader r;
    int i, k;
    uint8_t nw, nteams, nt;

    if (len < U_SAVE_SIZE) return -1;
    if (buf[0] != 'N' || buf[1] != 'M' || buf[2] != 'U') return -2;
    if (buf[3] != U_SAVE_VERSION) return -3;
    if (fletcher16(buf, U_SAVE_SIZE - 2) !=
        (uint16_t)(buf[U_SAVE_SIZE - 2] << 8 | buf[U_SAVE_SIZE - 1]))
        return -4;

    u_zero(u, sizeof *u);
    r.p = buf;
    r.n = 4;
    u->week = r16(&r);
    u->slot = r8(&r);
    u->rng = r32(&r);
    u->rumble_winner = r8(&r);
    u->king = r8(&r);
    nw = u->n_wrestlers = r8(&r);
    nteams = u->n_teams = r8(&r);
    nt = u->n_titles = r8(&r);
    if (nw > U_MAX_WRESTLERS || nteams > U_MAX_TEAMS || nt > U_MAX_TITLES ||
        u->slot > 2 || !u->rng || (u->rumble_winner != U_NONE && u->rumble_winner >= nw) ||
        (u->king != U_NONE && u->king >= nw))
        return load_fail(u, -5);

    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        UWrestler *x = &u->w[i];
        rname(&r, x->name);
        x->game_slot = r8(&r); x->brand = r8(&r); x->align = r8(&r);
        x->overall = r8(&r); x->momentum = (int8_t)r8(&r);
        x->injured_weeks = r8(&r); x->active = r8(&r); x->role = r8(&r);
        x->gender = r8(&r); x->game_attire = r8(&r); x->weight = r16(&r); x->persona = r8(&r);
        x->wins = r16(&r); x->losses = r16(&r);
        x->points = r16(&r); x->streak = (int8_t)r8(&r); x->last_show = r16(&r);
        if (i < nw && (x->brand >= U_BRAND_COUNT || x->role > U_ROLE_FILLIN ||
                       x->gender > U_FEMALE || x->game_attire > 3))
            return load_fail(u, -6);
    }
    for (i = 0; i < U_MAX_TEAMS; i++) {
        UTeam *t = &u->tm[i];
        rname(&r, t->name);
        t->a = r8(&r); t->b = r8(&r); t->active = r8(&r);
        t->wins = r16(&r); t->losses = r16(&r);
        t->points = r16(&r); t->streak = (int8_t)r8(&r);
        if (i < nteams && (t->a >= nw || t->b >= nw || t->a == t->b))
            return load_fail(u, -6);
    }
    for (i = 0; i < U_MAX_TITLES; i++) {
        UTitle *t = &u->t[i];
        rname(&r, t->name);
        t->brand = r8(&r); t->kind = r8(&r); t->active = r8(&r);
        t->vacant = r8(&r); t->reign_count = r8(&r);
        t->division = r8(&r); t->last_week = r16(&r); t->no_world = r8(&r);
        if (i < nt && t->division > U_DIV_LIGHT) return load_fail(u, -6);
        for (k = 0; k < U_MAX_REIGNS; k++) {
            UReign *rg = &t->history[k];
            rg->holder = r8(&r); rg->defenses = r8(&r);
            rg->won_week = r16(&r); rg->lost_week = r16(&r);
        }
    }
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        URivalry *rv = &u->r[i];
        rv->a = r8(&r); rv->b = r8(&r); rv->intensity = r8(&r); rv->weeks = r8(&r);
        rv->last_show = r16(&r);
        if (rv->intensity && (rv->a >= nw || rv->b >= nw || rv->a == rv->b))
            return load_fail(u, -6);
    }
    for (i = 0; i < U_MAX_ALLIES; i++) {
        UAlly *x = &u->al[i];
        x->a = r8(&r); x->b = r8(&r); x->level = r8(&r);
        if (x->level && (x->a >= nw || x->b >= nw || x->a == x->b || x->level > 100))
            return load_fail(u, -6);
    }
    for (i = 0; i < nt; i++) {
        const UTitle *t = &u->t[i];
        int lim = t->kind == U_TITLE_TAG ? nteams : nw;
        uint8_t h = u_title_holder(u, (uint8_t)i);
        if (t->kind > U_TITLE_TAG || (h != U_NONE && h >= lim))
            return load_fail(u, -6);
        /* every stored reign holder is printed in history views */
        for (k = 0; k < U_MAX_REIGNS && k < t->reign_count; k++)
            if (t->history[k].holder >= lim) return load_fail(u, -6);
    }
    return 0;
}
