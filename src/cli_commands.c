#include "cli_internal.h"
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cross-module integration hook implemented in misc.c */
bool prompt_new_game(App *a, const ParsedCommand *cmd, GameMeta *meta);

/* Internal CLI & command-handling prototypes */
void command_prompt(App *a);
void run_command(App *a, const char *line);
void show_help(App *a);
void show_stats(App *a, const char *const paths[], int count)
{
    StatsSummary sum;
    compute_stats_summary(paths, count, &sum);

    static char buf[FRAME_SIZE];
    char line[160];
    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;
    cli_get_terminal_size(&cols, &rows);

    const char *eol = cli_use_vt_sequences() ? ANSI_CLEAR_EOL : "";
    int rows_left = (rows > 6) ? rows : 6;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;
    buf[0] = '\0';

    append_fmt(buf, sizeof buf, "%s%-*s%s%s\n", cli_style_str(SGR_TITLE), title_width,
               "  OTHELLO - MATCH RESULTS", cli_style_str(SGR_RESET), eol);
    rows_left--;

    for (int i = 0; i < sum.total_games && rows_left > 2; i++) {
        const GameStat *game = &sum.games[i];
        if (game->is_tie) {
            snprintf(line, sizeof line, "  Game %d: Draw | Black %d - White %d",
                     i + 1, game->black_score, game->white_score);
        } else {
            snprintf(line, sizeof line, "  Game %d: %s (%s) won | %d - %d",
                     i + 1, game->winner_name, color_name(game->winner_color),
                     game->winner_score, game->opponent_score);
        }
        cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 2,
                               &rows_left, true);
    }

    snprintf(line, sizeof line, "  Games counted: %d | Files skipped: %d",
             sum.total_games, sum.skipped_files);
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 2,
                            &rows_left, true);

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_OK),
                            "Press any key to return...", cli_style_str(SGR_RESET),
                            eol, cols, 0, &rows_left, false);
    cli_flush_terminal_frame(buf);
    wait_key(a);
    set_status(a, false, "Match results: %d game(s), %d skipped.",
               sum.total_games, sum.skipped_files);
}
void view_replay(App *a, const GameState *game, int initial_step);

static const GameState *g_replay_full_game = NULL;

const GameState *cli_get_replay_game(void)
{
    return g_replay_full_game;
}

void set_status(App *a, bool is_error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
    a->status_error = is_error;
}

/*
 * ===========================================================================
 * COMMAND PARSING & LINE INPUT
 * ===========================================================================
 */

void set_error(ParsedCommand *o, const char *msg)
{
    o->type = CMD_INVALID;
    snprintf(o->error, sizeof o->error, "%s", msg);
}

/* Splits on whitespace. Double quotes group text and are removed. Returns the
 * token count, or -1 on error. */
int tokenize(const char *s, char tok[][MAX_PATH_LEN], char *err, size_t err_size)
{
    int n = 0;
    while (*s) {
        while (isspace((unsigned char)*s))
            s++;
        if (*s == '\0') break;
        if (n >= MAX_TOKENS) {
            snprintf(err, err_size, "too many arguments");
            return -1;
        }
        size_t len = 0;
        bool in_quotes = false;
        while (*s != '\0' && (in_quotes || !isspace((unsigned char)*s))) {
            if (*s == '"') {
                in_quotes = !in_quotes;
                s++;
                continue;
            }
            if (len >= MAX_PATH_LEN - 1) {
                snprintf(err, err_size, "argument too long");
                return -1;
            }
            tok[n][len++] = *s++;
        }
        if (in_quotes) {
            snprintf(err, err_size, "missing closing quote");
            return -1;
        }
        tok[n][len] = '\0';
        n++;
    }
    return n;
}

bool take_name(const char *value, char *dst, ParsedCommand *o)
{
    const size_t len = strlen(value);
    if (len == 0) {
        set_error(o, "name cannot be empty");
        return false;
    }
    if (len >= MAX_NAME_LEN) {
        set_error(o, "name too long (max 31 characters)");
        return false;
    }
    snprintf(dst, MAX_NAME_LEN, "%s", value);
    return true;
}

static bool is_white_flag(const char *arg)
{
    return (arg[0] == 'w' || arg[0] == 'W') && arg[1] == '\0';
}

bool parse_command(const char *input, ParsedCommand *out)
{
    assert(input != NULL && out != NULL);
    memset(out, 0, sizeof *out);

    while (isspace((unsigned char)*input))
        input++;
    if (*input != ':') {
        set_error(out, "commands start with ':'");
        return false;
    }

    char tok[MAX_TOKENS][MAX_PATH_LEN];
    char tok_err[ERROR_TEXT_LEN];
    const int n = tokenize(input, tok, tok_err, sizeof tok_err);
    if (n < 0) {
        set_error(out, tok_err);
        return false;
    }

    const char *cmd = tok[0];
    CommandType t;
    if (strcmp(cmd, ":n") == 0)
        t = CMD_NEW;
    else if (strcmp(cmd, ":l") == 0)
        t = CMD_LOAD;
    else if (strcmp(cmd, ":w") == 0)
        t = CMD_SAVE;
    else if (strcmp(cmd, ":s") == 0)
        t = CMD_STATS;
    else if (strcmp(cmd, ":v") == 0)
        t = CMD_VIEW;
    else if (strcmp(cmd, ":h") == 0)
        t = CMD_HELP;
    else if (strcmp(cmd, ":q") == 0)
        t = CMD_QUIT;
    else if (strcmp(cmd, ":t") == 0)
        t = CMD_TOGGLE_HINTS;
    else {
        set_error(out, "unknown command (type :h for help)");
        return false;
    }

    if ((t == CMD_HELP || t == CMD_QUIT || t == CMD_TOGGLE_HINTS) && n > 1) {
        set_error(out, "this command takes no arguments");
        return false;
    }

    const bool takes_single_path = (t == CMD_LOAD || t == CMD_VIEW || t == CMD_SAVE);
    int positional_names = 0;

    for (int i = 1; i < n; i++) {
        const char *a = tok[i];

        if (strncmp(a, "-p1=", 4) == 0 || strncmp(a, "-p2=", 4) == 0) {
            if (t != CMD_NEW) {
                set_error(out, "-p1= and -p2= are only valid for :n");
                return false;
            }
            if (!take_name(a + 4, a[2] == '1' ? out->p1 : out->p2, out)) return false;
        } else if (strncmp(a, "-w=", 3) == 0) {
            if (t != CMD_NEW) {
                set_error(out, "-w= is only valid for :n");
                return false;
            }
            const char *v = a + 3;
            if (tolower((unsigned char)v[0]) != 'p' || (v[1] != '1' && v[1] != '2') ||
                v[2] != '\0') {
                set_error(out, "-w= must be p1 or p2");
                return false;
            }
            out->white_is_p1 = (v[1] == '1');
        } else if (strncmp(a, "-f=", 3) == 0) {
            if (!takes_single_path && t != CMD_STATS) {
                set_error(out, "-f= is not valid for this command");
                return false;
            }
            if (a[3] == '\0') {
                set_error(out, "file path cannot be empty");
                return false;
            }
            if (takes_single_path) {
                if (out->path[0] != '\0') {
                    set_error(out, "expected a single file path");
                    return false;
                }
                snprintf(out->path, sizeof out->path, "%s", a + 3);
            } else {
                if (out->file_count >= MAX_STAT_FILES) {
                    set_error(out, "too many files (max 32)");
                    return false;
                }
                snprintf(out->files[out->file_count++], MAX_PATH_LEN, "%s", a + 3);
            }
        } else if (a[0] == '-') {
            set_error(out, "unknown option");
            return false;
        } else if (t == CMD_NEW) {
            if (is_white_flag(a)) {
                out->white_is_p1 = true;
            } else if (positional_names == 0 && out->p1[0] == '\0') {
                if (!take_name(a, out->p1, out)) return false;
                positional_names++;
            } else if (positional_names <= 1 && out->p2[0] == '\0') {
                if (!take_name(a, out->p2, out)) return false;
                positional_names++;
            } else {
                set_error(out, "usage: :n [p1] [p2] [w]");
                return false;
            }
        } else if (takes_single_path) {
            if (a[0] == '\0') {
                set_error(out, "file path cannot be empty");
                return false;
            }
            if (out->path[0] != '\0') {
                set_error(out, "expected a single file path");
                return false;
            }
            snprintf(out->path, sizeof out->path, "%s", a);
        } else if (t == CMD_STATS) {
            if (out->file_count >= MAX_STAT_FILES) {
                set_error(out, "too many files (max 32)");
                return false;
            }
            snprintf(out->files[out->file_count++], MAX_PATH_LEN, "%s", a);
        } else {
            set_error(out, "unexpected argument");
            return false;
        }
    }

    if (takes_single_path && out->path[0] == '\0' && t != CMD_VIEW) {
        if (t == CMD_LOAD)
            set_error(out, "usage: :l <file>");
        else
            set_error(out, "usage: :w <file>");
        return false;
    }
    if (t == CMD_STATS && out->file_count == 0) {
        set_error(out, "usage: :s <file1> [file2...]");
        return false;
    }
    out->type = t;
    return true;
}

/* Reads a line in the command bar. Enter accepts, Esc cancels (false),
 * Ctrl-C/EOF quits. */
bool edit_line(App *a, const char *prompt, char *buf, size_t size)
{
    size_t len = 0;
    buf[0] = '\0';
    for (;;) {
        const Screen s = app_screen(a, prompt, buf);
        render(&s);
        const InputEvent ev = term_read_event();
        switch (ev.kind) {
        case EV_ENTER:
            return true;
        case EV_ESC:
            return false;
        case EV_QUIT:
            a->quit = true;
            return false;
        case EV_BACKSPACE:
            if (len > 0) buf[--len] = '\0';
            break;
        case EV_CHAR:
            if (len + 1 < size && ev.ch >= 32 && ev.ch < 127) {
                buf[len++] = (char)ev.ch;
                buf[len] = '\0';
            }
            break;
        default:
            break;
        }
    }
}

void wait_key(App *a)
{
    for (;;) {
        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) {
            a->quit = true;
            return;
        }
        if (ev.kind != EV_NONE && ev.kind != EV_MOUSE_MOVE) return;
    }
}

void command_prompt(App *a)
{
    char text[MAX_LINE_LEN];
    if (!edit_line(a, ":", text, sizeof text) || text[0] == '\0') return;
    char line[MAX_LINE_LEN + 1];
    snprintf(line, sizeof line, ":%s", text);
    run_command(a, line);
}

void run_command(App *a, const char *line)
{
    ParsedCommand cmd;
    if (!parse_command(line, &cmd)) {
        set_status(a, true, "%s", cmd.error);
        return;
    }
    switch (cmd.type) {
    case CMD_INVALID:
        break;
    case CMD_QUIT:
        a->quit = true;
        break;
    case CMD_HELP:
        show_help(a);
        break;
    case CMD_TOGGLE_HINTS:
        a->hints = !a->hints;
        set_status(a, false, "Hints %s.", a->hints ? "on" : "off");
        break;
    case CMD_NEW: {
        GameMeta meta;
        if (!prompt_new_game(a, &cmd, &meta)) {
            set_status(a, false, "New game cancelled.");
            break;
        }
        init_game(&a->game, &meta);
        a->have_game = true;
        a->cursor = (Position){BOARD_SIZE / 2 - 1, BOARD_SIZE / 2 - 1};
        set_status(a, false, "New game: %s (%s) vs %s (%s). Black moves first.", meta.player1_name,
                   color_name(meta.p1_color), meta.player2_name,
                   color_name(opponent_of(meta.p1_color)));
        break;
    }
    case CMD_LOAD: {
        const StorageStatus st = load_game(&a->game, cmd.path);
        if (st == STORAGE_OK) {
            a->have_game = true;
            set_status(a, false, "Loaded %s (%d moves).", cmd.path, a->game.move_count);
        } else {
            set_status(a, true, "Load failed: %s.", storage_status_str(st));
        }
        break;
    }
    case CMD_SAVE: {
        if (!a->have_game) {
            set_status(a, true, "No game to save. Type :n first.");
            break;
        }
        const StorageStatus st = save_game(&a->game, cmd.path);
        if (st == STORAGE_OK)
            set_status(a, false, "Saved to %s.", cmd.path);
        else
            set_status(a, true, "Save failed: %s.", storage_status_str(st));
        break;
    }
    case CMD_VIEW: {
        if (cmd.path[0] == '\0') {
            if (!a->have_game) {
                set_status(a, true, "No active game to view. Use :v <file> or :n first.");
                break;
            }
            view_replay(a, &a->game, a->game.move_count);
            set_status(a, false, "Returned to active game.");
        } else {
            GameState view;
            const StorageStatus st = load_game(&view, cmd.path);
            if (st == STORAGE_OK) {
                view_replay(a, &view, 0);
                set_status(a, false, "Closed replay of %s.", cmd.path);
            } else {
                set_status(a, true, "Cannot view: %s.", storage_status_str(st));
            }
        }
        break;
    }
    case CMD_STATS: {
        const char *paths[MAX_STAT_FILES];
        for (int i = 0; i < cmd.file_count; i++)
            paths[i] = cmd.files[i];
        show_stats(a, paths, cmd.file_count);
        break;
    }
    }
}

/*
 * ===========================================================================
 * FULL-SCREEN HELP, STATISTICS & REPLAY VIEWS
 * ===========================================================================
 */

void show_help(App *a)
{
    static char buf[FRAME_SIZE];
    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;
    cli_get_terminal_size(&cols, &rows);

    const char *eol = cli_use_vt_sequences() ? ANSI_CLEAR_EOL : "";
    int rows_left = (rows > 6) ? rows : 6;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    buf[0] = '\0';

    append_fmt(buf, sizeof buf, "%s%-*s%s%s\n", cli_style_str(SGR_TITLE), title_width,
               "  OTHELLO " OTHELLO_VERSION " - COMMAND & CONTROLS HELP", cli_style_str(SGR_RESET),
               eol);
    rows_left--;

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_TAG),
                            "[1] SESSION COMMANDS (Any Mode)", cli_style_str(SGR_RESET), eol, cols,
                            0, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  :n [p1] [p2] [w]   Start new game ('w' = P1 plays "
                            "White; quotes allow spaces)",
                            "", eol, cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  :l <file>          Load a saved game (e.g. :l game.sav)", "", eol,
                            cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  :v [file]          View current game moves (:v) "
                            "or a saved game (:v game.sav)",
                            "", eol, cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  :s <f1> [f2...]    Show win/loss statistics "
                            "across finished save files",
                            "", eol, cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  :t                 Toggle legal-move hints (*) on or off", "", eol,
                            cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "", "  :h                 Show this command reference",
                            "", eol, cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "", "  :q                 Quit Othello", "", eol, cols,
                            4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_TAG),
                            "[2] BOARD PLAY CONTROLS (Active Game)", cli_style_str(SGR_RESET), eol,
                            cols, 0, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  Arrow keys         Move cursor (Up / Down / Left / Right)", "", eol,
                            cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  a-h, 1-8           Jump directly to column (a-h) or row (1-8)", "",
                            eol, cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  Enter / Space      Place a piece on the selected square", "", eol,
                            cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  Mouse              Hover to move cursor; left-click to place piece",
                            "", eol, cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  :w <file>          Save current game (e.g. :w game.sav)", "", eol,
                            cols, 4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_TAG),
                            "[3] COMMAND LINE & REPLAY CONTROLS", cli_style_str(SGR_RESET), eol,
                            cols, 0, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  :                  Open command input (Enter "
                            "execute | Backspace edit | Esc cancel)",
                            "", eol, cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  Left arrow         Step backward one move in replay", "", eol, cols,
                            4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  Right / Space      Step forward one move in replay", "", eol, cols,
                            4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "",
                            "  0-60 + Enter       Jump directly to move number in replay", "", eol,
                            cols, 4, &rows_left, true);
    cli_append_wrapped_line(buf, sizeof buf, "", "  q / Esc            Exit replay mode", "", eol,
                            cols, 4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_OK), "Press any key to return...",
                            cli_style_str(SGR_RESET), eol, cols, 0, &rows_left, false);

    cli_flush_terminal_frame(buf);
    wait_key(a);
    set_status(a, false, "Help closed.");
}

void show_stats(App *a, const char *const paths[], int count)
{
    StatsSummary sum;
    compute_stats_summary(paths, count, &sum);

    static char buf[FRAME_SIZE];
    char line[160];
    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;
    cli_get_terminal_size(&cols, &rows);

    const char *eol = cli_use_vt_sequences() ? ANSI_CLEAR_EOL : "";
    int rows_left = (rows > 6) ? rows : 6;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    buf[0] = '\0';

    append_fmt(buf, sizeof buf, "%s%-*s%s%s\n", cli_style_str(SGR_TITLE), title_width,
               "  OTHELLO - MATCH STATISTICS", cli_style_str(SGR_RESET), eol);
    rows_left--;

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_TAG), "[GAME STATISTICS SUMMARY]",
                            cli_style_str(SGR_RESET), eol, cols, 0, &rows_left, true);

    snprintf(line, sizeof line, "  Finished games counted : %d", sum.total_games);
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line,
             "  Files skipped          : %d (unreadable, corrupt, or unfinished)",
             sum.skipped_files);
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line, "  Black wins             : %d", sum.black_wins);
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line, "  White wins             : %d", sum.white_wins);
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line, "  Ties                   : %d", sum.ties);
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    if (sum.total_games > 0) {
        snprintf(line, sizeof line, "  Highest score          : %d (%s)", sum.max_score,
                 sum.highest_scoring_player);
    } else {
        snprintf(line, sizeof line, "  Highest score          : -");
    }
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_OK), "Press any key to return...",
                            cli_style_str(SGR_RESET), eol, cols, 0, &rows_left, false);

    cli_flush_terminal_frame(buf);
    wait_key(a);
    set_status(a, false, "Statistics: %d game(s) counted, %d skipped.", sum.total_games,
               sum.skipped_files);
}

void view_replay(App *a, const GameState *game, int initial_step)
{
    int step = clamp_int(initial_step, 0, game->move_count);
    int number = -1;
    GameState cur;
    const GameState *prev_replay = g_replay_full_game;
    g_replay_full_game = game;

    for (;;) {
        if (!rebuild_state_at(game, step, &cur)) {
            g_replay_full_game = prev_replay;
            set_status(a, true, "Cannot rebuild this game (illegal move history).");
            return;
        }
        char tag[64];
        snprintf(tag, sizeof tag, "REPLAY  move %d / %d", step, game->move_count);
        char status_line[96];
        if (number >= 0) {
            snprintf(status_line, sizeof status_line,
                     "Jump to move: %d (press Enter to jump, Esc to clear)", number);
        } else {
            snprintf(status_line, sizeof status_line, "Viewing move %d of %d.", step,
                     game->move_count);
        }
        Screen s = {.state = &cur,
                    .cursor = {0, 0},
                    .show_cursor = false,
                    .hints = false,
                    .tag = tag,
                    .status = status_line,
                    .status_error = false,
                    .input_prompt = NULL,
                    .input_text = NULL,
                    .help = "Left back | Right/Space forward | digits + Enter jump "
                            "| q/Esc exit"};
        render(&s);

        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) {
            a->quit = true;
            break;
        }
        if (ev.kind == EV_ESC) {
            if (number >= 0)
                number = -1;
            else
                break;
            continue;
        }
        if (ev.kind == EV_CHAR && (ev.ch == 'q' || ev.ch == 'Q')) break;
        if (ev.kind == EV_CHAR && isdigit((unsigned char)ev.ch)) {
            number = (number < 0 ? 0 : number) * 10 + (ev.ch - '0');
            number = clamp_int(number, 0, MAX_MOVES * 10);
            continue;
        }
        if (ev.kind == EV_BACKSPACE) {
            number = number / 10;
            if (number == 0) number = -1;
            continue;
        }
        const bool back = (ev.kind == EV_LEFT);
        const bool fwd = (ev.kind == EV_RIGHT) || (ev.kind == EV_CHAR && ev.ch == ' ');
        if (ev.kind == EV_ENTER) {
            if (number >= 0) {
                step = clamp_int(number, 0, game->move_count);
                number = -1;
            } else if (step < game->move_count) {
                step++;
            }
        } else if (back) {
            number = -1;
            if (step > 0) step--;
        } else if (fwd) {
            number = -1;
            if (step < game->move_count) step++;
        }
    }

    g_replay_full_game = prev_replay;
}
