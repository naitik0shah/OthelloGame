#ifndef CLI_INTERNAL_H
#define CLI_INTERNAL_H

#include "common.h"

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
#define SGR_RESET           SGR(SGR_ATTR_RESET)
#define SGR_TITLE           SGR(SGR_ATTR_BOLD ";" FG_BRIGHT_WHITE ";" BG_BLUE)
#define SGR_LABEL           SGR(SGR_ATTR_BOLD ";" FG_CYAN)
#define SGR_BG_BOARD        SGR(BG_GREEN)
#define SGR_BG_LAST         SGR(BG_BRIGHT_YELLOW)
#define SGR_BLACK           SGR(SGR_ATTR_BOLD ";" FG_BLACK)
#define SGR_WHITE           SGR(SGR_ATTR_BOLD ";" FG_BRIGHT_WHITE)
#define SGR_HINT            SGR(SGR_ATTR_BOLD ";" FG_YELLOW)
#define SGR_DIM             SGR(SGR_ATTR_DIM)
#define SGR_INVERSE         SGR(SGR_ATTR_INVERSE)
#define SGR_ERROR           SGR(SGR_ATTR_BOLD ";" FG_RED)
#define SGR_OK              SGR(SGR_ATTR_BOLD ";" FG_GREEN)
#define SGR_TAG             SGR(SGR_ATTR_BOLD ";" FG_YELLOW)

/* Layout sizing thresholds for word-wrapping and side-by-side panes */
enum {
    DEFAULT_TERM_COLS       = 80,
    DEFAULT_TERM_ROWS       = 24,
    MIN_WRAP_COLS           = 24,
    MIN_COLS_WITH_HISTORY   = 48
};

/* Platform & terminal capability helpers (implemented in cli_platform.c) */
void term_write(const char *text);
bool cli_use_vt_sequences(void);
bool cli_use_color(void);
const char *cli_style_str(const char *seq);
void cli_get_terminal_size(int *cols_out, int *rows_out);
void cli_win32_prepare_legacy_frame(void);

/* Frame-buffer & screen-output helpers (implemented in cli_render.c) */
int cli_append_wrapped_line(char *frame, size_t frame_size,
                            const char *prefix_style, const char *text,
                            const char *suffix_style, const char *eol,
                            int max_cols, int indent_spaces,
                            int *rows_left, bool trailing_newline);
void cli_flush_terminal_frame(const char *frame);

/* Replay state accessor (implemented in cli_commands.c) */
const GameState *cli_get_replay_game(void);

#endif /* CLI_INTERNAL_H */
