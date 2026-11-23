#ifndef USAVE_H
#define USAVE_H
#include <stdint.h>
/* The Universe save in the cartridge flash (sectors 6-7, see usave.c). */
int usave_load(uint8_t *data, int cap);        /* bytes read, -1 = none */
int usave_store(const uint8_t *data, int len); /* 0 = saved */
int flash_format_game(void);                   /* (No Mercy's format: sectors 0-5) */
#endif
