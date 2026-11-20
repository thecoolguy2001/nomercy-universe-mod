/* No Mercy Universe engine - see universe.h for the design rules. */
#include "universe.h"

#define U_SAVE_VERSION 1

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
    if (u->n_wrestlers >= U_MAX_WRESTLERS) return -1;
    w = &u->w[u->n_wrestlers];
    u_zero(w, sizeof *w);
    u_set_name(w->name, name);
    w->game_slot = game_slot;
    w->brand = brand;
    w->align = align;
    w->overall = (uint8_t)clamp(overall, 0, 100);
    w->active = 1;
    return u->n_wrestlers++;
}

int u_add_title(Universe *u, const char *name, uint8_t brand, uint8_t holder)
{
    UTitle *t;
    if (u->n_titles >= U_MAX_TITLES) return -1;
    if (holder != U_NONE && holder >= u->n_wrestlers) return -1;
    t = &u->t[u->n_titles];
    u_zero(t, sizeof *t);
    u_set_name(t->name, name);
    t->brand = brand;
    t->active = 1;
    if (holder != U_NONE) {
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
            u->r[i].intensity = intensity;
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
    return t->reign_count ? t->history[(t->reign_count - 1) % U_MAX_REIGNS].holder
                          : U_NONE;
}

static int is_champion(const Universe *u, uint8_t id)
{
    int i;
    for (i = 0; i < u->n_titles; i++)
        if (u->t[i].active && u_title_holder(u, (uint8_t)i) == id) return 1;
    return 0;
}

int u_score(const UWrestler *w)
{
    return w->overall + w->momentum + 2 * ((int)w->wins - (int)w->losses);
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

/* ---------- booking ---------- */

static void add_match(UShow *s, uint8_t a, uint8_t b, uint8_t title,
                      uint8_t riv, uint8_t *used)
{
    UMatch *m;
    if (s->count >= U_MAX_MATCHES) return;
    m = &s->m[s->count++];
    u_zero(m, sizeof *m);
    m->a = a;
    m->b = b;
    m->title = title;
    m->rivalry = riv;
    m->winner = U_NONE;
    used[a] = used[b] = 1;
}

/* Hottest rivalry whose two members are both still unbooked. If id is not
 * U_NONE, the rivalry must involve that wrestler. */
static int best_open_rivalry(const Universe *u, uint8_t id, const uint8_t *used)
{
    int i, best = -1;
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (!r->intensity || used[r->a] || used[r->b]) continue;
        if (id != U_NONE && r->a != id && r->b != id) continue;
        if (best < 0 || r->intensity > u->r[best].intensity) best = i;
    }
    return best;
}

/*
 * Booking priorities:
 *   1. Every champion defends: against a rival if they have one, else
 *      against the best-ranked non-champion (the #1 contender).
 *   2. Other active rivalries, hottest first.
 *   3. Everyone else paired by ranking, preferring face vs heel.
 * The first match booked is the most important, so the card is reversed
 * at the end to put it last (main event).
 */
void u_book_show(Universe *u, uint8_t brand, UShow *s)
{
    uint8_t used[U_MAX_WRESTLERS];
    uint8_t rank[U_MAX_WRESTLERS];
    int n, i, j, t;

    u_zero(s, sizeof *s);
    s->week = u->week;
    s->brand = brand;
    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        const UWrestler *w = &u->w[i];
        used[i] = (uint8_t)(i >= u->n_wrestlers || !w->active ||
                            w->brand != brand || w->injured_weeks > 0);
    }
    n = u_rankings(u, brand, rank, U_MAX_WRESTLERS);

    for (t = 0; t < u->n_titles; t++) {
        uint8_t h = u_title_holder(u, (uint8_t)t);
        int ri;
        if (!u->t[t].active || u->t[t].brand != brand || h == U_NONE || used[h])
            continue;
        ri = best_open_rivalry(u, h, used);
        if (ri >= 0) {
            uint8_t opp = u->r[ri].a == h ? u->r[ri].b : u->r[ri].a;
            add_match(s, h, opp, (uint8_t)t, (uint8_t)ri, used);
            continue;
        }
        /* #1 contender: best non-champion not saved for their own rivalry,
         * falling back to anyone available. */
        {
            int pick = -1;
            for (j = 0; j < n && pick < 0; j++)
                if (!used[rank[j]] && !is_champion(u, rank[j]) &&
                    best_open_rivalry(u, rank[j], used) < 0)
                    pick = j;
            for (j = 0; j < n && pick < 0; j++)
                if (!used[rank[j]] && !is_champion(u, rank[j])) pick = j;
            if (pick >= 0) add_match(s, h, rank[pick], (uint8_t)t, U_NONE, used);
        }
    }

    while (s->count < U_MAX_MATCHES) {
        int ri = best_open_rivalry(u, U_NONE, used);
        if (ri < 0) break;
        add_match(s, u->r[ri].a, u->r[ri].b, U_NONE, (uint8_t)ri, used);
    }

    for (i = 0; i < n && s->count < U_MAX_MATCHES; i++) {
        uint8_t a = rank[i];
        int pick = -1;
        if (used[a]) continue;
        for (j = i + 1; j < n && j <= i + 4; j++) { /* nearby, opposite alignment */
            if (!used[rank[j]] && u->w[rank[j]].align != u->w[a].align) {
                pick = j;
                break;
            }
        }
        for (j = i + 1; pick < 0 && j < n; j++)
            if (!used[rank[j]]) pick = j;
        if (pick < 0) break;
        add_match(s, a, rank[pick], U_NONE, U_NONE, used);
    }

    for (i = 0, j = s->count - 1; i < j; i++, j--) {
        UMatch tmp = s->m[i];
        s->m[i] = s->m[j];
        s->m[j] = tmp;
    }
}

/* ---------- results ---------- */

void u_sim_match(Universe *u, UMatch *m)
{
    const UWrestler *a = &u->w[m->a];
    const UWrestler *b = &u->w[m->b];
    int ra = a->overall + a->momentum / 4;
    int rb = b->overall + b->momentum / 4;
    int p = clamp(50 + (ra - rb), 15, 85); /* chance that a wins, percent */
    uint32_t roll;

    m->winner = (int)u_rand(u, 100) < p ? m->a : m->b;
    roll = u_rand(u, 100);
    m->method = roll < 70 ? U_WIN_PINFALL
              : roll < 88 ? U_WIN_SUBMISSION
              : roll < 95 ? U_WIN_COUNTOUT
              : U_WIN_DQ;
    m->source = U_SRC_SIMULATED;
    m->done = 1;
}

/* Record the result of a match the player actually played. */
int u_set_result(UMatch *m, uint8_t winner, uint8_t method)
{
    if (winner != m->a && winner != m->b) return -1;
    if (method >= U_WIN_METHOD_COUNT) return -1;
    m->winner = winner;
    m->method = method;
    m->source = U_SRC_PLAYED;
    m->done = 1;
    return 0;
}

static void add_momentum(UWrestler *w, int d)
{
    w->momentum = (int8_t)clamp(w->momentum + d, -100, 100);
}

void u_apply_show(Universe *u, UShow *s)
{
    uint8_t touched[U_MAX_RIVALRIES];
    int i;

    u_zero(touched, sizeof touched);
    for (i = 0; i < u->n_wrestlers; i++)
        if (u->w[i].injured_weeks) u->w[i].injured_weeks--;

    for (i = 0; i < s->count; i++) {
        UMatch *m = &s->m[i];
        uint8_t wi, li;
        UWrestler *W, *L;
        int upset, decisive, ri;

        if (!m->done) continue;
        wi = m->winner;
        li = wi == m->a ? m->b : m->a;
        W = &u->w[wi];
        L = &u->w[li];
        upset = u_score(W) + 10 < u_score(L);
        decisive = m->method == U_WIN_PINFALL || m->method == U_WIN_SUBMISSION;

        W->wins++;
        L->losses++;
        add_momentum(W, upset ? 14 : 8);
        add_momentum(L, -6);

        /* Titles only change hands by pinfall or submission. */
        if (m->title != U_NONE) {
            UTitle *t = &u->t[m->title];
            UReign *cur = current_reign(t);
            if (cur && (cur->holder == wi || !decisive)) {
                /* won, or lost by count-out/DQ: either way a retained title */
                if (cur->defenses < 255) cur->defenses++;
            } else if (decisive) {
                UReign *nr;
                if (cur) cur->lost_week = u->week;
                t->reign_count++;
                nr = current_reign(t);
                nr->holder = wi;
                nr->defenses = 0;
                nr->won_week = u->week;
                nr->lost_week = U_OPEN_WEEK;
                m->title_changed = 1;
                add_momentum(W, 10);
            }
        }

        /* Rivalries: cheap finishes heat them up, a clean win after a few
         * weeks settles them. Upsets and cheap finishes can start new ones. */
        ri = m->rivalry != U_NONE ? m->rivalry : find_rivalry(u, m->a, m->b);
        if (ri >= 0 && u->r[ri].intensity) {
            URivalry *r = &u->r[ri];
            touched[ri] = 1;
            if (r->weeks < 255) r->weeks++;
            if (!decisive) {
                r->intensity = (uint8_t)clamp(r->intensity + 20, 1, 100);
            } else if (r->weeks >= 3) {
                r->intensity = 0;
                m->rivalry_ended = 1;
            } else {
                r->intensity = (uint8_t)clamp(r->intensity + 10, 1, 100);
            }
        } else if ((upset || !decisive) && !in_rivalry(u, m->a) &&
                   !in_rivalry(u, m->b) && u_rand(u, 100) < 50) {
            int nr = u_add_rivalry(u, m->a, m->b, 30);
            if (nr >= 0) {
                touched[nr] = 1;
                m->rivalry_started = 1;
            }
        }

        if (u_rand(u, 100) < 4) {
            L->injured_weeks = (uint8_t)(1 + u_rand(u, 3));
            m->injury_weeks = L->injured_weeks;
        }
    }

    /* Rivalries that weren't on the show cool off. */
    for (i = 0; i < U_MAX_RIVALRIES; i++)
        if (u->r[i].intensity && !touched[i])
            u->r[i].intensity = (uint8_t)clamp(u->r[i].intensity - 15, 0, 100);

    u->week++;
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
    w32(&w, u->rng);
    w8(&w, u->n_wrestlers);
    w8(&w, u->n_titles);
    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        const UWrestler *x = &u->w[i];
        wname(&w, x->name);
        w8(&w, x->game_slot); w8(&w, x->brand); w8(&w, x->align);
        w8(&w, x->overall); w8(&w, (uint8_t)x->momentum);
        w8(&w, x->injured_weeks); w8(&w, x->active);
        w16(&w, x->wins); w16(&w, x->losses);
    }
    for (i = 0; i < U_MAX_TITLES; i++) {
        const UTitle *t = &u->t[i];
        wname(&w, t->name);
        w8(&w, t->brand); w8(&w, t->active); w8(&w, t->reign_count);
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

int u_load(Universe *u, const uint8_t *buf, int len)
{
    UReader r;
    int i, k;
    uint8_t nw, nt;

    if (len < U_SAVE_SIZE) return -1;
    if (buf[0] != 'N' || buf[1] != 'M' || buf[2] != 'U') return -2;
    if (buf[3] != U_SAVE_VERSION) return -3;
    if (fletcher16(buf, U_SAVE_SIZE - 2) !=
        (uint16_t)(buf[U_SAVE_SIZE - 2] << 8 | buf[U_SAVE_SIZE - 1]))
        return -4;
    nw = buf[10];
    nt = buf[11];
    if (nw > U_MAX_WRESTLERS || nt > U_MAX_TITLES) return -5;

    u_zero(u, sizeof *u);
    r.p = buf;
    r.n = 4;
    u->week = r16(&r);
    u->rng = r32(&r);
    u->n_wrestlers = r8(&r);
    u->n_titles = r8(&r);
    for (i = 0; i < U_MAX_WRESTLERS; i++) {
        UWrestler *x = &u->w[i];
        rname(&r, x->name);
        x->game_slot = r8(&r); x->brand = r8(&r); x->align = r8(&r);
        x->overall = r8(&r); x->momentum = (int8_t)r8(&r);
        x->injured_weeks = r8(&r); x->active = r8(&r);
        x->wins = r16(&r); x->losses = r16(&r);
    }
    for (i = 0; i < U_MAX_TITLES; i++) {
        UTitle *t = &u->t[i];
        rname(&r, t->name);
        t->brand = r8(&r); t->active = r8(&r); t->reign_count = r8(&r);
        for (k = 0; k < U_MAX_REIGNS; k++) {
            UReign *rg = &t->history[k];
            rg->holder = r8(&r); rg->defenses = r8(&r);
            rg->won_week = r16(&r); rg->lost_week = r16(&r);
        }
    }
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        URivalry *rv = &u->r[i];
        rv->a = r8(&r); rv->b = r8(&r); rv->intensity = r8(&r); rv->weeks = r8(&r);
        if (rv->intensity && (rv->a >= nw || rv->b >= nw || rv->a == rv->b)) {
            u_zero(u, sizeof *u);
            return -6;
        }
    }
    for (i = 0; i < nt; i++) {
        uint8_t h = u_title_holder(u, (uint8_t)i);
        if (h != U_NONE && h >= nw) {
            u_zero(u, sizeof *u);
            return -6;
        }
    }
    return 0;
}
