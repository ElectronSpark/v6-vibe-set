#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *bundle = "/opt/host-gui/host-egl-gbm-gl-smoke";
static const char *loader =
    "/opt/host-gui/host-egl-gbm-gl-smoke/lib/ld-linux-x86-64.so.2";
static const char *library_path =
    "/opt/host-gui/host-egl-gbm-gl-smoke/lib:"
    "/lib:/lib/x86_64-linux-gnu:/lib64:"
    "/usr/lib:/usr/lib/x86_64-linux-gnu:/usr/lib64";
static const char *program =
    "/opt/host-gui/host-egl-gbm-gl-smoke/bin/host-egl-gbm-gl-smoke";

static void
set_default_env(const char *name, const char *value)
{
    const char *existing = getenv(name);

    if (!existing || !existing[0])
        setenv(name, value, 1);
}

static void
redirect_log(void)
{
    const char *stdio = getenv("HOST_EGL_GBM_SMOKE_STDIO");
    int fd;

    if (stdio && stdio[0] && strcmp(stdio, "0") != 0)
        return;
    fd = open("/tmp/host-gui-host-egl-gbm-gl-smoke.log",
              O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    (void)lseek(fd, 0, SEEK_END);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

int
main(int argc, char **argv)
{
    char **child_argv;
    pid_t pid;
    int status = 0;
    int rc;

    set_default_env("XDG_RUNTIME_DIR", "/tmp");
    set_default_env("EGL_PLATFORM", "gbm");
    set_default_env("GALLIUM_DRIVER", "virgl");
    set_default_env("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu");
    set_default_env("LIBGL_DRIVERS_PATH",
                    "/lib/dri:/usr/lib/x86_64-linux-gnu/dri");
    set_default_env("GBM_BACKENDS_PATH",
                    "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm");
    setenv("LD_LIBRARY_PATH", library_path, 1);

    redirect_log();
    fprintf(stderr,
            "host-egl-gbm-gl-smoke-launcher: starting program=%s\n",
            program);
    fflush(stderr);

    if (chdir(bundle) != 0)
        fprintf(stderr,
                "host-egl-gbm-gl-smoke-launcher: chdir %s failed: %s\n",
                bundle, strerror(errno));

    child_argv = calloc((size_t)argc + 5, sizeof(*child_argv));
    if (!child_argv) {
        fprintf(stderr, "host-egl-gbm-gl-smoke-launcher: out of memory\n");
        return 127;
    }

    child_argv[0] = (char *)loader;
    child_argv[1] = "--library-path";
    child_argv[2] = (char *)library_path;
    child_argv[3] = (char *)program;
    for (int i = 1; i < argc; i++)
        child_argv[i + 3] = argv[i];

    pid = fork();
    if (pid < 0) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke-launcher: phase=fork status=FAIL errno=%d %s\n",
                errno, strerror(errno));
        return 127;
    }

    if (pid == 0) {
        execv(loader, child_argv);
        fprintf(stderr,
                "host-egl-gbm-gl-smoke-launcher: phase=exec status=FAIL path=%s errno=%d %s\n",
                loader, errno, strerror(errno));
        fflush(stderr);
        _exit(127);
    }

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        fprintf(stderr,
                "host-egl-gbm-gl-smoke-launcher: phase=wait status=FAIL errno=%d %s\n",
                errno, strerror(errno));
        return 127;
    }

    if (WIFEXITED(status)) {
        rc = WEXITSTATUS(status);
        fprintf(stderr,
                "host-egl-gbm-gl-smoke-launcher: phase=child_exit status=%s exit_status=%d\n",
                rc == 0 ? "PASS" : "FAIL", rc);
        return rc;
    }

    if (WIFSIGNALED(status)) {
        rc = 128 + WTERMSIG(status);
        fprintf(stderr,
                "host-egl-gbm-gl-smoke-launcher: phase=child_exit status=FAIL signal=%d exit_status=%d\n",
                WTERMSIG(status), rc);
        return rc;
    }

    fprintf(stderr,
            "host-egl-gbm-gl-smoke-launcher: phase=child_exit status=FAIL reason=unknown-wait-status raw_status=%d exit_status=127\n",
            status);
    return 127;
}
