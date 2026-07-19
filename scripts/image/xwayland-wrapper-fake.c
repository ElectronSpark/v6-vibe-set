#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    const char *help_mode = getenv("XV6_FAKE_XWAYLAND_HELP_MODE");

    if (argc == 2 && strcmp(argv[1], "-help") == 0) {
        if (help_mode && strcmp(help_mode, "exact") == 0)
            fputs("usage: Xwayland [options]\n  -glamor MODE\n", stderr);
        else if (help_mode && strcmp(help_mode, "substring") == 0)
            fputs("usage: Xwayland [options]\n  -glamorized-output MODE\n",
                  stderr);
        else
            fputs("usage: Xwayland [options]\n", stderr);
        return 0;
    }

    for (int i = 0; i < argc; i++)
        fprintf(stderr, "fake-Xwayland: argv[%d]=%s\n", i, argv[i]);
    return 0;
}
