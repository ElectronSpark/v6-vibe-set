/*
 * qprocess-sigchld-pipe-reducer: small QProcess-like SIGCHLD + exec-sync
 * pipe poll reducer.
 *
 * Shape:
 *  - the process under test creates several pipe read fds and arms SIGCHLD;
 *  - a helper child exits first, interrupting poll(2);
 *  - a writer child writes to the exec-sync pipe shortly after and stays
 *    alive long enough that the second poll should report pipe readiness
 *    instead of being interrupted by the writer's own SIGCHLD.
 *
 * The output is intentionally verbose enough to compare xv6 and Linux runs.
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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TEST_FDS 4
#define DEFAULT_NFDS 4
#define DEFAULT_TIMEOUT_MS 30000
#define DEFAULT_HELPER_DELAY_MS 100
#define DEFAULT_WRITER_DELAY_MS 140
#define DEFAULT_WRITER_EXIT_DELAY_MS 250

static volatile sig_atomic_t sigchld_seen;
static volatile sig_atomic_t sigchld_last_pid;
static volatile sig_atomic_t sigchld_last_code;

static uint64_t now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void sleep_ms(int ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
        ;
}

static long elapsed_ms(uint64_t start_us, uint64_t end_us)
{
    if (end_us < start_us)
        return 0;
    return (long)((end_us - start_us + 999) / 1000);
}

static void sigchld_handler(int sig, siginfo_t *si, void *ctx)
{
    (void)sig;
    (void)ctx;

    sigchld_seen++;
    if (si != NULL) {
        sigchld_last_pid = si->si_pid;
        sigchld_last_code = si->si_code;
    }
}

static int parse_int_arg(const char *name, const char *value, int min, int max)
{
    char *end = NULL;
    long v;

    errno = 0;
    v = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || v < min || v > max) {
        fprintf(stderr,
                "QPROCESS-SIGCHLD-PIPE-REDUCER: bad %s=%s range=%d..%d\n",
                name, value, min, max);
        exit(2);
    }
    return (int)v;
}

static void print_pollfds(const char *tag, struct pollfd *pfds, int nfds)
{
    printf("QPROCESS-SIGCHLD-PIPE-REDUCER: %s", tag);
    for (int i = 0; i < nfds; i++) {
        printf(" fd%d=%d events=0x%x revents=0x%x",
               i, pfds[i].fd, pfds[i].events, pfds[i].revents);
    }
    printf("\n");
}

static void reap_nonblocking(const char *tag)
{
    int status;
    pid_t pid;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: %s reaped_pid=%ld "
               "status=0x%x exited=%d code=%d signaled=%d sig=%d\n",
               tag, (long)pid, status, WIFEXITED(status),
               WIFEXITED(status) ? WEXITSTATUS(status) : -1,
               WIFSIGNALED(status),
               WIFSIGNALED(status) ? WTERMSIG(status) : -1);
    }
}

static int wait_for_pid_code(pid_t pid, const char *tag, int expected_code)
{
    int status;

    for (;;) {
        pid_t got = waitpid(pid, &status, 0);

        if (got == pid)
            break;
        if (got < 0 && errno == EINTR)
            continue;
        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: %s waitpid_errno=%d %s\n",
               tag, errno, strerror(errno));
        return 0;
    }

    printf("QPROCESS-SIGCHLD-PIPE-REDUCER: %s pid=%ld status=0x%x "
           "exited=%d code=%d signaled=%d sig=%d\n",
           tag, (long)pid, status, WIFEXITED(status),
           WIFEXITED(status) ? WEXITSTATUS(status) : -1,
           WIFSIGNALED(status),
           WIFSIGNALED(status) ? WTERMSIG(status) : -1);
    return WIFEXITED(status) && WEXITSTATUS(status) == expected_code;
}

static int run_reducer(int nfds, int timeout_ms, int helper_delay_ms,
                       int writer_delay_ms, int writer_exit_delay_ms)
{
    int pipes[MAX_TEST_FDS][2];
    struct pollfd pfds[MAX_TEST_FDS];
    struct sigaction sa;
    pid_t helper;
    pid_t writer;
    uint64_t start_us;
    uint64_t before_first_us;
    uint64_t after_first_us;
    uint64_t before_second_us;
    uint64_t after_second_us;
    int first_ret;
    int first_errno = 0;
    int second_ret;
    int second_errno = 0;
    int remaining_ms;
    int pass;
    char byte = 0;

    memset(pipes, -1, sizeof(pipes));
    memset(pfds, 0, sizeof(pfds));

    for (int i = 0; i < nfds; i++) {
        if (pipe(pipes[i]) < 0) {
            printf("QPROCESS-SIGCHLD-PIPE-REDUCER: pipe index=%d errno=%d %s\n",
                   i, errno, strerror(errno));
            return 2;
        }
        pfds[i].fd = pipes[i][0];
        pfds[i].events = POLLIN | POLLRDNORM;
        pfds[i].revents = 0;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: sigaction errno=%d %s\n",
               errno, strerror(errno));
        return 2;
    }

    start_us = now_us();
    helper = fork();
    if (helper == 0) {
        for (int i = 0; i < nfds; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        sleep_ms(helper_delay_ms);
        _exit(17);
    }
    if (helper < 0) {
        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: helper_fork errno=%d %s\n",
               errno, strerror(errno));
        return 2;
    }

    writer = fork();
    if (writer == 0) {
        for (int i = 0; i < nfds; i++)
            close(pipes[i][0]);
        sleep_ms(writer_delay_ms);
        if (write(pipes[nfds - 1][1], "X", 1) != 1)
            _exit(31);
        sleep_ms(writer_exit_delay_ms);
        for (int i = 0; i < nfds; i++)
            close(pipes[i][1]);
        _exit(0);
    }
    if (writer < 0) {
        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: writer_fork errno=%d %s\n",
               errno, strerror(errno));
        kill(helper, SIGKILL);
        return 2;
    }

    for (int i = 0; i < nfds; i++)
        close(pipes[i][1]);

    printf("QPROCESS-SIGCHLD-PIPE-REDUCER: start nfds=%d timeout_ms=%d "
           "helper_delay_ms=%d writer_delay_ms=%d writer_exit_delay_ms=%d "
           "helper_pid=%ld writer_pid=%ld exec_sync_index=%d exec_sync_fd=%d\n",
           nfds, timeout_ms, helper_delay_ms, writer_delay_ms,
           writer_exit_delay_ms, (long)helper, (long)writer, nfds - 1,
           pfds[nfds - 1].fd);
    print_pollfds("before_first_poll", pfds, nfds);

    before_first_us = now_us();
    first_ret = poll(pfds, (nfds_t)nfds, timeout_ms);
    after_first_us = now_us();
    if (first_ret < 0)
        first_errno = errno;

    printf("QPROCESS-SIGCHLD-PIPE-REDUCER: first_poll ret=%d errno=%d %s "
           "elapsed_ms=%ld since_start_ms=%ld sigchld_seen=%d "
           "sigchld_last_pid=%d sigchld_last_code=%d\n",
           first_ret, first_errno,
           first_ret < 0 ? strerror(first_errno) : "ok",
           elapsed_ms(before_first_us, after_first_us),
           elapsed_ms(start_us, after_first_us), (int)sigchld_seen,
           (int)sigchld_last_pid, (int)sigchld_last_code);
    print_pollfds("after_first_poll", pfds, nfds);

    reap_nonblocking("after_first_poll");

    remaining_ms = timeout_ms - (int)elapsed_ms(before_first_us, after_first_us);
    if (remaining_ms < 0)
        remaining_ms = 0;
    for (int i = 0; i < nfds; i++)
        pfds[i].revents = 0;

    printf("QPROCESS-SIGCHLD-PIPE-REDUCER: recompute original_timeout_ms=%d "
           "remaining_ms=%d\n", timeout_ms, remaining_ms);
    before_second_us = now_us();
    second_ret = poll(pfds, (nfds_t)nfds, remaining_ms);
    after_second_us = now_us();
    if (second_ret < 0)
        second_errno = errno;

    printf("QPROCESS-SIGCHLD-PIPE-REDUCER: second_poll ret=%d errno=%d %s "
           "elapsed_ms=%ld since_start_ms=%ld sigchld_seen=%d\n",
           second_ret, second_errno,
           second_ret < 0 ? strerror(second_errno) : "ok",
           elapsed_ms(before_second_us, after_second_us),
           elapsed_ms(start_us, after_second_us), (int)sigchld_seen);
    print_pollfds("after_second_poll", pfds, nfds);

    if (second_ret > 0 && (pfds[nfds - 1].revents & POLLIN)) {
        ssize_t n = read(pfds[nfds - 1].fd, &byte, 1);

        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: exec_sync_read n=%ld "
               "errno=%d byte=0x%x since_start_ms=%ld\n",
               (long)n, n < 0 ? errno : 0, (unsigned char)byte,
               elapsed_ms(start_us, now_us()));
    }

    pass = first_ret < 0 && first_errno == EINTR &&
           sigchld_seen > 0 &&
           second_ret > 0 &&
           (pfds[nfds - 1].revents & POLLIN);

    pass &= wait_for_pid_code(writer, "writer_wait", 0);
    /* The helper may already have been reaped by reap_nonblocking(). */
    int helper_status = 0;
    pid_t helper_wait = waitpid(helper, &helper_status, WNOHANG);
    if (helper_wait == helper) {
        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: helper_wait pid=%ld "
               "status=0x%x exited=%d code=%d signaled=%d sig=%d\n",
               (long)helper, helper_status, WIFEXITED(helper_status),
               WIFEXITED(helper_status) ? WEXITSTATUS(helper_status) : -1,
               WIFSIGNALED(helper_status),
               WIFSIGNALED(helper_status) ? WTERMSIG(helper_status) : -1);
        pass &= WIFEXITED(helper_status) && WEXITSTATUS(helper_status) == 17;
    } else if (helper_wait < 0 && errno == ECHILD) {
        printf("QPROCESS-SIGCHLD-PIPE-REDUCER: helper_wait already_reaped=1\n");
    } else {
        pass &= wait_for_pid_code(helper, "helper_wait", 17);
    }

    for (int i = 0; i < nfds; i++)
        close(pfds[i].fd);

    printf("QPROCESS-SIGCHLD-PIPE-REDUCER: RESULT=%s "
           "shape=%s first_elapsed_ms=%ld second_elapsed_ms=%ld total_ms=%ld\n",
           pass ? "PASS" : "FAIL",
           pass ? "EINTR_THEN_PIPE_READABLE" : "UNEXPECTED",
           elapsed_ms(before_first_us, after_first_us),
           elapsed_ms(before_second_us, after_second_us),
           elapsed_ms(start_us, now_us()));
    return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
    int nfds = DEFAULT_NFDS;
    int timeout_ms = DEFAULT_TIMEOUT_MS;
    int helper_delay_ms = DEFAULT_HELPER_DELAY_MS;
    int writer_delay_ms = DEFAULT_WRITER_DELAY_MS;
    int writer_exit_delay_ms = DEFAULT_WRITER_EXIT_DELAY_MS;

    setvbuf(stdout, NULL, _IONBF, 0);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--nfds") == 0 && i + 1 < argc) {
            nfds = parse_int_arg("--nfds", argv[++i], 1, MAX_TEST_FDS);
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            timeout_ms = parse_int_arg("--timeout-ms", argv[++i], 1, 60000);
        } else if (strcmp(argv[i], "--helper-ms") == 0 && i + 1 < argc) {
            helper_delay_ms = parse_int_arg("--helper-ms", argv[++i], 0, 10000);
        } else if (strcmp(argv[i], "--writer-ms") == 0 && i + 1 < argc) {
            writer_delay_ms = parse_int_arg("--writer-ms", argv[++i], 0, 10000);
        } else if (strcmp(argv[i], "--writer-exit-ms") == 0 && i + 1 < argc) {
            writer_exit_delay_ms = parse_int_arg("--writer-exit-ms",
                                                 argv[++i], 0, 10000);
        } else {
            fprintf(stderr,
                    "usage: %s [--nfds 1..4] [--timeout-ms N] "
                    "[--helper-ms N] [--writer-ms N] "
                    "[--writer-exit-ms N]\n", argv[0]);
            return 2;
        }
    }

    return run_reducer(nfds, timeout_ms, helper_delay_ms,
                       writer_delay_ms, writer_exit_delay_ms);
}
