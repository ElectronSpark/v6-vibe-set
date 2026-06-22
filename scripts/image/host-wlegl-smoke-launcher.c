#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *bundle = "/opt/host-gui/host-wlegl-smoke";
static const char *loader =
    "/opt/host-gui/host-wlegl-smoke/lib/ld-linux-x86-64.so.2";
static const char *library_path =
    "/opt/host-gui/host-wlegl-smoke/lib:"
    "/lib:/lib/x86_64-linux-gnu:/lib64:"
    "/usr/lib:/usr/lib/x86_64-linux-gnu:/usr/lib64";
static const char *program =
    "/opt/host-gui/host-wlegl-smoke/bin/host-wlegl-smoke";

static void set_default_env(const char *name, const char *value)
{
    const char *existing = getenv(name);

    if (!existing || !existing[0])
        setenv(name, value, 1);
}

static void redirect_log(void)
{
    int fd = open("/tmp/host-gui-host-wlegl-smoke.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0)
        return;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

int main(int argc, char **argv)
{
    char **child_argv;
    int i;

    set_default_env("XDG_RUNTIME_DIR", "/tmp");
    set_default_env("WAYLAND_DISPLAY", "wayland-0");
    setenv("LD_LIBRARY_PATH", library_path, 1);

    redirect_log();
    fprintf(stderr,
            "host-wlegl-smoke-launcher: starting program=%s XDG_RUNTIME_DIR=%s WAYLAND_DISPLAY=%s\n",
            program,
            getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)",
            getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)");
    fflush(stderr);

    if (chdir(bundle) != 0)
        fprintf(stderr, "host-wlegl-smoke-launcher: chdir %s failed: %s\n",
                bundle, strerror(errno));

    child_argv = calloc((size_t)argc + 5, sizeof(*child_argv));
    if (!child_argv) {
        fprintf(stderr, "host-wlegl-smoke-launcher: out of memory\n");
        return 127;
    }

    child_argv[0] = (char *)loader;
    child_argv[1] = "--library-path";
    child_argv[2] = (char *)library_path;
    child_argv[3] = (char *)program;
    for (i = 1; i < argc; i++)
        child_argv[i + 3] = argv[i];

    execv(loader, child_argv);
    fprintf(stderr, "host-wlegl-smoke-launcher: exec %s failed: %s\n",
            loader, strerror(errno));
    return 127;
}
