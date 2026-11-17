#ifndef OTHELLO_VERSION
#define OTHELLO_VERSION "dev"
#endif

#ifndef OTHELLO_COMMON_H
#define OTHELLO_COMMON_H

#include <stdbool.h>
#include <stddef.h>

enum {
    BOARD_SIZE = 8,
    MAX_MOVES = 60,
    MAX_NAME_LEN = 32,
    MAX_PATH_LEN = 256,
    MAX_STAT_FILES = 32,
    MAX_TOKENS = 40,
    MAX_LINE_LEN = 2048,
    MAX_MSG_LEN = 400,
    HISTORY_ROWS = 8,
    NUM_DIRECTIONS = 8,
    ERROR_TEXT_LEN = 96
};

typedef enum { CELL_EMPTY, CELL_BLACK, CELL_WHITE } CellState;
typedef enum { PLAYER_BLACK = CELL_BLACK, PLAYER_WHITE = CELL_WHITE } PlayerColor;
typedef struct {
    int row;
    int col;
} Position;
typedef struct {
    PlayerColor player;
    Position pos;
} MoveRecord;
typedef struct {
    char player1_name[MAX_NAME_LEN];
    char player2_name[MAX_NAME_LEN];
    PlayerColor p1_color;
} GameMeta;
typedef struct {
    CellState board[BOARD_SIZE][BOARD_SIZE];
    PlayerColor current_turn;
    MoveRecord history[MAX_MOVES];
    int move_count;
    GameMeta meta;
} GameState;
typedef enum { RESULT_IN_PROGRESS, RESULT_BLACK_WIN, RESULT_WHITE_WIN, RESULT_TIE } GameResult;
typedef enum { TURN_SWITCHED, TURN_OPPONENT_PASSED, TURN_GAME_OVER } TurnStatus;
typedef enum { MOVE_OK, MOVE_ERR_OCCUPIED, MOVE_ERR_NO_CAPTURE } MoveError;
typedef enum {
    STORAGE_OK,
    STORAGE_ERR_IO,
    STORAGE_ERR_FORMAT,
    STORAGE_ERR_ILLEGAL_MOVE
} StorageStatus;
typedef enum {
    CMD_INVALID,
    CMD_NEW,
    CMD_LOAD,
    CMD_SAVE,
    CMD_VIEW,
    CMD_STATS,
    CMD_HELP,
    CMD_QUIT,
    CMD_TOGGLE_HINTS
} CommandType;
typedef struct {
    CommandType type;
    char p1[MAX_NAME_LEN];
    char p2[MAX_NAME_LEN];
    bool white_is_p1;
    char path[MAX_PATH_LEN];
    char files[MAX_STAT_FILES][MAX_PATH_LEN];
    int file_count;
    char error[ERROR_TEXT_LEN];
} ParsedCommand;
typedef struct {
    int total_games;
    int skipped_files;
    int black_wins;
    int white_wins;
    int ties;
    int max_score;
    char highest_scoring_player[MAX_NAME_LEN];
} StatsSummary;

enum {
    BOARD_TOP = 3,
    BOARD_LEFT = 3,
    CELL_WIDTH = 3,
    HISTORY_LEFT = 30,
    FRAME_SIZE = 16384,
    STATUS_WIDTH = 76
};
typedef enum {
    EV_NONE,
    EV_CHAR,
    EV_ENTER,
    EV_BACKSPACE,
    EV_ESC,
    EV_UP,
    EV_DOWN,
    EV_LEFT,
    EV_RIGHT,
    EV_MOUSE_PRESS,
    EV_MOUSE_MOVE,
    EV_QUIT
} EventKind;
typedef struct {
    EventKind kind;
    int ch;
    int row;
    int col;
} InputEvent;
typedef struct {
    const GameState *state;
    Position cursor;
    bool show_cursor;
    bool hints;
    const char *tag;
    const char *status;
    bool status_error;
    const char *input_prompt;
    const char *input_text;
    const char *help;
} Screen;
typedef struct {
    GameState game;
    bool have_game;
    bool hints;
    bool quit;
    Position cursor;
    char status[MAX_MSG_LEN];
    bool status_error;
} App;

/* ===== COMMON / SHARED UTILITIES ===== */
bool pos_in_bounds(Position p);
PlayerColor opponent_of(PlayerColor p);
bool parse_position(const char *text, Position *out);
void position_to_string(Position pos, char out[3]);
const char *player_name(const GameState *g, PlayerColor c);
const char *color_name(PlayerColor c);
void trim(char *s);
void append_fmt(char *buf, size_t size, const char *fmt, ...);
int clamp_int(int v, int lo, int hi);

/* ===== VALIDATION / CAPTURE TASK ===== */
int count_flips(const GameState *s, PlayerColor player, Position pos, Position dir);
MoveError check_move(const GameState *s, PlayerColor player, Position pos);
const char *move_error_str(MoveError err);
bool validate_move(const GameState *s, PlayerColor player, Position pos);
int get_all_valid_moves(const GameState *s, PlayerColor player,
                        bool valid_out[BOARD_SIZE][BOARD_SIZE]);
bool has_any_valid_move(const GameState *s, PlayerColor player);
bool is_game_over(const GameState *s);
void init_game(GameState *s, const GameMeta *meta);
int execute_move(GameState *s, Position pos);
TurnStatus switch_turn(GameState *s);
void count_pieces(const GameState *s, int *black, int *white);
GameResult get_game_result(const GameState *s);
bool rebuild_state_at(const GameState *src, int step, GameState *out);

/* ===== STORAGE TASK ===== */
StorageStatus save_game(const GameState *s, const char *path);
StorageStatus load_game(GameState *state, const char *path);
const char *storage_status_str(StorageStatus st);

/* ===== STATISTICS TASK ===== */
void compute_stats_summary(const char *const paths[], int count, StatsSummary *out);
void print_stats_summary(const StatsSummary *s);

/* ===== COMMAND / CLI / APP TASKS ===== */
bool parse_command(const char *input, ParsedCommand *out);
void set_status(App *a, bool is_error, const char *fmt, ...);
Screen app_screen(const App *a, const char *prompt, const char *text);
void app_handle_event(App *a, InputEvent ev);

/* ===== UI / PLATFORM TASK ===== */
bool term_init(void);
void term_restore(void);
void term_write(const char *s);
InputEvent term_read_event(void);
void render(const Screen *s);

#endif /* OTHELLO_COMMON_H */
