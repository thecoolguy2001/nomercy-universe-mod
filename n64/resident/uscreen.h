#ifndef USCREEN_H
#define USCREEN_H
#include <stdint.h>
void us_enter(uint32_t seed);          /* create/continue the universe, book a card */
int  us_input(uint16_t pressed);       /* 0 = leave, 1 = stay, 2 = play/watch the pending match */
void us_draw(void *holder);
void us_cover(int on);                 /* 1: cover the whole screen (menu being steered) */
const char *us_pending_name(int side); /* game name of a side of the match to play */
int  us_pending_watch(void);           /* 1 = Watch (CPU vs CPU), 0 = Play */
/* What to set up in the game: No Mercy wrestler numbers of the two sides,
 * the belt on the line (-1 = none) and its current champion (0 = vacant). */
void us_pending_setup(uint8_t game_wrestler[2], int *belt, uint8_t *champion);
/* game_n: No Mercy wrestler numbers that actually wrestled (0 = unknown).
 * Called again after a rematch, replacing the result. */
void us_match_result(int winner_side, int loser_code, const uint8_t game_n[2]);
void us_match_closed(void);            /* back on the menus: match finished with */
#endif
