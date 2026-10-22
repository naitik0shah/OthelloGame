#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"

typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERR_IO,            /* missing file / cannot open / write failed */
    STORAGE_ERR_FORMAT,        /* header, field or move line malformed */
    STORAGE_ERR_ILLEGAL_MOVE   /* history does not replay legally */
} StorageStatus;

/* Text save format is defined in PRD.md section 5 */
StorageStatus save_game(const GameState *state, const char *filepath);

/* Board, turn, game_ended and valid_moves are reconstructed by replaying history */
StorageStatus load_game(GameState *state, const char *filepath);

/* Aggregates finished games only; bad/unfinished files are counted in skipped_files.
   Returns number of files successfully counted. */
int  compute_stats_summary(const char *const filepaths[], int file_count, StatsSummary *out);

/* Prints the summary table */
void print_stats_summary(const StatsSummary *summary);

const char *storage_status_str(StorageStatus status);

#endif /* STORAGE_H */
