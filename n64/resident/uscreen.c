/*
 * In-game Universe Mode screen, drawn with No Mercy's own menu font
 * (menu overlay draw_text routines) on top of the Single Play menu.
 *
 * Card view:    Up/Down choose a match, A simulate it, START simulate the
 *               whole card, A on a finished card -> results, B back.
 * Results view: headlines of the show, A -> next show.
 */
#include "../../engine/universe.h"
#include "../../host/demo_roster.h"
#include "uscreen.h"

/* Menu overlay draw_text(holder, str, x, y, alpha, style, flags).
 * x/y are in a 480x240 space (x is scaled to 640 on screen). */
typedef void (*draw_text_fn)(void *holder, const char *s, int x, int y,
                             int alpha, int style, int flags);
#define DRAW_TEXT_C ((draw_text_fn)0x800DDDE8u)   /* centred on x, y */
#define TEXT_STYLE  0x30
#define TEXT_FLAGS  0x100

#define PANEL_X     363   /* centre of the right-hand panel (480-wide space) */

#define B_A      0x8000
#define B_B      0x4000
#define B_START  0x1000
#define B_DUP    0x0800
#define B_DDOWN  0x0400
#define B_Z      0x2000
#define B_R      0x0010

static Universe U;
static UShow    SHOW;
static uint8_t  started, show_ready, cursor, view;  /* view 0 card, 1 results */
static uint8_t  pending = U_NONE, pending_watch;    /* match being played/watched in the game */

#define MAX_LINES 10
#define LINE_LEN  48
static char headline[MAX_LINES][LINE_LEN];
static int  n_headlines;

/* ---- tiny string helpers (no libc) ---- */
typedef struct { char *b; int n, cap; } Str;

static void s_init(Str *s, char *buf, int cap) { s->b = buf; s->n = 0; s->cap = cap; buf[0] = 0; }
static void s_add(Str *s, const char *t)
{
    while (*t && s->n < s->cap - 1) s->b[s->n++] = *t++;
    s->b[s->n] = 0;
}
static void s_num(Str *s, unsigned v)
{
    char tmp[12];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n--) { char c[2] = {tmp[n], 0}; s_add(s, c); }
}

static const char *side(const UMatch *m, int k)
{
    return m->type == U_MT_TAG ? U.tm[m->team[k]].name : U.w[m->side[k][0]].name;
}

static int card_done(void)
{
    int i;
    for (i = 0; i < SHOW.count; i++)
        if (!SHOW.m[i].done) return 0;
    return 1;
}

/* Solid dark panel behind the Universe text, drawn the way the game clears
 * the screen: FILL cycle with a fill colour (RGBA5551 x2). The frame buffer
 * is 480x240. The game's usual text render state is restored afterwards. */
#define PANEL_COLOUR 0x108D108Du    /* dark navy, RGBA5551 twice */

static void panel(void *holder, int x0, int y0, int x1, int y1)
{
    uint32_t **dl = (uint32_t **)holder;
    uint32_t *g = *dl;
    g[0]  = 0xE7000000u; g[1]  = 0;                   /* pipe sync */
    g[2]  = 0xE3000A01u; g[3]  = 0x00300000u;         /* cycle type: FILL */
    g[4]  = 0xE200001Fu; g[5]  = 0x00000000u;         /* render mode 0: FILL must not read the image */
    g[6]  = 0xF7000000u; g[7]  = PANEL_COLOUR;        /* fill colour */
    g[8]  = 0xF6000000u | (uint32_t)(x1 << 14) | (uint32_t)(y1 << 2);
    g[9]  = (uint32_t)(x0 << 14) | (uint32_t)(y0 << 2);
    g[10] = 0xE7000000u; g[11] = 0;                   /* pipe sync */
    g[12] = 0xEF08ACEFu; g[13] = 0x00504240u;         /* restore the game's text mode */
    g[14] = 0xFC5196A3u; g[15] = 0x112CFE7Fu;         /* and its text combine */
    *dl = g + 16;
}

/* draw_text's fifth argument is a scale (255 = 100%) */
static void text_scaled(void *holder, const char *s, int y, int scale)
{
    DRAW_TEXT_C(holder, s, PANEL_X, y, scale, TEXT_STYLE, TEXT_FLAGS);
}

static void text(void *holder, const char *s, int y) { text_scaled(holder, s, y, 0xFF); }
static void small(void *holder, const char *s, int y) { text_scaled(holder, s, y, 0xD8); }

/* ---- public ---- */

void us_enter(uint32_t seed)
{
    if (!started) {
        demo_universe(&U, seed | 1);
        started = 1;
    }
    if (!show_ready) {
        u_book_show(&U, &SHOW);
        show_ready = 1;
        cursor = 0;
    }
    view = 0;
}

static void finish_show(void)
{
    int i;
    Str s;
    u_apply_show(&U, &SHOW);
    n_headlines = 0;
    for (i = 0; i < SHOW.count && n_headlines < MAX_LINES; i++) {
        const UMatch *m = &SHOW.m[i];
        if (m->title_changed) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "NEW CHAMPION: ");
            s_add(&s, side(m, m->winner));
        }
        if (m->rivalry_started && n_headlines < MAX_LINES) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "Rivalry: ");
            s_add(&s, side(m, 0));
            s_add(&s, " vs ");
            s_add(&s, side(m, 1));
        }
        if (m->injured != U_NONE && n_headlines < MAX_LINES) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "Injured: ");
            s_add(&s, U.w[m->injured].name);
        }
    }
    if (!n_headlines) {
        s_init(&s, headline[n_headlines++], LINE_LEN);
        s_add(&s, "No title changes this week");
    }
    show_ready = 0;
    view = 1;
}

int us_input(uint16_t pressed)
{
    if (pressed & B_B) return 0;
    if (view == 1) {                      /* results -> next card */
        if (pressed & (B_A | B_START)) us_enter(0);
        return 1;
    }
    if (pressed & B_DUP) cursor = cursor ? cursor - 1 : (uint8_t)(SHOW.count - 1);
    if (pressed & B_DDOWN) cursor = (uint8_t)((cursor + 1) % SHOW.count);
    if (card_done()) {
        if (pressed & (B_A | B_START)) finish_show();
        return 1;
    }
    if ((pressed & (B_Z | B_R)) && !SHOW.m[cursor].done &&
        SHOW.m[cursor].type == U_MT_SINGLES) {
        pending = cursor;
        pending_watch = (pressed & B_Z) ? 1 : 0;
        return 2;                         /* resident launches the real match */
    }
    if (pressed & B_START) {
        int i;
        for (i = 0; i < SHOW.count; i++)
            if (!SHOW.m[i].done) u_sim_match(&U, &SHOW.m[i]);
    } else if ((pressed & B_A) && !SHOW.m[cursor].done) {
        u_sim_match(&U, &SHOW.m[cursor]);
    }
    return 1;
}

const char *us_pending_name(int side)
{
    return demo_game_name(SHOW.m[pending].side[side][0]);
}

int us_pending_watch(void) { return pending_watch; }

/* No Mercy belt for each Universe title (demo roster order: WWF, IC, Tag,
 * European); the game's belts are 0 WWF, 1 Tag, 2 IC, 3 European,
 * 4 Hardcore, 5 Light Heavyweight, 6 Women's. */
static const int8_t GAME_BELT[] = { 0, 2, 1, 3, -1, -1 };

void us_pending_setup(uint8_t game_wrestler[2], int *belt, uint8_t *champion)
{
    const UMatch *m = &SHOW.m[pending];
    int s;
    for (s = 0; s < 2; s++) game_wrestler[s] = demo_game_index(m->side[s][0]);
    *belt = -1;
    *champion = 0;
    if (m->title != U_NONE && m->title < sizeof GAME_BELT && GAME_BELT[m->title] >= 0 &&
        U.t[m->title].kind == U_TITLE_SINGLES) {
        uint8_t h = u_title_holder(&U, m->title);
        *belt = GAME_BELT[m->title];
        *champion = h == U_NONE ? 0 : demo_game_index(h);
    }
}

/* Universe wrestler for a No Mercy wrestler number (U_NONE if not in it) */
static uint8_t universe_wrestler(uint8_t game_n)
{
    int i;
    if (!game_n) return U_NONE;
    for (i = 0; i < U.n_wrestlers; i++)
        if (demo_game_index((uint8_t)i) == game_n) return (uint8_t)i;
    return U_NONE;
}

/* Result of a match played/watched in No Mercy. game_n: who actually
 * wrestled on each side (the player may change the picks on Superstar
 * Select); a wrestler from the universe replaces the card's one. The
 * loser's result byte says how they lost; 1 = pinned (seen on every
 * pinfall so far). A rematch calls this again and replaces the result. */
void us_match_result(int winner_side, int loser_code, const uint8_t game_n[2])
{
    UMatch *m;
    uint8_t w[2];
    int s;
    if (pending == U_NONE) return;
    m = &SHOW.m[pending];
    for (s = 0; s < 2; s++) {
        w[s] = universe_wrestler(game_n[s]);
        if (w[s] == U_NONE) w[s] = m->side[s][0];
    }
    if (w[0] != w[1]) { m->side[0][0] = w[0]; m->side[1][0] = w[1]; }
    u_set_result(m, (uint8_t)winner_side,
                 m->stip == U_STIP_LADDER ? U_WIN_SPECIAL : U_WIN_PINFALL);
    (void)loser_code;
}

/* The game is back on the menus: no more results for this match */
void us_match_closed(void) { pending = U_NONE; }

static uint8_t cover;      /* full-screen while the menu is steered back underneath */
void us_cover(int on) { cover = (uint8_t)on; }

/* Called at the end of every frame's display list: while the menus change
 * underneath, the whole screen is covered (on every frame, including the
 * first frames of a new menu, before it draws any text of its own). */
void us_draw_cover(void *holder)
{
    if (!cover) return;
    panel(holder, 0, 0, 480, 240);
    text(holder, "UNIVERSE", 110);
}

void us_draw(void *holder)
{
    static char line[12][LINE_LEN];
    Str s;
    int i, y;

    if (cover) return;                    /* drawn over the whole frame instead */
    panel(holder, 250, 8, 476, 228);
    text(holder, "UNIVERSE", 22);
    s_init(&s, line[0], LINE_LEN);
    s_add(&s, "Week ");
    s_num(&s, view == 1 ? SHOW.week : U.week);
    s_add(&s, "  ");
    if (view == 0 && SHOW.kind == U_SHOW_PPV) s_add(&s, "PPV: ");
    s_add(&s, view == 1 ? "Results" : u_show_name(&U));
    text(holder, line[0], 38);

    if (view == 1) {
        for (i = 0; i < n_headlines; i++) small(holder, headline[i], 62 + i * 15);
        text(holder, "$0Next Show   $1Back", 198);
        return;
    }

    y = 58;
    for (i = 0; i < SHOW.count && i < 8; i++, y += 14) {
        const UMatch *m = &SHOW.m[i];
        s_init(&s, line[1 + i], LINE_LEN);
        if (i == cursor) s_add(&s, "$d ");
        if (m->done) {
            s_add(&s, side(m, m->winner));
            s_add(&s, " def. ");
            s_add(&s, side(m, 1 - m->winner));
        } else {
            s_add(&s, side(m, 0));
            s_add(&s, " vs ");
            s_add(&s, side(m, 1));
        }
        small(holder, line[1 + i], y);
    }

    /* details of the selected match */
    {
        const UMatch *m = &SHOW.m[cursor];
        s_init(&s, line[10], LINE_LEN);
        if (cursor == SHOW.count - 1) s_add(&s, "Main Event  ");
        if (m->title != U_NONE) { s_add(&s, U.t[m->title].name); s_add(&s, "  "); }
        if (m->stip != U_STIP_NORMAL) s_add(&s, u_stip_name(m->stip));
        if (m->done) { s_add(&s, "  by "); s_add(&s, u_method_name(m->method, m->stip)); }
        small(holder, line[10], 176);
    }
    if (card_done())
        text(holder, "$0Results   $1Back", 198);
    else if (SHOW.m[cursor].type == U_MT_SINGLES)
        small(holder, "$3Play $4Watch $0Sim $eAll $1Back", 198);
    else
        small(holder, "$0Sim  $eSim All  $1Back", 198);
}
