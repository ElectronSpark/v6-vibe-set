#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef TIOCGPTPEER
#define TIOCGPTPEER 0x5441
#endif

enum handoff_stamp_id {
    HS_PARENT_START,
    HS_PTMX_OPEN,
    HS_GRANT_UNLOCK_DONE,
    HS_SLAVE_OPEN,
    HS_FORK_BEFORE,
    HS_FORK_PARENT_RETURN,
    HS_CHILD_FIRST,
    HS_CHILD_SETSID,
    HS_CHILD_TIOCSCTTY,
    HS_CHILD_DUP2_DONE,
    HS_CHILD_TCSETPGRP,
    HS_CHILD_BEFORE_EXEC_OR_MARKER,
    HS_PARENT_MARKER_READ,
    HS_PARENT_WAIT_DONE,
    HS_COUNT,
};

struct handoff_stamp {
    int id;
    int rc;
    int err;
    uint64_t us;
};

static const char *handoff_stamp_name(int id)
{
    switch (id) {
    case HS_PARENT_START: return "parent_start";
    case HS_PTMX_OPEN: return "ptmx_open";
    case HS_GRANT_UNLOCK_DONE: return "grant_unlock";
    case HS_SLAVE_OPEN: return "slave_open";
    case HS_FORK_BEFORE: return "fork_before";
    case HS_FORK_PARENT_RETURN: return "fork_parent_return";
    case HS_CHILD_FIRST: return "child_first";
    case HS_CHILD_SETSID: return "setsid";
    case HS_CHILD_TIOCSCTTY: return "tiocsctty";
    case HS_CHILD_DUP2_DONE: return "dup2_stdio";
    case HS_CHILD_TCSETPGRP: return "tcsetpgrp";
    case HS_CHILD_BEFORE_EXEC_OR_MARKER: return "before_exec_or_marker";
    case HS_PARENT_MARKER_READ: return "parent_marker_read";
    case HS_PARENT_WAIT_DONE: return "wait_done";
    default: return "unknown";
    }
}

static uint64_t now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void save_stamp(struct handoff_stamp stamps[HS_COUNT], int id,
                       int rc, int err)
{
    if (id < 0 || id >= HS_COUNT)
        return;
    stamps[id].id = id;
    stamps[id].rc = rc;
    stamps[id].err = err;
    stamps[id].us = now_us();
}

static void child_stamp(int fd, int id, int rc, int err)
{
    struct handoff_stamp st;
    ssize_t n;

    st.id = id;
    st.rc = rc;
    st.err = err;
    st.us = now_us();
    n = write(fd, &st, sizeof(st));
    (void)n;
}

static int open_slave_from_master(int master, char *name, size_t name_len,
                                  int *used_peer)
{
    int slave;

    *used_peer = 0;
    slave = ioctl(master, TIOCGPTPEER, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave >= 0) {
        snprintf(name, name_len, "TIOCGPTPEER");
        *used_peer = 1;
        return slave;
    }

    char *path = ptsname(master);
    if (path == NULL)
        return -1;
    snprintf(name, name_len, "%s", path);
    return open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
}

static int read_marker_timed(int master, const char *token, char *out,
                             size_t out_len)
{
    size_t used = 0;

    if (out_len > 0)
        out[0] = '\0';
    for (int i = 0; i < 80; i++) {
        struct pollfd pfd = { .fd = master, .events = POLLIN | POLLRDNORM };
        int pret = poll(&pfd, 1, 100);

        if (pret < 0 && errno == EINTR)
            continue;
        if (pret <= 0)
            continue;
        if (!(pfd.revents & (POLLIN | POLLRDNORM | POLLHUP)))
            return -2;

        ssize_t n = read(master, out + used, out_len - used - 1);
        if (n <= 0)
            return -3;
        used += (size_t)n;
        out[used] = '\0';
        if (strstr(out, token) != NULL)
            return 0;
        if (used >= out_len - 1) {
            memmove(out, out + used / 2, used - used / 2);
            used -= used / 2;
            out[used] = '\0';
        }
    }
    return -1;
}

static void read_child_stamps(int fd, struct handoff_stamp stamps[HS_COUNT])
{
    for (;;) {
        struct handoff_stamp st;
        ssize_t n = read(fd, &st, sizeof(st));

        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == (ssize_t)sizeof(st) && st.id >= 0 && st.id < HS_COUNT)
            stamps[st.id] = st;
    }
}

static void print_handoff_deltas(const struct handoff_stamp stamps[HS_COUNT],
                                 const char *mode, const char *slave_name,
                                 int used_peer, int status, int marker_rc,
                                 const char *output)
{
    uint64_t base = stamps[HS_PARENT_START].us;

    printf("kde_pty_openpty_probe handoff mode=%s slave=%s peer=%d "
           "marker_rc=%d wait_status=0x%x exited=%d code=%d\n",
           mode, slave_name, used_peer, marker_rc, status, WIFEXITED(status),
           WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    printf("kde_pty_openpty_probe handoff_output=%s\n",
           output && output[0] ? output : "(empty)");

    printf("kde_pty_openpty_probe handoff_abs_us");
    for (int i = 0; i < HS_COUNT; i++) {
        if (stamps[i].us == 0 || base == 0)
            continue;
        printf(" %s=%llu", handoff_stamp_name(i),
               (unsigned long long)(stamps[i].us - base));
        if (stamps[i].rc < 0)
            printf("(rc=%d,err=%d)", stamps[i].rc, stamps[i].err);
    }
    printf("\n");

    printf("kde_pty_openpty_probe handoff_delta_us");
    int prev = -1;
    for (int i = 0; i < HS_COUNT; i++) {
        if (stamps[i].us == 0)
            continue;
        if (prev >= 0) {
            int64_t delta = (int64_t)stamps[i].us -
                            (int64_t)stamps[prev].us;

            printf(" %s_to_%s=%lld", handoff_stamp_name(prev),
                   handoff_stamp_name(i),
                   (long long)delta);
        }
        prev = i;
    }
    printf("\n");
}

static int handoff_timing_probe(int use_bash, int no_exec)
{
    const char *token = "xv6-pty-handoff-marker";
    struct handoff_stamp stamps[HS_COUNT];
    int master = -1;
    int slave = -1;
    int used_peer = 0;
    int status = 0;
    int failed = 0;
    int stamp_pipe[2] = { -1, -1 };
    char slave_name[128] = "";
    char output[512];

    memset(stamps, 0, sizeof(stamps));
    save_stamp(stamps, HS_PARENT_START, 0, 0);

    master = open("/dev/ptmx", O_RDWR | O_NOCTTY | O_CLOEXEC);
    save_stamp(stamps, HS_PTMX_OPEN, master >= 0 ? 0 : -1, errno);
    if (master < 0) {
        printf("kde_pty_openpty_probe handoff /dev/ptmx errno=%d %s\n",
               errno, strerror(errno));
        return 2;
    }

    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        save_stamp(stamps, HS_GRANT_UNLOCK_DONE, -1, errno);
        printf("kde_pty_openpty_probe handoff grant_unlock errno=%d %s\n",
               errno, strerror(errno));
        close(master);
        return 2;
    }
    save_stamp(stamps, HS_GRANT_UNLOCK_DONE, 0, 0);

    slave = open_slave_from_master(master, slave_name, sizeof(slave_name),
                                   &used_peer);
    save_stamp(stamps, HS_SLAVE_OPEN, slave >= 0 ? 0 : -1, errno);
    if (slave < 0) {
        printf("kde_pty_openpty_probe handoff slave_open errno=%d %s\n",
               errno, strerror(errno));
        close(master);
        return 2;
    }

    if (pipe(stamp_pipe) < 0) {
        printf("kde_pty_openpty_probe handoff pipe errno=%d %s\n",
               errno, strerror(errno));
        close(slave);
        close(master);
        return 2;
    }
    (void)fcntl(stamp_pipe[1], F_SETFD, FD_CLOEXEC);

    save_stamp(stamps, HS_FORK_BEFORE, 0, 0);
    pid_t pid = fork();
    if (pid < 0) {
        save_stamp(stamps, HS_FORK_PARENT_RETURN, -1, errno);
        printf("kde_pty_openpty_probe handoff fork errno=%d %s\n",
               errno, strerror(errno));
        close(stamp_pipe[0]);
        close(stamp_pipe[1]);
        close(slave);
        close(master);
        return 2;
    }
    if (pid == 0) {
        close(stamp_pipe[0]);
        child_stamp(stamp_pipe[1], HS_CHILD_FIRST, 0, 0);

        int rc = setsid();
        child_stamp(stamp_pipe[1], HS_CHILD_SETSID, rc, rc < 0 ? errno : 0);
        if (rc < 0)
            _exit(120);

        rc = ioctl(slave, TIOCSCTTY, 0);
        child_stamp(stamp_pipe[1], HS_CHILD_TIOCSCTTY, rc,
                    rc < 0 ? errno : 0);
        if (rc < 0)
            _exit(121);

        if (dup2(slave, STDIN_FILENO) < 0)
            _exit(122);
        if (dup2(slave, STDOUT_FILENO) < 0)
            _exit(123);
        if (dup2(slave, STDERR_FILENO) < 0)
            _exit(124);
        child_stamp(stamp_pipe[1], HS_CHILD_DUP2_DONE, 0, 0);

        rc = tcsetpgrp(STDIN_FILENO, getpgrp());
        child_stamp(stamp_pipe[1], HS_CHILD_TCSETPGRP, rc,
                    rc < 0 ? errno : 0);

        if (slave > STDERR_FILENO)
            close(slave);
        close(master);

        child_stamp(stamp_pipe[1], HS_CHILD_BEFORE_EXEC_OR_MARKER, 0, 0);
        if (no_exec) {
            dprintf(STDOUT_FILENO, "%s\n", token);
            close(stamp_pipe[1]);
            _exit(0);
        }

        if (use_bash) {
            execl("/bin/bash", "bash", "--noprofile", "--norc", "-lc",
                  "/bin/echo xv6-pty-handoff-marker", NULL);
        } else {
            execl("/bin/sh", "sh", "-lc",
                  "/bin/echo xv6-pty-handoff-marker", NULL);
        }
        _exit(127);
    }

    save_stamp(stamps, HS_FORK_PARENT_RETURN, 0, 0);
    close(stamp_pipe[1]);
    stamp_pipe[1] = -1;
    close(slave);
    slave = -1;

    int marker_rc = read_marker_timed(master, token, output, sizeof(output));
    save_stamp(stamps, HS_PARENT_MARKER_READ, marker_rc == 0 ? 0 : -1,
               marker_rc == 0 ? 0 : errno);

    if (waitpid(pid, &status, 0) < 0) {
        status = 0;
        failed = 1;
    }
    save_stamp(stamps, HS_PARENT_WAIT_DONE, failed ? -1 : 0, errno);
    read_child_stamps(stamp_pipe[0], stamps);
    close(stamp_pipe[0]);
    close(master);

    if (marker_rc != 0)
        failed = 1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        failed = 1;

    print_handoff_deltas(stamps,
                         no_exec ? "tiny-marker" :
                         (use_bash ? "bash-exec" : "sh-exec"),
                         slave_name, used_peer, status, marker_rc, output);
    printf("kde_pty_openpty_probe handoff_result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed ? 2 : 0;
}

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

int main(int argc, char **argv)
{
    if (argc > 1) {
        int use_bash = 0;
        int no_exec = 0;

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--handoff-timing") == 0) {
                continue;
            } else if (strcmp(argv[i], "--bash") == 0) {
                use_bash = 1;
            } else if (strcmp(argv[i], "--no-exec") == 0) {
                no_exec = 1;
            } else {
                printf("usage: kde-pty-openpty-probe "
                       "[--handoff-timing [--bash|--no-exec]]\n");
                return 2;
            }
        }
        return handoff_timing_probe(use_bash, no_exec);
    }

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
