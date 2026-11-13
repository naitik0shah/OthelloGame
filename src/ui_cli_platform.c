#include "common.h"

static const char *const SGR_RESET     = "\x1b[0m";
static const char *const SGR_CLEAR_EOL = "\x1b[K";
static const char *const SGR_TITLE     = "\x1b[1;97;44m";   /* white on blue */
static const char *const SGR_LABEL     = "\x1b[1;36m";      /* cyan */
static const char *const SGR_BG_BOARD  = "\x1b[42m";        /* green board */
static const char *const SGR_BG_LAST   = "\x1b[103m";       /* bright yellow: last move */
static const char *const SGR_BLACK     = "\x1b[1;30m";
static const char *const SGR_WHITE     = "\x1b[1;97m";
static const char *const SGR_HINT      = "\x1b[1;33m";
static const char *const SGR_DIM       = "\x1b[2m";
static const char *const SGR_INVERSE   = "\x1b[7m";         /* cursor */
static const char *const SGR_ERROR     = "\x1b[1;31m";
static const char *const SGR_OK        = "\x1b[1;32m";
static const char *const SGR_TAG       = "\x1b[1;33m";
// ===== CLI / TERMINAL PLATFORM TASK =====
static const char *const ENTER_SCREEN =
    "\x1b[?1049h"      /* alternate screen */
    "\x1b[?25l"        /* hide cursor */
    "\x1b[?1003h"      /* report all mouse motion */
    "\x1b[?1006h"      /* SGR mouse coordinates */
    "\x1b[2J";
static const char *const LEAVE_SCREEN =
    "\x1b[?1003l\x1b[?1006l\x1b[?25h\x1b[?1049l\x1b[0m";

void term_write(const char *s) {
    fputs(s, stdout);
    fflush(stdout);
}

#ifdef _WIN32

static HANDLE g_console_in;
static DWORD  g_console_in_mode;
static bool   g_left_down;

bool term_init(void) {
    g_console_in = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(g_console_in, &g_console_in_mode)) return false;
    SetConsoleMode(g_console_in, ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD out_mode = 0;
    if (GetConsoleMode(out, &out_mode)) SetConsoleMode(out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    term_write(ENTER_SCREEN);
    return true;
}

void term_restore(void) {
    term_write(LEAVE_SCREEN);
    if (g_console_in) SetConsoleMode(g_console_in, g_console_in_mode);
}

InputEvent term_read_event(void) {
    InputEvent ev = { EV_NONE, 0, 0, 0 };
    for (;;) {
        INPUT_RECORD rec;
        DWORD got = 0;
        if (!ReadConsoleInput(g_console_in, &rec, 1, &got) || got == 0) {
            ev.kind = EV_QUIT;
            return ev;
        }
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            const KEY_EVENT_RECORD *k = &rec.Event.KeyEvent;
            switch (k->wVirtualKeyCode) {
            case VK_UP:     ev.kind = EV_UP;    return ev;
            case VK_DOWN:   ev.kind = EV_DOWN;  return ev;
            case VK_LEFT:   ev.kind = EV_LEFT;  return ev;
            case VK_RIGHT:  ev.kind = EV_RIGHT; return ev;
            case VK_ESCAPE: ev.kind = EV_ESC;   return ev;
            default: break;
            }
            const int ch = (unsigned char)k->uChar.AsciiChar;
            if (ch == 3)                 { ev.kind = EV_QUIT;      return ev; }
            if (ch == '\r')              { ev.kind = EV_ENTER;     return ev; }
            if (ch == 8)                 { ev.kind = EV_BACKSPACE; return ev; }
            if (ch >= 32 && ch < 127)    { ev.kind = EV_CHAR; ev.ch = ch; return ev; }
        } else if (rec.EventType == MOUSE_EVENT) {
            const MOUSE_EVENT_RECORD *m = &rec.Event.MouseEvent;
            const bool was_down = g_left_down;
            g_left_down = (m->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;
            ev.row = m->dwMousePosition.Y;
            ev.col = m->dwMousePosition.X;
            if (m->dwEventFlags & MOUSE_MOVED) {
                ev.kind = EV_MOUSE_MOVE;
                return ev;
            }
            if (m->dwEventFlags == 0 && g_left_down && !was_down) {
                ev.kind = EV_MOUSE_PRESS;
                return ev;
            }
        }
    }
}

#else

static struct termios g_saved_termios;
static bool           g_raw_mode = false;

bool input_ready(int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int read_byte(void) {
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? (int)c : -1;
}

bool term_init(void) {
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) return false;
        struct termios t = g_saved_termios;
        t.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);   /* Ctrl-C arrives as a byte */
        t.c_iflag &= (tcflag_t)~(IXON | ICRNL);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return false;
        g_raw_mode = true;
    }
    term_write(ENTER_SCREEN);
    return true;
}

void term_restore(void) {
    term_write(LEAVE_SCREEN);
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_raw_mode = false;
    }
}

/* SGR mouse report: ESC [ < button ; x ; y (M = press/motion, m = release). */
InputEvent parse_sgr_mouse(const char *seq) {
    InputEvent ev = { EV_NONE, 0, 0, 0 };
    int button = 0, x = 0, y = 0;
    char final = '\0';
    if (sscanf(seq, "<%d;%d;%d%c", &button, &x, &y, &final) != 4) return ev;
    ev.row = y - 1;
    ev.col = x - 1;
    if (final != 'M' || (button & 64)) return ev;          /* releases and wheel are ignored */
    if (button & 32)            ev.kind = EV_MOUSE_MOVE;  /* motion, with or without a button */
    else if ((button & 3) == 0) ev.kind = EV_MOUSE_PRESS; /* left button */
    return ev;
}

EventKind arrow_kind(int final) {
    switch (final) {
    case 'A': return EV_UP;
    case 'B': return EV_DOWN;
    case 'C': return EV_RIGHT;
    case 'D': return EV_LEFT;
    }
    return EV_NONE;
}

InputEvent term_read_event(void) {
    InputEvent ev = { EV_NONE, 0, 0, 0 };
    const int b = read_byte();
    if (b < 0 || b == 3) {
        ev.kind = EV_QUIT;
        return ev;
    }
    if (b == 27) {
        if (!input_ready(50)) {                       /* a lone Esc key */
            ev.kind = EV_ESC;
            return ev;
        }
        const int b2 = read_byte();
        if (b2 == 'O') {                              /* arrows in application mode */
            const int f = read_byte();
            ev.kind = f < 0 ? EV_QUIT : arrow_kind(f);
            return ev;
        }
        if (b2 != '[') return ev;
        char seq[32];
        size_t n = 0;
        int c;
        do {
            c = read_byte();
            if (c < 0) {
                ev.kind = EV_QUIT;
                return ev;
            }
            if (n + 1 < sizeof seq) seq[n++] = (char)c;
        } while (c < 0x40 || c > 0x7e);               /* final byte ends the sequence */
        seq[n] = '\0';
        if (seq[0] == '<') return parse_sgr_mouse(seq);
        ev.kind = arrow_kind(c);
        return ev;
    }
    if (b == '\r' || b == '\n') { ev.kind = EV_ENTER;     return ev; }
    if (b == 127 || b == 8)     { ev.kind = EV_BACKSPACE; return ev; }
    if (b >= 32 && b < 127)     { ev.kind = EV_CHAR; ev.ch = b; return ev; }
    return ev;
}

#endif
void append_cell(char *f, size_t n, const GameState *g, const Screen *s,
                        bool legal[BOARD_SIZE][BOARD_SIZE], int r, int c) {
    const CellState st = g->board[r][c];
    const bool cursor = s->show_cursor && s->cursor.row == r && s->cursor.col == c;
    const bool last = g->move_count > 0 &&
                      g->history[g->move_count - 1].pos.row == r &&
                      g->history[g->move_count - 1].pos.col == c;
    const char *bg = last ? SGR_BG_LAST : SGR_BG_BOARD;
    const char *fg = SGR_DIM;
    char glyph = '.';
    if (st == CELL_BLACK)      { fg = SGR_BLACK; glyph = 'X'; }
    else if (st == CELL_WHITE) { fg = SGR_WHITE; glyph = 'O'; }
    else if (legal[r][c])      { fg = SGR_HINT;  glyph = '*'; }
    append_fmt(f, n, "%s%s%s %c %s", bg, cursor ? SGR_INVERSE : "", fg, glyph, SGR_RESET);
}

void describe_result(const GameState *g, char *out, size_t n) {
    int b, w;
    count_pieces(g, &b, &w);
    switch (get_game_result(g)) {
    case RESULT_BLACK_WIN: snprintf(out, n, "%s (Black) wins %d-%d", player_name(g, PLAYER_BLACK), b, w); break;
    case RESULT_WHITE_WIN: snprintf(out, n, "%s (White) wins %d-%d", player_name(g, PLAYER_WHITE), w, b); break;
    default:               snprintf(out, n, "tie %d-%d", b, w);                                          break;
    }
}

/* Fixed 80x15 layout. Board occupies rows BOARD_TOP.. and columns BOARD_LEFT.. ; history starts at HISTORY_LEFT. */
void render(const Screen *s) {
    static char f[FRAME_SIZE];
    const GameState *g = s->state;
    char text[160];
    bool legal[BOARD_SIZE][BOARD_SIZE];
    f[0] = '\0';

    snprintf(text, sizeof text, g ? " OTHELLO    %s (X)  vs  %s (O)" : " OTHELLO",
             g ? player_name(g, PLAYER_BLACK) : "", g ? player_name(g, PLAYER_WHITE) : "");
    append_fmt(f, sizeof f, "%s%-78s%s%s\n", SGR_TITLE, text, SGR_RESET, SGR_CLEAR_EOL);

    if (!g) {
        append_fmt(f, sizeof f, "%s\n", SGR_CLEAR_EOL);
    } else if (s->tag) {
        append_fmt(f, sizeof f, " %s%s%s%s\n", SGR_TAG, s->tag, SGR_RESET, SGR_CLEAR_EOL);
    } else if (is_game_over(g)) {
        describe_result(g, text, sizeof text);
        append_fmt(f, sizeof f, " %sGAME OVER:%s %s%s\n", SGR_TAG, SGR_RESET, text, SGR_CLEAR_EOL);
    } else {
        append_fmt(f, sizeof f, " Turn: %s (%s)%s\n",
                   player_name(g, g->current_turn), color_name(g->current_turn), SGR_CLEAR_EOL);
    }

    append_fmt(f, sizeof f, "   ");
    for (int c = 0; c < BOARD_SIZE; c++) append_fmt(f, sizeof f, "%s %c %s", SGR_LABEL, 'A' + c, SGR_RESET);
    append_fmt(f, sizeof f, "   %sMoves%s%s\n", SGR_LABEL, SGR_RESET, SGR_CLEAR_EOL);

    memset(legal, 0, sizeof legal);
    if (g && s->hints && !is_game_over(g)) get_all_valid_moves(g, g->current_turn, legal);
    const int first = (g && g->move_count > HISTORY_ROWS) ? g->move_count - HISTORY_ROWS : 0;

    for (int r = 0; r < BOARD_SIZE; r++) {
        append_fmt(f, sizeof f, "%s %d %s", SGR_LABEL, r + 1, SGR_RESET);
        if (g) {
            for (int c = 0; c < BOARD_SIZE; c++) append_cell(f, sizeof f, g, s, legal, r, c);
        } else {
            append_fmt(f, sizeof f, "%*s", BOARD_SIZE * CELL_WIDTH, "");
        }
        append_fmt(f, sizeof f, "   ");
        const int i = first + r;
        if (g && i < g->move_count) {
            char sq[3];
            const bool latest = (i == g->move_count - 1);
            position_to_string(g->history[i].pos, sq);
            append_fmt(f, sizeof f, "%s%2d. %-5s %s%s", latest ? SGR_TAG : "", i + 1,
                       color_name(g->history[i].player), sq, latest ? SGR_RESET : "");
        }
        append_fmt(f, sizeof f, "%s\n", SGR_CLEAR_EOL);
    }

    if (g) {
        int b, w;
        count_pieces(g, &b, &w);
        append_fmt(f, sizeof f, "\n  Black X: %d   White O: %d%s\n", b, w, SGR_CLEAR_EOL);
    } else {
        append_fmt(f, sizeof f, "\n%s\n", SGR_CLEAR_EOL);
    }

    if (s->status && s->status[0] != '\0') {
        append_fmt(f, sizeof f, " %s%.*s%s%s\n", s->status_error ? SGR_ERROR : SGR_OK,
                   STATUS_WIDTH, s->status, SGR_RESET, SGR_CLEAR_EOL);
    } else {
        append_fmt(f, sizeof f, "%s\n", SGR_CLEAR_EOL);
    }

    if (s->input_prompt) {
        append_fmt(f, sizeof f, " %s%s%s%s%s %s%s", SGR_TAG, s->input_prompt, SGR_RESET,
                   s->input_text ? s->input_text : "", SGR_INVERSE, SGR_RESET, SGR_CLEAR_EOL);
    } else {
        append_fmt(f, sizeof f, " %s%s%s%s", SGR_DIM, s->help ? s->help : "", SGR_RESET, SGR_CLEAR_EOL);
    }

    term_write("\x1b[H");
    fputs(f, stdout);
    term_write("\x1b[J");
}
