#define _GNU_SOURCE

/* Low-overhead direct-link PulseAudio byte counter for parity measurements. */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct pa_stream pa_stream;
typedef void (*pa_free_cb_t)(void *);
typedef int (*stream_write_fn)(pa_stream *, const void *, size_t,
                               pa_free_cb_t, int64_t, int);

enum { MAX_PULSE_HANDLES = 4 };

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t loader_once = PTHREAD_ONCE_INIT;
static void *(*real_dlopen_fn)(const char *, int);
static void *(*real_dlsym_fn)(void *, const char *);
static _Atomic(uintptr_t) handle_slots[MAX_PULSE_HANDLES];
static _Atomic(uintptr_t) stream_write_slots[MAX_PULSE_HANDLES];
static _Atomic(uintptr_t) direct_stream_write;
static _Thread_local unsigned dispatch_slot = MAX_PULSE_HANDLES;
static pa_stream *streams[64];
static unsigned long long stream_pending_bytes[64];
static unsigned stream_pending_writes[64];
static unsigned long long stream_total_writes[64];
static unsigned stream_count;
static unsigned long long sequence;
static int trace_fd = -1;
static pid_t trace_pid;

static void initialize_loader(void)
{
    real_dlopen_fn = (void *(*)(const char *, int))
        dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.2.5");
    real_dlsym_fn = (void *(*)(void *, const char *))
        dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
}

__attribute__((constructor)) static void trace_constructor(void)
{
    pthread_once(&loader_once, initialize_loader);
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    if (!path)
        return "";
    for (const char *cursor = path; *cursor; cursor++)
        if (*cursor == '/')
            base = cursor + 1;
    return base;
}

static unsigned tracked_slot(void *handle)
{
    uintptr_t value = (uintptr_t)handle;
    for (unsigned slot = 0; slot < MAX_PULSE_HANDLES; slot++)
        if (atomic_load_explicit(&handle_slots[slot], memory_order_acquire) ==
            value)
            return slot;
    return MAX_PULSE_HANDLES;
}

static void track_handle(void *handle)
{
    if (!handle || tracked_slot(handle) < MAX_PULSE_HANDLES)
        return;
    for (unsigned slot = 0; slot < MAX_PULSE_HANDLES; slot++) {
        uintptr_t expected = 0;
        if (atomic_compare_exchange_strong_explicit(
                &handle_slots[slot], &expected, (uintptr_t)handle,
                memory_order_acq_rel, memory_order_relaxed))
            return;
    }
}

static int safe_path(const char *path)
{
    const unsigned char *cursor;
    if (!path || path[0] != '/' || strlen(path) >= 240 || strstr(path, "../"))
        return 0;
    for (cursor = (const unsigned char *)path; *cursor; cursor++) {
        if ((*cursor >= 'a' && *cursor <= 'z') ||
            (*cursor >= 'A' && *cursor <= 'Z') ||
            (*cursor >= '0' && *cursor <= '9') || *cursor == '/' ||
            *cursor == '.' || *cursor == '_' || *cursor == '-')
            continue;
        return 0;
    }
    return 1;
}

static void initialize_locked(void)
{
    const char *enabled = getenv("XV6_PULSE_TRACE");
    const char *path = getenv("XV6_PULSE_TRACE_LOG");
    trace_pid = getpid();
    if (!enabled || strcmp(enabled, "1") || !safe_path(path))
        return;
    trace_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
}

static unsigned identify_stream(pa_stream *stream)
{
    unsigned index;
    for (index = 0; index < stream_count; index++)
        if (streams[index] == stream)
            return index + 1;
    if (stream_count < sizeof(streams) / sizeof(streams[0])) {
        streams[stream_count++] = stream;
        return stream_count;
    }
    return 0;
}

static int trace_stream_write(pa_stream *stream, const void *data, size_t bytes,
                              pa_free_cb_t free_cb, int64_t offset, int seek)
{
    int rc;
    char row[192];
    int length;
    unsigned id;

    stream_write_fn real_stream_write;

    pthread_once(&loader_once, initialize_loader);
    if (dispatch_slot < MAX_PULSE_HANDLES) {
        real_stream_write = (stream_write_fn)(uintptr_t)
            atomic_load_explicit(&stream_write_slots[dispatch_slot],
                                 memory_order_acquire);
    } else {
        real_stream_write = (stream_write_fn)(uintptr_t)
            atomic_load_explicit(&direct_stream_write, memory_order_acquire);
        if (!real_stream_write && real_dlsym_fn) {
            void *symbol = real_dlsym_fn(RTLD_NEXT, "pa_stream_write");
            atomic_store_explicit(&direct_stream_write, (uintptr_t)symbol,
                                  memory_order_release);
            real_stream_write = (stream_write_fn)symbol;
        }
    }
    if (!real_stream_write) {
            errno = ENOSYS;
            return -1;
    }
    rc = real_stream_write(stream, data, bytes, free_cb, offset, seek);
    if (rc != 0)
        return rc;

    pthread_mutex_lock(&lock);
    if (trace_pid != getpid()) {
        if (trace_fd >= 0)
            close(trace_fd);
        trace_fd = -1;
        stream_count = 0;
        memset(stream_pending_bytes, 0, sizeof(stream_pending_bytes));
        memset(stream_pending_writes, 0, sizeof(stream_pending_writes));
        memset(stream_total_writes, 0, sizeof(stream_total_writes));
        sequence = 0;
        initialize_locked();
    } else if (trace_pid == 0) {
        initialize_locked();
    }
    id = identify_stream(stream);
    sequence++;
    if (id != 0) {
        unsigned index = id - 1;
        stream_pending_bytes[index] += bytes;
        stream_pending_writes[index]++;
        stream_total_writes[index]++;
    }
    /*
     * A write syscall for every 4-8 KiB Pulse chunk is not low overhead on
     * xv6's ext4 path and can perturb Chromium's renderer cadence.  Publish
     * the first write immediately for stream identity, then aggregate 64
     * successful writes per row.  Summing row bytes still gives a genuine
     * monotonic lower bound at both parity snapshots, with at most 63 writes
     * left unreported at the tail.
     */
    if (trace_fd >= 0 && id != 0 &&
        (stream_total_writes[id - 1] == 1 ||
         stream_pending_writes[id - 1] >= 64)) {
        ssize_t written;
        unsigned index = id - 1;
        unsigned batch_writes = stream_pending_writes[index];
        unsigned long long batch_bytes = stream_pending_bytes[index];
        length = snprintf(row, sizeof(row),
                          "PULSE_BYTE_TRACE_V1 event=stream_write seq=%llu stream_id=%u bytes=%llu writes=%u pid=%ld\n",
                          sequence, id, batch_bytes, batch_writes,
                          (long)getpid());
        if (length > 0 && (size_t)length < sizeof(row)) {
            written = write(trace_fd, row, (size_t)length);
            (void)written;
        }
        stream_pending_bytes[index] = 0;
        stream_pending_writes[index] = 0;
    }
    pthread_mutex_unlock(&lock);
    return rc;
}

#define DEFINE_STREAM_WRITE_SLOT(N)                                           \
static int stream_write_slot_##N(pa_stream *stream, const void *data,         \
        size_t bytes, pa_free_cb_t free_cb, int64_t offset, int seek)         \
{                                                                             \
    unsigned previous = dispatch_slot;                                         \
    dispatch_slot = (N);                                                       \
    int result = trace_stream_write(stream, data, bytes, free_cb, offset,      \
                                    seek);                                     \
    dispatch_slot = previous;                                                  \
    return result;                                                             \
}

DEFINE_STREAM_WRITE_SLOT(0)
DEFINE_STREAM_WRITE_SLOT(1)
DEFINE_STREAM_WRITE_SLOT(2)
DEFINE_STREAM_WRITE_SLOT(3)

static void *stream_write_wrapper(unsigned slot)
{
    switch (slot) {
    case 0: return (void *)stream_write_slot_0;
    case 1: return (void *)stream_write_slot_1;
    case 2: return (void *)stream_write_slot_2;
    case 3: return (void *)stream_write_slot_3;
    default: return NULL;
    }
}

void *dlopen(const char *filename, int flags)
{
    void *result;
    int saved_errno;

    pthread_once(&loader_once, initialize_loader);
    if (!real_dlopen_fn) {
        errno = ENOSYS;
        return NULL;
    }
    result = real_dlopen_fn(filename, flags);
    saved_errno = errno;
    if (result && strcmp(path_basename(filename), "libpulse.so.0") == 0)
        track_handle(result);
    errno = saved_errno;
    return result;
}

void *dlsym(void *handle, const char *name)
{
    void *provider;
    unsigned slot;

    pthread_once(&loader_once, initialize_loader);
    if (!real_dlsym_fn)
        return NULL;
    provider = real_dlsym_fn(handle, name);
    slot = tracked_slot(handle);
    if (provider && slot < MAX_PULSE_HANDLES && name &&
        strcmp(name, "pa_stream_write") == 0) {
        atomic_store_explicit(&stream_write_slots[slot], (uintptr_t)provider,
                              memory_order_release);
        return stream_write_wrapper(slot);
    }
    return provider;
}

int pa_stream_write(pa_stream *stream, const void *data, size_t bytes,
                    pa_free_cb_t free_cb, int64_t offset, int seek)
{
    return trace_stream_write(stream, data, bytes, free_cb, offset, seek);
}
