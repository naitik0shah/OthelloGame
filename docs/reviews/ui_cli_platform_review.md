# Terminal UI module review

**Scope:** `src/ui_cli_platform.c` only  
**Reviewed revision:** `95dfd13d48902da87a772b2c0f4b7809ada163bb`  
**Updated source revision:** `6c0f181ff8df65af91ac51ff6dfbf58ff7bb225f`

This report records portability and reliability concerns found while reviewing the terminal UI module. It is intentionally separate from the source file so it can be shared with the person responsible for this module.

## Findings

### 1. Terminal output is not checked before sending control sequences

**Area:** `term_init()`, `term_restore()`  
**Impact:** Running with redirected output, a pipe, or a non-terminal output destination can send ANSI control sequences into a file or another program. On the POSIX path, only standard input is checked with `isatty()`; standard output is not checked. The Windows path similarly assumes terminal output.

**Suggested follow-up:** Check that standard output is an interactive terminal before enabling screen-control sequences. Decide explicitly whether redirected/non-interactive use should fail with a clear message or use a plain-text mode.

### 2. Older terminals may not support the mouse and alternate-screen extensions

**Area:** `ENTER_SCREEN`, `LEAVE_SCREEN`, `parse_sgr_mouse()`  
**Impact:** The program requests DEC alternate-screen mode and xterm-style mouse modes 1003 and 1006. Older terminal emulators and some embedded/IDE consoles may not implement these extensions. Basic colour and cursor sequences can work while mouse input does not.

**Suggested follow-up:** Add terminal capability detection or a configurable keyboard-only/basic-output mode. Avoid assuming that support for ANSI colours implies support for mouse reporting.

### 3. Windows console mode changes are not fully checked

**Area:** Windows `term_init()`  
**Impact:** Failure to enable input modes is currently ignored. Failure to enable virtual-terminal output processing is also ignored, even though the renderer relies on escape sequences. The program can continue after only part of initialization succeeded.

**Suggested follow-up:** Check each `SetConsoleMode()` result. If initialization fails after changing input mode, restore the saved input mode before returning failure. If virtual-terminal output is unavailable, report the limitation or use a supported fallback.

### 4. POSIX input reads treat interruptions as end-of-input

**Area:** `read_byte()`, `input_ready()`  
**Impact:** POSIX `read()` and `select()` can be interrupted by a signal and return `-1` with `errno == EINTR`. The current code treats that as a failed read/no input. In `term_read_event()`, a failed byte read is interpreted as a quit event.

**Suggested follow-up:** Retry interrupted system calls when `errno == EINTR`; distinguish end-of-file and genuine I/O errors from an interrupted call.

### 5. Input supports printable ASCII, not general UTF-8 text

**Area:** POSIX and Windows character handling  
**Impact:** The accepted character range is limited to basic printable ASCII. Multi-byte UTF-8 characters are not decoded as complete characters. This is fine for the current board notation and likely command syntax, but player names or prompts should not be assumed to support arbitrary Unicode input through this event path.

**Suggested follow-up:** Either document the ASCII-only input limitation in user-facing instructions or implement UTF-8 input decoding if international names/text are a requirement.

### 6. Terminal dimensions are assumed rather than checked

**Area:** `render()` and fixed layout constants in `include/common.h`  
**Impact:** The layout is designed around approximately 80 columns and a fixed number of rows. Smaller terminals can wrap or clip the board/history. This is a usability limitation rather than a game-logic error.

**Suggested follow-up:** Query terminal dimensions and show a clear resize/too-small message, or document a minimum supported terminal size.

### 7. Mouse tracking requests every movement event

**Area:** `ENTER_SCREEN` mode 1003  
**Impact:** Mode 1003 requests reports for all mouse movement, including movement without a pressed button. This can generate many input events. The application currently uses movement events to update the UI, but terminals and remote connections may experience unnecessary event traffic.

**Suggested follow-up:** If hover movement is not essential, consider a less chatty tracking mode or enable movement reporting only while a button is held. Verify the desired interaction before changing the mode.

### 8. Terminal restoration depends on the application calling the cleanup function

**Area:** `term_restore()` and application shutdown path  
**Impact:** If the process terminates unexpectedly or exits through a path that does not call `term_restore()`, the cursor, alternate screen, mouse mode, or POSIX input settings may remain changed for that terminal session.

**Suggested follow-up:** Review all application exit paths and ensure cleanup is called exactly when initialization has changed terminal state. Signal handling can be considered, but cleanup from signal handlers must be designed carefully.

## Verification status

- The source was reviewed from the repository and updated through the GitHub connector.
- No Linux, Windows Terminal, PowerShell 7, or legacy-terminal runtime test was executed as part of this review.
- The source's POSIX branch uses standard POSIX terminal APIs and is intended for Linux/Unix-like systems.
- The source does not use Nerd Font glyphs. Its board symbols are ordinary ASCII characters (`X`, `O`, `*`, and `.`), so Nerd Fonts are not a requirement.
- A modern terminal with ANSI/VT support is still needed for the intended colours, cursor control, alternate screen, and mouse interactions.

## References

- Microsoft Console Virtual Terminal Sequences: https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
- Microsoft SetConsoleMode: https://learn.microsoft.com/en-us/windows/console/setconsolemode
- Microsoft ReadConsoleInput: https://learn.microsoft.com/en-us/windows/console/readconsoleinput
- POSIX termios: https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/termios.h.html
- POSIX read: https://pubs.opengroup.org/onlinepubs/9799919799/functions/read.html
- POSIX select: https://pubs.opengroup.org/onlinepubs/9799919799/functions/select.html
- xterm Control Sequences: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
