// host_runtime_probe.c - Linux-built runtime ABI probe for xv6.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#ifndef SYS_close_range
#define SYS_close_range 436
#endif
#ifndef SYS_copy_file_range
#define SYS_copy_file_range 326
#endif
#ifndef SYS_getcpu
#define SYS_getcpu 309
#endif
#ifndef SYS_sched_setattr
#define SYS_sched_setattr 314
#endif
#ifndef SYS_sched_getattr
#define SYS_sched_getattr 315
#endif
#ifndef SYS_signalfd4
#define SYS_signalfd4 289
#endif
#ifndef SYS_capget
#define SYS_capget 125
#endif
#ifndef SYS_capset
#define SYS_capset 126
#endif
#ifndef SYS_readahead
#define SYS_readahead 187
#endif
#ifndef SYS_sync_file_range
#define SYS_sync_file_range 277
#endif
#ifndef SYS_syncfs
#define SYS_syncfs 306
#endif
#ifndef SYS_ioprio_set
#define SYS_ioprio_set 251
#endif
#ifndef SYS_ioprio_get
#define SYS_ioprio_get 252
#endif
#ifndef SYS_pselect6
#define SYS_pselect6 270
#endif
#ifndef SYS_ppoll
#define SYS_ppoll 271
#endif
#ifndef O_PATH
#define O_PATH 010000000
#endif
#ifndef O_TMPFILE
#define O_TMPFILE 020200000
#endif
#ifndef O_PATH
#define O_PATH 010000000
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0400000
#endif
#ifndef O_NOATIME
#define O_NOATIME 01000000
#endif
#ifndef O_DIRECT
#define O_DIRECT 040000
#endif

#define IOPRIO_WHO_PROCESS 1

struct linux_open_how {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};

struct linux_cap_header {
    uint32_t version;
    int pid;
};

struct linux_cap_data {
    uint32_t effective;
    uint32_t permitted;
    uint32_t inheritable;
};

struct linux_sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
    uint32_t sched_util_min;
    uint32_t sched_util_max;
};

#define LINUX_CAPABILITY_VERSION_3 0x20080522

static void require_ok(int cond, const char *what)
{
    if (!cond) {
        printf("host_runtime_probe: FAIL %s errno=%d\n", what, errno);
        _exit(1);
    }
}

static void *thread_main(void *arg)
{
    int *slot = arg;
    *slot = 42;
    return slot;
}

static void test_pthread_malloc(void)
{
    int value = 0;
    pthread_t th;
    void *ret = NULL;
    char *mem = malloc(65536);
    require_ok(mem != NULL, "malloc");
    memset(mem, 0x5a, 65536);
    require_ok(pthread_create(&th, NULL, thread_main, &value) == 0,
               "pthread_create");
    require_ok(pthread_join(th, &ret) == 0, "pthread_join");
    require_ok(ret == &value && value == 42, "pthread result");
    free(mem);
    printf("host_runtime_probe: pthread/malloc OK\n");
}

static void test_fork_wait(void)
{
    pid_t pid = fork();
    require_ok(pid >= 0, "fork");
    if (pid == 0)
        _exit(23);
    int status = 0;
    require_ok(waitpid(pid, &status, 0) == pid, "waitpid");
    require_ok(WIFEXITED(status) && WEXITSTATUS(status) == 23,
               "wait status");
    printf("host_runtime_probe: fork/wait OK\n");
}

static void test_event_select(void)
{
    int fd = eventfd(3, 0);
    require_ok(fd >= 0, "eventfd");
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv = {0, 0};
    require_ok(select(fd + 1, &rfds, NULL, NULL, &tv) == 1, "select");
    uint64_t val = 0;
    require_ok(read(fd, &val, sizeof(val)) == (ssize_t)sizeof(val),
               "eventfd read");
    require_ok(val == 3, "eventfd value");
    close(fd);
    printf("host_runtime_probe: eventfd/select OK\n");
}

static void test_pipe_fcntl(void)
{
    int pipefd[2];
    require_ok(pipe2(pipefd, O_CLOEXEC) == 0, "pipe2");
    int size = fcntl(pipefd[0], F_GETPIPE_SZ, 0);
    require_ok(size > 0, "F_GETPIPE_SZ");
    require_ok(fcntl(pipefd[1], F_SETPIPE_SZ, size) == size,
               "F_SETPIPE_SZ fixed");
    require_ok(fcntl(pipefd[0], F_SETPIPE_SZ, -1) < 0 && errno == EINVAL,
               "F_SETPIPE_SZ negative");
    int fd = open("host-runtime-pipecheck", O_CREAT | O_RDWR | O_TRUNC, 0644);
    require_ok(fd >= 0, "pipecheck open");
    require_ok(fcntl(fd, F_GETPIPE_SZ, 0) < 0 && errno == EBADF,
               "F_GETPIPE_SZ non-pipe");
    int dupfd = fcntl(fd, F_DUPFD_CLOEXEC, fd + 10);
    require_ok(dupfd >= fd + 10, "F_DUPFD_CLOEXEC");
    close(dupfd);
    errno = 0;
    require_ok(fcntl(fd, 14, 0) < 0 && errno == EINVAL,
               "fcntl command 14 invalid");
    require_ok(fcntl(fd, F_SETOWN, getpid()) == 0, "F_SETOWN");
    require_ok(fcntl(fd, F_GETOWN, 0) == getpid(), "F_GETOWN");
    require_ok(fcntl(fd, F_SETSIG, SIGUSR1) == 0, "F_SETSIG");
    require_ok(fcntl(fd, F_GETSIG, 0) == SIGUSR1, "F_GETSIG");
    struct f_owner_ex owner = {
        .type = F_OWNER_PID,
        .pid = getpid(),
    };
    require_ok(fcntl(fd, F_SETOWN_EX, &owner) == 0, "F_SETOWN_EX");
    memset(&owner, 0, sizeof(owner));
    require_ok(fcntl(fd, F_GETOWN_EX, &owner) == 0, "F_GETOWN_EX");
    require_ok(owner.type == F_OWNER_PID && owner.pid == getpid(),
               "F_GETOWN_EX values");
    require_ok(fcntl(fd, F_GETLEASE, 0) == F_UNLCK, "F_GETLEASE");
    require_ok(fcntl(fd, F_SETLEASE, F_UNLCK) == 0, "F_SETLEASE unlock");
    require_ok(fcntl(fd, F_SETLEASE, F_WRLCK) < 0 && errno == EAGAIN,
               "F_SETLEASE unsupported write lease");
    require_ok(fcntl(fd, F_NOTIFY, 0) == 0, "F_NOTIFY clear");
    require_ok(fcntl(fd, F_NOTIFY, DN_MODIFY) < 0 && errno == ENOTDIR,
               "F_NOTIFY non-dir");
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_len = 1;
    require_ok(fcntl(fd, F_OFD_SETLK, &fl) == 0, "F_OFD_SETLK");
    int lock_fd = open("host-runtime-pipecheck", O_RDWR);
    require_ok(lock_fd >= 0, "ofd second open");
    struct flock probe_fl = fl;
    require_ok(fcntl(lock_fd, F_OFD_GETLK, &probe_fl) == 0,
               "F_OFD_GETLK");
    require_ok(probe_fl.l_type == F_WRLCK && probe_fl.l_pid == -1,
               "F_OFD_GETLK values");
    fl.l_type = F_UNLCK;
    require_ok(fcntl(fd, F_OFD_SETLK, &fl) == 0, "F_OFD unlock");
    close(lock_fd);
    close(fd);
    unlink("host-runtime-pipecheck");
    close(pipefd[0]);
    close(pipefd[1]);
    printf("host_runtime_probe: pipe/fcntl OK\n");
}

static void test_inotify(void)
{
    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    require_ok(fd >= 0, "inotify_init1");
    int wd = inotify_add_watch(fd, ".", IN_MODIFY);
    require_ok(wd > 0, "inotify_add_watch");
    char byte = 0;
    require_ok(read(fd, &byte, 1) < 0 && errno == EAGAIN,
               "empty inotify read");
    require_ok(inotify_rm_watch(fd, wd) == 0, "inotify_rm_watch");
    close(fd);
    printf("host_runtime_probe: inotify OK\n");
}

static void test_signalfd(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    int fd = syscall(SYS_signalfd4, -1, &mask, 8,
                     SFD_NONBLOCK | SFD_CLOEXEC);
    require_ok(fd >= 0, "signalfd4");
    char info[128];
    require_ok(read(fd, info, sizeof(info)) < 0 && errno == EAGAIN,
               "empty signalfd read");
    close(fd);
    printf("host_runtime_probe: signalfd OK\n");
}

static void test_pselect_ppoll_sigmask(void)
{
    struct timespec ts = {0, 0};
    uint64_t mask = 0;
    struct {
        void *ss;
        size_t ss_len;
    } psig = {&mask, 8};

    require_ok(syscall(SYS_pselect6, 0, NULL, NULL, NULL, &ts, &psig) == 0,
               "pselect6 sigmask");
    psig.ss_len = 4;
    require_ok(syscall(SYS_pselect6, 0, NULL, NULL, NULL, &ts, &psig) < 0 &&
                   errno == EINVAL,
               "pselect6 short sigset");
    psig.ss_len = 12;
    require_ok(syscall(SYS_pselect6, 0, NULL, NULL, NULL, &ts, &psig) < 0 &&
                   errno == EINVAL,
               "pselect6 oversized sigset");

    require_ok(syscall(SYS_ppoll, NULL, 0, &ts, &mask, 8) == 0,
               "ppoll sigmask");
    require_ok(syscall(SYS_ppoll, NULL, 0, &ts, &mask, 4) < 0 &&
                   errno == EINVAL,
               "ppoll short sigset");
    require_ok(syscall(SYS_ppoll, NULL, 0, &ts, &mask, 12) < 0 &&
                   errno == EINVAL,
               "ppoll oversized sigset");
    printf("host_runtime_probe: pselect/ppoll sigmask OK\n");
}

static void test_accept4_flags(void)
{
    const char *path = "host-runtime-accept4.sock";
    unlink(path);

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    require_ok(lfd >= 0, "accept4 listener socket");
    int cfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    require_ok(cfd >= 0, "accept4 client socket");

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, path);
    socklen_t sun_len = sizeof(sun.sun_family) + strlen(sun.sun_path) + 1;

    require_ok(bind(lfd, (struct sockaddr *)&sun, sun_len) == 0,
               "accept4 bind");
    require_ok(listen(lfd, 1) == 0, "accept4 listen");
    require_ok(accept4(lfd, NULL, NULL, 0x40000000) < 0 && errno == EINVAL,
               "accept4 invalid flags");

    int cr = connect(cfd, (struct sockaddr *)&sun, sun_len);
    require_ok(cr == 0 || errno == EINPROGRESS, "accept4 client connect");

    int afd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    require_ok(afd >= 0, "accept4");
    require_ok((fcntl(afd, F_GETFD) & FD_CLOEXEC) != 0,
               "accept4 SOCK_CLOEXEC");
    require_ok((fcntl(afd, F_GETFL) & O_NONBLOCK) != 0,
               "accept4 SOCK_NONBLOCK");

    close(afd);
    close(cfd);
    close(lfd);
    unlink(path);
    printf("host_runtime_probe: accept4 flags OK\n");
}

static void test_fstatfs_layout(void)
{
    int fd = open(".", O_RDONLY | O_DIRECTORY);
    require_ok(fd >= 0, "fstatfs open");
    struct statfs sfs;
    memset(&sfs, 0, sizeof(sfs));
    require_ok(fstatfs(fd, &sfs) == 0, "fstatfs");
    require_ok(sfs.f_bsize > 0 && sfs.f_namelen > 0, "fstatfs fields");
    errno = 0;
    require_ok(fstatfs(-1, &sfs) < 0 && errno == EBADF, "fstatfs bad fd");
    close(fd);
    printf("host_runtime_probe: fstatfs OK\n");
}

static void test_file_range(void)
{
    struct linux_open_how how;
    memset(&how, 0, sizeof(how));
    how.flags = O_CREAT | O_RDWR | O_TRUNC;
    how.mode = 0644;

    int a = syscall(SYS_openat2, AT_FDCWD, "host-runtime-a", &how,
                    sizeof(how));
    require_ok(a >= 0, "openat2 source");
    int b = syscall(SYS_openat2, AT_FDCWD, "host-runtime-b", &how,
                    sizeof(how));
    require_ok(b >= 0, "openat2 dest");
    char w0 = 'A';
    char w1 = 'B';
    char r0 = 0;
    char r1 = 0;
    char c = 0;
    struct iovec wiov[2] = {
        { &w0, 1 },
        { &w1, 1 },
    };
    struct iovec riov[2] = {
        { &r0, 1 },
        { &r1, 1 },
    };
    require_ok(writev(a, wiov, 2) == 2, "writev");
    require_ok(lseek(a, 0, SEEK_SET) == 0, "lseek source");
    require_ok(readv(a, riov, 2) == 2 && r0 == 'A' && r1 == 'B',
               "readv");
    require_ok(ftruncate(a, 0) == 0, "ftruncate source");
    require_ok(lseek(a, 0, SEEK_SET) == 0, "rewind truncated source");
    require_ok(pwrite(a, "Y", 1, 0) == 1, "pwrite source");
    require_ok(fsync(a) == 0, "fsync source");
    require_ok(fdatasync(a) == 0, "fdatasync source");
    require_ok(access("host-runtime-a", F_OK) == 0, "access source");
    require_ok(lseek(a, 0, SEEK_SET) == 0, "rewind pwrite source");
    require_ok(read(a, &c, 1) == 1 && c == 'Y', "pwrite data");
    require_ok(ftruncate(a, 0) == 0, "ftruncate after pwrite");
    require_ok(lseek(a, 0, SEEK_SET) == 0, "rewind source for copy");
    require_ok(write(a, "Z", 1) == 1, "write source");

    off_t in_off = 0;
    off_t out_off = 0;
    require_ok(syscall(SYS_copy_file_range, a, &in_off, b, &out_off, 1, 0) == 1,
               "copy_file_range");
    require_ok(lseek(b, 0, SEEK_SET) == 0, "lseek dest");
    require_ok(read(b, &c, 1) == 1 && c == 'Z', "copied data");

    int dupfd = dup(b);
    require_ok(dupfd >= 0, "dup for close_range");
    require_ok(syscall(SYS_close_range, dupfd, dupfd, 0) == 0,
               "close_range");
    require_ok(read(dupfd, &c, 1) < 0 && errno == EBADF,
               "close_range closed");

    int pfd = openat(AT_FDCWD, "host-runtime-a", O_PATH | O_CLOEXEC);
    require_ok(pfd >= 0, "openat O_PATH");
    struct stat st;
    require_ok(fstat(pfd, &st) == 0, "fstat O_PATH");
    require_ok(read(pfd, &c, 1) < 0 && errno == EBADF, "read O_PATH");
    close(pfd);
    require_ok(openat(AT_FDCWD, "host-runtime-a", O_DIRECTORY) < 0 &&
                   errno == ENOTDIR,
               "openat O_DIRECTORY non-dir");
    require_ok(openat(AT_FDCWD, "/", O_TMPFILE | O_RDWR, 0600) < 0 &&
                   errno == EOPNOTSUPP,
               "openat O_TMPFILE unsupported");
    unlink("host-runtime-open-link");
    unlink("host-runtime-mode");
    require_ok(symlink("host-runtime-a", "host-runtime-open-link") == 0,
               "open symlink setup");
    require_ok(openat(AT_FDCWD, "host-runtime-open-link", O_NOFOLLOW) < 0 &&
                   errno == ELOOP,
               "openat O_NOFOLLOW symlink");
    pfd = openat(AT_FDCWD, "host-runtime-open-link",
                 O_PATH | O_NOFOLLOW | O_CLOEXEC);
    require_ok(pfd >= 0, "openat O_PATH|O_NOFOLLOW");
    require_ok(fstat(pfd, &st) == 0 && S_ISLNK(st.st_mode),
               "fstat O_PATH symlink");
    close(pfd);
    int mode_fd = openat(AT_FDCWD, "host-runtime-mode",
                         O_CREAT | O_EXCL | O_RDWR, 0600);
    require_ok(mode_fd >= 0, "openat create mode");
    require_ok(fstat(mode_fd, &st) == 0 && (st.st_mode & 0777) == 0600,
               "openat mode value");
    close(mode_fd);
    require_ok(openat(AT_FDCWD, "host-runtime-a/",
                      O_RDONLY | O_NOATIME | O_DIRECT) < 0 &&
                   errno == ENOTDIR,
               "openat trailing slash non-dir");
    int direct_fd = openat(AT_FDCWD, "host-runtime-a",
                           O_RDONLY | O_NOATIME | O_DIRECT);
    require_ok(direct_fd >= 0, "openat O_NOATIME O_DIRECT");
    close(direct_fd);
    require_ok(syscall(SYS_readahead, a, 0, 4096) == 0, "readahead");
    require_ok(syscall(SYS_sync_file_range, a, 0, 1, 0) == 0,
               "sync_file_range");
    require_ok(syscall(SYS_syncfs, a) == 0, "syncfs");

    unlink("host-runtime-link");
    unlink("host-runtime-renamed");
    unlink("host-runtime-symlink");
    rmdir("host-runtime-dir");
    require_ok(mkdir("host-runtime-dir", 0755) == 0, "mkdir");
    require_ok(chdir("host-runtime-dir") == 0, "chdir child");
    require_ok(chdir("..") == 0, "chdir parent");
    require_ok(rmdir("host-runtime-dir") == 0, "rmdir");
    require_ok(link("host-runtime-a", "host-runtime-link") == 0, "link");
    require_ok(rename("host-runtime-link", "host-runtime-renamed") == 0,
               "rename");
    require_ok(symlink("host-runtime-renamed", "host-runtime-symlink") == 0,
               "symlink");
    char target[64];
    ssize_t target_len = readlink("host-runtime-symlink", target,
                                  sizeof(target));
    require_ok(target_len == (ssize_t)strlen("host-runtime-renamed") &&
                   memcmp(target, "host-runtime-renamed",
                          strlen("host-runtime-renamed")) == 0,
               "readlink");
    require_ok(unlink("host-runtime-symlink") == 0, "unlink symlink");
    require_ok(unlink("host-runtime-renamed") == 0, "unlink renamed");
    require_ok(unlink("host-runtime-open-link") == 0, "unlink open symlink");
    require_ok(unlink("host-runtime-mode") == 0, "unlink mode file");

    close(a);
    close(b);
    unlink("host-runtime-a");
    unlink("host-runtime-b");
    printf("host_runtime_probe: file/openat2/copy/close_range OK\n");
}

static void test_misc_syscalls(void)
{
    unsigned cpu = 999;
    unsigned node = 999;
    require_ok(syscall(SYS_getcpu, &cpu, &node, NULL) == 0, "getcpu");
    require_ok(node == 0, "getcpu node");

    struct linux_cap_header hdr = {
        .version = LINUX_CAPABILITY_VERSION_3,
        .pid = 0,
    };
    struct linux_cap_data data[2];
    memset(data, 0xff, sizeof(data));
    require_ok(syscall(SYS_capget, &hdr, data) == 0, "capget");
    require_ok(data[0].effective == 0 && data[1].permitted == 0,
               "capget zero");
    memset(data, 0, sizeof(data));
    require_ok(syscall(SYS_capset, &hdr, data) == 0, "capset zero");
    require_ok(syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, 0) >= 0,
               "ioprio_get");
    require_ok(syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, 0) == 0,
               "ioprio_set");

    struct linux_sched_attr attr;
    memset(&attr, 0, sizeof(attr));
    require_ok(syscall(SYS_sched_getattr, 0, &attr, sizeof(attr), 0) == 0,
               "sched_getattr");
    require_ok(attr.size >= 48 && attr.sched_policy == 0 &&
                   attr.sched_priority == 0,
               "sched_getattr values");
    attr.size = sizeof(attr);
    require_ok(syscall(SYS_sched_setattr, 0, &attr, 0) == 0,
               "sched_setattr");
    printf("host_runtime_probe: getcpu/capabilities OK\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_pthread_malloc();
    test_fork_wait();
    test_event_select();
    test_pipe_fcntl();
    test_inotify();
    test_signalfd();
    test_pselect_ppoll_sigmask();
    test_accept4_flags();
    test_fstatfs_layout();
    test_file_range();
    test_misc_syscalls();
    printf("host_runtime_probe: OK\n");
    return 0;
}
