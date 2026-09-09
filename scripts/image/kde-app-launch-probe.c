#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct app_probe {
    const char *name;
    char *const *argv;
    pid_t pid;
    int ok;
    int exited;
    int exit_code;
    int signal_code;
    long long launch_start_ms;
    long long launch_start_uptime_ms;
    long long launch_elapsed_ms;
};

struct konsole_phase_samples {
    long long ptmx_first_ms;
    long long ptmx_first_uptime_ms;
    long long pts_first_ms;
    long long pts_first_uptime_ms;
    long long bash_first_ms;
    long long bash_first_uptime_ms;
    int ptmx_fd;
    int pts_fd;
    char ptmx_target[256];
    char pts_target[256];
};

static const char *chromium_evidence_path =
    "/kde-chromium-process-evidence.log";
static const char *chromium_launcher_log_path =
    "/host-gui-wayland-chromium.log";
static const char *chromium_local_video_url =
    "file:///share/webkit/perf-video.html?asset=perf-1280x800-60fps.mp4&ms=15000&hud=1";
static const char *qtmm_stub_dir =
    "/opt/xv6-kde-abi-libs/qt5multimedia-shim";
static const char *qtmm_stub_call_log_path =
    "/tmp/xv6-qtmm-shim-calls.log";
static FILE *chromium_evidence;

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long uptime_ms(void)
{
    FILE *fp;
    char token[64];
    char *dot;
    char *end;
    long long sec;
    long long frac_ms = 0;

    fp = fopen("/proc/uptime", "r");
    if (!fp)
        return -1;
    if (fscanf(fp, "%63s", token) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    dot = strchr(token, '.');
    if (dot)
        *dot++ = '\0';
    errno = 0;
    sec = strtoll(token, &end, 10);
    if (errno || !end || *end != '\0')
        return -1;
    if (dot) {
        int digits = 0;

        while (*dot && digits < 3) {
            if (!isdigit((unsigned char)*dot))
                return -1;
            frac_ms = frac_ms * 10 + (*dot - '0');
            dot++;
            digits++;
        }
        while (digits < 3) {
            frac_ms *= 10;
            digits++;
        }
    }
    return sec * 1000 + frac_ms;
}

static void maybe_redirect_stderr_to_stdout(void)
{
    const char *value = getenv("KDE_APP_LAUNCH_PROBE_STDERR_TO_STDOUT");

    if (!value || value[0] == '\0' || strcmp(value, "0") == 0)
        return;
    fflush(stderr);
    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
        fprintf(stderr,
                "kde_app_launch_probe stderr_redirect_failed errno=%d %s\n",
                errno, strerror(errno));
}

static int env_truthy_early(const char *name)
{
    const char *value = getenv(name);

    if (!value || value[0] == '\0' || strcmp(value, "0") == 0 ||
        strcmp(value, "false") == 0 || strcmp(value, "False") == 0 ||
        strcmp(value, "FALSE") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "No") == 0 || strcmp(value, "NO") == 0 ||
        strcmp(value, "off") == 0 || strcmp(value, "Off") == 0 ||
        strcmp(value, "OFF") == 0)
        return 0;
    return 1;
}

static void set_kde_env(void)
{
    const int newstuff_stub =
        env_truthy_early("KDE_APP_LAUNCH_PROBE_NEWSTUFF_STUB");

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
    setenv("XDG_CONFIG_DIRS",
           "/etc/xdg", 1);
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
    setenv("EGL_PLATFORM", "wayland", 1);
    setenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "wayland-egl", 1);
    setenv("QSG_RHI_BACKEND", "opengl", 1);
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 0);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    if (newstuff_stub) {
        setenv("LD_LIBRARY_PATH",
               "/opt/xv6-kde-abi-libs/newstuff-shim:"
               "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:"
               "/lib/x86_64-linux-gnu:/usr/lib:/lib",
               1);
        fprintf(stderr,
                "kde_app_launch_probe newstuff_stub=enabled "
                "ld_library_path=%s\n",
                getenv("LD_LIBRARY_PATH"));
    } else {
        setenv("LD_LIBRARY_PATH",
               "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:"
               "/lib/x86_64-linux-gnu:/usr/lib:/lib",
               1);
    }
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
}

static int qtmm_stub_enabled(void)
{
    return env_truthy_early("KDE_APP_LAUNCH_PROBE_QTMM_STUB");
}

static void reset_qtmm_stub_call_log(void)
{
    if (qtmm_stub_enabled())
        unlink(qtmm_stub_call_log_path);
}

static void report_qtmm_stub_call_log(void)
{
    FILE *fp;
    char buf[1024];
    size_t n;
    int total = 0;

    if (!qtmm_stub_enabled())
        return;

    fp = fopen(qtmm_stub_call_log_path, "r");
    if (!fp) {
        fprintf(stderr,
                "kde_app_launch_probe qtmm_stub_call_log status=ABSENT "
                "path=%s errno=%d\n",
                qtmm_stub_call_log_path, errno);
        return;
    }

    fprintf(stderr,
            "kde_app_launch_probe qtmm_stub_call_log status=HIT path=%s "
            "risk=unsafe_load_only begin\n",
            qtmm_stub_call_log_path);
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        fwrite(buf, 1, n, stderr);
        total += (int)n;
        if (total >= 8192) {
            fprintf(stderr,
                    "kde_app_launch_probe qtmm_stub_call_log truncated=1 "
                    "bytes=%d\n",
                    total);
            break;
        }
    }
    fclose(fp);
    fprintf(stderr,
            "kde_app_launch_probe qtmm_stub_call_log status=HIT path=%s "
            "bytes=%d end\n",
            qtmm_stub_call_log_path, total);
}

static void enable_qtmm_stub_for_konsole_child(void)
{
    const char *current;
    char *next;
    size_t len;

    if (!qtmm_stub_enabled())
        return;

    current = getenv("LD_LIBRARY_PATH");
    if (current && strstr(current, qtmm_stub_dir))
        return;

    len = strlen(qtmm_stub_dir) + (current && current[0] ?
          strlen(current) + 2 : 1);
    next = malloc(len);
    if (!next) {
        fprintf(stderr,
                "kde_app_launch_probe qtmm_stub=enabled "
                "ld_library_path=malloc_failed\n");
        return;
    }
    if (current && current[0])
        snprintf(next, len, "%s:%s", qtmm_stub_dir, current);
    else
        snprintf(next, len, "%s", qtmm_stub_dir);
    setenv("LD_LIBRARY_PATH", next, 1);
    fprintf(stderr,
            "kde_app_launch_probe qtmm_stub=enabled scope=konsole-child "
            "shim_dir=%s ld_library_path=%s call_log=%s "
            "risk=unsafe_if_called\n",
            qtmm_stub_dir, getenv("LD_LIBRARY_PATH"),
            qtmm_stub_call_log_path);
    free(next);
}

static void fprint_escaped(FILE *out, const char *buf, ssize_t n)
{
    static const char hex[] = "0123456789abcdef";

    if (!buf) {
        fputs("(null)", out);
        return;
    }
    if (n < 0)
        n = (ssize_t)strlen(buf);

    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];

        if (c == '\0') {
            fputs("\\0", out);
        } else if (c == '\\' || c == '"') {
            fputc('\\', out);
            fputc(c, out);
        } else if (c >= 0x20 && c <= 0x7e) {
            fputc(c, out);
        } else {
            fputs("\\x", out);
            fputc(hex[c >> 4], out);
            fputc(hex[c & 0xf], out);
        }
    }
}

static void escape_string(char *out, size_t out_size, const char *in)
{
    static const char hex[] = "0123456789abcdef";
    size_t off = 0;

    if (out_size == 0)
        return;
    if (!in)
        in = "";
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;

        if (c == '\\' || c == '"') {
            if (off + 2 >= out_size)
                break;
            out[off++] = '\\';
            out[off++] = (char)c;
        } else if (c >= 0x20 && c <= 0x7e) {
            if (off + 1 >= out_size)
                break;
            out[off++] = (char)c;
        } else {
            if (off + 4 >= out_size)
                break;
            out[off++] = '\\';
            out[off++] = 'x';
            out[off++] = hex[c >> 4];
            out[off++] = hex[c & 0xf];
        }
    }
    out[off] = '\0';
}

static void chromium_evidence_open(void)
{
    int fd = open(chromium_evidence_path,
                  O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC,
                  0644);

    if (fd >= 0)
        chromium_evidence = fdopen(fd, "a");
    if (!chromium_evidence) {
        if (fd >= 0)
            close(fd);
        fprintf(stderr,
                "kde_app_launch_probe chromium_evidence open_failed path=%s "
                "errno=%d %s\n",
                chromium_evidence_path, errno, strerror(errno));
        return;
    }
    setvbuf(chromium_evidence, NULL, _IOLBF, 0);
    fprintf(chromium_evidence,
            "kde_chromium_process_evidence begin pid=%ld ppid=%ld\n",
            (long)getpid(), (long)getppid());
    fflush(chromium_evidence);
}

static void chromium_evidence_open_append(const char *phase)
{
    int fd = open(chromium_evidence_path,
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

    if (fd >= 0)
        chromium_evidence = fdopen(fd, "a");
    if (!chromium_evidence) {
        if (fd >= 0)
            close(fd);
        fprintf(stderr,
                "kde_app_launch_probe chromium_evidence append_open_failed "
                "path=%s errno=%d %s\n",
                chromium_evidence_path, errno, strerror(errno));
        return;
    }
    setvbuf(chromium_evidence, NULL, _IOLBF, 0);
    fprintf(chromium_evidence,
            "kde_chromium_process_evidence sampler phase=%s pid=%ld ppid=%ld "
            "begin\n",
            phase, (long)getpid(), (long)getppid());
    fflush(chromium_evidence);
}

static void chromium_evidence_close(int ok)
{
    if (!chromium_evidence)
        return;
    fprintf(chromium_evidence,
            "kde_chromium_process_evidence end status=%s\n",
            ok ? "PASS" : "FAIL");
    fclose(chromium_evidence);
    chromium_evidence = NULL;
}

static void chromium_evidence_sampler_close(int ok, int samples)
{
    if (!chromium_evidence)
        return;
    fprintf(chromium_evidence,
            "kde_chromium_process_evidence sampler end status=%s samples=%d\n",
            ok ? "PASS" : "FAIL", samples);
    fclose(chromium_evidence);
    chromium_evidence = NULL;
}

static void chromium_evidence_expected_url(pid_t launched_pid,
                                           const char *chromium_url)
{
    if (!chromium_evidence)
        return;
    fprintf(chromium_evidence,
            "kde_chromium_process_evidence launch launched_pid=%ld "
            "expected_url=\"",
            (long)launched_pid);
    fprint_escaped(chromium_evidence, chromium_url, -1);
    fputs("\"\n", chromium_evidence);
    fflush(chromium_evidence);
}

static int launch_app(struct app_probe *probe)
{
    long long start_ms;
    pid_t pid;
    char launch_start_env[64];

    if (access(probe->argv[0], X_OK) < 0) {
        fprintf(stderr, "kde_app_launch_probe %s missing path=%s errno=%d\n",
                probe->name, probe->argv[0], errno);
        return 0;
    }

    start_ms = monotonic_ms();
    probe->launch_start_ms = start_ms;
    probe->launch_start_uptime_ms = uptime_ms();
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "kde_app_launch_probe %s fork_failed errno=%d\n",
                probe->name, errno);
        return 0;
    }
    if (pid == 0) {
        if (strcmp(probe->name, "konsole") == 0) {
            snprintf(launch_start_env, sizeof(launch_start_env), "%lld",
                     start_ms);
            setenv("XV6_KONSOLE_LAUNCH_START_MS", launch_start_env, 1);
            enable_qtmm_stub_for_konsole_child();
        }
        execv(probe->argv[0], probe->argv);
        fprintf(stderr, "kde_app_launch_probe exec_failed app=%s errno=%d\n",
                probe->name, errno);
        _exit(127);
    }
    probe->pid = pid;
    probe->launch_elapsed_ms = monotonic_ms() - start_ms;
    fprintf(stderr,
            "kde_app_launch_probe launch app=%s pid=%ld "
            "launch_start_ms=%lld launch_start_uptime_ms=%lld "
            "elapsed_ms=%lld\n",
            probe->name, (long)pid, probe->launch_start_ms,
            probe->launch_start_uptime_ms, probe->launch_elapsed_ms);
    return 1;
}

static int child_still_running(pid_t pid)
{
    char path[64];
    char buf[512];
    int fd;
    ssize_t n;

    if (pid <= 0)
        return 0;
    snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return kill(pid, 0) == 0;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return kill(pid, 0) == 0;
    buf[n] = '\0';
    if (strstr(buf, "State:\tZ") || strstr(buf, "State:\tX"))
        return 0;
    return 1;
}

static int file_contains_string(const char *path, const char *needle)
{
    char buf[4096];
    char carry[512];
    int carry_len = 0;
    int fd;
    ssize_t n;
    size_t needle_len;

    if (!needle || !needle[0])
        return 0;
    needle_len = strlen(needle);
    if (needle_len >= sizeof(buf) / 2)
        return 0;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    while ((n = read(fd, buf + carry_len, sizeof(buf) - 1 - carry_len)) > 0) {
        int total = carry_len + (int)n;

        buf[total] = '\0';
        if (strstr(buf, needle)) {
            close(fd);
            return 1;
        }
        if ((int)needle_len > 1) {
            carry_len = (int)needle_len - 1;
            if (carry_len > (int)sizeof(carry))
                carry_len = sizeof(carry);
            if (carry_len > total)
                carry_len = total;
            memcpy(carry, buf + total - carry_len, (size_t)carry_len);
            memcpy(buf, carry, (size_t)carry_len);
        } else {
            carry_len = 0;
        }
    }
    close(fd);
    return 0;
}

static int launcher_log_has_argv_url(const char *path, const char *url)
{
    char escaped[2048];
    char line[4096];
    int fd;
    size_t len = 0;
    ssize_t n;
    char c;

    escape_string(escaped, sizeof(escaped), url);
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    while ((n = read(fd, &c, 1)) > 0) {
        if (len + 1 < sizeof(line))
            line[len++] = c;
        if (c != '\n')
            continue;
        line[len] = '\0';
        if (strstr(line, "wayland-chromium-launcher: argv_") &&
            strstr(line, "=\"") &&
            strstr(line, escaped) &&
            strstr(line, "\"\n")) {
            char *value = strstr(line, "=\"");

            if (value) {
                value += 2;
                char *end = strrchr(value, '"');

                if (end) {
                    *end = '\0';
                    if (strcmp(value, escaped) == 0) {
                        close(fd);
                        return 1;
                    }
                }
            }
        }
        len = 0;
    }
    if (len > 0) {
        line[len] = '\0';
        if (strstr(line, "wayland-chromium-launcher: argv_")) {
            char *value = strstr(line, "=\"");

            if (value) {
                value += 2;
                char *end = strrchr(value, '"');

                if (end) {
                    *end = '\0';
                    if (strcmp(value, escaped) == 0) {
                        close(fd);
                        return 1;
                    }
                }
            }
        }
    }
    close(fd);
    return 0;
}

static int chromium_launcher_log_has_wayland_platform_failure(const char *path)
{
    return file_contains_string(path, "Failed to connect to Wayland display") ||
           file_contains_string(path, "Failed to initialize Wayland platform") ||
           file_contains_string(path, "The platform failed to initialize");
}

static void wait_for_chromium_launcher_log(const char *path, const char *url,
                                           int *log_present, int *marker,
                                           int *child_exec, int *url_present,
                                           int *wayland_platform_fail)
{
    int i;

    *log_present = 0;
    *marker = 0;
    *child_exec = 0;
    *url_present = 0;
    *wayland_platform_fail = 0;
    for (i = 0; i < 30; i++) {
        *log_present = access(path, F_OK) == 0;
        if (*log_present) {
            *marker = file_contains_string(path, "launch_marker");
            *child_exec = file_contains_string(path, "child_exec");
            *url_present = launcher_log_has_argv_url(path, url);
            *wayland_platform_fail =
                chromium_launcher_log_has_wayland_platform_failure(path);
        }
        if (*wayland_platform_fail)
            return;
        usleep(100000);
    }
}

static void record_child_status(struct app_probe *probe)
{
    int status;
    pid_t got;

    if (probe->pid <= 0)
        return;
    got = waitpid(probe->pid, &status, WNOHANG);
    if (got == 0) {
        probe->ok = 1;
        return;
    }
    if (got < 0 && errno == ECHILD) {
        probe->ok = kill(probe->pid, 0) == 0;
        return;
    }
    probe->exited = 1;
    if (WIFEXITED(status))
        probe->exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        probe->signal_code = WTERMSIG(status);
    probe->ok = 0;
}

static int has_arg(int argc, char **argv, const char *needle)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], needle) == 0)
            return 1;
    }
    return 0;
}

static const char *arg_value(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "kde_app_launch_probe missing value for %s\n",
                        name);
                exit(64);
            }
            return argv[i + 1];
        }
    }
    return NULL;
}

static int parse_int_arg_or_default(int argc, char **argv, const char *name,
                                    int default_value, int min_value,
                                    int max_value)
{
    const char *value = arg_value(argc, argv, name);
    char *end = NULL;
    long parsed;

    if (!value || !value[0])
        return default_value;
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' ||
        parsed < min_value || parsed > max_value) {
        fprintf(stderr,
                "kde_app_launch_probe invalid integer for %s value=%s "
                "default=%d min=%d max=%d\n",
                name, value, default_value, min_value, max_value);
        return default_value;
    }
    return (int)parsed;
}

static const char *konsole_variant_select(int argc, char **argv)
{
    const char *value = arg_value(argc, argv, "--konsole-variant");

    if (!value || value[0] == '\0')
        value = getenv("KDE_APP_LAUNCH_PROBE_KONSOLE_VARIANT");
    if (!value || value[0] == '\0')
        return "baseline";
    if (strcmp(value, "baseline") == 0 ||
        strcmp(value, "no-profile") == 0 ||
        strcmp(value, "no-hold") == 0 ||
        strcmp(value, "minimal") == 0)
        return value;

    fprintf(stderr,
            "kde_app_launch_probe invalid konsole_variant=%s "
            "using=baseline\n",
            value);
    return "baseline";
}

static int cmdline_is_shell(const char *buf, ssize_t n)
{
    const char *base;

    if (n <= 0)
        return 0;
    base = strrchr(buf, '/');
    base = base ? base + 1 : buf;
    return strcmp(buf, "/bin/sh") == 0 || strcmp(base, "sh") == 0;
}

static int count_shell_processes(void)
{
    DIR *dir = opendir("/proc");
    struct dirent *de;
    int count = 0;

    if (!dir)
        return 0;

    while ((de = readdir(dir)) != NULL) {
        char path[320];
        char buf[256];
        int all_digits = 1;
        int fd;
        ssize_t n;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0)
            continue;
        buf[n] = '\0';
        if (cmdline_is_shell(buf, n))
            count++;
    }

    closedir(dir);
    return count;
}

static int parse_env_int_or_default(const char *name, int fallback, int min,
                                    int max)
{
    const char *value = getenv(name);
    char *end = NULL;
    long parsed;

    if (!value || value[0] == '\0')
        return fallback;
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno || end == value || *end != '\0')
        return fallback;
    if (parsed < min)
        return min;
    if (parsed > max)
        return max;
    return (int)parsed;
}

static int env_bool_or_default(const char *name, int fallback)
{
    const char *value = getenv(name);

    if (!value || value[0] == '\0')
        return fallback;
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
        strcmp(value, "TRUE") == 0 || strcmp(value, "yes") == 0 ||
        strcmp(value, "YES") == 0 || strcmp(value, "on") == 0 ||
        strcmp(value, "ON") == 0)
        return 1;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "FALSE") == 0 || strcmp(value, "no") == 0 ||
        strcmp(value, "NO") == 0 || strcmp(value, "off") == 0 ||
        strcmp(value, "OFF") == 0)
        return 0;
    return fallback;
}

static char *enable_konsole_event_trace_preload(void)
{
    const char *trace_so =
        "/opt/xv6-kde-abi-libs/konsole-wayland-event-trace-preload.so";
    const char *current;
    char *saved;
    char *next;
    size_t len;
    int wayland_trace = env_bool_or_default(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_WAYLAND_EVENT_TRACE", 0);
    int qt_trace = env_bool_or_default(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_QT_EVENT_TRACE", 0);
    int qt_cpp_trace = env_bool_or_default(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_QT_CPP_TRACE", 0);
    int qt_bridge_trace = env_bool_or_default(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_QT_BRIDGE_TRACE", 0);
    int loader_trace = env_bool_or_default(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_LOADER_TRACE", 0);

    if (!wayland_trace && !qt_trace && !qt_cpp_trace && !qt_bridge_trace &&
        !loader_trace)
        return NULL;

    current = getenv("LD_PRELOAD");
    saved = current ? strdup(current) : strdup("");
    if (!saved)
        return NULL;
    if (current && strstr(current, trace_so)) {
        fprintf(stderr,
                "kde_app_launch_probe konsole_wayland_event_trace=%d "
                "konsole_qt_event_trace=%d konsole_qt_cpp_trace=%d "
                "konsole_qt_bridge_trace=%d "
                "konsole_loader_trace=%d "
                "preload=already-present ld_preload=\"%s\"\n",
                wayland_trace, qt_trace, qt_cpp_trace, qt_bridge_trace,
                loader_trace, current);
        return saved;
    }

    len = (current ? strlen(current) : 0) + strlen(trace_so) + 2;
    next = malloc(len);
    if (!next) {
        free(saved);
        return NULL;
    }
    if (current && current[0])
        snprintf(next, len, "%s:%s", current, trace_so);
    else
        snprintf(next, len, "%s", trace_so);
    setenv("LD_PRELOAD", next, 1);
    fprintf(stderr,
            "kde_app_launch_probe konsole_wayland_event_trace=%d "
            "konsole_qt_event_trace=%d konsole_qt_cpp_trace=%d "
            "konsole_qt_bridge_trace=%d "
            "konsole_loader_trace=%d "
            "preload=append trace_so=%s ld_preload=\"%s\"\n",
            wayland_trace, qt_trace, qt_cpp_trace, qt_bridge_trace,
            loader_trace, trace_so, next);
    free(next);
    return saved;
}

static void restore_konsole_event_trace_preload(char *saved)
{
    if (!saved)
        return;
    if (saved[0])
        setenv("LD_PRELOAD", saved, 1);
    else
        unsetenv("LD_PRELOAD");
    free(saved);
}

static int read_text_file(const char *path, char *buf, size_t size)
{
    int fd;
    ssize_t n;

    if (size == 0)
        return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        buf[0] = '\0';
        return 0;
    }
    n = read(fd, buf, size - 1);
    close(fd);
    if (n <= 0) {
        buf[0] = '\0';
        return 0;
    }
    buf[n] = '\0';
    return (int)n;
}

static int process_ppid(const char *pid_name)
{
    char path[320];
    char buf[512];
    char comm[128];
    char state;
    int pid;
    int ppid;

    snprintf(path, sizeof(path), "/proc/%s/stat", pid_name);
    if (!read_text_file(path, buf, sizeof(buf)))
        return -1;
    if (sscanf(buf, "%d (%127[^)]) %c %d", &pid, comm, &state, &ppid) != 4)
        return -1;
    return ppid;
}

static const char *path_basename(const char *path)
{
    const char *base;

    if (!path)
        return "";
    base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static void normalize_cmdline(char *buf, int n)
{
    if (n <= 0)
        return;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\0')
            buf[i] = ' ';
    }
    buf[n] = '\0';
}

static int cmdline_option_value(const char *cmdline, const char *prefix,
                                char *out, size_t out_size)
{
    const char *p;
    size_t prefix_len;

    if (out_size == 0 || !cmdline || !prefix)
        return 0;
    p = cmdline;
    prefix_len = strlen(prefix);

    while ((p = strstr(p, prefix)) != NULL) {
        const char *start;
        const char *end;
        size_t len;

        if (p != cmdline && p[-1] != ' ') {
            p++;
            continue;
        }
        start = p + prefix_len;
        end = start;
        while (*end && *end != ' ')
            end++;
        len = (size_t)(end - start);
        if (len >= out_size)
            len = out_size - 1;
        memcpy(out, start, len);
        out[len] = '\0';
        return 1;
    }
    return 0;
}

static int chromium_browser_name_matches(const char *name)
{
    return name &&
           (strcmp(name, "wayland-chromium") == 0 ||
            strcmp(name, "chrome") == 0 ||
            strcmp(name, "chromium") == 0 ||
            strcmp(name, "chromium-browser") == 0);
}

static int chromium_process_name_matches(const char *name)
{
    return chromium_browser_name_matches(name) ||
           (name && (strncmp(name, "chrome_", 7) == 0 ||
                     strncmp(name, "chrome-", 7) == 0));
}

static int cmdline_argv0_has_chromium_name(const char *cmdline, int n)
{
    char argv0[1024];
    int len = 0;

    if (!cmdline || n <= 0)
        return 0;
    while (len < n && cmdline[len] != '\0' && len + 1 < (int)sizeof(argv0)) {
        argv0[len] = cmdline[len];
        len++;
    }
    argv0[len] = '\0';
    return chromium_process_name_matches(path_basename(argv0));
}

static int cmdline_option_present(const char *cmdline, const char *option)
{
    const char *p;
    size_t option_len;

    if (!cmdline || !option)
        return 0;
    p = cmdline;
    option_len = strlen(option);
    while ((p = strstr(p, option)) != NULL) {
        char next = p[option_len];

        if ((p == cmdline || p[-1] == ' ') && (next == '\0' || next == ' '))
            return 1;
        p++;
    }
    return 0;
}

static void cmdline_argv0_basename(const char *cmdline, int n, char *out,
                                   size_t out_size)
{
    char argv0[1024];
    int len = 0;
    const char *base;

    if (out_size == 0)
        return;
    out[0] = '\0';
    if (!cmdline || n <= 0)
        return;
    while (len < n && cmdline[len] != '\0' && len + 1 < (int)sizeof(argv0)) {
        argv0[len] = cmdline[len];
        len++;
    }
    argv0[len] = '\0';
    base = path_basename(argv0);
    len = (int)strlen(base);
    if (len >= (int)out_size)
        len = (int)out_size - 1;
    memcpy(out, base, (size_t)len);
    out[len] = '\0';
}

static int is_root_chrome_process(const char *cmdline, int n, const char *comm)
{
    char argv0[1024];
    int len = 0;
    const char *base;

    while (len < n && cmdline[len] != '\0' && len + 1 < (int)sizeof(argv0)) {
        argv0[len] = cmdline[len];
        len++;
    }
    argv0[len] = '\0';
    base = path_basename(argv0);

    if (chromium_browser_name_matches(base))
        return 1;
    if ((!cmdline || n <= 0) && chromium_browser_name_matches(comm))
        return 1;
    return 0;
}

static int derive_chromium_role(const char *cmdline, int n, const char *comm,
                                char *role, size_t role_size,
                                char *role_source, size_t role_source_size,
                                char *type_arg, size_t type_arg_size)
{
    int start = 0;

    if (role_size == 0)
        return 0;
    role[0] = '\0';
    if (role_source_size > 0)
        role_source[0] = '\0';
    if (type_arg_size > 0)
        type_arg[0] = '\0';
    for (int i = 0; i <= n; i++) {
        if (i == n || cmdline[i] == '\0') {
            static const char prefix[] = "--type=";
            int len = i - start;

            if (len > (int)sizeof(prefix) - 1 &&
                memcmp(cmdline + start, prefix, sizeof(prefix) - 1) == 0) {
                int role_len = len - ((int)sizeof(prefix) - 1);

                if (role_len >= (int)role_size)
                    role_len = (int)role_size - 1;
                memcpy(role, cmdline + start + sizeof(prefix) - 1, role_len);
                role[role_len] = '\0';
                if (type_arg_size > 0) {
                    size_t copy_len = (size_t)role_len;

                    if (copy_len >= type_arg_size)
                        copy_len = type_arg_size - 1;
                    memcpy(type_arg, role, copy_len);
                    type_arg[copy_len] = '\0';
                }
                if (role_source_size > 0)
                    snprintf(role_source, role_source_size, "%s",
                             "type_arg");
                return role[0] != '\0';
            }
            start = i + 1;
        }
    }

    if (is_root_chrome_process(cmdline, n, comm)) {
        snprintf(role, role_size, "%s", "browser");
        if (type_arg_size > 0)
            snprintf(type_arg, type_arg_size, "%s", "missing");
        if (role_source_size > 0)
            snprintf(role_source, role_source_size, "%s", "root_chrome");
        return 1;
    }
    snprintf(role, role_size, "%s", "unknown");
    if (type_arg_size > 0)
        snprintf(type_arg, type_arg_size, "%s", "missing");
    if (role_source_size > 0)
        snprintf(role_source, role_source_size, "%s", "unknown");
    return 1;
}

static int chromium_process_interesting(const char *cmdline, int n,
                                        const char *comm)
{
    if (cmdline_argv0_has_chromium_name(cmdline, n))
        return 1;
    return chromium_process_name_matches(comm);
}

static int chromium_env_name_interesting(const char *entry, int n)
{
    static const char *names[] = {
        "WAYLAND_DISPLAY=",
        "DISPLAY=",
        "XDG_RUNTIME_DIR=",
        "OZONE_PLATFORM=",
        "EGL_PLATFORM=",
        "GALLIUM_DRIVER=",
        "MESA_LOADER_DRIVER_OVERRIDE=",
        "LIBGL_DRIVERS_PATH=",
        "GBM_BACKENDS_PATH=",
        "LD_LIBRARY_PATH=",
        "LD_PRELOAD=",
        "WAYLAND_CHROMIUM_EGL_TRACE=",
        "WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION=",
        "SIMDUTF_FORCE_IMPLEMENTATION=",
        "CHROMIUM_EGL_TRACE=",
        "CHROMIUM_EGL_TRACE_LOG=",
        "LIBVA_DRIVERS_PATH=",
        "LIBVA_DRIVER_NAME=",
        "CHROME_LOG_FILE=",
    };

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t len = strlen(names[i]);

        if (n >= (int)len && memcmp(entry, names[i], len) == 0)
            return 1;
    }
    return 0;
}

static void chromium_evidence_env(const char *phase, const char *pid_name,
                                  const char *role)
{
    char path[320];
    char buf[8192];
    int fd;
    ssize_t n;
    int start = 0;
    int emitted = 0;
    int truncated = 0;

    if (!chromium_evidence)
        return;

    snprintf(path, sizeof(path), "/proc/%s/environ", pid_name);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence env phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fprintf(chromium_evidence, "\" open_errno=%d ", errno);
        fprint_escaped(chromium_evidence, strerror(errno), -1);
        fputc('\n', chromium_evidence);
        return;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    int saved_errno = errno;
    close(fd);
    if (n < 0) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence env phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fprintf(chromium_evidence, "\" read_errno=%d ", saved_errno);
        fprint_escaped(chromium_evidence, strerror(saved_errno), -1);
        fputc('\n', chromium_evidence);
        return;
    }
    truncated = n == (ssize_t)sizeof(buf) - 1;
    buf[n] = '\0';

    for (int i = 0; i <= n; i++) {
        if (i != n && buf[i] != '\0')
            continue;
        if (i > start && chromium_env_name_interesting(buf + start,
                                                       i - start)) {
            fprintf(chromium_evidence,
                    "kde_chromium_process_evidence env phase=%s pid=%s "
                    "role=\"",
                    phase, pid_name);
            fprint_escaped(chromium_evidence, role, -1);
            fputs("\" entry=\"", chromium_evidence);
            fprint_escaped(chromium_evidence, buf + start, i - start);
            fputs("\"\n", chromium_evidence);
            emitted++;
        }
        start = i + 1;
    }

    if (truncated) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence env phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fputs("\" truncated=1\n", chromium_evidence);
    }
    if (!emitted) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence env phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fputs("\" entries=0\n", chromium_evidence);
    }
}

static int chromium_map_line_interesting(const char *line)
{
    static const char *needles[] = {
        "libEGL",
        "libGLES",
        "libGLX",
        "libGLdispatch",
        "libOpenGL",
        "libgbm",
        "libdrm",
        "libgallium",
        "chromium-egl-trace-preload",
        "/chrome-linux64/chrome_crashpad_handler",
        "/dri/",
        "/gbm/",
        "swiftshader",
    };

    const char *chrome_path = strstr(line, "/chrome-linux64/chrome");
    if (chrome_path && strcmp(chrome_path, "/chrome-linux64/chrome") == 0)
        return 1;
    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        if (strstr(line, needles[i]))
            return 1;
    }
    return 0;
}

static int chromium_evidence_full_maps_enabled(void)
{
    const char *value = getenv("KDE_CHROMIUM_EVIDENCE_FULL_MAPS");
    char lower[16];
    size_t i;

    if (!value || value[0] == '\0')
        return 0;
    for (i = 0; i + 1 < sizeof(lower) && value[i]; i++)
        lower[i] = (char)tolower((unsigned char)value[i]);
    lower[i] = '\0';
    if (strcmp(lower, "0") == 0 || strcmp(lower, "off") == 0 ||
        strcmp(lower, "false") == 0 || strcmp(lower, "no") == 0)
        return 0;
    return 1;
}

static void chromium_evidence_maps(const char *phase, const char *pid_name,
                                   const char *role)
{
    char path[320];
    FILE *fp;
    char line[1024];
    int emitted = 0;

    if (!chromium_evidence)
        return;

    snprintf(path, sizeof(path), "/proc/%s/maps", pid_name);
    fp = fopen(path, "r");
    if (!fp) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence maps phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fprintf(chromium_evidence, "\" open_errno=%d ", errno);
        fprint_escaped(chromium_evidence, strerror(errno), -1);
        fputc('\n', chromium_evidence);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (!chromium_evidence_full_maps_enabled() &&
            !chromium_map_line_interesting(line))
            continue;
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence maps phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fputs("\" line=\"", chromium_evidence);
        fprint_escaped(chromium_evidence, line, -1);
        fputs("\"\n", chromium_evidence);
        emitted++;
    }
    fclose(fp);

    if (!emitted) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence maps phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fputs("\" entries=0\n", chromium_evidence);
    }
}

static int chromium_fd_target_interesting(const char *target)
{
    static const char *needles[] = {
        "/dev/dri/",
        "/dev/gpu",
        "anon_inode:",
        "renderD",
        "card",
        "drm",
        "pipe:",
        "socket:",
        "virtio",
    };

    if (!target)
        return 0;
    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        if (strstr(target, needles[i]))
            return 1;
    }
    return 0;
}

static void chromium_evidence_fds(const char *phase, const char *pid_name,
                                  const char *role)
{
    char fd_dir[320];
    DIR *dir;
    struct dirent *de;
    int readlink_errors = 0;
    int emitted = 0;
    int scanned = 0;

    if (!chromium_evidence)
        return;

    snprintf(fd_dir, sizeof(fd_dir), "/proc/%s/fd", pid_name);
    dir = opendir(fd_dir);
    if (!dir) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence fd phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fprintf(chromium_evidence, "\" opendir_errno=%d ", errno);
        fprint_escaped(chromium_evidence, strerror(errno), -1);
        fputc('\n', chromium_evidence);
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char link_path[768];
        char target[512];
        ssize_t n;

        if (de->d_name[0] == '.')
            continue;
        scanned++;
        if (snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir,
                     de->d_name) >= (int)sizeof(link_path)) {
            continue;
        }
        n = readlink(link_path, target, sizeof(target) - 1);
        if (n < 0) {
            readlink_errors++;
            continue;
        }
        target[n] = '\0';
        if (!chromium_fd_target_interesting(target))
            continue;
        if (emitted >= 32)
            continue;
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence fd phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fputs("\" fd=\"", chromium_evidence);
        fprint_escaped(chromium_evidence, de->d_name, -1);
        fputs("\" target=\"", chromium_evidence);
        fprint_escaped(chromium_evidence, target, -1);
        fputs("\"\n", chromium_evidence);
        emitted++;
    }
    closedir(dir);

    fprintf(chromium_evidence,
            "kde_chromium_process_evidence fd_summary phase=%s pid=%s role=\"",
            phase, pid_name);
    fprint_escaped(chromium_evidence, role, -1);
    fprintf(chromium_evidence,
            "\" scanned=%d emitted=%d readlink_errors=%d cap=32\n",
            scanned, emitted, readlink_errors);
}

static void trim_trailing_space(char *buf)
{
    size_t len;

    if (!buf)
        return;
    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
        buf[--len] = '\0';
    }
}

static void chromium_evidence_proc_text(const char *phase,
                                        const char *pid_name,
                                        const char *role, const char *kind,
                                        const char *relpath,
                                        size_t max_bytes)
{
    char path[384];
    char buf[1024];
    int n;

    if (!chromium_evidence || max_bytes == 0)
        return;
    if (max_bytes > sizeof(buf))
        max_bytes = sizeof(buf);
    snprintf(path, sizeof(path), "/proc/%s/%s", pid_name, relpath);
    n = read_text_file(path, buf, max_bytes);
    if (n <= 0) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence wait phase=%s pid=%s role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fputs("\" kind=\"", chromium_evidence);
        fprint_escaped(chromium_evidence, kind, -1);
        fputs("\" rel=\"", chromium_evidence);
        fprint_escaped(chromium_evidence, relpath, -1);
        fputs("\" bytes=0\n", chromium_evidence);
        return;
    }
    trim_trailing_space(buf);
    fprintf(chromium_evidence,
            "kde_chromium_process_evidence wait phase=%s pid=%s role=\"",
            phase, pid_name);
    fprint_escaped(chromium_evidence, role, -1);
    fputs("\" kind=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, kind, -1);
    fputs("\" rel=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, relpath, -1);
    fprintf(chromium_evidence, "\" bytes=%d text=\"", n);
    fprint_escaped(chromium_evidence, buf, -1);
    fputs("\"\n", chromium_evidence);
}

static void chromium_evidence_thread_waits(const char *phase,
                                           const char *pid_name,
                                           const char *role)
{
    char task_dir[320];
    DIR *dir;
    struct dirent *de;
    int emitted = 0;

    if (!chromium_evidence)
        return;
    snprintf(task_dir, sizeof(task_dir), "/proc/%s/task", pid_name);
    dir = opendir(task_dir);
    if (!dir) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence thread_wait phase=%s pid=%s "
                "role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fprintf(chromium_evidence, "\" opendir_errno=%d ", errno);
        fprint_escaped(chromium_evidence, strerror(errno), -1);
        fputc('\n', chromium_evidence);
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char rel[384];
        int all_digits = 1;

        if (de->d_name[0] == '.')
            continue;
        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        snprintf(rel, sizeof(rel), "task/%s/comm", de->d_name);
        chromium_evidence_proc_text(phase, pid_name, role, "thread-comm",
                                    rel, 128);
        snprintf(rel, sizeof(rel), "task/%s/status", de->d_name);
        chromium_evidence_proc_text(phase, pid_name, role, "thread-status",
                                    rel, 1024);
        snprintf(rel, sizeof(rel), "task/%s/wchan", de->d_name);
        chromium_evidence_proc_text(phase, pid_name, role, "thread-wchan",
                                    rel, 128);
        snprintf(rel, sizeof(rel), "task/%s/syscall", de->d_name);
        chromium_evidence_proc_text(phase, pid_name, role, "thread-syscall",
                                    rel, 256);
        emitted++;
    }
    closedir(dir);

    if (!emitted) {
        fprintf(chromium_evidence,
                "kde_chromium_process_evidence thread_wait phase=%s pid=%s "
                "role=\"",
                phase, pid_name);
        fprint_escaped(chromium_evidence, role, -1);
        fputs("\" entries=0\n", chromium_evidence);
    }
}

static void chromium_evidence_wait_state(const char *phase,
                                         const char *pid_name,
                                         const char *role)
{
    chromium_evidence_proc_text(phase, pid_name, role, "stat", "stat", 512);
    chromium_evidence_proc_text(phase, pid_name, role, "status", "status",
                                1024);
    chromium_evidence_proc_text(phase, pid_name, role, "wchan", "wchan", 128);
    chromium_evidence_proc_text(phase, pid_name, role, "syscall", "syscall",
                                256);
    chromium_evidence_proc_text(phase, pid_name, role, "stack", "stack", 512);
    chromium_evidence_thread_waits(phase, pid_name, role);
}

static void chromium_evidence_process(const char *phase, const char *pid_name,
                                      int ppid, const char *comm,
                                      const char *cmdline_raw, int cmd_n,
                                      const char *cmdline)
{
    char role[128];
    char role_source[32];
    char type_arg[128];
    char argv0_base[128];

    if (!chromium_evidence ||
        !chromium_process_interesting(cmdline_raw, cmd_n, comm))
        return;

    derive_chromium_role(cmdline_raw, cmd_n, comm, role, sizeof(role),
                         role_source, sizeof(role_source), type_arg,
                         sizeof(type_arg));
    cmdline_argv0_basename(cmdline_raw, cmd_n, argv0_base,
                           sizeof(argv0_base));
    fprintf(chromium_evidence,
            "kde_chromium_process_evidence process phase=%s pid=%s ppid=%d "
            "comm=\"",
            phase, pid_name, ppid);
    fprint_escaped(chromium_evidence, comm ? comm : "", -1);
    fputs("\" role=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, role, -1);
    fputs("\" cmd=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, cmdline ? cmdline : "", -1);
    fputs("\" argv=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, cmdline_raw, cmd_n);
    fputs("\" role_source=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, role_source, -1);
    fputs("\" type_arg=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, type_arg, -1);
    fputs("\" argv0=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, argv0_base, -1);
    fprintf(chromium_evidence, "\" cmdline_bytes=%d cmdline_truncated=%d\n",
            cmd_n, cmd_n >= 8191 ? 1 : 0);
    chromium_evidence_env(phase, pid_name, role);
    chromium_evidence_maps(phase, pid_name, role);
    chromium_evidence_fds(phase, pid_name, role);
    chromium_evidence_wait_state(phase, pid_name, role);
    fflush(chromium_evidence);
}

static void chromium_evidence_process_fast(const char *phase,
                                           const char *pid_name, int ppid,
                                           const char *comm,
                                           const char *cmdline_raw, int cmd_n,
                                           const char *cmdline)
{
    char role[128];
    char role_source[32];
    char type_arg[128];
    char argv0_base[128];
    char use_gl[96] = "missing";
    char use_angle[96] = "missing";
    char gpu_preferences[512] = "";
    int gpu_preferences_b64_len = 0;
    int disable_gpu_early_init;

    if (!chromium_evidence ||
        !chromium_process_interesting(cmdline_raw, cmd_n, comm))
        return;

    derive_chromium_role(cmdline_raw, cmd_n, comm, role, sizeof(role),
                         role_source, sizeof(role_source), type_arg,
                         sizeof(type_arg));
    cmdline_argv0_basename(cmdline_raw, cmd_n, argv0_base,
                           sizeof(argv0_base));
    cmdline_option_value(cmdline, "--use-gl=", use_gl, sizeof(use_gl));
    cmdline_option_value(cmdline, "--use-angle=", use_angle,
                         sizeof(use_angle));
    if (cmdline_option_value(cmdline, "--gpu-preferences=",
                             gpu_preferences, sizeof(gpu_preferences))) {
        gpu_preferences_b64_len = (int)strlen(gpu_preferences);
    }
    disable_gpu_early_init =
        cmdline_option_present(cmdline, "--disable-gpu-early-init");

    fprintf(chromium_evidence,
            "kde_chromium_process_evidence fast phase=%s pid=%s ppid=%d "
            "comm=\"",
            phase, pid_name, ppid);
    fprint_escaped(chromium_evidence, comm ? comm : "", -1);
    fputs("\" role=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, role, -1);
    fputs("\" cmd=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, cmdline ? cmdline : "", -1);
    fputs("\" role_source=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, role_source, -1);
    fputs("\" type_arg=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, type_arg, -1);
    fputs("\" argv0=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, argv0_base, -1);
    fputs("\" use_gl=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, use_gl, -1);
    fputs("\" use_angle=\"", chromium_evidence);
    fprint_escaped(chromium_evidence, use_angle, -1);
    fprintf(chromium_evidence,
            "\" gpu_preferences_b64_len=%d disable_gpu_early_init=%d "
            "cmdline_bytes=%d cmdline_truncated=%d gpu_preferences=\"",
            gpu_preferences_b64_len, disable_gpu_early_init, cmd_n,
            cmd_n >= 8191 ? 1 : 0);
    fprint_escaped(chromium_evidence, gpu_preferences, -1);
    fputs("\"\n", chromium_evidence);
    fflush(chromium_evidence);
}

static int cmdline_interesting(const char *cmdline, const char *comm)
{
    static const char *needles[] = {
        "konsole", "xterm", "kde-terminal-launcher",
        "kdeinit", "klauncher", "kded", "dbus", "sh",
        "dolphin", "kate", "kwrite",
        "kio", "kioworker", "kioslave",
    };

    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        if ((cmdline && strstr(cmdline, needles[i])) ||
            (comm && strstr(comm, needles[i]))) {
            return 1;
        }
    }
    return 0;
}

static void dump_process_fds(const char *phase, const char *pid_name)
{
    char fd_dir[320];
    DIR *dir;
    struct dirent *de;

    snprintf(fd_dir, sizeof(fd_dir), "/proc/%s/fd", pid_name);
    dir = opendir(fd_dir);
    if (!dir) {
        fprintf(stderr,
                "kde_app_launch_probe fd phase=%s pid=%s opendir_errno=%d %s\n",
                phase, pid_name, errno, strerror(errno));
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char link_path[768];
        char target[512];
        ssize_t n;
        int path_len;

        if (de->d_name[0] == '.')
            continue;
        path_len = snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir,
                            de->d_name);
        if (path_len < 0 || (size_t)path_len >= sizeof(link_path)) {
            fprintf(stderr,
                    "kde_app_launch_probe fd phase=%s pid=%s fd=%s path_truncated=1\n",
                    phase, pid_name, de->d_name);
            continue;
        }
        n = readlink(link_path, target, sizeof(target) - 1);
        if (n < 0) {
            fprintf(stderr,
                    "kde_app_launch_probe fd phase=%s pid=%s fd=%s readlink_errno=%d %s\n",
                    phase, pid_name, de->d_name, errno, strerror(errno));
            continue;
        }
        target[n] = '\0';
        fprintf(stderr,
                "kde_app_launch_probe fd phase=%s pid=%s fd=%s target=%s\n",
                phase, pid_name, de->d_name, target);
    }

    closedir(dir);
}

static void dump_process_file(const char *phase, const char *pid_name,
                              const char *name)
{
    char path[320];
    char buf[1024];
    int fd;
    ssize_t n;

    snprintf(path, sizeof(path), "/proc/%s/%s", pid_name, name);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr,
                "kde_app_launch_probe proc phase=%s pid=%s file=%s "
                "open_errno=%d %s\n",
                phase, pid_name, name, errno, strerror(errno));
        return;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        fprintf(stderr,
                "kde_app_launch_probe proc phase=%s pid=%s file=%s "
                "read_errno=%d %s\n",
                phase, pid_name, name, errno, strerror(errno));
        close(fd);
        return;
    }
    close(fd);
    buf[n] = '\0';

    fprintf(stderr,
            "kde_app_launch_probe proc phase=%s pid=%s file=%s bytes=%ld "
            "begin\n%s\nkde_app_launch_probe proc end\n",
            phase, pid_name, name, (long)n, buf);
}

static void dump_process_details(const char *phase, const char *pid_name)
{
    dump_process_fds(phase, pid_name);
    dump_process_file(phase, pid_name, "stat");
    dump_process_file(phase, pid_name, "status");
    dump_process_file(phase, pid_name, "wchan");
    dump_process_file(phase, pid_name, "syscall");
    dump_process_file(phase, pid_name, "stack");
}

static void dump_thread_file(const char *phase, const char *pid_name,
                             const char *tid_name, const char *name)
{
    char rel[384];

    snprintf(rel, sizeof(rel), "task/%s/%s", tid_name, name);
    dump_process_file(phase, pid_name, rel);
}

static void dump_wait_fd_target(const char *phase, const char *pid_name,
                                const char *kind, int fd)
{
    char link_path[512];
    char target[512];
    ssize_t n;

    snprintf(link_path, sizeof(link_path), "/proc/%s/fd/%d", pid_name, fd);
    n = readlink(link_path, target, sizeof(target) - 1);
    if (n < 0) {
        fprintf(stderr,
                "kde_app_launch_probe %s phase=%s pid=%s fd=%d "
                "readlink_errno=%d %s\n",
                kind, phase, pid_name, fd, errno, strerror(errno));
        return;
    }
    target[n] = '\0';
    fprintf(stderr,
            "kde_app_launch_probe %s phase=%s pid=%s fd=%d target=%s\n",
            kind, phase, pid_name, fd, target);
}

static void konsole_phase_samples_init(struct konsole_phase_samples *samples)
{
    samples->ptmx_first_ms = -1;
    samples->ptmx_first_uptime_ms = -1;
    samples->pts_first_ms = -1;
    samples->pts_first_uptime_ms = -1;
    samples->bash_first_ms = -1;
    samples->bash_first_uptime_ms = -1;
    samples->ptmx_fd = -1;
    samples->pts_fd = -1;
    samples->ptmx_target[0] = '\0';
    samples->pts_target[0] = '\0';
}

static void copy_target(char *dst, size_t dst_size, const char *src)
{
    size_t i;

    if (dst_size == 0)
        return;
    if (!src)
        src = "";
    for (i = 0; i + 1 < dst_size && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int target_is_ptmx(const char *target)
{
    return target && (strstr(target, "/dev/ptmx") ||
                      strstr(target, "ptmx") ||
                      strstr(target, "pty master"));
}

static int target_is_pts(const char *target)
{
    return target && (strstr(target, "/dev/pts/") ||
                      strstr(target, "pts/") ||
                      strstr(target, "pty slave"));
}

static void sample_konsole_pty_phase(pid_t pid, long long launch_start_ms,
                                     struct konsole_phase_samples *samples)
{
    char fd_dir[320];
    DIR *dir;
    struct dirent *de;
    int pid_int = (int)pid;

    if (pid <= 0 || !samples)
        return;
    if (samples->ptmx_first_ms >= 0 && samples->pts_first_ms >= 0)
        return;

    snprintf(fd_dir, sizeof(fd_dir), "/proc/%ld/fd", (long)pid);
    dir = opendir(fd_dir);
    if (!dir)
        return;

    while ((de = readdir(dir)) != NULL) {
        char link_path[768];
        char target[512];
        char *end;
        long fd_long;
        ssize_t n;
        int fd;
        long long now_ms;
        long long now_uptime_ms;

        if (de->d_name[0] == '.')
            continue;
        errno = 0;
        fd_long = strtol(de->d_name, &end, 10);
        if (errno || !end || *end != '\0' || fd_long < 0 || fd_long > INT_MAX)
            continue;
        fd = (int)fd_long;

        snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir, de->d_name);
        n = readlink(link_path, target, sizeof(target) - 1);
        if (n < 0)
            continue;
        target[n] = '\0';
        now_ms = monotonic_ms();
        now_uptime_ms = uptime_ms();

        if (samples->ptmx_first_ms < 0 && target_is_ptmx(target)) {
            samples->ptmx_first_ms = now_ms;
            samples->ptmx_first_uptime_ms = now_uptime_ms;
            samples->ptmx_fd = fd;
            copy_target(samples->ptmx_target, sizeof(samples->ptmx_target),
                        target);
            fprintf(stderr,
                    "kde_app_launch_probe konsole_phase_detail "
                    "phase=pty-ptmx pid=%d fd=%d since_launch_ms=%lld "
                    "pty_ptmx_uptime_ms=%lld target=\"",
                    pid_int, fd, now_ms - launch_start_ms, now_uptime_ms);
            fprint_escaped(stderr, target, -1);
            fputs("\"\n", stderr);
        }
        if (samples->pts_first_ms < 0 && target_is_pts(target)) {
            samples->pts_first_ms = now_ms;
            samples->pts_first_uptime_ms = now_uptime_ms;
            samples->pts_fd = fd;
            copy_target(samples->pts_target, sizeof(samples->pts_target),
                        target);
            fprintf(stderr,
                    "kde_app_launch_probe konsole_phase_detail "
                    "phase=pty-pts pid=%d fd=%d since_launch_ms=%lld "
                    "pty_pts_uptime_ms=%lld target=\"",
                    pid_int, fd, now_ms - launch_start_ms, now_uptime_ms);
            fprint_escaped(stderr, target, -1);
            fputs("\"\n", stderr);
        }
        if (samples->ptmx_first_ms >= 0 && samples->pts_first_ms >= 0)
            break;
    }

    closedir(dir);
}

static int extract_ll_key(const char *text, const char *key, long long *value)
{
    const char *p;
    char *end;
    long long parsed;
    size_t key_len;

    if (!text || !key || !value)
        return 0;
    key_len = strlen(key);
    p = text;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == text || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n') &&
            p[key_len] == '=') {
            errno = 0;
            parsed = strtoll(p + key_len + 1, &end, 10);
            if (!errno && end != p + key_len + 1) {
                *value = parsed;
                return 1;
            }
        }
        p += key_len;
    }
    return 0;
}

static int process_comm_contains(pid_t pid, const char *needle)
{
    char path[96];
    char buf[256];
    int n;

    if (pid <= 0 || !needle)
        return 0;
    snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
    n = read_text_file(path, buf, sizeof(buf));
    if (n <= 0)
        return 0;
    return strstr(buf, needle) != NULL;
}

static int wait_for_process_comm(pid_t pid, const char *needle, int timeout_ms,
                                 int interval_ms, long long launch_start_ms,
                                 long long wrapper_start_ms,
                                 long long *found_ms,
                                 long long *found_uptime_ms)
{
    long long start_ms = monotonic_ms();

    if (interval_ms <= 0)
        interval_ms = 20;
    for (;;) {
        long long now_ms = monotonic_ms();
        long long now_uptime_ms = uptime_ms();

        if (process_comm_contains(pid, needle)) {
            if (found_ms)
                *found_ms = now_ms;
            if (found_uptime_ms)
                *found_uptime_ms = now_uptime_ms;
            fprintf(stderr,
                    "kde_app_launch_probe konsole_phase_detail "
                    "phase=bash-start pid=%ld since_launch_ms=%lld "
                    "since_wrapper_ms=%lld bash_start_uptime_ms=%lld "
                    "comm=%s\n",
                    (long)pid, now_ms - launch_start_ms,
                    wrapper_start_ms > 0 ? now_ms - wrapper_start_ms : -1,
                    now_uptime_ms, needle);
            return 1;
        }
        if (now_ms - start_ms >= timeout_ms)
            break;
        usleep((useconds_t)interval_ms * 1000);
    }
    fprintf(stderr,
            "kde_app_launch_probe konsole_phase_detail "
            "phase=bash-start status=timeout pid=%ld timeout_ms=%d\n",
            (long)pid, timeout_ms);
    return 0;
}

static void dump_fd_table(const char *phase, const char *pid_name, int max_fds)
{
    char dir_path[320];
    DIR *dir;
    struct dirent *de;
    int count = 0;

    snprintf(dir_path, sizeof(dir_path), "/proc/%s/fd", pid_name);
    dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr,
                "kde_app_launch_probe fdtable phase=%s pid=%s "
                "opendir_errno=%d %s\n",
                phase, pid_name, errno, strerror(errno));
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        int all_digits = 1;
        char link_path[768];
        char target[512];
        ssize_t n;

        if (de->d_name[0] == '.')
            continue;
        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;
        if (count >= max_fds)
            break;
        count++;

        snprintf(link_path, sizeof(link_path), "/proc/%s/fd/%s",
                 pid_name, de->d_name);
        n = readlink(link_path, target, sizeof(target) - 1);
        if (n < 0) {
            fprintf(stderr,
                    "kde_app_launch_probe fdtable phase=%s pid=%s fd=%s "
                    "readlink_errno=%d %s\n",
                    phase, pid_name, de->d_name, errno, strerror(errno));
            continue;
        }
        target[n] = '\0';
        fprintf(stderr,
                "kde_app_launch_probe fdtable phase=%s pid=%s fd=%s "
                "target=%s\n",
                phase, pid_name, de->d_name, target);
    }

    closedir(dir);
}

static void dump_poll_fd_target(const char *phase, const char *pid_name, int fd)
{
    dump_wait_fd_target(phase, pid_name, "pollfd", fd);
}

static void dump_pc_map(const char *phase, const char *pid_name,
                        const char *tid_name, unsigned long long pc)
{
    char maps_path[384];
    char line[1024];
    FILE *fp;

    snprintf(maps_path, sizeof(maps_path), "/proc/%s/maps", pid_name);
    fp = fopen(maps_path, "r");
    if (!fp)
        return;
    while (fgets(line, sizeof(line), fp)) {
        unsigned long long start, end;

        if (sscanf(line, "%llx-%llx", &start, &end) == 2 &&
            pc >= start && pc < end) {
            size_t len = strlen(line);

            while (len > 0 && (line[len - 1] == '\n' ||
                               line[len - 1] == '\r'))
                line[--len] = '\0';
            fprintf(stderr,
                    "kde_app_launch_probe pcmap phase=%s pid=%s tid=%s "
                    "pc=0x%llx map=\"%s\"\n",
                    phase, pid_name, tid_name, pc, line);
            break;
        }
    }
    fclose(fp);
}

static void dump_thread_wait_detail(const char *phase, const char *pid_name,
                                    const char *tid_name)
{
    char syscall_path[384];
    char mem_path[384];
    char syscall_buf[256];
    unsigned long long nr, arg0, arg1, arg2, arg3, arg4, arg5, sp, pc;
    int memfd;

    snprintf(syscall_path, sizeof(syscall_path), "/proc/%s/task/%s/syscall",
             pid_name, tid_name);
    if (!read_text_file(syscall_path, syscall_buf, sizeof(syscall_buf)))
        return;
    if (sscanf(syscall_buf, "%llu %llx %llx %llx %llx %llx %llx %llx %llx",
               &nr, &arg0, &arg1, &arg2, &arg3, &arg4, &arg5, &sp, &pc) != 9)
        return;
    dump_pc_map(phase, pid_name, tid_name, pc);

    snprintf(mem_path, sizeof(mem_path), "/proc/%s/task/%s/mem", pid_name,
             tid_name);
    memfd = open(mem_path, O_RDONLY | O_CLOEXEC);
    if (memfd < 0) {
        fprintf(stderr,
                "kde_app_launch_probe waitdetail phase=%s pid=%s tid=%s "
                "syscall=%llu mem_open_errno=%d %s\n",
                phase, pid_name, tid_name, nr, errno, strerror(errno));
        return;
    }

    if (nr == 7 || nr == 271) {
        struct local_pollfd {
            int fd;
            short events;
            short revents;
        } fds[16];
        unsigned long long nfds = arg1;
        if (nfds > 16)
            nfds = 16;
        ssize_t n = pread(memfd, fds, nfds * sizeof(fds[0]), (off_t)arg0);
        fprintf(stderr,
                "kde_app_launch_probe waitdetail phase=%s pid=%s tid=%s "
                "syscall=%s fds_addr=0x%llx nfds=%llu read=%ld errno=%d %s\n",
                phase, pid_name, tid_name, nr == 7 ? "poll" : "ppoll",
                arg0, arg1, (long)n, errno, strerror(errno));
        if (n > 0) {
            int got = (int)(n / (ssize_t)sizeof(fds[0]));
            for (int i = 0; i < got; i++) {
                fprintf(stderr,
                        "kde_app_launch_probe pollfd phase=%s pid=%s tid=%s "
                        "idx=%d fd=%d events=0x%x revents=0x%x\n",
                        phase, pid_name, tid_name, i, fds[i].fd,
                        (unsigned short)fds[i].events,
                        (unsigned short)fds[i].revents);
                if (fds[i].fd >= 0)
                    dump_poll_fd_target(phase, pid_name, fds[i].fd);
            }
        }
    } else if (nr == 202) {
        unsigned int word = 0;
        ssize_t n = pread(memfd, &word, sizeof(word), (off_t)arg0);
        fprintf(stderr,
                "kde_app_launch_probe waitdetail phase=%s pid=%s tid=%s "
                "syscall=futex uaddr=0x%llx op=0x%llx val=0x%llx "
                "word_read=%ld word=0x%x errno=%d %s\n",
                phase, pid_name, tid_name, arg0, arg1, arg2, (long)n, word,
                errno, strerror(errno));
    } else if (nr == 0) {
        fprintf(stderr,
                "kde_app_launch_probe waitdetail phase=%s pid=%s tid=%s "
                "syscall=read fd=%llu buf=0x%llx count=%llu pc=0x%llx\n",
                phase, pid_name, tid_name, arg0, arg1, arg2, pc);
        if (arg0 <= INT_MAX)
            dump_wait_fd_target(phase, pid_name, "readfd", (int)arg0);
    }

    close(memfd);
}

static void dump_process_threads(const char *phase, const char *pid_name)
{
    char task_dir[320];
    DIR *dir;
    struct dirent *de;

    snprintf(task_dir, sizeof(task_dir), "/proc/%s/task", pid_name);
    dir = opendir(task_dir);
    if (!dir) {
        fprintf(stderr,
                "kde_app_launch_probe task phase=%s pid=%s opendir_errno=%d %s\n",
                phase, pid_name, errno, strerror(errno));
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        int all_digits = 1;

        if (de->d_name[0] == '.')
            continue;
        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        fprintf(stderr,
                "kde_app_launch_probe task phase=%s pid=%s tid=%s begin\n",
                phase, pid_name, de->d_name);
        dump_thread_file(phase, pid_name, de->d_name, "stat");
        dump_thread_file(phase, pid_name, de->d_name, "status");
        dump_thread_file(phase, pid_name, de->d_name, "wchan");
        dump_thread_file(phase, pid_name, de->d_name, "syscall");
        dump_thread_file(phase, pid_name, de->d_name, "stack");
        dump_thread_wait_detail(phase, pid_name, de->d_name);
        fprintf(stderr,
                "kde_app_launch_probe task phase=%s pid=%s tid=%s end\n",
                phase, pid_name, de->d_name);
    }

    closedir(dir);
}

static void dump_interesting_processes(const char *phase, int chromium_only)
{
    DIR *dir = opendir("/proc");
    struct dirent *de;

    if (!dir) {
        fprintf(stderr, "kde_app_launch_probe process_dump phase=%s opendir_errno=%d\n",
                phase, errno);
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[320];
        char cmdline_raw[8192];
        char cmdline[8192];
        char comm[128];
        int all_digits = 1;
        int cmd_n;
        int comm_n;
        int ppid;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        cmd_n = read_text_file(path, cmdline_raw, sizeof(cmdline_raw));
        if (cmd_n > 0)
            memcpy(cmdline, cmdline_raw, (size_t)cmd_n + 1);
        else
            cmdline[0] = '\0';
        normalize_cmdline(cmdline, cmd_n);
        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        comm_n = read_text_file(path, comm, sizeof(comm));
        if (comm_n > 0) {
            size_t len = strlen(comm);
            while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r'))
                comm[--len] = '\0';
        }
        ppid = process_ppid(de->d_name);
        chromium_evidence_process(phase, de->d_name, ppid,
                                  comm_n > 0 ? comm : "",
                                  cmd_n > 0 ? cmdline_raw : "", cmd_n,
                                  cmd_n > 0 ? cmdline : "");
        if (!chromium_only &&
            cmdline_interesting(cmd_n > 0 ? cmdline : "",
                                comm_n > 0 ? comm : "")) {
            fprintf(stderr,
                    "kde_app_launch_probe process phase=%s pid=%s ppid=%d comm=%s cmd=%s\n",
                    phase, de->d_name, ppid, comm_n > 0 ? comm : "",
                    cmd_n > 0 ? cmdline : "");
            if ((cmd_n > 0 && strstr(cmdline, "konsole")) ||
                (comm_n > 0 && strstr(comm, "konsole"))) {
                dump_process_details(phase, de->d_name);
                dump_process_threads(phase, de->d_name);
            }
        }
    }

    closedir(dir);
}

static int read_marker_payload(const char *path, char *buf, size_t size)
{
    int n;

    n = read_text_file(path, buf, size);
    if (n <= 0) {
        if (size > 0)
            buf[0] = '\0';
        return 0;
    }
    normalize_cmdline(buf, n);
    return 1;
}

static int wait_for_path_traced(const char *path, int timeout_ms,
                                const char *phase, pid_t child_pid,
                                int sample_ms, long long base_ms,
                                long long *found_ms,
                                long long *found_uptime_ms,
                                struct konsole_phase_samples *samples)
{
    long long start_ms = monotonic_ms();
    long long next_sample_ms = start_ms;
    int sample = 0;

    if (sample_ms <= 0)
        sample_ms = 0;

    for (;;) {
        long long now = monotonic_ms();
        long long now_uptime = uptime_ms();
        long long elapsed_ms = now - start_ms;

        if (samples)
            sample_konsole_pty_phase(child_pid, base_ms, samples);
        if (access(path, F_OK) == 0) {
            char payload[512];

            if (found_ms)
                *found_ms = now;
            if (found_uptime_ms)
                *found_uptime_ms = now_uptime;
            if (!read_marker_payload(path, payload, sizeof(payload)))
                snprintf(payload, sizeof(payload), "missing");
            fprintf(stderr,
                    "kde_app_launch_probe marker phase=%s status=found "
                    "elapsed_ms=%lld since_launch_ms=%lld uptime_ms=%lld "
                    "path=%s payload=\"%s\"\n",
                    phase, elapsed_ms, now - base_ms, now_uptime, path,
                    payload);
            return 1;
        }

        if (sample_ms > 0 && now >= next_sample_ms) {
            char pid_name[32];
            int alive = child_pid > 0 && kill(child_pid, 0) == 0;

            fprintf(stderr,
                    "kde_app_launch_probe marker phase=%s status=waiting "
                    "sample=%d elapsed_ms=%lld since_launch_ms=%lld "
                    "uptime_ms=%lld child_pid=%ld child_alive=%d "
                    "shell_count=%d\n",
                    phase, sample, elapsed_ms, now - base_ms,
                    now_uptime, (long)child_pid, alive,
                    count_shell_processes());
            if (alive) {
                snprintf(pid_name, sizeof(pid_name), "%ld", (long)child_pid);
                dump_process_file(phase, pid_name, "stat");
                dump_process_file(phase, pid_name, "syscall");
                dump_process_threads(phase, pid_name);
            }
            sample++;
            next_sample_ms = now + sample_ms;
        }

        if (elapsed_ms >= timeout_ms)
            break;
        usleep(100000);
    }

    fprintf(stderr,
            "kde_app_launch_probe marker phase=%s status=timeout "
            "elapsed_ms=%lld path=%s child_pid=%ld\n",
            phase, monotonic_ms() - start_ms, path, (long)child_pid);
    return 0;
}

static void dump_chromium_fast_census(const char *phase)
{
    DIR *dir = opendir("/proc");
    struct dirent *de;

    if (!dir) {
        fprintf(stderr,
                "kde_app_launch_probe chromium_fast_census phase=%s "
                "opendir_errno=%d\n",
                phase, errno);
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[320];
        char cmdline_raw[8192];
        char cmdline[8192];
        char comm[128];
        int all_digits = 1;
        int cmd_n;
        int comm_n;
        int ppid;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        cmd_n = read_text_file(path, cmdline_raw, sizeof(cmdline_raw));
        if (cmd_n > 0)
            memcpy(cmdline, cmdline_raw, (size_t)cmd_n + 1);
        else
            cmdline[0] = '\0';
        normalize_cmdline(cmdline, cmd_n);
        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        comm_n = read_text_file(path, comm, sizeof(comm));
        if (comm_n > 0) {
            size_t len = strlen(comm);

            while (len > 0 && (comm[len - 1] == '\n' ||
                               comm[len - 1] == '\r')) {
                comm[--len] = '\0';
            }
        }
        ppid = process_ppid(de->d_name);
        chromium_evidence_process_fast(phase, de->d_name, ppid,
                                       comm_n > 0 ? comm : "",
                                       cmd_n > 0 ? cmdline_raw : "", cmd_n,
                                       cmd_n > 0 ? cmdline : "");
    }

    closedir(dir);
}

static void dump_chromium_wait_census(const char *phase)
{
    DIR *dir = opendir("/proc");
    struct dirent *de;

    if (!dir) {
        fprintf(stderr,
                "kde_app_launch_probe chromium_wait_census phase=%s "
                "opendir_errno=%d\n",
                phase, errno);
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[320];
        char cmdline_raw[8192];
        char cmdline[8192];
        char comm[128];
        char role[128];
        char role_source[32];
        char type_arg[128];
        int all_digits = 1;
        int cmd_n;
        int comm_n;
        int ppid;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        cmd_n = read_text_file(path, cmdline_raw, sizeof(cmdline_raw));
        if (cmd_n > 0)
            memcpy(cmdline, cmdline_raw, (size_t)cmd_n + 1);
        else
            cmdline[0] = '\0';
        normalize_cmdline(cmdline, cmd_n);
        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        comm_n = read_text_file(path, comm, sizeof(comm));
        if (comm_n > 0) {
            size_t len = strlen(comm);

            while (len > 0 && (comm[len - 1] == '\n' ||
                               comm[len - 1] == '\r')) {
                comm[--len] = '\0';
            }
        }
        if (!chromium_process_interesting(cmd_n > 0 ? cmdline_raw : "",
                                          cmd_n, comm_n > 0 ? comm : ""))
            continue;

        derive_chromium_role(cmd_n > 0 ? cmdline_raw : "", cmd_n,
                             comm_n > 0 ? comm : "", role, sizeof(role),
                             role_source, sizeof(role_source), type_arg,
                             sizeof(type_arg));
        ppid = process_ppid(de->d_name);
        fprintf(stderr,
                "kde_app_launch_probe chromium_wait process phase=%s pid=%s "
                "ppid=%d comm=\"",
                phase, de->d_name, ppid);
        fprint_escaped(stderr, comm_n > 0 ? comm : "", -1);
        fputs("\" role=\"", stderr);
        fprint_escaped(stderr, role, -1);
        fputs("\" role_source=\"", stderr);
        fprint_escaped(stderr, role_source, -1);
        fputs("\" type_arg=\"", stderr);
        fprint_escaped(stderr, type_arg, -1);
        fputs("\" cmd=\"", stderr);
        fprint_escaped(stderr, cmd_n > 0 ? cmdline : "", -1);
        fputs("\"\n", stderr);

        dump_process_file(phase, de->d_name, "stat");
        dump_process_file(phase, de->d_name, "status");
        dump_process_file(phase, de->d_name, "wchan");
        dump_process_file(phase, de->d_name, "syscall");
        dump_process_file(phase, de->d_name, "stack");
        dump_fd_table(phase, de->d_name, 192);
        dump_process_threads(phase, de->d_name);
    }

    closedir(dir);
}

static int run_chromium_only(const char *chromium_url)
{
    char *chromium_argv[] = {
        "/bin/wayland-chromium", (char *)chromium_url, NULL
    };
    struct app_probe chromium = {
        "chromium", chromium_argv, -1, 0, 0, -1, 0, 0, 0, 0
    };
    int ok;
    int launcher_log = 0;
    int launcher_marker = 0;
    int launcher_child_exec = 0;
    int launcher_url = 0;
    int launcher_wayland_platform_fail = 0;

    set_kde_env();
    unlink(chromium_launcher_log_path);
    chromium_evidence_open();
    dump_interesting_processes("before", 0);
    ok = launch_app(&chromium);
    if (chromium.pid > 0)
        chromium_evidence_expected_url(chromium.pid, chromium_url);
    if (ok) {
        usleep(3000000);
        record_child_status(&chromium);
        if (chromium.pid > 0 && child_still_running(chromium.pid))
            chromium.ok = 1;
        ok = chromium.ok;
    }
    wait_for_chromium_launcher_log(chromium_launcher_log_path, chromium_url,
                                   &launcher_log, &launcher_marker,
                                   &launcher_child_exec, &launcher_url,
                                   &launcher_wayland_platform_fail);
    record_child_status(&chromium);
    if (chromium.pid > 0 && child_still_running(chromium.pid))
        chromium.ok = 1;
    if (!chromium.ok)
        ok = 0;
    if (!launcher_log || !launcher_marker || !launcher_child_exec ||
        !launcher_url || launcher_wayland_platform_fail)
        ok = 0;
    dump_interesting_processes("after", 0);
    printf("kde_app_launch_probe chromium_only=1 chromium=%d "
           "chromium_exited=%d chromium_exit=%d chromium_signal=%d "
           "launcher_log=%d launcher_marker=%d launcher_child_exec=%d "
           "launcher_url=%d launcher_wayland_platform_fail=%d "
           "launcher_log_path=%s url=\"%s\" status=%s\n",
           chromium.ok, chromium.exited, chromium.exit_code,
           chromium.signal_code, launcher_log, launcher_marker,
           launcher_child_exec, launcher_url, launcher_wayland_platform_fail,
           chromium_launcher_log_path, chromium_url, ok ? "PASS" : "FAIL");
    chromium_evidence_close(ok);
    return ok ? 0 : 2;
}

static int run_chromium_fast_census_only(int duration_ms, int interval_ms)
{
    int elapsed_ms = 0;
    int sample = 0;
    int count = 0;

    if (duration_ms < 0)
        duration_ms = 0;
    if (interval_ms <= 0)
        interval_ms = 50;

    chromium_evidence_open_append("fast-census");
    while (elapsed_ms <= duration_ms) {
        char phase[64];

        snprintf(phase, sizeof(phase), "fast-%03d", sample);
        dump_chromium_fast_census(phase);
        count++;
        if (elapsed_ms == duration_ms)
            break;
        usleep((useconds_t)interval_ms * 1000);
        elapsed_ms += interval_ms;
        if (elapsed_ms > duration_ms)
            elapsed_ms = duration_ms;
        sample++;
    }
    if (chromium_evidence_full_maps_enabled())
        dump_interesting_processes("fast-final-maps", 1);
    chromium_evidence_sampler_close(1, count);
    printf("kde_app_launch_probe chromium_fast_census_only=1 samples=%d "
           "duration_ms=%d interval_ms=%d status=PASS\n",
           count, duration_ms, interval_ms);
    return 0;
}

static int run_chromium_wait_census_only(int duration_ms, int interval_ms)
{
    int elapsed_ms = 0;
    int sample = 0;
    int count = 0;

    if (duration_ms < 0)
        duration_ms = 0;
    if (interval_ms <= 0)
        interval_ms = 250;

    while (elapsed_ms <= duration_ms) {
        char phase[64];

        snprintf(phase, sizeof(phase), "wait-%03d", sample);
        dump_chromium_wait_census(phase);
        count++;
        if (elapsed_ms == duration_ms)
            break;
        usleep((useconds_t)interval_ms * 1000);
        elapsed_ms += interval_ms;
        if (elapsed_ms > duration_ms)
            elapsed_ms = duration_ms;
        sample++;
    }
    printf("kde_app_launch_probe chromium_wait_census_only=1 samples=%d "
           "duration_ms=%d interval_ms=%d status=PASS\n",
           count, duration_ms, interval_ms);
    return 0;
}

static int run_chromium_sample_loop(const char *phase_prefix, int *sample,
                                    int duration_ms, int interval_ms)
{
    int elapsed_ms = 0;
    int count = 0;

    while (elapsed_ms <= duration_ms) {
        char phase[64];

        snprintf(phase, sizeof(phase), "%s-%03d", phase_prefix, *sample);
        dump_interesting_processes(phase, 1);
        count++;
        if (elapsed_ms == duration_ms)
            break;
        usleep((useconds_t)interval_ms * 1000);
        elapsed_ms += interval_ms;
        if (elapsed_ms > duration_ms)
            elapsed_ms = duration_ms;
        (*sample)++;
    }
    return count;
}

static int run_chromium_sample_only(int duration_ms, int interval_ms,
                                    int burst_ms, int burst_interval_ms)
{
    int sample = 0;
    int burst_samples = 0;
    int regular_samples;

    if (interval_ms <= 0)
        interval_ms = 500;
    if (burst_ms < 0)
        burst_ms = 0;
    if (burst_interval_ms <= 0)
        burst_interval_ms = 100;

    chromium_evidence_open_append("sample-only");
    if (burst_ms > 0) {
        burst_samples = run_chromium_sample_loop("burst", &sample, burst_ms,
                                                burst_interval_ms);
        sample++;
    }
    regular_samples = run_chromium_sample_loop("sample", &sample, duration_ms,
                                               interval_ms);
    chromium_evidence_sampler_close(1, burst_samples + regular_samples);
    printf("kde_app_launch_probe chromium_sample_only=1 samples=%d "
           "duration_ms=%d interval_ms=%d burst_samples=%d burst_ms=%d "
           "burst_interval_ms=%d status=PASS\n",
           burst_samples + regular_samples, duration_ms, interval_ms,
           burst_samples, burst_ms, burst_interval_ms);
    return 0;
}

int main(int argc, char **argv)
{
    const char *konsole_marker = "/dev/shm/xv6-konsole-shell-ready";
    const char *konsole_prompt_marker =
        "/dev/shm/xv6-konsole-bash-prompt-ready";
    char *konsole_baseline_argv[] = {
        "/usr/bin/konsole", "--separate", "--profile", "Shell", "--workdir",
        "/root", "--hold", "-e", "/bin/kde-konsole-shell-wrapper",
        (char *)konsole_marker, (char *)konsole_prompt_marker, NULL
    };
    char *konsole_no_profile_argv[] = {
        "/usr/bin/konsole", "--separate", "--workdir", "/root", "--hold",
        "-e", "/bin/kde-konsole-shell-wrapper", (char *)konsole_marker,
        (char *)konsole_prompt_marker, NULL
    };
    char *konsole_no_hold_argv[] = {
        "/usr/bin/konsole", "--separate", "--profile", "Shell", "--workdir",
        "/root", "-e", "/bin/kde-konsole-shell-wrapper",
        (char *)konsole_marker, (char *)konsole_prompt_marker, NULL
    };
    char *konsole_minimal_argv[] = {
        "/usr/bin/konsole", "--separate", "-e",
        "/bin/kde-konsole-shell-wrapper", (char *)konsole_marker,
        (char *)konsole_prompt_marker, NULL
    };
    char *const *konsole_argv = konsole_baseline_argv;
    const char *konsole_variant = konsole_variant_select(argc, argv);
    char *terminal_argv[] = { "/bin/kde-terminal-launcher", NULL };
    char *dolphin_argv[] = { "/usr/bin/dolphin", "--new-window", "/root",
                             NULL };
    char *kate_argv[] = { "/usr/bin/kate", "--new",
                          "/tmp/xv6-kde-app-probe.txt", NULL };
    char *kwrite_argv[] = { "/usr/bin/kwrite",
                            "/tmp/xv6-kde-app-probe.txt", NULL };
    char *chromium_argv[] = { "/bin/wayland-chromium", "about:blank", NULL };
    struct app_probe probes[] = {
        { "konsole", konsole_argv, -1, 0, 0, -1, 0, 0, 0, 0 },
        { "terminal", terminal_argv, -1, 0, 0, -1, 0, 0, 0, 0 },
        { "dolphin", dolphin_argv, -1, 0, 0, -1, 0, 0, 0, 0 },
        { "kate", kate_argv, -1, 0, 0, -1, 0, 0, 0, 0 },
        { "kwrite", kwrite_argv, -1, 0, 0, -1, 0, 0, 0, 0 },
        { "chromium", chromium_argv, -1, 0, 0, -1, 0, 0, 0, 0 },
    };
    int require_chromium = has_arg(argc, argv, "--require-chromium");
    int chromium_only = has_arg(argc, argv, "--chromium-only");
    int chromium_sample_only = has_arg(argc, argv, "--chromium-sample-only");
    int chromium_fast_census_only =
        has_arg(argc, argv, "--chromium-fast-census-only");
    int chromium_wait_census_only =
        has_arg(argc, argv, "--chromium-wait-census-only");
    int chromium_local_video = has_arg(argc, argv, "--chromium-local-video");
    int konsole_only = has_arg(argc, argv, "--konsole-only") ||
        env_bool_or_default("KDE_APP_LAUNCH_PROBE_KONSOLE_ONLY", 0);
    const char *chromium_url = arg_value(argc, argv, "--chromium-url");
    int chromium_sample_ms =
        parse_int_arg_or_default(argc, argv, "--sample-ms", 20000, 500, 120000);
    int chromium_sample_interval_ms =
        parse_int_arg_or_default(argc, argv, "--sample-interval-ms", 500, 100,
                                 5000);
    int chromium_burst_ms =
        parse_int_arg_or_default(argc, argv, "--burst-ms", 0, 0, 10000);
    int chromium_burst_interval_ms =
        parse_int_arg_or_default(argc, argv, "--burst-interval-ms", 100, 20,
                                 1000);
    int chromium_fast_census_ms =
        parse_int_arg_or_default(argc, argv, "--fast-census-ms", 0, 0, 60000);
    int chromium_fast_census_interval_ms =
        parse_int_arg_or_default(argc, argv, "--fast-census-interval-ms", 50,
                                 20, 1000);
    int chromium_wait_census_ms =
        parse_int_arg_or_default(argc, argv, "--wait-census-ms", 0, 0, 60000);
    int chromium_wait_census_interval_ms =
        parse_int_arg_or_default(argc, argv, "--wait-census-interval-ms", 250,
                                 50, 5000);
    int interlaunch_delay_ms =
        parse_env_int_or_default("KDE_APP_LAUNCH_PROBE_INTERLAUNCH_DELAY_MS",
                                 500, 0, 5000);
    int konsole_wait_sample_ms =
        parse_env_int_or_default("KDE_APP_LAUNCH_PROBE_KONSOLE_WAIT_SAMPLE_MS",
                                 0, 0, 5000);
    int konsole_prompt_wait_ms =
        parse_env_int_or_default("KDE_APP_LAUNCH_PROBE_PROMPT_WAIT_MS",
                                 5000, 0, 30000);
    size_t probe_count = require_chromium ? 6 : 5;
    int konsole_marker_ok = 1;
    int shells_before;
    int shells_after;
    int ok = 1;
    struct konsole_phase_samples konsole_phase_samples;
    long long konsole_marker_found_ms = -1;
    long long konsole_marker_found_uptime_ms = -1;

    maybe_redirect_stderr_to_stdout();
    konsole_phase_samples_init(&konsole_phase_samples);

    if (strcmp(konsole_variant, "no-profile") == 0)
        konsole_argv = konsole_no_profile_argv;
    else if (strcmp(konsole_variant, "no-hold") == 0)
        konsole_argv = konsole_no_hold_argv;
    else if (strcmp(konsole_variant, "minimal") == 0)
        konsole_argv = konsole_minimal_argv;
    probes[0].argv = konsole_argv;
    fprintf(stderr,
            "kde_app_launch_probe konsole_variant=%s marker_path=%s "
            "prompt_marker_path=%s\n",
            konsole_variant, konsole_marker, konsole_prompt_marker);

    if (!chromium_url || chromium_url[0] == '\0')
        chromium_url = getenv("KDE_CHROMIUM_URL");
    if ((!chromium_url || chromium_url[0] == '\0') && chromium_local_video)
        chromium_url = chromium_local_video_url;
    if (!chromium_url || chromium_url[0] == '\0')
        chromium_url = "about:blank";
    chromium_argv[1] = (char *)chromium_url;

    if (chromium_sample_only)
        return run_chromium_sample_only(chromium_sample_ms,
                                        chromium_sample_interval_ms,
                                        chromium_burst_ms,
                                        chromium_burst_interval_ms);
    if (chromium_fast_census_only)
        return run_chromium_fast_census_only(chromium_fast_census_ms,
                                             chromium_fast_census_interval_ms);
    if (chromium_wait_census_only)
        return run_chromium_wait_census_only(chromium_wait_census_ms,
                                             chromium_wait_census_interval_ms);
    if (chromium_only)
        return run_chromium_only(chromium_url);

    if (require_chromium)
        chromium_evidence_open();
    set_kde_env();
    reset_qtmm_stub_call_log();
    if (!require_chromium || access(konsole_marker, F_OK) != 0)
        unlink(konsole_marker);
    unlink(konsole_prompt_marker);
    shells_before = count_shell_processes();
    dump_interesting_processes("before", 0);

    FILE *f = fopen("/tmp/xv6-kde-app-probe.txt", "w");
    if (f) {
        fputs("xv6 KDE app launch probe\n", f);
        fclose(f);
    }

    {
        char *saved_ld_preload =
            enable_konsole_event_trace_preload();
        probes[0].ok = launch_app(&probes[0]);
        restore_konsole_event_trace_preload(saved_ld_preload);
    }
    if (!probes[0].ok)
        ok = 0;

    long long konsole_wait_start_ms = monotonic_ms();
    konsole_marker_ok = wait_for_path_traced(konsole_marker, 45000,
                                             "konsole-ready", probes[0].pid,
                                             konsole_wait_sample_ms,
                                             probes[0].launch_start_ms,
                                             &konsole_marker_found_ms,
                                             &konsole_marker_found_uptime_ms,
                                             &konsole_phase_samples);
    long long konsole_wait_elapsed_ms = monotonic_ms() - konsole_wait_start_ms;
    shells_after = count_shell_processes();
    fprintf(stderr,
            "kde_app_launch_probe konsole_ready marker=%d marker_path=%s "
            "shell_count_before=%d shell_count_after=%d elapsed_ms=%lld\n",
            konsole_marker_ok, konsole_marker, shells_before, shells_after,
            konsole_wait_elapsed_ms);
    {
        char marker_payload[512];
        FILE *phase;
        long long wrapper_pid = -1;
        long long wrapper_start_ms = -1;
        long long wrapper_start_uptime_ms = -1;
        long long wrapper_open_ms = -1;
        long long wrapper_marker_write_ms = -1;
        long long wrapper_written_ms = -1;
        long long wrapper_before_exec_ms = -1;
        long long wrapper_start_since_launch_ms = -1;
        long long wrapper_marker_found_since_wrapper_ms = -1;
        long long wrapper_before_exec_delta_ms = -1;
        long long pty_ptmx_since_launch_ms = -1;
        long long pty_ptmx_uptime_ms = -1;
        long long pty_pts_since_launch_ms = -1;
        long long pty_pts_uptime_ms = -1;
        long long bash_start_since_launch_ms = -1;
        long long bash_start_since_wrapper_ms = -1;
        long long bash_start_uptime_ms = -1;
        int bash_prompt_ok = 0;
        long long bash_prompt_found_ms = -1;
        long long bash_prompt_found_uptime_ms = -1;
        long long bash_prompt_uptime_ms = -1;
        long long bash_prompt_since_launch_ms = -1;
        long long bash_prompt_since_wrapper_ms = -1;
        char prompt_payload[512];

        prompt_payload[0] = '\0';

        if (!read_marker_payload(konsole_marker, marker_payload,
                                 sizeof(marker_payload)))
            snprintf(marker_payload, sizeof(marker_payload), "missing");
        extract_ll_key(marker_payload, "pid", &wrapper_pid);
        extract_ll_key(marker_payload, "start_uptime_ms",
                       &wrapper_start_uptime_ms);
        extract_ll_key(marker_payload, "start_ms", &wrapper_start_ms);
        extract_ll_key(marker_payload, "open_ms", &wrapper_open_ms);
        extract_ll_key(marker_payload, "marker_write_ms",
                       &wrapper_marker_write_ms);
        extract_ll_key(marker_payload, "written_ms", &wrapper_written_ms);
        extract_ll_key(marker_payload, "before_exec_ms",
                       &wrapper_before_exec_ms);

        if (konsole_phase_samples.ptmx_first_ms >= 0) {
            pty_ptmx_since_launch_ms =
                konsole_phase_samples.ptmx_first_ms - probes[0].launch_start_ms;
            pty_ptmx_uptime_ms = konsole_phase_samples.ptmx_first_uptime_ms;
        }
        if (konsole_phase_samples.pts_first_ms >= 0) {
            pty_pts_since_launch_ms =
                konsole_phase_samples.pts_first_ms - probes[0].launch_start_ms;
            pty_pts_uptime_ms = konsole_phase_samples.pts_first_uptime_ms;
        }
        if (wrapper_start_ms >= 0) {
            wrapper_start_since_launch_ms =
                wrapper_start_ms - probes[0].launch_start_ms;
            if (wrapper_start_uptime_ms < 0 &&
                probes[0].launch_start_uptime_ms >= 0)
                wrapper_start_uptime_ms =
                    probes[0].launch_start_uptime_ms +
                    wrapper_start_since_launch_ms;
            if (konsole_marker_found_ms >= 0)
                wrapper_marker_found_since_wrapper_ms =
                    konsole_marker_found_ms - wrapper_start_ms;
            if (wrapper_before_exec_ms >= 0)
                wrapper_before_exec_delta_ms =
                    wrapper_before_exec_ms - wrapper_start_ms;
            if (pty_pts_since_launch_ms < 0)
                pty_pts_since_launch_ms = wrapper_start_since_launch_ms;
            if (pty_pts_uptime_ms < 0)
                pty_pts_uptime_ms = wrapper_start_uptime_ms;
        }
        if (wrapper_pid > 0 &&
            wait_for_process_comm((pid_t)wrapper_pid, "bash", 2000, 20,
                                  probes[0].launch_start_ms,
                                  wrapper_start_ms,
                                  &konsole_phase_samples.bash_first_ms,
                                  &konsole_phase_samples.bash_first_uptime_ms)) {
            bash_start_since_launch_ms =
                konsole_phase_samples.bash_first_ms - probes[0].launch_start_ms;
            bash_start_uptime_ms =
                konsole_phase_samples.bash_first_uptime_ms;
            if (wrapper_start_ms >= 0)
                bash_start_since_wrapper_ms =
                    konsole_phase_samples.bash_first_ms - wrapper_start_ms;
        }
        if (konsole_prompt_wait_ms > 0) {
            bash_prompt_ok =
                wait_for_path_traced(konsole_prompt_marker,
                                     konsole_prompt_wait_ms, "bash-prompt",
                                     wrapper_pid > 0 ? (pid_t)wrapper_pid :
                                                       probes[0].pid,
                                     0, probes[0].launch_start_ms,
                                     &bash_prompt_found_ms,
                                     &bash_prompt_found_uptime_ms, NULL);
            if (bash_prompt_found_ms >= 0) {
                bash_prompt_since_launch_ms =
                    bash_prompt_found_ms - probes[0].launch_start_ms;
                bash_prompt_uptime_ms = bash_prompt_found_uptime_ms;
                if (read_marker_payload(konsole_prompt_marker, prompt_payload,
                                        sizeof(prompt_payload)))
                    extract_ll_key(prompt_payload, "uptime_ms",
                                   &bash_prompt_uptime_ms);
                if (wrapper_start_ms >= 0)
                    bash_prompt_since_wrapper_ms =
                        bash_prompt_found_ms - wrapper_start_ms;
            }
        }

        phase = fopen("/xv6-konsole-phase.log", "w");
        if (phase) {
            fprintf(phase,
                    "kde_app_launch_probe konsole_phase marker=%d "
                    "konsole_only=%d konsole_variant=%s "
                    "konsole_launch_start_ms=%lld "
                    "launch_start_uptime_ms=%lld "
                    "launch_call_ms=%lld wait_elapsed_ms=%lld "
                    "marker_found_since_launch_ms=%lld "
                    "marker_found_uptime_ms=%lld "
                    "wrapper_pid=%lld wrapper_start_ms=%lld "
                    "wrapper_start_uptime_ms=%lld "
                    "wrapper_start_since_launch_ms=%lld "
                    "wrapper_marker_found_since_wrapper_ms=%lld "
                    "wrapper_open_delta_ms=%lld "
                    "wrapper_marker_write_delta_ms=%lld "
                    "wrapper_written_delta_ms=%lld "
                    "wrapper_before_exec_delta_ms=%lld "
                    "pty_ptmx_since_launch_ms=%lld "
                    "pty_ptmx_uptime_ms=%lld "
                    "pty_ptmx_fd=%d pty_pts_since_launch_ms=%lld "
                    "pty_pts_uptime_ms=%lld "
                    "pty_pts_fd=%d bash_start_since_launch_ms=%lld "
                    "bash_start_uptime_ms=%lld "
                    "bash_start_since_wrapper_ms=%lld "
                    "bash_prompt_marker=%d bash_prompt_since_launch_ms=%lld "
                    "bash_prompt_uptime_ms=%lld "
                    "bash_prompt_since_wrapper_ms=%lld "
                    "bash_prompt_wait_ms=%d "
                    "shell_count_before=%d shell_count_after=%d "
                    "marker_path=%s prompt_marker_path=%s ptmx_target=\"",
                    konsole_marker_ok, konsole_only, konsole_variant,
                    probes[0].launch_start_ms,
                    probes[0].launch_start_uptime_ms,
                    probes[0].launch_elapsed_ms,
                    konsole_wait_elapsed_ms,
                    konsole_marker_found_ms >= 0 ?
                        konsole_marker_found_ms - probes[0].launch_start_ms : -1,
                    konsole_marker_found_uptime_ms,
                    wrapper_pid, wrapper_start_ms, wrapper_start_uptime_ms,
                    wrapper_start_since_launch_ms,
                    wrapper_marker_found_since_wrapper_ms,
                    wrapper_open_ms >= 0 && wrapper_start_ms >= 0 ?
                        wrapper_open_ms - wrapper_start_ms : -1,
                    wrapper_marker_write_ms >= 0 && wrapper_start_ms >= 0 ?
                        wrapper_marker_write_ms - wrapper_start_ms : -1,
                    wrapper_written_ms >= 0 && wrapper_start_ms >= 0 ?
                        wrapper_written_ms - wrapper_start_ms : -1,
                    wrapper_before_exec_delta_ms, pty_ptmx_since_launch_ms,
                    pty_ptmx_uptime_ms, konsole_phase_samples.ptmx_fd,
                    pty_pts_since_launch_ms, pty_pts_uptime_ms,
                    konsole_phase_samples.pts_fd, bash_start_since_launch_ms,
                    bash_start_uptime_ms, bash_start_since_wrapper_ms,
                    bash_prompt_ok, bash_prompt_since_launch_ms,
                    bash_prompt_uptime_ms, bash_prompt_since_wrapper_ms,
                    konsole_prompt_wait_ms, shells_before, shells_after,
                    konsole_marker, konsole_prompt_marker);
            fprint_escaped(phase, konsole_phase_samples.ptmx_target, -1);
            fprintf(phase, "\" pts_target=\"");
            fprint_escaped(phase, konsole_phase_samples.pts_target, -1);
            fprintf(phase, "\" marker_payload=\"");
            fprint_escaped(phase, marker_payload, -1);
            fprintf(phase, "\"\n");
            fclose(phase);
        }
        fprintf(stderr,
                "kde_app_launch_probe konsole_phase_detail "
                "phase=probe-summary launch_start_ms=%lld launch_call_ms=%lld "
                "launch_start_uptime_ms=%lld wait_elapsed_ms=%lld "
                "marker_found_since_launch_ms=%lld "
                "marker_found_uptime_ms=%lld "
                "wrapper_start_since_launch_ms=%lld "
                "wrapper_start_uptime_ms=%lld "
                "pty_ptmx_since_launch_ms=%lld pty_ptmx_uptime_ms=%lld "
                "pty_pts_since_launch_ms=%lld pty_pts_uptime_ms=%lld "
                "bash_start_since_launch_ms=%lld bash_start_uptime_ms=%lld "
                "bash_prompt_since_launch_ms=%lld "
                "bash_prompt_uptime_ms=%lld "
                "bash_prompt_since_wrapper_ms=%lld "
                "konsole_only=%d konsole_variant=%s "
                "marker_payload=\"",
                probes[0].launch_start_ms, probes[0].launch_elapsed_ms,
                probes[0].launch_start_uptime_ms,
                konsole_wait_elapsed_ms,
                konsole_marker_found_ms >= 0 ?
                    konsole_marker_found_ms - probes[0].launch_start_ms : -1,
                konsole_marker_found_uptime_ms,
                wrapper_start_since_launch_ms, wrapper_start_uptime_ms,
                pty_ptmx_since_launch_ms, pty_ptmx_uptime_ms,
                pty_pts_since_launch_ms, pty_pts_uptime_ms,
                bash_start_since_launch_ms, bash_start_uptime_ms,
                bash_prompt_since_launch_ms, bash_prompt_uptime_ms,
                bash_prompt_since_wrapper_ms,
                konsole_only, konsole_variant);
        fprint_escaped(stderr, marker_payload, -1);
        fprintf(stderr, "\"\n");
        report_qtmm_stub_call_log();
        if (konsole_only) {
            int konsole_ok;

            record_child_status(&probes[0]);
            if (probes[0].pid > 0 && child_still_running(probes[0].pid))
                probes[0].ok = 1;
            konsole_ok = konsole_marker_ok && probes[0].ok &&
                         (!konsole_prompt_wait_ms || bash_prompt_ok);
            printf("kde_app_launch_probe konsole=%d terminal=-1 dolphin=-1 "
                   "kate=-1 kwrite=-1 editor=-1 chromium=-1 "
                   "konsole_shell=%d bash_prompt=%d konsole_only=1 "
                   "konsole_variant=%s "
                   "konsole_wait_ms=%lld bash_prompt_since_launch_ms=%lld "
                   "bash_prompt_since_wrapper_ms=%lld "
                   "interlaunch_delay_ms=%d status=%s\n",
                   probes[0].ok, konsole_marker_ok, bash_prompt_ok,
                   konsole_variant, konsole_wait_elapsed_ms,
                   bash_prompt_since_launch_ms,
                   bash_prompt_since_wrapper_ms, interlaunch_delay_ms,
                   konsole_ok ? "PASS" : "FAIL");
            chromium_evidence_close(konsole_ok);
            return konsole_ok ? 0 : 2;
        }
    }
    if (!konsole_marker_ok)
        ok = 0;

    fprintf(stderr,
            "kde_app_launch_probe interlaunch_delay_ms=%d probe_count=%zu\n",
            interlaunch_delay_ms, probe_count);
    for (size_t i = 1; i < probe_count; i++) {
        probes[i].ok = launch_app(&probes[i]);
        if (!probes[i].ok)
            ok = 0;
        if (interlaunch_delay_ms > 0)
            usleep((useconds_t)interlaunch_delay_ms * 1000);
    }

    shells_after = count_shell_processes();
    dump_interesting_processes("after", 0);
    fprintf(stderr,
            "kde_app_launch_probe konsole_marker=%d marker_path=%s "
            "shell_count_before=%d shell_count_after=%d\n",
            konsole_marker_ok, konsole_marker, shells_before, shells_after);
    if (!konsole_marker_ok || shells_after <= shells_before) {
        probes[0].ok = 0;
        ok = 0;
    }

    for (size_t i = 0; i < probe_count; i++) {
        record_child_status(&probes[i]);
        if (probes[i].pid > 0 && child_still_running(probes[i].pid))
            probes[i].ok = 1;
        if (!probes[i].ok)
            ok = 0;
    }

    int editor_ok = probes[3].ok || probes[4].ok;

    ok = konsole_marker_ok && probes[0].ok && probes[1].ok &&
         probes[2].ok && editor_ok &&
         (!require_chromium || probes[5].ok);

    printf("kde_app_launch_probe konsole=%d terminal=%d dolphin=%d "
           "kate=%d kwrite=%d "
           "editor=%d chromium=%d konsole_shell=%d kate_exited=%d "
           "kate_exit=%d kate_signal=%d konsole_variant=%s "
           "konsole_wait_ms=%lld interlaunch_delay_ms=%d status=%s\n",
           probes[0].ok, probes[1].ok, probes[2].ok, probes[3].ok,
           probes[4].ok,
           editor_ok, require_chromium ? probes[5].ok : -1,
           konsole_marker_ok,
           probes[3].exited, probes[3].exit_code, probes[3].signal_code,
           konsole_variant, konsole_wait_elapsed_ms, interlaunch_delay_ms,
           ok ? "PASS" : "FAIL");
    chromium_evidence_close(ok);
    return ok ? 0 : 2;
}
