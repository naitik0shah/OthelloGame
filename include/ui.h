#ifndef UI_H
#define UI_H

#include "common.h"

typedef struct {
    Position cursor;
    bool     show_hints;     /* draw '*' on legal moves (play mode only) */
    int      replay_step;    /* -1 = live game; else step shown in replay */
} RenderOptions;

/* Full redraw: board (A-H / 1-8), move-history side panel, turn indicator, scores */
void ui_render_board(const GameState *state, const RenderOptions *opts);

/* Status line under the history panel; is_error -> red ANSI text */
void ui_display_status(const char *message, bool is_error);

void ui_render_welcome(void);
void ui_render_help(void);

/* Replay loop: '/' forward, '\' (or 'p') backward, ESC exits.
   Uses rebuild_state_at(); `game` is never modified. */
void ui_view_replay(const GameState *game);

/* Mouse support: terminal cell -> board square. Returns false if outside the board. */
bool ui_screen_to_cell(int screen_row, int screen_col, Position *out);

/* Click on a history-panel entry -> its 0-based move index, or -1 */
int  ui_screen_to_history_index(int screen_row, int screen_col, const GameState *state);

#endif /* UI_H */
