#ifndef USCREEN_H
#define USCREEN_H
#include <stdint.h>
void us_enter(uint32_t seed);          /* create/continue the universe, book a card */
void us_test_stip(uint32_t stip);      /* test: 1 + U_STIP_* forced on new cards (0 = off) */
void us_test_rumble(uint32_t n);       /* test: Royal Rumbles cut to n entrants (0 = off) */
int  us_input(uint16_t pressed);       /* 0 = leave, 1 = stay, 2 = play/watch the pending match */
void us_draw(void *holder);
int  us_show_arena(void);              /* No Mercy arena of the current show */
void us_draw_cover(void *holder);      /* full-screen cover, at the end of the frame */
void us_cover(int on);                 /* 1: cover the whole screen (menu being steered) */
int  us_pending_watch(void);           /* 1 = Watch (CPU vs CPU), 0 = Play */

#define US_RULES 12
#define US_RUMBLE_MAX 40
/* What to set up in the game for the match to play */
typedef struct {
    uint8_t mode;           /* game mode the Universe item opens (Exhibition, Ladder, ...) */
    uint8_t match_item;     /* MATCH tab item */
    uint8_t player;         /* PLAYER tab value (Play or Watch) */
    uint8_t n_slots;        /* picks on Superstar Select (a guest referee is the first) */
    uint8_t tag;            /* tag partners are picked back to back (slots 1 and 3) */
    uint8_t wrestler[4];    /* No Mercy wrestler number per slot */
    uint8_t attire[4];      /* and attire ("Type"): some numbers hold two characters */
    uint8_t human[4];       /* per slot: 1 = the player's controller, 0 = CPU */
    uint8_t rules[US_RULES];/* the RULES tab row the match is played with */
    int     belt;           /* game belt on the line, -1 = none */
    uint8_t champion[2];    /* its champion's wrestler number(s), tag: both (0 = vacant) */
    uint8_t preset_each;    /* the cursor is moved for every pick (same picker back to back) */
    uint8_t rumble_n;       /* Royal Rumble: entrants (4-40), 0 = not a Rumble */
    uint8_t rumble[US_RUMBLE_MAX]; /* their wrestler numbers in entry order */
    /* The story written into the game for this match (its ALLY/ENEMY data,
     * restored afterwards): per slot whose bit is set in rel_set, Rival 1-3
     * (hottest first) and Accompanied by, as wrestler numbers (0 = none) */
    uint8_t rel_set;
    uint8_t rel[4][4];
} USetup;
void us_pending_setup(USetup *st);

/* How the game's match ended */
typedef struct {
    uint8_t rumble_winner;  /* Royal Rumble: the winner's wrestler number (0 = unknown) */
    uint8_t result[4];      /* game result byte per slot: 4 = won */
    uint8_t game_n[4];      /* No Mercy wrestler numbers that actually wrestled (0 = unknown) */
    uint8_t game_attire[4]; /* their attires */
    uint8_t method;         /* U_WIN_* read from the game */
    uint8_t falls[4];       /* Ironman: falls per slot */
} UGameResult;
/* Called again after a rematch, replacing the result. */
void us_match_result(const UGameResult *r);
void us_match_closed(void);            /* back on the menus: match finished with */
int  game_pickable(uint8_t number);    /* (resident) Superstar Select can pick him */
#endif
