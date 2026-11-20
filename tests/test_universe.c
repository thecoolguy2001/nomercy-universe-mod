/* Automated checks for the Universe engine. Run with: make test */
#include <stdio.h>
#include <string.h>
#include "../engine/universe.h"
#include "../host/demo_roster.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void run_weeks(Universe *u, int weeks)
{
    UShow s;
    int i, k;
    for (k = 0; k < weeks; k++) {
        u_book_show(u, U_BRAND_RAW, &s);
        for (i = 0; i < s.count; i++) u_sim_match(u, &s.m[i]);
        u_apply_show(u, &s);
    }
}

static void test_save_roundtrip(void)
{
    Universe a, b;
    uint8_t buf1[U_SAVE_SIZE], buf2[U_SAVE_SIZE];
    demo_universe(&a, 7);
    run_weeks(&a, 20);
    CHECK(u_save(&a, buf1, sizeof buf1) == U_SAVE_SIZE);
    CHECK(u_save(&a, buf1, U_SAVE_SIZE - 1) < 0);
    CHECK(u_load(&b, buf1, sizeof buf1) == 0);
    CHECK(u_save(&b, buf2, sizeof buf2) == U_SAVE_SIZE);
    CHECK(memcmp(buf1, buf2, U_SAVE_SIZE) == 0);
    CHECK(b.week == 21);

    /* a loaded universe continues exactly like the original */
    run_weeks(&a, 5);
    run_weeks(&b, 5);
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
    run_weeks(&a, 30);
    run_weeks(&b, 30);
    run_weeks(&c, 30);
    u_save(&a, ba, sizeof ba);
    u_save(&b, bb, sizeof bb);
    u_save(&c, bc, sizeof bc);
    CHECK(memcmp(ba, bb, U_SAVE_SIZE) == 0);
    CHECK(memcmp(ba, bc, U_SAVE_SIZE) != 0);
}

/* A year of shows across several seeds never produces an invalid card. */
static void test_booking_invariants(void)
{
    uint32_t seed;
    for (seed = 1; seed <= 20; seed++) {
        Universe u;
        UShow s;
        int week, i, t;
        demo_universe(&u, seed);
        for (week = 0; week < 52; week++) {
            uint8_t seen[U_MAX_WRESTLERS];
            int title_matches = 0;
            memset(seen, 0, sizeof seen);
            u_book_show(&u, U_BRAND_RAW, &s);
            CHECK(s.count >= 4 && s.count <= U_MAX_MATCHES);
            for (i = 0; i < s.count; i++) {
                const UMatch *m = &s.m[i];
                CHECK(m->a != m->b);
                CHECK(m->a < u.n_wrestlers && m->b < u.n_wrestlers);
                CHECK(!seen[m->a] && !seen[m->b]);
                seen[m->a] = seen[m->b] = 1;
                CHECK(u.w[m->a].injured_weeks == 0 && u.w[m->b].injured_weeks == 0);
                if (m->title != U_NONE) {
                    uint8_t h = u_title_holder(&u, m->title);
                    CHECK(m->a == h || m->b == h);
                    title_matches++;
                }
            }
            /* every healthy champion defends */
            for (t = 0; t < u.n_titles; t++) {
                uint8_t h = u_title_holder(&u, (uint8_t)t);
                if (!u.w[h].injured_weeks) CHECK(seen[h]);
            }
            CHECK(title_matches <= u.n_titles);
            for (i = 0; i < s.count; i++) u_sim_match(&u, &s.m[i]);
            u_apply_show(&u, &s);
        }
        CHECK(u.week == 53);
    }
}

static int find_title_match(const UShow *s, uint8_t title)
{
    int i;
    for (i = 0; i < s->count; i++)
        if (s->m[i].title == title) return i;
    return -1;
}

static void test_title_changes(void)
{
    Universe u;
    UShow s;
    int mi;
    uint8_t champ, challenger;
    const UTitle *t;

    demo_universe(&u, 5);
    u_book_show(&u, U_BRAND_RAW, &s);
    mi = find_title_match(&s, 0);
    CHECK(mi >= 0);
    if (mi < 0) return;
    champ = u_title_holder(&u, 0);
    challenger = s.m[mi].a == champ ? s.m[mi].b : s.m[mi].a;

    /* Challenger wins by DQ: champion keeps the belt. */
    CHECK(u_set_result(&s.m[mi], challenger, U_WIN_DQ) == 0);
    u_apply_show(&u, &s);
    CHECK(u_title_holder(&u, 0) == champ);
    CHECK(!s.m[mi].title_changed);

    /* Next week the challenger wins by pinfall: new champion. */
    u_book_show(&u, U_BRAND_RAW, &s);
    mi = find_title_match(&s, 0);
    CHECK(mi >= 0);
    if (mi < 0) return;
    challenger = s.m[mi].a == champ ? s.m[mi].b : s.m[mi].a;
    CHECK(u_set_result(&s.m[mi], challenger, U_WIN_PINFALL) == 0);
    u_apply_show(&u, &s);
    t = &u.t[0];
    CHECK(u_title_holder(&u, 0) == challenger);
    CHECK(s.m[mi].title_changed);
    CHECK(t->reign_count == 2);
    CHECK(t->history[0].holder == champ);
    CHECK(t->history[0].lost_week == 2);
    CHECK(t->history[0].defenses == 1);
    CHECK(t->history[1].won_week == 2);
    CHECK(t->history[1].lost_week == U_OPEN_WEEK);
}

static void test_played_result_validation(void)
{
    Universe u;
    UShow s;
    uint8_t outsider;
    demo_universe(&u, 3);
    u_book_show(&u, U_BRAND_RAW, &s);
    for (outsider = 0; outsider < u.n_wrestlers; outsider++)
        if (outsider != s.m[0].a && outsider != s.m[0].b) break;
    CHECK(u_set_result(&s.m[0], outsider, U_WIN_PINFALL) == -1);
    CHECK(u_set_result(&s.m[0], s.m[0].a, U_WIN_METHOD_COUNT) == -1);
    CHECK(!s.m[0].done);
    CHECK(u_set_result(&s.m[0], s.m[0].b, U_WIN_SUBMISSION) == 0);
    CHECK(s.m[0].done && s.m[0].source == U_SRC_PLAYED);
}

static void test_rivalry_booked(void)
{
    Universe u;
    UShow s;
    int i, found = 0;
    demo_universe(&u, 11);
    u_add_rivalry(&u, 10, 13, 90); /* Rikishi vs Jeff Hardy */
    u_book_show(&u, U_BRAND_RAW, &s);
    for (i = 0; i < s.count; i++)
        if ((s.m[i].a == 10 && s.m[i].b == 13) || (s.m[i].a == 13 && s.m[i].b == 10))
            found = 1;
    CHECK(found);
}

/* A rival must not be stolen as a title contender (Rock vs Angle week 1). */
static void test_rivalry_not_stolen_by_title_match(void)
{
    Universe u;
    UShow s;
    int i, found = 0;
    demo_universe(&u, 2000);
    u_book_show(&u, U_BRAND_RAW, &s);
    for (i = 0; i < s.count; i++)
        if ((s.m[i].a == 0 && s.m[i].b == 4) || (s.m[i].a == 4 && s.m[i].b == 0))
            found = 1;
    CHECK(found);
}

static void test_injured_skip(void)
{
    Universe u;
    UShow s;
    int i;
    demo_universe(&u, 2);
    u.w[1].injured_weeks = 2; /* Stone Cold */
    u_book_show(&u, U_BRAND_RAW, &s);
    for (i = 0; i < s.count; i++) CHECK(s.m[i].a != 1 && s.m[i].b != 1);
    for (i = 0; i < s.count; i++) u_sim_match(&u, &s.m[i]);
    u_apply_show(&u, &s);
    CHECK(u.w[1].injured_weeks == 1);
}

int main(void)
{
    test_save_roundtrip();
    test_corrupt_save_rejected();
    test_deterministic();
    test_booking_invariants();
    test_title_changes();
    test_played_result_validation();
    test_rivalry_booked();
    test_rivalry_not_stolen_by_title_match();
    test_injured_skip();
    printf("%d checks, %d failures (save size %d bytes)\n", checks, failures,
           U_SAVE_SIZE);
    return failures ? 1 : 0;
}
