#include "common.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    const char *path = "test_game_storage_tmp.sav";
    GameMeta m = {"Alice", "Bob", PLAYER_BLACK};
    GameState g, loaded;
    init_game(&g, &m);
    execute_move(&g, (Position){2, 3});
    switch_turn(&g);
    assert(save_game(&g, path) == STORAGE_OK);
    assert(load_game(&loaded, path) == STORAGE_OK);
    assert(loaded.move_count == 1);
    assert(strcmp(loaded.meta.player1_name, "Alice") == 0);
    remove(path);
    return 0;
}
