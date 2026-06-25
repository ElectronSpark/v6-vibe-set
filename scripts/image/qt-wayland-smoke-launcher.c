#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_all(int fd, const char *text)
{
    size_t off = 0;
    size_t len = strlen(text);

    while (off < len) {
        ssize_t n = write(fd, text + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if (n == 0)
            return;
        off += (size_t)n;
    }
}

static void write_diag(const char *text)
{
    int fd = open("/tmp/qt-wayland-smoke.diag",
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    if (fd < 0)
        return;
    write_all(fd, text);
    close(fd);
}

static void set_kde_wayland_env(void)
{
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("SHELL", "/bin/sh", 1);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("XDG_CACHE_HOME", "/dev/shm/kde-cache", 1);
    setenv("XDG_CONFIG_HOME", "/dev/shm/kde-config", 1);
    setenv("XDG_DATA_HOME", "/dev/shm/kde-data", 1);
    setenv("XDG_STATE_HOME", "/dev/shm/kde-state", 1);
    setenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share:/share", 1);
    setenv("XDG_CONFIG_DIRS",
           "/etc/xdg:/usr/share/kubuntu-default-settings/kf5-settings", 1);
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_SESSION_ID", "1", 1);
    setenv("XDG_SEAT", "seat0", 1);
    setenv("XDG_VTNR", "1", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    setenv("DISPLAY", ":0", 0);
    setenv("KDE_FULL_SESSION", "true", 1);
    setenv("KDE_SESSION_VERSION", "5", 1);
    setenv("QT_QPA_PLATFORM", "wayland", 1);
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 0);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("LD_LIBRARY_PATH",
           "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:"
           "/usr/lib:/lib/x86_64-linux-gnu:/lib",
           1);
    setenv("LD_PRELOAD",
           "/usr/lib/x86_64-linux-gnu/libKF5Codecs.so.5:"
           "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0",
           0);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri", 1);
    setenv("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 0);
    setenv("GALLIUM_DRIVER", "virgl", 0);
}

static int read_smoke_pid(pid_t *pid_out)
{
    FILE *fp = fopen("/tmp/qt-wayland-smoke.pid", "r");
    long value;

    if (!fp)
        return -1;
    if (fscanf(fp, "%ld", &value) != 1 || value <= 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *pid_out = (pid_t)value;
    return 0;
}

static int wait_until_gone(pid_t pid, int attempts)
{
    for (int i = 0; i < attempts; i++) {
        if (kill(pid, 0) < 0 && errno == ESRCH)
            return 0;
        usleep(100000);
    }
    return -1;
}

static int stop_smoke(void)
{
    pid_t pid;

    if (read_smoke_pid(&pid) < 0) {
        fprintf(stderr, "qt-wayland-smoke-launcher: stop pidfile failed: %s\n",
                strerror(errno));
        return 1;
    }
    if (kill(pid, SIGTERM) < 0 && errno != ESRCH) {
        fprintf(stderr, "qt-wayland-smoke-launcher: stop SIGTERM pid=%ld failed: %s\n",
                (long)pid, strerror(errno));
        return 1;
    }
    if (wait_until_gone(pid, 30) == 0) {
        printf("qt-wayland-smoke-launcher: stopped pid=%ld\n", (long)pid);
        return 0;
    }
    if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
        fprintf(stderr, "qt-wayland-smoke-launcher: stop SIGKILL pid=%ld failed: %s\n",
                (long)pid, strerror(errno));
        return 1;
    }
    if (wait_until_gone(pid, 30) == 0) {
        printf("qt-wayland-smoke-launcher: killed pid=%ld\n", (long)pid);
        return 0;
    }
    fprintf(stderr, "qt-wayland-smoke-launcher: stop still-running pid=%ld\n",
            (long)pid);
    return 1;
}

int main(int argc, char **argv)
{
    const char *qml = "/opt/xv6-kde/qt-wayland-smoke.qml";
    char *const child_argv[] = { "qmlscene", (char *)qml, NULL };
    pid_t pid;
    FILE *pidf;
    int fd;

    if (argc == 2 && strcmp(argv[1], "--stop") == 0)
        return stop_smoke();

    set_kde_wayland_env();
    write_diag("qt-wayland-smoke-launcher: parent-before-fork\n");

    pid = fork();
    if (pid < 0) {
        write_diag("qt-wayland-smoke-launcher: fork-failed\n");
        fprintf(stderr, "qt-wayland-smoke-launcher: fork failed: %s\n",
                strerror(errno));
        return 1;
    }
    if (pid > 0) {
        write_diag("qt-wayland-smoke-launcher: parent-after-fork\n");
        pidf = fopen("/tmp/qt-wayland-smoke.pid", "w");
        if (pidf) {
            fprintf(pidf, "%ld\n", (long)pid);
            fclose(pidf);
        }
        printf("qt-wayland-smoke-launcher: started pid=%ld\n", (long)pid);
        return 0;
    }

    write_diag("qt-wayland-smoke-launcher: child-start\n");
    pidf = fopen("/tmp/qt-wayland-smoke.pid", "w");
    if (pidf) {
        fprintf(pidf, "%ld\n", (long)getpid());
        fclose(pidf);
    }
    write_diag("qt-wayland-smoke-launcher: child-after-pidfile\n");

    fd = open("/tmp/qt-wayland-smoke.log",
              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        write_diag("qt-wayland-smoke-launcher: child-open-log-failed\n");
        fprintf(stderr, "qt-wayland-smoke-launcher: open log failed: %s\n",
                strerror(errno));
        return 1;
    }
    write_diag("qt-wayland-smoke-launcher: child-after-open-log\n");
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        write_diag("qt-wayland-smoke-launcher: child-dup2-failed\n");
        fprintf(stderr, "qt-wayland-smoke-launcher: dup2 failed: %s\n",
                strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);
    write_diag("qt-wayland-smoke-launcher: child-before-exec\n");

    write_all(STDERR_FILENO,
              "qt-wayland-smoke-launcher: exec /usr/bin/qmlscene "
              "/opt/xv6-kde/qt-wayland-smoke.qml\n");
    execv("/usr/bin/qmlscene", child_argv);
    write_diag("qt-wayland-smoke-launcher: child-exec-failed\n");
    fprintf(stderr, "qt-wayland-smoke-launcher: exec failed: %s\n",
            strerror(errno));
    return 127;
}
