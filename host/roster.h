/* The full Universe roster: every No Mercy wrestler (plan step 5). */
#ifndef ROSTER_H
#define ROSTER_H
#include "../engine/universe.h"

/* A new universe with the whole game roster, the 7 belts (in No Mercy's
 * belt order: title index = game belt) and the starting champions. */
void roster_universe(Universe *u, uint32_t seed);

#endif
