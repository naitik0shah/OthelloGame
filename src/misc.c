// ===== INTEGRATION & MISCELLANEOUS TASK =====
#include "common.h"
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* CLI & command-handling functions implemented in ui_cli_platform.c */
bool edit_line(App *a, const char *prompt, char *buf, size_t size);
void command_prompt(App *a);
bool screen_to_cell(int row, int col, Position *out);

void move_cursor(App *a, int dr, int dc)
{
    a->cursor.row = clamp_int(a->cursor.row + dr, 0, BOARD_SIZE - 1);
    a->cursor.col = clamp_int(a->cursor.col + dc, 0, BOARD_SIZE - 1);
}

void app_place(App *a)
{
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
        if (me == MOVE_ERR_NO_CAPTURE && a->hints) {
            append_fmt(a->status, sizeof a->status, " Squares marked * are legal.");
        }
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

void app_handle_char(App *a, int ch)
{
    const int lc = tolower(ch);
    if (ch == ' ')
        app_place(a);
    else if (lc >= 'a' && lc <= 'h')
        a->cursor.col = lc - 'a';
    else if (ch >= '1' && ch <= '8')
        a->cursor.row = ch - '1';
    else if (ch == ':')
        command_prompt(a);
}

void app_handle_event(App *a, InputEvent ev)
{
    Position p;
    switch (ev.kind) {
    case EV_CHAR:
        app_handle_char(a, ev.ch);
        break;
    case EV_UP:
        move_cursor(a, -1, 0);
        break;
    case EV_DOWN:
        move_cursor(a, 1, 0);
        break;
    case EV_LEFT:
        move_cursor(a, 0, -1);
        break;
    case EV_RIGHT:
        move_cursor(a, 0, 1);
        break;
    case EV_ENTER:
        app_place(a);
        break;
    case EV_ESC:
        a->status[0] = '\0';
        break;
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

bool prompt_name(App *a, const char *label, char *dst)
{
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

bool prompt_new_game(App *a, const ParsedCommand *cmd, GameMeta *meta)
{
    memset(meta, 0, sizeof *meta);
    if (cmd->p1[0] != '\0')
        snprintf(meta->player1_name, sizeof meta->player1_name, "%s", cmd->p1);
    else if (!prompt_name(a, "Player 1 name: ", meta->player1_name))
        return false;

    if (cmd->p2[0] != '\0')
        snprintf(meta->player2_name, sizeof meta->player2_name, "%s", cmd->p2);
    else if (!prompt_name(a, "Player 2 name: ", meta->player2_name))
        return false;

    meta->p1_color = cmd->white_is_p1 ? PLAYER_WHITE : PLAYER_BLACK;
    return true;
}
