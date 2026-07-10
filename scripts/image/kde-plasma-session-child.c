#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
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

/*
 * U1 residual: EGL/DRI2 readiness gate (see docs/active-work-plan.md).
 *
 * Right after an fs.img rebuild the FIRST EGL/DRI2 screen creation on the
 * virtio-gpu/virgl render node can fail transiently. kwin recovers via its own
 * output-init retry (1-3x); plasmashell's Qt-Wayland EGL init has no retry and
 * drops to the QtQuick software backend ("failed to create dri2 screen" x4 +
 * "Failed to initialize EGL display 3001"). This gate runs a tiny standalone
 * probe that reproduces the failing operation and retries it until the virgl
 * DRI2 screen substrate can be created, THEN exec's plasmashell -- so
 * plasmashell no longer races kwin's output-init retry. It never wedges the
 * session: after the retry budget it proceeds anyway (plasmashell keeps its
 * pre-existing software fallback + KCrash-restart safety net).
 *
 * Default OFF (opt-in) per the guardrails: the probe forks a fresh process that
 * dlopens mesa, which is not a provably ~0 cost on the no-race path, so it is
 * not un-gated. Enable with cmdline kde_plasmashell_egl_ready_gate=1 or env
 * KDE_PLASMASHELL_EGL_READY_GATE=1.
 */
#define KDE_EGL_READINESS_PROBE_BIN "/bin/kde-egl-readiness-probe"
#define KDE_EGL_READINESS_MAX_ATTEMPTS 10
#define KDE_EGL_READINESS_BACKOFF_MS 200
#define KDE_EGL_READINESS_ATTEMPT_TIMEOUT_MS 8000

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

static void audio_status(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static int pactl_readiness_probe_enabled(void)
{
    return cmdline_has_flag("kde_pactl_probe=1") ||
           env_is_enabled("KDE_PACTL_READINESS_PROBE");
}

/*
 * Audio gap workaround: guarantee a working (silent) PulseAudio sink.
 *
 * The baked pipewire.conf declares an ALSA sink "alsa_output.xv6_virtio"
 * bound to hw:0 (factory.name = api.alsa.pcm.sink, flags = [ nofail ]).
 * On the current guest the spa-alsa hw:0 open does not complete, so that
 * node never instantiates and PipeWire/pipewire-pulse come up with ZERO
 * sinks. Chromium's PulseAudio renderer then fails to init a stream
 * (media/audio/pulse/pulse_util.cc "pa_operation is nullptr" +
 * PipelineStatus::AUDIO_RENDERER_ERROR), and because YouTube muxes an Opus
 * audio track the whole combined audio+video pipeline is failed -> black
 * player. The kde.plasma.pulseaudio widget likewise logs
 * "No object for name alsa_output.xv6_virtio.monitor".
 *
 * When this gate is ON we drop a PipeWire config fragment that instantiates
 * a support.null-audio-sink (node "xv6_null_output") so apps get a
 * working-but-silent server (equivalent to Chromium's own
 * --disable-audio-output null sink, but server-side and shared) AND we make
 * it the DEFAULT sink once pipewire-pulse is up, so Chromium's output stream
 * lands on a sink that always consumes samples instead of the ALSA hw:0 sink
 * (which, when it does register, is backed by a kernel /dev/snd with no host
 * backend and errors on stream start -> "MixableOutputStream: Error during
 * independent playback"). This is the pragmatic userspace-only path; it does
 * NOT route audio to the host (that needs the spa-alsa<->kernel /dev/snd path
 * debugged and working with a real QEMU audio backend).
 *
 * Default OFF (opt-in), byte-identical when unset: no fragment is written.
 * Enable with cmdline kde_audio_null_sink=1 or env KDE_AUDIO_NULL_SINK=1.
 */
#define AUDIO_NULL_SINK_NODE "xv6_null_output"
static int audio_null_sink_enabled(void)
{
    return flag_or_env_enabled("kde_audio_null_sink=1", "KDE_AUDIO_NULL_SINK");
}

static void write_audio_null_sink_dropin(void)
{
    const char *dir = "/dev/shm/kde-config/pipewire";
    const char *ddir = "/dev/shm/kde-config/pipewire/pipewire.conf.d";
    const char *path =
        "/dev/shm/kde-config/pipewire/pipewire.conf.d/50-xv6-null-sink.conf";
    FILE *fp;

    mkdir_one("/dev/shm/kde-config", 0700);
    mkdir_one(dir, 0700);
    mkdir_one(ddir, 0700);

    fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr,
                "kde-plasma-session-child: null-sink dropin %s: %s\n",
                path, strerror(errno));
        audio_status("phase=null-sink status=FAIL reason=open-%s", strerror(errno));
        return;
    }

    fprintf(fp,
        "# xv6 gated audio workaround (kde_audio_null_sink=1): a guaranteed\n"
        "# working silent sink so PulseAudio clients (Chromium) init cleanly\n"
        "# when the ALSA hw:0 sink does not instantiate.\n"
        "context.objects = [\n"
        "    {   factory = adapter\n"
        "        args = {\n"
        "            factory.name            = support.null-audio-sink\n"
        "            node.name               = \"" AUDIO_NULL_SINK_NODE "\"\n"
        "            node.description        = \"xv6 Null Output\"\n"
        "            media.class             = \"Audio/Sink\"\n"
        "            audio.position          = \"FL,FR\"\n"
        "            monitor.channel-volumes = true\n"
        "            priority.session        = 2000\n"
        "            priority.driver         = 2000\n"
        "            node.always-process     = true\n"
        "            object.linger           = true\n"
        "        }\n"
        "    }\n"
        "]\n");
    fclose(fp);

    audio_status("phase=null-sink status=WROTE path=%s node=%s",
                 path, AUDIO_NULL_SINK_NODE);
    fprintf(stderr,
            "kde-plasma-session-child: audio null-sink dropin written: %s\n",
            path);
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

    audio_status("phase=start status=START pactl_probe=%s null_sink=%s",
                 pactl_enabled ? "enabled" : "skipped",
                 audio_null_sink_enabled() ? "enabled" : "skipped");
    if (audio_null_sink_enabled())
        write_audio_null_sink_dropin();
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
    if (audio_null_sink_enabled() && pulse_ready) {
        /* Make the always-consuming null sink the default so Chromium's
         * PulseAudio output stream does not land on the broken ALSA hw:0
         * sink. pactl writes the wireplumber default-sink metadata. */
        run_optional((char *const[]){ "/usr/bin/pactl", "set-default-sink",
                                      AUDIO_NULL_SINK_NODE, NULL },
                     1, 4000);
        audio_status("phase=null-sink status=DEFAULT node=%s",
                     AUDIO_NULL_SINK_NODE);
    }
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

static int egl_readiness_gate_enabled(void)
{
    static int initialized;
    static int enabled;

    if (!initialized) {
        enabled = flag_or_env_enabled("kde_plasmashell_egl_ready_gate=1",
                                      "KDE_PLASMASHELL_EGL_READY_GATE");
        initialized = 1;
    }
    return enabled;
}

/*
 * Run the readiness probe once. Returns 1 if the probe exited 0 (a working
 * DRI2/virgl screen could be created right now), 0 otherwise (including probe
 * missing / fork failure / timeout, all treated as "not ready yet").
 */
static int run_egl_readiness_probe_once(void)
{
    char *argv[] = { (char *)KDE_EGL_READINESS_PROBE_BIN, NULL };
    pid_t pid;
    int status = 0;
    int waited_ms = 0;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr,
                "kde-plasma-session-child: egl-readiness-gate fork failed: %s\n",
                strerror(errno));
        return 0;
    }
    if (pid == 0) {
        /* Fold probe stdout into the plasma-child log (stderr) for grep. */
        dup2(STDERR_FILENO, STDOUT_FILENO);
        execv(argv[0], argv);
        _exit(127);
    }

    for (;;) {
        pid_t got = waitpid(pid, &status, WNOHANG);

        if (got == pid)
            break;
        if (got < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (waited_ms >= KDE_EGL_READINESS_ATTEMPT_TIMEOUT_MS) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            fprintf(stderr,
                    "kde-plasma-session-child: egl-readiness-gate probe "
                    "timed out after %dms\n", waited_ms);
            return 0;
        }
        usleep(100000);
        waited_ms += 100;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void run_egl_readiness_gate(void)
{
    long long start_ms;

    if (!egl_readiness_gate_enabled())
        return;

    if (access(KDE_EGL_READINESS_PROBE_BIN, X_OK) != 0) {
        fprintf(stderr,
                "kde-plasma-session-child: egl-readiness-gate status=SKIP "
                "reason=probe-missing path=%s\n", KDE_EGL_READINESS_PROBE_BIN);
        return;
    }

    start_ms = monotonic_ms();
    for (int attempt = 1; attempt <= KDE_EGL_READINESS_MAX_ATTEMPTS; attempt++) {
        if (run_egl_readiness_probe_once()) {
            fprintf(stderr,
                    "kde-plasma-session-child: egl-readiness-gate status=READY "
                    "attempt=%d elapsed_ms=%lld\n",
                    attempt, monotonic_ms() - start_ms);
            return;
        }
        fprintf(stderr,
                "kde-plasma-session-child: egl-readiness-gate status=RETRY "
                "attempt=%d/%d elapsed_ms=%lld backoff_ms=%d\n",
                attempt, KDE_EGL_READINESS_MAX_ATTEMPTS,
                monotonic_ms() - start_ms, KDE_EGL_READINESS_BACKOFF_MS);
        if (attempt < KDE_EGL_READINESS_MAX_ATTEMPTS)
            usleep((useconds_t)KDE_EGL_READINESS_BACKOFF_MS * 1000);
    }

    fprintf(stderr,
            "kde-plasma-session-child: egl-readiness-gate status=TIMEOUT "
            "attempts=%d elapsed_ms=%lld proceeding=1\n",
            KDE_EGL_READINESS_MAX_ATTEMPTS, monotonic_ms() - start_ms);
}

/*
 * U-KICKOFF: session-start Kickoff (start-menu) PREWARM.
 *
 * The COLD first Kickoff activation of a session costs ~2s (one-time QML
 * component compile + app/recents model population). Proven lever (M11 A/B,
 * hoverprobe _PREWARM gate): opening Kickoff ONCE at login and dismissing it
 * moves the user's first open from 2254/1892ms into the warm ~500ms band
 * (-75%). This ports that lever into the real product session so normal users
 * (not just the test tool) get it.
 *
 * Mechanism (kwin/plasmashell are prebuilt, so no C++ patch): input injection.
 * A double-forked helper waits for the taskbar to paint (FB scanout-read ROI
 * settle heuristic, borrowed from hoverprobe), then absolute-clicks the Kickoff
 * launcher icon via /dev/mouse, dwells to let the models build, and dismisses
 * with a click on the empty desktop -- leaving the desktop pristine (verified
 * by an ROI hash compare, in addition to the harness screenshot diff). It runs
 * in its own session (setsid) so it never steals the session-child's wait, and
 * is non-blocking for the rest of startup. At session start nothing else is
 * running, so the injected clicks cannot steal focus from the user.
 *
 * Default OFF (opt-in) per the guardrails -- an injected click has a non-~0
 * one-time cost (~2s of prewarm work paid at login, though nearly free within
 * the ~6.2-6.6s M5 first-visible window). Enable with cmdline
 * kde_kickoff_prewarm=1 or env KDE_KICKOFF_PREWARM=1. Promotion to default-on
 * needs the standard same-session A/B + owner sign-off + regression battery.
 */
#define KICKOFF_MOUSE_DEV        "/dev/mouse"
#define KICKOFF_FB_DEV           "/dev/fb0"
#define KICKOFF_FB_SCANOUT_READ  0x4635      /* FB_GPU_SCANOUT_READ */
#define KICKOFF_MOUSE_F_ABSOLUTE 0x01

/* Validated defaults from the M11 A/B (screen 1280x800). */
#define KICKOFF_ICON_ABS_X   1200
#define KICKOFF_ICON_ABS_Y   64200
#define KICKOFF_AWAY_ABS_X   32768
#define KICKOFF_AWAY_ABS_Y   32768
#define KICKOFF_DWELL_MS     4000
#define KICKOFF_SETTLE_MS    900
#define KICKOFF_GATE_MS      60000
#define KICKOFF_PRESS_MS     40
#define KICKOFF_OPEN_ATTEMPTS   8
#define KICKOFF_OPEN_TIMEOUT_MS 2500

/* /dev/mouse packet (matches kernel struct mouse_event, 8 bytes). */
struct kickoff_mouse_event {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
    uint8_t flags;
    int8_t  dz;
    uint8_t pad[1];
};

/* FB_GPU_SCANOUT_READ argument (matches kernel struct fb_gpu_scanout_read). */
struct kickoff_scanout_read {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t pitch;
    uint32_t flags;
    uint64_t pixels;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t screen_pitch;
    uint32_t reserved;
};

static int kickoff_prewarm_enabled(void)
{
    static int cached = -1;

    if (cached < 0)
        cached = flag_or_env_enabled("kde_kickoff_prewarm=1",
                                     "KDE_KICKOFF_PREWARM") ? 1 : 0;
    return cached;
}

/*
 * Tooltip prewarm (gated, default OFF): hover a taskbar icon once at login so
 * plasmashell instantiates the shared ToolTipDialog QML; the user's first real
 * hover then pays only the warm tooltip cost (~0.5s) instead of the cold one
 * (~1.0s measured). Rides the same double-forked worker as the Kickoff
 * prewarm; each step keeps its own gate so either can run alone.
 */
static int tooltip_prewarm_enabled(void)
{
    static int cached = -1;

    if (cached < 0)
        cached = flag_or_env_enabled("kde_tooltip_prewarm=1",
                                     "KDE_TOOLTIP_PREWARM") ? 1 : 0;
    return cached;
}

static long long kickoff_env_ll(const char *name, long long fallback,
                                long long lo, long long hi)
{
    const char *v = getenv(name);
    long long r;

    if (!v || !*v)
        return fallback;
    r = strtoll(v, NULL, 10);
    if (r < lo)
        r = lo;
    if (r > hi)
        r = hi;
    return r;
}

static int kickoff_clamp_abs(long long v)
{
    if (v < 0)
        return 0;
    if (v > 65535)
        return 65535;
    return (int)v;
}

static int kickoff_inject_abs(int mouse_fd, int ax, int ay, int buttons)
{
    struct kickoff_mouse_event ev;

    memset(&ev, 0, sizeof(ev));
    ev.flags = KICKOFF_MOUSE_F_ABSOLUTE;
    ev.dx = (int16_t)kickoff_clamp_abs(ax);
    ev.dy = (int16_t)kickoff_clamp_abs(ay);
    ev.buttons = (uint8_t)buttons;
    if (write(mouse_fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev))
        return -1;
    return 0;
}

static void kickoff_sleep_ms(long long ms)
{
    struct timespec ts;

    if (ms <= 0)
        return;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/*
 * Read a scanout ROI; return 0 on success. Fills *hash (FNV-1a over pixels),
 * *nonblack (count of pixels with any non-zero RGB) and, when requested, the
 * returned scanout dimensions. Any of the out pointers may be NULL.
 */
static int kickoff_sample_roi(int fb_fd, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, uint64_t *hash_out,
                              uint32_t *nonblack_out, uint32_t *sw_out,
                              uint32_t *sh_out)
{
    struct kickoff_scanout_read req;
    uint32_t *pix;
    uint64_t hh = 1469598103934665603ULL;
    uint32_t nb = 0;
    uint32_t n;

    if (fb_fd < 0 || w == 0 || h == 0)
        return -1;
    n = w * h;
    pix = malloc((size_t)n * sizeof(uint32_t));
    if (!pix)
        return -1;
    memset(&req, 0, sizeof(req));
    req.x = x;
    req.y = y;
    req.w = w;
    req.h = h;
    req.pitch = w * (uint32_t)sizeof(uint32_t);
    req.pixels = (uint64_t)(uintptr_t)pix;
    if (ioctl(fb_fd, KICKOFF_FB_SCANOUT_READ, &req) < 0) {
        free(pix);
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = pix[i];

        hh ^= p;
        hh *= 1099511628211ULL;
        if ((p & 0x00FFFFFFU) != 0)
            nb++;
    }
    if (hash_out)
        *hash_out = hh;
    if (nonblack_out)
        *nonblack_out = nb;
    if (sw_out)
        *sw_out = req.screen_width;
    if (sh_out)
        *sh_out = req.screen_height;
    free(pix);
    return 0;
}

/* Dump the full current scanout to a P6 PPM (pristine-desktop proof). */
static void kickoff_dump_frame_ppm(int fb_fd, const char *path,
                                   uint32_t w, uint32_t h)
{
    struct kickoff_scanout_read req;
    uint32_t *px;
    unsigned char *rgb;
    uint32_t n;
    FILE *fp;

    if (fb_fd < 0 || w == 0 || h == 0)
        return;
    n = w * h;
    px = malloc((size_t)n * sizeof(uint32_t));
    rgb = malloc((size_t)n * 3);
    if (!px || !rgb) {
        free(px);
        free(rgb);
        return;
    }
    memset(&req, 0, sizeof(req));
    req.x = 0;
    req.y = 0;
    req.w = w;
    req.h = h;
    req.pitch = w * (uint32_t)sizeof(uint32_t);
    req.pixels = (uint64_t)(uintptr_t)px;
    if (ioctl(fb_fd, KICKOFF_FB_SCANOUT_READ, &req) < 0) {
        free(px);
        free(rgb);
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = px[i];   /* XRGB8888 little-endian */

        rgb[i * 3 + 0] = (unsigned char)((p >> 16) & 0xff);
        rgb[i * 3 + 1] = (unsigned char)((p >> 8) & 0xff);
        rgb[i * 3 + 2] = (unsigned char)(p & 0xff);
    }
    fp = fopen(path, "wb");
    if (fp) {
        fprintf(fp, "P6\n%u %u\n255\n", w, h);
        fwrite(rgb, 1, (size_t)n * 3, fp);
        fclose(fp);
    }
    free(px);
    free(rgb);
}

/*
 * Wait for the taskbar (bottom-left launcher region) to be painted and stable.
 * Returns 1 = settled, 0 = timed out (caller proceeds anyway). Mirrors the
 * harness taskbar-settle heuristic: a bottom-left ROI must become non-black and
 * hash-stable for a few consecutive samples.
 */
static int kickoff_wait_taskbar_settled(int fb_fd, uint32_t sw, uint32_t sh,
                                        long long budget_ms)
{
    uint32_t rw = 56, rh = 40;
    uint32_t rx = 0;
    uint32_t ry = (sh > rh) ? sh - rh : 0;
    uint32_t need_nonblack;
    long long start_ms = monotonic_ms();
    long long deadline = start_ms + budget_ms;
    uint64_t last = 0;
    int have = 0, stable = 0, samples = 0;

    if (rw > sw)
        rw = sw;
    need_nonblack = (rw * rh) / 20;   /* >= ~5% of the ROI painted */
    if (need_nonblack < 1)
        need_nonblack = 1;

    while (monotonic_ms() < deadline) {
        uint64_t h;
        uint32_t nb;

        samples++;
        if (kickoff_sample_roi(fb_fd, rx, ry, rw, rh, &h, &nb,
                               NULL, NULL) < 0) {
            kickoff_sleep_ms(250);
            continue;
        }
        if (nb >= need_nonblack) {
            if (have && h == last) {
                if (++stable >= 3) {
                    fprintf(stderr,
                            "kde-plasma-session-child: kickoff-prewarm gate "
                            "status=READY elapsed_ms=%lld samples=%d "
                            "nonblack=%u roi=%u,%u,%u,%u\n",
                            monotonic_ms() - start_ms, samples, nb,
                            rx, ry, rw, rh);
                    return 1;
                }
            } else {
                stable = 0;
            }
            last = h;
            have = 1;
        } else {
            stable = 0;
            have = 0;
        }
        kickoff_sleep_ms(250);
    }

    fprintf(stderr,
            "kde-plasma-session-child: kickoff-prewarm gate status=TIMEOUT "
            "elapsed_ms=%lld samples=%d proceeding=1\n",
            monotonic_ms() - start_ms, samples);
    return 0;
}

/*
 * Poll an ROI until its hash differs from baseline (a change) or timeout.
 * Returns 1 = changed, 0 = timed out, -1 = readback error.
 */
static int kickoff_poll_roi_change(int fb_fd, uint32_t x, uint32_t y,
                                   uint32_t w, uint32_t h, uint64_t baseline,
                                   long long timeout_ms)
{
    long long deadline = monotonic_ms() + timeout_ms;

    while (monotonic_ms() < deadline) {
        uint64_t hh;

        if (kickoff_sample_roi(fb_fd, x, y, w, h, &hh, NULL, NULL, NULL) < 0)
            return -1;
        if (hh != baseline)
            return 1;
        kickoff_sleep_ms(20);
    }
    return 0;
}

/* Body of the prewarm worker (runs in the double-forked grandchild). */
static void kickoff_prewarm_run(void)
{
    int mouse_fd, fb_fd;
    int icon_x = KICKOFF_ICON_ABS_X, icon_y = KICKOFF_ICON_ABS_Y;
    int away_x = KICKOFF_AWAY_ABS_X, away_y = KICKOFF_AWAY_ABS_Y;
    long long dwell_ms = kickoff_env_ll("KDE_KICKOFF_PREWARM_DWELL_MS",
                                        KICKOFF_DWELL_MS, 0, 30000);
    long long settle_ms = kickoff_env_ll("KDE_KICKOFF_PREWARM_SETTLE_MS",
                                         KICKOFF_SETTLE_MS, 0, 5000);
    long long budget_ms = kickoff_env_ll("KDE_KICKOFF_PREWARM_GATE_MS",
                                         KICKOFF_GATE_MS, 1000, 180000);
    uint32_t sw = 1280, sh = 800;
    uint32_t mrx, mry, mrw = 200, mrh = 200;
    uint64_t base_hash = 0, open_hash = 0, post_hash = 0;
    uint32_t base_nb = 0, open_nb = 0, post_nb = 0;
    int have_base = 0, gate = -1;
    long long t0 = monotonic_ms();

    icon_x = (int)kickoff_env_ll("KDE_KICKOFF_PREWARM_ICON_ABS_X", icon_x,
                                 0, 65535);
    icon_y = (int)kickoff_env_ll("KDE_KICKOFF_PREWARM_ICON_ABS_Y", icon_y,
                                 0, 65535);

    mouse_fd = open(KICKOFF_MOUSE_DEV, O_RDWR);
    if (mouse_fd < 0) {
        fprintf(stderr,
                "kde-plasma-session-child: kickoff-prewarm status=SKIP "
                "reason=mouse-open-failed err=%s\n", strerror(errno));
        return;
    }
    fb_fd = open(KICKOFF_FB_DEV, O_RDWR);
    if (fb_fd >= 0) {
        /* Probe the current scanout size (also warms the readback path). */
        kickoff_sample_roi(fb_fd, 0, 0, 1, 1, NULL, NULL, &sw, &sh);
        if (sw == 0 || sh == 0) {
            sw = 1280;
            sh = 800;
        }
    }

    fprintf(stderr,
            "kde-plasma-session-child: kickoff-prewarm status=START "
            "screen=%ux%u icon_abs16=%d,%d away_abs16=%d,%d dwell_ms=%lld "
            "settle_ms=%lld gate_ms=%lld\n",
            sw, sh, icon_x, icon_y, away_x, away_y, dwell_ms, settle_ms,
            budget_ms);

    /* Readiness gate: wait for the taskbar to paint (best-effort). */
    if (fb_fd >= 0)
        gate = kickoff_wait_taskbar_settled(fb_fd, sw, sh, budget_ms);

    /* Pristine baseline over the Kickoff-body region (best-effort). */
    mrx = 100;
    mry = 330;
    if (mrx + mrw > sw)
        mrx = (sw > mrw) ? sw - mrw : 0;
    if (mry + mrh > sh)
        mry = (sh > mrh) ? sh - mrh : 0;
    if (fb_fd >= 0 &&
        kickoff_sample_roi(fb_fd, mrx, mry, mrw, mrh, &base_hash, &base_nb,
                           NULL, NULL) == 0)
        have_base = 1;

    /*
     * OPEN with verify-and-retry. The Kickoff applet may not be interactive the
     * instant the taskbar corner paints, so a single early click can be
     * swallowed. Click the launcher, confirm the menu-body ROI actually changed
     * (Kickoff drew over the wallpaper), and retry if not. Each attempt starts
     * from a known-closed state (click empty desktop) so a click that DID open
     * is not silently toggled shut by the next attempt.
     * (Skipped entirely when only the tooltip prewarm gate is on.)
     */
    long long open_timeout_ms =
        kickoff_env_ll("KDE_KICKOFF_PREWARM_OPEN_TIMEOUT_MS",
                       KICKOFF_OPEN_TIMEOUT_MS, 200, 10000);
    int max_attempts = have_base ? KICKOFF_OPEN_ATTEMPTS : 1;
    int opened = 0, attempt = 0;
    uint64_t pre_open_hash = base_hash;

    if (!kickoff_prewarm_enabled())
        max_attempts = 0;

    for (attempt = 1; attempt <= max_attempts; attempt++) {
        uint64_t attempt_base = base_hash;

        /* Ensure a known-closed, pristine desktop first. */
        kickoff_inject_abs(mouse_fd, away_x, away_y, 0);
        kickoff_sleep_ms(settle_ms);
        if (have_base)
            kickoff_inject_abs(mouse_fd, away_x, away_y, 1);
        kickoff_sleep_ms(KICKOFF_PRESS_MS);
        if (have_base)
            kickoff_inject_abs(mouse_fd, away_x, away_y, 0);
        kickoff_sleep_ms(settle_ms);
        if (have_base)
            kickoff_sample_roi(fb_fd, mrx, mry, mrw, mrh, &attempt_base, NULL,
                               NULL, NULL);
        pre_open_hash = attempt_base;

        /* Click the Kickoff launcher icon. */
        kickoff_inject_abs(mouse_fd, icon_x, icon_y, 1);
        kickoff_sleep_ms(KICKOFF_PRESS_MS);
        kickoff_inject_abs(mouse_fd, icon_x, icon_y, 0);

        if (!have_base) {
            opened = -1;   /* no fb: cannot verify, assume best-effort */
            break;
        }
        if (kickoff_poll_roi_change(fb_fd, mrx, mry, mrw, mrh, attempt_base,
                                    open_timeout_ms) == 1) {
            opened = 1;
            kickoff_sample_roi(fb_fd, mrx, mry, mrw, mrh, &open_hash, &open_nb,
                               NULL, NULL);
            fprintf(stderr,
                    "kde-plasma-session-child: kickoff-prewarm open status=OPEN "
                    "attempt=%d elapsed_ms=%lld\n",
                    attempt, monotonic_ms() - t0);
            break;
        }
        fprintf(stderr,
                "kde-plasma-session-child: kickoff-prewarm open status=RETRY "
                "attempt=%d/%d elapsed_ms=%lld\n",
                attempt, max_attempts, monotonic_ms() - t0);
    }

    if (kickoff_prewarm_enabled()) {
        /* Dwell so the one-time QML compile + app/recents models build. */
        kickoff_sleep_ms(dwell_ms);

        /* DISMISS: click the empty desktop to close Kickoff. */
        kickoff_inject_abs(mouse_fd, away_x, away_y, 1);
        kickoff_sleep_ms(KICKOFF_PRESS_MS);
        kickoff_inject_abs(mouse_fd, away_x, away_y, 0);
        kickoff_sleep_ms(settle_ms);

        /* Pristine-desktop proof: dump the full frame after the dismiss. */
        if (fb_fd >= 0)
            kickoff_dump_frame_ppm(fb_fd,
                                   "/kde-plasma-kickoff-prewarm-after.ppm",
                                   sw, sh);

        /* Verify the desktop is pristine again (menu-body ROI closed). */
        if (have_base &&
            kickoff_sample_roi(fb_fd, mrx, mry, mrw, mrh, &post_hash,
                               &post_nb, NULL, NULL) == 0) {
            int pristine = (post_hash == pre_open_hash);

            fprintf(stderr,
                    "kde-plasma-session-child: kickoff-prewarm status=DONE "
                    "gate=%s opened=%d attempts=%d pristine=%d elapsed_ms=%lld "
                    "menu_roi=%u,%u,%u,%u base_nonblack=%u open_nonblack=%u "
                    "post_nonblack=%u\n",
                    gate == 1 ? "READY" : (gate == 0 ? "TIMEOUT" : "NOFB"),
                    opened, attempt, pristine, monotonic_ms() - t0, mrx, mry,
                    mrw, mrh, base_nb, open_nb, post_nb);
        } else {
            fprintf(stderr,
                    "kde-plasma-session-child: kickoff-prewarm status=DONE "
                    "gate=%s opened=%d pristine=unknown elapsed_ms=%lld "
                    "reason=no-roi-verify\n",
                    gate == 1 ? "READY" : (gate == 0 ? "TIMEOUT" : "NOFB"),
                    opened, monotonic_ms() - t0);
        }
    }

    /*
     * Tooltip prewarm (own gate): hover a taskbar icon once so plasmashell
     * builds the shared ToolTipDialog QML now; move away so the tooltip
     * hides again. Runs after the Kickoff dismissal so the two prewarm
     * pop-ups never overlap; leaves the desktop pristine (tooltips
     * auto-hide on hover-out, nothing to click).
     */
    if (tooltip_prewarm_enabled()) {
        int tip_x = (int)kickoff_env_ll("KDE_TOOLTIP_PREWARM_ICON_ABS_X",
                                        11000, 0, 65535);
        int tip_y = (int)kickoff_env_ll("KDE_TOOLTIP_PREWARM_ICON_ABS_Y",
                                        64200, 0, 65535);
        long long tip_dwell_ms =
            kickoff_env_ll("KDE_TOOLTIP_PREWARM_DWELL_MS", 2500, 0, 30000);
        long long t1 = monotonic_ms();

        kickoff_inject_abs(mouse_fd, tip_x, tip_y, 0);
        kickoff_sleep_ms(tip_dwell_ms);   /* show-delay + QML build + paint */
        kickoff_inject_abs(mouse_fd, away_x, away_y, 0);
        kickoff_sleep_ms(settle_ms);      /* tooltip hides (~0.3s) */
        fprintf(stderr,
                "kde-plasma-session-child: tooltip-prewarm status=DONE "
                "icon_abs16=%d,%d dwell_ms=%lld elapsed_ms=%lld\n",
                tip_x, tip_y, tip_dwell_ms, monotonic_ms() - t1);
    }

    if (fb_fd >= 0)
        close(fb_fd);
    close(mouse_fd);
}

/*
 * Launch the prewarm worker without blocking the session-child. Double-forks so
 * the worker is reparented to init (no zombie, and the session-child's
 * waitpid(plasmashell) is never confused by the worker's exit).
 */
static void kickoff_prewarm_start(void)
{
    pid_t pid;

    if (!kickoff_prewarm_enabled() && !tooltip_prewarm_enabled())
        return;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr,
                "kde-plasma-session-child: kickoff-prewarm fork failed: %s\n",
                strerror(errno));
        return;
    }
    if (pid == 0) {
        pid_t worker;

        setsid();
        worker = fork();
        if (worker < 0)
            _exit(0);
        if (worker > 0)
            _exit(0);
        kickoff_prewarm_run();
        _exit(0);
    }

    /* Reap the intermediate; the worker is now an init child. */
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
    fprintf(stderr,
            "kde-plasma-session-child: kickoff-prewarm scheduled "
            "(gates: kickoff=%d kde_kickoff_prewarm=1, "
            "tooltip=%d kde_tooltip_prewarm=1)\n",
            kickoff_prewarm_enabled(), tooltip_prewarm_enabled());
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

    run_egl_readiness_gate();

    /* U-KICKOFF: schedule the session-start Kickoff prewarm (gated, default
     * OFF). Non-blocking: the worker self-gates on the taskbar painting, so it
     * is safe to launch here even though plasmashell is spawned just below. */
    kickoff_prewarm_start();

    return wait_for_plasmashell_logged(plasmashell);
}
