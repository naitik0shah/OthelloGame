#ifndef OTHELLO_VERSION
#define OTHELLO_VERSION "v1.2.2"
#endif

#include <stdbool.h>
#include <stddef.h>

/* Board dimensions and data limits used throughout the application. */
#ifndef OTHELLO_COMMON_H
#define OTHELLO_COMMON_H

enum {
    BOARD_SIZE = 8,      /**< Number of rows and columns on the Othello board. */
    MAX_MOVES = 60,      /**< Maximum number of played moves in a complete game. */
    MAX_NAME_LEN = 32,   /**< Maximum length of a stored player name, including NUL. */
    MAX_PATH_LEN = 256,  /**< Maximum length of a storage or replay path. */
    MAX_STAT_FILES = 32, /**< Maximum number of files accepted by statistics commands. */
    MAX_TOKENS = 40,     /**< Maximum number of whitespace-separated command tokens. */
    MAX_LINE_LEN = 2048, /**< Maximum length of an input or file line. */
    MAX_MSG_LEN = 400,   /**< Maximum length of an application status message. */
    HISTORY_ROWS = 8,    /**< Number of move-history rows visible on the board. */
    NUM_DIRECTIONS = 8,  /**< Number of adjacent directions used for capture checks. */
    ERROR_TEXT_LEN = 96  /**< Maximum length of a parsed-command error string. */
};

/** State of one board square. */
typedef enum { CELL_EMPTY, CELL_BLACK, CELL_WHITE } CellState;

/** Player identity.  Enum values match the corresponding board cell states. */
typedef enum { PLAYER_BLACK = CELL_BLACK, PLAYER_WHITE = CELL_WHITE } PlayerColor;

/** Zero-based coordinates of a board square. */
typedef struct {
    int row; /**< Row index, from 0 (top) to BOARD_SIZE - 1. */
    int col; /**< Column index, from 0 (left) to BOARD_SIZE - 1. */
} Position;

/** One move in the game's chronological history. */
typedef struct {
    PlayerColor player; /**< Player who placed the piece. */
    Position pos;       /**< Board square occupied by the move. */
} MoveRecord;

/** Names and color assignment that describe a game session. */
typedef struct {
    char player1_name[MAX_NAME_LEN]; /**< Display name of player one. */
    char player2_name[MAX_NAME_LEN]; /**< Display name of player two. */
    PlayerColor p1_color;            /**< Cell color assigned to player one. */
} GameMeta;

/** Complete mutable state of an Othello game. */
typedef struct {
    CellState board[BOARD_SIZE][BOARD_SIZE]; /**< Current board contents. */
    PlayerColor current_turn;                /**< Player whose turn is active. */
    MoveRecord history[MAX_MOVES];           /**< Ordered list of completed moves. */
    int move_count;                          /**< Number of entries in history. */
    GameMeta meta;                           /**< Names and color assignment. */
} GameState;

/** Final or in-progress game outcome. */
typedef enum {
    RESULT_IN_PROGRESS,
    RESULT_BLACK_WIN,
    RESULT_WHITE_WIN,
    RESULT_TIE,
    ERR_RESULT_INVALID
} GameResult;

/** Result of processing a turn after a move or pass. */
typedef enum { TURN_SWITCHED, TURN_OPPONENT_PASSED, TURN_GAME_OVER } TurnStatus;

/** Classification of an invalid move. */
typedef enum { MOVE_OK, MOVE_ERR_OCCUPIED, MOVE_ERR_NO_CAPTURE } MoveError;

/** Result of loading or saving a game file. */
typedef enum {
    STORAGE_OK,
    STORAGE_ERR_IO,
    STORAGE_ERR_FORMAT,
    STORAGE_ERR_ILLEGAL_MOVE
} StorageStatus;

/** Command types recognized by the command parser. */
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

/** Fully parsed command-line input and its optional arguments. */
typedef struct {
    CommandType type;                         /**< Kind of command requested. */
    char p1[MAX_NAME_LEN];                    /**< Optional first player name. */
    char p2[MAX_NAME_LEN];                    /**< Optional second player name. */
    bool white_is_p1;                         /**< True if player one starts as White. */
    char path[MAX_PATH_LEN];                  /**< Path supplied to load, save, or replay. */
    char files[MAX_STAT_FILES][MAX_PATH_LEN]; /**< Paths supplied to statistics commands. */
    int file_count;                           /**< Number of valid paths in files. */
    char error[ERROR_TEXT_LEN];               /**< Human-readable parse error, if any. */
} ParsedCommand;

/** Result details for one completed saved game. */
typedef struct {
    bool is_tie;                    /**< True when the game ended in a draw. */
    PlayerColor winner_color;       /**< Color played by the winner. */
    char winner_name[MAX_NAME_LEN]; /**< Name of the winner; empty for a draw. */
    int winner_score;               /**< Winner's final piece count. */
    int opponent_score;             /**< Other player's final piece count. */
    int black_score;                /**< Black's final piece count (used for draws). */
    int white_score;                /**< White's final piece count (used for draws). */
} GameStat;

/** Results of each completed game in the supplied save files. */
typedef struct {
    int total_games;                /**< Number of completed games successfully read. */
    int skipped_files;              /**< Number of unreadable, invalid, or unfinished files. */
    GameStat games[MAX_STAT_FILES]; /**< Individual game results in input-file order. */
} StatsSummary;

/** Fixed terminal layout constants. */
enum {
    BOARD_TOP = 3,
    BOARD_LEFT = 3,
    CELL_WIDTH = 3,
    HISTORY_LEFT = 30,
    FRAME_SIZE = 16384,
    STATUS_WIDTH = 76
};

/** Event kinds returned by the terminal input platform. */
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

/** A keyboard, mouse, or synthetic terminal event. */
typedef struct {
    EventKind kind; /**< Category of input event. */
    int ch;         /**< Character value for EV_CHAR events. */
    int row;        /**< Terminal row for mouse events. */
    int col;        /**< Terminal column for mouse events. */
} InputEvent;

/** Immutable content snapshot used by the renderer. */
typedef struct {
    const GameState *state;   /**< Active game, or NULL for the welcome screen. */
    Position cursor;          /**< Currently selected board square. */
    bool show_cursor;         /**< Whether to draw the keyboard cursor. */
    bool hints;               /**< Whether to show legal-move markers. */
    const char *tag;          /**< Optional replay or context label. */
    const char *status;       /**< Optional status message. */
    bool status_error;        /**< Whether status describes an error. */
    const char *input_prompt; /**< Optional active command prompt. */
    const char *input_text;   /**< Text currently entered at the prompt. */
    const char *help;         /**< Optional short help text. */
} Screen;

/** Application state owned by the main event loop. */
typedef struct {
    GameState game;           /**< Current game state. */
    bool have_game;           /**< Whether game contains an active session. */
    bool hints;               /**< Current legal-move hint setting. */
    bool quit;                /**< Set when the application should exit. */
    Position cursor;          /**< Keyboard cursor position. */
    char status[MAX_MSG_LEN]; /**< Latest status or feedback message. */
    bool status_error;        /**< Whether status is an error message. */
} App;

/* ===== COMMON / SHARED UTILITIES ===== */
/** Return true when p lies on the 8x8 board. */
bool pos_in_bounds(Position p);
/** Return the opposing player color. */
PlayerColor opponent_of(PlayerColor p);
/** Parse text in chess-like board notation (for example, "d3") into a position. */
bool parse_position(const char *text, Position *out);
/** Convert a position into a three-byte string such as "d3". */
void position_to_string(Position pos, char out[3]);
/** Get the display name assigned to c in game g. */
const char *player_name(const GameState *g, PlayerColor c);
/** Get the human-readable name of c ("Black" or "White"). */
const char *color_name(PlayerColor c);
/** Trim leading and trailing whitespace from s in place. */
void trim(char *s);
/** Format printf-style text into an existing bounded character buffer. */
void append_fmt(char *buf, size_t size, const char *fmt, ...);
/** Clamp v to the inclusive range [lo, hi]. */
int clamp_int(int v, int lo, int hi);

/* ===== VALIDATION / CAPTURE TASK ===== */
/** Count consecutive opponent pieces captured from pos in direction dir. */
int count_flips(const GameState *s, PlayerColor player, Position pos, Position dir);
/** Validate a move and return a move-specific result code. */
MoveError check_move(const GameState *s, PlayerColor player, Position pos);
/** Get a human-readable description of err. */
const char *move_error_str(MoveError err);
/** Return true if player may place a piece at pos. */
bool validate_move(const GameState *s, PlayerColor player, Position pos);
/** Fill valid_out with every legal move for player and return the move count. */
int get_all_valid_moves(const GameState *s, PlayerColor player,
                        bool valid_out[BOARD_SIZE][BOARD_SIZE]);
/** Return true if player has at least one legal move. */
bool has_any_valid_move(const GameState *s, PlayerColor player);
/** Return true when neither player has any remaining legal move. */
bool is_game_over(const GameState *s);
/** Initialize s with the standard opening board and supplied game metadata. */
void init_game(GameState *s, const GameMeta *meta);
/** Apply a legal move at pos and return the number of pieces captured. */
int execute_move(GameState *s, Position pos);
/** Switch turns, passing to the opponent or ending the game when appropriate. */
TurnStatus switch_turn(GameState *s);
/** Fill black and white with the number of pieces of each color. */
void count_pieces(const GameState *s, int *black, int *white);
/** Calculate whether Black wins, White wins, or the game is tied. */
GameResult get_game_result(const GameState *s);
/** Rebuild a game state after step moves; return true on success. */
bool rebuild_state_at(const GameState *src, int step, GameState *out);

/* ===== STORAGE TASK ===== */
/** Save a complete game to path and return an I/O or format status. */
StorageStatus save_game(const GameState *s, const char *path);
/** Load and validate a saved game at path into state. */
StorageStatus load_game(GameState *state, const char *path);
/** Get a human-readable description of a storage status code. */
const char *storage_status_str(StorageStatus st);

/* ===== STATISTICS TASK ===== */
/** Aggregate game outcomes across count saved-game paths. */
void compute_stats_summary(const char *const paths[], int count, StatsSummary *out);
/** Print a formatted statistics summary to the active text output. */
void print_stats_summary(const StatsSummary *s);

/* ===== COMMAND / CLI / APP TASKS ===== */
/** Parse input into a command and populate p1/p2, path, or files as applicable. */
bool parse_command(const char *input, ParsedCommand *out);
/** Set application feedback text, optionally marking it as an error. */
void set_status(App *a, bool is_error, const char *fmt, ...);
/** Build a renderer snapshot from application state and optional prompt text. */
Screen app_screen(const App *a, const char *prompt, const char *text);
/** Apply one terminal input event to the application state and command loop. */
void app_handle_event(App *a, InputEvent ev);

/* ===== UI / PLATFORM TASK ===== */
/** Initialize terminal mode and input/output capabilities. */
bool term_init(void);
/** Restore the terminal to the state it had before term_init(). */
void term_restore(void);
/** Write s through the active terminal output implementation. */
void term_write(const char *s);
/** Read and classify the next keyboard or mouse event. */
InputEvent term_read_event(void);
/** Render a complete screen snapshot and flush it to the terminal. */
void render(const Screen *s);

#endif /* OTHELLO_COMMON_H */
