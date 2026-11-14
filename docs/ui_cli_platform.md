# Terminal UI & Platform Layer (`src/ui_cli_platform.c`)

## 1. Role in the Architecture

`src/ui_cli_platform.c` acts as the hardware and operating-system boundary between the game engine and the user's terminal. The core game modules (`validation_capture.c`, `storage.c`, `stats.c`, `common.c`) operate purely on data structures (`GameState`, `Screen`, `InputEvent`) and never call OS-specific console functions directly.

This module is responsible for four tasks:

1. **Terminal Initialization (`term_init`):** Switches the terminal into raw/non-buffered input mode, detects terminal capabilities, and enables full-screen rendering.

2. **Input Translation (`term_read_event`):** Reads raw OS console records (Windows) or byte streams (POSIX/Linux) and normalizes them into platform-independent `InputEvent` structs.

3. **Frame Rendering (`render`):** Builds the entire 80-column UI frame in an in-memory buffer (`FRAME_SIZE` = 16,384 bytes) and flushes it to `stdout` in one write to prevent screen flicker.

4. **Terminal Cleanup (`term_restore`):** Re-enables the cursor, exits the alternate screen buffer, and restores original console/termios settings on exit.

---

## 2. Public Interface (`include/common.h`)

The rest of the application interacts with this module exclusively through five functions and three data types defined in `include/common.h`:

| Symbol | Purpose |
| --- | --- |
| `term_init()` | Saves current terminal state, configures raw input, detects VT/colour capabilities, and registers `term_restore()` with `atexit()`. |
| `term_restore()` | Idempotently restores screen state, cursor visibility, mouse modes, and original OS terminal flags. |
| `term_write(s)` | Writes a string to `stdout` and flushes immediately so escape sequences take effect without buffering delay. |
| `term_read_event()` | Blocks until an input event occurs and returns an `InputEvent` (`EV_UP`, `EV_DOWN`, `EV_LEFT`, `EV_RIGHT`, `EV_ENTER`, `EV_BACKSPACE`, `EV_ESC`, `EV_CHAR`, `EV_MOUSE_PRESS`, `EV_MOUSE_MOVE`, or `EV_QUIT`).

 |
| `render(screen)` | Draws the title bar, turn/status banner, 8×8 board, last 8 moves, piece counts, and status/input prompt from a `Screen` snapshot.

 |

---

## 3. ANSI / DEC Escape Sequences & Compile-Time Concatenation

### How Escape Sequences Work

Terminals distinguish printable text from control commands using **ANSI/ECMA-48 escape sequences**. Every sequence begins with the ASCII Escape byte (`0x1B`, written `\x1b` in C) followed by `[` — together called the **CSI** (*Control Sequence Introducer*).

Instead of hardcoding raw escape strings throughout the renderer, `ui_cli_platform.c` defines preprocessor macros that assemble sequences at compile time:

```c
#define CSI             "\x1b["
#define SGR(codes)      CSI codes "m"
#define DEC_SET(mode)   CSI "?" #mode "h"
#define DEC_RESET(mode) CSI "?" #mode "l"

```

### Why There Are No Commas in `ENTER_SCREEN` and `LEAVE_SCREEN`

In C, adjacent string literals separated only by whitespace are automatically merged into a single string literal during compilation:

```c
static const char *const ENTER_SCREEN =
    DEC_SET(1049)       /* "\x1b[?1049h" */
    DEC_SET(25)         /* "\x1b[?25l"   */
    DEC_SET(1003)       /* "\x1b[?1003h" */
    DEC_SET(1006)       /* "\x1b[?1006h" */
    ANSI_CLEAR_SCREEN;  /* "\x1b[2J"     */

```

Because `ENTER_SCREEN` is a single `const char *const` pointer (not an array of strings), omitting commas lets the compiler concatenate all five commands into one contiguous string with zero runtime overhead.

### Control Sequences Used in This Module

| Sequence | Macro | Effect |
| --- | --- | --- |
| `ESC[?1049h` / `l` | `DEC_SET(1049)` / `DEC_RESET(1049)` | Switches to/from the alternate screen buffer so the user's prior shell output is preserved on exit.

 |
| `ESC[?25l` / `h` | `DEC_SET(25)` / `DEC_RESET(25)` | Hides/shows the blinking terminal cursor (`l` = Low/Disable, `h` = High/Enable).

 |
| `ESC[?1003h` / `l` | `DEC_SET(1003)` / `DEC_RESET(1003)` | Enables/disables tracking of all mouse movement (used for hover highlighting).

 |
| `ESC[?1006h` / `l` | `DEC_SET(1006)` / `DEC_RESET(1006)` | Enables/disables SGR extended mouse reporting (`<button;x;yM`).

 |
| `ESC[2J` | `ANSI_CLEAR_SCREEN` | Clears the entire visible screen.

 |
| `ESC[H` | `ANSI_CURSOR_HOME` | Moves the cursor to row 1, column 1 (top-left) before drawing a frame.

 |
| `ESC[J` | `ANSI_CLEAR_BELOW` | Clears everything below the rendered frame.

 |
| `ESC[K` | `ANSI_CLEAR_EOL` | Clears from the cursor to the end of the current line.

 |
| `ESC[<n>m` | `SGR(...)` | Sets text attributes (`0` reset, `1` bold, `2` dim, `7` inverse) and foreground/background colours.

 |

### Pointer Constness (`static const char *const`)

* `static`: Restricts visibility of the symbol to `ui_cli_platform.c` (internal linkage).

* First `const`: The characters pointed to cannot be modified.

* Second `const`: The pointer itself cannot be reassigned to another address.

---

## 4. Platform Backends: Windows vs. POSIX (Linux / macOS)

Conditional compilation (`#ifdef _WIN32`) provides native terminal handling on both major platforms while exposing the exact same interface to `main.c`.

### Windows (`_WIN32`)

* **Input:** Uses `GetStdHandle(STD_INPUT_HANDLE)` and `ReadConsoleInput()` to read structured `INPUT_RECORD` events (`KEY_EVENT` and `MOUSE_EVENT`) directly from the Windows Console API.

* **Output:** Enables `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on `STD_OUTPUT_HANDLE` via `SetConsoleMode()` so modern terminals (Windows Terminal, PowerShell 7 host) process ANSI escape sequences natively.

* **Legacy Fallback:** If `ENABLE_VIRTUAL_TERMINAL_PROCESSING` fails (such as on older Windows 7/8 `conhost.exe`), `term_init()` disables VT sequences and falls back to Win32 API calls (`FillConsoleOutputCharacterA` and `SetConsoleCursorPosition`) with bracketed cursor indicators (`[X]`).

### POSIX (`Linux / Unix-like`)

* **Raw Input Mode:** By default, Unix terminals buffer input line-by-line until Enter is pressed. When `isatty(STDIN_FILENO)` is true, `term_init()` saves the current `struct termios` and clears `ICANON` (line buffering), `ECHO` (automatic character printing), `ISIG` (OS signal generation on `Ctrl+C`), `IXON` (`Ctrl+S`/`Ctrl+Q` flow control), and `ICRNL` (`\r` to `\n` translation).

* **Escape Sequence Disambiguation:** Pressing the `Esc` key sends a single `0x1B` byte, whereas pressing an arrow key or clicking the mouse sends a multi-byte sequence starting with `0x1B` (e.g., `ESC [ A` or `ESC [ < 0;12;5 M`). After reading `0x1B`, `input_ready(50)` uses `select()` with a 50 ms timeout:

* If no follow-up byte arrives within 50 ms, it returns `EV_ESC`.

* If `[` or `O` arrives immediately, it reads the remaining sequence bytes until a final command byte (`0x40`–`0x7E`) is reached.

* **Signal Safety (`EINTR`):** Both `read_byte()` and `input_ready()` loop if `read()` or `select()` returns `-1` with `errno == EINTR` (for instance, when the terminal window is resized), preventing false `EV_QUIT` events.

---

## 5. Character Encoding & Mouse Coordinate Mapping

### ASCII Character Constants

Input filtering accepts printable 7-bit ASCII characters (`' '` through `'~'`) and maps control bytes explicitly:

| Constant | Value | Meaning |
| --- | --- | --- |
| `ASCII_ETX` | `0x03` (3) | End of Text (`Ctrl+C`) — mapped to `EV_QUIT`.

 |
| `ASCII_BS` | `'\b'` (8) | Backspace (legacy terminals / `Ctrl+H`) — mapped to `EV_BACKSPACE`.

 |
| `ASCII_ESC` | `0x1B` (27) | Escape — starts CSI/SS3 sequences or emits `EV_ESC`.

 |
| `ASCII_SPACE` | `' '` (32) | Space — lowest printable ASCII character.

 |
| `ASCII_DEL` | `0x7F` (127) | Delete (sent by Backspace on modern terminals) — mapped to `EV_BACKSPACE`.

 |

Note: Multi-byte UTF-8 characters are outside the `32..126` range and are ignored by `term_read_event()`.

### SGR Mouse Coordinates

Terminal mouse sequences (`ESC[<button;x;yM`) report **1-based** column (`x`) and row (`y`) coordinates where the top-left cell is `(1, 1)`. `parse_sgr_mouse()` subtracts `1` from both values to convert them into **0-based** `(row, col)` coordinates used by the board layout:

* `final_character == 'M'` indicates a press or motion; `'m'` indicates button release (ignored).

* `button & 64` indicates a scroll-wheel event (ignored).

* `button & 32` indicates pointer motion (`EV_MOUSE_MOVE`).

* `(button & 3) == 0` indicates a primary (left) button press (`EV_MOUSE_PRESS`).

---

## 6. Compatibility, Fonts, and Terminal Requirements

* **No Nerd Fonts Required:** All board elements use standard 7-bit ASCII characters (`X` for Black, `O` for White, `*` for legal-move hints, and `.` for empty squares). Any standard monospace font works.

* **Capability Detection (`detect_terminal_capabilities`):**
* Checks `isatty(STDOUT_FILENO)` (or `SetConsoleMode` on Windows) so redirected output/pipes do not receive raw escape codes.
* Respects the `NO_COLOR` environment variable by disabling SGR colour codes and rendering the cursor as `[X]`.
* Detects `TERM=dumb` (disables all VT sequences) and `TERM=linux` / `vt100` / `vt220` (disables alternate screen and xterm mouse tracking while keeping basic cursor movement and colours).

* **Minimum Terminal Size:** The layout is formatted for an **80-column** terminal (`STATUS_WIDTH = 76`, title bar width `78`). Narrower windows will wrap lines and misalign the board and move history.

---

## 7. Glossary & References

### Abbreviations

* **ANSI** — American National Standards Institute

* **ASCII** — American Standard Code for Information Interchange

* **CLI** — Command-Line Interface

* **CSI** — Control Sequence Introducer (`ESC [`)

* **DEC** — Digital Equipment Corporation

* **DEL** — Delete control character (`0x7F` / 127)

* **EINTR** — Interrupted system call error code (`<errno.h>`)
* **ESC** — Escape control character (`0x1B` / 27)

* **ETX** — End of Text control character (`0x03` / 3)

* **POSIX** — Portable Operating System Interface

* **SGR** — Select Graphic Rendition

* **SS3** — Single Shift Three (`ESC O`)
* **TTY** — Teletypewriter (interactive terminal device)
* **UI** — User Interface

* **VT** — Virtual Terminal

### Official Documentation

* **C String Literals & Concatenation:** [https://en.cppreference.com/w/c/language/string_literal](https://en.cppreference.com/w/c/language/string_literal)

* **C Character Constants:** [https://en.cppreference.com/w/c/language/character_constant](https://en.cppreference.com/w/c/language/character_constant)

* **Windows Console Virtual Terminal Sequences:** [https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences)

* **Windows `SetConsoleMode`:** [https://learn.microsoft.com/en-us/windows/console/setconsolemode](https://learn.microsoft.com/en-us/windows/console/setconsolemode)

* **Windows `ReadConsoleInput`:** [https://learn.microsoft.com/en-us/windows/console/readconsoleinput](https://learn.microsoft.com/en-us/windows/console/readconsoleinput)

* **POSIX `termios.h`:** [https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/termios.h.html](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/termios.h.html)

* **POSIX `select()` & `read()`:** [https://pubs.opengroup.org/onlinepubs/9799919799/functions/select.html](https://pubs.opengroup.org/onlinepubs/9799919799/functions/select.html)

* **XTerm Control Sequences:** [https://invisible-island.net/xterm/ctlseqs/ctlseqs.html](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
