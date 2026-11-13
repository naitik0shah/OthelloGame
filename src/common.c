// ===== COMMON UTILITIES TASK =====
#include "common.h"

bool pos_in_bounds(Position p) {
    return p.row >= 0 && p.row < BOARD_SIZE && p.col >= 0 && p.col < BOARD_SIZE;
}

PlayerColor opponent_of(PlayerColor p) {
    return p == PLAYER_BLACK ? PLAYER_WHITE : PLAYER_BLACK;
}

/* Accepts exactly two characters: a-h (any case) followed by 1-8. */
bool parse_position(const char *text, Position *out) {
    if (!text[0] || !text[1] || text[2]) return false;
    int file = tolower((unsigned char)text[0]);
    if (file < 'a' || file > 'h' || text[1] < '1' || text[1] > '8') return false;
    out->col = file - 'a';
    out->row = text[1] - '1';
    return true;
}

void position_to_string(Position pos, char out[3]) {
    assert(pos_in_bounds(pos));
    out[0] = (char)('a' + pos.col);
    out[1] = (char)('1' + pos.row);
    out[2] = '\0';
}

const char *player_name(const GameState *g, PlayerColor c) {
    return g->meta.p1_color == c ? g->meta.player1_name : g->meta.player2_name;
}

const char *color_name(PlayerColor c) {
    return c == PLAYER_BLACK ? "Black" : "White";
}

void trim(char *s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    const char *start = s;
    while (isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
}

/* Appends formatted text to a NUL-terminated buffer, never writing past `size`. */
void append_fmt(char *buf, size_t size, const char *fmt, ...) {
    size_t len = strlen(buf);
    if (len >= size) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + len, size - len, fmt, ap);
    va_end(ap);
}
int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
