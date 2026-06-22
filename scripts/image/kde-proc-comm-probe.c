#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int read_file(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ssize_t n = read(fd, buf, size - 1);
    int saved = errno;
    close(fd);
    errno = saved;
    if (n < 0)
        return -1;

    buf[n] = '\0';
    return 0;
}

static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;

    ssize_t n = write(fd, value, strlen(value));
    int saved = errno;
    close(fd);
    errno = saved;
    return n == (ssize_t)strlen(value) ? 0 : -1;
}

static void strip_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

int main(void)
{
    long tid = syscall(SYS_gettid);
    char self_comm[64];
    char task_comm[64];
    char path[96];
    const char *probe = "xv6-pw-thread";

    if (tid <= 0) {
        printf("kde_proc_comm_probe result=FAIL phase=gettid errno=%d\n", errno);
        return 1;
    }

    if (read_file("/proc/self/comm", self_comm, sizeof(self_comm)) < 0) {
        printf("kde_proc_comm_probe result=FAIL phase=read-self errno=%d\n", errno);
        return 1;
    }
    strip_newline(self_comm);

    snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", tid);
    if (write_file(path, probe) < 0) {
        printf("kde_proc_comm_probe result=FAIL phase=write-task errno=%d path=%s\n",
               errno, path);
        return 1;
    }

    if (read_file(path, task_comm, sizeof(task_comm)) < 0) {
        printf("kde_proc_comm_probe result=FAIL phase=read-task errno=%d path=%s\n",
               errno, path);
        return 1;
    }
    strip_newline(task_comm);

    if (strcmp(task_comm, probe) != 0) {
        printf("kde_proc_comm_probe result=FAIL phase=verify-task got=%s expected=%s\n",
               task_comm, probe);
        return 1;
    }

    if (write_file("/proc/self/comm", self_comm) < 0) {
        printf("kde_proc_comm_probe result=FAIL phase=restore errno=%d\n", errno);
        return 1;
    }

    printf("kde_proc_comm_probe result=PASS tid=%ld name=%s\n", tid, task_comm);
    return 0;
}
