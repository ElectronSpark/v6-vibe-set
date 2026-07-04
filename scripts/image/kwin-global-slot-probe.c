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

#define LIBKWIN_NAME "libkwin.so.5"
#define POISON_PAGE_COUNT 128

struct kwin_mapping {
    uintptr_t base;
    char path[512];
};

struct slot_probe {
    const char *label;
    const char *symbol;
    uintptr_t sym_value;
    uint64_t sym_size;
    uint32_t sym_index;
    int sym_found;
    uintptr_t reloc_offset;
    uint32_t reloc_type;
    int reloc_found;
};

static struct slot_probe probes[] = {
    { "workspace_self", "_ZN4KWin9Workspace5_selfE", 0, 0, 0, 0, 0, 0, 0 },
    { "wayland_server_self", "_ZN4KWin13WaylandServer6s_selfE", 0, 0, 0, 0, 0, 0, 0 },
    { "cursors_self", "_ZN4KWin7Cursors6s_selfE", 0, 0, 0, 0, 0, 0, 0 },
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
    const char *env = getenv("KWIN_GLOBAL_SLOT_POISON");
    size_t page_size = (size_t)page_size_or_default();
    int failures = 0;

    if (env && env[0] && strcmp(env, "0") == 0)
        return 0;

    printf("KWIN_GLOBAL_SLOT_PROBE_POISON:start pages=%d page_size=%zu pattern=/x86_64-/usr/lib\n",
           POISON_PAGE_COUNT, page_size);
    for (size_t i = 0; i < POISON_PAGE_COUNT; i++) {
        void *page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (page == MAP_FAILED) {
            failures++;
            printf("KWIN_GLOBAL_SLOT_PROBE_POISON:mmap_fail index=%zu errno=%d %s\n",
                   i, errno, strerror(errno));
            continue;
        }
        fill_poison_page(page, page_size);
        if (munmap(page, page_size) < 0) {
            failures++;
            printf("KWIN_GLOBAL_SLOT_PROBE_POISON:munmap_fail index=%zu ptr=%p errno=%d %s\n",
                   i, page, errno, strerror(errno));
        }
    }
    printf("KWIN_GLOBAL_SLOT_PROBE_POISON:end failures=%d\n", failures);
    return failures == 0 ? 0 : -1;
}

static int
is_kwin_line(const char *line)
{
    return strstr(line, LIBKWIN_NAME) != NULL;
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

static void
dump_relevant_maps(const char *phase)
{
    FILE *fp;
    char line[1024];
    int count = 0;

    printf("KWIN_GLOBAL_SLOT_PROBE_PHASE:%s_maps_begin\n", phase);
    fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        printf("KWIN_GLOBAL_SLOT_PROBE_MAPS_OPEN_FAIL:%s errno=%d %s\n",
               phase, errno, strerror(errno));
        printf("KWIN_GLOBAL_SLOT_PROBE_PHASE:%s_maps_end count=0\n", phase);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (!is_kwin_line(line))
            continue;
        count++;
        printf("KWIN_GLOBAL_SLOT_PROBE_MAP:%s:%s", phase, line);
        if (line[0] && line[strlen(line) - 1] != '\n')
            putchar('\n');
    }
    fclose(fp);
    printf("KWIN_GLOBAL_SLOT_PROBE_PHASE:%s_maps_end count=%d\n", phase, count);
}

static int
find_kwin_mapping(struct kwin_mapping *mapping)
{
    FILE *fp;
    char line[1024];

    memset(mapping, 0, sizeof(*mapping));
    fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:maps_open errno=%d %s\n",
               errno, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        unsigned long offset = 0;
        char perms[8];

        if (!is_kwin_line(line))
            continue;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %lx", &start,
                   &end, perms, &offset) != 4)
            continue;
        (void)end;
        if (offset == 0) {
            mapping->base = start;
            if (copy_map_path(mapping->path, sizeof(mapping->path), line) < 0) {
                fclose(fp);
                printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:map_path_missing line=%s",
                       line);
                return -1;
            }
            fclose(fp);
            printf("KWIN_GLOBAL_SLOT_PROBE_BASE:base=0x%016" PRIxPTR
                   " path=%s\n",
                   mapping->base, mapping->path);
            return 0;
        }
    }
    fclose(fp);
    printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:base_not_found\n");
    return -1;
}

static void *
read_alloc(FILE *fp, long offset, size_t size, const char *label)
{
    void *buf;

    if (size == 0)
        return NULL;
    buf = calloc(1, size);
    if (!buf) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:alloc label=%s size=%zu\n",
               label, size);
        return NULL;
    }
    if (fseek(fp, offset, SEEK_SET) != 0 || fread(buf, 1, size, fp) != size) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:read label=%s offset=0x%lx size=%zu errno=%d %s\n",
               label, offset, size, errno, strerror(errno));
        free(buf);
        return NULL;
    }
    return buf;
}

static int
offset_in_rw_zero_tail(uintptr_t offset, size_t size, const Elf64_Phdr *ph)
{
    uintptr_t tail_start = (uintptr_t)ph->p_vaddr + (uintptr_t)ph->p_filesz;
    uintptr_t tail_end = (uintptr_t)ph->p_vaddr + (uintptr_t)ph->p_memsz;

    return offset >= tail_start && offset + size <= tail_end;
}

static int
validate_slot_layout(FILE *fp, const Elf64_Ehdr *ehdr,
                     const struct slot_probe *probe)
{
    int found = 0;

    for (Elf64_Half i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr ph;
        long phoff = (long)(ehdr->e_phoff + (Elf64_Off)i * ehdr->e_phentsize);
        uintptr_t tail_start;
        uintptr_t tail_end;

        if (fseek(fp, phoff, SEEK_SET) != 0 ||
            fread(&ph, 1, sizeof(ph), fp) != sizeof(ph)) {
            printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:phdr_read label=%s index=%u errno=%d %s\n",
                   probe->label, (unsigned)i, errno, strerror(errno));
            return -1;
        }
        if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_W))
            continue;

        tail_start = (uintptr_t)ph.p_vaddr + (uintptr_t)ph.p_filesz;
        tail_end = (uintptr_t)ph.p_vaddr + (uintptr_t)ph.p_memsz;
        printf("KWIN_GLOBAL_SLOT_PROBE_ELF_LOAD:index=%u label=%s vaddr=0x%lx filesz=0x%lx memsz=0x%lx tail=[0x%lx,0x%lx) sym=0x%lx size=%llu contains=%d\n",
               (unsigned)i, probe->label, (unsigned long)ph.p_vaddr,
               (unsigned long)ph.p_filesz, (unsigned long)ph.p_memsz,
               (unsigned long)tail_start, (unsigned long)tail_end,
               (unsigned long)probe->sym_value,
               (unsigned long long)probe->sym_size,
               offset_in_rw_zero_tail(probe->sym_value,
                                      probe->sym_size ? probe->sym_size : 8,
                                      &ph));
        if (offset_in_rw_zero_tail(probe->sym_value,
                                   probe->sym_size ? probe->sym_size : 8,
                                   &ph))
            found = 1;
    }

    if (!found) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:slot_not_in_rw_zero_tail label=%s sym=0x%lx size=%llu\n",
               probe->label, (unsigned long)probe->sym_value,
               (unsigned long long)probe->sym_size);
        return -1;
    }
    return 0;
}

static struct slot_probe *
probe_by_symbol(const char *name)
{
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        if (strcmp(probes[i].symbol, name) == 0)
            return &probes[i];
    }
    return NULL;
}

static int
load_elf_probe_layout(const char *path)
{
    FILE *fp;
    Elf64_Ehdr ehdr;
    Elf64_Shdr *shdrs = NULL;
    char *shstr = NULL;
    Elf64_Sym *dynsym = NULL;
    char *dynstr = NULL;
    Elf64_Rela *rela = NULL;
    size_t dynsym_count = 0;
    size_t rela_count = 0;
    int ret = -1;

    fp = fopen(path, "rb");
    if (!fp) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:elf_open path=%s errno=%d %s\n",
               path, errno, strerror(errno));
        return -1;
    }
    if (fread(&ehdr, 1, sizeof(ehdr), fp) != sizeof(ehdr)) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:elf_header_read path=%s errno=%d %s\n",
               path, errno, strerror(errno));
        goto out;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_shentsize != sizeof(Elf64_Shdr) ||
        ehdr.e_phentsize != sizeof(Elf64_Phdr) ||
        ehdr.e_shstrndx == SHN_UNDEF ||
        ehdr.e_shstrndx >= ehdr.e_shnum) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:elf_shape path=%s class=%u shentsize=%u phentsize=%u shstrndx=%u shnum=%u\n",
               path, ehdr.e_ident[EI_CLASS], ehdr.e_shentsize,
               ehdr.e_phentsize, ehdr.e_shstrndx, ehdr.e_shnum);
        goto out;
    }

    shdrs = read_alloc(fp, (long)ehdr.e_shoff,
                       (size_t)ehdr.e_shnum * sizeof(*shdrs), "shdrs");
    if (!shdrs)
        goto out;
    shstr = read_alloc(fp, (long)shdrs[ehdr.e_shstrndx].sh_offset,
                       (size_t)shdrs[ehdr.e_shstrndx].sh_size, "shstr");
    if (!shstr)
        goto out;

    for (Elf64_Half i = 0; i < ehdr.e_shnum; i++) {
        const char *name;

        if (shdrs[i].sh_name >= shdrs[ehdr.e_shstrndx].sh_size)
            continue;
        name = shstr + shdrs[i].sh_name;
        if (strcmp(name, ".dynsym") == 0) {
            dynsym = read_alloc(fp, (long)shdrs[i].sh_offset,
                                (size_t)shdrs[i].sh_size, ".dynsym");
            dynsym_count = (size_t)(shdrs[i].sh_size / sizeof(*dynsym));
        } else if (strcmp(name, ".dynstr") == 0) {
            dynstr = read_alloc(fp, (long)shdrs[i].sh_offset,
                                (size_t)shdrs[i].sh_size, ".dynstr");
        } else if (strcmp(name, ".rela.dyn") == 0) {
            rela = read_alloc(fp, (long)shdrs[i].sh_offset,
                              (size_t)shdrs[i].sh_size, ".rela.dyn");
            rela_count = (size_t)(shdrs[i].sh_size / sizeof(*rela));
        }
    }
    if (!dynsym || !dynstr || !rela) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:missing_sections dynsym=%d dynstr=%d rela=%d\n",
               dynsym != NULL, dynstr != NULL, rela != NULL);
        goto out;
    }

    for (size_t i = 0; i < dynsym_count; i++) {
        struct slot_probe *probe;
        const char *name;

        if (dynsym[i].st_name == 0)
            continue;
        name = dynstr + dynsym[i].st_name;
        probe = probe_by_symbol(name);
        if (!probe)
            continue;
        probe->sym_found = 1;
        probe->sym_index = (uint32_t)i;
        probe->sym_value = (uintptr_t)dynsym[i].st_value;
        probe->sym_size = dynsym[i].st_size;
        printf("KWIN_GLOBAL_SLOT_PROBE_SYMBOL:%s status=FOUND index=%zu value=0x%lx size=%llu shndx=%u\n",
               probe->label, i, (unsigned long)probe->sym_value,
               (unsigned long long)probe->sym_size, dynsym[i].st_shndx);
    }

    for (size_t i = 0; i < rela_count; i++) {
        uint32_t sym = (uint32_t)ELF64_R_SYM(rela[i].r_info);
        uint32_t type = (uint32_t)ELF64_R_TYPE(rela[i].r_info);

        for (size_t j = 0; j < sizeof(probes) / sizeof(probes[0]); j++) {
            if (!probes[j].sym_found || probes[j].sym_index != sym)
                continue;
            probes[j].reloc_found = 1;
            probes[j].reloc_offset = (uintptr_t)rela[i].r_offset;
            probes[j].reloc_type = type;
            printf("KWIN_GLOBAL_SLOT_PROBE_RELOC:%s status=FOUND offset=0x%lx type=%u addend=%lld\n",
                   probes[j].label, (unsigned long)probes[j].reloc_offset,
                   type, (long long)rela[i].r_addend);
        }
    }

    ret = 0;
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        if (!probes[i].sym_found) {
            printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:symbol_missing label=%s symbol=%s\n",
                   probes[i].label, probes[i].symbol);
            ret = -1;
            continue;
        }
        if (validate_slot_layout(fp, &ehdr, &probes[i]) < 0)
            ret = -1;
        if (!probes[i].reloc_found) {
            printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:reloc_missing label=%s symbol=%s\n",
                   probes[i].label, probes[i].symbol);
            ret = -1;
        } else if (probes[i].reloc_type != R_X86_64_GLOB_DAT) {
            printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:reloc_type label=%s type=%u expected=%u\n",
                   probes[i].label, probes[i].reloc_type, R_X86_64_GLOB_DAT);
            ret = -1;
        }
    }
    if (ret == 0)
        printf("KWIN_GLOBAL_SLOT_PROBE_ELF_LAYOUT:status=PASS path=%s\n", path);

out:
    free(rela);
    free(dynstr);
    free(dynsym);
    free(shstr);
    free(shdrs);
    fclose(fp);
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
has_path_poison(const unsigned char *bytes, size_t byte_count)
{
    static const char poison[] = "/x86_64-";

    for (size_t i = 0; i + sizeof(poison) - 1 <= byte_count; i++) {
        if (memcmp(bytes + i, poison, sizeof(poison) - 1) == 0)
            return 1;
    }
    return 0;
}

static int
read_bytes(const char *label, uintptr_t addr, unsigned char *bytes,
           size_t byte_count)
{
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        memcpy(bytes, (const void *)addr, byte_count);
        fault_armed = 0;
    } else {
        fault_armed = 0;
        printf("KWIN_GLOBAL_SLOT_PROBE_READ:%s status=FAIL reason=signal_%d addr=0x%016" PRIxPTR "\n",
               label, (int)fault_signal, addr);
        return -1;
    }
    return 0;
}

static uint64_t
load_u64(const unsigned char *bytes)
{
    uint64_t value;

    memcpy(&value, bytes, sizeof(value));
    return value;
}

static int
check_runtime_slot(void *handle, const struct kwin_mapping *mapping,
                   const struct slot_probe *probe)
{
    unsigned char slot_bytes[8];
    unsigned char got_bytes[8];
    char slot_ascii[sizeof(slot_bytes) + 1];
    char got_ascii[sizeof(got_bytes) + 1];
    uintptr_t expected_addr = mapping->base + probe->sym_value;
    uintptr_t got_addr = mapping->base + probe->reloc_offset;
    uintptr_t actual_addr;
    uint64_t slot_value;
    uint64_t got_value;
    const char *err;
    int failed = 0;

    dlerror();
    actual_addr = (uintptr_t)dlsym(handle, probe->symbol);
    err = dlerror();
    if (err || actual_addr == 0) {
        printf("KWIN_GLOBAL_SLOT_PROBE_DLSYM:%s status=FAIL symbol=%s error=%s\n",
               probe->label, probe->symbol, err ? err : "");
        return 1;
    }

    if (read_bytes(probe->label, actual_addr, slot_bytes,
                   sizeof(slot_bytes)) < 0)
        return 1;
    if (read_bytes(probe->label, got_addr, got_bytes, sizeof(got_bytes)) < 0)
        return 1;

    slot_value = load_u64(slot_bytes);
    got_value = load_u64(got_bytes);
    format_ascii(slot_ascii, sizeof(slot_ascii), slot_bytes, sizeof(slot_bytes));
    format_ascii(got_ascii, sizeof(got_ascii), got_bytes, sizeof(got_bytes));

    if (actual_addr != expected_addr)
        failed = 1;
    if (got_value != expected_addr)
        failed = 1;
    if (slot_value != 0 || has_path_poison(slot_bytes, sizeof(slot_bytes)))
        failed = 1;
    if (has_path_poison(got_bytes, sizeof(got_bytes)))
        failed = 1;

    printf("KWIN_GLOBAL_SLOT_PROBE_SLOT:%s status=%s symbol=%s sym_addr=0x%016" PRIxPTR
           " expected_addr=0x%016" PRIxPTR " sym_offset=0x%lx"
           " slot_value=0x%016" PRIx64 " slot_zero=%d slot_path_poison=%d"
           " slot_bytes=%02x%02x%02x%02x%02x%02x%02x%02x slot_ascii=\"%s\""
           " got_addr=0x%016" PRIxPTR " got_value=0x%016" PRIx64
           " got_matches=%d got_path_poison=%d got_ascii=\"%s\"\n",
           probe->label, failed ? "FAIL" : "PASS", probe->symbol,
           actual_addr, expected_addr, (unsigned long)probe->sym_value,
           slot_value, slot_value == 0,
           has_path_poison(slot_bytes, sizeof(slot_bytes)),
           slot_bytes[0], slot_bytes[1], slot_bytes[2], slot_bytes[3],
           slot_bytes[4], slot_bytes[5], slot_bytes[6], slot_bytes[7],
           slot_ascii, got_addr, got_value, got_value == expected_addr,
           has_path_poison(got_bytes, sizeof(got_bytes)), got_ascii);
    return failed;
}

int
main(void)
{
    const char *ld_library_path;
    const char *err;
    void *handle;
    struct kwin_mapping mapping;
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("KWIN_GLOBAL_SLOT_PROBE_PHASE:start\n");
    ld_library_path = getenv("LD_LIBRARY_PATH");
    printf("KWIN_GLOBAL_SLOT_PROBE_ENV:LD_LIBRARY_PATH=%s\n",
           ld_library_path ? ld_library_path : "(unset)");
    dump_relevant_maps("start");

    if (install_fault_handlers() < 0) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:sigaction errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    if (run_poison_mode() < 0) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:poison\n");
        return 1;
    }

    printf("KWIN_GLOBAL_SLOT_PROBE_PHASE:dlopen_libkwin\n");
    dlerror();
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        handle = dlopen(LIBKWIN_NAME, RTLD_NOW | RTLD_GLOBAL);
        fault_armed = 0;
    } else {
        fault_armed = 0;
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:dlopen_signal_%d\n",
               (int)fault_signal);
        return 1;
    }
    err = dlerror();
    if (!handle) {
        printf("KWIN_GLOBAL_SLOT_PROBE_DLOPEN:%s handle=NULL error=%s\n",
               LIBKWIN_NAME, err ? err : "");
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:dlopen_libkwin\n");
        return 1;
    }
    printf("KWIN_GLOBAL_SLOT_PROBE_DLOPEN:%s handle=%p\n",
           LIBKWIN_NAME, handle);
    dump_relevant_maps("after_dlopen");

    if (find_kwin_mapping(&mapping) < 0)
        return 1;
    if (load_elf_probe_layout(mapping.path) < 0)
        return 1;
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
        failures += check_runtime_slot(handle, &mapping, &probes[i]);

    if (failures != 0) {
        printf("KWIN_GLOBAL_SLOT_PROBE_FAIL:slot failures=%d\n", failures);
        return 1;
    }
    printf("KWIN_GLOBAL_SLOT_PROBE_PASS\n");
    return 0;
}
