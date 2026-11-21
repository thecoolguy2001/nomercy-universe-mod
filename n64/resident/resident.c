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
 * PLAYER -> ARENA -> RULES -> START; wrestlers are picked with feedback
 * from the highlighted-wrestler pointer; the result is read at the bell. */
enum { D_IDLE, D_ROUTE, D_PICK1, D_PICK2, D_INTRO, D_MATCH, D_DONE, D_FAIL };

#define UNIVERSE_ITEM_MODE  0x8040903Fu  /* byte 19 of the 3rd Single Play item */
#define MODE_SURVIVAL       0x03
#define MODE_EXHIBITION     0x02
#define B_R      0x0010
#define B_Z      0x2000
#define B_DLEFT  0x0200
#define B_DRIGHT 0x0100

typedef struct { uint16_t at; uint16_t buttons; uint8_t watch_only; } RouteStep;
static const RouteStep ROUTE[] = {
    {   0, B_A,     0 },   /* select the Universe item (now opens Exhibition) */
    { 160, B_A,     0 },   /* MATCH: Single Match */
    { 300, B_DDOWN, 1 },   /* PLAYER: down, down = Watch */
    { 330, B_DDOWN, 1 },
    { 350, B_A,     0 },   /* PLAYER */
    { 550, B_A,     0 },   /* ARENA */
    { 750, B_A,     0 },   /* RULES */
    { 950, B_START, 0 },   /* Decision -> Superstar Select */
};
#define ROUTE_END 1150
/* Per page: go to the top-left slot (the grid clamps at its edges), then
 * snake through all 9 slots, then turn to the next page. */
#define B_DUP_ 0x0800
static const uint16_t SCAN[] = { B_DUP_, B_DUP_, B_DLEFT, B_DLEFT,
                                 B_DRIGHT, B_DRIGHT, B_DDOWN, B_DLEFT, B_DLEFT,
                                 B_DDOWN, B_DRIGHT, B_DRIGHT, B_R };
#define SCAN_LEN (sizeof SCAN / sizeof SCAN[0])

static uint8_t dstate, dwatch, droute, dscan;
void text_seen(uint32_t str);
static uint16_t dsteps;
static uint8_t seen_zero;
static const char *dname[2];
static uint32_t dt0, dnext;
static uint32_t hold_until;
static uint16_t hold_buttons;

static void press(uint16_t b) { hold_buttons = b; hold_until = frames + 4; }

/* While picking, every drawn string is compared with the wanted wrestler's
 * full name; on Superstar Select the full name is only drawn in the label
 * of the highlighted wrestler. */
static uint32_t target_seen_frame;

void text_seen(uint32_t str)
{
    const volatile char *s;
    const char *want;
    int i;
    if (dstate != D_PICK1 && dstate != D_PICK2) return;
    if ((str & 0xFF800000u) != 0x80000000u) return;    /* not a plain RDRAM pointer */
    want = dname[dstate - D_PICK1];
    s = (const volatile char *)str;
    for (i = 0; want[i]; i++)
        if (s[i] != want[i]) return;
    if (s[i] == 0) target_seen_frame = frames;
}

static int highlighted_is(void)
{
    return frames - target_seen_frame < 8;
}

static void director_start(const char *a, const char *b, int watch)
{
    dname[0] = a;
    dname[1] = b;
    dwatch = (uint8_t)watch;
    droute = 0;
    dscan = 0;
    dsteps = 0;
    seen_zero = 0;
    RAM8(UNIVERSE_ITEM_MODE) = MODE_EXHIBITION;
    dt0 = frames;
    dstate = D_ROUTE;
}

static void director_tick(void)
{
    uint32_t t = frames - dt0;
    switch (dstate) {
    case D_ROUTE:
        while (droute < sizeof ROUTE / sizeof ROUTE[0] && t >= ROUTE[droute].at) {
            const RouteStep *r = &ROUTE[droute++];
            if (!r->watch_only || dwatch) press(r->buttons);
        }
        if (t >= 60) RAM8(UNIVERSE_ITEM_MODE) = MODE_SURVIVAL;  /* restore the menu item */
        if (t >= ROUTE_END) { dstate = D_PICK1; dnext = frames; }
        break;
    case D_PICK1:
    case D_PICK2:
        if (frames < dnext) break;
        if (highlighted_is()) {
            press(B_A);
            dscan = 0;
            dnext = frames + 90;
            if (dstate == D_PICK2) { dt0 = frames; dstate = D_INTRO; }
            else dstate = D_PICK2;
        } else if (++dsteps > 400) {
            dstate = D_FAIL;
        } else {
            uint16_t b = SCAN[dscan];
            press(b);
            dscan = (uint8_t)((dscan + 1) % SCAN_LEN);
            dnext = frames + (b == B_R ? 80 : 40);   /* the name label animates in */
        }
        break;
    case D_INTRO:           /* Watch: skip the entrances; Play: hands off */
        if (dwatch && t >= 300 && t <= 2700 && t % 300 < 1) press(B_A);
        /* fall through */
    case D_MATCH: {
        uint8_t r1 = RAM8(RESULT_SIDE1), r2 = RAM8(RESULT_SIDE2);
        if (r1 == 0 && r2 == 0) seen_zero = 1;
        if (seen_zero && (r1 == 4 || r2 == 4)) {
            us_match_result(r1 == 4 ? 0 : 1, r1 == 4 ? r2 : r1);
            dstate = D_DONE;
        } else if (dstate == D_INTRO && t > 2800) {
            dstate = D_MATCH;
        }
        break;
    }
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

#define B_B      0x4000
#define B_DUP    0x0800

static uint8_t universe_mode;      /* 1 while the player is inside Universe */
static uint8_t swallow;            /* block game input until buttons are released */
static uint16_t prev_buttons;
static uint8_t test_hold;

void universe_draw(void *holder)
{
    if (universe_mode) us_draw(holder);
}

/* Single Play menu record: +0x08 menu-table entry pointer, +0x0C u16
 * cursor, +0x18 u32 wheel page (0x00 Single Play, 0x01 Commissioner,
 * 0x10 Multi Play). Universe is the third Single Play item (cursor 2). */
#define SP_RECORD       0x80159C00u
#define SP_TABLE_ENTRY  0x8014F4E0u
#define PAGE_SINGLE     0x00

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
                universe_mode = 1;          /* enter Universe; the game never sees this A */
                us_enter(cp0_count());
                pads[0].button = 0;
            }
        } else {
            int r = us_input(pressed);
            if (r == 0) {
                universe_mode = 0;          /* back to the Single Play menu */
                swallow = 1;
            } else if (r == 2) {            /* Play / Watch the selected match */
                universe_mode = 0;
                director_start(us_pending_name(0), us_pending_name(1), us_pending_watch());
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
    mb->step = (uint8_t)dsteps;
    if (dstate != D_IDLE && dstate != D_DONE && dstate != D_FAIL &&
        !(dstate >= D_INTRO && !dwatch))
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
