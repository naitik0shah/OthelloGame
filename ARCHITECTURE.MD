# Othello Architecture & Technical Specification (v2)

## 0. Issues fixed from v1

| # | Issue in v1 | Fix |
|---|---|---|
| 1 | `const CellState board[8][8]` params: in C17 passing `CellState(*)[8]` to `const CellState(*)[8]` warns (`-pedantic`) | Validation takes `const GameState *` |
| 2 | `cli_parse_command(input, GameState*)` can't report errors, signal quit, or return file lists for stats | Parsing split from execution: returns `ParsedCommand` |
| 3 | `switch_turn` couldn't report a pass / game over, though the UI must show the "no moves" message | Returns `TurnStatus`; `check_game_over` returns `bool` |
| 4 | Replay (`/`, `\`) and `load_game` need a board at step N, but no function existed | Added `rebuild_state_at()`, `init_game()` |
| 5 | No winner / piece-count API (stats and game-over need it) | Added `count_pieces()`, `get_game_result()`, `GameResult` |
| 6 | Raw key input, ANSI, mouse are not in standard C17 (PRD requires cursor jump per keystroke + mouse) | New `platform.h` layer (POSIX + Windows) |
| 7 | Module numbering conflicts with TASKS.MD (4 vs 5 modules); UI owned by nobody; replay assigned to Validation | Single numbering below; replay lives in UI |
| 8 | `ui_render_board` had no way to hide hints, show replay step, or map mouse clicks | `RenderOptions`, `ui_screen_to_cell()` |
| 9 | `load/save` returned `bool`, so errors (missing vs corrupt vs illegal history) were indistinguishable | `StorageStatus` enum |
| 10 | Passes, validation preconditions, derived fields (`valid_moves`), name length not documented | Documented in headers |
| 11 | Direction table would be duplicated by Validation and Capture teams | Shared `DIRECTIONS` in `common.h` |

## 1. Modules and owners

| # | Module | Header | Source | Owners |
|---|---|---|---|---|
| 1 | CLI parsing & prompts | `cli.h` | `cli_input.c` | Akshar, Ojas |
| 2 | Move validation & hints | `validation.h` | `move_validation.c` | Aniruddh, Maulik, Naitik |
| 3 | Capture & state transitions | `capture.h` | `capture.c` | Aarav, Nishi, Prem |
| 4 | UI rendering & replay | `ui.h` | `cli_render.c` | **unassigned** (TASKS.MD gives rendering to Module 1) |
| 5 | Persistence & stats | `storage.h` | `save_load.c`, `stats.c` | Manthan, Yug |
| 6 | Platform I/O (new) | `platform.h` | `platform_posix.c`, `platform_win.c` | **unassigned** |

Dependency direction (no cycles): `ui → capture → validation → common`; `storage → capture`; `cli → common`; `main → everything`.

## 2. Headers (all compile cleanly with `gcc -std=c17 -Wall -Wextra -pedantic`)

### `include/common.h`

```c
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
```

### `include/cli.h`

```c
#ifndef CLI_H
#define CLI_H

#include "common.h"

#define MAX_STAT_FILES 32

typedef enum {
    CMD_NONE = 0, CMD_NEW, CMD_LOAD, CMD_SAVE, CMD_VIEW,
    CMD_STATS, CMD_HELP, CMD_QUIT, CMD_INVALID
} CommandType;

typedef enum { WHITE_UNSET = 0, WHITE_IS_P1, WHITE_IS_P2 } WhiteChoice;

/* Result of parsing one ":..." line. Parsing never touches game state. */
typedef struct {
    CommandType type;
    bool        has_p1, has_p2;
    char        p1[MAX_NAME_LEN], p2[MAX_NAME_LEN];       /* :n -p1= -p2= */
    WhiteChoice white;                                    /* :n -w=p1|p2 */
    bool        has_path;
    char        path[MAX_PATH_LEN];                       /* -f= for :l :s :v */
    char        files[MAX_STAT_FILES][MAX_PATH_LEN];      /* :s <f1> <f2> ... */
    int         file_count;
    char        error[96];                                /* set when type == CMD_INVALID */
} ParsedCommand;

/* Parses ":n -p1=\"A\" -p2=\"B\" -w=p1", ":l -f=\"x\"", ... Returns false on syntax error
   (out->type = CMD_INVALID, out->error filled). Names/paths longer than the limits are errors. */
bool cli_parse_command(const char *input, ParsedCommand *out);

/* Fills GameMeta from cmd, interactively prompting only for missing fields
   (names; white player defaults to P2 when -w is absent). */
void cli_prompt_new_game(const ParsedCommand *cmd, GameMeta *meta);

/* "e3"/"E3" -> {2,4}. Returns false for anything else. */
bool parse_position(const char *text, Position *out);

/* {2,4} -> "e3" (out needs 3 bytes) */
void position_to_string(Position pos, char out[3]);

#endif /* CLI_H */
```

### `include/validation.h`

```c
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
```

### `include/capture.h`

```c
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
```

### `include/ui.h`

```c
#ifndef UI_H
#define UI_H

#include "common.h"

typedef struct {
    Position cursor;
    bool     show_hints;     /* draw '*' on legal moves (play mode only) */
    int      replay_step;    /* -1 = live game; else step shown in replay */
} RenderOptions;

/* Full redraw: board (A-H / 1-8), move-history side panel, turn indicator, scores */
void ui_render_board(const GameState *state, const RenderOptions *opts);

/* Status line under the history panel; is_error -> red ANSI text */
void ui_display_status(const char *message, bool is_error);

void ui_render_welcome(void);
void ui_render_help(void);

/* Replay loop: '/' forward, '\' (or 'p') backward, ESC exits.
   Uses rebuild_state_at(); `game` is never modified. */
void ui_view_replay(const GameState *game);

/* Mouse support: terminal cell -> board square. Returns false if outside the board. */
bool ui_screen_to_cell(int screen_row, int screen_col, Position *out);

/* Click on a history-panel entry -> its 0-based move index, or -1 */
int  ui_screen_to_history_index(int screen_row, int screen_col, const GameState *state);

#endif /* UI_H */
```

### `include/storage.h`

```c
#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"

typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERR_IO,            /* missing file / cannot open / write failed */
    STORAGE_ERR_FORMAT,        /* header, field or move line malformed */
    STORAGE_ERR_ILLEGAL_MOVE   /* history does not replay legally */
} StorageStatus;

/* Text save format is defined in PRD.md section 5 */
StorageStatus save_game(const GameState *state, const char *filepath);

/* Board, turn, game_ended and valid_moves are reconstructed by replaying history */
StorageStatus load_game(GameState *state, const char *filepath);

/* Aggregates finished games only; bad/unfinished files are counted in skipped_files.
   Returns number of files successfully counted. */
int  compute_stats_summary(const char *const filepaths[], int file_count, StatsSummary *out);

/* Prints the summary table */
void print_stats_summary(const StatsSummary *summary);

const char *storage_status_str(StorageStatus status);

#endif /* STORAGE_H */
```

### `include/platform.h`

```c
#ifndef PLATFORM_H
#define PLATFORM_H

/* Raw key/mouse input and ANSI setup are NOT in the C17 standard library.
   Implement in platform_posix.c (termios) and platform_win.c (Windows Console API,
   VT processing enabled) behind this one interface. */

typedef enum { EVT_NONE = 0, EVT_KEY, EVT_MOUSE_CLICK } EventType;

#define KEY_ESC       27
#define KEY_ENTER     '\n'
#define KEY_BACKSPACE 127

typedef struct {
    EventType type;
    int key;                      /* EVT_KEY: character code */
    int screen_row, screen_col;   /* EVT_MOUSE_CLICK: 0-indexed terminal cell */
} InputEvent;

void       platform_init(void);        /* raw mode, hide cursor, enable mouse + ANSI */
void       platform_shutdown(void);    /* restore terminal (also call from atexit) */
InputEvent platform_read_event(void);  /* blocking */
void       platform_clear_screen(void);

#endif /* PLATFORM_H */
```

## 3. Behaviour contracts

- **Coordinates**: `Position{row,col}` 0-indexed. Rank 1 = row 0, file A = col 0.
- **Turn flow after a move**: `validate_move` → `execute_move` → `check_game_over`/`switch_turn` → UI shows message if `TURN_OPPONENT_PASSED` or result if `TURN_GAME_OVER`.
- **Game-end check happens before input is accepted** (per design notes).
- **Passes are not stored** in `history`; replay infers them from consecutive same-player records.
- **`valid_moves`** is derived; recomputed by `init_game`, `switch_turn`, `rebuild_state_at`; never saved.
- **Cursor, hint toggle, app mode** live in `main.c`, not in `GameState` (they are not game data).

## 4. Data flow

```text
 platform_read_event()  (keys / mouse)
          |
          v
 main loop (AppMode: MENU / PLAY / VIEW)
   |-- ESC then ':' ... Enter --> cli_parse_command --> ParsedCommand
   |       |-- NEW/LOAD/SAVE --> init_game / load_game / save_game   (Storage, Capture)
   |       |-- STATS --> compute_stats_summary --> print_stats_summary
   |       |-- VIEW  --> load_game --> ui_view_replay (uses rebuild_state_at)
   |       '-- HELP/QUIT
   |-- 'e3' / click --> parse_position / ui_screen_to_cell --> move cursor
   '-- Enter --> validate_move
            |-- invalid --> ui_display_status(red)
            '-- valid   --> execute_move --> switch_turn --> TurnStatus
                              (PASS msg | GAME_OVER result | normal)
          |
          v
 ui_render_board(state, RenderOptions)
```

## 5. File structure

```text
OthelloGame/
├── ARCHITECTURE.MD  PRD.md  README.MD  TASKS.MD
├── Makefile  CMakeLists.txt  .gitignore
├── include/  common.h cli.h validation.h capture.h ui.h storage.h platform.h
├── src/      main.c cli_input.c cli_render.c move_validation.c capture.c
│             save_load.c stats.c platform_posix.c platform_win.c
└── tests/    test_validation.c test_capture.c test_storage.c
```

Build flags: `-std=c17 -Wall -Wextra -pedantic`; on POSIX add `-D_POSIX_C_SOURCE=200809L` (termios is not visible under strict C17 otherwise).
