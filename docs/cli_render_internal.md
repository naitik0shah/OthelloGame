# cli_render.c: How the Othello Screen Is Drawn

This file draws the Othello program's full-screen text interface. It turns the
program's current state (the game, the cursor, and any message) into text, and
then sends that text to the terminal. It also converts a mouse click into a
board square.

This guide is written for readers who know basic C: functions, pointers,
structs, arrays, and `if`/`switch`. It explains the main functions in the order
they run.

---

## 1. The big picture

Every time the screen needs to change, the program does three things:

1. **Snapshot.** `app_screen()` copies the important program state into a
   `Screen` struct. The `Screen` is a read-only description of what to show.
2. **Build.** `render()` writes text into a large character array called the
   *frame buffer*. It writes the whole screen into this array first. Nothing is
   printed yet.
3. **Print.** `cli_flush_terminal_frame()` prints the whole frame in one go.
   Printing the whole screen at once, instead of line by line, stops the screen
   from flickering.

```
App state ──app_screen()──> Screen ──render()──> frame buffer ──cli_flush_terminal_frame()──> terminal
```

A **frame** is simply a string: one long `char` array that holds every line of
the screen, including the escape codes that set colours.

### Two ideas used everywhere

**Row budget.** The terminal has a fixed number of rows. Functions that write
lines take a pointer, `int *rows_left`, that holds how many rows are still free.
Each line written reduces this number by one, and a function does nothing once
the number reaches zero. This is how the program makes sure the footer and
prompt always fit at the bottom of the screen.

**Escape codes and styles.** Colours and bold text in a terminal are set by
special sequences of characters (for example, `\x1b[1m` for bold). The helper
`cli_style_str(SGR_...)` returns the right sequence for a named style.
Functions receive a `prefix_style` and `suffix_style` and wrap the text in them.
`eol` is an "erase to end of line" sequence. It is empty when the terminal does
not support these codes.

---

## 2. The functions, in the order they run

### `app_screen()` (public)

```c
Screen app_screen(const App *app, const char *input_prompt, const char *input_text);
```

Called by the main program loop before each redraw. It copies fields from `App`
into a `Screen`:

- `state` points at the game, or is `NULL` if no game has been loaded.
- `cursor` and `show_cursor` say where the highlighted square is.
- `status` and `status_error` hold the message shown at the bottom.
- `input_prompt` and `input_text` are set only while the user is typing a `:`
  command. Otherwise they are `NULL`.
- `help` is a fixed help string for play mode.

The `Screen` returned has `tag` set to `NULL`. Replay views set `tag` after this
call.

### `render()` (public): the main function

```c
void render(const Screen *screen);
```

This is the function that draws the whole screen. It runs in four steps:

1. **Measure the terminal.** `cli_get_terminal_size()` gives the width (`cols`)
   and height (`rows`). If no game is loaded, it hands off to the welcome view
   and returns.
2. **Header.** It writes the title line, `OTHELLO  Alice (X) vs Bob (O)`, and one
   status line. The status line shows whose turn it is, the result of a finished
   game, or the replay label, depending on the state.
3. **Board and history.** It writes the column letters `A`–`H`, then eight rows.
   Each row has a number on the left, eight squares, and, if the terminal is
   wide enough, a list of recent moves on the right. Each square is drawn by
   `append_board_cell()`.
4. **Footer.** It writes the piece counts, a status message, the command prompt,
   and a line of key hints. Then it calls `cli_flush_terminal_frame()`.

The `frame_buffer` is `static`. This means it lives for the whole program, not
just one call, so it does not take up stack space. The downside is that two
calls at the same time would overwrite each other's data.

### `determine_ui_mode()` (private)

Looks at the `Screen` and decides which of five modes applies: command input,
welcome, replay, game over, or active play. It checks these in a fixed order, so
the first match wins. For example, if a command prompt is open, the mode is
command input, even if a game is also loaded.

The mode is used only to choose the footer hint.

### `get_mode_hint()` (private)

Takes a mode and returns the line of key hints for it. It is a `switch` statement
that returns a fixed string for each mode. The string is never changed or
freed.

### `render_welcome_view()` (private)

Used when no game is loaded. It lists the commands (`:n`, `:l`, `:v`, `:s`, `:h`,
`:q`) and, on tall and wide terminals, also lists the board keys. The `spacious`
flag controls which of these are shown, so the screen never runs off the bottom.

### `cli_append_wrapped_line()` (public): the text workhorse

```c
int cli_append_wrapped_line(char *frame, size_t frame_size, const char *prefix_style,
                            const char *text, const char *suffix_style, const char *eol,
                            int max_cols, int indent_spaces, int *rows_left,
                            bool trailing_newline);
```

Most text in the screen goes through this function. It breaks `text` into
terminal rows and writes each row into the frame. It works like this:

- It keeps a pointer, `remaining`, that moves through the text.
- For each row, it counts characters until it reaches the row width. If the row
  ends in the middle of a word, it goes back to the last space, so words are not
  split.
- Rows after the first are indented by `indent_spaces`, so wrapped text lines up
  under the label.
- A `\n` in the text starts a new row.
- Each row is written with `append_fmt()`, which is a helper from another file
  that adds text to the frame safely.
- It stops early when `rows_left` reaches zero, and returns the number of rows
  it wrote.

Example: with a 40-column terminal, the text `"Arrows move a-h,1-8 jump Enter place"`
is split after `"jump"` onto a new row, because the row would be too long.

### `append_clipped_line()` (private)

A simpler cousin of the function above. It writes one row and cuts off anything
that does not fit, rather than wrapping. Used for the title and status lines, so
they always take exactly one row.

### `append_command_prompt_row()` (private)

Writes the command line. While the user is typing a `:` command, it shows the
prompt, the text typed so far, and an underscore as the cursor. If the typed text
is longer than the space available, it shows only the end of it. This is a
pointer trick: `typed + (typed_len - text_budget)` moves the pointer forward so
that only the last part of the string is printed.

When no command is being typed, it shows a short hint: "Press ':' to enter a
command", or the replay hint.

### `append_board_cell()` (private)

Draws one square. It decides three things:

- **Symbol:** `X` for a black piece, `O` for a white piece, `*` for a legal move
  (only when hints are on), and `.` for an empty square.
- **Colour:** based on the symbol, and a different background for the last move.
- **Cursor:** the cursor square is shown in inverse video. When colour is turned
  off, it is shown as `[X]` instead, so the cursor is still visible.

Every square is the same width, so the columns line up.

### `screen_to_cell()` (public): mouse clicks

```c
bool screen_to_cell(int row, int col, Position *out);
```

Converts a click position on the terminal into a board square. The board starts
at terminal row `BOARD_TOP` and column `BOARD_LEFT`, and each square is
`CELL_WIDTH` columns wide. The function subtracts the offset and divides the
column by the width to find the square:

```
board_row = row − BOARD_TOP
board_x   = col − BOARD_LEFT
square col = board_x / CELL_WIDTH
```

If the click is outside the board, the function returns `false` and does not
change `*out`. The caller must check the return value before using `*out`.

### `format_game_result()` (private)

Builds the text for a finished game, such as `Alice (Black) wins 36-28` or
`tie 32-32`. It always writes the winner's piece count first. It calls
`count_pieces()` and `get_game_result()`, which are defined in the game code.

### `cli_flush_terminal_frame()` (public)

Prints the finished frame. There are two paths:

- **VT terminals** (most modern terminals): move the cursor to the top-left
  corner, print the frame, then clear anything below it. Hiding the cursor
  during the update avoids a flashing cursor.
- **Legacy Windows console:** call `cli_win32_prepare_legacy_frame()` first, then
  print and flush the output.

---

## 3. A short walk-through of one redraw

Suppose the user has just typed `:v` and pressed Enter, and a game is loaded:

1. The main loop calls `app_screen()`. The `Screen` now has `state` set and
   `input_prompt` as `NULL`.
2. The main loop calls `render()`.
3. `render()` asks the terminal for its size.
4. It writes the title line with `cli_style_str(SGR_TITLE)` at the start and
   `cli_style_str(SGR_RESET)` at the end.
5. It writes the status line through `append_clipped_line()`.
6. It writes the column letters, then the eight board rows. For each row it calls
   `append_board_cell()` eight times.
7. If the terminal is wide enough, it writes the move list beside each row.
8. It writes the piece counts, the status message, the prompt row, and the hint.
9. It calls `cli_flush_terminal_frame()`, which prints everything at once.

When the user clicks the board, the main loop calls `screen_to_cell()` with the
click's row and column. If it returns `true`, the program knows which square was
clicked.

---

## 4. Glossary

| Term | Meaning |
|------|---------|
| Frame | The whole screen as one text buffer |
| Snapshot | A `Screen` struct with everything needed to draw one frame |
| Row budget | The number of terminal rows still free, tracked by `rows_left` |
| `eol` | The "erase to end of line" escape code (or empty) |
| SGR | Select Graphic Rendition: escape codes that set colour and bold |
| VT sequence | Escape codes understood by most modern terminals |

---

## 5. Notes and things to check

- **Not reentrant.** `render()` uses a `static` frame buffer. Calling it from two
  threads at once, or calling it recursively, would corrupt the output.
- **Buffer limits depend on `append_fmt()`.** Every write goes through
  `append_fmt()`, which is defined in another file. This file trusts that
  function to stop writing when the frame is full. If `append_fmt()` does not
  check the size, a long screen could overflow `frame_buffer` (a buffer-overflow
  risk). Check that function before relying on the frame size.
- **Input text is printed as-is.** Player names and status messages are written
  to the terminal without removing control characters. A name that contains
  escape codes could change how the terminal looks. Filter names where they are
  entered, if this matters for your use.
- **Layout constants are shared.** `BOARD_TOP`, `BOARD_LEFT`, and `CELL_WIDTH`
  are defined in `cli_internal.h`. Changing them moves the board on screen, and
  `screen_to_cell()` will follow automatically, but only if both files are kept
  in step.
- **Not compiled here.** The header `cli_internal.h` and the helper functions
  (`append_fmt`, `cli_style_str`, and others) were not available, so this
  rewrite was checked by reading, not by compiling. Build it with
  `-Wall -Wextra` before merging.
