#include "common.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    Position p;
    char square[3];
    assert(parse_position("D3", &p));
    assert(p.row == 2 && p.col == 3);
    position_to_string(p, square);
    assert(strcmp(square, "d3") == 0);
    assert(!parse_position("z9", &p));
    assert(clamp_int(12, 0, 8) == 8);
    return 0;
}
