#include "common.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===== VALIDATION TASK ===== */
static const Position DIRECTIONS[NUM_DIRECTIONS] = {{-1, 0}, {-1, 1}, {0, 1},  {1, 1},
                                                    {1, 0},  {1, -1}, {0, -1}, {-1, -1}};

// ===== CAPTURE TASK =====
/* Opponent discs that would flip from pos along dir (0 if dir is not a capture
 * line). */
int count_flips(const GameState *s, PlayerColor player, Position pos, Position dir)
{
    const CellState me = (CellState)player;
    const CellState opp = (CellState)opponent_of(player);
    int n = 0;
    Position p = {pos.row + dir.row, pos.col + dir.col};
    while (pos_in_bounds(p) && s->board[p.row][p.col] == opp) {
        n++;
        p.row += dir.row;
        p.col += dir.col;
    }
    return (n > 0 && pos_in_bounds(p) && s->board[p.row][p.col] == me) ? n : 0;
}

// ===== VALIDATION TASK =====
/* Precondition: pos is on the board. */
MoveError check_move(const GameState *s, PlayerColor player, Position pos)
{
    assert(s != NULL);
    assert(player == PLAYER_BLACK || player == PLAYER_WHITE);
    assert(pos_in_bounds(pos));

    if (s->board[pos.row][pos.col] != CELL_EMPTY) return MOVE_ERR_OCCUPIED;
    for (int d = 0; d < NUM_DIRECTIONS; d++) {
        if (count_flips(s, player, pos, DIRECTIONS[d]) > 0) return MOVE_OK;
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
    }
    return "Unknown error";
}

bool validate_move(const GameState *s, PlayerColor player, Position pos)
{
    return check_move(s, player, pos) == MOVE_OK;
}

/* Fills every cell of valid_out and returns the number of legal moves. */
int get_all_valid_moves(const GameState *s, PlayerColor player,
                        bool valid_out[BOARD_SIZE][BOARD_SIZE])
{
    assert(valid_out != NULL);
    int count = 0;
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            valid_out[r][c] = validate_move(s, player, (Position){r, c});
            count += valid_out[r][c];
        }
    }
    return count;
}

bool has_any_valid_move(const GameState *s, PlayerColor player)
{
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            if (validate_move(s, player, (Position){r, c})) return true;
        }
    }
    return false;
}

/* Derived from the board: nobody can move. */
bool is_game_over(const GameState *s)
{
    return !has_any_valid_move(s, PLAYER_BLACK) && !has_any_valid_move(s, PLAYER_WHITE);
}

void init_game(GameState *s, const GameMeta *meta)
{
    enum { MID = BOARD_SIZE / 2 };
    memset(s, 0, sizeof *s);
    s->board[MID - 1][MID - 1] = CELL_WHITE; /* D4 */
    s->board[MID - 1][MID] = CELL_BLACK;     /* E4 */
    s->board[MID][MID - 1] = CELL_BLACK;     /* D5 */
    s->board[MID][MID] = CELL_WHITE;         /* E5 */
    s->current_turn = PLAYER_BLACK;
    s->meta = *meta;
}

// ===== CAPTURE TASK =====
/* Precondition: check_move(s, current_turn, pos) == MOVE_OK. Does not change
 * the turn. */
int execute_move(GameState *s, Position pos)
{
    assert(check_move(s, s->current_turn, pos) == MOVE_OK);
    assert(s->move_count < MAX_MOVES);

    const PlayerColor me = s->current_turn;
    int total = 0;
    for (int d = 0; d < NUM_DIRECTIONS; d++) {
        const int n = count_flips(s, me, pos, DIRECTIONS[d]);
        Position p = pos;
        for (int k = 0; k < n; k++) {
            p.row += DIRECTIONS[d].row;
            p.col += DIRECTIONS[d].col;
            s->board[p.row][p.col] = (CellState)me;
        }
        total += n;
    }
    s->board[pos.row][pos.col] = (CellState)me;
    s->history[s->move_count] = (MoveRecord){me, pos};
    s->move_count++;
    return total;
}

// ===== TURN / GAME STATE TASK =====
/* Gives the turn to the opponent if they can move, otherwise keeps it if the
 * mover can still move. */
TurnStatus switch_turn(GameState *s)
{
    const PlayerColor opp = opponent_of(s->current_turn);
    if (has_any_valid_move(s, opp)) {
        s->current_turn = opp;
        return TURN_SWITCHED;
    }
    if (has_any_valid_move(s, s->current_turn)) return TURN_OPPONENT_PASSED;
    return TURN_GAME_OVER;
}

void count_pieces(const GameState *s, int *black, int *white)
{
    *black = 0;
    *white = 0;
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            if (s->board[r][c] == CELL_BLACK)
                (*black)++;
            else if (s->board[r][c] == CELL_WHITE)
                (*white)++;
        }
    }
}

GameResult get_game_result(const GameState *s)
{
    if (!is_game_over(s)) return RESULT_IN_PROGRESS;
    int b, w;
    count_pieces(s, &b, &w);
    if (b > w) return RESULT_BLACK_WIN;
    if (w > b) return RESULT_WHITE_WIN;
    return RESULT_TIE;
}

/* Replays the first `step` moves of src into out. Fails if any recorded move is
 * illegal. */
bool rebuild_state_at(const GameState *src, int step, GameState *out)
{
    assert(src != out);
    if (step < 0 || step > src->move_count) return false;

    init_game(out, &src->meta);
    for (int i = 0; i < step; i++) {
        const MoveRecord rec = src->history[i];
        if (is_game_over(out) || rec.player != out->current_turn) return false;
        if (!pos_in_bounds(rec.pos) || check_move(out, rec.player, rec.pos) != MOVE_OK)
            return false;
        execute_move(out, rec.pos);
        switch_turn(out);
    }
    return true;
}
