#ifndef CLI_H
#define CLI_H

#include "common.h"

#define MAX_STAT_FILES 32

typedef enum {
    CMD_NONE = 0, CMD_NEW, CMD_LOAD, CMD_SAVE, CMD_VIEW,
    CMD_STATS, CMD_HELP, CMD_QUIT, CMD_INVALID
} CommandType;

typedef enum { WHITE_UNSET = 0, WHITE_IS_P1, WHITE_IS_P2 } WhiteChoice;

/* Result of parsing one ":..." line. Parsing never touches game state. */
typedef struct {
    CommandType type;
    bool        has_p1, has_p2;
    char        p1[MAX_NAME_LEN], p2[MAX_NAME_LEN];       /* :n -p1= -p2= */
    WhiteChoice white;                                    /* :n -w=p1|p2 */
    bool        has_path;
    char        path[MAX_PATH_LEN];                       /* -f= for :l :s :v */
    char        files[MAX_STAT_FILES][MAX_PATH_LEN];      /* :s <f1> <f2> ... */
    int         file_count;
    char        error[96];                                /* set when type == CMD_INVALID */
} ParsedCommand;

/* Parses ":n -p1=\"A\" -p2=\"B\" -w=p1", ":l -f=\"x\"", ... Returns false on syntax error
   (out->type = CMD_INVALID, out->error filled). Names/paths longer than the limits are errors. */
bool cli_parse_command(const char *input, ParsedCommand *out);

/* Fills GameMeta from cmd, interactively prompting only for missing fields
   (names; white player defaults to P2 when -w is absent). */
void cli_prompt_new_game(const ParsedCommand *cmd, GameMeta *meta);

/* "e3"/"E3" -> {2,4}. Returns false for anything else. */
bool parse_position(const char *text, Position *out);

/* {2,4} -> "e3" (out needs 3 bytes) */
void position_to_string(Position pos, char out[3]);

#endif /* CLI_H */
