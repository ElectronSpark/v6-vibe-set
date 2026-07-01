#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    setenv("SHELL", "/bin/bash", 1);
    setenv("TERM", "xterm-256color", 0);

    if (access("/usr/bin/xterm", X_OK) == 0) {
        execl("/usr/bin/xterm", "/usr/bin/xterm",
              "-e", "/bin/bash", "-i", NULL);
        fprintf(stderr, "kde_terminal_launcher: exec xterm failed errno=%d %s\n",
                errno, strerror(errno));
    }

    if (access("/usr/bin/qterminal", X_OK) == 0) {
        execl("/usr/bin/qterminal", "qterminal",
              "-e", "/bin/bash", "-i",
              NULL);
        fprintf(stderr,
                "kde_terminal_launcher: exec qterminal failed errno=%d %s\n",
                errno, strerror(errno));
    }

    fprintf(stderr, "kde_terminal_launcher: no terminal emulator available\n");
    return 127;
}
