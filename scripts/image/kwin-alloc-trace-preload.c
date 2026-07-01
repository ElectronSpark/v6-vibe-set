#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <bits/struct_mutex.h>

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#define PCRE2_ALLOCATOR_LIST 0x8b250UL
#define PCRE2_ALLOCATOR_MUTEX 0x8b260UL
#define PCRE2_SAMPLE_BYTES 32
#define GLIBC_PTHREAD_MUTEX_PRIO_PROTECT_NP 0x40
#define GLIBC_PTHREAD_MUTEX_PRIO_CEILING_SHIFT 19
#define GLIBC_PTHREAD_MUTEX_PRIO_CEILING_MASK 0xff

typedef void (*kwin_check_input_window_stacking_fn)(void *);

static int log_fd = -1;
static __thread int in_hook;
static int resolving;
static int resolved;

static void *(*real_malloc)(size_t);
static void (*real_free)(void *);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static int (*real_posix_memalign)(void **, size_t, size_t);
static void *(*real_aligned_alloc)(size_t, size_t);
static void *(*real_memalign)(size_t, size_t);
static void *(*real_mmap)(void *, size_t, int, int, int, off_t);
static void *(*real_mmap64)(void *, size_t, int, int, int, off64_t);
static int (*real_munmap)(void *, size_t);
static int (*real_brk)(void *);
static void *(*real_sbrk)(intptr_t);
static void *(*real_dlopen)(const char *, int);
static void *(*real_new)(size_t);
static void *(*real_new_array)(size_t);
static void (*real_cxa_throw)(void *, void *, void (*)(void *));
static kwin_check_input_window_stacking_fn real_kwin_check_input_window_stacking;
static int (*real_pthread_mutex_init)(pthread_mutex_t *,
                                      const pthread_mutexattr_t *);
static int (*real_pthread_mutex_lock)(pthread_mutex_t *);
static int (*real_pthread_mutex_trylock)(pthread_mutex_t *);
static int (*real_pthread_mutex_timedlock)(pthread_mutex_t *,
                                           const struct timespec *);
static int (*real_pthread_mutex_unlock)(pthread_mutex_t *);
static int (*real_pthread_mutexattr_setprotocol)(pthread_mutexattr_t *, int);
static int (*real_pthread_mutexattr_setprioceiling)(pthread_mutexattr_t *, int);
static int (*real_pthread_mutexattr_getprioceiling)(const pthread_mutexattr_t *,
                                                   int *);
static int (*real_pcre2_match_16)(const void *, const uint16_t *, size_t,
                                  size_t, uint32_t, void *, void *);
static void *(*real_pcre2_compile_16)(const uint16_t *, size_t, uint32_t,
                                      int *, size_t *, void *);
static void *(*real_pcre2_match_data_create_from_pattern_16)(const void *,
                                                             void *);
static void (*real_pcre2_match_data_free_16)(void *);
static void *(*real_pcre2_match_context_create_16)(void *);
static void (*real_pcre2_match_context_free_16)(void *);
static void (*real_pcre2_code_free_16)(void *);
static int (*real_pcre2_dfa_match_16)(const void *, const uint16_t *, size_t,
                                      size_t, uint32_t, void *, void *, int *,
                                      size_t);

static uint64_t malloc_calls;
static uint64_t calloc_calls;
static uint64_t realloc_calls;
static uint64_t posix_memalign_calls;
static uint64_t aligned_alloc_calls;
static uint64_t memalign_calls;
static uint64_t new_calls;
static uint64_t new_array_calls;
static uint64_t free_calls;
static uint64_t mmap_calls;
static uint64_t munmap_calls;
static uint64_t brk_calls;
static uint64_t sbrk_calls;
static uint64_t alloc_failures;
static uint64_t mmap_failures;
static uint64_t brk_failures;
static uint64_t throw_calls;
static uint64_t dlopen_calls;
static uint64_t pcre2_match_calls;
static uint64_t pcre2_compile_calls;
static uint64_t pcre2_lifetime_calls;
static uint64_t pcre2_dfa_match_calls;
static uint64_t pthread_mutex_init_calls;
static uint64_t pthread_mutex_lock_calls;
static uint64_t pthread_mutex_trylock_calls;
static uint64_t pthread_mutex_timedlock_calls;
static uint64_t pthread_mutex_unlock_calls;
static uint64_t pthread_mutexattr_protocol_calls;
static uint64_t pthread_mutexattr_ceiling_calls;
static uint64_t pthread_mutexattr_get_ceiling_calls;
static uint64_t pthread_prio_protect_init_calls;
static uint64_t pthread_prio_protect_lock_calls;
static uint64_t pthread_prio_protect_trylock_calls;
static uint64_t pthread_prio_protect_timedlock_calls;
static uint64_t pthread_prio_protect_unlock_calls;
static uint64_t pthread_prio_protect_invalid_ceiling;
static uint64_t pthread_prio_protect_bad_lock_return;
static uint64_t kwin_effects_stacking_calls;
static uint64_t kwin_effects_stacking_real_missing;
static uintptr_t kwin_effects_last_self;
static uintptr_t kwin_effects_last_caller;
static uint64_t pcre2_slot_samples;
static uint64_t pcre2_slot_changes;
static uint64_t pcre2_slot_poison_samples;
static uint64_t requested_bytes;
static uint64_t mmap_bytes;
static uint64_t max_request;
static uint64_t last_fail_size;
static int last_fail_errno;
static char last_fail_func[32];
static uintptr_t pcre2_base;
static int fifo_min_prio;
static int fifo_max_prio;
static int have_fifo_prio_range;
static unsigned char last_pcre2_allocator_list[PCRE2_SAMPLE_BYTES];
static unsigned char last_pcre2_allocator_mutex[PCRE2_SAMPLE_BYTES];
static int have_last_pcre2_allocator_list;
static int have_last_pcre2_allocator_mutex;

static unsigned char emergency_heap[65536];
static size_t emergency_used;

static int emergency_ptr(const void *ptr)
{
    const unsigned char *p = (const unsigned char *)ptr;

    return p >= emergency_heap && p < emergency_heap + sizeof(emergency_heap);
}

static void *emergency_alloc(size_t size)
{
    uintptr_t base;
    size_t aligned;

    if (size == 0)
        size = 1;
    base = (uintptr_t)emergency_heap + emergency_used;
    aligned = (size_t)((base + 15u) & ~(uintptr_t)15u) - (uintptr_t)emergency_heap;
    if (aligned > sizeof(emergency_heap) || size > sizeof(emergency_heap) - aligned)
        return NULL;
    emergency_used = aligned + size;
    return emergency_heap + aligned;
}

static void write_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    if (log_fd < 0)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if ((size_t)n >= sizeof(buf))
        n = (int)sizeof(buf) - 1;
    while (n > 0) {
        ssize_t wrote = write(log_fd, buf, (size_t)n);

        if (wrote <= 0)
            return;
        if (wrote >= n)
            return;
        memmove(buf, buf + wrote, (size_t)(n - wrote));
        n -= (int)wrote;
    }
}

static void sync_log(void)
{
    if (log_fd >= 0)
        (void)fsync(log_fd);
}

static void ensure_fifo_prio_range(void)
{
    if (have_fifo_prio_range)
        return;
    fifo_min_prio = sched_get_priority_min(SCHED_FIFO);
    fifo_max_prio = sched_get_priority_max(SCHED_FIFO);
    have_fifo_prio_range = 1;
}

static unsigned int attr_raw(const pthread_mutexattr_t *attr)
{
    unsigned int raw = 0;

    if (attr)
        memcpy(&raw, attr, sizeof(raw));
    return raw;
}

static int attr_ceiling_from_raw(unsigned int raw)
{
    return (int)((raw >> 12) & GLIBC_PTHREAD_MUTEX_PRIO_CEILING_MASK);
}

static int mutex_prio_ceiling(const struct __pthread_mutex_s *data)
{
    return (int)((unsigned int)data->__lock >>
                 GLIBC_PTHREAD_MUTEX_PRIO_CEILING_SHIFT) &
           GLIBC_PTHREAD_MUTEX_PRIO_CEILING_MASK;
}

static int mutex_is_prio_protect(const struct __pthread_mutex_s *data)
{
    return (data->__kind & GLIBC_PTHREAD_MUTEX_PRIO_PROTECT_NP) != 0;
}

static int prio_ceiling_is_invalid(int ceiling)
{
    ensure_fifo_prio_range();
    return ceiling < fifo_min_prio || ceiling > fifo_max_prio;
}

static void log_mutex_state(const char *phase, const char *op,
                            const pthread_mutex_t *mutex, int rc,
                            const void *caller)
{
    const struct __pthread_mutex_s *data =
        (const struct __pthread_mutex_s *)mutex;
    int ceiling;
    int invalid;

    if (!mutex)
        return;
    ceiling = mutex_prio_ceiling(data);
    invalid = prio_ceiling_is_invalid(ceiling);
    if (invalid)
        pthread_prio_protect_invalid_ceiling++;
    write_log("kwin-alloc-trace: phase=%s op=%s mutex=%p caller=%p rc=%d kind=0x%x lock=0x%x count=%u owner=%d nusers=%u spins=%d elision=%d prio_ceiling=%d fifo_min=%d fifo_max=%d invalid_ceiling=%d lock_calls=%llu trylock_calls=%llu timedlock_calls=%llu protected_lock_calls=%llu protected_trylock_calls=%llu protected_timedlock_calls=%llu invalid_ceiling_total=%llu\n",
              phase, op, (const void *)mutex, caller, rc, data->__kind,
              data->__lock, data->__count, data->__owner, data->__nusers,
              data->__spins, data->__elision, ceiling, fifo_min_prio,
              fifo_max_prio, invalid,
              (unsigned long long)pthread_mutex_lock_calls,
              (unsigned long long)pthread_mutex_trylock_calls,
              (unsigned long long)pthread_mutex_timedlock_calls,
              (unsigned long long)pthread_prio_protect_lock_calls,
              (unsigned long long)pthread_prio_protect_trylock_calls,
              (unsigned long long)pthread_prio_protect_timedlock_calls,
              (unsigned long long)pthread_prio_protect_invalid_ceiling);
    if (invalid || strstr(phase, "_enter") != NULL)
        sync_log();
}

static void log_attr_state(const char *phase, const pthread_mutexattr_t *attr,
                           int requested, int rc)
{
    unsigned int raw = attr_raw(attr);
    int decoded_ceiling = attr_ceiling_from_raw(raw);
    int reported_ceiling = -1;
    int have_reported = 0;

    if (real_pthread_mutexattr_getprioceiling && attr &&
        real_pthread_mutexattr_getprioceiling(attr, &reported_ceiling) == 0)
        have_reported = 1;
    write_log("kwin-alloc-trace: phase=%s attr=%p requested=%d rc=%d raw=0x%x decoded_ceiling=%d reported_ceiling=%d have_reported=%d attr_protocol_calls=%llu attr_ceiling_calls=%llu attr_get_ceiling_calls=%llu\n",
              phase, (const void *)attr, requested, rc, raw, decoded_ceiling,
              reported_ceiling, have_reported,
              (unsigned long long)pthread_mutexattr_protocol_calls,
              (unsigned long long)pthread_mutexattr_ceiling_calls,
              (unsigned long long)pthread_mutexattr_get_ceiling_calls);
    sync_log();
}

static void note_request(size_t size)
{
    requested_bytes += size;
    if (size > max_request)
        max_request = size;
}

static void note_alloc_failure(const char *func, size_t size, int err)
{
    alloc_failures++;
    last_fail_size = size;
    last_fail_errno = err;
    snprintf(last_fail_func, sizeof(last_fail_func), "%s", func);
    write_log("kwin-alloc-trace: phase=alloc_fail func=%s size=%zu errno=%d malloc_calls=%llu calloc_calls=%llu realloc_calls=%llu new_calls=%llu new_array_calls=%llu requested_bytes=%llu max_request=%llu\n",
              func, size, err,
              (unsigned long long)malloc_calls,
              (unsigned long long)calloc_calls,
              (unsigned long long)realloc_calls,
              (unsigned long long)new_calls,
              (unsigned long long)new_array_calls,
              (unsigned long long)requested_bytes,
              (unsigned long long)max_request);
    sync_log();
}

static void resolve_kwin_symbols(void)
{
    if (!real_kwin_check_input_window_stacking) {
        real_kwin_check_input_window_stacking =
            dlsym(RTLD_NEXT,
                  "_ZN4KWin18EffectsHandlerImpl24checkInputWindowStackingEv");
    }
}

static kwin_check_input_window_stacking_fn
lookup_kwin_check_input_window_stacking(void)
{
    return (kwin_check_input_window_stacking_fn)
        dlsym(RTLD_NEXT,
              "_ZN4KWin18EffectsHandlerImpl24checkInputWindowStackingEv");
}

static void log_kwin_effects_stacking_state(const char *phase, void *self,
                                            const void *caller)
{
    write_log("kwin-alloc-trace: phase=effects_check_input_window_stacking_%s calls=%llu self=%p caller=%p real_missing_total=%llu\n",
              phase,
              (unsigned long long)kwin_effects_stacking_calls,
              self,
              caller,
              (unsigned long long)kwin_effects_stacking_real_missing);
}

static int bytes_are_zero(const unsigned char *bytes, size_t byte_count)
{
    for (size_t i = 0; i < byte_count; i++) {
        if (bytes[i] != 0)
            return 0;
    }
    return 1;
}

static int bytes_contain_literal(const unsigned char *bytes, size_t byte_count,
                                 const char *literal)
{
    size_t literal_len = strlen(literal);

    if (literal_len == 0 || literal_len > byte_count)
        return 0;
    for (size_t i = 0; i + literal_len <= byte_count; i++) {
        if (memcmp(bytes + i, literal, literal_len) == 0)
            return 1;
    }
    return 0;
}

static int bytes_have_path_poison(const unsigned char *bytes, size_t byte_count)
{
    return bytes_contain_literal(bytes, byte_count, "/x86_64-") ||
           bytes_contain_literal(bytes, byte_count, "x86_64-") ||
           bytes_contain_literal(bytes, byte_count, "/usr/lib");
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

static int parse_hex_field(const char *begin, const char *end,
                           uintptr_t *value)
{
    uintptr_t out = 0;
    int saw_digit = 0;

    while (begin < end) {
        unsigned char ch = (unsigned char)*begin;
        unsigned int digit;

        if (ch >= '0' && ch <= '9')
            digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f')
            digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F')
            digit = ch - 'A' + 10;
        else
            return -1;
        out = (out << 4) | digit;
        saw_digit = 1;
        begin++;
    }
    if (!saw_digit)
        return -1;
    *value = out;
    return 0;
}

static const char *skip_nonspace(const char *p, const char *end)
{
    while (p < end && *p != ' ' && *p != '\t')
        p++;
    return p;
}

static const char *skip_space(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

static int parse_maps_line_for_pcre2(const char *line, size_t len,
                                     uintptr_t *base)
{
    const char *end = line + len;
    const char *dash;
    const char *p;
    uintptr_t start = 0;
    uintptr_t offset = 0;

    if (!memmem(line, len, "libpcre2-16.so.0", sizeof("libpcre2-16.so.0") - 1))
        return 0;

    dash = memchr(line, '-', len);
    if (!dash || parse_hex_field(line, dash, &start) < 0)
        return 0;
    p = skip_space(skip_nonspace(dash + 1, end), end);
    p = skip_space(skip_nonspace(p, end), end);
    if (p >= end)
        return 0;
    if (parse_hex_field(p, skip_nonspace(p, end), &offset) < 0)
        return 0;
    *base = start - offset;
    return 1;
}

static int find_pcre2_base(void)
{
    char buf[8192];
    size_t used = 0;
    int fd;

    if (pcre2_base != 0)
        return 1;

    fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    for (;;) {
        ssize_t n = read(fd, buf + used, sizeof(buf) - used);
        size_t scan = 0;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        used += (size_t)n;
        while (scan < used) {
            char *nl = memchr(buf + scan, '\n', used - scan);
            size_t line_len;
            uintptr_t base = 0;

            if (!nl)
                break;
            line_len = (size_t)(nl - (buf + scan));
            if (parse_maps_line_for_pcre2(buf + scan, line_len, &base)) {
                pcre2_base = base;
                close(fd);
                write_log("kwin-alloc-trace: phase=pcre2_map status=FOUND base=0x%016" PRIxPTR "\n",
                          pcre2_base);
                return 1;
            }
            scan += line_len + 1;
        }
        if (n == 0)
            break;
        if (scan > 0 && scan < used) {
            memmove(buf, buf + scan, used - scan);
            used -= scan;
        } else if (scan == used) {
            used = 0;
        } else if (used == sizeof(buf)) {
            used = 0;
        }
    }
    close(fd);
    return 0;
}

static void read_slot_bytes(uintptr_t addr, unsigned char *bytes,
                            size_t byte_count)
{
    const volatile unsigned char *src = (const volatile unsigned char *)addr;

    for (size_t i = 0; i < byte_count; i++)
        bytes[i] = src[i];
}

static void sample_one_pcre2_slot(const char *trigger, const char *label,
                                  uintptr_t offset, unsigned char *last,
                                  int *have_last, int force)
{
    unsigned char bytes[PCRE2_SAMPLE_BYTES];
    char ascii[PCRE2_SAMPLE_BYTES + 1];
    uintptr_t addr = pcre2_base + offset;
    int zero;
    int poison;
    int changed;

    read_slot_bytes(addr, bytes, sizeof(bytes));
    pcre2_slot_samples++;
    zero = bytes_are_zero(bytes, sizeof(bytes));
    poison = bytes_have_path_poison(bytes, sizeof(bytes));
    changed = !*have_last || memcmp(last, bytes, sizeof(bytes)) != 0;
    if (changed) {
        memcpy(last, bytes, sizeof(bytes));
        *have_last = 1;
        pcre2_slot_changes++;
    }
    if (poison)
        pcre2_slot_poison_samples++;
    if (!force && !changed && !poison)
        return;
    format_ascii(ascii, sizeof(ascii), bytes, sizeof(bytes));
    write_log("kwin-alloc-trace: phase=pcre2_slot_sample trigger=%s label=%s addr=0x%016" PRIxPTR " offset=0x%lx zero=%d path_poison=%d changed=%d bytes=%02x%02x%02x%02x%02x%02x%02x%02x ascii=\"%s\" samples=%llu changes=%llu poison_samples=%llu\n",
              trigger, label, addr, (unsigned long)offset, zero, poison,
              changed, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
              bytes[5], bytes[6], bytes[7],
              ascii, (unsigned long long)pcre2_slot_samples,
              (unsigned long long)pcre2_slot_changes,
              (unsigned long long)pcre2_slot_poison_samples);
    if (poison)
        sync_log();
}

static void sample_pcre2_slots(const char *trigger, int force)
{
    if (!find_pcre2_base())
        return;
    sample_one_pcre2_slot(trigger, "allocator_list", PCRE2_ALLOCATOR_LIST,
                          last_pcre2_allocator_list,
                          &have_last_pcre2_allocator_list, force);
    sample_one_pcre2_slot(trigger, "allocator_mutex", PCRE2_ALLOCATOR_MUTEX,
                          last_pcre2_allocator_mutex,
                          &have_last_pcre2_allocator_mutex, force);
}

static void resolve_symbols(void)
{
    if (resolved || resolving)
        return;
    resolving = 1;
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_free = dlsym(RTLD_NEXT, "free");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
    real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
    real_memalign = dlsym(RTLD_NEXT, "memalign");
    real_mmap = dlsym(RTLD_NEXT, "mmap");
    real_mmap64 = dlsym(RTLD_NEXT, "mmap64");
    real_munmap = dlsym(RTLD_NEXT, "munmap");
    real_brk = dlsym(RTLD_NEXT, "brk");
    real_sbrk = dlsym(RTLD_NEXT, "sbrk");
    real_dlopen = dlsym(RTLD_NEXT, "dlopen");
    real_new = dlsym(RTLD_NEXT, "_Znwm");
    real_new_array = dlsym(RTLD_NEXT, "_Znam");
    real_cxa_throw = dlsym(RTLD_NEXT, "__cxa_throw");
    real_pthread_mutex_init = dlsym(RTLD_NEXT, "pthread_mutex_init");
    real_pthread_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
    real_pthread_mutex_trylock = dlsym(RTLD_NEXT, "pthread_mutex_trylock");
    real_pthread_mutex_timedlock = dlsym(RTLD_NEXT, "pthread_mutex_timedlock");
    real_pthread_mutex_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    real_pthread_mutexattr_setprotocol =
        dlsym(RTLD_NEXT, "pthread_mutexattr_setprotocol");
    real_pthread_mutexattr_setprioceiling =
        dlsym(RTLD_NEXT, "pthread_mutexattr_setprioceiling");
    real_pthread_mutexattr_getprioceiling =
        dlsym(RTLD_NEXT, "pthread_mutexattr_getprioceiling");
    resolved = 1;
    resolving = 0;
}

static void resolve_pcre2_symbols(void)
{
    if (!real_pcre2_match_16)
        real_pcre2_match_16 = dlsym(RTLD_NEXT, "pcre2_match_16");
    if (!real_pcre2_compile_16)
        real_pcre2_compile_16 = dlsym(RTLD_NEXT, "pcre2_compile_16");
    if (!real_pcre2_match_data_create_from_pattern_16)
        real_pcre2_match_data_create_from_pattern_16 =
            dlsym(RTLD_NEXT, "pcre2_match_data_create_from_pattern_16");
    if (!real_pcre2_match_data_free_16)
        real_pcre2_match_data_free_16 =
            dlsym(RTLD_NEXT, "pcre2_match_data_free_16");
    if (!real_pcre2_match_context_create_16)
        real_pcre2_match_context_create_16 =
            dlsym(RTLD_NEXT, "pcre2_match_context_create_16");
    if (!real_pcre2_match_context_free_16)
        real_pcre2_match_context_free_16 =
            dlsym(RTLD_NEXT, "pcre2_match_context_free_16");
    if (!real_pcre2_code_free_16)
        real_pcre2_code_free_16 = dlsym(RTLD_NEXT, "pcre2_code_free_16");
    if (!real_pcre2_dfa_match_16)
        real_pcre2_dfa_match_16 = dlsym(RTLD_NEXT, "pcre2_dfa_match_16");
}

static const char *typeinfo_name(void *tinfo)
{
    const char *name;

    if (!tinfo)
        return "null";
    name = ((const char **)tinfo)[1];
    if (!name)
        return "null";
    for (int i = 0; i < 96 && name[i]; i++) {
        unsigned char ch = (unsigned char)name[i];

        if (ch < 0x20 || ch > 0x7e)
            return "unprintable";
    }
    return name;
}

static void write_summary(const char *phase)
{
    sample_pcre2_slots(phase, 1);
    ensure_fifo_prio_range();
    write_log("kwin-alloc-trace: phase=%s status=DONE malloc_calls=%llu calloc_calls=%llu realloc_calls=%llu posix_memalign_calls=%llu aligned_alloc_calls=%llu memalign_calls=%llu new_calls=%llu new_array_calls=%llu free_calls=%llu mmap_calls=%llu mmap_failures=%llu munmap_calls=%llu brk_calls=%llu brk_failures=%llu sbrk_calls=%llu alloc_failures=%llu throw_calls=%llu dlopen_calls=%llu pcre2_match_calls=%llu pcre2_compile_calls=%llu pcre2_lifetime_calls=%llu pcre2_dfa_match_calls=%llu pthread_mutex_init_calls=%llu pthread_mutex_lock_calls=%llu pthread_mutex_trylock_calls=%llu pthread_mutex_timedlock_calls=%llu pthread_mutex_unlock_calls=%llu pthread_mutexattr_protocol_calls=%llu pthread_mutexattr_ceiling_calls=%llu pthread_mutexattr_get_ceiling_calls=%llu pthread_prio_protect_init_calls=%llu pthread_prio_protect_lock_calls=%llu pthread_prio_protect_trylock_calls=%llu pthread_prio_protect_timedlock_calls=%llu pthread_prio_protect_unlock_calls=%llu pthread_prio_protect_invalid_ceiling=%llu pthread_prio_protect_bad_lock_return=%llu fifo_min=%d fifo_max=%d pcre2_slot_samples=%llu pcre2_slot_changes=%llu pcre2_slot_poison_samples=%llu pcre2_base=0x%016" PRIxPTR " requested_bytes=%llu mmap_bytes=%llu max_request=%llu last_fail_func=%s last_fail_size=%llu last_fail_errno=%d emergency_used=%zu\n",
              phase,
              (unsigned long long)malloc_calls,
              (unsigned long long)calloc_calls,
              (unsigned long long)realloc_calls,
              (unsigned long long)posix_memalign_calls,
              (unsigned long long)aligned_alloc_calls,
              (unsigned long long)memalign_calls,
              (unsigned long long)new_calls,
              (unsigned long long)new_array_calls,
              (unsigned long long)free_calls,
              (unsigned long long)mmap_calls,
              (unsigned long long)mmap_failures,
              (unsigned long long)munmap_calls,
              (unsigned long long)brk_calls,
              (unsigned long long)brk_failures,
              (unsigned long long)sbrk_calls,
              (unsigned long long)alloc_failures,
              (unsigned long long)throw_calls,
              (unsigned long long)dlopen_calls,
              (unsigned long long)pcre2_match_calls,
              (unsigned long long)pcre2_compile_calls,
              (unsigned long long)pcre2_lifetime_calls,
              (unsigned long long)pcre2_dfa_match_calls,
              (unsigned long long)pthread_mutex_init_calls,
              (unsigned long long)pthread_mutex_lock_calls,
              (unsigned long long)pthread_mutex_trylock_calls,
              (unsigned long long)pthread_mutex_timedlock_calls,
              (unsigned long long)pthread_mutex_unlock_calls,
              (unsigned long long)pthread_mutexattr_protocol_calls,
              (unsigned long long)pthread_mutexattr_ceiling_calls,
              (unsigned long long)pthread_mutexattr_get_ceiling_calls,
              (unsigned long long)pthread_prio_protect_init_calls,
              (unsigned long long)pthread_prio_protect_lock_calls,
              (unsigned long long)pthread_prio_protect_trylock_calls,
              (unsigned long long)pthread_prio_protect_timedlock_calls,
              (unsigned long long)pthread_prio_protect_unlock_calls,
              (unsigned long long)pthread_prio_protect_invalid_ceiling,
              (unsigned long long)pthread_prio_protect_bad_lock_return,
              fifo_min_prio,
              fifo_max_prio,
              (unsigned long long)pcre2_slot_samples,
              (unsigned long long)pcre2_slot_changes,
              (unsigned long long)pcre2_slot_poison_samples,
              pcre2_base,
              (unsigned long long)requested_bytes,
              (unsigned long long)mmap_bytes,
              (unsigned long long)max_request,
              last_fail_func[0] ? last_fail_func : "none",
              (unsigned long long)last_fail_size,
              last_fail_errno,
              emergency_used);
    log_kwin_effects_stacking_state("summary",
                                    (void *)kwin_effects_last_self,
                                    (const void *)kwin_effects_last_caller);
    sync_log();
}

__attribute__((constructor)) static void trace_init(void)
{
    const char *path = getenv("KWIN_ALLOC_TRACE_LOG");
    const char *attempt = getenv("KWIN_ALLOC_TRACE_ATTEMPT");

    if (!path || !path[0])
        path = "/kde-kwin-alloc-trace.log";
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    in_hook++;
    resolve_symbols();
    in_hook--;
    write_log("kwin-alloc-trace: phase=begin pid=%d attempt=%s log=%s\n",
              (int)getpid(), attempt && attempt[0] ? attempt : "unknown", path);
    sample_pcre2_slots("begin", 1);
    sync_log();
}

__attribute__((destructor)) static void trace_fini(void)
{
    write_summary("summary");
    if (log_fd >= 0)
        close(log_fd);
    log_fd = -1;
}

void *malloc(size_t size)
{
    void *ptr;

    if (in_hook)
        return real_malloc ? real_malloc(size) : emergency_alloc(size);
    in_hook++;
    resolve_symbols();
    malloc_calls++;
    note_request(size);
    ptr = real_malloc ? real_malloc(size) : emergency_alloc(size);
    if (!ptr)
        note_alloc_failure("malloc", size, errno);
    in_hook--;
    return ptr;
}

void free(void *ptr)
{
    if (!ptr)
        return;
    if (emergency_ptr(ptr))
        return;
    if (in_hook) {
        if (real_free)
            real_free(ptr);
        return;
    }
    in_hook++;
    resolve_symbols();
    free_calls++;
    if (real_free)
        real_free(ptr);
    in_hook--;
}

void *calloc(size_t nmemb, size_t size)
{
    void *ptr;
    size_t total = nmemb * size;

    if (size != 0 && nmemb > (size_t)-1 / size)
        total = (size_t)-1;
    if (in_hook) {
        ptr = real_calloc ? real_calloc(nmemb, size) : emergency_alloc(total);
        if (ptr && !real_calloc)
            memset(ptr, 0, total);
        return ptr;
    }
    in_hook++;
    resolve_symbols();
    calloc_calls++;
    note_request(total);
    ptr = real_calloc ? real_calloc(nmemb, size) : emergency_alloc(total);
    if (ptr && !real_calloc)
        memset(ptr, 0, total);
    if (!ptr)
        note_alloc_failure("calloc", total, errno);
    in_hook--;
    return ptr;
}

void *realloc(void *old, size_t size)
{
    void *ptr;

    if (emergency_ptr(old))
        old = NULL;
    if (in_hook)
        return real_realloc ? real_realloc(old, size) : emergency_alloc(size);
    in_hook++;
    resolve_symbols();
    realloc_calls++;
    note_request(size);
    ptr = real_realloc ? real_realloc(old, size) : emergency_alloc(size);
    if (!ptr && size != 0)
        note_alloc_failure("realloc", size, errno);
    in_hook--;
    return ptr;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    int rc;

    if (in_hook)
        return real_posix_memalign ? real_posix_memalign(memptr, alignment, size) : ENOMEM;
    in_hook++;
    resolve_symbols();
    posix_memalign_calls++;
    note_request(size);
    rc = real_posix_memalign ? real_posix_memalign(memptr, alignment, size) : ENOMEM;
    if (rc != 0)
        note_alloc_failure("posix_memalign", size, rc);
    in_hook--;
    return rc;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    void *ptr;

    if (in_hook)
        return real_aligned_alloc ? real_aligned_alloc(alignment, size) : NULL;
    in_hook++;
    resolve_symbols();
    aligned_alloc_calls++;
    note_request(size);
    ptr = real_aligned_alloc ? real_aligned_alloc(alignment, size) : NULL;
    if (!ptr)
        note_alloc_failure("aligned_alloc", size, errno);
    in_hook--;
    return ptr;
}

void *memalign(size_t alignment, size_t size)
{
    void *ptr;

    if (in_hook)
        return real_memalign ? real_memalign(alignment, size) : NULL;
    in_hook++;
    resolve_symbols();
    memalign_calls++;
    note_request(size);
    ptr = real_memalign ? real_memalign(alignment, size) : NULL;
    if (!ptr)
        note_alloc_failure("memalign", size, errno);
    in_hook--;
    return ptr;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *ptr;

    if (in_hook)
        return real_mmap ? real_mmap(addr, length, prot, flags, fd, offset) : MAP_FAILED;
    in_hook++;
    resolve_symbols();
    mmap_calls++;
    mmap_bytes += length;
    ptr = real_mmap ? real_mmap(addr, length, prot, flags, fd, offset) : MAP_FAILED;
    if (ptr == MAP_FAILED) {
        mmap_failures++;
        write_log("kwin-alloc-trace: phase=mmap_fail length=%zu prot=0x%x flags=0x%x fd=%d offset=%lld errno=%d mmap_calls=%llu mmap_bytes=%llu\n",
                  length, prot, flags, fd, (long long)offset, errno,
                  (unsigned long long)mmap_calls,
                  (unsigned long long)mmap_bytes);
        sync_log();
    }
    sample_pcre2_slots("mmap", 0);
    in_hook--;
    return ptr;
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd,
             off64_t offset)
{
    void *ptr;

    if (in_hook)
        return real_mmap64 ? real_mmap64(addr, length, prot, flags, fd, offset) : MAP_FAILED;
    in_hook++;
    resolve_symbols();
    mmap_calls++;
    mmap_bytes += length;
    ptr = real_mmap64 ? real_mmap64(addr, length, prot, flags, fd, offset) : MAP_FAILED;
    if (ptr == MAP_FAILED) {
        mmap_failures++;
        write_log("kwin-alloc-trace: phase=mmap64_fail length=%zu prot=0x%x flags=0x%x fd=%d offset=%lld errno=%d mmap_calls=%llu mmap_bytes=%llu\n",
                  length, prot, flags, fd, (long long)offset, errno,
                  (unsigned long long)mmap_calls,
                  (unsigned long long)mmap_bytes);
        sync_log();
    }
    sample_pcre2_slots("mmap64", 0);
    in_hook--;
    return ptr;
}

int munmap(void *addr, size_t length)
{
    int rc;

    if (in_hook)
        return real_munmap ? real_munmap(addr, length) : -1;
    in_hook++;
    resolve_symbols();
    munmap_calls++;
    rc = real_munmap ? real_munmap(addr, length) : -1;
    sample_pcre2_slots("munmap", 0);
    in_hook--;
    return rc;
}

int brk(void *addr)
{
    int rc;

    if (in_hook)
        return real_brk ? real_brk(addr) : -1;
    in_hook++;
    resolve_symbols();
    brk_calls++;
    rc = real_brk ? real_brk(addr) : -1;
    if (rc != 0) {
        brk_failures++;
        write_log("kwin-alloc-trace: phase=brk_fail addr=%p errno=%d brk_calls=%llu\n",
                  addr, errno, (unsigned long long)brk_calls);
        sync_log();
    }
    in_hook--;
    return rc;
}

void *sbrk(intptr_t increment)
{
    void *ptr;

    if (in_hook)
        return real_sbrk ? real_sbrk(increment) : (void *)-1;
    in_hook++;
    resolve_symbols();
    sbrk_calls++;
    ptr = real_sbrk ? real_sbrk(increment) : (void *)-1;
    if (ptr == (void *)-1) {
        brk_failures++;
        write_log("kwin-alloc-trace: phase=sbrk_fail increment=%lld errno=%d sbrk_calls=%llu\n",
                  (long long)increment, errno, (unsigned long long)sbrk_calls);
        sync_log();
    }
    in_hook--;
    return ptr;
}

void *dlopen(const char *filename, int flags)
{
    void *handle;
    int interesting;

    if (in_hook)
        return real_dlopen ? real_dlopen(filename, flags) : NULL;
    in_hook++;
    resolve_symbols();
    dlopen_calls++;
    interesting = filename && (strstr(filename, "pcre2") ||
                               strstr(filename, "Qt5") ||
                               strstr(filename, "KF5") ||
                               strstr(filename, "kwin"));
    sample_pcre2_slots("dlopen-before", 0);
    handle = real_dlopen ? real_dlopen(filename, flags) : NULL;
    if (interesting) {
        write_log("kwin-alloc-trace: phase=dlopen path=%s flags=0x%x handle=%p errno=%d calls=%llu\n",
                  filename, flags, handle, errno,
                  (unsigned long long)dlopen_calls);
    }
    sample_pcre2_slots("dlopen-after", interesting);
    in_hook--;
    return handle;
}

void _ZN4KWin18EffectsHandlerImpl24checkInputWindowStackingEv(void *self)
{
    const void *caller = __builtin_return_address(0);

    if (in_hook) {
        void (*real_fn)(void *) = real_kwin_check_input_window_stacking;

        if (!real_fn)
            real_fn = lookup_kwin_check_input_window_stacking();
        if (real_fn) {
            real_fn(self);
            return;
        }
        _exit(127);
    }

    in_hook++;
    resolve_symbols();
    resolve_kwin_symbols();
    kwin_effects_stacking_calls++;
    kwin_effects_last_self = (uintptr_t)self;
    kwin_effects_last_caller = (uintptr_t)caller;
    in_hook--;

    if (real_kwin_check_input_window_stacking) {
        real_kwin_check_input_window_stacking(self);
        return;
    }

    in_hook++;
    kwin_effects_stacking_real_missing++;
    log_kwin_effects_stacking_state("real_missing", self, caller);
    write_summary("real_missing");
    in_hook--;
    _exit(127);
}

static int trace_pthread_mutex_init(pthread_mutex_t *mutex,
                                    const pthread_mutexattr_t *attr,
                                    const void *caller)
{
    int rc;

    if (in_hook)
        return real_pthread_mutex_init ?
            real_pthread_mutex_init(mutex, attr) : ENOSYS;
    in_hook++;
    resolve_symbols();
    pthread_mutex_init_calls++;
    rc = real_pthread_mutex_init ? real_pthread_mutex_init(mutex, attr) : ENOSYS;
    if (mutex && rc == 0) {
        const struct __pthread_mutex_s *data =
            (const struct __pthread_mutex_s *)mutex;

        if (mutex_is_prio_protect(data)) {
            pthread_prio_protect_init_calls++;
            log_mutex_state("pthread_mutex_init_protect", "init", mutex, rc,
                            caller);
        }
    }
    in_hook--;
    return rc;
}

static int trace_pthread_mutex_lock_common(const char *op,
                                           pthread_mutex_t *mutex,
                                           int timed,
                                           const struct timespec *abstime,
                                           const void *caller)
{
    const struct __pthread_mutex_s *data;
    int protect;
    int rc;

    if (in_hook) {
        if (timed)
            return real_pthread_mutex_timedlock ?
                real_pthread_mutex_timedlock(mutex, abstime) : ENOSYS;
        if (strcmp(op, "trylock") == 0)
            return real_pthread_mutex_trylock ?
                real_pthread_mutex_trylock(mutex) : ENOSYS;
        return real_pthread_mutex_lock ? real_pthread_mutex_lock(mutex) : ENOSYS;
    }
    in_hook++;
    resolve_symbols();
    data = (const struct __pthread_mutex_s *)mutex;
    protect = mutex_is_prio_protect(data);
    if (strcmp(op, "trylock") == 0) {
        pthread_mutex_trylock_calls++;
        if (protect)
            pthread_prio_protect_trylock_calls++;
    } else if (timed) {
        pthread_mutex_timedlock_calls++;
        if (protect)
            pthread_prio_protect_timedlock_calls++;
    } else {
        pthread_mutex_lock_calls++;
        if (protect)
            pthread_prio_protect_lock_calls++;
    }
    if (protect)
        log_mutex_state(timed ? "pthread_mutex_timedlock_enter" :
                        (strcmp(op, "trylock") == 0 ?
                         "pthread_mutex_trylock_enter" :
                         "pthread_mutex_lock_enter"),
                        op, mutex, 0, caller);
    if (timed)
        rc = real_pthread_mutex_timedlock ?
            real_pthread_mutex_timedlock(mutex, abstime) : ENOSYS;
    else if (strcmp(op, "trylock") == 0)
        rc = real_pthread_mutex_trylock ?
            real_pthread_mutex_trylock(mutex) : ENOSYS;
    else
        rc = real_pthread_mutex_lock ? real_pthread_mutex_lock(mutex) : ENOSYS;
    if (protect) {
        if (rc != 0)
            pthread_prio_protect_bad_lock_return++;
        log_mutex_state(timed ? "pthread_mutex_timedlock_exit" :
                        (strcmp(op, "trylock") == 0 ?
                         "pthread_mutex_trylock_exit" :
                         "pthread_mutex_lock_exit"),
                        op, mutex, rc, caller);
    }
    in_hook--;
    return rc;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    return trace_pthread_mutex_init(mutex, attr, __builtin_return_address(0));
}

int __pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    return trace_pthread_mutex_init(mutex, attr, __builtin_return_address(0));
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    return trace_pthread_mutex_lock_common("lock", mutex, 0, NULL,
                                           __builtin_return_address(0));
}

int __pthread_mutex_lock(pthread_mutex_t *mutex)
{
    return trace_pthread_mutex_lock_common("lock", mutex, 0, NULL,
                                           __builtin_return_address(0));
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    return trace_pthread_mutex_lock_common("trylock", mutex, 0, NULL,
                                           __builtin_return_address(0));
}

int __pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    return trace_pthread_mutex_lock_common("trylock", mutex, 0, NULL,
                                           __builtin_return_address(0));
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex,
                            const struct timespec *abstime)
{
    return trace_pthread_mutex_lock_common("timedlock", mutex, 1, abstime,
                                           __builtin_return_address(0));
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    const struct __pthread_mutex_s *data;
    int protect;
    int rc;

    if (in_hook)
        return real_pthread_mutex_unlock ?
            real_pthread_mutex_unlock(mutex) : ENOSYS;
    in_hook++;
    resolve_symbols();
    data = (const struct __pthread_mutex_s *)mutex;
    protect = mutex_is_prio_protect(data);
    pthread_mutex_unlock_calls++;
    if (protect) {
        pthread_prio_protect_unlock_calls++;
        log_mutex_state("pthread_mutex_unlock_enter", "unlock", mutex, 0,
                        __builtin_return_address(0));
    }
    rc = real_pthread_mutex_unlock ? real_pthread_mutex_unlock(mutex) : ENOSYS;
    if (protect && rc != 0)
        log_mutex_state("pthread_mutex_unlock_exit", "unlock", mutex, rc,
                        __builtin_return_address(0));
    in_hook--;
    return rc;
}

int __pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    return pthread_mutex_unlock(mutex);
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol)
{
    int rc;

    if (in_hook)
        return real_pthread_mutexattr_setprotocol ?
            real_pthread_mutexattr_setprotocol(attr, protocol) : ENOSYS;
    in_hook++;
    resolve_symbols();
    pthread_mutexattr_protocol_calls++;
    rc = real_pthread_mutexattr_setprotocol ?
        real_pthread_mutexattr_setprotocol(attr, protocol) : ENOSYS;
    if (protocol == PTHREAD_PRIO_PROTECT || rc != 0)
        log_attr_state("pthread_mutexattr_setprotocol", attr, protocol, rc);
    in_hook--;
    return rc;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int prioceiling)
{
    int rc;

    if (in_hook)
        return real_pthread_mutexattr_setprioceiling ?
            real_pthread_mutexattr_setprioceiling(attr, prioceiling) : ENOSYS;
    in_hook++;
    resolve_symbols();
    pthread_mutexattr_ceiling_calls++;
    rc = real_pthread_mutexattr_setprioceiling ?
        real_pthread_mutexattr_setprioceiling(attr, prioceiling) : ENOSYS;
    log_attr_state("pthread_mutexattr_setprioceiling", attr, prioceiling, rc);
    in_hook--;
    return rc;
}

int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *attr,
                                     int *prioceiling)
{
    int rc;

    if (in_hook)
        return real_pthread_mutexattr_getprioceiling ?
            real_pthread_mutexattr_getprioceiling(attr, prioceiling) : ENOSYS;
    in_hook++;
    resolve_symbols();
    pthread_mutexattr_get_ceiling_calls++;
    rc = real_pthread_mutexattr_getprioceiling ?
        real_pthread_mutexattr_getprioceiling(attr, prioceiling) : ENOSYS;
    if (rc != 0)
        log_attr_state("pthread_mutexattr_getprioceiling", attr,
                       *prioceiling, rc);
    in_hook--;
    return rc;
}

static void pcre2_enter_log(const char *func, uint64_t calls)
{
    write_log("kwin-alloc-trace: phase=%s_enter calls=%llu alloc_failures=%llu mmap_failures=%llu pcre2_base=0x%016" PRIxPTR "\n",
              func, (unsigned long long)calls,
              (unsigned long long)alloc_failures,
              (unsigned long long)mmap_failures,
              pcre2_base);
}

static void pcre2_exit_log(const char *func, long long rc, const void *ptr,
                           uint64_t calls)
{
    write_log("kwin-alloc-trace: phase=%s_exit rc=%lld ptr=%p calls=%llu pcre2_slot_samples=%llu pcre2_slot_changes=%llu pcre2_slot_poison_samples=%llu\n",
              func, rc, ptr, (unsigned long long)calls,
              (unsigned long long)pcre2_slot_samples,
              (unsigned long long)pcre2_slot_changes,
              (unsigned long long)pcre2_slot_poison_samples);
}

int pcre2_match_16(const void *code, const uint16_t *subject, size_t length,
                   size_t startoffset, uint32_t options, void *match_data,
                   void *mcontext)
{
    int rc;

    if (in_hook) {
        if (!real_pcre2_match_16)
            abort();
        return real_pcre2_match_16(code, subject, length, startoffset,
                                   options, match_data, mcontext);
    }
    in_hook++;
    resolve_pcre2_symbols();
    if (!real_pcre2_match_16)
        abort();
    pcre2_match_calls++;
    pcre2_enter_log("pcre2_match", pcre2_match_calls);
    sample_pcre2_slots("pcre2_match-enter", 1);
    sync_log();
    rc = real_pcre2_match_16(code, subject, length, startoffset, options,
                             match_data, mcontext);
    sample_pcre2_slots("pcre2_match-exit", 1);
    pcre2_exit_log("pcre2_match", rc, NULL, pcre2_match_calls);
    in_hook--;
    return rc;
}

void *pcre2_compile_16(const uint16_t *pattern, size_t length,
                       uint32_t options, int *errorcode, size_t *erroroffset,
                       void *gcontext)
{
    void *code;

    if (in_hook) {
        if (!real_pcre2_compile_16)
            abort();
        return real_pcre2_compile_16(pattern, length, options, errorcode,
                                     erroroffset, gcontext);
    }
    in_hook++;
    resolve_pcre2_symbols();
    if (!real_pcre2_compile_16)
        abort();
    pcre2_compile_calls++;
    pcre2_enter_log("pcre2_compile", pcre2_compile_calls);
    sample_pcre2_slots("pcre2_compile-enter", 1);
    sync_log();
    code = real_pcre2_compile_16(pattern, length, options, errorcode,
                                 erroroffset, gcontext);
    sample_pcre2_slots("pcre2_compile-exit", 1);
    pcre2_exit_log("pcre2_compile", code ? 0 : -1, code,
                   pcre2_compile_calls);
    in_hook--;
    return code;
}

void *pcre2_match_data_create_from_pattern_16(const void *code, void *gcontext)
{
    void *match_data;

    if (in_hook) {
        if (!real_pcre2_match_data_create_from_pattern_16)
            abort();
        return real_pcre2_match_data_create_from_pattern_16(code, gcontext);
    }
    in_hook++;
    resolve_pcre2_symbols();
    if (!real_pcre2_match_data_create_from_pattern_16)
        abort();
    pcre2_lifetime_calls++;
    pcre2_enter_log("pcre2_match_data_create_from_pattern",
                    pcre2_lifetime_calls);
    sample_pcre2_slots("pcre2_match_data_create-enter", 1);
    match_data = real_pcre2_match_data_create_from_pattern_16(code, gcontext);
    sample_pcre2_slots("pcre2_match_data_create-exit", 1);
    pcre2_exit_log("pcre2_match_data_create_from_pattern",
                   match_data ? 0 : -1, match_data, pcre2_lifetime_calls);
    in_hook--;
    return match_data;
}

void pcre2_match_data_free_16(void *match_data)
{
    if (in_hook) {
        if (real_pcre2_match_data_free_16)
            real_pcre2_match_data_free_16(match_data);
        return;
    }
    in_hook++;
    resolve_pcre2_symbols();
    pcre2_lifetime_calls++;
    pcre2_enter_log("pcre2_match_data_free", pcre2_lifetime_calls);
    sample_pcre2_slots("pcre2_match_data_free-enter", 1);
    if (real_pcre2_match_data_free_16)
        real_pcre2_match_data_free_16(match_data);
    sample_pcre2_slots("pcre2_match_data_free-exit", 1);
    pcre2_exit_log("pcre2_match_data_free", 0, match_data,
                   pcre2_lifetime_calls);
    in_hook--;
}

void *pcre2_match_context_create_16(void *gcontext)
{
    void *mcontext;

    if (in_hook) {
        if (!real_pcre2_match_context_create_16)
            abort();
        return real_pcre2_match_context_create_16(gcontext);
    }
    in_hook++;
    resolve_pcre2_symbols();
    if (!real_pcre2_match_context_create_16)
        abort();
    pcre2_lifetime_calls++;
    pcre2_enter_log("pcre2_match_context_create", pcre2_lifetime_calls);
    sample_pcre2_slots("pcre2_match_context_create-enter", 1);
    mcontext = real_pcre2_match_context_create_16(gcontext);
    sample_pcre2_slots("pcre2_match_context_create-exit", 1);
    pcre2_exit_log("pcre2_match_context_create", mcontext ? 0 : -1,
                   mcontext, pcre2_lifetime_calls);
    in_hook--;
    return mcontext;
}

void pcre2_match_context_free_16(void *mcontext)
{
    if (in_hook) {
        if (real_pcre2_match_context_free_16)
            real_pcre2_match_context_free_16(mcontext);
        return;
    }
    in_hook++;
    resolve_pcre2_symbols();
    pcre2_lifetime_calls++;
    pcre2_enter_log("pcre2_match_context_free", pcre2_lifetime_calls);
    sample_pcre2_slots("pcre2_match_context_free-enter", 1);
    if (real_pcre2_match_context_free_16)
        real_pcre2_match_context_free_16(mcontext);
    sample_pcre2_slots("pcre2_match_context_free-exit", 1);
    pcre2_exit_log("pcre2_match_context_free", 0, mcontext,
                   pcre2_lifetime_calls);
    in_hook--;
}

void pcre2_code_free_16(void *code)
{
    if (in_hook) {
        if (real_pcre2_code_free_16)
            real_pcre2_code_free_16(code);
        return;
    }
    in_hook++;
    resolve_pcre2_symbols();
    pcre2_lifetime_calls++;
    pcre2_enter_log("pcre2_code_free", pcre2_lifetime_calls);
    sample_pcre2_slots("pcre2_code_free-enter", 1);
    if (real_pcre2_code_free_16)
        real_pcre2_code_free_16(code);
    sample_pcre2_slots("pcre2_code_free-exit", 1);
    pcre2_exit_log("pcre2_code_free", 0, code, pcre2_lifetime_calls);
    in_hook--;
}

int pcre2_dfa_match_16(const void *code, const uint16_t *subject,
                       size_t length, size_t startoffset, uint32_t options,
                       void *match_data, void *mcontext, int *workspace,
                       size_t wscount)
{
    int rc;

    if (in_hook) {
        if (!real_pcre2_dfa_match_16)
            abort();
        return real_pcre2_dfa_match_16(code, subject, length, startoffset,
                                       options, match_data, mcontext,
                                       workspace, wscount);
    }
    in_hook++;
    resolve_pcre2_symbols();
    if (!real_pcre2_dfa_match_16)
        abort();
    pcre2_dfa_match_calls++;
    pcre2_enter_log("pcre2_dfa_match", pcre2_dfa_match_calls);
    sample_pcre2_slots("pcre2_dfa_match-enter", 1);
    sync_log();
    rc = real_pcre2_dfa_match_16(code, subject, length, startoffset, options,
                                 match_data, mcontext, workspace, wscount);
    sample_pcre2_slots("pcre2_dfa_match-exit", 1);
    pcre2_exit_log("pcre2_dfa_match", rc, NULL, pcre2_dfa_match_calls);
    in_hook--;
    return rc;
}

static void *trace_new_call(const char *name, size_t size,
                            void *(*real_func)(size_t))
{
    void *ptr;

    if (in_hook)
        return real_func ? real_func(size) : NULL;
    in_hook++;
    resolve_symbols();
    if (strcmp(name, "_Znam") == 0)
        new_array_calls++;
    else
        new_calls++;
    note_request(size);
    in_hook--;
    ptr = real_func ? real_func(size) : NULL;
    in_hook++;
    if (!ptr)
        note_alloc_failure(name, size, errno);
    in_hook--;
    return ptr;
}

void *_Znwm(size_t size)
{
    return trace_new_call("_Znwm", size, real_new);
}

void *_Znam(size_t size)
{
    return trace_new_call("_Znam", size, real_new_array);
}

void __cxa_throw(void *thrown_exception, void *tinfo, void (*dest)(void *))
{
    if (!in_hook) {
        in_hook++;
        resolve_symbols();
        throw_calls++;
        write_log("kwin-alloc-trace: phase=cxa_throw exception_type=%s exception=%p tinfo=%p alloc_failures=%llu mmap_failures=%llu brk_failures=%llu last_fail_func=%s last_fail_size=%llu last_fail_errno=%d requested_bytes=%llu max_request=%llu\n",
                  typeinfo_name(tinfo), thrown_exception, tinfo,
                  (unsigned long long)alloc_failures,
                  (unsigned long long)mmap_failures,
                  (unsigned long long)brk_failures,
                  last_fail_func[0] ? last_fail_func : "none",
                  (unsigned long long)last_fail_size,
                  last_fail_errno,
                  (unsigned long long)requested_bytes,
                  (unsigned long long)max_request);
        write_summary("throw-summary");
        in_hook--;
    }
    if (!real_cxa_throw) {
        resolve_symbols();
        if (!real_cxa_throw)
            abort();
    }
    real_cxa_throw(thrown_exception, tinfo, dest);
    abort();
}
