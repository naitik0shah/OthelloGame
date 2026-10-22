#ifndef VALIDATION_H
#define VALIDATION_H

#include "common.h"

/* Pure functions: read state->board only, never modify state.
   (Takes const GameState* instead of const CellState[8][8]: in C17, passing
   CellState(*)[8] to const CellState(*)[8] triggers incompatible-pointer warnings.) */

/* true iff pos is in bounds, EMPTY, and flips >= 1 opponent piece in >= 1 direction */
bool validate_move(const GameState *state, PlayerColor player, Position pos);

/* Fills valid_out (all 64 cells, false where illegal); returns legal-move count */
int  get_all_valid_moves(const GameState *state, PlayerColor player,
                         bool valid_out[BOARD_SIZE][BOARD_SIZE]);

/* true iff player has at least one legal move */
bool has_any_valid_move(const GameState *state, PlayerColor player);

#endif /* VALIDATION_H */
