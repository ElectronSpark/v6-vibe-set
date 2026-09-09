#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifndef TIOCGPTPEER
#define TIOCGPTPEER 0x5441
#endif

static int read_until_token(int master, const char *token, const char *phase)
{
    char buf[256];
    size_t used = 0;

    for (int i = 0; i < 40; i++) {
        struct pollfd pfd = { .fd = master, .events = POLLIN, .revents = 0 };
        int pret = poll(&pfd, 1, 250);

        if (pret < 0) {
            if (errno == EINTR)
                continue;
            printf("kde_pty_shell_probe %s poll errno=%d %s\n",
                   phase, errno, strerror(errno));
            return 1;
        }
        if (pret == 0)
            continue;
        if (!(pfd.revents & POLLIN)) {
            printf("kde_pty_shell_probe %s poll revents=0x%x\n",
                   phase, pfd.revents);
            return 1;
        }

        ssize_t n = read(master, buf + used, sizeof(buf) - 1 - used);
        if (n <= 0) {
            printf("kde_pty_shell_probe %s read ret=%zd errno=%d %s\n",
                   phase, n, errno, strerror(errno));
            return 1;
        }
        used += (size_t)n;
        buf[used] = '\0';
        if (strstr(buf, token) != NULL) {
            printf("kde_pty_shell_probe %s output=%s", phase, buf);
            return 0;
        }
        if (used >= sizeof(buf) - 1)
            used = 0;
    }

    buf[used] = '\0';
    printf("kde_pty_shell_probe %s timeout waiting for token=%s partial=%s\n",
           phase, token, used > 0 ? buf : "(empty)");
    return 1;
}

static int write_marker(int fd, const char *marker, const char *phase)
{
    size_t len = strlen(marker);
    ssize_t n = write(fd, marker, len);

    printf("kde_pty_shell_probe %s write ret=%zd errno=%d %s\n",
           phase, n, errno, strerror(errno));
    return n == (ssize_t)len ? 0 : 1;
}

static int implicit_ctty_case(void)
{
    int master = -1;
    pid_t pid;
    int status = 0;
    int failed = 0;

    master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0) {
        printf("kde_pty_shell_probe implicit-ctty posix_openpt errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        printf("kde_pty_shell_probe implicit-ctty grant_unlock errno=%d %s\n",
               errno, strerror(errno));
        close(master);
        return 1;
    }

    char *slave_path = ptsname(master);
    if (slave_path == NULL) {
        printf("kde_pty_shell_probe implicit-ctty ptsname errno=%d %s\n",
               errno, strerror(errno));
        close(master);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        printf("kde_pty_shell_probe implicit-ctty fork errno=%d %s\n",
               errno, strerror(errno));
        close(master);
        return 1;
    }

    if (pid == 0) {
        int slave;
        int ttyfd;

        if (setsid() < 0)
            _exit(120);

        slave = open(slave_path, O_RDWR | O_CLOEXEC);
        if (slave < 0)
            _exit(121);

        ttyfd = open("/dev/tty", O_RDWR | O_CLOEXEC);
        if (ttyfd < 0)
            _exit(122);

        if (write(ttyfd, "xv6-pty-implicit-ctty-ready\n", 29) != 29)
            _exit(123);

        close(ttyfd);
        close(slave);
        _exit(0);
    }

    failed = read_until_token(master, "xv6-pty-implicit-ctty-ready",
                              "implicit-ctty-read");
    if (waitpid(pid, &status, failed ? WNOHANG : 0) < 0) {
        printf("kde_pty_shell_probe implicit-ctty waitpid errno=%d %s\n",
               errno, strerror(errno));
        failed = 1;
    }
    if (!failed && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))
        failed = 1;
    if (failed) {
        if (waitpid(pid, &status, WNOHANG) == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }

    printf("kde_pty_shell_probe implicit_ctty_status=0x%x exited=%d code=%d\n",
           status, WIFEXITED(status),
           WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    close(master);
    return failed;
}

static void child_diag_mark(int fd, char marker)
{
    ssize_t n = write(fd, &marker, 1);
    (void)n;
}

static void drain_child_diag(int fd, const char *phase)
{
    char buf[64];
    size_t used = 0;

    for (;;) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        int pret = poll(&pfd, 1, 0);

        if (pret <= 0 || !(pfd.revents & POLLIN))
            break;

        ssize_t n = read(fd, buf + used, sizeof(buf) - 1 - used);
        if (n <= 0)
            break;
        used += (size_t)n;
        if (used >= sizeof(buf) - 1)
            break;
    }

    buf[used] = '\0';
    printf("kde_pty_shell_probe child_diag_%s=%s\n",
           phase, used > 0 ? buf : "(empty)");
}

static void print_child_proc(pid_t pid)
{
    char path[64];
    char buf[256];

    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            printf("kde_pty_shell_probe child_comm=%s", buf);
        } else {
            printf("kde_pty_shell_probe child_comm_read ret=%zd errno=%d %s\n",
                   n, errno, strerror(errno));
        }
        close(fd);
    } else {
        printf("kde_pty_shell_probe child_comm_open errno=%d %s\n",
               errno, strerror(errno));
    }

    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            for (ssize_t i = 0; i < n; i++) {
                if (buf[i] == '\0')
                    buf[i] = ' ';
            }
            printf("kde_pty_shell_probe child_cmdline=%s\n", buf);
        } else {
            printf("kde_pty_shell_probe child_cmdline_read ret=%zd errno=%d %s\n",
                   n, errno, strerror(errno));
        }
        close(fd);
    } else {
        printf("kde_pty_shell_probe child_cmdline_open errno=%d %s\n",
               errno, strerror(errno));
    }
}

static int reap_child(pid_t pid, int *statusp, int failed, int diag_fd)
{
    int status = 0;
    pid_t ret = waitpid(pid, &status, failed ? WNOHANG : 0);

    if (ret < 0) {
        printf("kde_pty_shell_probe waitpid errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    if (ret == 0) {
        drain_child_diag(diag_fd, "before_kill");
        print_child_proc(pid);
        printf("kde_pty_shell_probe child_state=running_after_timeout\n");
        kill(pid, SIGKILL);
        ret = waitpid(pid, &status, 0);
        if (ret < 0) {
            printf("kde_pty_shell_probe waitpid_after_kill errno=%d %s\n",
                   errno, strerror(errno));
            return 1;
        }
    }

    *statusp = status;
    drain_child_diag(diag_fd, "after_wait");
    printf("kde_pty_shell_probe child_status=0x%x exited=%d code=%d signaled=%d sig=%d\n",
           status, WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : -1,
           WIFSIGNALED(status), WIFSIGNALED(status) ? WTERMSIG(status) : -1);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 1;
    return 0;
}

int main(void)
{
    int master = -1;
    int slave = -1;
    int diag_pipe[2] = { -1, -1 };
    pid_t pid;
    int status;
    int failed = 0;

    master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0) {
        printf("kde_pty_shell_probe posix_openpt errno=%d %s\n",
               errno, strerror(errno));
        return 2;
    }
    printf("kde_pty_shell_probe master=%d\n", master);

    if (grantpt(master) < 0) {
        printf("kde_pty_shell_probe grantpt errno=%d %s\n", errno, strerror(errno));
        return 2;
    }
    if (unlockpt(master) < 0) {
        printf("kde_pty_shell_probe unlockpt errno=%d %s\n", errno, strerror(errno));
        return 2;
    }

    char *slave_path = ptsname(master);
    if (slave_path == NULL) {
        printf("kde_pty_shell_probe ptsname errno=%d %s\n",
               errno, strerror(errno));
        return 2;
    }
    printf("kde_pty_shell_probe ptsname=%s\n", slave_path);

    struct stat st;
    if (stat(slave_path, &st) < 0) {
        printf("kde_pty_shell_probe stat-ptsname path=%s errno=%d %s\n",
               slave_path, errno, strerror(errno));
        return 2;
    }
    printf("kde_pty_shell_probe stat-ptsname mode=0%o rdev=%u:%u\n",
           (unsigned)st.st_mode, (unsigned)major(st.st_rdev),
           (unsigned)minor(st.st_rdev));

    int path_slave = open(slave_path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (path_slave < 0) {
        printf("kde_pty_shell_probe open-ptsname path=%s errno=%d %s\n",
               slave_path, errno, strerror(errno));
        return 2;
    }
    printf("kde_pty_shell_probe path_slave=%d\n", path_slave);
    failed = write_marker(path_slave, "xv6-pty-ptsname-ready\n",
                          "ptsname-write");
    if (!failed)
        failed = read_until_token(master, "xv6-pty-ptsname-ready",
                                  "ptsname-read");
    close(path_slave);
    if (failed)
        goto out_close;

    if (implicit_ctty_case() != 0) {
        failed = 1;
        goto out_close;
    }

    slave = ioctl(master, TIOCGPTPEER, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0) {
        printf("kde_pty_shell_probe tiocgptpeer errno=%d %s\n",
               errno, strerror(errno));
        return 2;
    }
    printf("kde_pty_shell_probe slave=%d\n", slave);

    failed = write_marker(slave, "xv6-pty-direct-ready\n", "direct-write");
    if (!failed)
        failed = read_until_token(master, "xv6-pty-direct-ready", "direct-read");
    if (failed)
        goto out_close;

    if (pipe(diag_pipe) < 0) {
        printf("kde_pty_shell_probe diag-pipe errno=%d %s\n",
               errno, strerror(errno));
        failed = 1;
        goto out_close;
    }

    pid = fork();
    if (pid < 0) {
        printf("kde_pty_shell_probe fork errno=%d %s\n", errno, strerror(errno));
        return 2;
    }
    if (pid == 0) {
        close(diag_pipe[0]);
        child_diag_mark(diag_pipe[1], 'F');
        if (setsid() < 0)
            _exit(120);
        child_diag_mark(diag_pipe[1], 'S');
        if (ioctl(slave, TIOCSCTTY, 0) < 0)
            _exit(121);
        child_diag_mark(diag_pipe[1], 'C');
        if (dup2(slave, STDIN_FILENO) < 0)
            _exit(122);
        child_diag_mark(diag_pipe[1], '0');
        if (dup2(slave, STDOUT_FILENO) < 0)
            _exit(123);
        child_diag_mark(diag_pipe[1], '1');
        if (dup2(slave, STDERR_FILENO) < 0)
            _exit(124);
        child_diag_mark(diag_pipe[1], '2');
        if (slave > STDERR_FILENO)
            close(slave);
        child_diag_mark(diag_pipe[1], 'm');
        close(master);
        child_diag_mark(diag_pipe[1], 'M');
        child_diag_mark(diag_pipe[1], 'B');
        ssize_t child_write = write(STDOUT_FILENO, "xv6-pty-child-dup-ready\n", 24);
        child_diag_mark(diag_pipe[1], child_write == 24 ? 'A' : 'a');
        child_diag_mark(diag_pipe[1], 'E');
        close(diag_pipe[1]);
        execl("/bin/sh", "sh", "-lc", "/bin/echo xv6-pty-shell-ready", NULL);
        _exit(127);
    }

    close(diag_pipe[1]);
    diag_pipe[1] = -1;
    close(slave);
    slave = -1;
    failed = read_until_token(master, "xv6-pty-shell-ready", "child-shell-read");
    if (reap_child(pid, &status, failed, diag_pipe[0]) != 0)
        failed = 1;

out_close:
    if (diag_pipe[0] >= 0)
        close(diag_pipe[0]);
    if (diag_pipe[1] >= 0)
        close(diag_pipe[1]);
    if (slave >= 0)
        close(slave);
    close(master);

    printf("kde_pty_shell_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 2 : 0;
}
