# Product Requirements Document (PRD) - Othello Game C17 (v2)

## 0. Issues fixed from v1

1. Legal-move rule missed: target square must be **empty** and **in bounds**; capture needs ≥1 direction.
2. **Ties** were undefined (only "highest piece count").
3. `:s` was overloaded (save vs stats) with no rule to tell them apart.
4. `-w=` default and missing-flag behaviour undefined.
5. **ESC → command mode** (from design notes) and which commands work where were missing.
6. **Save file format** was unspecified, yet Storage, Replay and Stats all depend on it.
7. Stats content and treatment of unfinished/corrupt files undefined.
8. Mouse support (in project slides/README) absent from requirements.
9. "Standard C17" cannot do raw keystrokes, ANSI or mouse; a platform layer is now explicit.
10. Replay key `p` (README) not in PRD.

## 1. Product Overview

Terminal-based Othello for two human players in C17: VIM-style command mode, keyboard + mouse board input, legal-move hints, move history, replay, save/load, and cross-file statistics.

## 2. Functional Requirements

### 2.1 Rules

- **Board**: 8x8, columns A-H, rows 1-8. Internally `Position{row,col}` 0-indexed (`e3` = row 2, col 4).
- **Initial state**: D4=White, E4=Black, D5=Black, E5=White. Black moves first.
- **Legal move**: square is in bounds and **empty**, and in at least one of 8 directions an unbroken line of ≥1 opponent pieces ends in a piece of the mover. All such lines are flipped.
- **Hints**: legal moves of the current player shown as `*` in play mode only (not in replay).
- **Pass**: no legal move → message shown, turn passes automatically (passes are not recorded in history).
- **End**: board full, or neither player can move. Checked before each input.
- **Winner**: higher piece count; equal count = **tie**. Empty squares are not awarded to anyone.

### 2.2 Modes and commands

Modes: **Menu** (welcome screen), **Play**, **View** (replay). Pressing `ESC` then typing `:cmd` + Enter always works.

| Command | Meaning | Valid in |
|---|---|---|
| `:n [-p1="A"] [-p2="B"] [-w=p1\|p2]` | New game; missing flags are prompted one by one. `-w` = who plays White (default p2). Enters Play. | any |
| `:l -f="<path>"` | Load a game to continue. Enters Play. | any |
| `:s -f="<path>"` | Save current game | **Play only** |
| `:s <f1> <f2> ...` | Stats over files (no `-f=`) | any |
| `:v -f="<path>"` | Replay a saved game. Enters View. | any |
| `:h` | Help | any |
| `:q` | Quit | any |

`:s` is told apart by the `-f=` flag: present = save, absent = stats. (Open question: separate letters? see section 6.)

### 2.3 Input

- Typing `e3`/`E3` moves the cursor immediately (no Enter), with visible highlight.
- Mouse click on a square selects it; click on a history entry shows that earlier position (view only).
- `Enter` tries to place at the cursor; illegal → red "Invalid move" below the history panel.
- Replay: `/` forward, `\` or `p` backward, `ESC` exits.
- Name limit: 31 characters; quotes allow spaces.

## 3. Non-Functional Requirements

- **Language**: C17 (`-std=c17 -Wall -Wextra -pedantic`).
- **Build**: GNU Makefile on Windows (MinGW), Linux (GCC), macOS (Clang).
- **Platform layer**: raw input/mouse/ANSI isolated in `platform.h` (POSIX termios, Windows Console API).
- **Performance**: render < 50 ms.
- **Robustness**: bounded string handling (`snprintf`, never raw `strcpy`), no leaks, corrupted save files never crash the program.

## 4. Key definitions (full code in ARCHITECTURE.MD)

| Name | Definition |
|---|---|
| `CellState` | `CELL_EMPTY=0, CELL_BLACK=1, CELL_WHITE=2` |
| `PlayerColor` | `PLAYER_BLACK=1, PLAYER_WHITE=2` |
| `Position` | `{int row; int col;}` 0-indexed |
| `MoveRecord` | `{PlayerColor player; Position pos;}` |
| `GameMeta` | names (32 B each), `p1_color`, `p2_color` |
| `GameState` | board, valid_moves, current_turn, game_ended, history[60], move_count, meta |
| `GameResult` | `IN_PROGRESS, BLACK_WIN, WHITE_WIN, TIE` |
| `TurnStatus` | `TURN_SWITCHED, TURN_OPPONENT_PASSED, TURN_GAME_OVER` |
| `AppMode` | `MENU, PLAY, VIEW` |
| `CommandType` | `NEW, LOAD, SAVE, VIEW, STATS, HELP, QUIT, INVALID` |
| `StorageStatus` | `OK, ERR_IO, ERR_FORMAT, ERR_ILLEGAL_MOVE` |
| `StatsSummary` | total_games, skipped_files, black_wins, white_wins, ties, highest_scoring_player, max_score |

Functions: `validate_move`, `get_all_valid_moves`, `has_any_valid_move`, `init_game`, `execute_move`, `check_game_over`, `switch_turn`, `count_pieces`, `get_game_result`, `rebuild_state_at`, `save_game`, `load_game`, `compute_stats_summary`, `print_stats_summary`, `cli_parse_command`, `cli_prompt_new_game`, `parse_position`, `position_to_string`, `ui_render_board`, `ui_display_status`, `ui_view_replay`, `ui_screen_to_cell`, `platform_*`.

## 5. Save file format (text, one record per line)

```text
OTHELLO 1
P1=Alice Smith
P2=Bob
P1_COLOR=B
MOVES=3
B e3
W f3
B f4
```

- Board, turn, `game_ended` are **not stored**; `load_game` rebuilds them by replaying moves with `validate_move`/`execute_move`. An illegal move → `STORAGE_ERR_ILLEGAL_MOVE`.
- Pro: file can't contradict itself. Con: slightly slower load (negligible, ≤60 moves).
- Stats: only finished games count; unfinished or bad files are skipped and counted in `skipped_files`. `max_score` = highest final piece count of any player.

## 6. Open questions

1. Handwritten notes show a separate letter for save and `:s` for stats; PRD uses `:s` for both. Which?
2. Notes mention "std mode" for hints; is there a non-standard display mode?
3. Who owns `platform.h` and UI rendering (Modules 4 and 6)?
