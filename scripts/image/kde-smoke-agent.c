#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static FILE *log_file;

static void log_msg(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);

    if (!log_file)
        return;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    fprintf(log_file, "\n");
    fflush(log_file);
    va_end(ap);
}

static void set_kde_env(void)
{
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("SHELL", "/bin/bash", 1);
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
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
    setenv("DISPLAY", ":0", 0);
    setenv("KDE_FULL_SESSION", "true", 1);
    setenv("KDE_SESSION_VERSION", "5", 1);
    setenv("QT_QPA_PLATFORM", "wayland", 1);
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 0);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("LD_LIBRARY_PATH",
           "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:"
           "/lib/x86_64-linux-gnu:/usr/lib:/lib",
           1);
    setenv("LD_PRELOAD",
           "/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so:"
           "/usr/lib/x86_64-linux-gnu/libKF5Codecs.so.5:"
           "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0",
           0);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri", 1);
    setenv("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 0);
    setenv("GALLIUM_DRIVER", "virgl", 0);
}

static int has_arg(int argc, char **argv, const char *needle)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], needle) == 0)
            return 1;
    }
    return 0;
}

static void log_argv(const char *label, char *const argv[])
{
    printf("kde_smoke_agent step=%s argv=", label);
    if (log_file)
        fprintf(log_file, "kde_smoke_agent step=%s argv=", label);
    for (int i = 0; argv[i] != NULL; i++) {
        printf("%s%s", i ? " " : "", argv[i]);
        if (log_file)
            fprintf(log_file, "%s%s", i ? " " : "", argv[i]);
    }
    printf("\n");
    fflush(stdout);
    if (log_file) {
        fprintf(log_file, "\n");
        fflush(log_file);
    }
}

static int run_argv(const char *label, int required, char *const argv[])
{
    pid_t pid;
    int status;

    log_argv(label, argv);
    pid = fork();
    if (pid < 0) {
        log_msg("kde_smoke_agent step=%s result=FAIL fork_errno=%d %s",
                label, errno, strerror(errno));
        return 0;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr, "kde_smoke_agent exec_failed step=%s path=%s errno=%d %s\n",
                label, argv[0], errno, strerror(errno));
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        log_msg("kde_smoke_agent step=%s result=FAIL wait_errno=%d %s",
                label, errno, strerror(errno));
        return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log_msg("kde_smoke_agent step=%s result=%s status=0x%x",
                label, required ? "FAIL" : "WARN", status);
        return 0;
    }
    log_msg("kde_smoke_agent step=%s result=PASS", label);
    return 1;
}

static int wait_for_process_probe(const char *args, int timeout_s)
{
    time_t deadline = time(NULL) + timeout_s;
    char *audio_argv[] = { "/bin/kde-process-probe", "--require-audio",
                           NULL };

    (void)args;
    while (time(NULL) <= deadline) {
        if (run_argv("process-wait", 0, audio_argv))
            return 1;
        sleep(1);
    }
    return 0;
}

static int mouseinject_abs(const char *label, int x, int y, int buttons)
{
    char xbuf[32];
    char ybuf[32];
    char buttonsbuf[32];
    char *argv[] = { "/bin/mouseinject", xbuf, ybuf, buttonsbuf, NULL };

    snprintf(xbuf, sizeof(xbuf), "%d", x);
    snprintf(ybuf, sizeof(ybuf), "%d", y);
    snprintf(buttonsbuf, sizeof(buttonsbuf), "%d", buttons);
    return run_argv(label, 1, argv);
}

static int click_icon(const char *name, int x, int y)
{
    int ok = 1;

    ok &= mouseinject_abs(name, x, y, 0);
    ok &= mouseinject_abs(name, x, y, 1);
    ok &= mouseinject_abs(name, x, y, 0);
    return ok;
}

static int capture_fb(const char *label, const char *path)
{
    char *argv[] = { "/bin/fbstat", "ppm-current", (char *)path, "0", "0",
                     "1280", "800", NULL };

    return run_argv(label, 1, argv);
}

static int run_probe_suite(void)
{
    int ok = 1;
    char *libinput[] = { "/bin/kde-libinput-probe", NULL };
    char *dlopen[] = { "/bin/kde-dlopen-probe", NULL };
    char *unix_socket[] = { "/bin/kde-unix-socket-probe", NULL };
    char *drm[] = { "/bin/kde-drm-probe", NULL };
    char *mountinfo[] = { "/bin/kde-proc-mountinfo-probe", NULL };
    char *config_atomic[] = { "/bin/kde-config-atomic-probe", NULL };
    char *pulse_cookie[] = { "/bin/kde-pulse-cookie-probe", NULL };
    char *proc_comm[] = { "/bin/kde-proc-comm-probe", NULL };
    char *pty_shell[] = { "/bin/kde-pty-shell-probe", NULL };
    char *pty_openpty[] = { "/bin/kde-pty-openpty-probe", NULL };
    char *trash_stat[] = { "/bin/kde-trash-stat-probe", NULL };
    char *kwriteconfig[] = { "/bin/kde-kwriteconfig-probe", NULL };
    char *wayland_seat[] = { "/bin/kde-wayland-seat-probe", NULL };
    char *kwin_screenshot[] = { "/bin/kde-kwin-screenshot-probe", NULL };

    ok &= run_argv("libinput-probe", 1, libinput);
    ok &= run_argv("dlopen-probe", 1, dlopen);
    ok &= run_argv("unix-socket-probe", 1, unix_socket);
    ok &= run_argv("drm-probe", 1, drm);
    ok &= run_argv("proc-mountinfo-probe", 1, mountinfo);
    ok &= run_argv("config-atomic-probe", 1, config_atomic);
    ok &= run_argv("pulse-cookie-probe", 1, pulse_cookie);
    ok &= run_argv("proc-comm-probe", 1, proc_comm);
    ok &= run_argv("pty-shell-probe", 1, pty_shell);
    ok &= run_argv("pty-openpty-probe", 1, pty_openpty);
    ok &= run_argv("trash-stat-probe", 1, trash_stat);
    ok &= run_argv("kwriteconfig-probe", 1, kwriteconfig);
    ok &= run_argv("wayland-seat-probe", 1, wayland_seat);
    run_argv("kwin-screenshot-probe", 0, kwin_screenshot);
    return ok;
}

static void write_agent_status(const char *status)
{
    FILE *fp = fopen("/tmp/kde-smoke-agent.status", "w");

    if (fp) {
        fputs(status, fp);
        fclose(fp);
    }
    fp = fopen("/tmp/kde-smoke-agent.done", "w");
    if (fp) {
        fputs(status, fp);
        fclose(fp);
    }
}

int main(int argc, char **argv)
{
    int require_chromium = has_arg(argc, argv, "--require-chromium");
    int ok = 1;
    char *app_plain[] = { "/bin/kde-app-launch-probe", NULL };
    char *app_chromium[] = { "/bin/kde-app-launch-probe",
                             "--require-chromium", NULL };
    char *process_apps_plain[] = {
        "/bin/kde-process-probe",
        "--require-apps",
        "--require-audio",
        NULL
    };
    char *process_apps_chromium[] = {
        "/bin/kde-process-probe",
        "--require-apps",
        "--require-audio",
        "--require-chromium",
        NULL
    };

    log_file = fopen("/tmp/kde-smoke-agent.log", "a");
    set_kde_env();
    unlink("/tmp/kde-smoke-agent.done");
    unlink("/tmp/kde-smoke-agent.status");

    log_msg("KDE_SMOKE_AGENT_START require_chromium=%d", require_chromium);

    if (!wait_for_process_probe("--require-audio", 180)) {
        log_msg("kde_smoke_agent step=wait-kde-audio result=FAIL");
        ok = 0;
    }

    sleep(8);
    ok &= capture_fb("fb-before", "/kde-plasma-before.ppm");

    ok &= click_icon("click-chromium", 3000, 3300);
    ok &= click_icon("click-dolphin", 8900, 3300);
    ok &= click_icon("click-kwrite", 14850, 3300);
    ok &= click_icon("click-konsole", 20800, 3300);
    sleep(12);
    ok &= capture_fb("fb-after-clicks", "/kde-plasma-after.ppm");

    ok &= run_argv("direct-app-launch-probe", 1,
                   require_chromium ? app_chromium : app_plain);
    ok &= run_argv("apps-process-probe", 1,
                   require_chromium ? process_apps_chromium :
                                      process_apps_plain);
    ok &= run_probe_suite();

    if (ok) {
        write_agent_status("PASS");
        log_msg("KDE_SMOKE_AGENT_DONE status=PASS");
    } else {
        write_agent_status("FAIL");
        log_msg("KDE_SMOKE_AGENT_DONE status=FAIL");
    }
    if (log_file)
        fclose(log_file);
    return ok ? 0 : 2;
}
