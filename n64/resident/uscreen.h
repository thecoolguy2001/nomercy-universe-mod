#ifndef USCREEN_H
#define USCREEN_H
#include <stdint.h>
void us_enter(uint32_t seed);          /* create/continue the universe, book a card */
int  us_input(uint16_t pressed);       /* returns 0 when the player leaves */
void us_draw(void *holder);            /* draw into the menu's display list */
#endif
