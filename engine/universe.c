/* No Mercy Universe engine - see universe.h for the design rules. */
#include "universe.h"

#define U_SAVE_VERSION 2

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

const char *u_show_name(const Universe *u)
{
    if (u->slot == U_BRAND_RAW) return "Raw Is War";
    if (u->slot == U_BRAND_SMACKDOWN) return "SmackDown!";
    return PPV_NAMES[(u->week / U_PPV_EVERY - 1) % 12];
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
        "", "Cage", "Ladder", "Ironman", "Guest Referee"};
    return stip < U_STIP_COUNT ? N[stip] : "";
}

const char *u_format_name(uint8_t format)
{
    static const char *const N[U_FMT_COUNT] = {
        "Single", "Tag Team", "Triple Threat", "Handicap"};
    return format < U_FMT_COUNT ? N[format] : "";
}

int u_match_sides(const UMatch *m)
{
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
    return method < U_WIN_SPECIAL ? N[method] : "?";
}

int u_method_allowed(uint8_t stip, uint8_t method)
{
    switch (stip) {
    case U_STIP_NORMAL:
    case U_STIP_REFEREE: return method <= U_WIN_DQ;
    case U_STIP_CAGE:    return method == U_WIN_PINFALL || method == U_WIN_SUBMISSION ||
                                method == U_WIN_SPECIAL;
    case U_STIP_LADDER:  return method == U_WIN_SPECIAL;
    case U_STIP_IRONMAN: return method == U_WIN_FALLS;
    }
    return 0;
}

/* ---------- setup ---------- */

void u_init(Universe *u, uint32_t seed)
{
    u_zero(u, sizeof *u);
    u->rng = seed ? seed : 0x4E4D5531u; /* xorshift must not start at 0 */
    u->week = 1;
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
    w->active = 1;
    return u->n_wrestlers++;
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

/* Active wrestlers of a brand (U_NONE = all), best first. */
int u_rankings(const Universe *u, uint8_t brand, uint8_t *out, int max)
{
    int n = 0, i, j;
    for (i = 0; i < u->n_wrestlers && n < max; i++) {
        if (!u->w[i].active) continue;
        if (brand != U_NONE && u->w[i].brand != brand) continue;
        out[n++] = (uint8_t)i;
    }
    for (i = 1; i < n; i++) { /* insertion sort, stable by id */
        uint8_t id = out[i];
        int s = u_score(&u->w[id]);
        for (j = i; j > 0 && u_score(&u->w[out[j - 1]]) < s; j--)
            out[j] = out[j - 1];
        out[j] = id;
    }
    return n;
}

int u_team_rankings(const Universe *u, uint8_t brand, uint8_t *out, int max)
{
    int n = 0, i, j;
    for (i = 0; i < u->n_teams && n < max; i++)
        if (team_on_brand(u, (uint8_t)i, brand)) out[n++] = (uint8_t)i;
    for (i = 1; i < n; i++) {
        uint8_t id = out[i];
        int s = u_team_score(u, id);
        for (j = i; j > 0 && u_team_score(u, out[j - 1]) < s; j--)
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

static void book_singles(Booker *b, uint8_t x, uint8_t y, uint8_t title,
                         uint8_t riv, uint8_t stip)
{
    UMatch *m = new_match(b, U_FMT_SINGLE, title, stip);
    if (!m) return;
    m->side[0][0] = x;
    m->side[1][0] = y;
    m->rivalry = riv;
    b->used[x] = b->used[y] = 1;
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
    return team_on_brand(b->u, team, b->s->brand) && !b->used[t->a] && !b->used[t->b];
}

/* Hottest rivalry whose two members are both still unbooked. If id is not
 * U_NONE, the rivalry must involve that wrestler. */
static int best_open_rivalry(const Booker *b, uint8_t id)
{
    const Universe *u = b->u;
    int i, best = -1;
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (!r->intensity || b->used[r->a] || b->used[r->b]) continue;
        if (id != U_NONE && r->a != id && r->b != id) continue;
        if (best < 0 || r->intensity > u->r[best].intensity) best = i;
    }
    return best;
}

/* Best-ranked available non-champion not saved for their own rivalry,
 * falling back to anyone available. */
static int pick_contender(const Booker *b, const uint8_t *rank, int n)
{
    int j;
    for (j = 0; j < n; j++)
        if (!b->used[rank[j]] && !is_singles_champion(b->u, rank[j]) &&
            best_open_rivalry(b, rank[j]) < 0)
            return rank[j];
    for (j = 0; j < n; j++)
        if (!b->used[rank[j]] && !is_singles_champion(b->u, rank[j])) return rank[j];
    return -1;
}

/* Interim (step 1): the full stipulation rules of notes/universe-plan.md
 * come with the booking step. Weekly TV: a hot feud is sometimes a Cage. */
static uint8_t feud_stip(Booker *b, const URivalry *r, int title_match)
{
    if (b->s->kind != U_SHOW_PPV)
        return r->intensity >= 50 && u_rand(b->u, 100) < 15 ? U_STIP_CAGE : U_STIP_NORMAL;
    if (r->intensity >= 70)
        return title_match && u_rand(b->u, 2) ? U_STIP_LADDER : U_STIP_CAGE;
    return U_STIP_NORMAL;
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
void u_book_show(Universe *u, UShow *s)
{
    Booker b;
    uint8_t rank[U_MAX_WRESTLERS], trank[U_MAX_TEAMS];
    int n, nt, i, j, t, ppv;

    u_zero(s, sizeof *s);
    s->week = u->week;
    u_next_show(u, &s->kind, &s->brand);
    ppv = s->kind == U_SHOW_PPV;
    b.u = u;
    b.s = s;
    b.max = ppv ? U_PPV_MATCHES : U_WEEKLY_MATCHES;
    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        const UWrestler *w = &u->w[i];
        b.used[i] = (uint8_t)(i >= u->n_wrestlers || !w->active ||
                              w->injured_weeks > 0 ||
                              (s->brand != U_NONE && w->brand != s->brand));
    }
    n = u_rankings(u, s->brand, rank, U_MAX_WRESTLERS);
    nt = u_team_rankings(u, s->brand, trank, U_MAX_TEAMS);

    for (t = 0; t < u->n_titles; t++) {
        UTitle *ti = &u->t[t];
        uint8_t h = u_title_holder(u, (uint8_t)t);
        if (!ti->active || ti->kind != U_TITLE_SINGLES) continue;
        if (!ppv && ti->brand != s->brand) continue;
        if (h == U_NONE) {
            int x = pick_contender(&b, rank, n), y;
            if (x < 0) continue;
            b.used[x] = 1;
            y = pick_contender(&b, rank, n);
            b.used[x] = 0;
            if (y >= 0) book_singles(&b, (uint8_t)x, (uint8_t)y, (uint8_t)t, U_NONE,
                                     ppv ? U_STIP_LADDER : U_STIP_NORMAL);
            continue;
        }
        if (b.used[h]) continue;
        {
            /* the champion's hottest feud, unless the rival holds a singles
             * title too (they must be free to defend their own) */
            int ri = -1, k;
            for (k = 0; k < U_MAX_RIVALRIES; k++) {
                const URivalry *r = &u->r[k];
                uint8_t opp = r->a == h ? r->b : r->a;
                if (!r->intensity || (r->a != h && r->b != h) || b.used[opp] ||
                    is_singles_champion(u, opp))
                    continue;
                if (ri < 0 || r->intensity > u->r[ri].intensity) ri = k;
            }
            int title_match = ppv || u_rand(u, 3) == 0;
            uint8_t tid = title_match ? (uint8_t)t : U_NONE;
            if (ri >= 0) {
                URivalry *r = &u->r[ri];
                uint8_t opp = r->a == h ? r->b : r->a;
                book_singles(&b, h, opp, tid, (uint8_t)ri, feud_stip(&b, r, title_match));
            } else {
                int c = pick_contender(&b, rank, n);
                if (c >= 0) book_singles(&b, h, (uint8_t)c, tid, U_NONE, U_STIP_NORMAL);
            }
        }
    }

    for (t = 0; t < u->n_titles; t++) {
        UTitle *ti = &u->t[t];
        uint8_t h = u_title_holder(u, (uint8_t)t);
        int x = -1, y = -1;
        if (!ti->active || ti->kind != U_TITLE_TAG) continue;
        if (!ppv && ti->brand != s->brand) continue;
        if (h != U_NONE && !team_free(&b, h)) continue;
        if (h != U_NONE) x = h;
        for (j = 0; j < nt; j++) {
            if (!team_free(&b, trank[j]) || trank[j] == x) continue;
            if (x < 0) x = trank[j];
            else { y = trank[j]; break; }
        }
        if (x < 0 || y < 0) continue;
        book_tag(&b, (uint8_t)x, (uint8_t)y,
                 (h == U_NONE || ppv || u_rand(u, 3) == 0) ? (uint8_t)t : U_NONE,
                 h == U_NONE && ppv ? U_STIP_LADDER : U_STIP_NORMAL);
    }

    while (s->count < b.max) {
        int ri = best_open_rivalry(&b, U_NONE);
        if (ri < 0) break;
        book_singles(&b, u->r[ri].a, u->r[ri].b, U_NONE, (uint8_t)ri,
                     feud_stip(&b, &u->r[ri], 0));
    }

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
        for (i = 0; i < n && nf < 3; i++)
            if (!b.used[rank[i]]) f[nf++] = rank[i];
        if (nf == 3 && roll < 30) {
            book_triple(&b, f[0], f[1], f[2], U_NONE, U_STIP_NORMAL);
        } else if (nf == 3 && roll < 50) {
            /* the best of the three alone against the other two */
            book_handicap(&b, f[0], f[1], f[2], U_STIP_NORMAL);
        }
    }

    for (i = 0; i < n && s->count < b.max; i++) {
        uint8_t a = rank[i];
        int pick = -1;
        if (b.used[a]) continue;
        for (j = i + 1; j < n && j <= i + 4; j++) { /* nearby, opposite alignment */
            if (!b.used[rank[j]] && u->w[rank[j]].align != u->w[a].align) {
                pick = j;
                break;
            }
        }
        for (j = i + 1; pick < 0 && j < n; j++)
            if (!b.used[rank[j]]) pick = j;
        if (pick < 0) break;
        book_singles(&b, a, rank[pick], U_NONE, U_NONE, U_STIP_NORMAL);
    }

    for (i = 0, j = s->count - 1; i < j; i++, j--) {
        UMatch tmp = s->m[i];
        s->m[i] = s->m[j];
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

void u_sim_match(Universe *u, UMatch *m)
{
    uint32_t roll;

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
        uint8_t wf = (uint8_t)(1 + u_rand(u, 5)), lf = (uint8_t)u_rand(u, wf);
        m->method = U_WIN_FALLS;
        m->falls[m->winner] = wf;
        m->falls[1 - m->winner] = lf;
        break;
    }
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
    if (winning_side >= u_match_sides(m) || method == U_WIN_FALLS) return -1;
    if (!u_method_allowed(m->stip, method)) return -1;
    m->winner = winning_side;
    m->method = method;
    m->source = U_SRC_PLAYED;
    m->done = 1;
    return 0;
}

/* An Ironman match the player played: falls scored by each side */
int u_set_ironman_result(UMatch *m, uint8_t falls0, uint8_t falls1)
{
    if (m->stip != U_STIP_IRONMAN || u_match_sides(m) != 2 || falls0 == falls1) return -1;
    m->falls[0] = falls0;
    m->falls[1] = falls1;
    m->winner = falls0 > falls1 ? 0 : 1;
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

static void apply_match(Universe *u, const UShow *s, UMatch *m, uint8_t *touched)
{
    int W = m->winner, n = u_match_sides(m), L, k, ri, best_loser = -1;
    int decisive = m->method == U_WIN_PINFALL || m->method == U_WIN_SUBMISSION ||
                   m->method == U_WIN_SPECIAL || m->method == U_WIN_FALLS;
    int tag = m->format == U_FMT_TAG;
    int upset;
    uint8_t champ_id = tag ? m->team[W] : m->side[W][0];
    uint8_t losers[4];
    int nl = 0;

    for (L = 0; L < n; L++)
        if (L != W && (best_loser < 0 || side_score(u, m, L) > side_score(u, m, best_loser)))
            best_loser = L;
    upset = side_score(u, m, W) + 10 < side_score(u, m, best_loser);

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
    int i;

    u_zero(touched, sizeof touched);
    if (u->slot == 0) /* a new week: injuries heal by one week */
        for (i = 0; i < u->n_wrestlers; i++)
            if (u->w[i].injured_weeks) u->w[i].injured_weeks--;

    for (i = 0; i < s->count; i++)
        if (s->m[i].done) apply_match(u, s, &s->m[i], touched);

    /* Rivalries that could have been on this show but weren't cool off. */
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        URivalry *r = &u->r[i];
        if (!r->intensity || touched[i]) continue;
        if (s->brand == U_NONE || u->w[r->a].brand == s->brand ||
            u->w[r->b].brand == s->brand)
            r->intensity = (uint8_t)clamp(r->intensity - 10, 0, 100);
    }

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
    w8(&w, u->n_wrestlers);
    w8(&w, u->n_teams);
    w8(&w, u->n_titles);
    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        const UWrestler *x = &u->w[i];
        wname(&w, x->name);
        w8(&w, x->game_slot); w8(&w, x->brand); w8(&w, x->align);
        w8(&w, x->overall); w8(&w, (uint8_t)x->momentum);
        w8(&w, x->injured_weeks); w8(&w, x->active);
        w16(&w, x->wins); w16(&w, x->losses);
    }
    for (i = 0; i < U_MAX_TEAMS; i++) {
        const UTeam *t = &u->tm[i];
        wname(&w, t->name);
        w8(&w, t->a); w8(&w, t->b); w8(&w, t->active);
        w16(&w, t->wins); w16(&w, t->losses);
    }
    for (i = 0; i < U_MAX_TITLES; i++) {
        const UTitle *t = &u->t[i];
        wname(&w, t->name);
        w8(&w, t->brand); w8(&w, t->kind); w8(&w, t->active);
        w8(&w, t->vacant); w8(&w, t->reign_count);
        for (k = 0; k < U_MAX_REIGNS; k++) {
            const UReign *rg = &t->history[k];
            w8(&w, rg->holder); w8(&w, rg->defenses);
            w16(&w, rg->won_week); w16(&w, rg->lost_week);
        }
    }
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        w8(&w, r->a); w8(&w, r->b); w8(&w, r->intensity); w8(&w, r->weeks);
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
    nw = u->n_wrestlers = r8(&r);
    nteams = u->n_teams = r8(&r);
    nt = u->n_titles = r8(&r);
    if (nw > U_MAX_WRESTLERS || nteams > U_MAX_TEAMS || nt > U_MAX_TITLES ||
        u->slot > 2 || !u->rng)
        return load_fail(u, -5);

    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        UWrestler *x = &u->w[i];
        rname(&r, x->name);
        x->game_slot = r8(&r); x->brand = r8(&r); x->align = r8(&r);
        x->overall = r8(&r); x->momentum = (int8_t)r8(&r);
        x->injured_weeks = r8(&r); x->active = r8(&r);
        x->wins = r16(&r); x->losses = r16(&r);
        if (i < nw && x->brand >= U_BRAND_COUNT) return load_fail(u, -6);
    }
    for (i = 0; i < U_MAX_TEAMS; i++) {
        UTeam *t = &u->tm[i];
        rname(&r, t->name);
        t->a = r8(&r); t->b = r8(&r); t->active = r8(&r);
        t->wins = r16(&r); t->losses = r16(&r);
        if (i < nteams && (t->a >= nw || t->b >= nw || t->a == t->b))
            return load_fail(u, -6);
    }
    for (i = 0; i < U_MAX_TITLES; i++) {
        UTitle *t = &u->t[i];
        rname(&r, t->name);
        t->brand = r8(&r); t->kind = r8(&r); t->active = r8(&r);
        t->vacant = r8(&r); t->reign_count = r8(&r);
        for (k = 0; k < U_MAX_REIGNS; k++) {
            UReign *rg = &t->history[k];
            rg->holder = r8(&r); rg->defenses = r8(&r);
            rg->won_week = r16(&r); rg->lost_week = r16(&r);
        }
    }
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        URivalry *rv = &u->r[i];
        rv->a = r8(&r); rv->b = r8(&r); rv->intensity = r8(&r); rv->weeks = r8(&r);
        if (rv->intensity && (rv->a >= nw || rv->b >= nw || rv->a == rv->b))
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
