#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * Build as an ordinary Linux ELF; no PulseAudio or graphics dependencies.
 * Usage: unix-passcred-stream-reducer [rounds [timeout-seconds]]
 *
 * The receiver races write()/writev()/send(), then waits for a separate DONE token
 * proving that the send syscall returned. The sender waits for ACK before
 * writing again. A receiver that consumed bytes before their credentials
 * were queued can therefore encounter the orphaned control message here.
 * The parent retains a second reference to the sending endpoint throughout
 * the test: zero from a positive-length recvmsg cannot be genuine EOF.
 * Race coverage is probabilistic; repeat on multiple CPUs when qualifying a
 * fix. No timing threshold or message-boundary behavior is assumed.
 */

#define PREFIX "UNIX_PASSCRED_STREAM_V1"
#define MAX_PAYLOAD 127

static volatile sig_atomic_t expired;
static volatile sig_atomic_t reaping;
static volatile sig_atomic_t owned_child;
static int64_t deadline_ms;

struct counters {
    unsigned long rounds;
    unsigned long reads;
    unsigned long credentials;
    unsigned long empty_checks;
    unsigned long bytes;
    int invalid;
};

static void stop_handler(int signo)
{
    (void)signo;
    expired = 1;
    if (owned_child > 0)
        kill((pid_t)owned_child, SIGKILL);
    if (reaping) {
        static const char failure[] = PREFIX " status=FAIL reason=reap-timeout\n";
        ssize_t ignored = write(STDERR_FILENO, failure, sizeof(failure) - 1);
        (void)ignored;
        _exit(124);
    }
}

static int64_t now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int within_deadline(void)
{
    int64_t now = now_ms();

    if (!expired && now >= 0 && now < deadline_ms)
        return 1;
    errno = ETIMEDOUT;
    return 0;
}

static int token_write(int fd, unsigned char token)
{
    ssize_t n;

    do {
        n = write(fd, &token, 1);
    } while (n < 0 && errno == EINTR && !expired);
    return n == 1 ? 0 : -1;
}

static int token_read(int fd, unsigned char expected)
{
    unsigned char token = 0;
    ssize_t n;

    do {
        n = read(fd, &token, 1);
    } while (n < 0 && errno == EINTR && !expired);
    if (n == 1 && token == expected)
        return 0;
    if (n >= 0)
        errno = EPROTO;
    return -1;
}

static unsigned char payload_byte(unsigned long round, size_t offset)
{
    return (unsigned char)((round * 29UL + offset * 71UL + 13UL) & 255UL);
}

static size_t payload_size(unsigned long round)
{
    return 1 + (round * 37UL) % MAX_PAYLOAD;
}

static int send_payload(int fd, const unsigned char *data, size_t size,
                        unsigned method)
{
    size_t done = 0;

    while (done < size) {
        ssize_t n;

        if (method == 2) {
            n = send(fd, data + done, size - done, MSG_NOSIGNAL | MSG_DONTWAIT);
        } else if (method == 1) {
            size_t first = (size - done) / 2;
            struct iovec iov[2] = {
                { .iov_base = (void *)(data + done), .iov_len = first },
                { .iov_base = (void *)(data + done + first),
                  .iov_len = size - done - first }
            };
            n = writev(fd, iov, 2);
        } else {
            n = write(fd, data + done, size - done);
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            int rc = poll(&pfd, 1, 100);

            if (rc >= 0 || errno == EINTR)
                continue;
        }
        if (n <= 0)
            return -1;
        done += (size_t)n;
    }
    return 0;
}

static void sender(int fd, int done_fd, int ack_fd, unsigned long rounds,
                   unsigned seconds)
{
    unsigned char data[MAX_PAYLOAD];

    owned_child = 0;
    signal(SIGALRM, SIG_DFL);
    alarm(seconds + 2);
    for (unsigned long round = 0; round < rounds; ++round) {
        size_t size = payload_size(round);

        for (size_t i = 0; i < size; ++i)
            data[i] = payload_byte(round, i);
        if (send_payload(fd, data, size, (unsigned)(round % 3UL)) < 0 ||
            token_write(done_fd, 'D') < 0 || token_read(ack_fd, 'A') < 0)
            _exit(10);
    }
    if (token_read(ack_fd, 'Q') < 0)
        _exit(11);
    close(fd);
    close(done_fd);
    close(ack_fd);
    _exit(0);
}

static ssize_t receive(int fd, unsigned char *data, size_t size, pid_t pid,
                       struct counters *count)
{
    union {
        struct cmsghdr alignment;
        unsigned char bytes[4 * CMSG_SPACE(sizeof(struct ucred))];
    } control;
    struct iovec iov = { .iov_base = data, .iov_len = size };
    struct msghdr msg;
    ssize_t n;

    memset(&msg, 0, sizeof(msg));
    memset(&control, 0, sizeof(control));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.bytes;
    msg.msg_controllen = sizeof(control.bytes);
    n = recvmsg(fd, &msg, 0);
    if (n < 0)
        return n;
    ++count->reads;
    if (msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC))
        count->invalid = 1;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        struct ucred cred;

        if (cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_CREDENTIALS ||
            cmsg->cmsg_len != CMSG_LEN(sizeof(cred))) {
            count->invalid = 1;
            continue;
        }
        memcpy(&cred, CMSG_DATA(cmsg), sizeof(cred));
        /* Credentials attached to genuine EOF do not identify a sender. */
        if (n == 0 && pid < 0)
            continue;
        if (cred.pid != pid || cred.uid != getuid() || cred.gid != getgid()) {
            printf(PREFIX " phase=credentials status=FAIL pid=%ld expected_pid=%ld"
                   " uid=%lu gid=%lu\n", (long)cred.pid, (long)pid,
                   (unsigned long)cred.uid, (unsigned long)cred.gid);
            count->invalid = 1;
        }
        ++count->credentials;
    }
    return n;
}

static int wait_readable(int fd)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };

    while (within_deadline()) {
        int rc = poll(&pfd, 1, 100);

        if (rc > 0) {
            if (pfd.revents & POLLNVAL) {
                errno = EBADF;
                return -1;
            }
            return 0;
        }
        if (rc < 0 && errno != EINTR)
            return -1;
    }
    return -1;
}

static unsigned long parse_number(const char *text, unsigned long maximum)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || !text[0] || *end || value == 0 || value > maximum)
        return 0;
    return value;
}

int main(int argc, char **argv)
{
    unsigned long rounds = 4096;
    unsigned long seconds = 30;
    struct counters count = {0};
    struct sigaction action;
    int pair[2] = {-1, -1}, done[2] = {-1, -1}, ack[2] = {-1, -1};
    int passcred = 1, status = 0, failed = 1, eof = 0;
    pid_t child = -1;
    const char *reason = "setup";
    unsigned char data[MAX_PAYLOAD];

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 3 || (argc > 1 && !(rounds = parse_number(argv[1], 1000000))) ||
        (argc > 2 && !(seconds = parse_number(argv[2], 300)))) {
        fprintf(stderr, "usage: %s [rounds=1..1000000 [timeout-seconds=1..300]]\n",
                argv[0]);
        return 2;
    }
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = stop_handler;
    if (sigaction(SIGALRM, &action, NULL) < 0 ||
        sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0)
        goto out;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) < 0)
        goto out;
    deadline_ms = now_ms();
    if (deadline_ms < 0)
        goto out;
    deadline_ms += (int64_t)seconds * 1000;
    alarm((unsigned)seconds);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0 ||
        setsockopt(pair[0], SOL_SOCKET, SO_PASSCRED, &passcred, sizeof(passcred)) < 0 ||
        fcntl(pair[0], F_SETFL, O_NONBLOCK) < 0 || pipe(done) < 0 || pipe(ack) < 0)
        goto out;
    child = fork();
    if (child < 0)
        goto out;
    if (child == 0) {
        close(pair[0]);
        close(done[0]);
        close(ack[1]);
        sender(pair[1], done[1], ack[0], rounds, (unsigned)seconds);
    }
    owned_child = (sig_atomic_t)child;
    close(done[1]); done[1] = -1;
    close(ack[0]); ack[0] = -1;
    printf(PREFIX " phase=start rounds=%lu timeout_seconds=%lu sender_pid=%ld"
           " methods=write,writev,send_nonblocking peer_reference_retained=1\n",
           rounds, seconds, (long)child);

    for (unsigned long round = 0; round < rounds; ++round) {
        size_t expected = payload_size(round), received = 0;
        unsigned long credentials_before = count.credentials;

        while (received < expected) {
            ssize_t n;

            reason = "receive-timeout";
            if (!within_deadline())
                goto out;
            n = receive(pair[0], data, expected - received, child, &count);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (wait_readable(pair[0]) < 0)
                    goto out;
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            reason = n == 0 ? "false-eof-during-data" : "recvmsg";
            if (n <= 0)
                goto out;
            reason = "credentials-invalid";
            if (count.invalid)
                goto out;
            for (ssize_t i = 0; i < n; ++i) {
                reason = "data-mismatch";
                if (data[i] != payload_byte(round, received + (size_t)i))
                    goto out;
            }
            received += (size_t)n;
            count.bytes += (unsigned long)n;
        }
        reason = "sender-done";
        if (token_read(done[0], 'D') < 0)
            goto out;
        /* The sender has returned from write and is waiting for ACK. */
        struct pollfd drained = { .fd = pair[0], .events = POLLIN };
        reason = "empty-poll";
        if (poll(&drained, 1, 0) < 0)
            goto out;
        ssize_t n = receive(pair[0], data, sizeof(data), child, &count);
        ++count.empty_checks;
        if (n >= 0) {
            printf(PREFIX " phase=drained round=%lu recv=%ld poll_revents=0x%x"
                   " peer_reference_retained=1\n", round, (long)n, drained.revents);
            reason = n == 0 ? "false-eof-after-write" : "unexpected-data";
            goto out;
        }
        reason = "empty-recvmsg";
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            goto out;
        reason = "credentials-missing";
        if (count.credentials == credentials_before)
            goto out;
        reason = "sender-ack";
        if (token_write(ack[1], 'A') < 0)
            goto out;
        ++count.rounds;
    }
    reason = "sender-close-token";
    if (token_write(ack[1], 'Q') < 0)
        goto out;
    reason = "sender-reap";
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR && !expired);
    if (waited != child)
        goto out;
    child = -1;
    owned_child = 0;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        goto out;
    close(pair[1]); pair[1] = -1;
    reason = "true-eof";
    if (wait_readable(pair[0]) < 0 ||
        receive(pair[0], data, sizeof(data), (pid_t)-1, &count) != 0)
        goto out;
    eof = 1;
    failed = 0;
    reason = "complete";

out:
    {
        int saved_errno = errno;
        if (child > 0) {
            kill(child, SIGKILL);
            reaping = 1;
            alarm(5);
            while (waitpid(child, &status, 0) < 0) {
                if (errno == EINTR)
                    continue;
                reason = "cleanup-waitpid";
                break;
            }
            owned_child = 0;
            reaping = 0;
        }
        alarm(0);
        for (size_t i = 0; i < 2; ++i) {
            if (pair[i] >= 0) close(pair[i]);
            if (done[i] >= 0) close(done[i]);
            if (ack[i] >= 0) close(ack[i]);
        }
        printf(PREFIX " status=%s reason=%s rounds=%lu bytes=%lu recv_calls=%lu"
               " credentials=%lu empty_checks=%lu true_eof=%d timed_out=%d"
               " child_status=0x%x errno=%d\n",
               failed ? "FAIL" : "PASS", reason, count.rounds, count.bytes,
               count.reads, count.credentials, count.empty_checks, eof,
               (int)expired, status, saved_errno);
    }
    return failed ? 1 : 0;
}
