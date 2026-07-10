#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *root = "/opt/host-gui/chromium-pulse-stream-reducer";
static const char *loader =
    "/opt/host-gui/chromium-pulse-stream-reducer/lib/ld-linux-x86-64.so.2";
static const char *program =
    "/opt/host-gui/chromium-pulse-stream-reducer/bin/chromium-pulse-stream-reducer";
static const char *library_path =
    "/opt/host-gui/chromium-pulse-stream-reducer/lib:"
    "/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu/pulseaudio:"
    "/lib/x86_64-linux-gnu:/lib:/usr/lib";

int main(int argc, char **argv)
{
    char **child_argv;
    int index;

    child_argv = calloc((size_t)argc + 4, sizeof(*child_argv));
    if (!child_argv) {
        fprintf(stderr,
                "chromium-pulse-stream-reducer-launcher: allocation failed\n");
        return 125;
    }
    child_argv[0] = (char *)loader;
    child_argv[1] = (char *)"--library-path";
    child_argv[2] = (char *)library_path;
    child_argv[3] = (char *)program;
    for (index = 1; index < argc; index++)
        child_argv[index + 3] = argv[index];
    child_argv[argc + 3] = NULL;

    fprintf(stderr,
            "chromium-pulse-stream-reducer-launcher: phase=exec root=%s program=%s argc=%d\n",
            root, program, argc);
    fflush(stderr);
    execv(loader, child_argv);
    fprintf(stderr,
            "chromium-pulse-stream-reducer-launcher: phase=exec status=FAIL errno=%d error=%s\n",
            errno, strerror(errno));
    free(child_argv);
    return 127;
}
