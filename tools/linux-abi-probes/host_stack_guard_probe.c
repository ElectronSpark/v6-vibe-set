// host_stack_guard_probe.c - exercise glibc TLS canary and env/stdio paths.
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <termios.h>
#include <unistd.h>

static uintptr_t read_fs_canary(void)
{
    uintptr_t value;
    __asm__ volatile("movq %%fs:0x28, %0" : "=r"(value));
    return value;
}

__attribute__((noinline))
static int protected_copy(const char *src)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", src);
    return strcmp(buf, src);
}

int main(int argc, char **argv, char **envp)
{
    unsigned char *random = (unsigned char *)getauxval(AT_RANDOM);
    uintptr_t canary = read_fs_canary();
    int envc = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 1 || argv == NULL || argv[0] == NULL) {
        printf("host_stack_guard_probe: bad argc/argv\n");
        return 1;
    }
    if (random == NULL) {
        printf("host_stack_guard_probe: missing AT_RANDOM\n");
        return 1;
    }
    if (canary == 0) {
        printf("host_stack_guard_probe: zero stack canary\n");
        return 1;
    }
    if ((canary & 0xff) != 0) {
        printf("host_stack_guard_probe: canary low byte not zero: 0x%lx\n",
               (unsigned long)canary);
        return 1;
    }
    if ((canary >> 8) != (*(uintptr_t *)random >> 8)) {
        printf("host_stack_guard_probe: canary/random mismatch canary=0x%lx random=0x%lx\n",
               (unsigned long)canary, (unsigned long)*(uintptr_t *)random);
        return 1;
    }
    if (protected_copy("stack-guard-smoke") != 0) {
        printf("host_stack_guard_probe: protected copy failed\n");
        return 1;
    }
    errno = 0;
    int tty = isatty(STDOUT_FILENO);
    if (tty < 0) {
        printf("host_stack_guard_probe: isatty failed errno=%d\n", errno);
        return 1;
    }
    if (tty) {
        struct termios tio;
        memset(&tio, 0xa5, sizeof(tio));
        if (tcgetattr(STDOUT_FILENO, &tio) != 0) {
            printf("host_stack_guard_probe: tcgetattr failed errno=%d\n",
                   errno);
            return 1;
        }
    }
    for (char **ep = envp; ep != NULL && *ep != NULL; ep++)
        envc++;
    printf("host_stack_guard_probe: argv0=%s envc=%d tty=%d canary=0x%lx OK\n",
           argv[0], envc, tty, (unsigned long)canary);
    return 0;
}
