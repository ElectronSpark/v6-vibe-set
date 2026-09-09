#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static int report_ret(const char *op, int ret)
{
    if (ret < 0)
        printf("kde_config_atomic_probe %s ret=-1 errno=%d %s\n",
               op, errno, strerror(errno));
    else
        printf("kde_config_atomic_probe %s ret=%d errno=0\n", op, ret);
    return ret < 0 ? 1 : 0;
}

static int mkdir_if_needed(const char *path, mode_t mode)
{
    int ret = mkdir(path, mode);
    if (ret < 0 && errno == EEXIST)
        ret = 0;
    report_ret(path, ret);
    return ret;
}

static void dump_statvfs(const char *path)
{
    struct statvfs sv;

    errno = 0;
    if (statvfs(path, &sv) < 0) {
        printf("kde_config_atomic_probe statvfs path=%s errno=%d %s\n",
               path, errno, strerror(errno));
        return;
    }

    printf("kde_config_atomic_probe statvfs path=%s bsize=%lu blocks=%lu bfree=%lu bavail=%lu files=%lu ffree=%lu\n",
           path,
           (unsigned long)sv.f_bsize,
           (unsigned long)sv.f_blocks,
           (unsigned long)sv.f_bfree,
           (unsigned long)sv.f_bavail,
           (unsigned long)sv.f_files,
           (unsigned long)sv.f_ffree);
}

static int qsavefile_like(const char *dir, const char *name)
{
    char target[256];
    char tmpl[256];
    char lock_path[256];
    char old_path[256];
    int fd;
    int dirfd;
    ssize_t wr;
    int failed = 0;

    snprintf(target, sizeof(target), "%s/%s", dir, name);
    snprintf(tmpl, sizeof(tmpl), "%s/.%s.XXXXXX", dir, name);
    snprintf(lock_path, sizeof(lock_path), "%s/%s.lock", dir, name);
    snprintf(old_path, sizeof(old_path), "%s/%s.old", dir, name);

    printf("kde_config_atomic_probe case dir=%s target=%s\n", dir, target);
    dump_statvfs(dir);

    errno = 0;
    fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    failed |= report_ret("open-lock", fd);
    if (fd >= 0) {
        errno = 0;
        failed |= report_ret("flock-lock", flock(fd, LOCK_EX | LOCK_NB));
        errno = 0;
        failed |= report_ret("fcntl-cloexec-lock", fcntl(fd, F_SETFD, FD_CLOEXEC));
        errno = 0;
        failed |= report_ret("close-lock", close(fd));
    }

    errno = 0;
    fd = open(target, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    failed |= report_ret("open-initial", fd);
    if (fd >= 0) {
        errno = 0;
        wr = write(fd, "initial=1\n", 10);
        if (wr < 0) {
            printf("kde_config_atomic_probe write-initial ret=-1 errno=%d %s\n",
                   errno, strerror(errno));
            failed = 1;
        } else {
            printf("kde_config_atomic_probe write-initial ret=%ld errno=0\n",
                   (long)wr);
        }
        errno = 0;
        failed |= report_ret("fsync-initial", fsync(fd));
        errno = 0;
        failed |= report_ret("close-initial", close(fd));
    }

    errno = 0;
    fd = mkstemp(tmpl);
    failed |= report_ret("mkstemp", fd);
    if (fd < 0)
        return 1;

    errno = 0;
    failed |= report_ret("fcntl-cloexec-temp", fcntl(fd, F_SETFD, FD_CLOEXEC));
    errno = 0;
    failed |= report_ret("fchmod-temp", fchmod(fd, 0600));
    errno = 0;
    wr = write(fd, "[General]\nprobe=true\n", 21);
    if (wr < 0) {
        printf("kde_config_atomic_probe write-temp ret=-1 errno=%d %s\n",
               errno, strerror(errno));
        failed = 1;
    } else {
        printf("kde_config_atomic_probe write-temp ret=%ld errno=0\n",
               (long)wr);
    }

    errno = 0;
    failed |= report_ret("fdatasync-temp", fdatasync(fd));
    errno = 0;
    failed |= report_ret("fsync-temp", fsync(fd));
    errno = 0;
    failed |= report_ret("close-temp", close(fd));

    errno = 0;
    failed |= report_ret("rename-target-old", rename(target, old_path));
    errno = 0;
    failed |= report_ret("rename-temp-target", rename(tmpl, target));
    errno = 0;
    failed |= report_ret("unlink-old", unlink(old_path));

    errno = 0;
    dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    failed |= report_ret("open-dir", dirfd);
    if (dirfd >= 0) {
        errno = 0;
        failed |= report_ret("fsync-dir", fsync(dirfd));
        errno = 0;
        failed |= report_ret("close-dir", close(dirfd));
    }

    dump_statvfs(dir);
    return failed;
}

int main(void)
{
    int failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    failed |= mkdir_if_needed("/tmp", 01777);
    chmod("/tmp", 01777);
    dump_statvfs("/tmp");
    failed |= mkdir_if_needed("/dev/shm/kde-config", 0700);

    failed |= qsavefile_like("/dev/shm/kde-config", "xv6-kde-atomicrc");
    printf("kde_config_atomic_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
