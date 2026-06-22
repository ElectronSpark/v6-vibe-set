#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *marker = argc > 1 ? argv[1] : "/dev/shm/xv6-konsole-shell-ready";
    int fd = open(marker, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        fprintf(stderr, "kde_konsole_shell_wrapper marker=%s open failed errno=%d %s\n",
                marker, errno, strerror(errno));
        return 2;
    }

    dprintf(fd, "xv6-konsole-shell-ready\n");
    close(fd);
    dprintf(STDOUT_FILENO, "xv6-konsole-shell-ready\n");

    setenv("TERM", "xterm-256color", 0);
    execl("/bin/sh", "sh", "-i", NULL);
    fprintf(stderr, "kde_konsole_shell_wrapper exec /bin/sh failed errno=%d %s\n",
            errno, strerror(errno));
    return 127;
}
