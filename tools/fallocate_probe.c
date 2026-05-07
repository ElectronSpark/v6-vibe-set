#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int check_size(int fd, off_t expected)
{
    off_t end = lseek(fd, 0, SEEK_END);
    if (end != expected) {
        printf("fallocate_probe: size got=%lld expected=%lld\n",
               (long long)end, (long long)expected);
        return -1;
    }
    return 0;
}

int main(void)
{
    const char *path = "/tmp/fallocate-probe.bin";
    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (fd < 0) {
        printf("fallocate_probe: open failed errno=%d (%s)\n", errno,
               strerror(errno));
        return 1;
    }

    if (fallocate(fd, 0, 0, 8192) != 0) {
        printf("fallocate_probe: grow failed errno=%d (%s)\n", errno,
               strerror(errno));
        return 1;
    }
    if (check_size(fd, 8192) != 0)
        return 1;

    if (pwrite(fd, "ABCDEFGH", 8, 1024) != 8) {
        printf("fallocate_probe: pwrite failed errno=%d (%s)\n", errno,
               strerror(errno));
        return 1;
    }
    if (fallocate(fd, FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
                  1024, 8) != 0) {
        printf("fallocate_probe: punch failed errno=%d (%s)\n", errno,
               strerror(errno));
        return 1;
    }
    char buf[8];
    if (pread(fd, buf, sizeof(buf), 1024) != (ssize_t)sizeof(buf)) {
        printf("fallocate_probe: pread failed errno=%d (%s)\n", errno,
               strerror(errno));
        return 1;
    }
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            printf("fallocate_probe: punch byte[%zu]=0x%x\n", i,
                   (unsigned char)buf[i]);
            return 1;
        }
    }

    if (fallocate(fd, FALLOC_FL_KEEP_SIZE, 16384, 4096) != 0) {
        printf("fallocate_probe: keep-size failed errno=%d (%s)\n", errno,
               strerror(errno));
        return 1;
    }
    if (check_size(fd, 8192) != 0)
        return 1;

    close(fd);
    unlink(path);
    printf("fallocate_probe: PASS\n");
    return 0;
}
