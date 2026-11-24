/*
 * Othello validation/capture safety-net tests (C17).
 *
 * Tests the public API declared in common.h using an independent rules oracle.
 * Includes boundary/error cases, all eight capture directions, multi-ray
 * capture, invalid-move atomicity, pass/game-over handling, history replay,
 * and 1,000 deterministic random complete games.
 *
 * Build from the project root:
 *   gcc -std=c17 -Wall -Wextra -Wpedantic -Iinclude \
 *       tests/test_validation_capture_safety.c src/common.c \
 *       src/validatio_capture.c -DRESULT_INVALID=RESULT_IN_PROGRESS \
 *       -o test_validation_capture_safety
 *
 * IMPORTANT: The uploaded validation source references RESULT_INVALID for a
 * NULL game, but common.h does not declare that enum value. The temporary
 * compiler definition above lets this test compile without modifying project
 * files; it does NOT fix the API inconsistency. Resolve it in the project by
 * adding an appropriate RESULT_INVALID enum value (or changing the source's
 * NULL behaviour) before treating the build as clean.
 *
 * Sanitizer build (GCC/Clang):
 *   gcc -std=c17 -g -O1 -Wall -Wextra -Wpedantic \
 *       -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
 *       tests/test_validation_capture_safety.c src/common.c \
 *       src/validatio_capture.c -DRESULT_INVALID=RESULT_IN_PROGRESS \
 *       -o test_validation_capture_safety_asan
 */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
static unsigned long failures;

#define CHECK(x) do { \
    ++checks; \
    if (!(x)) { \
        ++failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    } \
} while (0)

static const Position directions[NUM_DIRECTIONS] = {
    {-1, 0}, {-1, 1}, {0, 1}, {1, 1},
    {1, 0}, {1, -1}, {0, -1}, {-1, -1}
};

static bool inside(int row, int col)
{
    return row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE;
}

/* Independent reference implementation: does not call the tested functions. */
static int oracle_ray(const GameState *s, PlayerColor player,
                      Position at, Position dir)
{
    int row = at.row + dir.row;
    int col = at.col + dir.col;
    int count = 0;
    CellState opponent = player == PLAYER_BLACK ? CELL_WHITE : CELL_BLACK;

    if (s == NULL || (player != PLAYER_BLACK && player != PLAYER_WHITE) ||
        !inside(at.row, at.col) || dir.row < -1 || dir.row > 1 ||
        dir.col < -1 || dir.col > 1 || (dir.row == 0 && dir.col == 0))
        return 0;

    while (inside(row, col) && s->board[row][col] == opponent) {
        ++count;
        row += dir.row;
        col += dir.col;
    }
    if (count > 0 && inside(row, col) &&
        s->board[row][col] == (CellState)player)
        return count;
    return 0;
}

static int oracle_flips(const GameState *s, PlayerColor player, Position at)
{
    int total = 0;
    int d;
    if (s == NULL || !inside(at.row, at.col) ||
        (player != PLAYER_BLACK && player != PLAYER_WHITE) ||
        s->board[at.row][at.col] != CELL_EMPTY)
        return 0;
    for (d = 0; d < NUM_DIRECTIONS; ++d)
        total += oracle_ray(s, player, at, directions[d]);
    return total;
}

static unsigned int rng_state = 0x5EED1234u;
static unsigned int next_random(void)
{
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static void new_game(GameState *s)
{
    GameMeta meta = {{0}, {0}, PLAYER_BLACK};
    (void)snprintf(meta.player1_name, sizeof meta.player1_name, "Black");
    (void)snprintf(meta.player2_name, sizeof meta.player2_name, "White");
    init_game(s, &meta);
}

static void compare_legal_moves(const GameState *s)
{
    int player;
    for (player = PLAYER_BLACK; player <= PLAYER_WHITE; ++player) {
        bool actual[BOARD_SIZE][BOARD_SIZE];
        int expected_count = 0;
        int actual_count;
        int row, col;

        actual_count = get_all_valid_moves(s, (PlayerColor)player, actual);
        for (row = 0; row < BOARD_SIZE; ++row) {
            for (col = 0; col < BOARD_SIZE; ++col) {
                bool expected = oracle_flips(s, (PlayerColor)player,
                                             (Position){row, col}) > 0;
                CHECK(actual[row][col] == expected);
                CHECK(validate_move(s, (PlayerColor)player,
                                    (Position){row, col}) == expected);
                if (expected) ++expected_count;
            }
        }
        CHECK(actual_count == expected_count);
        CHECK(has_any_valid_move(s, (PlayerColor)player) ==
              (expected_count != 0));
    }
}

static void test_argument_safety(void)
{
    GameState s;
    bool grid[BOARD_SIZE][BOARD_SIZE];
    new_game(&s);

    CHECK(count_flips(NULL, PLAYER_BLACK, (Position){2, 3},
                      (Position){1, 0}) == 0);
    CHECK(count_flips(&s, (PlayerColor)99, (Position){2, 3},
                      (Position){1, 0}) == 0);
    CHECK(count_flips(&s, PLAYER_BLACK, (Position){-1, 0},
                      (Position){1, 0}) == 0);
    CHECK(count_flips(&s, PLAYER_BLACK, (Position){8, 0},
                      (Position){1, 0}) == 0);
    CHECK(count_flips(&s, PLAYER_BLACK, (Position){2, 3},
                      (Position){0, 0}) == 0);
    CHECK(count_flips(&s, PLAYER_BLACK, (Position){2, 3},
                      (Position){2, 0}) == 0);
    CHECK(count_flips(&s, PLAYER_BLACK, (Position){2, 3},
                      (Position){0, -2}) == 0);

    CHECK(check_move(NULL, PLAYER_BLACK, (Position){0, 0}) == MOVE_ERR_NO_CAPTURE);
    CHECK(check_move(&s, (PlayerColor)99, (Position){0, 0}) == MOVE_ERR_NO_CAPTURE);
    CHECK(check_move(&s, PLAYER_BLACK, (Position){-1, 0}) == MOVE_ERR_NO_CAPTURE);
    CHECK(check_move(&s, PLAYER_BLACK, (Position){0, 8}) == MOVE_ERR_NO_CAPTURE);
    CHECK(!validate_move(NULL, PLAYER_BLACK, (Position){0, 0}));
    CHECK(!validate_move(&s, (PlayerColor)99, (Position){0, 0}));
    CHECK(get_all_valid_moves(NULL, PLAYER_BLACK, grid) == 0);
    CHECK(get_all_valid_moves(&s, PLAYER_BLACK, NULL) == 0);
    CHECK(!has_any_valid_move(NULL, PLAYER_BLACK));
    CHECK(!has_any_valid_move(&s, (PlayerColor)99));
    CHECK(is_game_over(NULL));

    memset(grid, 1, sizeof grid);
    CHECK(get_all_valid_moves(NULL, PLAYER_BLACK, grid) == 0);
    for (int r = 0; r < BOARD_SIZE; ++r)
        for (int c = 0; c < BOARD_SIZE; ++c)
            CHECK(grid[r][c] == false);
}

static void test_opening_and_error_messages(void)
{
    GameState s;
    bool moves[BOARD_SIZE][BOARD_SIZE];
    new_game(&s);
    CHECK(s.current_turn == PLAYER_BLACK);
    CHECK(s.move_count == 0);
    CHECK(s.board[3][3] == CELL_WHITE);
    CHECK(s.board[3][4] == CELL_BLACK);
    CHECK(s.board[4][3] == CELL_BLACK);
    CHECK(s.board[4][4] == CELL_WHITE);
    CHECK(get_all_valid_moves(&s, PLAYER_BLACK, moves) == 4);
    CHECK(check_move(&s, PLAYER_BLACK, (Position){2, 3}) == MOVE_OK);
    CHECK(check_move(&s, PLAYER_BLACK, (Position){3, 3}) == MOVE_ERR_OCCUPIED);
    CHECK(check_move(&s, PLAYER_BLACK, (Position){0, 0}) == MOVE_ERR_NO_CAPTURE);
    CHECK(strcmp(move_error_str(MOVE_OK), "Move is valid") == 0);
    CHECK(strcmp(move_error_str(MOVE_ERR_OCCUPIED),
                 "Square is already occupied") == 0);
    CHECK(strcmp(move_error_str(MOVE_ERR_NO_CAPTURE),
                 "Move must flip at least one opponent piece") == 0);
    CHECK(strcmp(move_error_str((MoveError)99), "Unknown error") == 0);
    compare_legal_moves(&s);
}

static void clear_board(GameState *s)
{
    memset(s, 0, sizeof *s);
    s->current_turn = PLAYER_BLACK;
    s->meta.p1_color = PLAYER_BLACK;
}

static void test_all_capture_directions(void)
{
    GameState s;
    Position origin = {3, 3};

    for (int d = 0; d < NUM_DIRECTIONS; ++d) {
        Position step = directions[d];
        int r1 = origin.row + step.row;
        int c1 = origin.col + step.col;
        int r2 = origin.row + 2 * step.row;
        int c2 = origin.col + 2 * step.col;
        clear_board(&s);
        CHECK(inside(r1, c1) && inside(r2, c2));
        s.board[r1][c1] = CELL_WHITE;
        s.board[r2][c2] = CELL_BLACK;
        CHECK(count_flips(&s, PLAYER_BLACK, origin, step) == 1);
        s.board[r2][c2] = CELL_EMPTY;
        CHECK(count_flips(&s, PLAYER_BLACK, origin, step) == 0);
    }

    /* Edge reached before a friendly bracket: must not capture. */
    clear_board(&s);
    s.board[0][1] = CELL_WHITE;
    CHECK(count_flips(&s, PLAYER_BLACK, (Position){0, 0},
                      (Position){0, 1}) == 0);
}

static void test_eight_direction_move_and_atomic_rejection(void)
{
    GameState s, before;
    Position at = {3, 3};
    int black, white;
    clear_board(&s);
    for (int d = 0; d < NUM_DIRECTIONS; ++d) {
        s.board[at.row + directions[d].row][at.col + directions[d].col] = CELL_WHITE;
        s.board[at.row + 2 * directions[d].row][at.col + 2 * directions[d].col] = CELL_BLACK;
    }
    CHECK(check_move(&s, PLAYER_BLACK, at) == MOVE_OK);
    CHECK(execute_move(&s, at) == 8);
    CHECK(s.move_count == 1);
    CHECK(s.board[at.row][at.col] == CELL_BLACK);
    for (int d = 0; d < NUM_DIRECTIONS; ++d)
        CHECK(s.board[at.row + directions[d].row]
                     [at.col + directions[d].col] == CELL_BLACK);
    count_pieces(&s, &black, &white);
    CHECK(black == 17 && white == 0);

    new_game(&s);
    before = s;
    CHECK(execute_move(&s, (Position){0, 0}) == 0);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    CHECK(execute_move(&s, (Position){3, 3}) == 0);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    CHECK(execute_move(&s, (Position){-1, 3}) == 0);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
}

static void test_initialization_and_capacity_guards(void)
{
    GameState s, before;
    GameMeta meta = {{0}, {0}, (PlayerColor)99};
    memset(&s, 0xA5, sizeof s);
    init_game(NULL, &meta);
    init_game(&s, NULL);
    CHECK(s.current_turn == PLAYER_BLACK);
    CHECK(s.move_count == 0);
    CHECK(s.meta.p1_color == PLAYER_BLACK);

    new_game(&s);
    s.meta.p1_color = PLAYER_WHITE;
    init_game(&s, &s.meta); /* metadata aliases destination */
    CHECK(s.meta.p1_color == PLAYER_WHITE);
    CHECK(s.current_turn == PLAYER_BLACK);

    new_game(&s);
    s.move_count = -1;
    before = s;
    CHECK(execute_move(&s, (Position){2, 3}) == 0);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    new_game(&s);
    s.move_count = MAX_MOVES;
    before = s;
    CHECK(execute_move(&s, (Position){2, 3}) == 0);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    new_game(&s);
    s.current_turn = (PlayerColor)99;
    before = s;
    CHECK(execute_move(&s, (Position){2, 3}) == 0);
    CHECK(memcmp(&s, &before, sizeof s) == 0);
    CHECK(switch_turn(&s) == TURN_GAME_OVER);
}

static void test_pass_and_terminal_state(void)
{
    GameState s;
    clear_board(&s);
    /* Only one empty square remains. Black can capture the lone White disc;
       White has no legal move because that empty square is adjacent to White. */
    for (int r = 0; r < BOARD_SIZE; ++r)
        for (int c = 0; c < BOARD_SIZE; ++c)
            s.board[r][c] = CELL_BLACK;
    s.board[0][0] = CELL_EMPTY;
    s.board[0][1] = CELL_WHITE;
    CHECK(has_any_valid_move(&s, PLAYER_BLACK));
    CHECK(!has_any_valid_move(&s, PLAYER_WHITE));
    CHECK(!is_game_over(&s));
    CHECK(switch_turn(&s) == TURN_OPPONENT_PASSED);
    CHECK(s.current_turn == PLAYER_BLACK);

    for (int r = 0; r < BOARD_SIZE; ++r)
        for (int c = 0; c < BOARD_SIZE; ++c)
            s.board[r][c] = CELL_BLACK;
    CHECK(is_game_over(&s));
    CHECK(switch_turn(&s) == TURN_GAME_OVER);
    CHECK(get_game_result(&s) == RESULT_BLACK_WIN);
}

static void test_random_full_games(void)
{
    const int game_total = 1000;
    for (int game = 0; game < game_total; ++game) {
        GameState s;
        int safety_plies = 0;
        new_game(&s);

        while (!is_game_over(&s) && safety_plies <= MAX_MOVES + 1) {
            bool legal[BOARD_SIZE][BOARD_SIZE];
            Position choices[BOARD_SIZE * BOARD_SIZE];
            int choice_count = 0;
            PlayerColor mover = s.current_turn;
            GameState before = s;

            compare_legal_moves(&s);
            get_all_valid_moves(&s, mover, legal);
            for (int r = 0; r < BOARD_SIZE; ++r)
                for (int c = 0; c < BOARD_SIZE; ++c)
                    if (legal[r][c]) choices[choice_count++] = (Position){r, c};

            if (choice_count == 0) {
                TurnStatus status = switch_turn(&s);
                CHECK(status == TURN_OPPONENT_PASSED || status == TURN_GAME_OVER);
                if (status == TURN_GAME_OVER) break;
                CHECK(s.current_turn == mover);
                ++safety_plies;
                continue;
            }

            Position selected = choices[next_random() % (unsigned)choice_count];
            int expected = oracle_flips(&s, mover, selected);
            int actual = execute_move(&s, selected);
            int black, white;
            CHECK(expected > 0);
            CHECK(actual == expected);
            CHECK(s.move_count == before.move_count + 1);
            CHECK(s.history[s.move_count - 1].player == mover);
            CHECK(s.history[s.move_count - 1].pos.row == selected.row);
            CHECK(s.history[s.move_count - 1].pos.col == selected.col);
            CHECK(s.board[selected.row][selected.col] == (CellState)mover);
            count_pieces(&s, &black, &white);
            CHECK(black + white == 4 + s.move_count);
            CHECK(switch_turn(&s) != TURN_GAME_OVER || is_game_over(&s));
            ++safety_plies;
        }

        CHECK(safety_plies <= MAX_MOVES + 2);
        CHECK(s.move_count >= 0 && s.move_count <= MAX_MOVES);
        CHECK(is_game_over(&s));
        CHECK(get_game_result(&s) == RESULT_BLACK_WIN ||
              get_game_result(&s) == RESULT_WHITE_WIN ||
              get_game_result(&s) == RESULT_TIE);

        for (int step = 0; step <= s.move_count; ++step) {
            GameState rebuilt;
            CHECK(rebuild_state_at(&s, step, &rebuilt));
            CHECK(rebuilt.move_count == step);
            if (step == s.move_count) {
                CHECK(memcmp(rebuilt.board, s.board, sizeof s.board) == 0);
                CHECK(rebuilt.current_turn == s.current_turn);
            }
        }
    }
}

static void test_rebuild_rejects_invalid_history(void)
{
    GameState s, out, before;
    new_game(&s);
    memset(&out, 0x3C, sizeof out);
    before = out;
    CHECK(!rebuild_state_at(NULL, 0, &out));
    CHECK(!rebuild_state_at(&s, -1, &out));
    CHECK(!rebuild_state_at(&s, 1, &out));
    CHECK(!rebuild_state_at(&s, 0, NULL));
    CHECK(!rebuild_state_at(&s, 0, &s));
    CHECK(memcmp(&out, &before, sizeof out) == 0);

    s.move_count = 1;
    s.history[0] = (MoveRecord){PLAYER_BLACK, {0, 0}};
    CHECK(!rebuild_state_at(&s, 1, &out));
    CHECK(memcmp(&out, &before, sizeof out) == 0);

    new_game(&s);
    s.move_count = MAX_MOVES + 1;
    CHECK(!rebuild_state_at(&s, 0, &out));
}

int main(void)
{
    test_argument_safety();
    test_opening_and_error_messages();
    test_all_capture_directions();
    test_eight_direction_move_and_atomic_rejection();
    test_initialization_and_capacity_guards();
    test_pass_and_terminal_state();
    test_rebuild_rejects_invalid_history();
    test_random_full_games();

    if (failures != 0) {
        fprintf(stderr, "\nFAILED: %lu of %lu checks failed.\n", failures, checks);
        return EXIT_FAILURE;
    }
    printf("PASS: %lu checks passed; %d deterministic random games completed.\n",
           checks, 1000);
    return EXIT_SUCCESS;
}
