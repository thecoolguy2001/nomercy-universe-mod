/* Resident Universe module: lives in Expansion Pak RAM while No Mercy runs. */
#ifndef RESIDENT_H
#define RESIDENT_H
#include <stdint.h>

#define RESIDENT_BASE   0x80410000u
#define RESIDENT_MAX    0x00070000u   /* image must end before 0x80480000 */
#define MAILBOX_ADDR    0x80408000u   /* status/command block (debugger + Universe UI) */
#define MAILBOX_MAGIC   0x4E4D5552u   /* 'NMUR' */

/* libultra OSContPad */
typedef struct {
    uint16_t button;
    int8_t   stick_x;
    int8_t   stick_y;
    uint8_t  errnum;
    uint8_t  pad;
} OSContPad;

/* Match director commands / states */
enum { CMD_NONE = 0, CMD_WATCH = 1, CMD_PLAY = 2 };
enum { ST_IDLE = 0, ST_SETUP = 1, ST_IN_MATCH = 2, ST_DONE = 3 };

typedef struct {
    uint32_t magic;        /* 0x00 */
    uint32_t calls;        /* 0x04 osContGetReadData calls since boot */
    uint32_t first_count;  /* 0x08 CP0 Count at the first call */
    uint32_t last_count;   /* 0x0C CP0 Count at the latest call */
    uint16_t last_buttons; /* 0x10 controller 1 as read from the PIF */
    uint16_t pad0;
    /* match director: request (written by caller) */
    uint8_t  cmd;          /* 0x14 CMD_*; cleared when accepted */
    uint8_t  p1;           /* 0x15 side 1 wrestler (Superstar Select grid index) */
    uint8_t  p2;           /* 0x16 side 2 wrestler */
    uint8_t  pad1;
    /* match director: status / result (written by the module) */
    uint8_t  state;        /* 0x18 ST_* */
    uint8_t  winner_side;  /* 0x19 0 or 1 (0xFF until known) */
    uint8_t  loser_code;   /* 0x1A loser's result byte (1/3/...) */
    uint8_t  step;         /* 0x1B current macro step */
    uint16_t clock_min;    /* 0x1C */
    uint16_t clock_sec;    /* 0x1E */
    uint32_t song_request;   /* 0x20 (0x100 | song) -> play_song(song); cleared when done */
    uint32_t music_override; /* 0x24 song that replaces menu music (0 = off); read by the asm hook */
    uint32_t test_select;    /* 0x28 test: 1 = press A on the Universe item (bypassing Universe Mode) */
    uint32_t test_seed;      /* 0x2C test: non-zero = seed for a new universe (repeatable cards) */
    uint32_t test_stip;      /* 0x30 test: 1 + U_STIP_* given to every match of new cards that allows it */
    uint32_t test_time;      /* 0x34 test: 1 + RULES time limit (0 5 min .. 5 none) for every match */
    uint32_t test_rumble;    /* 0x38 test: Royal Rumbles of new cards cut to this many entrants */
    uint32_t reserved[1];
} Mailbox;

#endif
