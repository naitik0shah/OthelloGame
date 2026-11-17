#include "common.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    GameMeta m = {"Black", "White", PLAYER_BLACK};
    GameState g;
    init_game(&g, &m);
    assert(g.move_count == 0);
    assert(validate_move(&g, PLAYER_BLACK, (Position){2, 3}));
    assert(!validate_move(&g, PLAYER_BLACK, (Position){0, 0}));
    bool legal[BOARD_SIZE][BOARD_SIZE];
    assert(get_all_valid_moves(&g, PLAYER_BLACK, legal) == 4);
    return 0;
}
