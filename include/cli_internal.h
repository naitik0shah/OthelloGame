/*
 * Internal declarations shared by the terminal platform and rendering layers.
 * Most callers should use the public functions declared in common.h.
 */

#ifndef CLI_INTERNAL_H
#define CLI_INTERNAL_H

#include "common.h"

/** DEC ANSI control and SGR-style escape-sequence primitives. */
/*
 * ANSI / DEC VT terminal control sequence builders.
 *
 * Every control sequence starts with ESC (ASCII 27 / 0x1B) followed by '['
 * (the Control Sequence Introducer, CSI). Adjacent C string literals are
 * concatenated by the compiler at translation time with zero runtime cost.
 */
#define CSI             "\x1b["
#define SGR(codes)      CSI codes "m"
#define DEC_SET(mode)   CSI "?" #mode "h"
#define DEC_RESET(mode) CSI "?" #mode "l"

/** ANSI Select Graphic Rendition attribute and colour constants. */
/* SGR (Select Graphic Rendition) text attributes and colour parameters
 * use standard ECMA-48 codes for both modern and legacy terminals. */
#define SGR_ATTR_RESET     "0"
#define SGR_ATTR_BOLD      "1"
#define SGR_ATTR_DIM       "2"
#define SGR_ATTR_UNDERLINE "4"
#define SGR_ATTR_INVERSE   "7"

#define FG_DEFAULT         "39"
#define FG_RED             "31"
#define FG_GREEN           "32"
#define FG_YELLOW          "33"
#define FG_CYAN            "36"

#define BG_DEFAULT         "49"

/* Cursor and erase sequences (DEC ?12l disables blink; ?25l hides cursor) */
#define ANSI_CURSOR_HOME  CSI "H"
#define ANSI_CURSOR_HIDE  DEC_RESET(12) DEC_RESET(25)
#define ANSI_CURSOR_SHOW  DEC_SET(12) DEC_SET(25)
#define ANSI_CLEAR_SCREEN CSI "2J"
#define ANSI_CLEAR_BELOW  CSI "J"
#define ANSI_CLEAR_EOL    CSI "K"

/** Semantic text and board styles used by the CLI renderer. */
#define SGR_RESET    SGR(SGR_ATTR_RESET)
#define SGR_TITLE    SGR(SGR_ATTR_INVERSE ";" SGR_ATTR_BOLD)
#define SGR_LABEL    SGR(FG_CYAN)
#define SGR_BG_BOARD SGR(BG_DEFAULT)
#define SGR_BG_LAST  SGR(FG_YELLOW ";" SGR_ATTR_UNDERLINE)
#define SGR_BLACK    SGR(SGR_ATTR_BOLD ";" FG_CYAN)
#define SGR_WHITE    SGR(SGR_ATTR_BOLD ";" FG_DEFAULT)
#define SGR_HINT     SGR(FG_GREEN)
#define SGR_DIM      SGR(SGR_ATTR_DIM)
#define SGR_INVERSE  SGR(SGR_ATTR_INVERSE)
#define SGR_ERROR    SGR(FG_RED)
#define SGR_OK       SGR(FG_GREEN)
#define SGR_TAG      SGR(FG_YELLOW)

/** Layout thresholds controlling wrapping and the optional history pane. */
enum {
    DEFAULT_TERM_COLS = 80,
    DEFAULT_TERM_ROWS = 24,
    MIN_WRAP_COLS = 24,
    MIN_COLS_WITH_HISTORY = 48
};

/** Write text through the platform-specific terminal output path. */
void term_write(const char *text);
/** Return true when ANSI/VT control sequences are supported. */
bool cli_use_vt_sequences(void);
/** Return true when the terminal supports rendered text styles. */
bool cli_use_color(void);
/** Return a style sequence or an empty string when styles are unavailable. */
const char *cli_style_str(const char *seq);
/** Fill outputs with the terminal's visible columns and rows. */
void cli_get_terminal_size(int *cols_out, int *rows_out);
/** Prepare a complete frame for Windows legacy-console output. */
void cli_win32_prepare_legacy_frame(void);

/**
 * Append and word-wrap one line to the in-memory frame buffer.
 * @param frame Output frame buffer.
 * @param frame_size Size of frame in bytes.
 * @param prefix_style Optional style emitted before text.
 * @param text Text to wrap.
 * @param suffix_style Optional style emitted after text.
 * @param eol End-of-line control sequence.
 * @param max_cols Available terminal columns.
 * @param indent_spaces Additional indentation for continuation lines.
 * @param rows_left Remaining visible rows; decremented for emitted rows.
 * @param trailing_newline Whether to emit a final newline.
 * @return Number of frame rows emitted.
 */
int cli_append_wrapped_line(char *frame, size_t frame_size, const char *prefix_style,
                            const char *text, const char *suffix_style, const char *eol,
                            int max_cols, int indent_spaces, int *rows_left, bool trailing_newline);

/** Flush a complete frame atomically and park the cursor at the origin. */
void cli_flush_terminal_frame(const char *frame);

/** Return the full game currently backing replay mode, or NULL. */
const GameState *cli_get_replay_game(void);

#endif /* CLI_INTERNAL_H */
