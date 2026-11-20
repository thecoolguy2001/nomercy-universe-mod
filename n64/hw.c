/* Bare-metal N64 hardware access: video, controller, caches, hand-off. */
#include "hw.h"
#include "font.h"

#define REG(a) (*(volatile uint32_t *)(a))

/* Video Interface */
#define VI_CONTROL  REG(0xA4400000)
#define VI_ORIGIN   REG(0xA4400004)
#define VI_WIDTH    REG(0xA4400008)
#define VI_V_INTR   REG(0xA440000C)
#define VI_V_CURRENT REG(0xA4400010)
#define VI_BURST    REG(0xA4400014)
#define VI_V_SYNC   REG(0xA4400018)
#define VI_H_SYNC   REG(0xA440001C)
#define VI_LEAP     REG(0xA4400020)
#define VI_H_START  REG(0xA4400024)
#define VI_V_START  REG(0xA4400028)
#define VI_V_BURST  REG(0xA440002C)
#define VI_X_SCALE  REG(0xA4400030)
#define VI_Y_SCALE  REG(0xA4400034)

/* Serial Interface (controllers via PIF) */
#define SI_DRAM_ADDR      REG(0xA4800000)
#define SI_PIF_ADDR_RD64B REG(0xA4800004)
#define SI_PIF_ADDR_WR64B REG(0xA4800010)
#define SI_STATUS         REG(0xA4800018)
#define PIF_RAM_ADDR      0x1FC007C0u

#define FB_PHYS   0x00090000u
#define FB        ((volatile uint16_t *)(0xA0000000u | FB_PHYS))
#define FB_W      320
#define FB_H      240

void vi_init(void)
{
    fb_clear(0);
    /* NTSC 320x240, 16-bit, resample only (No Mercy US is NTSC). */
    VI_CONTROL  = 0x0000320E;
    VI_ORIGIN   = FB_PHYS;
    VI_WIDTH    = FB_W;
    VI_V_INTR   = 0x3FF;
    VI_V_CURRENT = 0;
    VI_BURST    = 0x03E52239;
    VI_V_SYNC   = 0x0000020D;
    VI_H_SYNC   = 0x00000C15;
    VI_LEAP     = 0x0C150C15;
    VI_H_START  = 0x006C02EC;
    VI_V_START  = 0x002501FF;
    VI_V_BURST  = 0x000E0204;
    VI_X_SCALE  = 0x00000200;
    VI_Y_SCALE  = 0x00000400;
}

void vi_blank(void)
{
    VI_H_START = 0;
    VI_CONTROL = 0;
}

void fb_rect(int x, int y, int w, int h, uint16_t color)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    for (j = 0; j < h; j++) {
        volatile uint16_t *p = FB + (y + j) * FB_W + x;
        for (i = 0; i < w; i++) p[i] = color;
    }
}

void fb_clear(uint16_t color)
{
    fb_rect(0, 0, FB_W, FB_H, color);
}

void draw_text(int col, int row, const char *s, uint16_t color)
{
    for (; *s && col < SCREEN_COLS; s++, col++) {
        int c = (unsigned char)*s, y, x;
        const uint8_t *g;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 127) c = '?';
        g = FONT[c - 32];
        for (y = 0; y < 7; y++) {
            volatile uint16_t *p = FB + (row * 8 + y) * FB_W + col * 8 + 1;
            for (x = 0; x < 5; x++)
                if (g[y] & (0x10 >> x)) p[x] = color;
        }
    }
}

/* Wait for the next frame: VI_V_CURRENT wraps back to 0 at each new field.
 * Bounded so an emulator with a frozen counter still gets a short delay. */
void wait_frame(void)
{
    uint32_t prev = VI_V_CURRENT, cur, spins = 0;
    for (;;) {
        cur = VI_V_CURRENT;
        if (cur < prev || ++spins > 400000) break;
        prev = cur;
    }
}

static void si_wait(void)
{
    uint32_t spins = 0;
    while ((SI_STATUS & 3) && ++spins < 1000000) {}
}

/* 64-byte PIF command block, only ever touched through KSEG1 (uncached). */
static uint32_t pif_buf[16] __attribute__((aligned(16)));

uint16_t pad_read(void)
{
    volatile uint32_t *p = (volatile uint32_t *)(0xA0000000u | ((uint32_t)pif_buf & 0x1FFFFFFFu));
    uint32_t phys = (uint32_t)pif_buf & 0x1FFFFFFFu;
    uint32_t resp;
    uint16_t btn;
    int8_t sx, sy;
    int i;

    /* channel 1: read buttons (tx 1, rx 4, cmd 0x01); channels 2-4 skipped */
    p[0] = 0xFF010401u;
    p[1] = 0xFFFFFFFFu;
    p[2] = 0xFE000000u;
    for (i = 3; i < 15; i++) p[i] = 0;
    p[15] = 1;

    si_wait();
    SI_DRAM_ADDR = phys;
    SI_PIF_ADDR_WR64B = PIF_RAM_ADDR;
    si_wait();
    SI_STATUS = 0;
    SI_DRAM_ADDR = phys;
    SI_PIF_ADDR_RD64B = PIF_RAM_ADDR;
    si_wait();
    SI_STATUS = 0;

    if (p[0] & 0x0000C000u) return 0; /* error bits: no controller */
    resp = p[1];
    btn = (uint16_t)(resp >> 16);
    sx = (int8_t)(resp >> 8);
    sy = (int8_t)resp;
    if (sy > 40) btn |= BTN_UP;
    if (sy < -40) btn |= BTN_DOWN;
    if (sx < -40) btn |= BTN_LEFT;
    if (sx > 40) btn |= BTN_RIGHT;
    return btn;
}

uint32_t mem_size(void)
{
    return REG(0xA0000318); /* osMemSize, written by the boot code */
}

uint32_t cp0_count(void)
{
    uint32_t c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}

extern const uint32_t orig_entry[24];

/* Put No Mercy's original entry code back and run it. It clears the BSS
 * (wiping this payload) and starts the game as if we were never here. */
void handoff_to_game(void)
{
    volatile uint32_t *entry = (volatile uint32_t *)0xA0000400u;
    uint32_t a;
    int i;

    vi_blank();
    for (i = 0; i < 24; i++) entry[i] = orig_entry[i];
    for (a = 0x80000000u; a < 0x80002000u; a += 16)      /* 8 KB D-cache */
        __asm__ volatile(".set push\n\t.set mips3\n\tcache 0x01, 0(%0)\n\t.set pop" : : "r"(a));  /* index writeback-invalidate */
    for (a = 0x80000000u; a < 0x80004000u; a += 32)      /* 16 KB I-cache */
        __asm__ volatile(".set push\n\t.set mips3\n\tcache 0x00, 0(%0)\n\t.set pop" : : "r"(a));  /* index invalidate */
    __asm__ volatile("jr %0\n\tnop" : : "r"(0x80000400u));
    for (;;) {}
}
