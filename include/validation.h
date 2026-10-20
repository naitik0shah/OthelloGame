#ifndef MOVE_RULES_H
#define MOVE_RULES_H

#include "common.h"

// Returns true if placing piece by `player` at `pos` is valid according to Othello rules
bool validate_move(const CellState board[BOARD_SIZE][BOARD_SIZE], PlayerColor player, Position pos);

// Populates the 8x8 boolean matrix `valid_out` with legal moves and returns valid move count
int get_all_valid_moves(const CellState board[BOARD_SIZE][BOARD_SIZE], PlayerColor player, bool valid_out[BOARD_SIZE][BOARD_SIZE]);

// Check if current player has any valid moves available
bool has_any_valid_move(const CellState board[BOARD_SIZE][BOARD_SIZE], PlayerColor player);

#endif // MOVE_RULES_H