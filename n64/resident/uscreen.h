#ifndef USCREEN_H
#define USCREEN_H
#include <stdint.h>
void us_enter(uint32_t seed);          /* create/continue the universe, book a card */
int  us_input(uint16_t pressed);       /* 0 = leave, 1 = stay, 2 = play/watch the pending match */
void us_draw(void *holder);
int  us_show_arena(void);              /* No Mercy arena of the current show */
void us_draw_cover(void *holder);      /* full-screen cover, at the end of the frame */
void us_cover(int on);                 /* 1: cover the whole screen (menu being steered) */
int  us_pending_watch(void);           /* 1 = Watch (CPU vs CPU), 0 = Play */
/* What to set up in the game for the match to play */
typedef struct {
    uint8_t n_slots;        /* wrestlers picked on Superstar Select */
    uint8_t match_item;     /* Exhibition MATCH tab item */
    uint8_t wrestler[4];    /* No Mercy wrestler number per slot */
    int     belt;           /* game belt on the line, -1 = none */
    uint8_t champion[2];    /* its champion's wrestler number(s), tag: both (0 = vacant) */
} USetup;
void us_pending_setup(USetup *st);
/* game_n: No Mercy wrestler numbers that actually wrestled (0 = unknown).
 * Called again after a rematch, replacing the result. */
void us_match_result(const uint8_t result[4], const uint8_t game_n[4]);
void us_match_closed(void);            /* back on the menus: match finished with */
#endif
