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
#include "../../engine/universe.h"

#define PIF_RAM   ((const volatile uint8_t *)0x800A0200u) /* __osContPifRam */
#define MAX_CONT  (*(const volatile uint8_t *)0x800A2614u) /* __osMaxControllers */
#define MAILBOX   ((volatile Mailbox *)MAILBOX_ADDR)

#define RAM8(a)   (*(volatile uint8_t *)(a))
#define RAM16(a)  (*(volatile uint16_t *)(a))
#define RAM32(a)  (*(volatile uint32_t *)(a))

/* Game state found with the debugger (notes/re-findings.md) */
#define SS_CURSOR_A    0x8015B815u  /* Superstar Select: P1 grid cursor */
#define SS_CURSOR_B    0x800AAAB5u  /* Superstar Select: shared grid cursor */
#define SS_CURSOR_CPU  0x8015B94Du  /* Superstar Select: second pick cursor */
#define SS_CURSOR_CPU2 0x80045849u
#define CLOCK_MIN      0x8015AB18u
#define CLOCK_SEC      0x8015AB1Cu
#define RESULT(slot)   (0x8015AF45u + 0x358u * (slot))  /* per slot: 0 in the match, 4 = won */
#define FALLS(slot)    (0x8015A73Bu + 4u * (slot))      /* Ironman falls scored (low byte of a u32) */
/* Both are only valid on the match screen: the result screen reuses them. */
#define FINISH_FLAGS   0x800A261Cu   /* u32 set at the bell, kept for the result screen */

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
 * match and records the result. The Universe item's "mode opened" byte is
 * switched to Exhibition and A is pressed on it; as soon as Exhibition's
 * MATCH tab is up, Single Match and Watch / 1P vs CPU are written and START
 * ("Decision") goes straight to Superstar Select. Its cursor starts on the
 * card's wrestlers; the player confirms them (or picks someone else) and
 * starts the match. The result is read at the bell; a rematch replaces it. */
enum { D_IDLE, D_ROUTE, D_SELECT, D_INTRO, D_MATCH, D_RETURN, D_STEER, D_DONE, D_FAIL };
static uint32_t footer_frame;          /* last frame a menu footer was drawn (menus on screen) */
static void enter_universe(void);
static uint8_t universe_mode;
static int universe_highlighted(void);

#define UNIVERSE_ITEM_MODE  0x8040903Fu  /* byte 19 of the 3rd Single Play item */
#define MODE_SURVIVAL       0x03
#define B_R      0x0010
#define B_L      0x0020
#define B_Z      0x2000
#define B_DLEFT  0x0200
#define B_DRIGHT 0x0100

/* Exhibition settings (menu overlay data) */
#define EX_MATCH_CURSOR   0x8015A0EDu    /* MATCH tab item (USetup.match_item) */
/* PLAYER tab choice: a position in the type's full list (greyed-out
 * multiplayer entries included); USetup.player */
#define EX_PLAYER         0x80159FB5u
/* RULES: the tab's live row (filled from the match type's saved rules when
 * the tab opens, which Universe never does) and the match's rules. A on the
 * loading screen first turns the match's rules into the match settings
 * (0x80137414: flags 0x800AAE80, time limit 0x800A139C ...), then commits:
 * the live row is copied to the match's rules and packed into the save data
 * as the player's rules for that match type (call at 0x8013576C), which the
 * game soon writes to the cartridge. Universe writes both rows, keeps that
 * call out while it runs a match, and puts the player's rows back after. */
#define LIVE_RULES        0x800931D8u
#define MATCH_RULES       0x800AB574u
#define SAVE_RULES_CALL   0x8013576Cu    /* menu overlay: jal 0x800E4A98 */
#define SAVE_RULES_JAL    0x0C0392A6u
#define SAVE_RULES_BEFORE 0x24040004u    /* addiu a0, zero, 4 (the call's last slot choice) */
/* ARENA tab choice (0 Raw is War .. 9 Armageddon, see us_show_arena): the
 * tab's own bytes and the copy the match loader uses */
static const uint32_t EX_ARENA[] = { 0x80159E7Du, 0x80159656u, 0x8015971Eu, 0x800AA99Fu };
/* Match setup read by the match loader */
#define SETUP_SLOT(i)     (0x800AAAD0u + 12u * (i))  /* +0 wrestler id (u16, 0xFFxx = not picked) */
#define SLOT_HUMAN        1              /* slot +3 before Superstar Select: controller */
#define SLOT_CPU          0
#define SETUP_BELT        0x800AB5B4u    /* s32 belt on the line, -1 = none */
#define CHAMPION(b)       (0x800A53E2u + 0x34u * (b)) /* wrestler number of the champion (+1: tag partner) */
#define GAME_SCREEN       0x80045845u    /* screen being shown (low byte of a u16) */
#define SCREEN_MULTI_PLAY   0x02
#define SCREEN_SINGLE_PLAY  0x03
#define SCREEN_EX_MATCH     0x07         /* Exhibition MATCH tab */
#define SCREEN_EX_PLAYER    0x06         /* Exhibition PLAYER tab */
#define SCREEN_LOADING      0x19         /* "Single Match / A VS B" loading screen */
#define SCREEN_SELECT       0x1A         /* Superstar Select (picker 26) */
#define SCREEN_SELECT_2     0x1B         /* Superstar Select, picker 27 */
#define SCREEN_RESULT       0x1D
#define SCREEN_MATCH        0x2A
#define NEXT_SCREEN       0x801596C0u    /* u16 screen the loading transition goes to */
#define WRESTLER_TABLE    0x800471C4u    /* [n] (16 bytes): pointer to the record, +0 u16 id */
/* Superstar Select starts its cursor from these (saved from the last visit) */
#define SS_SAVED_CELL     0x800AAAB4u    /* s16 grid cell 0-8 */
#define SS_SAVED_PAGE     0x800AAAB6u    /* u16 page */
#define SS_PAGES          0x80047150u    /* [page][12] wrestler numbers, 9 cells used */
#define SS_SUPERSTAR_PAGES 5

static uint8_t dstate, dwatch, seen_result, dresults, dlast_screen;
static USetup  dsetup;                 /* the match being staged */
static UGameResult dlast;              /* its latest result values, read while it is on screen */
static uint8_t dlast_valid, dseen_zero;
static uint8_t dsaved_champion[2], darena;
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

static uint8_t game_number(uint16_t id)   /* the reverse; 0 = unknown */
{
    unsigned n;
    for (n = 1; n <= 64; n++)
        if (game_id((uint8_t)n) == id) return (uint8_t)n;
    return 0;
}

/* Superstar Select opens on the page and cell of wrestler n */
static int find_cell(uint8_t n, unsigned *page, unsigned *cell)
{
    unsigned p, c;
    for (p = 0; p < SS_SUPERSTAR_PAGES; p++)
        for (c = 0; c < 9; c++)
            if (RAM8(SS_PAGES + 12u * p + c) == n) {
                *page = p;
                *cell = c;
                return 1;
            }
    return 0;
}

static void preset_cursor(uint8_t n)
{
    unsigned p, c;
    if (!find_cell(n, &p, &c)) return;
    RAM16(SS_SAVED_PAGE) = (uint16_t)p;
    RAM16(SS_SAVED_CELL) = (uint16_t)c;
}

/* Tag partners are picked by the same picker straight after each other
 * (no new picker init): the cursor is moved there - R / L to the page,
 * then the picker's cell. Pickers: 26 while screen 1A, 27 while 1B. */
#define PICKER_OBJ(screen) ((screen) == SCREEN_SELECT ? 0x8015B810u : 0x8015B948u)
static uint8_t dfilled, dmove;

static int slots_filled(void)
{
    int i = 0;
    while (i < dsetup.n_slots && RAM8(SETUP_SLOT(i)) != 0xFF) i++;
    return i;
}

static void move_partner_cursor(uint8_t screen)
{
    int filled = slots_filled();
    unsigned p, c, cur;
    if (filled != dfilled) {
        dfilled = (uint8_t)filled;
        dmove = dsetup.tag && (filled == 1 || filled == 3);
        dnext = frames + 30;                   /* let the confirm settle */
    }
    if (!dmove || frames < dnext) return;
    if ((screen != SCREEN_SELECT && screen != SCREEN_SELECT_2) ||
        !find_cell(dsetup.wrestler[filled], &p, &c)) {
        dmove = 0;
        return;
    }
    cur = RAM16(SS_SAVED_PAGE);
    if (cur != p) {
        press(cur < p ? B_R : B_L);
        dnext = frames + 24;                   /* the page slides in */
        return;
    }
    RAM16(PICKER_OBJ(screen) + 4) = (uint16_t)c;
    dmove = 0;
}

/* Superstar Select's picker init (menu overlay, in RAM only while the menus
 * are loaded) is redirected to ss_picker_init while the director needs it. */
#define PICKER_INIT       0x801124D0u
#define PICKER_SIDE1      26
static const uint32_t PICKER_INIT_ORIG[6] = {
    0x27BDFFE0u, 0xAFB10014u, 0x00808821u, 0xAFB20018u, 0x00A09021u, 0xAFB00010u };
#define PICKER_INIT_JUMP  0x0810400Au     /* j 0x80410028 */

static void sync_code(uint32_t addr)       /* make a code write visible to the CPU */
{
    __asm__ volatile(".set push\n.set mips3\n"
                     "cache 0x19, 0(%0)\n"   /* write back the data cache line */
                     "cache 0x10, 0(%0)\n"   /* invalidate the instruction cache line */
                     ".set pop" : : "r"(addr) : "memory");
}

static void picker_hook(int on)
{
    unsigned i;
    if (on) {
        for (i = 0; i < 6; i++)
            if (RAM32(PICKER_INIT + 4u * i) != PICKER_INIT_ORIG[i]) return;
        RAM32(PICKER_INIT) = PICKER_INIT_JUMP;
        RAM32(PICKER_INIT + 4) = 0;
    } else {
        if (RAM32(PICKER_INIT) != PICKER_INIT_JUMP || RAM32(PICKER_INIT + 4) != 0) return;
        RAM32(PICKER_INIT) = PICKER_INIT_ORIG[0];
        RAM32(PICKER_INIT + 4) = PICKER_INIT_ORIG[1];
    }
    sync_code(PICKER_INIT);
}

void ss_picker_init(int picker)
{
    /* The two pickers (26 and 27) take turns; the slot being picked is the
     * first one not confirmed yet */
    int slot = 0;
    if (picker != PICKER_SIDE1 && picker != PICKER_SIDE1 + 1) return;
    while (slot < dsetup.n_slots && RAM8(SETUP_SLOT(slot)) != 0xFF) slot++;
    if ((dstate == D_ROUTE || dstate == D_SELECT) && slot < dsetup.n_slots)
        preset_cursor(dsetup.wrestler[slot]);
}

static void set_player_mode(void)
{
    RAM8(EX_PLAYER) = dsetup.player;
}

/* The player's own rules, kept while Universe uses the game's */
static uint8_t keep_live[US_RULES], keep_match[US_RULES], rules_kept;

static void keep_rules(void)
{
    unsigned i;
    if (rules_kept) return;                /* still Universe's: keep the player's */
    for (i = 0; i < US_RULES; i++) {
        keep_live[i] = RAM8(LIVE_RULES + i);
        keep_match[i] = RAM8(MATCH_RULES + i);
    }
    rules_kept = 1;
}

static void restore_rules(void)
{
    unsigned i;
    if (!rules_kept) return;
    for (i = 0; i < US_RULES; i++) {
        RAM8(LIVE_RULES + i) = keep_live[i];
        RAM8(MATCH_RULES + i) = keep_match[i];
    }
    rules_kept = 0;
}

/* The commit's save call is a nop while the director runs a match (only
 * patched when the menu overlay is there; the words around it are checked) */
static void save_rules_hook(int off)
{
    if (RAM32(SAVE_RULES_CALL - 4) != SAVE_RULES_BEFORE || RAM32(SAVE_RULES_CALL + 4) != 0)
        return;
    if (off && RAM32(SAVE_RULES_CALL) == SAVE_RULES_JAL) {
        RAM32(SAVE_RULES_CALL) = 0;
        sync_code(SAVE_RULES_CALL);
    } else if (!off && RAM32(SAVE_RULES_CALL) == 0) {
        RAM32(SAVE_RULES_CALL) = SAVE_RULES_JAL;
        sync_code(SAVE_RULES_CALL);
    }
}

static void set_rules(void)
{
    unsigned i;
    for (i = 0; i < US_RULES; i++) {
        uint8_t v = dsetup.rules[i];
        if (i == 1 && MAILBOX->test_time) v = (uint8_t)(MAILBOX->test_time - 1);
        RAM8(LIVE_RULES + i) = v;
        RAM8(MATCH_RULES + i) = v;
    }
}

/* The show's arena */
static void set_arena(void)
{
    unsigned i;
    for (i = 0; i < sizeof EX_ARENA / sizeof EX_ARENA[0]; i++) RAM8(EX_ARENA[i]) = darena;
}

/* Who controls each side, as the PLAYER tab leaves it for Superstar Select
 * (only valid before the picks: the byte means something else afterwards) */
static void set_controllers(void)
{
    /* 1P vs CPU: the game marks the player's side (slot 0, and slot 1 too
     * when it is slot 0's partner: Tag and Handicap); a guest referee
     * (slot 0) is the player; Watch: all CPU */
    int i;
    for (i = 0; i < 4; i++)
        RAM8(SETUP_SLOT(i) + 3) = dsetup.human[i] ? SLOT_HUMAN : SLOT_CPU;
}

/* The title on the line (the menu's BELT tab is greyed out on a fresh save) */
static void write_belt(void)
{
    RAM32(SETUP_BELT) = (uint32_t)dsetup.belt;
    if (dsetup.belt >= 0) {
        RAM8(CHAMPION(dsetup.belt)) = dsetup.champion[0];
        RAM8(CHAMPION(dsetup.belt) + 1) = dsetup.champion[1];
    }
}

static void director_start(int watch)
{
    us_pending_setup(&dsetup);
    if (dsetup.belt >= 0) {
        dsaved_champion[0] = RAM8(CHAMPION(dsetup.belt));
        dsaved_champion[1] = RAM8(CHAMPION(dsetup.belt) + 1);
    }
    dwatch = (uint8_t)watch;
    keep_rules();
    dlast_valid = 0;
    dseen_zero = 0;
    dlast_screen = 0xFF;
    dfilled = 0;
    dmove = 0;
    darena = (uint8_t)us_show_arena();
    preset_cursor(dsetup.wrestler[0]);
    RAM8(UNIVERSE_ITEM_MODE) = dsetup.mode;      /* Exhibition, Ladder, Ironman, ... */
    us_cover(1);
    press(B_A);
    dt0 = frames;
    dstate = D_ROUTE;
}

/* Back on the menus for good: the game's own title records stay as they were */
static void director_end_match(void)
{
    if (dsetup.belt >= 0) {
        RAM8(CHAMPION(dsetup.belt)) = dsaved_champion[0];
        RAM8(CHAMPION(dsetup.belt) + 1) = dsaved_champion[1];
    }
    RAM32(SETUP_BELT) = 0xFFFFFFFFu;
    restore_rules();
    us_match_closed();
}

/* Who actually wrestled: the game number in each slot (0 = unknown) */
static void actual_wrestlers(uint8_t n[4])
{
    int s;
    for (s = 0; s < 4; s++)
        n[s] = s < dsetup.n_slots ? game_number(*(volatile uint16_t *)SETUP_SLOT(s)) : 0;
}

/* How the match ended, from the finish flags the result screen shows
 * (0x8010B3F8 turns them into its caption: 0x2000 Submit, 0x4000 Pin,
 * 0x8000 Draw, 0x10000 TKO, 0x20000 DQ, 0x80000-0x400000 Count out; the
 * ladder / cage finishes have no caption) */
static uint8_t game_method(uint32_t f)
{
    if (f & 0x2000u) return U_WIN_SUBMISSION;
    if (f & 0x4000u) return U_WIN_PINFALL;
    if (f & 0x8000u) return U_WIN_DRAW;
    if (f & 0x10000u) return U_WIN_SUBMISSION;      /* TKO (off in Universe) */
    if (f & 0x20000u) return U_WIN_DQ;
    if (f & 0x780000u) return U_WIN_COUNTOUT;
    return U_WIN_SPECIAL;                           /* the rest: ladder, cage */
}

static int dlast_won(void)
{
    int i;
    for (i = 0; i < 4; i++)
        if (dlast.result[i] == 4) return 1;
    return 0;
}

/* Backing out of Superstar Select (B) would go to Exhibition's MATCH tab:
 * the transition's target screen is changed to the Single Play page instead,
 * and Universe reopens under the cover. */
static int backed_out(void)
{
    if (RAM16(NEXT_SCREEN) != SCREEN_EX_MATCH) return 0;
    RAM16(NEXT_SCREEN) = SCREEN_SINGLE_PLAY;
    us_cover(1);
    director_end_match();
    seen_result = 0;
    dstate = D_RETURN;
    dt0 = frames;
    return 1;
}

static void director_tick(void)
{
    uint32_t t = frames - dt0;
    uint8_t screen = RAM8(GAME_SCREEN);
    switch (dstate) {
    case D_ROUTE:
        /* Universe item -> Exhibition MATCH tab: the match type is
         * highlighted and chosen with A (the game commits it and resets the
         * PLAYER choice) -> PLAYER tab: Watch or 1P is set and START
         * ("Decision") goes straight to Superstar Select. */
        if (t >= 60) RAM8(UNIVERSE_ITEM_MODE) = MODE_SURVIVAL;  /* restore the menu item */
        if (screen == SCREEN_EX_MATCH || screen == SCREEN_EX_PLAYER) {
            if (screen == SCREEN_EX_MATCH) {
                RAM8(EX_MATCH_CURSOR) = dsetup.match_item;  /* Single, Tag, ... */
            } else {
                set_player_mode();                     /* Watch or 1P vs CPU */
                set_controllers();
                set_arena();
                set_rules();
            }
            if (screen != dlast_screen) dnext = frames + 6;  /* let the tab settle first */
            if (frames >= dnext) {
                press(screen == SCREEN_EX_MATCH ? B_A : B_START);
                dnext = frames + 20;
            }
        }
        dlast_screen = screen;
        if (screen == SCREEN_SELECT) {
            us_cover(0);
            dstate = D_SELECT;
            dt0 = frames;
        } else if (t > 900) {
            us_cover(0);
            RAM8(UNIVERSE_ITEM_MODE) = MODE_SURVIVAL;
            restore_rules();
            dstate = D_FAIL;
        }
        break;
    case D_SELECT:
        /* The player picks (the cursor starts on the card's wrestlers) and
         * starts the match from the loading screen (B there goes back to
         * Superstar Select, so it still counts as picking). */
        set_player_mode();
        set_arena();
        set_rules();
        write_belt();
        if (backed_out()) break;
        move_partner_cursor(screen);
        if (screen == SCREEN_MATCH) {
            dt0 = frames;
            dstate = D_INTRO;
        } else if (screen == SCREEN_MULTI_PLAY || screen == SCREEN_SINGLE_PLAY) {
            director_end_match();
            dstate = D_RETURN;
            dt0 = frames;
        }
        break;
    case D_INTRO:           /* entrances, match, replays: all hands off */
    case D_MATCH: {
        int i;
        if (backed_out()) break;
        if (screen == SCREEN_MATCH && !(dlast_valid && dseen_zero && dlast_won())) {
            /* the per-slot values, kept from the bell on (a 4 after the
             * match started at 0): the game clears them a few seconds later
             * and the result screen reuses them; falls only ever go up */
            int zero = 1;
            for (i = 0; i < 4; i++) {
                uint8_t f = i < dsetup.n_slots ? RAM8(FALLS(i)) : 0;
                dlast.result[i] = i < dsetup.n_slots ? RAM8(RESULT(i)) : 0;
                if (dlast.result[i]) zero = 0;
                if (!dseen_zero || f > dlast.falls[i]) dlast.falls[i] = f;
            }
            if (zero) dseen_zero = 1;
            dlast_valid = 1;
        }
        /* The result is recorded when the result screen comes up: the
         * winner (4) from the match, how it ended from the finish flags
         * (0 until the match ends); no winner is a draw (time limit, double
         * count-out, Ironman level on falls) */
        if (screen == SCREEN_RESULT && dlast_valid) {
            uint32_t f = RAM32(FINISH_FLAGS);
            int won = dseen_zero && dlast_won();
            if (won || f) {
                dlast.method = won ? game_method(f) : U_WIN_DRAW;
                if (!won) for (i = 0; i < 4; i++) dlast.result[i] = 0;
                actual_wrestlers(dlast.game_n);
                us_match_result(&dlast);
                dresults++;
            }
            dlast_valid = 0;
            dstate = D_RETURN;
            dt0 = frames;
        } else if (dstate == D_INTRO && t > 2800) {
            dstate = D_MATCH;
        }
        break;
    }
    case D_RETURN:
        /* Hands off: the player watches the replays and picks Exit or
         * Rematch on the result screen. A rematch goes back to the match
         * (its result replaces this one). Exit asks for the Multi Play page
         * of the main menu; it is changed to Single Play, where Universe
         * is, and the Universe screen is shown as soon as the menu is up. */
        if (screen == SCREEN_RESULT) seen_result = 1;
        if (seen_result && (screen == SCREEN_LOADING || screen == SCREEN_MATCH)) {
            dlast_valid = 0;                           /* Rematch */
            dseen_zero = 0;
            dt0 = frames;
            dstate = D_INTRO;
            break;
        }
        if (screen == SCREEN_MULTI_PLAY) RAM8(GAME_SCREEN) = SCREEN_SINGLE_PLAY;
        if (screen == SCREEN_MULTI_PLAY || screen == SCREEN_SINGLE_PLAY) {
            us_cover(1);                               /* until Universe is open */
            director_end_match();
        }
        if (frames - dt0 > 60 && frames - footer_frame < 10 &&
            (screen == SCREEN_MULTI_PLAY || screen == SCREEN_SINGLE_PLAY)) {
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

/* End of the frame's display list (hook at 0x80002DE4) */
uint32_t *dl_end(uint32_t *g)
{
    us_draw_cover(&g);
    return g;
}

static void enter_universe(void)
{
    volatile Mailbox *mb = MAILBOX;
    universe_mode = 1;
    us_test_stip(mb->test_stip);
    us_enter(mb->test_seed ? mb->test_seed : cp0_count());
}


static int universe_highlighted(void)
{
    /* the record keeps stale values on other screens: check the screen too */
    return RAM8(GAME_SCREEN) == SCREEN_SINGLE_PLAY &&
           RAM32(SP_RECORD + 0x08) == SP_TABLE_ENTRY &&
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
        } else if (dstate == D_ROUTE || dstate == D_STEER) {
            /* the director is driving */
        } else if (!universe_mode) {
            if (swallow) {
                if (btn) pads[0].button = 0; else swallow = 0;
            } else if (universe_highlighted() && (pressed & B_A)) {
                /* A on Universe always opens it (the item underneath is
                 * Survival); a director that lost track of the match ends */
                if (dstate != D_IDLE && dstate != D_DONE && dstate != D_FAIL) {
                    director_end_match();
                    us_cover(0);
                    dstate = D_IDLE;
                }
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
    picker_hook(dstate == D_ROUTE || dstate == D_SELECT);
    save_rules_hook(dstate >= D_ROUTE && dstate <= D_RETURN);
    mb->state = dstate;
    mb->step = dresults;                  /* match results recorded (debug) */
    if (dstate == D_ROUTE || dstate == D_STEER || dmove)
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
