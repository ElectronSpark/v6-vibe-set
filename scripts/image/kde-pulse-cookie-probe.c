#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int mkdir_if_needed(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == 0 || errno == EEXIST) {
        chmod(path, mode);
        return 0;
    }
    printf("kde_pulse_cookie_probe mkdir path=%s ret=-1 errno=%d %s\n",
           path, errno, strerror(errno));
    return 1;
}

static int write_full(int fd, const unsigned char *data, size_t len)
{
    size_t done = 0;

    while (done < len) {
        ssize_t n = write(fd, data + done, len - done);
        if (n < 0) {
            printf("kde_pulse_cookie_probe write fd=%d done=%zu len=%zu ret=-1 errno=%d %s\n",
                   fd, done, len, errno, strerror(errno));
            return -1;
        }
        if (n == 0) {
            printf("kde_pulse_cookie_probe write fd=%d done=%zu len=%zu ret=0\n",
                   fd, done, len);
            errno = EIO;
            return -1;
        }
        done += (size_t)n;
    }
    return 0;
}

static int read_full(int fd, unsigned char *data, size_t len, size_t *got)
{
    size_t done = 0;

    while (done < len) {
        ssize_t n = read(fd, data + done, len - done);
        if (n < 0) {
            printf("kde_pulse_cookie_probe read fd=%d done=%zu len=%zu ret=-1 errno=%d %s\n",
                   fd, done, len, errno, strerror(errno));
            return -1;
        }
        if (n == 0)
            break;
        done += (size_t)n;
    }
    *got = done;
    return 0;
}

static int pulse_cookie_flow(const char *path)
{
    unsigned char cookie[256];
    unsigned char loaded[256];
    struct flock lock;
    struct stat st;
    size_t got = 0;
    int fd;
    int failed = 0;

    unlink(path);
    memset(cookie, 0xa5, sizeof(cookie));
    memset(loaded, 0, sizeof(loaded));

    errno = 0;
    fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    printf("kde_pulse_cookie_probe open path=%s fd=%d errno=%d %s\n",
           path, fd, errno, strerror(errno));
    if (fd < 0)
        return 1;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    errno = 0;
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        printf("kde_pulse_cookie_probe lock path=%s ret=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        failed = 1;
        goto out_close;
    }

    errno = 0;
    if (read_full(fd, loaded, sizeof(loaded), &got) < 0) {
        failed = 1;
        goto out_unlock;
    }
    printf("kde_pulse_cookie_probe initial-read path=%s bytes=%zu errno=%d %s\n",
           path, got, errno, strerror(errno));
    if (got != 0) {
        printf("kde_pulse_cookie_probe initial-read-unexpected path=%s bytes=%zu\n",
               path, got);
        failed = 1;
        goto out_unlock;
    }

    errno = 0;
    if (lseek(fd, 0, SEEK_SET) != 0) {
        printf("kde_pulse_cookie_probe lseek path=%s ret=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        failed = 1;
        goto out_unlock;
    }

    errno = 0;
    if (ftruncate(fd, 0) < 0) {
        printf("kde_pulse_cookie_probe ftruncate path=%s ret=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        failed = 1;
        goto out_unlock;
    }

    errno = 0;
    if (write_full(fd, cookie, sizeof(cookie)) < 0) {
        failed = 1;
        goto out_unlock;
    }

    errno = 0;
    if (fstat(fd, &st) < 0) {
        printf("kde_pulse_cookie_probe fstat path=%s ret=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        failed = 1;
        goto out_unlock;
    }
    printf("kde_pulse_cookie_probe after-write path=%s size=%lld mode=0%o\n",
           path, (long long)st.st_size, (unsigned int)st.st_mode);
    if (st.st_size != (off_t)sizeof(cookie))
        failed = 1;

    errno = 0;
    if (fsync(fd) < 0) {
        printf("kde_pulse_cookie_probe fsync path=%s ret=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        failed = 1;
    }

out_unlock:
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    errno = 0;
    if (fcntl(fd, F_SETLKW, &lock) < 0) {
        printf("kde_pulse_cookie_probe unlock path=%s ret=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        failed = 1;
    }

out_close:
    errno = 0;
    if (close(fd) < 0) {
        printf("kde_pulse_cookie_probe close path=%s ret=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        failed = 1;
    }

    if (failed)
        return 1;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    printf("kde_pulse_cookie_probe reopen path=%s fd=%d errno=%d %s\n",
           path, fd, errno, strerror(errno));
    if (fd < 0)
        return 1;
    memset(loaded, 0, sizeof(loaded));
    got = 0;
    errno = 0;
    if (read_full(fd, loaded, sizeof(loaded), &got) < 0) {
        close(fd);
        return 1;
    }
    close(fd);
    if (got != sizeof(cookie) || memcmp(cookie, loaded, sizeof(cookie)) != 0) {
        printf("kde_pulse_cookie_probe verify path=%s bytes=%zu status=FAIL\n",
               path, got);
        return 1;
    }
    printf("kde_pulse_cookie_probe verify path=%s bytes=%zu status=PASS\n",
           path, got);
    return 0;
}

int main(void)
{
    int failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    failed |= mkdir_if_needed("/root", 0700);
    failed |= mkdir_if_needed("/root/.config", 0700);
    failed |= mkdir_if_needed("/root/.config/pulse", 0700);
    failed |= mkdir_if_needed("/dev/shm", 01777);
    failed |= mkdir_if_needed("/dev/shm/kde-config", 0700);
    failed |= mkdir_if_needed("/dev/shm/kde-config/pulse", 0700);

    if (!failed) {
        failed |= pulse_cookie_flow("/root/.config/pulse/cookie");
        failed |= pulse_cookie_flow("/dev/shm/kde-config/pulse/cookie");
    }

    printf("kde_pulse_cookie_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
