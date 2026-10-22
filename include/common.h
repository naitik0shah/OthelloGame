#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>

#define BOARD_SIZE     8
#define MAX_MOVES      60   /* 64 squares - 4 initial pieces; passes are NOT recorded */
#define MAX_NAME_LEN   32   /* 31 chars + '\0' */
#define MAX_PATH_LEN   256
#define NUM_DIRECTIONS 8

/* State of one board cell */
typedef enum { CELL_EMPTY = 0, CELL_BLACK = 1, CELL_WHITE = 2 } CellState;

/* Player colour. Values match CellState so (CellState)player is valid. */
typedef enum { PLAYER_BLACK = 1, PLAYER_WHITE = 2 } PlayerColor;

/* 0-indexed board coordinate. "a1" -> {row 0, col 0}, "e3" -> {row 2, col 4} */
typedef struct { int row; int col; } Position;

/* One recorded move (passes are not recorded; a pass is implied when two
   consecutive records have the same player). */
typedef struct { PlayerColor player; Position pos; } MoveRecord;

/* Game metadata. Invariant: p1_color != p2_color. */
typedef struct {
    char        player1_name[MAX_NAME_LEN];
    char        player2_name[MAX_NAME_LEN];
    PlayerColor p1_color;
    PlayerColor p2_color;
} GameMeta;

/* Complete game state */
typedef struct {
    CellState   board[BOARD_SIZE][BOARD_SIZE];
    bool        valid_moves[BOARD_SIZE][BOARD_SIZE]; /* derived; for current_turn; never saved */
    PlayerColor current_turn;
    bool        game_ended;
    MoveRecord  history[MAX_MOVES];
    int         move_count;                          /* 0..MAX_MOVES */
    GameMeta    meta;
} GameState;

/* Outcome of a finished (or running) game */
typedef enum {
    RESULT_IN_PROGRESS = 0,
    RESULT_BLACK_WIN,
    RESULT_WHITE_WIN,
    RESULT_TIE
} GameResult;

/* What happened when the turn was handed over (see switch_turn) */
typedef enum {
    TURN_SWITCHED = 0,      /* opponent moves next */
    TURN_OPPONENT_PASSED,   /* opponent had no move; same player moves again */
    TURN_GAME_OVER          /* nobody can move / board full */
} TurnStatus;

/* Top-level application mode */
typedef enum { APP_MODE_MENU = 0, APP_MODE_PLAY, APP_MODE_VIEW } AppMode;

/* Aggregated statistics over several saved games */
typedef struct {
    int  total_games;                       /* finished games counted */
    int  skipped_files;                     /* unreadable / corrupt / unfinished */
    int  black_wins;
    int  white_wins;
    int  ties;
    char highest_scoring_player[MAX_NAME_LEN];
    int  max_score;                         /* highest final piece count of any player */
} StatsSummary;

/* Shared direction table: N, NE, E, SE, S, SW, W, NW (row delta, col delta) */
static const Position DIRECTIONS[NUM_DIRECTIONS] = {
    {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}
};

static inline bool pos_in_bounds(Position p) {
    return p.row >= 0 && p.row < BOARD_SIZE && p.col >= 0 && p.col < BOARD_SIZE;
}
static inline PlayerColor opponent_of(PlayerColor p) {
    return (p == PLAYER_BLACK) ? PLAYER_WHITE : PLAYER_BLACK;
}
static inline CellState cell_of(PlayerColor p) { return (CellState)p; }

#endif /* COMMON_H */
