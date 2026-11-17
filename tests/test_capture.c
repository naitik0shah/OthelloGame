#include "common.h"
#include <assert.h>
int main(void)
{
    GameMeta m = {"Black", "White", PLAYER_BLACK};
    GameState g;
    init_game(&g, &m);
    assert(execute_move(&g, (Position){2, 3}) == 1);
    assert(g.board[3][3] == CELL_BLACK);
    assert(g.board[2][3] == CELL_BLACK);
    assert(g.move_count == 1);
    assert(switch_turn(&g) == TURN_SWITCHED);
    assert(g.current_turn == PLAYER_WHITE);
    return 0;
}
