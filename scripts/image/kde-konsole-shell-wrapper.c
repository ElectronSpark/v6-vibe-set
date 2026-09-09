#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long parse_ll_env(const char *name)
{
    const char *value = getenv(name);
    char *end;
    long long parsed;

    if (!value || value[0] == '\0')
        return -1;
    errno = 0;
    parsed = strtoll(value, &end, 10);
    if (errno || !end || *end != '\0')
        return -1;
    return parsed;
}

static long long delta_from_base(long long value, long long base)
{
    if (value < 0 || base < 0)
        return -1;
    return value - base;
}

static int write_prompt_rc(const char *path)
{
    static const char rc[] =
        "__xv6_konsole_prompt_once() {\n"
        "  local __xv6_marker=\"${XV6_KONSOLE_PROMPT_MARKER:-}\"\n"
        "  local __xv6_rc=\"${XV6_KONSOLE_PROMPT_RC:-}\"\n"
        "  if [ -n \"$__xv6_marker\" ]; then\n"
        "    local __xv6_up __xv6_sec __xv6_frac __xv6_ms\n"
        "    __xv6_up=\"$(/bin/cat /proc/uptime 2>/dev/null)\"\n"
        "    __xv6_up=\"${__xv6_up%% *}\"\n"
        "    __xv6_sec=\"${__xv6_up%.*}\"\n"
        "    __xv6_frac=\"${__xv6_up#*.}\"\n"
        "    if [ \"$__xv6_frac\" = \"$__xv6_up\" ]; then __xv6_frac=0; fi\n"
        "    case \"$__xv6_sec$__xv6_frac\" in\n"
        "      ''|*[!0-9]*) __xv6_ms=-1 ;;\n"
        "      *) __xv6_frac=\"${__xv6_frac}000\"; __xv6_frac=\"${__xv6_frac:0:3}\"; __xv6_ms=$((10#$__xv6_sec * 1000 + 10#$__xv6_frac)) ;;\n"
        "    esac\n"
        "    printf 'xv6-konsole-bash-prompt-ready uptime_ms=%s pid=%s ppid=%s shlvl=%s pwd=\"%s\"\\n' \"$__xv6_ms\" \"$$\" \"$PPID\" \"${SHLVL:-}\" \"$PWD\" >\"$__xv6_marker\"\n"
        "  fi\n"
        "  PROMPT_COMMAND=\n"
        "  unset XV6_KONSOLE_PROMPT_MARKER XV6_KONSOLE_PROMPT_RC\n"
        "  if [ -n \"$__xv6_rc\" ]; then /bin/rm -f \"$__xv6_rc\" 2>/dev/null; fi\n"
        "  unset -f __xv6_konsole_prompt_once\n"
        "}\n"
        "PROMPT_COMMAND=__xv6_konsole_prompt_once\n";
    int fd;
    ssize_t written;
    size_t len = strlen(rc);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    written = write(fd, rc, len);
    if (close(fd) < 0)
        return -1;
    return written == (ssize_t)len ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *marker =
        argc > 1 ? argv[1] : "/dev/shm/xv6-konsole-shell-ready";
    const char *prompt_marker =
        argc > 2 ? argv[2] : "/dev/shm/xv6-konsole-bash-prompt-ready";
    long long launch_start_ms = parse_ll_env("XV6_KONSOLE_LAUNCH_START_MS");
    long long start_ms = monotonic_ms();
    long long open_ms;
    long long marker_write_ms;
    long long written_ms;
    long long before_exec_ms;
    char rc_path[128];
    int prompt_rc_ok;
    const char *tty0 = ttyname(STDIN_FILENO);
    int stdin_isatty = isatty(STDIN_FILENO);
    int stdout_isatty = isatty(STDOUT_FILENO);
    pid_t sid = getsid(0);
    pid_t pgrp = getpgrp();
    pid_t tty_pgrp = tcgetpgrp(STDIN_FILENO);
    int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    snprintf(rc_path, sizeof(rc_path), "/dev/shm/xv6-konsole-bashrc-%ld",
             (long)getpid());
    setenv("XV6_KONSOLE_PROMPT_MARKER", prompt_marker, 1);
    setenv("XV6_KONSOLE_PROMPT_RC", rc_path, 1);
    prompt_rc_ok = write_prompt_rc(rc_path) == 0;

    open_ms = monotonic_ms();
    if (fd < 0) {
        fprintf(stderr,
                "kde_konsole_shell_wrapper marker=%s open failed errno=%d %s "
                "launch_start_ms=%lld start_ms=%lld open_ms=%lld\n",
                marker, errno, strerror(errno), launch_start_ms, start_ms,
                open_ms);
        return 2;
    }

    marker_write_ms = monotonic_ms();
    before_exec_ms = marker_write_ms;
    dprintf(fd,
            "xv6-konsole-shell-ready launch_start_ms=%lld start_ms=%lld "
            "start_since_launch_ms=%lld open_ms=%lld open_since_launch_ms=%lld "
            "marker_write_ms=%lld marker_write_since_launch_ms=%lld "
            "before_exec_ms=%lld before_exec_since_launch_ms=%lld "
            "pid=%ld ppid=%ld sid=%ld pgrp=%ld tty_pgrp=%ld "
            "stdin_isatty=%d stdout_isatty=%d prompt_marker=%s "
            "prompt_rc=%s prompt_rc_path=%s tty0=\"%s\"\n",
            launch_start_ms, start_ms,
            delta_from_base(start_ms, launch_start_ms), open_ms,
            delta_from_base(open_ms, launch_start_ms), marker_write_ms,
            delta_from_base(marker_write_ms, launch_start_ms), before_exec_ms,
            delta_from_base(before_exec_ms, launch_start_ms), (long)getpid(),
            (long)getppid(), (long)sid, (long)pgrp, (long)tty_pgrp,
            stdin_isatty, stdout_isatty, prompt_marker,
            prompt_rc_ok ? "ready" : "failed", rc_path,
            tty0 ? tty0 : "missing");
    close(fd);
    written_ms = monotonic_ms();
    dprintf(STDOUT_FILENO,
            "xv6-konsole-shell phase=marker-written launch_start_ms=%lld "
            "start_ms=%lld start_since_launch_ms=%lld open_ms=%lld "
            "marker_write_ms=%lld written_ms=%lld before_exec_ms=%lld "
            "pid=%ld tty0=\"%s\"\n",
            launch_start_ms, start_ms,
            delta_from_base(start_ms, launch_start_ms), open_ms,
            marker_write_ms, written_ms, before_exec_ms, (long)getpid(),
            tty0 ? tty0 : "missing");

    setenv("TERM", "xterm-256color", 0);
    dprintf(STDOUT_FILENO,
            "xv6-konsole-shell phase=before-exec bash=/bin/bash "
            "before_exec_ms=%lld before_exec_since_launch_ms=%lld pid=%ld "
            "prompt_marker=%s prompt_rc=%s\n",
            before_exec_ms, delta_from_base(before_exec_ms, launch_start_ms),
            (long)getpid(), prompt_marker, prompt_rc_ok ? "ready" : "failed");
    if (prompt_rc_ok)
        execl("/bin/bash", "bash", "--rcfile", rc_path, "-i", NULL);
    else
        execl("/bin/bash", "bash", "-i", NULL);
    fprintf(stderr,
            "kde_konsole_shell_wrapper exec /bin/bash failed errno=%d %s "
            "launch_start_ms=%lld before_exec_ms=%lld\n",
            errno, strerror(errno), launch_start_ms, before_exec_ms);
    return 127;
}
