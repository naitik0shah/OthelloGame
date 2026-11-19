// ===== GAME STATISTICS TASK =====
#include "common.h"
#include <stdio.h>
#include <string.h>

void compute_stats_summary(const char *const paths[], int count, StatsSummary *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < count && i < MAX_STAT_FILES; i++) {
        GameState g;
        if (load_game(&g, paths[i]) != STORAGE_OK || !is_game_over(&g)) {
            out->skipped_files++;
            continue;
        }

        int b, w;
        count_pieces(&g, &b, &w);
        GameResult result = get_game_result(&g);
        GameStat *game = &out->games[out->total_games];

        game->black_score = b;
        game->white_score = w;
        game->is_tie = result == RESULT_TIE;

        if (result == RESULT_BLACK_WIN) {
            game->winner_color = PLAYER_BLACK;
            game->winner_score = b;
            game->opponent_score = w;
            snprintf(game->winner_name, sizeof game->winner_name, "%s",
                     player_name(&g, PLAYER_BLACK));
        } else if (result == RESULT_WHITE_WIN) {
            game->winner_color = PLAYER_WHITE;
            game->winner_score = w;
            game->opponent_score = b;
            snprintf(game->winner_name, sizeof game->winner_name, "%s",
                     player_name(&g, PLAYER_WHITE));
        }

        out->total_games++;
    }
}

void print_stats_summary(const StatsSummary *s)
{
    puts("---------- Match Results ----------");
    for (int i = 0; i < s->total_games; i++) {
        const GameStat *game = &s->games[i];
        printf("Game %d: ", i + 1);
        if (game->is_tie) {
            printf("Draw | Black: %d, White: %d\n",
                   game->black_score, game->white_score);
        } else {
            printf("%s won as %s | Score: %d - %d\n",
                   game->winner_name, color_name(game->winner_color),
                   game->winner_score, game->opponent_score);
        }
    }
    printf("Games counted: %d | Files skipped: %d\n",
           s->total_games, s->skipped_files);
    puts("---------------------------------");
}
