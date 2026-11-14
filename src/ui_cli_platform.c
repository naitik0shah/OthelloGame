#include "common.h"
#include <errno.h>

#ifndef _WIN32
#include <sys/ioctl.h>
#endif

/* Cross-module integration hook implemented in misc.c */
bool prompt_new_game(App *a, const ParsedCommand *cmd, GameMeta *meta);

/* Internal CLI & command-handling prototypes */
void command_prompt(App *a);
void run_command(App *a, const char *line);
void show_help(App *a);
void show_stats(App *a, const char *const paths[], int count);
static const GameState *g_replay_full_game = NULL;

void view_replay(App *a, const GameState *game, int initial_step);

/*
 * ANSI / DEC VT terminal control sequence builders.
 *
 * Every control sequence starts with ESC (ASCII 27 / 0x1B) followed by '['
 * (the Control Sequence Introducer, CSI). Adjacent C string literals are
 * concatenated by the compiler at translation time with zero runtime cost.
 */
#define CSI                 "\x1b["
#define SGR(codes)          CSI codes "m"
#define DEC_SET(mode)       CSI "?" #mode "h"
#define DEC_RESET(mode)     CSI "?" #mode "l"

/* SGR (Select Graphic Rendition) text attributes and colour parameters */
#define SGR_ATTR_RESET      "0"
#define SGR_ATTR_BOLD       "1"
#define SGR_ATTR_DIM        "2"
#define SGR_ATTR_INVERSE    "7"

#define FG_BLACK            "30"
#define FG_RED              "31"
#define FG_GREEN            "32"
#define FG_YELLOW           "33"
#define FG_CYAN             "36"
#define FG_BRIGHT_WHITE     "97"

#define BG_GREEN            "42"
#define BG_BLUE             "44"
#define BG_BRIGHT_YELLOW    "103"

/* Cursor and erase sequences (DEC ?12l disables blink; ?25l hides cursor) */
#define ANSI_CURSOR_HOME    CSI "H"
#define ANSI_CURSOR_HIDE    DEC_RESET(12) DEC_RESET(25)
#define ANSI_CURSOR_SHOW    DEC_SET(12) DEC_SET(25)
#define ANSI_CLEAR_SCREEN   CSI "2J"
#define ANSI_CLEAR_BELOW    CSI "J"
#define ANSI_CLEAR_EOL      CSI "K"

/* Semantic UI styles */
static const char *const SGR_RESET     = SGR(SGR_ATTR_RESET);
static const char *const SGR_TITLE     = SGR(SGR_ATTR_BOLD ";" FG_BRIGHT_WHITE ";" BG_BLUE);
static const char *const SGR_LABEL     = SGR(SGR_ATTR_BOLD ";" FG_CYAN);
static const char *const SGR_BG_BOARD  = SGR(BG_GREEN);
static const char *const SGR_BG_LAST   = SGR(BG_BRIGHT_YELLOW);
static const char *const SGR_BLACK     = SGR(SGR_ATTR_BOLD ";" FG_BLACK);
static const char *const SGR_WHITE     = SGR(SGR_ATTR_BOLD ";" FG_BRIGHT_WHITE);
static const char *const SGR_HINT      = SGR(SGR_ATTR_BOLD ";" FG_YELLOW);
static const char *const SGR_DIM       = SGR(SGR_ATTR_DIM);
static const char *const SGR_INVERSE   = SGR(SGR_ATTR_INVERSE);
static const char *const SGR_ERROR     = SGR(SGR_ATTR_BOLD ";" FG_RED);
static const char *const SGR_OK        = SGR(SGR_ATTR_BOLD ";" FG_GREEN);
static const char *const SGR_TAG       = SGR(SGR_ATTR_BOLD ";" FG_YELLOW);

/*
 * ENTER_SCREEN switches to the alternate screen buffer, stops cursor blink,
 * hides the terminal cursor, enables mouse tracking, and clears the screen.
 *
 * LEAVE_SCREEN reverses these settings when exiting the application.
 */
static const char *const ENTER_SCREEN =
    DEC_SET(1049)       /* use alternate screen */
    DEC_RESET(12)       /* stop blinking the terminal cursor */
    DEC_RESET(25)       /* hide the terminal cursor */
    DEC_SET(1003)       /* report all mouse movement */
    DEC_SET(1006)       /* use SGR mouse coordinates */
    ANSI_CLEAR_SCREEN;  /* clear the screen */

static const char *const LEAVE_SCREEN =
    DEC_RESET(1003)     /* stop reporting all mouse movement */
    DEC_RESET(1006)     /* stop SGR mouse reporting */
    DEC_SET(25)         /* show the terminal cursor again */
    DEC_SET(12)         /* restore cursor blinking */
    DEC_RESET(1049)     /* return to the normal screen */
    SGR(SGR_ATTR_RESET);/* reset text formatting and colours */

/* ASCII control and printable character boundaries */
enum {
    ASCII_ETX   = 0x03, /* Ctrl+C (End of Text) */
    ASCII_BS    = '\b', /* 8:   Backspace (legacy terminals / Ctrl+H) */
    ASCII_ESC   = 0x1B, /* 27:  Escape */
    ASCII_SPACE = ' ',  /* 32:  First printable ASCII character */
    ASCII_DEL   = 0x7F  /* 127: Delete (modern terminals' Backspace key) */
};

/* Layout sizing thresholds for word-wrapping and side-by-side panes */
enum {
    DEFAULT_TERM_COLS       = 80,
    DEFAULT_TERM_ROWS       = 24,
    MIN_WRAP_COLS           = 24,
    MIN_COLS_WITH_HISTORY   = 48
};

/*
 * High-level UI modes inferred from the Screen snapshot so that context-aware
 * instructions appear where the user needs them most.
 */
typedef enum {
    UI_MODE_WELCOME,        /* No active game loaded yet */
    UI_MODE_COMMAND_INPUT,  /* User is typing a ':' command */
    UI_MODE_ACTIVE_PLAY,    /* Game in progress; board accepts moves */
    UI_MODE_REPLAY,         /* Read-only replay mode (:v) */
    UI_MODE_POST_GAME       /* Game finished */
} UiMode;

/* Terminal capability flags for legacy consoles and non-TTY streams */
static bool g_term_active = false;
static bool g_atexit_registered = false;
static bool g_use_vt_sequences = true;
static bool g_use_color = true;
static bool g_use_alt_screen = true;
static bool g_use_mouse = true;

/*
 * Return the given ANSI style string when colour output is enabled,
 * or an empty string "" when colour is disabled or unsupported.
 */
static const char *style_str(const char *seq)
{
    return g_use_color ? seq : "";
}

/*
 * Inspect TTY status and environment variables (NO_COLOR, TERM) to enable
 * only the ANSI/DEC features supported by the current terminal.
 */
static void detect_terminal_capabilities(bool is_tty)
{
    const char *no_color = getenv("NO_COLOR");
    const char *term = getenv("TERM");

    g_use_vt_sequences = is_tty;
    g_use_color = is_tty && !(no_color && no_color[0] != '\0');
    g_use_alt_screen = is_tty;
    g_use_mouse = is_tty;

    if (!term || term[0] == '\0') {
        return;
    }

    if (strcmp(term, "dumb") == 0) {
        g_use_vt_sequences = false;
        g_use_color = false;
        g_use_alt_screen = false;
        g_use_mouse = false;
    } else if (strcmp(term, "linux") == 0 || strncmp(term, "vt100", 5) == 0 ||
               strncmp(term, "vt220", 5) == 0) {
        /* Linux virtual console (tty1-tty6) and classic VT terminals lack
         * xterm mouse reporting (1003/1006) and alternate screen (1049). */
        g_use_alt_screen = false;
        g_use_mouse = false;
    }
}

/*
 * Write a string immediately to stdout and flush the stream so control
 * sequences take effect without buffering delay.
 */
void term_write(const char *text)
{
    if (!text || text[0] == '\0') {
        return;
    }
    fputs(text, stdout);
    fflush(stdout);
}

/*
 * Send the subset of ENTER_SCREEN escape sequences supported by the active
 * terminal (full sequence on modern terminals; degraded subset on VT/Linux).
 */
static void enter_screen_compat(void)
{
    if (!g_use_vt_sequences) {
        return;
    }
    if (g_use_alt_screen && g_use_mouse) {
        term_write(ENTER_SCREEN);
        return;
    }
    if (g_use_alt_screen) {
        term_write(DEC_SET(1049));
    }
    term_write(ANSI_CURSOR_HIDE);
    if (g_use_mouse) {
        term_write(DEC_SET(1003) DEC_SET(1006));
    }
    term_write(ANSI_CLEAR_SCREEN);
}

/*
 * Restore terminal modes enabled by enter_screen_compat().
 */
static void leave_screen_compat(void)
{
    if (!g_use_vt_sequences) {
        return;
    }
    if (g_use_alt_screen && g_use_mouse) {
        term_write(LEAVE_SCREEN);
        return;
    }
    if (g_use_mouse) {
        term_write(DEC_RESET(1003) DEC_RESET(1006));
    }
    term_write(ANSI_CURSOR_SHOW);
    if (g_use_alt_screen) {
        term_write(DEC_RESET(1049));
    }
    term_write(SGR_RESET);
}

#ifdef _WIN32

/* Windows console state that must be restored when the program exits. */
static HANDLE g_console_in = INVALID_HANDLE_VALUE;
static HANDLE g_console_out = INVALID_HANDLE_VALUE;
static DWORD g_original_console_input_mode = 0;
static DWORD g_original_console_output_mode = 0;
static CONSOLE_CURSOR_INFO g_original_cursor_info;
static bool g_has_saved_input_mode = false;
static bool g_has_saved_output_mode = false;
static bool g_has_saved_cursor_info = false;
static bool g_left_mouse_button_down = false;

/*
 * Hide the legacy Win32 console cursor when VT sequences are unavailable.
 * Note: When VT sequences are active (Windows Terminal / ConPTY), we rely
 * purely on DEC ?12l + ?25l because mixing Win32 SetConsoleCursorInfo()
 * with VT output causes ConPTY to desynchronize and re-show the caret.
 */
static void win32_hide_legacy_cursor(void)
{
    if (!g_use_vt_sequences && g_console_out != INVALID_HANDLE_VALUE) {
        CONSOLE_CURSOR_INFO hidden = { 1, FALSE };
        SetConsoleCursorInfo(g_console_out, &hidden);
    }
}

/*
 * Query the current visible Windows console dimensions (columns and rows).
 * Falls back to 80x24 if stdout is redirected or unavailable.
 */
static void get_terminal_size(int *cols_out, int *rows_out)
{
    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;

    if (g_console_out != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(g_console_out, &info)) {
            int w = (int)(info.srWindow.Right - info.srWindow.Left + 1);
            int h = (int)(info.srWindow.Bottom - info.srWindow.Top + 1);
            if (w > 0) {
                cols = w;
            }
            if (h > 0) {
                rows = h;
            }
        }
    }

    *cols_out = cols;
    *rows_out = rows;
}

/*
 * Clear the screen and home the cursor via Win32 Console API calls when
 * ENABLE_VIRTUAL_TERMINAL_PROCESSING is unavailable on older Windows hosts.
 */
static void win32_clear_legacy_console(void)
{
    if (g_console_out == INVALID_HANDLE_VALUE) {
        return;
    }
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(g_console_out, &info)) {
        return;
    }
    COORD origin = { 0, 0 };
    DWORD cells = (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y;
    DWORD written = 0;
    FillConsoleOutputCharacterA(g_console_out, ' ', cells, origin, &written);
    SetConsoleCursorPosition(g_console_out, origin);
}

/*
 * Configure the Windows console for raw keyboard/mouse input and VT output,
 * saving original console modes and registering term_restore() via atexit().
 */
bool term_init(void)
{
    if (g_term_active) {
        return true;
    }

    g_console_in = GetStdHandle(STD_INPUT_HANDLE);
    g_console_out = GetStdHandle(STD_OUTPUT_HANDLE);

    if (g_console_in == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(g_console_in, &g_original_console_input_mode)) {
        return false;
    }
    g_has_saved_input_mode = true;

    /*
     * Ask the Windows console to send keyboard, mouse, and resize events
     * through ReadConsoleInput().
     */
    if (!SetConsoleMode(
            g_console_in,
            ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS)) {
        g_has_saved_input_mode = false;
        return false;
    }

    /*
     * Enable ANSI/VT escape-sequence processing for terminal output.
     * Older Windows console hosts (Windows 7/8/early Windows 10 conhost)
     * do not support ENABLE_VIRTUAL_TERMINAL_PROCESSING; fall back cleanly.
     */
    DWORD output_mode = 0;
    bool has_vt = false;

    if (g_console_out != INVALID_HANDLE_VALUE &&
        GetConsoleMode(g_console_out, &output_mode)) {
        g_original_console_output_mode = output_mode;
        g_has_saved_output_mode = true;

        if (SetConsoleMode(
                g_console_out,
                output_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            has_vt = true;
        }

        if (GetConsoleCursorInfo(g_console_out, &g_original_cursor_info)) {
            g_has_saved_cursor_info = true;
        }
    }

    detect_terminal_capabilities(has_vt);
    g_term_active = true;

    if (!g_atexit_registered) {
        atexit(term_restore);
        g_atexit_registered = true;
    }

    if (g_use_vt_sequences) {
        enter_screen_compat();
    } else {
        win32_hide_legacy_cursor();
        win32_clear_legacy_console();
    }

    return true;
}

/*
 * Restore the Windows console screen, cursor visibility, and saved modes.
 */
void term_restore(void)
{
    if (!g_term_active) {
        return;
    }
    g_term_active = false;

    if (g_use_vt_sequences) {
        leave_screen_compat();
    }

    if (g_has_saved_cursor_info && g_console_out != INVALID_HANDLE_VALUE) {
        SetConsoleCursorInfo(g_console_out, &g_original_cursor_info);
        g_has_saved_cursor_info = false;
    }
    if (g_has_saved_output_mode && g_console_out != INVALID_HANDLE_VALUE) {
        SetConsoleMode(g_console_out, g_original_console_output_mode);
        g_has_saved_output_mode = false;
    }
    if (g_has_saved_input_mode && g_console_in != INVALID_HANDLE_VALUE) {
        SetConsoleMode(g_console_in, g_original_console_input_mode);
        g_has_saved_input_mode = false;
    }
}

/*
 * Block until a Windows console input record arrives and translate keyboard,
 * mouse, or window-resize activity into a platform-independent InputEvent.
 */
InputEvent term_read_event(void)
{
    InputEvent event = { EV_NONE, 0, 0, 0 };

    for (;;) {
        INPUT_RECORD record;
        DWORD records_read = 0;

        if (!ReadConsoleInput(g_console_in, &record, 1, &records_read) ||
            records_read == 0) {
            event.kind = EV_QUIT;
            return event;
        }

        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            return event;
        }

        if (record.EventType == KEY_EVENT &&
            record.Event.KeyEvent.bKeyDown) {
            const KEY_EVENT_RECORD *key = &record.Event.KeyEvent;
            const int character = (unsigned char)key->uChar.AsciiChar;

            switch (key->wVirtualKeyCode) {
            case VK_UP:     event.kind = EV_UP;    return event;
            case VK_DOWN:   event.kind = EV_DOWN;  return event;
            case VK_LEFT:   event.kind = EV_LEFT;  return event;
            case VK_RIGHT:  event.kind = EV_RIGHT; return event;
            case VK_ESCAPE: event.kind = EV_ESC;   return event;
            default: break;
            }

            if (character == ASCII_ETX) {
                event.kind = EV_QUIT;
                return event;
            }
            if (character == '\r') {
                event.kind = EV_ENTER;
                return event;
            }
            if (character == ASCII_BS || character == ASCII_DEL) {
                event.kind = EV_BACKSPACE;
                return event;
            }
            if (character >= ASCII_SPACE && character < ASCII_DEL) {
                event.kind = EV_CHAR;
                event.ch = character;
                return event;
            }
        } else if (record.EventType == MOUSE_EVENT) {
            const MOUSE_EVENT_RECORD *mouse = &record.Event.MouseEvent;
            const bool was_down = g_left_mouse_button_down;

            g_left_mouse_button_down =
                (mouse->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;

            event.row = mouse->dwMousePosition.Y;
            event.col = mouse->dwMousePosition.X;

            if (mouse->dwEventFlags & MOUSE_MOVED) {
                event.kind = EV_MOUSE_MOVE;
                return event;
            }

            if (mouse->dwEventFlags == 0 &&
                g_left_mouse_button_down && !was_down) {
                event.kind = EV_MOUSE_PRESS;
                return event;
            }
        }
    }
}

#else

/* POSIX terminal state saved before enabling raw input mode. */
static struct termios g_saved_termios;
static bool g_raw_mode = false;

enum {
    ESC_SEQUENCE_TIMEOUT_MS = 50,
    SGR_MOUSE_BUTTON_MASK   = 3,
    SGR_MOUSE_MOTION_BIT    = 32,
    SGR_MOUSE_WHEEL_BIT     = 64,
    CSI_FINAL_MIN           = 0x40,
    CSI_FINAL_MAX           = 0x7E
};

/*
 * Query the current POSIX terminal dimensions via ioctl(TIOCGWINSZ),
 * falling back to COLUMNS/LINES environment variables or 80x24.
 */
static void get_terminal_size(int *cols_out, int *rows_out)
{
    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) {
            cols = (int)ws.ws_col;
        }
        if (ws.ws_row > 0) {
            rows = (int)ws.ws_row;
        }
    } else {
        const char *env_cols = getenv("COLUMNS");
        const char *env_rows = getenv("LINES");
        if (env_cols && atoi(env_cols) > 0) {
            cols = atoi(env_cols);
        }
        if (env_rows && atoi(env_rows) > 0) {
            rows = atoi(env_rows);
        }
    }

    *cols_out = cols;
    *rows_out = rows;
}

/*
 * Wait up to timeout_ms milliseconds for bytes on STDIN_FILENO.
 * Retries automatically if select() is interrupted by a signal (EINTR).
 */
static bool input_ready(int timeout_ms)
{
    for (;;) {
        fd_set input_fds;
        FD_ZERO(&input_fds);
        FD_SET(STDIN_FILENO, &input_fds);

        struct timeval timeout = {
            timeout_ms / 1000,
            (timeout_ms % 1000) * 1000
        };

        int ready = select(STDIN_FILENO + 1, &input_fds, NULL, NULL, &timeout);
        if (ready > 0) {
            return true;
        }
        if (ready == 0 || errno != EINTR) {
            return false;
        }
    }
}

/*
 * Read a single byte from STDIN_FILENO, retrying if interrupted by EINTR.
 * Returns the byte value (0-255) on success, or -1 on EOF or I/O error.
 */
static int read_byte(void)
{
    for (;;) {
        unsigned char byte;
        ssize_t n = read(STDIN_FILENO, &byte, 1);
        if (n == 1) {
            return (int)byte;
        }
        if (n == 0 || errno != EINTR) {
            return -1;
        }
    }
}

/*
 * Switch STDIN_FILENO to non-canonical, no-echo raw mode when attached to a
 * TTY, detect terminal capabilities on STDOUT_FILENO, and enter screen mode.
 */
bool term_init(void)
{
    if (g_term_active) {
        return true;
    }

    const bool stdin_is_tty = isatty(STDIN_FILENO) != 0;
    const bool stdout_is_tty = isatty(STDOUT_FILENO) != 0;

    if (stdin_is_tty) {
        if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) {
            return false;
        }

        struct termios terminal_settings = g_saved_termios;

        /* Read keys immediately; do not echo typed characters. */
        terminal_settings.c_lflag &=
            (tcflag_t)~(ICANON | ECHO | ISIG);

        /* Do not translate Enter or consume Ctrl+S/Ctrl+Q as flow control. */
        terminal_settings.c_iflag &=
            (tcflag_t)~(IXON | ICRNL);

        terminal_settings.c_cc[VMIN] = 1;
        terminal_settings.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal_settings) != 0) {
            return false;
        }

        g_raw_mode = true;
    }

    detect_terminal_capabilities(stdout_is_tty);
    g_term_active = true;

    if (!g_atexit_registered) {
        atexit(term_restore);
        g_atexit_registered = true;
    }

    enter_screen_compat();
    return true;
}

/*
 * Leave alternate screen / mouse modes and restore saved termios settings.
 */
void term_restore(void)
{
    if (!g_term_active) {
        return;
    }
    g_term_active = false;

    leave_screen_compat();

    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_raw_mode = false;
    }
}

/*
 * Parse an SGR mouse sequence ("<button;x;yM") into a zero-based InputEvent.
 * Ignores button release ('m') and scroll-wheel events.
 */
static InputEvent parse_sgr_mouse(const char *sequence)
{
    InputEvent event = { EV_NONE, 0, 0, 0 };
    int button = 0;
    int x = 0;
    int y = 0;
    char final_character = '\0';

    if (sscanf(sequence, "<%d;%d;%d%c",
               &button, &x, &y, &final_character) != 4) {
        return event;
    }

    event.row = y - 1;
    event.col = x - 1;

    /* Ignore button releases and mouse-wheel events. */
    if (final_character != 'M' || (button & SGR_MOUSE_WHEEL_BIT)) {
        return event;
    }

    if (button & SGR_MOUSE_MOTION_BIT) {
        event.kind = EV_MOUSE_MOVE;
    } else if ((button & SGR_MOUSE_BUTTON_MASK) == 0) {
        event.kind = EV_MOUSE_PRESS;
    }

    return event;
}

/*
 * Map the final byte ('A'..'D') of a CSI or SS3 arrow sequence to an EventKind.
 */
static EventKind arrow_kind(int final_character)
{
    switch (final_character) {
    case 'A': return EV_UP;
    case 'B': return EV_DOWN;
    case 'C': return EV_RIGHT;
    case 'D': return EV_LEFT;
    default:  return EV_NONE;
    }
}

/*
 * Read bytes from stdin and decode keys, escape sequences (arrows, SGR mouse),
 * and control characters into a platform-independent InputEvent.
 */
InputEvent term_read_event(void)
{
    InputEvent event = { EV_NONE, 0, 0, 0 };
    const int first_byte = read_byte();

    if (first_byte < 0 || first_byte == ASCII_ETX) {
        event.kind = EV_QUIT;
        return event;
    }

    if (first_byte == ASCII_ESC) {
        /* ESC may be a key by itself or the start of a longer key sequence. */
        if (!input_ready(ESC_SEQUENCE_TIMEOUT_MS)) {
            event.kind = EV_ESC;
            return event;
        }

        const int second_byte = read_byte();

        /* Some terminals use ESC O A/B/C/D for arrow keys (SS3). */
        if (second_byte == 'O') {
            const int final_character = read_byte();
            event.kind = final_character < 0
                ? EV_QUIT
                : arrow_kind(final_character);
            return event;
        }

        /* Most terminals use ESC [ ... for control sequences (CSI). */
        if (second_byte != '[') {
            return event;
        }

        char sequence[32];
        size_t sequence_length = 0;
        int current_byte;

        do {
            current_byte = read_byte();

            if (current_byte < 0) {
                event.kind = EV_QUIT;
                return event;
            }

            if (sequence_length + 1 < sizeof sequence) {
                sequence[sequence_length++] = (char)current_byte;
            }
        } while (current_byte < CSI_FINAL_MIN || current_byte > CSI_FINAL_MAX);

        sequence[sequence_length] = '\0';

        if (sequence[0] == '<') {
            return parse_sgr_mouse(sequence);
        }

        event.kind = arrow_kind(current_byte);
        return event;
    }

    if (first_byte == '\r' || first_byte == '\n') {
        event.kind = EV_ENTER;
        return event;
    }

    if (first_byte == ASCII_DEL || first_byte == ASCII_BS) {
        event.kind = EV_BACKSPACE;
        return event;
    }

    if (first_byte >= ASCII_SPACE && first_byte < ASCII_DEL) {
        event.kind = EV_CHAR;
        event.ch = first_byte;
        return event;
    }

    return event;
}

#endif /* _WIN32 */

/*
 * ===========================================================================
 * CLI SCREEN SNAPSHOT, STATUS & COORDINATE MAPPING
 * ===========================================================================
 */

void set_status(App *a, bool is_error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
    a->status_error = is_error;
}

Screen app_screen(const App *a, const char *prompt, const char *text)
{
    static const char *const PLAY_HELP =
        "Arrows move  a-h,1-8 jump  Enter/click place  :v view moves  :command  :h help";
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
        return "Commands: :n [p1] [p2] [w] | :l <file> | :v [file] | :s <files> | :h help | :q quit";

    case UI_MODE_COMMAND_INPUT:
        return "Input: Enter execute | Backspace edit | Esc cancel";

    case UI_MODE_ACTIVE_PLAY:
        return "Play: Arrows move | a-h,1-8 jump | Enter/Click place | :v view moves | :t hints | :w <file> save";

    case UI_MODE_REPLAY:
        return "Replay: Left back | Right/Space forward | 0-60 + Enter jump | q/Esc exit";

    case UI_MODE_POST_GAME:
    default:
        return "Session: :n new game | :w <file> save | :l <file> load | :v [file] view | :s <files> stats | :q quit";
    }
}

/*
 * Append a single non-wrapping line clipped safely to `max_cols` so header
 * rows above the board always occupy exactly 1 terminal row (keeping
 * BOARD_TOP == 3 invariant for mouse click alignment).
 */
static void append_single_line(char *frame, size_t frame_size,
                               const char *prefix_style, const char *text,
                               const char *suffix_style, const char *eol,
                               int max_cols, int *rows_left)
{
    if (rows_left && *rows_left <= 0) {
        return;
    }

    int usable = (max_cols > 4) ? (max_cols - 3) : 1;
    append_fmt(frame, frame_size, "  %s%.*s%s%s\n",
               prefix_style,
               usable,
               text ? text : "",
               suffix_style,
               eol);

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
static int append_wrapped_line(char *frame, size_t frame_size,
                               const char *prefix_style, const char *text,
                               const char *suffix_style, const char *eol,
                               int max_cols, int indent_spaces,
                               int *rows_left, bool trailing_newline)
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
            append_fmt(frame, frame_size, "%s%s",
                       eol, (more || trailing_newline) ? "\n" : "");
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
        if (cursor[len] != '\0' && cursor[len] != '\n' &&
            cursor[len] != ' ' && last_space > 0) {
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
        append_fmt(frame, frame_size, "%*s%s%.*s%s%s%s",
                   current_indent, "",
                   prefix_style,
                   len,
                   cursor,
                   suffix_style,
                   eol,
                   (has_more || trailing_newline) ? "\n" : "");

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
static void append_prompt_row(char *frame, size_t frame_size,
                              const Screen *screen, const char *eol,
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

        append_fmt(frame, frame_size, "  %s%s%s%s%s_%s%s\n",
                   style_str(SGR_TAG),
                   prompt,
                   style_str(SGR_RESET),
                   visible_text,
                   style_str(SGR_HINT),
                   style_str(SGR_RESET),
                   eol);
    } else if (screen->tag) {
        append_fmt(frame, frame_size, "  %sReplay mode active (press q or Esc to exit)%s%s\n",
                   style_str(SGR_DIM),
                   style_str(SGR_RESET),
                   eol);
    } else {
        append_fmt(frame, frame_size, "  %sPress ':' to enter a command%s%s\n",
                   style_str(SGR_DIM),
                   style_str(SGR_RESET),
                   eol);
    }

    if (rows_left) {
        (*rows_left)--;
    }
}

/*
 * Format a single board square (piece, legal-move hint, last-move highlight,
 * and cursor indicator) and append it to the in-memory frame buffer.
 */
static void append_cell(char *frame, size_t frame_size, const GameState *game,
                        const Screen *screen,
                        bool legal[BOARD_SIZE][BOARD_SIZE], int row, int col)
{
    const CellState cell = game->board[row][col];

    const bool cursor_is_here =
        screen->show_cursor &&
        screen->cursor.row == row &&
        screen->cursor.col == col;

    const bool is_last_move =
        game->move_count > 0 &&
        game->history[game->move_count - 1].pos.row == row &&
        game->history[game->move_count - 1].pos.col == col;

    const char *background =
        style_str(is_last_move ? SGR_BG_LAST : SGR_BG_BOARD);

    const char *foreground = style_str(SGR_DIM);
    char symbol = '.';

    if (cell == CELL_BLACK) {
        foreground = style_str(SGR_BLACK);
        symbol = 'X';
    } else if (cell == CELL_WHITE) {
        foreground = style_str(SGR_WHITE);
        symbol = 'O';
    } else if (legal[row][col]) {
        foreground = style_str(SGR_HINT);
        symbol = '*';
    }

    if (!g_use_color && cursor_is_here) {
        append_fmt(frame, frame_size, "[%c]", symbol);
    } else {
        append_fmt(frame, frame_size, "%s%s%s %c %s",
                   background,
                   cursor_is_here ? style_str(SGR_INVERSE) : "",
                   foreground,
                   symbol,
                   style_str(SGR_RESET));
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
        snprintf(output, output_size, "%s (Black) wins %d-%d",
                 player_name(game, PLAYER_BLACK),
                 black_pieces, white_pieces);
        break;

    case RESULT_WHITE_WIN:
        snprintf(output, output_size, "%s (White) wins %d-%d",
                 player_name(game, PLAYER_WHITE),
                 white_pieces, black_pieces);
        break;

    default:
        snprintf(output, output_size, "tie %d-%d",
                 black_pieces, white_pieces);
        break;
    }
}

/*
 * Write a complete full-screen buffer using active VT/legacy terminal rules
 * and park the cursor at (1,1) so no static caret sits after the last char.
 */
static void flush_terminal_frame(const char *frame)
{
    if (g_use_vt_sequences) {
        term_write(ANSI_CURSOR_HOME ANSI_CURSOR_HIDE);
        fputs(frame, stdout);
        term_write(ANSI_CLEAR_BELOW ANSI_CURSOR_HOME ANSI_CURSOR_HIDE);
    } else {
#ifdef _WIN32
        win32_hide_legacy_cursor();
        if (g_console_out != INVALID_HANDLE_VALUE) {
            COORD origin = { 0, 0 };
            SetConsoleCursorPosition(g_console_out, origin);
        }
#endif
        fputs(frame, stdout);
        fflush(stdout);
    }
}

/*
 * Render the Welcome & Mode Command Reference screen when no game is active
 * (screen->state == NULL), grouping the status and command prompt right with
 * the reference content so all text sits cohesively in one place.
 */
static void render_welcome_screen(char *frame, size_t frame_size,
                                  const Screen *screen, const char *eol,
                                  int cols, int rows)
{
    int total_rows = (rows > 6) ? rows : 6;
    const bool spacious = (total_rows >= 25 && cols >= 72);

    /* Reserve 4 rows for Status + Prompt + Hint so the interactive command
     * line never overflows short terminals. */
    int body_rows = total_rows - 4;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    /* Row 0: Full-width Title Bar */
    append_fmt(frame, frame_size, "%s%-*s%s%s\n",
               style_str(SGR_TITLE),
               title_width,
               "  OTHELLO",
               style_str(SGR_RESET),
               eol);
    body_rows--;

    append_wrapped_line(frame, frame_size,
                        style_str(SGR_LABEL),
                        "Welcome to Othello!",
                        style_str(SGR_RESET), eol, cols, 0, &body_rows, true);

    if (spacious && body_rows > 1) {
        append_fmt(frame, frame_size, "%s\n", eol);
        body_rows--;
    }

    /* Mode 1: Session & File Commands */
    append_wrapped_line(frame, frame_size,
                        style_str(SGR_TAG),
                        "[1] SESSION COMMANDS (Any Mode)",
                        style_str(SGR_RESET), eol, cols, 0, &body_rows, true);
    append_wrapped_line(frame, frame_size, "",
                        "  :n [p1] [p2] [w]  New game ('w' = P1 White)",
                        "", eol, cols, 4, &body_rows, true);
    append_wrapped_line(frame, frame_size, "",
                        "  :l <file>         Load saved game",
                        "", eol, cols, 4, &body_rows, true);
    append_wrapped_line(frame, frame_size, "",
                        "  :v [file]         View current or saved game moves",
                        "", eol, cols, 4, &body_rows, true);
    append_wrapped_line(frame, frame_size, "",
                        "  :s <f1> [f2...]   Win/loss stats across files",
                        "", eol, cols, 4, &body_rows, true);
    append_wrapped_line(frame, frame_size, "",
                        "  :h                Show full command help",
                        "", eol, cols, 4, &body_rows, true);
    append_wrapped_line(frame, frame_size, "",
                        "  :q                Quit Othello",
                        "", eol, cols, 4, &body_rows, true);

    if (spacious) {
        if (body_rows > 1) {
            append_fmt(frame, frame_size, "%s\n", eol);
            body_rows--;
        }
        append_wrapped_line(frame, frame_size,
                            style_str(SGR_TAG),
                            "[2] BOARD PLAY MODE (Active Game)",
                            style_str(SGR_RESET), eol, cols, 0, &body_rows, true);
        append_wrapped_line(frame, frame_size, "",
                            "  Arrow keys        Move cursor on 8x8 board",
                            "", eol, cols, 4, &body_rows, true);
        append_wrapped_line(frame, frame_size, "",
                            "  a-h, 1-8          Jump to column or row",
                            "", eol, cols, 4, &body_rows, true);
        append_wrapped_line(frame, frame_size, "",
                            "  Enter / Click     Place piece on square",
                            "", eol, cols, 4, &body_rows, true);
        append_wrapped_line(frame, frame_size, "",
                            "  :t                Toggle legal-move hints (*)",
                            "", eol, cols, 4, &body_rows, true);
        append_wrapped_line(frame, frame_size, "",
                            "  :w <file>         Save current game",
                            "", eol, cols, 4, &body_rows, true);

        if (body_rows > 1) {
            append_fmt(frame, frame_size, "%s\n", eol);
            body_rows--;
        }
        append_wrapped_line(frame, frame_size,
                            style_str(SGR_TAG),
                            "[3] COMMAND INPUT (After ':')",
                            style_str(SGR_RESET), eol, cols, 0, &body_rows, true);
        append_wrapped_line(frame, frame_size, "",
                            "  Enter execute  |  Backspace edit  |  Esc cancel",
                            "", eol, cols, 4, &body_rows, true);
    } else {
        append_wrapped_line(frame, frame_size,
                            style_str(SGR_TAG),
                            "[2] PLAY: Arrows, a-h/1-8, Enter/Click, :v view, :t hints, :w <file> save",
                            style_str(SGR_RESET), eol, cols, 2, &body_rows, true);
    }

    if (body_rows > 0) {
        append_fmt(frame, frame_size, "%s\n", eol);
        body_rows--;
    }

    /* Interactive Status + Command Line + Shortcut Bar grouped directly with text */
    int footer_rows = 4;

    if (screen->status && screen->status[0] != '\0') {
        append_wrapped_line(frame, frame_size,
                            style_str(screen->status_error ? SGR_ERROR : SGR_OK),
                            screen->status,
                            style_str(SGR_RESET), eol, cols, 0, &footer_rows, true);
    } else {
        append_wrapped_line(frame, frame_size,
                            style_str(SGR_OK),
                            "Type :n for a new game, :h for help, :q to quit.",
                            style_str(SGR_RESET), eol, cols, 0, &footer_rows, true);
    }

    append_prompt_row(frame, frame_size, screen, eol, cols, &footer_rows);

    append_wrapped_line(frame, frame_size,
                        style_str(SGR_DIM),
                        mode_contextual_hint(screen->input_prompt ? UI_MODE_COMMAND_INPUT : UI_MODE_WELCOME),
                        style_str(SGR_RESET), eol, cols, 0, &footer_rows, false);
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
    get_terminal_size(&cols, &rows);

    const char *eol = g_use_vt_sequences ? ANSI_CLEAR_EOL : "";
    const GameState *game = screen->state;
    const UiMode mode = detect_ui_mode(screen);
    char message[160];
    bool legal_moves[BOARD_SIZE][BOARD_SIZE];

    frame[0] = '\0';

    /* Show the dedicated welcome screen whenever no game is loaded */
    if (!game) {
        render_welcome_screen(frame, sizeof frame, screen, eol, cols, rows);
        flush_terminal_frame(frame);
        return;
    }

    int rows_left = (rows > 14) ? rows : 14;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    /* Row 0: Full-width Title Bar */
    snprintf(message, sizeof message, "  OTHELLO    %s (X)  vs  %s (O)",
             player_name(game, PLAYER_BLACK),
             player_name(game, PLAYER_WHITE));
    append_fmt(frame, sizeof frame, "%s%-*s%s%s\n",
               style_str(SGR_TITLE), title_width, message, style_str(SGR_RESET), eol);
    rows_left--;

    /* Row 1: Single-line Turn / Replay / Game-Over Banner (keeps BOARD_TOP == 3) */
    if (screen->tag) {
        snprintf(message, sizeof message, "%s  (Read-Only Replay)", screen->tag);
        append_single_line(frame, sizeof frame,
                           style_str(SGR_TAG), message,
                           style_str(SGR_RESET), eol, cols, &rows_left);
    } else if (is_game_over(game)) {
        char result_text[96];
        describe_result(game, result_text, sizeof result_text);
        snprintf(message, sizeof message, "GAME OVER: %s  (:w <file> save | :n new | :q quit)",
                 result_text);
        append_single_line(frame, sizeof frame,
                           style_str(SGR_TAG), message,
                           style_str(SGR_RESET), eol, cols, &rows_left);
    } else {
        snprintf(message, sizeof message, "Turn: %s (%s)   [%s]",
                 player_name(game, game->current_turn),
                 color_name(game->current_turn),
                 screen->hints ? "Hints ON (*)" : "Hints OFF (:t)");
        append_single_line(frame, sizeof frame,
                           "", message, "", eol, cols, &rows_left);
    }

    /* Row 2: Column Headers (fixed BOARD_LEFT = 3 for mouse alignment) */
    const bool show_history_pane = (cols >= MIN_COLS_WITH_HISTORY);
    append_fmt(frame, sizeof frame, "   ");

    for (int col = 0; col < BOARD_SIZE; col++) {
        append_fmt(frame, sizeof frame, "%s %c %s",
                   style_str(SGR_LABEL), 'A' + col, style_str(SGR_RESET));
    }

    if (show_history_pane) {
        append_fmt(frame, sizeof frame, "   %sMoves%s%s\n",
                   style_str(SGR_LABEL), style_str(SGR_RESET), eol);
    } else {
        append_fmt(frame, sizeof frame, "%s\n", eol);
    }
    rows_left--;

    memset(legal_moves, 0, sizeof legal_moves);

    if (screen->hints && !is_game_over(game)) {
        get_all_valid_moves(game, game->current_turn, legal_moves);
    }

    const GameState *history_game =
        (screen->tag && g_replay_full_game) ? g_replay_full_game : game;
    const int total_history_moves = history_game->move_count;
    const int active_history_idx = game->move_count - 1;

    int first_history_row = 0;
    if (total_history_moves > HISTORY_ROWS) {
        if (screen->tag && g_replay_full_game) {
            const int anchor = (active_history_idx >= 0) ? active_history_idx : 0;
            first_history_row = clamp_int(anchor - HISTORY_ROWS / 2,
                                          0, total_history_moves - HISTORY_ROWS);
        } else {
            first_history_row = total_history_moves - HISTORY_ROWS;
        }
    }

    /* Rows 3..10: 8x8 Board Grid + Optional Right-Hand Move History */
    for (int row = 0; row < BOARD_SIZE; row++) {
        append_fmt(frame, sizeof frame, "%s %d %s",
                   style_str(SGR_LABEL), row + 1, style_str(SGR_RESET));

        for (int col = 0; col < BOARD_SIZE; col++) {
            append_cell(frame, sizeof frame, game, screen,
                        legal_moves, row, col);
        }

        if (show_history_pane) {
            append_fmt(frame, sizeof frame, "   ");

            const int history_index = first_history_row + row;

            if (history_index < total_history_moves) {
                char square[3];
                const bool is_current_move = (history_index == active_history_idx);
                const bool is_future_step = (history_index > active_history_idx);
                const char *entry_style = is_current_move ? style_str(SGR_TAG)
                                        : (is_future_step ? style_str(SGR_DIM) : "");

                position_to_string(history_game->history[history_index].pos, square);

                append_fmt(frame, sizeof frame, "%s%2d. %-5s %s%s",
                           entry_style,
                           history_index + 1,
                           color_name(history_game->history[history_index].player),
                           square,
                           entry_style[0] != '\0' ? style_str(SGR_RESET) : "");
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

        snprintf(message, sizeof message, "Black X: %d   White O: %d   Moves: %d/%d",
                 black_pieces, white_pieces, game->move_count, MAX_MOVES);
        append_wrapped_line(frame, sizeof frame, "", message, "", eol, cols, 0, &body_budget, true);
    }

    int footer_rows = 3 + body_budget;

    /* Status / Feedback Line */
    if (screen->status && screen->status[0] != '\0') {
        append_wrapped_line(frame, sizeof frame,
                            style_str(screen->status_error ? SGR_ERROR : SGR_OK),
                            screen->status,
                            style_str(SGR_RESET), eol, cols, 0, &footer_rows, true);
    } else if (footer_rows > 2) {
        append_fmt(frame, sizeof frame, "%s\n", eol);
        footer_rows--;
    }

    /* Fixed-height Command Prompt Row */
    append_prompt_row(frame, sizeof frame, screen, eol, cols, &footer_rows);

    /* Mode-Contextual Hint Row (no trailing '\n' so bottom edge never scrolls) */
    append_wrapped_line(frame, sizeof frame,
                        style_str(SGR_DIM),
                        mode_contextual_hint(mode),
                        style_str(SGR_RESET), eol, cols, 0, &footer_rows, false);

    flush_terminal_frame(frame);
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

/* Splits on whitespace. Double quotes group text and are removed. Returns the token count, or -1 on error. */
int tokenize(const char *s, char tok[][MAX_PATH_LEN], char *err, size_t err_size)
{
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
            if (tolower((unsigned char)v[0]) != 'p' || (v[1] != '1' && v[1] != '2') || v[2] != '\0') {
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
        if (t == CMD_LOAD)      set_error(out, "usage: :l <file>");
        else                    set_error(out, "usage: :w <file>");
        return false;
    }
    if (t == CMD_STATS && out->file_count == 0) {
        set_error(out, "usage: :s <file1> [file2...]");
        return false;
    }
    out->type = t;
    return true;
}

/* Reads a line in the command bar. Enter accepts, Esc cancels (false), Ctrl-C/EOF quits. */
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
        for (int i = 0; i < cmd.file_count; i++) paths[i] = cmd.files[i];
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
    get_terminal_size(&cols, &rows);

    const char *eol = g_use_vt_sequences ? ANSI_CLEAR_EOL : "";
    int rows_left = (rows > 6) ? rows : 6;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    buf[0] = '\0';

    append_fmt(buf, sizeof buf, "%s%-*s%s%s\n",
               style_str(SGR_TITLE),
               title_width,
               "  OTHELLO - COMMAND & CONTROLS HELP",
               style_str(SGR_RESET),
               eol);
    rows_left--;

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    append_wrapped_line(buf, sizeof buf,
                        style_str(SGR_TAG),
                        "[1] SESSION COMMANDS (Any Mode)",
                        style_str(SGR_RESET), eol, cols, 0, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :n [p1] [p2] [w]   Start new game ('w' = P1 plays White; quotes allow spaces)",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :l <file>          Load a saved game (e.g. :l game.sav)",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :v [file]          View current game moves (:v) or a saved game (:v game.sav)",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :s <f1> [f2...]    Show win/loss statistics across finished save files",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :t                 Toggle legal-move hints (*) on or off",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :h                 Show this command reference",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :q                 Quit Othello",
                        "", eol, cols, 4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    append_wrapped_line(buf, sizeof buf,
                        style_str(SGR_TAG),
                        "[2] BOARD PLAY CONTROLS (Active Game)",
                        style_str(SGR_RESET), eol, cols, 0, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  Arrow keys         Move cursor (Up / Down / Left / Right)",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  a-h, 1-8           Jump directly to column (a-h) or row (1-8)",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  Enter / Space      Place a piece on the selected square",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  Mouse              Hover to move cursor; left-click to place piece",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :w <file>          Save current game (e.g. :w game.sav)",
                        "", eol, cols, 4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    append_wrapped_line(buf, sizeof buf,
                        style_str(SGR_TAG),
                        "[3] COMMAND LINE & REPLAY CONTROLS",
                        style_str(SGR_RESET), eol, cols, 0, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  :                  Open command input (Enter execute | Backspace edit | Esc cancel)",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  Left arrow         Step backward one move in replay",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  Right / Space      Step forward one move in replay",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  0-60 + Enter       Jump directly to move number in replay",
                        "", eol, cols, 4, &rows_left, true);
    append_wrapped_line(buf, sizeof buf, "",
                        "  q / Esc            Exit replay mode",
                        "", eol, cols, 4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    append_wrapped_line(buf, sizeof buf,
                        style_str(SGR_OK),
                        "Press any key to return...",
                        style_str(SGR_RESET), eol, cols, 0, &rows_left, false);

    flush_terminal_frame(buf);
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
    get_terminal_size(&cols, &rows);

    const char *eol = g_use_vt_sequences ? ANSI_CLEAR_EOL : "";
    int rows_left = (rows > 6) ? rows : 6;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;

    buf[0] = '\0';

    append_fmt(buf, sizeof buf, "%s%-*s%s%s\n",
               style_str(SGR_TITLE),
               title_width,
               "  OTHELLO - MATCH STATISTICS",
               style_str(SGR_RESET),
               eol);
    rows_left--;

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    append_wrapped_line(buf, sizeof buf,
                        style_str(SGR_TAG),
                        "[GAME STATISTICS SUMMARY]",
                        style_str(SGR_RESET), eol, cols, 0, &rows_left, true);

    snprintf(line, sizeof line, "  Finished games counted : %d", sum.total_games);
    append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line, "  Files skipped          : %d (unreadable, corrupt, or unfinished)", sum.skipped_files);
    append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line, "  Black wins             : %d", sum.black_wins);
    append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line, "  White wins             : %d", sum.white_wins);
    append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    snprintf(line, sizeof line, "  Ties                   : %d", sum.ties);
    append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    if (sum.total_games > 0) {
        snprintf(line, sizeof line, "  Highest score          : %d (%s)",
                 sum.max_score, sum.highest_scoring_player);
    } else {
        snprintf(line, sizeof line, "  Highest score          : -");
    }
    append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 4, &rows_left, true);

    if (rows_left > 1) {
        append_fmt(buf, sizeof buf, "%s\n", eol);
        rows_left--;
    }

    append_wrapped_line(buf, sizeof buf,
                        style_str(SGR_OK),
                        "Press any key to return...",
                        style_str(SGR_RESET), eol, cols, 0, &rows_left, false);

    flush_terminal_frame(buf);
    wait_key(a);
    set_status(a, false, "Statistics: %d game(s) counted, %d skipped.", sum.total_games, sum.skipped_files);
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
            snprintf(status_line, sizeof status_line, "Jump to move: %d (press Enter to jump, Esc to clear)", number);
        } else {
            snprintf(status_line, sizeof status_line, "Viewing move %d of %d.", step, game->move_count);
        }
        Screen s = {
            .state = &cur, .cursor = { 0, 0 }, .show_cursor = false, .hints = false,
            .tag = tag, .status = status_line, .status_error = false,
            .input_prompt = NULL, .input_text = NULL,
            .help = "Left back | Right/Space forward | digits + Enter jump | q/Esc exit"
        };
        render(&s);

        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) {
            a->quit = true;
            break;
        }
        if (ev.kind == EV_ESC) {
            if (number >= 0) number = -1;
            else break;
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
        const bool fwd  = (ev.kind == EV_RIGHT) || (ev.kind == EV_CHAR && ev.ch == ' ');
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
