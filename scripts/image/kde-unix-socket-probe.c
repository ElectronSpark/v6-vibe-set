#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void print_errno(const char *op)
{
    printf("kde_unix_socket_probe %s errno=%d %s\n",
           op, errno, strerror(errno));
}

static int check_stat_socket(const char *path, const char *phase)
{
    struct stat st;

    errno = 0;
    if (stat(path, &st) < 0) {
        printf("kde_unix_socket_probe stat phase=%s ret=-1 errno=%d %s\n",
               phase, errno, strerror(errno));
        return 1;
    }

    printf("kde_unix_socket_probe stat phase=%s mode=0%o type=0%o is_socket=%d\n",
           phase, (unsigned int)st.st_mode,
           (unsigned int)(st.st_mode & S_IFMT), S_ISSOCK(st.st_mode) ? 1 : 0);
    return S_ISSOCK(st.st_mode) ? 0 : 1;
}

static int poll_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct pollfd pfd;
    char buf[4] = {0};
    int server = -1;
    int client = -1;
    int connected = 0;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("poll-roundtrip-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("poll-roundtrip-bind");
        close(server);
        return 1;
    }
    if (listen(server, 1) < 0) {
        print_errno("poll-roundtrip-listen");
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("poll-roundtrip-fork");
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        int accepted = accept(server, NULL, NULL);
        if (accepted < 0) {
            print_errno("poll-roundtrip-child-accept");
            _exit(10);
        }
        pfd.fd = accepted;
        pfd.events = POLLIN;
        pfd.revents = 0;
        errno = 0;
        int pret = poll(&pfd, 1, 5000);
        printf("kde_unix_socket_probe poll-roundtrip child-poll ret=%d revents=0x%x errno=%d %s\n",
               pret, pfd.revents, errno, strerror(errno));
        if (pret != 1 || !(pfd.revents & POLLIN)) {
            close(accepted);
            _exit(11);
        }
        if (read(accepted, buf, 3) != 3 || memcmp(buf, "REQ", 3) != 0) {
            print_errno("poll-roundtrip-child-read");
            close(accepted);
            _exit(12);
        }
        if (write(accepted, "RSP", 3) != 3) {
            print_errno("poll-roundtrip-child-write");
            close(accepted);
            _exit(13);
        }
        close(accepted);
        close(server);
        _exit(0);
    }

    client = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0) {
        print_errno("poll-roundtrip-socket-client");
        failed = 1;
        goto out;
    }
    if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("poll-roundtrip-connect");
        failed = 1;
        goto out;
    }
    connected = 1;
    if (write(client, "REQ", 3) != 3) {
        print_errno("poll-roundtrip-client-write");
        failed = 1;
        goto out;
    }

    pfd.fd = client;
    pfd.events = POLLIN;
    pfd.revents = 0;
    errno = 0;
    int pret = poll(&pfd, 1, 5000);
    printf("kde_unix_socket_probe poll-roundtrip client-poll ret=%d revents=0x%x errno=%d %s\n",
           pret, pfd.revents, errno, strerror(errno));
    if (pret != 1 || !(pfd.revents & POLLIN)) {
        failed = 1;
        goto out;
    }
    memset(buf, 0, sizeof(buf));
    if (read(client, buf, 3) != 3 || memcmp(buf, "RSP", 3) != 0) {
        print_errno("poll-roundtrip-client-read");
        failed = 1;
        goto out;
    }

out:
    if (client >= 0)
        close(client);
    close(server);
    if (!connected)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("poll-roundtrip-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe poll-roundtrip child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    unlink(path);
    printf("kde_unix_socket_probe poll-roundtrip result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int epoll_listen_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct epoll_event ev;
    struct epoll_event out_ev;
    char buf[4] = {0};
    int server = -1;
    int epfd = -1;
    int accepted = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("epoll-listen-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("epoll-listen-bind");
        close(server);
        return 1;
    }
    if (listen(server, 1) < 0) {
        print_errno("epoll-listen-listen");
        close(server);
        unlink(path);
        return 1;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        print_errno("epoll-listen-create");
        close(server);
        unlink(path);
        return 1;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = server;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server, &ev) < 0) {
        print_errno("epoll-listen-ctl-add");
        close(epfd);
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("epoll-listen-fork");
        close(epfd);
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0) {
            print_errno("epoll-listen-child-socket");
            _exit(20);
        }
        usleep(100000);
        if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            print_errno("epoll-listen-child-connect");
            close(client);
            _exit(21);
        }
        if (write(client, "HEL", 3) != 3) {
            print_errno("epoll-listen-child-write");
            close(client);
            _exit(22);
        }
        if (read(client, buf, 3) != 3 || memcmp(buf, "ACK", 3) != 0) {
            print_errno("epoll-listen-child-read");
            close(client);
            _exit(23);
        }
        close(client);
        _exit(0);
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    int eret = epoll_wait(epfd, &out_ev, 1, 5000);
    printf("kde_unix_socket_probe epoll-listen wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != server || !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }

    accepted = accept(server, NULL, NULL);
    if (accepted < 0) {
        print_errno("epoll-listen-accept");
        failed = 1;
        goto out;
    }
    if (read(accepted, buf, 3) != 3 || memcmp(buf, "HEL", 3) != 0) {
        print_errno("epoll-listen-read");
        failed = 1;
        goto out;
    }
    if (write(accepted, "ACK", 3) != 3) {
        print_errno("epoll-listen-write");
        failed = 1;
        goto out;
    }

out:
    if (accepted >= 0)
        close(accepted);
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("epoll-listen-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe epoll-listen child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    close(epfd);
    close(server);
    unlink(path);
    printf("kde_unix_socket_probe epoll-listen result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int socketpair_sendmsg_ppoll_fd3_one(int sock_type, const char *label)
{
    struct pollfd pfd;
    struct timespec timeout;
    struct iovec iov;
    struct msghdr msg;
    char payload[8] = { 4, 0, 0, 0, 3, 0, 0, 0 };
    char buf[8] = {0};
    int sv[2] = {-1, -1};
    int fd3_saved = -1;
    int failed = 0;

    errno = 0;
    if (socketpair(AF_UNIX, sock_type | SOCK_CLOEXEC, 0, sv) < 0) {
        char op[96];
        snprintf(op, sizeof(op), "socketpair-sendmsg-ppoll-fd3-%s-socketpair",
                 label);
        print_errno(op);
        return 1;
    }

    fd3_saved = dup(3);
    if (fd3_saved < 0 && errno != EBADF) {
        char op[96];
        snprintf(op, sizeof(op), "socketpair-sendmsg-ppoll-fd3-%s-dup-save",
                 label);
        print_errno(op);
        failed = 1;
        goto out;
    }

    if (dup2(sv[1], 3) != 3) {
        char op[96];
        snprintf(op, sizeof(op), "socketpair-sendmsg-ppoll-fd3-%s-dup2",
                 label);
        print_errno(op);
        failed = 1;
        goto out;
    }

    iov.iov_base = payload;
    iov.iov_len = sizeof(payload);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    errno = 0;
    ssize_t n = sendmsg(sv[0], &msg, MSG_NOSIGNAL);
    printf("kde_unix_socket_probe socketpair-sendmsg-ppoll-fd3-%s sendmsg ret=%zd errno=%d %s\n",
           label, n, errno, strerror(errno));
    if (n != (ssize_t)sizeof(payload)) {
        failed = 1;
        goto out;
    }

    pfd.fd = 3;
    pfd.events = POLLIN;
    pfd.revents = 0;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 100000000;
    errno = 0;
    int pret = ppoll(&pfd, 1, &timeout, NULL);
    printf("kde_unix_socket_probe socketpair-sendmsg-ppoll-fd3-%s ppoll ret=%d revents=0x%x errno=%d %s\n",
           label, pret, pfd.revents, errno, strerror(errno));
    if (pret != 1 || !(pfd.revents & POLLIN)) {
        failed = 1;
        goto out;
    }

    errno = 0;
    ssize_t r = read(3, buf, sizeof(buf));
    printf("kde_unix_socket_probe socketpair-sendmsg-ppoll-fd3-%s read ret=%zd errno=%d %s\n",
           label, r, errno, strerror(errno));
    if (r != (ssize_t)sizeof(payload) ||
        memcmp(buf, payload, sizeof(payload)) != 0)
        failed = 1;

out:
    if (fd3_saved >= 0) {
        dup2(fd3_saved, 3);
        close(fd3_saved);
    } else {
        close(3);
    }
    if (sv[0] >= 0)
        close(sv[0]);
    if (sv[1] >= 0)
        close(sv[1]);
    printf("kde_unix_socket_probe socketpair-sendmsg-ppoll-fd3-%s result=%s\n",
           label, failed ? "FAIL" : "PASS");
    return failed;
}

static int socketpair_sendmsg_ppoll_fd3(void)
{
    int failed = 0;

    failed |= socketpair_sendmsg_ppoll_fd3_one(SOCK_STREAM, "stream");
    failed |= socketpair_sendmsg_ppoll_fd3_one(SOCK_SEQPACKET, "seqpacket");
    printf("kde_unix_socket_probe socketpair-sendmsg-ppoll-fd3 result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int socketpair_fd3_child_ack_one(int sock_type, const char *label,
                                        int passcred, int ack_sendmsg)
{
    struct pollfd pfd;
    struct timespec timeout;
    struct iovec iov;
    struct msghdr msg;
    char payload[8] = { 4, 0, 0, 0, 3, 0, 0, 0 };
    char ack[4] = { 'A', 'C', 'K', '!' };
    char buf[8] = {0};
    int sv[2] = {-1, -1};
    int failed = 0;
    int status = 0;
    pid_t pid;

    errno = 0;
    if (socketpair(AF_UNIX, sock_type | SOCK_CLOEXEC, 0, sv) < 0) {
        char op[128];
        snprintf(op, sizeof(op), "socketpair-fd3-child-ack-%s-socketpair",
                 label);
        print_errno(op);
        return 1;
    }

    if (passcred) {
        int one = 1;
        errno = 0;
        if (setsockopt(sv[0], SOL_SOCKET, SO_PASSCRED,
                       &one, sizeof(one)) < 0) {
            char op[128];
            snprintf(op, sizeof(op),
                     "socketpair-fd3-child-ack-%s-so-passcred", label);
            print_errno(op);
            failed = 1;
            goto out_nokill;
        }
    }

    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        char op[128];
        snprintf(op, sizeof(op), "socketpair-fd3-child-ack-%s-fork", label);
        print_errno(op);
        failed = 1;
        goto out_nokill;
    }

    if (pid == 0) {
        close(sv[0]);
        if (dup2(sv[1], 3) != 3) {
            char op[128];
            snprintf(op, sizeof(op),
                     "socketpair-fd3-child-ack-%s-child-dup2", label);
            print_errno(op);
            fflush(stdout);
            _exit(20);
        }
        if (sv[1] != 3)
            close(sv[1]);

        pfd.fd = 3;
        pfd.events = POLLIN;
        pfd.revents = 0;
        timeout.tv_sec = 5;
        timeout.tv_nsec = 0;
        errno = 0;
        int pret = ppoll(&pfd, 1, &timeout, NULL);
        printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s child-ppoll ret=%d revents=0x%x errno=%d %s\n",
               label, pret, pfd.revents, errno, strerror(errno));
        if (pret != 1 || !(pfd.revents & POLLIN)) {
            fflush(stdout);
            _exit(21);
        }

        errno = 0;
        ssize_t r = read(3, buf, sizeof(payload));
        printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s child-read ret=%zd errno=%d %s\n",
               label, r, errno, strerror(errno));
        if (r != (ssize_t)sizeof(payload) ||
            memcmp(buf, payload, sizeof(payload)) != 0) {
            fflush(stdout);
            _exit(22);
        }

        errno = 0;
        ssize_t w;
        if (ack_sendmsg) {
            struct iovec aiov;
            struct msghdr amsg;

            aiov.iov_base = ack;
            aiov.iov_len = sizeof(ack);
            memset(&amsg, 0, sizeof(amsg));
            amsg.msg_iov = &aiov;
            amsg.msg_iovlen = 1;
            w = sendmsg(3, &amsg, MSG_NOSIGNAL);
        } else {
            w = write(3, ack, sizeof(ack));
        }
        printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s child-%s ret=%zd errno=%d %s\n",
               label, ack_sendmsg ? "sendmsg" : "write", w, errno,
               strerror(errno));
        fflush(stdout);
        close(3);
        _exit(w == (ssize_t)sizeof(ack) ? 0 : 23);
    }

    close(sv[1]);
    sv[1] = -1;

    iov.iov_base = payload;
    iov.iov_len = sizeof(payload);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    errno = 0;
    ssize_t n = sendmsg(sv[0], &msg, MSG_NOSIGNAL);
    printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s parent-sendmsg ret=%zd errno=%d %s\n",
           label, n, errno, strerror(errno));
    if (n != (ssize_t)sizeof(payload)) {
        failed = 1;
        goto out;
    }

    pfd.fd = sv[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    timeout.tv_sec = 5;
    timeout.tv_nsec = 0;
    errno = 0;
    int pret = ppoll(&pfd, 1, &timeout, NULL);
    printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s parent-ppoll ret=%d revents=0x%x errno=%d %s\n",
           label, pret, pfd.revents, errno, strerror(errno));
    if (pret != 1 || !(pfd.revents & POLLIN)) {
        failed = 1;
        goto out;
    }

    memset(buf, 0, sizeof(buf));
    errno = 0;
    ssize_t r = read(sv[0], buf, sizeof(ack));
    printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s parent-read ret=%zd errno=%d %s\n",
           label, r, errno, strerror(errno));
    if (r != (ssize_t)sizeof(ack) || memcmp(buf, ack, sizeof(ack)) != 0)
        failed = 1;

out:
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        char op[128];
        snprintf(op, sizeof(op), "socketpair-fd3-child-ack-%s-waitpid",
                 label);
        print_errno(op);
        failed = 1;
    } else {
        printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s child-status=0x%x exited=%d code=%d\n",
               label, status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }

out_nokill:
    if (sv[0] >= 0)
        close(sv[0]);
    if (sv[1] >= 0)
        close(sv[1]);
    printf("kde_unix_socket_probe socketpair-fd3-child-ack-%s result=%s\n",
           label, failed ? "FAIL" : "PASS");
    return failed;
}

static int socketpair_fd3_child_ack(void)
{
    int failed = 0;

    failed |= socketpair_fd3_child_ack_one(SOCK_STREAM,
                                           "stream-write", 0, 0);
    failed |= socketpair_fd3_child_ack_one(SOCK_SEQPACKET,
                                           "seqpacket-write", 0, 0);
    failed |= socketpair_fd3_child_ack_one(SOCK_SEQPACKET,
                                           "seqpacket-sendmsg", 0, 1);
    failed |= socketpair_fd3_child_ack_one(SOCK_SEQPACKET,
                                           "seqpacket-passcred-write", 1, 0);
    printf("kde_unix_socket_probe socketpair-fd3-child-ack result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int epoll_connected_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct epoll_event ev;
    struct epoll_event out_ev;
    char buf[4] = {0};
    int server = -1;
    int epfd = -1;
    int accepted = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("epoll-connected-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("epoll-connected-bind");
        close(server);
        return 1;
    }
    if (listen(server, 1) < 0) {
        print_errno("epoll-connected-listen");
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("epoll-connected-fork");
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0) {
            print_errno("epoll-connected-child-socket");
            _exit(30);
        }
        if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            print_errno("epoll-connected-child-connect");
            close(client);
            _exit(31);
        }
        usleep(100000);
        if (write(client, "WAY", 3) != 3) {
            print_errno("epoll-connected-child-write");
            close(client);
            _exit(32);
        }
        if (read(client, buf, 3) != 3 || memcmp(buf, "ACK", 3) != 0) {
            print_errno("epoll-connected-child-read");
            close(client);
            _exit(33);
        }
        close(client);
        _exit(0);
    }

    accepted = accept(server, NULL, NULL);
    if (accepted < 0) {
        print_errno("epoll-connected-accept");
        failed = 1;
        goto out;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        print_errno("epoll-connected-create");
        failed = 1;
        goto out;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = accepted;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, accepted, &ev) < 0) {
        print_errno("epoll-connected-ctl-add");
        failed = 1;
        goto out;
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    int eret = epoll_wait(epfd, &out_ev, 1, 5000);
    printf("kde_unix_socket_probe epoll-connected wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != accepted ||
        !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }
    if (read(accepted, buf, 3) != 3 || memcmp(buf, "WAY", 3) != 0) {
        print_errno("epoll-connected-read");
        failed = 1;
        goto out;
    }
    if (write(accepted, "ACK", 3) != 3) {
        print_errno("epoll-connected-write");
        failed = 1;
        goto out;
    }

out:
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("epoll-connected-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe epoll-connected child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    if (epfd >= 0)
        close(epfd);
    if (accepted >= 0)
        close(accepted);
    close(server);
    unlink(path);
    printf("kde_unix_socket_probe epoll-connected result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int epoll_listen_accept4_cloexec_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct epoll_event ev;
    struct epoll_event out_ev;
    char buf[4] = {0};
    int server = -1;
    int epfd = -1;
    int accepted = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("epoll-listen-accept4-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("epoll-listen-accept4-bind");
        close(server);
        return 1;
    }
    if (listen(server, 16) < 0) {
        print_errno("epoll-listen-accept4-listen");
        close(server);
        unlink(path);
        return 1;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        print_errno("epoll-listen-accept4-create");
        close(server);
        unlink(path);
        return 1;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = server;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server, &ev) < 0) {
        print_errno("epoll-listen-accept4-ctl-add");
        close(epfd);
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("epoll-listen-accept4-fork");
        close(epfd);
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        struct pollfd pfd;
        int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (client < 0) {
            print_errno("epoll-listen-accept4-child-socket");
            _exit(60);
        }
        usleep(100000);
        errno = 0;
        if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            print_errno("epoll-listen-accept4-child-connect");
            close(client);
            _exit(61);
        }
        printf("kde_unix_socket_probe epoll-listen-accept4 child-connect errno=%d %s\n",
               errno, strerror(errno));
        pfd.fd = client;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        errno = 0;
        int pret = poll(&pfd, 1, 5000);
        printf("kde_unix_socket_probe epoll-listen-accept4 child-poll ret=%d revents=0x%x errno=%d %s\n",
               pret, pfd.revents, errno, strerror(errno));
        if (pret != 1 || !(pfd.revents & POLLOUT)) {
            close(client);
            _exit(64);
        }
        if (write(client, "PWC", 3) != 3) {
            print_errno("epoll-listen-accept4-child-write");
            close(client);
            _exit(62);
        }
        pfd.fd = client;
        pfd.events = POLLIN;
        pfd.revents = 0;
        errno = 0;
        pret = poll(&pfd, 1, 5000);
        printf("kde_unix_socket_probe epoll-listen-accept4 child-read-poll ret=%d revents=0x%x errno=%d %s\n",
               pret, pfd.revents, errno, strerror(errno));
        if (pret != 1 || !(pfd.revents & POLLIN)) {
            close(client);
            _exit(65);
        }
        if (read(client, buf, 3) != 3 || memcmp(buf, "ACK", 3) != 0) {
            print_errno("epoll-listen-accept4-child-read");
            close(client);
            _exit(63);
        }
        close(client);
        _exit(0);
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    int eret = epoll_wait(epfd, &out_ev, 1, 5000);
    printf("kde_unix_socket_probe epoll-listen-accept4 wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != server || !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }

    errno = 0;
    accepted = accept4(server, NULL, NULL, SOCK_CLOEXEC);
    printf("kde_unix_socket_probe epoll-listen-accept4 accept4 fd=%d errno=%d %s\n",
           accepted, errno, strerror(errno));
    if (accepted < 0) {
        failed = 1;
        goto out;
    }
    if (read(accepted, buf, 3) != 3 || memcmp(buf, "PWC", 3) != 0) {
        print_errno("epoll-listen-accept4-read");
        failed = 1;
        goto out;
    }
    if (write(accepted, "ACK", 3) != 3) {
        print_errno("epoll-listen-accept4-write");
        failed = 1;
        goto out;
    }

out:
    if (accepted >= 0)
        close(accepted);
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("epoll-listen-accept4-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe epoll-listen-accept4 child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    close(epfd);
    close(server);
    unlink(path);
    printf("kde_unix_socket_probe epoll-listen-accept4 result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int epoll_aggregate_poll_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct epoll_event ev;
    struct epoll_event out_ev;
    struct pollfd pfd;
    char buf[4] = {0};
    int server = -1;
    int epfd = -1;
    int accepted = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("epoll-aggregate-poll-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("epoll-aggregate-poll-bind");
        close(server);
        return 1;
    }
    if (listen(server, 1) < 0) {
        print_errno("epoll-aggregate-poll-listen");
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("epoll-aggregate-poll-fork");
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0) {
            print_errno("epoll-aggregate-poll-child-socket");
            _exit(40);
        }
        if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            print_errno("epoll-aggregate-poll-child-connect");
            close(client);
            _exit(41);
        }
        usleep(100000);
        if (write(client, "AGG", 3) != 3) {
            print_errno("epoll-aggregate-poll-child-write");
            close(client);
            _exit(42);
        }
        if (read(client, buf, 3) != 3 || memcmp(buf, "ACK", 3) != 0) {
            print_errno("epoll-aggregate-poll-child-read");
            close(client);
            _exit(43);
        }
        close(client);
        _exit(0);
    }

    accepted = accept(server, NULL, NULL);
    if (accepted < 0) {
        print_errno("epoll-aggregate-poll-accept");
        failed = 1;
        goto out;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        print_errno("epoll-aggregate-poll-create");
        failed = 1;
        goto out;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = accepted;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, accepted, &ev) < 0) {
        print_errno("epoll-aggregate-poll-ctl-add");
        failed = 1;
        goto out;
    }

    pfd.fd = epfd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    errno = 0;
    int pret = poll(&pfd, 1, 5000);
    printf("kde_unix_socket_probe epoll-aggregate-poll poll ret=%d revents=0x%x errno=%d %s\n",
           pret, pfd.revents, errno, strerror(errno));
    if (pret != 1 || !(pfd.revents & POLLIN)) {
        failed = 1;
        goto out;
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    int eret = epoll_wait(epfd, &out_ev, 1, 0);
    printf("kde_unix_socket_probe epoll-aggregate-poll wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != accepted ||
        !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }
    if (read(accepted, buf, 3) != 3 || memcmp(buf, "AGG", 3) != 0) {
        print_errno("epoll-aggregate-poll-read");
        failed = 1;
        goto out;
    }
    if (write(accepted, "ACK", 3) != 3) {
        print_errno("epoll-aggregate-poll-write");
        failed = 1;
        goto out;
    }

out:
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("epoll-aggregate-poll-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe epoll-aggregate-poll child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    if (epfd >= 0)
        close(epfd);
    if (accepted >= 0)
        close(accepted);
    close(server);
    unlink(path);
    printf("kde_unix_socket_probe epoll-aggregate-poll result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int epoll_aggregate_nested_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct epoll_event ev;
    struct epoll_event out_ev;
    char buf[4] = {0};
    int server = -1;
    int inner_epfd = -1;
    int outer_epfd = -1;
    int accepted = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("epoll-aggregate-nested-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("epoll-aggregate-nested-bind");
        close(server);
        return 1;
    }
    if (listen(server, 1) < 0) {
        print_errno("epoll-aggregate-nested-listen");
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("epoll-aggregate-nested-fork");
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0) {
            print_errno("epoll-aggregate-nested-child-socket");
            _exit(50);
        }
        if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            print_errno("epoll-aggregate-nested-child-connect");
            close(client);
            _exit(51);
        }
        usleep(100000);
        if (write(client, "NAG", 3) != 3) {
            print_errno("epoll-aggregate-nested-child-write");
            close(client);
            _exit(52);
        }
        if (read(client, buf, 3) != 3 || memcmp(buf, "ACK", 3) != 0) {
            print_errno("epoll-aggregate-nested-child-read");
            close(client);
            _exit(53);
        }
        close(client);
        _exit(0);
    }

    accepted = accept(server, NULL, NULL);
    if (accepted < 0) {
        print_errno("epoll-aggregate-nested-accept");
        failed = 1;
        goto out;
    }

    inner_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (inner_epfd < 0) {
        print_errno("epoll-aggregate-nested-create-inner");
        failed = 1;
        goto out;
    }
    outer_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (outer_epfd < 0) {
        print_errno("epoll-aggregate-nested-create-outer");
        failed = 1;
        goto out;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = accepted;
    if (epoll_ctl(inner_epfd, EPOLL_CTL_ADD, accepted, &ev) < 0) {
        print_errno("epoll-aggregate-nested-inner-add");
        failed = 1;
        goto out;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = inner_epfd;
    if (epoll_ctl(outer_epfd, EPOLL_CTL_ADD, inner_epfd, &ev) < 0) {
        print_errno("epoll-aggregate-nested-outer-add");
        failed = 1;
        goto out;
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    int eret = epoll_wait(outer_epfd, &out_ev, 1, 5000);
    printf("kde_unix_socket_probe epoll-aggregate-nested outer-wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != inner_epfd ||
        !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    eret = epoll_wait(inner_epfd, &out_ev, 1, 0);
    printf("kde_unix_socket_probe epoll-aggregate-nested inner-wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != accepted ||
        !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }
    if (read(accepted, buf, 3) != 3 || memcmp(buf, "NAG", 3) != 0) {
        print_errno("epoll-aggregate-nested-read");
        failed = 1;
        goto out;
    }
    if (write(accepted, "ACK", 3) != 3) {
        print_errno("epoll-aggregate-nested-write");
        failed = 1;
        goto out;
    }

out:
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("epoll-aggregate-nested-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe epoll-aggregate-nested child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    if (outer_epfd >= 0)
        close(outer_epfd);
    if (inner_epfd >= 0)
        close(inner_epfd);
    if (accepted >= 0)
        close(accepted);
    close(server);
    unlink(path);
    printf("kde_unix_socket_probe epoll-aggregate-nested result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int wayland_msg_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct pollfd pfd;
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    char req_a[] = "W";
    char req_b[] = "LND";
    char ack_a[] = "A";
    char ack_b[] = "CK";
    char buf[8] = {0};
    int server = -1;
    int accepted = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) {
        print_errno("wayland-msg-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("wayland-msg-bind");
        close(server);
        return 1;
    }
    if (listen(server, 1) < 0) {
        print_errno("wayland-msg-listen");
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("wayland-msg-fork");
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        struct iovec req_iov[2];
        struct iovec ack_iov;
        struct msghdr msg;
        int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (client < 0) {
            print_errno("wayland-msg-child-socket");
            _exit(60);
        }
        if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            print_errno("wayland-msg-child-connect");
            close(client);
            _exit(61);
        }

        req_iov[0].iov_base = req_a;
        req_iov[0].iov_len = 1;
        req_iov[1].iov_base = req_b;
        req_iov[1].iov_len = 3;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = req_iov;
        msg.msg_iovlen = 2;
        errno = 0;
        ssize_t n = sendmsg(client, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
        printf("kde_unix_socket_probe wayland-msg child-sendmsg ret=%zd errno=%d %s\n",
               n, errno, strerror(errno));
        if (n != 4) {
            close(client);
            _exit(62);
        }

        pfd.fd = client;
        pfd.events = POLLIN;
        pfd.revents = 0;
        errno = 0;
        int pret = poll(&pfd, 1, 5000);
        printf("kde_unix_socket_probe wayland-msg child-poll ret=%d revents=0x%x errno=%d %s\n",
               pret, pfd.revents, errno, strerror(errno));
        if (pret != 1 || !(pfd.revents & POLLIN)) {
            close(client);
            _exit(63);
        }

        ack_iov.iov_base = buf;
        ack_iov.iov_len = 3;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &ack_iov;
        msg.msg_iovlen = 1;
        errno = 0;
        n = recvmsg(client, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        printf("kde_unix_socket_probe wayland-msg child-recvmsg ret=%zd flags=0x%x errno=%d %s\n",
               n, msg.msg_flags, errno, strerror(errno));
        if (n != 3 || memcmp(buf, "ACK", 3) != 0) {
            close(client);
            _exit(64);
        }
        close(client);
        _exit(0);
    }

    accepted = accept4(server, NULL, NULL, SOCK_CLOEXEC);
    if (accepted < 0) {
        print_errno("wayland-msg-accept4");
        failed = 1;
        goto out;
    }

    errno = 0;
    if (getsockopt(accepted, SOL_SOCKET, SO_PEERCRED,
                   &cred, &cred_len) < 0) {
        print_errno("wayland-msg-peersockcred");
        failed = 1;
        goto out;
    }
    printf("kde_unix_socket_probe wayland-msg peercred pid=%d uid=%u gid=%u len=%u\n",
           cred.pid, (unsigned int)cred.uid, (unsigned int)cred.gid,
           (unsigned int)cred_len);
    if (cred_len != sizeof(cred) || cred.pid <= 0) {
        failed = 1;
        goto out;
    }

    pfd.fd = accepted;
    pfd.events = POLLIN;
    pfd.revents = 0;
    errno = 0;
    int pret = poll(&pfd, 1, 5000);
    printf("kde_unix_socket_probe wayland-msg server-poll ret=%d revents=0x%x errno=%d %s\n",
           pret, pfd.revents, errno, strerror(errno));
    if (pret != 1 || !(pfd.revents & POLLIN)) {
        failed = 1;
        goto out;
    }

    struct iovec req_iov;
    struct msghdr msg;
    req_iov.iov_base = buf;
    req_iov.iov_len = 4;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &req_iov;
    msg.msg_iovlen = 1;
    errno = 0;
    ssize_t n = recvmsg(accepted, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    printf("kde_unix_socket_probe wayland-msg server-recvmsg ret=%zd flags=0x%x errno=%d %s\n",
           n, msg.msg_flags, errno, strerror(errno));
    if (n != 4 || memcmp(buf, "WLND", 4) != 0) {
        failed = 1;
        goto out;
    }

    struct iovec ack_iov[2];
    ack_iov[0].iov_base = ack_a;
    ack_iov[0].iov_len = 1;
    ack_iov[1].iov_base = ack_b;
    ack_iov[1].iov_len = 2;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = ack_iov;
    msg.msg_iovlen = 2;
    errno = 0;
    n = sendmsg(accepted, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
    printf("kde_unix_socket_probe wayland-msg server-sendmsg ret=%zd errno=%d %s\n",
           n, errno, strerror(errno));
    if (n != 3) {
        failed = 1;
        goto out;
    }

out:
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("wayland-msg-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe wayland-msg child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    if (accepted >= 0)
        close(accepted);
    close(server);
    unlink(path);
    printf("kde_unix_socket_probe wayland-msg result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int epoll_aggregate_nested_edge_roundtrip(const char *path)
{
    struct sockaddr_un sa;
    struct epoll_event ev;
    struct epoll_event out_ev;
    char buf[4] = {0};
    int server = -1;
    int inner_epfd = -1;
    int outer_epfd = -1;
    int accepted = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;

    unlink(path);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("epoll-aggregate-edge-socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("epoll-aggregate-edge-bind");
        close(server);
        return 1;
    }
    if (listen(server, 1) < 0) {
        print_errno("epoll-aggregate-edge-listen");
        close(server);
        unlink(path);
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("epoll-aggregate-edge-fork");
        close(server);
        unlink(path);
        return 1;
    }

    if (pid == 0) {
        int client = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client < 0) {
            print_errno("epoll-aggregate-edge-child-socket");
            _exit(70);
        }
        if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
            print_errno("epoll-aggregate-edge-child-connect");
            close(client);
            _exit(71);
        }
        usleep(100000);
        if (write(client, "EDG", 3) != 3) {
            print_errno("epoll-aggregate-edge-child-write");
            close(client);
            _exit(72);
        }
        if (read(client, buf, 3) != 3 || memcmp(buf, "ACK", 3) != 0) {
            print_errno("epoll-aggregate-edge-child-read");
            close(client);
            _exit(73);
        }
        close(client);
        _exit(0);
    }

    accepted = accept(server, NULL, NULL);
    if (accepted < 0) {
        print_errno("epoll-aggregate-edge-accept");
        failed = 1;
        goto out;
    }

    inner_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (inner_epfd < 0) {
        print_errno("epoll-aggregate-edge-create-inner");
        failed = 1;
        goto out;
    }
    outer_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (outer_epfd < 0) {
        print_errno("epoll-aggregate-edge-create-outer");
        failed = 1;
        goto out;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = accepted;
    if (epoll_ctl(inner_epfd, EPOLL_CTL_ADD, accepted, &ev) < 0) {
        print_errno("epoll-aggregate-edge-inner-add");
        failed = 1;
        goto out;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    ev.data.fd = inner_epfd;
    if (epoll_ctl(outer_epfd, EPOLL_CTL_ADD, inner_epfd, &ev) < 0) {
        print_errno("epoll-aggregate-edge-outer-add");
        failed = 1;
        goto out;
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    int eret = epoll_wait(outer_epfd, &out_ev, 1, 5000);
    printf("kde_unix_socket_probe epoll-aggregate-edge outer-wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != inner_epfd ||
        !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }

    memset(&out_ev, 0, sizeof(out_ev));
    errno = 0;
    eret = epoll_wait(inner_epfd, &out_ev, 1, 0);
    printf("kde_unix_socket_probe epoll-aggregate-edge inner-wait ret=%d events=0x%x data_fd=%d errno=%d %s\n",
           eret, eret > 0 ? out_ev.events : 0,
           eret > 0 ? out_ev.data.fd : -1, errno, strerror(errno));
    if (eret != 1 || out_ev.data.fd != accepted ||
        !(out_ev.events & EPOLLIN)) {
        failed = 1;
        goto out;
    }
    if (read(accepted, buf, 3) != 3 || memcmp(buf, "EDG", 3) != 0) {
        print_errno("epoll-aggregate-edge-read");
        failed = 1;
        goto out;
    }
    if (write(accepted, "ACK", 3) != 3) {
        print_errno("epoll-aggregate-edge-write");
        failed = 1;
        goto out;
    }

out:
    if (failed)
        kill(pid, SIGKILL);
    if (waitpid(pid, &status, 0) < 0) {
        print_errno("epoll-aggregate-edge-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe epoll-aggregate-edge child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    if (outer_epfd >= 0)
        close(outer_epfd);
    if (inner_epfd >= 0)
        close(inner_epfd);
    if (accepted >= 0)
        close(accepted);
    close(server);
    unlink(path);
    printf("kde_unix_socket_probe epoll-aggregate-edge result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int scm_zero_readiness_one(const char *label, int use_sendmsg)
{
    struct pollfd pfd;
    char byte = 0;
    int sv[2] = {-1, -1};
    int failed = 0;
    int one = 1;

    errno = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        print_errno("scm-zero-readiness-socketpair");
        return 1;
    }

    errno = 0;
    if (setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) < 0) {
        print_errno("scm-zero-readiness-so-passcred");
        failed = 1;
        goto out;
    }

    errno = 0;
    ssize_t n;
    if (use_sendmsg) {
        struct msghdr msg;

        memset(&msg, 0, sizeof(msg));
        n = sendmsg(sv[0], &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
    } else {
        n = write(sv[0], "", 0);
    }
    printf("kde_unix_socket_probe scm-zero-readiness-%s send ret=%zd errno=%d %s\n",
           label, n, errno, strerror(errno));
    if (n != 0) {
        failed = 1;
        goto out;
    }

    pfd.fd = sv[1];
    pfd.events = POLLIN;
    pfd.revents = 0;
    errno = 0;
    int pret = poll(&pfd, 1, 100);
    printf("kde_unix_socket_probe scm-zero-readiness-%s poll ret=%d revents=0x%x errno=%d %s\n",
           label, pret, pfd.revents, errno, strerror(errno));
    if (pret != 0 || pfd.revents != 0)
        failed = 1;

    int flags = fcntl(sv[1], F_GETFL, 0);
    if (flags < 0 || fcntl(sv[1], F_SETFL, flags | O_NONBLOCK) < 0) {
        print_errno("scm-zero-readiness-nonblock");
        failed = 1;
        goto out;
    }

    errno = 0;
    ssize_t r = read(sv[1], &byte, 1);
    printf("kde_unix_socket_probe scm-zero-readiness-%s read ret=%zd errno=%d %s\n",
           label, r, errno, strerror(errno));
    if (r != -1 || (errno != EAGAIN && errno != EWOULDBLOCK))
        failed = 1;

out:
    if (sv[0] >= 0)
        close(sv[0]);
    if (sv[1] >= 0)
        close(sv[1]);
    printf("kde_unix_socket_probe scm-zero-readiness-%s result=%s\n",
           label, failed ? "FAIL" : "PASS");
    return failed;
}

static int scm_zero_readiness(void)
{
    int failed = 0;

    failed |= scm_zero_readiness_one("write", 0);
    failed |= scm_zero_readiness_one("sendmsg", 1);
    printf("kde_unix_socket_probe scm-zero-readiness result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static long monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int set_fd_nonblock(const char *label, int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        printf("kde_unix_socket_probe %s set-nonblock fd=%d errno=%d %s\n",
               label, fd, errno, strerror(errno));
        return 1;
    }
    return 0;
}

static int notify_mix_one(const char *label, int signal_fd)
{
    struct pollfd pfds[3];
    char byte = 0;
    int sv[2] = {-1, -1};
    int pipefd[2] = {-1, -1};
    int efd = -1;
    int failed = 0;
    int status = 0;
    pid_t pid;
    long start_ms;
    long end_ms;

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        print_errno("notify-mix-socketpair");
        return 1;
    }
    if (pipe(pipefd) < 0) {
        print_errno("notify-mix-pipe");
        failed = 1;
        goto out_nokill;
    }
    efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (efd < 0) {
        print_errno("notify-mix-eventfd");
        failed = 1;
        goto out_nokill;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("notify-mix-fork");
        failed = 1;
        goto out_nokill;
    }

    if (pid == 0) {
        unsigned long long one = 1;

        close(sv[0]);
        close(pipefd[0]);
        usleep(100000);
        errno = 0;
        if (signal_fd == 0) {
            ssize_t n = write(sv[1], "S", 1);
            printf("kde_unix_socket_probe notify-mix-%s child-socket-write ret=%zd errno=%d %s\n",
                   label, n, errno, strerror(errno));
            fflush(stdout);
            _exit(n == 1 ? 0 : 20);
        }
        if (signal_fd == 1) {
            ssize_t n = write(pipefd[1], "P", 1);
            printf("kde_unix_socket_probe notify-mix-%s child-pipe-write ret=%zd errno=%d %s\n",
                   label, n, errno, strerror(errno));
            fflush(stdout);
            _exit(n == 1 ? 0 : 21);
        }

        ssize_t n = write(efd, &one, sizeof(one));
        printf("kde_unix_socket_probe notify-mix-%s child-eventfd-write ret=%zd errno=%d %s\n",
               label, n, errno, strerror(errno));
        fflush(stdout);
        _exit(n == (ssize_t)sizeof(one) ? 0 : 22);
    }

    close(sv[1]);
    sv[1] = -1;
    close(pipefd[1]);
    pipefd[1] = -1;

    pfds[0].fd = sv[0];
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = pipefd[0];
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;
    pfds[2].fd = efd;
    pfds[2].events = POLLIN;
    pfds[2].revents = 0;

    start_ms = monotonic_ms();
    errno = 0;
    int pret = poll(pfds, 3, 3000);
    end_ms = monotonic_ms();
    printf("kde_unix_socket_probe notify-mix-%s poll ret=%d elapsed_ms=%ld "
           "sock=0x%x pipe=0x%x eventfd=0x%x errno=%d %s\n",
           label, pret, (start_ms >= 0 && end_ms >= start_ms) ?
           end_ms - start_ms : -1, pfds[0].revents, pfds[1].revents,
           pfds[2].revents, errno, strerror(errno));
    if (pret < 1)
        failed = 1;
    if (signal_fd == 0 && !(pfds[0].revents & POLLIN))
        failed = 1;
    if (signal_fd == 1 && !(pfds[1].revents & POLLIN))
        failed = 1;
    if (signal_fd == 2 && !(pfds[2].revents & POLLIN))
        failed = 1;

    if (!failed) {
        if (signal_fd == 0)
            failed |= read(sv[0], &byte, 1) != 1 || byte != 'S';
        else if (signal_fd == 1)
            failed |= read(pipefd[0], &byte, 1) != 1 || byte != 'P';
        else {
            unsigned long long val = 0;
            failed |= read(efd, &val, sizeof(val)) != (ssize_t)sizeof(val) ||
                      val != 1;
        }
    }

    if (waitpid(pid, &status, 0) < 0) {
        print_errno("notify-mix-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe notify-mix-%s child-status=0x%x exited=%d code=%d\n",
               label, status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    goto out_nokill;

out_nokill:
    if (sv[0] >= 0)
        close(sv[0]);
    if (sv[1] >= 0)
        close(sv[1]);
    if (pipefd[0] >= 0)
        close(pipefd[0]);
    if (pipefd[1] >= 0)
        close(pipefd[1]);
    if (efd >= 0)
        close(efd);
    printf("kde_unix_socket_probe notify-mix-%s result=%s\n",
           label, failed ? "FAIL" : "PASS");
    return failed;
}

static int notify_mix(void)
{
    int failed = 0;

    failed |= notify_mix_one("socket-with-pipe-eventfd", 0);
    failed |= notify_mix_one("pipe-with-socket-eventfd", 1);
    failed |= notify_mix_one("eventfd-with-socket-pipe", 2);
    printf("kde_unix_socket_probe notify-mix result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

struct qt_dispatch_channel {
    const char *label;
    const char *path;
    int server;
    int client;
    int accepted;
};

static void qt_dispatch_channel_close(struct qt_dispatch_channel *ch)
{
    if (ch->accepted >= 0)
        close(ch->accepted);
    if (ch->client >= 0)
        close(ch->client);
    if (ch->server >= 0)
        close(ch->server);
    if (ch->path != NULL)
        unlink(ch->path);
    ch->accepted = -1;
    ch->client = -1;
    ch->server = -1;
}

static int qt_dispatch_prepare_parent_dir(const char *path)
{
    char dir[108];
    const char *slash = strrchr(path, '/');
    size_t len;

    if (slash == NULL || slash == path)
        return 0;
    len = (size_t)(slash - path);
    if (len >= sizeof(dir))
        return 0;
    memcpy(dir, path, len);
    dir[len] = '\0';
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
        printf("kde_unix_socket_probe qt-dispatch mkdir path=%s errno=%d %s\n",
               dir, errno, strerror(errno));
        return 1;
    }
    return 0;
}

static int qt_dispatch_channel_open(struct qt_dispatch_channel *ch,
                                    const char *label, const char *path)
{
    struct sockaddr_un sa;

    ch->label = label;
    ch->path = path;
    ch->server = -1;
    ch->client = -1;
    ch->accepted = -1;

    if (qt_dispatch_prepare_parent_dir(path))
        return 1;

    unlink(path);
    ch->server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (ch->server < 0) {
        print_errno("qt-dispatch-socket-server");
        goto fail;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    if (bind(ch->server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("qt-dispatch-bind");
        goto fail;
    }
    if (listen(ch->server, 1) < 0) {
        print_errno("qt-dispatch-listen");
        goto fail;
    }

    ch->client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (ch->client < 0) {
        print_errno("qt-dispatch-socket-client");
        goto fail;
    }
    if (connect(ch->client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("qt-dispatch-connect");
        goto fail;
    }

    ch->accepted = accept4(ch->server, NULL, NULL, SOCK_CLOEXEC);
    if (ch->accepted < 0) {
        print_errno("qt-dispatch-accept4");
        goto fail;
    }

    if (set_fd_nonblock(label, ch->client) ||
        set_fd_nonblock(label, ch->accepted))
        goto fail;

    printf("kde_unix_socket_probe qt-dispatch-%s pair path=%s client=%d accepted=%d\n",
           label, path, ch->client, ch->accepted);
    return 0;

fail:
    qt_dispatch_channel_close(ch);
    return 1;
}

static ssize_t qt_dispatch_sendmsg_split(int fd, const char *a, const char *b)
{
    struct iovec iov[2];
    struct msghdr msg;

    iov[0].iov_base = (void *)a;
    iov[0].iov_len = strlen(a);
    iov[1].iov_base = (void *)b;
    iov[1].iov_len = strlen(b);
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    errno = 0;
    return sendmsg(fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
}

static ssize_t qt_dispatch_recv_socket(const char *label, int fd,
                                       const char *expected)
{
    struct iovec iov;
    struct msghdr msg;
    char buf[32];
    ssize_t n;
    size_t expected_len = strlen(expected);

    memset(buf, 0, sizeof(buf));
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf) - 1;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    errno = 0;
    n = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    printf("kde_unix_socket_probe qt-dispatch-%s recvmsg ret=%zd flags=0x%x "
           "data=%.*s errno=%d %s\n",
           label, n, msg.msg_flags, n > 0 ? (int)n : 0, buf,
           errno, strerror(errno));
    if (n != (ssize_t)expected_len || memcmp(buf, expected, expected_len) != 0)
        return -1;
    return n;
}

static int qt_dispatch_drain_pipe(const char *label, int fd)
{
    char buf[16];
    ssize_t total = 0;

    for (;;) {
        errno = 0;
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            total += n;
            continue;
        }
        printf("kde_unix_socket_probe qt-dispatch-%s pipe-drain total=%zd "
               "last_ret=%zd errno=%d %s\n",
               label, total, n, errno, strerror(errno));
        return total > 0 && n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ?
            0 : 1;
    }
}

static int qt_dispatch_drain_eventfd(const char *label, int fd)
{
    unsigned long long val = 0;

    errno = 0;
    ssize_t n = read(fd, &val, sizeof(val));
    printf("kde_unix_socket_probe qt-dispatch-%s eventfd-drain ret=%zd "
           "value=%llu errno=%d %s\n",
           label, n, val, errno, strerror(errno));
    return n == (ssize_t)sizeof(val) && val > 0 ? 0 : 1;
}

static int qt_dispatch_poll_print(const char *label, const char *phase,
                                  struct pollfd *pfds, nfds_t nfds,
                                  int timeout_ms, int min_ready)
{
    long start_ms = monotonic_ms();
    errno = 0;
    int pret = poll(pfds, nfds, timeout_ms);
    long end_ms = monotonic_ms();

    printf("kde_unix_socket_probe qt-dispatch-%s %s poll ret=%d "
           "elapsed_ms=%ld errno=%d %s",
           label, phase, pret,
           (start_ms >= 0 && end_ms >= start_ms) ? end_ms - start_ms : -1,
           errno, strerror(errno));
    for (nfds_t i = 0; i < nfds; i++)
        printf(" fd%u=%d:0x%x", (unsigned int)i, pfds[i].fd,
               pfds[i].revents);
    printf("\n");
    if (pret < min_ready)
        return 1;
    return 0;
}

static int qt_dispatch_rearm_one(const char *label, const char *path,
                                 int use_eventfd)
{
    struct qt_dispatch_channel ch;
    struct pollfd pfds[2];
    int pipefd[2] = {-1, -1};
    int efd = -1;
    int failed = 0;
    int status = 0;
    pid_t pid = -1;

    if (qt_dispatch_channel_open(&ch, label, path))
        return 1;

    if (!use_eventfd) {
        if (pipe(pipefd) < 0) {
            print_errno("qt-dispatch-pipe");
            failed = 1;
            goto out_nokill;
        }
        if (set_fd_nonblock(label, pipefd[0]))
            failed = 1;
    } else {
        efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0) {
            print_errno("qt-dispatch-eventfd");
            failed = 1;
            goto out_nokill;
        }
    }
    if (failed)
        goto out_nokill;

    pid = fork();
    if (pid < 0) {
        print_errno("qt-dispatch-fork");
        failed = 1;
        goto out_nokill;
    }

    if (pid == 0) {
        unsigned long long one = 1;
        ssize_t n;

        usleep(100000);
        if (!use_eventfd) {
            errno = 0;
            n = write(pipefd[1], "P", 1);
            printf("kde_unix_socket_probe qt-dispatch-%s child-pipe-write ret=%zd errno=%d %s\n",
                   label, n, errno, strerror(errno));
            if (n != 1)
                _exit(30);
        } else {
            errno = 0;
            n = write(efd, &one, sizeof(one));
            printf("kde_unix_socket_probe qt-dispatch-%s child-eventfd-write ret=%zd errno=%d %s\n",
                   label, n, errno, strerror(errno));
            if (n != (ssize_t)sizeof(one))
                _exit(31);
        }

        usleep(250000);
        n = qt_dispatch_sendmsg_split(ch.accepted, use_eventfd ? "QD" : "WL",
                                      use_eventfd ? "B" : "A");
        printf("kde_unix_socket_probe qt-dispatch-%s child-sendmsg ret=%zd errno=%d %s\n",
               label, n, errno, strerror(errno));
        fflush(stdout);
        _exit(n == 3 ? 0 : 32);
    }

    pfds[0].fd = ch.client;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = use_eventfd ? efd : pipefd[0];
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;

    failed |= qt_dispatch_poll_print(label, "self-wake", pfds, 2, 3000, 1);
    if (!(pfds[1].revents & POLLIN) || (pfds[0].revents & POLLIN))
        failed = 1;
    if (!failed)
        failed |= use_eventfd ? qt_dispatch_drain_eventfd(label, efd) :
                  qt_dispatch_drain_pipe(label, pipefd[0]);

    pfds[0].revents = 0;
    pfds[1].revents = 0;
    failed |= qt_dispatch_poll_print(label, "socket-rearm", pfds, 2, 3000, 1);
    if (!(pfds[0].revents & POLLIN))
        failed = 1;
    if (!failed)
        failed |= qt_dispatch_recv_socket(label, ch.client,
                                          use_eventfd ? "QDB" : "WLA") < 0;

    if (waitpid(pid, &status, 0) < 0) {
        print_errno("qt-dispatch-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe qt-dispatch-%s child-status=0x%x exited=%d code=%d\n",
               label, status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    pid = -1;

out_nokill:
    if (pid > 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
    if (pipefd[0] >= 0)
        close(pipefd[0]);
    if (pipefd[1] >= 0)
        close(pipefd[1]);
    if (efd >= 0)
        close(efd);
    qt_dispatch_channel_close(&ch);
    printf("kde_unix_socket_probe qt-dispatch-%s result=%s\n",
           label, failed ? "FAIL" : "PASS");
    return failed;
}

static int qt_dispatch_combined(void)
{
    struct qt_dispatch_channel wl = {
        .label = NULL, .path = NULL, .server = -1, .client = -1,
        .accepted = -1
    };
    struct qt_dispatch_channel dbus = {
        .label = NULL, .path = NULL, .server = -1, .client = -1,
        .accepted = -1
    };
    struct pollfd pfds[4];
    int pipefd[2] = {-1, -1};
    int efd = -1;
    int failed = 0;
    int status = 0;
    int saw_wl = 0;
    int saw_pipe = 0;
    int saw_dbus = 0;
    int saw_eventfd = 0;
    pid_t pid = -1;

    if (qt_dispatch_channel_open(&wl, "combined-wayland",
                                 "/tmp/kde-qt-dispatch-wayland-0") ||
        qt_dispatch_channel_open(&dbus, "combined-dbus",
                                 "/tmp/kde-qt-dispatch-dbus/bus")) {
        failed = 1;
        goto out_nokill;
    }
    if (pipe(pipefd) < 0) {
        print_errno("qt-dispatch-combined-pipe");
        failed = 1;
        goto out_nokill;
    }
    if (set_fd_nonblock("combined", pipefd[0])) {
        failed = 1;
        goto out_nokill;
    }
    efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (efd < 0) {
        print_errno("qt-dispatch-combined-eventfd");
        failed = 1;
        goto out_nokill;
    }

    pid = fork();
    if (pid < 0) {
        print_errno("qt-dispatch-combined-fork");
        failed = 1;
        goto out_nokill;
    }

    if (pid == 0) {
        unsigned long long one = 1;
        ssize_t n;

        usleep(80000);
        errno = 0;
        n = write(pipefd[1], "P", 1);
        printf("kde_unix_socket_probe qt-dispatch-combined child-pipe-write ret=%zd errno=%d %s\n",
               n, errno, strerror(errno));
        if (n != 1)
            _exit(40);
        usleep(100000);
        n = qt_dispatch_sendmsg_split(wl.accepted, "W", "1");
        printf("kde_unix_socket_probe qt-dispatch-combined child-wayland-sendmsg ret=%zd errno=%d %s\n",
               n, errno, strerror(errno));
        if (n != 2)
            _exit(41);
        usleep(100000);
        errno = 0;
        n = write(efd, &one, sizeof(one));
        printf("kde_unix_socket_probe qt-dispatch-combined child-eventfd-write ret=%zd errno=%d %s\n",
               n, errno, strerror(errno));
        if (n != (ssize_t)sizeof(one))
            _exit(42);
        usleep(100000);
        n = qt_dispatch_sendmsg_split(dbus.accepted, "D", "1");
        printf("kde_unix_socket_probe qt-dispatch-combined child-dbus-sendmsg ret=%zd errno=%d %s\n",
               n, errno, strerror(errno));
        fflush(stdout);
        _exit(n == 2 ? 0 : 43);
    }

    pfds[0].fd = wl.client;
    pfds[0].events = POLLIN;
    pfds[1].fd = pipefd[0];
    pfds[1].events = POLLIN;
    pfds[2].fd = dbus.client;
    pfds[2].events = POLLIN;
    pfds[3].fd = efd;
    pfds[3].events = POLLIN;

    for (int iter = 0; iter < 8 &&
         !(saw_wl && saw_pipe && saw_dbus && saw_eventfd); iter++) {
        for (int i = 0; i < 4; i++)
            pfds[i].revents = 0;
        char phase[32];
        snprintf(phase, sizeof(phase), "combined-iter-%d", iter);
        if (qt_dispatch_poll_print("combined", phase, pfds, 4, 3000, 1)) {
            failed = 1;
            break;
        }
        if (pfds[0].revents & POLLIN) {
            saw_wl = 1;
            failed |= qt_dispatch_recv_socket("combined-wayland",
                                              wl.client, "W1") < 0;
        }
        if (pfds[1].revents & POLLIN) {
            saw_pipe = 1;
            failed |= qt_dispatch_drain_pipe("combined", pipefd[0]);
        }
        if (pfds[2].revents & POLLIN) {
            saw_dbus = 1;
            failed |= qt_dispatch_recv_socket("combined-dbus",
                                              dbus.client, "D1") < 0;
        }
        if (pfds[3].revents & POLLIN) {
            saw_eventfd = 1;
            failed |= qt_dispatch_drain_eventfd("combined", efd);
        }
        if (failed)
            break;
    }

    printf("kde_unix_socket_probe qt-dispatch-combined observed "
           "wayland=%d pipe=%d dbus=%d eventfd=%d\n",
           saw_wl, saw_pipe, saw_dbus, saw_eventfd);
    if (!(saw_wl && saw_pipe && saw_dbus && saw_eventfd))
        failed = 1;

    if (waitpid(pid, &status, 0) < 0) {
        print_errno("qt-dispatch-combined-waitpid");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe qt-dispatch-combined child-status=0x%x exited=%d code=%d\n",
               status, WIFEXITED(status) ? 1 : 0,
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            failed = 1;
    }
    pid = -1;

out_nokill:
    if (pid > 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }
    if (pipefd[0] >= 0)
        close(pipefd[0]);
    if (pipefd[1] >= 0)
        close(pipefd[1]);
    if (efd >= 0)
        close(efd);
    qt_dispatch_channel_close(&wl);
    qt_dispatch_channel_close(&dbus);
    rmdir("/tmp/kde-qt-dispatch-dbus");
    printf("kde_unix_socket_probe qt-dispatch-combined result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

static int qt_dispatch_mix(void)
{
    int failed = 0;

    failed |= qt_dispatch_rearm_one("wayland-pipe-rearm",
                                    "/tmp/kde-qt-dispatch-wayland-0", 0);
    failed |= qt_dispatch_rearm_one("qdbus-eventfd-rearm",
                                    "/tmp/kde-qt-dispatch-dbus/bus", 1);
    failed |= qt_dispatch_combined();
    printf("kde_unix_socket_probe qt-dispatch-mix result=%s\n",
           failed ? "FAIL" : "PASS");
    return failed;
}

int main(int argc, char **argv)
{
    const char *path = "/tmp/kde-unix-socket-probe.sock";
    struct sockaddr_un sa;
    char byte = 0;
    int server = -1;
    int client = -1;
    int accepted = -1;
    int failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc == 2 && strcmp(argv[1], "--scm-zero-readiness") == 0)
        return scm_zero_readiness() ? 1 : 0;
    if (argc == 2 && strcmp(argv[1], "--notify-mix") == 0)
        return notify_mix() ? 1 : 0;
    if (argc == 2 && strcmp(argv[1], "--qt-dispatch-mix") == 0)
        return qt_dispatch_mix() ? 1 : 0;
    if (argc != 1) {
        printf("kde_unix_socket_probe unknown-argument=%s\n", argv[1]);
        return 2;
    }

    unlink(path);

    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
        print_errno("socket-server");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);

    errno = 0;
    if (bind(server, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("bind");
        failed = 1;
        goto out;
    }
    printf("kde_unix_socket_probe bind path=%s ret=0\n", path);

    failed |= check_stat_socket(path, "after-bind");

    errno = 0;
    if (listen(server, 1) < 0) {
        print_errno("listen");
        failed = 1;
        goto out;
    }

    client = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0) {
        print_errno("socket-client");
        failed = 1;
        goto out;
    }

    errno = 0;
    if (connect(client, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        print_errno("connect");
        failed = 1;
        goto out;
    }
    printf("kde_unix_socket_probe connect ret=0\n");

    errno = 0;
    accepted = accept(server, NULL, NULL);
    if (accepted < 0) {
        print_errno("accept");
        failed = 1;
        goto out;
    }
    printf("kde_unix_socket_probe accept fd=%d\n", accepted);

    if (write(client, "K", 1) != 1) {
        print_errno("write-client");
        failed = 1;
        goto out;
    }
    if (read(accepted, &byte, 1) != 1 || byte != 'K') {
        print_errno("read-server");
        failed = 1;
        goto out;
    }
    printf("kde_unix_socket_probe data ret=0 byte=%c\n", byte);
    failed |= poll_roundtrip("/tmp/kde-unix-socket-probe-poll.sock");
    failed |= socketpair_sendmsg_ppoll_fd3();
    failed |= socketpair_fd3_child_ack();
    failed |= epoll_listen_roundtrip("/tmp/kde-unix-socket-probe-epoll.sock");
    failed |= epoll_connected_roundtrip("/tmp/kde-unix-socket-probe-epoll-connected.sock");
    failed |= epoll_listen_accept4_cloexec_roundtrip("/tmp/kde-unix-socket-probe-epoll-accept4.sock");
    failed |= epoll_aggregate_poll_roundtrip("/tmp/kde-unix-socket-probe-epoll-aggregate-poll.sock");
    failed |= epoll_aggregate_nested_roundtrip("/tmp/kde-unix-socket-probe-epoll-aggregate-nested.sock");
    failed |= wayland_msg_roundtrip("/tmp/kde-unix-socket-probe-wayland-msg.sock");
    failed |= epoll_aggregate_nested_edge_roundtrip("/tmp/kde-unix-socket-probe-epoll-aggregate-edge.sock");

out:
    if (accepted >= 0)
        close(accepted);
    if (client >= 0)
        close(client);
    if (server >= 0)
        close(server);

    if (!failed)
        failed |= check_stat_socket(path, "after-close");

    errno = 0;
    if (unlink(path) < 0) {
        print_errno("unlink");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe unlink ret=0\n");
    }

    errno = 0;
    if (stat(path, &(struct stat){0}) == 0 || errno != ENOENT) {
        print_errno("stat-after-unlink");
        failed = 1;
    } else {
        printf("kde_unix_socket_probe stat phase=after-unlink ret=-1 errno=%d ENOENT\n",
               errno);
    }

    printf("kde_unix_socket_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
