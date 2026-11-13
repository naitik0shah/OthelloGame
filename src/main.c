#include "common.h"

int main(void) {
    static App app;
    app.hints = true;
    app.cursor = (Position){ BOARD_SIZE / 2 - 1, BOARD_SIZE / 2 - 1 };
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
