/*
 * Boot-time Universe Mode screen. Runs before No Mercy starts, uses the same
 * engine as the host CLI, then hands control to the game.
 *
 * Persistence: with an Expansion Pak, the universe is kept at the top of the
 * extra RAM (0x807F0000), which survives a console reset. That is how the
 * result of a match played in No Mercy gets back here: pick "play", play
 * the match in Exhibition, press reset, enter the result.
 */
#include "hw.h"
#include "../engine/universe.h"
#include "../host/demo_roster.h"

#define C_BG     RGB(12, 12, 36)
#define C_BAR    RGB(150, 16, 16)
#define C_TEXT   RGB(230, 230, 230)
#define C_DIM    RGB(130, 130, 150)
#define C_HI     RGB(255, 210, 40)
#define C_SEL    RGB(50, 50, 110)
#define C_GOOD   RGB(90, 220, 120)

/* ---------- persistent state ---------- */

#define PERSIST_ADDR  0xA07F0000u
#define PERSIST_MAGIC 0x4E4D5550u /* 'NMUP' */

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint8_t  save[U_SAVE_SIZE];
    uint8_t  show_active;
    uint8_t  pending;           /* match being played in No Mercy, or U_NONE */
    uint8_t  pad[2];
    UShow    show;
    uint32_t check;
} Persist;

static Universe U;
static UShow    SHOW;
static uint8_t  show_active;
static uint8_t  pending = U_NONE;
static uint8_t  has_persist;
static Persist  P;
static const char *notice = "";

static uint32_t persist_check(const Persist *p)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t n = (uint32_t)((const uint8_t *)&p->check - b), h = 0x811C9DC5u, i;
    for (i = 0; i < n; i++) h = (h ^ b[i]) * 0x01000193u; /* FNV-1a */
    return h;
}

static void persist_store(void)
{
    volatile uint8_t *dst = (volatile uint8_t *)PERSIST_ADDR;
    const uint8_t *src = (const uint8_t *)&P;
    uint32_t i;
    if (!has_persist) return;
    P.magic = PERSIST_MAGIC;
    P.length = sizeof P;
    u_save(&U, P.save, U_SAVE_SIZE);
    P.show_active = show_active;
    P.pending = pending;
    P.show = SHOW;
    P.check = persist_check(&P);
    for (i = 0; i < sizeof P; i++) dst[i] = src[i];
}

static int show_valid(const UShow *s, const Universe *u)
{
    int i, k;
    if (s->count > U_MAX_MATCHES) return 0;
    for (i = 0; i < s->count; i++) {
        const UMatch *m = &s->m[i];
        for (k = 0; k < 4; k++) {
            uint8_t id = m->side[k / 2][k % 2];
            if (id != U_NONE && id >= u->n_wrestlers) return 0;
        }
        if (m->type == U_MT_TAG &&
            (m->team[0] >= u->n_teams || m->team[1] >= u->n_teams)) return 0;
        if (m->title != U_NONE && m->title >= u->n_titles) return 0;
        if (m->stip >= U_STIP_COUNT || (m->done && m->winner > 1)) return 0;
    }
    return 1;
}

static int persist_load(void)
{
    volatile const uint8_t *src = (volatile const uint8_t *)PERSIST_ADDR;
    uint8_t *dst = (uint8_t *)&P;
    uint32_t i;
    for (i = 0; i < sizeof P; i++) dst[i] = src[i];
    if (P.magic != PERSIST_MAGIC || P.length != sizeof P || P.check != persist_check(&P))
        return 0;
    if (u_load(&U, P.save, U_SAVE_SIZE) != 0) return 0;
    SHOW = P.show;
    show_active = P.show_active ? 1 : 0;
    pending = P.pending;
    if (show_active && !show_valid(&SHOW, &U)) show_active = 0;
    if (!show_active || pending >= SHOW.count) pending = U_NONE;
    return 1;
}

/* ---------- tiny string builder ---------- */

typedef struct {
    char b[SCREEN_COLS + 1];
    int  n;
} Line;

static void ln_reset(Line *l) { l->n = 0; l->b[0] = 0; }

static void ln_str(Line *l, const char *s)
{
    while (*s && l->n < SCREEN_COLS) l->b[l->n++] = *s++;
    l->b[l->n] = 0;
}

static void ln_num(Line *l, int v)
{
    char tmp[12];
    int n = 0;
    unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    if (v < 0) ln_str(l, "-");
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n--) {
        char c[2] = {tmp[n], 0};
        ln_str(l, c);
    }
}

static void ln_pad(Line *l, int col)
{
    while (l->n < col && l->n < SCREEN_COLS) l->b[l->n++] = ' ';
    l->b[l->n] = 0;
}

static void text(int col, int row, const char *s, uint16_t c) { draw_text(col, row, s, c); }

static const char *side_name(const UMatch *m, int side)
{
    return m->type == U_MT_TAG ? U.tm[m->team[side]].name : U.w[m->side[side][0]].name;
}

static const char *holder_name(uint8_t title, uint8_t h)
{
    return U.t[title].kind == U_TITLE_TAG ? U.tm[h].name : U.w[h].name;
}

/* ---------- screens ---------- */

enum { SC_HOME, SC_CARD, SC_PLAY, SC_REPORT, SC_HEADLINES, SC_CHAMPS, SC_RANK,
       SC_RIVALS, SC_NEW };

static int screen = SC_HOME, cursor, report_step, report_side, rank_brand;
static int leave;

#define MAX_HEADLINES 14
static char headlines[MAX_HEADLINES][SCREEN_COLS + 1];
static int  n_headlines;

static void add_headline(const char *a, const char *b, const char *c, const char *d)
{
    Line l;
    int i;
    if (n_headlines >= MAX_HEADLINES) return;
    ln_reset(&l);
    ln_str(&l, a); ln_str(&l, b); ln_str(&l, c); ln_str(&l, d);
    for (i = 0; i <= l.n; i++) headlines[n_headlines][i] = l.b[i];
    n_headlines++;
}

static void header(const char *title)
{
    Line l;
    fb_clear(C_BG);
    fb_rect(0, 12, 320, 12, C_BAR);
    text(2, 2, "NO MERCY UNIVERSE", C_TEXT);
    ln_reset(&l);
    ln_str(&l, "WEEK ");
    ln_num(&l, U.week);
    text(38 - l.n, 2, l.b, C_HI);
    text(2, 4, title, C_TEXT);
}

static void footer(const char *s) { text(2, 27, s, C_DIM); }

static void menu_item(int row, const char *s, int selected)
{
    if (selected) fb_rect(8, row * 8 - 1, 304, 9, C_SEL);
    text(3, row, s, selected ? C_HI : C_TEXT);
}

static const char *const HOME_ITEMS[] = {
    "NEXT SHOW", "CHAMPIONS", "RANKINGS", "RIVALRIES", "NEW UNIVERSE", "PLAY NO MERCY",
};
#define HOME_COUNT 6

static void draw_home(void)
{
    Line l;
    int i;
    header("MAIN MENU");
    ln_reset(&l);
    ln_str(&l, show_active ? "SHOW IN PROGRESS: " : "NEXT: ");
    ln_str(&l, u_show_name(&U));
    text(2, 7, l.b, C_HI);
    for (i = 0; i < HOME_COUNT; i++) menu_item(9 + i * 2, HOME_ITEMS[i], cursor == i);
    text(2, 23, notice, C_GOOD);
    text(2, 25, has_persist ? "SAVED IN EXPANSION PAK RAM"
                            : "NO EXPANSION PAK: NOT KEPT", C_DIM);
    footer("A: SELECT   START: PLAY GAME");
}

static int all_done(void)
{
    int i;
    for (i = 0; i < SHOW.count; i++)
        if (!SHOW.m[i].done) return 0;
    return 1;
}

static void draw_card(void)
{
    Line l;
    int i, row = 6;
    ln_reset(&l);
    if (SHOW.kind == U_SHOW_PPV) ln_str(&l, "PPV: ");
    ln_str(&l, u_show_name(&U));
    header(l.b);
    for (i = 0; i < SHOW.count; i++, row += 2) {
        const UMatch *m = &SHOW.m[i];
        ln_reset(&l);
        ln_num(&l, i + 1);
        ln_str(&l, i == SHOW.count - 1 ? "* " : ". ");
        ln_str(&l, side_name(m, 0));
        ln_str(&l, " VS ");
        ln_str(&l, side_name(m, 1));
        menu_item(row, l.b, cursor == i);
        ln_reset(&l);
        ln_pad(&l, 5);
        if (m->title != U_NONE) { ln_str(&l, U.t[m->title].name); ln_str(&l, " "); }
        if (m->stip != U_STIP_NORMAL) { ln_str(&l, u_stip_name(m->stip)); ln_str(&l, " "); }
        if (m->done) {
            ln_str(&l, "> ");
            ln_str(&l, side_name(m, m->winner));
            ln_str(&l, m->source == U_SRC_PLAYED ? " (PLAYED)" : "");
            text(0, row + 1, l.b, C_GOOD);
        } else if (pending == i) {
            ln_str(&l, "> BEING PLAYED");
            text(0, row + 1, l.b, C_HI);
        } else {
            text(0, row + 1, l.b, C_DIM);
        }
    }
    if (all_done()) menu_item(row + 1, "FINISH SHOW", cursor == SHOW.count);
    footer(all_done() ? "A: FINISH SHOW   B: BACK"
                      : "A:SIM Z:SIM ALL START:PLAY B:BACK");
}

static void draw_play(void)
{
    const UMatch *m = &SHOW.m[pending];
    Line l;
    header("PLAY THIS MATCH IN NO MERCY");
    ln_reset(&l);
    ln_str(&l, side_name(m, 0));
    ln_str(&l, " VS ");
    ln_str(&l, side_name(m, 1));
    text(2, 7, l.b, C_HI);
    ln_reset(&l);
    ln_str(&l, "EXHIBITION > ");
    ln_str(&l, m->type == U_MT_TAG ? "TAG TEAM" : "SINGLES");
    if (m->stip != U_STIP_NORMAL) { ln_str(&l, " > "); ln_str(&l, u_stip_name(m->stip)); }
    text(2, 10, l.b, C_TEXT);
    if (m->title != U_NONE) {
        ln_reset(&l);
        ln_str(&l, "FOR THE ");
        ln_str(&l, U.t[m->title].name);
        text(2, 12, l.b, C_TEXT);
    }
    text(2, 15, "WHEN THE MATCH ENDS, PRESS RESET.", C_TEXT);
    text(2, 16, "THIS SCREEN WILL ASK WHO WON.", C_TEXT);
    if (!has_persist) {
        text(2, 19, "WARNING: NO EXPANSION PAK, THE", C_HI);
        text(2, 20, "RESULT CANNOT BE BROUGHT BACK.", C_HI);
    }
    footer("A: START NO MERCY   B: CANCEL");
}

static uint8_t report_methods[U_WIN_METHOD_COUNT];

static int allowed_methods(const UMatch *m)
{
    int i, n = 0;
    for (i = 0; i < U_WIN_METHOD_COUNT; i++)
        if (u_method_allowed(m->stip, (uint8_t)i)) report_methods[n++] = (uint8_t)i;
    return n;
}

static void draw_report(void)
{
    const UMatch *m = &SHOW.m[pending];
    Line l;
    int i, n;
    header("REPORT YOUR MATCH RESULT");
    ln_reset(&l);
    ln_str(&l, side_name(m, 0));
    ln_str(&l, " VS ");
    ln_str(&l, side_name(m, 1));
    text(2, 7, l.b, C_HI);
    if (report_step == 0) {
        text(2, 10, "WHO WON?", C_TEXT);
        menu_item(12, side_name(m, 0), cursor == 0);
        menu_item(14, side_name(m, 1), cursor == 1);
        menu_item(17, "I DIDN'T PLAY IT", cursor == 2);
        footer("A: SELECT");
    } else {
        ln_reset(&l);
        ln_str(&l, side_name(m, report_side));
        ln_str(&l, " WON BY...");
        text(2, 10, l.b, C_TEXT);
        n = allowed_methods(m);
        for (i = 0; i < n; i++)
            menu_item(12 + i * 2, u_method_name(report_methods[i], m->stip), cursor == i);
        footer("A: CONFIRM   B: BACK");
    }
}

static void draw_headlines(void)
{
    int i;
    header("SHOW RESULTS");
    for (i = 0; i < n_headlines; i++) text(2, 7 + i * (n_headlines > 9 ? 1 : 2), headlines[i], i ? C_TEXT : C_HI);
    footer("A: CONTINUE");
}

static void draw_champs(void)
{
    Line l;
    int i, row = 7;
    header("CHAMPIONS");
    for (i = 0; i < U.n_titles; i++, row += 4) {
        const UTitle *t = &U.t[i];
        uint8_t h = u_title_holder(&U, (uint8_t)i);
        ln_reset(&l);
        ln_str(&l, t->name);
        ln_str(&l, t->brand == U_BRAND_RAW ? " (RAW)" : " (SD)");
        text(2, row, l.b, C_HI);
        ln_reset(&l);
        ln_str(&l, "  ");
        if (h == U_NONE) {
            ln_str(&l, "VACANT");
        } else {
            const UReign *r = &t->history[(t->reign_count - 1) % U_MAX_REIGNS];
            ln_str(&l, holder_name((uint8_t)i, h));
            ln_str(&l, "  SINCE WK ");
            ln_num(&l, r->won_week);
            ln_str(&l, ", ");
            ln_num(&l, r->defenses);
            ln_str(&l, " DEF");
        }
        text(2, row + 1, l.b, C_TEXT);
        if (t->reign_count > 1 || (t->vacant && t->reign_count)) {
            int k = t->vacant ? t->reign_count : t->reign_count - 1;
            const UReign *r = &t->history[(k - 1) % U_MAX_REIGNS];
            ln_reset(&l);
            ln_str(&l, "  PREV: ");
            ln_str(&l, holder_name((uint8_t)i, r->holder));
            ln_str(&l, " WK ");
            ln_num(&l, r->won_week);
            ln_str(&l, "-");
            ln_num(&l, r->lost_week);
            text(2, row + 2, l.b, C_DIM);
        }
    }
    footer("B: BACK");
}

static void draw_rank(void)
{
    uint8_t rank[U_MAX_WRESTLERS];
    Line l;
    int n = u_rankings(&U, (uint8_t)rank_brand, rank, U_MAX_WRESTLERS), i;
    header(rank_brand == U_BRAND_RAW ? "RANKINGS: RAW" : "RANKINGS: SMACKDOWN");
    for (i = 0; i < n && i < 16; i++) {
        const UWrestler *w = &U.w[rank[i]];
        ln_reset(&l);
        ln_num(&l, i + 1);
        ln_str(&l, ". ");
        ln_str(&l, w->name);
        ln_pad(&l, 20);
        ln_num(&l, w->wins);
        ln_str(&l, "-");
        ln_num(&l, w->losses);
        ln_pad(&l, 28);
        if (w->injured_weeks) ln_str(&l, "INJURED");
        else if (w->align == U_HEEL) ln_str(&l, "HEEL");
        else ln_str(&l, "FACE");
        text(2, 6 + i, l.b, w->injured_weeks ? C_DIM : C_TEXT);
    }
    footer("LEFT/RIGHT: BRAND   B: BACK");
}

static void draw_rivals(void)
{
    Line l;
    int i, row = 7;
    header("RIVALRIES");
    for (i = 0; i < U_MAX_RIVALRIES; i++) {
        const URivalry *r = &U.r[i];
        if (!r->intensity) continue;
        ln_reset(&l);
        ln_str(&l, U.w[r->a].name);
        ln_str(&l, " VS ");
        ln_str(&l, U.w[r->b].name);
        text(2, row, l.b, C_TEXT);
        ln_reset(&l);
        ln_str(&l, "  HEAT ");
        ln_num(&l, r->intensity);
        ln_str(&l, "  SHOWS ");
        ln_num(&l, r->weeks);
        text(2, row + 1, l.b, r->intensity >= 70 ? C_HI : C_DIM);
        row += 3;
    }
    if (row == 7) text(2, row, "NO ACTIVE RIVALRIES", C_DIM);
    footer("B: BACK");
}

static void draw_new(void)
{
    header("NEW UNIVERSE");
    text(2, 8, "START A NEW UNIVERSE?", C_HI);
    text(2, 10, "ALL CURRENT PROGRESS WILL BE LOST.", C_TEXT);
    footer("A: YES   B: NO");
}

static void draw(void)
{
    switch (screen) {
    case SC_HOME:      draw_home(); break;
    case SC_CARD:      draw_card(); break;
    case SC_PLAY:      draw_play(); break;
    case SC_REPORT:    draw_report(); break;
    case SC_HEADLINES: draw_headlines(); break;
    case SC_CHAMPS:    draw_champs(); break;
    case SC_RANK:      draw_rank(); break;
    case SC_RIVALS:    draw_rivals(); break;
    case SC_NEW:       draw_new(); break;
    }
}

/* ---------- actions ---------- */

static void go(int s)
{
    screen = s;
    cursor = 0;
}

static void finish_show(void)
{
    int i;
    char ppv = SHOW.kind == U_SHOW_PPV;
    Line l;

    ln_reset(&l);
    ln_str(&l, ppv ? "PPV: " : "");
    ln_str(&l, u_show_name(&U));
    ln_str(&l, " IS IN THE BOOKS");
    u_apply_show(&U, &SHOW);
    n_headlines = 0;
    add_headline(l.b, "", "", "");
    for (i = 0; i < SHOW.count; i++) {
        const UMatch *m = &SHOW.m[i];
        if (m->title_changed)
            add_headline("NEW CHAMP: ", side_name(m, m->winner), "", "");
        if (m->rivalry_started)
            add_headline("NEW RIVALRY: ", side_name(m, 0), " VS ", side_name(m, 1));
        if (m->rivalry_ended)
            add_headline(side_name(m, m->winner), " ENDS FEUD W/ ", side_name(m, 1 - m->winner), "");
        if (m->injured != U_NONE)
            add_headline("INJURED: ", U.w[m->injured].name, "", "");
    }
    if (n_headlines == 1) add_headline("NO TITLE CHANGES OR NEW FEUDS.", "", "", "");
    show_active = 0;
    pending = U_NONE;
    persist_store();
    go(SC_HEADLINES);
}

static void new_universe(void)
{
    demo_universe(&U, cp0_count() | 1);
    show_active = 0;
    pending = U_NONE;
    persist_store();
}

static void move_cursor(uint16_t btn, int count)
{
    if (btn & BTN_UP) cursor = cursor > 0 ? cursor - 1 : count - 1;
    if (btn & BTN_DOWN) cursor = cursor + 1 < count ? cursor + 1 : 0;
}

static void handle(uint16_t btn)
{
    int i;
    switch (screen) {
    case SC_HOME:
        move_cursor(btn, HOME_COUNT);
        if (btn & BTN_START) { leave = 1; break; }
        if (!(btn & BTN_A)) break;
        notice = "";
        switch (cursor) {
        case 0:
            if (!show_active) {
                u_book_show(&U, &SHOW);
                show_active = 1;
                pending = U_NONE;
                persist_store();
            }
            go(SC_CARD);
            break;
        case 1: go(SC_CHAMPS); break;
        case 2: go(SC_RANK); rank_brand = U_BRAND_RAW; break;
        case 3: go(SC_RIVALS); break;
        case 4: go(SC_NEW); break;
        case 5: leave = 1; break;
        }
        break;

    case SC_CARD:
        move_cursor(btn, SHOW.count + (all_done() ? 1 : 0));
        if (btn & BTN_B) { go(SC_HOME); break; }
        if (btn & BTN_Z) {
            for (i = 0; i < SHOW.count; i++)
                if (!SHOW.m[i].done) u_sim_match(&U, &SHOW.m[i]);
            pending = U_NONE;
            persist_store();
            cursor = SHOW.count;
            break;
        }
        if (cursor == SHOW.count) {
            if (btn & BTN_A) finish_show();
            break;
        }
        if ((btn & BTN_A) && !SHOW.m[cursor].done) {
            u_sim_match(&U, &SHOW.m[cursor]);
            if (pending == cursor) pending = U_NONE;
            persist_store();
            if (all_done()) cursor = SHOW.count;
        } else if ((btn & BTN_START) && !SHOW.m[cursor].done) {
            pending = (uint8_t)cursor;
            persist_store();
            go(SC_PLAY);
        }
        break;

    case SC_PLAY:
        if (btn & BTN_A) leave = 1;
        if (btn & BTN_B) {
            i = pending;
            pending = U_NONE;
            persist_store();
            go(SC_CARD);
            cursor = i;
        }
        break;

    case SC_REPORT:
        if (report_step == 0) {
            move_cursor(btn, 3);
            if (!(btn & BTN_A)) break;
            if (cursor == 2) {          /* not played: back to the card */
                i = pending;
                pending = U_NONE;
                persist_store();
                go(SC_CARD);
                cursor = i;
                break;
            }
            report_side = cursor;
            report_step = 1;
            cursor = 0;
            if (allowed_methods(&SHOW.m[pending]) == 1) btn = BTN_A; else break;
        }
        {
            int n = allowed_methods(&SHOW.m[pending]);
            move_cursor(btn, n);
            if (btn & BTN_B) { report_step = 0; cursor = report_side; break; }
            if (!(btn & BTN_A)) break;
            u_set_result(&SHOW.m[pending], (uint8_t)report_side, report_methods[cursor]);
            i = pending;
            pending = U_NONE;
            persist_store();
            go(SC_CARD);
            cursor = all_done() ? SHOW.count : i;
        }
        break;

    case SC_HEADLINES:
        if (btn & (BTN_A | BTN_B | BTN_START)) go(SC_HOME);
        break;

    case SC_RANK:
        if (btn & (BTN_LEFT | BTN_RIGHT | BTN_L | BTN_R)) rank_brand ^= 1;
        if (btn & BTN_B) go(SC_HOME);
        break;

    case SC_NEW:
        if (btn & BTN_A) {
            new_universe();
            notice = "NEW UNIVERSE STARTED";
            go(SC_HOME);
        }
        if (btn & BTN_B) go(SC_HOME);
        break;

    default:
        if (btn & BTN_B) go(SC_HOME);
        break;
    }
}

/* The old boot-time Universe screen. Now only shown when Z is held at power-on
 * (kept for testing until the in-game mode replaces it). */
void boot_ui_main(void)
{
    uint16_t prev, now, pressed;

    vi_init();
    has_persist = mem_size() >= 0x800000u;
    if (has_persist && persist_load()) {
        notice = "UNIVERSE LOADED";
    } else {
        demo_universe(&U, cp0_count() | 1);
        notice = has_persist ? "NEW UNIVERSE STARTED" : "";
        persist_store();
    }
    if (pending != U_NONE) {
        screen = SC_REPORT;
        report_step = 0;
    }

    prev = pad_read();
    draw();
    while (!leave) {
        wait_frame();
        now = pad_read();
        pressed = now & (uint16_t)~prev;
        prev = now;
        if (pressed) {
            handle(pressed);
            if (!leave) draw();
        }
    }
    if (screen != SC_PLAY) pending = U_NONE;
    persist_store();
}
