/*
 * Host driver for the Universe engine (the same engine the ROM runs).
 *
 *   universe new    <save> [seed]        start a new universe
 *   universe show   <save> [--play]      book and run the next show
 *   universe week   <save> [--play]      run every show left this week
 *   universe status <save>               champions, rankings, rivalries
 *   universe roster <save>               list wrestlers with their ids
 *   universe draft  <save> <id> raw|sd   move a wrestler (titles follow)
 *   universe turn   <save> <id>          face <-> heel
 *   universe rate   <save> <id> <0-100>  set overall rating
 *   universe release|rehire <save> <id>  remove from / return to roster
 *
 * With --play you enter the result of each match you played in No Mercy
 * (or press Enter to simulate that match).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/universe.h"
#include "demo_roster.h"
#include "roster.h"

static const char *BRAND[] = {"Raw", "SmackDown"};

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

static const char *holder_name(const Universe *u, const UTitle *t, uint8_t h)
{
    return t->kind == U_TITLE_TAG ? u->tm[h].name : u->w[h].name;
}

static const char *side_name(const Universe *u, const UMatch *m, int side)
{
    static char buf[4][2 * U_NAME_LEN + 4];
    static int next;
    char *b = buf[next++ & 3];
    u_side_text(u, m, side, b, sizeof buf[0]);
    return b;
}

static void print_status(const Universe *u)
{
    uint8_t rank[U_MAX_WRESTLERS];
    int n, i, k, b;

    printf("\n== Champions ==\n");
    for (i = 0; i < u->n_titles; i++) {
        const UTitle *t = &u->t[i];
        uint8_t h = u_title_holder(u, (uint8_t)i);
        printf("  %-16s %s\n", t->name, h == U_NONE ? "(vacant)" : holder_name(u, t, h));
        for (k = t->reign_count; k > 0 && t->reign_count - k < U_MAX_REIGNS; k--) {
            const UReign *r = &t->history[(k - 1) % U_MAX_REIGNS];
            printf("      #%-2d %-16s wk %u-", k, holder_name(u, t, r->holder), r->won_week);
            if (r->lost_week == U_OPEN_WEEK && !t->vacant) printf("now ");
            else if (r->lost_week == U_OPEN_WEEK) printf("?   ");
            else printf("%-3u ", r->lost_week);
            printf(" defenses: %u\n", r->defenses);
        }
    }
    for (b = 0; b < 2; b++) {                 /* no brand split: men, women */
        n = u_division_rankings(u, b ? U_DIV_WOMEN : U_DIV_MEN, rank, U_MAX_WRESTLERS);
        printf("\n== %s ==\n", b ? "Women" : "Men");
        for (i = 0; i < n; i++) {
            const UWrestler *w = &u->w[rank[i]];
            printf("  %2d. %-16s %4u pts  ovr %2u (%+d)  %s  %3u-%-3u streak %+3d%s\n", i + 1,
                   w->name, w->points, w->overall, (int)w->overall - (int)w->base_overall,
                   w->align == U_FACE ? "face" : "heel", w->wins, w->losses,
                   w->streak, w->injured_weeks ? "  [injured]" : "");
        }
    }
    n = u_team_rankings(u, U_NONE, rank, U_MAX_TEAMS);
    printf("\n== Tag teams ==\n");
    for (i = 0; i < n; i++) {
        const UTeam *t = &u->tm[rank[i]];
        printf("  %2d. %-16s %4u pts  %3u-%-3u streak %+3d\n", i + 1, t->name, t->points,
               t->wins, t->losses, t->streak);
    }
    printf("\n== Title pictures (champion, top %d contenders) ==\n", U_CONTENDERS);
    for (i = 0; i < u->n_titles; i++) {
        const UTitle *t = &u->t[i];
        uint8_t h = u_title_holder(u, (uint8_t)i);
        printf("  %-16s %s:", t->name, h == U_NONE ? "(vacant)" : holder_name(u, t, h));
        n = u_contenders(u, (uint8_t)i, rank, U_CONTENDERS);
        for (k = 0; k < n; k++)
            printf("%s %d. %s", k ? "," : "", k + 1,
                   t->kind == U_TITLE_TAG ? u->tm[rank[k]].name : u->w[rank[k]].name);
        printf("\n");
    }
    printf("\n== Injuries ==\n");
    n = u_injuries(u, rank, U_MAX_WRESTLERS);
    for (i = 0; i < n; i++)
        printf("  %-16s %u week%s\n", u->w[rank[i]].name, u->w[rank[i]].injured_weeks,
               u->w[rank[i]].injured_weeks == 1 ? "" : "s");
    if (!n) printf("  (none)\n");
    printf("\n== Allies ==\n");
    for (i = 0, k = 0; i < U_MAX_ALLIES; i++) {
        const UAlly *x = &u->al[i];
        if (!x->level) continue;
        printf("  %3u %s & %s\n", x->level, u->w[x->a].name, u->w[x->b].name);
        k++;
    }
    if (!k) printf("  (none)\n");
    printf("\n== Rivalries ==\n");
    for (i = 0, k = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &u->r[i];
        if (!r->intensity) continue;
        printf("  %s vs %s  (heat %u, %u shows)\n", u->w[r->a].name,
               u->w[r->b].name, r->intensity, r->weeks);
        k++;
    }
    if (!k) printf("  (none)\n");
}

static void print_match(const Universe *u, const UMatch *m, int num, int last)
{
    char text[128];
    u_match_text(u, m, 0, text, sizeof text);
    printf("  %d. %s%s", num, last ? "MAIN EVENT: " : "", text);
    if (m->kotr) printf("  <King of the Ring %s>", u_round_name(m));
    if (m->format != U_FMT_SINGLE) printf("  <%s>", u_format_name(m->format));
    if (m->stip != U_STIP_NORMAL) printf("  <%s>", u_stip_name(m->stip));
    if (m->title != U_NONE) printf("  [%s]", u->t[m->title].name);
    {
        char why[64];
        if (u_why_text(u, m, why, sizeof why)) printf("  -- %s", why);
        else if (m->rivalry != U_NONE) printf("  -- Rivalry");
    }
    printf("\n");
}

static int ask_result(const Universe *u, UMatch *m)
{
    char line[32];
    uint8_t allowed[U_WIN_METHOD_COUNT];
    int na = 0, i;
    for (i = 0; i < U_WIN_METHOD_COUNT; i++)
        if (i != U_WIN_DRAW && i != U_WIN_RUMBLE && u_method_allowed(m->stip, (uint8_t)i))
            allowed[na++] = (uint8_t)i;
    int sides = u_match_sides(m);
    if (m->stip == U_STIP_IRONMAN || m->format == U_FMT_RUMBLE) return 0;   /* simulated */
    for (;;) {
        printf("     Winner? 1 = %s, 2 = %s", side_name(u, m, 0), side_name(u, m, 1));
        if (sides == 3) printf(", 3 = %s", side_name(u, m, 2));
        printf(", d = draw, Enter = simulate: ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin) || line[0] == '\n') return 0;
        if (line[0] == 'd') return u_set_draw(m) == 0;
        if (line[0] >= '1' && line[0] < '1' + sides) {
            int pick = 0;
            if (na > 1) {
                printf("     How?");
                for (i = 0; i < na; i++)
                    printf(" %d = %s%s", i + 1, u_method_name(allowed[i], m->stip),
                           i + 1 < na ? "," : "");
                printf(" [1]: ");
                fflush(stdout);
                if (fgets(line, sizeof line, stdin) && line[0] >= '1' && line[0] < '1' + na)
                    pick = line[0] - '1';
            }
            return u_set_result(m, (uint8_t)(line[0] - '1'), allowed[pick]) == 0;
        }
    }
}

static void run_show(Universe *u, int play)
{
    UShow s;
    int i;
    const char *name = u_show_name(u);

    u_book_show(u, &s);
    printf("=== %s%s - Week %u ===\n", s.kind == U_SHOW_PPV ? "PPV: " : "", name, s.week);
    for (i = 0; i < s.count; i++) print_match(u, &s.m[i], i + 1, i == s.count - 1);

    printf("\n--- Results ---\n");
    for (i = 0; i < s.count; i++) {
        UMatch *m = &s.m[i];
        if (play) {
            print_match(u, m, i + 1, i == s.count - 1);
            if (!ask_result(u, m)) u_sim_match(u, m);
        } else {
            u_sim_match(u, m);
        }
        u_show_update(&s);                     /* tournament: the next round */
        {
            char text[128];
            u_match_text(u, m, 1, text, sizeof text);
            if (m->method == U_WIN_DRAW) printf("  %s", text);
            else printf("  %s by %s", text, u_method_name(m->method, m->stip));
            if (m->stip == U_STIP_IRONMAN) {
                int k;
                printf(" (falls");
                for (k = 0; k < u_match_sides(m); k++) printf(" %u", m->falls[k]);
                printf(")");
            }
            printf("%s\n", m->source == U_SRC_PLAYED ? "  (played)" : "");
        }
    }

    u_apply_show(u, &s);

    printf("\n--- Headlines ---\n");
    for (i = 0; i < s.count; i++) {
        const UMatch *m = &s.m[i];
        if (m->title_changed)
            printf("  NEW CHAMPION: %s wins the %s!\n", side_name(u, m, m->winner),
                   u->t[m->title].name);
        else if (m->title != U_NONE && u_title_holder(u, m->title) != U_NONE)
            printf("  %s retains the %s.\n",
                   holder_name(u, &u->t[m->title], u_title_holder(u, m->title)),
                   u->t[m->title].name);
        if (m->rivalry_started)
            printf("  A new rivalry: %s vs %s.\n", side_name(u, m, 0), side_name(u, m, 1));
        if (m->rivalry_ended)
            printf("  %s settles the score with %s.\n", side_name(u, m, m->winner),
                   side_name(u, m, m->winner ? 0 : 1));
        if (m->injured != U_NONE)
            printf("  %s is injured and out %u week(s).\n", u->w[m->injured].name,
                   m->injury_weeks);
        if (m->format == U_FMT_RUMBLE && m->winner < m->n_entrants)
            printf("  %s wins the Royal Rumble!\n", u->w[m->entrant[m->winner]].name);
        if (m->kotr == U_KOTR_MATCHES && m->done)
            printf("  %s is crowned King of the Ring!\n", side_name(u, m, m->winner));
        if (m->run_in != U_NONE)
            printf("  Run-in: %s comes to help %s.\n", u->w[m->run_in].name,
                   u->w[m->run_in_for].name);
    }
    if (s.team_formed != U_NONE)
        printf("  NEW TAG TEAM: %s (%s & %s).\n", u->tm[s.team_formed].name,
               u->w[u->tm[s.team_formed].a].name, u->w[u->tm[s.team_formed].b].name);
    if (s.team_split != U_NONE)
        printf("  %s have split up.\n", u->tm[s.team_split].name);
    printf("\n");
}

static int need_id(const Universe *u, int argc, char **argv, int idx)
{
    int id;
    if (argc <= idx) { fprintf(stderr, "missing wrestler id\n"); return -1; }
    id = atoi(argv[idx]);
    if (id < 0 || id >= u->n_wrestlers) { fprintf(stderr, "no wrestler %d\n", id); return -1; }
    return id;
}

int main(int argc, char **argv)
{
    Universe u;
    const char *cmd = argc >= 2 ? argv[1] : "";
    int id, i;

    if (argc >= 3 && !strcmp(cmd, "new")) {
        uint32_t seed = argc >= 4 ? (uint32_t)strtoul(argv[3], 0, 0) : 2000u;
        roster_universe(&u, seed);
        if (save_file(argv[2], &u)) return 1;
        printf("New universe saved to %s (%d bytes, seed %u)\n", argv[2], U_SAVE_SIZE, seed);
        print_status(&u);
        return 0;
    }
    if (argc < 3) goto usage;
    if (load_file(argv[2], &u)) return 1;

    if (!strcmp(cmd, "show") || !strcmp(cmd, "week")) {
        int play = argc >= 4 && !strcmp(argv[3], "--play");
        uint16_t week = u.week;
        do run_show(&u, play); while (!strcmp(cmd, "week") && u.week == week);
        print_status(&u);
    } else if (!strcmp(cmd, "status")) {
        printf("Week %u, next show: %s\n", u.week, u_show_name(&u));
        print_status(&u);
        return 0;
    } else if (!strcmp(cmd, "roster")) {
        for (i = 0; i < u.n_wrestlers; i++)
            printf("  %2d  %-16s %-9s %s ovr %3u%s\n", i, u.w[i].name, BRAND[u.w[i].brand],
                   u.w[i].align == U_FACE ? "face" : "heel", u.w[i].overall,
                   u.w[i].active ? "" : "  (released)");
        return 0;
    } else if (!strcmp(cmd, "draft")) {
        if ((id = need_id(&u, argc, argv, 3)) < 0 || argc < 5) goto usage;
        if (u_set_brand(&u, (uint8_t)id, !strcmp(argv[4], "sd") ? U_BRAND_SMACKDOWN
                                                               : U_BRAND_RAW))
            return 1;
        printf("%s drafted to %s\n", u.w[id].name, BRAND[u.w[id].brand]);
    } else if (!strcmp(cmd, "turn")) {
        if ((id = need_id(&u, argc, argv, 3)) < 0) return 1;
        u_set_align(&u, (uint8_t)id, !u.w[id].align);
        printf("%s is now a %s\n", u.w[id].name, u.w[id].align == U_FACE ? "face" : "heel");
    } else if (!strcmp(cmd, "rate")) {
        if ((id = need_id(&u, argc, argv, 3)) < 0 || argc < 5) goto usage;
        u_set_overall(&u, (uint8_t)id, (uint8_t)atoi(argv[4]));
        printf("%s overall %u\n", u.w[id].name, u.w[id].overall);
    } else if (!strcmp(cmd, "release") || !strcmp(cmd, "rehire")) {
        if ((id = need_id(&u, argc, argv, 3)) < 0) return 1;
        u_set_active(&u, (uint8_t)id, cmd[2] == 'h');
        printf("%s %s\n", u.w[id].name, u.w[id].active ? "rehired" : "released (titles vacated)");
    } else {
        goto usage;
    }
    return save_file(argv[2], &u) ? 1 : 0;

usage:
    fprintf(stderr,
            "usage: %s new <save> [seed] | show|week <save> [--play] | status <save>\n"
            "       roster <save> | draft <save> <id> raw|sd | turn <save> <id>\n"
            "       rate <save> <id> <0-100> | release|rehire <save> <id>\n", argv[0]);
    return 2;
}
