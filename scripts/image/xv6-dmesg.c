#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_syslog
#define SYS_syslog 103
#endif

static int print_file(const char *path) {
    char buf[4096];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0)
            break;
        if (n < 0) {
            int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));
            if (w < 0) {
                int saved = errno;
                close(fd);
                errno = saved;
                return -1;
            }
            off += w;
        }
    }

    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    int clear = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clear") == 0) {
            clear = 1;
            continue;
        }
        fprintf(stderr, "dmesg: unsupported option: %s\n", argv[i]);
        return 1;
    }

    if (print_file("/proc/kmsg") < 0) {
        fprintf(stderr, "dmesg: cannot read /proc/kmsg: %s\n",
                strerror(errno));
        return 1;
    }

    if (clear) {
        if (syscall(SYS_syslog, 5, NULL, 0) < 0) {
            fprintf(stderr, "dmesg: cannot clear kernel log: %s\n",
                    strerror(errno));
            return 1;
        }
    }

    return 0;
}
