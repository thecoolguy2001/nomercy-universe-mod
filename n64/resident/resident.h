/* Resident Universe module: lives in Expansion Pak RAM while No Mercy runs. */
#ifndef RESIDENT_H
#define RESIDENT_H
#include <stdint.h>

#define RESIDENT_BASE   0x80410000u
#define RESIDENT_MAX    0x00070000u   /* image must end before 0x80480000 */
#define MAILBOX_ADDR    0x80408000u   /* status block readable from the debugger */
#define MAILBOX_MAGIC   0x4E4D5552u   /* 'NMUR' */

/* libultra OSContPad */
typedef struct {
    uint16_t button;
    int8_t   stick_x;
    int8_t   stick_y;
    uint8_t  errnum;
    uint8_t  pad;
} OSContPad;

typedef struct {
    uint32_t magic;
    uint32_t calls;        /* osContGetReadData calls since boot */
    uint32_t first_count;  /* CP0 Count at the first call */
    uint32_t last_count;   /* CP0 Count at the latest call */
    uint16_t last_buttons; /* controller 1 as read from the PIF */
    uint16_t pad0;
    uint32_t reserved[10];
} Mailbox;

#endif
