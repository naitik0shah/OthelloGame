#include "cli_internal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * High-level UI modes inferred from the Screen snapshot so that context-aware
 * instructions appear where the user needs them most.
 */
typedef enum {
    UI_MODE_WELCOME,       /* No active game loaded yet */
    UI_MODE_COMMAND_INPUT, /* User is typing a ':' command */
    UI_MODE_ACTIVE_PLAY,   /* Game in progress; board accepts moves */
    UI_MODE_REPLAY,        /* Read-only replay mode (:v) */
    UI_MODE_POST_GAME      /* Game finished */
} UiMode;

/*
 * ===========================================================================
 * CLI SCREEN SNAPSHOT & COORDINATE MAPPING
 * ===========================================================================
 */

Screen app_screen(const App *a, const char *prompt, const char *text)
{
    static const char *const PLAY_HELP =
        "Arrows move  a-h,1-8 jump  Enter/click place  :v view moves  :command  "
        ":h help";
    Screen s;
    s.state = a->have_game ? &a->game : NULL;
    s.cursor = a->cursor;
    s.show_cursor = a->have_game;
    s.hints = a->hints;
    s.tag = NULL;
    s.status = a->status;
    s.status_error = a->status_error;
    s.input_prompt = prompt;
    s.input_text = text;
    s.help = PLAY_HELP;
    return s;
}

/* Maps a screen cell to a board square. False if the cell is not on the board.
 */
bool screen_to_cell(int row, int col, Position *out)
{
    const int r = row - BOARD_TOP;
    const int c = col - BOARD_LEFT;
    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE * CELL_WIDTH) {
        return false;
    }
    out->row = r;
    out->col = c / CELL_WIDTH;
    return true;
}

/*
 * ===========================================================================
 * RENDERING ENGINE & FULL-SCREEN CLI VIEWS
 * ===========================================================================
 */

/*
 * Determine the active UI mode from the Screen snapshot so contextual hints
 * and layouts match what the user is currently doing.
 */
static UiMode detect_ui_mode(const Screen *screen)
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

/*
 * Return mode-specific controls to display directly in the footer bar where
 * the user needs them most.
 */
static const char *mode_contextual_hint(UiMode mode)
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

/*
 * Append a single non-wrapping line clipped safely to `max_cols` so header
 * rows above the board always occupy exactly 1 terminal row (keeping
 * BOARD_TOP == 3 invariant for mouse click alignment).
 */
static void append_single_line(char *frame, size_t frame_size, const char *prefix_style,
                               const char *text, const char *suffix_style, const char *eol,
                               int max_cols, int *rows_left)
{
    if (rows_left && *rows_left <= 0) {
        return;
    }

    int usable = (max_cols > 4) ? (max_cols - 3) : 1;
    append_fmt(frame, frame_size, "  %s%.*s%s%s\n", prefix_style, usable, text ? text : "",
               suffix_style, eol);

    if (rows_left) {
        (*rows_left)--;
    }
}

/*
 * Word-wrap text into the frame buffer so lines break cleanly at spaces
 * rather than cutting words in half or losing characters on narrow terminals.
 * Never exceeds rows_left remaining terminal rows so the viewport never
 * scrolls off the top of the screen.
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

    int usable_cols = (max_cols > MIN_WRAP_COLS) ? (max_cols - 3) : MIN_WRAP_COLS;
    if (indent_spaces < 0) {
        indent_spaces = 0;
    }
    if (indent_spaces > usable_cols / 2) {
        indent_spaces = usable_cols / 2;
    }

    const char *cursor = text;
    bool first_line = true;
    int emitted_rows = 0;

    while (*cursor != '\0') {
        if (rows_left && *rows_left <= 0) {
            break;
        }

        if (*cursor == '\n') {
            cursor++;
            bool more = (*cursor != '\0') && (!rows_left || *rows_left > 1);
            append_fmt(frame, frame_size, "%s%s", eol, (more || trailing_newline) ? "\n" : "");
            if (rows_left) {
                (*rows_left)--;
            }
            emitted_rows++;
            first_line = false;
            continue;
        }

        int current_indent = first_line ? 2 : (2 + indent_spaces);
        int line_budget = usable_cols - current_indent;
        if (line_budget < 8) {
            line_budget = 8;
        }

        int len = 0;
        int last_space = -1;

        while (cursor[len] != '\0' && cursor[len] != '\n' && len < line_budget) {
            if (cursor[len] == ' ') {
                last_space = len;
            }
            len++;
        }

        /* Break at the last space before the column limit if a word overflows */
        if (cursor[len] != '\0' && cursor[len] != '\n' && cursor[len] != ' ' && last_space > 0) {
            len = last_space;
        }

        const char *next = cursor + len;
        while (*next == ' ') {
            next++;
        }
        if (*next == '\n') {
            next++;
        }

        bool has_more = (*next != '\0') && (!rows_left || *rows_left > 1);
        append_fmt(frame, frame_size, "%*s%s%.*s%s%s%s", current_indent, "", prefix_style, len,
                   cursor, suffix_style, eol, (has_more || trailing_newline) ? "\n" : "");

        if (rows_left) {
            (*rows_left)--;
        }
        emitted_rows++;
        cursor = next;
        first_line = false;
    }

    return emitted_rows;
}

/*
 * Format the single-line command prompt safely within `max_cols` aligned
 * with the rest of the UI text (`  ` left margin) so typing long arguments
 * never wraps onto a second line and shifts the layout.
 */
static void append_prompt_row(char *frame, size_t frame_size, const Screen *screen, const char *eol,
                              int max_cols, int *rows_left)
{
    if (rows_left && *rows_left <= 0) {
        return;
    }

    if (screen->input_prompt) {
        const char *prompt = screen->input_prompt;
        const char *text = screen->input_text ? screen->input_text : "";
        int prompt_len = (int)strlen(prompt);
        int text_len = (int)strlen(text);

        /* Reserve 2-space margin, prompt, '_' caret, and right padding */
        int text_budget = max_cols - prompt_len - 5;
        if (text_budget < 4) {
            text_budget = 4;
        }

        const char *visible_text = text;
        if (text_len > text_budget) {
            visible_text = text + (text_len - text_budget);
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

/*
 * Format a single board square (piece, legal-move hint, last-move highlight,
 * and cursor indicator) and append it to the in-memory frame buffer.
 */
static void append_cell(char *frame, size_t frame_size, const GameState *game, const Screen *screen,
                        bool legal[BOARD_SIZE][BOARD_SIZE], int row, int col)
{
    const CellState cell = game->board[row][col];

    const bool cursor_is_here =
        screen->show_cursor && screen->cursor.row == row && screen->cursor.col == col;

    const bool is_last_move = game->move_count > 0 &&
                              game->history[game->move_count - 1].pos.row == row &&
                              game->history[game->move_count - 1].pos.col == col;

    const char *background = cli_style_str(is_last_move ? SGR_BG_LAST : SGR_BG_BOARD);

    const char *foreground = cli_style_str(SGR_DIM);
    char symbol = '.';

    if (cell == CELL_BLACK) {
        foreground = cli_style_str(SGR_BLACK);
        symbol = 'X';
    } else if (cell == CELL_WHITE) {
        foreground = cli_style_str(SGR_WHITE);
        symbol = 'O';
    } else if (legal[row][col]) {
        foreground = cli_style_str(SGR_HINT);
        symbol = '*';
    }

    if (!cli_use_color() && cursor_is_here) {
        append_fmt(frame, frame_size, "[%c]", symbol);
    } else {
        append_fmt(frame, frame_size, "%s%s%s %c %s", background,
                   cursor_is_here ? cli_style_str(SGR_INVERSE) : "", foreground, symbol,
                   cli_style_str(SGR_RESET));
    }
}

/*
 * Format the end-of-game winner/tie summary and final piece counts into output.
 */
static void describe_result(const GameState *game, char *output, size_t output_size)
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

/*
 * Write a complete full-screen buffer using active VT/legacy terminal rules
 * and park the cursor at (1,1) so no static caret sits after the last char.
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

/*
 * Render the Welcome & Mode Command Reference screen when no game is active
 * (screen->state == NULL), grouping the status and command prompt right with
 * the reference content so all text sits cohesively in one place.
 */
static void render_welcome_screen(char *frame, size_t frame_size, const Screen *screen,
                                  const char *eol, int cols, int rows)
{
    int total_rows = (rows > 6) ? rows : 6;
    const bool spacious = (total_rows >= 25 && cols >= 72);

    /* Reserve 4 rows for Status + Prompt + Hint so the interactive command
   * line never overflows short terminals. */
    int body_rows = total_rows - 4;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    /* Row 0: Full-width Title Bar */
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

    /* Mode 1: Session & File Commands */
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

    /* Interactive Status + Command Line + Shortcut Bar grouped directly with text
   */
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

    append_prompt_row(frame, frame_size, screen, eol, cols, &footer_rows);

    cli_append_wrapped_line(
        frame, frame_size, cli_style_str(SGR_DIM),
        mode_contextual_hint(screen->input_prompt ? UI_MODE_COMMAND_INPUT : UI_MODE_WELCOME),
        cli_style_str(SGR_RESET), eol, cols, 0, &footer_rows, false);
}

/*
 * Build the complete UI frame in memory and flush it to the terminal in a
 * single write to prevent flicker.
 */
void render(const Screen *screen)
{
    static char frame[FRAME_SIZE];

    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;
    cli_get_terminal_size(&cols, &rows);

    const char *eol = cli_use_vt_sequences() ? ANSI_CLEAR_EOL : "";
    const GameState *game = screen->state;
    const UiMode mode = detect_ui_mode(screen);
    char message[160];
    bool legal_moves[BOARD_SIZE][BOARD_SIZE];

    frame[0] = '\0';

    /* Show the dedicated welcome screen whenever no game is loaded */
    if (!game) {
        render_welcome_screen(frame, sizeof frame, screen, eol, cols, rows);
        cli_flush_terminal_frame(frame);
        return;
    }

    int rows_left = (rows > 14) ? rows : 14;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    /* Row 0: Full-width Title Bar */
    snprintf(message, sizeof message, "  OTHELLO    %s (X)  vs  %s (O)",
             player_name(game, PLAYER_BLACK), player_name(game, PLAYER_WHITE));
    append_fmt(frame, sizeof frame, "%s%-*s%s%s\n", cli_style_str(SGR_TITLE), title_width, message,
               cli_style_str(SGR_RESET), eol);
    rows_left--;

    /* Row 1: Single-line Turn / Replay / Game-Over Banner (keeps BOARD_TOP == 3)
   */
    if (screen->tag) {
        snprintf(message, sizeof message, "%s  (Read-Only Replay)", screen->tag);
        append_single_line(frame, sizeof frame, cli_style_str(SGR_TAG), message,
                           cli_style_str(SGR_RESET), eol, cols, &rows_left);
    } else if (is_game_over(game)) {
        char result_text[96];
        describe_result(game, result_text, sizeof result_text);
        snprintf(message, sizeof message, "GAME OVER: %s  (:w <file> save | :n new | :q quit)",
                 result_text);
        append_single_line(frame, sizeof frame, cli_style_str(SGR_TAG), message,
                           cli_style_str(SGR_RESET), eol, cols, &rows_left);
    } else {
        snprintf(message, sizeof message, "Turn: %s (%s)   [%s]",
                 player_name(game, game->current_turn), color_name(game->current_turn),
                 screen->hints ? "Hints ON (*)" : "Hints OFF (:t)");
        append_single_line(frame, sizeof frame, "", message, "", eol, cols, &rows_left);
    }

    /* Row 2: Column Headers (fixed BOARD_LEFT = 3 for mouse alignment) */
    const bool show_history_pane = (cols >= MIN_COLS_WITH_HISTORY);
    append_fmt(frame, sizeof frame, "   ");

    for (int col = 0; col < BOARD_SIZE; col++) {
        append_fmt(frame, sizeof frame, "%s %c %s", cli_style_str(SGR_LABEL), 'A' + col,
                   cli_style_str(SGR_RESET));
    }

    if (show_history_pane) {
        append_fmt(frame, sizeof frame, "   %sMoves%s%s\n", cli_style_str(SGR_LABEL),
                   cli_style_str(SGR_RESET), eol);
    } else {
        append_fmt(frame, sizeof frame, "%s\n", eol);
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

    /* Rows 3..10: 8x8 Board Grid + Optional Right-Hand Move History */
    for (int row = 0; row < BOARD_SIZE; row++) {
        append_fmt(frame, sizeof frame, "%s %d %s", cli_style_str(SGR_LABEL), row + 1,
                   cli_style_str(SGR_RESET));

        for (int col = 0; col < BOARD_SIZE; col++) {
            append_cell(frame, sizeof frame, game, screen, legal_moves, row, col);
        }

        if (show_history_pane) {
            append_fmt(frame, sizeof frame, "   ");

            const int history_index = first_history_row + row;

            if (history_index < total_history_moves) {
                char square[3];
                const bool is_current_move = (history_index == active_history_idx);
                const bool is_future_step = (history_index > active_history_idx);
                const char *entry_style = is_current_move
                                              ? cli_style_str(SGR_TAG)
                                              : (is_future_step ? cli_style_str(SGR_DIM) : "");

                position_to_string(history_game->history[history_index].pos, square);

                append_fmt(frame, sizeof frame, "%s%2d. %-5s %s%s", entry_style, history_index + 1,
                           color_name(history_game->history[history_index].player), square,
                           entry_style[0] != '\0' ? cli_style_str(SGR_RESET) : "");
            }
        }

        append_fmt(frame, sizeof frame, "%s\n", eol);
        rows_left--;
    }

    /* Reserve 3 rows at the bottom (Status + Prompt + Hint) */
    int body_budget = (rows_left > 3) ? (rows_left - 3) : 0;

    /* Piece Tally Line */
    if (body_budget >= 2) {
        append_fmt(frame, sizeof frame, "%s\n", eol);
        body_budget--;
    }
    if (body_budget >= 1) {
        int black_pieces;
        int white_pieces;
        count_pieces(game, &black_pieces, &white_pieces);

        snprintf(message, sizeof message, "Black X: %d   White O: %d   Moves: %d/%d", black_pieces,
                 white_pieces, game->move_count, MAX_MOVES);
        cli_append_wrapped_line(frame, sizeof frame, "", message, "", eol, cols, 0, &body_budget,
                                true);
    }

    int footer_rows = 3 + body_budget;

    /* Status / Feedback Line */
    if (screen->status && screen->status[0] != '\0') {
        cli_append_wrapped_line(
            frame, sizeof frame, cli_style_str(screen->status_error ? SGR_ERROR : SGR_OK),
            screen->status, cli_style_str(SGR_RESET), eol, cols, 0, &footer_rows, true);
    } else if (footer_rows > 2) {
        append_fmt(frame, sizeof frame, "%s\n", eol);
        footer_rows--;
    }

    /* Fixed-height Command Prompt Row */
    append_prompt_row(frame, sizeof frame, screen, eol, cols, &footer_rows);

    /* Mode-Contextual Hint Row (no trailing '\n' so bottom edge never scrolls) */
    cli_append_wrapped_line(frame, sizeof frame, cli_style_str(SGR_DIM), mode_contextual_hint(mode),
                            cli_style_str(SGR_RESET), eol, cols, 0, &footer_rows, false);

    cli_flush_terminal_frame(frame);
}
