#include "common.h"
#include <assert.h>
int main(void)
{
    ParsedCommand c;
    assert(parse_command(":n -p1=Alice -p2=Bob", &c));
    assert(c.type == CMD_NEW);
    assert(parse_command(":q", &c));
    assert(c.type == CMD_QUIT);
    assert(!parse_command(":unknown", &c));
    return 0;
}
