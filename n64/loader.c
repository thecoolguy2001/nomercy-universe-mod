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

static void remove_hook(void)
{
    volatile uint32_t *hook = (volatile uint32_t *)(0xA0000000u | (HOOK_ADDR & 0x1FFFFFFFu));
    hook[0] = ORIG_INSN_0;
    hook[1] = ORIG_INSN_1;
}

void n64_main(void)
{
    if (mem_size() >= 0x800000u && resident_image_size <= RESIDENT_MAX)
        install_resident();
    else
        remove_hook();           /* no Expansion Pak: play the unmodified game */

    if (pad_read() & BTN_Z) {
        vi_init();
        boot_ui_main();
    }
    handoff_to_game();
}
