/*
 * Othello (terminal, C17). Single-file build.
 *   Linux/macOS: gcc -std=c17 -Wall -Wextra -pedantic othello_merged.c -o othello
 *   Windows:     x86_64-w64-mingw32-gcc -std=c17 -Wall -Wextra -pedantic othello_merged.c -o othello.exe
 * Controls are listed on the help screen (:h).
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

/* ============================================================
 * Constants
 * ============================================================ */

enum {
    BOARD_SIZE     = 8,
    MAX_MOVES      = 60,
    MAX_NAME_LEN   = 32,      /* includes the terminating NULL */
    MAX_PATH_LEN   = 256,
    MAX_STAT_FILES = 32,
    MAX_TOKENS     = 40,
    MAX_LINE_LEN   = 2048,
    MAX_MSG_LEN    = 400,
    HISTORY_ROWS   = 8,
    NUM_DIRECTIONS = 8,
    ERROR_TEXT_LEN = 96
};

/* ============================================================
 * Types
 * ============================================================ */

typedef enum { CELL_EMPTY, CELL_BLACK, CELL_WHITE } CellState;

typedef enum { PLAYER_BLACK = CELL_BLACK, PLAYER_WHITE = CELL_WHITE } PlayerColor;

/* Zero-based. "a1" is {row 0, col 0}; rank 1 is drawn at the top of the board. */
typedef struct {
    int row;
    int col;
} Position;

// MoveRecord is required because the number of moves to find white and black can be misleading if someone hasn't made legal move
// E.g. Move 1 -> Black; Move 2 -> White; but if black or white had skipped turn then we have to account for things and this method is simpler than using 3 integer variables: tot_moves, white_skips, black_skips 
typedef struct {
    PlayerColor player;
    Position    pos;
} MoveRecord;

typedef struct {
    char        player1_name[MAX_NAME_LEN];
    char        player2_name[MAX_NAME_LEN];
    PlayerColor p1_color;               /* player 2 always has the other colour */
} GameMeta;

typedef struct {
    CellState   board[BOARD_SIZE][BOARD_SIZE];
    PlayerColor current_turn;
    MoveRecord  history[MAX_MOVES];
    int         move_count;             /* history[0 .. move_count-1] is valid */
    GameMeta    meta;
} GameState;

typedef enum { RESULT_IN_PROGRESS, RESULT_BLACK_WIN, RESULT_WHITE_WIN, RESULT_TIE } GameResult;

typedef enum { TURN_SWITCHED, TURN_OPPONENT_PASSED, TURN_GAME_OVER } TurnStatus;

/* Only the reasons a player can cause. Bad arguments and off-board squares are asserted. */
typedef enum { MOVE_OK, MOVE_ERR_OCCUPIED, MOVE_ERR_NO_CAPTURE } MoveError;

typedef enum {
    STORAGE_OK,
    STORAGE_ERR_IO,
    STORAGE_ERR_FORMAT,
    STORAGE_ERR_ILLEGAL_MOVE
} StorageStatus;

typedef enum {
    CMD_INVALID,
    CMD_NEW,
    CMD_LOAD,
    CMD_SAVE,           /* :w */
    CMD_VIEW,           /* :v */
    CMD_STATS,          /* :s */
    CMD_HELP,
    CMD_QUIT,
    CMD_TOGGLE_HINTS    /* :t */
} CommandType;

typedef struct {
    CommandType type;
    char        p1[MAX_NAME_LEN];       /* empty when not given */
    char        p2[MAX_NAME_LEN];
    bool        white_is_p1;
    char        path[MAX_PATH_LEN];     /* empty when not given */
    char        files[MAX_STAT_FILES][MAX_PATH_LEN];
    int         file_count;
    char        error[ERROR_TEXT_LEN];  /* set when type == CMD_INVALID */
} ParsedCommand;

typedef struct {
    int  total_games;
    int  skipped_files;
    int  black_wins;
    int  white_wins;
    int  ties;
    int  max_score;
    char highest_scoring_player[MAX_NAME_LEN];
} StatsSummary;

/* N, NE, E, SE, S, SW, W, NW as (row delta, col delta). */
static const Position DIRECTIONS[NUM_DIRECTIONS] = {
    {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}
};

/* ============================================================
 * Screen types and colours
 * ============================================================ */

enum {
    BOARD_TOP    = 3,      /* screen row of rank 1 */
    BOARD_LEFT   = 3,      /* screen column of file A */
    CELL_WIDTH   = 3,
    HISTORY_LEFT = 30,
    FRAME_SIZE   = 16384,
    STATUS_WIDTH = 76
};

typedef enum {
    EV_NONE,
    EV_CHAR,
    EV_ENTER,
    EV_BACKSPACE,
    EV_ESC,
    EV_UP,
    EV_DOWN,
    EV_LEFT,
    EV_RIGHT,
    EV_MOUSE_PRESS,
    EV_MOUSE_MOVE,
    EV_QUIT
} EventKind;

typedef struct {
    EventKind kind;
    int       ch;                  /* EV_CHAR */
    int       row;                 /* mouse: 0-based screen row */
    int       col;                 /* mouse: 0-based screen column */
} InputEvent;

/* Everything the renderer needs. Built fresh for each frame. */
typedef struct {
    const GameState *state;        /* NULL before the first game */
    Position         cursor;
    bool             show_cursor;
    bool             hints;
    const char      *tag;          /* replay label, or NULL */
    const char      *status;
    bool             status_error;
    const char      *input_prompt; /* non-NULL: bottom line is a text prompt */
    const char      *input_text;
    const char      *help;         /* bottom line when there is no prompt */
} Screen;

typedef struct {
    GameState game;
    bool      have_game;
    bool      hints;
    bool      quit;
    Position  cursor;
    char      status[MAX_MSG_LEN];
    bool      status_error;
} App;

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

/* ============================================================
 * Function declarations
 * ============================================================ */

/* -- geometry and text -- */
static bool         pos_in_bounds(Position p);
static PlayerColor  opponent_of(PlayerColor p);
static bool         parse_position(const char *text, Position *out);
static void         position_to_string(Position pos, char out[3]);
static const char  *player_name(const GameState *g, PlayerColor c);
static const char  *color_name(PlayerColor c);
static void         trim(char *s);
static void         append_fmt(char *buf, size_t size, const char *fmt, ...);
static int          clamp_int(int v, int lo, int hi);

/* -- rules -- */
static int          count_flips(const GameState *s, PlayerColor player, Position pos, Position dir);
static MoveError    check_move(const GameState *s, PlayerColor player, Position pos);
static const char  *move_error_str(MoveError err);
static bool         validate_move(const GameState *s, PlayerColor player, Position pos);
static int          get_all_valid_moves(const GameState *s, PlayerColor player,
                                        bool valid_out[BOARD_SIZE][BOARD_SIZE]);
static bool         has_any_valid_move(const GameState *s, PlayerColor player);
static bool         is_game_over(const GameState *s);
static void         init_game(GameState *s, const GameMeta *meta);
static int          execute_move(GameState *s, Position pos);
static TurnStatus   switch_turn(GameState *s);
static void         count_pieces(const GameState *s, int *black, int *white);
static GameResult   get_game_result(const GameState *s);
static bool         rebuild_state_at(const GameState *src, int step, GameState *out);

/* -- save files and statistics -- */
static char         color_char(PlayerColor c);
static StorageStatus save_game(const GameState *s, const char *path);
static bool         read_field(FILE *f, const char *key, char *line, size_t size, const char **value);
static StorageStatus load_game(GameState *state, const char *path);
static const char  *storage_status_str(StorageStatus st);
static void         compute_stats_summary(const char *const paths[], int count, StatsSummary *out);
static void         print_stats_summary(const StatsSummary *s);

/* -- command line -- */
static void         set_error(ParsedCommand *o, const char *msg);
static int          tokenize(const char *s, char tok[][MAX_PATH_LEN], char *err, size_t err_size);
static bool         take_name(const char *value, char *dst, ParsedCommand *o);
static bool         parse_command(const char *input, ParsedCommand *out);

/* -- terminal -- */
static bool         term_init(void);
static void         term_restore(void);
static void         term_write(const char *s);
static InputEvent   term_read_event(void);

/* -- app state and rendering -- */
static void         set_status(App *a, bool is_error, const char *fmt, ...);
static Screen       app_screen(const App *a, const char *prompt, const char *text);
static void         render(const Screen *s);
static void         describe_result(const GameState *g, char *out, size_t n);
static bool         screen_to_cell(int row, int col, Position *out);
static void         move_cursor(App *a, int dr, int dc);
static void         app_place(App *a);
static void         app_handle_char(App *a, int ch);
static void         app_handle_event(App *a, InputEvent ev);
static bool         edit_line(App *a, const char *prompt, char *buf, size_t size);
static void         wait_key(App *a);
static bool         prompt_name(App *a, const char *label, char *dst);
static bool         prompt_new_game(App *a, const ParsedCommand *cmd, GameMeta *meta);
static void         command_prompt(App *a);
static void         run_command(App *a, const char *line);
static void         show_help(App *a);
static void         show_stats(App *a, const char *const paths[], int count);
static void         view_replay(App *a, const GameState *game);

int main(void);

/* ============================================================
 * Geometry and text
 * ============================================================ */

static bool pos_in_bounds(Position p) {
    return p.row >= 0 && p.row < BOARD_SIZE && p.col >= 0 && p.col < BOARD_SIZE;
}

static PlayerColor opponent_of(PlayerColor p) {
    return p == PLAYER_BLACK ? PLAYER_WHITE : PLAYER_BLACK;
}

/* Accepts exactly two characters: a-h (any case) followed by 1-8. */
static bool parse_position(const char *text, Position *out) {
    if (!text[0] || !text[1] || text[2]) return false;
    int file = tolower((unsigned char)text[0]);
    if (file < 'a' || file > 'h' || text[1] < '1' || text[1] > '8') return false;
    out->col = file - 'a';
    out->row = text[1] - '1';
    return true;
}

static void position_to_string(Position pos, char out[3]) {
    assert(pos_in_bounds(pos));
    out[0] = (char)('a' + pos.col);
    out[1] = (char)('1' + pos.row);
    out[2] = '\0';
}

static const char *player_name(const GameState *g, PlayerColor c) {
    return g->meta.p1_color == c ? g->meta.player1_name : g->meta.player2_name;
}

static const char *color_name(PlayerColor c) {
    return c == PLAYER_BLACK ? "Black" : "White";
}

static void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    const char *start = s;
    while (isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

/* Appends formatted text to a NUL-terminated buffer, never writing past `size`. */
static void append_fmt(char *buf, size_t size, const char *fmt, ...) {
    size_t len = strlen(buf);
    if (len >= size) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + len, size - len, fmt, ap);
    va_end(ap);
}

/* ============================================================
 * Rules: validation, capture, turns
 * ============================================================ */

/* Opponent discs that would flip from pos along dir (0 if dir is not a capture line). */
static int count_flips(const GameState *s, PlayerColor player, Position pos, Position dir) {
    const CellState me  = (CellState)player;
    const CellState opp = (CellState)opponent_of(player);
    int n = 0;
    Position p = { pos.row + dir.row, pos.col + dir.col };
    while (pos_in_bounds(p) && s->board[p.row][p.col] == opp) {
        n++;
        p.row += dir.row;
        p.col += dir.col;
    }
    return (n > 0 && pos_in_bounds(p) && s->board[p.row][p.col] == me) ? n : 0;
}

/* Precondition: pos is on the board. */
static MoveError check_move(const GameState *s, PlayerColor player, Position pos) {
    assert(s != NULL);
    assert(player == PLAYER_BLACK || player == PLAYER_WHITE);
    assert(pos_in_bounds(pos));

    if (s->board[pos.row][pos.col] != CELL_EMPTY) return MOVE_ERR_OCCUPIED;
    for (int d = 0; d < NUM_DIRECTIONS; d++) {
        if (count_flips(s, player, pos, DIRECTIONS[d]) > 0) return MOVE_OK;
    }
    return MOVE_ERR_NO_CAPTURE;
}

static const char *move_error_str(MoveError err) {
    switch (err) {
    case MOVE_OK:             return "Move is valid";
    case MOVE_ERR_OCCUPIED:   return "Square is already occupied";
    case MOVE_ERR_NO_CAPTURE: return "Move must flip at least one opponent piece";
    }
    return "Unknown error";
}

static bool validate_move(const GameState *s, PlayerColor player, Position pos) {
    return check_move(s, player, pos) == MOVE_OK;
}

/* Fills every cell of valid_out and returns the number of legal moves. */
static int get_all_valid_moves(const GameState *s, PlayerColor player,
                               bool valid_out[BOARD_SIZE][BOARD_SIZE]) {
    assert(valid_out != NULL);
    int count = 0;
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            valid_out[r][c] = validate_move(s, player, (Position){ r, c });
            count += valid_out[r][c];
        }
    }
    return count;
}

static bool has_any_valid_move(const GameState *s, PlayerColor player) {
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            if (validate_move(s, player, (Position){ r, c })) return true;
        }
    }
    return false;
}

/* Derived from the board: nobody can move. */
static bool is_game_over(const GameState *s) {
    return !has_any_valid_move(s, PLAYER_BLACK) && !has_any_valid_move(s, PLAYER_WHITE);
}

static void init_game(GameState *s, const GameMeta *meta) {
    enum { MID = BOARD_SIZE / 2 };
    memset(s, 0, sizeof *s);
    s->board[MID - 1][MID - 1] = CELL_WHITE;   /* D4 */
    s->board[MID - 1][MID]     = CELL_BLACK;   /* E4 */
    s->board[MID][MID - 1]     = CELL_BLACK;   /* D5 */
    s->board[MID][MID]         = CELL_WHITE;   /* E5 */
    s->current_turn = PLAYER_BLACK;
    s->meta = *meta;
}

/* Precondition: check_move(s, current_turn, pos) == MOVE_OK. Does not change the turn. */
static int execute_move(GameState *s, Position pos) {
    assert(check_move(s, s->current_turn, pos) == MOVE_OK);
    assert(s->move_count < MAX_MOVES);

    const PlayerColor me = s->current_turn;
    int total = 0;
    for (int d = 0; d < NUM_DIRECTIONS; d++) {
        const int n = count_flips(s, me, pos, DIRECTIONS[d]);
        Position p = pos;
        for (int k = 0; k < n; k++) {
            p.row += DIRECTIONS[d].row;
            p.col += DIRECTIONS[d].col;
            s->board[p.row][p.col] = (CellState)me;
        }
        total += n;
    }
    s->board[pos.row][pos.col] = (CellState)me;
    s->history[s->move_count] = (MoveRecord){ me, pos };
    s->move_count++;
    return total;
}

/* Gives the turn to the opponent if they can move, otherwise keeps it if the mover can still move. */
static TurnStatus switch_turn(GameState *s) {
    const PlayerColor opp = opponent_of(s->current_turn);
    if (has_any_valid_move(s, opp)) {
        s->current_turn = opp;
        return TURN_SWITCHED;
    }
    if (has_any_valid_move(s, s->current_turn)) return TURN_OPPONENT_PASSED;
    return TURN_GAME_OVER;
}

static void count_pieces(const GameState *s, int *black, int *white) {
    *black = 0;
    *white = 0;
    for (int r = 0; r < BOARD_SIZE; r++) {
        for (int c = 0; c < BOARD_SIZE; c++) {
            if (s->board[r][c] == CELL_BLACK)      (*black)++;
            else if (s->board[r][c] == CELL_WHITE) (*white)++;
        }
    }
}

static GameResult get_game_result(const GameState *s) {
    if (!is_game_over(s)) return RESULT_IN_PROGRESS;
    int b, w;
    count_pieces(s, &b, &w);
    if (b > w) return RESULT_BLACK_WIN;
    if (w > b) return RESULT_WHITE_WIN;
    return RESULT_TIE;
}

/* Replays the first `step` moves of src into out. Fails if any recorded move is illegal. */
static bool rebuild_state_at(const GameState *src, int step, GameState *out) {
    assert(src != out);
    if (step < 0 || step > src->move_count) return false;

    init_game(out, &src->meta);
    for (int i = 0; i < step; i++) {
        const MoveRecord rec = src->history[i];
        if (is_game_over(out) || rec.player != out->current_turn) return false;
        if (!pos_in_bounds(rec.pos) || check_move(out, rec.player, rec.pos) != MOVE_OK) return false;
        execute_move(out, rec.pos);
        switch_turn(out);
    }
    return true;
}

/* ============================================================
 * Terminal: raw input, mouse tracking, alternate screen
 * ============================================================ */

static const char *const ENTER_SCREEN =
    "\x1b[?1049h"      /* alternate screen */
    "\x1b[?25l"        /* hide cursor */
    "\x1b[?1003h"      /* report all mouse motion */
    "\x1b[?1006h"      /* SGR mouse coordinates */
    "\x1b[2J";
static const char *const LEAVE_SCREEN =
    "\x1b[?1003l\x1b[?1006l\x1b[?25h\x1b[?1049l\x1b[0m";

static void term_write(const char *s) {
    fputs(s, stdout);
    fflush(stdout);
}

#ifdef _WIN32

static HANDLE g_console_in;
static DWORD  g_console_in_mode;
static bool   g_left_down;

static bool term_init(void) {
    g_console_in = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(g_console_in, &g_console_in_mode)) return false;
    SetConsoleMode(g_console_in, ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD out_mode = 0;
    if (GetConsoleMode(out, &out_mode)) SetConsoleMode(out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    term_write(ENTER_SCREEN);
    return true;
}

static void term_restore(void) {
    term_write(LEAVE_SCREEN);
    if (g_console_in) SetConsoleMode(g_console_in, g_console_in_mode);
}

static InputEvent term_read_event(void) {
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

static bool input_ready(int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static int read_byte(void) {
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? (int)c : -1;
}

static bool term_init(void) {
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

static void term_restore(void) {
    term_write(LEAVE_SCREEN);
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_raw_mode = false;
    }
}

/* SGR mouse report: ESC [ < button ; x ; y (M = press/motion, m = release). */
static InputEvent parse_sgr_mouse(const char *seq) {
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

static EventKind arrow_kind(int final) {
    switch (final) {
    case 'A': return EV_UP;
    case 'B': return EV_DOWN;
    case 'C': return EV_RIGHT;
    case 'D': return EV_LEFT;
    }
    return EV_NONE;
}

static InputEvent term_read_event(void) {
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

/* ============================================================
 * Save files
 *
 * Format:
 *   OTHELLO 1
 *   P1=<name>
 *   P2=<name>
 *   P1COLOR=B|W
 *   MOVES=<n>
 *   <square> <B|W>     (n lines, for example "d3 B")
 * ============================================================ */

static char color_char(PlayerColor c) {
    return c == PLAYER_BLACK ? 'B' : 'W';
}

/* Writes to <path>.tmp and renames, so a failed save never leaves a truncated file. */
static StorageStatus save_game(const GameState *s, const char *path) {
    char tmp[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) return STORAGE_ERR_IO;

    fprintf(f, "OTHELLO 1\nP1=%s\nP2=%s\nP1COLOR=%c\nMOVES=%d\n",
            s->meta.player1_name, s->meta.player2_name, color_char(s->meta.p1_color), s->move_count);
    for (int i = 0; i < s->move_count; i++) {
        char sq[3];
        position_to_string(s->history[i].pos, sq);
        fprintf(f, "%s %c\n", sq, color_char(s->history[i].player));
    }
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmp);
        return STORAGE_ERR_IO;
    }

    if (rename(tmp, path) != 0) {
        remove(path);                   /* Windows: rename fails when the target exists */
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return STORAGE_ERR_IO;
        }
    }
    return STORAGE_OK;
}

/* Reads one line and checks that it starts with `key`. On success, *value points into `line`. */
static bool read_field(FILE *f, const char *key, char *line, size_t size, const char **value) {
    if (!fgets(line, (int)size, f)) return false;
    line[strcspn(line, "\r\n")] = '\0';
    const size_t k = strlen(key);
    if (strncmp(line, key, k) != 0) return false;
    *value = line + k;
    return true;
}

/* All-or-nothing: *state changes only when the result is STORAGE_OK. */
static StorageStatus load_game(GameState *state, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return STORAGE_ERR_IO;

    GameState src, rebuilt;
    memset(&src, 0, sizeof src);
    char line[128];
    const char *v = NULL;
    char *end = NULL;
    StorageStatus st = STORAGE_ERR_FORMAT;

    if (!fgets(line, sizeof line, f)) goto done;
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, "OTHELLO 1") != 0) goto done;

    if (!read_field(f, "P1=", line, sizeof line, &v) || !*v || strlen(v) >= MAX_NAME_LEN) goto done;
    snprintf(src.meta.player1_name, sizeof src.meta.player1_name, "%s", v);
    if (!read_field(f, "P2=", line, sizeof line, &v) || !*v || strlen(v) >= MAX_NAME_LEN) goto done;
    snprintf(src.meta.player2_name, sizeof src.meta.player2_name, "%s", v);

    if (!read_field(f, "P1COLOR=", line, sizeof line, &v)) goto done;
    if (strcmp(v, "B") == 0)      src.meta.p1_color = PLAYER_BLACK;
    else if (strcmp(v, "W") == 0) src.meta.p1_color = PLAYER_WHITE;
    else goto done;

    if (!read_field(f, "MOVES=", line, sizeof line, &v)) goto done;
    const long n = strtol(v, &end, 10);
    if (end == v || *end != '\0' || n < 0 || n > MAX_MOVES) goto done;
    src.move_count = (int)n;

    for (int i = 0; i < src.move_count; i++) {
        if (!fgets(line, sizeof line, f)) goto done;
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) != 4 || line[2] != ' ') goto done;
        const char sq[3] = { line[0], line[1], '\0' };
        if (!parse_position(sq, &src.history[i].pos)) goto done;
        if (line[3] == 'B')      src.history[i].player = PLAYER_BLACK;
        else if (line[3] == 'W') src.history[i].player = PLAYER_WHITE;
        else goto done;
    }

    if (!rebuild_state_at(&src, src.move_count, &rebuilt)) {
        st = STORAGE_ERR_ILLEGAL_MOVE;
        goto done;
    }
    *state = rebuilt;
    st = STORAGE_OK;

done:
    fclose(f);
    return st;
}

static const char *storage_status_str(StorageStatus st) {
    switch (st) {
    case STORAGE_OK:               return "OK";
    case STORAGE_ERR_IO:           return "cannot open/read/write the file";
    case STORAGE_ERR_FORMAT:       return "file is not a valid Othello save";
    case STORAGE_ERR_ILLEGAL_MOVE: return "saved moves are not a legal game";
    }
    return "unknown error";
}

/* ============================================================
 * Statistics
 * ============================================================ */

/* Counts finished games only. Unreadable, corrupt or unfinished files go into skipped_files. */
static void compute_stats_summary(const char *const paths[], int count, StatsSummary *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < count; i++) {
        GameState g;
        if (load_game(&g, paths[i]) != STORAGE_OK || !is_game_over(&g)) {
            out->skipped_files++;
            continue;
        }
        int b, w;
        count_pieces(&g, &b, &w);
        out->total_games++;
        switch (get_game_result(&g)) {
        case RESULT_BLACK_WIN: out->black_wins++; break;
        case RESULT_WHITE_WIN: out->white_wins++; break;
        default:               out->ties++;       break;
        }
        if (b > out->max_score) {
            out->max_score = b;
            snprintf(out->highest_scoring_player, sizeof out->highest_scoring_player,
                     "%s", player_name(&g, PLAYER_BLACK));
        }
        if (w > out->max_score) {
            out->max_score = w;
            snprintf(out->highest_scoring_player, sizeof out->highest_scoring_player,
                     "%s", player_name(&g, PLAYER_WHITE));
        }
    }
}

static void print_stats_summary(const StatsSummary *s) {
    puts("---------- Statistics ----------");
    printf("Finished games counted : %d\n", s->total_games);
    printf("Files skipped          : %d (unreadable, corrupt or unfinished)\n", s->skipped_files);
    printf("Black wins             : %d\n", s->black_wins);
    printf("White wins             : %d\n", s->white_wins);
    printf("Ties                   : %d\n", s->ties);
    if (s->total_games > 0) printf("Highest score          : %d (%s)\n", s->max_score, s->highest_scoring_player);
    else                    puts("Highest score          : -");
    puts("--------------------------------");
}

/* ============================================================
 * Command line
 * ============================================================ */

static void set_error(ParsedCommand *o, const char *msg) {
    o->type = CMD_INVALID;
    snprintf(o->error, sizeof o->error, "%s", msg);
}

/* Splits on whitespace. Double quotes group text and are removed. Returns the token count, or -1 on error. */
static int tokenize(const char *s, char tok[][MAX_PATH_LEN], char *err, size_t err_size) {
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

static bool take_name(const char *value, char *dst, ParsedCommand *o) {
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

static bool parse_command(const char *input, ParsedCommand *out) {
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

/* ============================================================
 * Screen rendering
 * ============================================================ */

static void append_cell(char *f, size_t n, const GameState *g, const Screen *s,
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

static void describe_result(const GameState *g, char *out, size_t n) {
    int b, w;
    count_pieces(g, &b, &w);
    switch (get_game_result(g)) {
    case RESULT_BLACK_WIN: snprintf(out, n, "%s (Black) wins %d-%d", player_name(g, PLAYER_BLACK), b, w); break;
    case RESULT_WHITE_WIN: snprintf(out, n, "%s (White) wins %d-%d", player_name(g, PLAYER_WHITE), w, b); break;
    default:               snprintf(out, n, "tie %d-%d", b, w);                                          break;
    }
}

/* Fixed 80x15 layout. Board occupies rows BOARD_TOP.. and columns BOARD_LEFT.. ; history starts at HISTORY_LEFT. */
static void render(const Screen *s) {
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

/* ============================================================
 * Application state and input handling
 * ============================================================ */

static void set_status(App *a, bool is_error, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
    a->status_error = is_error;
}

static Screen app_screen(const App *a, const char *prompt, const char *text) {
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

static int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* Maps a screen cell to a board square. False if the cell is not on the board. */
static bool screen_to_cell(int row, int col, Position *out) {
    const int r = row - BOARD_TOP;
    const int c = col - BOARD_LEFT;
    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE * CELL_WIDTH) return false;
    out->row = r;
    out->col = c / CELL_WIDTH;
    return true;
}

static void move_cursor(App *a, int dr, int dc) {
    a->cursor.row = clamp_int(a->cursor.row + dr, 0, BOARD_SIZE - 1);
    a->cursor.col = clamp_int(a->cursor.col + dc, 0, BOARD_SIZE - 1);
}

static void app_place(App *a) {
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

static void app_handle_char(App *a, int ch) {
    const int lc = tolower(ch);
    if (ch == ' ')                       app_place(a);
    else if (lc >= 'a' && lc <= 'h')     a->cursor.col = lc - 'a';
    else if (ch >= '1' && ch <= '8')     a->cursor.row = ch - '1';
    else if (lc == 'k')                  move_cursor(a, -1, 0);
    else if (lc == 'j')                  move_cursor(a, 1, 0);
    else if (lc == 'l')                  move_cursor(a, 0, 1);
    else if (ch == ':')                  command_prompt(a);
}

static void app_handle_event(App *a, InputEvent ev) {
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
static bool edit_line(App *a, const char *prompt, char *buf, size_t size) {
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

static void wait_key(App *a) {
    for (;;) {
        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) {
            a->quit = true;
            return;
        }
        if (ev.kind != EV_NONE && ev.kind != EV_MOUSE_MOVE) return;
    }
}

static bool prompt_name(App *a, const char *label, char *dst) {
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

static bool prompt_new_game(App *a, const ParsedCommand *cmd, GameMeta *meta) {
    memset(meta, 0, sizeof *meta);
    if (cmd->p1[0] != '\0') snprintf(meta->player1_name, sizeof meta->player1_name, "%s", cmd->p1);
    else if (!prompt_name(a, "Player 1 name: ", meta->player1_name)) return false;

    if (cmd->p2[0] != '\0') snprintf(meta->player2_name, sizeof meta->player2_name, "%s", cmd->p2);
    else if (!prompt_name(a, "Player 2 name: ", meta->player2_name)) return false;

    meta->p1_color = cmd->white_is_p1 ? PLAYER_WHITE : PLAYER_BLACK;
    return true;
}

static void command_prompt(App *a) {
    char text[MAX_LINE_LEN];
    if (!edit_line(a, ":", text, sizeof text) || text[0] == '\0') return;
    char line[MAX_LINE_LEN + 1];
    snprintf(line, sizeof line, ":%s", text);
    run_command(a, line);
}

static void run_command(App *a, const char *line) {
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

static void show_help(App *a) {
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

static void show_stats(App *a, const char *const paths[], int count) {
    StatsSummary sum;
    compute_stats_summary(paths, count, &sum);
    term_write("\x1b[H\x1b[2J");
    print_stats_summary(&sum);
    puts("\nPress any key to return.");
    fflush(stdout);
    wait_key(a);
    set_status(a, false, "Statistics: %d game(s) counted, %d skipped.", sum.total_games, sum.skipped_files);
}

static void view_replay(App *a, const GameState *game) {
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

/* ============================================================
 * Entry point
 * ============================================================ */

int main(void) {
    static App app;
    app.hints = true;
    app.cursor = (Position){ BOARD_SIZE / 2 - 1, BOARD_SIZE / 2 - 1 };
    set_status(&app, false, "Type :n for a new game, :h for help, :q to quit.");

    if (!term_init()) {
        fprintf(stderr, "Cannot set up the terminal.\n");
        return 1;
    }
    atexit(term_restore);

    while (!app.quit) {
        const Screen s = app_screen(&app, NULL, NULL);
        render(&s);
        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) break;
        app_handle_event(&app, ev);
    }
    return 0;
}