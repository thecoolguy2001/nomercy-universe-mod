/* Bare-metal N64 hardware access for the boot-time Universe screen. */
#ifndef HW_H
#define HW_H
#include <stdint.h>

#define SCREEN_COLS 40
#define SCREEN_ROWS 30

/* RGBA5551 colours */
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 3) << 6) | (((b) >> 3) << 1) | 1))

enum {
    BTN_A = 0x8000, BTN_B = 0x4000, BTN_Z = 0x2000, BTN_START = 0x1000,
    BTN_UP = 0x0800, BTN_DOWN = 0x0400, BTN_LEFT = 0x0200, BTN_RIGHT = 0x0100,
    BTN_L = 0x0020, BTN_R = 0x0010,
    BTN_CUP = 0x0008, BTN_CDOWN = 0x0004, BTN_CLEFT = 0x0002, BTN_CRIGHT = 0x0001,
};

void     vi_init(void);
void     vi_blank(void);
void     fb_clear(uint16_t color);
void     fb_rect(int x, int y, int w, int h, uint16_t color);
void     draw_text(int col, int row, const char *s, uint16_t color);
void     wait_frame(void);
uint16_t pad_read(void);     /* controller 1 buttons, stick mapped to D-pad */
uint32_t mem_size(void);     /* RDRAM size reported by the boot code */
uint32_t cp0_count(void);    /* CPU cycle counter, used as a random seed */
void     handoff_to_game(void) __attribute__((noreturn));

#endif
