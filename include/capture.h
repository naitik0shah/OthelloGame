#ifndef CAPTURE_H
#define CAPTURE_H

#include "common.h"

/* Reset to the standard start: D4=W, E4=B, D5=B, E5=W, Black to move, empty history.
   Copies meta, then computes valid_moves. */
void init_game(GameState *state, const GameMeta *meta);

/* PRECONDITION: validate_move(state, state->current_turn, pos) is true.
   Places the piece, flips captured pieces in all 8 directions, appends to history.
   Does NOT switch turn. Returns number of flipped pieces. */
int  execute_move(GameState *state, Position pos);

/* Board full or neither player can move -> sets state->game_ended; returns it */
bool check_game_over(GameState *state);

/* Hands the turn over and refreshes valid_moves:
   - opponent has a move        -> TURN_SWITCHED
   - only current player can go -> turn stays, TURN_OPPONENT_PASSED
   - nobody can move            -> game_ended = true, TURN_GAME_OVER */
TurnStatus switch_turn(GameState *state);

/* Piece counts */
void count_pieces(const GameState *state, int *black, int *white);

/* RESULT_IN_PROGRESS until game_ended; then compare counts (equal -> RESULT_TIE) */
GameResult get_game_result(const GameState *state);

/* Rebuild a state by replaying the first `step` moves of src->history
   (0 <= step <= src->move_count). Used by load_game and replay mode.
   Returns false if a recorded move is illegal. */
bool rebuild_state_at(const GameState *src, int step, GameState *out);

#endif /* CAPTURE_H */
