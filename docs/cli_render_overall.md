# `cli_render.c` — How the Terminal Screen Is Drawn

`cli_render.c` takes the current Othello game state and builds the entire text screen shown in your terminal.

Instead of calling `printf()` dozens of times directly to the screen (which causes visible flickering), it uses a **frame-buffer pattern**:

1. **Snapshot:** Pack everything needed for the screen into a `Screen` `struct`.
2. **Compose:** Build the entire screen as one big C string (`static char frame[FRAME_SIZE]`) in memory.
3. **Flush:** Print that string to `stdout` all at once.

---

## High-Level Execution Flow

```text
  App state ──▶ app_screen() ──▶ Screen struct ──▶ render()
                                                      │
                           ┌──────────────────────────┴──────────────────────────┐
                           ▼                                                     ▼
               [No active game loaded]                                 [Active game / Replay]
              render_welcome_screen()                              Draw Title, Turn, 8x8 Board,
                           │                                       History, Score & Footer
                           └──────────────────────────┬──────────────────────────┘
                                                      ▼
                                         cli_flush_terminal_frame()
                                          (Prints frame to screen)
```

---

## The Main Functions (And How They Work)

### 1. `app_screen()` — Preparing Data for the Renderer

* **Signature:** `Screen app_screen(const App *a, const char *prompt, const char *text)`
* **Role:** Acts as a bridge between the game logic (`App`) and the drawing code (`render`).
* **How it executes:**
  1. Creates a local `Screen` struct on the stack.
  2. Copies read-only pointers and values from `App` (such as `&a->game`, `a->cursor`, `a->hints`, and `a->status`).
  3. Attaches any active command prompt text (`prompt` and `text`) if the user is currently typing a `:` command.
  4. Returns the `Screen` struct by value so `render()` cannot accidentally modify the real game state.

---

### 2. `render()` — The Main Drawing Orchestrator

* **Signature:** `void render(const Screen *screen)`
* **Role:** The entry point that builds and displays every frame of the UI.
* **How it executes step-by-step:**
  1. **Checks terminal size:** Calls `cli_get_terminal_size(&cols, &rows)` to learn how many columns and rows fit in the window right now.
  2. **Detects UI mode:** Calls `detect_ui_mode(screen)` to figure out if the user is on the welcome screen, typing a command, playing a game, watching a replay, or viewing a finished game.
  3. **Handles the Welcome Screen shortcut:** If `screen->state == NULL` (no game is active), it delegates everything to `render_welcome_screen()`, flushes the buffer, and returns early.
  4. **Draws the Header (Rows 0–2):**
     * **Row 0:** Title bar with both player names (`X` vs `O`).
     * **Row 1:** Single-line status banner (whose turn it is, replay indicator, or game-over winner). Kept strictly to 1 line so the board always starts at row 3 (`BOARD_TOP`).
     * **Row 2:** Column letters (`A` through `H`), plus a `Moves` header if the terminal is wide enough (`cols >= 48`).
  5. **Draws the 8×8 Board & Move History (Rows 3–10):**
     * Loops `row` from `0` to `7`.
     * Prints the row number (`1`–`8`), then loops `col` from `0` to `7` calling `append_cell()` to draw each square.
     * On the right side of each board row, prints one entry from the move history list (scrolling automatically if more than 8 moves have been played).
  6. **Draws the Footer:**
     * Prints the current piece count (`Black X` vs `White O`) and move counter.
     * Prints any status or error message (`screen->status`).
     * Calls `append_prompt_row()` to draw the `:` command input line.
     * Calls `mode_contextual_hint()` to print the relevant keyboard shortcuts on the very bottom line.
  7. **Flushes to screen:** Calls `cli_flush_terminal_frame(frame)` to display the completed text buffer.

---

### 3. `render_welcome_screen()` — Drawing the Start Menu

* **Signature:** `static void render_welcome_screen(...)`
* **Role:** Draws the command reference menu when no game is loaded yet.
* **How it executes:**
  1. Checks if the terminal is large enough (`rows >= 25 && cols >= 72`) to set a `spacious` flag.
  2. Prints the title banner and `[1] SESSION COMMANDS` (`:n`, `:l`, `:v`, `:s`, `:h`, `:q`).
  3. If `spacious` is true, prints full multi-line guides for `[2] BOARD PLAY MODE` and `[3] COMMAND INPUT`; on smaller terminals, condenses them into a single compact line so nothing overflows.
  4. Appends the status line, command prompt, and welcome keyboard hints at the bottom.

---

### 4. `append_cell()` — Drawing One Board Square

* **Signature:** `static void append_cell(...)`
* **Role:** Formats a single square on the 8×8 board with the right character and terminal colors.
* **How it executes:**
  1. Checks `game->board[row][col]` to pick a character:
     * `'X'` for Black (`CELL_BLACK`)
     * `'O'` for White (`CELL_WHITE`)
     * `'*'` if the square is empty and `legal[row][col]` is `true`
     * `'.'` for all other empty squares
  2. Checks if this square was the **most recent move** in `game->history` and applies a yellow underline highlight (`SGR_BG_LAST`) if so.
  3. Checks if the **keyboard cursor** is currently on `(row, col)`. If colors are enabled, it inverts the background/foreground colors (`SGR_INVERSE`); in plain text mode, it wraps the character in brackets like `[X]`.

---

### 5. `cli_append_wrapped_line()` — Safe Word Wrapping

* **Signature:** `int cli_append_wrapped_line(...)`
* **Role:** Prevents long messages or help lines from getting cut off or scrolling the screen off the top.
* **How it executes:**
  1. Tracks how many vertical terminal rows remain via `*rows_left`. As soon as `*rows_left` hits `0`, it stops emitting lines.
  2. Scans forward up to the available column width (`line_budget`), remembering the index of the last space (`last_space`).
  3. If a word crosses the right edge, it rewinds to `last_space` so words break cleanly between lines instead of splitting mid-word.
  4. Decrements `*rows_left` for every line written to `frame`.

---

### 6. `cli_flush_terminal_frame()` — Updating the Terminal Without Flicker

* **Signature:** `void cli_flush_terminal_frame(const char *frame)`
* **Role:** Sends the finished `frame` string to the terminal cleanly.
* **How it executes:**
  1. **Modern / ANSI terminals (`cli_use_vt_sequences() == true`):**
     * Moves the cursor to the top-left corner `(1, 1)` and hides it (`ANSI_CURSOR_HOME ANSI_CURSOR_HIDE`).
     * Writes the entire `frame` string in a single `fputs(frame, stdout)` call, overwriting the old screen in place.
     * Clears any leftover lines below the frame (`ANSI_CLEAR_BELOW`) and parks the cursor back at `(1, 1)`.
  2. **Legacy Windows consoles:** Calls `cli_win32_prepare_legacy_frame()` to reset the console cursor via Win32 APIs, writes `frame`, and calls `fflush(stdout)`.

---

### 7. `screen_to_cell()` — Translating Mouse Clicks to Board Coordinates

* **Signature:** `bool screen_to_cell(int row, int col, Position *out)`
* **Role:** Converts raw terminal `(row, col)` mouse click coordinates into an `(0..7, 0..7)` Othello board square.
* **How it executes:**
  1. Subtracts the fixed top and left board margins (`BOARD_TOP = 3`, `BOARD_LEFT = 3`).
  2. Checks whether the resulting coordinates fall inside the 8 rows and `8 * 3 = 24` columns (`CELL_WIDTH = 3` characters per square, e.g., `" X "`).
  3. If inside bounds, sets `out->row = r` and `out->col = c / CELL_WIDTH`, returning `true`; otherwise returns `false`.
