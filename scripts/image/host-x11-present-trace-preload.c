#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
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
typedef struct _XDisplay Display;
typedef unsigned long GLXDrawable;

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

struct syscall_family_stats {
    uint64_t calls;
    uint64_t total_ns;
    uint64_t max_ns;
};

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
    uint64_t glx_swap_buffers_calls;
    uint64_t glx_swap_buffers_msc_oml_calls;
    uint64_t glx_swap_total_ns;
    uint64_t glx_swap_max_ns;
    uint64_t glx_swap_nested_pixmap_calls;
    uint64_t glx_swap_nested_pixmap_checked_calls;
    uint64_t glx_swap_nested_pixmap_total_ns;
    uint64_t glx_swap_nested_wait_special_calls;
    uint64_t glx_swap_nested_poll_special_calls;
    uint64_t glx_swap_nested_wait_special_total_ns;
    uint64_t glx_swap_nested_poll_special_total_ns;
    uint64_t glx_swap_nested_complete_events;
    uint64_t glx_swap_nested_xcb_flush_calls;
    uint64_t glx_swap_nested_xcb_flush_total_ns;
    uint64_t glx_swap_nested_xcb_request_check_calls;
    uint64_t glx_swap_nested_xcb_request_check_total_ns;
    uint64_t glx_swap_nested_xcb_wait_reply_calls;
    uint64_t glx_swap_nested_xcb_wait_reply_total_ns;
    uint64_t glx_swap_nested_xcb_poll_reply_calls;
    uint64_t glx_swap_nested_xcb_poll_reply_total_ns;
    uint64_t glx_swap_nested_xcb_wait_event_calls;
    uint64_t glx_swap_nested_xcb_wait_event_total_ns;
    uint64_t glx_swap_nested_xcb_poll_event_calls;
    uint64_t glx_swap_nested_xcb_poll_event_total_ns;
    uint64_t glx_swap_syscall_total_ns;
    uint64_t glx_swap_syscall_nested_x11_ns;
    struct syscall_family_stats glx_swap_syscall_ioctl;
    struct syscall_family_stats glx_swap_syscall_poll_ppoll;
    struct syscall_family_stats glx_swap_syscall_select_pselect;
    struct syscall_family_stats glx_swap_syscall_read_recv;
    struct syscall_family_stats glx_swap_syscall_write_send;
    uint64_t glx_swap_missing_symbols;
    uint64_t glx_swap_recursion_skips;
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
static void *glx_swap_buffers_sym;
static void *glx_swap_buffers_msc_oml_sym;
static void *glx_get_proc_address_sym;
static void *glx_get_proc_address_arb_sym;
static void *xcb_flush_sym;
static void *xcb_request_check_sym;
static void *xcb_wait_for_reply_sym;
static void *xcb_poll_for_reply_sym;
static void *xcb_wait_for_event_sym;
static void *xcb_poll_for_event_sym;
static void *ioctl_sym;
static void *poll_sym;
static void *ppoll_sym;
static void *select_sym;
static void *pselect_sym;
static void *read_sym;
static void *recvmsg_sym;
static void *write_sym;
static void *writev_sym;
static void *sendmsg_sym;
static void *present_lib_handle;
static int present_pixmap_resolved;
static int present_pixmap_checked_resolved;
static int wait_special_resolved;
static int poll_special_resolved;
static int register_special_xge_resolved;
static int present_id_resolved;
static int glx_swap_buffers_resolved;
static int glx_swap_buffers_msc_oml_resolved;
static int glx_get_proc_address_resolved;
static int glx_get_proc_address_arb_resolved;
static int xcb_flush_resolved;
static int xcb_request_check_resolved;
static int xcb_wait_for_reply_resolved;
static int xcb_poll_for_reply_resolved;
static int xcb_wait_for_event_resolved;
static int xcb_poll_for_event_resolved;
static int ioctl_resolved;
static int poll_resolved;
static int ppoll_resolved;
static int select_resolved;
static int pselect_resolved;
static int read_resolved;
static int recvmsg_resolved;
static int write_resolved;
static int writev_resolved;
static int sendmsg_resolved;
static int present_id_missing_recorded;
static int present_lib_handle_resolved;
static xcb_special_event_t *present_special_events[MAX_PRESENT_SPECIAL_EVENTS];
static volatile int present_special_events_lock;
static __thread int glx_swap_depth;
static __thread int glx_swap_x11_real_call_depth;
static __thread int present_special_wait_depth;
static __thread int syscall_trace_suppressed;

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
typedef void (*glx_swap_buffers_fn)(Display *, GLXDrawable);
typedef int64_t (*glx_swap_buffers_msc_oml_fn)(Display *, GLXDrawable,
                                               int64_t, int64_t, int64_t);
typedef void (*glx_proc_address_result_fn)(void);
typedef glx_proc_address_result_fn (*glx_get_proc_address_fn)(
    const unsigned char *);
typedef int (*xcb_flush_fn)(xcb_connection_t *);
typedef xcb_generic_error_t *(*xcb_request_check_fn)(xcb_connection_t *,
                                                     xcb_void_cookie_t);
typedef void *(*xcb_wait_for_reply_fn)(xcb_connection_t *, unsigned int,
                                       xcb_generic_error_t **);
typedef int (*xcb_poll_for_reply_fn)(xcb_connection_t *, unsigned int,
                                     void **, xcb_generic_error_t **);
typedef xcb_generic_event_t *(*xcb_wait_for_event_fn)(xcb_connection_t *);
typedef xcb_generic_event_t *(*xcb_poll_for_event_fn)(xcb_connection_t *);
typedef int (*ioctl_fn)(int, unsigned long, ...);
typedef int (*poll_fn)(struct pollfd *, nfds_t, int);
typedef int (*ppoll_fn)(struct pollfd *, nfds_t, const struct timespec *,
                        const sigset_t *);
typedef int (*select_fn)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
typedef int (*pselect_fn)(int, fd_set *, fd_set *, fd_set *,
                          const struct timespec *, const sigset_t *);
typedef ssize_t (*read_fn)(int, void *, size_t);
typedef ssize_t (*recvmsg_fn)(int, struct msghdr *, int);
typedef ssize_t (*write_fn)(int, const void *, size_t);
typedef ssize_t (*writev_fn)(int, const struct iovec *, int);
typedef ssize_t (*sendmsg_fn)(int, const struct msghdr *, int);

void glXSwapBuffers(Display *dpy, GLXDrawable drawable);
int64_t glXSwapBuffersMscOML(Display *dpy, GLXDrawable drawable,
                             int64_t target_msc, int64_t divisor,
                             int64_t remainder);

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

static void update_max_u64(uint64_t *ptr, uint64_t value)
{
    uint64_t old = __atomic_load_n(ptr, __ATOMIC_RELAXED);

    while (old < value &&
           !__atomic_compare_exchange_n(ptr, &old, value, 0,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED))
        ;
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

static void record_glx_swap_syscall(struct syscall_family_stats *family,
                                    uint64_t elapsed, int nested_x11)
{
    add_u64(&stats.glx_swap_syscall_total_ns, elapsed);
    if (nested_x11)
        add_u64(&stats.glx_swap_syscall_nested_x11_ns, elapsed);
    add_u64(&family->calls, 1);
    add_u64(&family->total_ns, elapsed);
    update_max_u64(&family->max_ns, elapsed);
}

static void enter_glx_swap_x11_real_call(int enabled)
{
    if (enabled)
        glx_swap_x11_real_call_depth++;
}

static void leave_glx_swap_x11_real_call(int enabled)
{
    if (enabled)
        glx_swap_x11_real_call_depth--;
}

struct glx_swap_x11_wait_cleanup {
    int in_glx_swap;
};

static void glx_swap_x11_wait_cleanup(void *arg)
{
    struct glx_swap_x11_wait_cleanup *cleanup = arg;

    if (!cleanup->in_glx_swap)
        return;
    present_special_wait_depth--;
    leave_glx_swap_x11_real_call(1);
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
    syscall_trace_suppressed++;
    if (log_fd >= 0)
        ignored = write(log_fd, buf, pos);
    else
        ignored = write(STDERR_FILENO, buf, pos);
    syscall_trace_suppressed--;
    (void)ignored;
}

static void trace_vlogf(const char *fmt, va_list ap)
{
    char buf[12288];
    int len;

    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len <= 0)
        return;
    if ((size_t)len >= sizeof(buf))
        len = (int)sizeof(buf) - 1;
    syscall_trace_suppressed++;
    if (log_fd >= 0) {
        ssize_t ignored = write(log_fd, buf, (size_t)len);
        (void)ignored;
    } else {
        ssize_t ignored = write(STDERR_FILENO, buf, (size_t)len);
        (void)ignored;
    }
    syscall_trace_suppressed--;
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

static void *resolve_libc_symbol(const char *name, void **slot, int *resolved)
{
    if (__atomic_load_n(resolved, __ATOMIC_ACQUIRE))
        return __atomic_load_n(slot, __ATOMIC_RELAXED);

    dlerror();
    *slot = dlsym(RTLD_NEXT, name);
    __atomic_store_n(resolved, 1, __ATOMIC_RELEASE);
    return *slot;
}

static void *resolve_glx_next_symbol(const char *name, void **slot,
                                     int *resolved, int record_missing)
{
    if (__atomic_load_n(resolved, __ATOMIC_ACQUIRE))
        return __atomic_load_n(slot, __ATOMIC_RELAXED);

    dlerror();
    *slot = dlsym(RTLD_NEXT, name);
    if (!*slot && record_missing)
        add_u64(&stats.glx_swap_missing_symbols, 1);
    __atomic_store_n(resolved, 1, __ATOMIC_RELEASE);
    return *slot;
}

static glx_proc_address_result_fn
lookup_real_glx_proc_address(const char *name)
{
    glx_get_proc_address_fn real_fn;
    glx_proc_address_result_fn result;

    real_fn = (glx_get_proc_address_fn)resolve_glx_next_symbol(
        "glXGetProcAddressARB", &glx_get_proc_address_arb_sym,
        &glx_get_proc_address_arb_resolved, 0);
    if (real_fn) {
        result = real_fn((const unsigned char *)name);
        if (result)
            return result;
    }

    real_fn = (glx_get_proc_address_fn)resolve_glx_next_symbol(
        "glXGetProcAddress", &glx_get_proc_address_sym,
        &glx_get_proc_address_resolved, 0);
    if (real_fn)
        return real_fn((const unsigned char *)name);
    return NULL;
}

static void *lookup_glx_target_proc_address(
    const unsigned char *proc_name, glx_get_proc_address_fn real_fn,
    const char *name, void **slot, int *resolved)
{
    void *symbol = NULL;

    if (__atomic_load_n(resolved, __ATOMIC_ACQUIRE))
        return __atomic_load_n(slot, __ATOMIC_RELAXED);

    if (real_fn)
        symbol = (void *)real_fn(proc_name);
    if (!symbol) {
        dlerror();
        symbol = dlsym(RTLD_NEXT, name);
    }

    if (symbol) {
        __atomic_store_n(slot, symbol, __ATOMIC_RELAXED);
        __atomic_store_n(resolved, 1, __ATOMIC_RELEASE);
    }
    return symbol;
}

static void *resolve_glx_symbol(const char *name, void **slot, int *resolved)
{
    void *symbol;

    if (__atomic_load_n(resolved, __ATOMIC_ACQUIRE))
        return __atomic_load_n(slot, __ATOMIC_RELAXED);

    dlerror();
    symbol = dlsym(RTLD_NEXT, name);
    if (!symbol && strcmp(name, "glXSwapBuffersMscOML") == 0)
        symbol = (void *)lookup_real_glx_proc_address(name);
    if (!symbol)
        add_u64(&stats.glx_swap_missing_symbols, 1);
    __atomic_store_n(slot, symbol, __ATOMIC_RELAXED);
    __atomic_store_n(resolved, 1, __ATOMIC_RELEASE);
    return symbol;
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
    if (glx_swap_depth > 0)
        add_u64(&stats.glx_swap_nested_complete_events, 1);
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
    trace_logf("host-x11-present-trace: phase=present_trace status=BEGIN pid=%ld glx_swap_trace=1 glx_swap_xcb_trace=1 glx_swap_syscall_trace=1 exe=%s\n",
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
    {
        uint64_t glx_swap_total_ns = load_u64(&stats.glx_swap_total_ns);
        uint64_t glx_swap_accounted_present_ns =
            load_u64(&stats.glx_swap_nested_pixmap_total_ns) +
            load_u64(&stats.glx_swap_nested_wait_special_total_ns) +
            load_u64(&stats.glx_swap_nested_poll_special_total_ns);
        uint64_t glx_swap_accounted_xcb_ns =
            load_u64(&stats.glx_swap_nested_xcb_flush_total_ns) +
            load_u64(&stats.glx_swap_nested_xcb_request_check_total_ns) +
            load_u64(&stats.glx_swap_nested_xcb_wait_reply_total_ns) +
            load_u64(&stats.glx_swap_nested_xcb_poll_reply_total_ns) +
            load_u64(&stats.glx_swap_nested_xcb_wait_event_total_ns) +
            load_u64(&stats.glx_swap_nested_xcb_poll_event_total_ns);
        uint64_t glx_swap_above_present_ns = 0;
        uint64_t glx_swap_above_xcb_ns = 0;
        uint64_t glx_swap_syscall_total_ns =
            load_u64(&stats.glx_swap_syscall_total_ns);
        uint64_t glx_swap_syscall_nested_x11_ns =
            load_u64(&stats.glx_swap_syscall_nested_x11_ns);
        uint64_t glx_swap_syscall_above_x11_ns = 0;

        if (glx_swap_total_ns > glx_swap_accounted_present_ns)
            glx_swap_above_present_ns =
                glx_swap_total_ns - glx_swap_accounted_present_ns;
        if (glx_swap_total_ns >
            glx_swap_accounted_present_ns + glx_swap_accounted_xcb_ns)
            glx_swap_above_xcb_ns =
                glx_swap_total_ns - glx_swap_accounted_present_ns -
                glx_swap_accounted_xcb_ns;
        if (glx_swap_syscall_total_ns > glx_swap_syscall_nested_x11_ns)
            glx_swap_syscall_above_x11_ns =
                glx_swap_syscall_total_ns - glx_swap_syscall_nested_x11_ns;
        trace_logf(
            "host-x11-present-trace: phase=glx_swap_trace_result status=PASS "
            "pid=%ld glx_swap_syscall_trace=1 "
            "glx_swap_buffers_calls=%" PRIu64 " "
            "glx_swap_buffers_msc_oml_calls=%" PRIu64 " "
            "glx_swap_total_ms=%.3f glx_swap_max_ms=%.3f "
            "glx_swap_nested_pixmap_calls=%" PRIu64 " "
            "glx_swap_nested_pixmap_checked_calls=%" PRIu64 " "
            "glx_swap_nested_pixmap_total_ms=%.3f "
            "glx_swap_nested_wait_special_calls=%" PRIu64 " "
            "glx_swap_nested_poll_special_calls=%" PRIu64 " "
            "glx_swap_nested_wait_special_total_ms=%.3f "
            "glx_swap_nested_poll_special_total_ms=%.3f "
            "glx_swap_nested_complete_events=%" PRIu64 " "
            "glx_swap_nested_xcb_flush_calls=%" PRIu64 " "
            "glx_swap_nested_xcb_flush_total_ms=%.3f "
            "glx_swap_nested_xcb_request_check_calls=%" PRIu64 " "
            "glx_swap_nested_xcb_request_check_total_ms=%.3f "
            "glx_swap_nested_xcb_wait_reply_calls=%" PRIu64 " "
            "glx_swap_nested_xcb_wait_reply_total_ms=%.3f "
            "glx_swap_nested_xcb_poll_reply_calls=%" PRIu64 " "
            "glx_swap_nested_xcb_poll_reply_total_ms=%.3f "
            "glx_swap_nested_xcb_wait_event_calls=%" PRIu64 " "
            "glx_swap_nested_xcb_wait_event_total_ms=%.3f "
            "glx_swap_nested_xcb_poll_event_calls=%" PRIu64 " "
            "glx_swap_nested_xcb_poll_event_total_ms=%.3f "
            "glx_swap_accounted_present_ms=%.3f "
            "glx_swap_above_present_ms=%.3f "
            "glx_swap_accounted_xcb_ms=%.3f "
            "glx_swap_above_xcb_ms=%.3f "
            "glx_swap_syscall_total_ms=%.3f "
            "glx_swap_syscall_nested_x11_ms=%.3f "
            "glx_swap_syscall_above_x11_ms=%.3f "
            "glx_swap_syscall_ioctl_calls=%" PRIu64 " "
            "glx_swap_syscall_ioctl_total_ms=%.3f "
            "glx_swap_syscall_ioctl_max_ms=%.3f "
            "glx_swap_syscall_poll_ppoll_calls=%" PRIu64 " "
            "glx_swap_syscall_poll_ppoll_total_ms=%.3f "
            "glx_swap_syscall_poll_ppoll_max_ms=%.3f "
            "glx_swap_syscall_select_pselect_calls=%" PRIu64 " "
            "glx_swap_syscall_select_pselect_total_ms=%.3f "
            "glx_swap_syscall_select_pselect_max_ms=%.3f "
            "glx_swap_syscall_read_recv_calls=%" PRIu64 " "
            "glx_swap_syscall_read_recv_total_ms=%.3f "
            "glx_swap_syscall_read_recv_max_ms=%.3f "
            "glx_swap_syscall_write_send_calls=%" PRIu64 " "
            "glx_swap_syscall_write_send_total_ms=%.3f "
            "glx_swap_syscall_write_send_max_ms=%.3f "
            "glx_swap_missing_symbols=%" PRIu64 " "
            "glx_swap_recursion_skips=%" PRIu64 "\n",
            (long)getpid(),
            load_u64(&stats.glx_swap_buffers_calls),
            load_u64(&stats.glx_swap_buffers_msc_oml_calls),
            (double)glx_swap_total_ns / 1000000.0,
            (double)load_u64(&stats.glx_swap_max_ns) / 1000000.0,
            load_u64(&stats.glx_swap_nested_pixmap_calls),
            load_u64(&stats.glx_swap_nested_pixmap_checked_calls),
            (double)load_u64(&stats.glx_swap_nested_pixmap_total_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_nested_wait_special_calls),
            load_u64(&stats.glx_swap_nested_poll_special_calls),
            (double)load_u64(&stats.glx_swap_nested_wait_special_total_ns) /
                1000000.0,
            (double)load_u64(&stats.glx_swap_nested_poll_special_total_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_nested_complete_events),
            load_u64(&stats.glx_swap_nested_xcb_flush_calls),
            (double)load_u64(&stats.glx_swap_nested_xcb_flush_total_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_nested_xcb_request_check_calls),
            (double)load_u64(
                &stats.glx_swap_nested_xcb_request_check_total_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_nested_xcb_wait_reply_calls),
            (double)load_u64(
                &stats.glx_swap_nested_xcb_wait_reply_total_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_nested_xcb_poll_reply_calls),
            (double)load_u64(
                &stats.glx_swap_nested_xcb_poll_reply_total_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_nested_xcb_wait_event_calls),
            (double)load_u64(
                &stats.glx_swap_nested_xcb_wait_event_total_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_nested_xcb_poll_event_calls),
            (double)load_u64(
                &stats.glx_swap_nested_xcb_poll_event_total_ns) /
                1000000.0,
            (double)glx_swap_accounted_present_ns / 1000000.0,
            (double)glx_swap_above_present_ns / 1000000.0,
            (double)glx_swap_accounted_xcb_ns / 1000000.0,
            (double)glx_swap_above_xcb_ns / 1000000.0,
            (double)glx_swap_syscall_total_ns / 1000000.0,
            (double)glx_swap_syscall_nested_x11_ns / 1000000.0,
            (double)glx_swap_syscall_above_x11_ns / 1000000.0,
            load_u64(&stats.glx_swap_syscall_ioctl.calls),
            (double)load_u64(&stats.glx_swap_syscall_ioctl.total_ns) /
                1000000.0,
            (double)load_u64(&stats.glx_swap_syscall_ioctl.max_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_syscall_poll_ppoll.calls),
            (double)load_u64(&stats.glx_swap_syscall_poll_ppoll.total_ns) /
                1000000.0,
            (double)load_u64(&stats.glx_swap_syscall_poll_ppoll.max_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_syscall_select_pselect.calls),
            (double)load_u64(
                &stats.glx_swap_syscall_select_pselect.total_ns) /
                1000000.0,
            (double)load_u64(&stats.glx_swap_syscall_select_pselect.max_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_syscall_read_recv.calls),
            (double)load_u64(&stats.glx_swap_syscall_read_recv.total_ns) /
                1000000.0,
            (double)load_u64(&stats.glx_swap_syscall_read_recv.max_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_syscall_write_send.calls),
            (double)load_u64(&stats.glx_swap_syscall_write_send.total_ns) /
                1000000.0,
            (double)load_u64(&stats.glx_swap_syscall_write_send.max_ns) /
                1000000.0,
            load_u64(&stats.glx_swap_missing_symbols),
            load_u64(&stats.glx_swap_recursion_skips));
    }
    if (log_fd >= 0)
        close(log_fd);
}

void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    glx_swap_buffers_fn real_fn;
    uint64_t start;
    uint64_t elapsed;

    real_fn = (glx_swap_buffers_fn)resolve_glx_symbol(
        "glXSwapBuffers", &glx_swap_buffers_sym,
        &glx_swap_buffers_resolved);
    if (!real_fn)
        return;
    if (glx_swap_depth > 0) {
        add_u64(&stats.glx_swap_recursion_skips, 1);
        real_fn(dpy, drawable);
        return;
    }

    add_u64(&stats.glx_swap_buffers_calls, 1);
    start = now_ns();
    glx_swap_depth++;
    real_fn(dpy, drawable);
    glx_swap_depth--;
    elapsed = elapsed_ns(start, now_ns());
    add_u64(&stats.glx_swap_total_ns, elapsed);
    update_max_u64(&stats.glx_swap_max_ns, elapsed);
}

int64_t glXSwapBuffersMscOML(Display *dpy, GLXDrawable drawable,
                             int64_t target_msc, int64_t divisor,
                             int64_t remainder)
{
    glx_swap_buffers_msc_oml_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int64_t result;

    real_fn = (glx_swap_buffers_msc_oml_fn)resolve_glx_symbol(
        "glXSwapBuffersMscOML", &glx_swap_buffers_msc_oml_sym,
        &glx_swap_buffers_msc_oml_resolved);
    if (!real_fn)
        return 0;
    if (glx_swap_depth > 0) {
        add_u64(&stats.glx_swap_recursion_skips, 1);
        return real_fn(dpy, drawable, target_msc, divisor, remainder);
    }

    add_u64(&stats.glx_swap_buffers_msc_oml_calls, 1);
    start = now_ns();
    glx_swap_depth++;
    result = real_fn(dpy, drawable, target_msc, divisor, remainder);
    glx_swap_depth--;
    elapsed = elapsed_ns(start, now_ns());
    add_u64(&stats.glx_swap_total_ns, elapsed);
    update_max_u64(&stats.glx_swap_max_ns, elapsed);
    return result;
}

static glx_proc_address_result_fn glx_get_proc_address_common(
    const unsigned char *proc_name, const char *real_name, void **slot,
    int *resolved)
{
    glx_get_proc_address_fn real_fn;
    glx_get_proc_address_fn fallback_fn;
    const char *name = (const char *)proc_name;

    real_fn = (glx_get_proc_address_fn)resolve_glx_next_symbol(
        real_name, slot, resolved, 0);
    if (!real_fn && strcmp(real_name, "glXGetProcAddressARB") == 0) {
        fallback_fn = (glx_get_proc_address_fn)resolve_glx_next_symbol(
            "glXGetProcAddress", &glx_get_proc_address_sym,
            &glx_get_proc_address_resolved, 0);
        real_fn = fallback_fn;
    } else if (!real_fn) {
        fallback_fn = (glx_get_proc_address_fn)resolve_glx_next_symbol(
            "glXGetProcAddressARB", &glx_get_proc_address_arb_sym,
            &glx_get_proc_address_arb_resolved, 0);
        real_fn = fallback_fn;
    }
    if (name && strcmp(name, "glXSwapBuffersMscOML") == 0) {
        if (lookup_glx_target_proc_address(
                proc_name, real_fn, name, &glx_swap_buffers_msc_oml_sym,
                &glx_swap_buffers_msc_oml_resolved))
            return (glx_proc_address_result_fn)glXSwapBuffersMscOML;
        return NULL;
    }
    if (name && strcmp(name, "glXSwapBuffers") == 0) {
        if (lookup_glx_target_proc_address(
                proc_name, real_fn, name, &glx_swap_buffers_sym,
                &glx_swap_buffers_resolved))
            return (glx_proc_address_result_fn)glXSwapBuffers;
        return NULL;
    }
    if (!real_fn) {
        add_u64(&stats.glx_swap_missing_symbols, 1);
        return NULL;
    }
    return real_fn(proc_name);
}

glx_proc_address_result_fn glXGetProcAddressARB(
    const unsigned char *proc_name)
{
    return glx_get_proc_address_common(
        proc_name, "glXGetProcAddressARB", &glx_get_proc_address_arb_sym,
        &glx_get_proc_address_arb_resolved);
}

glx_proc_address_result_fn glXGetProcAddress(const unsigned char *proc_name)
{
    return glx_get_proc_address_common(
        proc_name, "glXGetProcAddress", &glx_get_proc_address_sym,
        &glx_get_proc_address_resolved);
}

int xcb_flush(xcb_connection_t *c)
{
    xcb_flush_fn real_fn;
    uint64_t start;
    int in_glx_swap;
    int result;

    in_glx_swap = glx_swap_depth > 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_flush_calls, 1);
    real_fn = (xcb_flush_fn)resolve_glx_next_symbol(
        "xcb_flush", &xcb_flush_sym, &xcb_flush_resolved, in_glx_swap);
    if (!real_fn)
        return 0;

    start = in_glx_swap ? now_ns() : 0;
    enter_glx_swap_x11_real_call(in_glx_swap);
    result = real_fn(c);
    leave_glx_swap_x11_real_call(in_glx_swap);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_flush_total_ns,
                elapsed_ns(start, now_ns()));
    return result;
}

xcb_generic_error_t *xcb_request_check(xcb_connection_t *c,
                                       xcb_void_cookie_t cookie)
{
    xcb_request_check_fn real_fn;
    xcb_generic_error_t *error;
    uint64_t start;
    int in_glx_swap;

    in_glx_swap = glx_swap_depth > 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_request_check_calls, 1);
    real_fn = (xcb_request_check_fn)resolve_glx_next_symbol(
        "xcb_request_check", &xcb_request_check_sym,
        &xcb_request_check_resolved, in_glx_swap);
    if (!real_fn)
        return NULL;

    start = in_glx_swap ? now_ns() : 0;
    enter_glx_swap_x11_real_call(in_glx_swap);
    error = real_fn(c, cookie);
    leave_glx_swap_x11_real_call(in_glx_swap);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_request_check_total_ns,
                elapsed_ns(start, now_ns()));
    return error;
}

void *xcb_wait_for_reply(xcb_connection_t *c, unsigned int request,
                         xcb_generic_error_t **e)
{
    xcb_wait_for_reply_fn real_fn;
    void *reply;
    uint64_t start;
    int in_glx_swap;

    in_glx_swap = glx_swap_depth > 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_wait_reply_calls, 1);
    real_fn = (xcb_wait_for_reply_fn)resolve_glx_next_symbol(
        "xcb_wait_for_reply", &xcb_wait_for_reply_sym,
        &xcb_wait_for_reply_resolved, in_glx_swap);
    if (!real_fn)
        return NULL;

    start = in_glx_swap ? now_ns() : 0;
    enter_glx_swap_x11_real_call(in_glx_swap);
    reply = real_fn(c, request, e);
    leave_glx_swap_x11_real_call(in_glx_swap);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_wait_reply_total_ns,
                elapsed_ns(start, now_ns()));
    return reply;
}

int xcb_poll_for_reply(xcb_connection_t *c, unsigned int request,
                       void **reply, xcb_generic_error_t **error)
{
    xcb_poll_for_reply_fn real_fn;
    uint64_t start;
    int in_glx_swap;
    int result;

    in_glx_swap = glx_swap_depth > 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_poll_reply_calls, 1);
    real_fn = (xcb_poll_for_reply_fn)resolve_glx_next_symbol(
        "xcb_poll_for_reply", &xcb_poll_for_reply_sym,
        &xcb_poll_for_reply_resolved, in_glx_swap);
    if (!real_fn)
        return 0;

    start = in_glx_swap ? now_ns() : 0;
    enter_glx_swap_x11_real_call(in_glx_swap);
    result = real_fn(c, request, reply, error);
    leave_glx_swap_x11_real_call(in_glx_swap);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_poll_reply_total_ns,
                elapsed_ns(start, now_ns()));
    return result;
}

xcb_generic_event_t *xcb_wait_for_event(xcb_connection_t *c)
{
    xcb_wait_for_event_fn real_fn;
    xcb_generic_event_t *event;
    uint64_t start;
    int in_glx_swap;

    in_glx_swap = glx_swap_depth > 0 && present_special_wait_depth == 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_wait_event_calls, 1);
    real_fn = (xcb_wait_for_event_fn)resolve_glx_next_symbol(
        "xcb_wait_for_event", &xcb_wait_for_event_sym,
        &xcb_wait_for_event_resolved, in_glx_swap);
    if (!real_fn)
        return NULL;

    start = in_glx_swap ? now_ns() : 0;
    enter_glx_swap_x11_real_call(in_glx_swap);
    event = real_fn(c);
    leave_glx_swap_x11_real_call(in_glx_swap);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_wait_event_total_ns,
                elapsed_ns(start, now_ns()));
    return event;
}

xcb_generic_event_t *xcb_poll_for_event(xcb_connection_t *c)
{
    xcb_poll_for_event_fn real_fn;
    xcb_generic_event_t *event;
    uint64_t start;
    int in_glx_swap;

    in_glx_swap = glx_swap_depth > 0 && present_special_wait_depth == 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_poll_event_calls, 1);
    real_fn = (xcb_poll_for_event_fn)resolve_glx_next_symbol(
        "xcb_poll_for_event", &xcb_poll_for_event_sym,
        &xcb_poll_for_event_resolved, in_glx_swap);
    if (!real_fn)
        return NULL;

    start = in_glx_swap ? now_ns() : 0;
    enter_glx_swap_x11_real_call(in_glx_swap);
    event = real_fn(c);
    leave_glx_swap_x11_real_call(in_glx_swap);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_xcb_poll_event_total_ns,
                elapsed_ns(start, now_ns()));
    return event;
}

xcb_special_event_t *xcb_register_for_special_xge(
    xcb_connection_t *c, xcb_extension_t *ext, uint32_t eid, uint32_t *stamp)
{
    register_special_xge_fn real_fn;
    xcb_special_event_t *se;
    xcb_extension_t *present_id;
    uint64_t call_count;
    int in_glx_swap;

    call_count = __atomic_add_fetch(&stats.register_special_xge_calls, 1,
                                    __ATOMIC_RELAXED);
    in_glx_swap = glx_swap_depth > 0;
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

    enter_glx_swap_x11_real_call(in_glx_swap);
    se = real_fn(c, ext, eid, stamp);
    leave_glx_swap_x11_real_call(in_glx_swap);
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
    int in_glx_swap;

    call_count =
        __atomic_add_fetch(&stats.pixmap_calls, 1, __ATOMIC_RELAXED);
    in_glx_swap = glx_swap_depth > 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_pixmap_calls, 1);
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
    enter_glx_swap_x11_real_call(in_glx_swap);
    cookie = real_fn(c, window, pixmap, serial, valid, update, x_off, y_off,
                     target_crtc, wait_fence, idle_fence, options, target_msc,
                     divisor, remainder, notifies_len, notifies);
    leave_glx_swap_x11_real_call(in_glx_swap);
    {
        uint64_t elapsed = elapsed_ns(start, now_ns());

        add_u64(&stats.pixmap_total_ns, elapsed);
        if (in_glx_swap)
            add_u64(&stats.glx_swap_nested_pixmap_total_ns, elapsed);
    }
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
    int in_glx_swap;

    call_count = __atomic_add_fetch(&stats.pixmap_checked_calls, 1,
                                    __ATOMIC_RELAXED);
    in_glx_swap = glx_swap_depth > 0;
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_pixmap_checked_calls, 1);
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
    enter_glx_swap_x11_real_call(in_glx_swap);
    cookie = real_fn(c, window, pixmap, serial, valid, update, x_off, y_off,
                     target_crtc, wait_fence, idle_fence, options, target_msc,
                     divisor, remainder, notifies_len, notifies);
    leave_glx_swap_x11_real_call(in_glx_swap);
    {
        uint64_t elapsed = elapsed_ns(start, now_ns());

        add_u64(&stats.pixmap_total_ns, elapsed);
        if (in_glx_swap)
            add_u64(&stats.glx_swap_nested_pixmap_total_ns, elapsed);
    }
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
    int in_glx_swap;

    call_count =
        __atomic_add_fetch(&stats.wait_special_calls, 1, __ATOMIC_RELAXED);
    in_glx_swap = glx_swap_depth > 0 && is_present_special_event(se);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_wait_special_calls, 1);
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
    {
        struct glx_swap_x11_wait_cleanup cleanup = { in_glx_swap };

        enter_glx_swap_x11_real_call(in_glx_swap);
        if (in_glx_swap)
            present_special_wait_depth++;
        pthread_cleanup_push(glx_swap_x11_wait_cleanup, &cleanup);
        event = real_fn(c, se);
        pthread_cleanup_pop(0);
        if (in_glx_swap)
            present_special_wait_depth--;
        leave_glx_swap_x11_real_call(in_glx_swap);
    }
    {
        uint64_t elapsed = elapsed_ns(start, now_ns());

        add_u64(&stats.wait_special_total_ns, elapsed);
        if (in_glx_swap)
            add_u64(&stats.glx_swap_nested_wait_special_total_ns, elapsed);
    }
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
    int in_glx_swap;

    call_count =
        __atomic_add_fetch(&stats.poll_special_calls, 1, __ATOMIC_RELAXED);
    in_glx_swap = glx_swap_depth > 0 && is_present_special_event(se);
    if (in_glx_swap)
        add_u64(&stats.glx_swap_nested_poll_special_calls, 1);
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
    {
        struct glx_swap_x11_wait_cleanup cleanup = { in_glx_swap };

        enter_glx_swap_x11_real_call(in_glx_swap);
        if (in_glx_swap)
            present_special_wait_depth++;
        pthread_cleanup_push(glx_swap_x11_wait_cleanup, &cleanup);
        event = real_fn(c, se);
        pthread_cleanup_pop(0);
        if (in_glx_swap)
            present_special_wait_depth--;
        leave_glx_swap_x11_real_call(in_glx_swap);
    }
    {
        uint64_t elapsed = elapsed_ns(start, now_ns());

        add_u64(&stats.poll_special_total_ns, elapsed);
        if (in_glx_swap)
            add_u64(&stats.glx_swap_nested_poll_special_total_ns, elapsed);
    }
    decode_complete_event(se, event);
    if (should_log_progress(call_count))
        trace_progress("xcb_poll_for_special_event", "return");
    return event;
}

int ioctl(int fd, unsigned long request, ...)
{
    ioctl_fn real_fn;
    va_list ap;
    void *arg = NULL;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    int result;
    int saved_errno;

    real_fn = (ioctl_fn)resolve_libc_symbol("ioctl", &ioctl_sym,
                                            &ioctl_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }

    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    result = real_fn(fd, request, arg);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_ioctl, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    poll_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    int result;
    int saved_errno;

    real_fn = (poll_fn)resolve_libc_symbol("poll", &poll_sym,
                                           &poll_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(fds, nfds, timeout);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_poll_ppoll, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
          const sigset_t *sigmask)
{
    ppoll_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    int result;
    int saved_errno;

    real_fn = (ppoll_fn)resolve_libc_symbol("ppoll", &ppoll_sym,
                                            &ppoll_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(fds, nfds, timeout, sigmask);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_poll_ppoll, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    select_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    int result;
    int saved_errno;

    real_fn = (select_fn)resolve_libc_symbol("select", &select_sym,
                                             &select_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(nfds, readfds, writefds, exceptfds, timeout);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_select_pselect,
                                elapsed, nested_x11);
    errno = saved_errno;
    return result;
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const struct timespec *timeout, const sigset_t *sigmask)
{
    pselect_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    int result;
    int saved_errno;

    real_fn = (pselect_fn)resolve_libc_symbol("pselect", &pselect_sym,
                                              &pselect_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(nfds, readfds, writefds, exceptfds, timeout, sigmask);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_select_pselect,
                                elapsed, nested_x11);
    errno = saved_errno;
    return result;
}

ssize_t read(int fd, void *buf, size_t count)
{
    read_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    ssize_t result;
    int saved_errno;

    real_fn = (read_fn)resolve_libc_symbol("read", &read_sym,
                                           &read_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(fd, buf, count);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_read_recv, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    recvmsg_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    ssize_t result;
    int saved_errno;

    real_fn = (recvmsg_fn)resolve_libc_symbol("recvmsg", &recvmsg_sym,
                                              &recvmsg_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(sockfd, msg, flags);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_read_recv, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    write_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    ssize_t result;
    int saved_errno;

    real_fn = (write_fn)resolve_libc_symbol("write", &write_sym,
                                            &write_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(fd, buf, count);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_write_send, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    writev_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    ssize_t result;
    int saved_errno;

    real_fn = (writev_fn)resolve_libc_symbol("writev", &writev_sym,
                                             &writev_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(fd, iov, iovcnt);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_write_send, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    sendmsg_fn real_fn;
    uint64_t start;
    uint64_t elapsed;
    int trace;
    int nested_x11;
    ssize_t result;
    int saved_errno;

    real_fn = (sendmsg_fn)resolve_libc_symbol("sendmsg", &sendmsg_sym,
                                              &sendmsg_resolved);
    if (!real_fn) {
        errno = ENOSYS;
        return -1;
    }
    trace = glx_swap_depth > 0 && syscall_trace_suppressed == 0;
    nested_x11 = trace && glx_swap_x11_real_call_depth > 0;
    start = trace ? now_ns() : 0;
    result = real_fn(sockfd, msg, flags);
    saved_errno = errno;
    elapsed = trace ? elapsed_ns(start, now_ns()) : 0;
    if (trace)
        record_glx_swap_syscall(&stats.glx_swap_syscall_write_send, elapsed,
                                nested_x11);
    errno = saved_errno;
    return result;
}
