// ===== GAME STATISTICS TASK =====
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <cli_internal.h>

void show_stats(App *a, const char *const paths[], int count)
{
    StatsSummary sum;
    compute_stats_summary(paths, count, &sum);

    static char buf[FRAME_SIZE];
    char line[160];
    int cols = DEFAULT_TERM_COLS;
    int rows = DEFAULT_TERM_ROWS;
    cli_get_terminal_size(&cols, &rows);

    const char *eol = cli_use_vt_sequences() ? ANSI_CLEAR_EOL : "";
    int rows_left = (rows > 6) ? rows : 6;
    int title_width = (cols > 1) ? (cols - 1) : DEFAULT_TERM_COLS;
    buf[0] = '\0';

    append_fmt(buf, sizeof buf, "%s%-*s%s%s\n", cli_style_str(SGR_TITLE), title_width,
               "  OTHELLO - MATCH RESULTS", cli_style_str(SGR_RESET), eol);
    rows_left--;

    for (int i = 0; i < sum.total_games && rows_left > 2; i++) {
        const GameStat *game = &sum.games[i];
        if (game->is_tie) {
            snprintf(line, sizeof line, "  Game %d: Draw | Black %d - White %d", i + 1,
                     game->black_score, game->white_score);
        } else {
            snprintf(line, sizeof line, "  Game %d: %s (%s) won | %d - %d", i + 1,
                     game->winner_name, color_name(game->winner_color), game->winner_score,
                     game->opponent_score);
        }
        cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 2, &rows_left, true);
    }

    snprintf(line, sizeof line, "  Games counted: %d | Files skipped: %d", sum.total_games,
             sum.skipped_files);
    cli_append_wrapped_line(buf, sizeof buf, "", line, "", eol, cols, 2, &rows_left, true);

    cli_append_wrapped_line(buf, sizeof buf, cli_style_str(SGR_OK), "Press any key to return...",
                            cli_style_str(SGR_RESET), eol, cols, 0, &rows_left, false);
    cli_flush_terminal_frame(buf);
    wait_key(a);
    set_status(a, false, "Match results: %d game(s), %d skipped.", sum.total_games,
               sum.skipped_files);
}

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
            printf("Draw | Black: %d, White: %d\n", game->black_score, game->white_score);
        } else {
            printf("%s won as %s | Score: %d - %d\n", game->winner_name,
                   color_name(game->winner_color), game->winner_score, game->opponent_score);
        }
    }
    printf("Games counted: %d | Files skipped: %d\n", s->total_games, s->skipped_files);
    puts("---------------------------------");
}
