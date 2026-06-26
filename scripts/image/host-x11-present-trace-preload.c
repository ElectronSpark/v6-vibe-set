#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC 35
#endif

#ifndef XCB_PRESENT_COMPLETE_NOTIFY
#define XCB_PRESENT_COMPLETE_NOTIFY 1
#endif

#ifndef XCB_PRESENT_COMPLETE_MODE_COPY
#define XCB_PRESENT_COMPLETE_MODE_COPY 0
#endif

#ifndef XCB_PRESENT_COMPLETE_MODE_FLIP
#define XCB_PRESENT_COMPLETE_MODE_FLIP 1
#endif

#ifndef XCB_PRESENT_COMPLETE_MODE_SKIP
#define XCB_PRESENT_COMPLETE_MODE_SKIP 2
#endif

#ifndef XCB_PRESENT_COMPLETE_MODE_SUBOPTIMAL_COPY
#define XCB_PRESENT_COMPLETE_MODE_SUBOPTIMAL_COPY 3
#endif

typedef uint32_t xcb_present_event_t;
typedef uint32_t xcb_xfixes_region_t;
typedef uint32_t xcb_randr_crtc_t;
typedef uint32_t xcb_sync_fence_t;
typedef struct xcb_present_notify_t xcb_present_notify_t;

typedef struct {
    uint8_t response_type;
    uint8_t extension;
    uint16_t sequence;
    uint32_t length;
    uint16_t event_type;
    uint8_t kind;
    uint8_t mode;
    xcb_present_event_t event;
    xcb_window_t window;
    uint32_t serial;
    uint64_t ust;
    uint64_t msc;
} xcb_present_complete_notify_event_t;

#define MAX_PRESENT_SPECIAL_EVENTS 64

struct present_trace_stats {
    uint64_t pixmap_calls;
    uint64_t pixmap_checked_calls;
    uint64_t register_special_xge_calls;
    uint64_t present_special_registrations;
    uint64_t wait_special_calls;
    uint64_t poll_special_calls;
    uint64_t nonpresent_special_events;
    uint64_t pixmap_total_ns;
    uint64_t wait_special_total_ns;
    uint64_t poll_special_total_ns;
    uint64_t complete_events;
    uint64_t complete_copy;
    uint64_t complete_flip;
    uint64_t complete_skip;
    uint64_t complete_suboptimal_copy;
    uint64_t complete_decode_failures;
    uint64_t present_fallback_symbols;
    uint64_t missing_symbols;
    uint32_t last_serial;
    uint64_t last_msc;
};

static struct present_trace_stats stats;
static int log_fd = -1;
static void *present_pixmap_sym;
static void *present_pixmap_checked_sym;
static void *wait_special_sym;
static void *poll_special_sym;
static void *register_special_xge_sym;
static void *present_id_sym;
static void *present_lib_handle;
static int present_pixmap_resolved;
static int present_pixmap_checked_resolved;
static int wait_special_resolved;
static int poll_special_resolved;
static int register_special_xge_resolved;
static int present_id_resolved;
static int present_id_missing_recorded;
static int present_lib_handle_resolved;
static xcb_special_event_t *present_special_events[MAX_PRESENT_SPECIAL_EVENTS];
static volatile int present_special_events_lock;

typedef xcb_void_cookie_t (*present_pixmap_fn)(
    xcb_connection_t *, xcb_window_t, xcb_pixmap_t, uint32_t, xcb_xfixes_region_t,
    xcb_xfixes_region_t, int16_t, int16_t, xcb_randr_crtc_t, xcb_sync_fence_t,
    xcb_sync_fence_t, uint32_t, uint64_t, uint64_t, uint64_t, uint32_t,
    const xcb_present_notify_t *);
typedef xcb_void_cookie_t (*present_pixmap_checked_fn)(
    xcb_connection_t *, xcb_window_t, xcb_pixmap_t, uint32_t, xcb_xfixes_region_t,
    xcb_xfixes_region_t, int16_t, int16_t, xcb_randr_crtc_t, xcb_sync_fence_t,
    xcb_sync_fence_t, uint32_t, uint64_t, uint64_t, uint64_t, uint32_t,
    const xcb_present_notify_t *);
typedef xcb_generic_event_t *(*wait_special_fn)(xcb_connection_t *,
                                                xcb_special_event_t *);
typedef xcb_generic_event_t *(*poll_special_fn)(xcb_connection_t *,
                                                xcb_special_event_t *);
typedef xcb_special_event_t *(*register_special_xge_fn)(xcb_connection_t *,
                                                        xcb_extension_t *,
                                                        uint32_t, uint32_t *);

static uint64_t now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t elapsed_ns(uint64_t start, uint64_t end)
{
    if (start == 0 || end == 0 || end < start)
        return 0;
    return end - start;
}

static void add_u64(uint64_t *ptr, uint64_t value)
{
    __atomic_fetch_add(ptr, value, __ATOMIC_RELAXED);
}

static void store_u32(uint32_t *ptr, uint32_t value)
{
    __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
}

static void store_u64(uint64_t *ptr, uint64_t value)
{
    __atomic_store_n(ptr, value, __ATOMIC_RELAXED);
}

static uint64_t load_u64(uint64_t *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

static uint32_t load_u32(uint32_t *ptr)
{
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}

static int should_log_progress(uint64_t call_count)
{
    if (call_count <= 4)
        return 1;
    if ((call_count & (call_count - 1)) == 0)
        return 1;
    return (call_count % 64) == 0;
}

static void append_bytes(char *buf, size_t size, size_t *pos, const char *text)
{
    while (*text && *pos + 1 < size)
        buf[(*pos)++] = *text++;
}

static void append_u64(char *buf, size_t size, size_t *pos, uint64_t value)
{
    char tmp[32];
    size_t len = 0;

    do {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value && len < sizeof(tmp));
    while (len > 0 && *pos + 1 < size)
        buf[(*pos)++] = tmp[--len];
}

static void trace_progress(const char *where, const char *event)
{
    char buf[1024];
    size_t pos = 0;
    ssize_t ignored;

    append_bytes(buf, sizeof(buf), &pos,
                 "host-x11-present-trace: "
                 "phase=present_trace_progress pid=");
    append_u64(buf, sizeof(buf), &pos, (uint64_t)getpid());
    append_bytes(buf, sizeof(buf), &pos, " where=");
    append_bytes(buf, sizeof(buf), &pos, where);
    append_bytes(buf, sizeof(buf), &pos, " event=");
    append_bytes(buf, sizeof(buf), &pos, event);
    append_bytes(buf, sizeof(buf), &pos, " pixmap_calls=");
    append_u64(buf, sizeof(buf), &pos, load_u64(&stats.pixmap_calls));
    append_bytes(buf, sizeof(buf), &pos, " pixmap_checked_calls=");
    append_u64(buf, sizeof(buf), &pos,
               load_u64(&stats.pixmap_checked_calls));
    append_bytes(buf, sizeof(buf), &pos, " register_special_xge_calls=");
    append_u64(buf, sizeof(buf), &pos,
               load_u64(&stats.register_special_xge_calls));
    append_bytes(buf, sizeof(buf), &pos, " present_special_registrations=");
    append_u64(buf, sizeof(buf), &pos,
               load_u64(&stats.present_special_registrations));
    append_bytes(buf, sizeof(buf), &pos, " wait_special_calls=");
    append_u64(buf, sizeof(buf), &pos, load_u64(&stats.wait_special_calls));
    append_bytes(buf, sizeof(buf), &pos, " poll_special_calls=");
    append_u64(buf, sizeof(buf), &pos, load_u64(&stats.poll_special_calls));
    append_bytes(buf, sizeof(buf), &pos, " complete_events=");
    append_u64(buf, sizeof(buf), &pos, load_u64(&stats.complete_events));
    append_bytes(buf, sizeof(buf), &pos, " complete_decode_failures=");
    append_u64(buf, sizeof(buf), &pos,
               load_u64(&stats.complete_decode_failures));
    append_bytes(buf, sizeof(buf), &pos, " present_fallback_symbols=");
    append_u64(buf, sizeof(buf), &pos,
               load_u64(&stats.present_fallback_symbols));
    append_bytes(buf, sizeof(buf), &pos, " missing_symbols=");
    append_u64(buf, sizeof(buf), &pos, load_u64(&stats.missing_symbols));
    append_bytes(buf, sizeof(buf), &pos, " last_serial=");
    append_u64(buf, sizeof(buf), &pos, load_u32(&stats.last_serial));
    append_bytes(buf, sizeof(buf), &pos, " last_msc=");
    append_u64(buf, sizeof(buf), &pos, load_u64(&stats.last_msc));
    append_bytes(buf, sizeof(buf), &pos, "\n");

    if (pos == 0)
        return;
    if (log_fd >= 0)
        ignored = write(log_fd, buf, pos);
    else
        ignored = write(STDERR_FILENO, buf, pos);
    (void)ignored;
}

static void trace_vlogf(const char *fmt, va_list ap)
{
    char buf[1024];
    int len;

    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len <= 0)
        return;
    if ((size_t)len >= sizeof(buf))
        len = (int)sizeof(buf) - 1;
    if (log_fd >= 0) {
        ssize_t ignored = write(log_fd, buf, (size_t)len);
        (void)ignored;
    } else {
        ssize_t ignored = write(STDERR_FILENO, buf, (size_t)len);
        (void)ignored;
    }
}

static void trace_logf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    trace_vlogf(fmt, ap);
    va_end(ap);
}

static void *resolve_symbol(const char *name, void **slot, int *resolved)
{
    if (__atomic_load_n(resolved, __ATOMIC_ACQUIRE))
        return __atomic_load_n(slot, __ATOMIC_RELAXED);

    dlerror();
    *slot = dlsym(RTLD_NEXT, name);
    if (!*slot)
        add_u64(&stats.missing_symbols, 1);
    __atomic_store_n(resolved, 1, __ATOMIC_RELEASE);
    return *slot;
}

static void *resolve_present_library_handle(void)
{
    void *handle;

    if (__atomic_load_n(&present_lib_handle_resolved, __ATOMIC_ACQUIRE))
        return __atomic_load_n(&present_lib_handle, __ATOMIC_RELAXED);

    dlerror();
    handle = dlopen("libxcb-present.so.0", RTLD_LAZY | RTLD_LOCAL);
    __atomic_store_n(&present_lib_handle, handle, __ATOMIC_RELAXED);
    __atomic_store_n(&present_lib_handle_resolved, 1, __ATOMIC_RELEASE);
    return handle;
}

static void *resolve_present_symbol(const char *name, void **slot,
                                    int *resolved, int record_missing)
{
    void *symbol;
    void *handle;

    if (__atomic_load_n(resolved, __ATOMIC_ACQUIRE))
        return __atomic_load_n(slot, __ATOMIC_RELAXED);

    dlerror();
    symbol = dlsym(RTLD_NEXT, name);
    if (!symbol) {
        handle = resolve_present_library_handle();
        if (handle) {
            dlerror();
            symbol = dlsym(handle, name);
            if (symbol)
                add_u64(&stats.present_fallback_symbols, 1);
        }
    }
    if (!symbol && record_missing)
        add_u64(&stats.missing_symbols, 1);
    __atomic_store_n(slot, symbol, __ATOMIC_RELAXED);
    __atomic_store_n(resolved, 1, __ATOMIC_RELEASE);
    return symbol;
}

static void *resolve_present_id_symbol(void)
{
    return resolve_present_symbol("xcb_present_id", &present_id_sym,
                                  &present_id_resolved, 0);
}

static void record_present_id_missing(void)
{
    int expected = 0;

    if (__atomic_load_n(&present_id_resolved, __ATOMIC_ACQUIRE) &&
        __atomic_load_n(&present_id_sym, __ATOMIC_RELAXED))
        return;
    if (__atomic_compare_exchange_n(&present_id_missing_recorded, &expected, 1,
                                    0, __ATOMIC_ACQ_REL,
                                    __ATOMIC_RELAXED))
        add_u64(&stats.missing_symbols, 1);
}

static void present_special_events_lock_acquire(void)
{
    while (__sync_lock_test_and_set(&present_special_events_lock, 1))
        ;
}

static void present_special_events_lock_release(void)
{
    __sync_lock_release(&present_special_events_lock);
}

static void track_present_special_event(xcb_special_event_t *se)
{
    size_t i;

    if (!se)
        return;

    present_special_events_lock_acquire();
    for (i = 0; i < MAX_PRESENT_SPECIAL_EVENTS; i++) {
        if (present_special_events[i] == se) {
            present_special_events_lock_release();
            return;
        }
    }
    for (i = 0; i < MAX_PRESENT_SPECIAL_EVENTS; i++) {
        if (!present_special_events[i]) {
            present_special_events[i] = se;
            add_u64(&stats.present_special_registrations, 1);
            present_special_events_lock_release();
            return;
        }
    }
    present_special_events_lock_release();
    add_u64(&stats.complete_decode_failures, 1);
}

static int is_present_special_event(xcb_special_event_t *se)
{
    size_t i;
    int found = 0;

    if (!se)
        return 0;

    present_special_events_lock_acquire();
    for (i = 0; i < MAX_PRESENT_SPECIAL_EVENTS; i++) {
        if (present_special_events[i] == se) {
            found = 1;
            break;
        }
    }
    present_special_events_lock_release();
    return found;
}

static void decode_complete_event(xcb_special_event_t *se,
                                  const xcb_generic_event_t *event)
{
    const xcb_present_complete_notify_event_t *complete;
    uint8_t response_type;
    uint32_t bytes;

    if (!event)
        return;
    if (!is_present_special_event(se)) {
        add_u64(&stats.nonpresent_special_events, 1);
        return;
    }

    response_type = event->response_type & 0x7f;
    if (response_type != XCB_GE_GENERIC) {
        add_u64(&stats.complete_decode_failures, 1);
        return;
    }

    complete = (const xcb_present_complete_notify_event_t *)event;
    bytes = 32u + complete->length * 4u;
    if (bytes < sizeof(*complete)) {
        add_u64(&stats.complete_decode_failures, 1);
        return;
    }
    if (complete->event_type != XCB_PRESENT_COMPLETE_NOTIFY)
        return;

    add_u64(&stats.complete_events, 1);
    switch (complete->mode) {
    case XCB_PRESENT_COMPLETE_MODE_COPY:
        add_u64(&stats.complete_copy, 1);
        break;
    case XCB_PRESENT_COMPLETE_MODE_FLIP:
        add_u64(&stats.complete_flip, 1);
        break;
    case XCB_PRESENT_COMPLETE_MODE_SKIP:
        add_u64(&stats.complete_skip, 1);
        break;
    case XCB_PRESENT_COMPLETE_MODE_SUBOPTIMAL_COPY:
        add_u64(&stats.complete_suboptimal_copy, 1);
        break;
    default:
        add_u64(&stats.complete_decode_failures, 1);
        break;
    }
    store_u32(&stats.last_serial, complete->serial);
    store_u64(&stats.last_msc, complete->msc);
}

__attribute__((constructor)) static void present_trace_begin(void)
{
    const char *path = getenv("HOST_X11_EGL_SMOKE_LOG");
    char exe[256];
    ssize_t exe_len;

    if (path && path[0])
        log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    exe_len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (exe_len > 0) {
        exe[exe_len] = '\0';
    } else {
        snprintf(exe, sizeof(exe), "(unknown)");
    }
    trace_logf("host-x11-present-trace: phase=present_trace status=BEGIN pid=%ld exe=%s\n",
               (long)getpid(), exe);
}

__attribute__((destructor)) static void present_trace_end(void)
{
    if ((load_u64(&stats.register_special_xge_calls) > 0 ||
         load_u64(&stats.pixmap_calls) + load_u64(&stats.pixmap_checked_calls) >
             0) &&
        load_u64(&stats.present_special_registrations) == 0 &&
        !resolve_present_id_symbol())
        record_present_id_missing();

    trace_logf(
        "host-x11-present-trace: phase=present_trace_result status=PASS "
        "pixmap_calls=%" PRIu64 " pixmap_checked_calls=%" PRIu64 " "
        "register_special_xge_calls=%" PRIu64 " "
        "present_special_registrations=%" PRIu64 " "
        "wait_special_calls=%" PRIu64 " poll_special_calls=%" PRIu64 " "
        "nonpresent_special_events=%" PRIu64 " "
        "pixmap_total_ms=%.3f wait_special_total_ms=%.3f "
        "poll_special_total_ms=%.3f complete_events=%" PRIu64 " "
        "complete_copy=%" PRIu64 " complete_flip=%" PRIu64 " "
        "complete_skip=%" PRIu64 " complete_suboptimal_copy=%" PRIu64 " "
        "complete_decode_failures=%" PRIu64 " "
        "present_fallback_symbols=%" PRIu64 " "
        "missing_symbols=%" PRIu64 " "
        "last_serial=%" PRIu32 " last_msc=%" PRIu64 " pid=%ld\n",
        load_u64(&stats.pixmap_calls),
        load_u64(&stats.pixmap_checked_calls),
        load_u64(&stats.register_special_xge_calls),
        load_u64(&stats.present_special_registrations),
        load_u64(&stats.wait_special_calls),
        load_u64(&stats.poll_special_calls),
        load_u64(&stats.nonpresent_special_events),
        (double)load_u64(&stats.pixmap_total_ns) / 1000000.0,
        (double)load_u64(&stats.wait_special_total_ns) / 1000000.0,
        (double)load_u64(&stats.poll_special_total_ns) / 1000000.0,
        load_u64(&stats.complete_events),
        load_u64(&stats.complete_copy),
        load_u64(&stats.complete_flip),
        load_u64(&stats.complete_skip),
        load_u64(&stats.complete_suboptimal_copy),
        load_u64(&stats.complete_decode_failures),
        load_u64(&stats.present_fallback_symbols),
        load_u64(&stats.missing_symbols),
        load_u32(&stats.last_serial),
        load_u64(&stats.last_msc),
        (long)getpid());
    if (log_fd >= 0)
        close(log_fd);
}

xcb_special_event_t *xcb_register_for_special_xge(
    xcb_connection_t *c, xcb_extension_t *ext, uint32_t eid, uint32_t *stamp)
{
    register_special_xge_fn real_fn;
    xcb_special_event_t *se;
    xcb_extension_t *present_id;
    uint64_t call_count;

    call_count = __atomic_add_fetch(&stats.register_special_xge_calls, 1,
                                    __ATOMIC_RELAXED);
    if (should_log_progress(call_count))
        trace_progress("xcb_register_for_special_xge", "enter");
    real_fn = (register_special_xge_fn)resolve_symbol(
        "xcb_register_for_special_xge", &register_special_xge_sym,
        &register_special_xge_resolved);
    if (!real_fn) {
        if (should_log_progress(call_count))
            trace_progress("xcb_register_for_special_xge", "return");
        return NULL;
    }

    se = real_fn(c, ext, eid, stamp);
    present_id = (xcb_extension_t *)resolve_present_id_symbol();
    if (present_id && ext == present_id)
        track_present_special_event(se);
    if (should_log_progress(call_count))
        trace_progress("xcb_register_for_special_xge", "return");
    return se;
}

xcb_void_cookie_t xcb_present_pixmap(
    xcb_connection_t *c, xcb_window_t window, xcb_pixmap_t pixmap,
    uint32_t serial, xcb_xfixes_region_t valid, xcb_xfixes_region_t update,
    int16_t x_off, int16_t y_off, xcb_randr_crtc_t target_crtc,
    xcb_sync_fence_t wait_fence, xcb_sync_fence_t idle_fence,
    uint32_t options, uint64_t target_msc, uint64_t divisor,
    uint64_t remainder, uint32_t notifies_len,
    const xcb_present_notify_t *notifies)
{
    present_pixmap_fn real_fn;
    xcb_void_cookie_t cookie = { 0 };
    uint64_t start = now_ns();
    uint64_t call_count;

    call_count =
        __atomic_add_fetch(&stats.pixmap_calls, 1, __ATOMIC_RELAXED);
    store_u32(&stats.last_serial, serial);
    if (should_log_progress(call_count))
        trace_progress("xcb_present_pixmap", "enter");
    real_fn = (present_pixmap_fn)resolve_present_symbol(
        "xcb_present_pixmap", &present_pixmap_sym,
        &present_pixmap_resolved, 1);
    if (!real_fn) {
        if (should_log_progress(call_count))
            trace_progress("xcb_present_pixmap", "return");
        return cookie;
    }
    cookie = real_fn(c, window, pixmap, serial, valid, update, x_off, y_off,
                     target_crtc, wait_fence, idle_fence, options, target_msc,
                     divisor, remainder, notifies_len, notifies);
    add_u64(&stats.pixmap_total_ns, elapsed_ns(start, now_ns()));
    if (should_log_progress(call_count))
        trace_progress("xcb_present_pixmap", "return");
    return cookie;
}

xcb_void_cookie_t xcb_present_pixmap_checked(
    xcb_connection_t *c, xcb_window_t window, xcb_pixmap_t pixmap,
    uint32_t serial, xcb_xfixes_region_t valid, xcb_xfixes_region_t update,
    int16_t x_off, int16_t y_off, xcb_randr_crtc_t target_crtc,
    xcb_sync_fence_t wait_fence, xcb_sync_fence_t idle_fence,
    uint32_t options, uint64_t target_msc, uint64_t divisor,
    uint64_t remainder, uint32_t notifies_len,
    const xcb_present_notify_t *notifies)
{
    present_pixmap_checked_fn real_fn;
    xcb_void_cookie_t cookie = { 0 };
    uint64_t start = now_ns();
    uint64_t call_count;

    call_count = __atomic_add_fetch(&stats.pixmap_checked_calls, 1,
                                    __ATOMIC_RELAXED);
    store_u32(&stats.last_serial, serial);
    if (should_log_progress(call_count))
        trace_progress("xcb_present_pixmap_checked", "enter");
    real_fn = (present_pixmap_checked_fn)resolve_present_symbol(
        "xcb_present_pixmap_checked", &present_pixmap_checked_sym,
        &present_pixmap_checked_resolved, 1);
    if (!real_fn) {
        if (should_log_progress(call_count))
            trace_progress("xcb_present_pixmap_checked", "return");
        return cookie;
    }
    cookie = real_fn(c, window, pixmap, serial, valid, update, x_off, y_off,
                     target_crtc, wait_fence, idle_fence, options, target_msc,
                     divisor, remainder, notifies_len, notifies);
    add_u64(&stats.pixmap_total_ns, elapsed_ns(start, now_ns()));
    if (should_log_progress(call_count))
        trace_progress("xcb_present_pixmap_checked", "return");
    return cookie;
}

xcb_generic_event_t *xcb_wait_for_special_event(xcb_connection_t *c,
                                                xcb_special_event_t *se)
{
    wait_special_fn real_fn;
    xcb_generic_event_t *event;
    uint64_t start = now_ns();
    uint64_t call_count;

    call_count =
        __atomic_add_fetch(&stats.wait_special_calls, 1, __ATOMIC_RELAXED);
    if (should_log_progress(call_count))
        trace_progress("xcb_wait_for_special_event", "enter");
    real_fn = (wait_special_fn)resolve_symbol(
        "xcb_wait_for_special_event", &wait_special_sym,
        &wait_special_resolved);
    if (!real_fn) {
        if (should_log_progress(call_count))
            trace_progress("xcb_wait_for_special_event", "return");
        return NULL;
    }
    event = real_fn(c, se);
    add_u64(&stats.wait_special_total_ns, elapsed_ns(start, now_ns()));
    decode_complete_event(se, event);
    if (should_log_progress(call_count))
        trace_progress("xcb_wait_for_special_event", "return");
    return event;
}

xcb_generic_event_t *xcb_poll_for_special_event(xcb_connection_t *c,
                                                xcb_special_event_t *se)
{
    poll_special_fn real_fn;
    xcb_generic_event_t *event;
    uint64_t start = now_ns();
    uint64_t call_count;

    call_count =
        __atomic_add_fetch(&stats.poll_special_calls, 1, __ATOMIC_RELAXED);
    if (should_log_progress(call_count))
        trace_progress("xcb_poll_for_special_event", "enter");
    real_fn = (poll_special_fn)resolve_symbol(
        "xcb_poll_for_special_event", &poll_special_sym,
        &poll_special_resolved);
    if (!real_fn) {
        if (should_log_progress(call_count))
            trace_progress("xcb_poll_for_special_event", "return");
        return NULL;
    }
    event = real_fn(c, se);
    add_u64(&stats.poll_special_total_ns, elapsed_ns(start, now_ns()));
    decode_complete_event(se, event);
    if (should_log_progress(call_count))
        trace_progress("xcb_poll_for_special_event", "return");
    return event;
}
