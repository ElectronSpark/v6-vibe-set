#include <stdio.h>
#include <stdlib.h>
#include <readline/history.h>
#include <readline/readline.h>

int main(void) {
    HIST_ENTRY *entry;

    setenv("TERM", "xterm", 0);
    setenv("TERMINFO", "/usr/share/terminfo", 0);

    rl_readline_name = "xv6-host-readline-probe";
    rl_catch_signals = 0;
    rl_initialize();
    using_history();
    add_history("alpha");
    add_history("beta");

    if (history_length < 2) {
        fprintf(stderr, "host_readline_probe: history too short\n");
        return 1;
    }
    entry = history_get(history_base + history_length - 1);
    if (entry == NULL || entry->line == NULL) {
        fprintf(stderr, "host_readline_probe: missing history entry\n");
        return 1;
    }
    if (rl_variable_bind("editing-mode", "emacs") != 0) {
        fprintf(stderr, "host_readline_probe: rl_variable_bind failed\n");
        return 1;
    }

    clear_history();
    printf("host_readline_probe: OK\n");
    return 0;
}
