#define _GNU_SOURCE
#include <dlfcn.h>
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

typedef const char *(*u_getDataDirectory_74_fn)(void);
typedef int32_t (*ucnv_countAvailable_74_fn)(void);
typedef const char *(*ucnv_getAvailableName_74_fn)(int32_t);
typedef const char *(*ucnv_getStandardName_74_fn)(const char *, const char *,
                                                  int32_t *);

#define POISON_PAGE_COUNT 64

static sigjmp_buf fault_env;
static volatile sig_atomic_t fault_armed;
static volatile sig_atomic_t fault_signal;

static void fault_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)info;
    (void)ucontext;
    fault_signal = sig;
    if (fault_armed)
        siglongjmp(fault_env, 1);
    _exit(128 + sig);
}

static int install_fault_handlers(void)
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

static int is_x86_64_canonical(uintptr_t value)
{
#if UINTPTR_MAX > 0xffffffffU
    return value <= UINT64_C(0x00007fffffffffff) ||
           value >= UINT64_C(0xffff800000000000);
#else
    (void)value;
    return 1;
#endif
}

static int print_pointer_status(const char *label, const void *ptr)
{
    uintptr_t value = (uintptr_t)ptr;
    int canonical = is_x86_64_canonical(value);

    printf("ICU_ELF_TAIL_PROBE_PTR:%s ptr=%p raw=0x%016" PRIxPTR
           " canonical=%s null=%s\n",
           label, ptr, value, canonical ? "yes" : "no",
           ptr == NULL ? "yes" : "no");
    return canonical;
}

static int interesting_map_line(const char *line)
{
    return strstr(line, "libicu") != NULL ||
           strstr(line, "libX11") != NULL ||
           strstr(line, "libxcb") != NULL;
}

static void dump_relevant_maps(const char *phase)
{
    FILE *fp;
    char line[1024];
    int count = 0;

    printf("ICU_ELF_TAIL_PROBE_PHASE:%s_maps_begin\n", phase);
    fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        printf("ICU_ELF_TAIL_PROBE_MAPS_OPEN_FAIL:%s errno=%d %s\n",
               phase, errno, strerror(errno));
        printf("ICU_ELF_TAIL_PROBE_PHASE:%s_maps_end count=0\n", phase);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (!interesting_map_line(line))
            continue;
        count++;
        printf("ICU_ELF_TAIL_PROBE_MAP:%s:%s", phase, line);
        if (line[0] != '\0' && line[strlen(line) - 1] != '\n')
            putchar('\n');
    }

    fclose(fp);
    printf("ICU_ELF_TAIL_PROBE_PHASE:%s_maps_end count=%d\n", phase, count);
}

static int resolve_symbol(void *handle, const char *name, void **out)
{
    const char *err;

    dlerror();
    *out = dlsym(handle, name);
    err = dlerror();
    if (err || !*out) {
        printf("ICU_ELF_TAIL_PROBE_DLSYM:%s ptr=%p error=%s\n",
               name, *out, err ? err : "");
        printf("ICU_ELF_TAIL_PROBE_FAIL:dlsym_%s\n", name);
        return -1;
    }

    printf("ICU_ELF_TAIL_PROBE_DLSYM:%s ptr=%p\n", name, *out);
    return 0;
}

static long poison_page_size(void)
{
    long page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0)
        page_size = 4096;
    return page_size;
}

static void fill_poison_page(char *page, size_t page_size)
{
    static const char poison[] = "libX11-xcb.so.1";
    size_t offset = 0;

    while (offset < page_size) {
        size_t chunk = sizeof(poison) - 1;

        if (chunk > page_size - offset)
            chunk = page_size - offset;
        memcpy(page + offset, poison, chunk);
        offset += chunk;
    }
}

static int run_poison_mode(void)
{
    const char *env = getenv("ICU_ELF_TAIL_POISON");
    long page_size_long;
    size_t page_size;
    int failures = 0;
    size_t i;

    if (!env || env[0] == '\0' || strcmp(env, "0") == 0)
        return 0;

    page_size_long = poison_page_size();
    page_size = (size_t)page_size_long;
    printf("ICU_ELF_TAIL_PROBE_POISON:start pages=%d page_size=%zu pattern=libX11-xcb.so.1\n",
           POISON_PAGE_COUNT, page_size);

    for (i = 0; i < POISON_PAGE_COUNT; i++) {
        void *page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (page == MAP_FAILED) {
            failures++;
            printf("ICU_ELF_TAIL_PROBE_POISON:mmap_fail index=%zu errno=%d %s\n",
                   i, errno, strerror(errno));
            continue;
        }

        fill_poison_page(page, page_size);
        if (munmap(page, page_size) < 0) {
            failures++;
            printf("ICU_ELF_TAIL_PROBE_POISON:munmap_fail index=%zu ptr=%p errno=%d %s\n",
                   i, page, errno, strerror(errno));
        }
    }

    printf("ICU_ELF_TAIL_PROBE_POISON:end pages=%d failures=%d\n",
           POISON_PAGE_COUNT, failures);
    return failures == 0 ? 0 : -1;
}

static int faulted_call(const char *phase)
{
    fault_armed = 0;
    printf("ICU_ELF_TAIL_PROBE_FAIL:%s_signal_%d\n",
           phase, (int)fault_signal);
    return 1;
}

int main(void)
{
    const char *ld_library_path;
    void *handle;
    const char *err;
    u_getDataDirectory_74_fn u_getDataDirectory_74 = NULL;
    ucnv_countAvailable_74_fn ucnv_countAvailable_74 = NULL;
    ucnv_getAvailableName_74_fn ucnv_getAvailableName_74 = NULL;
    ucnv_getStandardName_74_fn ucnv_getStandardName_74 = NULL;
    const char *data_dir = NULL;
    const char *name0 = NULL;
    const char *standard0 = NULL;
    const char *standard_utf8 = NULL;
    int32_t standard0_status = 0;
    int32_t standard_utf8_status = 0;
    int32_t count = -1;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("ICU_ELF_TAIL_PROBE_PHASE:start\n");

    ld_library_path = getenv("LD_LIBRARY_PATH");
    printf("ICU_ELF_TAIL_PROBE_ENV:LD_LIBRARY_PATH=%s\n",
           ld_library_path ? ld_library_path : "(unset)");
    dump_relevant_maps("start");

    if (install_fault_handlers() < 0) {
        printf("ICU_ELF_TAIL_PROBE_FAIL:sigaction errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }

    if (run_poison_mode() < 0) {
        printf("ICU_ELF_TAIL_PROBE_FAIL:poison\n");
        return 1;
    }

    printf("ICU_ELF_TAIL_PROBE_PHASE:dlopen_libicuuc\n");
    dlerror();
    handle = dlopen("libicuuc.so.74", RTLD_NOW | RTLD_LOCAL);
    err = dlerror();
    if (!handle) {
        printf("ICU_ELF_TAIL_PROBE_DLOPEN:libicuuc.so.74 handle=NULL error=%s\n",
               err ? err : "");
        printf("ICU_ELF_TAIL_PROBE_FAIL:dlopen_libicuuc\n");
        return 1;
    }
    printf("ICU_ELF_TAIL_PROBE_DLOPEN:libicuuc.so.74 handle=%p\n", handle);
    dump_relevant_maps("after_dlopen");

    if (resolve_symbol(handle, "u_getDataDirectory_74",
                       (void **)&u_getDataDirectory_74) < 0 ||
        resolve_symbol(handle, "ucnv_countAvailable_74",
                       (void **)&ucnv_countAvailable_74) < 0 ||
        resolve_symbol(handle, "ucnv_getAvailableName_74",
                       (void **)&ucnv_getAvailableName_74) < 0 ||
        resolve_symbol(handle, "ucnv_getStandardName_74",
                       (void **)&ucnv_getStandardName_74) < 0)
        return 1;

    printf("ICU_ELF_TAIL_PROBE_PHASE:call_u_getDataDirectory_74\n");
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        data_dir = u_getDataDirectory_74();
        fault_armed = 0;
    } else {
        return faulted_call("u_getDataDirectory_74");
    }
    if (!print_pointer_status("u_getDataDirectory_74", data_dir)) {
        printf("ICU_ELF_TAIL_PROBE_FAIL:u_getDataDirectory_74_noncanonical\n");
        return 1;
    }
    print_pointer_status("u_getDataDirectory_74_before_standard_name", data_dir);

    printf("ICU_ELF_TAIL_PROBE_PHASE:call_ucnv_countAvailable_74\n");
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        count = ucnv_countAvailable_74();
        fault_armed = 0;
    } else {
        return faulted_call("ucnv_countAvailable_74");
    }
    printf("ICU_ELF_TAIL_PROBE_COUNT:ucnv_countAvailable_74=%" PRId32 "\n",
           count);
    if (count <= 0) {
        printf("ICU_ELF_TAIL_PROBE_FAIL:ucnv_countAvailable_74_nonpositive\n");
        return 1;
    }

    printf("ICU_ELF_TAIL_PROBE_PHASE:call_ucnv_getAvailableName_74_0\n");
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        name0 = ucnv_getAvailableName_74(0);
        fault_armed = 0;
    } else {
        return faulted_call("ucnv_getAvailableName_74_0");
    }
    if (!print_pointer_status("ucnv_getAvailableName_74_0", name0)) {
        printf("ICU_ELF_TAIL_PROBE_FAIL:ucnv_getAvailableName_74_0_noncanonical\n");
        return 1;
    }
    if (name0 == NULL) {
        printf("ICU_ELF_TAIL_PROBE_FAIL:ucnv_getAvailableName_74_0_null\n");
        return 1;
    }

    printf("ICU_ELF_TAIL_PROBE_PHASE:call_ucnv_getStandardName_74_name0_MIME\n");
    fault_signal = 0;
    standard0_status = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        standard0 = ucnv_getStandardName_74(name0, "MIME", &standard0_status);
        fault_armed = 0;
    } else {
        return faulted_call("ucnv_getStandardName_74_name0_MIME");
    }
    print_pointer_status("ucnv_getStandardName_74_name0_MIME", standard0);
    printf("ICU_ELF_TAIL_PROBE_STATUS:ucnv_getStandardName_74_name0_MIME=%" PRId32 "\n",
           standard0_status);

    printf("ICU_ELF_TAIL_PROBE_PHASE:call_ucnv_getStandardName_74_UTF-8_MIME\n");
    fault_signal = 0;
    standard_utf8_status = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        standard_utf8 = ucnv_getStandardName_74("UTF-8", "MIME",
                                                &standard_utf8_status);
        fault_armed = 0;
    } else {
        return faulted_call("ucnv_getStandardName_74_UTF-8_MIME");
    }
    print_pointer_status("ucnv_getStandardName_74_UTF-8_MIME", standard_utf8);
    printf("ICU_ELF_TAIL_PROBE_STATUS:ucnv_getStandardName_74_UTF-8_MIME=%" PRId32 "\n",
           standard_utf8_status);

    printf("ICU_ELF_TAIL_PROBE_PHASE:call_u_getDataDirectory_74_after_standard_name\n");
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        data_dir = u_getDataDirectory_74();
        fault_armed = 0;
    } else {
        return faulted_call("u_getDataDirectory_74_after_standard_name");
    }
    if (!print_pointer_status("u_getDataDirectory_74_after_standard_name",
                              data_dir)) {
        printf("ICU_ELF_TAIL_PROBE_FAIL:u_getDataDirectory_74_after_standard_name_noncanonical\n");
        return 1;
    }

    dump_relevant_maps("after_icu_calls");
    printf("ICU_ELF_TAIL_PROBE_PASS\n");
    return 0;
}
