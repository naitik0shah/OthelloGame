# Understanding `ui_cli_platform.c`: Terminal UI & Command Processing

If you have only written standard console programs using `printf()` and `scanf()`, full-screen terminal apps (like `vim`, `htop`, or this Othello game) can look intimidating at first.

In a standard C program, text scrolls endlessly downward and the program pauses until the user presses **Enter**. In `ui_cli_platform.c`, the terminal is treated like a **2D grid of character cells** that can be colored, cleared, redrawn in place, and clicked with a mouse.

This guide breaks down every part of `ui_cli_platform.c` from top to bottom using basic C examples.

---

## 1. The Big Picture: How This File Fits In

The rest of the codebase (`validation_capture.c`, `storage.c`, `stats.c`) knows nothing about Windows, Linux, colors, or keyboards. It only works with plain C structs like `GameState`.

`ui_cli_platform.c` is the **bridge** between the operating system and the game logic. It handles five main jobs:

1. **Terminal Setup & Cleanup (`term_init`, `term_restore`):** Puts the terminal into "raw" full-screen mode on startup and cleanly restores the user's normal shell on exit.
2. **Input Translation (`term_read_event`):** Converts OS-specific keystrokes and mouse clicks into a simple, unified `InputEvent` struct.
3. **Screen Rendering (`render`):** Draws the board, colors, move history, and status bars without screen flicker.
4. **Command Processing (`edit_line`, `tokenize`, `parse_command`, `run_command`):** Lets the user type commands like `:n Alice Bob` and parses them into structured actions.
5. **Full-Screen Views (`show_help`, `show_stats`, `view_replay`):** Renders dedicated screens for help, match statistics, and step-by-step game replays.

---

## 2. Painting the Terminal: ANSI Escape Codes

Terminals do not have functions like `set_text_color(RED)`. Instead, you print special invisible command strings called **ANSI Escape Sequences**.

### Basic Example

Every escape sequence starts with the **Escape** character (`ASCII 27`, written `\x1b` in C) followed by `[`:

```c
// In a normal C program, you could print red bold text like this:
printf("\x1b[1;31mGame Over!\x1b[0m\n");
//      ^^^^^^^           ^^^^^^
//      Bold + Red        Reset back to normal
```

When the terminal sees `\x1b[`, it does not print those characters to the screen—it treats everything up to the ending letter (`m`, `H`, `J`, etc.) as an instruction.

### How `ui_cli_platform.c` Builds Sequences

Instead of scattering cryptic codes like `"\x1b[1;97;44m"` everywhere, the top of the file uses C preprocessor macros:

```c
#define CSI             "\x1b["
#define SGR(codes)      CSI codes "m"
#define DEC_SET(mode)   CSI "?" #mode "h"
#define DEC_RESET(mode) CSI "?" #mode "l"
```

Two classic C tricks are used here:

* **Stringizing (`#mode`):** Putting `#` in front of a macro parameter wraps it in quotes. `DEC_SET(25)` expands to `"\x1b[" "?" "25" "h"`.
* **Adjacent String Literal Concatenation:** In C, if you place two string literals next to each other with only whitespace (no commas or `+`), the compiler merges them into a single string at compile time:

  ```c
  // These two lines produce the exact same string in memory:
  const char *a = "\x1b[" "?" "25" "h";
  const char *b = "\x1b[?25h";
  ```

  That is why `ENTER_SCREEN` and `LEAVE_SCREEN` have no commas between lines—they merge into one single control string.

### Key Escape Sequences Used

| Macro | Expands To | What It Tells the Terminal to Do |
| :--- | :--- | :--- |
| `DEC_SET(1049)` | `\x1b[?1049h` | Switch to an **alternate screen buffer** (so exiting restores your previous terminal output). |
| `ANSI_CURSOR_HIDE` | `\x1b[?12l\x1b[?25l` | Stop blinking and hide the text cursor so it doesn't flash over the board. |
| `DEC_SET(1003)` | `\x1b[?1003h` | Report all mouse movement (used for hovering over squares). |
| `DEC_SET(1006)` | `\x1b[?1006h` | Report mouse coordinates in a modern, easy-to-parse format (`<button;x;yM`). |
| `ANSI_CURSOR_HOME` | `\x1b[H` | Move the cursor to the top-left corner (`row 1, col 1`). |
| `ANSI_CLEAR_EOL` | `\x1b[K` | Erase from the cursor to the end of the current line. |

### Graceful Fallback (`detect_terminal_capabilities`)

Not all terminals support colors or mouse tracking (for example, if a user sets `NO_COLOR=1` or runs on a basic Linux `tty1` console).

* `detect_terminal_capabilities()` inspects `isatty()` and environment variables (`NO_COLOR`, `TERM`) to set global flags like `g_use_color` and `g_use_mouse`.
* `style_str(SGR_ERROR)` returns the ANSI color code if `g_use_color` is `true`, or an empty string `""` if colors are disabled.

---

## 3. Talking to the OS: Windows vs. POSIX (Linux/macOS)

Standard C has no cross-platform way to read a single keypress without pressing **Enter**, or to detect arrow keys and mouse clicks. Because of this, the file splits into two implementations using `#ifdef _WIN32`:

### Why "Raw Mode" Is Needed

Normally, the OS terminal driver operates in **canonical ("cooked") mode**:

1. It holds onto keys until the user presses **Enter**.
2. It automatically prints ("echoes") every character typed onto the screen.
3. Pressing `Ctrl+C` immediately kills the program without letting it clean up the screen.

`term_init()` turns off these behaviors so the game receives every keystroke immediately and draws the screen itself:

* **On Windows (`_WIN32`):** Calls `GetConsoleMode()` to save the original settings, then `SetConsoleMode()` to enable `ENABLE_MOUSE_INPUT`, `ENABLE_WINDOW_INPUT`, and `ENABLE_VIRTUAL_TERMINAL_PROCESSING`.
* **On POSIX (Linux/macOS):** Uses `tcgetattr()` to save a `struct termios`, clears flags like `ICANON` (line buffering), `ECHO`, and `ISIG` (`Ctrl+C` signal), and applies it with `tcsetattr()`.

> **Safety Net (`atexit`):** Both platforms register `atexit(term_restore)`. Even if the game exits early, `term_restore()` runs automatically to re-show the cursor and give the user their normal terminal back.

### Translating Input Events (`term_read_event`)

The game loop doesn't want to care whether `VK_UP` (Windows) or `"\x1b[A"` (Linux) was pressed—it just wants `EV_UP`.

* **Windows:** Calls `ReadConsoleInput()`, which hands back a structured `INPUT_RECORD`. If `EventType == KEY_EVENT`, a `switch` statement maps `VK_UP` to `EV_UP`, `'\r'` to `EV_ENTER`, etc. If `EventType == MOUSE_EVENT`, it converts pixel/cell coordinates directly.
* **POSIX:** Reads one byte at a time via `read(STDIN_FILENO, &byte, 1)`. This creates a classic terminal puzzle:
  * Pressing the **Esc** key sends a single byte: `0x1B`.
  * Pressing the **Up Arrow** sends three bytes: `0x1B`, `'['`, `'A'`.
  * Clicking the **Mouse** sends a string like: `0x1B`, `'['`, `'<'`, `'0'`, `';'`, `'1'`, `'2'`, `';'`, `'5'`, `'M'`.

**How POSIX solves the `Esc` vs. Arrow puzzle:**
When `read_byte()` sees `0x1B` (`ASCII_ESC`), it calls `input_ready(50)`, which uses the OS `select()` function to wait up to **50 milliseconds** for another byte:

* If nothing arrives within 50 ms, the user pressed the **Esc** key (`EV_ESC`).
* If `'['` arrives immediately, it keeps reading bytes into `sequence[]` until it hits an ending letter (`'A'`–`'D'` for arrows, or `'M'`/`'m'` for SGR mouse clicks via `parse_sgr_mouse()`).

---

## 4. The Rendering Engine: Drawing Without Flicker

### Why In-Memory Buffering (`frame[]`) Matters

If you draw an 8×8 board by calling `printf()` 64 separate times, the terminal may visibly flicker as lines draw top-to-bottom.

Instead, `render()` builds the **entire screen** inside one large string buffer (`static char frame[FRAME_SIZE]`) using helper functions like `append_fmt()` (a wrapper around `snprintf`). Once the whole frame is assembled in memory:

1. It moves the cursor to the top-left (`ANSI_CURSOR_HOME`).
2. It prints the entire `frame` string in **one** `fputs()` call.
3. It clears any leftover lines below (`ANSI_CLEAR_BELOW`).

### Screen Coordinates & Mouse Alignment (`screen_to_cell`)

For mouse clicks to work, the code must know the exact screen row and column where the 8×8 board starts:

```text
Row 0:   OTHELLO    Alice (X)  vs  Bob (O)        <-- Title bar
Row 1:   Turn: Alice (Black)   [Hints ON (*)]     <-- 1-line status banner
Row 2:      A  B  C  D  E  F  G  H                <-- Column headers
Row 3:    1 .  .  .  .  .  .  .  .                <-- BOARD_TOP = 3, BOARD_LEFT = 3
Row 4:    2 .  .  .  *  .  .  .  .                <-- Each cell is CELL_WIDTH (3) chars wide
```

* Because `BOARD_TOP` is fixed at `3`, Row 1 uses `append_single_line()` to clip long text rather than wrapping onto a second line (which would push the board down and break mouse click coordinates).
* When a user clicks at terminal `(row, col)`, `screen_to_cell()` subtracts `BOARD_TOP` and `BOARD_LEFT`, checks that it's inside the 8×8 grid, and divides `(col - BOARD_LEFT)` by `CELL_WIDTH` (3) to find which board square was clicked.

### Responsive Terminal Sizing & Word Wrapping

* `get_terminal_size()` asks the OS for the window's current width and height (`GetConsoleScreenBufferInfo` on Windows, `ioctl(TIOCGWINSZ)` on POSIX).
* If the terminal is at least `MIN_COLS_WITH_HISTORY` (48 columns) wide, `render()` draws the recent move history alongside the right edge of the board. On narrower screens, it hides the side pane so lines don't wrap awkwardly.
* `append_wrapped_line()` prints paragraphs (like status messages or help text) while tracking `rows_left`. It scans forward for spaces so words break cleanly at line boundaries and stops before overflowing the bottom of the terminal.

### Drawing Each Square (`append_cell`)

For every `(row, col)` on the board, `append_cell()` picks:

1. **Symbol:** `'X'` (Black), `'O'` (White), `'*'` (valid move if hints are on), or `'.'` (empty).
2. **Background:** Bright yellow (`SGR_BG_LAST`) if this square was the most recent move, otherwise green (`SGR_BG_BOARD`).
3. **Cursor:** Inverted colors (`SGR_INVERSE`) on color terminals, or brackets `[X]` when color is disabled.

---

## 5. Command Line & Input Parsing (`:` Commands)

Just like in `vim`, pressing `:` opens a command bar at the bottom of the screen.

### 1. Mini Line Editor (`edit_line`)

`edit_line()` runs its own mini event loop:

* Each time the user types a printable character (`EV_CHAR`), it appends it to `buf[]` and increments `len`.
* Pressing `EV_BACKSPACE` decrements `len` and moves the null terminator `'\0'` back one spot.
* On every keypress, it calls `render()` so the user sees their typing live with a `_` caret (handled in `append_prompt_row()`, which also horizontally scrolls the text if the command gets longer than the terminal width).
* Pressing `EV_ENTER` returns `true`; `EV_ESC` cancels and returns `false`.

### 2. Splitting Words & Quotes (`tokenize`)

Before understanding a command, the raw string (e.g., `:n "Player One" Bob w`) must be split into individual arguments ("tokens"):

* `tokenize()` skips whitespace and copies characters into `tok[0]`, `tok[1]`, etc.
* When it sees a `"` character, it flips `in_quotes = !in_quotes`. While `in_quotes` is `true`, spaces are treated as normal characters instead of argument separators, and the quotes themselves are stripped out.
* Thus, `:n "Player One" Bob w` becomes 4 clean tokens: `[":n", "Player One", "Bob", "w"]`.

### 3. Validating Arguments (`parse_command`)

`parse_command()` inspects `tok[0]` (`:n`, `:l`, `:w`, `:s`, `:v`, `:h`, `:q`, `:t`) and loops through the remaining tokens (`i = 1` to `n - 1`):

* It supports both **flag-style** arguments (`-p1=Alice`, `-w=p1`, `-f=save.txt`) and **positional** arguments (`:n Alice Bob w`).
* If anything is invalid (e.g., missing filename for `:l`, or a name longer than 31 characters), it calls `set_error()` with a clear message and returns `false`.
* Otherwise, it fills out a `ParsedCommand` struct and returns `true`.

### 4. Executing Commands (`run_command`)

`run_command()` takes the validated `ParsedCommand` and uses a `switch (cmd.type)` to perform the action—such as calling `init_game()` for `:n`, `load_game()` for `:l`, `save_game()` for `:w`, or launching sub-views for `:h`, `:s`, and `:v`.

---

## 6. Full-Screen Sub-Views

When the user isn't actively playing on the board, `ui_cli_platform.c` renders specialized full-screen views:

* **Welcome Screen (`render_welcome_screen`):** Shown automatically by `render()` whenever `screen->state == NULL` (no game loaded yet). It displays a quick command cheat sheet and adjusts its spacing (`spacious`) based on terminal height.
* **Help & Stats (`show_help`, `show_stats`):** Both build a full-screen text buffer using `append_wrapped_line()`, flush it with `flush_terminal_frame()`, and then call `wait_key()`—a simple loop that ignores mouse movements and pauses until the user presses any key.
* **Interactive Replay (`view_replay`):** Used by `:v` to step through a game's move history without modifying the actual game:
  * Maintains a `step` counter (`0` to `game->move_count`) and calls `rebuild_state_at(game, step, &cur)` to reconstruct the exact board at that moment in time.
  * Sets `screen.tag = "REPLAY move X / Y"`, which tells `detect_ui_mode()` to display replay-specific hints in the footer.
  * Typing digits (`0`–`9`) accumulates a multi-digit `number` (`number = number * 10 + digit`) so pressing **Enter** jumps directly to that move number.

---

## 7. Quick Reference Glossary

| Term | Meaning |
| :--- | :--- |
| **ANSI / VT Sequence** | Special character codes starting with `\x1b[` used to control cursor position, colors, and screen buffers in a terminal. |
| **CSI** | *Control Sequence Introducer* (`\x1b[`) — the prefix for most terminal control commands. |
| **SGR** | *Select Graphic Rendition* (`\x1b[...m`) — escape sequences that change text color, bold, dim, or inverted styles. |
| **TTY** | *Teletypewriter* — Unix terminology for an interactive terminal device (as opposed to piping output to a file). |
| **Raw Mode** | Disabling OS line-buffering and auto-echo so a program receives every keypress immediately. |
| **Idempotent** | Safe to call multiple times without unintended side effects (e.g., `term_restore()` checks `g_term_active` so running it twice is harmless). |
