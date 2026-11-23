/* Automated checks for the Universe engine. Run with: make test */
#include <stdio.h>
#include <string.h>
#include "../engine/universe.h"
#include "../host/demo_roster.h"
#include "../host/roster.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void sim_show(Universe *u, UShow *s)
{
    int i;
    u_book_show(u, s);
    u_sim_show(u, s);
    for (i = 0; i < s->count; i++) CHECK(s->m[i].done);
    u_apply_show(u, s);
}

static void run_shows(Universe *u, int shows)
{
    UShow s;
    while (shows--) sim_show(u, &s);
}

static int involves(const UMatch *m, uint8_t id)
{
    int s, k;
    for (s = 0; s < 3; s++)
        for (k = 0; k < 2; k++)
            if (m->side[s][k] == id) return 1;
    return 0;
}

static void test_save_roundtrip(void)
{
    Universe a, b;
    uint8_t buf1[U_SAVE_SIZE], buf2[U_SAVE_SIZE];
    demo_universe(&a, 7);
    run_shows(&a, 40);
    CHECK(u_save(&a, buf1, sizeof buf1) == U_SAVE_SIZE);
    CHECK(u_save(&a, buf1, U_SAVE_SIZE - 1) < 0);
    CHECK(u_load(&b, buf1, sizeof buf1) == 0);
    CHECK(u_save(&b, buf2, sizeof buf2) == U_SAVE_SIZE);
    CHECK(memcmp(buf1, buf2, U_SAVE_SIZE) == 0);
    CHECK(b.week == a.week && b.slot == a.slot);

    run_shows(&a, 9);
    run_shows(&b, 9);
    u_save(&a, buf1, sizeof buf1);
    u_save(&b, buf2, sizeof buf2);
    CHECK(memcmp(buf1, buf2, U_SAVE_SIZE) == 0);
}

static void test_corrupt_save_rejected(void)
{
    Universe u;
    uint8_t buf[U_SAVE_SIZE];
    demo_universe(&u, 7);
    u_save(&u, buf, sizeof buf);
    buf[100] ^= 0x40;
    CHECK(u_load(&u, buf, sizeof buf) == -4);
    buf[100] ^= 0x40;
    buf[0] = 'X';
    CHECK(u_load(&u, buf, sizeof buf) == -2);
    buf[0] = 'N';
    buf[3] = U_SAVE_VERSION - 1; /* old version */
    CHECK(u_load(&u, buf, sizeof buf) == -3);
    buf[3] = U_SAVE_VERSION;
    CHECK(u_load(&u, buf, 10) == -1);
    CHECK(u_load(&u, buf, sizeof buf) == 0);
}

static void test_deterministic(void)
{
    Universe a, b, c;
    uint8_t ba[U_SAVE_SIZE], bb[U_SAVE_SIZE], bc[U_SAVE_SIZE];
    demo_universe(&a, 1234);
    demo_universe(&b, 1234);
    demo_universe(&c, 99);
    run_shows(&a, 60);
    run_shows(&b, 60);
    run_shows(&c, 60);
    u_save(&a, ba, sizeof ba);
    u_save(&b, bb, sizeof bb);
    u_save(&c, bc, sizeof bc);
    CHECK(memcmp(ba, bb, U_SAVE_SIZE) == 0);
    CHECK(memcmp(ba, bc, U_SAVE_SIZE) != 0);
}

static void test_calendar(void)
{
    Universe u;
    UShow s;
    int i;
    demo_universe(&u, 1);
    /* weeks 1-3: Raw, SmackDown. Week 4: Raw, SmackDown, PPV. */
    for (i = 0; i < 3; i++) {
        CHECK(u.week == i + 1 && u.slot == 0);
        CHECK(!strcmp(u_show_name(&u), "Raw Is War"));
        sim_show(&u, &s);
        CHECK(s.kind == U_SHOW_WEEKLY && s.brand == U_BRAND_RAW);
        CHECK(!strcmp(u_show_name(&u), "SmackDown!"));
        sim_show(&u, &s);
        CHECK(s.brand == U_BRAND_SMACKDOWN);
    }
    sim_show(&u, &s);
    sim_show(&u, &s);
    CHECK(u.week == 4 && u.slot == 2);
    CHECK(!strcmp(u_show_name(&u), "Royal Rumble"));
    sim_show(&u, &s);
    CHECK(s.kind == U_SHOW_PPV && s.brand == U_NONE && s.week == 4);
    CHECK(u.week == 5 && u.slot == 0);
    run_shows(&u, 8); /* weeks 5-8 TV, then the week 8 PPV */
    CHECK(u.slot == 2 && !strcmp(u_show_name(&u), "No Way Out"));
}

/* A year of shows across many seeds never produces an invalid card. */
/* close allies on opposite sides of a match */
static int opponents_allied(const Universe *u, const UMatch *m)
{
    int x, y, i, j;
    for (x = 0; x < u_match_sides(m); x++)
        for (y = x + 1; y < u_match_sides(m); y++)
            for (i = 0; i < 2; i++)
                for (j = 0; j < 2; j++) {
                    uint8_t a = m->side[x][i], b = m->side[y][j];
                    if (a != U_NONE && b != U_NONE && u_ally_level(u, a, b) >= U_ALLY_CLOSE)
                        return 1;
                }
    return 0;
}

static void test_booking_invariants(void)
{
    uint32_t seed;
    for (seed = 1; seed <= 35; seed++) {     /* demo roster, then the full one */
        Universe u;
        UShow s;
        int show, i, t, k;
        if (seed <= 25) demo_universe(&u, seed);
        else roster_universe(&u, seed);
        for (show = 0; show < 52 * 2 + 13; show++) {
            uint8_t seen[U_MAX_WRESTLERS];
            int kotr, pi;
            memset(seen, 0, sizeof seen);
            u_book_show(&u, &s);
            CHECK(s.count >= 3); /* 12-man brand, two tag matches, injuries */
            kotr = s.kind == U_SHOW_PPV && s.m[0].kotr != 0;
            pi = s.kind == U_SHOW_PPV ? u_ppv_index(&u) : -1;
            CHECK(s.count <= (kotr ? U_MAX_MATCHES : pi == 2 ? U_PPV_MATCHES + 2   /* WrestleMania */
                              : s.kind == U_SHOW_PPV ? U_PPV_MATCHES : U_WEEKLY_MATCHES));
            for (i = 0; i < s.count; i++) {
                const UMatch *m = &s.m[i];
                CHECK(m->format < U_FMT_COUNT && m->stip < U_STIP_COUNT);
                CHECK(u_stip_supported(m->format, m->stip));
                if (m->format == U_FMT_RUMBLE) {
                    /* entrants: distinct, healthy, never the World champion,
                     * a multiple of 4; they may also wrestle elsewhere */
                    uint8_t in[U_MAX_WRESTLERS];
                    int nreg = 0, nfill = 0;
                    memset(in, 0, sizeof in);
                    CHECK(s.kind == U_SHOW_PPV && i == s.count - 1);
                    CHECK(m->n_entrants >= 4 && m->n_entrants <= U_RUMBLE_MAX &&
                          m->n_entrants % 4 == 0);
                    for (k = 0; k < m->n_entrants; k++) {
                        uint8_t id = m->entrant[k];
                        CHECK(id < u.n_wrestlers && !in[id]);
                        in[id] = 1;
                        CHECK(u.w[id].active && !u.w[id].injured_weeks);
                        CHECK(id != u_title_holder(&u, 0));
                        if (u.w[id].role == U_ROLE_REGULAR) nreg++; else nfill++;
                    }
                    for (k = 0; k < u.n_wrestlers; k++)  /* fill-ins only if short */
                        if (u.w[k].role == U_ROLE_REGULAR && u.w[k].active &&
                            !u.w[k].injured_weeks && k != u_title_holder(&u, 0))
                            CHECK(in[k] || nfill == 0);
                    continue;
                }
                /* a guest referee is a wrestler of the roster, not in the match */
                CHECK((m->referee != U_NONE) == (m->stip == U_STIP_REFEREE));
                if (m->referee != U_NONE) {
                    CHECK(m->referee < u.n_wrestlers && !involves(m, m->referee));
                    CHECK(!seen[m->referee]);
                    seen[m->referee] = 1;
                    CHECK(u.w[m->referee].active && u.w[m->referee].injured_weeks == 0);
                }
                for (k = 0; k < 6; k++) {
                    uint8_t id = m->side[k / 2][k % 2];
                    /* who may be on each side, by format */
                    int expect = k / 2 >= u_match_sides(m) ? 0
                               : m->format == U_FMT_TAG ? 1
                               : m->format == U_FMT_HANDICAP && k / 2 == 1 ? 1
                               : k % 2 == 0;
                    if (m->kotr > 8 && !u_match_ready(m)) expect = 0;  /* later round: TBD */
                    CHECK((id != U_NONE) == expect);
                    if (id == U_NONE) continue;
                    CHECK(id < u.n_wrestlers);
                    CHECK(!seen[id]);
                    seen[id] = 1;
                    CHECK(u.w[id].active && u.w[id].injured_weeks == 0);
                    CHECK(u.w[id].role == U_ROLE_REGULAR);
                }
                /* no intergender matches */
                for (k = 1; k < 6; k++)
                    if (m->side[k / 2][k % 2] != U_NONE && m->side[0][0] != U_NONE)
                        CHECK(u.w[m->side[k / 2][k % 2]].gender == u.w[m->side[0][0]].gender);
                /* (makeshift teams, no team ids: Survivor Series) */
                if (m->format == U_FMT_TAG)
                    CHECK(m->team[0] != m->team[1] || (m->team[0] == U_NONE && pi == 10));
                if (m->format == U_FMT_HANDICAP) CHECK(m->title == U_NONE);
                /* matches made from the rankings never pit close allies */
                if (m->title == U_NONE && m->rivalry == U_NONE && !m->kotr)
                    CHECK(!opponents_allied(&u, m));
                if (m->title != U_NONE) {
                    const UTitle *ti = &u.t[m->title];
                    uint8_t h = u_title_holder(&u, m->title);
                    CHECK((ti->kind == U_TITLE_TAG) == (m->format == U_FMT_TAG));
                    if (h != U_NONE && ti->kind == U_TITLE_SINGLES)
                        CHECK(involves(m, h));
                    if (h != U_NONE && ti->kind == U_TITLE_TAG)
                        CHECK(m->team[0] == h || m->team[1] == h);
                    if (ti->kind == U_TITLE_SINGLES)   /* challengers fit the division */
                        for (k = 0; k < u_match_sides(m); k++)
                            if (m->side[k][0] != h) CHECK(u_eligible(&u, m->side[k][0], ti->division));
                }
            }
            if (s.kind == U_SHOW_WEEKLY) {   /* the rest of the card from rankings */
                int feuds = 0;
                for (i = 0; i < s.count; i++)
                    feuds += s.m[i].rivalry != U_NONE && s.m[i].title == U_NONE;
                CHECK(feuds <= U_WEEKLY_FEUDS);
            }
            /* at a PPV every healthy singles champion defends (no brand
             * split: weekly defences come when they are due) */
            for (t = 0; t < u.n_titles; t++) {
                uint8_t h = u_title_holder(&u, (uint8_t)t);
                const UTitle *ti = &u.t[t];
                if (h == U_NONE || ti->kind != U_TITLE_SINGLES || u.w[h].injured_weeks)
                    continue;
                if (s.kind != U_SHOW_PPV) continue;
                if (kotr && t != 0) continue;     /* King of the Ring: one defence */
                if (pi == 10 && t != 0) continue; /* Survivor Series: one title match */
                CHECK(seen[h]);
                if (s.kind == U_SHOW_PPV) {
                    /* (a Rumble winner may spend WrestleMania challenging for
                     * the World title instead of defending his own) */
                    int defended = 0, challenger = 0;
                    for (i = 0; i < s.count; i++) {
                        defended |= s.m[i].title == t;
                        challenger |= s.m[i].title == 0 && involves(&s.m[i], h);
                    }
                    CHECK(defended || challenger);
                }
            }
            u_sim_show(&u, &s);
            for (i = 0; i < s.count; i++)
                CHECK(s.m[i].done && u_method_allowed(s.m[i].stip, s.m[i].method));
            u_apply_show(&u, &s);
        }
    }
}

static int find_title_match(const UShow *s, uint8_t title)
{
    int i;
    for (i = 0; i < s->count; i++)
        if (s->m[i].title == title) return i;
    return -1;
}

/* Run shows (simulated) until the next card has a match for this title. */
static int next_title_match(Universe *u, UShow *s, uint8_t title)
{
    int tries, i;
    for (tries = 0; tries < 40; tries++) {
        u_book_show(u, s);
        if ((i = find_title_match(s, title)) >= 0) return i;
        for (i = 0; i < s->count; i++) u_sim_match(u, &s->m[i]);
        u_apply_show(u, s);
    }
    return -1;
}

static void test_title_changes(void)
{
    Universe u;
    UShow s;
    int mi, cs;
    uint8_t champ;
    const UTitle *t;
    uint16_t week;

    demo_universe(&u, 5);
    mi = next_title_match(&u, &s, 0);
    CHECK(mi >= 0);
    if (mi < 0) return;
    champ = u_title_holder(&u, 0);
    cs = s.m[mi].side[0][0] == champ ? 1 : 0; /* challenger's side */

    if (s.m[mi].stip == U_STIP_NORMAL) {
        /* challenger wins by DQ: champion keeps the belt (a defense) */
        CHECK(u_set_result(&s.m[mi], (uint8_t)cs, U_WIN_DQ) == 0);
        u_apply_show(&u, &s);
        CHECK(u_title_holder(&u, 0) == champ);
        CHECK(!s.m[mi].title_changed);
        mi = next_title_match(&u, &s, 0);
        CHECK(mi >= 0);
        if (mi < 0) return;
        cs = s.m[mi].side[0][0] == champ ? 1 : 0;
    }
    week = u.week;
    CHECK(u_set_result(&s.m[mi], (uint8_t)cs,
                       s.m[mi].stip == U_STIP_LADDER ? U_WIN_SPECIAL : U_WIN_PINFALL) == 0);
    u_apply_show(&u, &s);
    t = &u.t[0];
    CHECK(u_title_holder(&u, 0) == s.m[mi].side[cs][0]);
    CHECK(s.m[mi].title_changed);
    CHECK(t->reign_count == 2);
    CHECK(t->history[0].holder == champ);
    CHECK(t->history[0].lost_week == week);
    CHECK(t->history[1].won_week == week);
    CHECK(t->history[1].lost_week == U_OPEN_WEEK);
}

static void test_tag_title_change(void)
{
    Universe u;
    UShow s;
    int mi, cs;
    uint8_t champs;
    demo_universe(&u, 8);
    champs = u_title_holder(&u, 2);
    mi = next_title_match(&u, &s, 2);
    CHECK(mi >= 0);
    if (mi < 0) return;
    CHECK(s.m[mi].format == U_FMT_TAG);
    cs = s.m[mi].team[0] == champs ? 1 : 0;
    CHECK(u_set_result(&s.m[mi], (uint8_t)cs,
                       s.m[mi].stip == U_STIP_LADDER ? U_WIN_SPECIAL : U_WIN_PINFALL) == 0);
    u_apply_show(&u, &s);
    CHECK(u_title_holder(&u, 2) == s.m[mi].team[cs]);
    CHECK(u.tm[s.m[mi].team[cs]].wins == 1 || u.tm[s.m[mi].team[cs]].wins > 1);
}

static void test_played_result_validation(void)
{
    Universe u;
    UShow s;
    UMatch m;
    demo_universe(&u, 3);
    u_book_show(&u, &s);
    m = s.m[0];
    m.format = U_FMT_SINGLE;
    m.stip = U_STIP_NORMAL;
    CHECK(u_set_result(&m, 2, U_WIN_PINFALL) == -1);
    CHECK(u_set_result(&m, 0, U_WIN_METHOD_COUNT) == -1);
    CHECK(u_set_result(&m, 0, U_WIN_SPECIAL) == -1);
    CHECK(!m.done);
    m.stip = U_STIP_LADDER;
    CHECK(u_set_result(&m, 0, U_WIN_PINFALL) == -1);
    CHECK(u_set_result(&m, 0, U_WIN_SPECIAL) == 0);
    m.stip = U_STIP_CAGE;
    CHECK(u_set_result(&m, 1, U_WIN_DQ) == -1);
    CHECK(u_set_result(&m, 1, U_WIN_SUBMISSION) == 0);
    CHECK(m.done && m.source == U_SRC_PLAYED && m.winner == 1);
    m.done = 0;
    m.stip = U_STIP_IRONMAN;
    CHECK(u_set_result(&m, 0, U_WIN_PINFALL) == -1);
    {
        const uint8_t level[3] = {2, 2, 0}, win[3] = {1, 3, 0}, three[3] = {1, 2, 4};
        CHECK(u_set_ironman_result(&m, win) == 0);
        CHECK(m.winner == 1 && m.method == U_WIN_FALLS && m.falls[1] == 3);
        CHECK(u_set_ironman_result(&m, level) == 0);   /* level on falls: a draw */
        CHECK(m.done && m.winner == U_NONE && m.method == U_WIN_DRAW);
        m.format = U_FMT_TRIPLE;
        m.side[2][0] = (uint8_t)(m.side[0][0] ? 0 : 1);   /* a third wrestler */
        CHECK(u_set_ironman_result(&m, three) == 0);
        CHECK(m.winner == 2 && m.falls[2] == 4);
        m.format = U_FMT_SINGLE;
        m.side[2][0] = U_NONE;
    }
    m.done = 0;
    m.stip = U_STIP_NODQ;
    CHECK(u_set_result(&m, 0, U_WIN_DQ) == -1);
    CHECK(u_set_result(&m, 0, U_WIN_COUNTOUT) == -1);
    CHECK(u_set_result(&m, 0, U_WIN_PINFALL) == 0);
    CHECK(u_set_result(&m, 0, U_WIN_DRAW) == -1);      /* draws: u_set_draw */
    CHECK(u_set_draw(&m) == 0 && m.winner == U_NONE && m.method == U_WIN_DRAW);
    m.done = 0;
    m.format = U_FMT_TRIPLE;
    m.stip = U_STIP_NORMAL;
    m.side[2][0] = (uint8_t)(m.side[0][0] ? 0 : 1);
    CHECK(u_set_result(&m, 2, U_WIN_PINFALL) == 0);
    CHECK(u_set_result(&m, 3, U_WIN_PINFALL) == -1);
}

/* The game's supported combinations (notes/universe-plan.md) */
static void test_supported_combinations(void)
{
    int f, st;
    for (f = 0; f < U_FMT_COUNT; f++)
        for (st = 0; st < U_STIP_COUNT; st++)
            CHECK(u_stip_supported((uint8_t)f, (uint8_t)st) ==
                  (f == U_FMT_RUMBLE ? st == U_STIP_NORMAL
                   : st == U_STIP_CAGE ? f == U_FMT_SINGLE
                   : st == U_STIP_REFEREE ? f != U_FMT_TAG : 1));
    CHECK(!u_stip_supported(U_FMT_COUNT, U_STIP_NORMAL));
    CHECK(!u_stip_supported(U_FMT_SINGLE, U_STIP_COUNT));
}

/* A drawn title match: nobody wins or loses, the champion keeps the title,
 * the feud is not settled. */
static void test_draw(void)
{
    Universe u;
    UShow s;
    int mi, k, sides;
    uint16_t tot[U_MAX_WRESTLERS];
    uint8_t holder;
    demo_universe(&u, 3);
    mi = next_title_match(&u, &s, 0);
    CHECK(mi >= 0);
    if (mi < 0) return;
    holder = u_title_holder(&u, 0);
    for (k = 0; k < u.n_wrestlers; k++) tot[k] = (uint16_t)(u.w[k].wins + u.w[k].losses);
    CHECK(u_set_draw(&s.m[mi]) == 0);
    {
        char t[96];
        u_match_text(&u, &s.m[mi], 1, t, sizeof t);
        CHECK(strstr(t, "Draw") != 0 && strstr(t, " def. ") == 0);
    }
    sides = u_match_sides(&s.m[mi]);
    for (k = 0; k < s.count; k++)
        if (k != mi) u_sim_match(&u, &s.m[k]);
    u_apply_show(&u, &s);
    CHECK(u_title_holder(&u, 0) == holder && !s.m[mi].title_changed);
    for (k = 0; k < 2 * sides; k++) {
        uint8_t id = s.m[mi].side[k / 2][k % 2];
        if (id != U_NONE) CHECK(u.w[id].wins + u.w[id].losses == tot[id]);
    }
}

static void test_rivalry_booked(void)
{
    Universe u;
    UShow s;
    int i, found = 0;
    demo_universe(&u, 11);
    u_add_rivalry(&u, 4, 5, 90); /* Bull Buchanan vs Rikishi (Raw) */
    u_book_show(&u, &s);
    for (i = 0; i < s.count; i++)
        if (s.m[i].format == U_FMT_SINGLE && involves(&s.m[i], 4) && involves(&s.m[i], 5))
            found = 1;
    CHECK(found);
}

/* A rival must not be stolen as a title contender (Rock vs Angle week 1). */
static void test_rivalry_not_stolen(void)
{
    uint32_t seed;
    for (seed = 1; seed <= 10; seed++) {
        Universe u;
        UShow s;
        int i, found = 0;
        demo_universe(&u, seed);
        u_book_show(&u, &s);
        for (i = 0; i < s.count; i++)
            if (involves(&s.m[i], 0) && involves(&s.m[i], 2)) found = 1;
        CHECK(found);
    }
}

/* A hot feud (Austin vs HHH) gets its PPV match, unless one of them is
 * already wrestling for a title there (or is injured). */
static void test_cross_brand_rivalry_at_ppv(void)
{
    uint32_t seed;
    int booked = 0;
    for (seed = 21; seed < 31; seed++) {
        Universe u;
        UShow s;
        int i, found = 0, title = 0;
        demo_universe(&u, seed);
        u.r[1].intensity = 100; /* keep it hot so it survives to the PPV */
        run_shows(&u, 8);       /* weeks 1-4 TV */
        CHECK(u.slot == 2);
        u.r[1].a = 12;          /* (it may have ended and its slot been reused) */
        u.r[1].b = 1;
        u.r[1].intensity = 100;
        u_book_show(&u, &s);
        for (i = 0; i < s.count; i++) {
            if (involves(&s.m[i], 12) && involves(&s.m[i], 1)) {
                found = 1;
                CHECK(u_stip_supported(s.m[i].format, s.m[i].stip));
            }
            if (s.m[i].title != U_NONE && (involves(&s.m[i], 12) || involves(&s.m[i], 1)))
                title = 1;
        }
        CHECK(found || title || u.w[12].injured_weeks || u.w[1].injured_weeks);
        booked += found;
    }
    CHECK(booked >= 1);
}

static void test_injured_skip(void)
{
    Universe u;
    UShow s;
    int i;
    demo_universe(&u, 2);
    u.w[0].injured_weeks = 2; /* The Rock: misses the next 2 Raws */
    sim_show(&u, &s);         /* Raw week 1 */
    for (i = 0; i < s.count; i++) CHECK(!involves(&s.m[i], 0));
    CHECK(u.w[0].injured_weeks == 1);
    sim_show(&u, &s);         /* SmackDown week 1 */
    sim_show(&u, &s);         /* Raw week 2 */
    for (i = 0; i < s.count; i++) CHECK(!involves(&s.m[i], 0));
    CHECK(u.w[0].injured_weeks == 0);
    sim_show(&u, &s);         /* SmackDown week 2 */
    u_book_show(&u, &s);      /* Raw week 3: back */
    for (i = 0; i < s.count; i++) if (involves(&s.m[i], 0)) break;
    CHECK(i < s.count);
}

static void test_roster_management(void)
{
    Universe u;
    UShow s;
    int i;
    demo_universe(&u, 4);

    /* Drafting the WWF champion to SmackDown takes the title with him. */
    CHECK(u_set_brand(&u, 1, U_BRAND_SMACKDOWN) == 0);
    CHECK(u.t[0].brand == U_BRAND_SMACKDOWN);
    CHECK(u_set_brand(&u, 1, 9) == -1);

    /* Releasing the IC champion vacates the title; a vacant title is
     * fought over and filled. */
    CHECK(u_set_active(&u, 14, 0) == 0);
    CHECK(u.t[1].vacant && u_title_holder(&u, 1) == U_NONE);
    CHECK(u.t[1].history[0].lost_week == 1);
    sim_show(&u, &s); /* Raw */
    for (i = 0; i < 6 && u.t[1].vacant; i++) sim_show(&u, &s);
    CHECK(!u.t[1].vacant && u_title_holder(&u, 1) != 14);
    CHECK(u.t[1].reign_count == 2);

    /* Released wrestlers are never booked; rehiring restores them. */
    for (i = 0; i < 10; i++) {
        int k;
        u_book_show(&u, &s);
        for (k = 0; k < s.count; k++) CHECK(!involves(&s.m[k], 14));
        for (k = 0; k < s.count; k++) u_sim_match(&u, &s.m[k]);
        u_apply_show(&u, &s);
    }
    CHECK(u_set_active(&u, 14, 1) == 0 && u.w[14].active);

    /* Releasing a tag champion vacates the tag titles and disbands the team. */
    {
        uint8_t team = u_title_holder(&u, 2);
        CHECK(team != U_NONE);
        if (team != U_NONE) {
            CHECK(u_set_active(&u, u.tm[team].a, 0) == 0);
            CHECK(u.t[2].vacant && !u.tm[team].active);
        }
    }

    CHECK(u_set_align(&u, 0, U_HEEL) == 0 && u.w[0].align == U_HEEL);
    CHECK(u_set_overall(&u, 0, 150) == 0 && u.w[0].overall == 100);
}

/* Over a year, weekly TV books every format and Cage matches; a triple
 * threat or handicap result gives every wrestler a win or a loss. */
static void test_formats_booked(void)
{
    Universe u;
    UShow s;
    int show, i, k, count[U_FMT_COUNT] = {0}, cages = 0;
    demo_universe(&u, 5);
    for (show = 0; show < 52 * 2; show++) {
        u_book_show(&u, &s);
        u_sim_show(&u, &s);
        for (i = 0; i < s.count; i++) {
            UMatch *m = &s.m[i];
            count[m->format]++;
            if (m->stip == U_STIP_CAGE && s.kind == U_SHOW_WEEKLY) cages++;
            CHECK(m->winner < u_match_sides(m));
        }
        if (s.m[s.count - 1].format == U_FMT_RUMBLE || s.m[0].kotr) {  /* entrants may double up */
            u_apply_show(&u, &s);
            continue;
        }
        {
            /* everyone in a triple threat / handicap gets exactly one result */
            uint16_t tot[U_MAX_WRESTLERS];
            for (k = 0; k < u.n_wrestlers; k++) tot[k] = (uint16_t)(u.w[k].wins + u.w[k].losses);
            u_apply_show(&u, &s);
            for (i = 0; i < s.count; i++)
                for (k = 0; k < 6; k++) {
                    uint8_t id = s.m[i].side[k / 2][k % 2];
                    if (id != U_NONE) CHECK(u.w[id].wins + u.w[id].losses == tot[id] + 1);
                }
        }
    }
    CHECK(count[U_FMT_SINGLE] > 0 && count[U_FMT_TAG] > 0);
    CHECK(count[U_FMT_TRIPLE] > 0 && count[U_FMT_HANDICAP] > 0);
    CHECK(count[U_FMT_RUMBLE] == 1);              /* 104 shows: one Royal Rumble PPV */
    {
        /* weekly cages are rare (hot feuds only): count a few universes */
        uint32_t seed;
        for (seed = 1; seed <= 4; seed++) {
            demo_universe(&u, seed);
            for (show = 0; show < 52 * 2; show++) {
                u_book_show(&u, &s);
                for (i = 0; i < s.count; i++) {
                    if (s.m[i].stip == U_STIP_CAGE && s.kind == U_SHOW_WEEKLY) cages++;
                    u_sim_match(&u, &s.m[i]);
                }
                u_apply_show(&u, &s);
            }
        }
    }
    CHECK(cages > 0);
}

/* Over a few years every stipulation turns up (interim booking). */
static void test_stips_booked(void)
{
    uint32_t seed;
    int count[U_STIP_COUNT] = {0}, st;
    for (seed = 1; seed <= 20; seed++) {    /* (Ironman and Guest Referee are rare) */
        Universe u;
        UShow s;
        int show, i;
        roster_universe(&u, seed);
        for (show = 0; show < 3 * 112; show++) {
            u_book_show(&u, &s);
            for (i = 0; i < s.count; i++) {
                count[s.m[i].stip]++;
                u_sim_match(&u, &s.m[i]);
                if (s.m[i].stip == U_STIP_IRONMAN) {
                    int k, top = 0;
                    for (k = 0; k < u_match_sides(&s.m[i]); k++)
                        if (k != s.m[i].winner && s.m[i].falls[k] >= s.m[i].falls[s.m[i].winner])
                            top = 1;
                    CHECK(!top);   /* the winner has the most falls */
                }
            }
            u_apply_show(&u, &s);
        }
    }
    for (st = 0; st < U_STIP_COUNT; st++) CHECK(count[st] > 0);
}

/* The Royal Rumble PPV's main event is the Rumble; its winner challenges
 * the World champion at WrestleMania, then the shot is gone. */
static void test_royal_rumble(void)
{
    uint32_t seed;
    int shots = 0;
    for (seed = 1; seed <= 20; seed++) {
        Universe u;
        UShow s;
        UMatch *r;
        int i, guard = 0;
        uint8_t winner;
        demo_universe(&u, seed);
        while (!(u.slot == 2 && u_ppv_index(&u) == 0) && guard++ < 100) sim_show(&u, &s);
        CHECK(u.week == 4 && u.slot == 2);
        CHECK(!strcmp(u_show_name(&u), "Royal Rumble"));
        u_book_show(&u, &s);
        r = &s.m[s.count - 1];
        CHECK(r->format == U_FMT_RUMBLE);
        if (r->format != U_FMT_RUMBLE) continue;
        CHECK(u_set_result(r, 0, U_WIN_PINFALL) == -1);
        CHECK(u_set_result(r, r->n_entrants, U_WIN_RUMBLE) == -1);
        CHECK(u_set_draw(r) == -1);
        CHECK(!r->done);
        for (i = 0; i < s.count; i++) u_sim_match(&u, &s.m[i]);
        CHECK(r->done && r->method == U_WIN_RUMBLE && r->winner < r->n_entrants);
        winner = r->entrant[r->winner];
        {
            char t[64];
            u_match_text(&u, r, 1, t, sizeof t);
            CHECK(strstr(t, "wins the Royal Rumble") != 0);
            u_match_text(&u, r, 0, t, sizeof t);
            CHECK(strstr(t, "Royal Rumble - ") == t);
        }
        u_apply_show(&u, &s);
        CHECK(u.rumble_winner == (u.w[winner].role == U_ROLE_REGULAR ? winner : U_NONE));
        while (!(u.slot == 2 && u_ppv_index(&u) == 2) && guard++ < 200) sim_show(&u, &s);
        CHECK(!strcmp(u_show_name(&u), "WrestleMania"));
        if (u.rumble_winner != U_NONE && !u.w[u.rumble_winner].injured_weeks &&
            u_title_holder(&u, 0) != U_NONE && !u.w[u_title_holder(&u, 0)].injured_weeks &&
            u.rumble_winner != u_title_holder(&u, 0)) {
            int found = 0;
            u_book_show(&u, &s);
            for (i = 0; i < s.count; i++)
                found |= s.m[i].title == 0 && involves(&s.m[i], u.rumble_winner) &&
                         involves(&s.m[i], u_title_holder(&u, 0));
            CHECK(found);
            shots++;
            for (i = 0; i < s.count; i++) u_sim_match(&u, &s.m[i]);
            u_apply_show(&u, &s);
            CHECK(u.rumble_winner == U_NONE);
        }
    }
    CHECK(shots > 0);
}

/* King of the Ring: the 16-man bracket fills round by round and its
 * winner is crowned King. */
static void test_king_of_the_ring(void)
{
    Universe u;
    UShow s;
    int i, guard = 0, at[U_KOTR_MATCHES], titles = 0, others = 0;
    uint8_t seen[U_MAX_WRESTLERS], rank[U_MAX_WRESTLERS], king;
    roster_universe(&u, 4);                      /* (a 16-man bracket) */
    while (!(u.slot == 2 && u_ppv_index(&u) == 5) && guard++ < 200) sim_show(&u, &s);
    CHECK(!strcmp(u_show_name(&u), "King of the Ring"));
    u_book_show(&u, &s);
    memset(seen, 0, sizeof seen);
    for (i = 0; i < U_KOTR_MATCHES; i++) at[i] = -1;
    for (i = 0; i < s.count; i++) {
        const UMatch *m = &s.m[i];
        if (!m->kotr) {
            if (m->title == 0) titles++; else others++;
            continue;
        }
        CHECK(m->kotr <= U_KOTR_MATCHES && at[m->kotr - 1] < 0);
        at[m->kotr - 1] = i;
        CHECK(m->format == U_FMT_SINGLE && m->title == U_NONE);
        CHECK(u_match_ready(m) == (m->kotr <= 8));        /* later rounds: TBD */
        if (m->kotr <= 8) {
            CHECK(!seen[m->side[0][0]] && !seen[m->side[1][0]]);
            seen[m->side[0][0]] = seen[m->side[1][0]] = 1;
        }
    }
    for (i = 0; i < U_KOTR_MATCHES; i++) CHECK(at[i] >= 0);
    CHECK(titles == 1 && others <= 1);
    CHECK(s.m[s.count - 1].kotr == U_KOTR_MATCHES);       /* the final is the main event */
    {
        /* the best-ranked entrant opens against the 16th */
        int n = u_rankings(&u, U_NONE, rank, U_MAX_WRESTLERS), j, first = -1;
        for (j = 0; j < n && first < 0; j++)
            if (seen[rank[j]]) first = rank[j];
        CHECK(s.m[at[0]].side[0][0] == first);
    }
    /* one first-round result fills its quarterfinal side */
    CHECK(u_set_draw(&s.m[at[0]]) == -1);
    u_sim_match(&u, &s.m[at[0]]);
    u_show_update(&s);
    CHECK(s.m[at[8]].side[0][0] == s.m[at[0]].side[s.m[at[0]].winner][0]);
    CHECK(s.m[at[8]].side[1][0] == U_NONE);
    u_sim_match(&u, &s.m[at[8]]);                         /* not ready: nothing */
    CHECK(!s.m[at[8]].done);
    u_sim_show(&u, &s);
    for (i = 0; i < s.count; i++) CHECK(s.m[i].done);
    for (i = 8; i < U_KOTR_MATCHES; i++) {                /* winners moved on */
        const UMatch *m = &s.m[at[i]];
        int k;
        for (k = 0; k < 2; k++) {
            const UMatch *f = &s.m[at[2 * (i - 8) + k]];
            CHECK(m->side[k][0] == f->side[f->winner][0]);
        }
    }
    king = s.m[at[14]].side[s.m[at[14]].winner][0];
    u_apply_show(&u, &s);
    CHECK(u.king == king);
    {
        uint8_t buf[U_SAVE_SIZE];
        Universe v;
        CHECK(u_save(&u, buf, sizeof buf) == U_SAVE_SIZE);
        CHECK(u_load(&v, buf, sizeof buf) == 0 && v.king == king);
    }
}

/* The World title (if on the card) is the main event, unless it's the
 * Royal Rumble */
static int prominence_ok(const Universe *u, const UShow *s)
{
    int i;
    (void)u;
    for (i = 0; i < s->count - 1; i++)
        if (s->m[i].title == 0 && s->m[s->count - 1].format != U_FMT_RUMBLE &&
            !s->m[s->count - 1].kotr)
            return 0;
    return 1;
}

/* The full roster: No Mercy's wrestlers, the 7 belts in game order,
 * champions who fit their division, a women's match on every weekly show. */
static void test_full_roster(void)
{
    Universe u;
    UShow s;
    int i, k, men = 0, women = 0, fill = 0, light = 0, shows, ok = 0;
    roster_universe(&u, 7);
    for (i = 0; i < u.n_wrestlers; i++) {
        const UWrestler *w = &u.w[i];
        CHECK(w->game_slot >= 1 && w->game_slot <= 64 && w->game_attire < 4);
        for (k = 0; k < i; k++)                  /* one wrestler per game character */
            CHECK(u.w[k].game_slot != w->game_slot || u.w[k].game_attire != w->game_attire);
        if (w->role == U_ROLE_FILLIN) { fill++; continue; }
        if (w->gender == U_MALE) men++; else women++;
        if (u_eligible(&u, (uint8_t)i, U_DIV_LIGHT)) light++;
    }
    CHECK(men == 50 && women == 10 && fill >= 1 && light >= 8);
    CHECK(u.n_titles == 7);
    CHECK(u.t[0].kind == U_TITLE_SINGLES && u.t[1].kind == U_TITLE_TAG);
    CHECK(u.t[5].division == U_DIV_LIGHT && u.t[6].division == U_DIV_WOMEN);
    for (i = 0; i < u.n_titles; i++) {
        uint8_t h = u_title_holder(&u, (uint8_t)i);
        CHECK(h != U_NONE);
        if (u.t[i].kind == U_TITLE_SINGLES && h != U_NONE)
            CHECK(u_eligible(&u, h, u.t[i].division));
    }
    for (shows = 0; shows < 60; shows++) {
        int womens = 0;
        u_book_show(&u, &s);
        for (i = 0; i < s.count; i++)
            if (s.m[i].side[0][0] != U_NONE && u.w[s.m[i].side[0][0]].gender == U_FEMALE)
                womens++;
        if (s.kind == U_SHOW_WEEKLY) ok += womens >= 1;
        {   /* one of Mankind / Cactus Jack / Mick Foley per card (Rumble aside) */
            int persona = 0;
            for (i = 0; i < s.count; i++)
                for (k = 0; k < 6; k++) {
                    uint8_t id = s.m[i].side[k / 2][k % 2];
                    if (id != U_NONE && u.w[id].persona == 1) persona |= 1 << u.w[id].game_slot;
                }
            CHECK((persona & (persona - 1)) == 0);
        }
        CHECK(prominence_ok(&u, &s));
        CHECK(womens <= 2);
        u_sim_show(&u, &s);
        u_apply_show(&u, &s);
    }
    CHECK(ok >= 40);                            /* (injuries can thin the division) */
}

static int rivalry_heat(const Universe *u, uint8_t a, uint8_t b)
{
    int i;
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (r->intensity && ((r->a == a && r->b == b) || (r->a == b && r->b == a)))
            return r->intensity;
    }
    return 0;
}

static int by_name(const Universe *u, const char *name)
{
    int i;
    for (i = 0; i < u->n_wrestlers; i++)
        if (!strcmp(u->w[i].name, name)) return i;
    return -1;
}

static int team_named(const Universe *u, const char *name, const char *a, const char *b)
{
    int i, x = by_name(u, a), y = by_name(u, b);
    for (i = 0; i < u->n_teams; i++)
        if (!strcmp(u->tm[i].name, name) &&
            ((u->tm[i].a == x && u->tm[i].b == y) || (u->tm[i].a == y && u->tm[i].b == x)))
            return i;
    return -1;
}

/* Plan step 5: rivalries, allies and tag teams from the game's ALLY/ENEMY data */
static void test_game_relations(void)
{
    Universe u, v;
    uint8_t buf[U_SAVE_SIZE];
    int i, n = 0, rock = 0, hhh = 0;
    roster_universe(&u, 3);
#define W(name) (uint8_t)by_name(&u, name)
    /* tag teams: the game's partners, T & A (same valet, a rival in common),
     * and the teams the game doesn't pair */
    CHECK(team_named(&u, "Edge & Christian", "Edge", "Christian") >= 0);
    CHECK(team_named(&u, "Hardy Boyz", "Matt Hardy", "Jeff Hardy") >= 0);
    CHECK(team_named(&u, "Dudley Boyz", "D-Von", "Buh Buh Ray") >= 0);
    CHECK(team_named(&u, "Too Cool", "G.M. Sexay", "Scotty") >= 0);
    CHECK(team_named(&u, "Right to Censor", "Steven Richards", "Bull Buchanan") >= 0);
    CHECK(team_named(&u, "T & A", "Test", "Albert") >= 0);
    CHECK(team_named(&u, "APA", "Faarooq", "Bradshaw") >= 0);
    CHECK(team_named(&u, "New Age Outlaws", "Road Dogg", "Mr. Ass") >= 0);
    CHECK(team_named(&u, "Kaientai", "Taka Michinoku", "Funaki") >= 0);
    CHECK(u.n_teams == 9);
    CHECK(u_title_holder(&u, 1) == team_named(&u, "Edge & Christian", "Edge", "Christian"));
    for (i = 0; i < u.n_teams; i++)          /* partners are close allies */
        CHECK(u_ally_level(&u, u.tm[i].a, u.tm[i].b) >= U_ALLY_CLOSE);
    /* valets and the game's other partners */
    CHECK(u_ally_level(&u, W("Val Venis"), W("Trish Stratus")) == 90);
    CHECK(u_ally_level(&u, W("Trish Stratus"), W("Test")) == 75);
    CHECK(u_ally_level(&u, W("X-Pac"), W("Tori")) == 75);
    CHECK(u_ally_level(&u, W("Eddie Guerrero"), W("Chyna")) == 90);
    CHECK(u_ally_level(&u, W("Test"), W("Val Venis")) == 50);
    CHECK(u_ally_level(&u, W("The Rock"), W("Triple H")) == 0);
    /* rivalries: a mutual first rival is hottest; first rival hotter */
    CHECK(rivalry_heat(&u, W("The Rock"), W("Triple H")) == 65);
    CHECK(rivalry_heat(&u, W("Trish Stratus"), W("Lita")) == 65);
    CHECK(rivalry_heat(&u, W("Kurt Angle"), W("Tazz")) > rivalry_heat(&u, W("Kurt Angle"), W("Rikishi")));
    CHECK(rivalry_heat(&u, W("Kane"), W("Triple H")) > rivalry_heat(&u, W("Undertaker"), W("Kane")));
    CHECK(rivalry_heat(&u, W("Mankind"), W("Triple H")) == 40);    /* (one man, three names) */
    CHECK(rivalry_heat(&u, W("Steve Austin"), W("Vince McMahon")) == 0);  /* not a regular */
    CHECK(rivalry_heat(&u, W("Essa Rios"), W("Lita")) == 0);       /* no intergender feuds */
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u.r[i];
        if (!r->intensity) continue;
        n++;
        CHECK(u.w[r->a].role == U_ROLE_REGULAR && u.w[r->b].role == U_ROLE_REGULAR);
        CHECK(u.w[r->a].gender == u.w[r->b].gender);
        CHECK(u_ally_level(&u, r->a, r->b) < U_ALLY_CLOSE);
        CHECK(strcmp(u.w[r->a].name, "Cactus Jack") && strcmp(u.w[r->b].name, "Mick Foley"));
        CHECK(r->intensity >= 25 && r->intensity <= 65);
        CHECK(i == 0 || !u.r[i - 1].intensity || u.r[i - 1].intensity >= r->intensity);
        rock += r->a == W("The Rock") || r->b == W("The Rock");
        hhh += r->a == W("Triple H") || r->b == W("Triple H");
    }
    CHECK(n == 67 && rock == 5 && hhh == 9);
    /* allies survive a save */
    CHECK(u_save(&u, buf, sizeof buf) == U_SAVE_SIZE);
    CHECK(u_load(&v, buf, sizeof buf) == 0);
    CHECK(u_ally_level(&v, W("Edge"), W("Christian")) == 90);
    CHECK(u_ally_level(&v, W("Christian"), W("Edge")) == 90);
    CHECK(u_add_ally(&u, W("Edge"), W("Edge"), 50) < 0);
    CHECK(u_add_ally(&u, W("Edge"), W("Christian"), 40) >= 0);
    CHECK(u_ally_level(&u, W("Edge"), W("Christian")) == 90);  /* (only raised) */
#undef W
}

/* One match on its own show (not the first show of a week, so no decay) */
static void play_one(Universe *u, int ppv, uint8_t format, uint8_t x0, uint8_t x1,
                     uint8_t y0, uint8_t y1, uint8_t tx, uint8_t ty, uint8_t winner)
{
    UShow s;
    UMatch *m = &s.m[0];
    int k;
    memset(&s, 0, sizeof s);
    s.week = u->week;
    s.kind = ppv ? U_SHOW_PPV : U_SHOW_WEEKLY;
    s.count = 1;
    m->format = format;
    m->stip = U_STIP_NORMAL;
    m->title = m->rivalry = m->referee = m->winner = m->injured = U_NONE;
    for (k = 0; k < 3; k++) {
        m->team[k] = U_NONE;
        m->side[k][0] = m->side[k][1] = U_NONE;
    }
    m->side[0][0] = x0; m->side[0][1] = x1;
    m->side[1][0] = y0; m->side[1][1] = y1;
    m->team[0] = tx; m->team[1] = ty;
    CHECK(u_set_result(m, winner, U_WIN_PINFALL) == 0);
    u->slot = 1;
    u_apply_show(u, &s);
}

/* Plan step 6: ranking points */
static void test_ranking_points(void)
{
    Universe u;
    UShow e;
    uint8_t a, b, c, d, rank[U_MAX_WRESTLERS];
    int t1, t2, i, n, k_div;
    roster_universe(&u, 9);
    a = (uint8_t)by_name(&u, "Test");
    b = (uint8_t)by_name(&u, "Kane");
    c = (uint8_t)by_name(&u, "Viscera");
    d = (uint8_t)by_name(&u, "Mark Henry");
    for (i = 0; i < u.n_wrestlers; i++) u.w[i].injured_weeks = 0;
    /* a new wrestler starts from his ability */
    CHECK(u.w[b].points == 3 * u.w[b].overall);
    /* beating a higher-ranked man: 20 + 1 per 10 points he was ahead (max
     * 20); the upset loser pays 10 + the same */
    u.w[a].points = 100; u.w[b].points = 300;
    play_one(&u, 0, U_FMT_SINGLE, a, U_NONE, b, U_NONE, U_NONE, U_NONE, 0);
    CHECK(u.w[a].points == 140 && u.w[b].points == 270);
    CHECK(u.w[a].streak == 1 && u.w[b].streak == -1);
    /* beating a lower-ranked man: 20; his loss: 10 */
    u.w[a].points = 300; u.w[c].points = 100; u.w[a].streak = 0; u.w[c].streak = 0;
    play_one(&u, 0, U_FMT_SINGLE, a, U_NONE, c, U_NONE, U_NONE, U_NONE, 0);
    CHECK(u.w[a].points == 320 && u.w[c].points == 90 && u.w[c].streak == -1);
    /* a streak bonus (3 per win in a row before), PPV wins double */
    u.w[a].points = 300; u.w[a].streak = 2; u.w[c].points = 100;
    play_one(&u, 1, U_FMT_SINGLE, c, U_NONE, a, U_NONE, U_NONE, U_NONE, 1);
    CHECK(u.w[a].points == 300 + 2 * (20 + 6) && u.w[a].streak == 3);
    CHECK(u.w[c].points == 90 && u.w[c].streak == -2);
    /* points never go below 0 */
    u.w[c].points = 5;
    play_one(&u, 0, U_FMT_SINGLE, a, U_NONE, c, U_NONE, U_NONE, U_NONE, 0);
    CHECK(u.w[c].points == 0);
    /* the lone wrestler who beats a handicap pair: +10 */
    u.w[d].points = 200; u.w[d].streak = 0; u.w[a].points = 200; u.w[b].points = 200;
    play_one(&u, 0, U_FMT_HANDICAP, d, U_NONE, a, b, U_NONE, U_NONE, 0);
    CHECK(u.w[d].points == 230 && u.w[a].points == 190 && u.w[b].points == 190);
    /* tag matches score the teams, not the wrestlers */
    t1 = team_named(&u, "Hardy Boyz", "Matt Hardy", "Jeff Hardy");
    t2 = team_named(&u, "Dudley Boyz", "D-Von", "Buh Buh Ray");
    u.tm[t1].points = 100; u.tm[t2].points = 150; u.tm[t1].streak = u.tm[t2].streak = 0;
    i = u.w[u.tm[t1].a].points;
    play_one(&u, 0, U_FMT_TAG, u.tm[t1].a, u.tm[t1].b, u.tm[t2].a, u.tm[t2].b,
             (uint8_t)t1, (uint8_t)t2, 0);
    CHECK(u.tm[t1].points == 125 && u.tm[t2].points == 135);
    CHECK(u.tm[t1].streak == 1 && u.tm[t2].streak == -1);
    CHECK(u.w[u.tm[t1].a].points == i);
    /* every week points decay by a tenth, injured wrestlers' by another */
    u.w[a].points = 200; u.w[b].points = 200; u.w[b].injured_weeks = 2;
    u.tm[t1].points = 100;
    memset(&e, 0, sizeof e);
    e.week = u.week;
    e.kind = U_SHOW_WEEKLY;
    u.slot = 0;
    u_apply_show(&u, &e);
    CHECK(u.w[a].points == 180 && u.w[b].points == 162 && u.tm[t1].points == 90);
    CHECK(u.w[b].injured_weeks == 1);
    /* the rankings follow the points */
    for (k_div = 0; k_div < 2; k_div++) {
        n = u_division_rankings(&u, k_div ? U_DIV_WOMEN : U_DIV_MEN, rank, U_MAX_WRESTLERS);
        for (i = 1; i < n; i++) CHECK(u.w[rank[i - 1]].points >= u.w[rank[i]].points);
    }
    n = u_team_rankings(&u, U_NONE, rank, U_MAX_TEAMS);
    for (i = 1; i < n; i++) CHECK(u.tm[rank[i - 1]].points >= u.tm[rank[i]].points);
    /* (saved) */
    {
        Universe v;
        uint8_t buf[U_SAVE_SIZE];
        u.w[a].streak = -7;
        CHECK(u_save(&u, buf, sizeof buf) == U_SAVE_SIZE && u_load(&v, buf, sizeof buf) == 0);
        CHECK(v.w[a].points == u.w[a].points && v.w[a].streak == -7);
        CHECK(v.tm[t1].points == 90 && v.w[a].last_show == u.w[a].last_show);
        CHECK(v.t[2].no_world == 1 && v.t[4].no_world == 0);
    }
}

/* Title defences go to a top-5 contender (a rival of the champion or the
 * #1 contender) unless all five are busy; the Royal Rumble winner's
 * WrestleMania shot aside. Contenders who hold another singles title are
 * in the picture but defend their own belt instead. */
static void test_title_pictures(void)
{
    uint32_t seed;
    int top = 0, all = 0, first = 0;
    for (seed = 40; seed < 46; seed++) {
        Universe u;
        UShow s;
        int show, i, k;
        roster_universe(&u, seed);
        for (show = 0; show < 104; show++) {
            u_book_show(&u, &s);
            for (i = 0; i < s.count; i++) {
                const UMatch *m = &s.m[i];
                uint8_t con[U_MAX_WRESTLERS], h, opp;
                int n, pos = -1;
                if (m->title == U_NONE || u.t[m->title].kind != U_TITLE_SINGLES) continue;
                h = u_title_holder(&u, m->title);
                if (h == U_NONE || m->format != U_FMT_SINGLE) continue;
                opp = m->side[0][0] == h ? m->side[1][0] : m->side[0][0];
                if (m->title == 0 && opp == u.rumble_winner) continue;
                n = u_contenders(&u, m->title, con, U_MAX_WRESTLERS);
                {   /* position among the contenders free of a singles title */
                    int c = 0, t;
                    for (k = 0; k < n; k++) {
                        int champ = 0;
                        for (t = 0; t < u.n_titles; t++)
                            champ |= u.t[t].kind == U_TITLE_SINGLES &&
                                     u_title_holder(&u, (uint8_t)t) == con[k];
                        if (con[k] == opp) pos = c;
                        c += !champ;
                    }
                }
                CHECK(pos >= 0);                 /* always a contender */
                all++;
                top += pos >= 0 && pos < U_CONTENDERS;
                first += pos == 0;
            }
            u_sim_show(&u, &s);
            u_apply_show(&u, &s);
        }
    }
    CHECK(top * 100 >= all * 95 && first * 100 >= all * 50);   /* (100% / 58% seen) */
}

static int is_tag_champ(const Universe *u, uint8_t team)
{
    int i;
    for (i = 0; i < u->n_titles; i++)
        if (u->t[i].kind == U_TITLE_TAG && u_title_holder(u, (uint8_t)i) == team) return 1;
    return 0;
}

/* the level of a pair, or -1 when they aren't allies at all */
static int ally_of(const Universe *u, uint8_t a, uint8_t b)
{
    int l = u_ally_level(u, a, b);
    return l ? l : -1;
}

/* one week of shows (first show applied first: the weekly updates run) */
static void run_week(Universe *u)
{
    do run_shows(u, 1); while (u->slot != 0);
}

/* Plan step 7: allies over time, teams forming and splitting */
static void test_allies_over_time(void)
{
    Universe u;
    UShow e;
    UMatch m;
    uint8_t ids[8], a, b, c, d;
    int i, n, t;
    roster_universe(&u, 21);
    for (i = 0; i < u.n_wrestlers; i++) u.w[i].injured_weeks = 0;
    for (i = 0; i < U_MAX_RIVALRIES; i++) u.r[i].intensity = 0;  /* (no common enemies) */
    /* teaming: partners +4 a tag match (+2 more for winning) */
    t = team_named(&u, "Hardy Boyz", "Matt Hardy", "Jeff Hardy");
    i = team_named(&u, "APA", "Faarooq", "Bradshaw");
    n = u_ally_level(&u, u.tm[t].a, u.tm[t].b);
    play_one(&u, 0, U_FMT_TAG, u.tm[t].a, u.tm[t].b, u.tm[i].a, u.tm[i].b,
             (uint8_t)t, (uint8_t)i, 0);
    CHECK(u_ally_level(&u, u.tm[t].a, u.tm[t].b) == (n + 6 > 100 ? 100 : n + 6));
    /* a handicap pair: +10 (a new pair starts there) */
    a = (uint8_t)by_name(&u, "Kane"); b = (uint8_t)by_name(&u, "Rikishi");
    c = (uint8_t)by_name(&u, "The Rock"); d = (uint8_t)by_name(&u, "Undertaker");
    CHECK(u_ally_level(&u, a, b) == 0);
    play_one(&u, 0, U_FMT_HANDICAP, c, U_NONE, a, b, U_NONE, U_NONE, 0);
    CHECK(u_ally_level(&u, a, b) == 10);
    /* companions: the strongest ally from 50 up, healthy, not in the match */
    memset(&m, 0xFF, sizeof m);
    m.side[0][0] = (uint8_t)by_name(&u, "Val Venis");
    m.side[1][0] = (uint8_t)by_name(&u, "Test");
    CHECK(u_companion(&u, &m, m.side[0][0]) == by_name(&u, "Trish Stratus"));
    CHECK(u_companion(&u, &m, (uint8_t)by_name(&u, "The Rock")) == U_NONE);
    u.w[by_name(&u, "Trish Stratus")].injured_weeks = 1;   /* then Albert (50; Test is */
    CHECK(u_companion(&u, &m, m.side[0][0]) == by_name(&u, "Albert"));   /* in the match) */
    u.w[by_name(&u, "Trish Stratus")].injured_weeks = 0;
    m.side[1][0] = (uint8_t)by_name(&u, "Trish Stratus");
    CHECK(u_companion(&u, &m, m.side[0][0]) == by_name(&u, "Test"));
    {   /* below 50: nobody */
        Universe w2 = u;
        int j;
        for (j = 0; j < U_MAX_ALLIES; j++)
            if (w2.al[j].level && (w2.al[j].a == m.side[0][0] || w2.al[j].b == m.side[0][0]) &&
                w2.al[j].level < 90)
                w2.al[j].level = 49;
        CHECK(u_companion(&w2, &m, m.side[0][0]) == U_NONE);
    }
    CHECK(u_companion(&u, &m, (uint8_t)by_name(&u, "Edge")) == by_name(&u, "Christian"));
    /* a run-in save: +10 with the wrestler helped, a rivalry with the
     * other side */
    {
        UShow s1;
        UMatch *x = &s1.m[0];
        memset(&s1, 0, sizeof s1);
        s1.kind = U_SHOW_WEEKLY;
        s1.count = 1;
        memset(x, 0xFF, sizeof *x);
        x->format = U_FMT_SINGLE; x->stip = U_STIP_NORMAL;
        x->kotr = 0; x->n_entrants = 0;
        x->side[0][0] = c;
        x->side[1][0] = (uint8_t)by_name(&u, "Kurt Angle");
        CHECK(u_set_result(x, 0, U_WIN_PINFALL) == 0);
        x->run_in = d;                       /* (as u_sim_match would) */
        x->run_in_for = c;
        n = u_ally_level(&u, c, d);
        u.slot = 1;
        u_apply_show(&u, &s1);
        CHECK(u_ally_level(&u, c, d) == n + 10);
        n = u_top_rivals(&u, d, ids, 8);
        for (i = 0; i < n && ids[i] != x->side[1][0]; i++) {}
        CHECK(i < n);
    }
    /* a common enemy: +5 a show for two wrestlers of the same alignment */
    {
        uint8_t p = (uint8_t)by_name(&u, "Chris Jericho"), q = (uint8_t)by_name(&u, "Mankind");
        uint8_t enemy = (uint8_t)by_name(&u, "Triple H");
        for (i = 0; i < U_MAX_RIVALRIES; i++) u.r[i].intensity = 0;
        u_add_rivalry(&u, p, enemy, 60);
        u_add_rivalry(&u, q, enemy, 60);
        CHECK(u.w[p].align == u.w[q].align);
        memset(&e, 0, sizeof e);
        e.kind = U_SHOW_WEEKLY;
        u.slot = 1;
        n = u_ally_level(&u, p, q);
        u_apply_show(&u, &e);
        CHECK(u_ally_level(&u, p, q) == n + 5);
        /* top rivals: hottest first */
        u_add_rivalry(&u, p, c, 90);
        CHECK(u_top_rivals(&u, p, ids, 8) == 2 && ids[0] == c && ids[1] == enemy);
    }
    /* a week apart: -2; together (a companion at ringside): kept */
    {
        uint8_t v = (uint8_t)by_name(&u, "Val Venis"), tr = (uint8_t)by_name(&u, "Trish Stratus");
        uint8_t x2 = (uint8_t)by_name(&u, "X-Pac"), to = (uint8_t)by_name(&u, "Tori");
        int lv = u_ally_level(&u, v, tr), lx = u_ally_level(&u, x2, to);
        u.slot = 1;
        play_one(&u, 0, U_FMT_SINGLE, v, U_NONE, (uint8_t)by_name(&u, "Rikishi"), U_NONE,
                 U_NONE, U_NONE, 0);
        memset(&e, 0, sizeof e);
        e.kind = U_SHOW_WEEKLY;
        u.slot = 0;                          /* the week turns */
        u_apply_show(&u, &e);
        CHECK(u_ally_level(&u, v, tr) == lv);
        CHECK(u_ally_level(&u, x2, to) == lx - 2);
    }
    CHECK(ally_of(&u, a, c) == -1);
}

/* New teams: from high allies; splits: low teams, never the champions */
static void test_teams_form_and_split(void)
{
    Universe u, v;
    UShow s;
    uint8_t a, b, buf[U_SAVE_SIZE];
    int i, k, t0, formed = -1, split = -1, debut = 0;
    roster_universe(&u, 22);
    a = (uint8_t)by_name(&u, "Chris Benoit");
    b = (uint8_t)by_name(&u, "Dean Malenko");
    t0 = u.n_teams;
    for (k = 0; k < 60 && formed < 0; k++) {    /* 30% a week */
        u_add_ally(&u, a, b, 95);
        for (i = 0; i < U_MAX_RIVALRIES; i++)
            if ((u.r[i].a == a && u.r[i].b == b) || (u.r[i].a == b && u.r[i].b == a))
                u.r[i].intensity = 0;
        do {
            u_book_show(&u, &s);
            u_sim_show(&u, &s);
            u_apply_show(&u, &s);
            if (s.team_formed != U_NONE) formed = s.team_formed;
        } while (u.slot != 0 && formed < 0);
    }
    CHECK(formed >= 0);
    if (formed < 0) return;
    CHECK(formed == t0 || u.tm[formed].split == 0);
    CHECK((u.tm[formed].a == a && u.tm[formed].b == b) || (u.tm[formed].a == b && u.tm[formed].b == a));
    CHECK(!strcmp(u.tm[formed].name, "Benoit&Malenko") ||
          !strcmp(u.tm[formed].name, "Dean Malenko&Chris Benoit") ||
          strlen(u.tm[formed].name) < U_NAME_LEN);
    CHECK(u.tm[formed].debut && u.tm[formed].low_weeks < 0);
    {   /* at the bottom of the tag ranking, then its debut tag match */
        uint8_t r[U_MAX_TEAMS];
        int n = u_team_rankings(&u, U_NONE, r, U_MAX_TEAMS);
        CHECK(r[n - 1] == formed);
        for (k = 0; k < 6 && !debut; k++) {      /* (a member may be busy defending a title) */
            u_book_show(&u, &s);
            for (i = 0; i < s.count; i++)
                if (s.m[i].format == U_FMT_TAG &&
                    (s.m[i].team[0] == formed || s.m[i].team[1] == formed)) {
                    debut = 1;
                    CHECK(s.m[i].why == U_WHY_DEBUT || s.kind == U_SHOW_PPV);
                }
            u_sim_show(&u, &s);
            u_apply_show(&u, &s);
        }
        CHECK(debut);
        CHECK(!u.tm[formed].debut);
    }
    /* saved */
    CHECK(u_save(&u, buf, sizeof buf) == U_SAVE_SIZE && u_load(&v, buf, sizeof buf) == 0);
    CHECK(!strcmp(v.tm[formed].name, u.tm[formed].name) && v.tm[formed].last_show == u.tm[formed].last_show);
    /* a team stuck low on a losing streak splits; the tag champions never */
    for (k = 0; k < 400 && split < 0; k++) {
        for (i = 0; i < u.n_teams; i++) {
            u.tm[i].low_weeks = 50;
            u.tm[i].streak = -5;
            u.tm[i].points = (uint16_t)(i == formed ? 0 : 100 + i);
        }
        do {
            u_book_show(&u, &s);
            u_sim_show(&u, &s);
            u_apply_show(&u, &s);
            if (s.team_split != U_NONE) split = s.team_split;
        } while (u.slot != 0 && split < 0);
    }
    CHECK(split >= 0);
    if (split < 0) return;
    CHECK(u.tm[split].split && !is_tag_champ(&u, (uint8_t)split));
    CHECK(u_ally_level(&u, u.tm[split].a, u.tm[split].b) == 0);    /* reset */
    {
        uint8_t r[U_MAX_TEAMS];
        int n = u_team_rankings(&u, U_NONE, r, U_MAX_TEAMS);
        for (i = 0; i < n; i++) CHECK(r[i] != split);
        CHECK(n >= 6);
    }
    /* never below 6 teams, champions never split */
    for (k = 0; k < 300; k++) {
        uint8_t r[U_MAX_TEAMS];
        for (i = 0; i < u.n_teams; i++) { u.tm[i].low_weeks = 50; u.tm[i].streak = -5; }
        run_week(&u);
        CHECK(u_team_rankings(&u, U_NONE, r, U_MAX_TEAMS) >= 6);
        for (i = 0; i < u.n_titles; i++)
            if (u.t[i].kind == U_TITLE_TAG && u_title_holder(&u, (uint8_t)i) != U_NONE)
                CHECK(!u.tm[u_title_holder(&u, (uint8_t)i)].split);
    }
}

static int heat_between(const Universe *u, uint8_t a, uint8_t b)
{
    int i;
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (r->intensity && ((r->a == a && r->b == b) || (r->a == b && r->b == a)))
            return r->intensity;
    }
    return 0;
}

/* the hottest feud across the ring */
static int heat_of_match(const Universe *u, const UMatch *m)
{
    int x, y, i, j, h = 0, n = u_match_sides(m);
    for (x = 0; x < n; x++)
        for (y = x + 1; y < n; y++)
            for (i = 0; i < 2; i++)
                for (j = 0; j < 2; j++)
                    if (m->side[x][i] != U_NONE && m->side[y][j] != U_NONE &&
                        heat_between(u, m->side[x][i], m->side[y][j]) > h)
                        h = heat_between(u, m->side[x][i], m->side[y][j]);
    return h;
}

/* Plan step 8: the booking rules, show by show */
static void test_booking_rules(void)
{
    enum { RR = 0, NWO, WM, BACKLASH, JD, KOTR, FL, SUMMERSLAM, UNFORGIVEN, NM, SS, ARM };
    uint32_t seed;
    int unforgiven = 0, uf_triple = 0, uf_handicap = 0, wm = 0, wm_ladder = 0, no1 = 0;
    int whys[U_WHY_COUNT] = {0}, weekly_cage = 0, ss_tags = 0, three_way = 0, k;
    for (seed = 50; seed < 62; seed++) {
        Universe u;
        UShow s;
        int show, i;
        roster_universe(&u, seed);
        for (show = 0; show < 3 * 112; show++) {
            int pi = u.slot == 2 ? u_ppv_index(&u) : -1;
            int cages = 0, ladders = 0, titles = 0, others = 0, handicap = 0;
            uint8_t rw = u.rumble_winner;
            u_book_show(&u, &s);
            for (i = 0; i < s.count; i++) {
                const UMatch *m = &s.m[i];
                int h = heat_of_match(&u, m);
                whys[m->why]++;
                if (m->format == U_FMT_RUMBLE || m->kotr) continue;
                if (m->title != U_NONE) titles++; else others++;
                handicap += m->format == U_FMT_HANDICAP;
                /* every title match says why; others don't claim a title reason */
                if (m->title != U_NONE)
                    CHECK(m->why == U_WHY_NO1 || m->why == U_WHY_TITLE_RIVAL ||
                          m->why == U_WHY_VACANT || m->why == U_WHY_CONTENDERS ||
                          m->why == U_WHY_RUMBLE_SHOT);
                else
                    CHECK(m->why == U_WHY_NONE || m->why == U_WHY_RIVALRY ||
                          m->why == U_WHY_NO1_MATCH || m->why == U_WHY_DEBUT);
                if (m->why == U_WHY_TITLE_RIVAL) CHECK(h > 0);
                if (m->why == U_WHY_RIVALRY && m->format == U_FMT_TRIPLE) three_way++;
                /* Cage: hot feuds on weekly TV, No Way Out's hottest, 1 in 5
                 * at Backlash, Fully Loaded, No Mercy, Armageddon */
                if (m->stip == U_STIP_CAGE) {
                    cages++;
                    CHECK(m->format == U_FMT_SINGLE);
                    CHECK(pi < 0 || pi == NWO || pi == BACKLASH || pi == FL || pi == NM || pi == ARM);
                    if (pi < 0) { CHECK(h >= 70); weekly_cage++; }
                }
                /* Ladder: WrestleMania's title match, SummerSlam's hottest feud */
                if (m->stip == U_STIP_LADDER) {
                    ladders++;
                    CHECK(pi == WM || pi == SUMMERSLAM);
                    if (pi == WM) CHECK(m->title != U_NONE && m->title != 0);
                }
                /* Ironman: a champion's near-max feud at Judgment Day */
                if (m->stip == U_STIP_IRONMAN)
                    CHECK(pi == JD && m->title != U_NONE && h >= 85);
                /* Guest Referee: any PPV, a rival of everyone in the match */
                if (m->stip == U_STIP_REFEREE) {
                    CHECK(pi >= 0);
                    for (k = 0; k < 6; k++)
                        if (m->side[k / 2][k % 2] != U_NONE)
                            CHECK(heat_between(&u, m->referee, m->side[k / 2][k % 2]) > 0);
                }
                /* No DQ: Hardcore title matches; hot feuds on weekly TV */
                if (m->title == 4) CHECK(m->stip == U_STIP_NODQ);
                else if (m->stip == U_STIP_NODQ) CHECK(pi < 0 && h >= 70);
                if (m->why == U_WHY_NO1_MATCH) {
                    CHECK(pi < 0 && m->why_title < u.n_titles);
                    no1++;
                }
                if (pi == SS && m->title == U_NONE) { CHECK(m->format == U_FMT_TAG); ss_tags++; }
            }
            CHECK(cages <= (pi < 0 ? s.count : 1));
            CHECK(ladders <= (pi == WM || pi == SUMMERSLAM ? 1 : 0));
            if (pi == RR) CHECK(others <= 1);                     /* ONE feud */
            if (pi == SS) CHECK(titles <= 1 && (titles == 0 || s.m[s.count - 1].title == 0 ||
                                                 s.m[s.count - 1].format == U_FMT_TAG));
            if (pi == UNFORGIVEN) {
                unforgiven++;
                uf_handicap += handicap > 0;
                for (i = 0; i < s.count; i++)
                    if (s.m[i].title == 0) uf_triple += s.m[i].format == U_FMT_TRIPLE;
            }
            if (pi == WM) {
                wm++;
                wm_ladder += ladders;
                for (i = 0; i < s.count; i++)                     /* the World title */
                    if (s.m[i].title == 0) {                      /* main event */
                        CHECK(i == s.count - 1);
                        uint8_t ch = u_title_holder(&u, 0);
                        int same = rw != U_NONE && ch != U_NONE && (ch == rw || (u.w[rw].persona &&    /* (one */
                                   u.w[rw].persona == u.w[ch].persona));          /* man) */
                        if (rw != U_NONE && !u.w[rw].injured_weeks && !same)
                            CHECK(involves(&s.m[i], rw));
                    }
            }
            u_sim_show(&u, &s);
            u_apply_show(&u, &s);
            /* the winner of a No. 1 Contender Match is now No. 1 */
            for (i = 0; i < s.count; i++) {
                const UMatch *m = &s.m[i];
                uint8_t con[U_MAX_WRESTLERS], w;
                if (m->why != U_WHY_NO1_MATCH || m->winner >= 2) continue;
                w = u.t[m->why_title].kind == U_TITLE_TAG ? m->team[m->winner] : m->side[m->winner][0];
                if (u.w[m->side[m->winner][0]].injured_weeks ||
                    (m->side[m->winner][1] != U_NONE && u.w[m->side[m->winner][1]].injured_weeks))
                    continue;                             /* (out of the picture) */
                CHECK(u_contenders(&u, m->why_title, con, 1) == 1 && con[0] == w);
            }
        }
    }
    CHECK(uf_triple * 10 >= unforgiven * 9 && uf_handicap * 10 >= unforgiven * 9);
    CHECK(wm_ladder * 10 >= wm * 9);
    CHECK(no1 > 0 && weekly_cage > 0 && ss_tags > 0 && three_way > 0);
    for (k = 0; k < U_WHY_COUNT; k++)
        if (k != U_WHY_VACANT) CHECK(whys[k] > 0);
}

/* Section 2: ability changes over time */
static void test_ratings(void)
{
    Universe u;
    UShow e;
    uint8_t a, b, c;
    int i, base;
    roster_universe(&u, 31);
    a = (uint8_t)by_name(&u, "Viscera");          /* (rated below b and c) */
    b = (uint8_t)by_name(&u, "Kane");
    c = (uint8_t)by_name(&u, "Al Snow");
    for (i = 0; i < u.n_wrestlers; i++) { u.w[i].injured_weeks = 0; u.w[i].form_w = u.w[i].form_l = 0; }
    base = u.w[a].overall;
    CHECK(u.w[a].base_overall == base);
    memset(&e, 0, sizeof e);
    e.kind = U_SHOW_WEEKLY;
    /* 3 wins over a better-rated man: +1 at the 4-weekly review; his 3
     * losses to a worse-rated one: -1 */
    for (i = 0; i < 3; i++)
        play_one(&u, 0, U_FMT_SINGLE, a, U_NONE, b, U_NONE, U_NONE, U_NONE, 0);
    CHECK(u.w[a].form_w == 3 && u.w[b].form_l == 3);
    i = u.w[b].overall;
    u.week = 8; u.slot = 0;                           /* (week 8: a review week) */
    u_apply_show(&u, &e);
    CHECK(u.w[a].overall == base + 1 && u.w[b].overall == i - 1);
    CHECK(u.w[a].form_w == 0 && u.w[b].form_l == 0);
    /* a single upset, or beating a worse-rated man: no change */
    play_one(&u, 0, U_FMT_SINGLE, c, U_NONE, b, U_NONE, U_NONE, U_NONE, 0);
    play_one(&u, 0, U_FMT_SINGLE, b, U_NONE, a, U_NONE, U_NONE, U_NONE, 0);
    play_one(&u, 0, U_FMT_SINGLE, b, U_NONE, a, U_NONE, U_NONE, U_NONE, 0);
    CHECK(u.w[b].form_w == 0 && u.w[b].form_l == 1);
    i = u.w[c].overall;
    u.week = 12; u.slot = 0;
    u_apply_show(&u, &e);
    CHECK(u.w[c].overall == i);
    /* never more than 10 above (or below) the start */
    for (i = 0; i < 30; i++) {
        u.w[a].form_w = 5; u.w[a].form_l = 0;
        u.week = (uint16_t)(16 + 4 * i); u.slot = 0;
        u_apply_show(&u, &e);
    }
    CHECK(u.w[a].overall == base + 10);
    u.w[b].overall = 33; u.w[b].base_overall = 35;
    for (i = 0; i < 10; i++) {
        u.w[b].form_w = 0; u.w[b].form_l = 4;
        u.week = (uint16_t)(200 + 4 * i); u.slot = 0;
        u_apply_show(&u, &e);
    }
    CHECK(u.w[b].overall == 30);                      /* (never under 30) */
    /* saved */
    {
        Universe v;
        uint8_t buf[U_SAVE_SIZE];
        u.w[c].form_w = 2;
        CHECK(u_save(&u, buf, sizeof buf) == U_SAVE_SIZE && u_load(&v, buf, sizeof buf) == 0);
        CHECK(v.w[a].base_overall == base && v.w[a].overall == base + 10 && v.w[c].form_w == 2);
    }
    /* big moments: the Royal Rumble winner +2 (over a year of shows, every
     * Rumble winner and King gains) */
    {
        Universe w;
        UShow s;
        int show, k, gained = 0, events = 0;
        roster_universe(&w, 32);
        for (show = 0; show < 112; show++) {
            uint8_t before[U_MAX_WRESTLERS];
            for (k = 0; k < w.n_wrestlers; k++) before[k] = w.w[k].overall;
            u_book_show(&w, &s);
            u_sim_show(&w, &s);
            u_apply_show(&w, &s);
            for (k = 0; k < s.count; k++) {
                const UMatch *m = &s.m[k];
                uint8_t id = U_NONE;
                if (m->format == U_FMT_RUMBLE && m->winner < m->n_entrants &&
                    w.w[m->entrant[m->winner]].role == U_ROLE_REGULAR)
                    id = m->entrant[m->winner];
                if (m->kotr == U_KOTR_MATCHES && m->winner < 2) id = m->side[m->winner][0];
                if (id == U_NONE) continue;
                events++;
                gained += w.w[id].overall > before[id] ||
                          w.w[id].overall == w.w[id].base_overall + 10 || w.w[id].overall == 99;
            }
        }
        CHECK(events >= 2 && gained == events);
    }
}

/* Title contenders: the top 5 per belt (plan step 6) */
static void test_contenders(void)
{
    Universe u;
    uint8_t con[U_MAX_WRESTLERS], world, hurt;
    int t, i, n;
    roster_universe(&u, 4);
    world = u_title_holder(&u, 0);
    u.w[world].points = 9000;               /* the best-ranked man */
    hurt = (uint8_t)by_name(&u, "The Rock");
    u.w[hurt].points = 8000;
    u.w[hurt].injured_weeks = 2;
    for (t = 0; t < u.n_titles; t++) {
        const UTitle *ti = &u.t[t];
        uint8_t h = u_title_holder(&u, (uint8_t)t);
        n = u_contenders(&u, (uint8_t)t, con, U_CONTENDERS);
        CHECK(n == U_CONTENDERS);
        for (i = 0; i < n; i++) {
            CHECK(con[i] != h);
            if (ti->kind == U_TITLE_TAG) {
                CHECK(con[i] < u.n_teams);
                if (i) CHECK(u.tm[con[i - 1]].points >= u.tm[con[i]].points);
                continue;
            }
            CHECK(u_eligible(&u, con[i], ti->division));
            CHECK(!u.w[con[i]].injured_weeks && con[i] != hurt);
            CHECK(u.w[con[i]].role == U_ROLE_REGULAR && u.w[con[i]].active);
            if (i) CHECK(u.w[con[i - 1]].points >= u.w[con[i]].points);
        }
        /* the World champion: not for the IC / European title, but #1
         * for the Hardcore title */
        if (t == 2 || t == 3) for (i = 0; i < n; i++) CHECK(con[i] != world);
        if (t == 4) CHECK(con[0] == world);
        if (t == 6) for (i = 0; i < n; i++) CHECK(u.w[con[i]].gender == U_FEMALE);
        if (t == 5) for (i = 0; i < n; i++) CHECK(u.w[con[i]].weight <= U_LIGHT_LBS);
    }
    /* the injuries list: injured regulars, most weeks out first */
    u.w[by_name(&u, "Kane")].injured_weeks = 3;
    n = u_injuries(&u, con, U_MAX_WRESTLERS);
    CHECK(n >= 2 && u.w[con[0]].injured_weeks >= u.w[con[n - 1]].injured_weeks);
    for (i = 0; i < n; i++) CHECK(u.w[con[i]].injured_weeks > 0);
    for (i = 1; i < n; i++) CHECK(u.w[con[i - 1]].injured_weeks >= u.w[con[i]].injured_weeks);
}

int main(void)
{
    test_save_roundtrip();
    test_corrupt_save_rejected();
    test_deterministic();
    test_calendar();
    test_booking_invariants();
    test_title_changes();
    test_tag_title_change();
    test_played_result_validation();
    test_rivalry_booked();
    test_rivalry_not_stolen();
    test_cross_brand_rivalry_at_ppv();
    test_injured_skip();
    test_roster_management();
    test_formats_booked();
    test_supported_combinations();
    test_draw();
    test_stips_booked();
    test_royal_rumble();
    test_king_of_the_ring();
    test_full_roster();
    test_game_relations();
    test_ranking_points();
    test_contenders();
    test_title_pictures();
    test_allies_over_time();
    test_teams_form_and_split();
    test_booking_rules();
    test_ratings();
    printf("%d checks, %d failures (save size %d bytes)\n", checks, failures,
           U_SAVE_SIZE);
    return failures ? 1 : 0;
}
