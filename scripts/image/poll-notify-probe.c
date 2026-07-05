/*
 * poll-notify-probe: reducer for the N5 poll notify-full-wait lost-wakeup
 * defects (timerfd / pipe / inotify producer-side missing knote notifies).
 *
 * Boot the kernel with poll_notify_full_wait=1 so blocking poll() parks in
 * the no-rescan kqueue wait path (timeout=-1 -> waits forever on a lost
 * wakeup).  Each test deadlocks on a pre-fix kernel (watchdog SIGALRM
 * kills the hung child -> FAIL line) and completes post-fix.
 *
 *   1. pipe-blocking-write: poller parks on an empty pipe (poll -1);
 *      writer then issues one write() far larger than the pipe ring and
 *      blocks on the full ring.  Pre-fix the readable notify only fires
 *      when write() returns, so the poll-only reader never drains the
 *      ring: deadlock.
 *   2. timerfd-repeat: 20ms interval timer, poll(-1) + read until 10
 *      expirations.  Guards the repeating-timer/notify path end to end.
 *   3. inotify-multi-watcher: two independent inotify fds watch the same
 *      file from two children, both parked in poll(-1); parent modifies
 *      the file; BOTH children must wake.  Pre-fix only the first
 *      matching watcher's fd was knote-notified.
 *
 * Output: POLL-NOTIFY-PROBE: <test> PASS|FAIL(detail) per test and a
 * final POLL-NOTIFY-PROBE: RESULT=PASS|FAIL summary line.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WRITE_TOTAL (1u << 20) /* 1 MiB: far beyond any pipe ring size */
#define WATCHDOG_SECS 15

static int g_failures;

static void report(const char *test, int ok, const char *detail)
{
    if (ok) {
        printf("POLL-NOTIFY-PROBE: %s PASS\n", test);
    } else {
        printf("POLL-NOTIFY-PROBE: %s FAIL(%s)\n", test, detail);
        g_failures++;
    }
    fflush(stdout);
}

/* waitpid with a coarse deadline; returns 0 if child exited 0. */
static int wait_child(pid_t pid, int secs, const char *who)
{
    for (int i = 0; i < secs * 10; i++) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                return 0;
            fprintf(stderr, "poll-notify-probe: %s exited status=%d\n",
                    who, status);
            return -1;
        }
        if (r < 0)
            return -1;
        struct timespec ts = {0, 100 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    fprintf(stderr, "poll-notify-probe: %s HUNG, killing\n", who);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return -1;
}

/* ── test 1: pipe blocking write vs poll-only reader ─────────────────── */
static void test_pipe_blocking_write(void)
{
    int fds[2];
    if (pipe(fds) < 0) {
        report("pipe-blocking-write", 0, "pipe");
        return;
    }

    pid_t reader = fork();
    if (reader == 0) {
        /* poll-only reader: never issues a blocking read */
        close(fds[1]);
        alarm(WATCHDOG_SECS);
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        size_t total = 0;
        int eof = 0;
        char buf[65536];
        while (total < WRITE_TOTAL && !eof) {
            struct pollfd p = {.fd = fds[0], .events = POLLIN};
            int pr = poll(&p, 1, -1); /* -1: no rescan safety net */
            if (pr < 0)
                _exit(2);
            for (;;) {
                ssize_t n = read(fds[0], buf, sizeof(buf));
                if (n > 0) {
                    total += (size_t)n;
                    continue;
                }
                if (n == 0) { /* writer exited, all write ends closed */
                    eof = 1;
                    break;
                }
                if (errno == EAGAIN)
                    break;
                _exit(3);
            }
        }
        _exit(total == WRITE_TOTAL ? 0 : 6);
    }

    pid_t writer = fork();
    if (writer == 0) {
        close(fds[0]);
        alarm(WATCHDOG_SECS);
        /* let the reader park inside poll() first */
        struct timespec ts = {0, 300 * 1000 * 1000};
        nanosleep(&ts, NULL);
        char *buf = malloc(WRITE_TOTAL);
        if (buf == NULL)
            _exit(4);
        memset(buf, 0x5a, WRITE_TOTAL);
        ssize_t n = write(fds[1], buf, WRITE_TOTAL); /* blocks on full ring */
        _exit(n == (ssize_t)WRITE_TOTAL ? 0 : 5);
    }

    close(fds[0]);
    close(fds[1]);
    int ok = wait_child(writer, WATCHDOG_SECS + 3, "pipe-writer") == 0;
    ok &= wait_child(reader, WATCHDOG_SECS + 3, "pipe-reader") == 0;
    report("pipe-blocking-write", ok, "deadlock-or-error");
}

/* ── test 2: repeating timerfd via poll(-1) ──────────────────────────── */
static void test_timerfd_repeat(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        alarm(WATCHDOG_SECS);
        int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (tfd < 0)
            _exit(2);
        struct itimerspec its = {
            .it_interval = {0, 20 * 1000 * 1000},
            .it_value = {0, 20 * 1000 * 1000},
        };
        if (timerfd_settime(tfd, 0, &its, NULL) < 0)
            _exit(3);
        uint64_t seen = 0;
        while (seen < 10) {
            struct pollfd p = {.fd = tfd, .events = POLLIN};
            if (poll(&p, 1, -1) < 0)
                _exit(4);
            uint64_t exp = 0;
            if (read(tfd, &exp, sizeof(exp)) != sizeof(exp))
                _exit(5);
            seen += exp;
        }
        _exit(0);
    }
    int ok = wait_child(pid, WATCHDOG_SECS + 3, "timerfd") == 0;
    report("timerfd-repeat", ok, "deadlock-or-error");
}

/* ── test 3: two inotify fds watching the same inode ─────────────────── */
static int inotify_watch_child(const char *path, int ready_fd)
{
    alarm(WATCHDOG_SECS);
    int ifd = inotify_init1(0);
    if (ifd < 0)
        _exit(2);
    if (inotify_add_watch(ifd, path, IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE) < 0)
        _exit(3);
    /* signal readiness AFTER the watch exists */
    if (write(ready_fd, "R", 1) != 1)
        _exit(4);
    close(ready_fd);
    struct pollfd p = {.fd = ifd, .events = POLLIN};
    if (poll(&p, 1, -1) < 0) /* pre-fix: 2nd watcher hangs here */
        _exit(5);
    char buf[4096];
    if (read(ifd, buf, sizeof(buf)) <= 0)
        _exit(6);
    _exit(0);
}

static void test_inotify_multi_watcher(void)
{
    const char *path = "/tmp/poll-notify-probe-inotify";
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        report("inotify-multi-watcher", 0, "create");
        return;
    }
    close(fd);

    int ready[2];
    if (pipe(ready) < 0) {
        report("inotify-multi-watcher", 0, "pipe");
        return;
    }

    pid_t kids[2];
    for (int i = 0; i < 2; i++) {
        kids[i] = fork();
        if (kids[i] == 0) {
            close(ready[0]);
            inotify_watch_child(path, ready[1]);
        }
    }
    close(ready[1]);

    /* both watches registered? */
    char c;
    int got = 0;
    while (got < 2 && read(ready[0], &c, 1) == 1)
        got++;
    close(ready[0]);
    if (got != 2) {
        report("inotify-multi-watcher", 0, "readiness");
        for (int i = 0; i < 2; i++)
            kill(kids[i], SIGKILL);
        return;
    }
    /* let both park inside poll() */
    struct timespec ts = {0, 300 * 1000 * 1000};
    nanosleep(&ts, NULL);

    fd = open(path, O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        (void)!write(fd, "x", 1);
        close(fd); /* IN_MODIFY + IN_CLOSE_WRITE */
    }

    int ok = 1;
    for (int i = 0; i < 2; i++)
        ok &= wait_child(kids[i], WATCHDOG_SECS + 3,
                         i == 0 ? "inotify-child0" : "inotify-child1") == 0;
    unlink(path);
    report("inotify-multi-watcher", ok, "deadlock-or-error");
}

/* ── test 4: poll() parked ON an epoll fd (KWin libinput-thread shape) ─ */
static void test_epoll_in_poll(void)
{
    int fds[2];
    if (pipe(fds) < 0) {
        report("epoll-in-poll", 0, "pipe");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(fds[1]);
        alarm(WATCHDOG_SECS);
        int epfd = epoll_create1(0);
        if (epfd < 0)
            _exit(2);
        struct epoll_event ev = {.events = EPOLLIN, .data.fd = fds[0]};
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[0], &ev) < 0)
            _exit(3);
        /* the frozen desktop shape: blocking poll ON the epoll fd */
        struct pollfd p = {.fd = epfd, .events = POLLIN};
        if (poll(&p, 1, -1) < 0)
            _exit(4);
        struct epoll_event out[4];
        if (epoll_wait(epfd, out, 4, 0) < 1)
            _exit(5);
        char c;
        if (read(fds[0], &c, 1) != 1)
            _exit(6);
        _exit(0);
    }

    close(fds[0]);
    struct timespec ts = {0, 300 * 1000 * 1000};
    nanosleep(&ts, NULL); /* let the child park in poll(epfd) first */
    if (write(fds[1], "x", 1) != 1) {
        report("epoll-in-poll", 0, "write");
        kill(pid, SIGKILL);
        return;
    }
    int ok = wait_child(pid, WATCHDOG_SECS + 3, "epoll-in-poll") == 0;
    close(fds[1]);
    report("epoll-in-poll", ok, "deadlock-or-error");
}

/* ── test 5: cross-process eventfd wakeup via poll(-1) ───────────────── */
static void test_eventfd_cross_process(void)
{
    int efd = eventfd(0, 0);
    if (efd < 0) {
        report("eventfd-cross-process", 0, "eventfd");
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        alarm(WATCHDOG_SECS);
        struct pollfd p = {.fd = efd, .events = POLLIN};
        if (poll(&p, 1, -1) < 0)
            _exit(2);
        uint64_t v = 0;
        if (read(efd, &v, sizeof(v)) != sizeof(v) || v == 0)
            _exit(3);
        _exit(0);
    }

    struct timespec ts = {0, 300 * 1000 * 1000};
    nanosleep(&ts, NULL); /* let the child park in poll(efd) first */
    uint64_t one = 1;
    if (write(efd, &one, sizeof(one)) != sizeof(one)) {
        report("eventfd-cross-process", 0, "write");
        kill(pid, SIGKILL);
        return;
    }
    int ok = wait_child(pid, WATCHDOG_SECS + 3, "eventfd-cross") == 0;
    close(efd);
    report("eventfd-cross-process", ok, "deadlock-or-error");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("POLL-NOTIFY-PROBE: start\n");
    test_pipe_blocking_write();
    test_timerfd_repeat();
    test_inotify_multi_watcher();
    test_epoll_in_poll();
    test_eventfd_cross_process();
    printf("POLL-NOTIFY-PROBE: RESULT=%s failures=%d\n",
           g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
