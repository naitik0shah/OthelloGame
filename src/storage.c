// ===== GAME PERSISTENCE / FILE MANAGEMENT TASK =====
#include "common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char color_char(PlayerColor c)
{
    return c == PLAYER_BLACK ? 'B' : 'W';
}

/* Writes to <path>.tmp and renames, so a failed save never leaves a truncated
 * file. */
StorageStatus save_game(const GameState *s, const char *path)
{    
    if (s == NULL || path == NULL)
        return STORAGE_ERR_IO;

    if (strlen(path) >= MAX_PATH_LEN)
        return STORAGE_ERR_IO;

    if (s->move_count < 0 || s->move_count > MAX_MOVES)
        return STORAGE_ERR_FORMAT;

    if (s->meta.p1_color != PLAYER_BLACK &&
        s->meta.p1_color != PLAYER_WHITE)
        return STORAGE_ERR_FORMAT;

    char tmp[MAX_PATH_LEN + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) return STORAGE_ERR_IO;

    fprintf(f, "OTHELLO 1\nP1=%s\nP2=%s\nP1COLOR=%c\nMOVES=%d\n", s->meta.player1_name,
            s->meta.player2_name, color_char(s->meta.p1_color), s->move_count);
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
        remove(path); /* Windows: rename fails when the target exists */
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return STORAGE_ERR_IO;
        }
    }
    return STORAGE_OK;
}

/* Reads one line and checks that it starts with `key`. On success, *value
 * points into `line`. */
bool read_field(FILE *f, const char *key, char *line, size_t size, const char **value)
{
    if (!fgets(line, (int)size, f)) return false;
    line[strcspn(line, "\r\n")] = '\0';
    const size_t k = strlen(key);
    if (strncmp(line, key, k) != 0) return false;
    *value = line + k;
    return true;
}

/* All-or-nothing: *state changes only when the result is STORAGE_OK. */
StorageStatus load_game(GameState *state, const char *path)
{
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
    if (strcmp(v, "B") == 0)
        src.meta.p1_color = PLAYER_BLACK;
    else if (strcmp(v, "W") == 0)
        src.meta.p1_color = PLAYER_WHITE;
    else
        goto done;

    if (!read_field(f, "MOVES=", line, sizeof line, &v)) goto done;
    const long n = strtol(v, &end, 10);
    if (end == v || *end != '\0' || n < 0 || n > MAX_MOVES) goto done;
    src.move_count = (int)n;

    for (int i = 0; i < src.move_count; i++) {
        if (!fgets(line, sizeof line, f)) goto done;
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) != 4 || line[2] != ' ') goto done;
        const char sq[3] = {line[0], line[1], '\0'};
        if (!parse_position(sq, &src.history[i].pos)) goto done;
        if (line[3] == 'B')
            src.history[i].player = PLAYER_BLACK;
        else if (line[3] == 'W')
            src.history[i].player = PLAYER_WHITE;
        else
            goto done;
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

const char *storage_status_str(StorageStatus st)
{
    switch (st) {
    case STORAGE_OK:
        return "OK";
    case STORAGE_ERR_IO:
        return "cannot open/read/write the file";
    case STORAGE_ERR_FORMAT:
        return "file is not a valid Othello save";
    case STORAGE_ERR_ILLEGAL_MOVE:
        return "saved moves are not a legal game";
    }
    return "unknown error";
}
