#ifndef USCREEN_H
#define USCREEN_H
#include <stdint.h>
void us_enter(uint32_t seed);          /* create/continue the universe, book a card */
int  us_input(uint16_t pressed);       /* 0 = leave, 1 = stay, 2 = play/watch the pending match */
void us_draw(void *holder);            /* draw into the menu's display list */
const char *us_pending_name(int side); /* game name of a side of the match to play */
int  us_pending_watch(void);           /* 1 = Watch (CPU vs CPU), 0 = Play */
void us_match_result(int winner_side, int loser_code);
#endif
