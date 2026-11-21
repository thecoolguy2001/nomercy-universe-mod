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

/* ---- arenas ----
 * The game's arenas (ARENA tab order; 8 and 9 are locked there but load
 * fine): 0 Raw is War, 1 No Mercy, 2 SmackDown!, 3 King of the Ring,
 * 4 SummerSlam, 5 Survivor Series, 6 Royal Rumble, 7 WrestleMania,
 * 8 Backlash, 9 Armageddon. PPVs without an arena of their own use No Mercy's. */
static const uint8_t PPV_ARENA[12] = {
    6, 1, 7, 8,     /* Royal Rumble, No Way Out, WrestleMania, Backlash */
    1, 3, 1, 4,     /* Judgment Day, King of the Ring, Fully Loaded, SummerSlam */
    1, 1, 5, 9,     /* Unforgiven, No Mercy, Survivor Series, Armageddon */
};

int us_show_arena(void)
{
    if (!started) return 0;
    if (U.slot == U_BRAND_RAW) return 0;
    if (U.slot == U_BRAND_SMACKDOWN) return 2;
    return PPV_ARENA[(U.week / U_PPV_EVERY + 11) % 12];
}

/* Arena preview pictures (tools/make_arenas.py), in asset order, and the
 * picture of each arena */
#define arena_tiles (*(const uint8_t (*)[10][6][512 + 2048])0x80480000u)  /* boot loader copies them here */
static const uint8_t ARENA_PICTURE[10] = { 2, 8, 1, 9, 0, 4, 6, 3, 5, 7 };

/* The arena's picture over the whole 480x240 frame. Laid out as the ARENA
 * tab does it (menu overlay 0x80125B98, tile rectangles at 0x8014C268):
 * six 64x32 CI8 tiles in two columns of three, tiles 0-2 down the left
 * column, 3-5 down the right one; here each is a 240x80 rectangle. */
static void arena_picture(void *holder, int arena)
{
    uint32_t **dl = (uint32_t **)holder;
    uint32_t *g = *dl;
    int t;
    g[0] = 0xE7000000u; g[1] = 0;                         /* pipe sync */
    g[2] = 0xEF008CEFu; g[3] = 0x0F0A4000u;               /* 1 cycle, TLUT RGBA16, point, opaque */
    g[4] = 0xFCFFFFFFu; g[5] = 0xFFFCFC7Eu;               /* colour = texel, alpha = 1 */
    g += 6;
    for (t = 0; t < 6; t++) {
        const uint8_t *tile = arena_tiles[ARENA_PICTURE[arena]][t];
        uint32_t pal = (uint32_t)tile & 0x00FFFFFFu, px = pal + 512;
        uint32_t x0 = (uint32_t)(t / 3) * 240, y0 = (uint32_t)(t % 3) * 80;
        g[0]  = 0xFD100000u; g[1]  = pal;                 /* palette: RGBA16 image */
        g[2]  = 0xE8000000u; g[3]  = 0;                   /* tile sync */
        g[4]  = 0xF5000100u; g[5]  = 0x07000000u;         /* tile 7 at TMEM 0x800 */
        g[6]  = 0xE6000000u; g[7]  = 0;                   /* load sync */
        g[8]  = 0xF0000000u; g[9]  = 0x073FC000u;         /* load 256 colours */
        g[10] = 0xE7000000u; g[11] = 0;
        g[12] = 0xFD500000u; g[13] = px;                  /* pixels, loaded as 16-bit */
        g[14] = 0xE8000000u; g[15] = 0;
        g[16] = 0xF5500000u; g[17] = 0x07000000u;
        g[18] = 0xE6000000u; g[19] = 0;
        g[20] = 0xF3000000u; g[21] = 0x073FF100u;         /* 1024 words, dxt 256 */
        g[22] = 0xE8000000u; g[23] = 0;
        g[24] = 0xF5481000u; g[25] = 0x00080200u;         /* tile 0: CI8 64 wide, clamp */
        g[26] = 0xF2000000u; g[27] = 0x000FC07Cu;         /* 64x32 */
        g[28] = 0xE4000000u | ((x0 + 240) << 14) | ((y0 + 80) << 2);
        g[29] = (x0 << 14) | (y0 << 2);                   /* texture rectangle, tile 0 */
        g[30] = 0xE1000000u; g[31] = 0;                   /* s, t = 0 */
        g[32] = 0xF1000000u; g[33] = (273u << 16) | 410u; /* 1/3.75, 1/2.5 (s5.10) */
        g += 34;
    }
    g[0] = 0xE7000000u; g[1] = 0;
    g[2] = 0xEF08ACEFu; g[3] = 0x00504240u;               /* restore the game's text mode */
    g[4] = 0xFC5196A3u; g[5] = 0x112CFE7Fu;               /* and its text combine */
    *dl = g + 6;
}

/* Called at the end of every frame's display list: while the menus change
 * underneath, the whole screen is covered (on every frame, including the
 * first frames of a new menu, before it draws any text of its own) by the
 * show's arena with UNIVERSE on it. */
void us_draw_cover(void *holder)
{
    if (!cover) return;
    arena_picture(holder, us_show_arena());
    DRAW_TEXT_C(holder, "UNIVERSE", 240, 110, 0xFF, TEXT_STYLE, TEXT_FLAGS);
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
