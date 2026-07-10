#define _GNU_SOURCE

/*
 * Default-off Chromium-shaped PulseAudio playback reducer and small owned-
 * process helpers used by chromium-audio-localizer.expect.
 *
 * The Pulse declarations below intentionally cover only the stable public ABI
 * used here.  The image carries libpulse.so.0 but the host build environment
 * does not carry PulseAudio development headers.  Resolving the ABI at runtime
 * also makes a missing/wrong guest library a deterministic substrate verdict.
 */

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct pa_threaded_mainloop pa_threaded_mainloop;
typedef struct pa_mainloop_api pa_mainloop_api;
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

enum {
    PA_SAMPLE_FLOAT32LE = 5,
    PA_CHANNEL_POSITION_FRONT_LEFT = 1,
    PA_CHANNEL_POSITION_FRONT_RIGHT = 2,
    PA_CONTEXT_NOAUTOSPAWN = 0x0001,
    PA_STREAM_START_CORKED = 0x0001,
    PA_STREAM_INTERPOLATE_TIMING = 0x0002,
    PA_STREAM_NOT_MONOTONIC = 0x0004,
    PA_STREAM_AUTO_TIMING_UPDATE = 0x0008,
    PA_STREAM_ADJUST_LATENCY = 0x2000,
    PA_CONTEXT_READY = 4,
    PA_CONTEXT_FAILED = 5,
    PA_CONTEXT_TERMINATED = 6,
    PA_STREAM_READY = 2,
    PA_STREAM_FAILED = 3,
    PA_STREAM_TERMINATED = 4,
    PA_OPERATION_RUNNING = 0,
    PA_SEEK_RELATIVE = 0
};

enum {
    CHROMIUM_FRAMES_PER_WRITE = 512,
    CHANNELS = 2,
    BYTES_PER_SAMPLE = 4,
    CHROMIUM_WRITE_BYTES = CHROMIUM_FRAMES_PER_WRITE * CHANNELS * BYTES_PER_SAMPLE,
    REQUESTED_MINREQ = 2048,
    REQUESTED_TLENGTH = 12288,
    DEFAULT_DURATION_SECONDS = 20,
    OPERATION_TIMEOUT_MS = 5000
};

typedef void (*pa_context_notify_cb_t)(pa_context *, void *);
typedef void (*pa_stream_notify_cb_t)(pa_stream *, void *);
typedef void (*pa_stream_request_cb_t)(pa_stream *, size_t, void *);
typedef void (*pa_stream_success_cb_t)(pa_stream *, int, void *);
typedef void (*pa_free_cb_t)(void *);

struct pulse_api {
    void *handle;
    pa_threaded_mainloop *(*threaded_mainloop_new)(void);
    void (*threaded_mainloop_free)(pa_threaded_mainloop *);
    pa_mainloop_api *(*threaded_mainloop_get_api)(pa_threaded_mainloop *);
    int (*threaded_mainloop_start)(pa_threaded_mainloop *);
    void (*threaded_mainloop_stop)(pa_threaded_mainloop *);
    void (*threaded_mainloop_lock)(pa_threaded_mainloop *);
    void (*threaded_mainloop_unlock)(pa_threaded_mainloop *);
    void (*threaded_mainloop_wait)(pa_threaded_mainloop *);
    void (*threaded_mainloop_signal)(pa_threaded_mainloop *, int);
    pa_context *(*context_new)(pa_mainloop_api *, const char *);
    void (*context_unref)(pa_context *);
    void (*context_set_state_callback)(pa_context *, pa_context_notify_cb_t, void *);
    int (*context_connect)(pa_context *, const char *, int, const void *);
    void (*context_disconnect)(pa_context *);
    int (*context_get_state)(const pa_context *);
    int (*context_errno)(const pa_context *);
    const char *(*strerror_fn)(int);
    pa_proplist *(*proplist_new)(void);
    void (*proplist_free)(pa_proplist *);
    pa_stream *(*stream_new_with_proplist)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *, pa_proplist *);
    void (*stream_unref)(pa_stream *);
    void (*stream_set_state_callback)(pa_stream *, pa_stream_notify_cb_t, void *);
    void (*stream_set_write_callback)(pa_stream *, pa_stream_request_cb_t, void *);
    int (*stream_connect_playback)(pa_stream *, const char *, const pa_buffer_attr *, int, const pa_cvolume *, pa_stream *);
    int (*stream_disconnect)(pa_stream *);
    int (*stream_get_state)(const pa_stream *);
    const pa_buffer_attr *(*stream_get_buffer_attr)(const pa_stream *);
    int (*stream_begin_write)(pa_stream *, void **, size_t *);
    int (*stream_cancel_write)(pa_stream *);
    int (*stream_write)(pa_stream *, const void *, size_t, pa_free_cb_t, int64_t, int);
    pa_operation *(*stream_cork)(pa_stream *, int, pa_stream_success_cb_t, void *);
    pa_operation *(*stream_flush)(pa_stream *, pa_stream_success_cb_t, void *);
    int (*operation_get_state)(const pa_operation *);
    void (*operation_unref)(pa_operation *);
};

struct reducer {
    struct pulse_api pa;
    pa_threaded_mainloop *mainloop;
    pa_context *context;
    pa_stream *stream;
    pa_proplist *proplist;
    int context_state;
    int stream_state;
    int failed;
    const char *failure;
    uint64_t callback_count;
    uint64_t write_count;
    uint64_t short_callback_count;
    uint64_t requested_total;
    size_t request_min;
    size_t request_max;
    uint64_t frames_written;
    uint64_t sample_cursor;
    unsigned callback_rows;
    int write_enabled;
    int mainloop_started;
    _Atomic int watchdog_running;
    _Atomic int watchdog_expired;
    _Atomic int64_t phase_deadline_ms;
    pthread_t watchdog_thread;
    int watchdog_started;
    unsigned teardown_failures;
    const char *teardown_first_failure;
    struct operation_wait *deferred_operations;
};

struct operation_wait {
    struct reducer *reducer;
    const char *name;
    int callback_seen;
    int success;
    int deferred;
    pa_operation *operation;
    struct operation_wait *next;
};

static int64_t monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(unsigned ms)
{
    struct timespec req;

    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) < 0 && errno == EINTR)
        ;
}

static const char *context_state_name(int state)
{
    static const char *const names[] = {
        "UNCONNECTED", "CONNECTING", "AUTHORIZING", "SETTING_NAME",
        "READY", "FAILED", "TERMINATED"
    };

    return state >= 0 && state < (int)(sizeof(names) / sizeof(names[0])) ?
        names[state] : "UNKNOWN";
}

static const char *stream_state_name(int state)
{
    static const char *const names[] = {
        "UNCONNECTED", "CREATING", "READY", "FAILED", "TERMINATED"
    };

    return state >= 0 && state < (int)(sizeof(names) / sizeof(names[0])) ?
        names[state] : "UNKNOWN";
}

static int context_error(struct reducer *r)
{
    return r->context ? r->pa.context_errno(r->context) : -1;
}

static const char *context_error_name(struct reducer *r, int error)
{
    const char *name = error >= 0 ? r->pa.strerror_fn(error) : NULL;

    return name && name[0] ? name : "unavailable";
}

static void context_state_callback(pa_context *context, void *userdata)
{
    struct reducer *r = userdata;
    int error;

    r->context_state = r->pa.context_get_state(context);
    error = context_error(r);
    printf("AUDIO_PULSE_REDUCER_V1 phase=context_state state=%s state_value=%d context_errno=%d context_error=\"%s\"\n",
           context_state_name(r->context_state), r->context_state, error,
           context_error_name(r, error));
    if (r->context_state == PA_CONTEXT_FAILED ||
        r->context_state == PA_CONTEXT_TERMINATED) {
        r->failed = 1;
        r->failure = "context-terminal";
    }
    r->pa.threaded_mainloop_signal(r->mainloop, 0);
}

static void stream_state_callback(pa_stream *stream, void *userdata)
{
    struct reducer *r = userdata;
    int error;

    r->stream_state = r->pa.stream_get_state(stream);
    error = context_error(r);
    printf("AUDIO_PULSE_REDUCER_V1 phase=stream_state state=%s state_value=%d context_errno=%d context_error=\"%s\"\n",
           stream_state_name(r->stream_state), r->stream_state, error,
           context_error_name(r, error));
    if (r->stream_state == PA_STREAM_FAILED ||
        r->stream_state == PA_STREAM_TERMINATED) {
        r->failed = 1;
        r->failure = "stream-terminal";
    }
    r->pa.threaded_mainloop_signal(r->mainloop, 0);
}

static void stream_write_callback(pa_stream *stream, size_t requested, void *userdata)
{
    struct reducer *r = userdata;
    size_t remaining = requested;
    unsigned writes_this_callback = 0;
    int error;

    r->callback_count++;
    r->requested_total += requested;
    if (requested < r->request_min)
        r->request_min = requested;
    if (requested > r->request_max)
        r->request_max = requested;
    if (requested < CHROMIUM_WRITE_BYTES)
        r->short_callback_count++;

    if (!r->write_enabled)
        return;
    while (remaining >= CHANNELS * BYTES_PER_SAMPLE && !r->failed) {
        void *write_buffer = (void *)(uintptr_t)0x1;
        size_t requested_chunk = remaining < CHROMIUM_WRITE_BYTES ?
            remaining : CHROMIUM_WRITE_BYTES;
        size_t begin_bytes;
        float *samples;
        size_t frame;
        size_t frames;
        int begin_ret;
        int begin_errno;
        int cancel_ret = 0;
        int ret;

        requested_chunk -= requested_chunk % (CHANNELS * BYTES_PER_SAMPLE);
        if (requested_chunk == 0)
            break;
        begin_bytes = requested_chunk;
        errno = 0;
        begin_ret = r->pa.stream_begin_write(stream, &write_buffer,
                                             &begin_bytes);
        begin_errno = errno;
        error = context_error(r);
        if (begin_ret < 0 || write_buffer == NULL ||
            write_buffer == (void *)(uintptr_t)0x1 ||
            begin_bytes == 0 || begin_bytes > requested_chunk ||
            begin_bytes % (CHANNELS * BYTES_PER_SAMPLE) != 0) {
            if (begin_ret == 0 && write_buffer != NULL &&
                write_buffer != (void *)(uintptr_t)0x1) {
                errno = 0;
                cancel_ret = r->pa.stream_cancel_write(stream);
            }
            printf("AUDIO_PULSE_REDUCER_V1 phase=begin_write callback=%llu requested_bytes=%zu requested_chunk=%zu ret=%d errno=%d returned_bytes=%zu alignment=%d pointer=%s cancel_ret=%d context_errno=%d verdict=FAIL\n",
                   (unsigned long long)r->callback_count, requested,
                   requested_chunk, begin_ret, begin_errno, begin_bytes,
                   CHANNELS * BYTES_PER_SAMPLE,
                   write_buffer != NULL &&
                   write_buffer != (void *)(uintptr_t)0x1 ? "nonnull" :
                   write_buffer == NULL ? "null" : "unchanged-poison",
                   cancel_ret, error);
            r->failed = 1;
            r->failure = "stream-begin-write";
            r->pa.threaded_mainloop_signal(r->mainloop, 0);
            break;
        }
        samples = write_buffer;
        frames = begin_bytes / (CHANNELS * BYTES_PER_SAMPLE);
        for (frame = 0; frame < frames; frame++) {
            /* Deterministic low-amplitude square wave; no libm dependency. */
            float value = ((r->sample_cursor / 24) & 1U) ? 0.08f : -0.08f;
            samples[frame * 2] = value;
            samples[frame * 2 + 1] = value;
            r->sample_cursor++;
        }
        errno = 0;
        ret = r->pa.stream_write(stream, samples, begin_bytes, NULL, 0,
                                 PA_SEEK_RELATIVE);
        error = context_error(r);
        if (r->callback_rows < 32) {
            printf("AUDIO_PULSE_REDUCER_V1 phase=write callback=%llu requested_bytes=%zu requested_chunk=%zu begin_ret=%d begin_errno=%d begin_bytes=%zu pointer=nonnull chunk_bytes=%zu chunk_frames=%zu write_index=%u ret=%d errno=%d context_errno=%d context_error=\"%s\"\n",
                   (unsigned long long)r->callback_count, requested,
                   requested_chunk, begin_ret, begin_errno, begin_bytes,
                   begin_bytes, frames,
                   writes_this_callback, ret, errno, error,
                   context_error_name(r, error));
            r->callback_rows++;
        }
        if (ret < 0) {
            r->failed = 1;
            r->failure = "stream-write";
            break;
        }
        r->write_count++;
        r->frames_written += frames;
        writes_this_callback++;
        remaining -= begin_bytes;
    }
    if (r->callback_rows < 32 && requested < CHROMIUM_WRITE_BYTES) {
        printf("AUDIO_PULSE_REDUCER_V1 phase=write_short callback=%llu requested_bytes=%zu preferred_bytes=%d action=%s context_errno=%d\n",
               (unsigned long long)r->callback_count, requested,
               CHROMIUM_WRITE_BYTES,
               requested >= CHANNELS * BYTES_PER_SAMPLE ? "write_returned" : "defer_unaligned_tail",
               context_error(r));
        r->callback_rows++;
    }
}

static void operation_callback(pa_stream *stream, int success, void *userdata)
{
    struct operation_wait *waiter = userdata;

    (void)stream;
    waiter->callback_seen = 1;
    waiter->success = success;
    waiter->reducer->pa.threaded_mainloop_signal(waiter->reducer->mainloop, 0);
}

static void *watchdog_main(void *userdata)
{
    struct reducer *r = userdata;

    while (atomic_load_explicit(&r->watchdog_running, memory_order_acquire)) {
        int64_t deadline = atomic_load_explicit(&r->phase_deadline_ms,
                                                memory_order_acquire);
        if (deadline > 0 && monotonic_ms() >= deadline) {
            int expected = 0;

            if (atomic_compare_exchange_strong_explicit(
                    &r->watchdog_expired, &expected, 1,
                    memory_order_acq_rel, memory_order_acquire)) {
                r->pa.threaded_mainloop_lock(r->mainloop);
                r->pa.threaded_mainloop_signal(r->mainloop, 0);
                r->pa.threaded_mainloop_unlock(r->mainloop);
            }
        }
        sleep_ms(5);
    }
    return NULL;
}

static void arm_watchdog(struct reducer *r, int64_t deadline_ms)
{
    atomic_store_explicit(&r->watchdog_expired, 0, memory_order_release);
    atomic_store_explicit(&r->phase_deadline_ms, deadline_ms,
                          memory_order_release);
}

static void disarm_watchdog(struct reducer *r)
{
    atomic_store_explicit(&r->phase_deadline_ms, 0, memory_order_release);
}

static int wait_for_context_ready(struct reducer *r, int timeout_ms)
{
    arm_watchdog(r, monotonic_ms() + timeout_ms);
    while (r->context_state != PA_CONTEXT_READY && !r->failed &&
           !atomic_load_explicit(&r->watchdog_expired, memory_order_acquire)) {
        r->pa.threaded_mainloop_wait(r->mainloop);
    }
    disarm_watchdog(r);
    if (r->context_state == PA_CONTEXT_READY)
        return 0;
    return atomic_load_explicit(&r->watchdog_expired, memory_order_acquire) ? 1 : -1;
}

static int wait_for_stream_ready(struct reducer *r, int timeout_ms)
{
    arm_watchdog(r, monotonic_ms() + timeout_ms);
    while (r->stream_state != PA_STREAM_READY && !r->failed &&
           !atomic_load_explicit(&r->watchdog_expired, memory_order_acquire)) {
        r->pa.threaded_mainloop_wait(r->mainloop);
    }
    disarm_watchdog(r);
    if (r->stream_state == PA_STREAM_READY)
        return 0;
    return atomic_load_explicit(&r->watchdog_expired, memory_order_acquire) ? 1 : -1;
}

static int load_symbol(void *handle, const char *name, void **out)
{
    const char *error;

    dlerror();
    *out = dlsym(handle, name);
    error = dlerror();
    if (error || !*out) {
        printf("AUDIO_PULSE_REDUCER_V1 phase=resolve symbol=%s status=FAIL error=\"%s\"\n",
               name, error ? error : "null");
        return -1;
    }
    return 0;
}

#define LOAD_API(api, member, symbol) \
    do { \
        if (load_symbol((api)->handle, (symbol), (void **)&(api)->member) < 0) \
            return -1; \
    } while (0)

static int load_pulse(struct pulse_api *pa)
{
    memset(pa, 0, sizeof(*pa));
    pa->handle = dlopen("libpulse.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!pa->handle) {
        printf("AUDIO_PULSE_REDUCER_V1 phase=dlopen library=libpulse.so.0 status=FAIL error=\"%s\"\n",
               dlerror());
        return -1;
    }
    printf("AUDIO_PULSE_REDUCER_V1 phase=dlopen library=libpulse.so.0 status=PASS\n");
    LOAD_API(pa, threaded_mainloop_new, "pa_threaded_mainloop_new");
    LOAD_API(pa, threaded_mainloop_free, "pa_threaded_mainloop_free");
    LOAD_API(pa, threaded_mainloop_get_api, "pa_threaded_mainloop_get_api");
    LOAD_API(pa, threaded_mainloop_start, "pa_threaded_mainloop_start");
    LOAD_API(pa, threaded_mainloop_stop, "pa_threaded_mainloop_stop");
    LOAD_API(pa, threaded_mainloop_lock, "pa_threaded_mainloop_lock");
    LOAD_API(pa, threaded_mainloop_unlock, "pa_threaded_mainloop_unlock");
    LOAD_API(pa, threaded_mainloop_wait, "pa_threaded_mainloop_wait");
    LOAD_API(pa, threaded_mainloop_signal, "pa_threaded_mainloop_signal");
    LOAD_API(pa, context_new, "pa_context_new");
    LOAD_API(pa, context_unref, "pa_context_unref");
    LOAD_API(pa, context_set_state_callback, "pa_context_set_state_callback");
    LOAD_API(pa, context_connect, "pa_context_connect");
    LOAD_API(pa, context_disconnect, "pa_context_disconnect");
    LOAD_API(pa, context_get_state, "pa_context_get_state");
    LOAD_API(pa, context_errno, "pa_context_errno");
    LOAD_API(pa, strerror_fn, "pa_strerror");
    LOAD_API(pa, proplist_new, "pa_proplist_new");
    LOAD_API(pa, proplist_free, "pa_proplist_free");
    LOAD_API(pa, stream_new_with_proplist, "pa_stream_new_with_proplist");
    LOAD_API(pa, stream_unref, "pa_stream_unref");
    LOAD_API(pa, stream_set_state_callback, "pa_stream_set_state_callback");
    LOAD_API(pa, stream_set_write_callback, "pa_stream_set_write_callback");
    LOAD_API(pa, stream_connect_playback, "pa_stream_connect_playback");
    LOAD_API(pa, stream_disconnect, "pa_stream_disconnect");
    LOAD_API(pa, stream_get_state, "pa_stream_get_state");
    LOAD_API(pa, stream_get_buffer_attr, "pa_stream_get_buffer_attr");
    LOAD_API(pa, stream_begin_write, "pa_stream_begin_write");
    LOAD_API(pa, stream_cancel_write, "pa_stream_cancel_write");
    LOAD_API(pa, stream_write, "pa_stream_write");
    LOAD_API(pa, stream_cork, "pa_stream_cork");
    LOAD_API(pa, stream_flush, "pa_stream_flush");
    LOAD_API(pa, operation_get_state, "pa_operation_get_state");
    LOAD_API(pa, operation_unref, "pa_operation_unref");
    return 0;
}

static int run_operation(struct reducer *r, const char *name, pa_operation *op,
                         struct operation_wait *waiter)
{
    int error = context_error(r);
    int state = -1;
    int timed_out = 0;

    if (!op) {
        printf("AUDIO_PULSE_REDUCER_V1 phase=operation name=%s result=null context_errno=%d context_error=\"%s\" verdict=FAIL\n",
               name, error, context_error_name(r, error));
        r->failed = 1;
        r->failure = name;
        return -1;
    }
    waiter->operation = op;
    arm_watchdog(r, monotonic_ms() + OPERATION_TIMEOUT_MS);
    while (!r->failed &&
           !atomic_load_explicit(&r->watchdog_expired, memory_order_acquire)) {
        state = r->pa.operation_get_state(op);
        if (state != PA_OPERATION_RUNNING)
            break;
        r->pa.threaded_mainloop_wait(r->mainloop);
    }
    disarm_watchdog(r);
    state = r->pa.operation_get_state(op);
    if (state == PA_OPERATION_RUNNING &&
        atomic_load_explicit(&r->watchdog_expired, memory_order_acquire))
        timed_out = 1;
    error = context_error(r);
    printf("AUDIO_PULSE_REDUCER_V1 phase=operation name=%s result=nonnull operation_state=%d timed_out=%d context_errno=%d context_error=\"%s\"\n",
           name, state, timed_out, error, context_error_name(r, error));
    if (state == PA_OPERATION_RUNNING || !waiter->callback_seen) {
        waiter->deferred = 1;
        waiter->next = r->deferred_operations;
        r->deferred_operations = waiter;
        printf("AUDIO_PULSE_REDUCER_V1 phase=operation_lifetime name=%s action=defer_until_mainloop_stop operation_state=%d callback_seen=%d\n",
               name, state, waiter->callback_seen);
    } else {
        r->pa.operation_unref(op);
        waiter->operation = NULL;
    }
    if (timed_out || !waiter->callback_seen) {
        r->failed = 1;
        r->failure = name;
        return -1;
    }
    return 0;
}

static int start_operation(struct reducer *r, const char *name, int cork)
{
    struct operation_wait *waiter = calloc(1, sizeof(*waiter));
    pa_operation *op;
    int result;

    if (!waiter) {
        r->failed = 1;
        r->failure = "operation-wait-allocation";
        return -1;
    }
    waiter->reducer = r;
    waiter->name = name;

    errno = 0;
    op = r->pa.stream_cork(r->stream, cork, operation_callback, waiter);
    printf("AUDIO_PULSE_REDUCER_V1 phase=operation_return name=%s pointer=%s errno=%d context_errno=%d\n",
           name, op ? "nonnull" : "null", errno, context_error(r));
    result = run_operation(r, name, op, waiter);
    printf("AUDIO_PULSE_REDUCER_V1 phase=operation_callback name=%s seen=%d success=%d\n",
           name, waiter->callback_seen, waiter->success);
    if (result == 0 && (!waiter->callback_seen || !waiter->success)) {
        r->failed = 1;
        r->failure = name;
        result = -1;
    }
    if (!waiter->deferred)
        free(waiter);
    return result;
}

static int simple_stream_operation(struct reducer *r, const char *name,
                                   pa_operation *(*fn)(pa_stream *, pa_stream_success_cb_t, void *))
{
    struct operation_wait *waiter = calloc(1, sizeof(*waiter));
    pa_operation *op;
    int result;

    if (!waiter) {
        r->failed = 1;
        r->failure = "operation-wait-allocation";
        return -1;
    }
    waiter->reducer = r;
    waiter->name = name;

    errno = 0;
    op = fn(r->stream, operation_callback, waiter);
    printf("AUDIO_PULSE_REDUCER_V1 phase=operation_return name=%s pointer=%s errno=%d context_errno=%d\n",
           name, op ? "nonnull" : "null", errno, context_error(r));
    result = run_operation(r, name, op, waiter);
    printf("AUDIO_PULSE_REDUCER_V1 phase=operation_callback name=%s seen=%d success=%d\n",
           name, waiter->callback_seen, waiter->success);
    if (result == 0 && (!waiter->callback_seen || !waiter->success)) {
        r->failed = 1;
        r->failure = name;
        result = -1;
    }
    if (!waiter->deferred)
        free(waiter);
    return result;
}

static int run_reducer(int duration_seconds)
{
    struct reducer r;
    const pa_buffer_attr requested = {
        UINT32_MAX, REQUESTED_TLENGTH, UINT32_MAX, REQUESTED_MINREQ,
        UINT32_MAX
    };
    const int stream_flags = PA_STREAM_INTERPOLATE_TIMING |
        PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE |
        PA_STREAM_NOT_MONOTONIC | PA_STREAM_START_CORKED;
    const pa_sample_spec spec = { PA_SAMPLE_FLOAT32LE, 48000, 2 };
    pa_channel_map map;
    const pa_buffer_attr *effective;
    const char *primary_reason = "none";
    const char *final_verdict;
    const char *final_class;
    const char *final_reason;
    int primary_failed = 0;
    int ret;
    int error;
    int locked = 0;
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    int64_t deadline;

    memset(&r, 0, sizeof(r));
    r.context_state = 0;
    r.stream_state = 0;
    r.request_min = SIZE_MAX;
    r.write_enabled = 1;
    atomic_init(&r.watchdog_running, 0);
    atomic_init(&r.watchdog_expired, 0);
    atomic_init(&r.phase_deadline_ms, 0);
    map.channels = 2;
    map.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
    map.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("AUDIO_PULSE_REDUCER_V1 phase=start schema=1 duration_seconds=%d\n",
           duration_seconds);
    printf("AUDIO_PULSE_REDUCER_V1 phase=contract mainloop=pa_threaded_mainloop stream_constructor=pa_stream_new_with_proplist stream_name=Playback proplist=empty sample_format=PA_SAMPLE_FLOAT32LE sample_format_value=%d rate=48000 channels=2 channel_map=front-left,front-right device=null context_flags=PA_CONTEXT_NOAUTOSPAWN context_flags_value=0x%x frames_per_write=%d bytes_per_write=%d write_path=pa_stream_begin_write,pa_stream_write\n",
           PA_SAMPLE_FLOAT32LE, PA_CONTEXT_NOAUTOSPAWN,
           CHROMIUM_FRAMES_PER_WRITE, CHROMIUM_WRITE_BYTES);
    printf("AUDIO_PULSE_REDUCER_V1 phase=requested_attr maxlength=%u minreq=%u prebuf=%u tlength=%u fragsize=%u\n",
           requested.maxlength, requested.minreq, requested.prebuf,
           requested.tlength, requested.fragsize);
    printf("AUDIO_PULSE_REDUCER_V1 phase=stream_flags names=START_CORKED,INTERPOLATE_TIMING,NOT_MONOTONIC,AUTO_TIMING_UPDATE,ADJUST_LATENCY value=0x%x\n",
           stream_flags);

    if (load_pulse(&r.pa) < 0) {
        printf("AUDIO_PULSE_REDUCER_V1 phase=result verdict=FAIL class=substrate reason=libpulse-load lifecycle_ms=0\n");
        return 20;
    }
    r.mainloop = r.pa.threaded_mainloop_new();
    printf("AUDIO_PULSE_REDUCER_V1 phase=threaded_mainloop_new result=%s\n",
           r.mainloop ? "nonnull" : "null");
    if (!r.mainloop) {
        r.failure = "threaded-mainloop-new";
        goto result;
    }
    errno = 0;
    ret = r.pa.threaded_mainloop_start(r.mainloop);
    printf("AUDIO_PULSE_REDUCER_V1 phase=threaded_mainloop_start ret=%d errno=%d\n",
           ret, errno);
    if (ret < 0) {
        r.failure = "threaded-mainloop-start";
        goto result;
    }
    r.mainloop_started = 1;
    atomic_store_explicit(&r.watchdog_running, 1, memory_order_release);
    if (pthread_create(&r.watchdog_thread, NULL, watchdog_main, &r) != 0) {
        r.failure = "watchdog-create";
        goto result;
    }
    r.watchdog_started = 1;
    r.pa.threaded_mainloop_lock(r.mainloop);
    locked = 1;

    r.context = r.pa.context_new(r.pa.threaded_mainloop_get_api(r.mainloop),
                                 "xv6-chromium-pulse-reducer");
    printf("AUDIO_PULSE_REDUCER_V1 phase=context_new result=%s\n",
           r.context ? "nonnull" : "null");
    if (!r.context) {
        r.failure = "context-new";
        goto result;
    }
    r.pa.context_set_state_callback(r.context, context_state_callback, &r);
    errno = 0;
    ret = r.pa.context_connect(r.context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);
    error = context_error(&r);
    printf("AUDIO_PULSE_REDUCER_V1 phase=context_connect server=default-null flags=0x%x ret=%d errno=%d context_errno=%d context_error=\"%s\"\n",
           PA_CONTEXT_NOAUTOSPAWN, ret, errno, error,
           context_error_name(&r, error));
    if (ret < 0) {
        r.failure = "context-connect";
        goto result;
    }
    ret = wait_for_context_ready(&r, 10000);
    if (ret != 0) {
        r.failure = ret > 0 ? "context-ready-timeout" : r.failure;
        goto result;
    }

    r.proplist = r.pa.proplist_new();
    printf("AUDIO_PULSE_REDUCER_V1 phase=proplist_new result=%s entries=0\n",
           r.proplist ? "nonnull" : "null");
    if (!r.proplist) {
        r.failure = "proplist-new";
        goto result;
    }
    r.stream = r.pa.stream_new_with_proplist(r.context, "Playback", &spec,
                                             &map, r.proplist);
    error = context_error(&r);
    printf("AUDIO_PULSE_REDUCER_V1 phase=stream_new_with_proplist name=Playback proplist=nonnull result=%s context_errno=%d context_error=\"%s\"\n",
           r.stream ? "nonnull" : "null", error,
           context_error_name(&r, error));
    if (!r.stream) {
        r.failure = "stream-new-with-proplist";
        goto result;
    }
    r.pa.stream_set_state_callback(r.stream, stream_state_callback, &r);
    r.pa.stream_set_write_callback(r.stream, stream_write_callback, &r);
    errno = 0;
    ret = r.pa.stream_connect_playback(r.stream, NULL, &requested,
                                       stream_flags, NULL, NULL);
    error = context_error(&r);
    printf("AUDIO_PULSE_REDUCER_V1 phase=stream_connect_playback device=null flags=0x%x ret=%d errno=%d context_errno=%d context_error=\"%s\"\n",
           stream_flags, ret, errno, error, context_error_name(&r, error));
    if (ret < 0) {
        r.failure = "stream-connect-playback";
        goto result;
    }
    ret = wait_for_stream_ready(&r, 10000);
    if (ret != 0) {
        r.failure = ret > 0 ? "stream-ready-timeout" : r.failure;
        goto result;
    }
    errno = 0;
    effective = r.pa.stream_get_buffer_attr(r.stream);
    error = context_error(&r);
    if (!effective) {
        printf("AUDIO_PULSE_REDUCER_V1 phase=effective_attr observed=1 result=null errno=%d context_errno=%d context_error=\"%s\" required_for_health=0\n",
               errno, error, context_error_name(&r, error));
    } else {
        printf("AUDIO_PULSE_REDUCER_V1 phase=effective_attr observed=1 result=nonnull maxlength=%u minreq=%u prebuf=%u tlength=%u fragsize=%u errno=%d context_errno=%d required_for_health=0\n",
               effective->maxlength, effective->minreq, effective->prebuf,
               effective->tlength, effective->fragsize, errno, error);
    }

    if (start_operation(&r, "start_uncork", 0) < 0)
        goto result;
    start_ms = monotonic_ms();
    deadline = start_ms + (int64_t)duration_seconds * 1000;
    r.pa.threaded_mainloop_unlock(r.mainloop);
    locked = 0;
    while (monotonic_ms() < deadline) {
        sleep_ms(10);
        r.pa.threaded_mainloop_lock(r.mainloop);
        locked = 1;
        ret = r.failed;
        r.pa.threaded_mainloop_unlock(r.mainloop);
        locked = 0;
        if (ret)
            break;
    }
    end_ms = monotonic_ms();
    r.pa.threaded_mainloop_lock(r.mainloop);
    locked = 1;
    printf("AUDIO_PULSE_REDUCER_V1 phase=lifecycle elapsed_ms=%lld required_ms=%d callbacks=%llu writes=%llu frames=%llu requested_total=%llu request_min=%zu request_max=%zu short_callbacks=%llu stream_state=%s context_state=%s\n",
           (long long)(end_ms - start_ms), duration_seconds * 1000,
           (unsigned long long)r.callback_count,
           (unsigned long long)r.write_count,
           (unsigned long long)r.frames_written,
           (unsigned long long)r.requested_total,
           r.request_min == SIZE_MAX ? 0 : r.request_min, r.request_max,
           (unsigned long long)r.short_callback_count,
           stream_state_name(r.stream_state),
           context_state_name(r.context_state));
    if (r.failed || end_ms - start_ms < (int64_t)duration_seconds * 1000 ||
        r.write_count == 0 || r.frames_written == 0) {
        primary_failed = 1;
        if (r.failure)
            primary_reason = r.failure;
        else if (end_ms - start_ms < (int64_t)duration_seconds * 1000)
            primary_reason = "lifecycle-short";
        else
            primary_reason = "no-writes";
    }

    /* Chrome's exact lifecycle: Start=uncork, Stop=flush+cork, Reset=flush. */
    r.write_enabled = 0;
    r.failed = 0;
    r.failure = NULL;
    if (simple_stream_operation(&r, "stop_flush", r.pa.stream_flush) < 0) {
        r.teardown_failures++;
        r.teardown_first_failure = "stop_flush";
    }
    r.failed = 0;
    r.failure = NULL;
    if (start_operation(&r, "stop_cork", 1) < 0) {
        r.teardown_failures++;
        if (!r.teardown_first_failure)
            r.teardown_first_failure = "stop_cork";
    }
    r.failed = 0;
    r.failure = NULL;
    if (simple_stream_operation(&r, "reset_flush", r.pa.stream_flush) < 0) {
        r.teardown_failures++;
        if (!r.teardown_first_failure)
            r.teardown_first_failure = "reset_flush";
    }

result:
    if (!primary_failed && r.failure) {
        primary_failed = 1;
        primary_reason = r.failure;
    }
    if (primary_failed) {
        final_verdict = "FAIL";
        final_class = "primary_playback";
        final_reason = primary_reason;
    } else if (r.teardown_failures > 0) {
        final_verdict = "FAIL";
        final_class = "teardown";
        final_reason = r.teardown_first_failure ? r.teardown_first_failure : "unknown";
    } else {
        final_verdict = "PASS";
        final_class = "healthy";
        final_reason = "complete";
    }
    {
    int deferred_lifetime = r.deferred_operations != NULL;
    if (r.stream) {
        r.pa.stream_set_write_callback(r.stream, NULL, NULL);
        printf("AUDIO_PULSE_REDUCER_V1 phase=close_clear_callback kind=write ret=void errno_preserved=1\n");
        r.pa.stream_set_state_callback(r.stream, NULL, NULL);
        printf("AUDIO_PULSE_REDUCER_V1 phase=close_clear_callback kind=state ret=void errno_preserved=1\n");
        errno = 0;
        ret = r.pa.stream_disconnect(r.stream);
        printf("AUDIO_PULSE_REDUCER_V1 phase=stream_disconnect ret=%d errno=%d context_errno=%d\n",
               ret, errno, context_error(&r));
        if (!deferred_lifetime) {
            r.pa.stream_unref(r.stream);
            printf("AUDIO_PULSE_REDUCER_V1 phase=stream_unref ret=void\n");
            r.stream = NULL;
        }
    }
    if (r.proplist) {
        r.pa.proplist_free(r.proplist);
        r.proplist = NULL;
    }
    if (r.context) {
        errno = 0;
        r.pa.context_disconnect(r.context);
        printf("AUDIO_PULSE_REDUCER_V1 phase=context_disconnect ret=void errno=%d context_errno=%d\n",
               errno, context_error(&r));
        if (!deferred_lifetime) {
            r.pa.context_unref(r.context);
            printf("AUDIO_PULSE_REDUCER_V1 phase=context_unref ret=void\n");
            r.context = NULL;
        }
    }
    if (locked) {
        r.pa.threaded_mainloop_unlock(r.mainloop);
        locked = 0;
    }
    if (r.watchdog_started) {
        atomic_store_explicit(&r.watchdog_running, 0, memory_order_release);
        pthread_join(r.watchdog_thread, NULL);
        r.watchdog_started = 0;
    }
    if (r.mainloop_started) {
        r.pa.threaded_mainloop_stop(r.mainloop);
        printf("AUDIO_PULSE_REDUCER_V1 phase=threaded_mainloop_stop ret=void\n");
        r.mainloop_started = 0;
    }
    while (r.deferred_operations) {
        struct operation_wait *waiter = r.deferred_operations;

        r.deferred_operations = waiter->next;
        if (waiter->operation)
            r.pa.operation_unref(waiter->operation);
        printf("AUDIO_PULSE_REDUCER_V1 phase=operation_deferred_release name=%s callback_seen=%d success=%d mainloop_stopped=1\n",
               waiter->name, waiter->callback_seen, waiter->success);
        free(waiter);
    }
    if (r.stream) {
        r.pa.stream_unref(r.stream);
        printf("AUDIO_PULSE_REDUCER_V1 phase=stream_unref ret=void deferred_safe=1\n");
        r.stream = NULL;
    }
    if (r.context) {
        r.pa.context_unref(r.context);
        printf("AUDIO_PULSE_REDUCER_V1 phase=context_unref ret=void deferred_safe=1\n");
        r.context = NULL;
    }
    if (r.mainloop) {
        r.pa.threaded_mainloop_free(r.mainloop);
        printf("AUDIO_PULSE_REDUCER_V1 phase=threaded_mainloop_free ret=void\n");
    }
    if (r.pa.handle)
        dlclose(r.pa.handle);
    }
    printf("AUDIO_PULSE_REDUCER_V1 phase=result verdict=%s class=%s reason=%s lifecycle_ms=%lld teardown_failures=%u teardown_first_failure=%s callbacks=%llu writes=%llu frames=%llu\n",
           final_verdict, final_class, final_reason,
           (long long)(start_ms > 0 && end_ms >= start_ms ? end_ms - start_ms : 0),
           r.teardown_failures,
           r.teardown_first_failure ? r.teardown_first_failure : "none",
           (unsigned long long)r.callback_count,
           (unsigned long long)r.write_count,
           (unsigned long long)r.frames_written);
    return !primary_failed && r.teardown_failures == 0 ? 0 : 21;
}

static void put_le16(FILE *file, uint16_t value)
{
    fputc(value & 0xff, file);
    fputc((value >> 8) & 0xff, file);
}

static void put_le32(FILE *file, uint32_t value)
{
    put_le16(file, value & 0xffff);
    put_le16(file, value >> 16);
}

static int write_fixtures(const char *wav_path, const char *raw_path)
{
    const uint32_t frames = 48000U * 3U;
    const uint32_t data_bytes = frames * 2U * 2U;
    FILE *wav = NULL;
    FILE *raw = NULL;
    uint32_t frame;
    int failed = 0;

    wav = fopen(wav_path, "wb");
    raw = fopen(raw_path, "wb");
    if (!wav || !raw) {
        printf("AUDIO_LOCALIZER_FIXTURE schema=1 verdict=FAIL errno=%d\n", errno);
        failed = 1;
        goto out;
    }
    fwrite("RIFF", 1, 4, wav);
    put_le32(wav, 36U + data_bytes);
    fwrite("WAVEfmt ", 1, 8, wav);
    put_le32(wav, 16);
    put_le16(wav, 1);
    put_le16(wav, 2);
    put_le32(wav, 48000);
    put_le32(wav, 48000U * 4U);
    put_le16(wav, 4);
    put_le16(wav, 16);
    fwrite("data", 1, 4, wav);
    put_le32(wav, data_bytes);
    for (frame = 0; frame < frames; frame++) {
        int16_t sample = ((frame / 24U) & 1U) ? 2048 : -2048;
        put_le16(wav, (uint16_t)sample);
        put_le16(wav, (uint16_t)sample);
        put_le16(raw, (uint16_t)sample);
        put_le16(raw, (uint16_t)sample);
    }
    if (fflush(wav) || fflush(raw) || ferror(wav) || ferror(raw))
        failed = 1;
out:
    if (wav && fclose(wav))
        failed = 1;
    if (raw && fclose(raw))
        failed = 1;
    printf("AUDIO_LOCALIZER_FIXTURE schema=1 verdict=%s wav=%s raw=%s frames=%u rate=48000 channels=2 format=s16le wav_bytes=%u raw_bytes=%u\n",
           failed ? "FAIL" : "PASS", wav_path, raw_path, frames,
           44U + data_bytes, data_bytes);
    return failed ? 30 : 0;
}

static int probe_socket(const char *path)
{
    struct stat st;
    struct sockaddr_un address;
    int fd;
    int ret;
    int saved_errno;

    errno = 0;
    ret = lstat(path, &st);
    saved_errno = errno;
    printf("AUDIO_LOCALIZER_SOCKET schema=1 path=%s lstat_ret=%d errno=%d type=%s mode=%o\n",
           path, ret, saved_errno,
           ret == 0 && S_ISSOCK(st.st_mode) ? "socket" :
           ret == 0 && S_ISLNK(st.st_mode) ? "symlink" :
           ret == 0 && S_ISREG(st.st_mode) ? "regular" :
           ret == 0 ? "other" : "missing",
           ret == 0 ? (unsigned)(st.st_mode & 07777) : 0U);
    if (ret < 0 || !S_ISSOCK(st.st_mode))
        return 40;
    if (strlen(path) >= sizeof(address.sun_path)) {
        printf("AUDIO_LOCALIZER_SOCKET schema=1 path=%s connect_ret=-1 errno=%d verdict=FAIL\n",
               path, ENAMETOOLONG);
        return 41;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        printf("AUDIO_LOCALIZER_SOCKET schema=1 path=%s socket_ret=-1 errno=%d verdict=FAIL\n",
               path, errno);
        return 42;
    }
    errno = 0;
    ret = connect(fd, (struct sockaddr *)&address, sizeof(address));
    saved_errno = errno;
    close(fd);
    printf("AUDIO_LOCALIZER_SOCKET schema=1 path=%s connect_ret=%d errno=%d verdict=%s\n",
           path, ret, saved_errno, ret == 0 ? "PASS" : "FAIL");
    return ret == 0 ? 0 : 43;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

struct owner_record {
    pid_t pid;
    pid_t pgid;
    unsigned long long starttime;
    char expected[64];
};

static int proc_stat_identity(pid_t pid, pid_t *pgid_out,
                              unsigned long long *starttime_out,
                              char *state_out)
{
    char path[64];
    char line[2048];
    char *close_paren;
    char *save = NULL;
    char *token;
    FILE *file;
    int index = 0;
    pid_t pgid = -1;
    unsigned long long starttime = 0;
    char state = '?';

    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    file = fopen(path, "r");
    if (!file)
        return -1;
    if (!fgets(line, sizeof(line), file)) {
        int saved = errno;
        fclose(file);
        errno = saved ? saved : EIO;
        return -1;
    }
    fclose(file);
    close_paren = strrchr(line, ')');
    if (!close_paren || close_paren[1] != ' ') {
        errno = EPROTO;
        return -1;
    }
    token = strtok_r(close_paren + 2, " ", &save);
    while (token) {
        if (index == 0)
            state = token[0];
        else if (index == 2) {
            char *end = NULL;
            long value = strtol(token, &end, 10);
            if (!end || (*end && *end != '\n') || value <= 0 || value > INT_MAX) {
                errno = EPROTO;
                return -1;
            }
            pgid = (pid_t)value;
        } else if (index == 19) {
            char *end = NULL;
            errno = 0;
            starttime = strtoull(token, &end, 10);
            if (errno || !end || (*end && *end != '\n') || starttime == 0) {
                errno = EPROTO;
                return -1;
            }
            break;
        }
        index++;
        token = strtok_r(NULL, " ", &save);
    }
    if (pgid <= 0 || starttime == 0) {
        errno = EPROTO;
        return -1;
    }
    if (pgid_out)
        *pgid_out = pgid;
    if (starttime_out)
        *starttime_out = starttime;
    if (state_out)
        *state_out = state;
    return 0;
}

static int process_identity(pid_t pid, const char *expected, char *path,
                            size_t path_size, pid_t *pgid_out,
                            unsigned long long expected_starttime,
                            unsigned long long *starttime_out)
{
    char proc_path[64];
    ssize_t length;
    pid_t pgid;
    unsigned long long starttime;

    if (proc_stat_identity(pid, &pgid, &starttime, NULL) < 0)
        return -1;
    snprintf(proc_path, sizeof(proc_path), "/proc/%ld/exe", (long)pid);
    length = readlink(proc_path, path, path_size - 1);
    if (length < 0)
        return -1;
    path[length] = '\0';
    if (pgid_out)
        *pgid_out = pgid;
    if (starttime_out)
        *starttime_out = starttime;
    if (pgid != pid || strcmp(base_name(path), expected) != 0 ||
        (expected_starttime != 0 && starttime != expected_starttime)) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int safe_owner_record_path(const char *path)
{
    const unsigned char *cursor;

    if (!path || path[0] != '/' || strlen(path) >= PATH_MAX ||
        strstr(path, "../") != NULL)
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

static int write_owner_record(const char *path, const struct owner_record *record)
{
    FILE *file;
    int failed = 0;

    if (!safe_owner_record_path(path)) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "wx");
    if (!file)
        return -1;
    if (fprintf(file, "schema=1 pid=%ld pgid=%ld starttime=%llu expected=%s\n",
                (long)record->pid, (long)record->pgid,
                record->starttime, record->expected) < 0 ||
        fflush(file) != 0 || ferror(file))
        failed = 1;
    if (fclose(file) != 0)
        failed = 1;
    if (failed) {
        unlink(path);
        errno = EIO;
        return -1;
    }
    return 0;
}

static int read_owner_record(const char *path, struct owner_record *record)
{
    FILE *file;
    long pid;
    long pgid;
    char trailing;

    if (!safe_owner_record_path(path)) {
        errno = EINVAL;
        return -1;
    }
    memset(record, 0, sizeof(*record));
    file = fopen(path, "r");
    if (!file)
        return -1;
    if (fscanf(file,
               "schema=1 pid=%ld pgid=%ld starttime=%llu expected=%63[A-Za-z0-9._-]%c",
               &pid, &pgid, &record->starttime, record->expected,
               &trailing) != 5 || trailing != '\n' ||
        fgetc(file) != EOF || pid <= 1 || pid > INT_MAX ||
        pgid != pid || record->starttime == 0 || !record->expected[0]) {
        fclose(file);
        errno = EPROTO;
        return -1;
    }
    fclose(file);
    record->pid = (pid_t)pid;
    record->pgid = (pid_t)pgid;
    return 0;
}

static int proc_group_live_count(pid_t wanted_pgid)
{
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    int count = 0;

    if (!dir)
        return -1;
    while ((entry = readdir(dir)) != NULL) {
        char *end = NULL;
        long pid = strtol(entry->d_name, &end, 10);
        char state = '?';
        pid_t pgrp = -1;

        if (!entry->d_name[0] || !end || *end || pid <= 0)
            continue;
        if (proc_stat_identity((pid_t)pid, &pgrp, NULL, &state) < 0)
            continue;
        if (pgrp == wanted_pgid && state != 'Z')
            count++;
    }
    closedir(dir);
    return count;
}

static int pgroup_leader(int argc, char **argv)
{
    const char *expected;
    const char *record_path;
    struct owner_record record;

    if (argc < 4)
        return 50;
    expected = argv[0];
    record_path = argv[1];
    if (strcmp(argv[2], "--") != 0 || !expected[0] ||
        strchr(expected, '/') != NULL || strcmp(expected, ".") == 0 ||
        strcmp(expected, "..") == 0 || !safe_owner_record_path(record_path)) {
        printf("AUDIO_LOCALIZER_OWNER schema=1 phase=leader verdict=FAIL reason=argv-contract\n");
        return 51;
    }
    if (setpgid(0, 0) < 0) {
        printf("AUDIO_LOCALIZER_OWNER schema=1 phase=leader verdict=FAIL reason=setpgid errno=%d\n",
               errno);
        return 52;
    }
    memset(&record, 0, sizeof(record));
    record.pid = getpid();
    if (proc_stat_identity(record.pid, &record.pgid, &record.starttime,
                           NULL) < 0 || record.pgid != record.pid ||
        snprintf(record.expected, sizeof(record.expected), "%s", expected) >=
            (int)sizeof(record.expected) ||
        write_owner_record(record_path, &record) < 0) {
        printf("AUDIO_LOCALIZER_OWNER schema=1 phase=leader verdict=FAIL reason=owner-record pid=%ld errno=%d record=%s\n",
               (long)getpid(), errno, record_path);
        return 53;
    }
    printf("AUDIO_LOCALIZER_OWNER schema=1 phase=leader verdict=PASS pid=%ld pgid=%ld starttime=%llu expected=%s record=%s command=%s\n",
           (long)record.pid, (long)record.pgid, record.starttime, expected,
           record_path, argv[3]);
    fflush(stdout);
    execv(argv[3], &argv[3]);
    printf("AUDIO_LOCALIZER_OWNER schema=1 phase=exec verdict=FAIL errno=%d\n",
           errno);
    return 54;
}

static int pgroup_identity(const char *record_path)
{
    struct owner_record record;
    pid_t pgid = -1;
    unsigned long long starttime = 0;
    char path[PATH_MAX];
    int ret;

    if (read_owner_record(record_path, &record) < 0) {
        printf("AUDIO_LOCALIZER_OWNER schema=1 phase=identity record=%s verdict=FAIL reason=record errno=%d\n",
               record_path, errno);
        return 60;
    }
    errno = 0;
    ret = process_identity(record.pid, record.expected, path, sizeof(path),
                           &pgid, record.starttime, &starttime);
    printf("AUDIO_LOCALIZER_OWNER schema=1 phase=identity pid=%ld pgid=%ld starttime=%llu recorded_starttime=%llu expected=%s exe=%s record=%s verdict=%s errno=%d\n",
           (long)record.pid, (long)pgid, starttime, record.starttime,
           record.expected,
           ret == 0 ? path : "unavailable", record_path,
           ret == 0 ? "PASS" : "FAIL", errno);
    return ret == 0 ? 0 : 61;
}

static int pgroup_terminate(const char *record_path)
{
    struct owner_record record;
    pid_t pgid = -1;
    unsigned long long starttime = 0;
    char path[PATH_MAX];
    int ret;
    int attempt;
    int live = -1;

    if (read_owner_record(record_path, &record) < 0) {
        printf("AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_identity record=%s verdict=FAIL reason=record errno=%d\n",
               record_path, errno);
        return 70;
    }
    errno = 0;
    ret = process_identity(record.pid, record.expected, path, sizeof(path),
                           &pgid, record.starttime, &starttime);
    printf("AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_identity pid=%ld pgid=%ld starttime=%llu recorded_starttime=%llu expected=%s exe=%s record=%s verdict=%s errno=%d\n",
           (long)record.pid, (long)pgid, starttime, record.starttime,
           record.expected,
           ret == 0 ? path : "unavailable", record_path,
           ret == 0 ? "PASS" : "FAIL", errno);
    if (ret < 0) {
        pid_t observed_pgid = -1;
        unsigned long long observed_starttime = 0;
        int stat_ret = proc_stat_identity(record.pid, &observed_pgid,
                                          &observed_starttime, NULL);
        live = proc_group_live_count(record.pgid);
        if (stat_ret == 0 && observed_starttime != record.starttime) {
            printf("AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_result pid=%ld pgid=%ld recorded_starttime=%llu observed_starttime=%llu live=%d verdict=FAIL reason=pid-reused\n",
                   (long)record.pid, (long)record.pgid, record.starttime,
                   observed_starttime, live);
            return 71;
        }
        if (live == 0) {
            printf("AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_result pid=%ld pgid=%ld starttime=%llu live=0 verdict=PASS reason=already-exited\n",
                   (long)record.pid, (long)record.pgid, record.starttime);
            return 0;
        }
        return 71;
    }
    errno = 0;
    ret = kill(-pgid, SIGTERM);
    printf("AUDIO_LOCALIZER_OWNER schema=1 phase=signal signal=TERM pid=%ld pgid=%ld starttime=%llu ret=%d errno=%d\n",
           (long)record.pid, (long)pgid, record.starttime, ret, errno);
    if (ret < 0 && errno != ESRCH)
        return 72;
    for (attempt = 0; attempt < 30; attempt++) {
        live = proc_group_live_count(pgid);
        if (live == 0)
            break;
        sleep_ms(100);
    }
    if (live != 0) {
        errno = 0;
        ret = kill(-pgid, SIGKILL);
        printf("AUDIO_LOCALIZER_OWNER schema=1 phase=signal signal=KILL pid=%ld pgid=%ld starttime=%llu ret=%d errno=%d live_before=%d\n",
               (long)record.pid, (long)pgid, record.starttime, ret, errno,
               live);
        for (attempt = 0; attempt < 20; attempt++) {
            live = proc_group_live_count(pgid);
            if (live == 0)
                break;
            sleep_ms(100);
        }
    }
    printf("AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_result pid=%ld pgid=%ld starttime=%llu live=%d verdict=%s\n",
           (long)record.pid, (long)pgid, record.starttime, live,
           live == 0 ? "PASS" : "FAIL");
    return live == 0 ? 0 : 73;
}

static int run_bounded(const char *seconds_text, const char *expected,
                       int argc, char **argv)
{
    char *end = NULL;
    long seconds;
    pid_t child;
    int status = 0;
    int timed_out = 0;
    int64_t start;
    int live;

    errno = 0;
    seconds = strtol(seconds_text, &end, 10);
    if (errno || !end || *end || seconds < 1 || seconds > 120 || argc < 1 ||
        strcmp(base_name(argv[0]), expected) != 0) {
        printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=preflight verdict=FAIL reason=argv-contract\n");
        return 80;
    }
    child = fork();
    if (child < 0) {
        printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=fork verdict=FAIL errno=%d\n",
               errno);
        return 81;
    }
    if (child == 0) {
        if (setpgid(0, 0) < 0)
            _exit(125);
        execv(argv[0], argv);
        _exit(127);
    }
    if (setpgid(child, child) < 0 && errno != EACCES && errno != ESRCH) {
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=parent_setpgid verdict=FAIL pid=%ld errno=%d\n",
               (long)child, errno);
        return 82;
    }
    printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=start pid=%ld pgid=%ld expected=%s timeout_seconds=%ld command=%s\n",
           (long)child, (long)child, expected, seconds, argv[0]);
    start = monotonic_ms();
    for (;;) {
        pid_t waited = waitpid(child, &status, WNOHANG);

        if (waited == child)
            break;
        if (waited < 0) {
            printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=wait verdict=FAIL pid=%ld errno=%d\n",
                   (long)child, errno);
            return 83;
        }
        if (monotonic_ms() - start >= seconds * 1000) {
            timed_out = 1;
            kill(-child, SIGTERM);
            sleep_ms(300);
            live = proc_group_live_count(child);
            if (live != 0)
                kill(-child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
                ;
            break;
        }
        sleep_ms(20);
    }
    live = proc_group_live_count(child);
    if (live != 0) {
        kill(-child, SIGKILL);
        sleep_ms(100);
        live = proc_group_live_count(child);
    }
    if (timed_out) {
        printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=result pid=%ld elapsed_ms=%lld timed_out=1 child_exit=NA child_signal=NA live=%d verdict=%s return_code=124\n",
               (long)child, (long long)(monotonic_ms() - start), live,
               live == 0 ? "TIMEOUT_CLEAN" : "FAIL");
        return live == 0 ? 124 : 84;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=result pid=%ld elapsed_ms=%lld timed_out=0 child_exit=%d child_signal=0 live=%d verdict=%s return_code=%d\n",
               (long)child, (long long)(monotonic_ms() - start), code, live,
               live == 0 && code == 0 ? "PASS" : "FAIL", code);
        return live == 0 ? code : 84;
    }
    if (WIFSIGNALED(status)) {
        int signal_number = WTERMSIG(status);
        printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=result pid=%ld elapsed_ms=%lld timed_out=0 child_exit=NA child_signal=%d live=%d verdict=FAIL return_code=%d\n",
               (long)child, (long long)(monotonic_ms() - start), signal_number,
               live, 128 + signal_number);
        return live == 0 ? 128 + signal_number : 84;
    }
    printf("AUDIO_LOCALIZER_BOUNDED schema=1 phase=result pid=%ld elapsed_ms=%lld timed_out=0 child_exit=NA child_signal=NA live=%d verdict=FAIL return_code=85\n",
           (long)child, (long long)(monotonic_ms() - start), live);
    return 85;
}

static void usage(FILE *out)
{
    fprintf(out,
            "usage:\n"
            "  chromium-pulse-stream-reducer [--duration-seconds=20..60]\n"
            "  chromium-pulse-stream-reducer --emit-fixtures WAV RAW\n"
            "  chromium-pulse-stream-reducer --probe-socket PATH\n"
            "  chromium-pulse-stream-reducer --pgroup-leader EXPECTED RECORD -- PROGRAM [ARGS...]\n"
            "  chromium-pulse-stream-reducer --pgroup-identity RECORD\n"
            "  chromium-pulse-stream-reducer --pgroup-terminate RECORD\n"
            "  chromium-pulse-stream-reducer --run-bounded SECONDS EXPECTED -- PROGRAM [ARGS...]\n");
}

int main(int argc, char **argv)
{
    int duration = DEFAULT_DURATION_SECONDS;

    if (argc == 1)
        return run_reducer(duration);
    if (argc == 2 && strncmp(argv[1], "--duration-seconds=", 19) == 0) {
        char *end = NULL;
        long value = strtol(argv[1] + 19, &end, 10);
        long minimum = getenv("XV6_PULSE_REDUCER_TEST_MODE") ? 1 : 20;
        if (!end || *end || value < minimum || value > 60) {
            usage(stderr);
            return 2;
        }
        return run_reducer((int)value);
    }
    if (argc == 4 && strcmp(argv[1], "--emit-fixtures") == 0)
        return write_fixtures(argv[2], argv[3]);
    if (argc == 3 && strcmp(argv[1], "--probe-socket") == 0)
        return probe_socket(argv[2]);
    if (argc >= 6 && strcmp(argv[1], "--pgroup-leader") == 0)
        return pgroup_leader(argc - 2, &argv[2]);
    if (argc == 3 && strcmp(argv[1], "--pgroup-identity") == 0)
        return pgroup_identity(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--pgroup-terminate") == 0)
        return pgroup_terminate(argv[2]);
    if (argc >= 6 && strcmp(argv[1], "--run-bounded") == 0 &&
        strcmp(argv[4], "--") == 0)
        return run_bounded(argv[2], argv[3], argc - 5, &argv[5]);
    usage(stderr);
    return 2;
}
