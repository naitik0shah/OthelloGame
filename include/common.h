#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_SIZE 8
#define MAX_MOVES 60
#define MAX_NAME_LEN 32
#define MAX_PATH_LEN 256

// State of a single cell on the 8x8 board
typedef enum {
    CELL_EMPTY = 0,
    CELL_BLACK = 1,
    CELL_WHITE = 2
} CellState;

// Player color assignment
typedef enum {
    PLAYER_BLACK = 1,
    PLAYER_WHITE = 2
} PlayerColor;

// Board coordinate (0-indexed: row 0..7, col 0..7)
typedef struct {
    int row;
    int col;
} Position;

// Single recorded move item
typedef struct {
    PlayerColor player;
    Position pos;
} MoveRecord;

// Game Metadata
typedef struct {
    char player1_name[MAX_NAME_LEN];
    char player2_name[MAX_NAME_LEN];
    PlayerColor p1_color;
    PlayerColor p2_color;
} GameMeta;

// Complete Game State container
typedef struct {
    CellState board[BOARD_SIZE][BOARD_SIZE];
    bool valid_moves[BOARD_SIZE][BOARD_SIZE];
    PlayerColor current_turn;
    bool game_ended;
    MoveRecord history[MAX_MOVES];
    int move_count;
    GameMeta meta;
} GameState;

// Aggregated game statistics summary
typedef struct {
    int total_games;
    int black_wins;
    int white_wins;
    int ties;
    char highest_scoring_player[MAX_NAME_LEN];
    int max_score;
} StatsSummary;

#endif // COMMON_H