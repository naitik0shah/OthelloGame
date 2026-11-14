#include "common.h"

/*
 * ANSI terminal control sequences.
 *
 * These strings are sent to the terminal, not displayed as ordinary text.
 * Every sequence begins with ESC (the ASCII Escape character, hexadecimal 1B).
 */
static const char *const SGR_RESET     = "\x1b[0m";
static const char *const SGR_CLEAR_EOL = "\x1b[K";
static const char *const SGR_TITLE     = "\x1b[1;97;44m"; /* white on blue */
static const char *const SGR_LABEL     = "\x1b[1;36m";    /* cyan */
static const char *const SGR_BG_BOARD  = "\x1b[42m";      /* green board */
static const char *const SGR_BG_LAST   = "\x1b[103m";     /* bright yellow */
static const char *const SGR_BLACK     = "\x1b[1;30m";
static const char *const SGR_WHITE     = "\x1b[1;97m";
static const char *const SGR_HINT      = "\x1b[1;33m";
static const char *const SGR_DIM       = "\x1b[2m";
static const char *const SGR_INVERSE   = "\x1b[7m";       /* cursor highlight */
static const char *const SGR_ERROR     = "\x1b[1;31m";
static const char *const SGR_OK        = "\x1b[1;32m";
static const char *const SGR_TAG       = "\x1b[1;33m";

/*
 * ENTER_SCREEN switches to the terminal's alternate screen, hides the
 * cursor, enables mouse reporting, and clears the alternate screen.
 *
 * Adjacent C string literals are automatically concatenated by the compiler.
 * There is deliberately no comma between these pieces: together they form
 * one string containing several terminal commands.
 *
 * LEAVE_SCREEN reverses the settings changed above.
 */
static const char *const ENTER_SCREEN =
    "\x1b[?1049h" /* use alternate screen */
    "\x1b[?25l"   /* hide the terminal cursor */
    "\x1b[?1003h" /* report all mouse movement */
    "\x1b[?1006h" /* use SGR mouse coordinates */
    "\x1b[2J";    /* clear the screen */

static const char *const LEAVE_SCREEN =
    "\x1b[?1003l" /* stop reporting all mouse movement */
    "\x1b[?1006l" /* stop SGR mouse reporting */
    "\x1b[?25h"   /* show the terminal cursor again */
    "\x1b[?1049l" /* return to the normal screen */
    "\x1b[0m";    /* reset text formatting and colours */

void term_write(const char *text)
{
    fputs(text, stdout);
    fflush(stdout);
}

#ifdef _WIN32

/* Windows console state that must be restored when the program exits. */
static HANDLE g_console_in;
static DWORD g_original_console_input_mode;
static bool g_left_mouse_button_down;

bool term_init(void)
{
    g_console_in = GetStdHandle(STD_INPUT_HANDLE);

    if (!GetConsoleMode(g_console_in, &g_original_console_input_mode)) {
        return false;
    }

    /*
     * Ask the Windows console to send keyboard, mouse, and resize events
     * through ReadConsoleInput().
     */
    if (!SetConsoleMode(
            g_console_in,
            ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS)) {
        return false;
    }

    /*
     * Enable ANSI/VT escape-sequence processing for terminal output.
     * Some older Windows console hosts do not support this mode.
     */
    HANDLE console_output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD output_mode = 0;

    if (GetConsoleMode(console_output, &output_mode)) {
        SetConsoleMode(
            console_output,
            output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    term_write(ENTER_SCREEN);
    return true;
}

void term_restore(void)
{
    term_write(LEAVE_SCREEN);

    if (g_console_in) {
        SetConsoleMode(g_console_in, g_original_console_input_mode);
    }
}

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

            if (character == '\x03') {
                event.kind = EV_QUIT;
                return event;
            }
            if (character == '\r') {
                event.kind = EV_ENTER;
                return event;
            }
            if (character == '\b') {
                event.kind = EV_BACKSPACE;
                return event;
            }
            if (character >= ' ' && character < '\x7f') {
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

/*
 * Wait until standard input has data available, or until the timeout expires.
 * select() uses a timeout measured in seconds and microseconds.
 */
bool input_ready(int timeout_ms)
{
    fd_set input_fds;
    FD_ZERO(&input_fds);
    FD_SET(STDIN_FILENO, &input_fds);

    struct timeval timeout = {
        timeout_ms / 1000,
        (timeout_ms % 1000) * 1000
    };

    return select(STDIN_FILENO + 1, &input_fds, NULL, NULL, &timeout) > 0;
}

/* Read exactly one byte. Return -1 if the read fails or reaches end-of-file. */
int read_byte(void)
{
    unsigned char byte;
    return read(STDIN_FILENO, &byte, 1) == 1 ? (int)byte : -1;
}

bool term_init(void)
{
    /*
     * Raw input mode is useful only when standard input is an interactive
     * terminal. If input is redirected, leave its settings untouched.
     */
    if (isatty(STDIN_FILENO)) {
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

    term_write(ENTER_SCREEN);
    return true;
}

void term_restore(void)
{
    term_write(LEAVE_SCREEN);

    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_raw_mode = false;
    }
}

/*
 * Parse an SGR mouse sequence.
 *
 * The input has the form: <button;x;yM
 * x and y are one-based terminal coordinates, so convert them to zero-based
 * coordinates before returning them to the rest of the application.
 */
InputEvent parse_sgr_mouse(const char *sequence)
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
    if (final_character != 'M' || (button & 64)) {
        return event;
    }

    if (button & 32) {
        event.kind = EV_MOUSE_MOVE;
    } else if ((button & 3) == 0) {
        event.kind = EV_MOUSE_PRESS;
    }

    return event;
}

/* Convert the final character of a terminal arrow-key sequence to an event. */
EventKind arrow_kind(int final_character)
{
    switch (final_character) {
    case 'A': return EV_UP;
    case 'B': return EV_DOWN;
    case 'C': return EV_RIGHT;
    case 'D': return EV_LEFT;
    default:  return EV_NONE;
    }
}

InputEvent term_read_event(void)
{
    InputEvent event = { EV_NONE, 0, 0, 0 };
    const int first_byte = read_byte();

    if (first_byte < 0 || first_byte == '\x03') {
        event.kind = EV_QUIT;
        return event;
    }

    if (first_byte == '\x1b') {
        /* ESC may be a key by itself or the start of a longer key sequence. */
        if (!input_ready(50)) {
            event.kind = EV_ESC;
            return event;
        }

        const int second_byte = read_byte();

        /* Some terminals use ESC O A/B/C/D for arrow keys. */
        if (second_byte == 'O') {
            const int final_character = read_byte();
            event.kind = final_character < 0
                ? EV_QUIT
                : arrow_kind(final_character);
            return event;
        }

        /* Most terminals use ESC [ ... for control sequences. */
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
        } while (current_byte < 0x40 || current_byte > 0x7e);

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

    if (first_byte == '\x7f' || first_byte == '\b') {
        event.kind = EV_BACKSPACE;
        return event;
    }

    if (first_byte >= ' ' && first_byte < '\x7f') {
        event.kind = EV_CHAR;
        event.ch = first_byte;
        return event;
    }

    return event;
}

#endif /* _WIN32 */

/*
 * Add the visual representation of one board square to the frame buffer.
 * The frame buffer is assembled in memory and written to the terminal once
 * per render, rather than printing every square separately.
 */
void append_cell(char *frame, size_t frame_size, const GameState *game,
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
        is_last_move ? SGR_BG_LAST : SGR_BG_BOARD;

    const char *foreground = SGR_DIM;
    char symbol = '.';

    if (cell == CELL_BLACK) {
        foreground = SGR_BLACK;
        symbol = 'X';
    } else if (cell == CELL_WHITE) {
        foreground = SGR_WHITE;
        symbol = 'O';
    } else if (legal[row][col]) {
        foreground = SGR_HINT;
        symbol = '*';
    }

    append_fmt(frame, frame_size, "%s%s%s %c %s",
               background,
               cursor_is_here ? SGR_INVERSE : "",
               foreground,
               symbol,
               SGR_RESET);
}

/* Create the text shown when the game has finished. */
void describe_result(const GameState *game, char *output, size_t output_size)
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
 * Draw the complete user interface.
 *
 * The layout is designed around an 80-column terminal. The board is on the
 * left and the move history is on the right.
 */
void render(const Screen *screen)
{
    static char frame[FRAME_SIZE];

    const GameState *game = screen->state;
    char message[160];
    bool legal_moves[BOARD_SIZE][BOARD_SIZE];

    frame[0] = '\0';

    if (game) {
        snprintf(message, sizeof message, " OTHELLO    %s (X)  vs  %s (O)",
                 player_name(game, PLAYER_BLACK),
                 player_name(game, PLAYER_WHITE));
    } else {
        snprintf(message, sizeof message, " OTHELLO");
    }

    append_fmt(frame, sizeof frame, "%s%-78s%s%s\n",
               SGR_TITLE, message, SGR_RESET, SGR_CLEAR_EOL);

    if (!game) {
        append_fmt(frame, sizeof frame, "%s\n", SGR_CLEAR_EOL);
    } else if (screen->tag) {
        append_fmt(frame, sizeof frame, " %s%s%s%s\n",
                   SGR_TAG, screen->tag, SGR_RESET, SGR_CLEAR_EOL);
    } else if (is_game_over(game)) {
        describe_result(game, message, sizeof message);
        append_fmt(frame, sizeof frame, " %sGAME OVER:%s %s%s\n",
                   SGR_TAG, SGR_RESET, message, SGR_CLEAR_EOL);
    } else {
        append_fmt(frame, sizeof frame, " Turn: %s (%s)%s\n",
                   player_name(game, game->current_turn),
                   color_name(game->current_turn),
                   SGR_CLEAR_EOL);
    }

    append_fmt(frame, sizeof frame, "   ");

    for (int col = 0; col < BOARD_SIZE; col++) {
        append_fmt(frame, sizeof frame, "%s %c %s",
                   SGR_LABEL, 'A' + col, SGR_RESET);
    }

    append_fmt(frame, sizeof frame, "   %sMoves%s%s\n",
               SGR_LABEL, SGR_RESET, SGR_CLEAR_EOL);

    memset(legal_moves, 0, sizeof legal_moves);

    if (game && screen->hints && !is_game_over(game)) {
        get_all_valid_moves(game, game->current_turn, legal_moves);
    }

    const int first_history_row =
        (game && game->move_count > HISTORY_ROWS)
            ? game->move_count - HISTORY_ROWS
            : 0;

    for (int row = 0; row < BOARD_SIZE; row++) {
        append_fmt(frame, sizeof frame, "%s %d %s",
                   SGR_LABEL, row + 1, SGR_RESET);

        if (game) {
            for (int col = 0; col < BOARD_SIZE; col++) {
                append_cell(frame, sizeof frame, game, screen,
                            legal_moves, row, col);
            }
        } else {
            append_fmt(frame, sizeof frame, "%*s",
                       BOARD_SIZE * CELL_WIDTH, "");
        }

        append_fmt(frame, sizeof frame, "   ");

        const int history_index = first_history_row + row;

        if (game && history_index < game->move_count) {
            char square[3];
            const bool is_latest_move =
                history_index == game->move_count - 1;

            position_to_string(game->history[history_index].pos, square);

            append_fmt(frame, sizeof frame, "%s%2d. %-5s %s%s",
                       is_latest_move ? SGR_TAG : "",
                       history_index + 1,
                       color_name(game->history[history_index].player),
                       square,
                       is_latest_move ? SGR_RESET : "");
        }

        append_fmt(frame, sizeof frame, "%s\n", SGR_CLEAR_EOL);
    }

    if (game) {
        int black_pieces;
        int white_pieces;
        count_pieces(game, &black_pieces, &white_pieces);

        append_fmt(frame, sizeof frame,
                   "\n  Black X: %d   White O: %d%s\n",
                   black_pieces, white_pieces, SGR_CLEAR_EOL);
    } else {
        append_fmt(frame, sizeof frame, "\n%s\n", SGR_CLEAR_EOL);
    }

    if (screen->status && screen->status[0] != '\0') {
        append_fmt(frame, sizeof frame, " %s%.*s%s%s\n",
                   screen->status_error ? SGR_ERROR : SGR_OK,
                   STATUS_WIDTH,
                   screen->status,
                   SGR_RESET,
                   SGR_CLEAR_EOL);
    } else {
        append_fmt(frame, sizeof frame, "%s\n", SGR_CLEAR_EOL);
    }

    if (screen->input_prompt) {
        append_fmt(frame, sizeof frame, " %s%s%s%s%s %s%s",
                   SGR_TAG,
                   screen->input_prompt,
                   SGR_RESET,
                   screen->input_text ? screen->input_text : "",
                   SGR_INVERSE,
                   SGR_RESET,
                   SGR_CLEAR_EOL);
    } else {
        append_fmt(frame, sizeof frame, " %s%s%s%s",
                   SGR_DIM,
                   screen->help ? screen->help : "",
                   SGR_RESET,
                   SGR_CLEAR_EOL);
    }

    /* Move to the top-left, print this frame, then clear anything below it. */
    term_write("\x1b[H");
    fputs(frame, stdout);
    term_write("\x1b[J");
}

/*
 * ---------------------------------------------------------------------------
 * BEGINNER'S GUIDE: TERMINAL, ANSI, INPUT, AND PORTABILITY
 * ---------------------------------------------------------------------------
 *
 * 1. What this file does
 *
 * This file is the boundary between the Othello game and the terminal.
 * The game logic knows about moves and pieces. This file knows how to:
 *   - prepare the terminal before the game starts;
 *   - read keyboard and mouse input;
 *   - turn that input into the application's InputEvent values;
 *   - draw the board and status information on the screen;
 *   - restore the terminal settings when the game exits.
 *
 * 2. CLI and UI
 *
 * CLI means Command-Line Interface: a program controlled through text input
 * in a terminal. UI means User Interface: everything the player sees and
 * interacts with. This file implements a terminal-based UI.
 *
 * 3. ANSI and escape sequences
 *
 * ANSI is short for American National Standards Institute. In terminal
 * programming, "ANSI escape sequence" commonly refers to control bytes used
 * to change terminal behaviour or appearance.
 *
 * An escape sequence starts with ESC, whose decimal ASCII value is 27 and
 * hexadecimal value is 1B. In C, write it as '\x1b'.
 *
 * The next characters specify the command. For example:
 *   ESC [ 2 J   clear the visible screen;
 *   ESC [ H     move the cursor to the home position;
 *   ESC [ 0 m   reset text attributes;
 *   ESC [ 1 m   enable bold/bright text;
 *   ESC [ 31 m  select a red foreground colour.
 *
 * The terminal interprets these bytes as commands. They are not ordinary
 * printable text. Support varies between terminal emulators and console
 * hosts, especially for mouse and alternate-screen extensions.
 *
 * 4. Why ENTER_SCREEN and LEAVE_SCREEN are adjacent strings
 *
 * C concatenates adjacent string literals during translation. Therefore:
 *
 *   const char *commands = "one" "two";
 *
 * has the same result as:
 *
 *   const char *commands = "onetwo";
 *
 * No comma is needed because this is one string, not an array of strings.
 * The compiler joins the pieces before the program runs.
 *
 * 5. Meaning of the screen-control commands
 *
 *   ESC[?1049h  switch to the alternate screen buffer;
 *   ESC[?1049l  return to the normal screen buffer;
 *   ESC[?25l    hide the terminal cursor;
 *   ESC[?25h    show the terminal cursor;
 *   ESC[?1003h  request reports for all mouse movement;
 *   ESC[?1003l  stop reports for all mouse movement;
 *   ESC[?1006h  enable SGR-format mouse coordinates;
 *   ESC[?1006l  disable SGR-format mouse coordinates;
 *   ESC[2J      clear the visible screen;
 *   ESC[H       move the cursor to row 1, column 1;
 *   ESC[J       clear from the cursor to the end of the screen;
 *   ESC[K       clear from the cursor to the end of the current line.
 *
 * The h suffix generally enables a DEC private terminal mode and l disables
 * it. These private modes are terminal extensions, not universally supported
 * by every old terminal.
 *
 * 6. What static, const, char, and pointers mean here
 *
 *   char       stores one character/byte;
 *   const      prevents modification through that declaration;
 *   *          declares a pointer, which stores an address;
 *   static     gives a file-scope variable internal linkage and static
 *              storage duration.
 *
 * For "static const char *const NAME":
 *   - NAME is a pointer to characters;
 *   - the pointer cannot be reassigned;
 *   - the characters cannot be modified through this pointer;
 *   - static means this name is private to this source file.
 *
 * 7. Keyboard character values used in this file
 *
 *   '\x03'  ASCII ETX (End of Text); commonly generated by Ctrl+C;
 *   '\r'    carriage return; commonly part of the Enter key;
 *   '\n'    line feed; also accepted as Enter;
 *   '\b'    backspace control character, decimal 8;
 *   '\x7f'  DEL (Delete control character), decimal 127;
 *   ' '     ordinary space character, decimal 32;
 *   '\x1b'  Escape control character, decimal 27.
 *
 * A condition such as character >= ' ' checks that the byte is at least the
 * printable-space character. character < '\x7f' excludes DEL and higher
 * values. This code handles basic ASCII input, not arbitrary UTF-8 text.
 *
 * 8. Input events
 *
 * InputEvent is the common format used by the rest of the application.
 * The platform-specific code converts operating-system or terminal input
 * into values such as EV_UP, EV_ENTER, EV_CHAR, EV_MOUSE_PRESS, and EV_QUIT.
 * The rest of the game does not need to know whether the key came from a
 * Windows console event or a POSIX terminal byte sequence.
 *
 * 9. Windows and POSIX paths
 *
 * _WIN32 selects the Windows implementation:
 *   - GetStdHandle obtains console input/output handles;
 *   - GetConsoleMode and SetConsoleMode inspect/change console modes;
 *   - ReadConsoleInput reads structured keyboard and mouse events;
 *   - ENABLE_VIRTUAL_TERMINAL_PROCESSING asks supported Windows consoles to
 *     interpret ANSI/VT output sequences.
 *
 * The other branch uses POSIX terminal facilities, commonly available on
 * Linux and other Unix-like systems:
 *   - termios saves and changes terminal input settings;
 *   - isatty checks whether standard input is an interactive terminal;
 *   - read reads input bytes;
 *   - select waits for input with a timeout.
 *
 * "Raw mode" here is a practical terminal-input configuration: keys are
 * delivered immediately and are not echoed. The original settings are saved
 * and restored by term_restore().
 *
 * 10. Mouse coordinates and mouse sequence formats
 *
 * Terminal mouse coordinates are conventionally one-based: the top-left
 * terminal cell is x=1, y=1. The application stores zero-based coordinates,
 * where the top-left cell is row=0, col=0. Subtracting one converts between
 * these coordinate systems.
 *
 * SGR mouse reports have a form similar to ESC[<button;x;yM. The parser reads
 * the button code and coordinates. This implementation ignores wheel events
 * and release events, and reports left-button presses and movement.
 *
 * 11. Fonts and NERD FONTS
 *
 * This source uses ordinary ASCII board symbols: X, O, *, and . . It does
 * not request or require a Nerd Font, Powerline font, or special Unicode
 * icon font. A normal monospace font is sufficient.
 *
 * 12. Linux, PowerShell 7, and older terminals
 *
 * The POSIX branch is intended for Linux terminals that provide termios,
 * select, and ANSI/VT-compatible output. Linux itself is not the terminal:
 * the terminal emulator must understand the escape sequences.
 *
 * PowerShell 7 is a shell, not a terminal emulator. On Windows, the program
 * uses the Windows console API for input and asks the output console to enable
 * virtual-terminal processing. A modern Windows Terminal or a suitably
 * configured modern console host is the expected environment.
 *
 * Older terminal emulators may support basic colours and cursor movement but
 * may not support alternate-screen or the 1003/1006 mouse extensions. In that
 * case keyboard controls may still work, but mouse behaviour and screen
 * restoration/formatting can be degraded. This implementation does not
 * negotiate terminal capabilities or provide a full plain-text fallback.
 * Do not assume that every terminal, SSH client, IDE console, or redirected
 * output destination supports every sequence.
 *
 * The layout is designed for approximately 80 columns. A narrower terminal
 * may wrap lines and make the board difficult to read. Use a terminal at
 * least 80 columns wide for the intended layout.
 *
 * 13. Abbreviations used in this file
 *
 *   ANSI  - American National Standards Institute;
 *   ASCII - American Standard Code for Information Interchange;
 *   CLI   - Command-Line Interface;
 *   UI    - User Interface;
 *   POSIX - Portable Operating System Interface;
 *   SGR   - Select Graphic Rendition, the ANSI colour/style sequence family;
 *   ESC   - Escape control character;
 *   ETX   - End of Text control character;
 *   DEL   - Delete control character;
 *   VT    - Virtual Terminal;
 *   API   - Application Programming Interface;
 *   UTF-8 - Unicode Transformation Format, 8-bit encoding.
 *
 * 14. Official documentation
 *
 *   C string literal concatenation:
 *   https://en.cppreference.com/w/c/language/string_literal
 *
 *   C character constants:
 *   https://en.cppreference.com/w/c/language/character_constant
 *
 *   Microsoft console modes:
 *   https://learn.microsoft.com/en-us/windows/console/setconsolemode
 *
 *   Microsoft ReadConsoleInput:
 *   https://learn.microsoft.com/en-us/windows/console/readconsoleinput
 *
 *   Microsoft virtual terminal sequences:
 *   https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences
 *
 *   POSIX termios:
 *   https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/termios.h.html
 *
 *   POSIX select:
 *   https://pubs.opengroup.org/onlinepubs/9799919799/functions/select.html
 *
 *   POSIX read:
 *   https://pubs.opengroup.org/onlinepubs/9799919799/functions/read.html
 *
 *   xterm control sequences:
 *   https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 *
 * ---------------------------------------------------------------------------
 * END OF BEGINNER'S GUIDE
 * ---------------------------------------------------------------------------
 */
