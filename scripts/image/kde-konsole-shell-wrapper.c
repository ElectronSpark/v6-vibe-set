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

int main(int argc, char **argv)
{
    const char *marker = argc > 1 ? argv[1] : "/dev/shm/xv6-konsole-shell-ready";
    long long start_ms = monotonic_ms();
    long long open_ms;
    long long written_ms;
    long long before_exec_ms;
    int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    open_ms = monotonic_ms();
    if (fd < 0) {
        fprintf(stderr,
                "kde_konsole_shell_wrapper marker=%s open failed errno=%d %s "
                "start_ms=%lld open_ms=%lld\n",
                marker, errno, strerror(errno), start_ms, open_ms);
        return 2;
    }

    before_exec_ms = monotonic_ms();
    dprintf(fd,
            "xv6-konsole-shell-ready start_ms=%lld open_ms=%lld "
            "before_exec_ms=%lld pid=%ld ppid=%ld\n",
            start_ms, open_ms, before_exec_ms, (long)getpid(),
            (long)getppid());
    close(fd);
    written_ms = monotonic_ms();
    dprintf(STDOUT_FILENO,
            "xv6-konsole-shell-ready start_ms=%lld open_ms=%lld "
            "written_ms=%lld before_exec_ms=%lld\n",
            start_ms, open_ms, written_ms, before_exec_ms);

    setenv("TERM", "xterm-256color", 0);
    execl("/bin/bash", "bash", "-i", NULL);
    fprintf(stderr,
            "kde_konsole_shell_wrapper exec /bin/bash failed errno=%d %s "
            "before_exec_ms=%lld\n",
            errno, strerror(errno), before_exec_ms);
    return 127;
}
