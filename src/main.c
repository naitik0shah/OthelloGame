#include "common.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0 ||
            strcmp(argv[1], "version") == 0) {
            printf("Othello %s\n", OTHELLO_VERSION);
            return 0;
        }
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "help") == 0) {
            printf("Othello %s\n\n"
                   "Usage: %s [options]\n\n"
                   "Options:\n"
                   "  -v, --version  Show version information\n"
                   "  -h, --help     Show this help message\n",
                   OTHELLO_VERSION, argv[0]);
            return 0;
        }
        fprintf(stderr, "Unknown option: %s\nRun '%s --help' for usage.\n", argv[1], argv[0]);
        return 1;
    }

    static App app;
    app.hints = true;
    app.cursor = (Position){BOARD_SIZE / 2 - 1, BOARD_SIZE / 2 - 1};
    set_status(&app, false, "Type :n for a new game, :h for help, :q to quit.");

    if (!term_init()) {
        fprintf(stderr, "Cannot set up the terminal.\n");
        return 1;
    }
    atexit(term_restore);

    while (!app.quit) {
        const Screen s = app_screen(&app, NULL, NULL);
        render(&s);
        const InputEvent ev = term_read_event();
        if (ev.kind == EV_QUIT) break;
        app_handle_event(&app, ev);
    }
    return 0;
}
