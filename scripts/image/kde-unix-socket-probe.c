#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
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

int main(void)
{
    const char *path = "/tmp/kde-unix-socket-probe.sock";
    struct sockaddr_un sa;
    char byte = 0;
    int server = -1;
    int client = -1;
    int accepted = -1;
    int failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
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
