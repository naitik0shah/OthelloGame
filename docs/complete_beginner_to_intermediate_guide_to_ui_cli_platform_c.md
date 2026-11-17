# Complete Guide to `ui_cli_platform.c`: Terminal UI, OS APIs & Command Processing

If you are comfortable with basic C (`int`, `char`, arrays, `if`/`else`, loops, and `printf`), reading a full-screen terminal program can still feel like hitting a wall. Why are there `\x1b[` strings everywhere? What is a `DWORD` or `termios`? Why doesn't the program use `scanf()`?

This guide explains **everything** in `ui_cli_platform.c` from the ground up—including standard library functions, operating-system types, intermediate C idioms, and a complete walkthrough of every single function in the file.

---

## 1. The Core Mental Shift: From Scrolling Text to a 2D Screen Grid

In a beginner C program:

* Output appends to the bottom of the console forever (`printf`).
* Input waits until the user presses **Enter** (`scanf` / `fgets`).

In a Terminal User Interface (TUI) like this Othello game:

* The terminal window is treated as a **2D grid of `(row, col)` character cells** (defaulting to `80` columns × `24` rows).
* The program turns off line-buffering ("raw mode") so it receives **every single keypress and mouse click immediately**.
* Instead of printing line-by-line, the program draws the entire screen into a memory buffer (`frame[]`) and overwrites the terminal from `(row 1, col 1)` on every update.

---

## 2. Variable Prefixes & Technical Abbreviations

C codebases rely heavily on shortforms. Here is a decoder for every abbreviation in this file:

### Naming Prefixes

* **`g_`** (`g_term_active`, `g_use_color`, `g_console_in`): **Global** (file-scope `static` variable). Signals that the variable lives for the entire lifetime of the program and is shared across functions in this file.
* **`SGR_` / `DEC_` / `ANSI_` / `ASCII_`**: Uppercase names are `#define` macros or `enum` constants.

### Technical Shortforms

| Shortform | Full Name | What It Means in Plain English |
| :--- | :--- | :--- |
| **CLI** | Command-Line Interface | Text-based interface inside a terminal window. |
| **TTY** | Teletypewriter | Unix name for an interactive terminal window (`isatty()` checks if output goes to a real terminal vs. a file). |
| **VT** | Virtual Terminal | DEC's classic VT100/VT220 terminals; "VT sequences" are the text commands modern terminals use for colors and cursor movement. |
| **ANSI** | American National Standards Institute | Organization that standardized terminal escape codes. |
| **CSI** | Control Sequence Introducer | The two-byte prefix `ESC` (`\x1b`) + `[` that begins almost all terminal control commands. |
| **SGR** | Select Graphic Rendition | Escape sequences ending in `m` (e.g., `\x1b[1;31m`) that set bold, dim, inverse, and text/background colors. |
| **DEC** | Digital Equipment Corporation | Creator of VT terminals; `DEC_SET` (`?mode h`) and `DEC_RESET` (`?mode l`) toggle terminal features like cursor visibility. |
| **EOL** | End Of Line | Used here for `ANSI_CLEAR_EOL` (`\x1b[K`), which erases leftover characters from the cursor to the right edge of the screen. |
| **ETX** | End of Text (`0x03`) | The ASCII control byte sent when the user presses `Ctrl+C`. |
| **BS / DEL** | Backspace (`8`) / Delete (`127`) | The two different ASCII codes terminals send when you press the Backspace key. |
| **EINTR** | Error: Interrupted System Call | An OS error code indicating a waiting call (`read` or `select`) was interrupted by a signal (such as resizing the window) and should retry. |

---

## 3. Intermediate C Syntax & Preprocessor Tricks

### A. Preprocessor Directives (`#include`, `#define`, `#ifndef`, `#ifdef`)

Preprocessor lines (`#`) are processed **before** the C compiler translates code to machine instructions:

* **`#ifndef _WIN32` ... `#endif`**: *"If `_WIN32` is NOT defined..."* The compiler defines `_WIN32` automatically on Windows, so `<sys/ioctl.h>` (a Unix header) is skipped on Windows.
* **`#ifdef _WIN32` ... `#else` ... `#endif`**: Compiles the Windows Console API code on Windows, and the POSIX/Linux/macOS `termios` code everywhere else.
* **Macro Stringizing (`#mode`)**: Putting `#` in front of a macro parameter wraps it in double quotes:

  ```c
  #define CSI           "\x1b["
  #define DEC_SET(mode) CSI "?" #mode "h"
  // DEC_SET(25) expands to: "\x1b[" "?" "25" "h"
  ```

* **Adjacent String Literal Concatenation**: In C, string literals separated only by whitespace/newlines are joined into one single string at compile time with zero runtime cost:

  ```c
  // Identical to "\x1b[?25l\x1b[2J":
  const char *seq = "\x1b[?25l"
                    "\x1b[2J";
  ```

  That is why `ENTER_SCREEN` and `LEAVE_SCREEN` have no commas between lines.

### B. `static` and `const char *const`

* **`static` at file scope** (`static bool g_use_color = true;`, `static void append_cell(...)`): Restricts visibility to `ui_cli_platform.c` only (internal linkage—similar to `private` in other languages).
* **`static` inside a function** (`static char frame[FRAME_SIZE];` in `render()`): Allocates the 16 KB buffer once in permanent static memory instead of allocating and destroying it on the call stack every frame.
* **`static const char *const SGR_TITLE`**: Read pointer declarations from **right to left**:
  1. `const SGR_TITLE`: The pointer itself cannot be reassigned to a different address.
  2. `*`: It is a pointer...
  3. `const char`: ...to characters that cannot be modified.

### C. `enum` and `typedef enum`

* Anonymous `enum { ASCII_ESC = 0x1B, ... };` defines named integer constants without polluting memory.
* `typedef enum { UI_MODE_WELCOME, ... } UiMode;` creates a distinct type name `UiMode` representing the 5 UI states.

### D. Infinite Loops (`for (;;)`), `snprintf` Widths & Variadic Functions

* **`for (;;)`**: Standard C idiom for an infinite loop (equivalent to `while (1)`), exited via `return` or `break`.
* **Dynamic `snprintf` Widths**:
  * `%.*s`: Takes an `int` max-length argument before the `const char *` string (used to clip lines so they don't wrap).
  * `%-*s`: Left-aligns a string inside a field of width `int` padded with spaces (used to stretch the title bar across the full terminal width).
  * `%*s`: Prints `int` leading spaces when given `""` (used for dynamic indentation).
* **Variadic Arguments (`va_list`)**: Used in `set_status(App *a, bool is_error, const char *fmt, ...)` so callers can pass `printf`-style format strings and arguments, which are forwarded to `vsnprintf()`.

---

## 4. C Standard Library Functions Used

| Header | Function | Role in `ui_cli_platform.c` |
| :--- | :--- | :--- |
| `<stdio.h>` | `fputs(str, stdout)` | Writes a plain string directly to `stdout` without format-string parsing. |
| `<stdio.h>` | `fflush(stdout)` | Forces C's buffered `stdout` stream to push bytes to the terminal immediately (crucial so cursor-hide and clear commands take effect right away). |
| `<stdio.h>` | `snprintf` / `vsnprintf` | Formats text into fixed-size character arrays while guaranteeing no buffer overflow and always null-terminating (`\0`). |
| `<stdio.h>` | `sscanf` | Extracts formatted values out of a string—used in `parse_sgr_mouse()` to parse `"<button;x;yM"`. |
| `<stdlib.h>` | `getenv` | Reads OS environment variables (`NO_COLOR`, `TERM`, `COLUMNS`, `LINES`). |
| `<stdlib.h>` | `atoi` | Converts numeric strings (like `"80"`) to `int`. |
| `<stdlib.h>` | `atexit` | Registers `term_restore()` to run automatically when the program exits so the user's terminal is never left broken. |
| `<string.h>` | `strlen`, `strcmp`, `strncmp` | Measures string length, compares full strings (`0` = equal), and compares prefixes (e.g., `"-p1="`). |
| `<string.h>` | `memset` | Zero-initializes structs (`ParsedCommand`) and 2D arrays (`legal_moves`) in one fast call. |
| `<ctype.h>` | `isspace`, `isdigit`, `tolower` | Character classification/conversion. Casts `(unsigned char)` first to avoid undefined behavior on negative `char` values. |
| `<assert.h>` | `assert` | Halts immediately in debug builds if a required precondition (such as `input != NULL`) is violated. |

---

## 5. Operating System Layer: Windows vs. POSIX (Linux/macOS)

Because standard C lacks raw keyboard/mouse APIs, this file implements the same 4 public terminal functions (`term_init`, `term_restore`, `term_read_event`, and `get_terminal_size`) twice—once for Windows and once for POSIX.

### Windows Console API (`#ifdef _WIN32`)

* **`HANDLE`**: An opaque OS handle referencing `STD_INPUT_HANDLE` (keyboard/mouse) or `STD_OUTPUT_HANDLE` (screen). `INVALID_HANDLE_VALUE` indicates failure.
* **`DWORD`**: *Double Word*—Windows's name for a 32-bit unsigned integer (`unsigned long`), used for bitmask flags and counts.
* **`COORD`**: A `{ SHORT X; SHORT Y; }` struct holding 0-based column/row console coordinates.
* **`CONSOLE_SCREEN_BUFFER_INFO`**: Filled by `GetConsoleScreenBufferInfo()`; `srWindow.Right - srWindow.Left + 1` gives the visible terminal width in columns.
* **`CONSOLE_CURSOR_INFO`**: `{ DWORD dwSize; BOOL bVisible; }` passed to `SetConsoleCursorInfo()` to hide the caret on older Windows consoles that lack ANSI support.
* **`GetConsoleMode()` / `SetConsoleMode()`**: Saves original console flags and enables `ENABLE_MOUSE_INPUT`, `ENABLE_WINDOW_INPUT`, and `ENABLE_VIRTUAL_TERMINAL_PROCESSING` (ANSI escape code support).
* **`INPUT_RECORD` & `ReadConsoleInput()`**: Blocks until a console event arrives, distinguishing `KEY_EVENT` (virtual key codes like `VK_UP` and ASCII characters), `MOUSE_EVENT` (`MOUSE_MOVED`, `FROM_LEFT_1ST_BUTTON_PRESSED`), and `WINDOW_BUFFER_SIZE_EVENT`.
* **`FillConsoleOutputCharacterA()` / `SetConsoleCursorPosition()`**: Legacy fallback used to clear the screen on older Windows hosts where `ENABLE_VIRTUAL_TERMINAL_PROCESSING` is unavailable.

### POSIX / Linux / macOS API (`#else`)

* **`STDIN_FILENO` / `STDOUT_FILENO`**: File descriptors `0` and `1`.
* **`isatty(fd)`**: Checks whether `fd` is an interactive terminal.
* **`struct termios`, `tcgetattr()`, `tcsetattr()`**: Saves original terminal settings and clears `ICANON` (line buffering), `ECHO` (auto-printing keys), `ISIG` (`Ctrl+C` signal), `IXON` (`Ctrl+S`/`Q` flow control), and `ICRNL` (`\r` to `\n` translation). `TCSANOW` applies changes immediately.
* **`struct winsize` & `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)`**: Queries the kernel for current terminal rows (`ws_row`) and columns (`ws_col`).
* **`read(STDIN_FILENO, &byte, 1)`**: Reads 1 raw byte directly from the OS input stream.
* **`fd_set`, `FD_ZERO`, `FD_SET`, `struct timeval`, `select()`**: Waits up to 50 ms (`ESC_SEQUENCE_TIMEOUT_MS`) after reading `0x1B` (`ESC`). If no follow-up bytes arrive within 50 ms, the user pressed the standalone **Esc** key (`EV_ESC`); if `[` or `O` arrives immediately, it reads the rest of the arrow or SGR mouse escape sequence.

---

## 6. How Rendering & Command Processing Work Together

1. **Flicker-Free Rendering (`render`)**:
   * `render()` builds the entire screen inside `static char frame[FRAME_SIZE]`.
   * Row `0` is the full-width title bar; Row `1` is clipped with `append_single_line()` so it never wraps onto two lines—preserving `BOARD_TOP == 3` and `BOARD_LEFT == 3` so `screen_to_cell()` always maps mouse clicks accurately to the 8×8 board.
   * `flush_terminal_frame()` homes the cursor (`\x1b[H`), writes `frame` in one `fputs()`, and clears below (`\x1b[J`).
2. **Command Pipeline (`:` Commands)**:
   * `command_prompt()` calls `edit_line()` to let the user type a command with live Backspace editing and horizontal scrolling (`append_prompt_row()`).
   * `run_command()` passes the completed line to `parse_command()`, which uses `tokenize()` to split arguments on whitespace (respecting `"quoted strings with spaces"`) and populates a `ParsedCommand` struct.
   * `run_command()` then switches on `cmd.type` to start a game, load/save files, toggle hints, or launch full-screen views (`show_help`, `show_stats`, `view_replay`).

---

## 7. Complete Function-by-Function Reference

Every function in `ui_cli_platform.c`, in order of appearance:

### Terminal Capabilities & Low-Level Output

1. **`style_str(seq)`**: Returns `seq` when `g_use_color` is enabled, or `""` when color is disabled.
2. **`detect_terminal_capabilities(is_tty)`**: Inspects `is_tty`, `NO_COLOR`, and `TERM` (`dumb`, `linux`, `vt100`, `vt220`) to enable only supported ANSI/DEC features.
3. **`term_write(text)`**: Writes `text` to `stdout` and flushes immediately.
4. **`enter_screen_compat()` / `leave_screen_compat()`**: Sends the supported subset of `ENTER_SCREEN` / `LEAVE_SCREEN` escape codes to switch to/from the alternate screen, hide/show the cursor, and toggle mouse reporting.

### Platform Initialization, Cleanup & Event Reading

5. **`win32_hide_legacy_cursor()`** *(Windows)*: Hides the Win32 caret via `SetConsoleCursorInfo()` when VT sequences are unavailable.
2. **`win32_clear_legacy_console()`** *(Windows)*: Fills the console buffer with spaces and homes the cursor via Win32 APIs when VT sequences are unavailable.
3. **`get_terminal_size(cols_out, rows_out)`** *(Windows & POSIX)*: Queries live terminal columns and rows (falling back to `80×24`).
4. **`input_ready(timeout_ms)`** *(POSIX)*: Uses `select()` to wait up to `timeout_ms` for input bytes, retrying on `EINTR`.
5. **`read_byte()`** *(POSIX)*: Reads 1 byte from `STDIN_FILENO`, retrying on `EINTR` and returning `-1` on EOF/error.
6. **`term_init()`** *(Windows & POSIX)*: Saves terminal state, enables raw input, detects capabilities, registers `term_restore()` with `atexit()`, and enters screen mode.
7. **`term_restore()`** *(Windows & POSIX)*: Idempotently restores screen buffer, cursor, mouse tracking, and saved OS terminal modes.
8. **`parse_sgr_mouse(sequence)`** *(POSIX)*: Parses `"<button;x;yM"`, converts 1-based terminal coordinates to 0-based `(row, col)`, ignores button release (`'m'`) and scroll wheel events, and emits `EV_MOUSE_MOVE` or `EV_MOUSE_PRESS`.
9. **`arrow_kind(final_character)`** *(POSIX)*: Maps `'A'..'D'` to `EV_UP`, `EV_DOWN`, `EV_RIGHT`, `EV_LEFT`.
10. **`term_read_event()`** *(Windows & POSIX)*: Blocks for the next OS input event and returns a normalized `InputEvent`.

### Screen State, Layout & Frame Rendering
 1. **`set_status(a, is_error, fmt, ...)`**: Formats a status/error message into `a->status` using `vsnprintf()`.
 2. **`app_screen(a, prompt, text)`**: Builds a `Screen` snapshot from the current `App` state.
 3. **`screen_to_cell(row, col, out)`**: Converts screen `(row, col)` to 0-based board coordinates `(0..7, 0..7)`, returning `false` if outside the board.
 4. **`detect_ui_mode(screen)`**: Infers the active `UiMode` (`WELCOME`, `COMMAND_INPUT`, `ACTIVE_PLAY`, `REPLAY`, or `POST_GAME`).
 5. **`mode_contextual_hint(mode)`**: Returns the footer shortcut hint string for the current `UiMode`.
 6. **`append_single_line(...)`**: Appends a single clipped line (`%.*s`) so header rows never wrap and shift `BOARD_TOP`.
 7. **`append_wrapped_line(...)`**: Word-wraps text cleanly at spaces without exceeding `rows_left` terminal rows.
 8. **`append_prompt_row(...)`**: Formats the `:` command input line with a `_` caret, scrolling horizontally if input exceeds terminal width.
 9. **`append_cell(...)`**: Renders one board cell (`X`, `O`, `*`, or `.`) with green/yellow background and inverse-video or `[X]` cursor highlighting.
10. **`describe_result(game, output, output_size)`**: Formats the final winner/tie summary and piece counts.
11. **`flush_terminal_frame(frame)`**: Homes the cursor, writes `frame` to `stdout`, and clears any trailing lines below.
12. **`render_welcome_screen(...)`**: Draws the title bar, grouped command reference, status, and prompt when no game is active.
13. **`render(screen)`**: Assembles the complete UI frame (welcome screen or active board + move history + piece tally + status + prompt + footer) and flushes it to the terminal.

### Command Parsing & Execution
 1. **`set_error(o, msg)`**: Sets `o->type = CMD_INVALID` and copies `msg` into `o->error`.
 2. **`tokenize(s, tok, err, err_size)`**: Splits a command line on whitespace while keeping `"quoted strings"` together and stripping quotes.
 3. **`take_name(value, dst, o)`**: Validates player name length (`1..31` chars) and copies it into `dst`.
 4. **`is_white_flag(arg)`**: Checks whether `arg` is `"w"` or `"W"`.
 5. **`parse_command(input, out)`**: Parses and validates `:n`, `:l`, `:w`, `:s`, `:v`, `:h`, `:q`, and `:t` along with both flag (`-p1=`, `-w=`, `-f=`) and positional arguments.
 6. **`edit_line(a, prompt, buf, size)`**: Runs an interactive single-line editor loop supporting typing, Backspace, Enter, and Esc.
 7. **`wait_key(a)`**: Blocks until any non-mouse key is pressed.
 8. **`command_prompt(a)`**: Prompts for a command via `edit_line()`, prepends `':'`, and invokes `run_command()`.
 9. **`run_command(a, line)`**: Parses `line` and dispatches the command (`init_game`, `load_game`, `save_game`, `view_replay`, `show_stats`, `show_help`, hint toggle, or quit).

### Full-Screen Sub-Views
 1. **`show_help(a)`**: Renders the full-screen controls/command reference and waits for a keypress.
 2. **`show_stats(a, paths, count)`**: Computes win/loss/tie statistics across save files via `compute_stats_summary()`, displays them, and waits for a keypress.
 3. **`view_replay(a, game, initial_step)`**: Runs an interactive step-by-step replay viewer supporting Left/Right stepping, multi-digit move-number jumps (`number = number * 10 + digit`), and `q`/`Esc` exit.
