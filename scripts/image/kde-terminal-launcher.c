#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void launch_terminal(const char *path, const char *name,
                            int argc, char **argv)
{
    int command_argc = argc > 1 ? argc - 1 : 2;
    char *terminal_argv[command_argc + 3];
    int out = 0;

    terminal_argv[out++] = (char *)name;
    terminal_argv[out++] = "-e";
    if (argc > 1) {
        for (int i = 1; i < argc; i++)
            terminal_argv[out++] = argv[i];
    } else {
        terminal_argv[out++] = "/bin/bash";
        terminal_argv[out++] = "-i";
    }
    terminal_argv[out] = NULL;

    execv(path, terminal_argv);
    fprintf(stderr, "kde_terminal_launcher: exec %s failed errno=%d %s\n",
            name, errno, strerror(errno));
}

int main(int argc, char **argv)
{
    setenv("SHELL", "/bin/bash", 1);
    setenv("TERM", "xterm-256color", 0);

    if (access("/usr/bin/xterm", X_OK) == 0) {
        launch_terminal("/usr/bin/xterm", "/usr/bin/xterm", argc, argv);
    }

    if (access("/usr/bin/qterminal", X_OK) == 0) {
        launch_terminal("/usr/bin/qterminal", "qterminal", argc, argv);
    }

    fprintf(stderr, "kde_terminal_launcher: no terminal emulator available\n");
    return 127;
}
