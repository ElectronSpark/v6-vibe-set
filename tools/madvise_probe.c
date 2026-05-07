#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MADV_POPULATE_READ
#define MADV_POPULATE_READ 22
#endif
#ifndef MADV_POPULATE_WRITE
#define MADV_POPULATE_WRITE 23
#endif

static int expect_ok(const char *name, int ret)
{
    if (ret == 0)
        return 0;
    fprintf(stderr, "madvise_probe: %s failed errno=%d (%s)\n",
            name, errno, strerror(errno));
    return 1;
}

static int wait_status(pid_t pid)
{
    int status = 0;

    if (waitpid(pid, &status, 0) != pid)
        return 1;
    if (!WIFEXITED(status))
        return 1;
    return WEXITSTATUS(status);
}

int main(void)
{
    const size_t len = 4096;
    char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED) {
        fprintf(stderr, "madvise_probe: mmap failed errno=%d (%s)\n",
                errno, strerror(errno));
        return 1;
    }

    p[0] = 0x5a;
    if (expect_ok("MADV_DONTDUMP", madvise(p, len, MADV_DONTDUMP)) ||
        expect_ok("MADV_DODUMP", madvise(p, len, MADV_DODUMP)) ||
        expect_ok("MADV_MERGEABLE", madvise(p, len, MADV_MERGEABLE)) ||
        expect_ok("MADV_UNMERGEABLE", madvise(p, len, MADV_UNMERGEABLE)) ||
        expect_ok("MADV_HUGEPAGE", madvise(p, len, MADV_HUGEPAGE)) ||
        expect_ok("MADV_NOHUGEPAGE", madvise(p, len, MADV_NOHUGEPAGE)) ||
        expect_ok("MADV_POPULATE_READ", madvise(p, len, MADV_POPULATE_READ)) ||
        expect_ok("MADV_POPULATE_WRITE", madvise(p, len, MADV_POPULATE_WRITE))) {
        return 1;
    }

    if (expect_ok("MADV_DONTFORK", madvise(p, len, MADV_DONTFORK)))
        return 1;

    pid_t pid = fork();
    if (pid == 0) {
        errno = 0;
        int ret = madvise(p, len, MADV_NORMAL);
        _exit(ret == -1 && errno == ENOMEM ? 0 : 2);
    }
    if (pid < 0 || wait_status(pid) != 0) {
        fprintf(stderr, "madvise_probe: MADV_DONTFORK child check failed\n");
        return 1;
    }

    if (expect_ok("MADV_DOFORK", madvise(p, len, MADV_DOFORK)))
        return 1;
    pid = fork();
    if (pid == 0)
        _exit(p[0] == 0x5a ? 0 : 3);
    if (pid < 0 || wait_status(pid) != 0) {
        fprintf(stderr, "madvise_probe: MADV_DOFORK child check failed\n");
        return 1;
    }

    if (expect_ok("MADV_WIPEONFORK", madvise(p, len, MADV_WIPEONFORK)))
        return 1;
    pid = fork();
    if (pid == 0)
        _exit(p[0] == 0 ? 0 : 4);
    if (pid < 0 || wait_status(pid) != 0) {
        fprintf(stderr, "madvise_probe: MADV_WIPEONFORK child check failed\n");
        return 1;
    }

    if (expect_ok("MADV_KEEPONFORK", madvise(p, len, MADV_KEEPONFORK)))
        return 1;

    munmap(p, len);
    fprintf(stderr, "madvise_probe: PASS\n");
    return 0;
}
