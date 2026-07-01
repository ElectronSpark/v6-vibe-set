#define _GNU_SOURCE
#include <errno.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

static int trace_enabled_cached = -1;
static pid_t trace_identity_pid = -1;
static int render_fds[1024];
static int socket_fds[1024];
static int socket_fd_log_count[1024];

typedef EGLDisplay (*egl_get_display_fn_t)(EGLNativeDisplayType);
typedef EGLDisplay (*egl_get_platform_display_fn_t)(EGLenum, void *,
                                                    const EGLAttrib *);
typedef EGLBoolean (*egl_initialize_fn_t)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*egl_bind_api_fn_t)(EGLenum);
typedef const char *(*egl_query_string_fn_t)(EGLDisplay, EGLint);
typedef EGLBoolean (*egl_get_configs_fn_t)(EGLDisplay, EGLConfig *, EGLint,
                                           EGLint *);
typedef EGLBoolean (*egl_get_config_attrib_fn_t)(EGLDisplay, EGLConfig,
                                                 EGLint, EGLint *);
typedef EGLint (*egl_get_error_fn_t)(void);
typedef EGLBoolean (*egl_choose_config_fn_t)(EGLDisplay, const EGLint *,
                                             EGLConfig *, EGLint, EGLint *);
typedef EGLContext (*egl_create_context_fn_t)(EGLDisplay, EGLConfig,
                                              EGLContext, const EGLint *);
typedef EGLSurface (*egl_create_pbuffer_surface_fn_t)(EGLDisplay, EGLConfig,
                                                      const EGLint *);
typedef EGLSurface (*egl_create_window_surface_fn_t)(EGLDisplay, EGLConfig,
                                                     EGLNativeWindowType,
                                                     const EGLint *);
typedef __eglMustCastToProperFunctionPointerType (*egl_get_proc_address_fn_t)(
    const char *);

static egl_get_display_fn_t real_egl_get_display;
static egl_get_platform_display_fn_t real_egl_get_platform_display;
static egl_get_platform_display_fn_t real_egl_get_platform_display_ext;
static egl_initialize_fn_t real_egl_initialize;
static egl_bind_api_fn_t real_egl_bind_api;
static egl_query_string_fn_t real_egl_query_string;
static egl_get_configs_fn_t real_egl_get_configs;
static egl_get_config_attrib_fn_t real_egl_get_config_attrib;
static egl_get_error_fn_t real_egl_get_error;
static egl_choose_config_fn_t real_egl_choose_config;
static egl_create_context_fn_t real_egl_create_context;
static egl_create_pbuffer_surface_fn_t real_egl_create_pbuffer_surface;
static egl_create_window_surface_fn_t real_egl_create_window_surface;
static egl_get_proc_address_fn_t real_egl_get_proc_address;

static void trace_process_identity_once(void);
static void *trace_lookup_next(const char *symbol);
static int trace_egl_symbol_is_interesting(const char *procname);
static __eglMustCastToProperFunctionPointerType
trace_egl_wrapper_for_name(const char *procname);

static int trace_enabled(void)
{
    const char *value;

    int cached = __atomic_load_n(&trace_enabled_cached, __ATOMIC_RELAXED);

    if (cached >= 0)
        return cached;
    value = getenv("CHROMIUM_EGL_TRACE");
    cached = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    __atomic_store_n(&trace_enabled_cached, cached, __ATOMIC_RELAXED);
    return cached;
}

static const char *trace_path(void)
{
    const char *path = getenv("CHROMIUM_EGL_TRACE_LOG");

    return path && path[0] ? path : "/chromium-egl-trace.log";
}

static void trace_pid_path(char *buf, size_t size)
{
    const char *path = trace_path();
    const char *suffix = ".log";
    size_t len;
    size_t suffix_len = strlen(suffix);

    if (size == 0)
        return;
    len = strlen(path);
    if (len > suffix_len && strcmp(path + len - suffix_len, suffix) == 0) {
        snprintf(buf, size, "%.*s.%ld%s", (int)(len - suffix_len), path,
                 (long)getpid(), suffix);
    } else {
        snprintf(buf, size, "%s.%ld", path, (long)getpid());
    }
}

static void trace_line(const char *fmt, ...)
{
    char path[512];
    char buf[8192];
    va_list ap;
    int n;
    int fd;

    if (!trace_enabled())
        return;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(buf))
        n = (int)sizeof(buf) - 1;
    buf[n++] = '\n';

    trace_pid_path(path, sizeof(path));
    fd = (int)syscall(SYS_openat, AT_FDCWD, path,
                      O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd >= 0) {
        (void)syscall(SYS_write, fd, buf, (size_t)n);
        (void)syscall(SYS_close, fd);
    }
}

static void *trace_lookup_next(const char *symbol)
{
    if (symbol == NULL)
        return NULL;
    return dlsym(RTLD_NEXT, symbol);
}

static uint64_t trace_now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static const char *egl_attr_name(EGLint attr)
{
    switch (attr) {
    case EGL_NONE:
        return "EGL_NONE";
    case EGL_RENDERABLE_TYPE:
        return "EGL_RENDERABLE_TYPE";
    case EGL_SURFACE_TYPE:
        return "EGL_SURFACE_TYPE";
    case EGL_BUFFER_SIZE:
        return "EGL_BUFFER_SIZE";
    case EGL_ALPHA_SIZE:
        return "EGL_ALPHA_SIZE";
    case EGL_RED_SIZE:
        return "EGL_RED_SIZE";
    case EGL_GREEN_SIZE:
        return "EGL_GREEN_SIZE";
    case EGL_BLUE_SIZE:
        return "EGL_BLUE_SIZE";
    case EGL_DEPTH_SIZE:
        return "EGL_DEPTH_SIZE";
    case EGL_STENCIL_SIZE:
        return "EGL_STENCIL_SIZE";
    case EGL_CONFIG_ID:
        return "EGL_CONFIG_ID";
    case EGL_CONTEXT_CLIENT_VERSION:
        return "EGL_CONTEXT_CLIENT_VERSION";
#ifdef EGL_CONTEXT_MINOR_VERSION
    case EGL_CONTEXT_MINOR_VERSION:
        return "EGL_CONTEXT_MINOR_VERSION";
#endif
#ifdef EGL_CONTEXT_OPENGL_PROFILE_MASK
    case EGL_CONTEXT_OPENGL_PROFILE_MASK:
        return "EGL_CONTEXT_OPENGL_PROFILE_MASK";
#endif
#ifdef EGL_CONTEXT_OPENGL_DEBUG
    case EGL_CONTEXT_OPENGL_DEBUG:
        return "EGL_CONTEXT_OPENGL_DEBUG";
#endif
#ifdef EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE
    case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
        return "EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE";
#endif
#ifdef EGL_CONTEXT_OPENGL_ROBUST_ACCESS
    case EGL_CONTEXT_OPENGL_ROBUST_ACCESS:
        return "EGL_CONTEXT_OPENGL_ROBUST_ACCESS";
#endif
#ifdef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY
    case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY:
        return "EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY";
#endif
#ifdef EGL_CONTEXT_OPENGL_NO_ERROR_KHR
    case EGL_CONTEXT_OPENGL_NO_ERROR_KHR:
        return "EGL_CONTEXT_OPENGL_NO_ERROR_KHR";
#endif
#ifdef EGL_CONTEXT_PRIORITY_LEVEL_IMG
    case EGL_CONTEXT_PRIORITY_LEVEL_IMG:
        return "EGL_CONTEXT_PRIORITY_LEVEL_IMG";
#endif
    default:
        return "unknown";
    }
}

static void trace_egl_attribs(char *buf, size_t size, const EGLint *attribs)
{
    size_t off = 0;

    if (size == 0)
        return;
    buf[0] = '\0';
    if (attribs == NULL) {
        snprintf(buf, size, "NULL");
        return;
    }
    for (size_t i = 0; i < 64; i += 2) {
        EGLint attr = attribs[i];
        int n;

        if (attr == EGL_NONE) {
            snprintf(buf + off, off < size ? size - off : 0,
                     "%sEGL_NONE", off ? "," : "");
            return;
        }
        if (off >= size)
            return;
        n = snprintf(buf + off, size - off, "%s%s(0x%x)=0x%x",
                     off ? "," : "", egl_attr_name(attr), attr,
                     attribs[i + 1]);
        if (n < 0)
            return;
        if ((size_t)n >= size - off) {
            buf[size - 1] = '\0';
            return;
        }
        off += (size_t)n;
    }
    if (off < size)
        snprintf(buf + off, size - off, "%s...", off ? "," : "...");
}

static int trace_egl_symbol_is_interesting(const char *procname)
{
    if (procname == NULL)
        return 0;
    return strcmp(procname, "eglGetDisplay") == 0 ||
           strcmp(procname, "eglGetPlatformDisplay") == 0 ||
           strcmp(procname, "eglGetPlatformDisplayEXT") == 0 ||
           strcmp(procname, "eglInitialize") == 0 ||
           strcmp(procname, "eglBindAPI") == 0 ||
           strcmp(procname, "eglQueryString") == 0 ||
           strcmp(procname, "eglGetConfigs") == 0 ||
           strcmp(procname, "eglGetConfigAttrib") == 0 ||
           strcmp(procname, "eglGetError") == 0 ||
           strcmp(procname, "eglChooseConfig") == 0 ||
           strcmp(procname, "eglCreateContext") == 0 ||
           strcmp(procname, "eglCreatePbufferSurface") == 0 ||
           strcmp(procname, "eglCreateWindowSurface") == 0;
}

static __eglMustCastToProperFunctionPointerType
trace_egl_wrapper_for_name(const char *procname)
{
    if (procname == NULL)
        return NULL;
    if (strcmp(procname, "eglGetDisplay") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetDisplay;
    if (strcmp(procname, "eglGetPlatformDisplay") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetPlatformDisplay;
    if (strcmp(procname, "eglGetPlatformDisplayEXT") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetPlatformDisplay;
    if (strcmp(procname, "eglInitialize") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglInitialize;
    if (strcmp(procname, "eglBindAPI") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglBindAPI;
    if (strcmp(procname, "eglQueryString") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglQueryString;
    if (strcmp(procname, "eglGetConfigs") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetConfigs;
    if (strcmp(procname, "eglGetConfigAttrib") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetConfigAttrib;
    if (strcmp(procname, "eglGetError") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetError;
    if (strcmp(procname, "eglChooseConfig") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglChooseConfig;
    if (strcmp(procname, "eglCreateContext") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglCreateContext;
    if (strcmp(procname, "eglCreatePbufferSurface") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglCreatePbufferSurface;
    if (strcmp(procname, "eglCreateWindowSurface") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglCreateWindowSurface;
    return NULL;
}

static void trace_fd_mark(int fd, const char *path)
{
    if (fd < 0 || fd >= (int)(sizeof(render_fds) / sizeof(render_fds[0])))
        return;
    if (path != NULL &&
        (strstr(path, "/dev/dri/renderD") != NULL ||
         strstr(path, "/dev/dri/card") != NULL)) {
        render_fds[fd] = 1;
    }
}

static int trace_fd_is_render(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(render_fds) / sizeof(render_fds[0])))
        return 0;
    return render_fds[fd];
}

static void trace_socket_fd_mark(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(socket_fds) / sizeof(socket_fds[0])))
        return;
    socket_fds[fd] = 1;
    socket_fd_log_count[fd] = 0;
}

static void trace_fd_clear(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(render_fds) / sizeof(render_fds[0])))
        return;
    render_fds[fd] = 0;
    socket_fds[fd] = 0;
    socket_fd_log_count[fd] = 0;
}

static void trace_fd_copy(int dst, int src)
{
    if (dst < 0 || dst >= (int)(sizeof(render_fds) / sizeof(render_fds[0])))
        return;
    if (src < 0 || src >= (int)(sizeof(render_fds) / sizeof(render_fds[0]))) {
        trace_fd_clear(dst);
        return;
    }
    render_fds[dst] = render_fds[src];
    socket_fds[dst] = socket_fds[src];
    socket_fd_log_count[dst] = 0;
}

static void trace_fd_clear_range(unsigned int first, unsigned int last)
{
    unsigned int limit = (unsigned int)(sizeof(render_fds) / sizeof(render_fds[0]));

    if (first >= limit)
        return;
    if (last >= limit)
        last = limit - 1;
    for (unsigned int fd = first; fd <= last; fd++)
        trace_fd_clear((int)fd);
}

static int trace_fd_is_socket(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(socket_fds) / sizeof(socket_fds[0])))
        return 0;
    return socket_fds[fd];
}

static int trace_path_interesting_for_launch(const char *path)
{
    if (!path)
        return 0;
    return strstr(path, "/dev/dri/") != NULL ||
           strstr(path, "libEGL") != NULL ||
           strstr(path, "libGLES") != NULL ||
           strstr(path, "gbm") != NULL;
}

static int open_flags_need_mode(int flags)
{
    if (flags & O_CREAT)
        return 1;
#ifdef __O_TMPFILE
    if ((flags & __O_TMPFILE) == __O_TMPFILE)
        return 1;
#elif defined(O_TMPFILE)
    if ((flags & O_TMPFILE) == O_TMPFILE)
        return 1;
#endif
    return 0;
}

static int close_range_closes_now(int flags)
{
#ifdef CLOSE_RANGE_CLOEXEC
    if (flags & CLOSE_RANGE_CLOEXEC)
        return 0;
#endif
    return 1;
}

__attribute__((always_inline)) static inline void *ioctl_arg_from_abi(void)
{
    void *arg = NULL;

#if defined(__x86_64__)
    __asm__ volatile("movq %%rdx, %0" : "=r"(arg));
#endif
    return arg;
}

static void append_escaped_bytes(char *buf, size_t size, size_t *off,
                                 const char *bytes, ssize_t len)
{
    static const char hex[] = "0123456789abcdef";

    if (!buf || size == 0 || !bytes || len <= 0)
        return;

    for (ssize_t i = 0; i < len && *off + 1 < size; i++) {
        unsigned char c = (unsigned char)bytes[i];

        if (c == '\\' || c == '"') {
            if (*off + 2 >= size)
                break;
            buf[(*off)++] = '\\';
            buf[(*off)++] = (char)c;
        } else if (c == '\0') {
            if (*off + 2 >= size)
                break;
            buf[(*off)++] = '\\';
            buf[(*off)++] = '0';
        } else if (c >= 0x20 && c <= 0x7e) {
            buf[(*off)++] = (char)c;
        } else {
            if (*off + 4 >= size)
                break;
            buf[(*off)++] = '\\';
            buf[(*off)++] = 'x';
            buf[(*off)++] = hex[c >> 4];
            buf[(*off)++] = hex[c & 0xf];
        }
    }
    buf[*off] = '\0';
}

static size_t trace_iov_total(const struct iovec *iov, size_t iovlen)
{
    size_t total = 0;

    if (!iov)
        return 0;
    for (size_t i = 0; i < iovlen && i < 1024; i++)
        total += iov[i].iov_len;
    return total;
}

static void trace_ipc_sample(char *dst, size_t dst_size,
                             const void *buf, ssize_t len)
{
    size_t off = 0;
    ssize_t sample_len = len;

    if (dst_size == 0)
        return;
    dst[0] = '\0';
    if (sample_len > 32)
        sample_len = 32;
    append_escaped_bytes(dst, dst_size, &off, buf, sample_len);
}

static void trace_iov_sample(char *dst, size_t dst_size,
                             const struct iovec *iov, size_t iovlen,
                             ssize_t len)
{
    if (dst_size == 0)
        return;
    dst[0] = '\0';
    if (!iov || iovlen == 0 || len <= 0)
        return;
    trace_ipc_sample(dst, dst_size, iov[0].iov_base,
                     len < (ssize_t)iov[0].iov_len ? len :
                     (ssize_t)iov[0].iov_len);
}

static void trace_appendf(char *buf, size_t size, size_t *off,
                          const char *fmt, ...)
{
    va_list ap;
    int n;

    if (!buf || size == 0 || !off || *off >= size)
        return;

    va_start(ap, fmt);
    n = vsnprintf(buf + *off, size - *off, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= size - *off) {
        *off = size - 1;
        buf[*off] = '\0';
        return;
    }
    *off += (size_t)n;
}

static void trace_fd_target_summary(int fd, char *buf, size_t size)
{
    char path[64];
    char target[256];
    ssize_t n;
    size_t off = 0;

    if (size == 0)
        return;
    buf[0] = '\0';
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    n = (ssize_t)syscall(SYS_readlinkat, AT_FDCWD, path, target,
                         sizeof(target) - 1);
    if (n < 0) {
        snprintf(buf, size, "readlink_errno=%d", errno);
        return;
    }
    target[n] = '\0';
    append_escaped_bytes(buf, size, &off, target, n);
}

static void trace_cmsg_summary(const struct msghdr *msg, char *summary,
                               size_t summary_size, int *cmsg_count,
                               int *scm_rights_count,
                               int *scm_rights_fd_count,
                               int *scm_credentials_count,
                               int *cmsg_other_count)
{
    struct msghdr *mutable_msg = (struct msghdr *)msg;
    struct cmsghdr *cmsg;
    size_t off = 0;

    if (summary_size > 0)
        summary[0] = '\0';
    if (cmsg_count)
        *cmsg_count = 0;
    if (scm_rights_count)
        *scm_rights_count = 0;
    if (scm_rights_fd_count)
        *scm_rights_fd_count = 0;
    if (scm_credentials_count)
        *scm_credentials_count = 0;
    if (cmsg_other_count)
        *cmsg_other_count = 0;

    if (!msg || !msg->msg_control || msg->msg_controllen < sizeof(*cmsg))
        return;

    for (cmsg = CMSG_FIRSTHDR(mutable_msg); cmsg != NULL;
         cmsg = CMSG_NXTHDR(mutable_msg, cmsg)) {
        size_t payload_len = 0;
        int index = cmsg_count ? *cmsg_count : 0;

        if (cmsg_count)
            (*cmsg_count)++;
        if (cmsg->cmsg_len >= CMSG_LEN(0))
            payload_len = cmsg->cmsg_len - CMSG_LEN(0);
        trace_appendf(summary, summary_size, &off,
                      "%s%d:level=%d,type=%d,len=%zu,payload=%zu",
                      index ? ";" : "", index, cmsg->cmsg_level,
                      cmsg->cmsg_type, (size_t)cmsg->cmsg_len, payload_len);

        if (cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type == SCM_RIGHTS) {
            int fd_count = (int)(payload_len / sizeof(int));
            int *fds = (int *)CMSG_DATA(cmsg);

            if (scm_rights_count)
                (*scm_rights_count)++;
            if (scm_rights_fd_count)
                *scm_rights_fd_count += fd_count;
            trace_appendf(summary, summary_size, &off, ",scm_rights_fds=%d[",
                          fd_count);
            for (int i = 0; i < fd_count && i < 8; i++) {
                char target[256];

                trace_fd_target_summary(fds[i], target, sizeof(target));
                trace_appendf(summary, summary_size, &off, "%s%d:", i ? "," : "",
                              fds[i]);
                append_escaped_bytes(summary, summary_size, &off, target,
                                     (ssize_t)strlen(target));
            }
            if (fd_count > 8)
                trace_appendf(summary, summary_size, &off, ",...");
            trace_appendf(summary, summary_size, &off, "]");
        } else if (cmsg->cmsg_level == SOL_SOCKET &&
                   cmsg->cmsg_type == SCM_CREDENTIALS) {
            if (scm_credentials_count)
                (*scm_credentials_count)++;
            trace_appendf(summary, summary_size, &off, ",scm_credentials=1");
        } else if (cmsg_other_count) {
            (*cmsg_other_count)++;
        }
    }
}

static int trace_ipc_budget_ok(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(socket_fd_log_count) /
                              sizeof(socket_fd_log_count[0])))
        return 0;
    if (socket_fd_log_count[fd] >= 256) {
        if (socket_fd_log_count[fd] == 256) {
            socket_fd_log_count[fd]++;
            trace_line("chromium_egl_trace phase=ipc_drop pid=%ld fd=%d "
                       "reason=budget",
                       (long)getpid(), fd);
        }
        return 0;
    }
    socket_fd_log_count[fd]++;
    return 1;
}

static void trace_ipc_buffer_event(const char *phase, int fd, size_t requested,
                                   ssize_t ret, int saved_errno, int flags,
                                   const void *buf)
{
    char sample[256];

    if (!trace_enabled() || !trace_fd_is_socket(fd) || !trace_ipc_budget_ok(fd))
        return;
    trace_process_identity_once();
    trace_ipc_sample(sample, sizeof(sample), buf, ret > 0 ? ret : 0);
    trace_line("chromium_egl_trace phase=%s pid=%ld fd=%d requested=%zu "
               "ret=%ld errno=%d flags=0x%x sample=\"%s\"",
               phase, (long)getpid(), fd, requested, (long)ret,
               ret < 0 ? saved_errno : 0, flags, sample);
}

static void trace_ipc_msg_event(const char *phase, int fd,
                                const struct msghdr *msg, ssize_t ret,
                                int saved_errno, int flags)
{
    char sample[256];
    char cmsg_summary[2048];
    size_t iovlen = msg ? msg->msg_iovlen : 0;
    size_t iov_total = msg ? trace_iov_total(msg->msg_iov, iovlen) : 0;
    size_t controllen = msg ? msg->msg_controllen : 0;
    int msg_flags = msg ? msg->msg_flags : 0;
    int cmsg_count = 0;
    int scm_rights_count = 0;
    int scm_rights_fd_count = 0;
    int scm_credentials_count = 0;
    int cmsg_other_count = 0;

    if (!trace_enabled() || !trace_fd_is_socket(fd) || !trace_ipc_budget_ok(fd))
        return;
    trace_process_identity_once();
    trace_iov_sample(sample, sizeof(sample), msg ? msg->msg_iov : NULL,
                     iovlen, ret > 0 ? ret : 0);
    trace_cmsg_summary(msg, cmsg_summary, sizeof(cmsg_summary), &cmsg_count,
                       &scm_rights_count, &scm_rights_fd_count,
                       &scm_credentials_count, &cmsg_other_count);
    trace_line("chromium_egl_trace phase=%s pid=%ld fd=%d iovlen=%zu "
               "iov_total=%zu controllen=%zu msg_flags=0x%x flags=0x%x "
               "ret=%ld errno=%d cmsg_count=%d scm_rights=%d "
               "scm_rights_fds=%d scm_credentials=%d cmsg_other=%d "
               "cmsg_truncated=%d cmsg=\"%s\" sample=\"%s\"",
               phase, (long)getpid(), fd, iovlen, iov_total, controllen,
               msg_flags, flags, (long)ret, ret < 0 ? saved_errno : 0,
               cmsg_count, scm_rights_count, scm_rights_fd_count,
               scm_credentials_count, cmsg_other_count,
               (msg_flags & MSG_CTRUNC) ? 1 : 0, cmsg_summary, sample);
}

static int cmdline_has_arg_prefix(const char *cmdline, ssize_t len,
                                  const char *prefix)
{
    ssize_t start = 0;
    size_t prefix_len;

    if (!cmdline || len <= 0 || !prefix)
        return 0;
    prefix_len = strlen(prefix);
    for (ssize_t i = 0; i <= len; i++) {
        if (i == len || cmdline[i] == '\0') {
            ssize_t arg_len = i - start;

            if (arg_len >= (ssize_t)prefix_len &&
                memcmp(cmdline + start, prefix, prefix_len) == 0) {
                return 1;
            }
            start = i + 1;
        }
    }
    return 0;
}

static int env_entry_interesting(const char *entry, ssize_t len)
{
    static const char *names[] = {
        "WAYLAND_DISPLAY=",
        "DISPLAY=",
        "XDG_RUNTIME_DIR=",
        "OZONE_PLATFORM=",
        "EGL_PLATFORM=",
        "GALLIUM_DRIVER=",
        "MESA_LOADER_DRIVER_OVERRIDE=",
        "LIBGL_DRIVERS_PATH=",
        "GBM_BACKENDS_PATH=",
        "LD_LIBRARY_PATH=",
        "LD_PRELOAD=",
        "WAYLAND_CHROMIUM_EGL_TRACE=",
        "WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION=",
        "SIMDUTF_FORCE_IMPLEMENTATION=",
        "CHROMIUM_EGL_TRACE=",
        "CHROMIUM_EGL_TRACE_LOG=",
        "LIBVA_DRIVERS_PATH=",
        "LIBVA_DRIVER_NAME=",
        "CHROME_LOG_FILE=",
    };

    if (!entry || len <= 0)
        return 0;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        size_t name_len = strlen(names[i]);

        if (len >= (ssize_t)name_len &&
            memcmp(entry, names[i], name_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static void trace_process_env(pid_t pid, const char *role)
{
    char raw[8192];
    ssize_t n = -1;
    ssize_t start = 0;
    int fd;

    fd = open("/proc/self/environ", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        n = read(fd, raw, sizeof(raw));
        close(fd);
    }
    if (n <= 0) {
        trace_line("chromium_egl_trace phase=process_env pid=%ld role=%s "
                   "entries=0 environ_bytes=%ld",
                   (long)pid, role ? role : "unknown", (long)n);
        return;
    }

    for (ssize_t i = 0; i <= n; i++) {
        if (i == n || raw[i] == '\0') {
            ssize_t entry_len = i - start;

            if (env_entry_interesting(raw + start, entry_len)) {
                char escaped[2048];
                size_t off = 0;

                escaped[0] = '\0';
                append_escaped_bytes(escaped, sizeof(escaped), &off,
                                     raw + start, entry_len);
                trace_line("chromium_egl_trace phase=process_env pid=%ld "
                           "role=%s entry=\"%s\"",
                           (long)pid, role ? role : "unknown", escaped);
            }
            start = i + 1;
        }
    }
    if (n == (ssize_t)sizeof(raw))
        trace_line("chromium_egl_trace phase=process_env pid=%ld role=%s "
                   "truncated=1 environ_bytes=%ld",
                   (long)pid, role ? role : "unknown", (long)n);
}

static void derive_cmdline_role(const char *cmdline, ssize_t len,
                                char *role, size_t role_size)
{
    ssize_t start = 0;
    ssize_t argv0_len = 0;
    const char *argv0_base;

    if (role_size == 0)
        return;
    snprintf(role, role_size, "%s", "unknown");
    if (!cmdline || len <= 0)
        return;
    while (argv0_len < len && cmdline[argv0_len] != '\0')
        argv0_len++;

    for (ssize_t i = 0; i <= len; i++) {
        if (i == len || cmdline[i] == '\0') {
            static const char prefix[] = "--type=";
            ssize_t arg_len = i - start;

            if (arg_len > (ssize_t)sizeof(prefix) - 1 &&
                memcmp(cmdline + start, prefix, sizeof(prefix) - 1) == 0) {
                size_t role_len = (size_t)arg_len - (sizeof(prefix) - 1);

                if (role_len >= role_size)
                    role_len = role_size - 1;
                memcpy(role, cmdline + start + sizeof(prefix) - 1, role_len);
                role[role_len] = '\0';
                return;
            }
            start = i + 1;
        }
    }

    argv0_base = memrchr(cmdline, '/', (size_t)argv0_len);
    argv0_base = argv0_base ? argv0_base + 1 : cmdline;
    if (argv0_len > 0 &&
        ((argv0_len - (argv0_base - cmdline) == 6 &&
          memcmp(argv0_base, "chrome", 6) == 0) ||
         (argv0_len - (argv0_base - cmdline) == 8 &&
          memcmp(argv0_base, "chromium", 8) == 0) ||
         (argv0_len - (argv0_base - cmdline) == 16 &&
          memcmp(argv0_base, "wayland-chromium", 16) == 0))) {
        snprintf(role, role_size, "%s", "browser");
        return;
    }
}

static void trace_process_identity_once(void)
{
    char raw[4096];
    char escaped[6144];
    char role[64];
    size_t escaped_off = 0;
    ssize_t n = -1;
    int fd;
    pid_t pid;

    if (!trace_enabled())
        return;

    pid = getpid();
    if (__atomic_exchange_n(&trace_identity_pid, pid,
                            __ATOMIC_RELAXED) == pid)
        return;

    role[0] = '\0';
    fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        n = read(fd, raw, sizeof(raw));
        close(fd);
    }
    escaped[0] = '\0';
    append_escaped_bytes(escaped, sizeof(escaped), &escaped_off,
                         n > 0 ? raw : NULL, n);
    derive_cmdline_role(n > 0 ? raw : NULL, n, role, sizeof(role));
    trace_line("chromium_egl_trace phase=process pid=%ld ppid=%ld "
               "program=%s role=%s trace_env=%d log_env=%d "
               "cmdline_bytes=%ld cmdline_truncated=%d "
               "use_gl=%d use_angle=%d disable_gpu_early_init=%d "
               "argv=\"%s\"",
               (long)pid, (long)getppid(),
               program_invocation_short_name ? program_invocation_short_name :
               "(null)",
               role[0] ? role : "unknown",
               getenv("CHROMIUM_EGL_TRACE") ? 1 : 0,
               getenv("CHROMIUM_EGL_TRACE_LOG") ? 1 : 0, (long)n,
               n == (ssize_t)sizeof(raw) ? 1 : 0,
               cmdline_has_arg_prefix(n > 0 ? raw : NULL, n, "--use-gl="),
               cmdline_has_arg_prefix(n > 0 ? raw : NULL, n, "--use-angle="),
               cmdline_has_arg_prefix(n > 0 ? raw : NULL, n,
                                      "--disable-gpu-early-init"),
               escaped);
    trace_process_env(pid, role[0] ? role : "unknown");
}

static void copy_arg_value(char *dst, size_t dst_size, const char *arg,
                           const char *prefix)
{
    size_t off = 0;
    size_t prefix_len;

    if (dst_size == 0)
        return;
    if (!arg || !prefix)
        return;
    prefix_len = strlen(prefix);
    if (strncmp(arg, prefix, prefix_len) != 0)
        return;
    dst[0] = '\0';
    append_escaped_bytes(dst, dst_size, &off, arg + prefix_len,
                         (ssize_t)strlen(arg + prefix_len));
}

static int argv_has_prefix(char *const argv[], const char *prefix)
{
    if (!argv || !prefix)
        return 0;
    for (int i = 0; argv[i] != NULL && i < 128; i++) {
        if (strncmp(argv[i], prefix, strlen(prefix)) == 0)
            return 1;
    }
    return 0;
}

static void trace_execve_attempt(const char *filename, char *const argv[])
{
    char path[1024];
    char type_arg[128] = "missing";
    char use_gl[128] = "missing";
    char use_angle[128] = "missing";
    char render_node[256] = "missing";
    size_t off = 0;
    int argc = 0;

    if (!trace_enabled())
        return;
    path[0] = '\0';
    append_escaped_bytes(path, sizeof(path), &off, filename,
                         filename ? (ssize_t)strlen(filename) : 0);
    if (argv != NULL) {
        for (int i = 0; argv[i] != NULL && i < 128; i++) {
            argc++;
            copy_arg_value(type_arg, sizeof(type_arg), argv[i], "--type=");
            copy_arg_value(use_gl, sizeof(use_gl), argv[i], "--use-gl=");
            copy_arg_value(use_angle, sizeof(use_angle), argv[i],
                           "--use-angle=");
            copy_arg_value(render_node, sizeof(render_node), argv[i],
                           "--render-node-override=");
        }
    }
    trace_line("chromium_egl_trace phase=execve pid=%ld ppid=%ld "
               "filename=\"%s\" argc=%d type=%s use_gl=%s use_angle=%s "
               "render_node=%s disable_gpu_early_init=%d",
               (long)getpid(), (long)getppid(), path, argc, type_arg,
               use_gl, use_angle, render_node,
               argv_has_prefix(argv, "--disable-gpu-early-init"));
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int fd;
    int saved_errno;
    int needs_mode = open_flags_need_mode(flags);

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    saved_errno = errno;
    trace_fd_mark(fd, path);
    if (trace_enabled() && trace_path_interesting_for_launch(path)) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=open pid=%ld path=\"%s\" "
                   "flags=0x%x fd=%d errno=%d render_fd=%d",
                   (long)getpid(), path, flags, fd, fd < 0 ? saved_errno : 0,
                   trace_fd_is_render(fd));
    }
    errno = saved_errno;
    return fd;
}

int open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int needs_mode = open_flags_need_mode(flags);

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
        return open(path, flags, mode);
    }
    return open(path, flags);
}

int openat(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    int fd;
    int saved_errno;
    int needs_mode = open_flags_need_mode(flags);

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    fd = (int)syscall(SYS_openat, dirfd, path, flags, mode);
    saved_errno = errno;
    trace_fd_mark(fd, path);
    if (trace_enabled() && trace_path_interesting_for_launch(path)) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=openat pid=%ld dirfd=%d "
                   "path=\"%s\" flags=0x%x fd=%d errno=%d render_fd=%d",
                   (long)getpid(), dirfd, path, flags, fd,
                   fd < 0 ? saved_errno : 0, trace_fd_is_render(fd));
    }
    errno = saved_errno;
    return fd;
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    int needs_mode = open_flags_need_mode(flags);

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
        return openat(dirfd, path, flags, mode);
    }
    return openat(dirfd, path, flags);
}

__attribute__((noinline)) int ioctl(int fd, unsigned long request, ...)
{
    void *arg = ioctl_arg_from_abi();
    uint64_t start_us = 0;
    uint64_t elapsed_us = 0;
    int ret;
    int saved_errno = 0;
    int render_fd = trace_fd_is_render(fd);

    if (render_fd)
        start_us = trace_now_us();
    ret = (int)syscall(SYS_ioctl, fd, request, arg);
    saved_errno = errno;
    if (render_fd && start_us != 0)
        elapsed_us = trace_now_us() - start_us;
    if (trace_enabled() && render_fd) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=ioctl pid=%ld fd=%d "
                   "request=0x%lx ret=%d errno=%d elapsed_us=%lu",
                   (long)getpid(), fd, request, ret, ret < 0 ? saved_errno : 0,
                   (unsigned long)elapsed_us);
    }
    errno = saved_errno;
    return ret;
}

int execve(const char *filename, char *const argv[], char *const envp[])
{
    int ret;
    int saved_errno;

    trace_execve_attempt(filename, argv);
    ret = (int)syscall(SYS_execve, filename, argv, envp);
    saved_errno = errno;
    trace_line("chromium_egl_trace phase=execve_return pid=%ld filename=\"%s\" "
               "ret=%d errno=%d",
               (long)getpid(), filename, ret, saved_errno);
    errno = saved_errno;
    return ret;
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_socketpair, domain, type, protocol, sv);
    saved_errno = errno;
    if (ret == 0 && domain == AF_UNIX && sv) {
        trace_socket_fd_mark(sv[0]);
        trace_socket_fd_mark(sv[1]);
    }
    if (trace_enabled() && domain == AF_UNIX) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=socketpair pid=%ld type=0x%x "
                   "protocol=%d ret=%d errno=%d fd0=%d fd1=%d",
                   (long)getpid(), type, protocol, ret,
                   ret < 0 ? saved_errno : 0,
                   ret == 0 && sv ? sv[0] : -1,
                   ret == 0 && sv ? sv[1] : -1);
    }
    errno = saved_errno;
    return ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_read, fd, buf, count);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_read", fd, count, ret, saved_errno, 0, buf);
    errno = saved_errno;
    return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_write, fd, buf, count);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_write", fd, count, ret, saved_errno, 0, buf);
    errno = saved_errno;
    return ret;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_sendto, fd, buf, len, flags, NULL, 0);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_send", fd, len, ret, saved_errno, flags, buf);
    errno = saved_errno;
    return ret;
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_recvfrom, fd, buf, len, flags, NULL, NULL);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_recv", fd, len, ret, saved_errno, flags, buf);
    errno = saved_errno;
    return ret;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_sendmsg, fd, msg, flags);
    saved_errno = errno;
    trace_ipc_msg_event("ipc_sendmsg", fd, msg, ret, saved_errno, flags);
    errno = saved_errno;
    return ret;
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_recvmsg, fd, msg, flags);
    saved_errno = errno;
    trace_ipc_msg_event("ipc_recvmsg", fd, msg, ret, saved_errno, flags);
    errno = saved_errno;
    return ret;
}

int close(int fd)
{
    int was_socket = trace_fd_is_socket(fd);
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_close, fd);
    saved_errno = errno;
    if (trace_enabled() && was_socket) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=ipc_close pid=%ld fd=%d ret=%d "
                   "errno=%d",
                   (long)getpid(), fd, ret, ret < 0 ? saved_errno : 0);
    }
    if (ret == 0)
        trace_fd_clear(fd);
    errno = saved_errno;
    return ret;
}

int dup(int oldfd)
{
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_dup, oldfd);
    saved_errno = errno;
    if (ret >= 0)
        trace_fd_copy(ret, oldfd);
    errno = saved_errno;
    return ret;
}

int dup2(int oldfd, int newfd)
{
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_dup2, oldfd, newfd);
    saved_errno = errno;
    if (ret >= 0)
        trace_fd_copy(ret, oldfd);
    errno = saved_errno;
    return ret;
}

int dup3(int oldfd, int newfd, int flags)
{
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_dup3, oldfd, newfd, flags);
    saved_errno = errno;
    if (ret >= 0)
        trace_fd_copy(ret, oldfd);
    errno = saved_errno;
    return ret;
}

int close_range(unsigned int first, unsigned int last, int flags)
{
    int ret;
    int saved_errno;

#ifdef SYS_close_range
    ret = (int)syscall(SYS_close_range, first, last, flags);
#else
    errno = ENOSYS;
    ret = -1;
#endif
    saved_errno = errno;
    if (ret == 0 && close_range_closes_now(flags))
        trace_fd_clear_range(first, last);
    errno = saved_errno;
    return ret;
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    EGLDisplay ret;

    if (real_egl_get_display == NULL) {
        real_egl_get_display =
            (egl_get_display_fn_t)trace_lookup_next("eglGetDisplay");
    }
    if (real_egl_get_display == NULL)
        return EGL_NO_DISPLAY;
    ret = real_egl_get_display(display_id);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglGetDisplay pid=%ld "
               "display_id=%p ret=%p",
               (long)getpid(), (void *)display_id, (void *)ret);
    return ret;
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display,
                                 const EGLAttrib *attrib_list)
{
    EGLDisplay ret;

    if (real_egl_get_platform_display == NULL) {
        real_egl_get_platform_display =
            (egl_get_platform_display_fn_t)
                trace_lookup_next("eglGetPlatformDisplay");
    }
    if (real_egl_get_platform_display == NULL) {
        real_egl_get_platform_display =
            (egl_get_platform_display_fn_t)
                trace_lookup_next("eglGetPlatformDisplayEXT");
    }
    if (real_egl_get_platform_display == NULL)
        return EGL_NO_DISPLAY;
    ret = real_egl_get_platform_display(platform, native_display, attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglGetPlatformDisplay pid=%ld "
               "platform=0x%x native_display=%p attribs=%p ret=%p",
               (long)getpid(), platform, native_display, (void *)attrib_list,
               (void *)ret);
    return ret;
}

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                    const EGLint *attrib_list)
{
    EGLDisplay ret;

    if (real_egl_get_platform_display_ext == NULL) {
        real_egl_get_platform_display_ext =
            (egl_get_platform_display_fn_t)
                trace_lookup_next("eglGetPlatformDisplayEXT");
    }
    if (real_egl_get_platform_display_ext == NULL) {
        real_egl_get_platform_display_ext =
            (egl_get_platform_display_fn_t)
                trace_lookup_next("eglGetPlatformDisplay");
    }
    if (real_egl_get_platform_display_ext == NULL)
        return EGL_NO_DISPLAY;
    ret = real_egl_get_platform_display_ext(platform, native_display,
                                            (const EGLAttrib *)attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglGetPlatformDisplayEXT pid=%ld "
               "platform=0x%x native_display=%p attribs=%p ret=%p",
               (long)getpid(), platform, native_display, (void *)attrib_list,
               (void *)ret);
    return ret;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    EGLBoolean ret;

    if (real_egl_initialize == NULL)
        real_egl_initialize =
            (egl_initialize_fn_t)trace_lookup_next("eglInitialize");
    if (real_egl_initialize == NULL)
        return EGL_FALSE;
    ret = real_egl_initialize(dpy, major, minor);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglInitialize pid=%ld dpy=%p "
               "ret=%d major=%d minor=%d",
               (long)getpid(), (void *)dpy, ret, major ? *major : -1,
               minor ? *minor : -1);
    return ret;
}

EGLBoolean eglBindAPI(EGLenum api)
{
    EGLBoolean ret;

    if (real_egl_bind_api == NULL)
        real_egl_bind_api =
            (egl_bind_api_fn_t)trace_lookup_next("eglBindAPI");
    if (real_egl_bind_api == NULL)
        return EGL_FALSE;
    ret = real_egl_bind_api(api);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglBindAPI pid=%ld api=0x%x ret=%d",
               (long)getpid(), api, ret);
    return ret;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    const char *ret;

    if (real_egl_query_string == NULL)
        real_egl_query_string =
            (egl_query_string_fn_t)trace_lookup_next("eglQueryString");
    if (real_egl_query_string == NULL)
        return NULL;
    ret = real_egl_query_string(dpy, name);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglQueryString pid=%ld dpy=%p "
               "name=0x%x ret=%p",
               (long)getpid(), (void *)dpy, name, ret);
    return ret;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                         EGLint config_size, EGLint *num_config)
{
    EGLBoolean ret;

    if (real_egl_get_configs == NULL)
        real_egl_get_configs =
            (egl_get_configs_fn_t)trace_lookup_next("eglGetConfigs");
    if (real_egl_get_configs == NULL)
        return EGL_FALSE;
    ret = real_egl_get_configs(dpy, configs, config_size, num_config);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglGetConfigs pid=%ld dpy=%p "
               "config_size=%d ret=%d num_config=%d first_config=%p",
               (long)getpid(), (void *)dpy, config_size, ret,
               num_config ? *num_config : -1,
               configs && config_size > 0 ? (void *)configs[0] : NULL);
    return ret;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                              EGLint attribute, EGLint *value)
{
    EGLBoolean ret;

    if (real_egl_get_config_attrib == NULL) {
        real_egl_get_config_attrib =
            (egl_get_config_attrib_fn_t)
                trace_lookup_next("eglGetConfigAttrib");
    }
    if (real_egl_get_config_attrib == NULL)
        return EGL_FALSE;
    ret = real_egl_get_config_attrib(dpy, config, attribute, value);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglGetConfigAttrib pid=%ld "
               "dpy=%p config=%p attr=%s(0x%x) ret=%d value=0x%x",
               (long)getpid(), (void *)dpy, (void *)config,
               egl_attr_name(attribute), attribute, ret, value ? *value : -1);
    return ret;
}

EGLint eglGetError(void)
{
    EGLint ret;

    if (real_egl_get_error == NULL)
        real_egl_get_error =
            (egl_get_error_fn_t)trace_lookup_next("eglGetError");
    if (real_egl_get_error == NULL)
        return EGL_SUCCESS;
    ret = real_egl_get_error();
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglGetError pid=%ld ret=0x%x",
               (long)getpid(), ret);
    return ret;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config)
{
    char attrs[2048];
    EGLBoolean ret;

    if (real_egl_choose_config == NULL) {
        real_egl_choose_config =
            (egl_choose_config_fn_t)trace_lookup_next("eglChooseConfig");
    }
    if (real_egl_choose_config == NULL)
        return EGL_FALSE;

    trace_egl_attribs(attrs, sizeof(attrs), attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglChooseConfig_enter pid=%ld "
               "dpy=%p config_size=%d attribs=\"%s\"",
               (long)getpid(), (void *)dpy, config_size, attrs);
    ret = real_egl_choose_config(dpy, attrib_list, configs, config_size,
                                 num_config);
    trace_line("chromium_egl_trace phase=eglChooseConfig_exit pid=%ld "
               "ret=%d num_config=%d first_config=%p",
               (long)getpid(), ret, num_config ? *num_config : -1,
               configs && config_size > 0 ? (void *)configs[0] : NULL);
    return ret;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint *attrib_list)
{
    char attrs[2048];
    EGLContext ret;

    if (real_egl_create_context == NULL) {
        real_egl_create_context =
            (egl_create_context_fn_t)trace_lookup_next("eglCreateContext");
    }
    if (real_egl_create_context == NULL)
        return EGL_NO_CONTEXT;

    trace_egl_attribs(attrs, sizeof(attrs), attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglCreateContext_enter pid=%ld "
               "dpy=%p config=%p share=%p attribs=\"%s\"",
               (long)getpid(), (void *)dpy, (void *)config,
               (void *)share_context, attrs);
    ret = real_egl_create_context(dpy, config, share_context, attrib_list);
    trace_line("chromium_egl_trace phase=eglCreateContext_exit pid=%ld "
               "ret=%p failed=%d",
               (long)getpid(), (void *)ret,
               ret == EGL_NO_CONTEXT ? 1 : 0);
    return ret;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint *attrib_list)
{
    char attrs[2048];
    EGLSurface ret;

    if (real_egl_create_pbuffer_surface == NULL) {
        real_egl_create_pbuffer_surface =
            (egl_create_pbuffer_surface_fn_t)
                trace_lookup_next("eglCreatePbufferSurface");
    }
    if (real_egl_create_pbuffer_surface == NULL)
        return EGL_NO_SURFACE;

    trace_egl_attribs(attrs, sizeof(attrs), attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglCreatePbufferSurface_enter "
               "pid=%ld dpy=%p config=%p attribs=\"%s\"",
               (long)getpid(), (void *)dpy, (void *)config, attrs);
    ret = real_egl_create_pbuffer_surface(dpy, config, attrib_list);
    trace_line("chromium_egl_trace phase=eglCreatePbufferSurface_exit "
               "pid=%ld ret=%p failed=%d",
               (long)getpid(), (void *)ret,
               ret == EGL_NO_SURFACE ? 1 : 0);
    return ret;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint *attrib_list)
{
    char attrs[2048];
    EGLSurface ret;

    if (real_egl_create_window_surface == NULL) {
        real_egl_create_window_surface =
            (egl_create_window_surface_fn_t)
                trace_lookup_next("eglCreateWindowSurface");
    }
    if (real_egl_create_window_surface == NULL)
        return EGL_NO_SURFACE;

    trace_egl_attribs(attrs, sizeof(attrs), attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglCreateWindowSurface_enter "
               "pid=%ld dpy=%p config=%p win=%p attribs=\"%s\"",
               (long)getpid(), (void *)dpy, (void *)config, (void *)win,
               attrs);
    ret = real_egl_create_window_surface(dpy, config, win, attrib_list);
    trace_line("chromium_egl_trace phase=eglCreateWindowSurface_exit "
               "pid=%ld ret=%p failed=%d",
               (long)getpid(), (void *)ret,
               ret == EGL_NO_SURFACE ? 1 : 0);
    return ret;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    __eglMustCastToProperFunctionPointerType ret;
    __eglMustCastToProperFunctionPointerType wrapper;
    int interesting;

    if (real_egl_get_proc_address == NULL) {
        real_egl_get_proc_address =
            (egl_get_proc_address_fn_t)
                trace_lookup_next("eglGetProcAddress");
    }
    if (real_egl_get_proc_address == NULL)
        return NULL;
    ret = real_egl_get_proc_address(procname);
    interesting = trace_egl_symbol_is_interesting(procname);
    wrapper = trace_egl_wrapper_for_name(procname);
    if (trace_enabled() && interesting) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=eglGetProcAddress pid=%ld "
                   "name=%s ret=%p traced=%d",
                   (long)getpid(), procname ? procname : "(null)",
                   (void *)ret, wrapper ? 1 : 0);
    }
    if (wrapper != NULL)
        return wrapper;
    return ret;
}

__attribute__((constructor)) static void chromium_egl_trace_init(void)
{
    __atomic_store_n(&trace_enabled_cached, trace_enabled(),
                     __ATOMIC_RELAXED);
    trace_line("chromium_egl_trace phase=init status=BEGIN pid=%ld ppid=%ld "
               "program=%s log=%s", (long)getpid(), (long)getppid(),
               program_invocation_short_name, trace_path());
    trace_process_identity_once();
}
