// ===== COMMAND PARSING TASK =====
#include "common.h"

void command_prompt(App *a);
void run_command(App *a, const char *line);
void show_help(App *a);
void show_stats(App *a, const char *const paths[], int count);
void view_replay(App *a, const GameState *game);

void set_error(ParsedCommand *o, const char *msg) {
    o->type = CMD_INVALID;
    snprintf(o->error, sizeof o->error, "%s", msg);
}

/* Splits on whitespace. Double quotes group text and are removed. Returns the token count, or -1 on error. */
int tokenize(const char *s, char tok[][MAX_PATH_LEN], char *err, size_t err_size) {
    int n = 0;
    while (*s) {
        while (isspace((unsigned char)*s)) s++;
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

bool take_name(const char *value, char *dst, ParsedCommand *o) {
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

bool parse_command(const char *input, ParsedCommand *out) {
    assert(input != NULL && out != NULL);
    memset(out, 0, sizeof *out);

    while (isspace((unsigned char)*input)) input++;
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
    if      (strcmp(cmd, ":n") == 0) t = CMD_NEW;
    else if (strcmp(cmd, ":l") == 0) t = CMD_LOAD;
    else if (strcmp(cmd, ":w") == 0) t = CMD_SAVE;
    else if (strcmp(cmd, ":s") == 0) t = CMD_STATS;
    else if (strcmp(cmd, ":v") == 0) t = CMD_VIEW;
    else if (strcmp(cmd, ":h") == 0) t = CMD_HELP;
    else if (strcmp(cmd, ":q") == 0) t = CMD_QUIT;
    else if (strcmp(cmd, ":t") == 0) t = CMD_TOGGLE_HINTS;
    else {
        set_error(out, "unknown command (type :h for help)");
        return false;
    }

    if ((t == CMD_HELP || t == CMD_QUIT || t == CMD_TOGGLE_HINTS) && n > 1) {
        set_error(out, "this command takes no arguments");
        return false;
    }
    const bool takes_path = (t == CMD_LOAD || t == CMD_VIEW || t == CMD_SAVE);

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
            if (tolower((unsigned char)v[0]) != 'p' || (v[1] != '1' && v[1] != '2') || v[2] != '\0') {
                set_error(out, "-w= must be p1 or p2");
                return false;
            }
            out->white_is_p1 = (v[1] == '1');
        } else if (strncmp(a, "-f=", 3) == 0) {
            if (!takes_path) {
                set_error(out, "-f= is not valid for this command");
                return false;
            }
            if (a[3] == '\0') {
                set_error(out, "file path cannot be empty");
                return false;
            }
            snprintf(out->path, sizeof out->path, "%s", a + 3);
        } else if (a[0] == '-') {
            set_error(out, "unknown option");
            return false;
        } else {
            if (t != CMD_STATS) {
                set_error(out, "unexpected argument");
                return false;
            }
            if (out->file_count >= MAX_STAT_FILES) {
                set_error(out, "too many files (max 32)");
                return false;
            }
            snprintf(out->files[out->file_count++], MAX_PATH_LEN, "%s", a);
        }
    }

    if (takes_path && out->path[0] == '\0') {
        set_error(out, "missing -f=\"file\"");
        return false;
    }
    if (t == CMD_STATS && out->file_count == 0) {
        set_error(out, "usage: :s file1 file2 ...");
        return false;
    }
    out->type = t;
    return true;
}
// ===== APPLICATION FLOW / MISCELLANEOUS TASK =====
void set_status(App *a, bool is_error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
    a->status_error = is_error;
}

Screen app_screen(const App *a, const char *prompt, const char *text) {
    static const char *const PLAY_HELP =
        "hjkl/arrows move  a-h,1-8 jump  Enter/click place  :command  :h help";
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

/* Maps a screen cell to a board square. False if the cell is not on the board. */
bool screen_to_cell(int row, int col, Position *out) {
    const int r = row - BOARD_TOP;
    const int c = col - BOARD_LEFT;
    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE * CELL_WIDTH) return false;
    out->row = r;
    out->col = c / CELL_WIDTH;
    return true;
}

void move_cursor(App *a, int dr, int dc) {
    a->cursor.row = clamp_int(a->cursor.row + dr, 0, BOARD_SIZE - 1);
    a->cursor.col = clamp_int(a->cursor.col + dc, 0, BOARD_SIZE - 1);
}

void app_place(App *a) {
    if (!a->have_game) {
        set_status(a, true, "No game in progress. Type :n to start.");
        return;
    }
    if (is_game_over(&a->game)) {
        set_status(a, true, "The game is over. Use :n for a new game.");
        return;
    }
    char sq[3];
    position_to_string(a->cursor, sq);
    const MoveError me = check_move(&a->game, a->game.current_turn, a->cursor);
    if (me != MOVE_OK) {
        set_status(a, true, "%s: %s.", sq, move_error_str(me));
        if (me == MOVE_ERR_NO_CAPTURE && a->hints) append_fmt(a->status, sizeof a->status, " Squares marked * are legal.");
        return;
    }
    const PlayerColor mover = a->game.current_turn;
    const int flips = execute_move(&a->game, a->cursor);
    const TurnStatus ts = switch_turn(&a->game);
    set_status(a, false, "%s played %s, flipped %d.", player_name(&a->game, mover), sq, flips);
    if (ts == TURN_OPPONENT_PASSED) {
        append_fmt(a->status, sizeof a->status, " %s has no legal move and passes.",
                   player_name(&a->game, opponent_of(mover)));
    } else if (ts == TURN_GAME_OVER) {
        append_fmt(a->status, sizeof a->status, " No more moves: game over!");
    }
}

void app_handle_char(App *a, int ch) {
    const int lc = tolower(ch);
    if (ch == ' ')                       app_place(a);
    else if (lc >= 'a' && lc <= 'h')     a->cursor.col = lc - 'a';
    else if (ch >= '1' && ch <= '8')     a->cursor.row = ch - '1';
    else if (lc == 'k')                  move_cursor(a, -1, 0);
    else if (lc == 'j')                  move_cursor(a, 1, 0);
    else if (lc == 'l')                  move_cursor(a, 0, 1);
    else if (ch == ':')                  command_prompt(a);
}

void app_handle_event(App *a, InputEvent ev) {
    Position p;
    switch (ev.kind) {
    case EV_CHAR:        app_handle_char(a, ev.ch); break;
    case EV_UP:          move_cursor(a, -1, 0);     break;
    case EV_DOWN:        move_cursor(a, 1, 0);      break;
    case EV_LEFT:        move_cursor(a, 0, -1);     break;
    case EV_RIGHT:       move_cursor(a, 0, 1);      break;
    case EV_ENTER:       app_place(a);              break;
    case EV_ESC:         a->status[0] = '\0';       break;
    case EV_MOUSE_PRESS:
        if (a->have_game && screen_to_cell(ev.row, ev.col, &p)) {
            a->cursor = p;
            app_place(a);
        }
        break;
    case EV_MOUSE_MOVE:
        if (a->have_game && screen_to_cell(ev.row, ev.col, &p)) a->cursor = p;
        break;
    case EV_BACKSPACE:
    case EV_NONE:
    case EV_QUIT:
        break;
    }
}

/* Reads a line in the bottom bar. Enter accepts, Esc cancels (false), Ctrl-C/EOF quits. */
bool edit_line(App *a, const char *prompt, char *buf, size_t size) {
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

void wait_key(App *a) {
    for (;;) {
        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) {
            a->quit = true;
            return;
        }
        if (ev.kind != EV_NONE && ev.kind != EV_MOUSE_MOVE) return;
    }
}

bool prompt_name(App *a, const char *label, char *dst) {
    char buf[MAX_LINE_LEN];
    for (;;) {
        if (!edit_line(a, label, buf, sizeof buf)) return false;
        trim(buf);
        const size_t len = strlen(buf);
        if (len == 0) {
            set_status(a, true, "Name cannot be empty.");
            continue;
        }
        if (len >= MAX_NAME_LEN) {
            set_status(a, true, "Name too long (max %d characters).", MAX_NAME_LEN - 1);
            continue;
        }
        memcpy(dst, buf, len + 1);
        return true;
    }
}

bool prompt_new_game(App *a, const ParsedCommand *cmd, GameMeta *meta) {
    memset(meta, 0, sizeof *meta);
    if (cmd->p1[0] != '\0') snprintf(meta->player1_name, sizeof meta->player1_name, "%s", cmd->p1);
    else if (!prompt_name(a, "Player 1 name: ", meta->player1_name)) return false;

    if (cmd->p2[0] != '\0') snprintf(meta->player2_name, sizeof meta->player2_name, "%s", cmd->p2);
    else if (!prompt_name(a, "Player 2 name: ", meta->player2_name)) return false;

    meta->p1_color = cmd->white_is_p1 ? PLAYER_WHITE : PLAYER_BLACK;
    return true;
}

void command_prompt(App *a) {
    char text[MAX_LINE_LEN];
    if (!edit_line(a, ":", text, sizeof text) || text[0] == '\0') return;
    char line[MAX_LINE_LEN + 1];
    snprintf(line, sizeof line, ":%s", text);
    run_command(a, line);
}

void run_command(App *a, const char *line) {
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
        a->cursor = (Position){ BOARD_SIZE / 2 - 1, BOARD_SIZE / 2 - 1 };
        set_status(a, false, "New game: %s (%s) vs %s (%s). Black moves first.",
                   meta.player1_name, color_name(meta.p1_color),
                   meta.player2_name, color_name(opponent_of(meta.p1_color)));
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
        if (st == STORAGE_OK) set_status(a, false, "Saved to %s.", cmd.path);
        else                  set_status(a, true, "Save failed: %s.", storage_status_str(st));
        break;
    }
    case CMD_VIEW: {
        GameState view;
        const StorageStatus st = load_game(&view, cmd.path);
        if (st == STORAGE_OK) {
            view_replay(a, &view);
        } else {
            set_status(a, true, "Cannot view: %s.", storage_status_str(st));
        }
        break;
    }
    case CMD_STATS: {
        const char *paths[MAX_STAT_FILES];
        for (int i = 0; i < cmd.file_count; i++) paths[i] = cmd.files[i];
        show_stats(a, paths, cmd.file_count);
        break;
    }
    }
}

/* ============================================================
 * Full-screen views
 * ============================================================ */

void show_help(App *a) {
    static const char *const LINES[] = {
        "KEYS (play)",
        "  h j k l / arrows   move the cursor (left is the Left arrow: h means file H)",
        "  a-h, 1-8           set column and row, e.g. d3 jumps to d3",
        "  Enter / Space      place a piece at the cursor",
        "  mouse              move over the board to move the cursor; click to place",
        "  :                  command line (Enter runs, Esc cancels)",
        "",
        "COMMANDS",
        "  :n [-p1=\"Ann\"] [-p2=\"Bob\"] [-w=p1]   new game (-w chooses White; default p2)",
        "  :w -f=\"game.sav\"     save the current game",
        "  :l -f=\"game.sav\"     load a saved game",
        "  :v -f=\"game.sav\"     replay a saved game",
        "  :s f1.sav f2.sav     statistics over finished games",
        "  :t                   toggle * hints",
        "  :h   help            :q  quit",
        "",
        "REPLAY",
        "  h / Left           back              l / Right / Space   forward",
        "  digits + Enter     jump to move      q / Esc             leave replay",
    };
    term_write("\x1b[H\x1b[2J");
    for (size_t i = 0; i < sizeof LINES / sizeof LINES[0]; i++) puts(LINES[i]);
    puts("\nPress any key to return.");
    fflush(stdout);
    wait_key(a);
    set_status(a, false, "Help closed.");
}

void show_stats(App *a, const char *const paths[], int count) {
    StatsSummary sum;
    compute_stats_summary(paths, count, &sum);
    term_write("\x1b[H\x1b[2J");
    print_stats_summary(&sum);
    puts("\nPress any key to return.");
    fflush(stdout);
    wait_key(a);
    set_status(a, false, "Statistics: %d game(s) counted, %d skipped.", sum.total_games, sum.skipped_files);
}

void view_replay(App *a, const GameState *game) {
    int step = 0;
    int number = -1;
    GameState cur;
    for (;;) {
        if (!rebuild_state_at(game, step, &cur)) {
            set_status(a, true, "Cannot rebuild this game (illegal move history).");
            return;
        }
        char tag[64];
        snprintf(tag, sizeof tag, "REPLAY  move %d / %d", step, game->move_count);
        Screen s = {
            .state = &cur, .cursor = { 0, 0 }, .show_cursor = false, .hints = false,
            .tag = tag, .status = "", .status_error = false,
            .input_prompt = NULL, .input_text = NULL,
            .help = "h/l or arrows step   digits + Enter jump   q leave replay"
        };
        render(&s);

        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) {
            a->quit = true;
            return;
        }
        if (ev.kind == EV_ESC) {
            if (number >= 0) number = -1;
            else return;
            continue;
        }
        if (ev.kind == EV_CHAR && ev.ch == 'q') return;
        if (ev.kind == EV_CHAR && isdigit(ev.ch)) {
            number = (number < 0 ? 0 : number) * 10 + (ev.ch - '0');
            number = clamp_int(number, 0, MAX_MOVES * 10);
            continue;
        }
        if (ev.kind == EV_BACKSPACE) {
            number = number / 10;
            if (number == 0) number = -1;
            continue;
        }
        const bool back = ev.kind == EV_LEFT || (ev.kind == EV_CHAR && ev.ch == 'h');
        const bool fwd  = ev.kind == EV_RIGHT || (ev.kind == EV_CHAR && (ev.ch == 'l' || ev.ch == ' '));
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
}

