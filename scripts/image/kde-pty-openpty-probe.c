#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int read_until_token(int fd, const char *token, const char *phase)
{
    char buf[512];
    size_t used = 0;

    for (int i = 0; i < 40; i++) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLRDNORM };
        int pret = poll(&pfd, 1, 250);

        if (pret < 0 && errno == EINTR)
            continue;
        if (pret <= 0)
            continue;
        if (!(pfd.revents & (POLLIN | POLLRDNORM))) {
            printf("kde_pty_openpty_probe %s poll revents=0x%x\n",
                   phase, pfd.revents);
            return 1;
        }

        ssize_t n = read(fd, buf + used, sizeof(buf) - used - 1);
        if (n <= 0) {
            printf("kde_pty_openpty_probe %s read ret=%zd errno=%d %s\n",
                   phase, n, errno, strerror(errno));
            return 1;
        }
        used += (size_t)n;
        buf[used] = '\0';
        printf("kde_pty_openpty_probe %s chunk=%s", phase, buf);
        if (strstr(buf, token) != NULL)
            return 0;
        if (used >= sizeof(buf) - 1)
            used = 0;
    }

    buf[used] = '\0';
    printf("kde_pty_openpty_probe %s timeout token=%s partial=%s\n",
           phase, token, used ? buf : "(empty)");
    return 1;
}

static int epoll_expect_readable(int fd, const char *phase)
{
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ev;
    struct epoll_event out;
    int ret;

    if (epfd < 0) {
        printf("kde_pty_openpty_probe %s epoll_create1 errno=%d %s\n",
               phase, errno, strerror(errno));
        return 1;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLRDNORM | EPOLLET;
    ev.data.u64 = 0x6f70656e707479ULL;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0) {
        printf("kde_pty_openpty_probe %s epoll_ctl errno=%d %s\n",
               phase, errno, strerror(errno));
        close(epfd);
        return 1;
    }
    memset(&out, 0, sizeof(out));
    ret = epoll_wait(epfd, &out, 1, 1000);
    printf("kde_pty_openpty_probe %s epoll_wait ret=%d errno=%d %s "
           "events=0x%x data=0x%llx\n",
           phase, ret, errno, strerror(errno), out.events,
           (unsigned long long)out.data.u64);
    close(epfd);
    if (ret <= 0)
        return 1;
    if (!(out.events & (EPOLLIN | EPOLLRDNORM)))
        return 1;
    if (out.data.u64 != 0x6f70656e707479ULL)
        return 1;
    return 0;
}

int main(void)
{
    int master = -1;
    int slave = -1;
    int failed = 0;
    int status = 0;
    char name[128] = "";
    struct winsize ws = {
        .ws_row = 30,
        .ws_col = 100,
        .ws_xpixel = 800,
        .ws_ypixel = 480,
    };
    struct termios tio;

    memset(&tio, 0, sizeof(tio));
    tio.c_iflag = ICRNL | IXON;
    tio.c_oflag = OPOST | ONLCR;
    tio.c_cflag = CS8 | CREAD | CLOCAL;
    tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK;
    tio.c_cc[VINTR] = 0x03;
    tio.c_cc[VQUIT] = 0x1c;
    tio.c_cc[VERASE] = 0x7f;
    tio.c_cc[VKILL] = 0x15;
    tio.c_cc[VEOF] = 0x04;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VSTART] = 0x11;
    tio.c_cc[VSTOP] = 0x13;
    tio.c_cc[VSUSP] = 0x1a;

    if (openpty(&master, &slave, name, &tio, &ws) < 0) {
        printf("kde_pty_openpty_probe openpty errno=%d %s\n",
               errno, strerror(errno));
        return 2;
    }

    struct stat st;
    struct stat path_st;
    int sret = fstat(slave, &st);
    int pret = stat(name, &path_st);
    char *tty = ttyname(slave);
    char procfd[64];
    char linkbuf[256];
    ssize_t linklen;

    snprintf(procfd, sizeof(procfd), "/proc/self/fd/%d", slave);
    linklen = readlink(procfd, linkbuf, sizeof(linkbuf) - 1);
    if (linklen >= 0)
        linkbuf[linklen] = '\0';
    else
        linkbuf[0] = '\0';

    printf("kde_pty_openpty_probe master=%d slave=%d name=%s ttyname=%s "
           "isatty=%d fstat_ret=%d mode=0%o dev=%llu rdev=%llu ino=%llu "
           "path_stat_ret=%d path_mode=0%o path_dev=%llu path_rdev=%llu "
           "path_ino=%llu fdlink=%s link_ret=%zd errno=%d %s\n",
           master, slave, name, tty ? tty : "(null)", isatty(slave), sret,
           sret == 0 ? (unsigned)st.st_mode : 0,
           sret == 0 ? (unsigned long long)st.st_dev : 0,
           sret == 0 ? (unsigned long long)st.st_rdev : 0,
           sret == 0 ? (unsigned long long)st.st_ino : 0,
           pret, pret == 0 ? (unsigned)path_st.st_mode : 0,
           pret == 0 ? (unsigned long long)path_st.st_dev : 0,
           pret == 0 ? (unsigned long long)path_st.st_rdev : 0,
           pret == 0 ? (unsigned long long)path_st.st_ino : 0,
           linklen >= 0 ? linkbuf : "(error)", linklen, errno, strerror(errno));

    struct winsize got_ws;
    if (ioctl(slave, TIOCGWINSZ, &got_ws) < 0) {
        printf("kde_pty_openpty_probe tiocgwinsz errno=%d %s\n",
               errno, strerror(errno));
        failed = 1;
    } else {
        printf("kde_pty_openpty_probe winsize rows=%u cols=%u xp=%u yp=%u\n",
               got_ws.ws_row, got_ws.ws_col, got_ws.ws_xpixel,
               got_ws.ws_ypixel);
        if (got_ws.ws_row != ws.ws_row || got_ws.ws_col != ws.ws_col)
            failed = 1;
    }

    struct termios got_tio;
    if (tcgetattr(slave, &got_tio) < 0) {
        printf("kde_pty_openpty_probe tcgetattr errno=%d %s\n",
               errno, strerror(errno));
        failed = 1;
    } else {
        printf("kde_pty_openpty_probe termios iflag=0x%x oflag=0x%x "
               "cflag=0x%x lflag=0x%x vmin=%u vtime=%u vstart=%u "
               "vstop=%u vsusp=%u\n",
               got_tio.c_iflag, got_tio.c_oflag, got_tio.c_cflag,
               got_tio.c_lflag, got_tio.c_cc[VMIN], got_tio.c_cc[VTIME],
               got_tio.c_cc[VSTART], got_tio.c_cc[VSTOP],
               got_tio.c_cc[VSUSP]);
        if (got_tio.c_cc[VMIN] != 1 || got_tio.c_cc[VSTART] != 0x11 ||
            got_tio.c_cc[VSTOP] != 0x13 || got_tio.c_cc[VSUSP] != 0x1a)
            failed = 1;
    }

    if (write(slave, "xv6-openpty-slave-ready\n", 24) != 24) {
        printf("kde_pty_openpty_probe slave-write errno=%d %s\n",
               errno, strerror(errno));
        failed = 1;
    }
    if (!failed)
        failed = epoll_expect_readable(master, "slave-write");
    if (!failed)
        failed = read_until_token(master, "xv6-openpty-slave-ready",
                                  "slave-readback");

    pid_t pid = fork();
    if (pid < 0) {
        printf("kde_pty_openpty_probe fork errno=%d %s\n",
               errno, strerror(errno));
        failed = 1;
        goto out;
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
        execl("/bin/sh", "sh", "-lc", "echo xv6-openpty-child-ready", NULL);
        _exit(127);
    }

    close(slave);
    slave = -1;
    if (!failed)
        failed = read_until_token(master, "xv6-openpty-child-ready",
                                  "child-readback");

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
    printf("kde_pty_openpty_probe child_status=0x%x exited=%d code=%d\n",
           status, WIFEXITED(status),
           WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        failed = 1;

out:
    if (slave >= 0)
        close(slave);
    if (master >= 0)
        close(master);
    printf("kde_pty_openpty_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 2 : 0;
}
