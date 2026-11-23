/*
 * Boot payload entry: installs the resident Universe module into Expansion
 * Pak RAM, then starts No Mercy. (The old boot-time Universe screen, held Z
 * at power-on, is gone: Universe lives on the Single Play menu.)
 */
#include "hw.h"
#include "resident/resident.h"

extern const uint8_t resident_image[];
extern const uint32_t resident_image_size;

/* osContGetReadData in RAM; the ROM has "j 0x80410000; nop" here. */
#define HOOK_ADDR       0x800346A8u
#define ORIG_INSN_0     0x27BDFFF0u  /* addiu sp, sp, -0x10 */
#define ORIG_INSN_1     0x00804821u  /* move  t1, a0 */
/* Asset lookup in RAM; the ROM has "j 0x80410008; nop" here. */
#define ASSET_HOOK_ADDR 0x80003BD0u
#define ASSET_ORIG_0    0x30A2FFFFu  /* andi  v0, a1, 0xffff */
#define ASSET_ORIG_1    0x2C430001u  /* sltiu v1, v0, 1 */
/* play_song in RAM; the ROM has "j 0x80410010; nop" here. */
#define SONG_HOOK_ADDR  0x80017CF4u
#define SONG_ORIG_0     0x3C028005u  /* lui v0, 0x8005 */
#define SONG_ORIG_1     0x8C428588u  /* lw  v0, -0x7a78(v0) */
/* get_text_image in RAM; the ROM has "j 0x80410020; nop" here. */
#define TEXT_HOOK_ADDR  0x80009FFCu
#define TEXT_ORIG_0     0x27BDFFB0u  /* addiu sp, sp, -0x50 */
#define TEXT_ORIG_1     0xAFB60048u  /* sw s6, 0x48(sp) */
/* Display list end in RAM; the ROM has "j 0x80410030; nop" here. */
#define DLEND_HOOK_ADDR 0x80002DE4u
#define DLEND_ORIG_0    0x27BDFFE8u  /* addiu sp, sp, -0x18 */
#define DLEND_ORIG_1    0x00A04821u  /* move  t1, a1 */
/* No Mercy's flash format; the ROM has "jal 0x80410038" (the resident
 * erases sectors 0-5 only, keeping the Universe save) */
#define FORMAT_CALL_ADDR 0x80004C74u
#define FORMAT_CALL_ORIG 0x0C00C05Cu  /* jal osFlashChipErase */

/* Arena pictures (tools/make_arenas.py), stored after the payload in ROM;
 * copied next to the resident module (where it expects them). */
#define ARENAS_CART  0x11FDA800u          /* the last 0x25800 bytes of the ROM (tools/build_rom.py) */
#define ARENAS_RAM   0x00480000u          /* = ARENAS_ADDR 0x80480000 */
#define ARENAS_LEN   (10u * 6u * 2560u)
#define PI_REG(o)    (*(volatile uint32_t *)(0xA4600000u + (o)))

static void install_arenas(void)
{
    PI_REG(0x00) = ARENAS_RAM;            /* PI_DRAM_ADDR */
    PI_REG(0x04) = ARENAS_CART;           /* PI_CART_ADDR */
    PI_REG(0x0C) = ARENAS_LEN - 1;        /* PI_WR_LEN: cart -> RDRAM */
    while (PI_REG(0x10) & 3) {}           /* DMA / IO busy */
    PI_REG(0x10) = 2;                     /* clear the PI interrupt */
}

static void install_resident(void)
{
    volatile uint8_t *dst = (volatile uint8_t *)(0xA0000000u | (RESIDENT_BASE & 0x1FFFFFFFu));
    volatile uint32_t *mb = (volatile uint32_t *)(0xA0000000u | (MAILBOX_ADDR & 0x1FFFFFFFu));
    uint32_t i;
    for (i = 0; i < resident_image_size; i++) dst[i] = resident_image[i];
    for (i = 0; i < sizeof(Mailbox) / 4; i++) mb[i] = 0;
}

/* Single Play menu items (22 bytes each). The ROM's menu table entry for
 * Single Play is patched to (SP_ITEMS_ADDR, 3). Fields: x, y (s16),
 * 0x04 0x00, up, down, left, right, label image id, help id, ...,
 * mode opened (byte 19). Items 0 and 1 are the game's own. */
#define SP_ITEMS_ADDR 0x80409000u
static const uint8_t SP_ITEMS[3][22] = {
    /* Championship: up -> Universe, down -> Survival */
    {0xFF,0xB0,0xFF,0xDD,0x04,0x00, 2,1,0,0, 0x85,0x09, 0xF6,0x97, 0x05,0x00, 0xFF,0xFF,0x04,0x0C,0x08,0x00},
    /* Survival: up -> Championship, down -> Universe */
    {0xFF,0xB0,0xFF,0xEE,0x04,0x00, 0,2,1,1, 0x85,0x0A, 0xF6,0x98, 0x06,0x00, 0xFF,0xFF,0x04,0x03,0x08,0x00},
    /* Universe Mode: label id 0x850E ("Records", unused by any menu) is
     * redirected to the Universe Mode label by the resident asset hook.
     * Still opens Survival for now. */
    {0xFF,0xB0,0xFF,0xFF,0x04,0x00, 1,0,2,2, 0x85,0x0E, 0xF6,0x98, 0x06,0x00, 0xFF,0xFF,0x04,0x03,0x08,0x00},
};

static void install_menu(void)
{
    volatile uint8_t *dst = (volatile uint8_t *)(0xA0000000u | (SP_ITEMS_ADDR & 0x1FFFFFFFu));
    unsigned i;
    for (i = 0; i < sizeof SP_ITEMS; i++) dst[i] = ((const uint8_t *)SP_ITEMS)[i];
}

static void remove_hook(void)
{
    volatile uint32_t *hook = (volatile uint32_t *)(0xA0000000u | (HOOK_ADDR & 0x1FFFFFFFu));
    volatile uint32_t *ahook = (volatile uint32_t *)(0xA0000000u | (ASSET_HOOK_ADDR & 0x1FFFFFFFu));
    hook[0] = ORIG_INSN_0;
    hook[1] = ORIG_INSN_1;
    volatile uint32_t *shook = (volatile uint32_t *)(0xA0000000u | (SONG_HOOK_ADDR & 0x1FFFFFFFu));
    ahook[0] = ASSET_ORIG_0;
    ahook[1] = ASSET_ORIG_1;
    volatile uint32_t *thook = (volatile uint32_t *)(0xA0000000u | (TEXT_HOOK_ADDR & 0x1FFFFFFFu));
    shook[0] = SONG_ORIG_0;
    shook[1] = SONG_ORIG_1;
    thook[0] = TEXT_ORIG_0;
    thook[1] = TEXT_ORIG_1;
    volatile uint32_t *dhook = (volatile uint32_t *)(0xA0000000u | (DLEND_HOOK_ADDR & 0x1FFFFFFFu));
    dhook[0] = DLEND_ORIG_0;
    dhook[1] = DLEND_ORIG_1;
    *(volatile uint32_t *)(0xA0000000u | (FORMAT_CALL_ADDR & 0x1FFFFFFFu)) = FORMAT_CALL_ORIG;
}

void n64_main(void)
{
    if (mem_size() >= 0x800000u && resident_image_size <= RESIDENT_MAX)
    {
        install_resident();
        install_menu();
        install_arenas();
    } else
        remove_hook();           /* no Expansion Pak: play the unmodified game */

    handoff_to_game();
}
