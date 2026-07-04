#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PLASMASHELL_IMMEDIATE_EXIT_MS 8000
#define KDE_AUDIO_STATUS_LOG "/kde-audio-status.log"

static void mkdir_one(const char *path, mode_t mode)
{
    if (mkdir(path, mode) < 0 && errno != EEXIST)
        fprintf(stderr, "kde-plasma-session-child: mkdir %s: %s\n",
                path, strerror(errno));
    if (chmod(path, mode) < 0)
        fprintf(stderr, "kde-plasma-session-child: chmod %s: %s\n",
                path, strerror(errno));
}

static int cmdline_has_flag(const char *flag)
{
    FILE *fp;
    char buf[4096];
    char *save = NULL;
    char *tok;

    fp = fopen("/proc/cmdline", "r");
    if (!fp)
        return 0;
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    for (tok = strtok_r(buf, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
        if (strcmp(tok, flag) == 0)
            return 1;
    }
    return 0;
}

static int env_is_enabled(const char *name)
{
    const char *value = getenv(name);

    if (!value || !*value)
        return 0;
    return strcmp(value, "0") != 0 && strcasecmp(value, "false") != 0 &&
           strcasecmp(value, "no") != 0 && strcasecmp(value, "off") != 0;
}

static int pactl_readiness_probe_enabled(void)
{
    return cmdline_has_flag("kde_pactl_probe=1") ||
           env_is_enabled("KDE_PACTL_READINESS_PROBE");
}

static void audio_status(const char *fmt, ...)
{
    FILE *fp;
    va_list ap;

    fprintf(stderr, "kde-plasma-session-child: audio ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);

    fp = fopen(KDE_AUDIO_STATUS_LOG, "a");
    if (!fp)
        return;

    fprintf(fp, "kde_audio_status ");
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

static void seed_pulse_cookie_file(const char *path)
{
    unsigned char cookie[256];
    struct stat st;
    int fd;

    if (stat(path, &st) == 0 && st.st_size == (off_t)sizeof(cookie)) {
        chmod(path, 0600);
        return;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        fprintf(stderr, "kde-plasma-session-child: open %s: %s\n",
                path, strerror(errno));
        return;
    }

    for (size_t i = 0; i < sizeof(cookie); i++)
        cookie[i] = (unsigned char)(0x5a ^ (i * 37u) ^ (i >> 1));

    size_t done = 0;
    while (done < sizeof(cookie)) {
        ssize_t n = write(fd, cookie + done, sizeof(cookie) - done);
        if (n <= 0) {
            fprintf(stderr, "kde-plasma-session-child: write %s: %s\n",
                    path, n < 0 ? strerror(errno) : "short write");
            break;
        }
        done += (size_t)n;
    }
    if (close(fd) < 0)
        fprintf(stderr, "kde-plasma-session-child: close %s: %s\n",
                path, strerror(errno));
    chmod(path, 0600);
}

static void seed_pulse_cookie(void)
{
    mkdir_one("/root/.config", 0700);
    mkdir_one("/root/.config/pulse", 0700);
    mkdir_one("/dev/shm/kde-config", 0700);
    mkdir_one("/dev/shm/kde-config/pulse", 0700);
    seed_pulse_cookie_file("/dev/shm/kde-config/pulse/cookie");
    seed_pulse_cookie_file("/root/.config/pulse/cookie");
    seed_pulse_cookie_file("/root/.pulse-cookie");
}

static void terminate_child(pid_t pid)
{
    int status;

    if (pid <= 0)
        return;
    if (waitpid(pid, &status, WNOHANG) == pid)
        return;
    kill(pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return;
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static pid_t spawn_plasmashell(char *const argv[])
{
    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "kde-plasma-session-child: fork plasmashell: %s\n",
                strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr,
                "kde-plasma-session-child: exec plasmashell failed: %s\n",
                strerror(errno));
        _exit(127);
    }
    fprintf(stderr, "kde-plasma-session-child: plasmashell pid=%ld\n",
            (long)pid);
    return pid;
}

static int wait_for_plasmashell_logged(char *const argv[])
{
    long long start_ms = monotonic_ms();
    long long lifetime_ms;
    int status;
    pid_t pid = spawn_plasmashell(argv);

    if (pid < 0)
        return 127;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr,
                    "kde-plasma-session-child: wait plasmashell: %s\n",
                    strerror(errno));
            return 127;
        }
    }

    lifetime_ms = monotonic_ms() - start_ms;
    if (WIFEXITED(status)) {
        fprintf(stderr,
                "kde-plasma-session-child: plasmashell exited status=%d lifetime_ms=%lld immediate=%d\n",
                WEXITSTATUS(status), lifetime_ms,
                lifetime_ms < PLASMASHELL_IMMEDIATE_EXIT_MS);
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr,
                "kde-plasma-session-child: plasmashell signaled signal=%d lifetime_ms=%lld immediate=%d\n",
                WTERMSIG(status), lifetime_ms,
                lifetime_ms < PLASMASHELL_IMMEDIATE_EXIT_MS);
        return 128 + WTERMSIG(status);
    }

    fprintf(stderr,
            "kde-plasma-session-child: plasmashell stopped status=%d lifetime_ms=%lld immediate=%d\n",
            status, lifetime_ms, lifetime_ms < PLASMASHELL_IMMEDIATE_EXIT_MS);
    return 127;
}

static int wait_for_pipewire_core(void);
static int wait_for_pulse_server(void);
static int wait_for_pactl_entry(char *const argv[], const char *needle,
                                const char *label);

static void run_optional(char *const argv[], int wait_for_exit, int timeout_ms)
{
    pid_t pid = fork();
    int status;
    int waited_ms = 0;

    if (access(argv[0], X_OK) < 0)
        return;

    if (pid < 0) {
        fprintf(stderr, "kde-plasma-session-child: fork %s: %s\n",
                argv[0], strerror(errno));
        return;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(errno == ENOENT ? 0 : 127);
    }
    if (!wait_for_exit)
        return;

    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (timeout_ms > 0 && waited_ms >= timeout_ms) {
            fprintf(stderr, "kde-plasma-session-child: %s timed out after %dms\n",
                    argv[0], timeout_ms);
            terminate_child(pid);
            return;
        }
        usleep(100000);
        waited_ms += 100;
    }
}

static void run_audio_services(char *const pipewire[],
                               char *const wireplumber[],
                               char *const pipewire_pulse[])
{
    const char *ld_library_path = getenv("LD_LIBRARY_PATH");
    const char *ld_preload = getenv("LD_PRELOAD");
    char *saved_ld_library_path = ld_library_path ? strdup(ld_library_path) : NULL;
    char *saved_ld_preload = ld_preload ? strdup(ld_preload) : NULL;
    int core_ready;
    int pulse_ready;
    int pactl_enabled = pactl_readiness_probe_enabled();

    audio_status("phase=start status=START pactl_probe=%s",
                 pactl_enabled ? "enabled" : "skipped");
    unsetenv("LD_LIBRARY_PATH");
    unsetenv("LD_PRELOAD");
    run_optional(pipewire, 0, 0);
    core_ready = wait_for_pipewire_core();
    run_optional(wireplumber, 0, 0);
    core_ready = wait_for_pipewire_core() || core_ready;
    run_optional(pipewire_pulse, 0, 0);
    pulse_ready = wait_for_pulse_server();
    audio_status("phase=socket status=%s pipewire_core=%d pulse_socket=%d",
                 pulse_ready ? "PASS" : "FAIL", core_ready, pulse_ready);
    if (!pactl_enabled) {
        audio_status("phase=pactl status=SKIPPED reason=nonessential-readiness-probe enable=kde_pactl_probe=1");
    } else {
        int sink_ready;
        int monitor_ready;

        sink_ready = wait_for_pactl_entry((char *const[]){ "/usr/bin/pactl",
                                                           "list", "short",
                                                           "sinks", NULL },
                                          "alsa_output.xv6_virtio", "sink");
        monitor_ready = wait_for_pactl_entry((char *const[]){ "/usr/bin/pactl",
                                                              "list", "short",
                                                              "sources", NULL },
                                             "alsa_output.xv6_virtio.monitor",
                                             "monitor");
        audio_status("phase=pactl status=%s sink=%d monitor=%d",
                     sink_ready && monitor_ready ? "PASS" : "FAIL",
                     sink_ready, monitor_ready);
    }

    if (saved_ld_library_path) {
        setenv("LD_LIBRARY_PATH", saved_ld_library_path, 1);
        free(saved_ld_library_path);
    }
    if (saved_ld_preload) {
        setenv("LD_PRELOAD", saved_ld_preload, 1);
        free(saved_ld_preload);
    }
}

static int wait_for_pipewire_core(void)
{
    const char *path = "/dev/shm/xdg-runtime-root/pipewire-0";

    for (int i = 0; i < 50; i++) {
        struct stat st;

        if (stat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
            usleep(1000000);
            return 1;
        }
        usleep(100000);
    }
    fprintf(stderr, "kde-plasma-session-child: PipeWire core not ready after grace\n");
    return 0;
}

static int wait_for_pulse_server(void)
{
    const char *path = "/dev/shm/xdg-runtime-root/pulse/native";

    for (int i = 0; i < 80; i++) {
        struct stat st;

        if (stat(path, &st) == 0 && S_ISSOCK(st.st_mode)) {
            usleep(500000);
            return 1;
        }
        usleep(100000);
    }
    fprintf(stderr, "kde-plasma-session-child: PulseAudio compatibility socket not ready after grace\n");
    return 0;
}

static int run_pactl_probe_once(char *const argv[], const char *needle)
{
    char buf[4096];
    int fds[2];
    pid_t pid;
    int status = 0;
    int waited_ms = 0;
    ssize_t total = 0;

    if (pipe(fds) < 0)
        return 0;

    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return 0;
    }

    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        if (fds[1] > STDERR_FILENO)
            close(fds[1]);
        execv(argv[0], argv);
        _exit(127);
    }

    close(fds[1]);
    while (waitpid(pid, &status, WNOHANG) == 0) {
        if (waited_ms >= 2000) {
            terminate_child(pid);
            close(fds[0]);
            return 0;
        }
        usleep(100000);
        waited_ms += 100;
    }

    for (;;) {
        ssize_t n = read(fds[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0)
            break;
        total += n;
        if (total >= (ssize_t)sizeof(buf) - 1)
            break;
    }
    close(fds[0]);
    buf[total] = '\0';

    return WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
           strstr(buf, needle) != NULL;
}

static int wait_for_pactl_entry(char *const argv[], const char *needle,
                                const char *label)
{
    for (int i = 0; i < 60; i++) {
        if (run_pactl_probe_once(argv, needle)) {
            fprintf(stderr,
                    "kde-plasma-session-child: PipeWire Pulse %s ready: %s\n",
                    label, needle);
            return 1;
        }
        usleep(250000);
    }

    fprintf(stderr,
            "kde-plasma-session-child: PipeWire Pulse %s missing after grace: %s\n",
            label, needle);
    return 0;
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
    setenv("XDG_CONFIG_DIRS", "/etc/xdg:/usr/share/kubuntu-default-settings/kf5-settings", 1);
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_SESSION_ID", "1", 1);
    setenv("XDG_SEAT", "seat0", 1);
    setenv("XDG_VTNR", "1", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
    setenv("KDE_FULL_SESSION", "true", 1);
    setenv("KDE_SESSION_VERSION", "5", 1);
    setenv("KWIN_COMPOSE", "O2ES", 1);
    setenv("KWIN_OPENGL_INTERFACE", "egl", 1);
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
    setenv("PIPEWIRE_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("PIPEWIRE_NO_RT", "1", 1);
    setenv("PULSE_COOKIE", "/dev/shm/kde-config/pulse/cookie", 1);
    setenv("PULSE_SERVER", "unix:/dev/shm/xdg-runtime-root/pulse/native", 1);
    seed_pulse_cookie();
}

int main(void)
{
    char *dbus_env[] = {
        "/usr/bin/dbus-update-activation-environment",
        "HOME",
        "USER",
        "LOGNAME",
        "SHELL",
        "XDG_RUNTIME_DIR",
        "XDG_CACHE_HOME",
        "XDG_CONFIG_HOME",
        "XDG_DATA_HOME",
        "XDG_STATE_HOME",
        "XDG_DATA_DIRS",
        "XDG_CONFIG_DIRS",
        "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_DESKTOP",
        "XDG_SESSION_TYPE",
        "XDG_SESSION_ID",
        "XDG_SEAT",
        "XDG_VTNR",
        "WAYLAND_DISPLAY",
        "KDE_FULL_SESSION",
        "KDE_SESSION_VERSION",
        "KWIN_COMPOSE",
        "KWIN_OPENGL_INTERFACE",
        "QT_QPA_PLATFORM",
        "DBUS_SYSTEM_BUS_ADDRESS",
        "DBUS_SESSION_BUS_ADDRESS",
        "PATH",
        "LD_LIBRARY_PATH",
        "LIBGL_DRIVERS_PATH",
        "GBM_BACKENDS_PATH",
        "MESA_LOADER_DRIVER_OVERRIDE",
        "GALLIUM_DRIVER",
        "PIPEWIRE_RUNTIME_DIR",
        "PIPEWIRE_NO_RT",
        "PULSE_COOKIE",
        "PULSE_SERVER",
        NULL
    };
    char *sycoca[] = { "/usr/bin/kbuildsycoca5", "--noincremental", NULL };
    char *kded[] = { "/usr/bin/kded5", NULL };
    char *activity[] = { "/usr/lib/x86_64-linux-gnu/libexec/kactivitymanagerd", NULL };
    char *pipewire[] = { "/usr/bin/pipewire", NULL };
    char *wireplumber[] = { "/usr/bin/wireplumber", NULL };
    char *pipewire_pulse[] = { "/usr/bin/pipewire-pulse", NULL };
    char *plasmashell[] = { "/usr/bin/plasmashell", NULL };

    signal(SIGCHLD, SIG_DFL);
    set_kde_env();
    unlink(KDE_AUDIO_STATUS_LOG);
    fprintf(stderr, "kde-plasma-session-child: starting Plasma services\n");

    run_optional(dbus_env, 1, 5000);
    run_optional(sycoca, 1, 15000);
    if (cmdline_has_flag("kde_audio=0")) {
        fprintf(stderr, "kde-plasma-session-child: audio services disabled by kde_audio=0\n");
        audio_status("phase=disabled status=SKIPPED reason=kde_audio=0");
    } else {
        fprintf(stderr, "kde-plasma-session-child: starting audio services\n");
        run_audio_services(pipewire, wireplumber, pipewire_pulse);
    }
    run_optional(kded, 0, 0);
    run_optional(activity, 0, 0);

    return wait_for_plasmashell_logged(plasmashell);
}
