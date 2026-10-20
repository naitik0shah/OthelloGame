#ifndef UI_H
#define UI_H

#include "common.h"

// Redraws full terminal screen: Board + Move History side panel + Turn indicator
void ui_render_board(const GameState *state, Position current_cursor);

// Displays status bar messages (e.g., Red error text for invalid moves)
void ui_display_status(const char *message, bool is_error);

// Replay UI viewer mode navigation (/ for forward, \ for backward)
void ui_view_replay(const GameState *state);

#endif // UI_H