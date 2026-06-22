#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    setenv("LD_PRELOAD", "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0", 0);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri", 1);
    setenv("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 0);
    setenv("GALLIUM_DRIVER", "virgl", 0);
}

int main(void)
{
    const char *qml = "/opt/xv6-kde/qt-wayland-smoke.qml";
    char *const argv[] = { "qmlscene", (char *)qml, NULL };
    FILE *pidf;
    int fd;

    set_kde_wayland_env();

    pidf = fopen("/tmp/qt-wayland-smoke.pid", "w");
    if (pidf) {
        fprintf(pidf, "%ld\n", (long)getpid());
        fclose(pidf);
    }

    fd = open("/tmp/qt-wayland-smoke.log",
              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "qt-wayland-smoke-launcher: open log failed: %s\n",
                strerror(errno));
        return 1;
    }
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        fprintf(stderr, "qt-wayland-smoke-launcher: dup2 failed: %s\n",
                strerror(errno));
        close(fd);
        return 1;
    }
    close(fd);

    fprintf(stderr, "qt-wayland-smoke-launcher: exec /usr/bin/qmlscene %s\n",
            qml);
    execv("/usr/bin/qmlscene", argv);
    fprintf(stderr, "qt-wayland-smoke-launcher: exec failed: %s\n",
            strerror(errno));
    return 127;
}
