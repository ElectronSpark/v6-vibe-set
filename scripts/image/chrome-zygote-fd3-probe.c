#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x4000
#endif

static long now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void set_cloexec(int fd, int enabled)
{
    int flags = fcntl(fd, F_GETFD, 0);

    if (flags < 0)
        return;
    if (enabled)
        flags |= FD_CLOEXEC;
    else
        flags &= ~FD_CLOEXEC;
    (void)fcntl(fd, F_SETFD, flags);
}

static int make_seqpacket_pair(int sv[2])
{
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) == 0)
        return 0;
    if (errno != EINVAL && errno != EPROTONOSUPPORT)
        return -1;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) != 0)
        return -1;
    set_cloexec(sv[0], 1);
    set_cloexec(sv[1], 1);
    return 0;
}

static int wait_child_nonblock(pid_t pid, int *status)
{
    pid_t ret;

    errno = 0;
    ret = waitpid(pid, status, WNOHANG);
    if (ret == pid)
        return 1;
    if (ret == 0)
        return 0;
    return -1;
}

static int run_variant(const char *variant, const char *chrome, int timeout_ms)
{
    static const unsigned char bootstrap[8] = {
        0x04, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00
    };
    unsigned char ack[4] = {0};
    struct iovec iov;
    struct msghdr msg;
    struct pollfd pfd;
    int sv[2] = {-1, -1};
    int passcred = 1;
    int status = 0;
    long start;
    pid_t pid;
    int failed = 0;

    if (make_seqpacket_pair(sv) != 0) {
        printf("chrome_zygote_fd3_probe variant=%s phase=socketpair ret=-1 "
               "errno=%d %s result=FAIL\n",
               variant, errno, strerror(errno));
        return 1;
    }

    if (setsockopt(sv[0], SOL_SOCKET, SO_PASSCRED, &passcred,
                   sizeof(passcred)) != 0) {
        printf("chrome_zygote_fd3_probe variant=%s phase=setsockopt-passcred "
               "ret=-1 errno=%d %s result=FAIL\n",
               variant, errno, strerror(errno));
        close(sv[0]);
        close(sv[1]);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        printf("chrome_zygote_fd3_probe variant=%s phase=fork ret=-1 errno=%d "
               "%s result=FAIL\n",
               variant, errno, strerror(errno));
        close(sv[0]);
        close(sv[1]);
        return 1;
    }

    if (pid == 0) {
        char *argv_a[] = {
            (char *)chrome,
            "--type=zygote",
            "--no-zygote-sandbox",
            "--disable-seccomp-filter-sandbox",
            "--no-sandbox",
            "--enable-logging=stderr",
            "--crashpad-handler-pid=0",
            "--enable-crash-reporter=,",
            "--user-data-dir=/tmp/wayland-chromium-profile",
            "--change-stack-guard-on-fork=enable",
            NULL,
        };
        char *argv_b[] = {
            (char *)chrome,
            "--type=zygote",
            "--disable-seccomp-filter-sandbox",
            "--no-sandbox",
            "--enable-logging=stderr",
            "--crashpad-handler-pid=0",
            "--enable-crash-reporter=,",
            "--user-data-dir=/tmp/wayland-chromium-profile",
            "--change-stack-guard-on-fork=enable",
            NULL,
        };
        char **argv = strcmp(variant, "zygote-no-sandbox") == 0 ?
            argv_a : argv_b;

        close(sv[0]);
        if (sv[1] != 3) {
            if (dup2(sv[1], 3) < 0) {
                printf("chrome_zygote_fd3_probe child variant=%s phase=dup2 "
                       "ret=-1 errno=%d %s\n",
                       variant, errno, strerror(errno));
                _exit(111);
            }
            close(sv[1]);
        }
        set_cloexec(3, 0);
        setenv("LD_LIBRARY_PATH",
               "/opt/host-gui/wayland-chromium/lib:"
               "/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:"
               "/usr/lib:/lib",
               1);
        setenv("CHROME_HEADLESS", "1", 0);
        execv(chrome, argv);
        printf("chrome_zygote_fd3_probe child variant=%s phase=execv ret=-1 "
               "errno=%d %s chrome=%s\n",
               variant, errno, strerror(errno), chrome);
        _exit(112);
    }

    close(sv[1]);
    memset(&msg, 0, sizeof(msg));
    memset(&iov, 0, sizeof(iov));
    iov.iov_base = (void *)bootstrap;
    iov.iov_len = sizeof(bootstrap);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    errno = 0;
    ssize_t sent = sendmsg(sv[0], &msg, MSG_NOSIGNAL);
    printf("chrome_zygote_fd3_probe variant=%s phase=send-bootstrap ret=%zd "
           "errno=%d %s hex=0400000003000000 child=%d\n",
           variant, sent, errno, strerror(errno), (int)pid);
    if (sent != (ssize_t)sizeof(bootstrap))
        failed = 1;

    start = now_ms();
    while (!failed) {
        long elapsed = now_ms() - start;
        int child_state;

        child_state = wait_child_nonblock(pid, &status);
        if (child_state == 1) {
            printf("chrome_zygote_fd3_probe variant=%s phase=child-exit "
                   "status=0x%x exited=%d code=%d signaled=%d signal=%d\n",
                   variant, status, WIFEXITED(status) ? 1 : 0,
                   WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                   WIFSIGNALED(status) ? 1 : 0,
                   WIFSIGNALED(status) ? WTERMSIG(status) : -1);
            failed = 1;
            break;
        } else if (child_state < 0) {
            printf("chrome_zygote_fd3_probe variant=%s phase=waitpid ret=-1 "
                   "errno=%d %s\n",
                   variant, errno, strerror(errno));
            failed = 1;
            break;
        }

        if (elapsed >= timeout_ms) {
            printf("chrome_zygote_fd3_probe variant=%s phase=ack-timeout "
                   "elapsed_ms=%ld child=%d\n",
                   variant, elapsed, (int)pid);
            failed = 1;
            break;
        }

        pfd.fd = sv[0];
        pfd.events = POLLIN | POLLHUP | POLLERR;
        pfd.revents = 0;
        errno = 0;
        int pret = poll(&pfd, 1, 100);
        if (pret < 0) {
            if (errno == EINTR)
                continue;
            printf("chrome_zygote_fd3_probe variant=%s phase=poll ret=-1 "
                   "errno=%d %s\n",
                   variant, errno, strerror(errno));
            failed = 1;
            break;
        }
        if (pret == 0)
            continue;
        if (pfd.revents & POLLIN) {
            errno = 0;
            ssize_t got = read(sv[0], ack, sizeof(ack));
            printf("chrome_zygote_fd3_probe variant=%s phase=read-ack ret=%zd "
                   "errno=%d %s bytes=%02x%02x%02x%02x elapsed_ms=%ld\n",
                   variant, got, errno, strerror(errno), ack[0], ack[1],
                   ack[2], ack[3], now_ms() - start);
            if (got == (ssize_t)sizeof(ack))
                break;
            failed = 1;
            break;
        }
        if (pfd.revents & (POLLHUP | POLLERR)) {
            printf("chrome_zygote_fd3_probe variant=%s phase=poll-hup-err "
                   "revents=0x%x elapsed_ms=%ld\n",
                   variant, pfd.revents, now_ms() - start);
            failed = 1;
            break;
        }
    }

    if (failed) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    } else {
        kill(pid, SIGTERM);
        for (int i = 0; i < 20; i++) {
            if (wait_child_nonblock(pid, &status) == 1)
                break;
            usleep(50000);
        }
        if (wait_child_nonblock(pid, &status) == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }

    printf("chrome_zygote_fd3_probe variant=%s child-final-status=0x%x "
           "result=%s\n",
           variant, status, failed ? "FAIL" : "PASS");
    close(sv[0]);
    return failed;
}

int main(int argc, char **argv)
{
    const char *chrome =
        "/opt/host-gui/wayland-chromium/chrome-linux64/chrome";
    int timeout_ms = 60000;
    int failed = 0;

    if (argc > 1 && argv[1][0] != '\0')
        chrome = argv[1];
    if (argc > 2)
        timeout_ms = atoi(argv[2]);
    if (timeout_ms <= 0)
        timeout_ms = 60000;

    printf("chrome_zygote_fd3_probe start chrome=%s timeout_ms=%d\n",
           chrome, timeout_ms);
    failed |= run_variant("zygote-no-sandbox", chrome, timeout_ms);
    failed |= run_variant("zygote-plain", chrome, timeout_ms);
    printf("chrome_zygote_fd3_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
