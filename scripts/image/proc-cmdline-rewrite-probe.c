#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static int read_cmdline(char *buf, size_t size)
{
    int fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, size - 1);
    int saved = errno;
    close(fd);
    if (n < 0) {
        errno = saved;
        return -1;
    }
    buf[n] = '\0';
    return (int)n;
}

static void print_cmdline(const char *phase, const char *buf, int len)
{
    printf("proc-cmdline-rewrite-probe phase=%s len=%d text=\"", phase, len);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c == '\0')
            putchar('|');
        else if (c >= 32 && c < 127)
            putchar(c);
        else
            printf("\\x%02x", c);
    }
    printf("\"\n");
}

int main(int argc, char **argv)
{
    char before[1024];
    char after[1024];
    int before_len = read_cmdline(before, sizeof(before));
    if (before_len < 0) {
        perror("proc-cmdline-rewrite-probe before");
        return 1;
    }
    print_cmdline("before", before, before_len);

    char *start = argv[0];
    char *argv_end = argv[argc - 1] + strlen(argv[argc - 1]) + 1;
    char *title_end = argv_end;
    if (start == NULL || argv_end <= start) {
        fprintf(stderr, "proc-cmdline-rewrite-probe: invalid argv span\n");
        return 1;
    }

    for (char **ep = environ; ep != NULL && *ep != NULL; ep++) {
        if (*ep < start)
            continue;
        char *env_end = *ep + strlen(*ep) + 1;
        if (env_end > title_end)
            title_end = env_end;
    }

    size_t argv_span = (size_t)(argv_end - start);
    size_t title_span = (size_t)(title_end - start);
    const char head[] = "proc-cmdline-live-long-probe";
    const char tail[] = " --type=renderer marker=env-span";
    size_t head_len = strlen(head);
    size_t tail_len = strlen(tail);
    if (title_span <= argv_span + tail_len + 1 ||
        argv_span <= head_len + 1) {
        fprintf(stderr,
                "proc-cmdline-rewrite-probe: insufficient title span argv=%zu title=%zu\n",
                argv_span, title_span);
        return 1;
    }

    memset(start, 0, title_span);
    memset(start, ' ', argv_span + tail_len);
    memcpy(start, head, head_len);
    memcpy(start + argv_span, tail, tail_len);
    start[argv_span + tail_len] = '\0';

    int after_len = read_cmdline(after, sizeof(after));
    if (after_len < 0) {
        perror("proc-cmdline-rewrite-probe after");
        return 1;
    }
    print_cmdline("after", after, after_len);

    if (memmem(after, (size_t)after_len, "--type=renderer", 15) == NULL) {
        printf("proc-cmdline-rewrite-probe result=FAIL reason=stale-cmdline argv_span=%zu title_span=%zu\n",
               argv_span, title_span);
        return 2;
    }

    printf("proc-cmdline-rewrite-probe result=PASS argv_span=%zu title_span=%zu\n",
           argv_span, title_span);
    return 0;
}
