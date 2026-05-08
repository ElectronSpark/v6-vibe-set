// host_startup_probe.c - Linux-built startup ABI probe for xv6.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <unistd.h>

static unsigned char file_buf[4096];

static int read_file(const char *path, unsigned char *buf, size_t cap,
                     ssize_t *out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("host_startup_probe: open %s failed: %d\n", path, errno);
        return -1;
    }
    ssize_t n = read(fd, buf, cap);
    int saved = errno;
    close(fd);
    if (n < 0) {
        printf("host_startup_probe: read %s failed: %d\n", path, saved);
        return -1;
    }
    *out = n;
    return 0;
}

static int auxv_blob_has(const uint64_t *auxv, int pairs, uint64_t tag)
{
    for (int i = 0; i < pairs; i++) {
        if (auxv[i * 2] == AT_NULL)
            break;
        if (auxv[i * 2] == tag)
            return 1;
    }
    return 0;
}

static int require_aux(unsigned long tag, const char *name)
{
    errno = 0;
    unsigned long value = getauxval(tag);
    if (value == 0 && errno != 0) {
        printf("host_startup_probe: missing %s\n", name);
        return -1;
    }
    printf("host_startup_probe: %s=0x%lx\n", name, value);
    return 0;
}

int main(int argc, char **argv, char **envp)
{
    ssize_t n = 0;
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("host_startup_probe: argc=%d argv0=%s env0=%s\n",
           argc, argv[0] ? argv[0] : "(null)",
           envp && envp[0] ? envp[0] : "(none)");

    failures += require_aux(AT_RANDOM, "AT_RANDOM") != 0;
    failures += require_aux(AT_EXECFN, "AT_EXECFN") != 0;
    failures += require_aux(AT_PLATFORM, "AT_PLATFORM") != 0;
    failures += require_aux(AT_CLKTCK, "AT_CLKTCK") != 0;
    failures += require_aux(AT_HWCAP, "AT_HWCAP") != 0;
    failures += require_aux(AT_HWCAP2, "AT_HWCAP2") != 0;

    if (read_file("/proc/self/cmdline", file_buf, sizeof(file_buf), &n) != 0 ||
        n <= 0 || file_buf[n - 1] != '\0') {
        printf("host_startup_probe: /proc/self/cmdline invalid\n");
        failures++;
    } else {
        printf("host_startup_probe: cmdline bytes=%ld argv0=%s\n", n, file_buf);
    }

    if (read_file("/proc/self/environ", file_buf, sizeof(file_buf), &n) != 0) {
        failures++;
    } else {
        printf("host_startup_probe: environ bytes=%ld\n", n);
    }

    if (read_file("/proc/self/auxv", file_buf, sizeof(file_buf), &n) != 0 ||
        n <= 0 || (n & 15) != 0) {
        printf("host_startup_probe: /proc/self/auxv invalid size=%ld\n", n);
        failures++;
    } else {
        const uint64_t *auxv = (const uint64_t *)file_buf;
        int pairs = (int)(n / (ssize_t)(sizeof(uint64_t) * 2));
        if (!auxv_blob_has(auxv, pairs, AT_RANDOM) ||
            !auxv_blob_has(auxv, pairs, AT_EXECFN) ||
            !auxv_blob_has(auxv, pairs, AT_PLATFORM) ||
            !auxv_blob_has(auxv, pairs, AT_CLKTCK) ||
            !auxv_blob_has(auxv, pairs, AT_HWCAP) ||
            !auxv_blob_has(auxv, pairs, AT_HWCAP2)) {
            printf("host_startup_probe: /proc/self/auxv missing tags\n");
            failures++;
        }
        printf("host_startup_probe: auxv bytes=%ld\n", n);
    }

    if (failures != 0) {
        printf("host_startup_probe: FAIL failures=%d\n", failures);
        return 1;
    }
    printf("host_startup_probe: OK\n");
    return 0;
}
