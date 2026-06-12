#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *bundle = "/opt/host-gui/host-python-repl";
static const char *program = "/opt/host-gui/host-python-repl/bin/host-python-repl";

static void set_default_env(const char *name, const char *value) {
    const char *existing = getenv(name);

    if (!existing || !existing[0])
        setenv(name, value, 1);
}

static void redirect_log(void) {
    int fd = open("/tmp/host-gui-host-python-repl.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0)
        return;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

int main(int argc, char **argv) {
    char **child_argv;
    int i;

    set_default_env("XDG_RUNTIME_DIR", "/tmp");
    set_default_env("WAYLAND_DISPLAY", "wayland-0");
    set_default_env("GDK_BACKEND", "wayland");
    set_default_env("QT_QPA_PLATFORM", "wayland");
    set_default_env("SDL_VIDEODRIVER", "wayland");
    set_default_env("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt");

    redirect_log();
    fprintf(stderr, "host-python-repl-launcher: starting program=%s\n", program);
    fflush(stderr);
    if (chdir(bundle) != 0)
        fprintf(stderr, "host-python-repl-launcher: chdir %s failed: %s\n",
                bundle, strerror(errno));

    child_argv = calloc((size_t)argc + 1, sizeof(*child_argv));
    if (!child_argv) {
        fprintf(stderr, "host-python-repl-launcher: out of memory\n");
        return 127;
    }

    child_argv[0] = (char *)program;
    for (i = 1; i < argc; i++)
        child_argv[i] = argv[i];

    execv(program, child_argv);
    fprintf(stderr, "host-python-repl-launcher: exec %s failed: %s\n",
            program, strerror(errno));
    return 127;
}
