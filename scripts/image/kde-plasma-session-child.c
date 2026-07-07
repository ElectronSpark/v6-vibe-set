#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PLASMASHELL_IMMEDIATE_EXIT_MS 8000
#define KDE_AUDIO_STATUS_LOG "/kde-audio-status.log"
#define PLASMASHELL_CRASH_CAPTURE_LOG "/kde-plasmashell-crash-capture.log"

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

static int flag_or_env_enabled(const char *flag, const char *env)
{
    return cmdline_has_flag(flag) || env_is_enabled(env);
}

static int pactl_readiness_probe_enabled(void)
{
    return cmdline_has_flag("kde_pactl_probe=1") ||
           env_is_enabled("KDE_PACTL_READINESS_PROBE");
}

static int plasmashell_crash_capture_enabled(void)
{
    static int initialized;
    static int enabled;

    if (!initialized) {
        enabled = flag_or_env_enabled("kde_plasmashell_crash_capture=1",
                                      "KDE_PLASMASHELL_CRASH_CAPTURE");
        initialized = 1;
    }
    return enabled;
}

static int plasmashell_disable_kcrash_enabled(void)
{
    static int initialized;
    static int enabled;

    if (!initialized) {
        enabled = flag_or_env_enabled("kde_plasmashell_disable_kcrash=1",
                                      "KDE_PLASMASHELL_DISABLE_KCRASH");
        initialized = 1;
    }
    return enabled;
}

static int plasmashell_core_enabled(void)
{
    static int initialized;
    static int enabled;

    if (!initialized) {
        enabled = flag_or_env_enabled("kde_plasmashell_core=1",
                                      "KDE_PLASMASHELL_CORE");
        initialized = 1;
    }
    return enabled;
}

static const char *env_or_unset(const char *name)
{
    const char *value = getenv(name);

    return value ? value : "(unset)";
}

static void crash_capture_printf(const char *fmt, ...)
{
    FILE *fp;
    va_list ap;

    if (!plasmashell_crash_capture_enabled())
        return;

    fp = fopen(PLASMASHELL_CRASH_CAPTURE_LOG, "a");
    if (!fp)
        return;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputc('\n', fp);
    fclose(fp);
}

static void configure_plasmashell_crash_debug(void)
{
    struct rlimit core;
    int disable_kcrash = plasmashell_disable_kcrash_enabled();
    int core_enabled = plasmashell_core_enabled();
    int prctl_rc = 0;
    int prctl_errno = 0;
    int setrlimit_rc = 0;
    int setrlimit_errno = 0;
    int getrlimit_rc;

    if (disable_kcrash) {
        setenv("KDE_DEBUG", "1", 1);
        setenv("KCRASH_DUMP_ONLY", "1", 1);
        /*
         * plasmashell uses KCrash's auto-restart flag.  KDE_DEBUG disables
         * DrKonqi, but KCRASH_AUTO_RESTARTED is what delays the restart
         * handler long enough for startup crashes to reach the kernel.
         */
        setenv("KCRASH_AUTO_RESTARTED", "1", 1);
    }

    if (core_enabled) {
        if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) < 0) {
            prctl_rc = -1;
            prctl_errno = errno;
        }
        core.rlim_cur = RLIM_INFINITY;
        core.rlim_max = RLIM_INFINITY;
        if (setrlimit(RLIMIT_CORE, &core) < 0) {
            setrlimit_rc = -1;
            setrlimit_errno = errno;
        }
    }

    memset(&core, 0, sizeof(core));
    getrlimit_rc = getrlimit(RLIMIT_CORE, &core);
    crash_capture_printf(
        "plasmashell_crash_capture child_setup disable_kcrash=%d core=%d "
        "prctl_dumpable_rc=%d prctl_dumpable_errno=%d "
        "setrlimit_rc=%d setrlimit_errno=%d getrlimit_rc=%d "
        "rlimit_core_cur=%llu rlimit_core_max=%llu "
        "KDE_DEBUG=%s KCRASH_DUMP_ONLY=%s KCRASH_AUTO_RESTARTED=%s",
        disable_kcrash, core_enabled, prctl_rc, prctl_errno, setrlimit_rc,
        setrlimit_errno, getrlimit_rc, (unsigned long long)core.rlim_cur,
        (unsigned long long)core.rlim_max, env_or_unset("KDE_DEBUG"),
        env_or_unset("KCRASH_DUMP_ONLY"),
        env_or_unset("KCRASH_AUTO_RESTARTED"));
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

static void fprint_escaped_bytes(FILE *fp, const char *buf, ssize_t n)
{
    static const char hex[] = "0123456789abcdef";

    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];

        if (c == '\0') {
            fputs("\\0", fp);
        } else if (c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7e)) {
            fputc(c, fp);
        } else {
            fputs("\\x", fp);
            fputc(hex[c >> 4], fp);
            fputc(hex[c & 0xf], fp);
        }
    }
}

static void dump_proc_file_limited(FILE *fp, pid_t pid, const char *rel,
                                   size_t limit)
{
    char path[512];
    char buf[1024];
    size_t total = 0;
    int fd;

    snprintf(path, sizeof(path), "/proc/%ld/%s", (long)pid, rel);
    fprintf(fp, "plasmashell_crash_capture proc pid=%ld file=%s begin\n",
            (long)pid, rel);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(fp, "open errno=%d %s\n", errno, strerror(errno));
        fprintf(fp, "plasmashell_crash_capture proc pid=%ld file=%s end\n",
                (long)pid, rel);
        return;
    }

    for (;;) {
        size_t want = sizeof(buf);
        ssize_t n;

        if (limit > 0 && total + want > limit)
            want = limit - total;
        if (want == 0)
            break;
        n = read(fd, buf, want);
        if (n <= 0)
            break;
        fprint_escaped_bytes(fp, buf, n);
        total += (size_t)n;
        if (limit > 0 && total >= limit)
            break;
    }
    if (limit > 0 && total >= limit)
        fprintf(fp,
                "\nplasmashell_crash_capture proc pid=%ld file=%s truncated_at=%zu\n",
                (long)pid, rel, limit);
    close(fd);
    fprintf(fp, "plasmashell_crash_capture proc pid=%ld file=%s end\n",
            (long)pid, rel);
}

static void dump_fd_targets(FILE *fp, pid_t pid)
{
    char dirpath[128];
    DIR *dir;
    struct dirent *de;

    snprintf(dirpath, sizeof(dirpath), "/proc/%ld/fd", (long)pid);
    fprintf(fp, "plasmashell_crash_capture fd pid=%ld begin\n", (long)pid);
    dir = opendir(dirpath);
    if (!dir) {
        fprintf(fp, "opendir errno=%d %s\n", errno, strerror(errno));
        fprintf(fp, "plasmashell_crash_capture fd pid=%ld end\n", (long)pid);
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char linkpath[512];
        char target[256];
        int all_digits = 1;
        ssize_t n;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;
        snprintf(linkpath, sizeof(linkpath), "/proc/%ld/fd/%s",
                 (long)pid, de->d_name);
        n = readlink(linkpath, target, sizeof(target) - 1);
        if (n < 0) {
            fprintf(fp, "fd=%s readlink_errno=%d %s\n",
                    de->d_name, errno, strerror(errno));
            continue;
        }
        target[n] = '\0';
        fprintf(fp, "fd=%s target=%s\n", de->d_name, target);
    }
    closedir(dir);
    fprintf(fp, "plasmashell_crash_capture fd pid=%ld end\n", (long)pid);
}

static void dump_task_files(FILE *fp, pid_t pid)
{
    char dirpath[128];
    DIR *dir;
    struct dirent *de;

    snprintf(dirpath, sizeof(dirpath), "/proc/%ld/task", (long)pid);
    fprintf(fp, "plasmashell_crash_capture tasks pid=%ld begin\n", (long)pid);
    dir = opendir(dirpath);
    if (!dir) {
        fprintf(fp, "opendir errno=%d %s\n", errno, strerror(errno));
        fprintf(fp, "plasmashell_crash_capture tasks pid=%ld end\n", (long)pid);
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char rel[320];
        int all_digits = 1;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;
        snprintf(rel, sizeof(rel), "task/%s/status", de->d_name);
        dump_proc_file_limited(fp, pid, rel, 4096);
        snprintf(rel, sizeof(rel), "task/%s/wchan", de->d_name);
        dump_proc_file_limited(fp, pid, rel, 1024);
        snprintf(rel, sizeof(rel), "task/%s/syscall", de->d_name);
        dump_proc_file_limited(fp, pid, rel, 1024);
        snprintf(rel, sizeof(rel), "task/%s/stack", de->d_name);
        dump_proc_file_limited(fp, pid, rel, 4096);
    }
    closedir(dir);
    fprintf(fp, "plasmashell_crash_capture tasks pid=%ld end\n", (long)pid);
}

static void dump_plasmashell_snapshot(pid_t pid, const char *phase, int full)
{
    FILE *fp;

    if (!plasmashell_crash_capture_enabled() || pid <= 0)
        return;

    fp = fopen(PLASMASHELL_CRASH_CAPTURE_LOG, "a");
    if (!fp)
        return;
    fprintf(fp,
            "plasmashell_crash_capture snapshot phase=%s pid=%ld full=%d monotonic_ms=%lld\n",
            phase, (long)pid, full, monotonic_ms());
    dump_proc_file_limited(fp, pid, "comm", 1024);
    dump_proc_file_limited(fp, pid, "cmdline", 8192);
    dump_proc_file_limited(fp, pid, "environ", 32768);
    dump_proc_file_limited(fp, pid, "status", 8192);
    dump_proc_file_limited(fp, pid, "stat", 4096);
    dump_proc_file_limited(fp, pid, "wchan", 1024);
    dump_proc_file_limited(fp, pid, "syscall", 2048);
    dump_proc_file_limited(fp, pid, "stack", 8192);
    dump_fd_targets(fp, pid);
    if (full)
        dump_proc_file_limited(fp, pid, "maps", 65536);
    dump_task_files(fp, pid);
    fprintf(fp, "plasmashell_crash_capture snapshot phase=%s pid=%ld end\n",
            phase, (long)pid);
    fclose(fp);
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
        configure_plasmashell_crash_debug();
        execv(argv[0], argv);
        fprintf(stderr,
                "kde-plasma-session-child: exec plasmashell failed: %s\n",
                strerror(errno));
        _exit(127);
    }
    fprintf(stderr, "kde-plasma-session-child: plasmashell pid=%ld\n",
            (long)pid);
    crash_capture_printf("plasmashell_crash_capture spawn pid=%ld capture=1 disable_kcrash=%d core=%d",
                         (long)pid, plasmashell_disable_kcrash_enabled(),
                         plasmashell_core_enabled());
    dump_plasmashell_snapshot(pid, "spawn", 1);
    return pid;
}

static int wait_for_plasmashell_logged(char *const argv[])
{
    long long start_ms = monotonic_ms();
    long long next_snapshot_ms = start_ms + 1000;
    long long lifetime_ms;
    int status;
    int snapshot_count = 0;
    pid_t pid = spawn_plasmashell(argv);

    if (pid < 0)
        return 127;
    for (;;) {
        pid_t got = waitpid(pid, &status, WNOHANG);
        long long now_ms;

        if (got == pid)
            break;
        if (got < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr,
                    "kde-plasma-session-child: wait plasmashell: %s\n",
                    strerror(errno));
            crash_capture_printf("plasmashell_crash_capture wait_error pid=%ld errno=%d %s",
                                 (long)pid, errno, strerror(errno));
            return 127;
        }
        now_ms = monotonic_ms();
        if (plasmashell_crash_capture_enabled() && now_ms >= next_snapshot_ms) {
            char phase[32];

            snprintf(phase, sizeof(phase), "live-%03d", snapshot_count);
            dump_plasmashell_snapshot(pid, phase,
                                      (snapshot_count % 4) == 0);
            snapshot_count++;
            next_snapshot_ms = now_ms + 1000;
        }
        usleep(100000);
    }

    lifetime_ms = monotonic_ms() - start_ms;
    crash_capture_printf("plasmashell_crash_capture wait_done pid=%ld raw_status=%d lifetime_ms=%lld snapshots=%d",
                         (long)pid, status, lifetime_ms, snapshot_count);
    dump_plasmashell_snapshot(pid, "post-wait", 0);
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
    setenv("XDG_DATA_DIRS", "/usr/share:/share", 1);
    setenv("XDG_CONFIG_DIRS", "/etc/xdg", 1);
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
    setenv("EGL_PLATFORM", "wayland", 1);
    setenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "wayland-egl", 1);
    setenv("QSG_RHI_BACKEND", "opengl", 1);
    setenv("QSG_INFO", "1", 0);
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
    setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 0);
    setenv("GALLIUM_DRIVER", "virgl", 0);
    setenv("PIPEWIRE_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("PIPEWIRE_NO_RT", "1", 1);
    setenv("PULSE_COOKIE", "/dev/shm/kde-config/pulse/cookie", 1);
    setenv("PULSE_SERVER", "unix:/dev/shm/xdg-runtime-root/pulse/native", 1);
    fprintf(stderr,
            "kde-plasma-session-child: graphics EGL_PLATFORM=%s "
            "QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=%s QSG_RHI_BACKEND=%s "
            "LIBGL_ALWAYS_SOFTWARE=%s MESA_LOADER_DRIVER_OVERRIDE=%s "
            "GALLIUM_DRIVER=%s\n",
            env_or_unset("EGL_PLATFORM"),
            env_or_unset("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"),
            env_or_unset("QSG_RHI_BACKEND"),
            env_or_unset("LIBGL_ALWAYS_SOFTWARE"),
            env_or_unset("MESA_LOADER_DRIVER_OVERRIDE"),
            env_or_unset("GALLIUM_DRIVER"));
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
        "EGL_PLATFORM",
        "QT_WAYLAND_CLIENT_BUFFER_INTEGRATION",
        "QSG_RHI_BACKEND",
        "QSG_INFO",
        "DBUS_SYSTEM_BUS_ADDRESS",
        "DBUS_SESSION_BUS_ADDRESS",
        "PATH",
        "LD_LIBRARY_PATH",
        "LIBGL_DRIVERS_PATH",
        "GBM_BACKENDS_PATH",
        "LIBGL_ALWAYS_SOFTWARE",
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
    if (plasmashell_crash_capture_enabled()) {
        unlink(PLASMASHELL_CRASH_CAPTURE_LOG);
        crash_capture_printf("plasmashell_crash_capture config capture=1 disable_kcrash=%d core=%d",
                             plasmashell_disable_kcrash_enabled(),
                             plasmashell_core_enabled());
    }
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
