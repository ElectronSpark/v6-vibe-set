#define _GNU_SOURCE
#include <errno.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#endif
#ifndef PR_GET_NAME
#define PR_GET_NAME 16
#endif
#ifndef SYS_gettid
#define SYS_gettid SYS_getpid
#endif

static int trace_enabled_cached = -1;
static int trace_proc_enabled_cached = -1;
static int trace_callsite_enabled_cached = -1;
static int trace_wayland_enabled_cached = -1;
static int trace_gtk_enabled_cached = -1;
static int trace_egl_dlsym_wrap_enabled_cached = -1;
static pid_t trace_identity_pid = -1;
static uint32_t trace_identity_last_proc_hash;
static uint32_t trace_identity_last_title_hash;
static ssize_t trace_identity_last_proc_len = -2;
static ssize_t trace_identity_last_title_len = -2;
static int trace_identity_title_log_count;
static int trace_wayland_marshal_log_count;
static int trace_gtk_event_log_count;
static int render_fds[1024];
static int socket_fds[1024];
static int wayland_fds[1024];
static int socket_fd_log_count[1024];
static int wayland_fd_log_count[1024];
static int epoll_fd_log_count[1024];
static int proc_fds[1024];
static int proc_fd_kind[1024];
static int proc_fd_log_count[1024];
static char proc_fd_paths[1024][128];

struct trace_wayland_object {
    int used;
    int fd;
    uint32_t id;
    char class_name[40];
};

static struct trace_wayland_object trace_wayland_objects[2048];

extern char **environ;

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
typedef EGLBoolean (*egl_make_current_fn_t)(EGLDisplay, EGLSurface,
                                            EGLSurface, EGLContext);
typedef EGLBoolean (*egl_swap_buffers_fn_t)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*egl_swap_buffers_with_damage_fn_t)(
    EGLDisplay, EGLSurface, const EGLint *, EGLint);
typedef EGLImage (*egl_create_image_fn_t)(EGLDisplay, EGLContext, EGLenum,
                                          EGLClientBuffer,
                                          const EGLAttrib *);
typedef EGLImageKHR (*egl_create_image_khr_fn_t)(EGLDisplay, EGLContext,
                                                 EGLenum, EGLClientBuffer,
                                                 const EGLint *);
typedef EGLBoolean (*egl_destroy_image_fn_t)(EGLDisplay, EGLImage);
typedef EGLBoolean (*egl_destroy_image_khr_fn_t)(EGLDisplay, EGLImageKHR);
typedef __eglMustCastToProperFunctionPointerType (*egl_get_proc_address_fn_t)(
    const char *);
typedef void *(*wl_egl_window_create_fn_t)(void *, int, int);
typedef void (*wl_egl_window_destroy_fn_t)(void *);
typedef void (*wl_egl_window_resize_fn_t)(void *, int, int, int, int);
typedef void *(*gbm_bo_create_fn_t)(void *, uint32_t, uint32_t, uint32_t,
                                    uint32_t);
typedef void *(*gbm_bo_create_with_modifiers_fn_t)(void *, uint32_t, uint32_t,
                                                   uint32_t, const uint64_t *,
                                                   unsigned int);
typedef void *(*gbm_bo_create_with_modifiers2_fn_t)(void *, uint32_t, uint32_t,
                                                    uint32_t, const uint64_t *,
                                                    unsigned int, uint32_t);
typedef void *(*gbm_bo_import_fn_t)(void *, uint32_t, void *, uint32_t);
typedef void (*gbm_bo_destroy_fn_t)(void *);
typedef void *(*dlsym_fn_t)(void *, const char *);
struct wl_object;
struct wl_proxy;
struct wl_array;

union wl_argument {
    int32_t i;
    uint32_t u;
    int32_t f;
    const char *s;
    struct wl_object *o;
    uint32_t n;
    struct wl_array *a;
    int32_t h;
};

struct wl_message {
    const char *name;
    const char *signature;
    const struct wl_interface **types;
};

struct wl_interface {
    const char *name;
    int version;
    int method_count;
    const struct wl_message *methods;
    int event_count;
    const struct wl_message *events;
};

typedef struct wl_proxy *(*wl_proxy_marshal_flags_fn_t)(
    struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t,
    uint32_t, ...);
typedef struct wl_proxy *(*wl_proxy_marshal_array_flags_fn_t)(
    struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t,
    uint32_t, union wl_argument *);
typedef void (*wl_proxy_marshal_fn_t)(struct wl_proxy *, uint32_t, ...);
typedef struct wl_proxy *(*wl_proxy_marshal_constructor_fn_t)(
    struct wl_proxy *, uint32_t, const struct wl_interface *, ...);
typedef struct wl_proxy *(*wl_proxy_marshal_constructor_versioned_fn_t)(
    struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t, ...);
typedef void (*wl_proxy_marshal_array_fn_t)(
    struct wl_proxy *, uint32_t, union wl_argument *);
typedef struct wl_proxy *(*wl_proxy_marshal_array_constructor_fn_t)(
    struct wl_proxy *, uint32_t, union wl_argument *,
    const struct wl_interface *);
typedef struct wl_proxy *(*wl_proxy_marshal_array_constructor_versioned_fn_t)(
    struct wl_proxy *, uint32_t, union wl_argument *,
    const struct wl_interface *, uint32_t);
typedef const char *(*wl_proxy_get_class_fn_t)(struct wl_proxy *);
typedef uint32_t (*wl_proxy_get_version_fn_t)(struct wl_proxy *);
typedef const char *(*g_type_name_from_instance_fn_t)(const void *);
typedef int (*gtk_widget_get_bool_fn_t)(void *);
typedef void *(*gtk_widget_get_window_fn_t)(void *);
typedef const char *(*gtk_window_get_title_fn_t)(void *);
typedef int (*gdk_window_get_int_fn_t)(void *);
typedef void (*void_ptr_fn_t)(void *);
typedef void (*void_ptr_int_fn_t)(void *, int);
typedef void (*void_ptr_uint32_fn_t)(void *, uint32_t);
typedef int (*int_ptr_fn_t)(void *);
typedef void *(*ptr_ptr_fn_t)(void *);
typedef void (*void_ptr_const_char_fn_t)(void *, const char *);

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
static egl_make_current_fn_t real_egl_make_current;
static egl_swap_buffers_fn_t real_egl_swap_buffers;
static egl_swap_buffers_with_damage_fn_t real_egl_swap_buffers_with_damage_ext;
static egl_swap_buffers_with_damage_fn_t real_egl_swap_buffers_with_damage_khr;
static egl_create_image_fn_t real_egl_create_image;
static egl_create_image_khr_fn_t real_egl_create_image_khr;
static egl_destroy_image_fn_t real_egl_destroy_image;
static egl_destroy_image_khr_fn_t real_egl_destroy_image_khr;
static egl_get_proc_address_fn_t real_egl_get_proc_address;
static wl_egl_window_create_fn_t real_wl_egl_window_create;
static wl_egl_window_destroy_fn_t real_wl_egl_window_destroy;
static wl_egl_window_resize_fn_t real_wl_egl_window_resize;
static gbm_bo_create_fn_t real_gbm_bo_create;
static gbm_bo_create_with_modifiers_fn_t real_gbm_bo_create_with_modifiers;
static gbm_bo_create_with_modifiers2_fn_t real_gbm_bo_create_with_modifiers2;
static gbm_bo_import_fn_t real_gbm_bo_import;
static gbm_bo_destroy_fn_t real_gbm_bo_destroy;
static wl_proxy_marshal_flags_fn_t real_wl_proxy_marshal_flags;
static wl_proxy_marshal_array_flags_fn_t real_wl_proxy_marshal_array_flags;
static wl_proxy_marshal_fn_t real_wl_proxy_marshal;
static wl_proxy_marshal_constructor_fn_t real_wl_proxy_marshal_constructor;
static wl_proxy_marshal_constructor_versioned_fn_t
    real_wl_proxy_marshal_constructor_versioned;
static wl_proxy_marshal_array_fn_t real_wl_proxy_marshal_array;
static wl_proxy_marshal_array_constructor_fn_t
    real_wl_proxy_marshal_array_constructor;
static wl_proxy_marshal_array_constructor_versioned_fn_t
    real_wl_proxy_marshal_array_constructor_versioned;
static wl_proxy_get_class_fn_t real_wl_proxy_get_class;
static wl_proxy_get_version_fn_t real_wl_proxy_get_version;
static dlsym_fn_t real_dlsym;
static g_type_name_from_instance_fn_t real_g_type_name_from_instance;
static gtk_widget_get_bool_fn_t real_gtk_widget_get_visible;
static gtk_widget_get_bool_fn_t real_gtk_widget_get_realized;
static gtk_widget_get_bool_fn_t real_gtk_widget_get_mapped;
static gtk_widget_get_window_fn_t real_gtk_widget_get_window;
static gtk_window_get_title_fn_t real_gtk_window_get_title;
static gdk_window_get_int_fn_t real_gdk_window_is_visible;
static gdk_window_get_int_fn_t real_gdk_window_get_state;
static gdk_window_get_int_fn_t real_gdk_window_get_width;
static gdk_window_get_int_fn_t real_gdk_window_get_height;
static void_ptr_fn_t real_gtk_widget_show;
static void_ptr_fn_t real_gtk_widget_show_all;
static void_ptr_fn_t real_gtk_widget_show_now;
static void_ptr_fn_t real_gtk_widget_realize;
static void_ptr_fn_t real_gtk_widget_map;
static void_ptr_int_fn_t real_gtk_widget_set_visible;
static void_ptr_fn_t real_gtk_window_present;
static void_ptr_uint32_fn_t real_gtk_window_present_with_time;
static void_ptr_fn_t real_gdk_window_show;
static void_ptr_fn_t real_gdk_window_show_unraised;
static int_ptr_fn_t real_gdk_window_ensure_native;
static ptr_ptr_fn_t real_gdk_wayland_window_get_wl_surface;
static void_ptr_const_char_fn_t real_gdk_window_set_title;

static void trace_process_identity_once(void);
static ssize_t trace_read_file_raw(const char *path, char *buf, size_t size);
static void trace_proc_open_event(const char *phase, int dirfd,
                                  const char *path, int flags, int fd,
                                  int saved_errno, int kind);
static void trace_proc_read_event(int fd, size_t requested, ssize_t ret,
                                  int saved_errno, const void *buf);
static dlsym_fn_t trace_real_dlsym(void);
static void *trace_lookup_next(const char *symbol);
static void *trace_dynamic_wrapper_for_name(const char *symbol);
static int trace_dynamic_symbol_interesting(const char *symbol);
static int trace_dynamic_symbol_trace_enabled(const char *symbol);
static int trace_egl_symbol_is_interesting(const char *procname);
static __eglMustCastToProperFunctionPointerType
trace_egl_wrapper_for_name(const char *procname);
EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface,
                                       const EGLint *rects, EGLint n_rects);
EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surface,
                                       const EGLint *rects, EGLint n_rects);
EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                              EGLClientBuffer buffer,
                              const EGLint *attrib_list);
EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image);
void *wl_egl_window_create(void *surface, int width, int height);
void wl_egl_window_destroy(void *egl_window);
void wl_egl_window_resize(void *egl_window, int width, int height, int dx,
                          int dy);
void *gbm_bo_create(void *gbm, uint32_t width, uint32_t height,
                    uint32_t format, uint32_t flags);
void *gbm_bo_create_with_modifiers(void *gbm, uint32_t width, uint32_t height,
                                   uint32_t format, const uint64_t *modifiers,
                                   unsigned int count);
void *gbm_bo_create_with_modifiers2(void *gbm, uint32_t width, uint32_t height,
                                    uint32_t format,
                                    const uint64_t *modifiers,
                                    unsigned int count, uint32_t flags);
void *gbm_bo_import(void *gbm, uint32_t type, void *buffer, uint32_t flags);
void gbm_bo_destroy(void *bo);

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

static int trace_proc_enabled(void)
{
    const char *value;
    int cached;

    if (!trace_enabled())
        return 0;

    cached = __atomic_load_n(&trace_proc_enabled_cached, __ATOMIC_RELAXED);
    if (cached >= 0)
        return cached;
    value = getenv("CHROMIUM_EGL_TRACE_PROC");
    cached = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    __atomic_store_n(&trace_proc_enabled_cached, cached, __ATOMIC_RELAXED);
    return cached;
}

static int trace_callsite_enabled(void)
{
    const char *value;
    int cached;

    if (!trace_enabled())
        return 0;

    cached = __atomic_load_n(&trace_callsite_enabled_cached,
                             __ATOMIC_RELAXED);
    if (cached >= 0)
        return cached;
    value = getenv("CHROMIUM_EGL_TRACE_CALLSITE");
    cached = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    __atomic_store_n(&trace_callsite_enabled_cached, cached,
                     __ATOMIC_RELAXED);
    return cached;
}

static int trace_wayland_enabled(void)
{
    const char *value;
    int cached;

    if (!trace_enabled())
        return 0;

    cached = __atomic_load_n(&trace_wayland_enabled_cached, __ATOMIC_RELAXED);
    if (cached >= 0)
        return cached;
    value = getenv("CHROMIUM_EGL_TRACE_WAYLAND");
    cached = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    __atomic_store_n(&trace_wayland_enabled_cached, cached,
                     __ATOMIC_RELAXED);
    return cached;
}

static int trace_gtk_enabled(void)
{
    const char *value;
    int cached;

    if (!trace_enabled())
        return 0;

    cached = __atomic_load_n(&trace_gtk_enabled_cached, __ATOMIC_RELAXED);
    if (cached >= 0)
        return cached;
    value = getenv("CHROMIUM_EGL_TRACE_GTK");
    cached = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    __atomic_store_n(&trace_gtk_enabled_cached, cached,
                     __ATOMIC_RELAXED);
    return cached;
}

static int trace_egl_dlsym_wrap_enabled(void)
{
    const char *value;
    int cached;

    if (!trace_enabled())
        return 0;

    cached = __atomic_load_n(&trace_egl_dlsym_wrap_enabled_cached,
                             __ATOMIC_RELAXED);
    if (cached >= 0)
        return cached;
    value = getenv("CHROMIUM_EGL_TRACE_DLSYM_WRAP");
    cached = value && value[0] && strcmp(value, "0") != 0 ? 1 : 0;
    __atomic_store_n(&trace_egl_dlsym_wrap_enabled_cached, cached,
                     __ATOMIC_RELAXED);
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

static ssize_t trace_read_file_raw(const char *path, char *buf, size_t size)
{
    int fd;
    int saved_errno;
    ssize_t n;

    fd = (int)syscall(SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    n = (ssize_t)syscall(SYS_read, fd, buf, size);
    saved_errno = errno;
    (void)syscall(SYS_close, fd);
    errno = saved_errno;
    return n;
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

static dlsym_fn_t trace_real_dlsym(void)
{
    dlsym_fn_t resolved;

    resolved = __atomic_load_n(&real_dlsym, __ATOMIC_RELAXED);
    if (resolved != NULL)
        return resolved;
#if defined(__GLIBC__)
    resolved = (dlsym_fn_t)dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
#endif
    if (resolved != NULL)
        __atomic_store_n(&real_dlsym, resolved, __ATOMIC_RELAXED);
    return resolved;
}

static void *trace_lookup_next(const char *symbol)
{
    dlsym_fn_t resolved;

    if (symbol == NULL)
        return NULL;
    resolved = trace_real_dlsym();
    if (resolved == NULL)
        return NULL;
    return resolved(RTLD_NEXT, symbol);
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
           strcmp(procname, "eglCreateWindowSurface") == 0 ||
           strcmp(procname, "eglMakeCurrent") == 0 ||
           strcmp(procname, "eglSwapBuffers") == 0 ||
           strcmp(procname, "eglSwapBuffersWithDamageEXT") == 0 ||
           strcmp(procname, "eglSwapBuffersWithDamageKHR") == 0 ||
           strcmp(procname, "eglCreateImage") == 0 ||
           strcmp(procname, "eglCreateImageKHR") == 0 ||
           strcmp(procname, "eglDestroyImage") == 0 ||
           strcmp(procname, "eglDestroyImageKHR") == 0;
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
    if (strcmp(procname, "eglMakeCurrent") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglMakeCurrent;
    if (strcmp(procname, "eglSwapBuffers") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglSwapBuffers;
    if (strcmp(procname, "eglSwapBuffersWithDamageEXT") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglSwapBuffersWithDamageEXT;
    if (strcmp(procname, "eglSwapBuffersWithDamageKHR") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglSwapBuffersWithDamageKHR;
    if (strcmp(procname, "eglCreateImage") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglCreateImage;
    if (strcmp(procname, "eglCreateImageKHR") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglCreateImageKHR;
    if (strcmp(procname, "eglDestroyImage") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglDestroyImage;
    if (strcmp(procname, "eglDestroyImageKHR") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglDestroyImageKHR;
    return NULL;
}

enum {
    TRACE_PROC_KIND_NONE = 0,
    TRACE_PROC_KIND_CMDLINE,
    TRACE_PROC_KIND_COMM,
    TRACE_PROC_KIND_STAT,
    TRACE_PROC_KIND_STATUS,
};

static const char *trace_proc_kind_name(int kind)
{
    switch (kind) {
    case TRACE_PROC_KIND_CMDLINE:
        return "cmdline";
    case TRACE_PROC_KIND_COMM:
        return "comm";
    case TRACE_PROC_KIND_STAT:
        return "stat";
    case TRACE_PROC_KIND_STATUS:
        return "status";
    default:
        return "unknown";
    }
}

static int trace_string_ends_with(const char *s, const char *suffix)
{
    size_t s_len;
    size_t suffix_len;

    if (!s || !suffix)
        return 0;
    s_len = strlen(s);
    suffix_len = strlen(suffix);
    if (s_len < suffix_len)
        return 0;
    return memcmp(s + s_len - suffix_len, suffix, suffix_len) == 0;
}

static int trace_proc_path_kind_id(const char *path)
{
    int procish;

    if (!path || path[0] == '\0')
        return TRACE_PROC_KIND_NONE;
    procish = strncmp(path, "/proc/", 6) == 0 ||
              strncmp(path, "proc/", 5) == 0 ||
              strstr(path, "/proc/") != NULL ||
              strcmp(path, "cmdline") == 0 ||
              strcmp(path, "comm") == 0 ||
              strcmp(path, "stat") == 0 ||
              strcmp(path, "status") == 0;
    if (!procish)
        return TRACE_PROC_KIND_NONE;
    if (strcmp(path, "cmdline") == 0 ||
        trace_string_ends_with(path, "/cmdline"))
        return TRACE_PROC_KIND_CMDLINE;
    if (strcmp(path, "comm") == 0 || trace_string_ends_with(path, "/comm"))
        return TRACE_PROC_KIND_COMM;
    if (strcmp(path, "stat") == 0 || trace_string_ends_with(path, "/stat"))
        return TRACE_PROC_KIND_STAT;
    if (strcmp(path, "status") == 0 ||
        trace_string_ends_with(path, "/status"))
        return TRACE_PROC_KIND_STATUS;
    return TRACE_PROC_KIND_NONE;
}

static void trace_proc_fd_mark(int fd, const char *path, int kind)
{
    if (fd < 0 || fd >= (int)(sizeof(proc_fds) / sizeof(proc_fds[0])) ||
        kind == TRACE_PROC_KIND_NONE)
        return;
    proc_fds[fd] = 1;
    proc_fd_kind[fd] = kind;
    proc_fd_log_count[fd] = 0;
    snprintf(proc_fd_paths[fd], sizeof(proc_fd_paths[fd]), "%s",
             path ? path : "(null)");
}

static int trace_fd_is_proc(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(proc_fds) / sizeof(proc_fds[0])))
        return 0;
    return proc_fds[fd];
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

static void trace_wayland_object_clear_fd(int fd)
{
    for (size_t i = 0; i < sizeof(trace_wayland_objects) /
                           sizeof(trace_wayland_objects[0]); i++) {
        if (trace_wayland_objects[i].used &&
            trace_wayland_objects[i].fd == fd) {
            trace_wayland_objects[i].used = 0;
            trace_wayland_objects[i].fd = -1;
            trace_wayland_objects[i].id = 0;
            trace_wayland_objects[i].class_name[0] = '\0';
        }
    }
}

static void trace_wayland_object_set(int fd, uint32_t id,
                                     const char *class_name)
{
    size_t free_index = sizeof(trace_wayland_objects) /
                        sizeof(trace_wayland_objects[0]);

    if (fd < 0 || id == 0 || class_name == NULL || class_name[0] == '\0')
        return;
    for (size_t i = 0; i < sizeof(trace_wayland_objects) /
                           sizeof(trace_wayland_objects[0]); i++) {
        if (trace_wayland_objects[i].used &&
            trace_wayland_objects[i].fd == fd &&
            trace_wayland_objects[i].id == id) {
            snprintf(trace_wayland_objects[i].class_name,
                     sizeof(trace_wayland_objects[i].class_name), "%s",
                     class_name);
            return;
        }
        if (!trace_wayland_objects[i].used &&
            free_index == sizeof(trace_wayland_objects) /
                          sizeof(trace_wayland_objects[0])) {
            free_index = i;
        }
    }
    if (free_index < sizeof(trace_wayland_objects) /
                     sizeof(trace_wayland_objects[0])) {
        trace_wayland_objects[free_index].used = 1;
        trace_wayland_objects[free_index].fd = fd;
        trace_wayland_objects[free_index].id = id;
        snprintf(trace_wayland_objects[free_index].class_name,
                 sizeof(trace_wayland_objects[free_index].class_name), "%s",
                 class_name);
    }
}

static const char *trace_wayland_object_class(int fd, uint32_t id)
{
    if (id == 1)
        return "wl_display";
    for (size_t i = 0; i < sizeof(trace_wayland_objects) /
                           sizeof(trace_wayland_objects[0]); i++) {
        if (trace_wayland_objects[i].used &&
            trace_wayland_objects[i].fd == fd &&
            trace_wayland_objects[i].id == id) {
            return trace_wayland_objects[i].class_name;
        }
    }
    return "unknown";
}

static void trace_wayland_object_copy_fd(int dst, int src)
{
    trace_wayland_object_clear_fd(dst);
    for (size_t i = 0; i < sizeof(trace_wayland_objects) /
                           sizeof(trace_wayland_objects[0]); i++) {
        if (trace_wayland_objects[i].used &&
            trace_wayland_objects[i].fd == src) {
            trace_wayland_object_set(dst, trace_wayland_objects[i].id,
                                     trace_wayland_objects[i].class_name);
        }
    }
}

static void trace_wayland_fd_mark(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(wayland_fds) / sizeof(wayland_fds[0])))
        return;
    wayland_fds[fd] = 1;
    wayland_fd_log_count[fd] = 0;
    trace_wayland_object_clear_fd(fd);
    trace_wayland_object_set(fd, 1, "wl_display");
}

static int trace_fd_is_wayland(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(wayland_fds) / sizeof(wayland_fds[0])))
        return 0;
    return wayland_fds[fd];
}

static void trace_fd_clear(int fd)
{
    if (fd < 0 || fd >= (int)(sizeof(render_fds) / sizeof(render_fds[0])))
        return;
    render_fds[fd] = 0;
    socket_fds[fd] = 0;
    wayland_fds[fd] = 0;
    socket_fd_log_count[fd] = 0;
    wayland_fd_log_count[fd] = 0;
    epoll_fd_log_count[fd] = 0;
    proc_fds[fd] = 0;
    proc_fd_kind[fd] = TRACE_PROC_KIND_NONE;
    proc_fd_log_count[fd] = 0;
    proc_fd_paths[fd][0] = '\0';
    trace_wayland_object_clear_fd(fd);
}

static void trace_fd_copy(int dst, int src)
{
    char proc_path[sizeof(proc_fd_paths[0])];

    if (dst < 0 || dst >= (int)(sizeof(render_fds) / sizeof(render_fds[0])))
        return;
    if (src < 0 || src >= (int)(sizeof(render_fds) / sizeof(render_fds[0]))) {
        trace_fd_clear(dst);
        return;
    }
    if (dst == src)
        return;
    render_fds[dst] = render_fds[src];
    socket_fds[dst] = socket_fds[src];
    wayland_fds[dst] = wayland_fds[src];
    socket_fd_log_count[dst] = 0;
    wayland_fd_log_count[dst] = 0;
    epoll_fd_log_count[dst] = 0;
    proc_fds[dst] = proc_fds[src];
    proc_fd_kind[dst] = proc_fd_kind[src];
    proc_fd_log_count[dst] = 0;
    snprintf(proc_path, sizeof(proc_path), "%s", proc_fd_paths[src]);
    memcpy(proc_fd_paths[dst], proc_path, sizeof(proc_fd_paths[dst]));
    if (wayland_fds[src])
        trace_wayland_object_copy_fd(dst, src);
    else
        trace_wayland_object_clear_fd(dst);
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

static void trace_escape_text(char *dst, size_t dst_size, const char *src)
{
    size_t off = 0;

    if (dst_size == 0)
        return;
    dst[0] = '\0';
    append_escaped_bytes(dst, dst_size, &off, src,
                         src ? (ssize_t)strlen(src) : 0);
}

static void trace_proc_open_event(const char *phase, int dirfd,
                                  const char *path, int flags, int fd,
                                  int saved_errno, int kind)
{
    char escaped_path[256];

    if (!trace_proc_enabled() || kind == TRACE_PROC_KIND_NONE)
        return;
    trace_process_identity_once();
    trace_escape_text(escaped_path, sizeof(escaped_path), path);
    trace_line("chromium_egl_trace phase=%s pid=%ld dirfd=%d path=\"%s\" "
               "kind=%s flags=0x%x fd=%d errno=%d",
               phase, (long)getpid(), dirfd, escaped_path,
               trace_proc_kind_name(kind), flags, fd,
               fd < 0 ? saved_errno : 0);
}

static void trace_proc_read_event(int fd, size_t requested, ssize_t ret,
                                  int saved_errno, const void *buf)
{
    char sample[512];
    char escaped_path[256];
    size_t off = 0;
    ssize_t sample_len = ret;

    if (!trace_proc_enabled() || !trace_fd_is_proc(fd))
        return;
    if (proc_fd_log_count[fd] >= 16) {
        if (proc_fd_log_count[fd] == 16) {
            trace_escape_text(escaped_path, sizeof(escaped_path),
                              proc_fd_paths[fd]);
            trace_line("chromium_egl_trace phase=proc_drop pid=%ld fd=%d "
                       "kind=%s path=\"%s\" reason=budget",
                       (long)getpid(), fd,
                       trace_proc_kind_name(proc_fd_kind[fd]), escaped_path);
        }
        proc_fd_log_count[fd]++;
        return;
    }
    proc_fd_log_count[fd]++;
    trace_process_identity_once();
    if (sample_len > 192)
        sample_len = 192;
    if (sample_len < 0)
        sample_len = 0;
    sample[0] = '\0';
    append_escaped_bytes(sample, sizeof(sample), &off, buf, sample_len);
    trace_escape_text(escaped_path, sizeof(escaped_path), proc_fd_paths[fd]);
    trace_line("chromium_egl_trace phase=proc_read pid=%ld fd=%d kind=%s "
               "path=\"%s\" requested=%zu ret=%ld errno=%d sample=\"%s\"",
               (long)getpid(), fd, trace_proc_kind_name(proc_fd_kind[fd]),
               escaped_path, requested, (long)ret,
               ret < 0 ? saved_errno : 0, sample);
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
                          const char *fmt, ...);

static int trace_mojom_name_char(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.';
}

static int trace_token_list_has(const char *list, const char *token)
{
    size_t token_len;
    const char *p;

    if (!list || !token)
        return 0;
    token_len = strlen(token);
    p = list;
    while (*p) {
        const char *end = strchr(p, '|');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len == token_len && memcmp(p, token, len) == 0)
            return 1;
        if (!end)
            break;
        p = end + 1;
    }
    return 0;
}

static void trace_append_mojom_token(char *dst, size_t dst_size, size_t *off,
                                     int *count, const char *start,
                                     size_t len)
{
    char token[192];

    if (!dst || !off || !count || !start || len == 0 ||
        len >= sizeof(token) || *count >= 16)
        return;
    memcpy(token, start, len);
    token[len] = '\0';
    if (strstr(token, ".mojom.") == NULL || trace_token_list_has(dst, token))
        return;
    trace_appendf(dst, dst_size, off, "%s%s", *count ? "|" : "", token);
    (*count)++;
}

static void trace_mojom_from_bytes(char *dst, size_t dst_size, size_t *off,
                                   int *count, const char *bytes, size_t len)
{
    static const char needle[] = ".mojom.";
    const size_t needle_len = sizeof(needle) - 1;
    const char *end;
    const char *p;

    if (!bytes || len < needle_len)
        return;
    end = bytes + len;
    p = bytes;
    while (p + needle_len <= end && *count < 16) {
        const char *hit = memmem(p, (size_t)(end - p), needle, needle_len);
        const char *lo;
        const char *hi;

        if (!hit)
            break;
        lo = hit;
        while (lo > bytes && trace_mojom_name_char((unsigned char)lo[-1]))
            lo--;
        hi = hit + needle_len;
        while (hi < end && trace_mojom_name_char((unsigned char)*hi))
            hi++;
        if (lo < hit && hi > hit + needle_len)
            trace_append_mojom_token(dst, dst_size, off, count, lo,
                                     (size_t)(hi - lo));
        p = hit + needle_len;
    }
}

static int trace_iov_mojom_interfaces(char *dst, size_t dst_size,
                                      const struct iovec *iov, size_t iovlen,
                                      ssize_t len)
{
    size_t off = 0;
    size_t remaining;
    int count = 0;

    if (dst_size == 0)
        return 0;
    dst[0] = '\0';
    if (!iov || iovlen == 0 || len <= 0)
        return 0;
    remaining = (size_t)len;
    for (size_t i = 0; i < iovlen && i < 1024 && remaining > 0; i++) {
        size_t chunk = iov[i].iov_len;

        if (chunk > remaining)
            chunk = remaining;
        trace_mojom_from_bytes(dst, dst_size, &off, &count,
                               (const char *)iov[i].iov_base, chunk);
        remaining -= chunk;
        if (count >= 16)
            break;
    }
    return count;
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

static int trace_epoll_budget_ok(int epfd, int ready)
{
    int limit = ready ? 256 : 96;

    if (!trace_callsite_enabled())
        return 0;
    if (epfd < 0 || epfd >= (int)(sizeof(epoll_fd_log_count) /
                                  sizeof(epoll_fd_log_count[0])))
        return 0;
    if (epoll_fd_log_count[epfd] >= limit) {
        if (epoll_fd_log_count[epfd] == limit) {
            epoll_fd_log_count[epfd]++;
            trace_line("chromium_egl_trace phase=epoll_drop pid=%ld tid=%ld "
                       "epfd=%d reason=budget ready=%d",
                       (long)getpid(), (long)syscall(SYS_gettid), epfd,
                       ready);
        }
        return 0;
    }
    epoll_fd_log_count[epfd]++;
    return 1;
}

static void trace_callsite_fields(char *dst, size_t dst_size, void *caller)
{
    Dl_info info;
    char obj[512];
    char sym[256];
    unsigned long obj_off = 0;
    unsigned long sym_off = 0;

    if (dst_size == 0)
        return;
    if (!trace_callsite_enabled() || caller == NULL) {
        snprintf(dst, dst_size, "callsite=0 tid=%ld",
                 (long)syscall(SYS_gettid));
        return;
    }

    memset(&info, 0, sizeof(info));
    obj[0] = '\0';
    sym[0] = '\0';
    if (dladdr(caller, &info) != 0) {
        trace_escape_text(obj, sizeof(obj), info.dli_fname);
        trace_escape_text(sym, sizeof(sym), info.dli_sname);
        if (info.dli_fbase != NULL) {
            obj_off = (unsigned long)((uintptr_t)caller -
                                      (uintptr_t)info.dli_fbase);
        }
        if (info.dli_saddr != NULL) {
            sym_off = (unsigned long)((uintptr_t)caller -
                                      (uintptr_t)info.dli_saddr);
        }
    }
    snprintf(dst, dst_size,
             "callsite=1 tid=%ld caller=%p caller_obj=\"%s\" "
             "caller_obj_off=0x%lx caller_sym=\"%s\" caller_sym_off=0x%lx",
             (long)syscall(SYS_gettid), caller, obj, obj_off, sym, sym_off);
}

static int trace_wayland_budget_ok(void)
{
    int old;

    if (!trace_wayland_enabled())
        return 0;
    old = __atomic_fetch_add(&trace_wayland_marshal_log_count, 1,
                             __ATOMIC_RELAXED);
    if (old < 1536)
        return 1;
    if (old == 1536) {
        trace_line("chromium_egl_trace phase=wayland_marshal_drop pid=%ld "
                   "reason=budget",
                   (long)getpid());
    }
    return 0;
}

static const char *trace_wayland_known_request(const char *class_name,
                                               uint32_t opcode)
{
    if (!class_name)
        return "unknown";
    if (strcmp(class_name, "wl_display") == 0) {
        switch (opcode) {
        case 0: return "sync";
        case 1: return "get_registry";
        }
    } else if (strcmp(class_name, "wl_registry") == 0) {
        if (opcode == 0)
            return "bind";
    } else if (strcmp(class_name, "wl_compositor") == 0) {
        switch (opcode) {
        case 0: return "create_surface";
        case 1: return "create_region";
        }
    } else if (strcmp(class_name, "wl_shm") == 0) {
        if (opcode == 0)
            return "create_pool";
    } else if (strcmp(class_name, "wl_shm_pool") == 0) {
        switch (opcode) {
        case 0: return "create_buffer";
        case 1: return "destroy";
        case 2: return "resize";
        }
    } else if (strcmp(class_name, "wl_surface") == 0) {
        switch (opcode) {
        case 0: return "destroy";
        case 1: return "attach";
        case 2: return "damage";
        case 3: return "frame";
        case 4: return "set_opaque_region";
        case 5: return "set_input_region";
        case 6: return "commit";
        case 7: return "set_buffer_transform";
        case 8: return "set_buffer_scale";
        case 9: return "damage_buffer";
        case 10: return "offset";
        }
    } else if (strcmp(class_name, "xdg_wm_base") == 0) {
        switch (opcode) {
        case 0: return "destroy";
        case 1: return "create_positioner";
        case 2: return "get_xdg_surface";
        case 3: return "pong";
        }
    } else if (strcmp(class_name, "xdg_surface") == 0) {
        switch (opcode) {
        case 0: return "destroy";
        case 1: return "get_toplevel";
        case 2: return "get_popup";
        case 3: return "set_window_geometry";
        case 4: return "ack_configure";
        }
    } else if (strcmp(class_name, "xdg_toplevel") == 0) {
        switch (opcode) {
        case 0: return "destroy";
        case 1: return "set_parent";
        case 2: return "set_title";
        case 3: return "set_app_id";
        case 4: return "show_window_menu";
        case 5: return "move";
        case 6: return "resize";
        case 7: return "set_max_size";
        case 8: return "set_min_size";
        case 9: return "set_maximized";
        case 10: return "unset_maximized";
        case 11: return "set_fullscreen";
        case 12: return "unset_fullscreen";
        case 13: return "set_minimized";
        }
    }
    return "unknown";
}

static const char *trace_wayland_request_name(const char *class_name,
                                              const struct wl_interface *iface,
                                              uint32_t opcode)
{
    (void)iface;
    return trace_wayland_known_request(class_name, opcode);
}

static void trace_wayland_marshal_event(const char *phase, struct wl_proxy *proxy,
                                        uint32_t opcode,
                                        const struct wl_interface *interface,
                                        uint32_t version, uint32_t flags,
                                        void *caller)
{
    char callsite[1024];
    const char *class_name = "unknown";
    const char *request_name;
    const char *new_interface = "none";
    uint32_t proxy_version = 0;

    if (!trace_wayland_budget_ok())
        return;
    if (real_wl_proxy_get_class == NULL) {
        real_wl_proxy_get_class =
            (wl_proxy_get_class_fn_t)trace_lookup_next("wl_proxy_get_class");
    }
    if (real_wl_proxy_get_version == NULL) {
        real_wl_proxy_get_version =
            (wl_proxy_get_version_fn_t)trace_lookup_next("wl_proxy_get_version");
    }
    if (real_wl_proxy_get_class != NULL && proxy != NULL) {
        const char *resolved = real_wl_proxy_get_class(proxy);
        if (resolved != NULL)
            class_name = resolved;
    }
    if (real_wl_proxy_get_version != NULL && proxy != NULL)
        proxy_version = real_wl_proxy_get_version(proxy);
    if (interface != NULL && interface->name != NULL)
        new_interface = interface->name;
    request_name = trace_wayland_request_name(class_name, interface, opcode);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_line("chromium_egl_trace phase=%s pid=%ld proxy=%p class=%s "
               "opcode=%u request=%s new_interface=%s version=%u "
               "proxy_version=%u flags=0x%x %s",
               phase, (long)getpid(), proxy, class_name, opcode, request_name,
               new_interface, version, proxy_version, flags, callsite);
}

static int trace_wayland_wire_budget_ok(int fd)
{
    if (!trace_wayland_enabled() || !trace_fd_is_wayland(fd))
        return 0;
    if (fd < 0 || fd >= (int)(sizeof(wayland_fd_log_count) /
                              sizeof(wayland_fd_log_count[0])))
        return 0;
    if (wayland_fd_log_count[fd] >= 2048) {
        if (wayland_fd_log_count[fd] == 2048) {
            wayland_fd_log_count[fd]++;
            trace_line("chromium_egl_trace phase=wayland_wire_drop pid=%ld "
                       "fd=%d reason=budget",
                       (long)getpid(), fd);
        }
        return 0;
    }
    wayland_fd_log_count[fd]++;
    return 1;
}

static uint32_t trace_wayland_load_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int trace_wayland_wire_string(const unsigned char *payload,
                                     size_t payload_len, size_t off,
                                     char *dst, size_t dst_size,
                                     size_t *next)
{
    uint32_t len;
    size_t padded;
    size_t copy_len;

    if (dst_size > 0)
        dst[0] = '\0';
    if (next)
        *next = off;
    if (off + 4 > payload_len)
        return 0;
    len = trace_wayland_load_u32(payload + off);
    off += 4;
    padded = (len + 3U) & ~3U;
    if (len == 0 || off + padded > payload_len)
        return 0;
    copy_len = len;
    if (copy_len > 0 && payload[off + copy_len - 1] == '\0')
        copy_len--;
    if (dst_size > 0) {
        if (copy_len >= dst_size)
            copy_len = dst_size - 1;
        memcpy(dst, payload + off, copy_len);
        dst[copy_len] = '\0';
    }
    if (next)
        *next = off + padded;
    return 1;
}

static const char *
trace_wayland_new_object_class(const char *class_name, uint32_t opcode,
                               const unsigned char *payload,
                               size_t payload_len, uint32_t *new_id,
                               uint32_t *arg_object, char *dynamic_class,
                               size_t dynamic_class_size)
{
    if (new_id)
        *new_id = 0;
    if (arg_object)
        *arg_object = 0;
    if (dynamic_class_size > 0)
        dynamic_class[0] = '\0';
    if (!class_name || payload_len < 4)
        return NULL;

    if (strcmp(class_name, "wl_display") == 0) {
        if (opcode == 0) {
            if (new_id)
                *new_id = trace_wayland_load_u32(payload);
            return "wl_callback";
        }
        if (opcode == 1) {
            if (new_id)
                *new_id = trace_wayland_load_u32(payload);
            return "wl_registry";
        }
    } else if (strcmp(class_name, "wl_registry") == 0 && opcode == 0) {
        size_t next = 0;
        uint32_t id = 0;

        if (arg_object)
            *arg_object = trace_wayland_load_u32(payload);
        if (!trace_wayland_wire_string(payload, payload_len, 4,
                                       dynamic_class, dynamic_class_size,
                                       &next)) {
            return NULL;
        }
        if (next + 8 <= payload_len)
            id = trace_wayland_load_u32(payload + next + 4);
        if (new_id)
            *new_id = id;
        return dynamic_class[0] ? dynamic_class : NULL;
    } else if (strcmp(class_name, "wl_compositor") == 0) {
        if (new_id)
            *new_id = trace_wayland_load_u32(payload);
        if (opcode == 0)
            return "wl_surface";
        if (opcode == 1)
            return "wl_region";
    } else if (strcmp(class_name, "wl_shm") == 0 && opcode == 0) {
        if (new_id)
            *new_id = trace_wayland_load_u32(payload);
        return "wl_shm_pool";
    } else if (strcmp(class_name, "wl_shm_pool") == 0 && opcode == 0) {
        if (new_id)
            *new_id = trace_wayland_load_u32(payload);
        return "wl_buffer";
    } else if (strcmp(class_name, "wl_surface") == 0 && opcode == 3) {
        if (new_id)
            *new_id = trace_wayland_load_u32(payload);
        return "wl_callback";
    } else if (strcmp(class_name, "xdg_wm_base") == 0) {
        if (opcode == 1) {
            if (new_id)
                *new_id = trace_wayland_load_u32(payload);
            return "xdg_positioner";
        }
        if (opcode == 2) {
            if (new_id)
                *new_id = trace_wayland_load_u32(payload);
            if (arg_object && payload_len >= 8)
                *arg_object = trace_wayland_load_u32(payload + 4);
            return "xdg_surface";
        }
    } else if (strcmp(class_name, "xdg_surface") == 0) {
        if (opcode == 1) {
            if (new_id)
                *new_id = trace_wayland_load_u32(payload);
            return "xdg_toplevel";
        }
        if (opcode == 2) {
            if (new_id)
                *new_id = trace_wayland_load_u32(payload);
            return "xdg_popup";
        }
    }
    return NULL;
}

static void trace_wayland_wire_bytes(const char *phase, int fd, size_t requested,
                                     ssize_t ret, int saved_errno, int flags,
                                     const unsigned char *bytes, size_t len,
                                     int truncated, void *caller)
{
    size_t off = 0;
    int decoded = 0;

    if (!trace_wayland_enabled() || !trace_fd_is_wayland(fd) ||
        ret <= 0 || bytes == NULL || len < 8)
        return;

    while (off + 8 <= len) {
        uint32_t object_id;
        uint32_t word;
        uint32_t opcode;
        uint32_t size;
        const unsigned char *payload;
        size_t payload_len;
        const char *class_name;
        const char *request_name;
        const char *new_class;
        uint32_t new_id = 0;
        uint32_t arg_object = 0;
        char dynamic_class[64];
        char callsite[1024];

        object_id = trace_wayland_load_u32(bytes + off);
        word = trace_wayland_load_u32(bytes + off + 4);
        opcode = word & 0xffffU;
        size = word >> 16;
        if (size < 8 || off + size > len)
            break;
        payload = bytes + off + 8;
        payload_len = size - 8;
        class_name = trace_wayland_object_class(fd, object_id);
        request_name = trace_wayland_known_request(class_name, opcode);
        dynamic_class[0] = '\0';
        new_class = trace_wayland_new_object_class(class_name, opcode, payload,
                                                   payload_len, &new_id,
                                                   &arg_object, dynamic_class,
                                                   sizeof(dynamic_class));
        if (!trace_wayland_wire_budget_ok(fd))
            break;
        trace_process_identity_once();
        trace_callsite_fields(callsite, sizeof(callsite), caller);
        trace_line("chromium_egl_trace phase=%s pid=%ld fd=%d requested=%zu "
                   "ret=%ld errno=%d flags=0x%x object=%u class=%s opcode=%u "
                   "request=%s size=%u offset=%zu new_id=%u new_class=%s "
                   "arg_object=%u truncated=%d %s",
                   phase, (long)getpid(), fd, requested, (long)ret,
                   ret < 0 ? saved_errno : 0, flags, object_id, class_name,
                   opcode, request_name, size, off, new_id,
                   new_class ? new_class : "none", arg_object, truncated,
                   callsite);
        if (new_id != 0 && new_class != NULL && new_class[0] != '\0')
            trace_wayland_object_set(fd, new_id, new_class);
        decoded++;
        off += size;
    }

    if (decoded == 0 && trace_wayland_wire_budget_ok(fd)) {
        char sample[256];
        char callsite[1024];
        size_t sample_len = len < 32 ? len : 32;

        sample[0] = '\0';
        off = 0;
        append_escaped_bytes(sample, sizeof(sample), &off,
                             (const char *)bytes, (ssize_t)sample_len);
        trace_process_identity_once();
        trace_callsite_fields(callsite, sizeof(callsite), caller);
        trace_line("chromium_egl_trace phase=wayland_wire_unknown pid=%ld "
                   "fd=%d requested=%zu ret=%ld errno=%d flags=0x%x "
                   "bytes=%zu truncated=%d %s sample=\"%s\"",
                   (long)getpid(), fd, requested, (long)ret,
                   ret < 0 ? saved_errno : 0, flags, len, truncated,
                   callsite, sample);
    }
}

static size_t trace_iov_copy_bytes(unsigned char *dst, size_t dst_size,
                                   const struct iovec *iov, size_t iovlen,
                                   ssize_t ret, int *truncated)
{
    size_t copied = 0;
    size_t remaining;

    if (truncated)
        *truncated = 0;
    if (!dst || dst_size == 0 || !iov || iovlen == 0 || ret <= 0)
        return 0;
    remaining = (size_t)ret;
    for (size_t i = 0; i < iovlen && i < 1024 && remaining > 0; i++) {
        size_t chunk = iov[i].iov_len;

        if (chunk > remaining)
            chunk = remaining;
        if (chunk > dst_size - copied) {
            chunk = dst_size - copied;
            if (truncated)
                *truncated = 1;
        }
        if (chunk > 0 && iov[i].iov_base != NULL)
            memcpy(dst + copied, iov[i].iov_base, chunk);
        copied += chunk;
        remaining -= iov[i].iov_len > remaining ? remaining : iov[i].iov_len;
        if (copied == dst_size)
            break;
    }
    if (remaining > 0 && truncated)
        *truncated = 1;
    return copied;
}

static void trace_wayland_buffer_event(const char *phase, int fd,
                                       size_t requested, ssize_t ret,
                                       int saved_errno, int flags,
                                       const void *buf, void *caller)
{
    size_t len;
    int truncated = 0;

    if (!trace_wayland_enabled() || !trace_fd_is_wayland(fd) ||
        ret <= 0 || buf == NULL)
        return;
    len = (size_t)ret;
    if (len > 4096) {
        len = 4096;
        truncated = 1;
    }
    trace_wayland_wire_bytes(phase, fd, requested, ret, saved_errno, flags,
                             (const unsigned char *)buf, len, truncated,
                             caller);
}

static void trace_wayland_iov_event(const char *phase, int fd,
                                    const struct iovec *iov, size_t iovlen,
                                    size_t requested, ssize_t ret,
                                    int saved_errno, int flags, void *caller)
{
    unsigned char bytes[4096];
    size_t copied;
    int truncated = 0;

    if (!trace_wayland_enabled() || !trace_fd_is_wayland(fd) || ret <= 0)
        return;
    copied = trace_iov_copy_bytes(bytes, sizeof(bytes), iov, iovlen, ret,
                                  &truncated);
    trace_wayland_wire_bytes(phase, fd, requested, ret, saved_errno, flags,
                             bytes, copied, truncated, caller);
}

static int trace_gtk_budget_ok(void)
{
    int old;

    if (!trace_gtk_enabled())
        return 0;
    old = __atomic_fetch_add(&trace_gtk_event_log_count, 1,
                             __ATOMIC_RELAXED);
    if (old < 1024)
        return 1;
    if (old == 1024) {
        trace_line("chromium_egl_trace phase=gtk_drop pid=%ld reason=budget",
                   (long)getpid());
    }
    return 0;
}

static int trace_name_in_list(const char *name, const char *const *names,
                              size_t count)
{
    if (name == NULL)
        return 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(name, names[i]) == 0)
            return 1;
    }
    return 0;
}

static int trace_gtk_symbol_is_interesting(const char *symbol)
{
    static const char *const names[] = {
        "gtk_widget_show",
        "gtk_widget_show_all",
        "gtk_widget_show_now",
        "gtk_widget_realize",
        "gtk_widget_map",
        "gtk_widget_set_visible",
        "gtk_window_present",
        "gtk_window_present_with_time",
        "gdk_window_show",
        "gdk_window_show_unraised",
        "gdk_window_ensure_native",
        "gdk_wayland_window_get_wl_surface",
        "gdk_window_set_title",
    };

    return trace_name_in_list(symbol, names, sizeof(names) / sizeof(names[0]));
}

static int trace_wl_egl_symbol_is_interesting(const char *symbol)
{
    static const char *const names[] = {
        "wl_egl_window_create",
        "wl_egl_window_destroy",
        "wl_egl_window_resize",
    };

    return trace_name_in_list(symbol, names, sizeof(names) / sizeof(names[0]));
}

static int trace_gbm_symbol_is_interesting(const char *symbol)
{
    static const char *const names[] = {
        "gbm_bo_create",
        "gbm_bo_create_with_modifiers",
        "gbm_bo_create_with_modifiers2",
        "gbm_bo_import",
        "gbm_bo_destroy",
    };

    return trace_name_in_list(symbol, names, sizeof(names) / sizeof(names[0]));
}

static int trace_dynamic_symbol_interesting(const char *symbol)
{
    return trace_gtk_symbol_is_interesting(symbol) ||
           trace_wl_egl_symbol_is_interesting(symbol) ||
           trace_gbm_symbol_is_interesting(symbol) ||
           trace_egl_symbol_is_interesting(symbol) ||
           strcmp(symbol ? symbol : "", "wl_proxy_marshal_flags") == 0 ||
           strcmp(symbol ? symbol : "", "wl_proxy_marshal_array_flags") == 0 ||
           strcmp(symbol ? symbol : "", "wl_proxy_marshal") == 0 ||
           strcmp(symbol ? symbol : "", "wl_proxy_marshal_constructor") == 0 ||
           strcmp(symbol ? symbol : "",
                  "wl_proxy_marshal_constructor_versioned") == 0 ||
           strcmp(symbol ? symbol : "", "wl_proxy_marshal_array") == 0 ||
           strcmp(symbol ? symbol : "",
                  "wl_proxy_marshal_array_constructor") == 0 ||
           strcmp(symbol ? symbol : "",
                  "wl_proxy_marshal_array_constructor_versioned") == 0;
}

static int trace_dynamic_symbol_trace_enabled(const char *symbol)
{
    if (trace_gtk_symbol_is_interesting(symbol))
        return trace_gtk_enabled();
    if (trace_wl_egl_symbol_is_interesting(symbol) ||
        trace_gbm_symbol_is_interesting(symbol))
        return trace_enabled();
    if (strcmp(symbol ? symbol : "", "wl_proxy_marshal_flags") == 0 ||
        strcmp(symbol ? symbol : "", "wl_proxy_marshal_array_flags") == 0 ||
        strcmp(symbol ? symbol : "", "wl_proxy_marshal") == 0 ||
        strcmp(symbol ? symbol : "", "wl_proxy_marshal_constructor") == 0 ||
        strcmp(symbol ? symbol : "",
               "wl_proxy_marshal_constructor_versioned") == 0 ||
        strcmp(symbol ? symbol : "", "wl_proxy_marshal_array") == 0 ||
        strcmp(symbol ? symbol : "",
               "wl_proxy_marshal_array_constructor") == 0 ||
        strcmp(symbol ? symbol : "",
               "wl_proxy_marshal_array_constructor_versioned") == 0)
        return trace_wayland_enabled();
    if (trace_egl_symbol_is_interesting(symbol))
        return trace_enabled();
    return 0;
}

static const char *trace_gtype_name(void *object)
{
    const char *name;

    if (object == NULL)
        return "null";
    if (real_g_type_name_from_instance == NULL) {
        real_g_type_name_from_instance =
            (g_type_name_from_instance_fn_t)
                trace_lookup_next("g_type_name_from_instance");
    }
    if (real_g_type_name_from_instance == NULL)
        return "unknown";
    name = real_g_type_name_from_instance(object);
    return name ? name : "unknown";
}

static int trace_gtk_bool_getter(gtk_widget_get_bool_fn_t *fn,
                                 const char *symbol, void *widget)
{
    if (widget == NULL)
        return -1;
    if (*fn == NULL)
        *fn = (gtk_widget_get_bool_fn_t)trace_lookup_next(symbol);
    if (*fn == NULL)
        return -1;
    return (*fn)(widget) ? 1 : 0;
}

static void *trace_gtk_widget_window(void *widget)
{
    if (widget == NULL)
        return NULL;
    if (real_gtk_widget_get_window == NULL) {
        real_gtk_widget_get_window =
            (gtk_widget_get_window_fn_t)
                trace_lookup_next("gtk_widget_get_window");
    }
    if (real_gtk_widget_get_window == NULL)
        return NULL;
    return real_gtk_widget_get_window(widget);
}

static void trace_gtk_widget_event(const char *phase, void *widget,
                                   int arg_present, int arg_value,
                                   void *caller)
{
    char callsite[1024];
    int visible;
    int realized;
    int mapped;
    void *window;

    if (!trace_gtk_budget_ok())
        return;
    visible = trace_gtk_bool_getter(&real_gtk_widget_get_visible,
                                    "gtk_widget_get_visible", widget);
    realized = trace_gtk_bool_getter(&real_gtk_widget_get_realized,
                                     "gtk_widget_get_realized", widget);
    mapped = trace_gtk_bool_getter(&real_gtk_widget_get_mapped,
                                   "gtk_widget_get_mapped", widget);
    window = trace_gtk_widget_window(widget);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_line("chromium_egl_trace phase=%s pid=%ld object=%p type=%s "
               "visible=%d realized=%d mapped=%d gdk_window=%p arg_present=%d "
               "arg_value=%d %s",
               phase, (long)getpid(), widget, trace_gtype_name(widget),
               visible, realized, mapped, window, arg_present, arg_value,
               callsite);
}

static const char *trace_gtk_window_title(void *window)
{
    if (window == NULL)
        return NULL;
    if (real_gtk_window_get_title == NULL) {
        real_gtk_window_get_title =
            (gtk_window_get_title_fn_t)
                trace_lookup_next("gtk_window_get_title");
    }
    if (real_gtk_window_get_title == NULL)
        return NULL;
    return real_gtk_window_get_title(window);
}

static void trace_gtk_window_event(const char *phase, void *window,
                                   int timestamp_present,
                                   uint32_t timestamp, void *caller)
{
    char callsite[1024];
    char title[256];
    const char *raw_title;

    if (!trace_gtk_budget_ok())
        return;
    raw_title = trace_gtk_window_title(window);
    trace_escape_text(title, sizeof(title), raw_title);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_line("chromium_egl_trace phase=%s pid=%ld object=%p type=%s "
               "title=\"%s\" timestamp_present=%d timestamp=%u %s",
               phase, (long)getpid(), window, trace_gtype_name(window), title,
               timestamp_present, timestamp, callsite);
}

static int trace_gdk_int_getter(gdk_window_get_int_fn_t *fn,
                                const char *symbol, void *window)
{
    if (window == NULL)
        return -1;
    if (*fn == NULL)
        *fn = (gdk_window_get_int_fn_t)trace_lookup_next(symbol);
    if (*fn == NULL)
        return -1;
    return (*fn)(window);
}

static void trace_gdk_window_event(const char *phase, void *window,
                                   int ret_present, int ret, void *ret_ptr,
                                   int arg_present, const char *arg,
                                   void *caller)
{
    char callsite[1024];
    char escaped_arg[256];
    int visible;
    int state;
    int width;
    int height;

    if (!trace_gtk_budget_ok())
        return;
    visible = trace_gdk_int_getter(&real_gdk_window_is_visible,
                                   "gdk_window_is_visible", window);
    state = trace_gdk_int_getter(&real_gdk_window_get_state,
                                 "gdk_window_get_state", window);
    width = trace_gdk_int_getter(&real_gdk_window_get_width,
                                 "gdk_window_get_width", window);
    height = trace_gdk_int_getter(&real_gdk_window_get_height,
                                  "gdk_window_get_height", window);
    trace_escape_text(escaped_arg, sizeof(escaped_arg), arg);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_line("chromium_egl_trace phase=%s pid=%ld object=%p type=%s "
               "visible=%d state=0x%x width=%d height=%d ret_present=%d "
               "ret=%d ret_ptr=%p arg_present=%d arg=\"%s\" %s",
               phase, (long)getpid(), window, trace_gtype_name(window),
               visible, state, width, height, ret_present, ret, ret_ptr,
               arg_present, escaped_arg, callsite);
}

static void trace_ipc_buffer_event(const char *phase, int fd, size_t requested,
                                   ssize_t ret, int saved_errno, int flags,
                                   const void *buf, void *caller)
{
    char sample[256];
    char callsite[1024];

    if (!trace_enabled() || !trace_fd_is_socket(fd) || !trace_ipc_budget_ok(fd))
        return;
    trace_process_identity_once();
    trace_ipc_sample(sample, sizeof(sample), buf, ret > 0 ? ret : 0);
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_line("chromium_egl_trace phase=%s pid=%ld fd=%d requested=%zu "
               "ret=%ld errno=%d flags=0x%x %s sample=\"%s\"",
               phase, (long)getpid(), fd, requested, (long)ret,
               ret < 0 ? saved_errno : 0, flags, callsite, sample);
}

static void trace_ipc_msg_event(const char *phase, int fd,
                                const struct msghdr *msg, ssize_t ret,
                                int saved_errno, int flags, void *caller)
{
    char sample[256];
    char cmsg_summary[2048];
    char mojom_summary[2048];
    char callsite[1024];
    size_t iovlen = msg ? msg->msg_iovlen : 0;
    size_t iov_total = msg ? trace_iov_total(msg->msg_iov, iovlen) : 0;
    size_t controllen = msg ? msg->msg_controllen : 0;
    int msg_flags = msg ? msg->msg_flags : 0;
    int mojom_count = 0;
    int cmsg_count = 0;
    int scm_rights_count = 0;
    int scm_rights_fd_count = 0;
    int scm_credentials_count = 0;
    int cmsg_other_count = 0;

    if (!trace_enabled() || !trace_fd_is_socket(fd) || !trace_ipc_budget_ok(fd))
        return;
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_iov_sample(sample, sizeof(sample), msg ? msg->msg_iov : NULL,
                     iovlen, ret > 0 ? ret : 0);
    mojom_count = trace_iov_mojom_interfaces(mojom_summary,
                                             sizeof(mojom_summary),
                                             msg ? msg->msg_iov : NULL,
                                             iovlen, ret > 0 ? ret : 0);
    trace_cmsg_summary(msg, cmsg_summary, sizeof(cmsg_summary), &cmsg_count,
                       &scm_rights_count, &scm_rights_fd_count,
                       &scm_credentials_count, &cmsg_other_count);
    trace_line("chromium_egl_trace phase=%s pid=%ld fd=%d iovlen=%zu "
               "iov_total=%zu controllen=%zu msg_flags=0x%x flags=0x%x "
               "ret=%ld errno=%d cmsg_count=%d scm_rights=%d "
               "scm_rights_fds=%d scm_credentials=%d cmsg_other=%d "
               "cmsg_truncated=%d mojom_count=%d mojom=\"%s\" "
               "%s cmsg=\"%s\" sample=\"%s\"",
               phase, (long)getpid(), fd, iovlen, iov_total, controllen,
               msg_flags, flags, (long)ret, ret < 0 ? saved_errno : 0,
               cmsg_count, scm_rights_count, scm_rights_fd_count,
               scm_credentials_count, cmsg_other_count,
               (msg_flags & MSG_CTRUNC) ? 1 : 0, mojom_count, mojom_summary,
               callsite, cmsg_summary, sample);
}

static void trace_epoll_events_summary(char *dst, size_t dst_size,
                                       const struct epoll_event *events,
                                       int ret)
{
    size_t off = 0;

    if (dst_size == 0)
        return;
    dst[0] = '\0';
    if (!events || ret <= 0)
        return;
    for (int i = 0; i < ret && i < 8; i++) {
        trace_appendf(dst, dst_size, &off, "%s%d:events=0x%x,data=0x%llx",
                      i ? ";" : "", i, events[i].events,
                      (unsigned long long)events[i].data.u64);
    }
    if (ret > 8)
        trace_appendf(dst, dst_size, &off, ";...");
}

static void trace_epoll_wait_event(const char *phase, int epfd,
                                   struct epoll_event *events, int maxevents,
                                   int timeout, int ret, int saved_errno,
                                   void *caller)
{
    char event_summary[1024];
    char callsite[1024];

    if (!trace_epoll_budget_ok(epfd, ret > 0))
        return;
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_epoll_events_summary(event_summary, sizeof(event_summary), events,
                               ret);
    trace_line("chromium_egl_trace phase=%s pid=%ld epfd=%d maxevents=%d "
               "timeout=%d ret=%d errno=%d %s events=\"%s\"",
               phase, (long)getpid(), epfd, maxevents, timeout, ret,
               ret < 0 ? saved_errno : 0, callsite, event_summary);
}

static void trace_epoll_ctl_event(int epfd, int op, int fd,
                                  const struct epoll_event *event, int ret,
                                  int saved_errno, void *caller)
{
    char callsite[1024];
    unsigned int events = event ? event->events : 0;
    unsigned long long data = event ? (unsigned long long)event->data.u64 : 0;

    if (!trace_epoll_budget_ok(epfd, 1))
        return;
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite), caller);
    trace_line("chromium_egl_trace phase=epoll_ctl pid=%ld epfd=%d op=%d "
               "fd=%d event_events=0x%x event_data=0x%llx ret=%d errno=%d %s",
               (long)getpid(), epfd, op, fd, events, data, ret,
               ret < 0 ? saved_errno : 0, callsite);
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

static uint32_t trace_hash_bytes(const char *buf, ssize_t len)
{
    uint32_t h = 2166136261u;

    if (!buf || len <= 0)
        return h;
    for (ssize_t i = 0; i < len; i++) {
        h ^= (unsigned char)buf[i];
        h *= 16777619u;
    }
    return h;
}

static int trace_bytes_contain(const char *buf, ssize_t len, const char *needle)
{
    size_t needle_len;

    if (!buf || len <= 0 || !needle)
        return 0;
    needle_len = strlen(needle);
    if (needle_len == 0 || (size_t)len < needle_len)
        return 0;
    return memmem(buf, (size_t)len, needle, needle_len) != NULL;
}

static ssize_t trace_read_memory_title(char *buf, size_t size)
{
    const char *start = program_invocation_name;
    const char *limit = NULL;
    size_t max;
    size_t len;

    if (!buf || size == 0 || !start)
        return -1;

    max = size - 1;
    if (environ != NULL) {
        for (char **ep = environ; *ep != NULL; ep++) {
            const char *entry = *ep;
            const char *end;
            size_t entry_len;

            if (entry == NULL || entry < start)
                continue;
            entry_len = strnlen(entry, 4096);
            end = entry + entry_len + 1;
            if (end > limit)
                limit = end;
        }
    }
    if (limit != NULL && limit > start && (size_t)(limit - start) < max)
        max = (size_t)(limit - start);

    len = strnlen(start, max);
    if (len < max)
        len++;
    memcpy(buf, start, len);
    if (len < size)
        buf[len] = '\0';
    return (ssize_t)len;
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
        "CHROMIUM_EGL_TRACE_PROC=",
        "CHROMIUM_EGL_TRACE_CALLSITE=",
        "CHROMIUM_EGL_TRACE_WAYLAND=",
        "CHROMIUM_EGL_TRACE_GTK=",
        "CHROMIUM_EGL_TRACE_DLSYM_WRAP=",
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

    n = trace_read_file_raw("/proc/self/environ", raw, sizeof(raw));
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

    if (argv0_len > 0) {
        static const char type_needle[] = " --type=";
        const char *argv0_end = cmdline + argv0_len;
        const char *hit = memmem(cmdline, (size_t)argv0_len, type_needle,
                                 sizeof(type_needle) - 1);

        if (hit) {
            const char *value = hit + sizeof(type_needle) - 1;
            const char *value_end = value;
            size_t role_len;

            while (value_end < argv0_end && *value_end != ' ' &&
                   *value_end != '\t') {
                value_end++;
            }
            role_len = (size_t)(value_end - value);
            if (role_len > 0) {
                if (role_len >= role_size)
                    role_len = role_size - 1;
                memcpy(role, value, role_len);
                role[role_len] = '\0';
                return;
            }
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

static void trace_process_title_snapshot(pid_t pid, const char *proc_cmdline,
                                         ssize_t proc_len,
                                         const char *proc_role, int force)
{
    char title[4096];
    char title_escaped[2048];
    char proc_escaped[2048];
    char title_role[64];
    size_t title_off = 0;
    size_t proc_off = 0;
    ssize_t title_len;
    uint32_t proc_hash;
    uint32_t title_hash;
    int proc_changed;
    int title_changed;

    if (!trace_proc_enabled())
        return;

    title_len = trace_read_memory_title(title, sizeof(title));
    proc_hash = trace_hash_bytes(proc_cmdline, proc_len);
    title_hash = trace_hash_bytes(title_len > 0 ? title : NULL, title_len);
    proc_changed = proc_len != trace_identity_last_proc_len ||
                   proc_hash != trace_identity_last_proc_hash;
    title_changed = title_len != trace_identity_last_title_len ||
                    title_hash != trace_identity_last_title_hash;

    trace_identity_last_proc_len = proc_len;
    trace_identity_last_proc_hash = proc_hash;
    trace_identity_last_title_len = title_len;
    trace_identity_last_title_hash = title_hash;

    if (!force && !proc_changed && !title_changed)
        return;
    if (!force && trace_identity_title_log_count >= 64)
        return;
    trace_identity_title_log_count++;

    title_role[0] = '\0';
    derive_cmdline_role(title_len > 0 ? title : NULL, title_len,
                        title_role, sizeof(title_role));
    title_escaped[0] = '\0';
    proc_escaped[0] = '\0';
    append_escaped_bytes(title_escaped, sizeof(title_escaped), &title_off,
                         title_len > 0 ? title : NULL, title_len);
    append_escaped_bytes(proc_escaped, sizeof(proc_escaped), &proc_off,
                         proc_len > 0 ? proc_cmdline : NULL, proc_len);

    trace_line("chromium_egl_trace phase=process_title pid=%ld ppid=%ld "
               "program=%s proc_role=%s title_role=%s proc_bytes=%ld "
               "title_bytes=%ld proc_changed=%d title_changed=%d "
               "proc_hash=0x%08x title_hash=0x%08x proc_has_renderer=%d "
               "title_has_renderer=%d proc_has_type_renderer=%d "
               "title_has_type_renderer=%d proc_argv=\"%s\" "
               "mem_title=\"%s\"",
               (long)pid, (long)getppid(),
               program_invocation_short_name ? program_invocation_short_name :
               "(null)",
               proc_role && proc_role[0] ? proc_role : "unknown",
               title_role[0] ? title_role : "unknown",
               (long)proc_len, (long)title_len, proc_changed, title_changed,
               proc_hash, title_hash,
               trace_bytes_contain(proc_cmdline, proc_len, "renderer"),
               trace_bytes_contain(title_len > 0 ? title : NULL, title_len,
                                   "renderer"),
               trace_bytes_contain(proc_cmdline, proc_len, "--type=renderer"),
               trace_bytes_contain(title_len > 0 ? title : NULL, title_len,
                                   "--type=renderer"),
               proc_escaped, title_escaped);
}

static void trace_process_identity_once(void)
{
    char raw[4096];
    char escaped[6144];
    char role[64];
    size_t escaped_off = 0;
    ssize_t n = -1;
    pid_t pid;
    pid_t old_pid;
    int first_for_pid = 0;

    if (!trace_enabled())
        return;

    pid = getpid();
    old_pid = __atomic_load_n(&trace_identity_pid, __ATOMIC_RELAXED);
    if (old_pid != pid) {
        __atomic_store_n(&trace_identity_pid, pid, __ATOMIC_RELAXED);
        trace_identity_last_proc_len = -2;
        trace_identity_last_title_len = -2;
        trace_identity_last_proc_hash = 0;
        trace_identity_last_title_hash = 0;
        trace_identity_title_log_count = 0;
        first_for_pid = 1;
    } else if (!trace_proc_enabled()) {
        return;
    }

    role[0] = '\0';
    n = trace_read_file_raw("/proc/self/cmdline", raw, sizeof(raw));
    escaped[0] = '\0';
    append_escaped_bytes(escaped, sizeof(escaped), &escaped_off,
                         n > 0 ? raw : NULL, n);
    derive_cmdline_role(n > 0 ? raw : NULL, n, role, sizeof(role));
    if (first_for_pid) {
        trace_line("chromium_egl_trace phase=process pid=%ld ppid=%ld "
                   "program=%s role=%s trace_env=%d log_env=%d "
                   "cmdline_bytes=%ld cmdline_truncated=%d "
                   "use_gl=%d use_angle=%d disable_gpu_early_init=%d "
                   "argv=\"%s\"",
                   (long)pid, (long)getppid(),
                   program_invocation_short_name ?
                   program_invocation_short_name : "(null)",
                   role[0] ? role : "unknown",
                   getenv("CHROMIUM_EGL_TRACE") ? 1 : 0,
                   getenv("CHROMIUM_EGL_TRACE_LOG") ? 1 : 0, (long)n,
                   n == (ssize_t)sizeof(raw) ? 1 : 0,
                   cmdline_has_arg_prefix(n > 0 ? raw : NULL, n, "--use-gl="),
                   cmdline_has_arg_prefix(n > 0 ? raw : NULL, n,
                                          "--use-angle="),
                   cmdline_has_arg_prefix(n > 0 ? raw : NULL, n,
                                          "--disable-gpu-early-init"),
                   escaped);
        trace_process_env(pid, role[0] ? role : "unknown");
    }
    trace_process_title_snapshot(pid, n > 0 ? raw : NULL, n,
                                 role[0] ? role : "unknown",
                                 first_for_pid);
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
    int proc_kind = trace_proc_path_kind_id(path);

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    saved_errno = errno;
    trace_fd_mark(fd, path);
    if (fd >= 0)
        trace_proc_fd_mark(fd, path, proc_kind);
    trace_proc_open_event("proc_open", AT_FDCWD, path, flags, fd,
                          saved_errno, proc_kind);
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
    int proc_kind = trace_proc_path_kind_id(path);

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    fd = (int)syscall(SYS_openat, dirfd, path, flags, mode);
    saved_errno = errno;
    trace_fd_mark(fd, path);
    if (fd >= 0)
        trace_proc_fd_mark(fd, path, proc_kind);
    trace_proc_open_event("proc_openat", dirfd, path, flags, fd,
                          saved_errno, proc_kind);
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

#ifndef DRM_IOCTL_TYPE
#define DRM_IOCTL_TYPE 'd'
#endif

#ifndef DRM_COMMAND_BASE
#define DRM_COMMAND_BASE 0x40
#endif

static const char *trace_ioctl_request_name(unsigned long request)
{
    unsigned int type = _IOC_TYPE(request);
    unsigned int nr = _IOC_NR(request);

    if (type != DRM_IOCTL_TYPE)
        return "non_drm";

    switch (nr) {
    case 0x00:
        return "DRM_IOCTL_VERSION";
    case 0x0c:
        return "DRM_IOCTL_GET_CAP";
    case 0x2d:
        return "DRM_IOCTL_PRIME_HANDLE_TO_FD";
    case 0x2e:
        return "DRM_IOCTL_PRIME_FD_TO_HANDLE";
    case 0x3a:
        return "DRM_IOCTL_WAIT_VBLANK";
    case DRM_COMMAND_BASE + 0x01:
        return "DRM_IOCTL_VIRTGPU_MAP";
    case DRM_COMMAND_BASE + 0x02:
        return "DRM_IOCTL_VIRTGPU_EXECBUFFER";
    case DRM_COMMAND_BASE + 0x03:
        return "DRM_IOCTL_VIRTGPU_GETPARAM";
    case DRM_COMMAND_BASE + 0x04:
        return "DRM_IOCTL_VIRTGPU_RESOURCE_CREATE";
    case DRM_COMMAND_BASE + 0x05:
        return "DRM_IOCTL_VIRTGPU_RESOURCE_INFO";
    case DRM_COMMAND_BASE + 0x06:
        return "DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST";
    case DRM_COMMAND_BASE + 0x07:
        return "DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST";
    case DRM_COMMAND_BASE + 0x08:
        return "DRM_IOCTL_VIRTGPU_WAIT";
    case DRM_COMMAND_BASE + 0x09:
        return "DRM_IOCTL_VIRTGPU_GET_CAPS";
    case DRM_COMMAND_BASE + 0x0a:
        return "DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB";
    case DRM_COMMAND_BASE + 0x0b:
        return "DRM_IOCTL_VIRTGPU_CONTEXT_INIT";
    case 0xbc:
        return "DRM_IOCTL_MODE_ATOMIC";
    case 0xbf:
        return "DRM_IOCTL_SYNCOBJ_CREATE";
    case 0xc0:
        return "DRM_IOCTL_SYNCOBJ_DESTROY";
    case 0xc1:
        return "DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD";
    case 0xc2:
        return "DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE";
    case 0xc3:
        return "DRM_IOCTL_SYNCOBJ_WAIT";
    case 0xc4:
        return "DRM_IOCTL_SYNCOBJ_RESET";
    case 0xc5:
        return "DRM_IOCTL_SYNCOBJ_SIGNAL";
    case 0xca:
        return "DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT";
    case 0xcc:
        return "DRM_IOCTL_SYNCOBJ_TRANSFER";
    case 0xcf:
        return "DRM_IOCTL_SYNCOBJ_EVENTFD";
    default:
        return "DRM_IOCTL_UNKNOWN";
    }
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
        char callsite[1024];

        trace_callsite_fields(callsite, sizeof(callsite),
                              __builtin_return_address(0));
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=ioctl pid=%ld fd=%d "
                   "request=0x%lx request_name=%s ret=%d errno=%d "
                   "elapsed_us=%lu %s",
                   (long)getpid(), fd, request,
                   trace_ioctl_request_name(request), ret,
                   ret < 0 ? saved_errno : 0, (unsigned long)elapsed_us,
                   callsite);
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

static int trace_sockaddr_un_path(const struct sockaddr *addr, socklen_t len,
                                  char *path, size_t path_size)
{
    const struct sockaddr_un *un;
    size_t path_off = offsetof(struct sockaddr_un, sun_path);
    size_t raw_len;
    size_t copy_len;

    if (path_size > 0)
        path[0] = '\0';
    if (addr == NULL || len <= path_off || addr->sa_family != AF_UNIX)
        return 0;
    un = (const struct sockaddr_un *)addr;
    raw_len = (size_t)len - path_off;
    if (raw_len == 0 || un->sun_path[0] == '\0')
        return 0;
    copy_len = strnlen(un->sun_path, raw_len);
    if (copy_len >= path_size)
        copy_len = path_size ? path_size - 1 : 0;
    if (path_size > 0) {
        memcpy(path, un->sun_path, copy_len);
        path[copy_len] = '\0';
    }
    return 1;
}

static int trace_path_is_wayland_socket(const char *path)
{
    const char *base;

    if (path == NULL || path[0] == '\0')
        return 0;
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strncmp(base, "wayland-", 8) == 0 ||
           strstr(path, "/wayland-") != NULL;
}

int socket(int domain, int type, int protocol)
{
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_socket, domain, type, protocol);
    saved_errno = errno;
    if (trace_wayland_enabled() && domain == AF_UNIX) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=socket pid=%ld domain=%d "
                   "type=0x%x protocol=%d ret=%d errno=%d",
                   (long)getpid(), domain, type, protocol, ret,
                   ret < 0 ? saved_errno : 0);
    }
    errno = saved_errno;
    return ret;
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    char path[256];
    char escaped[512];
    size_t off = 0;
    int is_unix;
    int is_wayland = 0;
    int ret;
    int saved_errno;

    is_unix = trace_sockaddr_un_path(addr, len, path, sizeof(path));
    if (is_unix)
        is_wayland = trace_path_is_wayland_socket(path);
    ret = (int)syscall(SYS_connect, fd, addr, len);
    saved_errno = errno;
    if (ret == 0 && is_wayland)
        trace_wayland_fd_mark(fd);
    if (trace_wayland_enabled() && is_unix) {
        escaped[0] = '\0';
        append_escaped_bytes(escaped, sizeof(escaped), &off, path,
                             (ssize_t)strlen(path));
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=connect pid=%ld fd=%d "
                   "family=AF_UNIX path=\"%s\" wayland=%d ret=%d errno=%d",
                   (long)getpid(), fd, escaped, is_wayland, ret,
                   ret < 0 ? saved_errno : 0);
    }
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
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_read, fd, buf, count);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_read", fd, count, ret, saved_errno, 0, buf,
                           caller);
    trace_proc_read_event(fd, count, ret, saved_errno, buf);
    errno = saved_errno;
    return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_write, fd, buf, count);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_write", fd, count, ret, saved_errno, 0, buf,
                           caller);
    trace_wayland_buffer_event("wayland_wire_write", fd, count, ret,
                               saved_errno, 0, buf, caller);
    errno = saved_errno;
    return ret;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;
    size_t requested = trace_iov_total(iov, iovcnt > 0 ? (size_t)iovcnt : 0);

    ret = (ssize_t)syscall(SYS_writev, fd, iov, iovcnt);
    saved_errno = errno;
    trace_wayland_iov_event("wayland_wire_writev", fd, iov,
                            iovcnt > 0 ? (size_t)iovcnt : 0, requested, ret,
                            saved_errno, 0, caller);
    errno = saved_errno;
    return ret;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_sendto, fd, buf, len, flags, NULL, 0);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_send", fd, len, ret, saved_errno, flags, buf,
                           caller);
    trace_wayland_buffer_event("wayland_wire_send", fd, len, ret,
                               saved_errno, flags, buf, caller);
    errno = saved_errno;
    return ret;
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *addr, socklen_t addrlen)
{
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_sendto, fd, buf, len, flags, addr, addrlen);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_send", fd, len, ret, saved_errno, flags, buf,
                           caller);
    trace_wayland_buffer_event("wayland_wire_sendto", fd, len, ret,
                               saved_errno, flags, buf, caller);
    errno = saved_errno;
    return ret;
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_recvfrom, fd, buf, len, flags, NULL, NULL);
    saved_errno = errno;
    trace_ipc_buffer_event("ipc_recv", fd, len, ret, saved_errno, flags, buf,
                           caller);
    errno = saved_errno;
    return ret;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_sendmsg, fd, msg, flags);
    saved_errno = errno;
    trace_ipc_msg_event("ipc_sendmsg", fd, msg, ret, saved_errno, flags,
                        caller);
    trace_wayland_iov_event("wayland_wire_sendmsg", fd,
                            msg ? msg->msg_iov : NULL,
                            msg ? msg->msg_iovlen : 0,
                            msg ? trace_iov_total(msg->msg_iov,
                                                  msg->msg_iovlen) : 0,
                            ret, saved_errno, flags, caller);
    errno = saved_errno;
    return ret;
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
    void *caller = __builtin_return_address(0);
    ssize_t ret;
    int saved_errno;

    ret = (ssize_t)syscall(SYS_recvmsg, fd, msg, flags);
    saved_errno = errno;
    trace_ipc_msg_event("ipc_recvmsg", fd, msg, ret, saved_errno, flags,
                        caller);
    errno = saved_errno;
    return ret;
}

int epoll_create1(int flags)
{
    void *caller = __builtin_return_address(0);
    char callsite[1024];
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_epoll_create1, flags);
    saved_errno = errno;
    if (trace_callsite_enabled()) {
        trace_process_identity_once();
        trace_callsite_fields(callsite, sizeof(callsite), caller);
        trace_line("chromium_egl_trace phase=epoll_create1 pid=%ld flags=0x%x "
                   "ret=%d errno=%d %s",
                   (long)getpid(), flags, ret, ret < 0 ? saved_errno : 0,
                   callsite);
    }
    errno = saved_errno;
    return ret;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    void *caller = __builtin_return_address(0);
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_epoll_ctl, epfd, op, fd, event);
    saved_errno = errno;
    trace_epoll_ctl_event(epfd, op, fd, event, ret, saved_errno, caller);
    errno = saved_errno;
    return ret;
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents,
               int timeout)
{
    void *caller = __builtin_return_address(0);
    int ret;
    int saved_errno;

#ifdef SYS_epoll_wait
    ret = (int)syscall(SYS_epoll_wait, epfd, events, maxevents, timeout);
#else
    ret = (int)syscall(SYS_epoll_pwait, epfd, events, maxevents, timeout,
                       NULL, 0);
#endif
    saved_errno = errno;
    trace_epoll_wait_event("epoll_wait", epfd, events, maxevents, timeout,
                           ret, saved_errno, caller);
    errno = saved_errno;
    return ret;
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
                int timeout, const sigset_t *sigmask)
{
    void *caller = __builtin_return_address(0);
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_epoll_pwait, epfd, events, maxevents, timeout,
                       sigmask, sigmask ? sizeof(*sigmask) : 0);
    saved_errno = errno;
    trace_epoll_wait_event("epoll_pwait", epfd, events, maxevents, timeout,
                           ret, saved_errno, caller);
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

int prctl(int option, unsigned long arg2, unsigned long arg3,
          unsigned long arg4, unsigned long arg5)
{
    char name[64];
    size_t off = 0;
    int ret;
    int saved_errno;

    ret = (int)syscall(SYS_prctl, option, arg2, arg3, arg4, arg5);
    saved_errno = errno;
    if (trace_proc_enabled() &&
        (option == PR_SET_NAME || option == PR_GET_NAME)) {
        name[0] = '\0';
        if (ret == 0 && arg2 != 0) {
            const char *raw = (const char *)arg2;
            append_escaped_bytes(name, sizeof(name), &off, raw,
                                 (ssize_t)strnlen(raw, 16));
        }
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=prctl pid=%ld option=%s "
                   "option_num=%d ret=%d errno=%d name=\"%s\"",
                   (long)getpid(),
                   option == PR_SET_NAME ? "PR_SET_NAME" : "PR_GET_NAME",
                   option, ret, ret < 0 ? saved_errno : 0, name);
    }
    errno = saved_errno;
    return ret;
}

void wl_proxy_marshal(struct wl_proxy *proxy, uint32_t opcode, ...)
{
#if defined(__GNUC__)
    void *args = __builtin_apply_args();
#endif
    void *caller = __builtin_return_address(0);

    if (real_wl_proxy_marshal == NULL) {
        real_wl_proxy_marshal =
            (wl_proxy_marshal_fn_t)trace_lookup_next("wl_proxy_marshal");
    }
    trace_wayland_marshal_event("wayland_marshal", proxy, opcode, NULL, 0, 0,
                                caller);
    if (real_wl_proxy_marshal == NULL) {
        errno = ENOSYS;
        return;
    }

#if defined(__GNUC__)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        void *ret = __builtin_apply((void (*)())real_wl_proxy_marshal,
                                    args, 512);
#pragma GCC diagnostic pop
        __builtin_return(ret);
    }
#else
    errno = ENOSYS;
    return;
#endif
}

struct wl_proxy *
wl_proxy_marshal_constructor(struct wl_proxy *proxy, uint32_t opcode,
                             const struct wl_interface *interface, ...)
{
#if defined(__GNUC__)
    void *args = __builtin_apply_args();
#endif
    void *caller = __builtin_return_address(0);

    if (real_wl_proxy_marshal_constructor == NULL) {
        real_wl_proxy_marshal_constructor =
            (wl_proxy_marshal_constructor_fn_t)
                trace_lookup_next("wl_proxy_marshal_constructor");
    }
    trace_wayland_marshal_event("wayland_marshal_constructor", proxy, opcode,
                                interface, interface ? interface->version : 0,
                                0, caller);
    if (real_wl_proxy_marshal_constructor == NULL) {
        errno = ENOSYS;
        return NULL;
    }

#if defined(__GNUC__)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        void *ret =
            __builtin_apply((void (*)())real_wl_proxy_marshal_constructor,
                            args, 512);
#pragma GCC diagnostic pop
        __builtin_return(ret);
    }
#else
    errno = ENOSYS;
    return NULL;
#endif
}

struct wl_proxy *
wl_proxy_marshal_constructor_versioned(struct wl_proxy *proxy, uint32_t opcode,
                                       const struct wl_interface *interface,
                                       uint32_t version, ...)
{
#if defined(__GNUC__)
    void *args = __builtin_apply_args();
#endif
    void *caller = __builtin_return_address(0);

    if (real_wl_proxy_marshal_constructor_versioned == NULL) {
        real_wl_proxy_marshal_constructor_versioned =
            (wl_proxy_marshal_constructor_versioned_fn_t)
                trace_lookup_next("wl_proxy_marshal_constructor_versioned");
    }
    trace_wayland_marshal_event("wayland_marshal_constructor_versioned",
                                proxy, opcode, interface, version, 0,
                                caller);
    if (real_wl_proxy_marshal_constructor_versioned == NULL) {
        errno = ENOSYS;
        return NULL;
    }

#if defined(__GNUC__)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        void *ret = __builtin_apply(
            (void (*)())real_wl_proxy_marshal_constructor_versioned,
            args, 512);
#pragma GCC diagnostic pop
        __builtin_return(ret);
    }
#else
    errno = ENOSYS;
    return NULL;
#endif
}

struct wl_proxy *
wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
                       const struct wl_interface *interface, uint32_t version,
                       uint32_t flags, ...)
{
#if defined(__GNUC__)
    void *args = __builtin_apply_args();
#endif
    void *caller = __builtin_return_address(0);

    if (real_wl_proxy_marshal_flags == NULL) {
        real_wl_proxy_marshal_flags =
            (wl_proxy_marshal_flags_fn_t)
                trace_lookup_next("wl_proxy_marshal_flags");
    }
    trace_wayland_marshal_event("wayland_marshal_flags", proxy, opcode,
                                interface, version, flags, caller);
    if (real_wl_proxy_marshal_flags == NULL) {
        errno = ENOSYS;
        return NULL;
    }

#if defined(__GNUC__)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
        void *ret = __builtin_apply((void (*)())real_wl_proxy_marshal_flags,
                                    args, 512);
#pragma GCC diagnostic pop
        __builtin_return(ret);
    }
#else
    errno = ENOSYS;
    return NULL;
#endif
}

struct wl_proxy *
wl_proxy_marshal_array_flags(struct wl_proxy *proxy, uint32_t opcode,
                             const struct wl_interface *interface,
                             uint32_t version, uint32_t flags,
                             union wl_argument *args)
{
    void *caller = __builtin_return_address(0);
    struct wl_proxy *ret;
    int saved_errno;

    if (real_wl_proxy_marshal_array_flags == NULL) {
        real_wl_proxy_marshal_array_flags =
            (wl_proxy_marshal_array_flags_fn_t)
                trace_lookup_next("wl_proxy_marshal_array_flags");
    }
    trace_wayland_marshal_event("wayland_marshal_array_flags", proxy, opcode,
                                interface, version, flags, caller);
    if (real_wl_proxy_marshal_array_flags == NULL) {
        errno = ENOSYS;
        return NULL;
    }
    ret = real_wl_proxy_marshal_array_flags(proxy, opcode, interface, version,
                                            flags, args);
    saved_errno = errno;
    errno = saved_errno;
    return ret;
}

void wl_proxy_marshal_array(struct wl_proxy *proxy, uint32_t opcode,
                            union wl_argument *args)
{
    void *caller = __builtin_return_address(0);

    if (real_wl_proxy_marshal_array == NULL) {
        real_wl_proxy_marshal_array =
            (wl_proxy_marshal_array_fn_t)
                trace_lookup_next("wl_proxy_marshal_array");
    }
    trace_wayland_marshal_event("wayland_marshal_array", proxy, opcode, NULL,
                                0, 0, caller);
    if (real_wl_proxy_marshal_array == NULL) {
        errno = ENOSYS;
        return;
    }
    real_wl_proxy_marshal_array(proxy, opcode, args);
}

struct wl_proxy *
wl_proxy_marshal_array_constructor(struct wl_proxy *proxy, uint32_t opcode,
                                   union wl_argument *args,
                                   const struct wl_interface *interface)
{
    void *caller = __builtin_return_address(0);
    struct wl_proxy *ret;
    int saved_errno;

    if (real_wl_proxy_marshal_array_constructor == NULL) {
        real_wl_proxy_marshal_array_constructor =
            (wl_proxy_marshal_array_constructor_fn_t)
                trace_lookup_next("wl_proxy_marshal_array_constructor");
    }
    trace_wayland_marshal_event("wayland_marshal_array_constructor", proxy,
                                opcode, interface,
                                interface ? interface->version : 0, 0,
                                caller);
    if (real_wl_proxy_marshal_array_constructor == NULL) {
        errno = ENOSYS;
        return NULL;
    }
    ret = real_wl_proxy_marshal_array_constructor(proxy, opcode, args,
                                                  interface);
    saved_errno = errno;
    errno = saved_errno;
    return ret;
}

struct wl_proxy *
wl_proxy_marshal_array_constructor_versioned(
    struct wl_proxy *proxy, uint32_t opcode, union wl_argument *args,
    const struct wl_interface *interface, uint32_t version)
{
    void *caller = __builtin_return_address(0);
    struct wl_proxy *ret;
    int saved_errno;

    if (real_wl_proxy_marshal_array_constructor_versioned == NULL) {
        real_wl_proxy_marshal_array_constructor_versioned =
            (wl_proxy_marshal_array_constructor_versioned_fn_t)
                trace_lookup_next(
                    "wl_proxy_marshal_array_constructor_versioned");
    }
    trace_wayland_marshal_event("wayland_marshal_array_constructor_versioned",
                                proxy, opcode, interface, version, 0,
                                caller);
    if (real_wl_proxy_marshal_array_constructor_versioned == NULL) {
        errno = ENOSYS;
        return NULL;
    }
    ret = real_wl_proxy_marshal_array_constructor_versioned(
        proxy, opcode, args, interface, version);
    saved_errno = errno;
    errno = saved_errno;
    return ret;
}

void gtk_widget_show(void *widget)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_widget_show == NULL)
        real_gtk_widget_show = (void_ptr_fn_t)trace_lookup_next("gtk_widget_show");
    trace_gtk_widget_event("gtk_widget_show_enter", widget, 0, 0, caller);
    if (real_gtk_widget_show != NULL)
        real_gtk_widget_show(widget);
    trace_gtk_widget_event("gtk_widget_show_exit", widget, 0, 0, caller);
}

void gtk_widget_show_all(void *widget)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_widget_show_all == NULL) {
        real_gtk_widget_show_all =
            (void_ptr_fn_t)trace_lookup_next("gtk_widget_show_all");
    }
    trace_gtk_widget_event("gtk_widget_show_all_enter", widget, 0, 0,
                           caller);
    if (real_gtk_widget_show_all != NULL)
        real_gtk_widget_show_all(widget);
    trace_gtk_widget_event("gtk_widget_show_all_exit", widget, 0, 0, caller);
}

void gtk_widget_show_now(void *widget)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_widget_show_now == NULL) {
        real_gtk_widget_show_now =
            (void_ptr_fn_t)trace_lookup_next("gtk_widget_show_now");
    }
    trace_gtk_widget_event("gtk_widget_show_now_enter", widget, 0, 0, caller);
    if (real_gtk_widget_show_now != NULL)
        real_gtk_widget_show_now(widget);
    trace_gtk_widget_event("gtk_widget_show_now_exit", widget, 0, 0, caller);
}

void gtk_widget_realize(void *widget)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_widget_realize == NULL) {
        real_gtk_widget_realize =
            (void_ptr_fn_t)trace_lookup_next("gtk_widget_realize");
    }
    trace_gtk_widget_event("gtk_widget_realize_enter", widget, 0, 0, caller);
    if (real_gtk_widget_realize != NULL)
        real_gtk_widget_realize(widget);
    trace_gtk_widget_event("gtk_widget_realize_exit", widget, 0, 0, caller);
}

void gtk_widget_map(void *widget)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_widget_map == NULL)
        real_gtk_widget_map = (void_ptr_fn_t)trace_lookup_next("gtk_widget_map");
    trace_gtk_widget_event("gtk_widget_map_enter", widget, 0, 0, caller);
    if (real_gtk_widget_map != NULL)
        real_gtk_widget_map(widget);
    trace_gtk_widget_event("gtk_widget_map_exit", widget, 0, 0, caller);
}

void gtk_widget_set_visible(void *widget, int visible)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_widget_set_visible == NULL) {
        real_gtk_widget_set_visible =
            (void_ptr_int_fn_t)trace_lookup_next("gtk_widget_set_visible");
    }
    trace_gtk_widget_event("gtk_widget_set_visible_enter", widget, 1, visible,
                           caller);
    if (real_gtk_widget_set_visible != NULL)
        real_gtk_widget_set_visible(widget, visible);
    trace_gtk_widget_event("gtk_widget_set_visible_exit", widget, 1, visible,
                           caller);
}

void gtk_window_present(void *window)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_window_present == NULL) {
        real_gtk_window_present =
            (void_ptr_fn_t)trace_lookup_next("gtk_window_present");
    }
    trace_gtk_window_event("gtk_window_present_enter", window, 0, 0, caller);
    if (real_gtk_window_present != NULL)
        real_gtk_window_present(window);
    trace_gtk_window_event("gtk_window_present_exit", window, 0, 0, caller);
}

void gtk_window_present_with_time(void *window, uint32_t timestamp)
{
    void *caller = __builtin_return_address(0);

    if (real_gtk_window_present_with_time == NULL) {
        real_gtk_window_present_with_time =
            (void_ptr_uint32_fn_t)
                trace_lookup_next("gtk_window_present_with_time");
    }
    trace_gtk_window_event("gtk_window_present_with_time_enter", window, 1,
                           timestamp, caller);
    if (real_gtk_window_present_with_time != NULL)
        real_gtk_window_present_with_time(window, timestamp);
    trace_gtk_window_event("gtk_window_present_with_time_exit", window, 1,
                           timestamp, caller);
}

void gdk_window_show(void *window)
{
    void *caller = __builtin_return_address(0);

    if (real_gdk_window_show == NULL)
        real_gdk_window_show = (void_ptr_fn_t)trace_lookup_next("gdk_window_show");
    trace_gdk_window_event("gdk_window_show_enter", window, 0, 0, NULL, 0,
                           NULL, caller);
    if (real_gdk_window_show != NULL)
        real_gdk_window_show(window);
    trace_gdk_window_event("gdk_window_show_exit", window, 0, 0, NULL, 0,
                           NULL, caller);
}

void gdk_window_show_unraised(void *window)
{
    void *caller = __builtin_return_address(0);

    if (real_gdk_window_show_unraised == NULL) {
        real_gdk_window_show_unraised =
            (void_ptr_fn_t)trace_lookup_next("gdk_window_show_unraised");
    }
    trace_gdk_window_event("gdk_window_show_unraised_enter", window, 0, 0,
                           NULL, 0, NULL, caller);
    if (real_gdk_window_show_unraised != NULL)
        real_gdk_window_show_unraised(window);
    trace_gdk_window_event("gdk_window_show_unraised_exit", window, 0, 0,
                           NULL, 0, NULL, caller);
}

int gdk_window_ensure_native(void *window)
{
    void *caller = __builtin_return_address(0);
    int ret = 0;

    if (real_gdk_window_ensure_native == NULL) {
        real_gdk_window_ensure_native =
            (int_ptr_fn_t)trace_lookup_next("gdk_window_ensure_native");
    }
    trace_gdk_window_event("gdk_window_ensure_native_enter", window, 0, 0,
                           NULL, 0, NULL, caller);
    if (real_gdk_window_ensure_native != NULL)
        ret = real_gdk_window_ensure_native(window);
    trace_gdk_window_event("gdk_window_ensure_native_exit", window, 1, ret,
                           NULL, 0, NULL, caller);
    return ret;
}

void *gdk_wayland_window_get_wl_surface(void *window)
{
    void *caller = __builtin_return_address(0);
    void *ret = NULL;

    if (real_gdk_wayland_window_get_wl_surface == NULL) {
        real_gdk_wayland_window_get_wl_surface =
            (ptr_ptr_fn_t)
                trace_lookup_next("gdk_wayland_window_get_wl_surface");
    }
    trace_gdk_window_event("gdk_wayland_window_get_wl_surface_enter", window,
                           0, 0, NULL, 0, NULL, caller);
    if (real_gdk_wayland_window_get_wl_surface != NULL)
        ret = real_gdk_wayland_window_get_wl_surface(window);
    trace_gdk_window_event("gdk_wayland_window_get_wl_surface_exit", window,
                           1, ret != NULL ? 1 : 0, ret, 0, NULL, caller);
    return ret;
}

void gdk_window_set_title(void *window, const char *title)
{
    void *caller = __builtin_return_address(0);

    if (real_gdk_window_set_title == NULL) {
        real_gdk_window_set_title =
            (void_ptr_const_char_fn_t)trace_lookup_next("gdk_window_set_title");
    }
    trace_gdk_window_event("gdk_window_set_title_enter", window, 0, 0, NULL,
                           1, title, caller);
    if (real_gdk_window_set_title != NULL)
        real_gdk_window_set_title(window, title);
    trace_gdk_window_event("gdk_window_set_title_exit", window, 0, 0, NULL,
                           1, title, caller);
}

static void *trace_gtk_wrapper_for_name(const char *symbol)
{
    if (symbol == NULL)
        return NULL;
    if (strcmp(symbol, "gtk_widget_show") == 0)
        return (void *)gtk_widget_show;
    if (strcmp(symbol, "gtk_widget_show_all") == 0)
        return (void *)gtk_widget_show_all;
    if (strcmp(symbol, "gtk_widget_show_now") == 0)
        return (void *)gtk_widget_show_now;
    if (strcmp(symbol, "gtk_widget_realize") == 0)
        return (void *)gtk_widget_realize;
    if (strcmp(symbol, "gtk_widget_map") == 0)
        return (void *)gtk_widget_map;
    if (strcmp(symbol, "gtk_widget_set_visible") == 0)
        return (void *)gtk_widget_set_visible;
    if (strcmp(symbol, "gtk_window_present") == 0)
        return (void *)gtk_window_present;
    if (strcmp(symbol, "gtk_window_present_with_time") == 0)
        return (void *)gtk_window_present_with_time;
    if (strcmp(symbol, "gdk_window_show") == 0)
        return (void *)gdk_window_show;
    if (strcmp(symbol, "gdk_window_show_unraised") == 0)
        return (void *)gdk_window_show_unraised;
    if (strcmp(symbol, "gdk_window_ensure_native") == 0)
        return (void *)gdk_window_ensure_native;
    if (strcmp(symbol, "gdk_wayland_window_get_wl_surface") == 0)
        return (void *)gdk_wayland_window_get_wl_surface;
    if (strcmp(symbol, "gdk_window_set_title") == 0)
        return (void *)gdk_window_set_title;
    return NULL;
}

static void *trace_dynamic_wrapper_for_name(const char *symbol)
{
    void *wrapper;

    wrapper = trace_gtk_wrapper_for_name(symbol);
    if (wrapper != NULL)
        return wrapper;
    wrapper = (void *)trace_egl_wrapper_for_name(symbol);
    if (wrapper != NULL)
        return wrapper;
    if (strcmp(symbol ? symbol : "", "wl_egl_window_create") == 0)
        return (void *)wl_egl_window_create;
    if (strcmp(symbol ? symbol : "", "wl_egl_window_destroy") == 0)
        return (void *)wl_egl_window_destroy;
    if (strcmp(symbol ? symbol : "", "wl_egl_window_resize") == 0)
        return (void *)wl_egl_window_resize;
    if (strcmp(symbol ? symbol : "", "gbm_bo_create") == 0)
        return (void *)gbm_bo_create;
    if (strcmp(symbol ? symbol : "", "gbm_bo_create_with_modifiers") == 0)
        return (void *)gbm_bo_create_with_modifiers;
    if (strcmp(symbol ? symbol : "", "gbm_bo_create_with_modifiers2") == 0)
        return (void *)gbm_bo_create_with_modifiers2;
    if (strcmp(symbol ? symbol : "", "gbm_bo_import") == 0)
        return (void *)gbm_bo_import;
    if (strcmp(symbol ? symbol : "", "gbm_bo_destroy") == 0)
        return (void *)gbm_bo_destroy;
    if (strcmp(symbol ? symbol : "", "wl_proxy_marshal") == 0)
        return (void *)wl_proxy_marshal;
    if (strcmp(symbol ? symbol : "", "wl_proxy_marshal_constructor") == 0)
        return (void *)wl_proxy_marshal_constructor;
    if (strcmp(symbol ? symbol : "",
               "wl_proxy_marshal_constructor_versioned") == 0)
        return (void *)wl_proxy_marshal_constructor_versioned;
    if (strcmp(symbol ? symbol : "", "wl_proxy_marshal_flags") == 0)
        return (void *)wl_proxy_marshal_flags;
    if (strcmp(symbol ? symbol : "", "wl_proxy_marshal_array") == 0)
        return (void *)wl_proxy_marshal_array;
    if (strcmp(symbol ? symbol : "",
               "wl_proxy_marshal_array_constructor") == 0)
        return (void *)wl_proxy_marshal_array_constructor;
    if (strcmp(symbol ? symbol : "",
               "wl_proxy_marshal_array_constructor_versioned") == 0)
        return (void *)wl_proxy_marshal_array_constructor_versioned;
    if (strcmp(symbol ? symbol : "", "wl_proxy_marshal_array_flags") == 0)
        return (void *)wl_proxy_marshal_array_flags;
    return NULL;
}

void *dlsym(void *handle, const char *symbol)
{
    dlsym_fn_t resolved = trace_real_dlsym();
    void *ret;
    void *wrapper = NULL;
    int interesting;
    int enabled;

    if (resolved == NULL)
        return NULL;
    ret = resolved(handle, symbol);
    interesting = trace_dynamic_symbol_interesting(symbol);
    enabled = trace_dynamic_symbol_trace_enabled(symbol);
    if (enabled) {
        wrapper = trace_dynamic_wrapper_for_name(symbol);
        if (trace_egl_symbol_is_interesting(symbol) &&
            (!trace_egl_dlsym_wrap_enabled() || ret == NULL))
            wrapper = NULL;
    }
    if (enabled && interesting) {
        trace_process_identity_once();
        trace_line("chromium_egl_trace phase=dlsym pid=%ld name=%s ret=%p "
                   "traced=%d", (long)getpid(),
                   symbol, ret, wrapper ? 1 : 0);
    }
    return wrapper != NULL ? wrapper : ret;
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

static void *trace_egl_lookup_proc(const char *name)
{
    void *ret;

    ret = trace_lookup_next(name);
    if (ret != NULL)
        return ret;
    if (real_egl_get_proc_address == NULL) {
        real_egl_get_proc_address =
            (egl_get_proc_address_fn_t)
                trace_lookup_next("eglGetProcAddress");
    }
    if (real_egl_get_proc_address == NULL)
        return NULL;
    return (void *)real_egl_get_proc_address(name);
}

static const char *trace_egl_image_target_name(EGLenum target)
{
    switch (target) {
#ifdef EGL_LINUX_DMA_BUF_EXT
    case EGL_LINUX_DMA_BUF_EXT:
        return "EGL_LINUX_DMA_BUF_EXT";
#endif
#ifdef EGL_GL_TEXTURE_2D
    case EGL_GL_TEXTURE_2D:
        return "EGL_GL_TEXTURE_2D";
#endif
#ifdef EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X
    case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        return "EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X";
#endif
#ifdef EGL_GL_RENDERBUFFER
    case EGL_GL_RENDERBUFFER:
        return "EGL_GL_RENDERBUFFER";
#endif
    default:
        return "unknown";
    }
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx)
{
    EGLBoolean ret;

    if (real_egl_make_current == NULL) {
        real_egl_make_current =
            (egl_make_current_fn_t)trace_lookup_next("eglMakeCurrent");
    }
    if (real_egl_make_current == NULL)
        return EGL_FALSE;
    ret = real_egl_make_current(dpy, draw, read, ctx);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglMakeCurrent pid=%ld dpy=%p "
               "draw=%p read=%p ctx=%p ret=%d",
               (long)getpid(), (void *)dpy, (void *)draw, (void *)read,
               (void *)ctx, ret);
    return ret;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    EGLBoolean ret;

    if (real_egl_swap_buffers == NULL) {
        real_egl_swap_buffers =
            (egl_swap_buffers_fn_t)trace_lookup_next("eglSwapBuffers");
    }
    if (real_egl_swap_buffers == NULL)
        return EGL_FALSE;
    ret = real_egl_swap_buffers(dpy, surface);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglSwapBuffers pid=%ld dpy=%p "
               "surface=%p ret=%d",
               (long)getpid(), (void *)dpy, (void *)surface, ret);
    return ret;
}

EGLBoolean eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface,
                                       const EGLint *rects, EGLint n_rects)
{
    EGLBoolean ret;

    if (real_egl_swap_buffers_with_damage_ext == NULL) {
        real_egl_swap_buffers_with_damage_ext =
            (egl_swap_buffers_with_damage_fn_t)
                trace_egl_lookup_proc("eglSwapBuffersWithDamageEXT");
    }
    if (real_egl_swap_buffers_with_damage_ext == NULL)
        return EGL_FALSE;
    ret = real_egl_swap_buffers_with_damage_ext(dpy, surface, rects, n_rects);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglSwapBuffersWithDamageEXT "
               "pid=%ld dpy=%p surface=%p n_rects=%d rects=%p ret=%d",
               (long)getpid(), (void *)dpy, (void *)surface, n_rects,
               (void *)rects, ret);
    return ret;
}

EGLBoolean eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surface,
                                       const EGLint *rects, EGLint n_rects)
{
    EGLBoolean ret;

    if (real_egl_swap_buffers_with_damage_khr == NULL) {
        real_egl_swap_buffers_with_damage_khr =
            (egl_swap_buffers_with_damage_fn_t)
                trace_egl_lookup_proc("eglSwapBuffersWithDamageKHR");
    }
    if (real_egl_swap_buffers_with_damage_khr == NULL)
        return EGL_FALSE;
    ret = real_egl_swap_buffers_with_damage_khr(dpy, surface, rects, n_rects);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglSwapBuffersWithDamageKHR "
               "pid=%ld dpy=%p surface=%p n_rects=%d rects=%p ret=%d",
               (long)getpid(), (void *)dpy, (void *)surface, n_rects,
               (void *)rects, ret);
    return ret;
}

EGLImage eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                        EGLClientBuffer buffer, const EGLAttrib *attrib_list)
{
    EGLImage ret;

    if (real_egl_create_image == NULL) {
        real_egl_create_image =
            (egl_create_image_fn_t)trace_egl_lookup_proc("eglCreateImage");
    }
    if (real_egl_create_image == NULL)
        return EGL_NO_IMAGE;
    ret = real_egl_create_image(dpy, ctx, target, buffer, attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglCreateImage pid=%ld dpy=%p "
               "ctx=%p target=%s(0x%x) buffer=%p attribs=%p ret=%p failed=%d",
               (long)getpid(), (void *)dpy, (void *)ctx,
               trace_egl_image_target_name(target), target, (void *)buffer,
               (void *)attrib_list, (void *)ret, ret == EGL_NO_IMAGE ? 1 : 0);
    return ret;
}

EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                              EGLClientBuffer buffer,
                              const EGLint *attrib_list)
{
    EGLImageKHR ret;

    if (real_egl_create_image_khr == NULL) {
        real_egl_create_image_khr =
            (egl_create_image_khr_fn_t)
                trace_egl_lookup_proc("eglCreateImageKHR");
    }
    if (real_egl_create_image_khr == NULL)
        return EGL_NO_IMAGE_KHR;
    ret = real_egl_create_image_khr(dpy, ctx, target, buffer, attrib_list);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglCreateImageKHR pid=%ld dpy=%p "
               "ctx=%p target=%s(0x%x) buffer=%p attribs=%p ret=%p failed=%d",
               (long)getpid(), (void *)dpy, (void *)ctx,
               trace_egl_image_target_name(target), target, (void *)buffer,
               (void *)attrib_list, (void *)ret,
               ret == EGL_NO_IMAGE_KHR ? 1 : 0);
    return ret;
}

EGLBoolean eglDestroyImage(EGLDisplay dpy, EGLImage image)
{
    EGLBoolean ret;

    if (real_egl_destroy_image == NULL) {
        real_egl_destroy_image =
            (egl_destroy_image_fn_t)trace_egl_lookup_proc("eglDestroyImage");
    }
    if (real_egl_destroy_image == NULL)
        return EGL_FALSE;
    ret = real_egl_destroy_image(dpy, image);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglDestroyImage pid=%ld dpy=%p "
               "image=%p ret=%d",
               (long)getpid(), (void *)dpy, (void *)image, ret);
    return ret;
}

EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image)
{
    EGLBoolean ret;

    if (real_egl_destroy_image_khr == NULL) {
        real_egl_destroy_image_khr =
            (egl_destroy_image_khr_fn_t)
                trace_egl_lookup_proc("eglDestroyImageKHR");
    }
    if (real_egl_destroy_image_khr == NULL)
        return EGL_FALSE;
    ret = real_egl_destroy_image_khr(dpy, image);
    trace_process_identity_once();
    trace_line("chromium_egl_trace phase=eglDestroyImageKHR pid=%ld dpy=%p "
               "image=%p ret=%d",
               (long)getpid(), (void *)dpy, (void *)image, ret);
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

void *wl_egl_window_create(void *surface, int width, int height)
{
    char callsite[1024];
    void *ret;

    if (real_wl_egl_window_create == NULL) {
        real_wl_egl_window_create =
            (wl_egl_window_create_fn_t)
                trace_lookup_next("wl_egl_window_create");
    }
    if (real_wl_egl_window_create == NULL)
        return NULL;
    ret = real_wl_egl_window_create(surface, width, height);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=wl_egl_window_create pid=%ld "
               "surface=%p width=%d height=%d ret=%p %s",
               (long)getpid(), surface, width, height, ret, callsite);
    return ret;
}

void wl_egl_window_destroy(void *egl_window)
{
    char callsite[1024];

    if (real_wl_egl_window_destroy == NULL) {
        real_wl_egl_window_destroy =
            (wl_egl_window_destroy_fn_t)
                trace_lookup_next("wl_egl_window_destroy");
    }
    if (real_wl_egl_window_destroy != NULL)
        real_wl_egl_window_destroy(egl_window);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=wl_egl_window_destroy pid=%ld "
               "window=%p %s",
               (long)getpid(), egl_window, callsite);
}

void wl_egl_window_resize(void *egl_window, int width, int height, int dx,
                          int dy)
{
    char callsite[1024];

    if (real_wl_egl_window_resize == NULL) {
        real_wl_egl_window_resize =
            (wl_egl_window_resize_fn_t)
                trace_lookup_next("wl_egl_window_resize");
    }
    if (real_wl_egl_window_resize != NULL)
        real_wl_egl_window_resize(egl_window, width, height, dx, dy);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=wl_egl_window_resize pid=%ld "
               "window=%p width=%d height=%d dx=%d dy=%d %s",
               (long)getpid(), egl_window, width, height, dx, dy, callsite);
}

void *gbm_bo_create(void *gbm, uint32_t width, uint32_t height,
                    uint32_t format, uint32_t flags)
{
    char callsite[1024];
    void *ret;

    if (real_gbm_bo_create == NULL) {
        real_gbm_bo_create =
            (gbm_bo_create_fn_t)trace_lookup_next("gbm_bo_create");
    }
    if (real_gbm_bo_create == NULL)
        return NULL;
    ret = real_gbm_bo_create(gbm, width, height, format, flags);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=gbm_bo_create pid=%ld gbm=%p "
               "width=%u height=%u format=0x%x flags=0x%x ret=%p %s",
               (long)getpid(), gbm, width, height, format, flags, ret,
               callsite);
    return ret;
}

void *gbm_bo_create_with_modifiers(void *gbm, uint32_t width, uint32_t height,
                                   uint32_t format, const uint64_t *modifiers,
                                   unsigned int count)
{
    char callsite[1024];
    void *ret;
    uint64_t first_modifier = modifiers && count > 0 ? modifiers[0] : 0;

    if (real_gbm_bo_create_with_modifiers == NULL) {
        real_gbm_bo_create_with_modifiers =
            (gbm_bo_create_with_modifiers_fn_t)
                trace_lookup_next("gbm_bo_create_with_modifiers");
    }
    if (real_gbm_bo_create_with_modifiers == NULL)
        return NULL;
    ret = real_gbm_bo_create_with_modifiers(gbm, width, height, format,
                                            modifiers, count);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=gbm_bo_create_with_modifiers "
               "pid=%ld gbm=%p width=%u height=%u format=0x%x count=%u "
               "first_modifier=0x%llx ret=%p %s",
               (long)getpid(), gbm, width, height, format, count,
               (unsigned long long)first_modifier, ret, callsite);
    return ret;
}

void *gbm_bo_create_with_modifiers2(void *gbm, uint32_t width, uint32_t height,
                                    uint32_t format,
                                    const uint64_t *modifiers,
                                    unsigned int count, uint32_t flags)
{
    char callsite[1024];
    void *ret;
    uint64_t first_modifier = modifiers && count > 0 ? modifiers[0] : 0;

    if (real_gbm_bo_create_with_modifiers2 == NULL) {
        real_gbm_bo_create_with_modifiers2 =
            (gbm_bo_create_with_modifiers2_fn_t)
                trace_lookup_next("gbm_bo_create_with_modifiers2");
    }
    if (real_gbm_bo_create_with_modifiers2 == NULL)
        return NULL;
    ret = real_gbm_bo_create_with_modifiers2(gbm, width, height, format,
                                             modifiers, count, flags);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=gbm_bo_create_with_modifiers2 "
               "pid=%ld gbm=%p width=%u height=%u format=0x%x count=%u "
               "first_modifier=0x%llx flags=0x%x ret=%p %s",
               (long)getpid(), gbm, width, height, format, count,
               (unsigned long long)first_modifier, flags, ret, callsite);
    return ret;
}

void *gbm_bo_import(void *gbm, uint32_t type, void *buffer, uint32_t flags)
{
    char callsite[1024];
    void *ret;

    if (real_gbm_bo_import == NULL) {
        real_gbm_bo_import =
            (gbm_bo_import_fn_t)trace_lookup_next("gbm_bo_import");
    }
    if (real_gbm_bo_import == NULL)
        return NULL;
    ret = real_gbm_bo_import(gbm, type, buffer, flags);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=gbm_bo_import pid=%ld gbm=%p "
               "type=0x%x buffer=%p flags=0x%x ret=%p %s",
               (long)getpid(), gbm, type, buffer, flags, ret, callsite);
    return ret;
}

void gbm_bo_destroy(void *bo)
{
    char callsite[1024];

    if (real_gbm_bo_destroy == NULL) {
        real_gbm_bo_destroy =
            (gbm_bo_destroy_fn_t)trace_lookup_next("gbm_bo_destroy");
    }
    if (real_gbm_bo_destroy != NULL)
        real_gbm_bo_destroy(bo);
    trace_process_identity_once();
    trace_callsite_fields(callsite, sizeof(callsite),
                          __builtin_return_address(0));
    trace_line("chromium_egl_trace phase=gbm_bo_destroy pid=%ld bo=%p %s",
               (long)getpid(), bo, callsite);
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
