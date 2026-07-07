#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_gettid
#define SYS_gettid 186
#endif

struct wl_display;
struct wl_event_queue;
typedef struct _GMainContext GMainContext;
typedef int gboolean;
typedef int gint;
typedef unsigned int guint;

typedef struct {
    gint fd;
    unsigned short events;
    unsigned short revents;
} GPollFD;

#define TRACE_MAX_FD 256
#define TRACE_MAX_DISPLAY 32
#define TRACE_MAX_PIPE 64
#define TRACE_TARGET_LEN 96
#define TRACE_COMM_LEN 32
#define TRACE_THRESHOLD_US 20000ULL
#define TRACE_OUTPUT_LIMIT_DEFAULT 512ULL
#define LOADER_TRACE_OUTPUT_LIMIT_DEFAULT 4096ULL

typedef int (*open_fn_t)(const char *, int, ...);
typedef int (*openat_fn_t)(int, const char *, int, ...);
typedef void *(*dlopen_fn_t)(const char *, int);
typedef int (*poll_fn_t)(struct pollfd *, nfds_t, int);
typedef int (*ppoll_fn_t)(struct pollfd *, nfds_t, const struct timespec *,
                          const sigset_t *);
typedef ssize_t (*read_fn_t)(int, void *, size_t);
typedef ssize_t (*write_fn_t)(int, const void *, size_t);
typedef ssize_t (*recvmsg_fn_t)(int, struct msghdr *, int);
typedef ssize_t (*sendmsg_fn_t)(int, const struct msghdr *, int);
typedef struct wl_display *(*wl_display_connect_fn_t)(const char *);
typedef struct wl_display *(*wl_display_connect_to_fd_fn_t)(int);
typedef int (*wl_display_get_fd_fn_t)(struct wl_display *);
typedef int (*wl_display_dispatch_fn_t)(struct wl_display *);
typedef int (*wl_display_dispatch_queue_fn_t)(struct wl_display *,
                                              struct wl_event_queue *);
typedef int (*wl_display_roundtrip_fn_t)(struct wl_display *);
typedef int (*wl_display_roundtrip_queue_fn_t)(struct wl_display *,
                                               struct wl_event_queue *);
typedef int (*wl_display_prepare_read_fn_t)(struct wl_display *);
typedef int (*wl_display_prepare_read_queue_fn_t)(struct wl_display *,
                                                  struct wl_event_queue *);
typedef int (*wl_display_flush_fn_t)(struct wl_display *);
typedef int (*wl_display_read_events_fn_t)(struct wl_display *);
typedef int (*wl_display_dispatch_pending_fn_t)(struct wl_display *);
typedef int (*wl_display_dispatch_queue_pending_fn_t)(struct wl_display *,
                                                      struct wl_event_queue *);
typedef void (*wl_display_cancel_read_fn_t)(struct wl_display *);
typedef int (*wl_proxy_add_listener_fn_t)(void *, void (**)(void), void *);
typedef void (*wl_proxy_destroy_fn_t)(void *);
typedef unsigned int (*wl_proxy_get_id_fn_t)(void *);
typedef gboolean (*g_main_context_iteration_fn_t)(GMainContext *, gboolean);
typedef gboolean (*g_main_context_pending_fn_t)(GMainContext *);
typedef void (*g_main_context_wakeup_fn_t)(GMainContext *);
typedef gboolean (*g_main_context_prepare_fn_t)(GMainContext *, gint *);
typedef gint (*g_main_context_query_fn_t)(GMainContext *, gint, gint *,
                                          GPollFD *, gint);
typedef gboolean (*g_main_context_check_fn_t)(GMainContext *, gint, GPollFD *,
                                              gint);
typedef void (*g_main_context_dispatch_fn_t)(GMainContext *);
typedef gint (*g_poll_fn_t)(GPollFD *, guint, gint);
typedef g_poll_fn_t (*g_main_context_get_poll_func_fn_t)(GMainContext *);
typedef void (*g_main_context_set_poll_func_fn_t)(GMainContext *, g_poll_fn_t);
typedef unsigned char qt_bool_t;
typedef qt_bool_t (*qt_process_events_fn_t)(void *, int);
typedef qt_bool_t (*qt_window_system_events_fn_t)(int);
typedef void (*qt_send_posted_events_fn_t)(void *, int);
typedef void (*qt_private_send_posted_events_fn_t)(void *, int, void *);
typedef qt_bool_t (*qt_notify_internal2_fn_t)(void *, void *);
typedef qt_bool_t (*qt_qsocket_notifier_event_fn_t)(void *, void *);
typedef void (*qt_qwayland_void_fn_t)(void *);
typedef int (*qt_qwayland_int_fn_t)(void *);

enum qt_cpp_op_id {
    QT_CPP_PROCESS_EVENTS = 0,
    QT_CPP_SEND_WINDOW_SYSTEM_EVENTS,
    QT_CPP_FLUSH_WINDOW_SYSTEM_EVENTS,
    QT_CPP_CORE_SEND_POSTED_EVENTS,
    QT_CPP_PRIVATE_SEND_POSTED_EVENTS,
    QT_CPP_NOTIFY_INTERNAL2,
    QT_CPP_QSOCKETNOTIFIER_EVENT,
    QT_CPP_QWAYLAND_HANDLE_SYNC,
    QT_CPP_QWAYLAND_REQUEST_SYNC,
    QT_CPP_QWAYLAND_BLOCKING_READ,
    QT_CPP_QWAYLAND_FLUSH_REQUESTS,
    QT_CPP_OP_COUNT
};

enum active_span_op_id {
    ACTIVE_SPAN_NONE = 0,
    ACTIVE_SPAN_PROCESS_EVENTS,
    ACTIVE_SPAN_GLIB_ITERATION,
    ACTIVE_SPAN_SEND_WINDOW_SYSTEM_EVENTS,
    ACTIVE_SPAN_OP_COUNT
};

enum glib_phase_op_id {
    GLIB_PHASE_NONE = 0,
    GLIB_PHASE_PREPARE,
    GLIB_PHASE_QUERY,
    GLIB_PHASE_CHECK,
    GLIB_PHASE_DISPATCH,
    GLIB_PHASE_POLL,
    GLIB_PHASE_OP_COUNT
};

enum public_wl_op_id {
    PUBLIC_WL_CONNECT = 0,
    PUBLIC_WL_CONNECT_TO_FD,
    PUBLIC_WL_GET_FD,
    PUBLIC_WL_PREPARE_READ,
    PUBLIC_WL_PREPARE_READ_QUEUE,
    PUBLIC_WL_CANCEL_READ,
    PUBLIC_WL_READ_EVENTS,
    PUBLIC_WL_DISPATCH,
    PUBLIC_WL_DISPATCH_PENDING,
    PUBLIC_WL_DISPATCH_QUEUE_PENDING,
    PUBLIC_WL_FLUSH,
    PUBLIC_WL_ROUNDTRIP,
    PUBLIC_WL_ROUNDTRIP_QUEUE,
    PUBLIC_WL_PROXY_ADD_LISTENER,
    PUBLIC_WL_PROXY_DESTROY,
    PUBLIC_WL_PROXY_GET_ID,
    PUBLIC_WL_OP_COUNT
};

enum wl_fd_syscall_op_id {
    WL_FD_SYSCALL_POLL = 0,
    WL_FD_SYSCALL_PPOLL,
    WL_FD_SYSCALL_READ,
    WL_FD_SYSCALL_WRITE,
    WL_FD_SYSCALL_RECVMSG,
    WL_FD_SYSCALL_SENDMSG,
    WL_FD_SYSCALL_OP_COUNT
};

struct public_wl_op_state {
    const char *name;
    unsigned long long calls;
    unsigned long long event_thread_calls;
    unsigned long long main_thread_calls;
    uint64_t max_elapsed_us;
    uint64_t max_ready_us;
};

struct wl_fd_syscall_state {
    const char *name;
    unsigned long long calls;
    uint64_t max_elapsed_us;
    uint64_t max_ready_us;
};

struct fd_state {
    int interesting;
    int is_wayland;
    int is_pipe;
    int last_poll_revents;
    int last_poll_ret;
    int last_poll_errno;
    int last_fionread;
    uint64_t last_poll_begin_us;
    uint64_t last_poll_return_us;
    char target[TRACE_TARGET_LEN];
};

struct display_state {
    const struct wl_display *display;
    int fd;
};

struct pipe_state {
    char target[TRACE_TARGET_LEN];
    uint64_t last_write_us;
    uint64_t last_poll_return_us;
    uint64_t write_to_poll_max_us;
    uint64_t poll_to_read_max_us;
    unsigned long long writes;
    unsigned long long reads;
    unsigned long long poll_returns;
};

struct qt_cpp_op_state {
    const char *name;
    unsigned long long calls;
    unsigned long long slow;
    uint64_t max_elapsed_us;
    uint64_t max_ready_us;
};

struct active_span_record {
    enum active_span_op_id op_id;
    uint64_t start_us;
    unsigned int depth;
    unsigned long long call_id;
    unsigned long long ready_seq_at_enter;
    int active;
    int pushed;
};

struct glib_phase_record {
    enum glib_phase_op_id op_id;
    GMainContext *context;
    uint64_t start_us;
    unsigned int depth;
    unsigned long long call_id;
    unsigned long long ready_seq_at_enter;
    int active;
    int pushed;
};

struct bridge_poll_context {
    GMainContext *context;
    g_poll_fn_t original;
    int installed;
};

static poll_fn_t real_poll;
static ppoll_fn_t real_ppoll;
static open_fn_t real_open;
static open_fn_t real_open64;
static openat_fn_t real_openat;
static openat_fn_t real_openat64;
static dlopen_fn_t real_dlopen;
static read_fn_t real_read;
static write_fn_t real_write;
static recvmsg_fn_t real_recvmsg;
static sendmsg_fn_t real_sendmsg;
static wl_display_connect_fn_t real_wl_display_connect;
static wl_display_connect_to_fd_fn_t real_wl_display_connect_to_fd;
static wl_display_get_fd_fn_t real_wl_display_get_fd;
static wl_display_dispatch_fn_t real_wl_display_dispatch;
static wl_display_dispatch_queue_fn_t real_wl_display_dispatch_queue;
static wl_display_roundtrip_fn_t real_wl_display_roundtrip;
static wl_display_roundtrip_queue_fn_t real_wl_display_roundtrip_queue;
static wl_display_prepare_read_fn_t real_wl_display_prepare_read;
static wl_display_prepare_read_queue_fn_t real_wl_display_prepare_read_queue;
static wl_display_flush_fn_t real_wl_display_flush;
static wl_display_read_events_fn_t real_wl_display_read_events;
static wl_display_dispatch_pending_fn_t real_wl_display_dispatch_pending;
static wl_display_dispatch_queue_pending_fn_t
    real_wl_display_dispatch_queue_pending;
static wl_display_cancel_read_fn_t real_wl_display_cancel_read;
static wl_proxy_add_listener_fn_t real_wl_proxy_add_listener;
static wl_proxy_destroy_fn_t real_wl_proxy_destroy;
static wl_proxy_get_id_fn_t real_wl_proxy_get_id;
static g_main_context_iteration_fn_t real_g_main_context_iteration;
static g_main_context_pending_fn_t real_g_main_context_pending;
static g_main_context_wakeup_fn_t real_g_main_context_wakeup;
static g_main_context_prepare_fn_t real_g_main_context_prepare;
static g_main_context_query_fn_t real_g_main_context_query;
static g_main_context_check_fn_t real_g_main_context_check;
static g_main_context_dispatch_fn_t real_g_main_context_dispatch;
static g_poll_fn_t real_g_poll;
static g_main_context_get_poll_func_fn_t real_g_main_context_get_poll_func;
static g_main_context_set_poll_func_fn_t real_g_main_context_set_poll_func;
static qt_process_events_fn_t real_qt_process_events;
static qt_window_system_events_fn_t real_qt_send_window_system_events;
static qt_window_system_events_fn_t real_qt_flush_window_system_events;
static qt_send_posted_events_fn_t real_qt_core_send_posted_events;
static qt_private_send_posted_events_fn_t
    real_qt_private_send_posted_events;
static qt_notify_internal2_fn_t real_qt_notify_internal2;
static qt_qsocket_notifier_event_fn_t real_qt_qsocket_notifier_event;
static qt_qwayland_void_fn_t real_qt_qwayland_handle_sync;
static qt_qwayland_void_fn_t real_qt_qwayland_request_sync;
static qt_qwayland_int_fn_t real_qt_qwayland_blocking_read;
static qt_qwayland_void_fn_t real_qt_qwayland_flush_requests;

static struct fd_state fd_states[TRACE_MAX_FD];
static struct display_state displays[TRACE_MAX_DISPLAY];
static struct pipe_state pipes[TRACE_MAX_PIPE];
static struct qt_cpp_op_state qt_cpp_ops[QT_CPP_OP_COUNT] = {
    [QT_CPP_PROCESS_EVENTS] = { "process_events", 0, 0, 0, 0 },
    [QT_CPP_SEND_WINDOW_SYSTEM_EVENTS] = {
        "send_window_system_events", 0, 0, 0, 0
    },
    [QT_CPP_FLUSH_WINDOW_SYSTEM_EVENTS] = {
        "flush_window_system_events", 0, 0, 0, 0
    },
    [QT_CPP_CORE_SEND_POSTED_EVENTS] = {
        "core_send_posted_events", 0, 0, 0, 0
    },
    [QT_CPP_PRIVATE_SEND_POSTED_EVENTS] = {
        "private_send_posted_events", 0, 0, 0, 0
    },
    [QT_CPP_NOTIFY_INTERNAL2] = { "notify_internal2", 0, 0, 0, 0 },
    [QT_CPP_QSOCKETNOTIFIER_EVENT] = {
        "qsocketnotifier_event", 0, 0, 0, 0
    },
    [QT_CPP_QWAYLAND_HANDLE_SYNC] = {
        "qwayland_handle_sync", 0, 0, 0, 0
    },
    [QT_CPP_QWAYLAND_REQUEST_SYNC] = {
        "qwayland_request_sync", 0, 0, 0, 0
    },
    [QT_CPP_QWAYLAND_BLOCKING_READ] = {
        "qwayland_blocking_read", 0, 0, 0, 0
    },
    [QT_CPP_QWAYLAND_FLUSH_REQUESTS] = {
        "qwayland_flush_requests", 0, 0, 0, 0
    },
};
static struct public_wl_op_state public_wl_ops[PUBLIC_WL_OP_COUNT] = {
    [PUBLIC_WL_CONNECT] = { "connect", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_CONNECT_TO_FD] = { "connect_to_fd", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_GET_FD] = { "get_fd", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_PREPARE_READ] = { "prepare_read", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_PREPARE_READ_QUEUE] = {
        "prepare_read_queue", 0, 0, 0, 0, 0
    },
    [PUBLIC_WL_CANCEL_READ] = { "cancel_read", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_READ_EVENTS] = { "read_events", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_DISPATCH] = { "dispatch", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_DISPATCH_PENDING] = {
        "dispatch_pending", 0, 0, 0, 0, 0
    },
    [PUBLIC_WL_DISPATCH_QUEUE_PENDING] = {
        "dispatch_queue_pending", 0, 0, 0, 0, 0
    },
    [PUBLIC_WL_FLUSH] = { "flush", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_ROUNDTRIP] = { "roundtrip", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_ROUNDTRIP_QUEUE] = { "roundtrip_queue", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_PROXY_ADD_LISTENER] = {
        "proxy_add_listener", 0, 0, 0, 0, 0
    },
    [PUBLIC_WL_PROXY_DESTROY] = { "proxy_destroy", 0, 0, 0, 0, 0 },
    [PUBLIC_WL_PROXY_GET_ID] = { "proxy_get_id", 0, 0, 0, 0, 0 },
};
static struct wl_fd_syscall_state wl_fd_syscall_ops[WL_FD_SYSCALL_OP_COUNT] = {
    [WL_FD_SYSCALL_POLL] = { "poll", 0, 0, 0 },
    [WL_FD_SYSCALL_PPOLL] = { "ppoll", 0, 0, 0 },
    [WL_FD_SYSCALL_READ] = { "read", 0, 0, 0 },
    [WL_FD_SYSCALL_WRITE] = { "write", 0, 0, 0 },
    [WL_FD_SYSCALL_RECVMSG] = { "recvmsg", 0, 0, 0 },
    [WL_FD_SYSCALL_SENDMSG] = { "sendmsg", 0, 0, 0 },
};
static int trace_enabled_cache = -1;
static int wayland_trace_enabled_cache = -1;
static int qt_trace_enabled_cache = -1;
static int qt_cpp_trace_enabled_cache = -1;
static int qt_bridge_trace_enabled_cache = -1;
static int loader_trace_enabled_cache = -1;
static int process_is_konsole_cache = -1;
static int wayland_threshold_us = (int)TRACE_THRESHOLD_US;
static int qt_threshold_us = (int)TRACE_THRESHOLD_US;
static int qt_cpp_threshold_us = (int)TRACE_THRESHOLD_US;
static unsigned long long output_limit = TRACE_OUTPUT_LIMIT_DEFAULT;
static unsigned long long output_lines;
static unsigned long long output_dropped;
static unsigned long long loader_output_limit =
    LOADER_TRACE_OUTPUT_LIMIT_DEFAULT;
static unsigned long long loader_output_lines;
static unsigned long long loader_output_dropped;
static unsigned long long loader_calls;
static unsigned long long loader_open_calls;
static unsigned long long loader_openat_calls;
static unsigned long long loader_dlopen_calls;
static unsigned long long loader_enoent_count;
static unsigned long long loader_total_elapsed_us;
static unsigned long long poll_calls;
static unsigned long long poll_slow;
static unsigned long long ppoll_calls;
static unsigned long long ppoll_slow;
static unsigned long long wl_calls;
static unsigned long long wl_slow;
static unsigned long long read_calls;
static unsigned long long write_calls;
static uint64_t max_poll_us;
static uint64_t max_wl_us;
static uint64_t max_write_to_poll_us;
static uint64_t max_poll_to_read_us;
static unsigned long long qt_fd3_ready_count;
static unsigned long long qt_fd3_ready_sequence;
static unsigned long long qt_fd3_ready_in_active_span;
static unsigned long long qt_fd3_ready_in_process_events;
static unsigned long long qt_fd3_ready_in_glib_iteration;
static unsigned long long qt_fd3_ready_in_send_wse;
static unsigned long long qt_span_ready_during_calls;
static unsigned long long qt_active_span_call_sequence;
static unsigned long long qt_glib_iteration_calls;
static unsigned long long qt_glib_pending_calls;
static unsigned long long qt_glib_wakeup_calls;
static unsigned long long qt_wl_main_calls;
static unsigned long long qt_wl_main_slow;
static unsigned long long qt_glib_slow;
static unsigned long long glib_phase_calls[GLIB_PHASE_OP_COUNT];
static unsigned long long glib_phase_slow[GLIB_PHASE_OP_COUNT];
static uint64_t glib_phase_total_us[GLIB_PHASE_OP_COUNT];
static uint64_t glib_phase_max_us[GLIB_PHASE_OP_COUNT];
static unsigned long long qt_fd3_ready_in_glib_poll;
static unsigned long long qt_fd3_ready_in_glib_dispatch;
static unsigned long long qt_fd3_ready_in_glib_other_phase;
static uint64_t qt_max_fd3_ready_glib_poll_age_us;
static uint64_t qt_max_fd3_ready_glib_dispatch_age_us;
static uint64_t qt_max_fd3_ready_glib_other_age_us;
static unsigned long long qt_cpp_total_calls;
static unsigned long long qt_cpp_total_slow;
static unsigned long long qt_bridge_enter_count;
static unsigned long long qt_bridge_exit_count;
static unsigned long long qt_bridge_process_events_enter_count;
static unsigned long long qt_bridge_process_events_exit_count;
static unsigned long long qt_bridge_glib_iteration_enter_count;
static unsigned long long qt_bridge_glib_iteration_exit_count;
static unsigned long long qt_bridge_poll_calls;
static unsigned long long qt_bridge_poll_slow;
static unsigned long long qt_bridge_poll_fd3_ready;
static unsigned long long qt_bridge_poll_fd3_ready_during;
static unsigned long long qt_bridge_poll_install_attempts;
static unsigned long long qt_bridge_poll_install_null_context;
static unsigned long long qt_bridge_poll_install_missing_get_poll_func;
static unsigned long long qt_bridge_poll_install_missing_set_poll_func;
static unsigned long long qt_bridge_poll_install_installed;
static unsigned long long qt_bridge_poll_install_already_wrapped;
static unsigned long long qt_bridge_poll_install_slot_full;
static uintptr_t qt_bridge_poll_install_last_context;
static int qt_bridge_poll_install_get_poll_func_missing;
static int qt_bridge_poll_install_set_poll_func_missing;
static uint64_t qt_bridge_max_ready_to_process_events_us;
static uint64_t qt_bridge_max_ready_to_glib_iteration_us;
static uint64_t qt_bridge_max_process_events_elapsed_us;
static uint64_t qt_bridge_max_glib_iteration_elapsed_us;
static uint64_t qt_bridge_public_wl_delta_max_us;
static uint64_t qt_bridge_wl_fd_syscall_delta_max_us;
static uint64_t qt_bridge_poll_max_elapsed_us;
static uint64_t qt_bridge_poll_max_fd3_ready_offset_us;
static unsigned long long public_wl_total_calls;
static unsigned long long public_wl_event_thread_calls;
static unsigned long long public_wl_main_thread_calls;
static unsigned long long wl_fd_syscall_total_calls;
static uint64_t qt_last_fd3_ready_us;
static uint64_t qt_max_ready_to_call_us;
static uint64_t qt_max_ready_to_glib_us;
static uint64_t qt_max_ready_to_wl_us;
static uint64_t qt_cpp_max_ready_to_call_us;
static uint64_t public_wl_max_elapsed_us;
static uint64_t public_wl_max_ready_us;
static uint64_t wl_fd_syscall_max_elapsed_us;
static uint64_t wl_fd_syscall_max_ready_us;
static uint64_t public_wl_last_call_us;
static uint64_t wl_fd_syscall_last_call_us;
static uint64_t qt_max_fd3_ready_span_age_us;
static uint64_t qt_max_span_ready_offset_us;
static unsigned int qt_max_fd3_ready_span_depth;
static uint64_t qt_max_glib_iteration_us;
static uint64_t qt_max_glib_pending_us;
static uint64_t qt_max_glib_wakeup_us;
static uint64_t qt_max_wl_main_us;
static long qt_last_fd3_ready_tid;
static int qt_last_fd3_ready_revents;
static int qt_last_fd3_ready_fionread;
static char qt_last_fd3_ready_comm[TRACE_COMM_LEN];
static int qt_active_span_op;
static unsigned int qt_active_span_depth;
static uint64_t qt_active_span_start_us;
static unsigned long long qt_active_span_call_id;
static long qt_active_span_tid;
static int qt_glib_phase_op;
static unsigned int qt_glib_phase_depth;
static uint64_t qt_glib_phase_start_us;
static unsigned long long qt_glib_phase_call_id;
static unsigned long long qt_glib_phase_call_sequence;
static long qt_glib_phase_tid;
static uintptr_t qt_glib_phase_context;
static __thread unsigned int active_span_depth_tls;
static __thread struct active_span_record active_span_stack[16];
static __thread unsigned int glib_phase_depth_tls;
static __thread struct glib_phase_record glib_phase_stack[16];
static struct bridge_poll_context bridge_poll_contexts[8];
static __thread GMainContext *bridge_poll_context_tls;
static __thread g_poll_fn_t bridge_poll_original_tls;
static __thread int bridge_poll_depth_tls;
static __thread int in_trace;

static uint64_t now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static int env_truthy(const char *name)
{
    const char *value = getenv(name);

    return value && value[0] && strcmp(value, "0") != 0 &&
        strcmp(value, "false") != 0 && strcmp(value, "FALSE") != 0 &&
        strcmp(value, "no") != 0 && strcmp(value, "NO") != 0 &&
        strcmp(value, "off") != 0 && strcmp(value, "OFF") != 0;
}

static int env_positive(const char *name)
{
    const char *value = getenv(name);

    return value && (strcmp(value, "1") == 0 ||
                     strcasecmp(value, "true") == 0 ||
                     strcasecmp(value, "yes") == 0 ||
                     strcasecmp(value, "on") == 0);
}

static int parse_env_ms(const char *name, int fallback_us)
{
    const char *value;
    char *end = NULL;
    long parsed;

    value = getenv(name);
    if (!value || !value[0])
        return fallback_us;
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno == 0 && end && *end == '\0' && parsed >= 1 &&
        parsed <= 10000)
        return (int)parsed * 1000;
    return fallback_us;
}

static unsigned long long parse_env_ull(const char *name,
                                        unsigned long long fallback,
                                        unsigned long long max)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long long parsed;

    if (!value || !value[0])
        return fallback;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno == 0 && end && *end == '\0' && parsed <= max)
        return parsed;
    return fallback;
}

static unsigned long long parse_env_limit(const char *name,
                                          unsigned long long fallback,
                                          unsigned long long max)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long long parsed;

    if (!value || !value[0] || value[0] == '-')
        return fallback;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno == 0 && end && *end == '\0' && parsed >= 1 &&
        parsed <= max)
        return parsed;
    return fallback;
}

static void configure_trace(void)
{
    if (trace_enabled_cache >= 0)
        return;
    wayland_trace_enabled_cache =
        env_truthy("KDE_APP_LAUNCH_PROBE_KONSOLE_WAYLAND_EVENT_TRACE") ? 1 : 0;
    qt_trace_enabled_cache =
        env_truthy("KDE_APP_LAUNCH_PROBE_KONSOLE_QT_EVENT_TRACE") ? 1 : 0;
    qt_cpp_trace_enabled_cache =
        env_truthy("KDE_APP_LAUNCH_PROBE_KONSOLE_QT_CPP_TRACE") ? 1 : 0;
    qt_bridge_trace_enabled_cache =
        (qt_trace_enabled_cache &&
         env_truthy("KDE_APP_LAUNCH_PROBE_KONSOLE_QT_BRIDGE_TRACE")) ? 1 : 0;
    loader_trace_enabled_cache =
        env_positive("KDE_APP_LAUNCH_PROBE_KONSOLE_LOADER_TRACE") ? 1 : 0;
    wayland_threshold_us = parse_env_ms(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_WAYLAND_EVENT_TRACE_MIN_MS",
        (int)TRACE_THRESHOLD_US);
    qt_threshold_us = parse_env_ms(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_QT_EVENT_TRACE_MIN_MS",
        (int)TRACE_THRESHOLD_US);
    qt_cpp_threshold_us = parse_env_ms(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_QT_CPP_TRACE_MIN_MS",
        (int)TRACE_THRESHOLD_US);
    output_limit = parse_env_ull(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_EVENT_TRACE_LIMIT",
        TRACE_OUTPUT_LIMIT_DEFAULT, 1000000ULL);
    output_limit = parse_env_ull(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_WAYLAND_EVENT_TRACE_LIMIT",
        output_limit, 1000000ULL);
    output_limit = parse_env_ull(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_QT_EVENT_TRACE_LIMIT",
        output_limit, 1000000ULL);
    output_limit = parse_env_ull(
        "KDE_APP_LAUNCH_PROBE_KONSOLE_QT_CPP_TRACE_LIMIT",
        output_limit, 1000000ULL);
    loader_output_limit = parse_env_limit(
        "KONSOLE_LOADER_TRACE_LIMIT",
        LOADER_TRACE_OUTPUT_LIMIT_DEFAULT, 1000000ULL);
    trace_enabled_cache =
        wayland_trace_enabled_cache || qt_trace_enabled_cache ||
        qt_cpp_trace_enabled_cache || qt_bridge_trace_enabled_cache ||
        loader_trace_enabled_cache;
}

static int trace_enabled(void)
{
    configure_trace();
    return trace_enabled_cache;
}

static int wayland_trace_enabled(void)
{
    configure_trace();
    return wayland_trace_enabled_cache;
}

static int qt_trace_enabled(void)
{
    configure_trace();
    return qt_trace_enabled_cache;
}

static int qt_cpp_trace_enabled(void)
{
    configure_trace();
    return qt_cpp_trace_enabled_cache;
}

static int qt_bridge_trace_enabled(void)
{
    configure_trace();
    return qt_bridge_trace_enabled_cache;
}

static int loader_trace_enabled(void)
{
    configure_trace();
    return loader_trace_enabled_cache;
}

static void sys_read_text(const char *path, char *buf, size_t size)
{
    int fd;
    ssize_t n;

    if (size == 0)
        return;
    buf[0] = '\0';
    fd = (int)syscall(SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    n = (ssize_t)syscall(SYS_read, fd, buf, size - 1);
    (void)syscall(SYS_close, fd);
    if (n <= 0)
        return;
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
}

static long trace_gettid(void)
{
    return (long)syscall(SYS_gettid);
}

static void thread_comm(char *buf, size_t size)
{
    char path[96];

    if (size == 0)
        return;
    snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", trace_gettid());
    sys_read_text(path, buf, size);
}

static int process_is_konsole(void)
{
    char comm[TRACE_COMM_LEN];
    char cmdline[256];

    if (process_is_konsole_cache >= 0)
        return process_is_konsole_cache;
    process_is_konsole_cache = 0;
    sys_read_text("/proc/self/comm", comm, sizeof(comm));
    if (strstr(comm, "konsole")) {
        process_is_konsole_cache = 1;
        return process_is_konsole_cache;
    }
    sys_read_text("/proc/self/cmdline", cmdline, sizeof(cmdline));
    if (strstr(cmdline, "konsole"))
        process_is_konsole_cache = 1;
    return process_is_konsole_cache;
}

static void trace_write_line_impl(int force, const char *fmt, va_list ap)
{
    char buf[2048];
    int n;
    unsigned long long line_no;

    if (!trace_enabled() || in_trace)
        return;
    if (!force && output_limit > 0) {
        line_no = __sync_add_and_fetch(&output_lines, 1);
        if (line_no > output_limit) {
            __sync_add_and_fetch(&output_dropped, 1);
            return;
        }
    }
    in_trace = 1;
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) {
        if ((size_t)n >= sizeof(buf))
            n = (int)sizeof(buf) - 1;
        buf[n++] = '\n';
        (void)syscall(SYS_write, STDERR_FILENO, buf, (size_t)n);
    }
    in_trace = 0;
}

static void trace_write_line(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    trace_write_line_impl(0, fmt, ap);
    va_end(ap);
}

static void trace_write_line_force(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    trace_write_line_impl(1, fmt, ap);
    va_end(ap);
}

static void loader_escape(char *out, size_t out_size, const char *in)
{
    static const char hex[] = "0123456789abcdef";
    size_t off = 0;

    if (out_size == 0)
        return;
    if (!in)
        in = "(null)";
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        unsigned char c = *p;

        if (c == '\\' || c == '"') {
            if (off + 2 >= out_size)
                break;
            out[off++] = '\\';
            out[off++] = (char)c;
        } else if (c >= 0x20 && c <= 0x7e) {
            if (off + 1 >= out_size)
                break;
            out[off++] = (char)c;
        } else {
            if (off + 4 >= out_size)
                break;
            out[off++] = '\\';
            out[off++] = 'x';
            out[off++] = hex[c >> 4];
            out[off++] = hex[c & 0xf];
        }
    }
    out[off] = '\0';
}

static const char *loader_env_value(const char *name, const char *fallback)
{
    const char *value = getenv(name);

    return value && value[0] ? value : fallback;
}

static void loader_trace_write_line_impl(int force, const char *fmt, va_list ap)
{
    char buf[2048];
    int n;
    unsigned long long line_no;

    if (!loader_trace_enabled() || !process_is_konsole() || in_trace)
        return;
    if (!force && loader_output_limit > 0) {
        line_no = __sync_add_and_fetch(&loader_output_lines, 1);
        if (line_no > loader_output_limit) {
            __sync_add_and_fetch(&loader_output_dropped, 1);
            return;
        }
    }
    in_trace = 1;
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) {
        if ((size_t)n >= sizeof(buf))
            n = (int)sizeof(buf) - 1;
        buf[n++] = '\n';
        (void)syscall(SYS_write, STDERR_FILENO, buf, (size_t)n);
    }
    in_trace = 0;
}

static void loader_trace_write_line(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    loader_trace_write_line_impl(0, fmt, ap);
    va_end(ap);
}

static void loader_trace_write_line_force(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    loader_trace_write_line_impl(1, fmt, ap);
    va_end(ap);
}

static int open_flags_need_mode(int flags)
{
    if (flags & O_CREAT)
        return 1;
#ifdef O_TMPFILE
    if ((flags & O_TMPFILE) == O_TMPFILE)
        return 1;
#endif
    return 0;
}

static void resolve_loader_symbols(void)
{
    int saved_in_trace = in_trace;

    if (in_trace)
        return;
    in_trace = 1;
    if (!real_open)
        real_open = (open_fn_t)dlsym(RTLD_NEXT, "open");
    if (!real_open64)
        real_open64 = (open_fn_t)dlsym(RTLD_NEXT, "open64");
    if (!real_openat)
        real_openat = (openat_fn_t)dlsym(RTLD_NEXT, "openat");
    if (!real_openat64)
        real_openat64 = (openat_fn_t)dlsym(RTLD_NEXT, "openat64");
    if (!real_dlopen)
        real_dlopen = (dlopen_fn_t)dlsym(RTLD_NEXT, "dlopen");
    in_trace = saved_in_trace;
}

static void loader_record_path_event(const char *event, int dirfd,
                                     const char *path, int flags,
                                     long long rc, int err,
                                     uint64_t elapsed_us)
{
    char escaped[512];

    if (!loader_trace_enabled() || !process_is_konsole())
        return;
    loader_escape(escaped, sizeof(escaped), path);
    __sync_add_and_fetch(&loader_calls, 1);
    if (strcmp(event, "openat") == 0)
        __sync_add_and_fetch(&loader_openat_calls, 1);
    else
        __sync_add_and_fetch(&loader_open_calls, 1);
    if (err == ENOENT)
        __sync_add_and_fetch(&loader_enoent_count, 1);
    __sync_add_and_fetch(&loader_total_elapsed_us,
                         (unsigned long long)elapsed_us);
    loader_trace_write_line(
        "konsole-loader-trace: event=%s pid=%ld tid=%ld "
        "launch_index=%s launch_temperature=%s repeat_count=%s dirfd=%d "
        "flags=0x%x path=\"%s\" rc=%lld errno=%d elapsed_us=%llu",
        event, (long)getpid(), trace_gettid(),
        loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_INDEX", "missing"),
        loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_TEMPERATURE", "missing"),
        loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_REPEAT_COUNT", "missing"),
        dirfd, flags, escaped, rc, err,
        (unsigned long long)elapsed_us);
}

static void loader_record_dlopen(const char *filename, int flags,
                                 void *handle, int err,
                                 uint64_t elapsed_us)
{
    char escaped[512];

    if (!loader_trace_enabled() || !process_is_konsole())
        return;
    loader_escape(escaped, sizeof(escaped), filename);
    __sync_add_and_fetch(&loader_calls, 1);
    __sync_add_and_fetch(&loader_dlopen_calls, 1);
    if (!handle && err == ENOENT)
        __sync_add_and_fetch(&loader_enoent_count, 1);
    __sync_add_and_fetch(&loader_total_elapsed_us,
                         (unsigned long long)elapsed_us);
    loader_trace_write_line(
        "konsole-loader-trace: event=dlopen pid=%ld tid=%ld "
        "launch_index=%s launch_temperature=%s repeat_count=%s flags=0x%x "
        "path=\"%s\" rc=%p errno=%d elapsed_us=%llu",
        (long)getpid(), trace_gettid(),
        loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_INDEX", "missing"),
        loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_TEMPERATURE", "missing"),
        loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_REPEAT_COUNT", "missing"),
        flags, escaped, handle, handle ? 0 : err,
        (unsigned long long)elapsed_us);
}

static void fd_target(int fd, char *buf, size_t size)
{
    char path[64];
    ssize_t n;

    if (size == 0)
        return;
    buf[0] = '\0';
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    n = (ssize_t)syscall(SYS_readlink, path, buf, size - 1);
    if (n < 0) {
        snprintf(buf, size, "readlink_errno:%d", errno);
        return;
    }
    buf[n] = '\0';
}

static int fd_fionread(int fd)
{
    int pending = -1;

    if (syscall(SYS_ioctl, fd, FIONREAD, &pending) < 0)
        return -1;
    return pending;
}

static int target_is_pipe(const char *target)
{
    return strncmp(target, "pipe:", 5) == 0 ||
        strncmp(target, "pipe[", 5) == 0 || strstr(target, "pipe:") != NULL;
}

static int target_is_socket(const char *target)
{
    return strncmp(target, "socket:", 7) == 0 ||
        strncmp(target, "socket[", 7) == 0 || strstr(target, "socket:") != NULL;
}

static struct fd_state *fd_state_for(int fd)
{
    if (fd < 0 || fd >= TRACE_MAX_FD)
        return NULL;
    return &fd_states[fd];
}

static struct pipe_state *pipe_state_for_target(const char *target, int create)
{
    int free_slot = -1;

    if (!target || !target[0])
        return NULL;
    for (int i = 0; i < TRACE_MAX_PIPE; i++) {
        if (pipes[i].target[0] &&
            strncmp(pipes[i].target, target, sizeof(pipes[i].target)) == 0)
            return &pipes[i];
        if (!pipes[i].target[0] && free_slot < 0)
            free_slot = i;
    }
    if (!create || free_slot < 0)
        return NULL;
    snprintf(pipes[free_slot].target, sizeof(pipes[free_slot].target), "%s",
             target);
    return &pipes[free_slot];
}

static void mark_fd_target(int fd, const char *target, int is_wayland)
{
    struct fd_state *st = fd_state_for(fd);

    if (!st)
        return;
    st->interesting = 1;
    st->is_wayland = st->is_wayland || is_wayland;
    st->is_pipe = st->is_pipe || target_is_pipe(target);
    snprintf(st->target, sizeof(st->target), "%s", target ? target : "");
    if (st->is_pipe)
        (void)pipe_state_for_target(st->target, 1);
}

static void remember_display_fd(const struct wl_display *display, int fd)
{
    int free_slot = -1;
    char target[TRACE_TARGET_LEN];

    if (!display || fd < 0)
        return;
    for (int i = 0; i < TRACE_MAX_DISPLAY; i++) {
        if (displays[i].display == display) {
            displays[i].fd = fd;
            goto done;
        }
        if (!displays[i].display && free_slot < 0)
            free_slot = i;
    }
    if (free_slot >= 0) {
        displays[free_slot].display = display;
        displays[free_slot].fd = fd;
    }

done:
    fd_target(fd, target, sizeof(target));
    mark_fd_target(fd, target, 1);
}

static int display_fd(const struct wl_display *display)
{
    if (!display)
        return -1;
    for (int i = 0; i < TRACE_MAX_DISPLAY; i++) {
        if (displays[i].display == display)
            return displays[i].fd;
    }
    return -1;
}

static int fd_is_tracked_wayland(int fd)
{
    struct fd_state *st = fd_state_for(fd);

    return st && st->is_wayland;
}

static void return_address_info(void *return_address, const char **module,
                                const char **symbol)
{
    Dl_info info;

    *module = "missing";
    *symbol = "missing";
    if (!return_address || dladdr(return_address, &info) == 0)
        return;
    if (info.dli_fname && info.dli_fname[0])
        *module = info.dli_fname;
    if (info.dli_sname && info.dli_sname[0])
        *symbol = info.dli_sname;
}

static void *qwayland_private_wl_display(void *self)
{
    if (!self)
        return NULL;
    return *(void **)((char *)self + 0x20);
}

static int should_trace_thread(const char *comm)
{
    return strstr(comm, "WaylandEventThr") || strstr(comm, "konsole") ||
        strstr(comm, "Konsole") || strstr(comm, "QDBusConnection");
}

static int is_main_konsole_thread(const char *comm)
{
    return comm && (strcmp(comm, "konsole") == 0 ||
                    strcmp(comm, "Konsole") == 0 ||
                    strstr(comm, "konsole") != NULL ||
                    strstr(comm, "Konsole") != NULL);
}

static int span_tracking_enabled(void)
{
    return qt_trace_enabled() || qt_cpp_trace_enabled();
}

static int glib_trace_enabled(void)
{
    return qt_trace_enabled() || qt_cpp_trace_enabled();
}

static const char *active_span_name(enum active_span_op_id op_id)
{
    switch (op_id) {
    case ACTIVE_SPAN_PROCESS_EVENTS:
        return "process_events";
    case ACTIVE_SPAN_GLIB_ITERATION:
        return "glib_iteration";
    case ACTIVE_SPAN_SEND_WINDOW_SYSTEM_EVENTS:
        return "send_window_system_events";
    default:
        return "none";
    }
}

static const char *glib_phase_name(enum glib_phase_op_id op_id)
{
    switch (op_id) {
    case GLIB_PHASE_PREPARE:
        return "prepare";
    case GLIB_PHASE_QUERY:
        return "query";
    case GLIB_PHASE_CHECK:
        return "check";
    case GLIB_PHASE_DISPATCH:
        return "dispatch";
    case GLIB_PHASE_POLL:
        return "poll";
    default:
        return "none";
    }
}

static void publish_active_span(const struct active_span_record *span)
{
    if (!span || !span->active) {
        __atomic_store_n(&qt_active_span_start_us, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_active_span_call_id, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_active_span_tid, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_active_span_depth, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_active_span_op, ACTIVE_SPAN_NONE,
                         __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&qt_active_span_start_us, span->start_us,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&qt_active_span_call_id, span->call_id,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&qt_active_span_tid, trace_gettid(), __ATOMIC_RELEASE);
    __atomic_store_n(&qt_active_span_depth, span->depth, __ATOMIC_RELEASE);
    __atomic_store_n(&qt_active_span_op, (int)span->op_id, __ATOMIC_RELEASE);
}

static void restore_previous_active_span(void)
{
    if (active_span_depth_tls == 0) {
        publish_active_span(NULL);
        return;
    }
    publish_active_span(&active_span_stack[active_span_depth_tls - 1]);
}

static void publish_glib_phase(const struct glib_phase_record *phase)
{
    if (!phase || !phase->active) {
        __atomic_store_n(&qt_glib_phase_start_us, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_glib_phase_call_id, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_glib_phase_tid, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_glib_phase_depth, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&qt_glib_phase_context, (uintptr_t)0,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&qt_glib_phase_op, GLIB_PHASE_NONE,
                         __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&qt_glib_phase_start_us, phase->start_us,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&qt_glib_phase_call_id, phase->call_id,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&qt_glib_phase_tid, trace_gettid(), __ATOMIC_RELEASE);
    __atomic_store_n(&qt_glib_phase_depth, phase->depth, __ATOMIC_RELEASE);
    __atomic_store_n(&qt_glib_phase_context, (uintptr_t)phase->context,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&qt_glib_phase_op, (int)phase->op_id, __ATOMIC_RELEASE);
}

static void restore_previous_glib_phase(void)
{
    if (glib_phase_depth_tls == 0) {
        publish_glib_phase(NULL);
        return;
    }
    publish_glib_phase(&glib_phase_stack[glib_phase_depth_tls - 1]);
}

static struct glib_phase_record glib_phase_enter(enum glib_phase_op_id op_id,
                                                 GMainContext *context,
                                                 uint64_t begin)
{
    struct glib_phase_record phase;
    char comm[TRACE_COMM_LEN];

    memset(&phase, 0, sizeof(phase));
    phase.op_id = op_id;
    phase.context = context;
    if (op_id == GLIB_PHASE_NONE || !glib_trace_enabled() ||
        !process_is_konsole())
        return phase;
    thread_comm(comm, sizeof(comm));
    if (!is_main_konsole_thread(comm))
        return phase;
    phase.active = 1;
    phase.start_us = begin;
    phase.depth = glib_phase_depth_tls + 1;
    phase.call_id = __sync_add_and_fetch(&qt_glib_phase_call_sequence, 1);
    phase.ready_seq_at_enter =
        __atomic_load_n(&qt_fd3_ready_sequence, __ATOMIC_ACQUIRE);
    if (glib_phase_depth_tls <
        sizeof(glib_phase_stack) / sizeof(glib_phase_stack[0])) {
        glib_phase_stack[glib_phase_depth_tls++] = phase;
        phase.pushed = 1;
        glib_phase_stack[glib_phase_depth_tls - 1].pushed = 1;
    }
    publish_glib_phase(&phase);
    return phase;
}

static void glib_phase_exit(const struct glib_phase_record *phase,
                            uint64_t end, int *ready_during,
                            uint64_t *ready_offset_us,
                            unsigned long long *ready_seq)
{
    unsigned long long current_seq;
    uint64_t ready_us;

    if (ready_during)
        *ready_during = 0;
    if (ready_offset_us)
        *ready_offset_us = 0;
    if (ready_seq)
        *ready_seq = 0;
    if (!phase || !phase->active)
        return;
    current_seq = __atomic_load_n(&qt_fd3_ready_sequence, __ATOMIC_ACQUIRE);
    ready_us = __atomic_load_n(&qt_last_fd3_ready_us, __ATOMIC_ACQUIRE);
    if (ready_seq)
        *ready_seq = current_seq;
    if (current_seq > phase->ready_seq_at_enter &&
        ready_us >= phase->start_us && ready_us <= end) {
        uint64_t offset = ready_us - phase->start_us;

        if (ready_during)
            *ready_during = 1;
        if (ready_offset_us)
            *ready_offset_us = offset;
    }
    if (phase->pushed && glib_phase_depth_tls > 0)
        glib_phase_depth_tls--;
    restore_previous_glib_phase();
}

static struct active_span_record active_span_enter(enum active_span_op_id op_id,
                                                  uint64_t begin)
{
    struct active_span_record span;
    char comm[TRACE_COMM_LEN];

    memset(&span, 0, sizeof(span));
    span.op_id = op_id;
    if (op_id == ACTIVE_SPAN_NONE || !span_tracking_enabled() ||
        !process_is_konsole())
        return span;
    thread_comm(comm, sizeof(comm));
    if (!is_main_konsole_thread(comm))
        return span;
    span.active = 1;
    span.start_us = begin;
    span.depth = active_span_depth_tls + 1;
    span.call_id = __sync_add_and_fetch(&qt_active_span_call_sequence, 1);
    span.ready_seq_at_enter =
        __atomic_load_n(&qt_fd3_ready_sequence, __ATOMIC_ACQUIRE);
    if (active_span_depth_tls <
        sizeof(active_span_stack) / sizeof(active_span_stack[0])) {
        active_span_stack[active_span_depth_tls++] = span;
        span.pushed = 1;
        active_span_stack[active_span_depth_tls - 1].pushed = 1;
    }
    publish_active_span(&span);
    return span;
}

static void active_span_exit(const struct active_span_record *span,
                             uint64_t end, int *ready_during,
                             uint64_t *ready_offset_us,
                             unsigned long long *ready_seq)
{
    unsigned long long current_seq;
    uint64_t ready_us;

    if (ready_during)
        *ready_during = 0;
    if (ready_offset_us)
        *ready_offset_us = 0;
    if (ready_seq)
        *ready_seq = 0;
    if (!span || !span->active)
        return;
    current_seq = __atomic_load_n(&qt_fd3_ready_sequence, __ATOMIC_ACQUIRE);
    ready_us = __atomic_load_n(&qt_last_fd3_ready_us, __ATOMIC_ACQUIRE);
    if (ready_seq)
        *ready_seq = current_seq;
    if (current_seq > span->ready_seq_at_enter &&
        ready_us >= span->start_us && ready_us <= end) {
        uint64_t offset = ready_us - span->start_us;

        if (ready_during)
            *ready_during = 1;
        if (ready_offset_us)
            *ready_offset_us = offset;
        __sync_add_and_fetch(&qt_span_ready_during_calls, 1);
        if (offset > qt_max_span_ready_offset_us)
            qt_max_span_ready_offset_us = offset;
    }
    if (span->pushed && active_span_depth_tls > 0)
        active_span_depth_tls--;
    restore_previous_active_span();
}

static uint64_t ready_to_call_gap(uint64_t begin)
{
    uint64_t ready = __atomic_load_n(&qt_last_fd3_ready_us, __ATOMIC_ACQUIRE);

    if (!ready || begin < ready)
        return 0;
    return begin - ready;
}

static void update_ready_to_call_max(uint64_t gap)
{
    if (gap > qt_max_ready_to_call_us)
        qt_max_ready_to_call_us = gap;
}

static gint qt_bridge_poll_func(GPollFD *fds, guint nfds, gint timeout);

static void bridge_delta_values(uint64_t end, uint64_t *public_wl_delta,
                                uint64_t *wl_fd_syscall_delta)
{
    uint64_t public_wl_last;
    uint64_t wl_fd_last;

    public_wl_last =
        __atomic_load_n(&public_wl_last_call_us, __ATOMIC_ACQUIRE);
    wl_fd_last =
        __atomic_load_n(&wl_fd_syscall_last_call_us, __ATOMIC_ACQUIRE);
    if (public_wl_delta)
        *public_wl_delta = public_wl_last && end >= public_wl_last ?
            end - public_wl_last : 0;
    if (wl_fd_syscall_delta)
        *wl_fd_syscall_delta = wl_fd_last && end >= wl_fd_last ?
            end - wl_fd_last : 0;
}

static int qt_bridge_trace_enter(const char *op,
                                 const struct active_span_record *span,
                                 uint64_t begin, int flags,
                                 GMainContext *context, int may_block,
                                 int has_context)
{
    uint64_t ready_gap;
    char comm[TRACE_COMM_LEN];

    if (!qt_bridge_trace_enabled() || !span || !span->active)
        return 0;
    ready_gap = ready_to_call_gap(begin);
    if (strcmp(op, "process_events") == 0) {
        if (ready_gap > qt_bridge_max_ready_to_process_events_us)
            qt_bridge_max_ready_to_process_events_us = ready_gap;
    } else if (strcmp(op, "glib_iteration") == 0) {
        if (ready_gap > qt_bridge_max_ready_to_glib_iteration_us)
            qt_bridge_max_ready_to_glib_iteration_us = ready_gap;
    }
    if (ready_gap < (uint64_t)qt_threshold_us)
        return 0;
    thread_comm(comm, sizeof(comm));
    __sync_add_and_fetch(&qt_bridge_enter_count, 1);
    if (strcmp(op, "process_events") == 0)
        __sync_add_and_fetch(&qt_bridge_process_events_enter_count, 1);
    else if (strcmp(op, "glib_iteration") == 0)
        __sync_add_and_fetch(&qt_bridge_glib_iteration_enter_count, 1);
    if (has_context) {
        trace_write_line("konsole-qt-event-trace: bridge-enter "
                         "op=%s tid=%ld comm=%s call_id=%llu "
                         "context=%p may_block=%d ready_to_call_us=%llu "
                         "ready_seq=%llu ready_tid=%ld last_revents=0x%x "
                         "last_fionread=%d active_span_depth=%u",
                         op, trace_gettid(), comm, span->call_id,
                         (void *)context, may_block,
                         (unsigned long long)ready_gap,
                         qt_fd3_ready_sequence, qt_last_fd3_ready_tid,
                         qt_last_fd3_ready_revents,
                         qt_last_fd3_ready_fionread, span->depth);
    } else {
        trace_write_line("konsole-qt-event-trace: bridge-enter "
                         "op=%s tid=%ld comm=%s call_id=%llu flags=0x%x "
                         "ready_to_call_us=%llu ready_seq=%llu "
                         "ready_tid=%ld last_revents=0x%x "
                         "last_fionread=%d active_span_depth=%u",
                         op, trace_gettid(), comm, span->call_id, flags,
                         (unsigned long long)ready_gap,
                         qt_fd3_ready_sequence, qt_last_fd3_ready_tid,
                         qt_last_fd3_ready_revents,
                         qt_last_fd3_ready_fionread, span->depth);
    }
    return 1;
}

static void qt_bridge_trace_exit(const char *op,
                                 const struct active_span_record *span,
                                 int enter_emitted, uint64_t begin,
                                 uint64_t end, int ret, int flags,
                                 GMainContext *context, int may_block,
                                 int has_context, int ready_during_span,
                                 uint64_t ready_offset_us)
{
    uint64_t elapsed;
    uint64_t ready_gap;
    uint64_t public_wl_delta;
    uint64_t wl_fd_syscall_delta;
    char comm[TRACE_COMM_LEN];

    if (!qt_bridge_trace_enabled() || !span || !span->active)
        return;
    elapsed = end >= begin ? end - begin : 0;
    ready_gap = ready_to_call_gap(begin);
    bridge_delta_values(end, &public_wl_delta, &wl_fd_syscall_delta);
    if (public_wl_delta > qt_bridge_public_wl_delta_max_us)
        qt_bridge_public_wl_delta_max_us = public_wl_delta;
    if (wl_fd_syscall_delta > qt_bridge_wl_fd_syscall_delta_max_us)
        qt_bridge_wl_fd_syscall_delta_max_us = wl_fd_syscall_delta;
    if (strcmp(op, "process_events") == 0) {
        if (elapsed > qt_bridge_max_process_events_elapsed_us)
            qt_bridge_max_process_events_elapsed_us = elapsed;
    } else if (strcmp(op, "glib_iteration") == 0) {
        if (elapsed > qt_bridge_max_glib_iteration_elapsed_us)
            qt_bridge_max_glib_iteration_elapsed_us = elapsed;
    }
    if (!enter_emitted && elapsed < (uint64_t)qt_threshold_us &&
        !ready_during_span)
        return;
    thread_comm(comm, sizeof(comm));
    __sync_add_and_fetch(&qt_bridge_exit_count, 1);
    if (strcmp(op, "process_events") == 0)
        __sync_add_and_fetch(&qt_bridge_process_events_exit_count, 1);
    else if (strcmp(op, "glib_iteration") == 0)
        __sync_add_and_fetch(&qt_bridge_glib_iteration_exit_count, 1);
    if (has_context) {
        trace_write_line("konsole-qt-event-trace: bridge-exit "
                         "op=%s tid=%ld comm=%s call_id=%llu context=%p "
                         "may_block=%d ret=%d elapsed_us=%llu "
                         "ready_to_call_us=%llu fd3_ready_during_span=%d "
                         "fd3_ready_offset_us=%llu public_wl_delta_us=%llu "
                         "wl_fd_syscall_delta_us=%llu ready_seq=%llu "
                         "last_revents=0x%x last_fionread=%d "
                         "active_span_depth=%u",
                         op, trace_gettid(), comm, span->call_id,
                         (void *)context, may_block, ret,
                         (unsigned long long)elapsed,
                         (unsigned long long)ready_gap, ready_during_span,
                         (unsigned long long)ready_offset_us,
                         (unsigned long long)public_wl_delta,
                         (unsigned long long)wl_fd_syscall_delta,
                         qt_fd3_ready_sequence, qt_last_fd3_ready_revents,
                         qt_last_fd3_ready_fionread, span->depth);
    } else {
        trace_write_line("konsole-qt-event-trace: bridge-exit "
                         "op=%s tid=%ld comm=%s call_id=%llu flags=0x%x "
                         "ret=%d elapsed_us=%llu ready_to_call_us=%llu "
                         "fd3_ready_during_span=%d "
                         "fd3_ready_offset_us=%llu public_wl_delta_us=%llu "
                         "wl_fd_syscall_delta_us=%llu ready_seq=%llu "
                         "last_revents=0x%x last_fionread=%d "
                         "active_span_depth=%u",
                         op, trace_gettid(), comm, span->call_id, flags, ret,
                         (unsigned long long)elapsed,
                         (unsigned long long)ready_gap, ready_during_span,
                         (unsigned long long)ready_offset_us,
                         (unsigned long long)public_wl_delta,
                         (unsigned long long)wl_fd_syscall_delta,
                         qt_fd3_ready_sequence, qt_last_fd3_ready_revents,
                         qt_last_fd3_ready_fionread, span->depth);
    }
}

static int qt_should_log_wl_op(const char *name)
{
    return strstr(name, "dispatch") != NULL ||
        strcmp(name, "read_events") == 0 ||
        strcmp(name, "flush") == 0 ||
        strstr(name, "roundtrip") != NULL;
}

static void record_qt_fd3_ready(const char *op, const char *comm,
                                const struct pollfd *pfd, int ret,
                                uint64_t begin, uint64_t end, int fion,
                                const char *target)
{
    unsigned long long seq;
    uint64_t elapsed = end >= begin ? end - begin : 0;
    int span_op;
    uint64_t span_start;
    unsigned int span_depth;
    unsigned long long span_call_id;
    long span_tid;
    int span_active = 0;
    uint64_t span_age = 0;
    int glib_phase_op;
    uint64_t glib_phase_start;
    unsigned int glib_phase_depth;
    unsigned long long glib_phase_call_id;
    long glib_phase_tid;
    uintptr_t glib_phase_context;
    int glib_phase_active = 0;
    uint64_t glib_phase_age = 0;
    int allow_main_poll_sample;

    allow_main_poll_sample =
        strcmp(op, "g_poll") == 0 || strcmp(op, "bridge_poll") == 0;
    if ((!qt_trace_enabled() && !qt_cpp_trace_enabled()) ||
        pfd->fd != 3 || ret <= 0 ||
        pfd->revents == 0 || fion <= 0 ||
        (!allow_main_poll_sample && !strstr(comm, "WaylandEventThr")))
        return;
    seq = __sync_add_and_fetch(&qt_fd3_ready_sequence, 1);
    qt_fd3_ready_count++;
    __atomic_store_n(&qt_last_fd3_ready_us, end, __ATOMIC_RELEASE);
    qt_last_fd3_ready_tid = trace_gettid();
    qt_last_fd3_ready_revents = pfd->revents;
    qt_last_fd3_ready_fionread = fion;
    snprintf(qt_last_fd3_ready_comm, sizeof(qt_last_fd3_ready_comm), "%s",
             comm);
    span_op = __atomic_load_n(&qt_active_span_op, __ATOMIC_ACQUIRE);
    span_start = __atomic_load_n(&qt_active_span_start_us, __ATOMIC_ACQUIRE);
    span_depth = __atomic_load_n(&qt_active_span_depth, __ATOMIC_ACQUIRE);
    span_call_id =
        __atomic_load_n(&qt_active_span_call_id, __ATOMIC_ACQUIRE);
    span_tid = __atomic_load_n(&qt_active_span_tid, __ATOMIC_ACQUIRE);
    if (span_op > ACTIVE_SPAN_NONE && span_op < ACTIVE_SPAN_OP_COUNT &&
        span_start && end >= span_start) {
        span_active = 1;
        span_age = end - span_start;
        __sync_add_and_fetch(&qt_fd3_ready_in_active_span, 1);
        if (span_op == ACTIVE_SPAN_PROCESS_EVENTS)
            __sync_add_and_fetch(&qt_fd3_ready_in_process_events, 1);
        else if (span_op == ACTIVE_SPAN_GLIB_ITERATION)
            __sync_add_and_fetch(&qt_fd3_ready_in_glib_iteration, 1);
        else if (span_op == ACTIVE_SPAN_SEND_WINDOW_SYSTEM_EVENTS)
            __sync_add_and_fetch(&qt_fd3_ready_in_send_wse, 1);
        if (span_age > qt_max_fd3_ready_span_age_us)
            qt_max_fd3_ready_span_age_us = span_age;
        if (span_depth > qt_max_fd3_ready_span_depth)
            qt_max_fd3_ready_span_depth = span_depth;
    }
    glib_phase_op = __atomic_load_n(&qt_glib_phase_op, __ATOMIC_ACQUIRE);
    glib_phase_start =
        __atomic_load_n(&qt_glib_phase_start_us, __ATOMIC_ACQUIRE);
    glib_phase_depth =
        __atomic_load_n(&qt_glib_phase_depth, __ATOMIC_ACQUIRE);
    glib_phase_call_id =
        __atomic_load_n(&qt_glib_phase_call_id, __ATOMIC_ACQUIRE);
    glib_phase_tid = __atomic_load_n(&qt_glib_phase_tid, __ATOMIC_ACQUIRE);
    glib_phase_context =
        __atomic_load_n(&qt_glib_phase_context, __ATOMIC_ACQUIRE);
    if (glib_phase_op > GLIB_PHASE_NONE &&
        glib_phase_op < GLIB_PHASE_OP_COUNT && glib_phase_start &&
        end >= glib_phase_start) {
        glib_phase_active = 1;
        glib_phase_age = end - glib_phase_start;
        if (glib_phase_op == GLIB_PHASE_POLL) {
            __sync_add_and_fetch(&qt_fd3_ready_in_glib_poll, 1);
            if (glib_phase_age > qt_max_fd3_ready_glib_poll_age_us)
                qt_max_fd3_ready_glib_poll_age_us = glib_phase_age;
        } else if (glib_phase_op == GLIB_PHASE_DISPATCH) {
            __sync_add_and_fetch(&qt_fd3_ready_in_glib_dispatch, 1);
            if (glib_phase_age > qt_max_fd3_ready_glib_dispatch_age_us)
                qt_max_fd3_ready_glib_dispatch_age_us = glib_phase_age;
        } else {
            __sync_add_and_fetch(&qt_fd3_ready_in_glib_other_phase, 1);
            if (glib_phase_age > qt_max_fd3_ready_glib_other_age_us)
                qt_max_fd3_ready_glib_other_age_us = glib_phase_age;
        }
    } else if (span_active && span_op == ACTIVE_SPAN_GLIB_ITERATION) {
        __sync_add_and_fetch(&qt_fd3_ready_in_glib_other_phase, 1);
        if (span_age > qt_max_fd3_ready_glib_other_age_us)
            qt_max_fd3_ready_glib_other_age_us = span_age;
    }
    if (qt_trace_enabled()) {
        trace_write_line("konsole-qt-event-trace: fd3-ready seq=%llu "
                         "poll_op=%s tid=%ld comm=%s fd=3 revents=0x%x "
                         "fionread=%d poll_ret=%d elapsed_us=%llu "
                         "main_span_active=%d main_span_op=%s "
                         "main_span_age_us=%llu main_span_depth=%u "
                         "main_span_call_id=%llu main_span_tid=%ld "
                         "main_glib_phase_active=%d main_glib_phase=%s "
                         "main_glib_phase_age_us=%llu "
                         "main_glib_phase_depth=%u "
                         "main_glib_phase_call_id=%llu "
                         "main_glib_phase_tid=%ld "
                         "main_glib_phase_context=%p "
                         "target=\"%s\"",
                         seq, op, trace_gettid(), comm, pfd->revents, fion,
                         ret, (unsigned long long)elapsed,
                         span_active,
                         active_span_name((enum active_span_op_id)span_op),
                         (unsigned long long)span_age, span_depth,
                         span_call_id, span_tid,
                         glib_phase_active,
                         glib_phase_name((enum glib_phase_op_id)
                                         glib_phase_op),
                         (unsigned long long)glib_phase_age,
                         glib_phase_depth, glib_phase_call_id,
                         glib_phase_tid, (void *)glib_phase_context,
                         target ? target : "");
    }
    if (qt_cpp_trace_enabled()) {
        trace_write_line("konsole-qt-cpp-trace: fd3-ready seq=%llu "
                         "poll_op=%s tid=%ld comm=%s fd=3 revents=0x%x "
                         "fionread=%d poll_ret=%d elapsed_us=%llu "
                         "main_span_active=%d main_span_op=%s "
                         "main_span_age_us=%llu main_span_depth=%u "
                         "main_span_call_id=%llu main_span_tid=%ld "
                         "main_glib_phase_active=%d main_glib_phase=%s "
                         "main_glib_phase_age_us=%llu "
                         "main_glib_phase_depth=%u "
                         "main_glib_phase_call_id=%llu "
                         "main_glib_phase_tid=%ld "
                         "main_glib_phase_context=%p "
                         "target=\"%s\"",
                         seq, op, trace_gettid(), comm, pfd->revents, fion,
                         ret, (unsigned long long)elapsed,
                         span_active,
                         active_span_name((enum active_span_op_id)span_op),
                         (unsigned long long)span_age, span_depth,
                         span_call_id, span_tid,
                         glib_phase_active,
                         glib_phase_name((enum glib_phase_op_id)
                                         glib_phase_op),
                         (unsigned long long)glib_phase_age,
                         glib_phase_depth, glib_phase_call_id,
                         glib_phase_tid, (void *)glib_phase_context,
                         target ? target : "");
    }
}

static int pollfd_interesting(const struct pollfd *pfd, char *target,
                              size_t target_size, int *fion)
{
    struct fd_state *st;

    if (!pfd || pfd->fd < 0)
        return 0;
    fd_target(pfd->fd, target, target_size);
    if (fion)
        *fion = fd_fionread(pfd->fd);
    st = fd_state_for(pfd->fd);
    if (st && st->interesting) {
        if (target && target[0])
            mark_fd_target(pfd->fd, target, st->is_wayland);
        return 1;
    }
    if (pfd->fd == 3 || pfd->fd == 9 || target_is_pipe(target) ||
        target_is_socket(target)) {
        mark_fd_target(pfd->fd, target, pfd->fd == 3 && target_is_socket(target));
        return pfd->fd == 3 || pfd->fd == 9 || target_is_pipe(target);
    }
    return 0;
}

static const char *fd_kind(int fd)
{
    struct fd_state *st = fd_state_for(fd);

    if (!st)
        return "unknown";
    if (st->is_wayland)
        return "wayland";
    if (st->is_pipe)
        return "pipe";
    if (target_is_socket(st->target))
        return "socket";
    return "other";
}

static void trace_wl_fd_syscall(enum wl_fd_syscall_op_id op_id, int fd,
                                long ret, int saved_errno, uint64_t begin,
                                uint64_t end, unsigned int events,
                                unsigned int revents, void *return_address);

static void record_poll_return(const char *op, struct pollfd *fds, nfds_t nfds,
                               int ret, int saved_errno, uint64_t begin,
                               uint64_t end, void *return_address)
{
    char comm[TRACE_COMM_LEN];
    uint64_t elapsed = end >= begin ? end - begin : 0;
    int logged = 0;

    thread_comm(comm, sizeof(comm));
    for (nfds_t i = 0; i < nfds; i++) {
        char target[TRACE_TARGET_LEN];
        int fion = -1;
        struct fd_state *st;
        int interesting;
        struct pipe_state *pipe_st;
        uint64_t write_gap = 0;

        target[0] = '\0';
        interesting = pollfd_interesting(&fds[i], target, sizeof(target),
                                         &fion);
        if (!interesting)
            continue;
        st = fd_state_for(fds[i].fd);
        if (st) {
            st->last_poll_begin_us = begin;
            st->last_poll_return_us = end;
            st->last_poll_revents = fds[i].revents;
            st->last_poll_ret = ret;
            st->last_poll_errno = saved_errno;
            st->last_fionread = fion;
        }
        if (st && st->is_wayland) {
            trace_wl_fd_syscall(strcmp(op, "ppoll") == 0 ?
                                WL_FD_SYSCALL_PPOLL : WL_FD_SYSCALL_POLL,
                                fds[i].fd, ret, saved_errno, begin, end,
                                fds[i].events, fds[i].revents,
                                return_address);
        }
        pipe_st = target_is_pipe(target) ? pipe_state_for_target(target, 1) :
            NULL;
        if (pipe_st) {
            pipe_st->poll_returns++;
            pipe_st->last_poll_return_us = end;
            if (pipe_st->last_write_us && end >= pipe_st->last_write_us)
                write_gap = end - pipe_st->last_write_us;
            if (write_gap > pipe_st->write_to_poll_max_us)
                pipe_st->write_to_poll_max_us = write_gap;
            if (write_gap > max_write_to_poll_us)
                max_write_to_poll_us = write_gap;
        }
        record_qt_fd3_ready(op, comm, &fds[i], ret, begin, end, fion, target);
        if (!wayland_trace_enabled())
            continue;
        if (elapsed < (uint64_t)wayland_threshold_us &&
            write_gap < (uint64_t)wayland_threshold_us)
            continue;
        if (!should_trace_thread(comm) && !strstr(fd_kind(fds[i].fd), "wayland"))
            continue;
        trace_write_line("konsole-wayland-event-trace: poll op=%s tid=%ld "
                         "comm=%s fd=%d kind=%s events=0x%x revents=0x%x "
                         "ret=%d errno=%d elapsed_us=%llu fionread=%d "
                         "write_to_poll_us=%llu target=\"%s\"",
                         op, trace_gettid(), comm, fds[i].fd,
                         fd_kind(fds[i].fd), fds[i].events, fds[i].revents,
                         ret, saved_errno, (unsigned long long)elapsed, fion,
                         (unsigned long long)write_gap, target);
        logged = 1;
    }
    if (elapsed > max_poll_us)
        max_poll_us = elapsed;
    if (logged) {
        if (strcmp(op, "ppoll") == 0)
            ppoll_slow++;
        else
            poll_slow++;
    }
}

static void resolve_common(void)
{
    if (!real_poll)
        real_poll = (poll_fn_t)dlsym(RTLD_NEXT, "poll");
    if (!real_ppoll)
        real_ppoll = (ppoll_fn_t)dlsym(RTLD_NEXT, "ppoll");
    if (!real_read)
        real_read = (read_fn_t)dlsym(RTLD_NEXT, "read");
    if (!real_write)
        real_write = (write_fn_t)dlsym(RTLD_NEXT, "write");
    if (!real_recvmsg)
        real_recvmsg = (recvmsg_fn_t)dlsym(RTLD_NEXT, "recvmsg");
    if (!real_sendmsg)
        real_sendmsg = (sendmsg_fn_t)dlsym(RTLD_NEXT, "sendmsg");
}

static void *resolve_wayland(const char *name)
{
    return dlsym(RTLD_NEXT, name);
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int needs_mode = open_flags_need_mode(flags);
    int fd;
    int saved_errno;
    int do_trace;
    uint64_t begin = 0;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    resolve_loader_symbols();
    do_trace = loader_trace_enabled() && process_is_konsole();
    if (do_trace)
        begin = now_us();
    if (real_open) {
        if (needs_mode)
            fd = real_open(path, flags, mode);
        else
            fd = real_open(path, flags);
    } else {
        fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    }
    saved_errno = errno;
    if (do_trace)
        loader_record_path_event("open", AT_FDCWD, path, flags, fd,
                                 fd < 0 ? saved_errno : 0,
                                 now_us() - begin);
    errno = saved_errno;
    return fd;
}

int open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int needs_mode = open_flags_need_mode(flags);
    int fd;
    int saved_errno;
    int do_trace;
    uint64_t begin = 0;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    resolve_loader_symbols();
    do_trace = loader_trace_enabled() && process_is_konsole();
    if (do_trace)
        begin = now_us();
    if (real_open64) {
        if (needs_mode)
            fd = real_open64(path, flags, mode);
        else
            fd = real_open64(path, flags);
    } else {
        fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    }
    saved_errno = errno;
    if (do_trace)
        loader_record_path_event("open", AT_FDCWD, path, flags, fd,
                                 fd < 0 ? saved_errno : 0,
                                 now_us() - begin);
    errno = saved_errno;
    return fd;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    int needs_mode = open_flags_need_mode(flags);
    int fd;
    int saved_errno;
    int do_trace;
    uint64_t begin = 0;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    resolve_loader_symbols();
    do_trace = loader_trace_enabled() && process_is_konsole();
    if (do_trace)
        begin = now_us();
    if (real_openat) {
        if (needs_mode)
            fd = real_openat(dirfd, path, flags, mode);
        else
            fd = real_openat(dirfd, path, flags);
    } else {
        fd = (int)syscall(SYS_openat, dirfd, path, flags, mode);
    }
    saved_errno = errno;
    if (do_trace)
        loader_record_path_event("openat", dirfd, path, flags, fd,
                                 fd < 0 ? saved_errno : 0,
                                 now_us() - begin);
    errno = saved_errno;
    return fd;
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    int needs_mode = open_flags_need_mode(flags);
    int fd;
    int saved_errno;
    int do_trace;
    uint64_t begin = 0;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    resolve_loader_symbols();
    do_trace = loader_trace_enabled() && process_is_konsole();
    if (do_trace)
        begin = now_us();
    if (real_openat64) {
        if (needs_mode)
            fd = real_openat64(dirfd, path, flags, mode);
        else
            fd = real_openat64(dirfd, path, flags);
    } else {
        fd = (int)syscall(SYS_openat, dirfd, path, flags, mode);
    }
    saved_errno = errno;
    if (do_trace)
        loader_record_path_event("openat", dirfd, path, flags, fd,
                                 fd < 0 ? saved_errno : 0,
                                 now_us() - begin);
    errno = saved_errno;
    return fd;
}

void *dlopen(const char *filename, int flags)
{
    void *handle;
    int saved_errno;
    int do_trace;
    uint64_t begin = 0;

    resolve_loader_symbols();
    do_trace = loader_trace_enabled() && process_is_konsole();
    if (do_trace)
        begin = now_us();
    handle = real_dlopen ? real_dlopen(filename, flags) : NULL;
    saved_errno = errno;
    if (do_trace)
        loader_record_dlopen(filename, flags, handle, saved_errno,
                             now_us() - begin);
    errno = saved_errno;
    return handle;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    resolve_common();
    if (!real_poll)
        return -1;
    if (!trace_enabled() || in_trace || !process_is_konsole()) {
        return real_poll(fds, nfds, timeout);
    }
    poll_calls++;
    begin = now_us();
    ret = real_poll(fds, nfds, timeout);
    saved_errno = errno;
    record_poll_return("poll", fds, nfds, ret, saved_errno, begin, now_us(),
                       __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
          const sigset_t *sigmask)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    resolve_common();
    if (!real_ppoll)
        return -1;
    if (!trace_enabled() || in_trace || !process_is_konsole()) {
        return real_ppoll(fds, nfds, timeout, sigmask);
    }
    ppoll_calls++;
    begin = now_us();
    ret = real_ppoll(fds, nfds, timeout, sigmask);
    saved_errno = errno;
    record_poll_return("ppoll", fds, nfds, ret, saved_errno, begin, now_us(),
                       __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
    char target[TRACE_TARGET_LEN];
    char comm[TRACE_COMM_LEN];
    struct pipe_state *pipe_st;
    uint64_t begin;
    uint64_t end;
    uint64_t poll_gap = 0;
    ssize_t ret;
    int saved_errno;

    resolve_common();
    if (!real_read)
        return -1;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_read(fd, buf, count);
    begin = now_us();
    ret = real_read(fd, buf, count);
    saved_errno = errno;
    end = now_us();
    read_calls++;
    fd_target(fd, target, sizeof(target));
    if (fd_is_tracked_wayland(fd)) {
        trace_wl_fd_syscall(WL_FD_SYSCALL_READ, fd, ret, saved_errno, begin,
                            end, 0, 0, __builtin_return_address(0));
    }
    if (target_is_pipe(target)) {
        mark_fd_target(fd, target, 0);
        pipe_st = pipe_state_for_target(target, 1);
        if (pipe_st) {
            pipe_st->reads++;
            if (pipe_st->last_poll_return_us && end >= pipe_st->last_poll_return_us)
                poll_gap = end - pipe_st->last_poll_return_us;
            if (poll_gap > pipe_st->poll_to_read_max_us)
                pipe_st->poll_to_read_max_us = poll_gap;
            if (poll_gap > max_poll_to_read_us)
                max_poll_to_read_us = poll_gap;
        }
        if (wayland_trace_enabled() &&
            poll_gap >= (uint64_t)wayland_threshold_us) {
            thread_comm(comm, sizeof(comm));
            if (should_trace_thread(comm)) {
                trace_write_line("konsole-wayland-event-trace: pipe-read "
                                 "tid=%ld comm=%s fd=%d ret=%ld errno=%d "
                                 "elapsed_us=%llu poll_to_read_us=%llu "
                                 "target=\"%s\"",
                                 trace_gettid(), comm, fd, (long)ret,
                                 saved_errno,
                                 (unsigned long long)(end - begin),
                                 (unsigned long long)poll_gap, target);
            }
        }
    }
    errno = saved_errno;
    return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    char target[TRACE_TARGET_LEN];
    struct pipe_state *pipe_st;
    uint64_t begin;
    uint64_t end;
    ssize_t ret;
    int saved_errno;

    resolve_common();
    if (!real_write)
        return -1;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_write(fd, buf, count);
    begin = now_us();
    ret = real_write(fd, buf, count);
    saved_errno = errno;
    end = now_us();
    write_calls++;
    fd_target(fd, target, sizeof(target));
    if (fd_is_tracked_wayland(fd)) {
        trace_wl_fd_syscall(WL_FD_SYSCALL_WRITE, fd, ret, saved_errno, begin,
                            end, 0, 0, __builtin_return_address(0));
    }
    if (target_is_pipe(target)) {
        mark_fd_target(fd, target, 0);
        pipe_st = pipe_state_for_target(target, 1);
        if (pipe_st) {
            pipe_st->writes++;
            pipe_st->last_write_us = end;
        }
    }
    errno = saved_errno;
    return ret;
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
    uint64_t begin;
    uint64_t end;
    ssize_t ret;
    int saved_errno;

    resolve_common();
    if (!real_recvmsg)
        return -1;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_recvmsg(fd, msg, flags);
    begin = now_us();
    ret = real_recvmsg(fd, msg, flags);
    saved_errno = errno;
    end = now_us();
    trace_wl_fd_syscall(WL_FD_SYSCALL_RECVMSG, fd, ret, saved_errno, begin,
                        end, (unsigned int)flags, 0,
                        __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    uint64_t begin;
    uint64_t end;
    ssize_t ret;
    int saved_errno;

    resolve_common();
    if (!real_sendmsg)
        return -1;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_sendmsg(fd, msg, flags);
    begin = now_us();
    ret = real_sendmsg(fd, msg, flags);
    saved_errno = errno;
    end = now_us();
    trace_wl_fd_syscall(WL_FD_SYSCALL_SENDMSG, fd, ret, saved_errno, begin,
                        end, (unsigned int)flags, 0,
                        __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

static void trace_public_wl_call(enum public_wl_op_id op_id,
                                 const struct wl_display *display,
                                 struct wl_event_queue *queue, int fd_hint,
                                 long ret, int saved_errno, uint64_t begin,
                                 uint64_t end, void *return_address);

struct wl_display *wl_display_connect(const char *name)
{
    uint64_t begin;
    uint64_t end;
    struct wl_display *ret;
    int saved_errno;
    int fd = -1;

    if (!real_wl_display_connect)
        real_wl_display_connect =
            (wl_display_connect_fn_t)resolve_wayland("wl_display_connect");
    if (!real_wl_display_connect)
        return NULL;
    begin = now_us();
    ret = real_wl_display_connect(name);
    saved_errno = errno;
    end = now_us();
    if (trace_enabled() && process_is_konsole() && ret) {
        if (!real_wl_display_get_fd)
            real_wl_display_get_fd =
                (wl_display_get_fd_fn_t)resolve_wayland("wl_display_get_fd");
        if (real_wl_display_get_fd) {
            fd = real_wl_display_get_fd(ret);
            if (fd >= 0)
                remember_display_fd(ret, fd);
        }
    }
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_CONNECT, ret, NULL, fd, (long)ret,
                             saved_errno, begin, end,
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

struct wl_display *wl_display_connect_to_fd(int fd)
{
    uint64_t begin;
    uint64_t end;
    struct wl_display *ret;
    int saved_errno;

    if (!real_wl_display_connect_to_fd)
        real_wl_display_connect_to_fd =
            (wl_display_connect_to_fd_fn_t)resolve_wayland(
                "wl_display_connect_to_fd");
    if (!real_wl_display_connect_to_fd)
        return NULL;
    begin = now_us();
    ret = real_wl_display_connect_to_fd(fd);
    saved_errno = errno;
    end = now_us();
    if (trace_enabled() && process_is_konsole() && ret && fd >= 0)
        remember_display_fd(ret, fd);
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_CONNECT_TO_FD, ret, NULL, fd,
                             (long)ret, saved_errno, begin, end,
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

static void trace_wl_call(const char *name, const struct wl_display *display,
                          struct wl_event_queue *queue, int ret,
                          int saved_errno, uint64_t begin, uint64_t end)
{
    int fd = display_fd(display);
    struct fd_state *st = fd_state_for(fd);
    uint64_t elapsed = end >= begin ? end - begin : 0;
    uint64_t poll_gap = 0;
    uint64_t ready_gap = 0;
    int fion = -1;
    char comm[TRACE_COMM_LEN];
    const char *target = "";

    wl_calls++;
    if (elapsed > max_wl_us)
        max_wl_us = elapsed;
    if (st) {
        target = st->target;
        fion = fd_fionread(fd);
        if (st->last_poll_return_us && begin >= st->last_poll_return_us)
            poll_gap = begin - st->last_poll_return_us;
    }
    thread_comm(comm, sizeof(comm));
    if (qt_trace_enabled() && qt_should_log_wl_op(name) &&
        is_main_konsole_thread(comm)) {
        ready_gap = ready_to_call_gap(begin);
        qt_wl_main_calls++;
        if (elapsed > qt_max_wl_main_us)
            qt_max_wl_main_us = elapsed;
        if (ready_gap > qt_max_ready_to_wl_us)
            qt_max_ready_to_wl_us = ready_gap;
        update_ready_to_call_max(ready_gap);
        if (elapsed >= (uint64_t)qt_threshold_us ||
            ready_gap >= (uint64_t)qt_threshold_us) {
            qt_wl_main_slow++;
            trace_write_line("konsole-qt-event-trace: wl-main op=%s tid=%ld "
                             "comm=%s display=%p queue=%p fd=%d ret=%d "
                             "errno=%d elapsed_us=%llu ready_to_call_us=%llu "
                             "ready_seq=%llu ready_tid=%ld "
                             "last_revents=0x%x last_fionread=%d "
                             "target=\"%s\"",
                             name, trace_gettid(), comm,
                             (const void *)display, (void *)queue, fd, ret,
                             saved_errno, (unsigned long long)elapsed,
                             (unsigned long long)ready_gap,
                             qt_fd3_ready_sequence, qt_last_fd3_ready_tid,
                             qt_last_fd3_ready_revents,
                             qt_last_fd3_ready_fionread, target);
        }
    }
    if (!wayland_trace_enabled() ||
        (elapsed < (uint64_t)wayland_threshold_us &&
         poll_gap < (uint64_t)wayland_threshold_us) ||
        !should_trace_thread(comm))
        return;
    wl_slow++;
    trace_write_line("konsole-wayland-event-trace: wl op=%s tid=%ld comm=%s "
                     "display=%p queue=%p fd=%d ret=%d errno=%d "
                     "elapsed_us=%llu poll_to_wl_us=%llu fionread=%d "
                     "last_revents=0x%x target=\"%s\"",
                     name, trace_gettid(), comm, (const void *)display,
                     (void *)queue, fd, ret, saved_errno,
                     (unsigned long long)elapsed,
                     (unsigned long long)poll_gap, fion,
                     st ? st->last_poll_revents : 0, target);
}

static void trace_public_wl_call(enum public_wl_op_id op_id,
                                 const struct wl_display *display,
                                 struct wl_event_queue *queue, int fd_hint,
                                 long ret, int saved_errno, uint64_t begin,
                                 uint64_t end, void *return_address)
{
    struct public_wl_op_state *op;
    struct fd_state *st;
    uint64_t elapsed = end >= begin ? end - begin : 0;
    uint64_t ready_gap = ready_to_call_gap(begin);
    uint64_t poll_gap = 0;
    int fd = fd_hint >= 0 ? fd_hint : display_fd(display);
    int fion = -1;
    int span_op;
    uint64_t span_start;
    unsigned int span_depth;
    unsigned long long span_call_id;
    long span_tid;
    int span_active = 0;
    uint64_t span_age = 0;
    char comm[TRACE_COMM_LEN];
    const char *target = "";
    const char *caller_module;
    const char *caller_symbol;

    if ((!qt_cpp_trace_enabled() && !qt_bridge_trace_enabled()) ||
        op_id < 0 || op_id >= PUBLIC_WL_OP_COUNT)
        return;
    op = &public_wl_ops[op_id];
    public_wl_total_calls++;
    op->calls++;
    __atomic_store_n(&public_wl_last_call_us, end, __ATOMIC_RELEASE);
    if (elapsed > op->max_elapsed_us)
        op->max_elapsed_us = elapsed;
    if (elapsed > public_wl_max_elapsed_us)
        public_wl_max_elapsed_us = elapsed;
    if (ready_gap > op->max_ready_us)
        op->max_ready_us = ready_gap;
    if (ready_gap > public_wl_max_ready_us)
        public_wl_max_ready_us = ready_gap;

    thread_comm(comm, sizeof(comm));
    if (strstr(comm, "WaylandEventThr")) {
        public_wl_event_thread_calls++;
        op->event_thread_calls++;
    }
    if (is_main_konsole_thread(comm)) {
        public_wl_main_thread_calls++;
        op->main_thread_calls++;
    }
    if (!qt_cpp_trace_enabled())
        return;

    st = fd_state_for(fd);
    if (st) {
        target = st->target;
        fion = fd_fionread(fd);
        if (st->last_poll_return_us && begin >= st->last_poll_return_us)
            poll_gap = begin - st->last_poll_return_us;
    }

    span_op = __atomic_load_n(&qt_active_span_op, __ATOMIC_ACQUIRE);
    span_start = __atomic_load_n(&qt_active_span_start_us, __ATOMIC_ACQUIRE);
    span_depth = __atomic_load_n(&qt_active_span_depth, __ATOMIC_ACQUIRE);
    span_call_id =
        __atomic_load_n(&qt_active_span_call_id, __ATOMIC_ACQUIRE);
    span_tid = __atomic_load_n(&qt_active_span_tid, __ATOMIC_ACQUIRE);
    if (span_op > ACTIVE_SPAN_NONE && span_op < ACTIVE_SPAN_OP_COUNT &&
        span_start && end >= span_start) {
        span_active = 1;
        span_age = end - span_start;
    }
    return_address_info(return_address, &caller_module, &caller_symbol);
    trace_write_line("konsole-qt-cpp-trace: public-wl op=%s pid=%ld "
                     "tid=%ld comm=%s display=%p queue=%p fd=%d ret=%ld "
                     "errno=%d elapsed_us=%llu ready_to_call_us=%llu "
                     "poll_to_wl_us=%llu fionread=%d last_revents=0x%x "
                     "ready_seq=%llu ready_tid=%ld last_fionread=%d "
                     "main_span_active=%d main_span_op=%s "
                     "main_span_age_us=%llu main_span_depth=%u "
                     "main_span_call_id=%llu main_span_tid=%ld "
                     "caller_module=\"%s\" caller_symbol=\"%s\" "
                     "target=\"%s\"",
                     op->name, (long)getpid(), trace_gettid(), comm,
                     (const void *)display, (void *)queue, fd, ret,
                     saved_errno, (unsigned long long)elapsed,
                     (unsigned long long)ready_gap,
                     (unsigned long long)poll_gap, fion,
                     st ? st->last_poll_revents : 0,
                     qt_fd3_ready_sequence, qt_last_fd3_ready_tid,
                     qt_last_fd3_ready_fionread, span_active,
                     active_span_name((enum active_span_op_id)span_op),
                     (unsigned long long)span_age, span_depth, span_call_id,
                     span_tid, caller_module, caller_symbol, target);
}

static void trace_wl_fd_syscall(enum wl_fd_syscall_op_id op_id, int fd,
                                long ret, int saved_errno, uint64_t begin,
                                uint64_t end, unsigned int events,
                                unsigned int revents, void *return_address)
{
    struct wl_fd_syscall_state *op;
    struct fd_state *st;
    uint64_t elapsed = end >= begin ? end - begin : 0;
    uint64_t ready_gap = ready_to_call_gap(begin);
    uint64_t last_poll_age = 0;
    int fion = -1;
    char comm[TRACE_COMM_LEN];
    const char *caller_module;
    const char *caller_symbol;

    if ((!qt_cpp_trace_enabled() && !qt_bridge_trace_enabled()) || op_id < 0 ||
        op_id >= WL_FD_SYSCALL_OP_COUNT || !fd_is_tracked_wayland(fd))
        return;
    st = fd_state_for(fd);
    op = &wl_fd_syscall_ops[op_id];
    wl_fd_syscall_total_calls++;
    op->calls++;
    __atomic_store_n(&wl_fd_syscall_last_call_us, end, __ATOMIC_RELEASE);
    if (elapsed > op->max_elapsed_us)
        op->max_elapsed_us = elapsed;
    if (elapsed > wl_fd_syscall_max_elapsed_us)
        wl_fd_syscall_max_elapsed_us = elapsed;
    if (ready_gap > op->max_ready_us)
        op->max_ready_us = ready_gap;
    if (ready_gap > wl_fd_syscall_max_ready_us)
        wl_fd_syscall_max_ready_us = ready_gap;
    if (st) {
        fion = fd_fionread(fd);
        if (st->last_poll_return_us && end >= st->last_poll_return_us)
            last_poll_age = end - st->last_poll_return_us;
    }
    if (!qt_cpp_trace_enabled())
        return;
    thread_comm(comm, sizeof(comm));
    return_address_info(return_address, &caller_module, &caller_symbol);
    trace_write_line("konsole-qt-cpp-trace: wl-fd-syscall op=%s pid=%ld "
                     "tid=%ld comm=%s fd=%d ret=%ld errno=%d "
                     "elapsed_us=%llu ready_to_call_us=%llu "
                     "events=0x%x revents=0x%x fionread=%d "
                     "last_poll_age_us=%llu last_poll_ret=%d "
                     "last_poll_errno=%d last_poll_revents=0x%x "
                     "ready_seq=%llu ready_tid=%ld last_fionread=%d "
                     "caller_module=\"%s\" caller_symbol=\"%s\" "
                     "target=\"%s\"",
                     op->name, (long)getpid(), trace_gettid(), comm, fd, ret,
                     saved_errno, (unsigned long long)elapsed,
                     (unsigned long long)ready_gap, events, revents, fion,
                     (unsigned long long)last_poll_age,
                     st ? st->last_poll_ret : 0,
                     st ? st->last_poll_errno : 0,
                     st ? st->last_poll_revents : 0,
                     qt_fd3_ready_sequence, qt_last_fd3_ready_tid,
                     qt_last_fd3_ready_fionread, caller_module,
                     caller_symbol, st ? st->target : "");
}

int wl_display_get_fd(struct wl_display *display)
{
    uint64_t begin;
    uint64_t end;
    int ret;
    int saved_errno;

    if (!real_wl_display_get_fd)
        real_wl_display_get_fd =
            (wl_display_get_fd_fn_t)resolve_wayland("wl_display_get_fd");
    if (!real_wl_display_get_fd)
        return -1;
    begin = now_us();
    ret = real_wl_display_get_fd(display);
    saved_errno = errno;
    end = now_us();
    if (trace_enabled() && process_is_konsole() && ret >= 0) {
        char target[TRACE_TARGET_LEN];
        char comm[TRACE_COMM_LEN];

        remember_display_fd(display, ret);
        if (wayland_trace_enabled()) {
            fd_target(ret, target, sizeof(target));
            thread_comm(comm, sizeof(comm));
            trace_write_line("konsole-wayland-event-trace: wl-get-fd tid=%ld "
                             "comm=%s display=%p fd=%d target=\"%s\"",
                             trace_gettid(), comm, (void *)display, ret,
                             target);
        }
    }
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_GET_FD, display, NULL, ret, ret,
                             saved_errno, begin, end,
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_dispatch(struct wl_display *display)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_dispatch)
        real_wl_display_dispatch =
            (wl_display_dispatch_fn_t)resolve_wayland("wl_display_dispatch");
    if (!real_wl_display_dispatch)
        return -1;
    begin = now_us();
    ret = real_wl_display_dispatch(display);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("dispatch", display, NULL, ret, saved_errno, begin,
                      now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_DISPATCH, display, NULL, -1, ret,
                             saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_dispatch_queue(struct wl_display *display,
                              struct wl_event_queue *queue)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_dispatch_queue)
        real_wl_display_dispatch_queue =
            (wl_display_dispatch_queue_fn_t)resolve_wayland(
                "wl_display_dispatch_queue");
    if (!real_wl_display_dispatch_queue)
        return -1;
    begin = now_us();
    ret = real_wl_display_dispatch_queue(display, queue);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("dispatch_queue", display, queue, ret, saved_errno,
                      begin, now_us());
    errno = saved_errno;
    return ret;
}

int wl_display_roundtrip(struct wl_display *display)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_roundtrip)
        real_wl_display_roundtrip =
            (wl_display_roundtrip_fn_t)resolve_wayland(
                "wl_display_roundtrip");
    if (!real_wl_display_roundtrip)
        return -1;
    begin = now_us();
    ret = real_wl_display_roundtrip(display);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("roundtrip", display, NULL, ret, saved_errno, begin,
                      now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_ROUNDTRIP, display, NULL, -1, ret,
                             saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_roundtrip_queue(struct wl_display *display,
                               struct wl_event_queue *queue)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_roundtrip_queue)
        real_wl_display_roundtrip_queue =
            (wl_display_roundtrip_queue_fn_t)resolve_wayland(
                "wl_display_roundtrip_queue");
    if (!real_wl_display_roundtrip_queue)
        return -1;
    begin = now_us();
    ret = real_wl_display_roundtrip_queue(display, queue);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("roundtrip_queue", display, queue, ret, saved_errno,
                      begin, now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_ROUNDTRIP_QUEUE, display, queue, -1,
                             ret, saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_prepare_read(struct wl_display *display)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_prepare_read)
        real_wl_display_prepare_read =
            (wl_display_prepare_read_fn_t)resolve_wayland(
                "wl_display_prepare_read");
    if (!real_wl_display_prepare_read)
        return -1;
    begin = now_us();
    ret = real_wl_display_prepare_read(display);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("prepare_read", display, NULL, ret, saved_errno, begin,
                      now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_PREPARE_READ, display, NULL, -1, ret,
                             saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_prepare_read_queue(struct wl_display *display,
                                  struct wl_event_queue *queue)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_prepare_read_queue)
        real_wl_display_prepare_read_queue =
            (wl_display_prepare_read_queue_fn_t)resolve_wayland(
                "wl_display_prepare_read_queue");
    if (!real_wl_display_prepare_read_queue)
        return -1;
    begin = now_us();
    ret = real_wl_display_prepare_read_queue(display, queue);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("prepare_read_queue", display, queue, ret, saved_errno,
                      begin, now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_PREPARE_READ_QUEUE, display, queue, -1,
                             ret, saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_flush(struct wl_display *display)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_flush)
        real_wl_display_flush =
            (wl_display_flush_fn_t)resolve_wayland("wl_display_flush");
    if (!real_wl_display_flush)
        return -1;
    begin = now_us();
    ret = real_wl_display_flush(display);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("flush", display, NULL, ret, saved_errno, begin,
                      now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_FLUSH, display, NULL, -1, ret,
                             saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_read_events(struct wl_display *display)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_read_events)
        real_wl_display_read_events =
            (wl_display_read_events_fn_t)resolve_wayland(
                "wl_display_read_events");
    if (!real_wl_display_read_events)
        return -1;
    begin = now_us();
    ret = real_wl_display_read_events(display);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("read_events", display, NULL, ret, saved_errno, begin,
                      now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_READ_EVENTS, display, NULL, -1, ret,
                             saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_dispatch_pending(struct wl_display *display)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_dispatch_pending)
        real_wl_display_dispatch_pending =
            (wl_display_dispatch_pending_fn_t)resolve_wayland(
                "wl_display_dispatch_pending");
    if (!real_wl_display_dispatch_pending)
        return -1;
    begin = now_us();
    ret = real_wl_display_dispatch_pending(display);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("dispatch_pending", display, NULL, ret, saved_errno,
                      begin, now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_DISPATCH_PENDING, display, NULL, -1,
                             ret, saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

int wl_display_dispatch_queue_pending(struct wl_display *display,
                                      struct wl_event_queue *queue)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_wl_display_dispatch_queue_pending)
        real_wl_display_dispatch_queue_pending =
            (wl_display_dispatch_queue_pending_fn_t)resolve_wayland(
                "wl_display_dispatch_queue_pending");
    if (!real_wl_display_dispatch_queue_pending)
        return -1;
    begin = now_us();
    ret = real_wl_display_dispatch_queue_pending(display, queue);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("dispatch_queue_pending", display, queue, ret,
                      saved_errno, begin, now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_DISPATCH_QUEUE_PENDING, display, queue,
                             -1, ret, saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

void wl_display_cancel_read(struct wl_display *display)
{
    uint64_t begin;
    int saved_errno;

    if (!real_wl_display_cancel_read)
        real_wl_display_cancel_read =
            (wl_display_cancel_read_fn_t)resolve_wayland(
                "wl_display_cancel_read");
    if (!real_wl_display_cancel_read)
        return;
    begin = now_us();
    real_wl_display_cancel_read(display);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_wl_call("cancel_read", display, NULL, 0, saved_errno, begin,
                      now_us());
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_CANCEL_READ, display, NULL, -1, 0,
                             saved_errno, begin, now_us(),
                             __builtin_return_address(0));
    errno = saved_errno;
}

int wl_proxy_add_listener(void *proxy, void (**implementation)(void),
                          void *data)
{
    uint64_t begin;
    uint64_t end;
    int ret;
    int saved_errno;

    if (!real_wl_proxy_add_listener)
        real_wl_proxy_add_listener =
            (wl_proxy_add_listener_fn_t)resolve_wayland(
                "wl_proxy_add_listener");
    if (!real_wl_proxy_add_listener)
        return -1;
    begin = now_us();
    ret = real_wl_proxy_add_listener(proxy, implementation, data);
    saved_errno = errno;
    end = now_us();
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_PROXY_ADD_LISTENER, NULL, NULL, -1,
                             ret, saved_errno, begin, end,
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

void wl_proxy_destroy(void *proxy)
{
    uint64_t begin;
    uint64_t end;
    int saved_errno;

    if (!real_wl_proxy_destroy)
        real_wl_proxy_destroy =
            (wl_proxy_destroy_fn_t)resolve_wayland("wl_proxy_destroy");
    if (!real_wl_proxy_destroy)
        return;
    begin = now_us();
    real_wl_proxy_destroy(proxy);
    saved_errno = errno;
    end = now_us();
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_PROXY_DESTROY, NULL, NULL, -1, 0,
                             saved_errno, begin, end,
                             __builtin_return_address(0));
    errno = saved_errno;
}

unsigned int wl_proxy_get_id(void *proxy)
{
    uint64_t begin;
    uint64_t end;
    unsigned int ret;
    int saved_errno;

    if (!real_wl_proxy_get_id)
        real_wl_proxy_get_id =
            (wl_proxy_get_id_fn_t)resolve_wayland("wl_proxy_get_id");
    if (!real_wl_proxy_get_id)
        return 0;
    begin = now_us();
    ret = real_wl_proxy_get_id(proxy);
    saved_errno = errno;
    end = now_us();
    if (trace_enabled() && process_is_konsole())
        trace_public_wl_call(PUBLIC_WL_PROXY_GET_ID, NULL, NULL, -1, ret,
                             saved_errno, begin, end,
                             __builtin_return_address(0));
    errno = saved_errno;
    return ret;
}

static void *resolve_glib(const char *name)
{
    return dlsym(RTLD_NEXT, name);
}

static void trace_glib_call(const char *name, GMainContext *context,
                            int arg, int ret, uint64_t begin, uint64_t end,
                            const struct active_span_record *span,
                            int ready_during_span,
                            uint64_t ready_offset_us,
                            unsigned long long span_ready_seq)
{
    uint64_t elapsed = end >= begin ? end - begin : 0;
    uint64_t ready_gap;
    char comm[TRACE_COMM_LEN];

    if (!qt_trace_enabled())
        return;
    thread_comm(comm, sizeof(comm));
    if (!is_main_konsole_thread(comm))
        return;
    ready_gap = ready_to_call_gap(begin);
    update_ready_to_call_max(ready_gap);
    if (strcmp(name, "iteration") == 0) {
        qt_glib_iteration_calls++;
        if (elapsed > qt_max_glib_iteration_us)
            qt_max_glib_iteration_us = elapsed;
        if (ready_gap > qt_max_ready_to_glib_us)
            qt_max_ready_to_glib_us = ready_gap;
    } else if (strcmp(name, "pending") == 0) {
        qt_glib_pending_calls++;
        if (elapsed > qt_max_glib_pending_us)
            qt_max_glib_pending_us = elapsed;
    } else if (strcmp(name, "wakeup") == 0) {
        qt_glib_wakeup_calls++;
        if (elapsed > qt_max_glib_wakeup_us)
            qt_max_glib_wakeup_us = elapsed;
    }
    if (elapsed < (uint64_t)qt_threshold_us &&
        ready_gap < (uint64_t)qt_threshold_us && !ready_during_span)
        return;
    qt_glib_slow++;
    trace_write_line("konsole-qt-event-trace: glib op=%s tid=%ld comm=%s "
                     "context=%p arg=%d ret=%d elapsed_us=%llu "
                     "ready_to_call_us=%llu ready_seq=%llu ready_tid=%ld "
                     "last_revents=0x%x last_fionread=%d "
                     "span_active=%d span_op=%s span_depth=%u "
                     "span_call_id=%llu fd3_ready_during_span=%d "
                     "fd3_ready_offset_us=%llu fd3_ready_seq=%llu",
                     name, trace_gettid(), comm, (void *)context, arg, ret,
                     (unsigned long long)elapsed,
                     (unsigned long long)ready_gap, qt_fd3_ready_sequence,
                     qt_last_fd3_ready_tid, qt_last_fd3_ready_revents,
                     qt_last_fd3_ready_fionread, span && span->active,
                     active_span_name(span && span->active ? span->op_id :
                                      ACTIVE_SPAN_NONE),
                     span && span->active ? span->depth : 0,
                     span && span->active ? span->call_id : 0,
	                     ready_during_span,
	                     (unsigned long long)ready_offset_us, span_ready_seq);
}

static void trace_glib_phase_call(enum glib_phase_op_id op_id,
                                  GMainContext *context, int arg0, int arg1,
                                  unsigned int nfds, int ret, int has_ret,
                                  uint64_t begin, uint64_t end,
                                  const struct glib_phase_record *phase,
                                  int ready_during_phase,
                                  uint64_t ready_offset_us,
                                  unsigned long long phase_ready_seq)
{
    uint64_t elapsed = end >= begin ? end - begin : 0;
    uint64_t ready_gap;
    int span_op;
    uint64_t span_start;
    unsigned int span_depth;
    unsigned long long span_call_id;
    long span_tid;
    int span_active = 0;
    uint64_t span_age = 0;
    char comm[TRACE_COMM_LEN];

    if (!glib_trace_enabled() || op_id <= GLIB_PHASE_NONE ||
        op_id >= GLIB_PHASE_OP_COUNT)
        return;
    thread_comm(comm, sizeof(comm));
    if (!is_main_konsole_thread(comm))
        return;

    ready_gap = ready_to_call_gap(begin);
    update_ready_to_call_max(ready_gap);
    glib_phase_calls[op_id]++;
    glib_phase_total_us[op_id] += elapsed;
    if (elapsed > glib_phase_max_us[op_id])
        glib_phase_max_us[op_id] = elapsed;
    if (op_id == GLIB_PHASE_POLL && ready_gap > qt_max_ready_to_glib_us)
        qt_max_ready_to_glib_us = ready_gap;

    span_op = __atomic_load_n(&qt_active_span_op, __ATOMIC_ACQUIRE);
    span_start = __atomic_load_n(&qt_active_span_start_us, __ATOMIC_ACQUIRE);
    span_depth = __atomic_load_n(&qt_active_span_depth, __ATOMIC_ACQUIRE);
    span_call_id =
        __atomic_load_n(&qt_active_span_call_id, __ATOMIC_ACQUIRE);
    span_tid = __atomic_load_n(&qt_active_span_tid, __ATOMIC_ACQUIRE);
    if (span_op > ACTIVE_SPAN_NONE && span_op < ACTIVE_SPAN_OP_COUNT &&
        span_start && end >= span_start) {
        span_active = 1;
        span_age = end - span_start;
    }

    if (elapsed < (uint64_t)qt_threshold_us &&
        ready_gap < (uint64_t)qt_threshold_us && !ready_during_phase)
        return;
    glib_phase_slow[op_id]++;
    trace_write_line("konsole-qt-event-trace: glib-phase op=%s tid=%ld "
                     "comm=%s context=%p arg0=%d arg1=%d nfds=%u ret=%d "
                     "has_ret=%d elapsed_us=%llu total_us=%llu "
                     "ready_to_call_us=%llu ready_seq=%llu ready_tid=%ld "
                     "last_revents=0x%x last_fionread=%d "
                     "phase_depth=%u phase_call_id=%llu "
                     "fd3_ready_during_phase=%d fd3_ready_offset_us=%llu "
                     "fd3_ready_seq=%llu main_span_active=%d "
                     "main_span_op=%s main_span_age_us=%llu "
                     "main_span_depth=%u main_span_call_id=%llu "
                     "main_span_tid=%ld",
                     glib_phase_name(op_id), trace_gettid(), comm,
                     (void *)context, arg0, arg1, nfds, ret, has_ret,
                     (unsigned long long)elapsed,
                     (unsigned long long)glib_phase_total_us[op_id],
                     (unsigned long long)ready_gap, qt_fd3_ready_sequence,
                     qt_last_fd3_ready_tid, qt_last_fd3_ready_revents,
                     qt_last_fd3_ready_fionread,
                     phase && phase->active ? phase->depth : 0,
                     phase && phase->active ? phase->call_id : 0,
                     ready_during_phase,
                     (unsigned long long)ready_offset_us, phase_ready_seq,
                     span_active,
                     active_span_name((enum active_span_op_id)span_op),
                     (unsigned long long)span_age, span_depth, span_call_id,
                     span_tid);
}

static void record_gpoll_return(GPollFD *fds, guint nfds, int ret,
                                int saved_errno, uint64_t begin, uint64_t end)
{
    char comm[TRACE_COMM_LEN];

    if (!fds || ret <= 0)
        return;
    thread_comm(comm, sizeof(comm));
    for (guint i = 0; i < nfds; i++) {
        char target[TRACE_TARGET_LEN];
        struct pollfd pfd;
        int fion;

        if (fds[i].fd != 3 || fds[i].revents == 0)
            continue;
        fd_target(fds[i].fd, target, sizeof(target));
        fion = fd_fionread(fds[i].fd);
        mark_fd_target(fds[i].fd, target, target_is_socket(target));
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fds[i].fd;
        pfd.events = fds[i].events;
        pfd.revents = fds[i].revents;
        record_qt_fd3_ready("g_poll", comm, &pfd, ret, begin, end, fion,
                            target);
        (void)saved_errno;
    }
}

static struct bridge_poll_context *bridge_poll_context_for(GMainContext *context,
                                                           int create)
{
    int free_slot = -1;

    for (int i = 0;
         i < (int)(sizeof(bridge_poll_contexts) /
                   sizeof(bridge_poll_contexts[0]));
         i++) {
        if (bridge_poll_contexts[i].installed &&
            bridge_poll_contexts[i].context == context)
            return &bridge_poll_contexts[i];
        if (!bridge_poll_contexts[i].installed && free_slot < 0)
            free_slot = i;
    }
    if (!create || free_slot < 0)
        return NULL;
    bridge_poll_contexts[free_slot].context = context;
    bridge_poll_contexts[free_slot].original = NULL;
    bridge_poll_contexts[free_slot].installed = 1;
    return &bridge_poll_contexts[free_slot];
}

static void install_bridge_poll_func(GMainContext *context)
{
    struct bridge_poll_context *slot;
    g_poll_fn_t current;

    if (!qt_bridge_trace_enabled())
        return;
    __sync_add_and_fetch(&qt_bridge_poll_install_attempts, 1);
    __atomic_store_n(&qt_bridge_poll_install_last_context, (uintptr_t)context,
                     __ATOMIC_RELAXED);
    if (!context) {
        __sync_add_and_fetch(&qt_bridge_poll_install_null_context, 1);
        return;
    }
    if (!real_g_main_context_get_poll_func &&
        !qt_bridge_poll_install_get_poll_func_missing)
        real_g_main_context_get_poll_func =
            (g_main_context_get_poll_func_fn_t)resolve_glib(
                "g_main_context_get_poll_func");
    if (!real_g_main_context_get_poll_func) {
        qt_bridge_poll_install_get_poll_func_missing = 1;
        __sync_add_and_fetch(&qt_bridge_poll_install_missing_get_poll_func, 1);
        return;
    }
    if (!real_g_main_context_set_poll_func &&
        !qt_bridge_poll_install_set_poll_func_missing)
        real_g_main_context_set_poll_func =
            (g_main_context_set_poll_func_fn_t)resolve_glib(
                "g_main_context_set_poll_func");
    if (!real_g_main_context_set_poll_func) {
        qt_bridge_poll_install_set_poll_func_missing = 1;
        __sync_add_and_fetch(&qt_bridge_poll_install_missing_set_poll_func, 1);
        return;
    }
    current = real_g_main_context_get_poll_func(context);
    if (current == qt_bridge_poll_func) {
        __sync_add_and_fetch(&qt_bridge_poll_install_already_wrapped, 1);
        return;
    }
    slot = bridge_poll_context_for(context, 1);
    if (!slot) {
        __sync_add_and_fetch(&qt_bridge_poll_install_slot_full, 1);
        return;
    }
    if (current && current != qt_bridge_poll_func)
        slot->original = current;
    real_g_main_context_set_poll_func(context, qt_bridge_poll_func);
    __sync_add_and_fetch(&qt_bridge_poll_install_installed, 1);
}

static g_poll_fn_t bridge_poll_original_for(GMainContext *context)
{
    struct bridge_poll_context *slot;

    if (bridge_poll_original_tls)
        return bridge_poll_original_tls;
    slot = bridge_poll_context_for(context, 0);
    if (slot && slot->original && slot->original != qt_bridge_poll_func)
        return slot->original;
    if (!real_g_poll)
        real_g_poll = (g_poll_fn_t)resolve_glib("g_poll");
    return real_g_poll;
}

static int gpoll_fd3_snapshot(GPollFD *fds, guint nfds, int *revents,
                              int *fionread, int *present)
{
    if (revents)
        *revents = 0;
    if (fionread)
        *fionread = -1;
    if (present)
        *present = 0;
    if (!fds)
        return -1;
    for (guint i = 0; i < nfds; i++) {
        if (fds[i].fd != 3)
            continue;
        if (present)
            *present = 1;
        if (revents)
            *revents = fds[i].revents;
        if (fionread)
            *fionread = fd_fionread(fds[i].fd);
        return (int)i;
    }
    return -1;
}

static gint qt_bridge_poll_func(GPollFD *fds, guint nfds, gint timeout)
{
    uint64_t begin;
    uint64_t end;
    uint64_t elapsed;
    uint64_t ready_offset = 0;
    unsigned long long seq_before;
    unsigned long long seq_after;
    g_poll_fn_t original;
    gint ret;
    int saved_errno;
    int fd3_index;
    int fd3_present = 0;
    int fd3_revents_before = 0;
    int fd3_revents_after = 0;
    int fd3_fion_before = -1;
    int fd3_fion_after = -1;
    int ready_during_poll = 0;
    char comm[TRACE_COMM_LEN];

    original = bridge_poll_original_for(bridge_poll_context_tls);
    if (!original || original == qt_bridge_poll_func)
        original = real_g_poll;
    if (!original)
        return -1;
    if (!qt_bridge_trace_enabled() || in_trace || bridge_poll_depth_tls > 0)
        return original(fds, nfds, timeout);

    fd3_index = gpoll_fd3_snapshot(fds, nfds, &fd3_revents_before,
                                   &fd3_fion_before, &fd3_present);
    seq_before = __atomic_load_n(&qt_fd3_ready_sequence, __ATOMIC_ACQUIRE);
    begin = now_us();
    bridge_poll_depth_tls++;
    ret = original(fds, nfds, timeout);
    saved_errno = errno;
    bridge_poll_depth_tls--;
    end = now_us();
    elapsed = end >= begin ? end - begin : 0;
    seq_after = __atomic_load_n(&qt_fd3_ready_sequence, __ATOMIC_ACQUIRE);
    if (fd3_index >= 0) {
        fd3_revents_after = fds[fd3_index].revents;
        fd3_fion_after = fd_fionread(fds[fd3_index].fd);
        if (ret > 0 && fd3_revents_after != 0 && fd3_fion_after > 0 &&
            seq_after == seq_before) {
            struct pollfd pfd;

            memset(&pfd, 0, sizeof(pfd));
            pfd.fd = fds[fd3_index].fd;
            pfd.events = fds[fd3_index].events;
            pfd.revents = fds[fd3_index].revents;
            thread_comm(comm, sizeof(comm));
            record_qt_fd3_ready("bridge_poll", comm, &pfd, ret, begin, end,
                                fd3_fion_after, "fd3");
            seq_after =
                __atomic_load_n(&qt_fd3_ready_sequence, __ATOMIC_ACQUIRE);
        }
    }
    if (seq_after > seq_before) {
        uint64_t ready_us =
            __atomic_load_n(&qt_last_fd3_ready_us, __ATOMIC_ACQUIRE);

        if (ready_us >= begin && ready_us <= end) {
            ready_during_poll = 1;
            ready_offset = ready_us - begin;
        }
    }

    __sync_add_and_fetch(&qt_bridge_poll_calls, 1);
    if (elapsed > qt_bridge_poll_max_elapsed_us)
        qt_bridge_poll_max_elapsed_us = elapsed;
    if (ready_during_poll) {
        __sync_add_and_fetch(&qt_bridge_poll_fd3_ready_during, 1);
        if (ready_offset > qt_bridge_poll_max_fd3_ready_offset_us)
            qt_bridge_poll_max_fd3_ready_offset_us = ready_offset;
    }
    if (fd3_revents_after != 0 || fd3_fion_after > 0)
        __sync_add_and_fetch(&qt_bridge_poll_fd3_ready, 1);
    if (elapsed >= (uint64_t)qt_threshold_us || ready_during_poll ||
        (fd3_revents_after != 0 && fd3_fion_after > 0)) {
        thread_comm(comm, sizeof(comm));
        if (elapsed >= (uint64_t)qt_threshold_us)
            __sync_add_and_fetch(&qt_bridge_poll_slow, 1);
        trace_write_line("konsole-qt-event-trace: bridge-poll "
                         "tid=%ld comm=%s context=%p nfds=%u timeout=%d "
                         "ret=%d errno=%d elapsed_us=%llu "
                         "fd3_present=%d fd3_revents_before=0x%x "
                         "fd3_revents_after=0x%x fd3_fionread_before=%d "
                         "fd3_fionread_after=%d fd3_ready_during_poll=%d "
                         "fd3_ready_offset_us=%llu ready_seq_before=%llu "
                         "ready_seq_after=%llu",
                         trace_gettid(), comm, (void *)bridge_poll_context_tls,
                         nfds, timeout, ret, saved_errno,
                         (unsigned long long)elapsed, fd3_present,
                         fd3_revents_before, fd3_revents_after,
                         fd3_fion_before, fd3_fion_after, ready_during_poll,
                         (unsigned long long)ready_offset, seq_before,
                         seq_after);
    }
    errno = saved_errno;
    return ret;
}

gboolean g_main_context_iteration(GMainContext *context, gboolean may_block)
{
    uint64_t begin;
    uint64_t end;
    struct active_span_record span;
    int ready_during_span = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long span_ready_seq = 0;
    int bridge_enter_emitted = 0;
    g_poll_fn_t saved_bridge_poll_original;
    GMainContext *saved_bridge_poll_context;
    gboolean ret;
    int saved_errno;

    if (!real_g_main_context_iteration)
        real_g_main_context_iteration =
            (g_main_context_iteration_fn_t)resolve_glib(
                "g_main_context_iteration");
    if (!real_g_main_context_iteration)
        return 0;
    begin = now_us();
    span = active_span_enter(ACTIVE_SPAN_GLIB_ITERATION, begin);
    bridge_enter_emitted = qt_bridge_trace_enter("glib_iteration", &span,
                                                 begin, 0, context,
                                                 may_block, 1);
    if (qt_bridge_trace_enabled())
        install_bridge_poll_func(context);
    saved_bridge_poll_context = bridge_poll_context_tls;
    saved_bridge_poll_original = bridge_poll_original_tls;
    bridge_poll_context_tls = context;
    bridge_poll_original_tls = bridge_poll_original_for(context);
    ret = real_g_main_context_iteration(context, may_block);
    saved_errno = errno;
    bridge_poll_context_tls = saved_bridge_poll_context;
    bridge_poll_original_tls = saved_bridge_poll_original;
    end = now_us();
    active_span_exit(&span, end, &ready_during_span, &ready_offset_us,
                     &span_ready_seq);
    qt_bridge_trace_exit("glib_iteration", &span, bridge_enter_emitted,
                         begin, end, ret, 0, context, may_block, 1,
                         ready_during_span, ready_offset_us);
    if (trace_enabled() && process_is_konsole())
        trace_glib_call("iteration", context, may_block, ret, begin,
                        end, &span, ready_during_span, ready_offset_us,
                        span_ready_seq);
    errno = saved_errno;
    return ret;
}

gboolean g_main_context_pending(GMainContext *context)
{
    uint64_t begin;
    gboolean ret;
    int saved_errno;

    if (!real_g_main_context_pending)
        real_g_main_context_pending =
            (g_main_context_pending_fn_t)resolve_glib(
                "g_main_context_pending");
    if (!real_g_main_context_pending)
        return 0;
    begin = now_us();
    ret = real_g_main_context_pending(context);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_glib_call("pending", context, -1, ret, begin, now_us(), NULL,
                        0, 0, 0);
    errno = saved_errno;
    return ret;
}

void g_main_context_wakeup(GMainContext *context)
{
    uint64_t begin;
    int saved_errno;
    int install_errno;

    if (!real_g_main_context_wakeup)
        real_g_main_context_wakeup =
            (g_main_context_wakeup_fn_t)resolve_glib(
                "g_main_context_wakeup");
    if (!real_g_main_context_wakeup)
        return;
    if (qt_bridge_trace_enabled()) {
        install_errno = errno;
        install_bridge_poll_func(context);
        errno = install_errno;
    }
    begin = now_us();
    real_g_main_context_wakeup(context);
    saved_errno = errno;
    if (trace_enabled() && process_is_konsole())
        trace_glib_call("wakeup", context, -1, 0, begin, now_us(), NULL, 0,
                        0, 0);
    errno = saved_errno;
}

gboolean g_main_context_prepare(GMainContext *context, gint *priority)
{
    uint64_t begin;
    uint64_t end;
    struct glib_phase_record phase;
    int ready_during_phase = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long phase_ready_seq = 0;
    gboolean ret;
    int saved_errno;
    int priority_value = priority ? *priority : 0;

    if (!real_g_main_context_prepare)
        real_g_main_context_prepare =
            (g_main_context_prepare_fn_t)resolve_glib(
                "g_main_context_prepare");
    if (!real_g_main_context_prepare)
        return 0;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_g_main_context_prepare(context, priority);
    begin = now_us();
    phase = glib_phase_enter(GLIB_PHASE_PREPARE, context, begin);
    ret = real_g_main_context_prepare(context, priority);
    saved_errno = errno;
    end = now_us();
    if (priority)
        priority_value = *priority;
    glib_phase_exit(&phase, end, &ready_during_phase, &ready_offset_us,
                    &phase_ready_seq);
    trace_glib_phase_call(GLIB_PHASE_PREPARE, context, priority_value, 0, 0,
                          ret, 1, begin, end, &phase, ready_during_phase,
                          ready_offset_us, phase_ready_seq);
    errno = saved_errno;
    return ret;
}

gint g_main_context_query(GMainContext *context, gint max_priority,
                          gint *timeout_, GPollFD *fds, gint n_fds)
{
    uint64_t begin;
    uint64_t end;
    struct glib_phase_record phase;
    int ready_during_phase = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long phase_ready_seq = 0;
    gint ret;
    int saved_errno;
    int timeout_value = timeout_ ? *timeout_ : 0;

    if (!real_g_main_context_query)
        real_g_main_context_query =
            (g_main_context_query_fn_t)resolve_glib("g_main_context_query");
    if (!real_g_main_context_query)
        return 0;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_g_main_context_query(context, max_priority, timeout_, fds,
                                         n_fds);
    begin = now_us();
    phase = glib_phase_enter(GLIB_PHASE_QUERY, context, begin);
    ret = real_g_main_context_query(context, max_priority, timeout_, fds,
                                    n_fds);
    saved_errno = errno;
    end = now_us();
    if (timeout_)
        timeout_value = *timeout_;
    glib_phase_exit(&phase, end, &ready_during_phase, &ready_offset_us,
                    &phase_ready_seq);
    trace_glib_phase_call(GLIB_PHASE_QUERY, context, max_priority,
                          timeout_value, n_fds < 0 ? 0 : (unsigned int)n_fds,
                          ret, 1, begin, end, &phase, ready_during_phase,
                          ready_offset_us, phase_ready_seq);
    errno = saved_errno;
    return ret;
}

gboolean g_main_context_check(GMainContext *context, gint max_priority,
                              GPollFD *fds, gint n_fds)
{
    uint64_t begin;
    uint64_t end;
    struct glib_phase_record phase;
    int ready_during_phase = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long phase_ready_seq = 0;
    gboolean ret;
    int saved_errno;

    if (!real_g_main_context_check)
        real_g_main_context_check =
            (g_main_context_check_fn_t)resolve_glib("g_main_context_check");
    if (!real_g_main_context_check)
        return 0;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_g_main_context_check(context, max_priority, fds, n_fds);
    begin = now_us();
    phase = glib_phase_enter(GLIB_PHASE_CHECK, context, begin);
    ret = real_g_main_context_check(context, max_priority, fds, n_fds);
    saved_errno = errno;
    end = now_us();
    glib_phase_exit(&phase, end, &ready_during_phase, &ready_offset_us,
                    &phase_ready_seq);
    trace_glib_phase_call(GLIB_PHASE_CHECK, context, max_priority, 0,
                          n_fds < 0 ? 0 : (unsigned int)n_fds, ret, 1,
                          begin, end, &phase, ready_during_phase,
                          ready_offset_us, phase_ready_seq);
    errno = saved_errno;
    return ret;
}

void g_main_context_dispatch(GMainContext *context)
{
    uint64_t begin;
    uint64_t end;
    struct glib_phase_record phase;
    int ready_during_phase = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long phase_ready_seq = 0;
    int saved_errno;

    if (!real_g_main_context_dispatch)
        real_g_main_context_dispatch =
            (g_main_context_dispatch_fn_t)resolve_glib(
                "g_main_context_dispatch");
    if (!real_g_main_context_dispatch)
        return;
    if (!trace_enabled() || in_trace || !process_is_konsole()) {
        real_g_main_context_dispatch(context);
        return;
    }
    begin = now_us();
    phase = glib_phase_enter(GLIB_PHASE_DISPATCH, context, begin);
    real_g_main_context_dispatch(context);
    saved_errno = errno;
    end = now_us();
    glib_phase_exit(&phase, end, &ready_during_phase, &ready_offset_us,
                    &phase_ready_seq);
    trace_glib_phase_call(GLIB_PHASE_DISPATCH, context, 0, 0, 0, 0, 0,
                          begin, end, &phase, ready_during_phase,
                          ready_offset_us, phase_ready_seq);
    errno = saved_errno;
}

gint g_poll(GPollFD *fds, guint nfds, gint timeout)
{
    uint64_t begin;
    uint64_t end;
    struct glib_phase_record phase;
    int ready_during_phase = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long phase_ready_seq = 0;
    gint ret;
    int saved_errno;

    if (!real_g_poll)
        real_g_poll = (g_poll_fn_t)resolve_glib("g_poll");
    if (!real_g_poll)
        return -1;
    if (!trace_enabled() || in_trace || !process_is_konsole())
        return real_g_poll(fds, nfds, timeout);
    begin = now_us();
    phase = glib_phase_enter(GLIB_PHASE_POLL, NULL, begin);
    ret = real_g_poll(fds, nfds, timeout);
    saved_errno = errno;
    end = now_us();
    record_gpoll_return(fds, nfds, ret, saved_errno, begin, end);
    glib_phase_exit(&phase, end, &ready_during_phase, &ready_offset_us,
                    &phase_ready_seq);
    trace_glib_phase_call(GLIB_PHASE_POLL, NULL, timeout, 0, nfds, ret, 1,
                          begin, end, &phase, ready_during_phase,
                          ready_offset_us, phase_ready_seq);
    errno = saved_errno;
    return ret;
}

static void *resolve_qt_symbol(const char *name)
{
    return dlsym(RTLD_NEXT, name);
}

static void trace_qt_cpp_call(enum qt_cpp_op_id op_id, int flags, int ret,
                              int has_ret, int saved_errno, const void *arg0,
                              const void *arg1, const void *arg2,
                              uint64_t begin, uint64_t end,
                              const struct active_span_record *span,
                              int ready_during_span,
                              uint64_t ready_offset_us,
                              unsigned long long span_ready_seq)
{
    struct qt_cpp_op_state *op;
    uint64_t elapsed = end >= begin ? end - begin : 0;
    uint64_t ready_gap = ready_to_call_gap(begin);
    char comm[TRACE_COMM_LEN];
    unsigned long long call_id;
    int span_op = ACTIVE_SPAN_NONE;
    uint64_t span_start = 0;
    unsigned int span_depth = 0;
    unsigned long long span_call_id = 0;
    long span_tid = 0;
    int span_active = 0;
    uint64_t span_age = 0;
    int glib_phase_op;
    uint64_t glib_phase_start;
    unsigned int glib_phase_depth;
    unsigned long long glib_phase_call_id;
    long glib_phase_tid;
    uintptr_t glib_phase_context;
    int glib_phase_active = 0;
    uint64_t glib_phase_age = 0;
    void *qwayland_wl_display = NULL;
    int qwayland_wl_fd = -1;

    if (!qt_cpp_trace_enabled() || op_id < 0 || op_id >= QT_CPP_OP_COUNT)
        return;
    op = &qt_cpp_ops[op_id];
    call_id = __sync_add_and_fetch(&qt_cpp_total_calls, 1);
    op->calls++;
    if (elapsed > op->max_elapsed_us)
        op->max_elapsed_us = elapsed;
    if (ready_gap > op->max_ready_us)
        op->max_ready_us = ready_gap;
    if (ready_gap > qt_cpp_max_ready_to_call_us)
        qt_cpp_max_ready_to_call_us = ready_gap;
    if (span && span->active) {
        span_active = 1;
        span_op = span->op_id;
        span_start = span->start_us;
        span_depth = span->depth;
        span_call_id = span->call_id;
        span_tid = trace_gettid();
    } else {
        span_op = __atomic_load_n(&qt_active_span_op, __ATOMIC_ACQUIRE);
        span_start = __atomic_load_n(&qt_active_span_start_us, __ATOMIC_ACQUIRE);
        span_depth = __atomic_load_n(&qt_active_span_depth, __ATOMIC_ACQUIRE);
        span_call_id =
            __atomic_load_n(&qt_active_span_call_id, __ATOMIC_ACQUIRE);
        span_tid = __atomic_load_n(&qt_active_span_tid, __ATOMIC_ACQUIRE);
        if (span_op > ACTIVE_SPAN_NONE && span_op < ACTIVE_SPAN_OP_COUNT &&
            span_start && end >= span_start)
            span_active = 1;
    }
    if (span_active && end >= span_start)
        span_age = end - span_start;

    glib_phase_op = __atomic_load_n(&qt_glib_phase_op, __ATOMIC_ACQUIRE);
    glib_phase_start =
        __atomic_load_n(&qt_glib_phase_start_us, __ATOMIC_ACQUIRE);
    glib_phase_depth =
        __atomic_load_n(&qt_glib_phase_depth, __ATOMIC_ACQUIRE);
    glib_phase_call_id =
        __atomic_load_n(&qt_glib_phase_call_id, __ATOMIC_ACQUIRE);
    glib_phase_tid = __atomic_load_n(&qt_glib_phase_tid, __ATOMIC_ACQUIRE);
    glib_phase_context =
        __atomic_load_n(&qt_glib_phase_context, __ATOMIC_ACQUIRE);
    if (glib_phase_op > GLIB_PHASE_NONE &&
        glib_phase_op < GLIB_PHASE_OP_COUNT && glib_phase_start &&
        end >= glib_phase_start) {
        glib_phase_active = 1;
        glib_phase_age = end - glib_phase_start;
    }
    if (elapsed < (uint64_t)qt_cpp_threshold_us &&
        ready_gap < (uint64_t)qt_cpp_threshold_us && !ready_during_span)
        return;
    qt_cpp_total_slow++;
    op->slow++;
    if (op_id == QT_CPP_QWAYLAND_HANDLE_SYNC ||
        op_id == QT_CPP_QWAYLAND_REQUEST_SYNC ||
        op_id == QT_CPP_QWAYLAND_BLOCKING_READ ||
        op_id == QT_CPP_QWAYLAND_FLUSH_REQUESTS) {
        qwayland_wl_display = qwayland_private_wl_display((void *)arg0);
        qwayland_wl_fd = display_fd((const struct wl_display *)qwayland_wl_display);
    }
    thread_comm(comm, sizeof(comm));
    trace_write_line("konsole-qt-cpp-trace: call op=%s tid=%ld comm=%s "
                     "call_id=%llu flags=0x%x ret=%d has_ret=%d errno=%d "
                     "elapsed_us=%llu "
                     "ready_to_call_us=%llu ready_seq=%llu ready_tid=%ld "
                     "last_revents=0x%x last_fionread=%d self=%p event=%p "
                     "arg0=%p arg1=%p arg2=%p span_active=%d span_op=%s "
                     "span_age_us=%llu span_depth=%u span_call_id=%llu "
                     "span_tid=%ld main_glib_phase_active=%d "
                     "main_glib_phase=%s main_glib_phase_age_us=%llu "
                     "main_glib_phase_depth=%u main_glib_phase_call_id=%llu "
                     "main_glib_phase_tid=%ld main_glib_phase_context=%p "
                     "qwayland_private_wl_display=%p "
                     "qwayland_private_wl_fd=%d "
                     "fd3_ready_during_span=%d "
                     "fd3_ready_offset_us=%llu fd3_ready_seq=%llu",
                     op->name, trace_gettid(), comm, call_id, flags, ret,
                     has_ret, saved_errno,
                     (unsigned long long)elapsed,
                     (unsigned long long)ready_gap, qt_fd3_ready_sequence,
                     qt_last_fd3_ready_tid, qt_last_fd3_ready_revents,
                     qt_last_fd3_ready_fionread, arg0, arg1, arg0, arg1,
                     arg2, span_active,
                     active_span_name((enum active_span_op_id)span_op),
                     (unsigned long long)span_age, span_depth, span_call_id,
                     span_tid, glib_phase_active,
                     glib_phase_name((enum glib_phase_op_id)glib_phase_op),
                     (unsigned long long)glib_phase_age, glib_phase_depth,
                     glib_phase_call_id, glib_phase_tid,
                     (void *)glib_phase_context, qwayland_wl_display,
                     qwayland_wl_fd, ready_during_span,
                     (unsigned long long)ready_offset_us, span_ready_seq);
}

qt_bool_t _ZN20QEventDispatcherGlib13processEventsE6QFlagsIN10QEventLoop17ProcessEventsFlagEE(
    void *self, int flags)
{
    uint64_t begin;
    uint64_t end;
    struct active_span_record span;
    int ready_during_span = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long span_ready_seq = 0;
    int bridge_enter_emitted = 0;
    qt_bool_t ret;
    int saved_errno;

    if (!real_qt_process_events)
        real_qt_process_events = (qt_process_events_fn_t)resolve_qt_symbol(
            "_ZN20QEventDispatcherGlib13processEventsE6QFlagsIN10QEventLoop17ProcessEventsFlagEE");
    if (!real_qt_process_events)
        return 0;
    begin = now_us();
    span = active_span_enter(ACTIVE_SPAN_PROCESS_EVENTS, begin);
    bridge_enter_emitted = qt_bridge_trace_enter("process_events", &span,
                                                 begin, flags, NULL, 0, 0);
    ret = real_qt_process_events(self, flags);
    saved_errno = errno;
    end = now_us();
    active_span_exit(&span, end, &ready_during_span, &ready_offset_us,
                     &span_ready_seq);
    qt_bridge_trace_exit("process_events", &span, bridge_enter_emitted,
                         begin, end, ret, flags, NULL, 0, 0,
                         ready_during_span, ready_offset_us);
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_PROCESS_EVENTS, flags, ret, 1, saved_errno,
                          self, NULL, NULL, begin, end, &span, ready_during_span,
                          ready_offset_us, span_ready_seq);
    errno = saved_errno;
    return ret;
}

qt_bool_t _ZN22QWindowSystemInterface22sendWindowSystemEventsE6QFlagsIN10QEventLoop17ProcessEventsFlagEE(
    int flags)
{
    uint64_t begin;
    uint64_t end;
    struct active_span_record span;
    int ready_during_span = 0;
    uint64_t ready_offset_us = 0;
    unsigned long long span_ready_seq = 0;
    qt_bool_t ret;
    int saved_errno;

    if (!real_qt_send_window_system_events)
        real_qt_send_window_system_events =
            (qt_window_system_events_fn_t)resolve_qt_symbol(
                "_ZN22QWindowSystemInterface22sendWindowSystemEventsE6QFlagsIN10QEventLoop17ProcessEventsFlagEE");
    if (!real_qt_send_window_system_events)
        return 0;
    begin = now_us();
    span = active_span_enter(ACTIVE_SPAN_SEND_WINDOW_SYSTEM_EVENTS, begin);
    ret = real_qt_send_window_system_events(flags);
    saved_errno = errno;
    end = now_us();
    active_span_exit(&span, end, &ready_during_span, &ready_offset_us,
                     &span_ready_seq);
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_SEND_WINDOW_SYSTEM_EVENTS, flags, ret, 1,
                          saved_errno, NULL, NULL, NULL, begin, end, &span,
                          ready_during_span, ready_offset_us,
                          span_ready_seq);
    errno = saved_errno;
    return ret;
}

qt_bool_t _ZN22QWindowSystemInterface23flushWindowSystemEventsE6QFlagsIN10QEventLoop17ProcessEventsFlagEE(
    int flags)
{
    uint64_t begin;
    qt_bool_t ret;
    int saved_errno;

    if (!real_qt_flush_window_system_events)
        real_qt_flush_window_system_events =
            (qt_window_system_events_fn_t)resolve_qt_symbol(
                "_ZN22QWindowSystemInterface23flushWindowSystemEventsE6QFlagsIN10QEventLoop17ProcessEventsFlagEE");
    if (!real_qt_flush_window_system_events)
        return 0;
    begin = now_us();
    ret = real_qt_flush_window_system_events(flags);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_FLUSH_WINDOW_SYSTEM_EVENTS, flags, ret, 1,
                          saved_errno, NULL, NULL, NULL, begin, now_us(), NULL,
                          0, 0, 0);
    errno = saved_errno;
    return ret;
}

void _ZN16QCoreApplication16sendPostedEventsEP7QObjecti(void *receiver,
                                                        int event_type)
{
    uint64_t begin;
    int saved_errno;

    if (!real_qt_core_send_posted_events)
        real_qt_core_send_posted_events =
            (qt_send_posted_events_fn_t)resolve_qt_symbol(
                "_ZN16QCoreApplication16sendPostedEventsEP7QObjecti");
    if (!real_qt_core_send_posted_events)
        return;
    begin = now_us();
    real_qt_core_send_posted_events(receiver, event_type);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_CORE_SEND_POSTED_EVENTS, event_type, 0, 0,
                          saved_errno, receiver, NULL, NULL, begin, now_us(),
                          NULL, 0, 0, 0);
    errno = saved_errno;
}

void _ZN23QCoreApplicationPrivate16sendPostedEventsEP7QObjectiP11QThreadData(
    void *receiver, int event_type, void *thread_data)
{
    uint64_t begin;
    int saved_errno;

    if (!real_qt_private_send_posted_events)
        real_qt_private_send_posted_events =
            (qt_private_send_posted_events_fn_t)resolve_qt_symbol(
                "_ZN23QCoreApplicationPrivate16sendPostedEventsEP7QObjectiP11QThreadData");
    if (!real_qt_private_send_posted_events)
        return;
    begin = now_us();
    real_qt_private_send_posted_events(receiver, event_type, thread_data);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_PRIVATE_SEND_POSTED_EVENTS, event_type, 0,
                          0, saved_errno, receiver, thread_data, NULL, begin,
                          now_us(), NULL, 0, 0, 0);
    errno = saved_errno;
}

qt_bool_t _ZN16QCoreApplication15notifyInternal2EP7QObjectP6QEvent(
    void *receiver, void *event)
{
    uint64_t begin;
    qt_bool_t ret;
    int saved_errno;

    if (!real_qt_notify_internal2)
        real_qt_notify_internal2 = (qt_notify_internal2_fn_t)resolve_qt_symbol(
            "_ZN16QCoreApplication15notifyInternal2EP7QObjectP6QEvent");
    if (!real_qt_notify_internal2)
        return 0;
    begin = now_us();
    ret = real_qt_notify_internal2(receiver, event);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_NOTIFY_INTERNAL2, 0, ret, 1, saved_errno,
                          receiver, event, NULL, begin, now_us(), NULL, 0, 0,
                          0);
    errno = saved_errno;
    return ret;
}

qt_bool_t _ZN15QSocketNotifier5eventEP6QEvent(void *self, void *event)
{
    uint64_t begin;
    qt_bool_t ret;
    int saved_errno;

    if (!real_qt_qsocket_notifier_event)
        real_qt_qsocket_notifier_event =
            (qt_qsocket_notifier_event_fn_t)resolve_qt_symbol(
                "_ZN15QSocketNotifier5eventEP6QEvent");
    if (!real_qt_qsocket_notifier_event)
        return 0;
    begin = now_us();
    ret = real_qt_qsocket_notifier_event(self, event);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_QSOCKETNOTIFIER_EVENT, 0, ret, 1,
                          saved_errno, self, event, NULL, begin, now_us(),
                          NULL, 0, 0, 0);
    errno = saved_errno;
    return ret;
}

void _ZN15QtWaylandClient15QWaylandDisplay17handleWaylandSyncEv(void *self)
{
    uint64_t begin;
    int saved_errno;

    if (!real_qt_qwayland_handle_sync)
        real_qt_qwayland_handle_sync =
            (qt_qwayland_void_fn_t)resolve_qt_symbol(
                "_ZN15QtWaylandClient15QWaylandDisplay17handleWaylandSyncEv");
    if (!real_qt_qwayland_handle_sync)
        return;
    begin = now_us();
    real_qt_qwayland_handle_sync(self);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_QWAYLAND_HANDLE_SYNC, 0, 0, 0, saved_errno,
                          self, NULL, NULL, begin, now_us(), NULL, 0, 0, 0);
    errno = saved_errno;
}

void _ZN15QtWaylandClient15QWaylandDisplay18requestWaylandSyncEv(void *self)
{
    uint64_t begin;
    int saved_errno;

    if (!real_qt_qwayland_request_sync)
        real_qt_qwayland_request_sync =
            (qt_qwayland_void_fn_t)resolve_qt_symbol(
                "_ZN15QtWaylandClient15QWaylandDisplay18requestWaylandSyncEv");
    if (!real_qt_qwayland_request_sync)
        return;
    begin = now_us();
    real_qt_qwayland_request_sync(self);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_QWAYLAND_REQUEST_SYNC, 0, 0, 0, saved_errno,
                          self, NULL, NULL, begin, now_us(), NULL, 0, 0, 0);
    errno = saved_errno;
}

int _ZN15QtWaylandClient15QWaylandDisplay18blockingReadEventsEv(void *self)
{
    uint64_t begin;
    int ret;
    int saved_errno;

    if (!real_qt_qwayland_blocking_read)
        real_qt_qwayland_blocking_read =
            (qt_qwayland_int_fn_t)resolve_qt_symbol(
                "_ZN15QtWaylandClient15QWaylandDisplay18blockingReadEventsEv");
    if (!real_qt_qwayland_blocking_read)
        return -1;
    begin = now_us();
    ret = real_qt_qwayland_blocking_read(self);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_QWAYLAND_BLOCKING_READ, 0, ret, 1,
                          saved_errno, self, NULL, NULL, begin, now_us(),
                          NULL, 0, 0, 0);
    errno = saved_errno;
    return ret;
}

void _ZN15QtWaylandClient15QWaylandDisplay13flushRequestsEv(void *self)
{
    uint64_t begin;
    int saved_errno;

    if (!real_qt_qwayland_flush_requests)
        real_qt_qwayland_flush_requests =
            (qt_qwayland_void_fn_t)resolve_qt_symbol(
                "_ZN15QtWaylandClient15QWaylandDisplay13flushRequestsEv");
    if (!real_qt_qwayland_flush_requests)
        return;
    begin = now_us();
    real_qt_qwayland_flush_requests(self);
    saved_errno = errno;
    if (qt_cpp_trace_enabled() && process_is_konsole())
        trace_qt_cpp_call(QT_CPP_QWAYLAND_FLUSH_REQUESTS, 0, 0, 0,
                          saved_errno, self, NULL, NULL, begin, now_us(),
                          NULL, 0, 0, 0);
    errno = saved_errno;
}

__attribute__((destructor)) static void trace_summary(void)
{
    if (!trace_enabled() || !process_is_konsole())
        return;
    if (loader_trace_enabled()) {
        loader_trace_write_line_force(
            "konsole-loader-trace: event=summary pid=%ld launch_index=%s "
            "launch_temperature=%s repeat_count=%s calls=%llu "
            "open_calls=%llu openat_calls=%llu dlopen_calls=%llu "
            "enoent=%llu dropped=%llu output_lines=%llu output_limit=%llu "
            "total_elapsed_us=%llu",
            (long)getpid(),
            loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_INDEX", "missing"),
            loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_TEMPERATURE",
                             "missing"),
            loader_env_value("KDE_SMOKE_DIRECT_LAUNCH_REPEAT_COUNT",
                             "missing"),
            loader_calls, loader_open_calls, loader_openat_calls,
            loader_dlopen_calls, loader_enoent_count, loader_output_dropped,
            loader_output_lines, loader_output_limit,
            loader_total_elapsed_us);
    }
    if (wayland_trace_enabled()) {
        trace_write_line_force("konsole-wayland-event-trace: summary pid=%ld "
                               "poll_calls=%llu poll_slow=%llu "
                               "ppoll_calls=%llu ppoll_slow=%llu "
                               "wl_calls=%llu wl_slow=%llu read_calls=%llu "
                               "write_calls=%llu max_poll_us=%llu "
                               "max_wl_us=%llu max_write_to_poll_us=%llu "
                               "max_poll_to_read_us=%llu output_lines=%llu "
                               "output_dropped=%llu output_limit=%llu",
                               (long)getpid(), poll_calls, poll_slow,
                               ppoll_calls, ppoll_slow, wl_calls, wl_slow,
                               read_calls, write_calls,
                               (unsigned long long)max_poll_us,
                               (unsigned long long)max_wl_us,
                               (unsigned long long)max_write_to_poll_us,
                               (unsigned long long)max_poll_to_read_us,
                               output_lines, output_dropped, output_limit);
    }
    if (qt_trace_enabled()) {
        trace_write_line_force("konsole-qt-event-trace: summary pid=%ld "
                               "fd3_ready=%llu last_ready_seq=%llu "
                               "last_ready_tid=%ld last_ready_comm=%s "
                               "last_ready_revents=0x%x "
                               "last_ready_fionread=%d "
                               "fd3_ready_in_active_span=%llu "
                               "fd3_ready_in_process_events=%llu "
                               "fd3_ready_in_glib_iteration=%llu "
                               "fd3_ready_in_send_wse=%llu "
                               "span_ready_during_calls=%llu "
                               "max_fd3_ready_span_age_us=%llu "
                               "max_fd3_ready_span_depth=%u "
                               "max_span_ready_offset_us=%llu "
                               "glib_iteration_calls=%llu "
                               "glib_pending_calls=%llu "
                               "glib_wakeup_calls=%llu glib_slow=%llu "
                               "glib_phase_calls=%llu "
                               "glib_prepare_calls=%llu "
                               "glib_query_calls=%llu "
                               "glib_check_calls=%llu "
                               "glib_dispatch_calls=%llu "
                               "glib_poll_calls=%llu "
                               "glib_poll_total_us=%llu "
                               "glib_dispatch_total_us=%llu "
                               "max_glib_prepare_us=%llu "
                               "max_glib_query_us=%llu "
                               "max_glib_check_us=%llu "
                               "max_glib_dispatch_us=%llu "
                               "max_glib_poll_us=%llu "
                               "fd3_ready_in_glib_poll=%llu "
                               "fd3_ready_in_glib_dispatch=%llu "
                               "fd3_ready_in_glib_other_phase=%llu "
                               "max_fd3_ready_glib_poll_age_us=%llu "
                               "max_fd3_ready_glib_dispatch_age_us=%llu "
                               "max_fd3_ready_glib_other_age_us=%llu "
                               "qt_bridge_enter_count=%llu "
                               "qt_bridge_exit_count=%llu "
                               "qt_bridge_process_events_enter_count=%llu "
                               "qt_bridge_process_events_exit_count=%llu "
                               "qt_bridge_glib_iteration_enter_count=%llu "
                               "qt_bridge_glib_iteration_exit_count=%llu "
                               "max_qt_bridge_ready_to_process_events_us=%llu "
                               "max_qt_bridge_ready_to_glib_iteration_us=%llu "
                               "max_qt_bridge_process_events_elapsed_us=%llu "
                               "max_qt_bridge_glib_iteration_elapsed_us=%llu "
                               "qt_bridge_public_wl_delta_max_us=%llu "
                               "qt_bridge_wl_fd_syscall_delta_max_us=%llu "
                               "qt_bridge_poll_calls=%llu "
                               "qt_bridge_poll_slow=%llu "
                               "qt_bridge_poll_fd3_ready=%llu "
                               "qt_bridge_poll_fd3_ready_during=%llu "
                               "qt_bridge_poll_install_attempts=%llu "
                               "qt_bridge_poll_install_null_context=%llu "
                               "qt_bridge_poll_install_missing_get_poll_func=%llu "
                               "qt_bridge_poll_install_missing_set_poll_func=%llu "
                               "qt_bridge_poll_install_installed=%llu "
                               "qt_bridge_poll_install_already_wrapped=%llu "
                               "qt_bridge_poll_install_slot_full=%llu "
                               "qt_bridge_poll_install_last_context=0x%llx "
                               "qt_bridge_poll_max_elapsed_us=%llu "
                               "qt_bridge_poll_max_fd3_ready_offset_us=%llu "
                               "wl_main_calls=%llu wl_main_slow=%llu "
                               "max_ready_to_call_us=%llu "
                               "max_ready_to_glib_us=%llu "
                               "max_ready_to_wl_us=%llu "
                               "max_glib_iteration_us=%llu "
                               "max_glib_pending_us=%llu "
                               "max_glib_wakeup_us=%llu "
                               "max_wl_main_us=%llu output_lines=%llu "
                               "output_dropped=%llu output_limit=%llu",
                               (long)getpid(), qt_fd3_ready_count,
                               qt_fd3_ready_sequence, qt_last_fd3_ready_tid,
                               qt_last_fd3_ready_comm,
                               qt_last_fd3_ready_revents,
                               qt_last_fd3_ready_fionread,
                               qt_fd3_ready_in_active_span,
                               qt_fd3_ready_in_process_events,
                               qt_fd3_ready_in_glib_iteration,
                               qt_fd3_ready_in_send_wse,
                               qt_span_ready_during_calls,
                               (unsigned long long)
                                   qt_max_fd3_ready_span_age_us,
                               qt_max_fd3_ready_span_depth,
                               (unsigned long long)
                                   qt_max_span_ready_offset_us,
                               qt_glib_iteration_calls,
                               qt_glib_pending_calls, qt_glib_wakeup_calls,
                               qt_glib_slow,
                               glib_phase_calls[GLIB_PHASE_PREPARE] +
                                   glib_phase_calls[GLIB_PHASE_QUERY] +
                                   glib_phase_calls[GLIB_PHASE_CHECK] +
                                   glib_phase_calls[GLIB_PHASE_DISPATCH] +
                                   glib_phase_calls[GLIB_PHASE_POLL],
                               glib_phase_calls[GLIB_PHASE_PREPARE],
                               glib_phase_calls[GLIB_PHASE_QUERY],
                               glib_phase_calls[GLIB_PHASE_CHECK],
                               glib_phase_calls[GLIB_PHASE_DISPATCH],
                               glib_phase_calls[GLIB_PHASE_POLL],
                               (unsigned long long)
                                   glib_phase_total_us[GLIB_PHASE_POLL],
                               (unsigned long long)
                                   glib_phase_total_us[GLIB_PHASE_DISPATCH],
                               (unsigned long long)
                                   glib_phase_max_us[GLIB_PHASE_PREPARE],
                               (unsigned long long)
                                   glib_phase_max_us[GLIB_PHASE_QUERY],
                               (unsigned long long)
                                   glib_phase_max_us[GLIB_PHASE_CHECK],
                               (unsigned long long)
                                   glib_phase_max_us[GLIB_PHASE_DISPATCH],
                               (unsigned long long)
                                   glib_phase_max_us[GLIB_PHASE_POLL],
                               qt_fd3_ready_in_glib_poll,
                               qt_fd3_ready_in_glib_dispatch,
                               qt_fd3_ready_in_glib_other_phase,
                               (unsigned long long)
                                   qt_max_fd3_ready_glib_poll_age_us,
                               (unsigned long long)
                                   qt_max_fd3_ready_glib_dispatch_age_us,
                               (unsigned long long)
                                   qt_max_fd3_ready_glib_other_age_us,
                               qt_bridge_enter_count,
                               qt_bridge_exit_count,
                               qt_bridge_process_events_enter_count,
                               qt_bridge_process_events_exit_count,
                               qt_bridge_glib_iteration_enter_count,
                               qt_bridge_glib_iteration_exit_count,
                               (unsigned long long)
                                   qt_bridge_max_ready_to_process_events_us,
                               (unsigned long long)
                                   qt_bridge_max_ready_to_glib_iteration_us,
                               (unsigned long long)
                                   qt_bridge_max_process_events_elapsed_us,
                               (unsigned long long)
                                   qt_bridge_max_glib_iteration_elapsed_us,
                               (unsigned long long)
                                   qt_bridge_public_wl_delta_max_us,
                               (unsigned long long)
                                   qt_bridge_wl_fd_syscall_delta_max_us,
                               qt_bridge_poll_calls,
                               qt_bridge_poll_slow,
                               qt_bridge_poll_fd3_ready,
                               qt_bridge_poll_fd3_ready_during,
                               qt_bridge_poll_install_attempts,
                               qt_bridge_poll_install_null_context,
                               qt_bridge_poll_install_missing_get_poll_func,
                               qt_bridge_poll_install_missing_set_poll_func,
                               qt_bridge_poll_install_installed,
                               qt_bridge_poll_install_already_wrapped,
                               qt_bridge_poll_install_slot_full,
                               (unsigned long long)__atomic_load_n(
                                   &qt_bridge_poll_install_last_context,
                                   __ATOMIC_RELAXED),
                               (unsigned long long)qt_bridge_poll_max_elapsed_us,
                               (unsigned long long)
                                   qt_bridge_poll_max_fd3_ready_offset_us,
                               qt_wl_main_calls,
                               qt_wl_main_slow,
                               (unsigned long long)qt_max_ready_to_call_us,
                               (unsigned long long)qt_max_ready_to_glib_us,
                               (unsigned long long)qt_max_ready_to_wl_us,
                               (unsigned long long)qt_max_glib_iteration_us,
                               (unsigned long long)qt_max_glib_pending_us,
                               (unsigned long long)qt_max_glib_wakeup_us,
                               (unsigned long long)qt_max_wl_main_us,
                               output_lines, output_dropped, output_limit);
    }
    if (qt_cpp_trace_enabled()) {
        trace_write_line_force(
            "konsole-qt-cpp-trace: summary pid=%ld "
            "fd3_ready=%llu last_ready_seq=%llu last_ready_tid=%ld "
            "last_ready_comm=%s last_ready_revents=0x%x "
            "last_ready_fionread=%d total_calls=%llu total_slow=%llu "
            "fd3_ready_in_active_span=%llu "
            "fd3_ready_in_process_events=%llu "
            "fd3_ready_in_glib_iteration=%llu "
            "fd3_ready_in_send_wse=%llu "
            "span_ready_during_calls=%llu "
            "max_fd3_ready_span_age_us=%llu "
            "max_fd3_ready_span_depth=%u "
            "max_span_ready_offset_us=%llu "
            "process_events_calls=%llu process_events_slow=%llu "
            "send_wse_calls=%llu send_wse_slow=%llu "
            "flush_wse_calls=%llu flush_wse_slow=%llu "
            "core_send_posted_calls=%llu core_send_posted_slow=%llu "
            "private_send_posted_calls=%llu private_send_posted_slow=%llu "
            "notify_internal2_calls=%llu notify_internal2_slow=%llu "
            "qsocketnotifier_event_calls=%llu "
            "qsocketnotifier_event_slow=%llu "
            "qwayland_handle_sync_calls=%llu "
            "qwayland_handle_sync_slow=%llu "
            "qwayland_request_sync_calls=%llu "
            "qwayland_request_sync_slow=%llu "
            "qwayland_blocking_read_calls=%llu "
            "qwayland_blocking_read_slow=%llu "
            "qwayland_flush_requests_calls=%llu "
            "qwayland_flush_requests_slow=%llu "
            "max_ready_to_call_us=%llu "
            "max_ready_to_process_events_us=%llu "
            "max_ready_to_send_wse_us=%llu "
            "max_ready_to_flush_wse_us=%llu "
            "max_ready_to_core_send_posted_us=%llu "
            "max_ready_to_private_send_posted_us=%llu "
            "max_ready_to_notify_internal2_us=%llu "
            "max_ready_to_qsocketnotifier_event_us=%llu "
            "max_ready_to_qwayland_handle_sync_us=%llu "
            "max_ready_to_qwayland_request_sync_us=%llu "
            "max_ready_to_qwayland_blocking_read_us=%llu "
            "max_ready_to_qwayland_flush_requests_us=%llu "
            "max_process_events_us=%llu max_send_wse_us=%llu "
            "max_flush_wse_us=%llu max_core_send_posted_us=%llu "
            "max_private_send_posted_us=%llu max_notify_internal2_us=%llu "
            "max_qsocketnotifier_event_us=%llu "
            "max_qwayland_handle_sync_us=%llu "
            "max_qwayland_request_sync_us=%llu "
            "max_qwayland_blocking_read_us=%llu "
            "max_qwayland_flush_requests_us=%llu "
            "qtwayland_public_calls=%llu "
            "public_wl_event_thread_calls=%llu "
            "public_wl_main_thread_calls=%llu "
            "wl_connect_calls=%llu wl_connect_to_fd_calls=%llu "
            "wl_get_fd_calls=%llu "
            "wl_prepare_read_calls=%llu "
            "wl_prepare_read_queue_calls=%llu "
            "wl_cancel_read_calls=%llu "
            "wl_read_events_calls=%llu "
            "wl_dispatch_calls=%llu "
            "wl_dispatch_pending_calls=%llu "
            "wl_dispatch_queue_pending_calls=%llu "
            "wl_flush_calls=%llu "
            "wl_roundtrip_calls=%llu "
            "wl_roundtrip_queue_calls=%llu "
            "wl_proxy_add_listener_calls=%llu "
            "wl_proxy_destroy_calls=%llu "
            "wl_proxy_get_id_calls=%llu "
            "wl_fd_syscall_calls=%llu "
            "wl_fd_poll_calls=%llu wl_fd_ppoll_calls=%llu "
            "wl_fd_read_calls=%llu wl_fd_write_calls=%llu "
            "wl_fd_recvmsg_calls=%llu wl_fd_sendmsg_calls=%llu "
            "max_public_wl_elapsed_us=%llu "
            "max_public_wl_ready_to_call_us=%llu "
            "max_wl_fd_syscall_elapsed_us=%llu "
            "max_wl_fd_syscall_ready_to_call_us=%llu "
            "output_lines=%llu output_dropped=%llu output_limit=%llu",
            (long)getpid(), qt_fd3_ready_count, qt_fd3_ready_sequence,
            qt_last_fd3_ready_tid, qt_last_fd3_ready_comm,
            qt_last_fd3_ready_revents, qt_last_fd3_ready_fionread,
            qt_cpp_total_calls, qt_cpp_total_slow,
            qt_fd3_ready_in_active_span,
            qt_fd3_ready_in_process_events,
            qt_fd3_ready_in_glib_iteration,
            qt_fd3_ready_in_send_wse,
            qt_span_ready_during_calls,
            (unsigned long long)qt_max_fd3_ready_span_age_us,
            qt_max_fd3_ready_span_depth,
            (unsigned long long)qt_max_span_ready_offset_us,
            qt_cpp_ops[QT_CPP_PROCESS_EVENTS].calls,
            qt_cpp_ops[QT_CPP_PROCESS_EVENTS].slow,
            qt_cpp_ops[QT_CPP_SEND_WINDOW_SYSTEM_EVENTS].calls,
            qt_cpp_ops[QT_CPP_SEND_WINDOW_SYSTEM_EVENTS].slow,
            qt_cpp_ops[QT_CPP_FLUSH_WINDOW_SYSTEM_EVENTS].calls,
            qt_cpp_ops[QT_CPP_FLUSH_WINDOW_SYSTEM_EVENTS].slow,
            qt_cpp_ops[QT_CPP_CORE_SEND_POSTED_EVENTS].calls,
            qt_cpp_ops[QT_CPP_CORE_SEND_POSTED_EVENTS].slow,
            qt_cpp_ops[QT_CPP_PRIVATE_SEND_POSTED_EVENTS].calls,
            qt_cpp_ops[QT_CPP_PRIVATE_SEND_POSTED_EVENTS].slow,
            qt_cpp_ops[QT_CPP_NOTIFY_INTERNAL2].calls,
            qt_cpp_ops[QT_CPP_NOTIFY_INTERNAL2].slow,
            qt_cpp_ops[QT_CPP_QSOCKETNOTIFIER_EVENT].calls,
            qt_cpp_ops[QT_CPP_QSOCKETNOTIFIER_EVENT].slow,
            qt_cpp_ops[QT_CPP_QWAYLAND_HANDLE_SYNC].calls,
            qt_cpp_ops[QT_CPP_QWAYLAND_HANDLE_SYNC].slow,
            qt_cpp_ops[QT_CPP_QWAYLAND_REQUEST_SYNC].calls,
            qt_cpp_ops[QT_CPP_QWAYLAND_REQUEST_SYNC].slow,
            qt_cpp_ops[QT_CPP_QWAYLAND_BLOCKING_READ].calls,
            qt_cpp_ops[QT_CPP_QWAYLAND_BLOCKING_READ].slow,
            qt_cpp_ops[QT_CPP_QWAYLAND_FLUSH_REQUESTS].calls,
            qt_cpp_ops[QT_CPP_QWAYLAND_FLUSH_REQUESTS].slow,
            (unsigned long long)qt_cpp_max_ready_to_call_us,
            (unsigned long long)qt_cpp_ops[QT_CPP_PROCESS_EVENTS].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_SEND_WINDOW_SYSTEM_EVENTS].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_FLUSH_WINDOW_SYSTEM_EVENTS].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_CORE_SEND_POSTED_EVENTS].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_PRIVATE_SEND_POSTED_EVENTS].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_NOTIFY_INTERNAL2].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QSOCKETNOTIFIER_EVENT].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_HANDLE_SYNC].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_REQUEST_SYNC].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_BLOCKING_READ].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_FLUSH_REQUESTS].max_ready_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_PROCESS_EVENTS].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_SEND_WINDOW_SYSTEM_EVENTS].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_FLUSH_WINDOW_SYSTEM_EVENTS].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_CORE_SEND_POSTED_EVENTS].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_PRIVATE_SEND_POSTED_EVENTS].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_NOTIFY_INTERNAL2].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QSOCKETNOTIFIER_EVENT].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_HANDLE_SYNC].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_REQUEST_SYNC].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_BLOCKING_READ].max_elapsed_us,
            (unsigned long long)
                qt_cpp_ops[QT_CPP_QWAYLAND_FLUSH_REQUESTS].max_elapsed_us,
            public_wl_total_calls,
            public_wl_event_thread_calls,
            public_wl_main_thread_calls,
            public_wl_ops[PUBLIC_WL_CONNECT].calls,
            public_wl_ops[PUBLIC_WL_CONNECT_TO_FD].calls,
            public_wl_ops[PUBLIC_WL_GET_FD].calls,
            public_wl_ops[PUBLIC_WL_PREPARE_READ].calls,
            public_wl_ops[PUBLIC_WL_PREPARE_READ_QUEUE].calls,
            public_wl_ops[PUBLIC_WL_CANCEL_READ].calls,
            public_wl_ops[PUBLIC_WL_READ_EVENTS].calls,
            public_wl_ops[PUBLIC_WL_DISPATCH].calls,
            public_wl_ops[PUBLIC_WL_DISPATCH_PENDING].calls,
            public_wl_ops[PUBLIC_WL_DISPATCH_QUEUE_PENDING].calls,
            public_wl_ops[PUBLIC_WL_FLUSH].calls,
            public_wl_ops[PUBLIC_WL_ROUNDTRIP].calls,
            public_wl_ops[PUBLIC_WL_ROUNDTRIP_QUEUE].calls,
            public_wl_ops[PUBLIC_WL_PROXY_ADD_LISTENER].calls,
            public_wl_ops[PUBLIC_WL_PROXY_DESTROY].calls,
            public_wl_ops[PUBLIC_WL_PROXY_GET_ID].calls,
            wl_fd_syscall_total_calls,
            wl_fd_syscall_ops[WL_FD_SYSCALL_POLL].calls,
            wl_fd_syscall_ops[WL_FD_SYSCALL_PPOLL].calls,
            wl_fd_syscall_ops[WL_FD_SYSCALL_READ].calls,
            wl_fd_syscall_ops[WL_FD_SYSCALL_WRITE].calls,
            wl_fd_syscall_ops[WL_FD_SYSCALL_RECVMSG].calls,
            wl_fd_syscall_ops[WL_FD_SYSCALL_SENDMSG].calls,
            (unsigned long long)public_wl_max_elapsed_us,
            (unsigned long long)public_wl_max_ready_us,
            (unsigned long long)wl_fd_syscall_max_elapsed_us,
            (unsigned long long)wl_fd_syscall_max_ready_us,
            output_lines, output_dropped, output_limit);
    }
}
