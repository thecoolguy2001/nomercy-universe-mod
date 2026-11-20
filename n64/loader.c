/*
 * Boot payload entry: installs the resident Universe module into Expansion
 * Pak RAM, then starts No Mercy. Holding Z at power-on shows the old
 * boot-time Universe screen first.
 */
#include "hw.h"
#include "resident/resident.h"

extern const uint8_t resident_image[];
extern const uint32_t resident_image_size;
void boot_ui_main(void);

/* osContGetReadData in RAM; the ROM has "j 0x80410000; nop" here. */
#define HOOK_ADDR       0x800346A8u
#define ORIG_INSN_0     0x27BDFFF0u  /* addiu sp, sp, -0x10 */
#define ORIG_INSN_1     0x00804821u  /* move  t1, a0 */

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
    /* Universe Mode (TEST: Championship label, opens Survival for now) */
    {0xFF,0xB0,0xFF,0xFF,0x04,0x00, 1,0,2,2, 0x85,0x09, 0xF6,0x98, 0x06,0x00, 0xFF,0xFF,0x04,0x03,0x08,0x00},
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
    hook[0] = ORIG_INSN_0;
    hook[1] = ORIG_INSN_1;
}

void n64_main(void)
{
    if (mem_size() >= 0x800000u && resident_image_size <= RESIDENT_MAX)
    {
        install_resident();
        install_menu();
    } else
        remove_hook();           /* no Expansion Pak: play the unmodified game */

    if (pad_read() & BTN_Z) {
        vi_init();
        boot_ui_main();
    }
    handoff_to_game();
}
