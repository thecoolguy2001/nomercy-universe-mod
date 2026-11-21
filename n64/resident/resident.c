/*
 * Resident Universe module.
 *
 * Replaces libultra's osContGetReadData (0x800346A8 in No Mercy US 1.0),
 * which the game calls on every controller update (from 0x800058A8). We do
 * exactly what the original did, then run the match director, which can
 * press buttons on controller 1 to drive the game's own Exhibition menus.
 *
 * Stage 2 (current): CMD_WATCH / CMD_PLAY started from the Exhibition MATCH
 * tab with a fixed menu route; result read from the game at the bell.
 */
#include "resident.h"
#include "uscreen.h"

#define PIF_RAM   ((const volatile uint8_t *)0x800A0200u) /* __osContPifRam */
#define MAX_CONT  (*(const volatile uint8_t *)0x800A2614u) /* __osMaxControllers */
#define MAILBOX   ((volatile Mailbox *)MAILBOX_ADDR)

#define RAM8(a)   (*(volatile uint8_t *)(a))
#define RAM32(a)  (*(volatile uint32_t *)(a))

/* Game state found with the debugger (notes/re-findings.md) */
#define SS_CURSOR_A    0x8015B815u  /* Superstar Select: P1 grid cursor */
#define SS_CURSOR_B    0x800AAAB5u  /* Superstar Select: shared grid cursor */
#define SS_CURSOR_CPU  0x8015B94Du  /* Superstar Select: second pick cursor */
#define SS_CURSOR_CPU2 0x80045849u
#define CLOCK_MIN      0x8015AB18u
#define CLOCK_SEC      0x8015AB1Cu
#define RESULT_SIDE1   0x8015AF45u  /* 4 = won */
#define RESULT_SIDE2   0x8015B29Du

/* Buttons (OSContPad.button) */
#define B_A      0x8000
#define B_START  0x1000
#define B_DDOWN  0x0400

/* Single Play menu record: +0x08 menu-table entry pointer, +0x0C u16
 * cursor, +0x18 u32 wheel page (0x00 Single Play, 0x01 Commissioner,
 * 0x10 Multi Play). Universe is the third Single Play item (cursor 2). */
#define SP_RECORD       0x80159C00u
#define SP_TABLE_ENTRY  0x8014F4E0u
#define PAGE_SINGLE     0x00
#define B_B      0x4000

#define COUNTS_PER_FRAME 781250u

/* Game music: play_song(song, mode, volume) at 0x80017CF4. Menus call it
 * with mode 4, volume 0x50. Song 0x29 = The Undertaker's theme. */
typedef void (*play_song_fn)(uint32_t song, uint32_t mode, uint32_t volume);
#define PLAY_SONG ((play_song_fn)0x80017CF4u)
#define SONG_UNDERTAKER 0x29     /* CP0 Count runs at 46.875 MHz; 60 fields/s */

static uint32_t cp0_count(void)
{
    uint32_t c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}

/* ---- frame clock (VI frames, from CP0 Count deltas; Count wraps ~91 s) ---- */
static uint32_t last_cnt, frac, frames;

static void tick(void)
{
    uint32_t now = cp0_count();
    frac += now - last_cnt;          /* unsigned subtraction handles wraparound */
    last_cnt = now;
    while (frac >= COUNTS_PER_FRAME) {
        frac -= COUNTS_PER_FRAME;
        frames++;
    }
}

/* ---- Match director: launches a real No Mercy match for a Universe card
 * match and records the result. Route: the Universe item's "mode opened"
 * byte is switched to Exhibition and A is pressed on it; then MATCH ->
 * PLAYER -> ARENA -> RULES -> START. On Superstar Select both picks are
 * confirmed straight away, and the game's own match setup is then written
 * directly: wrestlers, human/CPU control and the title belt on the line.
 * The result is read at the bell. */
enum { D_IDLE, D_ROUTE, D_SELECT, D_INTRO, D_MATCH, D_RETURN, D_STEER, D_DONE, D_FAIL };
static uint32_t footer_frame;          /* last frame a menu footer was drawn (menus on screen) */
static void enter_universe(void);
static uint8_t universe_mode;
static int universe_highlighted(void);

#define UNIVERSE_ITEM_MODE  0x8040903Fu  /* byte 19 of the 3rd Single Play item */
#define MODE_SURVIVAL       0x03
#define MODE_EXHIBITION     0x02
#define B_R      0x0010
#define B_L      0x0020
#define B_Z      0x2000
#define B_DLEFT  0x0200
#define B_DRIGHT 0x0100

/* Exhibition settings (menu overlay data) */
#define EX_MATCH_CURSOR   0x8015A0EDu    /* MATCH tab: 0 = Single Match */
static const uint32_t EX_PLAYER[] = { 0x8015971Cu, 0x80159655u, 0x80159FB5u };
#define PLAYER_1P_VS_CPU  1               /* PLAYER tab: 1 1P vs CPU, 2 CPU vs 1P, 3 Watch */
#define PLAYER_WATCH      3
/* Match setup read by the match loader */
#define SETUP_SLOT(i)     (0x800AAAD0u + 12u * (i))  /* +0 wrestler id (u16, 0xFFxx = not picked) */
#define SETUP_LIST(i)     (0x800A5568u + 4u * (i))   /* u32 wrestler id per participant */
#define SETUP_BELT        0x800AB5B4u    /* s32 belt on the line, -1 = none */
#define CHAMPION(b)       (0x800A53E2u + 0x34u * (b)) /* wrestler number of the champion */
#define GAME_SCREEN       0x80045845u    /* screen to show: 2 Multi Play, 3 Single Play, 0x1D result, 0x2A match */
#define SCREEN_MULTI_PLAY   2
#define SCREEN_SINGLE_PLAY  3
#define WRESTLER_TABLE    0x800471C4u    /* [n] (16 bytes): pointer to the record, +0 u16 id */

typedef struct { uint16_t at; uint16_t buttons; } RouteStep;
static const RouteStep ROUTE[] = {
    {   0, B_A     },   /* select the Universe item (now opens Exhibition) */
    { 160, B_A     },   /* MATCH: Single Match */
    { 350, B_A     },   /* PLAYER (the choice is written before this press) */
    { 550, B_A     },   /* ARENA */
    { 750, B_A     },   /* RULES */
    { 950, B_START },   /* Decision -> Superstar Select */
};
#define ROUTE_END 1000

static uint8_t dstate, dwatch, droute, dpicked, seen_zero;
static uint8_t dwrestler[2], dchampion, dsaved_champion;
static int     dbelt;
static uint16_t dsteps;
static uint32_t dt0, dnext;
static uint32_t hold_until;
static uint16_t hold_buttons;

static void press(uint16_t b) { hold_buttons = b; hold_until = frames + 4; }

void text_seen(uint32_t str) { (void)str; }

static uint16_t game_id(uint8_t n)   /* wrestler number -> the id used in the setup */
{
    uint32_t rec = RAM32(WRESTLER_TABLE + 16u * n);
    return *(volatile uint16_t *)rec;
}

static void set_player_mode(void)
{
    unsigned i;
    for (i = 0; i < sizeof EX_PLAYER / sizeof EX_PLAYER[0]; i++)
        RAM8(EX_PLAYER[i]) = dwatch ? PLAYER_WATCH : PLAYER_1P_VS_CPU;
}

/* Overwrites the confirmed Superstar Select picks with the card's match. */
static void write_setup(void)
{
    int s;
    for (s = 0; s < 2; s++) {
        uint16_t id = game_id(dwrestler[s]);
        *(volatile uint16_t *)SETUP_SLOT(s) = id;
        RAM32(SETUP_LIST(s)) = id;
    }
    set_player_mode();
    RAM32(SETUP_BELT) = (uint32_t)dbelt;
    if (dbelt >= 0) RAM8(CHAMPION(dbelt)) = dchampion;
}

static void director_start(int watch)
{
    us_pending_setup(dwrestler, &dbelt, &dchampion);
    if (dbelt >= 0) dsaved_champion = RAM8(CHAMPION(dbelt));
    dwatch = (uint8_t)watch;
    droute = 0;
    dpicked = 0;
    dsteps = 0;
    seen_zero = 0;
    RAM8(UNIVERSE_ITEM_MODE) = MODE_EXHIBITION;
    dt0 = frames;
    dstate = D_ROUTE;
}

static void director_end_match(void)
{
    /* the game's own title records stay as they were */
    if (dbelt >= 0) RAM8(CHAMPION(dbelt)) = dsaved_champion;
    RAM32(SETUP_BELT) = 0xFFFFFFFFu;
}

static void director_tick(void)
{
    uint32_t t = frames - dt0;
    switch (dstate) {
    case D_ROUTE:
        if (t < 160) RAM8(EX_MATCH_CURSOR) = 0;          /* Single Match */
        if (t >= 200 && t < 350) set_player_mode();      /* Watch or 1P vs CPU */
        while (droute < sizeof ROUTE / sizeof ROUTE[0] && t >= ROUTE[droute].at)
            press(ROUTE[droute++].buttons);
        if (t >= 60) RAM8(UNIVERSE_ITEM_MODE) = MODE_SURVIVAL;  /* restore the menu item */
        if (t >= ROUTE_END) { dstate = D_SELECT; dnext = frames; dsteps = 0; }
        break;
    case D_SELECT:
        /* dpicked: 0 wait for Superstar Select (it empties both slots),
         * 1 confirm side 1, 2 confirm side 2, 3 write the real setup.
         * Whatever is highlighted is confirmed - nothing is searched for. */
        if (dpicked == 3) {
            write_setup();
            if (frames - dnext > 60) { dt0 = frames; dstate = D_INTRO; }
            break;
        }
        if (frames < dnext) break;
        if (++dsteps > 120) { dstate = D_FAIL; break; }
        if (dpicked == 0) {
            if (RAM8(SETUP_SLOT(0)) == 0xFF && RAM8(SETUP_SLOT(1)) == 0xFF) {
                dpicked = 1;
                dnext = frames + 40;          /* let the screen settle */
            } else {
                dnext = frames + 10;
            }
            break;
        }
        if (RAM8(SETUP_SLOT(dpicked - 1)) != 0xFF) {   /* that side is confirmed */
            if (++dpicked == 3) { write_setup(); dnext = frames; break; }
        }
        press(B_A);
        dnext = frames + 45;
        break;
    case D_INTRO:           /* entrances, match, replays: all hands off */
    case D_MATCH: {
        uint8_t r1 = RAM8(RESULT_SIDE1), r2 = RAM8(RESULT_SIDE2);
        if (r1 == 0 && r2 == 0) seen_zero = 1;
        if (seen_zero && (r1 == 4 || r2 == 4)) {
            us_match_result(r1 == 4 ? 0 : 1, r1 == 4 ? r2 : r1);
            director_end_match();
            dstate = D_RETURN;
            dt0 = frames;
            dnext = frames + 300;
        } else if (dstate == D_INTRO && t > 2800) {
            dstate = D_MATCH;
        }
        break;
    }
    case D_RETURN:
        /* Hands off: the player watches the replays and picks Exit (or
         * Rematch) on the result screen. Exit asks for the Multi Play page
         * of the main menu; it is changed to Single Play, where Universe
         * is, and the Universe screen is shown as soon as the menu is up. */
        if (RAM8(GAME_SCREEN) == SCREEN_MULTI_PLAY) RAM8(GAME_SCREEN) = SCREEN_SINGLE_PLAY;
        if (frames - dt0 > 60 && frames - footer_frame < 10) {
            universe_mode = 1;
            us_cover(1);
            dstate = D_STEER;
            dt0 = frames;
            dnext = frames + 2;
        }
        break;
    case D_STEER:
        /* Under the full-screen Universe panel, turn the main-menu wheel
         * from Multi Play to Single Play and put the cursor on Universe. */
        if (frames < dnext) break;
        if (frames - dt0 > 900) { us_cover(0); dstate = D_FAIL; break; }
        {
            uint32_t page = RAM32(SP_RECORD + 0x18);
            uint16_t cur = *(volatile uint16_t *)(SP_RECORD + 0x0C);
            if (universe_highlighted()) {
                us_cover(0);
                dstate = D_DONE;
                enter_universe();
                break;
            }
            if (page == 0x10) press(B_R);            /* Multi Play -> Single Play */
            else if (page == 0x01) press(B_L);       /* Commissioner -> Single Play */
            else if (page == 0x00 && cur != 2) press(B_DDOWN);
            else { dnext = frames + 2; break; }      /* wheel still turning */
            dnext = frames + 12;
        }
        break;
    default:
        break;
    }
}

/* ---- Universe Mode (in-game) ---- */
/* Menu overlay draw_text(holder, str, x, y, alpha, style, flags): 640x240
 * coordinates; the footer uses alpha 0xFF, style 0x30, flags 0x100. */
typedef void (*draw_text_fn)(void *holder, const char *s, int x, int y,
                             int alpha, int style, int flags);
#define DRAW_TEXT ((draw_text_fn)0x800DDC20u)

#define B_DUP    0x0800

/* universe_mode: 1 while the player is inside Universe (declared above) */
static uint8_t swallow;            /* block game input until buttons are released */
static uint16_t prev_buttons;
static uint8_t test_hold;

void universe_draw(void *holder)
{
    footer_frame = frames;
    if (universe_mode) us_draw(holder);
}

static void enter_universe(void)
{
    universe_mode = 1;
    us_enter(cp0_count());
}


static int universe_highlighted(void)
{
    return RAM32(SP_RECORD + 0x08) == SP_TABLE_ENTRY &&
           RAM32(SP_RECORD + 0x18) == PAGE_SINGLE &&
           *(volatile uint16_t *)(SP_RECORD + 0x0C) == 2;
}

static void universe_frame(OSContPad *pads)
{
    volatile Mailbox *mb = MAILBOX;
    uint32_t now = cp0_count();
    if (mb->magic != MAILBOX_MAGIC) {
        mb->magic = MAILBOX_MAGIC;
        mb->calls = 0;
        mb->first_count = now;
        last_cnt = now;
    }
    mb->calls++;
    mb->last_count = now;
    mb->last_buttons = pads[0].button;
    tick();

    {
        uint16_t btn = pads[0].button;
        uint16_t pressed = btn & (uint16_t)~prev_buttons;
        prev_buttons = btn;
        if (mb->test_select) {             /* test hook: let the game act on the item */
            mb->test_select = 0;
            test_hold = 4;
        }
        if (test_hold) {
            test_hold--;
            pads[0].button |= B_A;
        } else if (dstate != D_IDLE && dstate != D_DONE && dstate != D_FAIL) {
            /* the director is driving */
        } else if (!universe_mode) {
            if (swallow) {
                if (btn) pads[0].button = 0; else swallow = 0;
            } else if (universe_highlighted() && (pressed & B_A)) {
                enter_universe();           /* the game never sees this A */
                pads[0].button = 0;
            }
        } else {
            int r = us_input(pressed);
            if (r == 0) {
                universe_mode = 0;          /* back to the Single Play menu */
                swallow = 1;
            } else if (r == 2) {            /* Play / Watch the selected match */
                universe_mode = 0;
                director_start(us_pending_watch());
            }
            pads[0].button = 0;             /* the menu underneath gets no input */
        }
    }

    /* Universe Mode's music is The Undertaker's theme (only inside the mode) */
    mb->music_override = universe_mode ? SONG_UNDERTAKER : 0;

    if (mb->song_request & 0x100) {
        uint32_t song = mb->song_request & 0xFF;
        mb->song_request = 0;
        PLAY_SONG(song, 4, 0x50);
    }

    director_tick();
    mb->state = dstate;
    mb->step = (uint8_t)(dsteps | dpicked << 6);
    if (dstate == D_ROUTE || dstate == D_SELECT || dstate == D_STEER)
        pads[0].button = 0;                /* menus: only the director presses buttons */
    if (frames < hold_until) pads[0].button |= hold_buttons;
}

/* Same behaviour as the original: for each controller, copy the error
 * bits, and on success the buttons and stick, from the PIF read block. */
void hook_cont_get_read_data(OSContPad *data)
{
    int i, n = MAX_CONT;
    for (i = 0; i < n; i++) {
        const volatile uint8_t *f = PIF_RAM + 8 * i;
        uint8_t err = (uint8_t)((f[2] & 0xC0) >> 4);
        data[i].errnum = err;
        if (err == 0) {
            data[i].button = (uint16_t)(f[4] << 8 | f[5]);
            data[i].stick_x = (int8_t)f[6];
            data[i].stick_y = (int8_t)f[7];
        }
    }
    universe_frame(data);
}
