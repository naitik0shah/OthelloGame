#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "cli_internal.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

/*
 * ENTER_SCREEN switches to the alternate screen buffer, stops cursor blink,
 * hides the terminal cursor, enables mouse tracking, and clears the screen.
 *
 * LEAVE_SCREEN reverses these settings when exiting the application.
 */
static const char *const ENTER_SCREEN = DEC_SET(1049) /* use alternate screen */
    DEC_RESET(12)                                     /* stop blinking the terminal cursor */
    DEC_RESET(25)                                     /* hide the terminal cursor */
    DEC_SET(1003)                                     /* report all mouse movement */
    DEC_SET(1006)                                     /* use SGR mouse coordinates */
    ANSI_CLEAR_SCREEN;                                /* clear the screen */

static const char *const LEAVE_SCREEN = DEC_RESET(1003) /* stop reporting all mouse movement */
    DEC_RESET(1006)                                     /* stop SGR mouse reporting */
    DEC_SET(25)                                         /* show the terminal cursor again */
    DEC_SET(12)                                         /* restore cursor blinking */
    DEC_RESET(1049)                                     /* return to the normal screen */
    SGR_RESET;                                          /* reset text formatting and colours */

/* ASCII control and printable character boundaries */
enum {
    ASCII_ETX = 0x03,  /* Ctrl+C (End of Text) */
    ASCII_BS = '\b',   /* 8:   Backspace (legacy terminals / Ctrl+H) */
    ASCII_ESC = 0x1B,  /* 27:  Escape */
    ASCII_SPACE = ' ', /* 32:  First printable ASCII character */
    ASCII_DEL = 0x7F   /* 127: Delete (modern terminals' Backspace key) */
};

/* Terminal capability flags for legacy consoles and non-TTY streams */
static bool g_term_active = false;
static bool g_atexit_registered = false;
static bool g_use_vt_sequences = true;
static bool g_use_color = true;
static bool g_use_alt_screen = true;
static bool g_use_mouse = true;

bool cli_use_vt_sequences(void)
{
    return g_use_vt_sequences;
}

bool cli_use_color(void)
{
    return g_use_color;
}

/*
 * Return the given ANSI style string when colour output is enabled,
 * or an empty string "" when colour is disabled or unsupported.
 */
const char *cli_style_str(const char *seq)
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
        CONSOLE_CURSOR_INFO hidden = {1, FALSE};
        SetConsoleCursorInfo(g_console_out, &hidden);
    }
}

void cli_win32_prepare_legacy_frame(void)
{
    win32_hide_legacy_cursor();
    if (g_console_out != INVALID_HANDLE_VALUE) {
        COORD origin = {0, 0};
        SetConsoleCursorPosition(g_console_out, origin);
    }
}

/*
 * Query the current visible Windows console dimensions (columns and rows).
 * Falls back to 80x24 if stdout is redirected or unavailable.
 */
void cli_get_terminal_size(int *cols_out, int *rows_out)
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
    COORD origin = {0, 0};
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
    if (!SetConsoleMode(g_console_in,
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

    if (g_console_out != INVALID_HANDLE_VALUE && GetConsoleMode(g_console_out, &output_mode)) {
        g_original_console_output_mode = output_mode;
        g_has_saved_output_mode = true;

        if (SetConsoleMode(g_console_out, output_mode | ENABLE_PROCESSED_OUTPUT |
                                              ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
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
    InputEvent event = {EV_NONE, 0, 0, 0};

    for (;;) {
        INPUT_RECORD record;
        DWORD records_read = 0;

        if (!ReadConsoleInput(g_console_in, &record, 1, &records_read) || records_read == 0) {
            event.kind = EV_QUIT;
            return event;
        }

        if (record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            return event;
        }

        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
            const KEY_EVENT_RECORD *key = &record.Event.KeyEvent;
            const int character = (unsigned char)key->uChar.AsciiChar;

            switch (key->wVirtualKeyCode) {
            case VK_UP:
                event.kind = EV_UP;
                return event;
            case VK_DOWN:
                event.kind = EV_DOWN;
                return event;
            case VK_LEFT:
                event.kind = EV_LEFT;
                return event;
            case VK_RIGHT:
                event.kind = EV_RIGHT;
                return event;
            case VK_ESCAPE:
                event.kind = EV_ESC;
                return event;
            default:
                break;
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

            g_left_mouse_button_down = (mouse->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;

            event.row = mouse->dwMousePosition.Y;
            event.col = mouse->dwMousePosition.X;

            if (mouse->dwEventFlags & MOUSE_MOVED) {
                event.kind = EV_MOUSE_MOVE;
                return event;
            }

            if (mouse->dwEventFlags == 0 && g_left_mouse_button_down && !was_down) {
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
    SGR_MOUSE_BUTTON_MASK = 3,
    SGR_MOUSE_MOTION_BIT = 32,
    SGR_MOUSE_WHEEL_BIT = 64,
    CSI_FINAL_MIN = 0x40,
    CSI_FINAL_MAX = 0x7E
};

void cli_win32_prepare_legacy_frame(void)
{
}

/*
 * Query the current POSIX terminal dimensions via ioctl(TIOCGWINSZ),
 * falling back to COLUMNS/LINES environment variables or 80x24.
 */
void cli_get_terminal_size(int *cols_out, int *rows_out)
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

        struct timeval timeout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};

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
        terminal_settings.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG);

        /* Do not translate Enter or consume Ctrl+S/Ctrl+Q as flow control. */
        terminal_settings.c_iflag &= (tcflag_t) ~(IXON | ICRNL);

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
    InputEvent event = {EV_NONE, 0, 0, 0};
    int button = 0;
    int x = 0;
    int y = 0;
    char final_character = '\0';

    if (sscanf(sequence, "<%d;%d;%d%c", &button, &x, &y, &final_character) != 4) {
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
    case 'A':
        return EV_UP;
    case 'B':
        return EV_DOWN;
    case 'C':
        return EV_RIGHT;
    case 'D':
        return EV_LEFT;
    default:
        return EV_NONE;
    }
}

/*
 * Read bytes from stdin and decode keys, escape sequences (arrows, SGR mouse),
 * and control characters into a platform-independent InputEvent.
 */
InputEvent term_read_event(void)
{
    InputEvent event = {EV_NONE, 0, 0, 0};
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
            event.kind = final_character < 0 ? EV_QUIT : arrow_kind(final_character);
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
