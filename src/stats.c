// ===== GAME STATISTICS TASK =====
#include "common.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void compute_stats_summary(const char *const paths[], int count, StatsSummary *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < count; i++) {
        GameState g;
        if (load_game(&g, paths[i]) != STORAGE_OK || !is_game_over(&g)) {
            out->skipped_files++;
            continue;
        }
        int b, w;
        count_pieces(&g, &b, &w);
        out->total_games++;
        switch (get_game_result(&g)) {
        case RESULT_BLACK_WIN:
            out->black_wins++;
            break;
        case RESULT_WHITE_WIN:
            out->white_wins++;
            break;
        default:
            out->ties++;
            break;
        }
        if (b > out->max_score) {
            out->max_score = b;
            snprintf(out->highest_scoring_player, sizeof out->highest_scoring_player, "%s",
                     player_name(&g, PLAYER_BLACK));
        }
        if (w > out->max_score) {
            out->max_score = w;
            snprintf(out->highest_scoring_player, sizeof out->highest_scoring_player, "%s",
                     player_name(&g, PLAYER_WHITE));
        }
    }
}

void print_stats_summary(const StatsSummary *s)
{
    puts("---------- Statistics ----------");
    printf("Finished games counted : %d\n", s->total_games);
    printf("Files skipped          : %d (unreadable, corrupt or unfinished)\n", s->skipped_files);
    printf("Black wins             : %d\n", s->black_wins);
    printf("White wins             : %d\n", s->white_wins);
    printf("Ties                   : %d\n", s->ties);
    if (s->total_games > 0)
        printf("Highest score          : %d (%s)\n", s->max_score, s->highest_scoring_player);
    else
        puts("Highest score          : -");
    puts("--------------------------------");
}
