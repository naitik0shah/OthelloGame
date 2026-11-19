/**
 * @file cli_render.c
 * @brief Full-screen terminal renderer for the Othello CLI.
 *
 * Each screen is composed as plain text in an off-screen frame buffer and then
 * written to the terminal in a single operation, which avoids flicker. The file
 * also maps mouse positions back to board squares.
 *
 * The board's first row is drawn at terminal row BOARD_TOP. Header and footer
 * lines are clipped or wrapped so that they never take more terminal rows than
 * their budget, which keeps the board in the rows that screen_to_cell() expects.
 */

#include <cli_internal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Interface mode that selects the layout and the footer hint.
 */
typedef enum {
    UI_MODE_WELCOME,       /**< No game is loaded. */
    UI_MODE_COMMAND_INPUT, /**< The user is typing a colon command. */
    UI_MODE_ACTIVE_PLAY,   /**< A game is in progress and accepts moves. */
    UI_MODE_REPLAY,        /**< Past moves are shown read-only. */
    UI_MODE_POST_GAME      /**< The loaded or completed game is over. */
} UiMode;

/**
 * @brief Build a render snapshot from the application state.
 *
 * The snapshot copies values from @p app and does not keep a pointer to it
 * beyond the fields that refer to the game state. Screen::tag is always NULL in
 * the returned snapshot.
 *
 * @param app          Application state to copy from.
 * @param input_prompt Command prompt to display, or NULL when not entering a command.
 * @param input_text   Text typed so far at the command prompt, or NULL.
 * @return Snapshot to pass to render().
 */
Screen app_screen(const App *app, const char *input_prompt, const char *input_text)
{
    static const char *const PLAY_HELP_TEXT =
        "Arrows move  a-h,1-8 jump  Enter/click place  :v view moves  :command  "
        ":h help";

    Screen snapshot;
    snapshot.state = app->have_game ? &app->game : NULL;
    snapshot.cursor = app->cursor;
    snapshot.show_cursor = app->have_game;
    snapshot.hints = app->hints;
    snapshot.tag = NULL;
    snapshot.status = app->status;
    snapshot.status_error = app->status_error;
    snapshot.input_prompt = input_prompt;
    snapshot.input_text = input_text;
    snapshot.help = PLAY_HELP_TEXT;
    return snapshot;
}

/**
 * @brief Convert a terminal mouse position into a board square.
 *
 * @param row Terminal row of the mouse event.
 * @param col Terminal column of the mouse event.
 * @param out Receives the board square when the position is on the board.
 *            Not modified when the function returns false.
 * @return true if (@p row, @p col) lies on a board square, otherwise false.
 */
bool screen_to_cell(int row, int col, Position *out)
{
    const int board_row = row - BOARD_TOP;
    const int board_x = col - BOARD_LEFT;

    if (board_row < 0 || board_row >= BOARD_SIZE || board_x < 0 ||
        board_x >= BOARD_SIZE * CELL_WIDTH) {
        return false;
    }

    out->row = board_row;
    out->col = board_x / CELL_WIDTH;
    return true;
}

/**
 * @brief Determine the interface mode from a render snapshot.
 *
 * Checks are made in priority order: command input, welcome, replay,
 * post-game, and finally active play.
 *
 * @param screen Snapshot to inspect.
 * @return The UiMode that matches the snapshot.
 */
static UiMode determine_ui_mode(const Screen *screen)
{
    if (screen->input_prompt) {
        return UI_MODE_COMMAND_INPUT;
    }
    if (!screen->state) {
        return UI_MODE_WELCOME;
    }
    if (screen->tag) {
        return UI_MODE_REPLAY;
    }
    if (is_game_over(screen->state)) {
        return UI_MODE_POST_GAME;
    }
    return UI_MODE_ACTIVE_PLAY;
}

/**
 * @brief Return the footer hint that lists the keys available in a mode.
 *
 * @param mode Interface mode.
 * @return Pointer to a static, null-terminated string.
 */
static const char *get_mode_hint(UiMode mode)
{
    switch (mode) {
    case UI_MODE_WELCOME:
        return "Commands: :n [p1] [p2] [w] | :l <file> | :v [file] | :s <files> | "
               ":h help | :q quit";

    case UI_MODE_COMMAND_INPUT:
        return "Input: Enter execute | Backspace edit | Esc cancel";

    case UI_MODE_ACTIVE_PLAY:
        return "Play: Arrows move | a-h,1-8 jump | Enter/Click place | :v view "
               "moves | :t hints | :w <file> save";

    case UI_MODE_REPLAY:
        return "Replay: Left back | Right/Space forward | 0-60 + Enter jump | "
               "q/Esc exit";

    case UI_MODE_POST_GAME:
    default:
        return "Session: :n new game | :w <file> save | :l <file> load | :v [file] "
               "view | :s <files> stats | :q quit";
    }
}

/**
 * @brief Append one line of text, truncated so it fits in @p max_cols.
 *
 * The line is always one terminal row. Text that does not fit is cut off at
 * the right edge rather than wrapped.
 *
 * @param frame        Destination frame buffer.
 * @param frame_size   Capacity of @p frame in bytes.
 * @param prefix_style ANSI style sequence written before the text.
 * @param text         Text to append. NULL is treated as an empty string.
 * @param suffix_style ANSI style sequence written after the text (usually a reset).
 * @param eol          End-of-line sequence (erase-to-end-of-line or empty).
 * @param max_cols     Terminal width in columns.
 * @param rows_left    Remaining row budget, or NULL. Skipped when the budget is
 *                     exhausted; decremented when a line is written.
 */
static void append_clipped_line(char *frame, size_t frame_size, const char *prefix_style,
                                const char *text, const char *suffix_style, const char *eol,
                                int max_cols, int *rows_left)
{
    if (rows_left && *rows_left <= 0) {
        return;
    }

    const int visible_cols = (max_cols > 4) ? (max_cols - 3) : 1;
    append_fmt(frame, frame_size, "  %s%.*s%s%s\n", prefix_style, visible_cols, text ? text : "",
               suffix_style, eol);

    if (rows_left) {
        (*rows_left)--;
    }
}

/**
 * @brief Append text word-wrapped to the terminal width, within a row budget.
 *
 * Wrapping happens at the last space that fits. Explicit '\n' characters in
 * @p text start a new row. Continuation rows are indented by @p indent_spaces
 * more than the first row. Output stops early if the row budget runs out.
 *
 * @param frame            Destination frame buffer.
 * @param frame_size       Capacity of @p frame in bytes.
 * @param prefix_style     ANSI style sequence written before each row's text.
 * @param text             Text to wrap. An empty or NULL text writes one blank row.
 * @param suffix_style     ANSI style sequence written after each row's text.
 * @param eol              End-of-line sequence (erase-to-end-of-line or empty).
 * @param max_cols         Terminal width in columns.
 * @param indent_spaces    Extra indentation for continuation rows. Clamped to
 *                         the range [0, usable width / 2].
 * @param rows_left        Remaining row budget, or NULL for no limit. Decremented
 *                         for each row written.
 * @param trailing_newline true to end the last row with a newline; false to leave
 *                         the cursor on that row.
 * @return Number of terminal rows written.
 *
 * @note Each row ends with @p eol and a newline when more rows follow or
 *       @p trailing_newline is true.
 */
int cli_append_wrapped_line(char *frame, size_t frame_size, const char *prefix_style,
                            const char *text, const char *suffix_style, const char *eol,
                            int max_cols, int indent_spaces, int *rows_left, bool trailing_newline)
{
    if (rows_left && *rows_left <= 0) {
        return 0;
    }

    if (!text || text[0] == '\0') {
        append_fmt(frame, frame_size, "%s%s", eol, trailing_newline ? "\n" : "");
        if (rows_left) {
            (*rows_left)--;
        }
        return 1;
    }

    const int usable_cols = (max_cols > MIN_WRAP_COLS) ? (max_cols - 3) : MIN_WRAP_COLS;
    if (indent_spaces < 0) {
        indent_spaces = 0;
    }
    if (indent_spaces > usable_cols / 2) {
        indent_spaces = usable_cols / 2;
    }

    const char *remaining = text;
    bool first_line = true;
    int emitted_rows = 0;

    while (*remaining != '\0') {
        if (rows_left && *rows_left <= 0) {
            break;
        }

        if (*remaining == '\n') {
            remaining++;
            const bool more_follows = (*remaining != '\0') && (!rows_left || *rows_left > 1);
            append_fmt(frame, frame_size, "%s%s", eol,
                       (more_follows || trailing_newline) ? "\n" : "");
            if (rows_left) {
                (*rows_left)--;
            }
            emitted_rows++;
            first_line = false;
            continue;
        }

        const int indent = first_line ? 2 : (2 + indent_spaces);
        int line_budget = usable_cols - indent;
        if (line_budget < 8) {
            line_budget = 8;
        }

        int line_len = 0;
        int last_space_idx = -1;
        while (remaining[line_len] != '\0' && remaining[line_len] != '\n' &&
               line_len < line_budget) {
            if (remaining[line_len] == ' ') {
                last_space_idx = line_len;
            }
            line_len++;
        }

        const bool cut_mid_word = remaining[line_len] != '\0' && remaining[line_len] != '\n' &&
                                  remaining[line_len] != ' ';
        if (cut_mid_word && last_space_idx > 0) {
            line_len = last_space_idx;
        }

        const char *next_start = remaining + line_len;
        while (*next_start == ' ') {
            next_start++;
        }
        if (*next_start == '\n') {
            next_start++;
        }

        const bool has_more_text = (*next_start != '\0') && (!rows_left || *rows_left > 1);
        append_fmt(frame, frame_size, "%*s%s%.*s%s%s%s", indent, "", prefix_style, line_len,
                   remaining, suffix_style, eol, (has_more_text || trailing_newline) ? "\n" : "");

        if (rows_left) {
            (*rows_left)--;
        }
        emitted_rows++;
        remaining = next_start;
        first_line = false;
    }

    return emitted_rows;
}

/**
 * @brief Append the one-line command prompt, or a short hint when no prompt is open.
 *
 * Long command text scrolls horizontally so the row never wraps.
 *
 * @param frame      Destination frame buffer.
 * @param frame_size Capacity of @p frame in bytes.
 * @param screen     Snapshot to display.
 * @param eol        End-of-line sequence (erase-to-end-of-line or empty).
 * @param max_cols   Terminal width in columns.
 * @param rows_left  Remaining row budget, or NULL. Decremented when a row is written.
 */
static void append_command_prompt_row(char *frame, size_t frame_size, const Screen *screen,
                                      const char *eol, int max_cols, int *rows_left)
{
    if (rows_left && *rows_left <= 0) {
        return;
    }

    if (screen->input_prompt) {
        const char *prompt = screen->input_prompt;
        const char *typed = screen->input_text ? screen->input_text : "";
        const int prompt_len = (int)strlen(prompt);
        const int typed_len = (int)strlen(typed);

        int text_budget = max_cols - prompt_len - 5;
        if (text_budget < 4) {
            text_budget = 4;
        }

        /* Show the end of the typed text when it is longer than the budget. */
        const char *visible_text = typed;
        if (typed_len > text_budget) {
            visible_text = typed + (typed_len - text_budget);
        }

        append_fmt(frame, frame_size, "  %s%s%s%s%s_%s%s\n", cli_style_str(SGR_TAG), prompt,
                   cli_style_str(SGR_RESET), visible_text, cli_style_str(SGR_HINT),
                   cli_style_str(SGR_RESET), eol);
    } else if (screen->tag) {
        append_fmt(frame, frame_size, "  %sReplay mode active (press q or Esc to exit)%s%s\n",
                   cli_style_str(SGR_DIM), cli_style_str(SGR_RESET), eol);
    } else {
        append_fmt(frame, frame_size, "  %sPress ':' to enter a command%s%s\n",
                   cli_style_str(SGR_DIM), cli_style_str(SGR_RESET), eol);
    }

    if (rows_left) {
        (*rows_left)--;
    }
}

/**
 * @brief Append one board square with its piece, hint, last-move, and cursor styling.
 *
 * Squares are drawn three characters wide (CELL_WIDTH). Without color, the
 * cursor square is drawn as "[symbol]" instead of using inverse video.
 *
 * @param frame       Destination frame buffer.
 * @param frame_size  Capacity of @p frame in bytes.
 * @param game        Game whose board is shown.
 * @param screen      Snapshot that holds the cursor and display flags.
 * @param legal_moves Legal moves for the side to move; true marks a square to hint.
 * @param row         Zero-based board row.
 * @param col         Zero-based board column.
 */
static void append_board_cell(char *frame, size_t frame_size, const GameState *game,
                              const Screen *screen, bool legal_moves[BOARD_SIZE][BOARD_SIZE],
                              int row, int col)
{
    const CellState cell = game->board[row][col];

    const bool cursor_is_here =
        screen->show_cursor && screen->cursor.row == row && screen->cursor.col == col;

    const bool is_last_move = game->move_count > 0 &&
                              game->history[game->move_count - 1].pos.row == row &&
                              game->history[game->move_count - 1].pos.col == col;

    const char *style = cli_style_str(SGR_DIM);
    char symbol = '.';

    if (cell == CELL_BLACK) {
        style = cli_style_str(SGR_BLACK);
        symbol = 'X';
    } else if (cell == CELL_WHITE) {
        style = cli_style_str(SGR_WHITE);
        symbol = 'O';
    } else if (legal_moves[row][col]) {
        style = cli_style_str(SGR_HINT);
        symbol = '*';
    }

    if (is_last_move) {
        style = cli_style_str(SGR_BG_LAST);
    }

    if (!cli_use_color() && cursor_is_here) {
        append_fmt(frame, frame_size, "[%c]", symbol);
    } else {
        append_fmt(frame, frame_size, "%s%s%s %c %s", cli_style_str(SGR_BG_BOARD),
                   cursor_is_here ? cli_style_str(SGR_INVERSE) : "", style, symbol,
                   cli_style_str(SGR_RESET));
    }
}

/**
 * @brief Write the final result and piece counts of a game as text.
 *
 * The winner's count is always written first. A draw is written as "tie".
 *
 * @param game        Finished game.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output in bytes.
 */
static void format_game_result(const GameState *game, char *output, size_t output_size)
{
    int black_pieces;
    int white_pieces;
    count_pieces(game, &black_pieces, &white_pieces);

    switch (get_game_result(game)) {
    case RESULT_BLACK_WIN:
        snprintf(output, output_size, "%s (Black) wins %d-%d", player_name(game, PLAYER_BLACK),
                 black_pieces, white_pieces);
        break;

    case RESULT_WHITE_WIN:
        snprintf(output, output_size, "%s (White) wins %d-%d", player_name(game, PLAYER_WHITE),
                 white_pieces, black_pieces);
        break;

    default:
        snprintf(output, output_size, "tie %d-%d", black_pieces, white_pieces);
        break;
    }
}

/**
 * @brief Write a completed frame to the terminal.
 *
 * With VT (ANSI) sequences, the cursor is moved home, the frame is printed, and
 * anything below it is cleared. Otherwise the legacy console is prepared first
 * and the frame is printed and flushed.
 *
 * @param frame Null-terminated frame to display.
 */
void cli_flush_terminal_frame(const char *frame)
{
    if (cli_use_vt_sequences()) {
        term_write(ANSI_CURSOR_HOME ANSI_CURSOR_HIDE);
        fputs(frame, stdout);
        term_write(ANSI_CLEAR_BELOW ANSI_CURSOR_HOME ANSI_CURSOR_HIDE);
    } else {
        cli_win32_prepare_legacy_frame();
        fputs(frame, stdout);
        fflush(stdout);
    }
}

/**
 * @brief Build the welcome screen shown when no game is loaded.
 *
 * Lists the session commands. On tall, wide terminals it also lists the
 * board-play and command-input keys, with spacing between sections.
 *
 * @param frame      Destination frame buffer.
 * @param frame_size Capacity of @p frame in bytes.
 * @param screen     Snapshot to display; its state is NULL.
 * @param eol        End-of-line sequence (erase-to-end-of-line or empty).
 * @param cols       Terminal width in columns.
 * @param rows       Terminal height in rows.
 */
static void render_welcome_view(char *frame, size_t frame_size, const Screen *screen,
                                const char *eol, int cols, int rows)
{
    const int total_rows = (rows > 6) ? rows : 6;
    const bool spacious = (total_rows >= 25 && cols >= 72);

    int body_rows = total_rows - 4;
    const int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    append_fmt(frame, frame_size, "%s%-*s%s%s\n", cli_style_str(SGR_TITLE), title_width,
               "  OTHELLO " OTHELLO_VERSION, cli_style_str(SGR_RESET), eol);
    body_rows--;

    char welcome_msg[64];
    snprintf(welcome_msg, sizeof welcome_msg, "Welcome to Othello (%s)!", OTHELLO_VERSION);
    cli_append_wrapped_line(frame, frame_size, cli_style_str(SGR_LABEL), welcome_msg,
                            cli_style_str(SGR_RESET), eol, cols, 0, &body_rows, true);

    if (spacious && body_rows > 1) {
        append_fmt(frame, frame_size, "%s\n", eol);
        body_rows--;
    }

    cli_append_wrapped_line(frame, frame_size, cli_style_str(SGR_TAG),
                            "[1] SESSION COMMANDS (Any Mode)", cli_style_str(SGR_RESET), eol, cols,
                            0, &body_rows, true);
    cli_append_wrapped_line(frame, frame_size, "", "  :n [p1] [p2] [w]  New game ('w' = P1 White)",
                            "", eol, cols, 4, &body_rows, true);
    cli_append_wrapped_line(frame, frame_size, "", "  :l <file>         Load saved game", "", eol,
                            cols, 4, &body_rows, true);
    cli_append_wrapped_line(frame, frame_size, "",
                            "  :v [file]         View current or saved game moves", "", eol, cols,
                            4, &body_rows, true);
    cli_append_wrapped_line(frame, frame_size, "",
                            "  :s <f1> [f2...]   Win/loss stats across files", "", eol, cols, 4,
                            &body_rows, true);
    cli_append_wrapped_line(frame, frame_size, "", "  :h                Show full command help", "",
                            eol, cols, 4, &body_rows, true);
    cli_append_wrapped_line(frame, frame_size, "", "  :q                Quit Othello", "", eol,
                            cols, 4, &body_rows, true);

    if (spacious) {
        if (body_rows > 1) {
            append_fmt(frame, frame_size, "%s\n", eol);
            body_rows--;
        }
        cli_append_wrapped_line(frame, frame_size, cli_style_str(SGR_TAG),
                                "[2] BOARD PLAY MODE (Active Game)", cli_style_str(SGR_RESET), eol,
                                cols, 0, &body_rows, true);
        cli_append_wrapped_line(frame, frame_size, "",
                                "  Arrow keys        Move cursor on 8x8 board", "", eol, cols, 4,
                                &body_rows, true);
        cli_append_wrapped_line(frame, frame_size, "", "  a-h, 1-8          Jump to column or row",
                                "", eol, cols, 4, &body_rows, true);
        cli_append_wrapped_line(frame, frame_size, "", "  Enter / Click     Place piece on square",
                                "", eol, cols, 4, &body_rows, true);
        cli_append_wrapped_line(frame, frame_size, "",
                                "  :t                Toggle legal-move hints (*)", "", eol, cols, 4,
                                &body_rows, true);
        cli_append_wrapped_line(frame, frame_size, "", "  :w <file>         Save current game", "",
                                eol, cols, 4, &body_rows, true);

        if (body_rows > 1) {
            append_fmt(frame, frame_size, "%s\n", eol);
            body_rows--;
        }
        cli_append_wrapped_line(frame, frame_size, cli_style_str(SGR_TAG),
                                "[3] COMMAND INPUT (After ':')", cli_style_str(SGR_RESET), eol,
                                cols, 0, &body_rows, true);
        cli_append_wrapped_line(frame, frame_size, "",
                                "  Enter execute  |  Backspace edit  |  Esc cancel", "", eol, cols,
                                4, &body_rows, true);
    } else {
        cli_append_wrapped_line(frame, frame_size, cli_style_str(SGR_TAG),
                                "[2] PLAY: Arrows, a-h/1-8, Enter/Click, :v view, "
                                ":t hints, :w <file> save",
                                cli_style_str(SGR_RESET), eol, cols, 2, &body_rows, true);
    }

    if (body_rows > 0) {
        append_fmt(frame, frame_size, "%s\n", eol);
        body_rows--;
    }

    int footer_rows = 4;

    if (screen->status && screen->status[0] != '\0') {
        cli_append_wrapped_line(
            frame, frame_size, cli_style_str(screen->status_error ? SGR_ERROR : SGR_OK),
            screen->status, cli_style_str(SGR_RESET), eol, cols, 0, &footer_rows, true);
    } else {
        cli_append_wrapped_line(frame, frame_size, cli_style_str(SGR_OK),
                                "Type :n for a new game, :h for help, :q to quit.",
                                cli_style_str(SGR_RESET), eol, cols, 0, &footer_rows, true);
    }

    append_command_prompt_row(frame, frame_size, screen, eol, cols, &footer_rows);

    cli_append_wrapped_line(
        frame, frame_size, cli_style_str(SGR_DIM),
        get_mode_hint(screen->input_prompt ? UI_MODE_COMMAND_INPUT : UI_MODE_WELCOME),
        cli_style_str(SGR_RESET), eol, cols, 0, &footer_rows, false);
}

/**
 * @brief Draw a complete screen and write it to the terminal.
 *
 * Without a game, this draws the welcome view. With a game, it draws, in order:
 *  - a title line with the player names;
 *  - a status line (replay tag, game result, or whose turn it is);
 *  - the column labels;
 *  - the 8x8 board, with an optional move-history column on the right when the
 *    terminal is wide enough (MIN_COLS_WITH_HISTORY);
 *  - a piece count line;
 *  - the status message, the command prompt, and the footer hint.
 *
 * Output is built in a static buffer and written with cli_flush_terminal_frame().
 *
 * @param screen Snapshot to display. Must not be NULL.
 *
 * @note The frame buffer is static, so this function is not reentrant and must
 *       not be called from more than one thread at a time.
 */
void render(const Screen *screen)
{
    static char frame_buffer[FRAME_SIZE];

    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;
    cli_get_terminal_size(&cols, &rows);

    const char *eol = cli_use_vt_sequences() ? ANSI_CLEAR_EOL : "";
    const GameState *game = screen->state;
    const UiMode mode = determine_ui_mode(screen);
    char line_text[160];
    bool legal_moves[BOARD_SIZE][BOARD_SIZE];

    frame_buffer[0] = '\0';

    if (!game) {
        render_welcome_view(frame_buffer, sizeof frame_buffer, screen, eol, cols, rows);
        cli_flush_terminal_frame(frame_buffer);
        return;
    }

    int rows_left = (rows > 14) ? rows : 14;
    const int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    snprintf(line_text, sizeof line_text, "  OTHELLO    %s (X)  vs  %s (O)",
             player_name(game, PLAYER_BLACK), player_name(game, PLAYER_WHITE));
    append_fmt(frame_buffer, sizeof frame_buffer, "%s%-*s%s%s\n", cli_style_str(SGR_TITLE),
               title_width, line_text, cli_style_str(SGR_RESET), eol);
    rows_left--;

    if (screen->tag) {
        snprintf(line_text, sizeof line_text, "%s  (Read-Only Replay)", screen->tag);
        append_clipped_line(frame_buffer, sizeof frame_buffer, cli_style_str(SGR_TAG), line_text,
                            cli_style_str(SGR_RESET), eol, cols, &rows_left);
    } else if (is_game_over(game)) {
        char result_text[96];
        format_game_result(game, result_text, sizeof result_text);
        snprintf(line_text, sizeof line_text, "GAME OVER: %s  (:w <file> save | :n new | :q quit)",
                 result_text);
        append_clipped_line(frame_buffer, sizeof frame_buffer, cli_style_str(SGR_TAG), line_text,
                            cli_style_str(SGR_RESET), eol, cols, &rows_left);
    } else {
        snprintf(line_text, sizeof line_text, "Turn: %s (%s)   [%s]",
                 player_name(game, game->current_turn), color_name(game->current_turn),
                 screen->hints ? "Hints ON (*)" : "Hints OFF (:t)");
        append_clipped_line(frame_buffer, sizeof frame_buffer, "", line_text, "", eol, cols,
                            &rows_left);
    }

    const bool show_history_pane = (cols >= MIN_COLS_WITH_HISTORY);
    append_fmt(frame_buffer, sizeof frame_buffer, "   ");

    for (int col = 0; col < BOARD_SIZE; col++) {
        append_fmt(frame_buffer, sizeof frame_buffer, "%s %c %s", cli_style_str(SGR_LABEL),
                   'A' + col, cli_style_str(SGR_RESET));
    }

    if (show_history_pane) {
        append_fmt(frame_buffer, sizeof frame_buffer, "   %sMoves%s%s\n", cli_style_str(SGR_LABEL),
                   cli_style_str(SGR_RESET), eol);
    } else {
        append_fmt(frame_buffer, sizeof frame_buffer, "%s\n", eol);
    }
    rows_left--;

    memset(legal_moves, 0, sizeof legal_moves);

    if (screen->hints && !is_game_over(game)) {
        get_all_valid_moves(game, game->current_turn, legal_moves);
    }

    const GameState *replay_full_game = cli_get_replay_game();
    const GameState *history_game = (screen->tag && replay_full_game) ? replay_full_game : game;
    const int total_history_moves = history_game->move_count;
    const int active_history_idx = game->move_count - 1;

    /* Index of the first move shown in the history column. */
    int first_history_row = 0;
    if (total_history_moves > HISTORY_ROWS) {
        if (screen->tag && replay_full_game) {
            const int anchor = (active_history_idx >= 0) ? active_history_idx : 0;
            first_history_row =
                clamp_int(anchor - HISTORY_ROWS / 2, 0, total_history_moves - HISTORY_ROWS);
        } else {
            first_history_row = total_history_moves - HISTORY_ROWS;
        }
    }

    for (int row = 0; row < BOARD_SIZE; row++) {
        append_fmt(frame_buffer, sizeof frame_buffer, "%s %d %s", cli_style_str(SGR_LABEL), row + 1,
                   cli_style_str(SGR_RESET));

        for (int col = 0; col < BOARD_SIZE; col++) {
            append_board_cell(frame_buffer, sizeof frame_buffer, game, screen, legal_moves, row,
                              col);
        }

        if (show_history_pane) {
            append_fmt(frame_buffer, sizeof frame_buffer, "   ");

            const int history_index = first_history_row + row;

            if (history_index < total_history_moves) {
                char square_name[3];
                const bool is_current_move = (history_index == active_history_idx);
                const bool is_future_step = (history_index > active_history_idx);
                const char *entry_style = is_current_move
                                              ? cli_style_str(SGR_TAG)
                                              : (is_future_step ? cli_style_str(SGR_DIM) : "");

                position_to_string(history_game->history[history_index].pos, square_name);

                append_fmt(frame_buffer, sizeof frame_buffer, "%s%2d. %-5s %s%s", entry_style,
                           history_index + 1,
                           color_name(history_game->history[history_index].player), square_name,
                           entry_style[0] != '\0' ? cli_style_str(SGR_RESET) : "");
            }
        }

        append_fmt(frame_buffer, sizeof frame_buffer, "%s\n", eol);
        rows_left--;
    }

    /* Reserve three rows for the status message, prompt, and hint line. */
    int body_budget = (rows_left > 3) ? (rows_left - 3) : 0;

    if (body_budget >= 2) {
        append_fmt(frame_buffer, sizeof frame_buffer, "%s\n", eol);
        body_budget--;
    }
    if (body_budget >= 1) {
        int black_pieces;
        int white_pieces;
        count_pieces(game, &black_pieces, &white_pieces);

        snprintf(line_text, sizeof line_text, "Black X: %d   White O: %d   Moves: %d/%d",
                 black_pieces, white_pieces, game->move_count, MAX_MOVES);
        cli_append_wrapped_line(frame_buffer, sizeof frame_buffer, "", line_text, "", eol, cols, 0,
                                &body_budget, true);
    }

    int footer_rows = 3 + body_budget;

    if (screen->status && screen->status[0] != '\0') {
        cli_append_wrapped_line(frame_buffer, sizeof frame_buffer,
                                cli_style_str(screen->status_error ? SGR_ERROR : SGR_OK),
                                screen->status, cli_style_str(SGR_RESET), eol, cols, 0,
                                &footer_rows, true);
    } else if (footer_rows > 2) {
        append_fmt(frame_buffer, sizeof frame_buffer, "%s\n", eol);
        footer_rows--;
    }

    append_command_prompt_row(frame_buffer, sizeof frame_buffer, screen, eol, cols, &footer_rows);

    cli_append_wrapped_line(frame_buffer, sizeof frame_buffer, cli_style_str(SGR_DIM),
                            get_mode_hint(mode), cli_style_str(SGR_RESET), eol, cols, 0,
                            &footer_rows, false);

    cli_flush_terminal_frame(frame_buffer);
}
