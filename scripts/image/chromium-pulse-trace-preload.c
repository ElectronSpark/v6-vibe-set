#define _GNU_SOURCE

/*
 * Handle-aware, default-off libpulse call tracer for the Chromium A2
 * localizer.  This interposer deliberately exports only dlopen/dlsym.  Pulse
 * wrappers are returned only for symbols resolved from an exact
 * libpulse.so.0 dlopen handle, so RTLD_LOCAL providers are traced without
 * changing unrelated symbol lookup.
 */

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct pa_context pa_context;
typedef struct pa_stream pa_stream;
typedef struct pa_operation pa_operation;
typedef struct pa_cvolume pa_cvolume;
typedef struct pa_proplist pa_proplist;

typedef struct pa_sample_spec {
    int format;
    uint32_t rate;
    uint8_t channels;
} pa_sample_spec;

typedef struct pa_channel_map {
    uint8_t channels;
    int map[32];
} pa_channel_map;

typedef struct pa_buffer_attr {
    uint32_t maxlength;
    uint32_t tlength;
    uint32_t prebuf;
    uint32_t minreq;
    uint32_t fragsize;
} pa_buffer_attr;

typedef void (*pa_context_notify_cb_t)(pa_context *, void *);
typedef void (*pa_stream_notify_cb_t)(pa_stream *, void *);
typedef void (*pa_stream_request_cb_t)(pa_stream *, size_t, void *);
typedef void (*pa_stream_success_cb_t)(pa_stream *, int, void *);
typedef void (*pa_free_cb_t)(void *);

enum {
    MAX_HANDLES = 4,
    MAX_CONTEXTS = 64,
    MAX_STREAMS = 128,
    MAX_OPERATIONS = 512,
    MAX_SUCCESS_SLOTS = 512,
    MAX_FREE_SLOTS = 256,
    ESSENTIAL_CAP = 8192,
    HOT_CAP = 64,
    FAILURE_CAP = 256
};

enum trace_priority { TRACE_ESSENTIAL, TRACE_HOT, TRACE_FAILURE, TRACE_FORCE };

enum symbol_id {
    SYM_CONTEXT_NEW,
    SYM_CONTEXT_CONNECT,
    SYM_CONTEXT_GET_STATE,
    SYM_CONTEXT_ERRNO,
    SYM_CONTEXT_SET_STATE_CALLBACK,
    SYM_CONTEXT_DISCONNECT,
    SYM_CONTEXT_UNREF,
    SYM_STREAM_NEW,
    SYM_STREAM_NEW_WITH_PROPLIST,
    SYM_STREAM_CONNECT_PLAYBACK,
    SYM_STREAM_GET_STATE,
    SYM_STREAM_BEGIN_WRITE,
    SYM_STREAM_WRITE,
    SYM_STREAM_SET_STATE_CALLBACK,
    SYM_STREAM_SET_WRITE_CALLBACK,
    SYM_STREAM_CORK,
    SYM_STREAM_FLUSH,
    SYM_STREAM_DISCONNECT,
    SYM_STREAM_UNREF,
    SYM_OPERATION_GET_STATE,
    SYM_OPERATION_UNREF,
    SYM_STREAM_GET_BUFFER_ATTR,
    SYM_COUNT
};

struct binding {
    const char *name;
    _Atomic(uintptr_t) provider[MAX_HANDLES];
    _Atomic(unsigned) requested[MAX_HANDLES];
};

struct tracked_handle {
    _Atomic(uintptr_t) handle;
    unsigned id;
    char request_path[256];
    _Atomic(unsigned) identity_emitted;
};

struct context_entry {
    pa_context *pointer;
    unsigned id;
    int active;
    pa_context_notify_cb_t notify;
    void *notify_userdata;
};

struct stream_entry {
    pa_stream *pointer;
    unsigned id;
    unsigned context_id;
    int active;
    unsigned cork_calls;
    unsigned flush_calls;
    pa_stream_notify_cb_t state_cb;
    void *state_userdata;
    pa_stream_request_cb_t write_cb;
    void *write_userdata;
};

struct operation_entry {
    pa_operation *pointer;
    unsigned id;
    unsigned stream_id;
    int active;
    char action[24];
};

struct success_slot {
    int used;
    unsigned stream_id;
    char action[24];
    pa_stream_success_cb_t callback;
    void *userdata;
};

struct free_slot {
    int used;
    void *data;
    unsigned stream_id;
    pa_free_cb_t callback;
};

static struct binding bindings[SYM_COUNT] = {
    [SYM_CONTEXT_NEW] = { "pa_context_new" },
    [SYM_CONTEXT_CONNECT] = { "pa_context_connect" },
    [SYM_CONTEXT_GET_STATE] = { "pa_context_get_state" },
    [SYM_CONTEXT_ERRNO] = { "pa_context_errno" },
    [SYM_CONTEXT_SET_STATE_CALLBACK] = { "pa_context_set_state_callback" },
    [SYM_CONTEXT_DISCONNECT] = { "pa_context_disconnect" },
    [SYM_CONTEXT_UNREF] = { "pa_context_unref" },
    [SYM_STREAM_NEW] = { "pa_stream_new" },
    [SYM_STREAM_NEW_WITH_PROPLIST] = { "pa_stream_new_with_proplist" },
    [SYM_STREAM_CONNECT_PLAYBACK] = { "pa_stream_connect_playback" },
    [SYM_STREAM_GET_STATE] = { "pa_stream_get_state" },
    [SYM_STREAM_BEGIN_WRITE] = { "pa_stream_begin_write" },
    [SYM_STREAM_WRITE] = { "pa_stream_write" },
    [SYM_STREAM_SET_STATE_CALLBACK] = { "pa_stream_set_state_callback" },
    [SYM_STREAM_SET_WRITE_CALLBACK] = { "pa_stream_set_write_callback" },
    [SYM_STREAM_CORK] = { "pa_stream_cork" },
    [SYM_STREAM_FLUSH] = { "pa_stream_flush" },
    [SYM_STREAM_DISCONNECT] = { "pa_stream_disconnect" },
    [SYM_STREAM_UNREF] = { "pa_stream_unref" },
    [SYM_OPERATION_GET_STATE] = { "pa_operation_get_state" },
    [SYM_OPERATION_UNREF] = { "pa_operation_unref" },
    [SYM_STREAM_GET_BUFFER_ATTR] = { "pa_stream_get_buffer_attr" }
};

static pthread_once_t loader_once = PTHREAD_ONCE_INIT;
static pthread_once_t trace_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER;
static void *(*real_dlopen_fn)(const char *, int);
static void *(*real_dlsym_fn)(void *, const char *);
static struct tracked_handle handles[MAX_HANDLES];
static struct context_entry contexts[MAX_CONTEXTS];
static struct stream_entry streams[MAX_STREAMS];
static struct operation_entry operations[MAX_OPERATIONS];
static struct success_slot success_slots[MAX_SUCCESS_SLOTS];
static struct free_slot free_slots[MAX_FREE_SLOTS];
static _Atomic(unsigned) next_handle_id = 1;
static unsigned next_context_id = 1;
static unsigned next_stream_id = 1;
static unsigned next_operation_id = 1;
static unsigned long long sequence;
static unsigned essential_rows;
static unsigned hot_rows;
static unsigned failure_rows;
static unsigned long long hot_seen;
static unsigned long long hot_suppressed;
static unsigned overflow_count;
static unsigned pointer_reuse_count;
static unsigned fork_resets;
static pid_t trace_pid;
static int trace_enabled;
static int trace_fd = -1;
static char trace_path[256];
static _Thread_local unsigned trace_depth;
static _Thread_local unsigned dispatch_slot;

static pa_context *wrap_pa_context_new(void *, const char *);
static int wrap_pa_context_connect(pa_context *, const char *, int, const void *);
static int wrap_pa_context_get_state(const pa_context *);
static int wrap_pa_context_errno(const pa_context *);
static void wrap_pa_context_set_state_callback(pa_context *, pa_context_notify_cb_t, void *);
static void wrap_pa_context_disconnect(pa_context *);
static void wrap_pa_context_unref(pa_context *);
static pa_stream *wrap_pa_stream_new(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *);
static pa_stream *wrap_pa_stream_new_with_proplist(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *, pa_proplist *);
static int wrap_pa_stream_connect_playback(pa_stream *, const char *, const pa_buffer_attr *, int, const pa_cvolume *, pa_stream *);
static int wrap_pa_stream_get_state(const pa_stream *);
static int wrap_pa_stream_begin_write(pa_stream *, void **, size_t *);
static int wrap_pa_stream_write(pa_stream *, const void *, size_t, pa_free_cb_t, int64_t, int);
static void wrap_pa_stream_set_state_callback(pa_stream *, pa_stream_notify_cb_t, void *);
static void wrap_pa_stream_set_write_callback(pa_stream *, pa_stream_request_cb_t, void *);
static pa_operation *wrap_pa_stream_cork(pa_stream *, int, pa_stream_success_cb_t, void *);
static pa_operation *wrap_pa_stream_flush(pa_stream *, pa_stream_success_cb_t, void *);
static int wrap_pa_stream_disconnect(pa_stream *);
static void wrap_pa_stream_unref(pa_stream *);
static int wrap_pa_operation_get_state(const pa_operation *);
static void wrap_pa_operation_unref(pa_operation *);
static const pa_buffer_attr *wrap_pa_stream_get_buffer_attr(const pa_stream *);

static long trace_tid(void)
{
    return (long)syscall(SYS_gettid);
}

static unsigned long long monotonic_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (unsigned long long)now.tv_sec * 1000000000ULL +
           (unsigned long long)now.tv_nsec;
}

static int safe_path(const char *path)
{
    const unsigned char *cursor;

    if (!path || path[0] != '/' || strlen(path) >= sizeof(trace_path))
        return 0;
    for (cursor = (const unsigned char *)path; *cursor; cursor++) {
        if ((*cursor >= 'a' && *cursor <= 'z') ||
            (*cursor >= 'A' && *cursor <= 'Z') ||
            (*cursor >= '0' && *cursor <= '9') || *cursor == '/' ||
            *cursor == '.' || *cursor == '_' || *cursor == '-')
            continue;
        return 0;
    }
    return strstr(path, "../") == NULL;
}

static const char *path_basename(const char *path)
{
    const char *slash;

    if (!path)
        return "";
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void sanitize_token(char *output, size_t capacity, const char *input)
{
    size_t used = 0;
    const unsigned char *cursor = (const unsigned char *)(input ? input : "null");

    if (capacity == 0)
        return;
    while (*cursor && used + 1 < capacity) {
        unsigned char ch = *cursor++;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '/' || ch == '.' ||
            ch == '_' || ch == '-' || ch == ',') {
            output[used++] = (char)ch;
        } else {
            output[used++] = '_';
        }
    }
    output[used] = '\0';
}

static void reset_child_state_locked(void)
{
    size_t handle_index;

    memset(contexts, 0, sizeof(contexts));
    memset(streams, 0, sizeof(streams));
    memset(operations, 0, sizeof(operations));
    memset(success_slots, 0, sizeof(success_slots));
    memset(free_slots, 0, sizeof(free_slots));
    next_context_id = 1;
    next_stream_id = 1;
    next_operation_id = 1;
    sequence = 0;
    essential_rows = 0;
    hot_rows = 0;
    failure_rows = 0;
    hot_seen = 0;
    hot_suppressed = 0;
    overflow_count = 0;
    pointer_reuse_count = 0;
    fork_resets++;
    trace_pid = getpid();
    for (handle_index = 0; handle_index < MAX_HANDLES; handle_index++) {
        if (atomic_load_explicit(&handles[handle_index].handle,
                                 memory_order_relaxed) > 1) {
            atomic_store_explicit(&handles[handle_index].identity_emitted, 0,
                                  memory_order_relaxed);
        }
    }
}

static void atfork_prepare(void)
{
    pthread_mutex_lock(&trace_lock);
}

static void atfork_parent(void)
{
    pthread_mutex_unlock(&trace_lock);
}

static void atfork_child(void)
{
    reset_child_state_locked();
    pthread_mutex_unlock(&trace_lock);
}

static void initialize_loader(void)
{
    real_dlopen_fn = (void *(*)(const char *, int))
        dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.2.5");
    real_dlsym_fn = (void *(*)(void *, const char *))
        dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
}

static void initialize_trace(void)
{
    const char *gate = getenv("XV6_PULSE_TRACE");
    const char *path = getenv("XV6_PULSE_TRACE_LOG");

    trace_pid = getpid();
    (void)pthread_atfork(atfork_prepare, atfork_parent, atfork_child);
    if (!gate || strcmp(gate, "1") != 0 || !safe_path(path))
        return;
    memcpy(trace_path, path, strlen(path) + 1);
    trace_fd = open(trace_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (trace_fd >= 0)
        trace_enabled = 1;
}

__attribute__((constructor)) static void trace_constructor(void)
{
    pthread_once(&loader_once, initialize_loader);
    pthread_once(&trace_once, initialize_trace);
}

static int admit_row_locked(enum trace_priority priority)
{
    if (priority == TRACE_FORCE)
        return 1;
    if (priority == TRACE_FAILURE) {
        if (failure_rows >= FAILURE_CAP)
            return 0;
        failure_rows++;
        return 1;
    }
    if (priority == TRACE_HOT) {
        hot_seen++;
        if (hot_rows >= HOT_CAP) {
            hot_suppressed++;
            return 0;
        }
        hot_rows++;
        return 1;
    }
    if (essential_rows >= ESSENTIAL_CAP)
        return 0;
    essential_rows++;
    return 1;
}

static void trace_event(enum trace_priority priority, const char *format, ...)
{
    char body[1152];
    char row[1536];
    va_list args;
    int saved_errno = errno;
    int length;

    if (!trace_enabled || trace_depth != 0)
        return;
    trace_depth++;
    pthread_mutex_lock(&trace_lock);
    if (trace_pid != getpid())
        reset_child_state_locked();
    if (!admit_row_locked(priority)) {
        pthread_mutex_unlock(&trace_lock);
        trace_depth--;
        errno = saved_errno;
        return;
    }
    va_start(args, format);
    (void)vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    sequence++;
    length = snprintf(row, sizeof(row),
        "CHROMIUM_PULSE_TRACE_V2 schema=2 pid=%ld tid=%ld seq=%llu mono_ns=%llu %s\n",
        (long)getpid(), trace_tid(), sequence, monotonic_ns(), body);
    if (length > 0 && trace_fd >= 0) {
        size_t bytes = (size_t)length < sizeof(row) ?
            (size_t)length : sizeof(row) - 1;
        ssize_t ignored = write(trace_fd, row, bytes);
        (void)ignored;
    }
    pthread_mutex_unlock(&trace_lock);
    trace_depth--;
    errno = saved_errno;
}

static struct tracked_handle *tracked_for(void *handle)
{
    uintptr_t value = (uintptr_t)handle;
    size_t index;

    for (index = 0; index < MAX_HANDLES; index++) {
        if (atomic_load_explicit(&handles[index].handle, memory_order_acquire) == value)
            return &handles[index];
    }
    return NULL;
}

static void track_handle(void *handle, const char *path)
{
    size_t index;

    if (!handle)
        return;
    for (index = 0; index < MAX_HANDLES; index++)
        if (atomic_load_explicit(&handles[index].handle,
                                 memory_order_acquire) == (uintptr_t)handle)
            return;
    for (index = 0; index < MAX_HANDLES; index++) {
        uintptr_t expected = 0;
        if (!atomic_compare_exchange_strong_explicit(&handles[index].handle,
                &expected, (uintptr_t)1, memory_order_acq_rel,
                memory_order_relaxed))
            continue;
        handles[index].id = atomic_fetch_add_explicit(&next_handle_id, 1,
                                                       memory_order_relaxed);
        sanitize_token(handles[index].request_path,
                       sizeof(handles[index].request_path), path);
        atomic_store_explicit(&handles[index].identity_emitted, 0,
                              memory_order_relaxed);
        atomic_store_explicit(&handles[index].handle, (uintptr_t)handle,
                              memory_order_release);
        return;
    }
}

struct provider_identity {
    uintptr_t base;
    char build_id[129];
};

static int build_id_callback(struct dl_phdr_info *info, size_t size, void *data)
{
    struct provider_identity *identity = data;
    size_t segment;

    (void)size;
    if ((uintptr_t)info->dlpi_addr != identity->base)
        return 0;
    for (segment = 0; segment < info->dlpi_phnum; segment++) {
        const ElfW(Phdr) *phdr = &info->dlpi_phdr[segment];
        const unsigned char *cursor;
        const unsigned char *end;

        if (phdr->p_type != PT_NOTE)
            continue;
        cursor = (const unsigned char *)(info->dlpi_addr + phdr->p_vaddr);
        end = cursor + phdr->p_memsz;
        while ((size_t)(end - cursor) >= sizeof(ElfW(Nhdr))) {
            const ElfW(Nhdr) *note = (const ElfW(Nhdr) *)cursor;
            const unsigned char *name;
            const unsigned char *description;
            size_t namesz = (note->n_namesz + 3U) & ~3U;
            size_t descsz = (note->n_descsz + 3U) & ~3U;
            size_t total = sizeof(*note) + namesz + descsz;
            size_t byte;

            if (total > (size_t)(end - cursor))
                break;
            name = cursor + sizeof(*note);
            description = name + namesz;
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz >= 3 &&
                memcmp(name, "GNU", 3) == 0 && note->n_descsz > 0 &&
                note->n_descsz * 2 < sizeof(identity->build_id)) {
                for (byte = 0; byte < note->n_descsz; byte++)
                    (void)snprintf(identity->build_id + byte * 2, 3, "%02x",
                                   description[byte]);
                return 1;
            }
            cursor += total;
        }
    }
    return 1;
}

static void emit_provider_identity(uintptr_t handle_value)
{
    struct tracked_handle *tracked = tracked_for((void *)handle_value);
    struct link_map *map = NULL;
    struct provider_identity identity;
    const char *soname = "unavailable";
    char path[256];
    char soname_token[128];
    ElfW(Dyn) *dynamic;
    const char *string_table = NULL;
    unsigned expected = 0;

    if (!trace_enabled || !tracked ||
        !atomic_compare_exchange_strong_explicit(&tracked->identity_emitted,
            &expected, 1, memory_order_acq_rel, memory_order_relaxed))
        return;
    memset(&identity, 0, sizeof(identity));
    if (dlinfo((void *)handle_value, RTLD_DI_LINKMAP, &map) == 0 && map) {
        identity.base = (uintptr_t)map->l_addr;
        if (map->l_name && map->l_name[0] != '\0')
            sanitize_token(path, sizeof(path), map->l_name);
        else
            memcpy(path, tracked->request_path, strlen(tracked->request_path) + 1);
        for (dynamic = map->l_ld; dynamic && dynamic->d_tag != DT_NULL; dynamic++) {
            if (dynamic->d_tag == DT_STRTAB)
                string_table = (const char *)(uintptr_t)dynamic->d_un.d_ptr;
        }
        if (string_table) {
            for (dynamic = map->l_ld; dynamic && dynamic->d_tag != DT_NULL;
                 dynamic++) {
                if (dynamic->d_tag == DT_SONAME) {
                    soname = string_table + dynamic->d_un.d_val;
                    break;
                }
            }
        }
        (void)dl_iterate_phdr(build_id_callback, &identity);
    } else {
        memcpy(path, tracked->request_path, strlen(tracked->request_path) + 1);
    }
    sanitize_token(soname_token, sizeof(soname_token), soname);
    trace_event(TRACE_ESSENTIAL,
        "event=provider provider_id=%u handle_id=%u path=%s soname=%s build_id=%s errno_before=0 errno_after=0",
        tracked->id, tracked->id, path, soname_token,
        identity.build_id[0] ? identity.build_id : "unavailable");
}

static int symbol_index(const char *name)
{
    int index;

    if (!name)
        return -1;
    for (index = 0; index < SYM_COUNT; index++) {
        if (strcmp(name, bindings[index].name) == 0)
            return index;
    }
    return -1;
}

#define SLOT_BEGIN(N) unsigned previous_slot = dispatch_slot; dispatch_slot = (N)
#define SLOT_END() dispatch_slot = previous_slot

#define DEFINE_SLOT_WRAPPERS(N) \
static pa_context *slot##N##_context_new(void *a, const char *b) { SLOT_BEGIN(N); pa_context *r = wrap_pa_context_new(a, b); SLOT_END(); return r; } \
static int slot##N##_context_connect(pa_context *a, const char *b, int c, const void *d) { SLOT_BEGIN(N); int r = wrap_pa_context_connect(a, b, c, d); SLOT_END(); return r; } \
static int slot##N##_context_get_state(const pa_context *a) { SLOT_BEGIN(N); int r = wrap_pa_context_get_state(a); SLOT_END(); return r; } \
static int slot##N##_context_errno(const pa_context *a) { SLOT_BEGIN(N); int r = wrap_pa_context_errno(a); SLOT_END(); return r; } \
static void slot##N##_context_set_state_callback(pa_context *a, pa_context_notify_cb_t b, void *c) { SLOT_BEGIN(N); wrap_pa_context_set_state_callback(a, b, c); SLOT_END(); } \
static void slot##N##_context_disconnect(pa_context *a) { SLOT_BEGIN(N); wrap_pa_context_disconnect(a); SLOT_END(); } \
static void slot##N##_context_unref(pa_context *a) { SLOT_BEGIN(N); wrap_pa_context_unref(a); SLOT_END(); } \
static pa_stream *slot##N##_stream_new(pa_context *a, const char *b, const pa_sample_spec *c, const pa_channel_map *d) { SLOT_BEGIN(N); pa_stream *r = wrap_pa_stream_new(a, b, c, d); SLOT_END(); return r; } \
static pa_stream *slot##N##_stream_new_with_proplist(pa_context *a, const char *b, const pa_sample_spec *c, const pa_channel_map *d, pa_proplist *e) { SLOT_BEGIN(N); pa_stream *r = wrap_pa_stream_new_with_proplist(a, b, c, d, e); SLOT_END(); return r; } \
static int slot##N##_stream_connect_playback(pa_stream *a, const char *b, const pa_buffer_attr *c, int d, const pa_cvolume *e, pa_stream *f) { SLOT_BEGIN(N); int r = wrap_pa_stream_connect_playback(a, b, c, d, e, f); SLOT_END(); return r; } \
static int slot##N##_stream_get_state(const pa_stream *a) { SLOT_BEGIN(N); int r = wrap_pa_stream_get_state(a); SLOT_END(); return r; } \
static int slot##N##_stream_begin_write(pa_stream *a, void **b, size_t *c) { SLOT_BEGIN(N); int r = wrap_pa_stream_begin_write(a, b, c); SLOT_END(); return r; } \
static int slot##N##_stream_write(pa_stream *a, const void *b, size_t c, pa_free_cb_t d, int64_t e, int f) { SLOT_BEGIN(N); int r = wrap_pa_stream_write(a, b, c, d, e, f); SLOT_END(); return r; } \
static void slot##N##_stream_set_state_callback(pa_stream *a, pa_stream_notify_cb_t b, void *c) { SLOT_BEGIN(N); wrap_pa_stream_set_state_callback(a, b, c); SLOT_END(); } \
static void slot##N##_stream_set_write_callback(pa_stream *a, pa_stream_request_cb_t b, void *c) { SLOT_BEGIN(N); wrap_pa_stream_set_write_callback(a, b, c); SLOT_END(); } \
static pa_operation *slot##N##_stream_cork(pa_stream *a, int b, pa_stream_success_cb_t c, void *d) { SLOT_BEGIN(N); pa_operation *r = wrap_pa_stream_cork(a, b, c, d); SLOT_END(); return r; } \
static pa_operation *slot##N##_stream_flush(pa_stream *a, pa_stream_success_cb_t b, void *c) { SLOT_BEGIN(N); pa_operation *r = wrap_pa_stream_flush(a, b, c); SLOT_END(); return r; } \
static int slot##N##_stream_disconnect(pa_stream *a) { SLOT_BEGIN(N); int r = wrap_pa_stream_disconnect(a); SLOT_END(); return r; } \
static void slot##N##_stream_unref(pa_stream *a) { SLOT_BEGIN(N); wrap_pa_stream_unref(a); SLOT_END(); } \
static int slot##N##_operation_get_state(const pa_operation *a) { SLOT_BEGIN(N); int r = wrap_pa_operation_get_state(a); SLOT_END(); return r; } \
static void slot##N##_operation_unref(pa_operation *a) { SLOT_BEGIN(N); wrap_pa_operation_unref(a); SLOT_END(); } \
static const pa_buffer_attr *slot##N##_stream_get_buffer_attr(const pa_stream *a) { SLOT_BEGIN(N); const pa_buffer_attr *r = wrap_pa_stream_get_buffer_attr(a); SLOT_END(); return r; }

DEFINE_SLOT_WRAPPERS(0)
DEFINE_SLOT_WRAPPERS(1)
DEFINE_SLOT_WRAPPERS(2)
DEFINE_SLOT_WRAPPERS(3)

#define PICK_SLOT(name) (slot == 0 ? (void *)slot0_##name : \
                         slot == 1 ? (void *)slot1_##name : \
                         slot == 2 ? (void *)slot2_##name : \
                         slot == 3 ? (void *)slot3_##name : NULL)

static void *wrapper_for(int id, unsigned slot)
{
    switch (id) {
    case SYM_CONTEXT_NEW: return PICK_SLOT(context_new);
    case SYM_CONTEXT_CONNECT: return PICK_SLOT(context_connect);
    case SYM_CONTEXT_GET_STATE: return PICK_SLOT(context_get_state);
    case SYM_CONTEXT_ERRNO: return PICK_SLOT(context_errno);
    case SYM_CONTEXT_SET_STATE_CALLBACK: return PICK_SLOT(context_set_state_callback);
    case SYM_CONTEXT_DISCONNECT: return PICK_SLOT(context_disconnect);
    case SYM_CONTEXT_UNREF: return PICK_SLOT(context_unref);
    case SYM_STREAM_NEW: return PICK_SLOT(stream_new);
    case SYM_STREAM_NEW_WITH_PROPLIST: return PICK_SLOT(stream_new_with_proplist);
    case SYM_STREAM_CONNECT_PLAYBACK: return PICK_SLOT(stream_connect_playback);
    case SYM_STREAM_GET_STATE: return PICK_SLOT(stream_get_state);
    case SYM_STREAM_BEGIN_WRITE: return PICK_SLOT(stream_begin_write);
    case SYM_STREAM_WRITE: return PICK_SLOT(stream_write);
    case SYM_STREAM_SET_STATE_CALLBACK: return PICK_SLOT(stream_set_state_callback);
    case SYM_STREAM_SET_WRITE_CALLBACK: return PICK_SLOT(stream_set_write_callback);
    case SYM_STREAM_CORK: return PICK_SLOT(stream_cork);
    case SYM_STREAM_FLUSH: return PICK_SLOT(stream_flush);
    case SYM_STREAM_DISCONNECT: return PICK_SLOT(stream_disconnect);
    case SYM_STREAM_UNREF: return PICK_SLOT(stream_unref);
    case SYM_OPERATION_GET_STATE: return PICK_SLOT(operation_get_state);
    case SYM_OPERATION_UNREF: return PICK_SLOT(operation_unref);
    case SYM_STREAM_GET_BUFFER_ATTR: return PICK_SLOT(stream_get_buffer_attr);
    default: return NULL;
    }
}

void *dlopen(const char *filename, int flags)
{
    void *result;
    int saved_errno;
    int targeted = filename &&
        strcmp(path_basename(filename), "libpulse.so.0") == 0;

    pthread_once(&loader_once, initialize_loader);
    pthread_once(&trace_once, initialize_trace);
    if (!real_dlopen_fn) {
        errno = ENOSYS;
        return NULL;
    }
    result = real_dlopen_fn(filename, flags);
    saved_errno = errno;
    if (trace_enabled && targeted && result)
        track_handle(result, filename);
    errno = saved_errno;
    return result;
}

void *dlsym(void *handle, const char *name)
{
    struct tracked_handle *tracked;
    void *provider;
    void *wrapper;
    int id;
    unsigned slot = MAX_HANDLES;

    pthread_once(&loader_once, initialize_loader);
    pthread_once(&trace_once, initialize_trace);
    if (!real_dlsym_fn)
        return NULL;
    tracked = trace_enabled ? tracked_for(handle) : NULL;
    id = tracked ? symbol_index(name) : -1;
    if (tracked)
        slot = (unsigned)(tracked - handles);
    wrapper = id >= 0 ? wrapper_for(id, slot) : NULL;
    provider = real_dlsym_fn(handle, name);
    if (id >= 0 && provider && wrapper) {
        atomic_store_explicit(&bindings[id].provider[slot], (uintptr_t)provider,
                              memory_order_release);
        atomic_store_explicit(&bindings[id].requested[slot], 1,
                              memory_order_release);
        return wrapper;
    }
    return provider;
}

#define LOAD_PROVIDER(id, type) ((type)(uintptr_t)atomic_load_explicit(\
    &bindings[(id)].provider[dispatch_slot], memory_order_acquire))

static unsigned context_id_locked(const pa_context *pointer)
{
    size_t index;
    for (index = 0; index < MAX_CONTEXTS; index++)
        if (contexts[index].active && contexts[index].pointer == pointer)
            return contexts[index].id;
    return 0;
}

static struct context_entry *context_entry_locked(const pa_context *pointer)
{
    size_t index;
    for (index = 0; index < MAX_CONTEXTS; index++)
        if (contexts[index].active && contexts[index].pointer == pointer)
            return &contexts[index];
    return NULL;
}

static unsigned assign_context(pa_context *pointer)
{
    size_t index;
    struct context_entry *vacant = NULL;
    unsigned id = 0;

    if (!pointer)
        return 0;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_CONTEXTS; index++) {
        if (contexts[index].active && contexts[index].pointer == pointer) {
            id = contexts[index].id;
            goto done;
        }
        if (!contexts[index].active && contexts[index].pointer == pointer) {
            vacant = &contexts[index];
            pointer_reuse_count++;
            break;
        }
        if (!contexts[index].active && !vacant)
            vacant = &contexts[index];
    }
    if (!vacant) {
        overflow_count++;
        goto done;
    }
    memset(vacant, 0, sizeof(*vacant));
    vacant->pointer = pointer;
    vacant->active = 1;
    vacant->id = next_context_id++;
    id = vacant->id;
done:
    pthread_mutex_unlock(&trace_lock);
    return id;
}

static unsigned context_id(const pa_context *pointer)
{
    unsigned id;
    pthread_mutex_lock(&trace_lock);
    id = context_id_locked(pointer);
    pthread_mutex_unlock(&trace_lock);
    return id;
}

static unsigned stream_id_locked(const pa_stream *pointer)
{
    size_t index;
    for (index = 0; index < MAX_STREAMS; index++)
        if (streams[index].active && streams[index].pointer == pointer)
            return streams[index].id;
    return 0;
}

static struct stream_entry *stream_entry_locked(const pa_stream *pointer)
{
    size_t index;
    for (index = 0; index < MAX_STREAMS; index++)
        if (streams[index].active && streams[index].pointer == pointer)
            return &streams[index];
    return NULL;
}

static unsigned assign_stream(pa_stream *pointer, unsigned context)
{
    size_t index;
    struct stream_entry *vacant = NULL;
    unsigned id = 0;

    if (!pointer)
        return 0;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_STREAMS; index++) {
        if (streams[index].active && streams[index].pointer == pointer) {
            id = streams[index].id;
            goto done;
        }
        if (!streams[index].active && streams[index].pointer == pointer) {
            vacant = &streams[index];
            pointer_reuse_count++;
            break;
        }
        if (!streams[index].active && !vacant)
            vacant = &streams[index];
    }
    if (!vacant) {
        overflow_count++;
        goto done;
    }
    memset(vacant, 0, sizeof(*vacant));
    vacant->pointer = pointer;
    vacant->active = 1;
    vacant->context_id = context;
    vacant->id = next_stream_id++;
    id = vacant->id;
done:
    pthread_mutex_unlock(&trace_lock);
    return id;
}

static unsigned stream_id(const pa_stream *pointer)
{
    unsigned id;
    pthread_mutex_lock(&trace_lock);
    id = stream_id_locked(pointer);
    pthread_mutex_unlock(&trace_lock);
    return id;
}

static unsigned stream_context_id(const pa_stream *pointer)
{
    struct stream_entry *entry;
    unsigned id = 0;
    pthread_mutex_lock(&trace_lock);
    entry = stream_entry_locked(pointer);
    if (entry)
        id = entry->context_id;
    pthread_mutex_unlock(&trace_lock);
    return id;
}

static unsigned assign_operation(pa_operation *pointer, unsigned stream,
                                 const char *action)
{
    size_t index;
    struct operation_entry *vacant = NULL;
    unsigned id = 0;

    if (!pointer)
        return 0;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_OPERATIONS; index++) {
        if (operations[index].active && operations[index].pointer == pointer) {
            id = operations[index].id;
            goto done;
        }
        if (!operations[index].active && operations[index].pointer == pointer) {
            vacant = &operations[index];
            pointer_reuse_count++;
            break;
        }
        if (!operations[index].active && !vacant)
            vacant = &operations[index];
    }
    if (!vacant) {
        overflow_count++;
        goto done;
    }
    memset(vacant, 0, sizeof(*vacant));
    vacant->pointer = pointer;
    vacant->stream_id = stream;
    vacant->active = 1;
    vacant->id = next_operation_id++;
    sanitize_token(vacant->action, sizeof(vacant->action), action);
    id = vacant->id;
done:
    pthread_mutex_unlock(&trace_lock);
    return id;
}

static unsigned operation_values(const pa_operation *pointer,
                                 unsigned *stream, char *action,
                                 size_t action_size)
{
    size_t index;
    unsigned id = 0;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_OPERATIONS; index++) {
        if (operations[index].active && operations[index].pointer == pointer) {
            id = operations[index].id;
            *stream = operations[index].stream_id;
            memcpy(action, operations[index].action,
                   strlen(operations[index].action) + 1);
            break;
        }
    }
    pthread_mutex_unlock(&trace_lock);
    if (id == 0) {
        *stream = 0;
        if (action_size > 0)
            memcpy(action, "unknown", sizeof("unknown"));
    }
    return id;
}

static const char *stream_cork_action(pa_stream *stream, int cork)
{
    struct stream_entry *entry;
    const char *action;
    pthread_mutex_lock(&trace_lock);
    entry = stream_entry_locked(stream);
    if (!entry) {
        action = cork ? "StopCork" : "Start";
    } else {
        entry->cork_calls++;
        action = cork ? "StopCork" : "Start";
    }
    pthread_mutex_unlock(&trace_lock);
    return action;
}

static const char *stream_flush_action(pa_stream *stream)
{
    struct stream_entry *entry;
    const char *action = "StopFlush";
    pthread_mutex_lock(&trace_lock);
    entry = stream_entry_locked(stream);
    if (entry) {
        entry->flush_calls++;
        if (entry->flush_calls > 1)
            action = "ResetFlush";
    }
    pthread_mutex_unlock(&trace_lock);
    return action;
}

static struct success_slot *allocate_success_slot(unsigned stream,
        const char *action, pa_stream_success_cb_t callback, void *userdata)
{
    size_t index;
    struct success_slot *slot = NULL;

    if (!callback)
        return NULL;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_SUCCESS_SLOTS; index++) {
        if (!success_slots[index].used) {
            slot = &success_slots[index];
            slot->used = 1;
            slot->stream_id = stream;
            sanitize_token(slot->action, sizeof(slot->action), action);
            slot->callback = callback;
            slot->userdata = userdata;
            break;
        }
    }
    if (!slot)
        overflow_count++;
    pthread_mutex_unlock(&trace_lock);
    return slot;
}

static void success_callback_thunk(pa_stream *stream, int success, void *userdata)
{
    struct success_slot *slot = userdata;
    pa_stream_success_cb_t callback = slot ? slot->callback : NULL;
    void *original = slot ? slot->userdata : NULL;
    unsigned id = slot ? slot->stream_id : stream_id(stream);
    const char *action = slot ? slot->action : "unknown";
    int callback_errno = errno;

    trace_event(success ? TRACE_ESSENTIAL : TRACE_FAILURE,
        "event=success_callback stream_id=%u action=%s success=%d errno_before=%d errno_after=%d",
        id, action, success, callback_errno, callback_errno);
    errno = callback_errno;
    if (callback)
        callback(stream, success, original);
    if (slot) {
        pthread_mutex_lock(&trace_lock);
        memset(slot, 0, sizeof(*slot));
        pthread_mutex_unlock(&trace_lock);
    }
}

static struct free_slot *allocate_free_slot(void *data, unsigned stream,
                                            pa_free_cb_t callback)
{
    size_t index;
    struct free_slot *slot = NULL;
    if (!callback)
        return NULL;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_FREE_SLOTS; index++) {
        if (!free_slots[index].used) {
            slot = &free_slots[index];
            slot->used = 1;
            slot->data = data;
            slot->stream_id = stream;
            slot->callback = callback;
            break;
        }
    }
    if (!slot)
        overflow_count++;
    pthread_mutex_unlock(&trace_lock);
    return slot;
}

static void free_callback_thunk(void *data)
{
    size_t index;
    pa_free_cb_t callback = NULL;
    unsigned stream = 0;
    int callback_errno = errno;

    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_FREE_SLOTS; index++) {
        if (free_slots[index].used && free_slots[index].data == data) {
            callback = free_slots[index].callback;
            stream = free_slots[index].stream_id;
            memset(&free_slots[index], 0, sizeof(free_slots[index]));
            break;
        }
    }
    pthread_mutex_unlock(&trace_lock);
    trace_event(TRACE_ESSENTIAL,
        "event=free_callback stream_id=%u matched=%d errno_before=%d errno_after=%d",
        stream, callback != NULL, callback_errno, callback_errno);
    errno = callback_errno;
    if (callback)
        callback(data);
}

static void context_notify_thunk(pa_context *context, void *userdata)
{
    struct context_entry *entry;
    pa_context_notify_cb_t callback = NULL;
    void *original = NULL;
    unsigned id = 0;
    int callback_errno = errno;
    (void)userdata;

    pthread_mutex_lock(&trace_lock);
    entry = context_entry_locked(context);
    if (entry) {
        id = entry->id;
        callback = entry->notify;
        original = entry->notify_userdata;
    }
    pthread_mutex_unlock(&trace_lock);
    trace_event(TRACE_ESSENTIAL,
        "event=context_notify context_id=%u errno_before=%d errno_after=%d",
        id, callback_errno, callback_errno);
    errno = callback_errno;
    if (callback)
        callback(context, original);
}

static void stream_state_thunk(pa_stream *stream, void *userdata)
{
    struct stream_entry *entry;
    pa_stream_notify_cb_t callback = NULL;
    void *original = NULL;
    unsigned id = 0;
    int callback_errno = errno;
    (void)userdata;

    pthread_mutex_lock(&trace_lock);
    entry = stream_entry_locked(stream);
    if (entry) {
        id = entry->id;
        callback = entry->state_cb;
        original = entry->state_userdata;
    }
    pthread_mutex_unlock(&trace_lock);
    trace_event(TRACE_ESSENTIAL,
        "event=stream_notify stream_id=%u errno_before=%d errno_after=%d",
        id, callback_errno, callback_errno);
    errno = callback_errno;
    if (callback)
        callback(stream, original);
}

static void stream_request_thunk(pa_stream *stream, size_t bytes, void *userdata)
{
    struct stream_entry *entry;
    pa_stream_request_cb_t callback = NULL;
    void *original = NULL;
    unsigned id = 0;
    int callback_errno = errno;
    (void)userdata;

    pthread_mutex_lock(&trace_lock);
    entry = stream_entry_locked(stream);
    if (entry) {
        id = entry->id;
        callback = entry->write_cb;
        original = entry->write_userdata;
    }
    pthread_mutex_unlock(&trace_lock);
    trace_event(TRACE_HOT,
        "event=request_callback stream_id=%u bytes=%zu errno_before=%d errno_after=%d",
        id, bytes, callback_errno, callback_errno);
    errno = callback_errno;
    if (callback)
        callback(stream, bytes, original);
}

static void emit_binding_identity(int symbol)
{
    (void)symbol;
    emit_provider_identity(atomic_load_explicit(&handles[dispatch_slot].handle,
                                                memory_order_acquire));
}

static pa_context *wrap_pa_context_new(void *api, const char *name)
{
    pa_context *(*real)(void *, const char *) = LOAD_PROVIDER(SYM_CONTEXT_NEW, pa_context *(*)(void *, const char *));
    pa_context *result;
    char token[128];
    int before = errno;
    int after;
    result = real(api, name);
    after = errno;
    emit_binding_identity(SYM_CONTEXT_NEW);
    sanitize_token(token, sizeof(token), name);
    trace_event(result ? TRACE_ESSENTIAL : TRACE_FAILURE,
        "event=context_new context_id=%u name=%s ret=%s errno_before=%d errno_after=%d",
        assign_context(result), token, result ? "nonnull" : "null", before, after);
    errno = after;
    return result;
}

static int wrap_pa_context_connect(pa_context *context, const char *server,
                                   int flags, const void *api)
{
    int (*real)(pa_context *, const char *, int, const void *) = LOAD_PROVIDER(SYM_CONTEXT_CONNECT, int (*)(pa_context *, const char *, int, const void *));
    int before = errno;
    int result = real(context, server, flags, api);
    int after = errno;
    trace_event(result == 0 ? TRACE_ESSENTIAL : TRACE_FAILURE,
        "event=context_connect context_id=%u server=%s flags=0x%x ret=%d errno_before=%d errno_after=%d",
        context_id(context), server ? "explicit" : "default-null", flags,
        result, before, after);
    errno = after;
    return result;
}

static int wrap_pa_context_get_state(const pa_context *context)
{
    int (*real)(const pa_context *) = LOAD_PROVIDER(SYM_CONTEXT_GET_STATE, int (*)(const pa_context *));
    int before = errno;
    int result = real(context);
    int after = errno;
    trace_event(result == 5 || result == 6 ? TRACE_FAILURE : TRACE_HOT,
        "event=context_state context_id=%u state=%d errno_before=%d errno_after=%d",
        context_id(context), result, before, after);
    errno = after;
    return result;
}

static int wrap_pa_context_errno(const pa_context *context)
{
    int (*real)(const pa_context *) = LOAD_PROVIDER(SYM_CONTEXT_ERRNO, int (*)(const pa_context *));
    int before = errno;
    int result = real(context);
    int after = errno;
    trace_event(result ? TRACE_FAILURE : TRACE_HOT,
        "event=context_errno context_id=%u value=%d errno_before=%d errno_after=%d",
        context_id(context), result, before, after);
    errno = after;
    return result;
}

static void wrap_pa_context_set_state_callback(pa_context *context,
        pa_context_notify_cb_t callback, void *userdata)
{
    void (*real)(pa_context *, pa_context_notify_cb_t, void *) =
        LOAD_PROVIDER(SYM_CONTEXT_SET_STATE_CALLBACK,
                      void (*)(pa_context *, pa_context_notify_cb_t, void *));
    struct context_entry *entry;
    int can_wrap = 0;
    int before = errno;
    int after;

    pthread_mutex_lock(&trace_lock);
    entry = context_entry_locked(context);
    if (entry) {
        entry->notify = callback;
        entry->notify_userdata = userdata;
        can_wrap = 1;
    }
    pthread_mutex_unlock(&trace_lock);
    real(context, callback && can_wrap ? context_notify_thunk : callback,
         userdata);
    after = errno;
    trace_event(TRACE_ESSENTIAL,
        "event=set_context_state_callback context_id=%u value=%s errno_before=%d errno_after=%d",
        context_id(context), callback ? "set" : "clear", before, after);
    errno = after;
}

static void wrap_pa_context_disconnect(pa_context *context)
{
    void (*real)(pa_context *) = LOAD_PROVIDER(SYM_CONTEXT_DISCONNECT, void (*)(pa_context *));
    int before = errno;
    int after;
    real(context);
    after = errno;
    trace_event(TRACE_ESSENTIAL,
        "event=context_disconnect context_id=%u ret=void errno_before=%d errno_after=%d",
        context_id(context), before, after);
    errno = after;
}

static void wrap_pa_context_unref(pa_context *context)
{
    void (*real)(pa_context *) = LOAD_PROVIDER(SYM_CONTEXT_UNREF, void (*)(pa_context *));
    unsigned id = context_id(context);
    size_t index;
    int before = errno;
    int after;
    real(context);
    after = errno;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_CONTEXTS; index++)
        if (contexts[index].active && contexts[index].pointer == context)
            contexts[index].active = 0;
    pthread_mutex_unlock(&trace_lock);
    trace_event(TRACE_ESSENTIAL,
        "event=context_unref context_id=%u ret=void errno_before=%d errno_after=%d",
        id, before, after);
    errno = after;
}

static pa_stream *stream_new_common(int symbol, pa_context *context,
        const char *name, const pa_sample_spec *spec, const pa_channel_map *map,
        pa_proplist *properties)
{
    pa_stream *result;
    unsigned cid = context_id(context);
    char token[128];
    int before = errno;
    int after;
    if (symbol == SYM_STREAM_NEW) {
        pa_stream *(*real)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *) = LOAD_PROVIDER(SYM_STREAM_NEW, pa_stream *(*)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *));
        result = real(context, name, spec, map);
    } else {
        pa_stream *(*real)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *, pa_proplist *) = LOAD_PROVIDER(SYM_STREAM_NEW_WITH_PROPLIST, pa_stream *(*)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *, pa_proplist *));
        result = real(context, name, spec, map, properties);
    }
    after = errno;
    sanitize_token(token, sizeof(token), name);
    trace_event(result ? TRACE_ESSENTIAL : TRACE_FAILURE,
        "event=stream_new context_id=%u stream_id=%u variant=%s name=%s proplist=%s format=%d rate=%u channels=%u map_channels=%u map0=%d map1=%d ret=%s errno_before=%d errno_after=%d",
        cid, assign_stream(result, cid),
        symbol == SYM_STREAM_NEW ? "plain" : "with_proplist", token,
        symbol == SYM_STREAM_NEW ? "absent" : (properties ? "opaque" : "null"),
        spec ? spec->format : -1, spec ? spec->rate : 0,
        spec ? spec->channels : 0, map ? map->channels : 0,
        map && map->channels > 0 ? map->map[0] : -1,
        map && map->channels > 1 ? map->map[1] : -1,
        result ? "nonnull" : "null", before, after);
    errno = after;
    return result;
}

static pa_stream *wrap_pa_stream_new(pa_context *context, const char *name,
        const pa_sample_spec *spec, const pa_channel_map *map)
{
    return stream_new_common(SYM_STREAM_NEW, context, name, spec, map, NULL);
}

static pa_stream *wrap_pa_stream_new_with_proplist(pa_context *context,
        const char *name, const pa_sample_spec *spec, const pa_channel_map *map,
        pa_proplist *properties)
{
    return stream_new_common(SYM_STREAM_NEW_WITH_PROPLIST, context, name, spec,
                             map, properties);
}

static int wrap_pa_stream_connect_playback(pa_stream *stream,
        const char *device, const pa_buffer_attr *attr, int flags,
        const pa_cvolume *volume, pa_stream *sync_stream)
{
    int (*real)(pa_stream *, const char *, const pa_buffer_attr *, int, const pa_cvolume *, pa_stream *) = LOAD_PROVIDER(SYM_STREAM_CONNECT_PLAYBACK, int (*)(pa_stream *, const char *, const pa_buffer_attr *, int, const pa_cvolume *, pa_stream *));
    int before = errno;
    int result = real(stream, device, attr, flags, volume, sync_stream);
    int after = errno;
    trace_event(result == 0 ? TRACE_ESSENTIAL : TRACE_FAILURE,
        "event=stream_connect context_id=%u stream_id=%u device=%s flags=0x%x maxlength=%u tlength=%u prebuf=%u minreq=%u fragsize=%u ret=%d errno_before=%d errno_after=%d",
        stream_context_id(stream), stream_id(stream),
        device ? "explicit" : "default-null", flags,
        attr ? attr->maxlength : 0, attr ? attr->tlength : 0,
        attr ? attr->prebuf : 0, attr ? attr->minreq : 0,
        attr ? attr->fragsize : 0, result, before, after);
    errno = after;
    return result;
}

static int wrap_pa_stream_get_state(const pa_stream *stream)
{
    int (*real)(const pa_stream *) = LOAD_PROVIDER(SYM_STREAM_GET_STATE, int (*)(const pa_stream *));
    int before = errno;
    int result = real(stream);
    int after = errno;
    trace_event(result == 3 || result == 4 ? TRACE_FAILURE : TRACE_HOT,
        "event=stream_state context_id=%u stream_id=%u state=%d errno_before=%d errno_after=%d",
        stream_context_id(stream), stream_id(stream), result, before, after);
    errno = after;
    return result;
}

static int wrap_pa_stream_begin_write(pa_stream *stream, void **data,
                                      size_t *bytes)
{
    int (*real)(pa_stream *, void **, size_t *) = LOAD_PROVIDER(SYM_STREAM_BEGIN_WRITE, int (*)(pa_stream *, void **, size_t *));
    int before = errno;
    int result = real(stream, data, bytes);
    int after = errno;
    if (result == 0) {
        trace_event(TRACE_HOT,
            "event=begin_write context_id=%u stream_id=%u ret=0 buffer=%s bytes=%zu errno_before=%d errno_after=%d",
            stream_context_id(stream), stream_id(stream),
            data && *data ? "nonnull" : "null", bytes ? *bytes : 0,
            before, after);
    } else {
        trace_event(TRACE_FAILURE,
            "event=begin_write context_id=%u stream_id=%u ret=%d buffer=unavailable bytes=unavailable errno_before=%d errno_after=%d",
            stream_context_id(stream), stream_id(stream), result, before, after);
    }
    errno = after;
    return result;
}

static int wrap_pa_stream_write(pa_stream *stream, const void *data,
        size_t bytes, pa_free_cb_t free_cb, int64_t offset, int seek)
{
    int (*real)(pa_stream *, const void *, size_t, pa_free_cb_t, int64_t, int) = LOAD_PROVIDER(SYM_STREAM_WRITE, int (*)(pa_stream *, const void *, size_t, pa_free_cb_t, int64_t, int));
    struct free_slot *slot = allocate_free_slot((void *)data, stream_id(stream),
                                                free_cb);
    int before = errno;
    int result = real(stream, data, bytes, slot ? free_callback_thunk : free_cb,
                      offset, seek);
    int after = errno;
    trace_event(result == 0 ? TRACE_HOT : TRACE_FAILURE,
        "event=stream_write context_id=%u stream_id=%u bytes=%zu frames_f32le_stereo=%zu free_callback=%d offset=%lld seek=%d ret=%d errno_before=%d errno_after=%d",
        stream_context_id(stream), stream_id(stream), bytes, bytes / 8,
        free_cb != NULL, (long long)offset, seek, result, before, after);
    errno = after;
    return result;
}

static void wrap_pa_stream_set_state_callback(pa_stream *stream,
        pa_stream_notify_cb_t callback, void *userdata)
{
    void (*real)(pa_stream *, pa_stream_notify_cb_t, void *) = LOAD_PROVIDER(SYM_STREAM_SET_STATE_CALLBACK, void (*)(pa_stream *, pa_stream_notify_cb_t, void *));
    struct stream_entry *entry;
    int can_wrap = 0;
    int before = errno;
    int after;
    pthread_mutex_lock(&trace_lock);
    entry = stream_entry_locked(stream);
    if (entry) {
        entry->state_cb = callback;
        entry->state_userdata = userdata;
        can_wrap = 1;
    }
    pthread_mutex_unlock(&trace_lock);
    real(stream, callback && can_wrap ? stream_state_thunk : callback, userdata);
    after = errno;
    trace_event(TRACE_ESSENTIAL,
        "event=set_state_callback stream_id=%u value=%s errno_before=%d errno_after=%d",
        stream_id(stream), callback ? "set" : "clear", before, after);
    errno = after;
}

static void wrap_pa_stream_set_write_callback(pa_stream *stream,
        pa_stream_request_cb_t callback, void *userdata)
{
    void (*real)(pa_stream *, pa_stream_request_cb_t, void *) = LOAD_PROVIDER(SYM_STREAM_SET_WRITE_CALLBACK, void (*)(pa_stream *, pa_stream_request_cb_t, void *));
    struct stream_entry *entry;
    int can_wrap = 0;
    int before = errno;
    int after;
    pthread_mutex_lock(&trace_lock);
    entry = stream_entry_locked(stream);
    if (entry) {
        entry->write_cb = callback;
        entry->write_userdata = userdata;
        can_wrap = 1;
    }
    pthread_mutex_unlock(&trace_lock);
    real(stream, callback && can_wrap ? stream_request_thunk : callback, userdata);
    after = errno;
    trace_event(TRACE_ESSENTIAL,
        "event=set_write_callback stream_id=%u value=%s errno_before=%d errno_after=%d",
        stream_id(stream), callback ? "set" : "clear", before, after);
    errno = after;
}

static pa_operation *stream_operation_common(int symbol, pa_stream *stream,
        int cork, pa_stream_success_cb_t callback, void *userdata)
{
    const char *action = symbol == SYM_STREAM_CORK ?
        stream_cork_action(stream, cork) : stream_flush_action(stream);
    unsigned sid = stream_id(stream);
    struct success_slot *slot = allocate_success_slot(sid, action, callback,
                                                      userdata);
    pa_operation *result;
    unsigned operation;
    int before = errno;
    int after;
    if (symbol == SYM_STREAM_CORK) {
        pa_operation *(*real)(pa_stream *, int, pa_stream_success_cb_t, void *) = LOAD_PROVIDER(SYM_STREAM_CORK, pa_operation *(*)(pa_stream *, int, pa_stream_success_cb_t, void *));
        result = real(stream, cork, slot ? success_callback_thunk : callback,
                      slot ? slot : userdata);
    } else {
        pa_operation *(*real)(pa_stream *, pa_stream_success_cb_t, void *) = LOAD_PROVIDER(SYM_STREAM_FLUSH, pa_operation *(*)(pa_stream *, pa_stream_success_cb_t, void *));
        result = real(stream, slot ? success_callback_thunk : callback,
                      slot ? slot : userdata);
    }
    after = errno;
    operation = assign_operation(result, sid, action);
    trace_event(result ? TRACE_ESSENTIAL : TRACE_FAILURE,
        "event=operation_return context_id=%u stream_id=%u operation_id=%u action=%s cork=%d ret=%s errno_before=%d errno_after=%d failure_class=%s",
        stream_context_id(stream), sid, operation, action,
        symbol == SYM_STREAM_CORK ? cork : -1,
        result ? "nonnull" : "null", before, after,
        result ? "none" : (strcmp(action, "Start") == 0 ? "primary_playback" : "teardown"));
    errno = after;
    return result;
}

static pa_operation *wrap_pa_stream_cork(pa_stream *stream, int cork,
        pa_stream_success_cb_t callback, void *userdata)
{
    return stream_operation_common(SYM_STREAM_CORK, stream, cork, callback,
                                   userdata);
}

static pa_operation *wrap_pa_stream_flush(pa_stream *stream,
        pa_stream_success_cb_t callback, void *userdata)
{
    return stream_operation_common(SYM_STREAM_FLUSH, stream, 0, callback,
                                   userdata);
}

static int wrap_pa_stream_disconnect(pa_stream *stream)
{
    int (*real)(pa_stream *) = LOAD_PROVIDER(SYM_STREAM_DISCONNECT, int (*)(pa_stream *));
    int before = errno;
    int result = real(stream);
    int after = errno;
    trace_event(result == 0 ? TRACE_ESSENTIAL : TRACE_FAILURE,
        "event=stream_disconnect context_id=%u stream_id=%u ret=%d errno_before=%d errno_after=%d",
        stream_context_id(stream), stream_id(stream), result, before, after);
    errno = after;
    return result;
}

static void wrap_pa_stream_unref(pa_stream *stream)
{
    void (*real)(pa_stream *) = LOAD_PROVIDER(SYM_STREAM_UNREF, void (*)(pa_stream *));
    unsigned sid = stream_id(stream);
    unsigned cid = stream_context_id(stream);
    size_t index;
    int before = errno;
    int after;
    real(stream);
    after = errno;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_STREAMS; index++)
        if (streams[index].active && streams[index].pointer == stream)
            streams[index].active = 0;
    pthread_mutex_unlock(&trace_lock);
    trace_event(TRACE_ESSENTIAL,
        "event=stream_unref context_id=%u stream_id=%u ret=void errno_before=%d errno_after=%d",
        cid, sid, before, after);
    errno = after;
}

static int wrap_pa_operation_get_state(const pa_operation *operation)
{
    int (*real)(const pa_operation *) = LOAD_PROVIDER(SYM_OPERATION_GET_STATE, int (*)(const pa_operation *));
    unsigned sid;
    char action[24];
    unsigned oid = operation_values(operation, &sid, action, sizeof(action));
    int before = errno;
    int result = real(operation);
    int after = errno;
    trace_event(TRACE_HOT,
        "event=operation_state stream_id=%u operation_id=%u action=%s state=%d errno_before=%d errno_after=%d",
        sid, oid, action, result, before, after);
    errno = after;
    return result;
}

static void wrap_pa_operation_unref(pa_operation *operation)
{
    void (*real)(pa_operation *) = LOAD_PROVIDER(SYM_OPERATION_UNREF, void (*)(pa_operation *));
    unsigned sid;
    char action[24];
    unsigned oid = operation_values(operation, &sid, action, sizeof(action));
    size_t index;
    int before = errno;
    int after;
    real(operation);
    after = errno;
    pthread_mutex_lock(&trace_lock);
    for (index = 0; index < MAX_OPERATIONS; index++)
        if (operations[index].active && operations[index].pointer == operation)
            operations[index].active = 0;
    pthread_mutex_unlock(&trace_lock);
    trace_event(TRACE_ESSENTIAL,
        "event=operation_unref stream_id=%u operation_id=%u action=%s ret=void errno_before=%d errno_after=%d",
        sid, oid, action, before, after);
    errno = after;
}

static const pa_buffer_attr *wrap_pa_stream_get_buffer_attr(const pa_stream *stream)
{
    const pa_buffer_attr *(*real)(const pa_stream *) = LOAD_PROVIDER(SYM_STREAM_GET_BUFFER_ATTR, const pa_buffer_attr *(*)(const pa_stream *));
    int before = errno;
    const pa_buffer_attr *result = real(stream);
    int after = errno;
    if (result) {
        trace_event(TRACE_ESSENTIAL,
            "event=effective_attr context_id=%u stream_id=%u observed=1 ret=nonnull maxlength=%u tlength=%u prebuf=%u minreq=%u fragsize=%u errno_before=%d errno_after=%d",
            stream_context_id(stream), stream_id(stream), result->maxlength,
            result->tlength, result->prebuf, result->minreq,
            result->fragsize, before, after);
    } else {
        trace_event(TRACE_FAILURE,
            "event=effective_attr context_id=%u stream_id=%u observed=1 ret=null values=unavailable errno_before=%d errno_after=%d",
            stream_context_id(stream), stream_id(stream), before, after);
    }
    errno = after;
    return result;
}

static void emit_summary(void)
{
    unsigned index;
    unsigned binding_count = 0;
    unsigned long long binding_mask = 0;
    unsigned effective_observed = 0;
    unsigned slot;
    int saved_errno = errno;

    if (!trace_enabled)
        return;
    for (index = 0; index < SYM_COUNT; index++) {
        for (slot = 0; slot < MAX_HANDLES; slot++) {
            if (atomic_load_explicit(&bindings[index].requested[slot],
                                     memory_order_acquire)) {
                binding_count++;
                binding_mask |= 1ULL << index;
                break;
            }
        }
    }
    for (slot = 0; slot < MAX_HANDLES; slot++)
        if (atomic_load_explicit(
                &bindings[SYM_STREAM_GET_BUFFER_ATTR].requested[slot],
                memory_order_acquire))
            effective_observed = 1;
    if (binding_count == 0) {
        errno = saved_errno;
        return;
    }
    trace_event(TRACE_FORCE,
        "event=summary binding_count=%u binding_mask=0x%llx effective_attr_observed=%u effective_attr_reason=%s pointer_reuse=%u overflow=%u hot_seen=%llu hot_suppressed=%llu failure_rows=%u fork_resets=%u errno_before=0 errno_after=0",
        binding_count, binding_mask,
        effective_observed,
        effective_observed ? "symbol_requested" : "symbol_not_requested",
        pointer_reuse_count, overflow_count, hot_seen, hot_suppressed,
        failure_rows, fork_resets);
    errno = saved_errno;
}

__attribute__((destructor)) static void trace_destructor(void)
{
    emit_summary();
    if (trace_fd >= 0)
        close(trace_fd);
}
