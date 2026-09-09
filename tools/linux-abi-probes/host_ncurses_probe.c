#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <term.h>

int main(void) {
    int err = 0;
    const char *clear_cap;
    SCREEN *screen;

    setenv("TERM", "xterm", 0);
    setenv("TERMINFO", "/usr/share/terminfo", 0);

    if (setupterm(NULL, 1, &err) != OK) {
        fprintf(stderr, "host_ncurses_probe: setupterm failed err=%d\n", err);
        return 1;
    }

    clear_cap = tigetstr("clear");
    if (clear_cap == NULL || clear_cap == (char *)-1) {
        fprintf(stderr, "host_ncurses_probe: missing clear capability\n");
        return 1;
    }

    screen = newterm("xterm", stdout, stdin);
    if (screen == NULL) {
        fprintf(stderr, "host_ncurses_probe: newterm failed\n");
        return 1;
    }
    set_term(screen);
    endwin();
    delscreen(screen);

    printf("host_ncurses_probe: OK\n");
    return 0;
}
