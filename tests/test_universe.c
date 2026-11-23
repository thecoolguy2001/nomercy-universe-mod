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
            int kotr;
            memset(seen, 0, sizeof seen);
            u_book_show(&u, &s);
            CHECK(s.count >= 3); /* 12-man brand, two tag matches, injuries */
            kotr = s.kind == U_SHOW_PPV && s.m[0].kotr != 0;
            CHECK(s.count <= (kotr ? U_MAX_MATCHES
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
                if (m->format == U_FMT_TAG) CHECK(m->team[0] != m->team[1]);
                if (m->format == U_FMT_HANDICAP) CHECK(m->title == U_NONE);
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
            /* at a PPV every healthy singles champion defends (no brand
             * split: weekly defences come when they are due) */
            for (t = 0; t < u.n_titles; t++) {
                uint8_t h = u_title_holder(&u, (uint8_t)t);
                const UTitle *ti = &u.t[t];
                if (h == U_NONE || ti->kind != U_TITLE_SINGLES || u.w[h].injured_weeks)
                    continue;
                if (s.kind != U_SHOW_PPV) continue;
                if (kotr && t != 0) continue;     /* King of the Ring: one defence */
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

/* The cross-brand feud (Austin vs HHH) can only happen at a PPV. */
static void test_cross_brand_rivalry_at_ppv(void)
{
    Universe u;
    UShow s;
    int i, found = 0;
    demo_universe(&u, 21);
    u.r[1].intensity = 100; /* keep it hot so it survives to the PPV */
    run_shows(&u, 8);       /* weeks 1-4 TV */
    CHECK(u.slot == 2);
    u.r[1].intensity = 100;
    u_book_show(&u, &s);
    for (i = 0; i < s.count; i++)
        if (involves(&s.m[i], 12) && involves(&s.m[i], 1)) {
            found = 1;
            CHECK(u_stip_supported(s.m[i].format, s.m[i].stip));
        }
    CHECK(found);
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
    for (seed = 1; seed <= 6; seed++) {
        Universe u;
        UShow s;
        int show, i;
        demo_universe(&u, seed);
        for (show = 0; show < 52 * 2 + 13; show++) {
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
    demo_universe(&u, 4);
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
    printf("%d checks, %d failures (save size %d bytes)\n", checks, failures,
           U_SAVE_SIZE);
    return failures ? 1 : 0;
}
