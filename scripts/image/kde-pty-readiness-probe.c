#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef TIOCGPTPEER
#define TIOCGPTPEER 0x5441
#endif

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int open_probe_pty(int *master_out, int *slave_out, const char *phase)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    int slave;

    if (master < 0) {
        printf("kde_pty_readiness_probe %s posix_openpt errno=%d %s\n",
               phase, errno, strerror(errno));
        return 1;
    }
    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        printf("kde_pty_readiness_probe %s grant_unlock errno=%d %s\n",
               phase, errno, strerror(errno));
        close(master);
        return 1;
    }
    slave = ioctl(master, TIOCGPTPEER, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) {
        printf("kde_pty_readiness_probe %s tiocgptpeer errno=%d %s\n",
               phase, errno, strerror(errno));
        close(master);
        return 1;
    }
    if (set_nonblock(master) < 0) {
        printf("kde_pty_readiness_probe %s set_nonblock errno=%d %s\n",
               phase, errno, strerror(errno));
        close(slave);
        close(master);
        return 1;
    }
    printf("kde_pty_readiness_probe %s master=%d slave=%d\n",
           phase, master, slave);
    *master_out = master;
    *slave_out = slave;
    return 0;
}

static int expect_poll_readable(int fd, const char *phase)
{
    struct pollfd pfds[3] = {
        { .fd = -1, .events = POLLIN },
        { .fd = fd, .events = POLLIN | POLLRDNORM | POLLRDBAND | POLLRDHUP },
        { .fd = -1, .events = POLLIN },
    };
    int ret = poll(pfds, 3, 1000);

    printf("kde_pty_readiness_probe %s poll ret=%d errno=%d %s "
           "revents=0x%x\n",
           phase, ret, errno, strerror(errno), pfds[1].revents);
    if (ret <= 0)
        return 1;
    if (!(pfds[1].revents & (POLLIN | POLLRDNORM | POLLRDBAND | POLLHUP)))
        return 1;
    return 0;
}

static int expect_epoll_readable(int fd, uint32_t flags, const char *phase)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ev;
    struct epoll_event out;
    int ret;

    if (epfd < 0) {
        printf("kde_pty_readiness_probe %s epoll_create1 errno=%d %s\n",
               phase, errno, strerror(errno));
        return 1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLRDNORM | EPOLLRDBAND | EPOLLRDHUP | flags;
    ev.data.u64 = 0x7074796d61737465ULL;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0) {
        printf("kde_pty_readiness_probe %s epoll_ctl errno=%d %s\n",
               phase, errno, strerror(errno));
        close(epfd);
        return 1;
    }

    memset(&out, 0, sizeof(out));
    ret = epoll_wait(epfd, &out, 1, 1000);
    printf("kde_pty_readiness_probe %s epoll_wait ret=%d errno=%d %s "
           "events=0x%x data=0x%llx\n",
           phase, ret, errno, strerror(errno), out.events,
           (unsigned long long)out.data.u64);
    close(epfd);
    if (ret <= 0)
        return 1;
    if (!(out.events & (EPOLLIN | EPOLLRDNORM | EPOLLRDBAND | EPOLLHUP)))
        return 1;
    if (out.data.u64 != 0x7074796d61737465ULL)
        return 1;
    return 0;
}

static int read_token(int fd, const char *token, const char *phase)
{
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);

    if (n < 0) {
        printf("kde_pty_readiness_probe %s read ret=%zd errno=%d %s\n",
               phase, n, errno, strerror(errno));
        return 1;
    }
    buf[n] = '\0';
    printf("kde_pty_readiness_probe %s read=%s", phase, buf);
    if (strstr(buf, token) == NULL)
        return 1;
    return 0;
}

static void dump_bytes(const char *phase, const char *buf, size_t len)
{
    printf("kde_pty_readiness_probe %s hex=", phase);
    for (size_t i = 0; i < len; i++)
        printf("%02x", (unsigned char)buf[i]);
    printf("\n");
}

static int write_all(int fd, const char *text, const char *phase)
{
    ssize_t n = write(fd, text, strlen(text));

    printf("kde_pty_readiness_probe %s write ret=%zd errno=%d %s\n",
           phase, n, errno, strerror(errno));
    return n == (ssize_t)strlen(text) ? 0 : 1;
}

static int slave_read_case(int master, int slave)
{
    pid_t pid = fork();
    int status = 0;
    int failed = 0;

    if (pid < 0) {
        printf("kde_pty_readiness_probe slave-read fork errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    if (pid == 0) {
        char buf[128];
        ssize_t n;

        if (setsid() < 0)
            _exit(120);
        if (ioctl(slave, TIOCSCTTY, 0) < 0)
            _exit(121);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO)
            close(slave);
        close(master);

        n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0)
            _exit(122);
        if (write(STDOUT_FILENO, "xv6-slave-read-got:", 19) != 19)
            _exit(123);
        if (write(STDOUT_FILENO, buf, (size_t)n) != n)
            _exit(124);
        _exit(0);
    }

    failed = write_all(master, "xv6-slave-read-input\n",
                       "slave-read-master-write");
    if (!failed)
        failed = expect_poll_readable(master, "slave-read-master-poll");
    if (!failed)
        failed = read_token(master, "xv6-slave-read-got:xv6-slave-read-input",
                            "slave-read-master-read");

    for (int i = 0; i < 20; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            break;
        usleep(100000);
    }
    if (waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        failed = 1;
    }

    printf("kde_pty_readiness_probe slave_read_status=0x%x exited=%d code=%d\n",
           status, WIFEXITED(status),
           WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        failed = 1;
    return failed;
}

static int child_shell_case(int master, int slave)
{
    pid_t pid = fork();
    int status = 0;
    int failed;
    char buf[512];
    size_t used = 0;

    if (pid < 0) {
        printf("kde_pty_readiness_probe fork errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    if (pid == 0) {
        if (setsid() < 0)
            _exit(120);
        if (ioctl(slave, TIOCSCTTY, 0) < 0)
            _exit(121);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO)
            close(slave);
        close(master);
        ssize_t child_write = write(STDOUT_FILENO,
                                    "xv6-pty-child-before-exec\n", 26);
        (void)child_write;
        execl("/bin/sh", "sh", "-lc", "echo xv6-pty-child-after-exec", NULL);
        _exit(127);
    }

    failed = expect_poll_readable(master, "child-poll");
    for (int i = 0; !failed && i < 10 &&
                    strstr(buf, "xv6-pty-child-after-exec") == NULL; i++) {
        ssize_t n = read(master, buf + used, sizeof(buf) - used - 1);

        if (n < 0) {
            printf("kde_pty_readiness_probe child-read ret=%zd errno=%d %s\n",
                   n, errno, strerror(errno));
            failed = 1;
            break;
        }
        used += (size_t)n;
        buf[used] = '\0';
        printf("kde_pty_readiness_probe child-read chunk=%s", buf);
        if (strstr(buf, "xv6-pty-child-after-exec"))
            break;
        failed = expect_poll_readable(master, "child-poll-more");
    }
    if (!failed && (strstr(buf, "xv6-pty-child-before-exec") == NULL ||
                    strstr(buf, "xv6-pty-child-after-exec") == NULL))
        failed = 1;
    waitpid(pid, &status, 0);
    printf("kde_pty_readiness_probe child_status=0x%x exited=%d code=%d\n",
           status, WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        failed = 1;
    return failed;
}

static int preloaded_epoll_case(int master, int slave)
{
    int failed;

    failed = write_all(slave, "xv6-pty-preloaded-epoll\n",
                       "preloaded-epoll-write");
    if (!failed)
        failed = expect_epoll_readable(master, 0, "preloaded-epoll");
    if (!failed)
        failed = read_token(master, "xv6-pty-preloaded-epoll",
                            "preloaded-epoll-read");
    return failed;
}

static int preloaded_epollet_case(int master, int slave)
{
    int failed;

    failed = write_all(slave, "xv6-pty-preloaded-epollet\n",
                       "preloaded-epollet-write");
    if (!failed)
        failed = expect_epoll_readable(master, EPOLLET,
                                       "preloaded-epollet");
    if (!failed)
        failed = read_token(master, "xv6-pty-preloaded-epollet",
                            "preloaded-epollet-read");
    return failed;
}

static int interactive_shell_case(int master, int slave)
{
    pid_t pid = fork();
    int status = 0;
    int failed = 0;
    int saw_before = 0;
    int saw_prompt = 0;
    char buf[1024];
    size_t used = 0;

    if (pid < 0) {
        printf("kde_pty_readiness_probe interactive fork errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    if (pid == 0) {
        if (setsid() < 0)
            _exit(120);
        if (ioctl(slave, TIOCSCTTY, 0) < 0)
            _exit(121);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO)
            close(slave);
        close(master);
        execl("/bin/sh", "sh", "-l", NULL);
        _exit(127);
    }

    for (int i = 0; !failed && i < 40 && !saw_prompt; i++) {
        struct pollfd pfd = { .fd = master, .events = POLLIN | POLLRDNORM };
        int ret = poll(&pfd, 1, 250);

        printf("kde_pty_readiness_probe interactive prompt-poll iter=%d ret=%d "
               "errno=%d %s revents=0x%x\n",
               i, ret, errno, strerror(errno), pfd.revents);
        if (ret < 0) {
            failed = 1;
            break;
        }
        if (ret == 0)
            continue;
        ssize_t n = read(master, buf + used, sizeof(buf) - used - 1);
        printf("kde_pty_readiness_probe interactive prompt-read ret=%zd errno=%d %s\n",
               n, errno, strerror(errno));
        if (n < 0) {
            failed = 1;
            break;
        }
        used += (size_t)n;
        buf[used] = '\0';
        dump_bytes("interactive-prompt-read", buf, used);
        if (strstr(buf, "# ") != NULL || strstr(buf, "$ ") != NULL)
            saw_prompt = 1;
    }
    printf("kde_pty_readiness_probe interactive prompt_buffer=%s\n",
           used ? buf : "");
    if (!saw_prompt)
        failed = 1;

    const char command[] = "echo xv6-pty-output-ok\n";
    if (!failed)
        failed = write_all(master, command, "interactive-master-write");
    for (int i = 0; !failed && i < 20 && !saw_before; i++) {
        struct pollfd pfd = { .fd = master, .events = POLLIN | POLLRDNORM };
        int ret = poll(&pfd, 1, 500);

        printf("kde_pty_readiness_probe interactive poll iter=%d ret=%d "
               "errno=%d %s revents=0x%x\n",
               i, ret, errno, strerror(errno), pfd.revents);
        if (ret < 0) {
            failed = 1;
            break;
        }
        if (ret == 0)
            continue;
        ssize_t n = read(master, buf + used, sizeof(buf) - used - 1);
        printf("kde_pty_readiness_probe interactive read ret=%zd errno=%d %s\n",
               n, errno, strerror(errno));
        if (n < 0) {
            failed = 1;
            break;
        }
        if (used + (size_t)n >= sizeof(buf) - 1)
            used = 0;
        used += (size_t)n;
        buf[used] = '\0';
        dump_bytes("interactive-read", buf, used);
        if (strstr(buf, "xv6-pty-output-ok") != NULL) {
            const char *first = strstr(buf, "xv6-pty-output-ok");
            const char *second = strstr(first + 1, "xv6-pty-output-ok");

            if (second != NULL)
                saw_before = 1;
        }
        if (strstr(buf, "xv6-pty-output-ok\r\n") != NULL &&
            strstr(buf, "echo xv6-pty-output-ok") == NULL)
            saw_before = 1;
    }
    printf("kde_pty_readiness_probe interactive buffer=%s\n", used ? buf : "");

    if (!failed && saw_before)
        failed = write_all(master, "exit\n", "interactive-master-exit");

    for (int i = 0; i < 40; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            break;
        usleep(100000);
    }
    if (waitpid(pid, &status, WNOHANG) == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        failed = 1;
    }
    printf("kde_pty_readiness_probe interactive_status=0x%x exited=%d code=%d "
           "saw_token=%d\n",
           status, WIFEXITED(status),
           WIFEXITED(status) ? WEXITSTATUS(status) : -1, saw_before);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !saw_before)
        failed = 1;
    return failed;
}

int main(void)
{
    int master = -1;
    int slave = -1;
    int failed = 0;

    if (open_probe_pty(&master, &slave, "direct-open"))
        return 2;

    failed = write_all(slave, "xv6-pty-direct-poll\n", "direct-poll-write");
    if (!failed)
        failed = expect_poll_readable(master, "direct-poll");
    if (!failed)
        failed = read_token(master, "xv6-pty-direct-poll", "direct-poll-read");

    if (!failed)
        failed = write_all(slave, "xv6-pty-direct-epoll\n", "direct-epoll-write");
    if (!failed)
        failed = expect_epoll_readable(master, 0, "direct-epoll");
    if (!failed)
        failed = read_token(master, "xv6-pty-direct-epoll", "direct-epoll-read");

    if (!failed)
        failed = write_all(slave, "xv6-pty-direct-epollet\n", "direct-epollet-write");
    if (!failed)
        failed = expect_epoll_readable(master, EPOLLET, "direct-epollet");
    if (!failed)
        failed = read_token(master, "xv6-pty-direct-epollet",
                            "direct-epollet-read");

    if (!failed)
        failed = preloaded_epoll_case(master, slave);

    if (!failed)
        failed = preloaded_epollet_case(master, slave);

    close(slave);
    close(master);
    slave = master = -1;

    if (!failed && !open_probe_pty(&master, &slave, "slave-read-open"))
        failed = slave_read_case(master, slave);
    if (slave >= 0)
        close(slave);
    if (master >= 0)
        close(master);
    slave = master = -1;

    if (!failed && !open_probe_pty(&master, &slave, "child-open"))
        failed = child_shell_case(master, slave);
    if (slave >= 0)
        close(slave);
    if (master >= 0)
        close(master);
    slave = master = -1;

    if (!failed && !open_probe_pty(&master, &slave, "interactive-open"))
        failed = interactive_shell_case(master, slave);

    if (slave >= 0)
        close(slave);
    if (master >= 0)
        close(master);
    printf("kde_pty_readiness_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 2 : 0;
}
