#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *bundle = "/opt/host-gui/host-x11-egl-smoke";
static const char *loader =
    "/opt/host-gui/host-x11-egl-smoke/lib/ld-linux-x86-64.so.2";
static const char *library_path =
    "/opt/host-gui/host-x11-egl-smoke/lib:"
    "/lib:/lib/x86_64-linux-gnu:/lib64:"
    "/usr/lib:/usr/lib/x86_64-linux-gnu:/usr/lib64";
static const char *program =
    "/opt/host-gui/host-x11-egl-smoke/bin/host-x11-egl-smoke";
static const char *default_log_path = "/tmp/host-gui-host-x11-egl-smoke.log";
#define PROBE_CHILD_TIMEOUT_MS 30000

static void
set_default_env(const char *name, const char *value)
{
    const char *existing = getenv(name);

    if (!existing || !existing[0])
        setenv(name, value, 1);
}

static int
bounded_probe_requested(int argc, char **argv)
{
    const char *mode = getenv("HOST_X11_EGL_SMOKE_MODE");

    if (mode &&
        (strcmp(mode, "x11-connect") == 0 ||
         strcmp(mode, "glx-probe") == 0 ||
         strcmp(mode, "glx-fps") == 0))
        return 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--x11-connect-only") == 0 ||
            strcmp(argv[i], "--glx-probe-only") == 0 ||
            strcmp(argv[i], "--glx-fps") == 0)
            return 1;
    }
    return 0;
}

static void
redirect_log(void)
{
    const char *env_log_path = getenv("HOST_X11_EGL_SMOKE_LOG");
    const char *log_path = env_log_path && env_log_path[0]
                               ? env_log_path
                               : default_log_path;
    int fd;

    fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
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
    int bounded_probe;
    int waited_ms = 0;

    set_default_env("XDG_RUNTIME_DIR", "/tmp");
    set_default_env("DISPLAY", ":0");
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("GDK_BACKEND");
    setenv("EGL_PLATFORM", "x11", 1);
    setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
    setenv("GALLIUM_DRIVER", "virgl", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 1);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri", 1);
    setenv("LD_LIBRARY_PATH", library_path, 1);

    redirect_log();
    fprintf(stderr,
            "host-x11-egl-smoke-launcher: starting program=%s\n",
            program);
    fprintf(stderr,
            "host-x11-egl-smoke-launcher: env XDG_RUNTIME_DIR=%s DISPLAY=%s EGL_PLATFORM=%s\n",
            getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)",
            getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)",
            getenv("EGL_PLATFORM") ? getenv("EGL_PLATFORM") : "(unset)");
    fflush(stderr);

    if (chdir(bundle) != 0)
        fprintf(stderr, "host-x11-egl-smoke-launcher: chdir %s failed: %s\n",
                bundle, strerror(errno));

    child_argv = calloc((size_t)argc + 5, sizeof(*child_argv));
    if (!child_argv) {
        fprintf(stderr, "host-x11-egl-smoke-launcher: out of memory\n");
        return 127;
    }

    child_argv[0] = (char *)loader;
    child_argv[1] = "--library-path";
    child_argv[2] = (char *)library_path;
    child_argv[3] = (char *)program;
    for (int i = 1; i < argc; i++)
        child_argv[i + 3] = argv[i];
    bounded_probe = bounded_probe_requested(argc, argv);

    fprintf(stderr,
            "host-x11-egl-smoke-launcher: phase=child_spawn status=BEGIN bounded=%d timeout_ms=%d\n",
            bounded_probe, bounded_probe ? PROBE_CHILD_TIMEOUT_MS : 0);
    fflush(stderr);
    pid = fork();
    if (pid < 0) {
        fprintf(stderr,
                "host-x11-egl-smoke-launcher: phase=fork status=FAIL errno=%d %s\n",
                errno, strerror(errno));
        return 127;
    }

    if (pid == 0) {
        setpgid(0, 0);
        fprintf(stderr,
                "host-x11-egl-smoke-launcher: phase=child_exec status=BEGIN path=%s\n",
                loader);
        fflush(stderr);
        execv(loader, child_argv);
        fprintf(stderr,
                "host-x11-egl-smoke-launcher: phase=exec status=FAIL path=%s errno=%d %s\n",
                loader, errno, strerror(errno));
        fflush(stderr);
        _exit(127);
    }
    setpgid(pid, pid);
    fprintf(stderr,
            "host-x11-egl-smoke-launcher: phase=child_spawn status=PASS pid=%ld bounded=%d\n",
            (long)pid, bounded_probe);
    fflush(stderr);

    for (;;) {
        pid_t got = waitpid(pid, &status, bounded_probe ? WNOHANG : 0);

        if (got == pid)
            break;
        if (got < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr,
                    "host-x11-egl-smoke-launcher: phase=wait status=FAIL errno=%d %s\n",
                    errno, strerror(errno));
            return 127;
        }
        if (!bounded_probe)
            continue;
        if (waited_ms >= PROBE_CHILD_TIMEOUT_MS) {
            rc = 124;
            fprintf(stderr,
                    "host-x11-egl-smoke-launcher: phase=child_exit status=TIMEOUT pid=%ld exit_status=%d timeout_ms=%d\n",
                    (long)pid, rc, PROBE_CHILD_TIMEOUT_MS);
            fflush(stderr);
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                ;
            return rc;
        }
        usleep(100000);
        waited_ms += 100;
    }

    if (WIFEXITED(status)) {
        rc = WEXITSTATUS(status);
        fprintf(stderr,
                "host-x11-egl-smoke-launcher: phase=child_exit status=%s exit_status=%d\n",
                rc == 0 ? "PASS" : "FAIL", rc);
        return rc;
    }

    if (WIFSIGNALED(status)) {
        rc = 128 + WTERMSIG(status);
        fprintf(stderr,
                "host-x11-egl-smoke-launcher: phase=child_exit status=FAIL signal=%d exit_status=%d\n",
                WTERMSIG(status), rc);
        return rc;
    }

    fprintf(stderr,
            "host-x11-egl-smoke-launcher: phase=child_exit status=FAIL reason=unknown-wait-status raw_status=%d exit_status=127\n",
            status);
    return 127;
}
