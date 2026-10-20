# Othello Architecture & Technical Specification

## 1. Core Data Models (`include/common.h`)

All core data structures and enums are centralized in `include/common.h` to ensure strict C17 type safety and clean modularity across all project files.

```c
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
```

---

## 2. Shared Module Function Interfaces

### Module 1: CLI Input & Command Parsing (`include/cli.h`)

*Assigned to: Akshar, Ojas*

```c
#ifndef CLI_H
#define CLI_H

#include "common.h"

// Parse raw string command inputs (:n, :l, :s, :v, :h, :q)
void cli_parse_command(const char *input, GameState *state);

// Prompt interactive player configuration if flags are omitted
void cli_prompt_new_game(GameMeta *meta);

// Display top header, help menu, and command list
void cli_render_help(void);

#endif // CLI_H
```

### Module 2: Move Validation & Hinting (`include/validation.h`)

*Assigned to: Aniruddh, Maulik, Naitik*

```c
#ifndef VALIDATION_H
#define VALIDATION_H

#include "common.h"

// Returns true if placing piece by `player` at `pos` is valid according to Othello rules
bool validate_move(const CellState board[BOARD_SIZE][BOARD_SIZE], PlayerColor player, Position pos);

// Populates the 8x8 boolean matrix `valid_out` with legal moves and returns valid move count
int get_all_valid_moves(const CellState board[BOARD_SIZE][BOARD_SIZE], PlayerColor player, bool valid_out[BOARD_SIZE][BOARD_SIZE]);

// Check if current player has any valid moves available
bool has_any_valid_move(const CellState board[BOARD_SIZE][BOARD_SIZE], PlayerColor player);

#endif // VALIDATION_H
```

### Module 3: Piece Capturing & State Mutations (`include/capture.h`)

*Assigned to: Aarav, Nishi, Prem*

```c
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
```

### Module 4: UI Engine & Board Rendering (`include/ui.h`)

*Assigned to: Shared / UI Integration*

```c
#ifndef UI_H
#define UI_H

#include "common.h"

// Redraws full terminal screen: Board + Move History side panel + Turn indicator
void ui_render_board(const GameState *state, Position current_cursor);

// Displays status bar messages (e.g., Red error text for invalid moves)
void ui_display_status(const char *message, bool is_error);

// Replay UI viewer mode navigation (/ for forward, \ for backward)
void ui_view_replay(const GameState *state);

#endif // UI_H
```

### Module 5: Game Persistence & Statistics (`include/storage.h`)

*Assigned to: Manthan, Yug*

```c
#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"

// Saves current game state to file path
bool save_game(const GameState *state, const char *filepath);

// Loads game state from file path into state pointer
bool load_game(GameState *state, const char *filepath);

// Parses multiple game save files and prints aggregated statistics summary table
void generate_stats_summary(const char * const filepaths[], int file_count);

#endif // STORAGE_H
```

---

## 3. System Data Flow Diagram

```text
                               +-----------------------+
                               |  User Input Terminal  |
                               +-----------------------+
                                           |
                            [Raw Input / Command String]
                                           v
                               +-----------------------+
                               |      CLI Module       |
                               | (Akshar, Ojas)        |
                               |    (cli_input.c)      |
                               +-----------------------+
                                   /               \
            [Command Mode: :n, :l, :s, :v]    [Move Input: e3 + Enter]
                         /                           \
                        v                             v
           +-------------------------+   +-------------------------+
           |     Storage Module      |   |    Validation Module    |
           |    (Manthan, Yug)       |   | (Aniruddh,Maulik,Naitik)|
           |   (save_load.c/stats.c) |   |   (move_validation.c)   |
           | - Save / Load State     |   | - Validate cell target  |
           | - Stats Aggregation     |   +-------------------------+
           +-------------------------+            /         \
                      |             [Invalid Move]           [Valid Move]
             [State Restored]             /                     \
                      |                  /                       v
                      |                 /            +-----------------------+
                      |                /             |    Capture Module     |
                      |               /              |  (Aarav, Nishi, Prem) |
                      |              /               |      (capture.c)      |
                      |             /                | - Execute Captures    |
                      |            /                 | - Record History      |
                      |           /                  | - Switch Turn         |
                      |          /                   +-----------------------+
                      |         /                               |
                      |        /                       [Turn Switched]
                      |       /                                 |
                      |      /                                  v
                      |     /                        +-----------------------+
                      |    /                         |   Validation Module   |
                      |   /                          |  (move_validation.c)  |
                      |  /                           | - Compute Valid Moves |
                      | /                            |   Matrix (* hints)    |
                      |/                             +-----------------------+
                      v                                          |
       +-------------------------------------------------------------------+
       |                        UI Renderer Module                         |
       |                       (cli_render.c / ui.h)                       |
       |  - Draw 8x8 Board & Legal Hints (*)                               |
       |  - Draw Move History Panel, Turn Info & Status/Error Messages     |
       +-------------------------------------------------------------------+
```

---

## 4. File Structure

```text
OthelloGame/
├── .gitignore                 # Git ignore rules for build outputs and temporary files
├── ARCHITECTURE.MD            # Data structures, module APIs, and system specification
├── CMakeLists.txt             # Modern CMake cross-platform build script
├── Makefile                   # GNU Make build script for GCC / Clang / MinGW
├── PRD.md                     # Product Requirements Document
├── README.MD                  # Project setup, requirements, and controls guide
├── TASKS.MD                   # Work breakdown and task list per team member
│
├── include/                   # Public header declarations
│   ├── capture.h              # Module 3: Piece flipping logic & state transitions
│   ├── cli.h                  # Module 1: CLI commands & input parsing interface
│   ├── common.h               # Central data models, enums, constants, and GameState struct
│   ├── storage.h              # Module 5: Save/load files & statistics summary interface
│   ├── ui.h                   # Module 4: Screen rendering & replay viewer interface
│   └── validation.h           # Module 2: Move validation & legal move generation interface
│
├── src/                       # Module implementations (.c source files)
│   ├── capture.c              # Module 3: Raycasting capture logic & piece flipping execution
│   ├── cli_input.c            # Module 1: Input parser for :n, :l, :s, :v, :q, and square jumps
│   ├── cli_render.c           # Module 4: Board display, history side panel, color/cursor highlights
│   ├── main.c                 # Application entry point & core event loop initialization
│   ├── move_validation.c      # Module 2: Directional move validation & valid move array calculation
│   ├── save_load.c            # Module 5: Serialization/deserialization for game state files
│   └── stats.c                # Module 5: Multi-file stats parser & summary table generator
│
└── tests/                     # Automated unit testing suite
    ├── test_capture.c         # Unit tests for multi-directional piece flipping
    ├── test_storage.c         # Tests for file save/load operations
    └── test_validation.c      # Unit tests for move rules and valid move generation
```