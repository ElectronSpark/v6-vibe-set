#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;

static void
check_lstat64(const char *label, const char *path, int expect_errno)
{
    struct stat64 st;
    int ret;
    int saved;

    errno = 0;
    memset(&st, 0, sizeof(st));
    ret = lstat64(path, &st);
    saved = errno;
    printf("kde_trash_stat_probe lstat64 label=%s path=\"%s\" ret=%d errno=%d %s mode=0%o dev=%llu ino=%llu\n",
           label, path, ret, saved, strerror(saved), ret == 0 ? st.st_mode : 0,
           ret == 0 ? (unsigned long long)st.st_dev : 0,
           ret == 0 ? (unsigned long long)st.st_ino : 0);
    if ((expect_errno == 0 && ret != 0) ||
        (expect_errno != 0 && (ret == 0 || saved != expect_errno)))
        failures++;
}

static void
check_newfstatat(const char *label, const char *path, int flags,
                 int expect_errno)
{
    struct stat st;
    long ret;
    int saved;

    errno = 0;
    memset(&st, 0, sizeof(st));
    ret = syscall(SYS_newfstatat, AT_FDCWD, path, &st, flags);
    saved = errno;
    printf("kde_trash_stat_probe newfstatat label=%s path=\"%s\" flags=0x%x ret=%ld errno=%d %s mode=0%o dev=%llu ino=%llu\n",
           label, path, flags, ret, saved, strerror(saved),
           ret == 0 ? st.st_mode : 0,
           ret == 0 ? (unsigned long long)st.st_dev : 0,
           ret == 0 ? (unsigned long long)st.st_ino : 0);
    if ((expect_errno == 0 && ret != 0) ||
        (expect_errno != 0 && (ret == 0 || saved != expect_errno)))
        failures++;
}

int
main(void)
{
    const char *home = getenv("HOME");
    struct passwd *pw = getpwuid(getuid());
    const char *pw_home = pw && pw->pw_dir ? pw->pw_dir : "";

    printf("kde_trash_stat_probe uid=%ld HOME=\"%s\" passwd_home=\"%s\"\n",
           (long)getuid(), home ? home : "(unset)", pw_home);

    if (!home || strcmp(home, "/root") != 0)
        failures++;
    if (strcmp(pw_home, "/root") != 0)
        failures++;

    check_lstat64("home", home ? home : "", 0);
    check_lstat64("passwd-home", pw_home, 0);
    check_lstat64("root", "/root", 0);
    check_lstat64("empty", "", ENOENT);
    check_newfstatat("home-nofollow", home ? home : "",
                     AT_SYMLINK_NOFOLLOW, 0);
    check_newfstatat("empty-no-empty-path", "", AT_SYMLINK_NOFOLLOW,
                     ENOENT);

    printf("kde_trash_stat_probe result=%s failures=%d\n",
           failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
