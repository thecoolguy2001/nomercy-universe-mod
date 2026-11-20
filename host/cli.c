/*
 * Host driver for the Universe engine. Stands in for the in-game menus
 * until the engine runs inside No Mercy.
 *
 *   universe new    <save> [seed]   start a new universe
 *   universe week   <save> [--play] book and run one weekly show
 *   universe status <save>          rankings, champions, rivalries
 *
 * With --play you enter the winner of each match you played in the
 * emulator (or press Enter to simulate that match).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/universe.h"
#include "demo_roster.h"

static const char *METHOD[] = {"pinfall", "submission", "count-out", "DQ"};

static int load_file(const char *path, Universe *u)
{
    uint8_t buf[U_SAVE_SIZE];
    FILE *f = fopen(path, "rb");
    size_t n;
    int rc;
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    rc = u_load(u, buf, (int)n);
    if (rc) fprintf(stderr, "%s: not a valid universe save (error %d)\n", path, rc);
    return rc;
}

static int save_file(const char *path, const Universe *u)
{
    uint8_t buf[U_SAVE_SIZE];
    int n = u_save(u, buf, sizeof buf);
    FILE *f;
    if (n != U_SAVE_SIZE) { fprintf(stderr, "save failed\n"); return -1; }
    f = fopen(path, "wb");
    if (!f || fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "cannot write %s\n", path);
        if (f) fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void print_status(const Universe *u)
{
    uint8_t rank[U_MAX_WRESTLERS];
    int n = u_rankings(u, U_NONE, rank, U_MAX_WRESTLERS), i, k;

    printf("\n== Champions ==\n");
    for (i = 0; i < u->n_titles; i++) {
        const UTitle *t = &u->t[i];
        uint8_t h = u_title_holder(u, (uint8_t)i);
        printf("  %-16s %s\n", t->name, h == U_NONE ? "(vacant)" : u->w[h].name);
        for (k = t->reign_count; k > 0 && t->reign_count - k < U_MAX_REIGNS; k--) {
            const UReign *r = &t->history[(k - 1) % U_MAX_REIGNS];
            printf("      #%-2d %-16s wk %u-", k, u->w[r->holder].name, r->won_week);
            if (r->lost_week == U_OPEN_WEEK) printf("now ");
            else printf("%-3u ", r->lost_week);
            printf(" defenses: %u\n", r->defenses);
        }
    }
    printf("\n== Rankings ==\n");
    for (i = 0; i < n; i++) {
        const UWrestler *w = &u->w[rank[i]];
        printf("  %2d. %-16s %s  %3u-%-3u mom %+4d%s\n", i + 1, w->name,
               w->align == U_FACE ? "face" : "heel", w->wins, w->losses,
               w->momentum, w->injured_weeks ? "  [injured]" : "");
    }
    printf("\n== Rivalries ==\n");
    for (i = 0, k = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (!r->intensity) continue;
        printf("  %s vs %s  (heat %u, %u weeks)\n", u->w[r->a].name,
               u->w[r->b].name, r->intensity, r->weeks);
        k++;
    }
    if (!k) printf("  (none)\n");
}

static void print_match(const Universe *u, const UMatch *m, int num, int last)
{
    printf("  %d. %s%s vs %s", num, last ? "MAIN EVENT: " : "",
           u->w[m->a].name, u->w[m->b].name);
    if (m->title != U_NONE) printf("  [%s]", u->t[m->title].name);
    if (m->rivalry != U_NONE) printf("  (rivalry)");
    printf("\n");
}

static int ask_result(const Universe *u, UMatch *m)
{
    char line[32];
    for (;;) {
        printf("     Winner? 1 = %s, 2 = %s, Enter = simulate: ",
               u->w[m->a].name, u->w[m->b].name);
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin) || line[0] == '\n') return 0;
        if (line[0] == '1' || line[0] == '2') {
            uint8_t win = line[0] == '1' ? m->a : m->b;
            int method = 0;
            printf("     How? 1 = pinfall, 2 = submission, 3 = count-out, 4 = DQ [1]: ");
            fflush(stdout);
            if (fgets(line, sizeof line, stdin) && line[0] >= '1' && line[0] <= '4')
                method = line[0] - '1';
            return u_set_result(m, win, (uint8_t)method) == 0;
        }
    }
}

static int cmd_week(const char *path, int play)
{
    Universe u;
    UShow s;
    int i;

    if (load_file(path, &u)) return 1;
    u_book_show(&u, U_BRAND_RAW, &s);

    printf("=== RAW IS WAR - Week %u ===\n", s.week);
    for (i = 0; i < s.count; i++) print_match(&u, &s.m[i], i + 1, i == s.count - 1);

    printf("\n--- Results ---\n");
    for (i = 0; i < s.count; i++) {
        UMatch *m = &s.m[i];
        uint8_t loser;
        if (play) {
            print_match(&u, m, i + 1, i == s.count - 1);
            if (!ask_result(&u, m)) u_sim_match(&u, m);
        } else {
            u_sim_match(&u, m);
        }
        loser = m->winner == m->a ? m->b : m->a;
        printf("  %s def. %s by %s%s\n", u.w[m->winner].name, u.w[loser].name,
               METHOD[m->method], m->source == U_SRC_PLAYED ? "  (played)" : "");
    }

    u_apply_show(&u, &s);

    printf("\n--- Headlines ---\n");
    for (i = 0; i < s.count; i++) {
        const UMatch *m = &s.m[i];
        uint8_t loser = m->winner == m->a ? m->b : m->a;
        if (m->title_changed)
            printf("  NEW CHAMPION: %s wins the %s!\n", u.w[m->winner].name,
                   u.t[m->title].name);
        else if (m->title != U_NONE)
            printf("  %s retains the %s.\n", u.w[u_title_holder(&u, m->title)].name,
                   u.t[m->title].name);
        if (m->rivalry_started)
            printf("  A new rivalry: %s vs %s.\n", u.w[m->a].name, u.w[m->b].name);
        if (m->rivalry_ended)
            printf("  %s settles the score with %s.\n", u.w[m->winner].name,
                   u.w[loser].name);
        if (m->injury_weeks)
            printf("  %s is injured and out %u week(s).\n", u.w[loser].name,
                   m->injury_weeks);
    }

    print_status(&u);
    return save_file(path, &u) ? 1 : 0;
}

int main(int argc, char **argv)
{
    Universe u;
    if (argc >= 3 && !strcmp(argv[1], "new")) {
        uint32_t seed = argc >= 4 ? (uint32_t)strtoul(argv[3], 0, 0) : 2000u;
        demo_universe(&u, seed);
        if (save_file(argv[2], &u)) return 1;
        printf("New universe saved to %s (%d bytes, seed %u)\n", argv[2],
               U_SAVE_SIZE, seed);
        print_status(&u);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "week"))
        return cmd_week(argv[2], argc >= 4 && !strcmp(argv[3], "--play"));
    if (argc >= 3 && !strcmp(argv[1], "status")) {
        if (load_file(argv[2], &u)) return 1;
        printf("Week %u\n", u.week);
        print_status(&u);
        return 0;
    }
    fprintf(stderr, "usage: %s new <save> [seed] | week <save> [--play] | status <save>\n",
            argv[0]);
    return 2;
}
