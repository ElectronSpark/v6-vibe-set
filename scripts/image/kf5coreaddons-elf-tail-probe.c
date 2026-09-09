#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define POISON_PAGE_COUNT 128
#define SLOT_STATIC_PLUGIN_A 0xb9298UL
#define SLOT_STATIC_PLUGIN_B 0xb92a0UL

struct kf5_mapping {
    uintptr_t base;
    char path[512];
};

static sigjmp_buf fault_env;
static volatile sig_atomic_t fault_armed;
static volatile sig_atomic_t fault_signal;

static void
fault_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)info;
    (void)ucontext;
    fault_signal = sig;
    if (fault_armed)
        siglongjmp(fault_env, 1);
    _exit(128 + sig);
}

static int
install_fault_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fault_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, NULL) < 0)
        return -1;
    if (sigaction(SIGBUS, &sa, NULL) < 0)
        return -1;
    if (sigaction(SIGILL, &sa, NULL) < 0)
        return -1;
    if (sigaction(SIGABRT, &sa, NULL) < 0)
        return -1;
    return 0;
}

static long
page_size_or_default(void)
{
    long page_size = sysconf(_SC_PAGESIZE);

    return page_size > 0 ? page_size : 4096;
}

static void
fill_poison_page(char *page, size_t page_size)
{
    static const char poison[] = "/x86_64-/usr/lib";
    size_t offset = 0;

    while (offset < page_size) {
        size_t chunk = sizeof(poison) - 1;

        if (chunk > page_size - offset)
            chunk = page_size - offset;
        memcpy(page + offset, poison, chunk);
        offset += chunk;
    }
}

static int
run_poison_mode(void)
{
    const char *env = getenv("KF5_ELF_TAIL_POISON");
    size_t page_size = (size_t)page_size_or_default();
    int failures = 0;

    if (env && env[0] && strcmp(env, "0") == 0)
        return 0;

    printf("KF5_ELF_TAIL_PROBE_POISON:start pages=%d page_size=%zu pattern=/x86_64-/usr/lib\n",
           POISON_PAGE_COUNT, page_size);
    for (size_t i = 0; i < POISON_PAGE_COUNT; i++) {
        void *page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (page == MAP_FAILED) {
            failures++;
            printf("KF5_ELF_TAIL_PROBE_POISON:mmap_fail index=%zu errno=%d %s\n",
                   i, errno, strerror(errno));
            continue;
        }
        fill_poison_page(page, page_size);
        if (munmap(page, page_size) < 0) {
            failures++;
            printf("KF5_ELF_TAIL_PROBE_POISON:munmap_fail index=%zu ptr=%p errno=%d %s\n",
                   i, page, errno, strerror(errno));
        }
    }
    printf("KF5_ELF_TAIL_PROBE_POISON:end failures=%d\n", failures);
    return failures == 0 ? 0 : -1;
}

static int
is_kf5_line(const char *line)
{
    return strstr(line, "libKF5CoreAddons.so.5") != NULL;
}

static void
dump_relevant_maps(const char *phase)
{
    FILE *fp;
    char line[1024];
    int count = 0;

    printf("KF5_ELF_TAIL_PROBE_PHASE:%s_maps_begin\n", phase);
    fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        printf("KF5_ELF_TAIL_PROBE_MAPS_OPEN_FAIL:%s errno=%d %s\n",
               phase, errno, strerror(errno));
        printf("KF5_ELF_TAIL_PROBE_PHASE:%s_maps_end count=0\n", phase);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (!is_kf5_line(line))
            continue;
        count++;
        printf("KF5_ELF_TAIL_PROBE_MAP:%s:%s", phase, line);
        if (line[0] && line[strlen(line) - 1] != '\n')
            putchar('\n');
    }
    fclose(fp);
    printf("KF5_ELF_TAIL_PROBE_PHASE:%s_maps_end count=%d\n", phase, count);
}

static int
copy_map_path(char *out, size_t out_size, const char *line)
{
    const char *path = strchr(line, '/');
    size_t len;

    if (!path || out_size == 0)
        return -1;
    len = strcspn(path, "\r\n");
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int
find_kf5_mapping(struct kf5_mapping *mapping)
{
    FILE *fp;
    char line[1024];

    memset(mapping, 0, sizeof(*mapping));
    fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:maps_open errno=%d %s\n",
               errno, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        unsigned long offset = 0;
        char perms[8];

        if (!is_kf5_line(line))
            continue;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %lx", &start,
                   &end, perms, &offset) != 4)
            continue;
        (void)end;
        if (offset == 0) {
            mapping->base = start;
            if (copy_map_path(mapping->path, sizeof(mapping->path), line) < 0) {
                fclose(fp);
                printf("KF5_ELF_TAIL_PROBE_FAIL:map_path_missing line=%s",
                       line);
                return -1;
            }
            fclose(fp);
            printf("KF5_ELF_TAIL_PROBE_BASE:base=0x%016" PRIxPTR
                   " path=%s\n",
                   mapping->base, mapping->path);
            return 0;
        }
    }
    fclose(fp);
    printf("KF5_ELF_TAIL_PROBE_FAIL:base_not_found\n");
    return -1;
}

static int
offset_in_range(uintptr_t offset, const Elf64_Phdr *ph)
{
    uintptr_t tail_start = (uintptr_t)ph->p_vaddr + (uintptr_t)ph->p_filesz;
    uintptr_t tail_end = (uintptr_t)ph->p_vaddr + (uintptr_t)ph->p_memsz;

    return offset >= tail_start && offset + 32 <= tail_end;
}

static int
validate_elf_tail_slot(FILE *fp, const char *path, const Elf64_Ehdr *ehdr,
                       uintptr_t offset, const char *label)
{
    int found = 0;

    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr ph;
        long phoff = (long)(ehdr->e_phoff + (Elf64_Off)i * ehdr->e_phentsize);
        uintptr_t tail_start;
        uintptr_t tail_end;

        if (fseek(fp, phoff, SEEK_SET) != 0 ||
            fread(&ph, 1, sizeof(ph), fp) != sizeof(ph)) {
            printf("KF5_ELF_TAIL_PROBE_FAIL:phdr_read label=%s path=%s index=%u errno=%d %s\n",
                   label, path, (unsigned)i, errno, strerror(errno));
            return -1;
        }
        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_W))
            continue;

        tail_start = (uintptr_t)ph.p_vaddr + (uintptr_t)ph.p_filesz;
        tail_end = (uintptr_t)ph.p_vaddr + (uintptr_t)ph.p_memsz;
        printf("KF5_ELF_TAIL_PROBE_ELF_LOAD:index=%u label=%s vaddr=0x%lx filesz=0x%lx memsz=0x%lx tail=[0x%lx,0x%lx) offset=0x%lx contains=%d\n",
               (unsigned)i, label, (unsigned long)ph.p_vaddr,
               (unsigned long)ph.p_filesz, (unsigned long)ph.p_memsz,
               (unsigned long)tail_start, (unsigned long)tail_end,
               (unsigned long)offset, offset_in_range(offset, &ph));
        if (offset_in_range(offset, &ph))
            found = 1;
    }

    if (!found) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:slot_not_in_rw_zero_tail label=%s path=%s offset=0x%lx\n",
               label, path, (unsigned long)offset);
        return -1;
    }
    return 0;
}

static int
validate_elf_tail_layout(const char *path)
{
    FILE *fp;
    Elf64_Ehdr ehdr;
    int ret = 0;

    fp = fopen(path, "rb");
    if (!fp) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:elf_open path=%s errno=%d %s\n",
               path, errno, strerror(errno));
        return -1;
    }
    if (fread(&ehdr, 1, sizeof(ehdr), fp) != sizeof(ehdr)) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:elf_header_read path=%s errno=%d %s\n",
               path, errno, strerror(errno));
        fclose(fp);
        return -1;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:elf_shape path=%s class=%u phentsize=%u\n",
               path, ehdr.e_ident[EI_CLASS], ehdr.e_phentsize);
        fclose(fp);
        return -1;
    }

    if (validate_elf_tail_slot(fp, path, &ehdr, SLOT_STATIC_PLUGIN_A,
                               "static_plugin_a") < 0)
        ret = -1;
    if (validate_elf_tail_slot(fp, path, &ehdr, SLOT_STATIC_PLUGIN_B,
                               "static_plugin_b") < 0)
        ret = -1;

    fclose(fp);
    if (ret == 0)
        printf("KF5_ELF_TAIL_PROBE_ELF_LAYOUT:status=PASS path=%s\n", path);
    return ret;
}

static void
format_ascii(char *out, size_t out_size, const unsigned char *bytes,
             size_t byte_count)
{
    size_t pos = 0;

    if (out_size == 0)
        return;
    for (size_t i = 0; i < byte_count && pos + 1 < out_size; i++) {
        unsigned char c = bytes[i];

        out[pos++] = isprint(c) ? (char)c : '.';
    }
    out[pos] = '\0';
}

static int
slot_has_path_poison(const unsigned char *bytes, size_t byte_count)
{
    static const char poison[] = "/x86_64-";

    for (size_t i = 0; i + sizeof(poison) - 1 <= byte_count; i++) {
        if (memcmp(bytes + i, poison, sizeof(poison) - 1) == 0)
            return 1;
    }
    return 0;
}

static int
slot_is_zero(const unsigned char *bytes, size_t byte_count)
{
    for (size_t i = 0; i < byte_count; i++) {
        if (bytes[i] != 0)
            return 0;
    }
    return 1;
}

static int
read_slot(const char *label, uintptr_t base, uintptr_t offset)
{
    unsigned char bytes[32];
    char ascii[sizeof(bytes) + 1];
    uintptr_t addr = base + offset;
    int zero;
    int poison;

    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        memcpy(bytes, (const void *)addr, sizeof(bytes));
        fault_armed = 0;
    } else {
        fault_armed = 0;
        printf("KF5_ELF_TAIL_PROBE_SLOT:%s status=FAIL reason=signal_%d addr=0x%016" PRIxPTR "\n",
               label, (int)fault_signal, addr);
        return 1;
    }

    format_ascii(ascii, sizeof(ascii), bytes, sizeof(bytes));
    zero = slot_is_zero(bytes, sizeof(bytes));
    poison = slot_has_path_poison(bytes, sizeof(bytes));
    printf("KF5_ELF_TAIL_PROBE_SLOT:%s status=%s addr=0x%016" PRIxPTR
           " offset=0x%lx zero=%d path_poison=%d"
           " bytes=%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x"
           " ascii=\"%s\"\n",
           label, zero && !poison ? "PASS" : "FAIL", addr,
           (unsigned long)offset,
           zero, poison, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
           bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10],
           bytes[11], bytes[12], bytes[13], bytes[14], bytes[15], ascii);
    return zero && !poison ? 0 : 1;
}

int
main(void)
{
    const char *ld_library_path;
    const char *err;
    void *handle;
    struct kf5_mapping mapping;
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("KF5_ELF_TAIL_PROBE_PHASE:start\n");
    ld_library_path = getenv("LD_LIBRARY_PATH");
    printf("KF5_ELF_TAIL_PROBE_ENV:LD_LIBRARY_PATH=%s\n",
           ld_library_path ? ld_library_path : "(unset)");
    dump_relevant_maps("start");

    if (install_fault_handlers() < 0) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:sigaction errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    if (run_poison_mode() < 0) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:poison\n");
        return 1;
    }

    printf("KF5_ELF_TAIL_PROBE_PHASE:dlopen_libKF5CoreAddons\n");
    dlerror();
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        handle = dlopen("libKF5CoreAddons.so.5", RTLD_NOW | RTLD_LOCAL);
        fault_armed = 0;
    } else {
        fault_armed = 0;
        printf("KF5_ELF_TAIL_PROBE_FAIL:dlopen_signal_%d\n",
               (int)fault_signal);
        return 1;
    }
    err = dlerror();
    if (!handle) {
        printf("KF5_ELF_TAIL_PROBE_DLOPEN:libKF5CoreAddons.so.5 handle=NULL error=%s\n",
               err ? err : "");
        printf("KF5_ELF_TAIL_PROBE_FAIL:dlopen_libKF5CoreAddons\n");
        return 1;
    }
    printf("KF5_ELF_TAIL_PROBE_DLOPEN:libKF5CoreAddons.so.5 handle=%p\n",
           handle);
    dump_relevant_maps("after_dlopen");

    if (find_kf5_mapping(&mapping) < 0)
        return 1;
    if (validate_elf_tail_layout(mapping.path) < 0)
        return 1;
    failures += read_slot("static_plugin_a", mapping.base,
                          SLOT_STATIC_PLUGIN_A);
    failures += read_slot("static_plugin_b", mapping.base,
                          SLOT_STATIC_PLUGIN_B);

    if (failures != 0) {
        printf("KF5_ELF_TAIL_PROBE_FAIL:nonzero_tail failures=%d\n",
               failures);
        return 1;
    }
    printf("KF5_ELF_TAIL_PROBE_PASS\n");
    return 0;
}
