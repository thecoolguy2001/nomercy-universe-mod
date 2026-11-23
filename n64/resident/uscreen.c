/*
 * In-game Universe Mode screen, drawn with No Mercy's own menu font
 * (menu overlay draw_text routines) on top of the Single Play menu.
 *
 * Card view:    Up/Down choose a match, A simulate it, START simulate the
 *               whole card, A on a finished card -> results, B back,
 *               L rankings.
 * Results view: headlines of the show, A -> next show, L rankings.
 * Rankings:     L / R pages (men, women, tag teams, each belt with its
 *               champion and top 5 contenders, injuries), Up/Down scroll,
 *               B back.
 */
#include "../../engine/universe.h"
#include "../../host/roster.h"
#include "uscreen.h"
#include "usave.h"

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
#define B_L      0x0020

static Universe U;
static UShow    SHOW;
static uint8_t  started, show_ready, cursor, top, view;  /* view 0 card, 1 results, 2 rankings */
static uint8_t  rk_page, rk_top, rk_back;   /* rankings page, scroll, view to go back to */

/* The save (usave.c): the universe and the card in play, written on the
 * Universe screen after a change (a result, a new show) and when leaving */
enum { SAVE_NONE, SAVE_SAVED, SAVE_LOADED, SAVE_FAILED, SAVE_OLD };
static uint8_t  dirty, save_state, confirm_new;
#define SAVE_MAGIC 0x4E4D5553u            /* 'NMUS' */
#define SAVE_HEAD  16
static uint8_t  save_buf[SAVE_HEAD + U_SAVE_SIZE + sizeof(UShow)] __attribute__((aligned(8)));

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint8_t  pending = U_NONE, pending_watch;    /* match being played/watched in the game */

#define MAX_LINES 10
#define LINE_LEN  48
#define RK_ROWS   9      /* rankings lines on screen */
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
    static char buf[4][2 * U_NAME_LEN + 4];
    static uint8_t next;
    char *b = buf[next++ & 3];
    u_side_text(&U, m, k, b, sizeof buf[0]);
    return b;
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

/* A save whose own champions table has belts on it starts the universe
 * with those champions (u16 table at 0x800A53E2 + 0x34 * belt: wrestler
 * number, then the tag partner; 0 = none) */
#define GAME_CHAMPIONS 0x800A53E2u
static uint8_t universe_wrestler(uint8_t game_n, uint8_t attire);

static void champions_from_game(void)
{
    int b, k;
    for (b = 0; b < 7 && b < U.n_titles; b++) {
        uint8_t n0 = *(volatile uint8_t *)(GAME_CHAMPIONS + 0x34u * (unsigned)b);
        uint8_t n1 = *(volatile uint8_t *)(GAME_CHAMPIONS + 0x34u * (unsigned)b + 1u);
        uint8_t a, c;
        if (!n0) continue;
        a = universe_wrestler(n0, 0);
        if (a == U_NONE) continue;
        if (U.t[b].kind != U_TITLE_TAG) {
            if (u_eligible(&U, a, U.t[b].division)) u_set_champion(&U, (uint8_t)b, a);
            continue;
        }
        c = universe_wrestler(n1, 0);
        for (k = 0; k < U.n_teams && c != U_NONE; k++)
            if ((U.tm[k].a == a && U.tm[k].b == c) || (U.tm[k].a == c && U.tm[k].b == a))
                u_set_champion(&U, (uint8_t)b, (uint8_t)k);
    }
}

static uint32_t test_stip, test_rumble;
void us_test_stip(uint32_t stip) { test_stip = stip; }
void us_test_rumble(uint32_t n) { test_rumble = n; }

/* Test: every match of the card that allows it gets the stipulation (a
 * guest referee is someone not on the card) */
static void force_stip(void)
{
    uint8_t st = (uint8_t)(test_stip - 1), on_card[U_MAX_WRESTLERS] = {0};
    int i, k;
    if (!test_stip || st >= U_STIP_COUNT) return;
    for (i = 0; i < SHOW.count; i++)
        for (k = 0; k < 6; k++)
            if (SHOW.m[i].side[k / 2][k % 2] != U_NONE) on_card[SHOW.m[i].side[k / 2][k % 2]] = 1;
    for (i = 0; i < SHOW.count; i++) {
        UMatch *m = &SHOW.m[i];
        if (!u_stip_supported(m->format, st)) continue;
        if (st == U_STIP_REFEREE) {
            for (k = 0; k < U.n_wrestlers; k++)
                if (!on_card[k] && U.w[k].active && !U.w[k].injured_weeks) break;
            if (k == U.n_wrestlers) continue;
            on_card[k] = 1;
            m->referee = (uint8_t)k;
        }
        m->stip = st;
    }
}

/* Wrestlers the save hasn't unlocked yet (hidden ones) are left out of
 * booking until it has; Royal Rumble fill-ins come in through the entry
 * list and need no unlocking. */
static void refresh_unlocks(void)
{
    int i;
    for (i = 0; i < U.n_wrestlers; i++) {
        int can;
        if (U.w[i].role != U_ROLE_REGULAR) continue;
        can = game_pickable(U.w[i].game_slot);
        if (!can && U.w[i].active) u_set_active(&U, (uint8_t)i, 0);
        else if (can && !U.w[i].active) u_set_active(&U, (uint8_t)i, 1);
    }
}

/* The first four Rumble entrants are picked on Superstar Select: a fill-in
 * there swaps places with the next regular */
static void rumble_pickable_first(UMatch *m)
{
    int i, j;
    for (i = 0; i < 4 && i < m->n_entrants; i++) {
        if (U.w[m->entrant[i]].role == U_ROLE_REGULAR) continue;
        for (j = 4; j < m->n_entrants; j++)
            if (U.w[m->entrant[j]].role == U_ROLE_REGULAR) {
                uint8_t t = m->entrant[i];
                m->entrant[i] = m->entrant[j];
                m->entrant[j] = t;
                break;
            }
    }
}

static void save_now(void)
{
    unsigned i, n = SAVE_HEAD + U_SAVE_SIZE;
    put32(save_buf, SAVE_MAGIC);
    put32(save_buf + 4, U_SAVE_VERSION);
    put32(save_buf + 8, sizeof(UShow));
    save_buf[12] = show_ready;
    save_buf[13] = cursor;
    save_buf[14] = top;
    save_buf[15] = 0;
    if (u_save(&U, save_buf + SAVE_HEAD, U_SAVE_SIZE) != U_SAVE_SIZE) { save_state = SAVE_FAILED; return; }
    if (show_ready)
        for (i = 0; i < sizeof(UShow); i++) save_buf[n++] = ((const uint8_t *)&SHOW)[i];
    save_state = usave_store(save_buf, (int)n) == 0 ? SAVE_SAVED : SAVE_FAILED;
    dirty = 0;
}

/* The saved universe (and its card), if there is one of this version */
static int load_saved(void)
{
    int n = usave_load(save_buf, (int)sizeof save_buf);
    unsigned i;
    if (n < SAVE_HEAD + U_SAVE_SIZE || get32(save_buf) != SAVE_MAGIC) return 0;
    if (get32(save_buf + 4) != U_SAVE_VERSION ||
        u_load(&U, save_buf + SAVE_HEAD, U_SAVE_SIZE) != 0) {
        save_state = SAVE_OLD;                    /* (an earlier version's) */
        return 0;
    }
    show_ready = 0;
    if (save_buf[12] && get32(save_buf + 8) == sizeof(UShow) &&
        n >= (int)(SAVE_HEAD + U_SAVE_SIZE + sizeof(UShow))) {
        for (i = 0; i < sizeof(UShow); i++)
            ((uint8_t *)&SHOW)[i] = save_buf[SAVE_HEAD + U_SAVE_SIZE + i];
        show_ready = 1;
        cursor = save_buf[13] < SHOW.count ? save_buf[13] : 0;
        top = save_buf[14];
    }
    save_state = SAVE_LOADED;
    return 1;
}

static void new_universe(uint32_t seed)
{
    roster_universe(&U, seed | 1);
    champions_from_game();
    show_ready = 0;
    dirty = 1;
}

void us_enter(uint32_t seed)
{
    int i;
    if (!started) {
        if (!load_saved()) new_universe(seed);
        started = 1;
    }
    if (!show_ready) refresh_unlocks();
    if (!show_ready) {
        u_book_show(&U, &SHOW);
        force_stip();
        for (i = 0; i < SHOW.count; i++)          /* test: short Rumbles */
            if (SHOW.m[i].format == U_FMT_RUMBLE && test_rumble >= 4 &&
                test_rumble < SHOW.m[i].n_entrants)
                SHOW.m[i].n_entrants = (uint8_t)(test_rumble & ~3u);
        show_ready = 1;
        cursor = 0;
        top = 0;
        dirty = 1;
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
        if (m->kotr == U_KOTR_MATCHES && m->done && m->winner < 2 && n_headlines < MAX_LINES) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "KING OF THE RING: ");
            s_add(&s, side(m, m->winner));
        }
        if (m->format == U_FMT_RUMBLE && m->done && m->winner < m->n_entrants &&
            n_headlines < MAX_LINES) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "ROYAL RUMBLE WINNER: ");
            s_add(&s, side(m, m->winner));
            if (U.rumble_winner == m->entrant[m->winner] && n_headlines < MAX_LINES) {
                s_init(&s, headline[n_headlines++], LINE_LEN);
                s_add(&s, "Title shot at WrestleMania");
            }
        }
        if (m->rivalry_started && n_headlines < MAX_LINES) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "Rivalry: ");
            s_add(&s, side(m, 0));
            s_add(&s, " vs ");
            s_add(&s, side(m, 1));
        }
        if (m->run_in != U_NONE && n_headlines < MAX_LINES) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "Run-in: ");
            s_add(&s, U.w[m->run_in].name);
            s_add(&s, " helps ");
            s_add(&s, U.w[m->run_in_for].name);
        }
        if (m->injured != U_NONE && n_headlines < MAX_LINES) {
            s_init(&s, headline[n_headlines++], LINE_LEN);
            s_add(&s, "Injured: ");
            s_add(&s, U.w[m->injured].name);
            s_add(&s, ", out ");
            s_num(&s, m->injury_weeks);
            s_add(&s, m->injury_weeks == 1 ? " week" : " weeks");
        }
    }
    if (SHOW.team_formed != U_NONE && n_headlines < MAX_LINES) {
        s_init(&s, headline[n_headlines++], LINE_LEN);
        s_add(&s, "NEW TAG TEAM: ");
        s_add(&s, U.tm[SHOW.team_formed].name);
    }
    if (SHOW.team_split != U_NONE && n_headlines < MAX_LINES) {
        s_init(&s, headline[n_headlines++], LINE_LEN);
        s_add(&s, U.tm[SHOW.team_split].name);
        s_add(&s, " have split up");
    }
    if (!n_headlines) {
        s_init(&s, headline[n_headlines++], LINE_LEN);
        s_add(&s, "No title changes this week");
    }
    show_ready = 0;
    view = 1;
    dirty = 1;
}

/* Which game slot holds which wrestler of the match: Superstar Select fills
 * slots in pick order. Tag: 0-1 vs 2-3. Handicap: the pair (slots 0-1)
 * vs the lone wrestler (slot 2). Triple Threat: one per slot. With a guest
 * referee, the referee is picked first and the wrestlers follow. */
static const uint8_t SLOT_SIDE[U_FMT_COUNT][4] = {
    {0, 1, 0, 0}, {0, 0, 1, 1}, {0, 1, 2, 0}, {1, 1, 0, 0}};
static const uint8_t SLOT_K[U_FMT_COUNT][4] = {
    {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 1, 0, 0}};
static const uint8_t FMT_SLOTS[U_FMT_COUNT] = {2, 4, 3, 3};

/* Game modes the Universe item opens (byte 19 of a menu item), and their
 * MATCH tab items: Exhibition 0 Single, 1 Tag, 2 Triple Threat, 3 Handicap,
 * 4 Cage (1 on 1); Ladder / Ironman 0-3 the same formats; Guest Referee
 * 0 Single, 1 Triple Threat, 2 Handicap. */
#define MODE_EXHIBITION 0x02
#define MODE_RUMBLE     0x04            /* MATCH ("ORDER") item 0: Random */
#define MODE_REFEREE    0x0D
#define MODE_LADDER     0x0E
#define MODE_IRONMAN    0x0F
#define EX_CAGE         4
static const uint8_t REFEREE_ITEM[U_FMT_COUNT] = {0, 0, 1, 2};
/* PLAYER tab value (position in the format's list, greyed entries
 * included), the same in Exhibition, Ladder and Ironman. Guest Referee has
 * one choice with one controller, "REF. CPU vs CPU": the player referees. */
static const uint8_t PLAYER_1P[U_FMT_COUNT]      = {1, 5, 3, 3};
static const uint8_t PLAYER_WATCH[U_FMT_COUNT]   = {3, 7, 6, 5};
static const uint8_t PLAYER_REFEREE[U_FMT_COUNT] = {3, 0, 6, 5};

/* Royal Rumble: PLAYER 3 = "1P vs CPU vs CPU vs CPU" (the player is the
 * first entrant), 7 = Watch; the game's own Rumble rules (over the top). */
#define RUMBLE_PLAYER_1P    3
#define RUMBLE_PLAYER_WATCH 7
static const uint8_t RUMBLE_RULES[US_RULES] = {0x0D, 5, 2, 0, 0, 1, 0, 1, 0, 0, 0, 1};

/* Every match type Universe books can be played (notes/universe-plan.md:
 * supported combinations); Guest Referee only as the referee (no Watch). */
static int playable(const UMatch *m, int watch)
{
    if (!u_stip_supported(m->format, m->stip) || !u_match_ready(m)) return 0;
    return !(watch && m->stip == U_STIP_REFEREE);
}

/* The RULES tab row (live at 0x800931D8 while the menus are up, committed
 * when the match starts): 0 rules slot (match type), 1 time limit (0 5 min,
 * 1 10, 2 15, 3 30, 4 60, 5 none), 2 count out (0 10 counts, 1 20 counts,
 * 2 hardcore, 3 none), 3 pin, 4 submission, 5 TKO, 6 rope break, 7 DQ,
 * 8 bloodshed, 9 interference (0 yes / 1 no), 10 tag help time (0 5 s,
 * 1 10 s, ...). Universe rules: count-outs (10) and DQs on, except No DQ
 * matches (neither) and Cage / Ladder (the game has none there); Ironman
 * matches last 30 minutes; otherwise the game's own defaults. */
static void match_rules(const UMatch *m, uint8_t *r)
{
    int i, off = m->stip == U_STIP_NODQ || m->stip == U_STIP_CAGE || m->stip == U_STIP_LADDER;
    for (i = 0; i < US_RULES; i++) r[i] = 0;
    switch (m->stip) {
    case U_STIP_CAGE:    r[0] = 4; break;
    case U_STIP_LADDER:  r[0] = 5; break;
    case U_STIP_REFEREE: r[0] = (uint8_t)(6 + REFEREE_ITEM[m->format]); break;
    case U_STIP_IRONMAN: r[0] = (uint8_t)(9 + m->format); break;
    default:             r[0] = m->format; break;
    }
    r[1] = m->stip == U_STIP_IRONMAN ? 3 : 5;
    r[2] = off ? 3 : 0;
    r[5] = 1;                                    /* TKO off */
    r[7] = off ? 1 : 0;
    r[10] = m->stip == U_STIP_IRONMAN && m->format == U_FMT_TAG;  /* game default */
}

/* Rankings pages: men, women, tag teams, one per belt, injuries, the
 * Universe (its save, New Universe) */
static int rk_pages(void) { return 3 + U.n_titles + 2; }
static int rk_universe_page(void) { return rk_page == rk_pages() - 1; }
static int rk_lines(void);

static uint32_t count_register(void)
{
    uint32_t c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}

int us_input(uint16_t pressed)
{
    if (dirty) save_now();                /* (a result or a new show: saved now) */
    if (view == 2) {                      /* rankings */
        int n = rk_lines();
        if (rk_universe_page() && (pressed & B_START)) {
            if (!confirm_new) {
                confirm_new = 1;              /* START again to confirm */
                return 1;
            }
            new_universe(count_register());   /* a new universe, saved at once */
            confirm_new = 0;
            us_enter(0);
            save_now();
            return 1;
        }
        if (pressed) confirm_new = 0;
        if (pressed & B_B) view = rk_back;
        if (pressed & (B_L | B_R)) {
            rk_page = (uint8_t)((rk_page + ((pressed & B_R) ? 1 : rk_pages() - 1)) % rk_pages());
            rk_top = 0;
        }
        if ((pressed & B_DUP) && rk_top) rk_top--;
        if ((pressed & B_DDOWN) && rk_top + RK_ROWS < n) rk_top++;
        return 1;
    }
    if (pressed & B_L) {
        rk_back = view;
        view = 2;
        rk_top = 0;
        return 1;
    }
    if (pressed & B_B) {                  /* leaving: whatever is new is saved */
        save_now();
        return 0;
    }
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
        playable(&SHOW.m[cursor], !(pressed & B_R))) {
        pending = cursor;
        pending_watch = (pressed & B_R) ? 0 : 1;
        if (SHOW.m[cursor].format == U_FMT_RUMBLE) rumble_pickable_first(&SHOW.m[cursor]);
        return 2;                         /* resident launches the real match */
    }
    if (pressed & B_START) {
        u_sim_show(&U, &SHOW);            /* (tournament rounds in order) */
    } else if ((pressed & B_A) && !SHOW.m[cursor].done) {
        u_sim_match(&U, &SHOW.m[cursor]);
        u_show_update(&SHOW);             /* a winner moves on in the bracket */
    }
    return 1;
}

int us_pending_watch(void) { return pending_watch; }

/* Universe titles are in No Mercy's belt order: 0 WWF, 1 Tag, 2 IC,
 * 3 European, 4 Hardcore, 5 Light Heavyweight, 6 Women's. */
#define GAME_BELTS 7

/* No Mercy wrestler number / attire of a Universe wrestler */
static uint8_t gnum(uint8_t id) { return id < U.n_wrestlers ? U.w[id].game_slot : 0; }
static uint8_t gatt(uint8_t id) { return id < U.n_wrestlers ? U.w[id].game_attire : 0; }

/* The Royal Rumble: the player confirms (or changes) the first four
 * entrants on Superstar Select; the director writes the rest of the entry
 * order into the game. */
static void rumble_setup(const UMatch *m, USetup *st)
{
    int i;
    st->mode = MODE_RUMBLE;
    st->match_item = 0;
    st->player = pending_watch ? RUMBLE_PLAYER_WATCH : RUMBLE_PLAYER_1P;
    st->n_slots = 4;
    st->tag = 0;
    st->preset_each = 1;
    for (i = 0; i < 4; i++) {
        st->wrestler[i] = gnum(m->entrant[i]);
        st->attire[i] = gatt(m->entrant[i]);
        st->human[i] = (uint8_t)(i == 0 && !pending_watch);
    }
    for (i = 0; i < US_RULES; i++) st->rules[i] = RUMBLE_RULES[i];
    st->rumble_n = m->n_entrants;
    for (i = 0; i < m->n_entrants && i < US_RUMBLE_MAX; i++)
        st->rumble[i] = gnum(m->entrant[i]);
    st->belt = -1;
    st->champion[0] = st->champion[1] = 0;
}

/* A wrestler's hottest rivals and his companion, for the game's own
 * run-ins (plan step 7). Two characters sharing a game number share its
 * data: the first one in the match writes it. */
static void story(const UMatch *m, uint8_t id, int slot, USetup *st)
{
    uint8_t riv[3], c;
    int k, n;
    for (k = 0; k < slot; k++)
        if ((st->rel_set & (1u << k)) && st->wrestler[k] == gnum(id)) return;
    n = u_top_rivals(&U, id, riv, 3);
    for (k = 0; k < 3; k++) st->rel[slot][k] = k < n ? gnum(riv[k]) : 0;
    c = u_companion(&U, m, id);
    st->rel[slot][3] = c != U_NONE ? gnum(c) : 0;
    st->rel_set |= (uint8_t)(1u << slot);
}

void us_pending_setup(USetup *st)
{
    const UMatch *m = &SHOW.m[pending];
    int i, ref = m->stip == U_STIP_REFEREE, n = FMT_SLOTS[m->format];
    st->preset_each = 0;
    st->rumble_n = 0;
    st->rel_set = 0;
    if (m->format == U_FMT_RUMBLE) {
        rumble_setup(m, st);
        return;
    }
    switch (m->stip) {
    case U_STIP_LADDER:  st->mode = MODE_LADDER;  st->match_item = m->format; break;
    case U_STIP_IRONMAN: st->mode = MODE_IRONMAN; st->match_item = m->format; break;
    case U_STIP_REFEREE: st->mode = MODE_REFEREE; st->match_item = REFEREE_ITEM[m->format]; break;
    case U_STIP_CAGE:    st->mode = MODE_EXHIBITION; st->match_item = EX_CAGE; break;
    default:             st->mode = MODE_EXHIBITION; st->match_item = m->format; break;
    }
    st->player = ref ? PLAYER_REFEREE[m->format]
               : (pending_watch ? PLAYER_WATCH : PLAYER_1P)[m->format];
    st->n_slots = (uint8_t)(n + ref);
    st->tag = m->format == U_FMT_TAG;
    for (i = 0; i < 4; i++) {
        st->wrestler[i] = 0;
        st->attire[i] = 0;
        st->human[i] = 0;
    }
    if (ref) {
        st->wrestler[0] = gnum(m->referee);
        st->attire[0] = gatt(m->referee);
    }
    for (i = 0; i < n; i++) {
        uint8_t id = m->side[SLOT_SIDE[m->format][i]][SLOT_K[m->format][i]];
        st->wrestler[ref + i] = gnum(id);
        st->attire[ref + i] = gatt(id);
        story(m, id, ref + i, st);
    }
    /* Play: the player's wrestler (and the CPU partner in Tag / Handicap,
     * as the game marks it), or the guest referee; Watch: all CPU */
    if (ref) st->human[0] = 1;
    else if (!pending_watch) {
        st->human[0] = 1;
        st->human[1] = m->format == U_FMT_TAG || m->format == U_FMT_HANDICAP;
    }
    match_rules(m, st->rules);
    st->belt = -1;
    st->champion[0] = st->champion[1] = 0;
    if (m->title != U_NONE && m->title < GAME_BELTS) {
        uint8_t h = u_title_holder(&U, m->title);
        st->belt = m->title;
        if (h != U_NONE && U.t[m->title].kind == U_TITLE_TAG) {
            st->champion[0] = gnum(U.tm[h].a);
            st->champion[1] = gnum(U.tm[h].b);
        } else if (h != U_NONE) {
            st->champion[0] = gnum(h);
        }
    }
}

/* Universe wrestler for a No Mercy wrestler number (U_NONE if not in it) */
static uint8_t universe_wrestler(uint8_t game_n, uint8_t attire)
{
    int i;
    if (!game_n) return U_NONE;
    for (i = 0; i < U.n_wrestlers; i++)
        if (U.w[i].game_slot == game_n && U.w[i].game_attire == attire) return (uint8_t)i;
    for (i = 0; i < U.n_wrestlers; i++)       /* (an attire Universe doesn't use) */
        if (U.w[i].game_slot == game_n && U.w[i].game_attire == 0) return (uint8_t)i;
    return U_NONE;
}

static int in_match(const UMatch *m, uint8_t id)
{
    int k;
    if (m->format == U_FMT_RUMBLE) {
        for (k = 0; k < m->n_entrants; k++)
            if (m->entrant[k] == id) return 1;
        return 0;
    }
    for (k = 0; k < 6; k++)
        if (m->side[k / 2][k % 2] == id) return 1;
    return 0;
}

/* Result of a match played/watched in No Mercy: the game's result byte
 * per slot (4 = won), who actually wrestled there (the player may change the
 * picks on Superstar Select; outside tag matches a wrestler from the
 * universe replaces the card's one, and the guest referee likewise), the
 * finish, and Ironman falls. A rematch calls this again and replaces the
 * result. */
void us_match_result(const UGameResult *r)
{
    UMatch *m;
    int i, n, first, win = -1;
    uint8_t method;
    if (pending == U_NONE) return;
    dirty = 1;                            /* (saved when the Universe screen is back) */
    m = &SHOW.m[pending];
    if (m->format == U_FMT_RUMBLE) {
        /* the winner by wrestler number; someone the player picked instead
         * of a card entrant takes that entrant's place */
        uint8_t w = universe_wrestler(r->rumble_winner, 0);
        for (i = 0; i < 4 && i < m->n_entrants; i++) {
            uint8_t p = universe_wrestler(r->game_n[i], r->game_attire[i]);
            if (p != U_NONE && p != m->entrant[i] && !in_match(m, p)) m->entrant[i] = p;
        }
        for (i = 0; i < m->n_entrants; i++)
            if (m->entrant[i] == w) {
                m->done = 0;
                u_set_result(m, (uint8_t)i, U_WIN_RUMBLE);
                break;
            }
        return;
    }
    first = m->stip == U_STIP_REFEREE;           /* slot 0 is the referee */
    n = FMT_SLOTS[m->format];
    for (i = 0; i < n; i++)
        if (r->result[first + i] == 4) { win = SLOT_SIDE[m->format][i]; break; }
    if (m->format != U_FMT_TAG)
        for (i = 0; i < n; i++) {
            uint8_t w = universe_wrestler(r->game_n[first + i], r->game_attire[first + i]);
            uint8_t *slot = &m->side[SLOT_SIDE[m->format][i]][SLOT_K[m->format][i]];
            if (w != U_NONE && w != *slot && !in_match(m, w) && w != m->referee) *slot = w;
        }
    if (first) {
        uint8_t w = universe_wrestler(r->game_n[0], r->game_attire[0]);
        if (w != U_NONE && !in_match(m, w)) m->referee = w;
    }
    m->done = 0;
    if (m->stip == U_STIP_IRONMAN) {
        uint8_t falls[3] = {0, 0, 0};
        for (i = 0; i < n; i++) {
            uint8_t *f = &falls[SLOT_SIDE[m->format][i]];
            *f = (uint8_t)(*f + r->falls[first + i]);
        }
        u_set_ironman_result(m, falls);
        /* the game's verdict decides (the falls are for the card) */
        m->winner = win < 0 ? U_NONE : (uint8_t)win;
        m->method = win < 0 ? U_WIN_DRAW : U_WIN_FALLS;
        return;
    }
    if (win < 0) {
        if (r->method == U_WIN_DRAW) u_set_draw(m);
        return;
    }
    method = r->method;
    if (!u_method_allowed(m->stip, method) || method == U_WIN_DRAW || method == U_WIN_FALLS)
        method = m->stip == U_STIP_LADDER || m->stip == U_STIP_CAGE ? U_WIN_SPECIAL
                                                                     : U_WIN_PINFALL;
    u_set_result(m, (uint8_t)win, method);
    u_show_update(&SHOW);                 /* a tournament winner moves on */
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

/* ---- Rankings screen ---- */

#define RK_MAX   64
static char rk_text[RK_MAX][LINE_LEN];

static const char *rk_title(void)
{
    if (rk_page == 0) return "Men";
    if (rk_page == 1) return "Women";
    if (rk_page == 2) return "Tag Teams";
    if (rk_page < 3 + U.n_titles) return U.t[rk_page - 3].name;
    if (rk_page == 3 + U.n_titles) return "Injuries";
    return "Universe";
}

/* the singles title a wrestler holds, or 0 */
static const char *title_held(uint8_t id)
{
    int t;
    for (t = 0; t < U.n_titles; t++)
        if (U.t[t].kind == U_TITLE_SINGLES && u_title_holder(&U, (uint8_t)t) == id)
            return U.t[t].name;
    return 0;
}

static void rk_add_ranked(Str *s, int pos, const char *name)
{
    s_num(s, (unsigned)pos);
    s_add(s, ". ");
    s_add(s, name);
}

/* fills rk_text for the current page; returns the number of lines */
static int rk_lines(void)
{
    uint8_t ids[U_MAX_WRESTLERS];
    Str s;
    int n = 0, i, k;
    if (rk_page < 2) {                                    /* men, women */
        k = u_division_rankings(&U, rk_page ? U_DIV_WOMEN : U_DIV_MEN, ids, U_MAX_WRESTLERS);
        for (i = 0; i < k && n < RK_MAX; i++) {
            const UWrestler *w = &U.w[ids[i]];
            s_init(&s, rk_text[n++], LINE_LEN);
            rk_add_ranked(&s, i + 1, w->name);
            s_add(&s, " - ");
            s_num(&s, w->points);
            if (w->injured_weeks) s_add(&s, " - injured");
        }
    } else if (rk_page == 2) {                            /* tag teams */
        k = u_team_rankings(&U, U_NONE, ids, U_MAX_TEAMS);
        for (i = 0; i < k && n < RK_MAX; i++) {
            s_init(&s, rk_text[n++], LINE_LEN);
            rk_add_ranked(&s, i + 1, U.tm[ids[i]].name);
            s_add(&s, " - ");
            s_num(&s, U.tm[ids[i]].points);
        }
    } else if (rk_page < 3 + U.n_titles) {                /* a belt */
        uint8_t t = (uint8_t)(rk_page - 3), h = u_title_holder(&U, t);
        int tag = U.t[t].kind == U_TITLE_TAG;
        s_init(&s, rk_text[n++], LINE_LEN);
        if (h == U_NONE) {
            s_add(&s, "Vacant");
        } else {
            s_add(&s, "Champion: ");
            s_add(&s, tag ? U.tm[h].name : U.w[h].name);
        }
        s_init(&s, rk_text[n++], LINE_LEN);
        k = u_contenders(&U, t, ids, U_CONTENDERS);
        for (i = 0; i < k; i++) {
            const char *held = tag ? 0 : title_held(ids[i]);
            s_init(&s, rk_text[n++], LINE_LEN);
            rk_add_ranked(&s, i + 1, tag ? U.tm[ids[i]].name : U.w[ids[i]].name);
            if (held) { s_add(&s, " - "); s_add(&s, held); }
        }
        if (!k) { s_init(&s, rk_text[n++], LINE_LEN); s_add(&s, "No contenders"); }
    } else if (rk_page == 3 + U.n_titles) {               /* injuries */
        k = u_injuries(&U, ids, U_MAX_WRESTLERS);
        for (i = 0; i < k && n < RK_MAX; i++) {
            const UWrestler *w = &U.w[ids[i]];
            s_init(&s, rk_text[n++], LINE_LEN);
            s_add(&s, w->name);
            s_add(&s, " - out ");
            s_num(&s, w->injured_weeks);
            s_add(&s, w->injured_weeks == 1 ? " week" : " weeks");
        }
        if (!k) { s_init(&s, rk_text[n++], LINE_LEN); s_add(&s, "No injuries"); }
    } else {                                              /* the Universe */
        static const char *const STATE[] = {
            "Not saved yet", "Saved on the cartridge", "Loaded from the cartridge",
            "Could not save on the cartridge", "Older version's save: new Universe"};
        s_init(&s, rk_text[n++], LINE_LEN);
        s_add(&s, "Week ");
        s_num(&s, U.week);
        s_init(&s, rk_text[n++], LINE_LEN);
        s_add(&s, STATE[save_state]);
        s_init(&s, rk_text[n++], LINE_LEN);
        s_init(&s, rk_text[n++], LINE_LEN);
        s_add(&s, confirm_new ? "Press $e again to start over" : "$e New Universe");
    }
    return n;
}

static void draw_rankings(void *holder)
{
    static char head[LINE_LEN];
    Str s;
    int n = rk_lines(), i, y;
    if (rk_top + RK_ROWS > n) rk_top = (uint8_t)(n > RK_ROWS ? n - RK_ROWS : 0);
    text(holder, "RANKINGS", 22);
    s_init(&s, head, LINE_LEN);
    s_add(&s, "$2  ");
    s_add(&s, rk_title());
    s_add(&s, "  $3");
    text(holder, head, 38);
    for (i = rk_top, y = 58; i < n && i < rk_top + RK_ROWS; i++, y += 14) {
        int len = 0;
        while (rk_text[i][len]) len++;
        text_scaled(holder, rk_text[i], y, len > 30 ? 0xB0 : 0xD8);
    }
    small(holder, n > RK_ROWS ? "$2$3Pages  $a$bScroll  $1Back" : "$2$3Pages  $1Back", 200);
}

void us_draw(void *holder)
{
    static char line[12][LINE_LEN];
    Str s;
    int i, y;

    if (cover) return;                    /* drawn over the whole frame instead */
    panel(holder, 250, 8, 476, 228);
    if (view == 2) {
        draw_rankings(holder);
        return;
    }
    text(holder, "UNIVERSE", 22);
    s_init(&s, line[0], LINE_LEN);
    s_add(&s, "Week ");
    s_num(&s, view == 1 ? SHOW.week : U.week);
    s_add(&s, "  ");
    if (view == 0 && SHOW.kind == U_SHOW_PPV) s_add(&s, "PPV: ");
    s_add(&s, view == 1 ? "Results" : u_show_name(&U));
    text(holder, line[0], 38);

    if (view == 1) {
        for (i = 0; i < n_headlines; i++) {     /* long lines smaller, inside the panel */
            int len = 0;
            while (headline[i][len]) len++;
            text_scaled(holder, headline[i], 62 + i * 15, len > 34 ? 0xB0 : len > 30 ? 0xC4 : 0xD8);
        }
        text(holder, "$0Next Show   $1Back", 198);
        small(holder, "$2Rankings", 214);
        return;
    }

    /* eight lines at a time, scrolled to keep the cursor in view (King of
     * the Ring cards are longer) */
    if (cursor < top) top = cursor;
    if (cursor >= top + 8) top = (uint8_t)(cursor - 7);
    if (top + 8 > SHOW.count) top = SHOW.count > 8 ? (uint8_t)(SHOW.count - 8) : 0;
    y = 58;
    for (i = top; i < SHOW.count && i < top + 8; i++, y += 14) {
        const UMatch *m = &SHOW.m[i];
        s_init(&s, line[1 + i - top], LINE_LEN);
        if (i == cursor) s_add(&s, "$d ");
        {
            char t[LINE_LEN];
            u_match_text(&U, m, 1, t, sizeof t);
            s_add(&s, t);
        }
        /* long lines (handicap, triple threat, tag names) are drawn smaller
         * so they stay inside the panel */
        text_scaled(holder, line[1 + i - top], y, s.n > 34 ? 0xA8 : s.n > 28 ? 0xC0 : 0xD8);
    }

    /* details of the selected match: its type and title, then the guest
     * referee or how it ended */
    {
        const UMatch *m = &SHOW.m[cursor];
        s_init(&s, line[9], LINE_LEN);
        if (cursor == SHOW.count - 1) s_add(&s, "Main Event  ");
        if (m->kotr) { s_add(&s, "King of the Ring "); s_add(&s, u_round_name(m)); s_add(&s, "  "); }
        if (m->format != U_FMT_SINGLE) { s_add(&s, u_format_name(m->format)); s_add(&s, " "); }
        if (m->stip != U_STIP_NORMAL) { s_add(&s, u_stip_name(m->stip)); s_add(&s, " "); }
        if (m->format != U_FMT_SINGLE || m->stip != U_STIP_NORMAL) s_add(&s, "Match  ");
        if (m->title != U_NONE && (m->done || !m->why)) s_add(&s, U.t[m->title].name);
        text_scaled(holder, line[9], 172, s.n > 34 ? 0xA8 : s.n > 28 ? 0xC0 : 0xD8);
        s_init(&s, line[10], LINE_LEN);
        if (!m->done && m->why && m->format != U_FMT_RUMBLE && u_match_ready(m) &&
            m->referee == U_NONE) {
            char why[LINE_LEN];               /* why it's on the card */
            u_why_text(&U, m, why, sizeof why);
            s_add(&s, why);
        } else if (m->format == U_FMT_RUMBLE) {
            if (m->done && m->winner < m->n_entrants) {
                s_add(&s, "Winner came in at number ");   /* ('#' is a control code) */
                s_num(&s, (unsigned)m->winner + 1u);
            } else {
                s_add(&s, "Prize: title shot at WrestleMania");
            }
        } else if (!u_match_ready(m)) {
            s_add(&s, "Waiting for the earlier rounds");
        } else if (m->done && m->method == U_WIN_DRAW) {
            s_add(&s, "Draw");
        } else if (m->done) {
            s_add(&s, "Won by ");
            s_add(&s, u_method_name(m->method, m->stip));
        }
        if (m->done && m->stip == U_STIP_IRONMAN) {
            int k;
            s_add(&s, "  Falls ");
            for (k = 0; k < u_match_sides(m); k++) {
                if (k) s_add(&s, "-");
                s_num(&s, m->falls[k]);
            }
        }
        if (m->referee != U_NONE) {
            if (s.n) s_add(&s, "   ");
            s_add(&s, "Referee: ");
            s_add(&s, U.w[m->referee].name);
        }
        if (s.n) text_scaled(holder, line[10], 185, s.n > 36 ? 0xB0 : s.n > 30 ? 0xC4 : 0xD8);
    }
    small(holder, "$2Rankings", 214);
    if (card_done())
        text(holder, "$0Results   $1Back", 200);
    else if (SHOW.m[cursor].done || !u_match_ready(&SHOW.m[cursor]))
        small(holder, "$eSim All  $1Back", 200);
    else if (playable(&SHOW.m[cursor], 1))
        small(holder, "$3Play $4Watch $0Sim $eAll $1Back", 200);
    else if (playable(&SHOW.m[cursor], 0))
        small(holder, "$3Referee $0Sim $eAll $1Back", 200);
    else
        small(holder, "$0Sim  $eSim All  $1Back", 200);
}
