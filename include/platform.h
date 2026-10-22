#ifndef PLATFORM_H
#define PLATFORM_H

/* Raw key/mouse input and ANSI setup are NOT in the C17 standard library.
   Implement in platform_posix.c (termios) and platform_win.c (Windows Console API,
   VT processing enabled) behind this one interface. */

typedef enum { EVT_NONE = 0, EVT_KEY, EVT_MOUSE_CLICK } EventType;

#define KEY_ESC       27
#define KEY_ENTER     '\n'
#define KEY_BACKSPACE 127

typedef struct {
    EventType type;
    int key;                      /* EVT_KEY: character code */
    int screen_row, screen_col;   /* EVT_MOUSE_CLICK: 0-indexed terminal cell */
} InputEvent;

void       platform_init(void);        /* raw mode, hide cursor, enable mouse + ANSI */
void       platform_shutdown(void);    /* restore terminal (also call from atexit) */
InputEvent platform_read_event(void);  /* blocking */
void       platform_clear_screen(void);

#endif /* PLATFORM_H */
