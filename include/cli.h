#ifndef CLI_H
#define CLI_H

#include "common.h"

// Parse raw string command inputs (:n, :l, :s, :v, :h, :q)
void cli_parse_command(const char *input, GameState *state);

// Prompt interactive player configuration if flags are omitted
void cli_prompt_new_game(GameMeta *meta);

// Display top header, help menu, and command list
void cli_render_help(void);

#endif // CLI_H