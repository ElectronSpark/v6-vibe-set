#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct app_probe {
    const char *name;
    char *const *argv;
    pid_t pid;
    int ok;
    int exited;
    int exit_code;
    int signal_code;
};

static void set_kde_env(void)
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
    setenv("LD_PRELOAD", "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0", 0);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri", 1);
    setenv("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 0);
    setenv("GALLIUM_DRIVER", "virgl", 0);
}

static int launch_app(struct app_probe *probe)
{
    pid_t pid;

    if (access(probe->argv[0], X_OK) < 0) {
        fprintf(stderr, "kde_app_launch_probe %s missing path=%s errno=%d\n",
                probe->name, probe->argv[0], errno);
        return 0;
    }

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "kde_app_launch_probe %s fork_failed errno=%d\n",
                probe->name, errno);
        return 0;
    }
    if (pid == 0) {
        execv(probe->argv[0], probe->argv);
        fprintf(stderr, "kde_app_launch_probe exec_failed app=%s errno=%d\n",
                probe->name, errno);
        _exit(127);
    }
    probe->pid = pid;
    return 1;
}

static int child_still_running(pid_t pid)
{
    int status;
    pid_t got;

    got = waitpid(pid, &status, WNOHANG);
    if (got == 0)
        return 1;
    if (got < 0 && errno == ECHILD)
        return kill(pid, 0) == 0;
    return 0;
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

static int wait_for_path(const char *path, int timeout_ms)
{
    int waited = 0;

    while (waited <= timeout_ms) {
        if (access(path, F_OK) == 0)
            return 1;
        usleep(100000);
        waited += 100;
    }
    return 0;
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

static int cmdline_interesting(const char *cmdline, const char *comm)
{
    static const char *needles[] = {
        "konsole", "xterm", "kde-terminal-launcher",
        "kdeinit", "klauncher", "kded", "dbus", "sh",
        "dolphin", "kate", "kwrite", "wayland-chromium", "chromium",
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
        char link_path[512];
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

static void dump_poll_fd_target(const char *phase, const char *pid_name, int fd)
{
    char link_path[512];
    char target[512];
    ssize_t n;

    snprintf(link_path, sizeof(link_path), "/proc/%s/fd/%d", pid_name, fd);
    n = readlink(link_path, target, sizeof(target) - 1);
    if (n < 0) {
        fprintf(stderr,
                "kde_app_launch_probe pollfd phase=%s pid=%s fd=%d "
                "readlink_errno=%d %s\n",
                phase, pid_name, fd, errno, strerror(errno));
        return;
    }
    target[n] = '\0';
    fprintf(stderr,
            "kde_app_launch_probe pollfd phase=%s pid=%s fd=%d target=%s\n",
            phase, pid_name, fd, target);
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

static void dump_interesting_processes(const char *phase)
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
        char cmdline[512];
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
        cmd_n = read_text_file(path, cmdline, sizeof(cmdline));
        normalize_cmdline(cmdline, cmd_n);
        snprintf(path, sizeof(path), "/proc/%s/comm", de->d_name);
        comm_n = read_text_file(path, comm, sizeof(comm));
        if (comm_n > 0) {
            size_t len = strlen(comm);
            while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r'))
                comm[--len] = '\0';
        }
        ppid = process_ppid(de->d_name);
        if (cmdline_interesting(cmd_n > 0 ? cmdline : "", comm_n > 0 ? comm : "")) {
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

int main(int argc, char **argv)
{
    const char *konsole_marker = "/dev/shm/xv6-konsole-shell-ready";
    char *konsole_argv[] = {
        "/usr/bin/konsole", "--separate", "--profile", "Shell", "--workdir",
        "/root", "--hold", "-e", "/bin/kde-konsole-shell-wrapper",
        (char *)konsole_marker, NULL
    };
    char *terminal_argv[] = { "/bin/kde-terminal-launcher", NULL };
    char *dolphin_argv[] = { "/usr/bin/dolphin", "--new-window", "/root",
                             NULL };
    char *kate_argv[] = { "/usr/bin/kate", "--new",
                          "/tmp/xv6-kde-app-probe.txt", NULL };
    char *kwrite_argv[] = { "/usr/bin/kwrite",
                            "/tmp/xv6-kde-app-probe.txt", NULL };
    char *chromium_argv[] = { "/bin/wayland-chromium", "about:blank", NULL };
    struct app_probe probes[] = {
        { "konsole", konsole_argv, -1, 0, 0, -1, 0 },
        { "terminal", terminal_argv, -1, 0, 0, -1, 0 },
        { "dolphin", dolphin_argv, -1, 0, 0, -1, 0 },
        { "kate", kate_argv, -1, 0, 0, -1, 0 },
        { "kwrite", kwrite_argv, -1, 0, 0, -1, 0 },
        { "chromium", chromium_argv, -1, 0, 0, -1, 0 },
    };
    int require_chromium = has_arg(argc, argv, "--require-chromium");
    size_t probe_count = require_chromium ? 6 : 5;
    int konsole_marker_ok = 1;
    int shells_before;
    int shells_after;
    int ok = 1;

    set_kde_env();
    if (!require_chromium || access(konsole_marker, F_OK) != 0)
        unlink(konsole_marker);
    shells_before = count_shell_processes();
    dump_interesting_processes("before");

    FILE *f = fopen("/tmp/xv6-kde-app-probe.txt", "w");
    if (f) {
        fputs("xv6 KDE app launch probe\n", f);
        fclose(f);
    }

    probes[0].ok = launch_app(&probes[0]);
    if (!probes[0].ok)
        ok = 0;

    konsole_marker_ok = wait_for_path(konsole_marker, 45000);
    shells_after = count_shell_processes();
    fprintf(stderr,
            "kde_app_launch_probe konsole_ready marker=%d marker_path=%s "
            "shell_count_before=%d shell_count_after=%d\n",
            konsole_marker_ok, konsole_marker, shells_before, shells_after);
    if (!konsole_marker_ok)
        ok = 0;

    for (size_t i = 1; i < probe_count; i++) {
        probes[i].ok = launch_app(&probes[i]);
        if (!probes[i].ok)
            ok = 0;
        usleep(500000);
    }

    shells_after = count_shell_processes();
    dump_interesting_processes("after");
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
           "kate_exit=%d kate_signal=%d "
           "status=%s\n",
           probes[0].ok, probes[1].ok, probes[2].ok, probes[3].ok,
           probes[4].ok,
           editor_ok, require_chromium ? probes[5].ok : -1,
           konsole_marker_ok,
           probes[3].exited, probes[3].exit_code, probes[3].signal_code,
           ok ? "PASS" : "FAIL");
    return ok ? 0 : 2;
}
