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

/* ---- macro: menu route from the Exhibition MATCH tab ----
 * Offsets in frames from the command, taken from debugger scripts that
 * were verified to reach a match (the MATCH tab was at frame 2700). */
enum { OP_PRESS, OP_PICK1, OP_PICK2, OP_WATCH_ONLY, OP_END };

typedef struct {
    uint16_t at;
    uint8_t  op;
    uint16_t buttons;
} Step;

static const Step ROUTE[] = {
    {  10, OP_PRESS, B_A },          /* MATCH: Single Match */
    { 150, OP_WATCH_ONLY, B_DDOWN }, /* PLAYER: down, down = Watch */
    { 180, OP_WATCH_ONLY, B_DDOWN },
    { 200, OP_PRESS, B_A },          /* PLAYER confirm */
    { 400, OP_PRESS, B_A },          /* ARENA */
    { 600, OP_PRESS, B_A },          /* RULES */
    { 800, OP_PRESS, B_START },      /* Decision -> Superstar Select */
    { 995, OP_PICK1, 0 },
    {1000, OP_PRESS, B_A },
    {1305, OP_PICK2, 0 },
    {1310, OP_PRESS, B_A },
    {1400, OP_PICK2, 0 },
    {1405, OP_PRESS, B_A },
    {1500, OP_PICK2, 0 },
    {1505, OP_PRESS, B_A },
    {1800, OP_PRESS, B_A },          /* skip entrances / intro screen */
    {2100, OP_PRESS, B_A },
    {2400, OP_PRESS, B_A },
    {2700, OP_PRESS, B_A },
    {3000, OP_PRESS, B_A },
    {3300, OP_PRESS, B_A },
    {3600, OP_PRESS, B_A },
    {3700, OP_END, 0 },
};

#define HOLD_FRAMES 3

static uint32_t start_frame, hold_until;
static uint16_t hold_buttons;
static uint8_t watch_mode;

static void begin(volatile Mailbox *mb)
{
    watch_mode = mb->cmd == CMD_WATCH;
    mb->cmd = CMD_NONE;
    mb->state = ST_SETUP;
    mb->step = 0;
    mb->winner_side = 0xFF;
    mb->loser_code = 0;
    mb->clock_min = mb->clock_sec = 0;
    start_frame = frames;
    hold_until = 0;
}

static void run_setup(volatile Mailbox *mb)
{
    uint32_t t = frames - start_frame;
    while (mb->state == ST_SETUP && t >= ROUTE[mb->step].at) {
        const Step *s = &ROUTE[mb->step++];
        switch (s->op) {
        case OP_WATCH_ONLY:
            if (!watch_mode) break;
            /* fall through */
        case OP_PRESS:
            hold_buttons = s->buttons;
            hold_until = frames + HOLD_FRAMES;
            break;
        case OP_PICK1:
            RAM8(SS_CURSOR_A) = mb->p1;
            RAM8(SS_CURSOR_B) = mb->p1;
            break;
        case OP_PICK2:
            RAM8(SS_CURSOR_CPU) = mb->p2;
            RAM8(SS_CURSOR_B) = mb->p2;
            RAM8(SS_CURSOR_CPU2) = mb->p2;
            break;
        case OP_END:
            mb->state = ST_IN_MATCH;
            break;
        }
    }
}

static void check_result(volatile Mailbox *mb)
{
    uint8_t r1 = RAM8(RESULT_SIDE1), r2 = RAM8(RESULT_SIDE2);
    if (r1 != 4 && r2 != 4) return;
    mb->winner_side = r1 == 4 ? 0 : 1;
    mb->loser_code = r1 == 4 ? r2 : r1;
    mb->clock_min = (uint16_t)RAM32(CLOCK_MIN);
    mb->clock_sec = (uint16_t)RAM32(CLOCK_SEC);
    mb->state = ST_DONE;
}

/* Single Play menu record (see notes): +0x08 menu-table entry pointer,
 * +0x0C u16 cursor, +0x18 u32 wheel page (0x00 Single Play, 0x01 Commissioner,
 * 0x10 Multi Play). Universe is the third Single Play item (cursor 2). */
#define SP_RECORD       0x80159C00u
#define SP_TABLE_ENTRY  0x8014F4E0u
#define PAGE_SINGLE     0x00

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

void universe_draw(void *holder)
{
    if (universe_mode) us_draw(holder);
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
        if (!universe_mode) {
            if (swallow) {
                if (btn) pads[0].button = 0; else swallow = 0;
            } else if (universe_highlighted() && (pressed & B_A)) {
                universe_mode = 1;          /* enter Universe; the game never sees this A */
                us_enter(cp0_count());
                pads[0].button = 0;
            }
        } else {
            if (!us_input(pressed)) {
                universe_mode = 0;          /* back to the Single Play menu */
                swallow = 1;
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

    if ((mb->cmd == CMD_WATCH || mb->cmd == CMD_PLAY) &&
        (mb->state == ST_IDLE || mb->state == ST_DONE))
        begin(mb);
    if (mb->state == ST_SETUP) run_setup(mb);
    if (mb->state == ST_SETUP || mb->state == ST_IN_MATCH) check_result(mb);

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
