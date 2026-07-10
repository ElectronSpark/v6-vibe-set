#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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

typedef void (*pa_stream_notify_cb_t)(pa_stream *, void *);
typedef void (*pa_context_notify_cb_t)(pa_context *, void *);
typedef void (*pa_stream_request_cb_t)(pa_stream *, size_t, void *);
typedef void (*pa_stream_success_cb_t)(pa_stream *, int, void *);
typedef void (*pa_free_cb_t)(void *);

struct pulse_api {
    pa_context *(*context_new)(void *, const char *);
    int (*context_connect)(pa_context *, const char *, int, const void *);
    int (*context_state)(const pa_context *);
    int (*context_errno)(const pa_context *);
    void (*set_context_state_callback)(pa_context *, pa_context_notify_cb_t, void *);
    void (*context_disconnect)(pa_context *);
    void (*context_unref)(pa_context *);
    pa_stream *(*stream_new)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *);
    pa_stream *(*stream_new_with_proplist)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *, pa_proplist *);
    int (*stream_connect)(pa_stream *, const char *, const pa_buffer_attr *, int, const pa_cvolume *, pa_stream *);
    int (*stream_state)(const pa_stream *);
    int (*begin_write)(pa_stream *, void **, size_t *);
    int (*stream_write)(pa_stream *, const void *, size_t, pa_free_cb_t, int64_t, int);
    void (*set_state_callback)(pa_stream *, pa_stream_notify_cb_t, void *);
    void (*set_write_callback)(pa_stream *, pa_stream_request_cb_t, void *);
    pa_operation *(*cork)(pa_stream *, int, pa_stream_success_cb_t, void *);
    pa_operation *(*flush)(pa_stream *, pa_stream_success_cb_t, void *);
    int (*stream_disconnect)(pa_stream *);
    void (*stream_unref)(pa_stream *);
    int (*operation_state)(const pa_operation *);
    void (*operation_unref)(pa_operation *);
};

static int state_callbacks;
static int context_callbacks;
static int request_callbacks;
static int success_callbacks;
static int free_callbacks;

static void fail(const char *message)
{
    fprintf(stderr, "trace-reducer: %s errno=%d dlerror=%s\n", message,
            errno, dlerror());
    exit(2);
}

static void *required_symbol(void *handle, const char *name)
{
    void *symbol;
    const char *error;
    dlerror();
    symbol = dlsym(handle, name);
    error = dlerror();
    if (!symbol || error)
        fail(name);
    return symbol;
}

#define LOAD(api, member, handle, name, type) do { \
    (api).member = (type)required_symbol((handle), (name)); \
} while (0)

static void state_callback(pa_stream *stream, void *userdata)
{
    (void)stream;
    if (userdata != (void *)(uintptr_t)0x1111)
        fail("state callback userdata");
    state_callbacks++;
}

static void context_callback(pa_context *context, void *userdata)
{
    (void)context;
    if (userdata != (void *)(uintptr_t)0x4444)
        fail("context callback userdata");
    context_callbacks++;
}

static void request_callback(pa_stream *stream, size_t bytes, void *userdata)
{
    (void)stream;
    if (bytes != 4096 || userdata != (void *)(uintptr_t)0x2222)
        fail("request callback payload");
    request_callbacks++;
}

static void success_callback(pa_stream *stream, int success, void *userdata)
{
    (void)stream;
    if (success != 1 || userdata != (void *)(uintptr_t)0x3333)
        fail("success callback payload");
    success_callbacks++;
}

static void free_callback(void *data)
{
    if (!data)
        fail("free callback data");
    free_callbacks++;
}

static void complete_operation(struct pulse_api *api, pa_operation *operation,
                               int allow_null)
{
    if (!operation) {
        if (!allow_null)
            fail("unexpected null operation");
        return;
    }
    if (api->operation_state(operation) != 1)
        fail("operation state");
    api->operation_unref(operation);
}

struct thread_arg {
    struct pulse_api *api;
    pa_context *context;
    int failed;
};

static void *thread_worker(void *opaque)
{
    struct thread_arg *arg = opaque;
    int index;
    for (index = 0; index < 128; index++)
        if (arg->api->context_state(arg->context) != 4)
            arg->failed = 1;
    return NULL;
}

static void load_api(struct pulse_api *api, void *handle)
{
    memset(api, 0, sizeof(*api));
    LOAD(*api, context_new, handle, "pa_context_new", pa_context *(*)(void *, const char *));
    LOAD(*api, context_connect, handle, "pa_context_connect", int (*)(pa_context *, const char *, int, const void *));
    LOAD(*api, context_state, handle, "pa_context_get_state", int (*)(const pa_context *));
    LOAD(*api, context_errno, handle, "pa_context_errno", int (*)(const pa_context *));
    LOAD(*api, set_context_state_callback, handle, "pa_context_set_state_callback", void (*)(pa_context *, pa_context_notify_cb_t, void *));
    LOAD(*api, context_disconnect, handle, "pa_context_disconnect", void (*)(pa_context *));
    LOAD(*api, context_unref, handle, "pa_context_unref", void (*)(pa_context *));
    LOAD(*api, stream_new, handle, "pa_stream_new", pa_stream *(*)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *));
    LOAD(*api, stream_new_with_proplist, handle, "pa_stream_new_with_proplist", pa_stream *(*)(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *, pa_proplist *));
    LOAD(*api, stream_connect, handle, "pa_stream_connect_playback", int (*)(pa_stream *, const char *, const pa_buffer_attr *, int, const pa_cvolume *, pa_stream *));
    LOAD(*api, stream_state, handle, "pa_stream_get_state", int (*)(const pa_stream *));
    LOAD(*api, begin_write, handle, "pa_stream_begin_write", int (*)(pa_stream *, void **, size_t *));
    LOAD(*api, stream_write, handle, "pa_stream_write", int (*)(pa_stream *, const void *, size_t, pa_free_cb_t, int64_t, int));
    LOAD(*api, set_state_callback, handle, "pa_stream_set_state_callback", void (*)(pa_stream *, pa_stream_notify_cb_t, void *));
    LOAD(*api, set_write_callback, handle, "pa_stream_set_write_callback", void (*)(pa_stream *, pa_stream_request_cb_t, void *));
    LOAD(*api, cork, handle, "pa_stream_cork", pa_operation *(*)(pa_stream *, int, pa_stream_success_cb_t, void *));
    LOAD(*api, flush, handle, "pa_stream_flush", pa_operation *(*)(pa_stream *, pa_stream_success_cb_t, void *));
    LOAD(*api, stream_disconnect, handle, "pa_stream_disconnect", int (*)(pa_stream *));
    LOAD(*api, stream_unref, handle, "pa_stream_unref", void (*)(pa_stream *));
    LOAD(*api, operation_state, handle, "pa_operation_get_state", int (*)(const pa_operation *));
    LOAD(*api, operation_unref, handle, "pa_operation_unref", void (*)(pa_operation *));
}

static int run_two_provider_test(const char *first_path,
                                 const char *second_path)
{
    void *first = dlopen(first_path, RTLD_NOW | RTLD_LOCAL);
    void *second;
    pa_context *(*first_new)(void *, const char *);
    pa_context *(*second_new)(void *, const char *);
    int (*first_state)(const pa_context *);
    int (*second_state)(const pa_context *);
    void (*first_unref)(pa_context *);
    void (*second_unref)(pa_context *);
    pa_context *first_context;
    pa_context *second_context;
    int first_before;
    int first_after;
    int second_value;

    if (!first)
        fail("first provider dlopen");
    first_new = (pa_context *(*)(void *, const char *))
        required_symbol(first, "pa_context_new");
    first_state = (int (*)(const pa_context *))
        required_symbol(first, "pa_context_get_state");
    first_unref = (void (*)(pa_context *))
        required_symbol(first, "pa_context_unref");
    first_context = first_new(NULL, "ProviderA");
    first_before = first_state(first_context);

    second = dlopen(second_path, RTLD_NOW | RTLD_LOCAL);
    if (!second)
        fail("second provider dlopen");
    second_new = (pa_context *(*)(void *, const char *))
        required_symbol(second, "pa_context_new");
    second_state = (int (*)(const pa_context *))
        required_symbol(second, "pa_context_get_state");
    second_unref = (void (*)(pa_context *))
        required_symbol(second, "pa_context_unref");
    second_context = second_new(NULL, "ProviderB");
    second_value = second_state(second_context);
    first_after = first_state(first_context);
    if (first_before != 4 || first_after != 4 || second_value != 104 ||
        first_state == second_state)
        fail("provider handle retarget");
    first_unref(first_context);
    second_unref(second_context);
    if (dlclose(second) != 0 || dlclose(first) != 0)
        fail("two provider dlclose");
    printf("CHROMIUM-PULSE-TRACE-TWO-PROVIDER-PASS first_before=%d first_after=%d second=%d wrappers=distinct\n",
           first_before, first_after, second_value);
    return 0;
}

static void check_dlsym_contract(void *handle)
{
    int (*untargeted)(void);
    void *missing;
    const char *missing_error;
    const char *post_success_error;

    dlerror();
    missing = dlsym(handle, "pa_symbol_that_does_not_exist");
    missing_error = dlerror();
    if (missing || !missing_error)
        fail("missing-symbol dlerror");
    dlerror();
    untargeted = (int (*)(void))dlsym(handle, "fake_untargeted");
    post_success_error = dlerror();
    if (!untargeted || post_success_error || untargeted() != 77)
        fail("untargeted symbol");
    printf("TRACE-DLERROR missing=nonnull success=null untargeted=77\n");
}

static int run_session(struct pulse_api *api, int hot_calls, int allow_late_null)
{
    const pa_sample_spec spec = { 5, 48000, 2 };
    const pa_channel_map map = { 2, { 1, 2 } };
    const pa_buffer_attr requested = {
        UINT32_MAX, 12288U, UINT32_MAX, 2048U, UINT32_MAX
    };
    int empty_proplist;
    pa_context *context;
    pa_stream *stream;
    pa_operation *operation;
    void *buffer = (void *)(uintptr_t)0xfeedfaceU;
    size_t bytes = (size_t)0xdeadbeefU;
    int result;
    int index;

    errno = 0;
    context = api->context_new(NULL, "Chromium");
    if (!context || errno != EAGAIN)
        fail("context_new errno");
    api->set_context_state_callback(context, context_callback,
                                    (void *)(uintptr_t)0x4444);
    if (errno != EISCONN)
        fail("context setter errno");
    if (api->context_connect(context, NULL, 1, NULL) != 0 || errno != EALREADY)
        fail("context_connect errno");
    if (api->context_state(context) != 4 || errno != ENOTTY)
        fail("context_state errno");
    if (api->context_errno(context) != 0 || errno != ENOMSG)
        fail("context_errno errno");
    stream = api->stream_new_with_proplist(context, "Playback", &spec, &map,
                                            (pa_proplist *)&empty_proplist);
    if (!stream || errno != ECHRNG)
        fail("stream_new errno");
    api->set_state_callback(stream, state_callback, (void *)(uintptr_t)0x1111);
    if (errno != ESTALE)
        fail("state setter errno");
    api->set_write_callback(stream, request_callback, (void *)(uintptr_t)0x2222);
    if (errno != EREMOTEIO)
        fail("write setter errno");
    if (api->stream_connect(stream, NULL, &requested, 0x200f, NULL, NULL) != 0 ||
        errno != ECOMM)
        fail("stream_connect errno");
    if (api->stream_state(stream) != 2 || errno != EREMOTE)
        fail("stream_state errno");

    operation = api->cork(stream, 0, success_callback,
                          (void *)(uintptr_t)0x3333);
    complete_operation(api, operation, 0);

    result = api->begin_write(stream, &buffer, &bytes);
    if (getenv("XV6_PULSE_TRACE_FAKE_BEGIN_FAIL")) {
        if (result != -1 || errno != EIO ||
            buffer != (void *)(uintptr_t)0xfeedfaceU ||
            bytes != (size_t)0xdeadbeefU)
            fail("failed begin poison");
    } else {
        if (result != 0 || !buffer || bytes < 4096 || errno != ENODATA)
            fail("begin write");
        if (api->stream_write(stream, buffer, 4096, free_callback, 0, 0) != 0 ||
            errno != ENOSR)
            fail("stream write");
    }

    for (index = 0; index < hot_calls; index++)
        if (api->context_state(context) != 4)
            fail("hot context state");

    operation = api->flush(stream, success_callback,
                           (void *)(uintptr_t)0x3333);
    complete_operation(api, operation, 0);
    operation = api->cork(stream, 1, success_callback,
                          (void *)(uintptr_t)0x3333);
    complete_operation(api, operation, 0);
    operation = api->flush(stream, success_callback,
                           (void *)(uintptr_t)0x3333);
    complete_operation(api, operation, allow_late_null);

    api->set_write_callback(stream, NULL, NULL);
    api->set_state_callback(stream, NULL, NULL);
    if (api->stream_disconnect(stream) != 0 || errno != ESHUTDOWN)
        fail("stream_disconnect errno");
    api->stream_unref(stream);
    if (errno != ECANCELED)
        fail("stream_unref errno");
    api->set_context_state_callback(context, NULL, NULL);
    if (errno != EISCONN)
        fail("context clear errno");
    api->context_disconnect(context);
    if (errno != ENOLINK)
        fail("context_disconnect errno");
    api->context_unref(context);
    if (errno != EOWNERDEAD)
        fail("context_unref errno");
    return 0;
}

int main(int argc, char **argv)
{
    struct pulse_api api;
    void *library;
    pthread_t threads[64];
    struct thread_arg thread_args[64];
    pa_context *thread_context;
    int index;

    if (argc == 3)
        return run_two_provider_test(argv[1], argv[2]);
    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/libpulse.so.0 [/second/libpulse.so.0]\n", argv[0]);
        return 2;
    }
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
        fail("dlopen");
    check_dlsym_contract(library);
    load_api(&api, library);

    if (getenv("XV6_PULSE_TRACE_TEST_THREADS")) {
        thread_context = api.context_new(NULL, "Threaded");
        if (!thread_context)
            fail("thread context");
        for (index = 0; index < 64; index++) {
            thread_args[index].api = &api;
            thread_args[index].context = thread_context;
            thread_args[index].failed = 0;
            if (pthread_create(&threads[index], NULL, thread_worker,
                               &thread_args[index]) != 0)
                fail("pthread_create");
        }
        for (index = 0; index < 64; index++) {
            if (pthread_join(threads[index], NULL) != 0 ||
                thread_args[index].failed)
                fail("pthread_join");
        }
        api.context_unref(thread_context);
    }

    if (getenv("XV6_PULSE_TRACE_TEST_FORK")) {
        pid_t child = fork();
        int status;
        if (child < 0)
            fail("fork");
        if (child == 0) {
            pa_context *context = api.context_new(NULL, "ForkChild");
            if (!context || api.context_state(context) != 4)
                _exit(9);
            api.context_unref(context);
            _exit(0);
        }
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0)
            fail("fork child");
    }

    run_session(&api,
        getenv("XV6_PULSE_TRACE_TEST_HOT") ? 100001 : 0,
        getenv("XV6_PULSE_TRACE_FAKE_LATE_NULL") != NULL);
    if (getenv("XV6_PULSE_TRACE_TEST_REUSE"))
        run_session(&api, 0,
                    getenv("XV6_PULSE_TRACE_FAKE_LATE_NULL") != NULL);

    if (getenv("XV6_PULSE_TRACE_TEST_GET_ATTR")) {
        const pa_buffer_attr *(*get_attr)(const pa_stream *);
        get_attr = (const pa_buffer_attr *(*)(const pa_stream *))
            required_symbol(library, "pa_stream_get_buffer_attr");
        if (!get_attr(NULL))
            fail("effective attr");
    }
    if (context_callbacks < 1 || state_callbacks < 1 || request_callbacks < 1 || success_callbacks < 3 ||
        (!getenv("XV6_PULSE_TRACE_FAKE_BEGIN_FAIL") && free_callbacks < 1))
        fail("callback counts");
    if (dlclose(library) != 0)
        fail("dlclose");
    printf("CHROMIUM-PULSE-TRACE-REDUCER-PASS context_cb=%d state_cb=%d request_cb=%d success_cb=%d free_cb=%d\n",
           context_callbacks, state_callbacks, request_callbacks, success_callbacks,
           free_callbacks);
    return 0;
}
