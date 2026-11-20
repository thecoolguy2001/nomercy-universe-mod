/*
 * Resident Universe module (stage 1: hook proof).
 *
 * Replaces libultra's osContGetReadData (0x800346A8 in No Mercy US 1.0),
 * which the game calls once per controller update from 0x800058A8. We do
 * exactly what the original did, then run Universe code with the pads.
 */
#include "resident.h"

#define PIF_RAM   ((const volatile uint8_t *)0x800A0200u) /* __osContPifRam */
#define MAX_CONT  (*(const volatile uint8_t *)0x800A2614u) /* __osMaxControllers */
#define MAILBOX   ((volatile Mailbox *)MAILBOX_ADDR)

static uint32_t cp0_count(void)
{
    uint32_t c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}

static void universe_frame(OSContPad *pads)
{
    volatile Mailbox *mb = MAILBOX;
    uint32_t now = cp0_count();
    if (mb->magic != MAILBOX_MAGIC) {
        mb->magic = MAILBOX_MAGIC;
        mb->calls = 0;
        mb->first_count = now;
    }
    mb->calls++;
    mb->last_count = now;
    mb->last_buttons = pads[0].button;
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
