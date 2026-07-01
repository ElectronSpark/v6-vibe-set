#define _GNU_SOURCE
#include <ctype.h>
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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define POISON_PAGE_COUNT 128
#define KF5_STATIC_PLUGIN_A 0xb9298UL
#define KF5_STATIC_PLUGIN_B 0xb92a0UL
#define PCRE2_ALLOCATOR_LIST 0x8b250UL
#define PCRE2_ALLOCATOR_MUTEX 0x8b260UL
#define PCRE2_STRESS_LOOPS 64

struct wl_display;

typedef struct wl_display *(*wl_display_create_fn)(void);
typedef void (*wl_display_destroy_fn)(struct wl_display *);
typedef int (*wl_display_init_shm_fn)(struct wl_display *);

struct map_info {
    uintptr_t base;
    char path[512];
};

struct graph_handle {
    const char *phase;
    const char *path;
    int flags;
    void *handle;
};

struct kwin_self_state {
    const char *label;
    const char *symbol;
    uintptr_t first_addr;
    int seen;
};

struct pcre2_api {
    void *handle;
    void *(*compile)(const uint16_t *, size_t, uint32_t, int *, size_t *,
                     void *);
    void (*code_free)(void *);
    void *(*match_data_create_from_pattern)(const void *, void *);
    void (*match_data_free)(void *);
    int (*dfa_match)(const void *, const uint16_t *, size_t, size_t, uint32_t,
                     void *, void *, int *, size_t);
};

static sigjmp_buf fault_env;
static volatile sig_atomic_t fault_armed;
static volatile sig_atomic_t fault_signal;

static void set_kde_library_path(void)
{
    setenv("LD_LIBRARY_PATH",
           "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:/usr/lib:"
           "/lib/x86_64-linux-gnu:/lib",
           1);
}

static void exec_kwin_help(void)
{
    set_kde_library_path();
    setenv("LD_PRELOAD",
           "/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so:"
           "/usr/lib/x86_64-linux-gnu/libKF5Codecs.so.5:"
           "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0",
           1);
    setenv("QT_QPA_PLATFORM", "wayland", 1);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    execl("/usr/bin/kwin_wayland", "kwin_wayland", "--help", NULL);
    perror("kde_dlopen_probe exec kwin_wayland");
    _exit(127);
}

static int run_kwin_help_check(void)
{
    char captured[65536];
    size_t captured_len = 0;
    int pipefd[2];
    int status = 0;
    pid_t pid;

    if (pipe(pipefd) < 0) {
        printf("KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=FAIL reason=pipe errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }

    pid = fork();
    if (pid < 0) {
        int saved_errno = errno;

        close(pipefd[0]);
        close(pipefd[1]);
        printf("KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=FAIL reason=fork errno=%d %s\n",
               saved_errno, strerror(saved_errno));
        return 1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            dup2(pipefd[1], STDERR_FILENO) < 0)
            _exit(126);
        close(pipefd[1]);
        exec_kwin_help();
    }

    close(pipefd[1]);
    for (;;) {
        char buf[1024];
        ssize_t n = read(pipefd[0], buf, sizeof(buf));

        if (n < 0) {
            if (errno == EINTR)
                continue;
            printf("KDE_DLOPEN_PROBE_KWIN_HELP_READ status=FAIL errno=%d %s\n",
                   errno, strerror(errno));
            break;
        }
        if (n == 0)
            break;
        if (getenv("KDE_DLOPEN_PROBE_KWIN_HELP_QUIET") == NULL)
            fwrite(buf, 1, (size_t)n, stdout);
        if (captured_len < sizeof(captured) - 1) {
            size_t copy = (size_t)n;

            if (copy > sizeof(captured) - 1 - captured_len)
                copy = sizeof(captured) - 1 - captured_len;
            memcpy(captured + captured_len, buf, copy);
            captured_len += copy;
        }
    }
    close(pipefd[0]);
    captured[captured_len] = '\0';

    if (waitpid(pid, &status, 0) < 0) {
        printf("KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=FAIL reason=waitpid errno=%d %s captured_bytes=%zu\n",
               errno, strerror(errno), captured_len);
        return 1;
    }

    int exited = WIFEXITED(status);
    int exit_status = exited ? WEXITSTATUS(status) : -1;
    int signaled = WIFSIGNALED(status);
    int term_signal = signaled ? WTERMSIG(status) : 0;
    int saw_usage = strstr(captured, "Usage: kwin_wayland") != NULL;
    int saw_title = strstr(captured, "KDE window manager") != NULL;
    int saw_options = strstr(captured, "Options:") != NULL;
    int pass = exited && exit_status == 0 && saw_usage && saw_title &&
               saw_options;

    printf("KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=%s exit_status=%d signaled=%d signal=%d saw_usage=%d saw_title=%d saw_options=%d captured_bytes=%zu\n",
           pass ? "PASS" : "FAIL", exit_status, signaled, term_signal,
           saw_usage, saw_title, saw_options, captured_len);
    return pass ? 0 : 1;
}

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

static long page_size_or_default(void)
{
    long page_size = sysconf(_SC_PAGESIZE);

    return page_size > 0 ? page_size : 4096;
}

static void fill_poison_page(char *page, size_t page_size)
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

static int run_graph_poison(void)
{
    const char *env = getenv("KDE_QT_PLUGIN_GRAPH_POISON");
    size_t page_size = (size_t)page_size_or_default();
    int failures = 0;

    if (env && env[0] && strcmp(env, "0") == 0)
        return 0;

    printf("KDE_QT_PLUGIN_GRAPH_PROBE_POISON:start pages=%d page_size=%zu pattern=/x86_64-/usr/lib\n",
           POISON_PAGE_COUNT, page_size);
    for (size_t i = 0; i < POISON_PAGE_COUNT; i++) {
        void *page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (page == MAP_FAILED) {
            failures++;
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_POISON:mmap_fail index=%zu errno=%d %s\n",
                   i, errno, strerror(errno));
            continue;
        }
        fill_poison_page(page, page_size);
        if (munmap(page, page_size) < 0) {
            failures++;
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_POISON:munmap_fail index=%zu ptr=%p errno=%d %s\n",
                   i, page, errno, strerror(errno));
        }
    }
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_POISON:end failures=%d\n", failures);
    return failures == 0 ? 0 : -1;
}

static void graph_fail(const char *reason, const char *phase)
{
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_FAIL reason=%s phase=%s\n",
           reason, phase ? phase : "unknown");
}

static int copy_map_path(char *out, size_t out_size, const char *line)
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

static int find_loaded_mapping(const char *needle, struct map_info *mapping)
{
    FILE *fp;
    char line[1024];

    memset(mapping, 0, sizeof(*mapping));
    fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_MAP:needle=%s status=FAIL reason=maps_open errno=%d %s\n",
               needle, errno, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        unsigned long offset = 0;
        char perms[8];

        if (!strstr(line, needle))
            continue;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s %lx", &start,
                   &end, perms, &offset) != 4)
            continue;
        (void)end;
        if (offset != 0)
            continue;
        mapping->base = start;
        if (copy_map_path(mapping->path, sizeof(mapping->path), line) < 0) {
            fclose(fp);
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_MAP:needle=%s status=FAIL reason=path_missing\n",
                   needle);
            return -1;
        }
        fclose(fp);
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_MAP:needle=%s status=FOUND base=0x%016" PRIxPTR " path=%s\n",
               needle, mapping->base, mapping->path);
        return 0;
    }
    fclose(fp);
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_MAP:needle=%s status=ABSENT\n",
           needle);
    return 1;
}

static void format_ascii(char *out, size_t out_size,
                         const unsigned char *bytes, size_t byte_count)
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

static int bytes_are_zero(const unsigned char *bytes, size_t byte_count)
{
    for (size_t i = 0; i < byte_count; i++) {
        if (bytes[i] != 0)
            return 0;
    }
    return 1;
}

static int bytes_have_path_poison(const unsigned char *bytes, size_t byte_count)
{
    static const char poison[] = "/x86_64-";

    for (size_t i = 0; i + sizeof(poison) - 1 <= byte_count; i++) {
        if (memcmp(bytes + i, poison, sizeof(poison) - 1) == 0)
            return 1;
    }
    return 0;
}

static int guarded_read(const char *phase, const char *label, uintptr_t addr,
                        unsigned char *bytes, size_t byte_count)
{
    fault_signal = 0;
    if (sigsetjmp(fault_env, 1) == 0) {
        fault_armed = 1;
        memcpy(bytes, (const void *)addr, byte_count);
        fault_armed = 0;
    } else {
        fault_armed = 0;
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_READ:phase=%s label=%s status=FAIL reason=signal_%d addr=0x%016" PRIxPTR "\n",
               phase, label, (int)fault_signal, addr);
        return -1;
    }
    return 0;
}

static int check_kf5_tail_slot(const char *phase, uintptr_t base,
                               uintptr_t offset, const char *label)
{
    unsigned char bytes[32];
    char ascii[sizeof(bytes) + 1];
    uintptr_t addr = base + offset;
    int zero;
    int poison;

    if (guarded_read(phase, label, addr, bytes, sizeof(bytes)) < 0)
        return 1;
    zero = bytes_are_zero(bytes, sizeof(bytes));
    poison = bytes_have_path_poison(bytes, sizeof(bytes));
    format_ascii(ascii, sizeof(ascii), bytes, sizeof(bytes));
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_KF5_TAIL:phase=%s label=%s status=%s addr=0x%016" PRIxPTR " offset=0x%lx zero=%d path_poison=%d bytes=%02x%02x%02x%02x%02x%02x%02x%02x ascii=\"%s\"\n",
           phase, label, zero && !poison ? "PASS" : "FAIL", addr,
           (unsigned long)offset, zero, poison, bytes[0], bytes[1],
           bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
           ascii);
    return zero && !poison ? 0 : 1;
}

static int check_kf5_tail_if_loaded(const char *phase)
{
    struct map_info mapping;
    int failed = 0;
    int rc;

    rc = find_loaded_mapping("libKF5CoreAddons.so.5", &mapping);
    if (rc > 0) {
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_KF5_TAIL:phase=%s status=SKIP reason=not-loaded\n",
               phase);
        return 0;
    }
    if (rc < 0)
        return 1;
    failed |= check_kf5_tail_slot(phase, mapping.base, KF5_STATIC_PLUGIN_A,
                                  "static_plugin_a");
    failed |= check_kf5_tail_slot(phase, mapping.base, KF5_STATIC_PLUGIN_B,
                                  "static_plugin_b");
    return failed;
}

static int check_pcre2_tail_slot(const char *phase, uintptr_t base,
                                 uintptr_t offset, const char *label,
                                 int require_zero)
{
    unsigned char bytes[32];
    char ascii[sizeof(bytes) + 1];
    uintptr_t addr = base + offset;
    int zero;
    int poison;

    if (guarded_read(phase, label, addr, bytes, sizeof(bytes)) < 0)
        return 1;
    zero = bytes_are_zero(bytes, sizeof(bytes));
    poison = bytes_have_path_poison(bytes, sizeof(bytes));
    format_ascii(ascii, sizeof(ascii), bytes, sizeof(bytes));
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=%s label=%s status=%s addr=0x%016" PRIxPTR " offset=0x%lx zero=%d path_poison=%d bytes=%02x%02x%02x%02x%02x%02x%02x%02x ascii=\"%s\"\n",
           phase, label, (!poison && (!require_zero || zero)) ? "PASS" : "FAIL", addr,
           (unsigned long)offset, zero, poison, bytes[0], bytes[1],
           bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
           ascii);
    return (!poison && (!require_zero || zero)) ? 0 : 1;
}

static int check_pcre2_tail_if_loaded_mode(const char *phase, int require_zero)
{
    struct map_info mapping;
    int failed = 0;
    int rc;

    rc = find_loaded_mapping("libpcre2-16.so.0", &mapping);
    if (rc > 0) {
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=%s status=SKIP reason=not-loaded\n",
               phase);
        return 0;
    }
    if (rc < 0)
        return 1;
    failed |= check_pcre2_tail_slot(phase, mapping.base,
                                    PCRE2_ALLOCATOR_LIST,
                                    "allocator_list", require_zero);
    failed |= check_pcre2_tail_slot(phase, mapping.base,
                                    PCRE2_ALLOCATOR_MUTEX,
                                    "allocator_mutex", require_zero);
    return failed;
}

static int check_pcre2_tail_if_loaded(const char *phase)
{
    return check_pcre2_tail_if_loaded_mode(phase, 1);
}

static int pcre2_resolve_api(struct pcre2_api *api)
{
    const char *err;

    memset(api, 0, sizeof(*api));
    dlerror();
    api->handle = dlopen("libpcre2-16.so.0", RTLD_NOW | RTLD_GLOBAL);
    err = dlerror();
    if (!api->handle) {
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_STRESS:status=FAIL reason=dlopen error=%s\n",
               err ? err : "");
        return 1;
    }

#define RESOLVE_PCRE2_API(field, name)                                        \
    do {                                                                       \
        dlerror();                                                             \
        *(void **)(&api->field) = dlsym(api->handle, name);                   \
        err = dlerror();                                                       \
        if (err || !api->field) {                                              \
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_STRESS:status=FAIL reason=dlsym symbol=%s error=%s\n", \
                   name, err ? err : "");                                     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

    RESOLVE_PCRE2_API(compile, "pcre2_compile_16");
    RESOLVE_PCRE2_API(code_free, "pcre2_code_free_16");
    RESOLVE_PCRE2_API(match_data_create_from_pattern,
                      "pcre2_match_data_create_from_pattern_16");
    RESOLVE_PCRE2_API(match_data_free, "pcre2_match_data_free_16");
    RESOLVE_PCRE2_API(dfa_match, "pcre2_dfa_match_16");
#undef RESOLVE_PCRE2_API

    return 0;
}

static uint64_t monotonic_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static int run_pcre2_dfa_stress(const char *phase)
{
    static const uint16_t pattern[] = {
        '(', 'u', 's', 'r', '|', 'l', 'i', 'b', '|', 'x', '8', '6', '_',
        '6', '4', '|', 'k', 'w', 'i', 'n', '|', 'w', 'a', 'y', 'l', 'a',
        'n', 'd', ')', '+', 0
    };
    static const uint16_t subject[] = {
        '/', 'u', 's', 'r', '/', 'l', 'i', 'b', '/', 'x', '8', '6', '_',
        '6', '4', '-', 'l', 'i', 'n', 'u', 'x', '-', 'g', 'n', 'u', '/',
        'k', 'w', 'i', 'n', '_', 'w', 'a', 'y', 'l', 'a', 'n', 'd', 0
    };
    struct pcre2_api api;
    int failed = 0;
    int errorcode = 0;
    size_t erroroffset = 0;
    int workspace[256];
    uint64_t start_us;
    uint64_t total_us;
    int matches = 0;
    int last_rc = 0;

    if (pcre2_resolve_api(&api) != 0)
        return 1;

    if (check_pcre2_tail_if_loaded_mode("pcre2-stress-before", 1) != 0)
        failed = 1;

    start_us = monotonic_us();
    for (int i = 0; i < PCRE2_STRESS_LOOPS; i++) {
        void *code;
        void *match_data;

        code = api.compile(pattern, sizeof(pattern) / sizeof(pattern[0]) - 1,
                           0, &errorcode, &erroroffset, NULL);
        if (!code) {
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_STRESS:phase=%s status=FAIL reason=compile loop=%d errorcode=%d erroroffset=%zu\n",
                   phase, i, errorcode, erroroffset);
            return 1;
        }
        match_data = api.match_data_create_from_pattern(code, NULL);
        if (!match_data) {
            api.code_free(code);
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_STRESS:phase=%s status=FAIL reason=match_data loop=%d\n",
                   phase, i);
            return 1;
        }
        memset(workspace, 0, sizeof(workspace));
        last_rc = api.dfa_match(code, subject,
                                sizeof(subject) / sizeof(subject[0]) - 1, 0,
                                0, match_data, NULL, workspace,
                                sizeof(workspace) / sizeof(workspace[0]));
        if (last_rc >= 0)
            matches++;
        api.match_data_free(match_data);
        api.code_free(code);
    }
    total_us = monotonic_us() - start_us;

    if (check_pcre2_tail_if_loaded_mode("pcre2-stress-after", 0) != 0)
        failed = 1;
    if (matches != PCRE2_STRESS_LOOPS)
        failed = 1;

    printf("KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_STRESS:phase=%s status=%s loops=%d matches=%d last_rc=%d total_us=%" PRIu64 " avg_us=%" PRIu64 "\n",
           phase, failed ? "FAIL" : "PASS", PCRE2_STRESS_LOOPS, matches,
           last_rc, total_us,
           PCRE2_STRESS_LOOPS > 0 ? total_us / PCRE2_STRESS_LOOPS : 0);
    return failed;
}

static uint64_t load_u64(const unsigned char *bytes)
{
    uint64_t value;

    memcpy(&value, bytes, sizeof(value));
    return value;
}

static struct kwin_self_state kwin_self_states[] = {
    { "workspace_self", "_ZN4KWin9Workspace5_selfE", 0, 0 },
    { "wayland_server_self", "_ZN4KWin13WaylandServer6s_selfE", 0, 0 },
    { "cursors_self", "_ZN4KWin7Cursors6s_selfE", 0, 0 },
};

static int check_kwin_self_symbol(void *kwin, const char *phase,
                                  struct kwin_self_state *state)
{
    unsigned char bytes[8];
    char ascii[sizeof(bytes) + 1];
    uintptr_t addr;
    uint64_t value;
    int poison;
    const char *err;

    dlerror();
    addr = (uintptr_t)dlsym(kwin, state->symbol);
    err = dlerror();
    if (err || addr == 0) {
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_KWIN_SELF:phase=%s label=%s status=FAIL reason=dlsym symbol=%s error=%s\n",
               phase, state->label, state->symbol, err ? err : "");
        return 1;
    }
    if (!state->seen) {
        state->first_addr = addr;
        state->seen = 1;
    }
    if (guarded_read(phase, state->label, addr, bytes, sizeof(bytes)) < 0)
        return 1;
    value = load_u64(bytes);
    poison = bytes_have_path_poison(bytes, sizeof(bytes));
    format_ascii(ascii, sizeof(ascii), bytes, sizeof(bytes));
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_KWIN_SELF:phase=%s label=%s status=%s symbol=%s addr=0x%016" PRIxPTR " first_addr=0x%016" PRIxPTR " addr_stable=%d value=0x%016" PRIx64 " zero=%d path_poison=%d ascii=\"%s\"\n",
           phase, state->label,
           value == 0 && !poison && addr == state->first_addr ? "PASS" : "FAIL",
           state->symbol, addr, state->first_addr, addr == state->first_addr,
           value, value == 0, poison, ascii);
    return value == 0 && !poison && addr == state->first_addr ? 0 : 1;
}

static int check_kwin_self_if_loaded(const char *phase)
{
    void *kwin;
    int failed = 0;

    dlerror();
    kwin = dlopen("libkwin.so.5", RTLD_NOW | RTLD_NOLOAD);
    if (!kwin) {
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_KWIN_SELF:phase=%s status=SKIP reason=not-loaded\n",
               phase);
        return 0;
    }

    for (size_t i = 0; i < sizeof(kwin_self_states) / sizeof(kwin_self_states[0]); i++)
        failed |= check_kwin_self_symbol(kwin, phase, &kwin_self_states[i]);
    dlclose(kwin);
    return failed;
}

static int graph_checkpoint(const char *phase)
{
    int failed = 0;

    failed |= check_kf5_tail_if_loaded(phase);
    failed |= check_pcre2_tail_if_loaded(phase);
    failed |= check_kwin_self_if_loaded(phase);
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_CHECKPOINT:phase=%s status=%s\n",
           phase, failed ? "FAIL" : "PASS");
    return failed;
}

static int run_kwin_plugin_graph_probe(int pcre2_stress)
{
    struct graph_handle graph[] = {
        { "qtcore", "libQt5Core.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtdbus", "libQt5DBus.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtqml", "libQt5Qml.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtgui", "libQt5Gui.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtwidgets", "libQt5Widgets.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtquick", "libQt5Quick.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtquicktemplates2", "libQt5QuickTemplates2.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtquickcontrols2", "libQt5QuickControls2.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtwaylandclient", "libQt5WaylandClient.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "qtwaylandcompositor", "libQt5WaylandCompositor.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5coreaddons", "libKF5CoreAddons.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5configcore", "libKF5ConfigCore.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5configgui", "libKF5ConfigGui.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5configwidgets", "libKF5ConfigWidgets.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5i18n", "libKF5I18n.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5globalaccel", "libKF5GlobalAccel.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5globalaccelprivate", "libKF5GlobalAccelPrivate.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5windowsystem", "libKF5WindowSystem.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5notifications", "libKF5Notifications.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5service", "libKF5Service.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5package", "libKF5Package.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5crash", "libKF5Crash.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kf5style", "libKF5Style.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "breezecommon", "libbreezecommon5.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "breeze_style", "/usr/lib/x86_64-linux-gnu/qt5/plugins/styles/breeze.so", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "breeze_decoration", "/usr/lib/x86_64-linux-gnu/qt5/plugins/org.kde.kdecoration2/breezedecoration.so", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "frameworkintegration", "/usr/lib/x86_64-linux-gnu/qt5/plugins/kf5/FrameworkIntegrationPlugin.so", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kwindowsystem_wayland_plugin", "/usr/lib/x86_64-linux-gnu/qt5/plugins/kf5/kwindowsystem/KF5WindowSystemKWaylandPlugin.so", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kwin_nightcolor", "/usr/lib/x86_64-linux-gnu/qt5/plugins/kwin/plugins/libKWinNightColorPlugin.so", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kwin_krunnerintegration", "/usr/lib/x86_64-linux-gnu/qt5/plugins/kwin/plugins/krunnerintegration.so", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kwin_colorintegration", "/usr/lib/x86_64-linux-gnu/qt5/plugins/kwin/plugins/colordintegration.so", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kdecorations2", "libkdecorations2.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kwineffects", "libkwineffects.so.14", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "kwinglutils", "libkwinglutils.so.14", RTLD_NOW | RTLD_GLOBAL, NULL },
        { "libkwin", "libkwin.so.5", RTLD_NOW | RTLD_GLOBAL, NULL },
    };
    set_kde_library_path();
    setenv("QT_PLUGIN_PATH", "/usr/lib/x86_64-linux-gnu/qt5/plugins", 1);
    setenv("QT_QPA_PLATFORM", "offscreen", 1);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_START\n");
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_ENV:LD_LIBRARY_PATH=%s QT_PLUGIN_PATH=%s QT_QPA_PLATFORM=%s XDG_RUNTIME_DIR=%s\n",
           getenv("LD_LIBRARY_PATH"), getenv("QT_PLUGIN_PATH"),
           getenv("QT_QPA_PLATFORM"), getenv("XDG_RUNTIME_DIR"));

    if (install_fault_handlers() < 0) {
        graph_fail("sigaction", "start");
        return 1;
    }
    if (run_graph_poison() < 0) {
        graph_fail("poison", "start");
        return 1;
    }
    if (graph_checkpoint("initial") != 0) {
        graph_fail("checkpoint", "initial");
        return 1;
    }

    for (size_t i = 0; i < sizeof(graph) / sizeof(graph[0]); i++) {
        const char *err;

        dlerror();
        fault_signal = 0;
        if (sigsetjmp(fault_env, 1) == 0) {
            fault_armed = 1;
            graph[i].handle = dlopen(graph[i].path, graph[i].flags);
            fault_armed = 0;
        } else {
            fault_armed = 0;
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_DLOPEN:phase=%s path=%s status=FAIL reason=signal_%d\n",
                   graph[i].phase, graph[i].path, (int)fault_signal);
            graph_fail("signal", graph[i].phase);
            return 1;
        }
        err = dlerror();
        if (!graph[i].handle) {
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_DLOPEN:phase=%s path=%s status=FAIL error=%s\n",
                   graph[i].phase, graph[i].path, err ? err : "");
            graph_fail("dlopen", graph[i].phase);
            return 1;
        }
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_DLOPEN:phase=%s path=%s status=PASS handle=%p\n",
               graph[i].phase, graph[i].path, graph[i].handle);
        if (graph_checkpoint(graph[i].phase) != 0) {
            graph_fail("checkpoint", graph[i].phase);
            printf("KDE_QT_PLUGIN_GRAPH_PROBE_RESULT status=FAIL\n");
            return 1;
        }
    }

    printf("KDE_QT_PLUGIN_GRAPH_PROBE_RESULT status=PASS phases=%zu\n",
           sizeof(graph) / sizeof(graph[0]));
    if (pcre2_stress && run_pcre2_dfa_stress("post-libkwin") != 0) {
        graph_fail("pcre2-stress", "post-libkwin");
        printf("KDE_QT_PLUGIN_GRAPH_PROBE_RESULT status=FAIL\n");
        return 1;
    }
    printf("KDE_QT_PLUGIN_GRAPH_PROBE_PASS\n");
    return 0;
}

static int probe_symbol(void *handle, const char *symbol)
{
    void *ptr;

    dlerror();
    ptr = dlsym(handle, symbol);
    if (!ptr) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlsym=%s ptr=NULL error=%s\n",
               symbol, err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlsym=%s ptr=%p\n", symbol, ptr);
    return 0;
}

static int probe_library(const char *name, int flags)
{
    void *handle;
    int failed = 0;

    dlerror();
    handle = dlopen(name, flags);
    if (!handle) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=%s handle=NULL error=%s\n",
               name, err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=%s handle=%p\n", name, handle);
    if (name[0] == 'l' && name[1] == 'i' && name[2] == 'b' &&
        name[3] == 'p' && name[4] == 'c' && name[5] == 'r')
        failed |= probe_symbol(handle, "pcre2_code_free_16");
    dlclose(handle);
    return failed;
}

static int check_qtwidgets_allwidgets(void *handle, const char *phase)
{
    static const char all_widgets_sym[] =
        "_ZN14QWidgetPrivate10allWidgetsE";
    void *sym;
    uint64_t value = 0;

    dlerror();
    sym = dlsym(handle, all_widgets_sym);
    if (!sym) {
        const char *err = dlerror();
        printf("kde_dlopen_probe phase=%s dlsym=%s ptr=NULL error=%s\n",
               phase, all_widgets_sym, err ? err : "");
        return 1;
    }

    memcpy(&value, sym, sizeof(value));
    printf("kde_dlopen_probe phase=%s qtwidgets_allwidgets ptr=%p value=0x%016llx status=%s\n",
           phase, sym, (unsigned long long)value,
           value == 0 ? "PASS" : "FAIL");
    return value == 0 ? 0 : 1;
}

static void *open_qtwidgets(void)
{
    void *handle;

    dlerror();
    handle = dlopen("libQt5Widgets.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libQt5Widgets.so.5 handle=NULL error=%s\n",
               err ? err : "");
        return NULL;
    }
    printf("kde_dlopen_probe dlopen=libQt5Widgets.so.5 handle=%p\n", handle);
    return handle;
}

static int probe_configwidgets(void)
{
    void *codecs;
    void *configwidgets;
    void *sym;
    int failed = 0;

    dlerror();
    configwidgets = dlopen("libKF5ConfigWidgets.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!configwidgets) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 direct_handle=NULL error=%s\n",
               err ? err : "");
        failed = 1;
    } else {
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 direct_handle=%p\n",
               configwidgets);
        dlclose(configwidgets);
    }

    dlerror();
    codecs = dlopen("libKF5Codecs.so.5", RTLD_NOW | RTLD_NOLOAD);
    if (codecs) {
        printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 noload_handle=%p\n",
               codecs);
        dlclose(codecs);
    } else {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 noload_handle=NULL error=%s\n",
               err ? err : "");
    }

    dlerror();
    codecs = dlopen("libKF5Codecs.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!codecs) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 handle=NULL error=%s\n",
               err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 handle=%p\n", codecs);

    dlerror();
    sym = dlsym(codecs, "_ZNK9KCharsets12codecForNameERK7QStringRb");
    if (!sym) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlsym=KCharsets::codecForName(QString,bool&) ptr=NULL error=%s\n",
               err ? err : "");
        failed = 1;
    } else {
        printf("kde_dlopen_probe dlsym=KCharsets::codecForName(QString,bool&) ptr=%p\n",
               sym);
    }

    dlerror();
    configwidgets = dlopen("libKF5ConfigWidgets.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!configwidgets) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 handle=NULL error=%s\n",
               err ? err : "");
        failed = 1;
    } else {
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 handle=%p\n",
               configwidgets);
        dlclose(configwidgets);
    }

    dlclose(codecs);
    return failed;
}

static int probe_wayland_shm_after_kwin(void *qtwidgets)
{
    void *kwin;
    void *server;
    wl_display_create_fn wl_display_create;
    wl_display_destroy_fn wl_display_destroy;
    wl_display_init_shm_fn wl_display_init_shm;
    struct wl_display *display;
    int rc;

    dlerror();
    kwin = dlopen("libkwin.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!kwin) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libkwin.so.5 handle=NULL error=%s\n",
               err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libkwin.so.5 handle=%p\n", kwin);
    if (qtwidgets)
        (void)check_qtwidgets_allwidgets(qtwidgets, "after-libkwin");

    dlerror();
    server = dlopen("libwayland-server.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!server) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libwayland-server.so.0 handle=NULL error=%s\n",
               err ? err : "");
        dlclose(kwin);
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libwayland-server.so.0 handle=%p\n",
           server);
    if (qtwidgets)
        (void)check_qtwidgets_allwidgets(qtwidgets, "after-wayland-server");

    wl_display_create =
        (wl_display_create_fn)dlsym(server, "wl_display_create");
    wl_display_destroy =
        (wl_display_destroy_fn)dlsym(server, "wl_display_destroy");
    wl_display_init_shm =
        (wl_display_init_shm_fn)dlsym(server, "wl_display_init_shm");
    if (!wl_display_create || !wl_display_destroy || !wl_display_init_shm) {
        printf("kde_dlopen_probe wayland_shm_symbols=FAIL\n");
        dlclose(server);
        dlclose(kwin);
        return 1;
    }

    display = wl_display_create();
    if (!display) {
        printf("kde_dlopen_probe wayland_display_create=FAIL\n");
        dlclose(server);
        dlclose(kwin);
        return 1;
    }

    rc = wl_display_init_shm(display);
    printf("kde_dlopen_probe wayland_shm_init=%s rc=%d\n",
           rc == 0 ? "PASS" : "FAIL", rc);
    wl_display_destroy(display);

    dlclose(server);
    dlclose(kwin);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    int failed = 0;
    void *qtwidgets;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && strcmp(argv[1], "--kwin-help") == 0)
        exec_kwin_help();
    if (argc > 1 && strcmp(argv[1], "--kwin-help-check") == 0)
        return run_kwin_help_check();
    if (argc > 1 && strcmp(argv[1], "--kwin-plugin-graph") == 0)
        return run_kwin_plugin_graph_probe(0);
    if (argc > 1 && strcmp(argv[1], "--kwin-plugin-graph-pcre2-stress") == 0)
        return run_kwin_plugin_graph_probe(1);

    set_kde_library_path();
    printf("kde_dlopen_probe ld_library_path=%s\n", getenv("LD_LIBRARY_PATH"));
    failed |= probe_library("libQt5Core.so.5", RTLD_NOW | RTLD_GLOBAL);
    qtwidgets = open_qtwidgets();
    if (!qtwidgets)
        failed = 1;
    else
        failed |= check_qtwidgets_allwidgets(qtwidgets, "after-qtwidgets");
    failed |= probe_configwidgets();
    failed |= probe_library("libpcre2-16.so.0", RTLD_NOW | RTLD_GLOBAL);
    failed |= probe_wayland_shm_after_kwin(qtwidgets);
    if (qtwidgets)
        dlclose(qtwidgets);
    printf("kde_dlopen_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
