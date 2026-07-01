#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static const char *phase_log = "/dev/shm/xv6-konsole-phase.log";
static const char *phase_mirror = "/xv6-konsole-phase.log";

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void phase_log_append(const char *fmt, ...)
{
    const char *paths[] = { phase_log, phase_mirror };
    va_list ap;

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        int fd = open(paths[i], O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                      0644);

        if (fd < 0)
            continue;
        va_start(ap, fmt);
        vdprintf(fd, fmt, ap);
        va_end(ap);
        close(fd);
    }
}

int main(int argc, char **argv)
{
    const char *marker = argc > 1 ? argv[1] : "/dev/shm/xv6-konsole-shell-ready";
    long long start_ms = monotonic_ms();
    long long open_ms;
    long long written_ms;
    long long before_exec_ms;
    int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int stdin_isatty = isatty(STDIN_FILENO);
    int stdout_isatty = isatty(STDOUT_FILENO);
    int stderr_isatty = isatty(STDERR_FILENO);
    int session_id = getsid(0);
    int pgrp = getpgrp();
    int tty_pgrp = tcgetpgrp(STDIN_FILENO);
    const char *tty = ttyname(STDIN_FILENO);

    if (argc > 1 && strcmp(argv[1], "--bash-start") == 0) {
        marker = argc > 2 ? argv[2] : "/dev/shm/xv6-konsole-shell-ready";
        phase_log_append("xv6-konsole-shell phase=bash-start "
                         "shell_start_ms=%lld pid=%ld ppid=%ld "
                         "stdin_isatty=%d stdout_isatty=%d stderr_isatty=%d "
                         "getsid=%d getpgrp=%d tcgetpgrp=%d tty=%s marker=%s\n",
                         start_ms, (long)getpid(), (long)getppid(),
                         stdin_isatty, stdout_isatty, stderr_isatty,
                         session_id, pgrp, tty_pgrp, tty ? tty : "(none)",
                         marker);
        setenv("TERM", "xterm-256color", 0);
        execl("/bin/bash", "bash", "-i", NULL);
        fprintf(stderr,
                "kde_konsole_shell_wrapper exec /bin/bash failed errno=%d %s "
                "bash_start_ms=%lld\n",
                errno, strerror(errno), start_ms);
        phase_log_append("xv6-konsole-shell phase=exec-failed errno=%d "
                         "error=\"%s\" bash_start_ms=%lld pid=%ld\n",
                         errno, strerror(errno), start_ms, (long)getpid());
        return 127;
    }

    open_ms = monotonic_ms();
    if (fd < 0) {
        fprintf(stderr,
                "kde_konsole_shell_wrapper marker=%s open failed errno=%d %s "
                "start_ms=%lld open_ms=%lld\n",
                marker, errno, strerror(errno), start_ms, open_ms);
        return 2;
    }

    before_exec_ms = monotonic_ms();
    phase_log_append("xv6-konsole-shell phase=wrapper-start "
                     "start_ms=%lld open_ms=%lld before_exec_ms=%lld "
                     "pid=%ld ppid=%ld stdin_isatty=%d stdout_isatty=%d "
                     "stderr_isatty=%d getsid=%d getpgrp=%d tcgetpgrp=%d "
                     "tty=%s marker=%s\n",
                     start_ms, open_ms, before_exec_ms, (long)getpid(),
                     (long)getppid(), stdin_isatty, stdout_isatty,
                     stderr_isatty, session_id, pgrp, tty_pgrp,
                     tty ? tty : "(none)", marker);
    dprintf(fd,
            "xv6-konsole-shell-ready start_ms=%lld open_ms=%lld "
            "before_exec_ms=%lld pid=%ld ppid=%ld stdin_isatty=%d "
            "stdout_isatty=%d stderr_isatty=%d getsid=%d getpgrp=%d "
            "tcgetpgrp=%d tty=%s\n",
            start_ms, open_ms, before_exec_ms, (long)getpid(),
            (long)getppid(), stdin_isatty, stdout_isatty, stderr_isatty,
            session_id, pgrp, tty_pgrp, tty ? tty : "(none)");
    close(fd);
    written_ms = monotonic_ms();
    dprintf(STDOUT_FILENO,
            "xv6-konsole-shell-ready start_ms=%lld open_ms=%lld "
            "written_ms=%lld before_exec_ms=%lld\n",
            start_ms, open_ms, written_ms, before_exec_ms);
    phase_log_append("xv6-konsole-shell phase=marker-written "
                     "start_ms=%lld open_ms=%lld written_ms=%lld "
                     "before_exec_ms=%lld pid=%ld marker=%s\n",
                     start_ms, open_ms, written_ms, before_exec_ms,
                     (long)getpid(), marker);

    setenv("TERM", "xterm-256color", 0);
    execl("/bin/kde-konsole-shell-wrapper", "kde-konsole-shell-wrapper",
          "--bash-start", marker, NULL);
    fprintf(stderr,
            "kde_konsole_shell_wrapper exec bash-start helper failed errno=%d %s "
            "before_exec_ms=%lld\n",
            errno, strerror(errno), before_exec_ms);
    phase_log_append("xv6-konsole-shell phase=exec-failed errno=%d "
                     "error=\"%s\" before_exec_ms=%lld pid=%ld\n",
                     errno, strerror(errno), before_exec_ms, (long)getpid());
    return 127;
}
