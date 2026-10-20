#ifndef CAPTURE_H
#define CAPTURE_H

#include "common.h"

// Flips opposing pieces in all legal directions after a valid move placement
// Returns total number of opponent pieces flipped
int execute_move(GameState *state, Position pos);

// Evaluates piece counts and checks game over condition
void check_game_over(GameState *state);

// Switches player turn and updates valid move hints
void switch_turn(GameState *state);

#endif // CAPTURE_H