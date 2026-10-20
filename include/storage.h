#ifndef STORAGE_H
#define STORAGE_H

#include "common.h"

// Saves current game state to file path
bool save_game(const GameState *state, const char *filepath);

// Loads game state from file path into state pointer
bool load_game(GameState *state, const char *filepath);

// Parses multiple game save files and prints aggregated statistics summary table
void generate_stats_summary(const char * const filepaths[], int file_count);

#endif // STORAGE_H