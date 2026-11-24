#include "common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Validation and capture rules for Othello.
 *
 * The functions in this file do not depend on the terminal UI or storage
 * modules.  Invalid input is handled safely; valid game behaviour is unchanged.
 */

static const Position DIRECTIONS[NUM_DIRECTIONS] = {{-1, 0}, {-1, 1}, {0, 1},  {1, 1},
                                                    {1, 0},  {1, -1}, {0, -1}, {-1, -1}};

static bool valid_player(PlayerColor player)
{
    return player == PLAYER_BLACK || player == PLAYER_WHITE;
}

static bool valid_direction(Position dir)
{
    return dir.row >= -1 && dir.row <= 1 && dir.col >= -1 && dir.col <= 1 &&
           (dir.row != 0 || dir.col != 0);
}

/* Return the number of opponent pieces bracketed in one direction. */
int count_flips(const GameState *s, PlayerColor player, Position pos, Position dir)
{
    if (s == NULL || !valid_player(player) || !pos_in_bounds(pos) || !valid_direction(dir)) {
        return 0;
    }

    const CellState opponent = (CellState)opponent_of(player);
    Position current = {pos.row + dir.row, pos.col + dir.col};
    int flips = 0;

    while (pos_in_bounds(current) && s->board[current.row][current.col] == opponent) {
        flips++;
        current.row += dir.row;
        current.col += dir.col;
    }

    if (flips == 0 || !pos_in_bounds(current)) {
        return 0;
    }

    return s->board[current.row][current.col] == (CellState)player ? flips : 0;
}

/* Explain why a proposed move is invalid, or return MOVE_OK. */
MoveError check_move(const GameState *s, PlayerColor player, Position pos)
{
    if (s == NULL || !valid_player(player) || !pos_in_bounds(pos)) {
        return MOVE_ERR_NO_CAPTURE;
    }

    if (s->board[pos.row][pos.col] != CELL_EMPTY) {
        return MOVE_ERR_OCCUPIED;
    }

    for (int direction = 0; direction < NUM_DIRECTIONS; direction++) {
        if (count_flips(s, player, pos, DIRECTIONS[direction]) > 0) {
            return MOVE_OK;
        }
    }

    return MOVE_ERR_NO_CAPTURE;
}

const char *move_error_str(MoveError err)
{
    switch (err) {
    case MOVE_OK:
        return "Move is valid";
    case MOVE_ERR_OCCUPIED:
        return "Square is already occupied";
    case MOVE_ERR_NO_CAPTURE:
        return "Move must flip at least one opponent piece";
    default:
        return "Unknown error";
    }
}

bool validate_move(const GameState *s, PlayerColor player, Position pos)
{
    return check_move(s, player, pos) == MOVE_OK;
}

/* Fill the complete output grid, including false values for illegal squares. */
int get_all_valid_moves(const GameState *s, PlayerColor player,
                        bool valid_out[BOARD_SIZE][BOARD_SIZE])
{
    if (valid_out == NULL) {
        return 0;
    }

    int move_total = 0;

    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            const bool is_valid = validate_move(s, player, (Position){row, col});
            valid_out[row][col] = is_valid;
            if (is_valid) {
                move_total++;
            }
        }
    }

    return move_total;
}

bool has_any_valid_move(const GameState *s, PlayerColor player)
{
    if (s == NULL || !valid_player(player)) {
        return false;
    }

    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            if (validate_move(s, player, (Position){row, col})) {
                return true;
            }
        }
    }

    return false;
}

bool is_game_over(const GameState *s)
{
    if (s == NULL) {
        return true;
    }

    return !has_any_valid_move(s, PLAYER_BLACK) && !has_any_valid_move(s, PLAYER_WHITE);
}

void init_game(GameState *s, const GameMeta *meta)
{
    if (s == NULL) {
        return;
    }

    /* Copy first: meta may point inside s (for example, &s->meta). */
    GameMeta safe_meta = {0};
    if (meta != NULL) {
        safe_meta = *meta;
    }

    /* A malformed color assignment must not enter a new game. */
    if (!valid_player(safe_meta.p1_color)) {
        safe_meta.p1_color = PLAYER_BLACK;
    }

    memset(s, 0, sizeof *s);

    const int middle = BOARD_SIZE / 2;
    s->board[middle - 1][middle - 1] = CELL_WHITE;
    s->board[middle - 1][middle] = CELL_BLACK;
    s->board[middle][middle - 1] = CELL_BLACK;
    s->board[middle][middle] = CELL_WHITE;
    s->current_turn = PLAYER_BLACK;

    s->meta = safe_meta;
}

/* Apply a legal move. The caller remains responsible for switching turns. */
int execute_move(GameState *s, Position pos)
{
    if (s == NULL || !valid_player(s->current_turn) || s->move_count < 0 ||
        s->move_count >= MAX_MOVES || check_move(s, s->current_turn, pos) != MOVE_OK) {
        return 0;
    }

    const PlayerColor player = s->current_turn;
    int total_flips = 0;

    for (int direction = 0; direction < NUM_DIRECTIONS; direction++) {
        const Position step = DIRECTIONS[direction];
        const int flips = count_flips(s, player, pos, step);
        Position current = pos;

        for (int i = 0; i < flips; i++) {
            current.row += step.row;
            current.col += step.col;
            s->board[current.row][current.col] = (CellState)player;
        }

        total_flips += flips;
    }

    s->board[pos.row][pos.col] = (CellState)player;
    s->history[s->move_count] = (MoveRecord){player, pos};
    s->move_count++;

    return total_flips;
}

/* Switch to the opponent, keep the turn if the opponent must pass, or end. */
TurnStatus switch_turn(GameState *s)
{
    if (s == NULL || !valid_player(s->current_turn)) {
        return TURN_GAME_OVER;
    }

    const PlayerColor opponent = opponent_of(s->current_turn);

    if (has_any_valid_move(s, opponent)) {
        s->current_turn = opponent;
        return TURN_SWITCHED;
    }

    if (has_any_valid_move(s, s->current_turn)) {
        return TURN_OPPONENT_PASSED;
    }

    return TURN_GAME_OVER;
}

void count_pieces(const GameState *s, int *black, int *white)
{
    int black_total = 0;
    int white_total = 0;

    if (s != NULL) {
        for (int row = 0; row < BOARD_SIZE; row++) {
            for (int col = 0; col < BOARD_SIZE; col++) {
                if (s->board[row][col] == CELL_BLACK) {
                    black_total++;
                } else if (s->board[row][col] == CELL_WHITE) {
                    white_total++;
                }
            }
        }
    }

    if (black != NULL) {
        *black = black_total;
    }
    if (white != NULL) {
        *white = white_total;
    }
}

GameResult get_game_result(const GameState *s)
{
    if (s == NULL) {
        return ERR_RESULT_INVALID;
    }

    if (!is_game_over(s)) {
        return RESULT_IN_PROGRESS;
    }

    int black_total;
    int white_total;
    count_pieces(s, &black_total, &white_total);

    if (black_total > white_total) {
        return RESULT_BLACK_WIN;
    }
    if (white_total > black_total) {
        return RESULT_WHITE_WIN;
    }
    return RESULT_TIE;
}

/* Recreate the first 'step' moves without changing the original game. */
bool rebuild_state_at(const GameState *src, int step, GameState *out)
{
    if (src == NULL || out == NULL || src == out || src->move_count < 0 ||
        src->move_count > MAX_MOVES || step < 0 || step > src->move_count) {
        return false;
    }

    GameState rebuilt;
    init_game(&rebuilt, &src->meta);

    for (int i = 0; i < step; i++) {
        const MoveRecord move = src->history[i];

        if (is_game_over(&rebuilt) || move.player != rebuilt.current_turn ||
            !pos_in_bounds(move.pos) || check_move(&rebuilt, move.player, move.pos) != MOVE_OK) {
            return false;
        }

        if (execute_move(&rebuilt, move.pos) == 0) {
            return false;
        }

        switch_turn(&rebuilt);
    }

    /* Do not expose a half-rebuilt state when the history is invalid. */
    *out = rebuilt;
    return true;
}
