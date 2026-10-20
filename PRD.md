# Product Requirements Document (PRD) - Othello Game C17

## 1. Product Overview

The objective is to create a robust, terminal-based Othello game engine in standard C17. The program offers dual-mode input (interactive board moves and Vi-style command mode), game replay capabilities, save/load persistence, and cross-file statistical analysis.

---

## 2. Functional Requirements

### 2.1 Board Initialization & Gameplay Rules

- **Board Setup**: 8x8 grid (Columns A–H, Rows 1–8).
- **Initial State**: D4=White, E4=Black, D5=Black, E5=White. Black always moves first.
- **Move Validation**:
  - A move is legal only if it places a piece adjacent to an opposing piece and traps a contiguous line of opposing pieces between the placed piece and another piece of the current player.
  - Legal moves must be visually indicated on the board using `*` during standard play.
- **Turn Passing**: If a player has 0 legal moves, a message is displayed, and the turn automatically passes to the opponent.
- **Game Termination**: The game ends when neither player has valid moves or the board is completely filled. Winner is determined by highest piece count.

### 2.2 Command Mode Specs

Commands are entered by typing `:` followed by the command character:

- `:n` — Initiate new game creation. Supports command line flags: `-p1="Name"`, `-p2="Name"`, `-w=p1|p2`.
- `:l -f="<path>"` — Load game state from file.
- `:s -f="<path>"` — Save current active game to specified path.
- `:s <file1> <file2> ...` — Process multiple save files and generate an aggregated statistics report.
- `:v -f="<path>"` — Enter replay viewer mode for a finished or saved game.
- `:h` — Display command cheat sheet.
- `:q` — Terminate application.

### 2.3 Interactive Navigation & Input

- Typing coordinates (e.g., `e3` or `E3`) immediately moves selection cursor to that cell.
- Pressing `ENTER` attempts piece placement. If illegal, displays red error text below move history.
- In replay mode (`:v`), typing `/` moves 1 step forward, and `\` moves 1 step backward through game history.

---

## 3. Non-Functional Requirements

- **Language Standard**: C17 (`-std=c17`).
- **Build System**: Clean build via GNU `Makefile` across Windows (MinGW), Linux (GCC), and macOS (Clang).
- **Performance**: Instant board rendering (< 50ms per turn).
- **Robustness**: Safe string bounds handling (`strncpy`, `snprintf`), zero buffer overflows, memory leak-free execution.
