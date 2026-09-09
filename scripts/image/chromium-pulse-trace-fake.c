#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FAKE_CONTEXT_STATE
#define FAKE_CONTEXT_STATE 4
#endif

typedef struct pa_context pa_context;
typedef void (*pa_context_notify_cb_t)(pa_context *, void *);

struct pa_context {
    int error;
    int active;
    pa_context_notify_cb_t state_cb;
    void *state_userdata;
};

typedef struct pa_stream pa_stream;
typedef struct pa_operation pa_operation;
typedef struct pa_cvolume pa_cvolume;
typedef struct pa_proplist { int empty; } pa_proplist;
typedef struct pa_mainloop_api { int unused; } pa_mainloop_api;
typedef struct pa_threaded_mainloop {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pa_mainloop_api api;
    int started;
} pa_threaded_mainloop;

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

typedef void (*pa_stream_notify_cb_t)(pa_stream *, void *);
typedef void (*pa_stream_request_cb_t)(pa_stream *, size_t, void *);
typedef void (*pa_stream_success_cb_t)(pa_stream *, int, void *);
typedef void (*pa_free_cb_t)(void *);

struct pa_stream {
    pa_context *context;
    int active;
    pa_stream_notify_cb_t state_cb;
    void *state_userdata;
    pa_stream_request_cb_t write_cb;
    void *write_userdata;
};

struct pa_operation {
    int active;
    int state;
    pa_stream *stream;
    pa_stream_success_cb_t callback;
    void *userdata;
    int callback_pending;
    int late;
};

static pa_context context_object;
static pa_stream stream_object;
static pa_operation operation_objects[8];
static unsigned char write_buffer[16384];
static const pa_buffer_attr effective = {
    4194304U, 12288U, 8192U, 4096U, UINT32_MAX
};
static unsigned flush_calls;
static int late_assigned;
static size_t last_reservation;

static size_t fake_request_bytes(size_t fallback)
{
    const char *text = getenv("XV6_PULSE_REDUCER_FAKE_REQUEST_BYTES");
    char *end = NULL;
    unsigned long value;

    if (!text)
        return fallback;
    value = strtoul(text, &end, 10);
    return end && !*end ? (size_t)value : fallback;
}

pa_threaded_mainloop *pa_threaded_mainloop_new(void)
{
    pa_threaded_mainloop *mainloop = calloc(1, sizeof(*mainloop));
    if (!mainloop)
        return NULL;
    if (pthread_mutex_init(&mainloop->mutex, NULL) != 0 ||
        pthread_cond_init(&mainloop->condition, NULL) != 0) {
        free(mainloop);
        return NULL;
    }
    return mainloop;
}

void pa_threaded_mainloop_free(pa_threaded_mainloop *mainloop)
{
    if (!mainloop)
        return;
    pthread_cond_destroy(&mainloop->condition);
    pthread_mutex_destroy(&mainloop->mutex);
    free(mainloop);
}

pa_mainloop_api *pa_threaded_mainloop_get_api(pa_threaded_mainloop *mainloop)
{
    return mainloop ? &mainloop->api : NULL;
}

int pa_threaded_mainloop_start(pa_threaded_mainloop *mainloop)
{
    if (!mainloop)
        return -1;
    mainloop->started = 1;
    return 0;
}

void pa_threaded_mainloop_stop(pa_threaded_mainloop *mainloop)
{
    if (mainloop)
        mainloop->started = 0;
}

void pa_threaded_mainloop_lock(pa_threaded_mainloop *mainloop)
{
    pthread_mutex_lock(&mainloop->mutex);
}

void pa_threaded_mainloop_unlock(pa_threaded_mainloop *mainloop)
{
    pthread_mutex_unlock(&mainloop->mutex);
}

void pa_threaded_mainloop_wait(pa_threaded_mainloop *mainloop)
{
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 5000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    (void)pthread_cond_timedwait(&mainloop->condition, &mainloop->mutex,
                                 &deadline);
    if (stream_object.active && stream_object.write_cb)
        stream_object.write_cb(&stream_object, fake_request_bytes(12288),
                               stream_object.write_userdata);
}

void pa_threaded_mainloop_signal(pa_threaded_mainloop *mainloop, int wait)
{
    (void)wait;
    pthread_cond_broadcast(&mainloop->condition);
}

pa_proplist *pa_proplist_new(void)
{
    return calloc(1, sizeof(pa_proplist));
}

void pa_proplist_free(pa_proplist *properties)
{
    free(properties);
}

const char *pa_strerror(int error)
{
    (void)error;
    return "fake-error";
}

int fake_untargeted(void)
{
    return 77;
}

pa_context *pa_context_new(void *api, const char *name)
{
    (void)api;
    (void)name;
    memset(&context_object, 0, sizeof(context_object));
    context_object.active = 1;
    errno = EAGAIN;
    return &context_object;
}

int pa_context_connect(pa_context *context, const char *server, int flags,
                       const void *api)
{
    (void)server;
    (void)flags;
    (void)api;
    if (!context || !context->active) {
        errno = EINVAL;
        return -1;
    }
    errno = EALREADY;
    return 0;
}

int pa_context_get_state(const pa_context *context)
{
    errno = ENOTTY;
    return context && context->active ? FAKE_CONTEXT_STATE : 6;
}

int pa_context_errno(const pa_context *context)
{
    errno = ENOMSG;
    return context ? context->error : -1;
}

void pa_context_set_state_callback(pa_context *context,
        pa_context_notify_cb_t callback, void *userdata)
{
    if (context) {
        context->state_cb = callback;
        context->state_userdata = userdata;
    }
    errno = EISCONN;
    if (callback)
        callback(context, userdata);
}

void pa_context_disconnect(pa_context *context)
{
    (void)context;
    errno = ENOLINK;
}

void pa_context_unref(pa_context *context)
{
    if (context)
        context->active = 0;
    errno = EOWNERDEAD;
}

pa_stream *pa_stream_new(pa_context *context, const char *name,
                         const pa_sample_spec *spec,
                         const pa_channel_map *map)
{
    (void)name;
    (void)spec;
    (void)map;
    memset(&stream_object, 0, sizeof(stream_object));
    stream_object.context = context;
    stream_object.active = 1;
    errno = ECHRNG;
    return &stream_object;
}

pa_stream *pa_stream_new_with_proplist(pa_context *context, const char *name,
        const pa_sample_spec *spec, const pa_channel_map *map,
        pa_proplist *properties)
{
    (void)properties;
    return pa_stream_new(context, name, spec, map);
}

int pa_stream_connect_playback(pa_stream *stream, const char *device,
        const pa_buffer_attr *attr, int flags, const pa_cvolume *volume,
        pa_stream *sync_stream)
{
    (void)device;
    (void)attr;
    (void)flags;
    (void)volume;
    (void)sync_stream;
    if (!stream || !stream->active) {
        errno = EINVAL;
        return -1;
    }
    flush_calls = 0;
    errno = ECOMM;
    return 0;
}

int pa_stream_get_state(const pa_stream *stream)
{
    errno = EREMOTE;
    return stream && stream->active ? 2 : 4;
}

int pa_stream_begin_write(pa_stream *stream, void **data, size_t *bytes)
{
    const char *forced = getenv("XV6_PULSE_REDUCER_FAKE_BEGIN_BYTES");
    (void)stream;
    if (getenv("XV6_PULSE_TRACE_FAKE_BEGIN_FAIL")) {
        errno = EIO;
        return -1;
    }
    if (!data || !bytes) {
        errno = EINVAL;
        return -1;
    }
    *data = write_buffer;
    if (forced) {
        char *end = NULL;
        unsigned long value = strtoul(forced, &end, 10);
        if (!end || *end || value > sizeof(write_buffer)) {
            errno = EINVAL;
            return -1;
        }
        *bytes = (size_t)value;
    }
    if (*bytes > sizeof(write_buffer))
        *bytes = sizeof(write_buffer);
    last_reservation = *bytes;
    errno = ENODATA;
    return 0;
}

int pa_stream_write(pa_stream *stream, const void *data, size_t bytes,
                    pa_free_cb_t free_cb, int64_t offset, int seek)
{
    (void)stream;
    if (bytes > last_reservation) {
        errno = EMSGSIZE;
        return -1;
    }
    (void)offset;
    (void)seek;
    if (free_cb)
        free_cb((void *)data);
    errno = ENOSR;
    return 0;
}

int pa_stream_cancel_write(pa_stream *stream)
{
    (void)stream;
    last_reservation = 0;
    return 0;
}

void pa_stream_set_state_callback(pa_stream *stream,
        pa_stream_notify_cb_t callback, void *userdata)
{
    if (stream) {
        stream->state_cb = callback;
        stream->state_userdata = userdata;
    }
    errno = ESTALE;
    if (callback)
        callback(stream, userdata);
}

void pa_stream_set_write_callback(pa_stream *stream,
        pa_stream_request_cb_t callback, void *userdata)
{
    if (stream) {
        stream->write_cb = callback;
        stream->write_userdata = userdata;
    }
    errno = EREMOTEIO;
    if (callback)
        callback(stream, fake_request_bytes(4096), userdata);
}

static pa_operation *new_operation(pa_stream *stream,
        pa_stream_success_cb_t callback, void *userdata)
{
    pa_operation *operation = NULL;
    size_t index;

    for (index = 0; index < sizeof(operation_objects) / sizeof(operation_objects[0]);
         index++) {
        if (!operation_objects[index].active) {
            operation = &operation_objects[index];
            break;
        }
    }
    if (!operation) {
        errno = ENOSPC;
        return NULL;
    }
    memset(operation, 0, sizeof(*operation));
    operation->active = 1;
    operation->state = 0;
    operation->stream = stream;
    operation->callback = callback;
    operation->userdata = userdata;
    operation->callback_pending = callback != NULL;
    if (getenv("XV6_PULSE_REDUCER_FAKE_LATE_CALLBACK") && !late_assigned) {
        operation->late = 1;
        late_assigned = 1;
    }
    errno = EINPROGRESS;
    return operation;
}

pa_operation *pa_stream_cork(pa_stream *stream, int cork,
        pa_stream_success_cb_t callback, void *userdata)
{
    (void)cork;
    if (getenv("XV6_PULSE_TRACE_FAKE_NULL_CORK")) {
        errno = EPIPE;
        return NULL;
    }
    return new_operation(stream, callback, userdata);
}

pa_operation *pa_stream_flush(pa_stream *stream,
        pa_stream_success_cb_t callback, void *userdata)
{
    flush_calls++;
    if (getenv("XV6_PULSE_TRACE_FAKE_LATE_NULL") && flush_calls >= 2) {
        errno = EPIPE;
        return NULL;
    }
    return new_operation(stream, callback, userdata);
}

int pa_stream_disconnect(pa_stream *stream)
{
    size_t index;

    for (index = 0; index < sizeof(operation_objects) / sizeof(operation_objects[0]);
         index++) {
        pa_operation *operation = &operation_objects[index];
        if (operation->active && operation->late &&
            operation->callback_pending) {
            operation->callback_pending = 0;
            operation->state = 1;
            operation->callback(operation->stream, 1,
                                operation->userdata);
        }
    }
    errno = ESHUTDOWN;
    return stream && stream->active ? 0 : -1;
}

void pa_stream_unref(pa_stream *stream)
{
    if (stream)
        stream->active = 0;
    errno = ECANCELED;
}

int pa_operation_get_state(const pa_operation *operation_const)
{
    pa_operation *operation = (pa_operation *)operation_const;
    if (!operation || !operation->active) {
        errno = EINVAL;
        return 2;
    }
    if (operation->late) {
        errno = EINPROGRESS;
        return 0;
    }
    if (operation->callback_pending) {
        operation->callback_pending = 0;
        operation->callback(operation->stream, 1, operation->userdata);
    }
    operation->state = 1;
    errno = EBADE;
    return operation->state;
}

void pa_operation_unref(pa_operation *operation)
{
    if (operation)
        operation->active = 0;
    errno = EBADR;
}

const pa_buffer_attr *pa_stream_get_buffer_attr(const pa_stream *stream)
{
    (void)stream;
    errno = ENOANO;
    return &effective;
}
