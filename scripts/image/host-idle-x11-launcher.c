#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    const char *program = "/opt/host-gui/host-idle/host-idle";
    char **child_argv = calloc((size_t)argc + 1, sizeof(char *));
    int logfd;

    if (!child_argv) {
        fprintf(stderr, "host-idle-x11-launcher: out of memory\n");
        return 127;
    }
    child_argv[0] = (char *)program;
    for (int i = 1; i < argc; i++)
        child_argv[i] = argv[i];

    setenv("DISPLAY", getenv("DISPLAY") ? getenv("DISPLAY") : ":0", 1);
    setenv("XDG_RUNTIME_DIR",
           getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "/tmp", 1);
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("GDK_BACKEND");

    logfd = open("/tmp/host-idle-x11.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd >= 0) {
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        if (logfd > STDERR_FILENO)
            close(logfd);
    }

    fprintf(stderr, "host-idle-x11-launcher: exec %s DISPLAY=%s\n",
            program, getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
    fflush(stderr);
    execv(program, child_argv);
    fprintf(stderr, "host-idle-x11-launcher: exec %s failed: %s\n",
            program, strerror(errno));
    return 127;
}
