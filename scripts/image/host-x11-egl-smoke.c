#define _GNU_SOURCE

// Tiny host-built X11 EGL/GLES2 ABI probe for the imported-GUI lane.
//
// The binary is linked on the host, but the import helper intentionally skips
// host GL/EGL runtime libraries so it resolves the guest Mesa stack. This
// proves XWayland window surfaces, EGL-on-X11 setup, GLES draw/swap, keyboard
// input, WM_DELETE, and clean teardown without dragging in a browser/toolkit.

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WIN_W 640
#define WIN_H 320
#define GLX_FPS_TARGET_NS 5000000000LL
#define GLX_FPS_MAX_FRAMES 300
#define GLX_FPS_OML_QUEUE_DEPTH 3
#define GLX_FPS_OML_QUEUE_DEPTH_MAX 8
#define GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL_MAX 64

typedef const GLubyte *(GLAPIENTRY *gl_get_string_proc_t)(GLenum name);
typedef void (GLAPIENTRY *gl_viewport_proc_t)(GLint x, GLint y,
                                              GLsizei width,
                                              GLsizei height);
typedef void (GLAPIENTRY *gl_clear_color_proc_t)(GLfloat red, GLfloat green,
                                                 GLfloat blue, GLfloat alpha);
typedef void (GLAPIENTRY *gl_clear_proc_t)(GLbitfield mask);
typedef GLenum (GLAPIENTRY *gl_get_error_proc_t)(void);
typedef void (GLAPIENTRY *gl_flush_proc_t)(void);
typedef void (GLAPIENTRY *gl_finish_proc_t)(void);

enum glx_fps_variant_kind {
    GLX_FPS_VARIANT_BASELINE,
    GLX_FPS_VARIANT_FINISH_BEFORE_SWAP,
    GLX_FPS_VARIANT_SWAP_ONLY,
    GLX_FPS_VARIANT_SWAP_INTERVAL0_SWAP_ONLY,
    GLX_FPS_VARIANT_SWAP_INTERVAL0_OML_STATE_SWAP_ONLY,
    GLX_FPS_VARIANT_OML_QUEUE3_SWAP_ONLY,
    GLX_FPS_VARIANT_OML_QUEUE_DEPTH_SWAP_ONLY,
    GLX_FPS_VARIANT_OML_QUEUE_DEPTH_FLUSH_SWAP_TIMING,
    GLX_FPS_VARIANT_OML_QUEUE_DEPTH_ISSUE_STATE_TIMING,
};

enum glx_swap_interval_api {
    GLX_SWAP_INTERVAL_API_NONE,
    GLX_SWAP_INTERVAL_API_EXT,
    GLX_SWAP_INTERVAL_API_MESA,
    GLX_SWAP_INTERVAL_API_SGI,
};

enum glx_swap_interval_status {
    GLX_SWAP_INTERVAL_STATUS_UNAVAILABLE,
    GLX_SWAP_INTERVAL_STATUS_PASS,
    GLX_SWAP_INTERVAL_STATUS_FAIL,
};

struct gl_api {
    const char *source;
    gl_get_string_proc_t get_string;
    gl_viewport_proc_t viewport;
    gl_clear_color_proc_t clear_color;
    gl_clear_proc_t clear;
    gl_get_error_proc_t get_error;
    gl_flush_proc_t flush;
    gl_finish_proc_t finish;
};

struct glx_fps_timing {
    int64_t event_total_ns;
    int64_t gl_issue_total_ns;
    int64_t gl_error_total_ns;
    int64_t gl_finish_before_swap_total_ns;
    int64_t plain_swap_issue_total_ns;
    int64_t draw_total_ns;
    int64_t swap_total_ns;
    int64_t final_xsync_ns;
    int64_t max_event_ns;
    int64_t max_gl_issue_ns;
    int64_t max_gl_error_ns;
    int64_t max_gl_finish_before_swap_ns;
    int64_t max_plain_swap_ns;
    int64_t max_draw_ns;
    int64_t max_swap_ns;
    int swap_only_skipped_draw_frames;
    int swap_interval_requested;
    enum glx_swap_interval_api swap_interval_set_api;
    enum glx_swap_interval_status swap_interval_set_status;
    int swap_interval_before;
    int swap_interval_after;
    int oml_available;
    int oml_queue_depth;
    int oml_max_pending_sbc;
    int oml_gl_flush_before_swap;
    int oml_issue_state_sample_interval;
    int oml_issue_state_samples;
    int oml_issue_state_first_sample_frame;
    int oml_issue_state_last_sample_frame;
    int oml_post_issue_sbc_completed_count;
    int64_t oml_post_issue_sbc_lag_max;
    int64_t oml_post_issue_msc_delta_total;
    int64_t oml_post_issue_msc_delta_max;
    int64_t oml_issue_state_first_sample_sbc;
    int64_t oml_issue_state_last_sample_sbc;
    int64_t oml_sbc_issued;
    int64_t oml_sbc_completed;
    int64_t oml_issue_total_ns;
    int64_t oml_gl_flush_total_ns;
    int64_t oml_swap_msc_issue_total_ns;
    int64_t oml_get_sync_before_total_ns;
    int64_t oml_get_sync_after_total_ns;
    int64_t oml_wait_total_ns;
    int64_t oml_drain_wait_total_ns;
    int64_t oml_last_ust;
    int64_t oml_last_msc;
    int64_t oml_last_sbc;
    int plain_oml_available;
    int plain_oml_samples;
    int64_t plain_oml_get_sync_before_total_ns;
    int64_t plain_oml_get_sync_after_total_ns;
    int64_t plain_oml_before_first_sbc;
    int64_t plain_oml_before_last_sbc;
    int64_t plain_oml_after_first_sbc;
    int64_t plain_oml_after_last_sbc;
    int64_t plain_oml_post_swap_sbc_delta_total;
    int64_t plain_oml_post_swap_sbc_delta_max;
    int64_t plain_oml_post_swap_msc_delta_total;
    int64_t plain_oml_post_swap_msc_delta_max;
    int64_t plain_post_swap_xsync_total_ns;
    int64_t plain_post_swap_xsync_max_ns;
};

struct glx_fps_variant {
    enum glx_fps_variant_kind kind;
    const char *name;
    int requested;
    int invalid;
    int queue_depth;
    int issue_state_sample_interval;
    char raw[64];
};

struct app {
    Display *dpy;
    int screen;
    Window win;
    Atom wm_protocols;
    Atom wm_delete;
    XVisualInfo *visual;
    Colormap colormap;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    GLXContext glx_context;
    struct gl_api gl;
    int use_glx;
    int glx_direct;
    int glx_direct_available;
    int color_index;
    int frame;
    int running;
};

static const float colors[][3] = {
    {0.05f, 0.10f, 0.80f},
    {0.82f, 0.08f, 0.08f},
    {0.05f, 0.62f, 0.18f},
};

static void
log_line(const char *line)
{
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
}

static const char *egl_error_name(EGLint err);

static const char *
safe_str(const char *s)
{
    return s ? s : "(null)";
}

static int
extension_list_has_token(const char *extensions, const char *token)
{
    size_t token_len;
    const char *p;

    if (!extensions || !token || token[0] == '\0')
        return 0;
    token_len = strlen(token);
    for (p = extensions; *p; p++) {
        if ((p == extensions || p[-1] == ' ') &&
            strncmp(p, token, token_len) == 0 &&
            (p[token_len] == '\0' || p[token_len] == ' '))
            return 1;
    }
    return 0;
}

static void
copy_log_value(char *dst, size_t dst_size, const char *src)
{
    size_t out = 0;

    if (!dst_size)
        return;
    if (!src)
        src = "(null)";
    while (*src && out + 1 < dst_size) {
        char c = *src++;

        if (c == '"' || c == '\n' || c == '\r' || c == '\t')
            c = '_';
        dst[out++] = c;
    }
    dst[out] = '\0';
}

static int64_t
monotonic_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int64_t
elapsed_ns(int64_t start_ns, int64_t end_ns)
{
    if (start_ns == 0 || end_ns <= start_ns)
        return 0;
    return end_ns - start_ns;
}

static void
add_timing_sample(int64_t *total_ns, int64_t *max_ns, int64_t sample_ns)
{
    *total_ns += sample_ns;
    if (sample_ns > *max_ns)
        *max_ns = sample_ns;
}

static double
ns_to_ms(int64_t ns)
{
    return (double)ns / 1000000.0;
}

static const GLubyte *
linked_gl_get_string(GLenum name)
{
    return glGetString(name);
}

static void
linked_gl_viewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    glViewport(x, y, width, height);
}

static void
linked_gl_clear_color(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    glClearColor(red, green, blue, alpha);
}

static void
linked_gl_clear(GLbitfield mask)
{
    glClear(mask);
}

static GLenum
linked_gl_get_error(void)
{
    return glGetError();
}

static void
linked_gl_flush(void)
{
    glFlush();
}

static void
linked_gl_finish(void)
{
    glFinish();
}

static void
set_linked_gl_api(struct app *app)
{
    app->gl.source = "linked";
    app->gl.get_string = linked_gl_get_string;
    app->gl.viewport = linked_gl_viewport;
    app->gl.clear_color = linked_gl_clear_color;
    app->gl.clear = linked_gl_clear;
    app->gl.get_error = linked_gl_get_error;
    app->gl.flush = linked_gl_flush;
    app->gl.finish = linked_gl_finish;
}

static int
gl_map_line_interesting(const char *line)
{
    static const char *needles[] = {
        "libGL",
        "libGLX",
        "libGLdispatch",
        "libEGL",
        "libGLES",
        "libglapi",
        "libgbm",
        "libdrm",
        "Mesa",
        "mesa",
        "gallium",
        "/dri/",
    };
    size_t i;

    for (i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
        if (strstr(line, needles[i]))
            return 1;
    }
    return 0;
}

static void
dump_gl_loader_maps(const char *label)
{
    char line[1024];
    FILE *fp;
    int matched = 0;
    int emitted = 0;
    const int limit = 32;

    fprintf(stderr,
            "host-x11-egl-smoke: phase=gl_loader_maps status=BEGIN label=%s\n",
            label);
    fp = fopen("/proc/self/maps", "r");
    if (!fp) {
        fprintf(stderr,
                "host-x11-egl-smoke: diag gl_loader_maps label=%s result=FAIL errno=%d %s\n",
                label, errno, strerror(errno));
        fflush(stderr);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        size_t len;

        if (!gl_map_line_interesting(line))
            continue;
        matched++;
        if (emitted >= limit)
            continue;
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        fprintf(stderr,
                "host-x11-egl-smoke: diag gl_loader_maps label=%s line=%s\n",
                label, line);
        emitted++;
    }
    fclose(fp);
    fprintf(stderr,
            "host-x11-egl-smoke: phase=gl_loader_maps status=PASS label=%s matched=%d emitted=%d truncated=%d\n",
            label, matched, emitted, matched > emitted);
    fflush(stderr);
}

static void
log_dladdr_symbol(const char *label, const void *addr)
{
    Dl_info info;

    memset(&info, 0, sizeof(info));
    fprintf(stderr,
            "host-x11-egl-smoke: phase=dladdr status=BEGIN label=%s addr=%p\n",
            label, addr);
    if (dladdr(addr, &info)) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=dladdr status=PASS label=%s addr=%p symbol=%s file=%s base=0x%" PRIxPTR " symaddr=0x%" PRIxPTR "\n",
                label, addr, safe_str(info.dli_sname),
                safe_str(info.dli_fname), (uintptr_t)info.dli_fbase,
                (uintptr_t)info.dli_saddr);
    } else {
        fprintf(stderr,
                "host-x11-egl-smoke: diag dladdr label=%s addr=%p result=FAIL reason=not_found\n",
                label, addr);
    }
    fflush(stderr);
}

static const char *
gl_error_name(GLenum err)
{
    switch (err) {
    case GL_NO_ERROR:
        return "GL_NO_ERROR";
    case GL_INVALID_ENUM:
        return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
        return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
        return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY:
        return "GL_OUT_OF_MEMORY";
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
#endif
    default:
        return "GL_UNKNOWN_ERROR";
    }
}

static const char *
gl_string_enum_name(GLenum name)
{
    switch (name) {
    case GL_VENDOR:
        return "GL_VENDOR";
    case GL_RENDERER:
        return "GL_RENDERER";
    case GL_VERSION:
        return "GL_VERSION";
    default:
        return "GL_UNKNOWN_STRING";
    }
}

static __GLXextFuncPtr
glx_get_raw_proc(const char *name)
{
#ifdef GLX_ARB_get_proc_address
    __GLXextFuncPtr raw_proc;

    raw_proc = glXGetProcAddressARB((const GLubyte *)name);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_get_proc_address name=%s result=%s ptr=%p\n",
            name, raw_proc ? "PASS" : "FAIL", (void *)raw_proc);
    fflush(stderr);
    return raw_proc;
#else
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_get_proc_address name=%s result=FAIL reason=not_available_in_headers\n",
            name);
    fflush(stderr);
    return NULL;
#endif
}

static gl_get_string_proc_t
glx_get_gl_get_string_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        gl_get_string_proc_t typed;
    } conv;

    conv.raw = glx_get_raw_proc("glGetString");
    return conv.typed;
}

static gl_viewport_proc_t
glx_get_gl_viewport_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        gl_viewport_proc_t typed;
    } conv;

    conv.raw = glx_get_raw_proc("glViewport");
    return conv.typed;
}

static gl_clear_color_proc_t
glx_get_gl_clear_color_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        gl_clear_color_proc_t typed;
    } conv;

    conv.raw = glx_get_raw_proc("glClearColor");
    return conv.typed;
}

static gl_clear_proc_t
glx_get_gl_clear_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        gl_clear_proc_t typed;
    } conv;

    conv.raw = glx_get_raw_proc("glClear");
    return conv.typed;
}

static gl_get_error_proc_t
glx_get_gl_get_error_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        gl_get_error_proc_t typed;
    } conv;

    conv.raw = glx_get_raw_proc("glGetError");
    return conv.typed;
}

static gl_flush_proc_t
glx_get_gl_flush_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        gl_flush_proc_t typed;
    } conv;

    conv.raw = glx_get_raw_proc("glFlush");
    return conv.typed;
}

static gl_finish_proc_t
glx_get_gl_finish_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        gl_finish_proc_t typed;
    } conv;

    conv.raw = glx_get_raw_proc("glFinish");
    return conv.typed;
}

struct glx_oml_api {
    PFNGLXGETSYNCVALUESOMLPROC get_sync_values;
    PFNGLXSWAPBUFFERSMSCOMLPROC swap_buffers_msc;
    PFNGLXWAITFORSBCOMLPROC wait_for_sbc;
};

static PFNGLXGETSYNCVALUESOMLPROC
glx_get_sync_values_oml_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        PFNGLXGETSYNCVALUESOMLPROC typed;
    } conv;

    conv.raw = glx_get_raw_proc("glXGetSyncValuesOML");
    return conv.typed;
}

static PFNGLXSWAPBUFFERSMSCOMLPROC
glx_get_swap_buffers_msc_oml_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        PFNGLXSWAPBUFFERSMSCOMLPROC typed;
    } conv;

    conv.raw = glx_get_raw_proc("glXSwapBuffersMscOML");
    return conv.typed;
}

static PFNGLXWAITFORSBCOMLPROC
glx_get_wait_for_sbc_oml_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        PFNGLXWAITFORSBCOMLPROC typed;
    } conv;

    conv.raw = glx_get_raw_proc("glXWaitForSbcOML");
    return conv.typed;
}

static PFNGLXSWAPINTERVALEXTPROC
glx_get_swap_interval_ext_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        PFNGLXSWAPINTERVALEXTPROC typed;
    } conv;

    conv.raw = glx_get_raw_proc("glXSwapIntervalEXT");
    return conv.typed;
}

static PFNGLXSWAPINTERVALMESAPROC
glx_get_swap_interval_mesa_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        PFNGLXSWAPINTERVALMESAPROC typed;
    } conv;

    conv.raw = glx_get_raw_proc("glXSwapIntervalMESA");
    return conv.typed;
}

static PFNGLXGETSWAPINTERVALMESAPROC
glx_get_get_swap_interval_mesa_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        PFNGLXGETSWAPINTERVALMESAPROC typed;
    } conv;

    conv.raw = glx_get_raw_proc("glXGetSwapIntervalMESA");
    return conv.typed;
}

static PFNGLXSWAPINTERVALSGIPROC
glx_get_swap_interval_sgi_proc(void)
{
    union {
        __GLXextFuncPtr raw;
        PFNGLXSWAPINTERVALSGIPROC typed;
    } conv;

    conv.raw = glx_get_raw_proc("glXSwapIntervalSGI");
    return conv.typed;
}

static int
set_glx_gl_api(struct app *app)
{
    app->gl.source = "glx-proc";
    app->gl.get_string = glx_get_gl_get_string_proc();
    app->gl.viewport = glx_get_gl_viewport_proc();
    app->gl.clear_color = glx_get_gl_clear_color_proc();
    app->gl.clear = glx_get_gl_clear_proc();
    app->gl.get_error = glx_get_gl_get_error_proc();
    app->gl.finish = glx_get_gl_finish_proc();

    if (!app->gl.get_string || !app->gl.viewport || !app->gl.clear_color ||
        !app->gl.clear || !app->gl.get_error) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_gl_dispatch status=FAIL get_string=%p viewport=%p clear_color=%p clear=%p get_error=%p finish=%p\n",
                (void *)app->gl.get_string, (void *)app->gl.viewport,
                (void *)app->gl.clear_color, (void *)app->gl.clear,
                (void *)app->gl.get_error, (void *)app->gl.finish);
        fflush(stderr);
        return -1;
    }

    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_gl_dispatch status=PASS get_string=%p viewport=%p clear_color=%p clear=%p get_error=%p finish=%p finish_required=0\n",
            (void *)app->gl.get_string, (void *)app->gl.viewport,
            (void *)app->gl.clear_color, (void *)app->gl.clear,
            (void *)app->gl.get_error, (void *)app->gl.finish);
    fflush(stderr);
    return 0;
}

static const GLubyte *
log_gl_get_string_linked(GLenum name, const char *tag)
{
    GLenum clear_err;
    GLenum after_err;
    const GLubyte *value;

    clear_err = glGetError();
    fprintf(stderr,
            "host-x11-egl-smoke: diag gl_get_error where=before_linked_glGetString name=%s error=0x%x %s\n",
            gl_string_enum_name(name), clear_err, gl_error_name(clear_err));
    value = glGetString(name);
    after_err = glGetError();
    fprintf(stderr,
            "host-x11-egl-smoke: diag gl_get_string_linked tag=%s name=%s result=%s value=%s ptr=%p before_error=0x%x before_name=%s after_error=0x%x after_name=%s\n",
            tag, gl_string_enum_name(name), value ? "PASS" : "FAIL",
            safe_str((const char *)value), (const void *)value,
            clear_err, gl_error_name(clear_err), after_err,
            gl_error_name(after_err));
    fflush(stderr);
    return value;
}

static const GLubyte *
log_gl_get_string_proc(struct app *app, GLenum name)
{
    GLenum clear_err;
    GLenum after_err;
    const GLubyte *value;
    gl_get_string_proc_t proc = app->gl.get_string;
    gl_get_error_proc_t get_error = app->gl.get_error;

    if (!proc || !get_error) {
        fprintf(stderr,
                "host-x11-egl-smoke: diag gl_get_string_proc tag=%s name=%s proc=%p get_error=%p result=FAIL reason=unavailable\n",
                safe_str(app->gl.source), gl_string_enum_name(name),
                (void *)proc, (void *)get_error);
        fflush(stderr);
        return NULL;
    }

    clear_err = get_error();
    fprintf(stderr,
            "host-x11-egl-smoke: diag gl_get_error where=before_proc_glGetString name=%s error=0x%x %s\n",
            gl_string_enum_name(name), clear_err, gl_error_name(clear_err));
    value = proc(name);
    after_err = get_error();
    fprintf(stderr,
            "host-x11-egl-smoke: diag gl_get_string_proc tag=%s name=%s result=%s proc=%p value=%s ptr=%p before_error=0x%x before_name=%s after_error=0x%x after_name=%s\n",
            safe_str(app->gl.source), gl_string_enum_name(name),
            value ? "PASS" : "FAIL",
            (void *)proc, safe_str((const char *)value),
            (const void *)value, clear_err, gl_error_name(clear_err),
            after_err, gl_error_name(after_err));
    fflush(stderr);
    return value;
}

struct gl_string_results {
    const GLubyte *vendor;
    const GLubyte *renderer;
    const GLubyte *version;
};

static void
log_gl_get_string_diagnostics(struct app *app, struct gl_string_results *results)
{
    memset(results, 0, sizeof(*results));

    results->vendor = log_gl_get_string_proc(app, GL_VENDOR);
    results->renderer = log_gl_get_string_proc(app, GL_RENDERER);
    results->version = log_gl_get_string_proc(app, GL_VERSION);

    log_gl_get_string_linked(GL_VENDOR, "linked");
    log_gl_get_string_linked(GL_RENDERER, "linked");
    log_gl_get_string_linked(GL_VERSION, "linked");
}

static int
glx_query_context_attr(Display *dpy, GLXContext ctx, int attr, int *value)
{
#ifdef GLX_VERSION_1_3
    if (!dpy || !ctx || !value)
        return -1;
    return glXQueryContext(dpy, ctx, attr, value);
#else
    (void)dpy;
    (void)ctx;
    (void)attr;
    (void)value;
    return -1;
#endif
}

static void
log_glx_query_context_attrs(Display *dpy, GLXContext ctx, int direct)
{
    int fbconfig_id = -1;
    int render_type = -1;
    int screen = -1;
    int fbconfig_rc = -1;
    int render_rc = -1;
    int screen_rc = -1;

    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_query_context_attrs status=BEGIN ctx=%p\n",
            (void *)ctx);
#ifdef GLX_FBCONFIG_ID
    fbconfig_rc = glx_query_context_attr(dpy, ctx, GLX_FBCONFIG_ID,
                                         &fbconfig_id);
#endif
#ifdef GLX_RENDER_TYPE
    render_rc = glx_query_context_attr(dpy, ctx, GLX_RENDER_TYPE,
                                       &render_type);
#endif
#ifdef GLX_SCREEN
    screen_rc = glx_query_context_attr(dpy, ctx, GLX_SCREEN, &screen);
#endif
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_query_context_attrs status=PASS direct=%d fbconfig_rc=%d fbconfig_id=0x%x render_rc=%d render_type=0x%x screen_rc=%d screen=%d\n",
            direct, fbconfig_rc, fbconfig_id, render_rc, render_type,
            screen_rc, screen);
    fflush(stderr);
}

static void
log_glx_current_bindings(Display *expected_dpy)
{
    GLXContext ctx;
    GLXDrawable draw;
    GLXDrawable read_draw = 0;
    Display *current_dpy = NULL;
    int has_read_draw = 0;
    int has_current_dpy = 0;

    ctx = glXGetCurrentContext();
    draw = glXGetCurrentDrawable();
#ifdef GLX_VERSION_1_3
    read_draw = glXGetCurrentReadDrawable();
    has_read_draw = 1;
#endif
#ifdef GLX_VERSION_1_2
    current_dpy = glXGetCurrentDisplay();
    has_current_dpy = 1;
#endif
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_current_bindings status=PASS context=%p drawable=0x%lx read_drawable=0x%lx read_drawable_available=%d display=%p display_available=%d display_matches=%d\n",
            (void *)ctx, (unsigned long)draw, (unsigned long)read_draw,
            has_read_draw, (void *)current_dpy, has_current_dpy,
            has_current_dpy && current_dpy == expected_dpy);
    fflush(stderr);
}

static void
log_start(const char *mode)
{
    fprintf(stderr,
            "host-x11-egl-smoke: start mode=%s\n"
            "host-x11-egl-smoke: phase=start status=BEGIN mode=%s\n",
            mode, mode);
    fflush(stderr);
}

static int
egl_config_attr_or_neg1(EGLDisplay dpy, EGLConfig config, EGLint attr)
{
    EGLint value = -1;

    if (dpy == EGL_NO_DISPLAY || !config)
        return -1;
    if (!eglGetConfigAttrib(dpy, config, attr, &value))
        return -1;
    return value;
}

static void
log_egl_no_display_extensions(void)
{
    const char *extensions;
    EGLint err;

    eglGetError();
    extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (extensions) {
        fprintf(stderr,
                "host-x11-egl-smoke: diag egl_no_display_extensions=%s\n",
                extensions);
    } else {
        err = eglGetError();
        fprintf(stderr,
                "host-x11-egl-smoke: diag egl_no_display_extensions=(not_reported) egl_error=0x%x %s\n",
                err, egl_error_name(err));
    }
    fflush(stderr);
}

static void
log_egl_display_strings(EGLDisplay dpy)
{
    if (dpy == EGL_NO_DISPLAY) {
        log_line("host-x11-egl-smoke: diag egl_display_strings display=NULL");
        return;
    }

    fprintf(stderr,
            "host-x11-egl-smoke: diag egl_vendor=%s egl_version=%s egl_client_apis=%s egl_extensions=%s\n",
            safe_str(eglQueryString(dpy, EGL_VENDOR)),
            safe_str(eglQueryString(dpy, EGL_VERSION)),
            safe_str(eglQueryString(dpy, EGL_CLIENT_APIS)),
            safe_str(eglQueryString(dpy, EGL_EXTENSIONS)));
    fflush(stderr);
}

static void
log_egl_config(EGLDisplay dpy, EGLConfig config)
{
    fprintf(stderr,
            "host-x11-egl-smoke: diag egl_config config_id=%d native_visual_id=0x%x renderable_type=0x%x surface_type=0x%x red_size=%d green_size=%d blue_size=%d alpha_size=%d\n",
            egl_config_attr_or_neg1(dpy, config, EGL_CONFIG_ID),
            egl_config_attr_or_neg1(dpy, config, EGL_NATIVE_VISUAL_ID),
            egl_config_attr_or_neg1(dpy, config, EGL_RENDERABLE_TYPE),
            egl_config_attr_or_neg1(dpy, config, EGL_SURFACE_TYPE),
            egl_config_attr_or_neg1(dpy, config, EGL_RED_SIZE),
            egl_config_attr_or_neg1(dpy, config, EGL_GREEN_SIZE),
            egl_config_attr_or_neg1(dpy, config, EGL_BLUE_SIZE),
            egl_config_attr_or_neg1(dpy, config, EGL_ALPHA_SIZE));
    fflush(stderr);
}

static int
glx_fbconfig_attr_or_neg1(Display *dpy, GLXFBConfig config, int attr)
{
    int value = -1;

    if (!dpy || !config)
        return -1;
    if (glXGetFBConfigAttrib(dpy, config, attr, &value) != Success)
        return -1;
    return value;
}

static void
log_glx_diagnostics(Display *dpy, int screen)
{
    int error_base = 0;
    int event_base = 0;
    int major = 0;
    int minor = 0;
    int extension_present = 0;
    int version_ok = 0;
    int fbconfig_count = 0;
    GLXFBConfig *configs = NULL;
    const char *client_vendor;
    const char *client_version;
    const char *client_extensions;
    const char *server_vendor;
    const char *server_version;
    const char *server_extensions;

    if (!dpy) {
        log_line("host-x11-egl-smoke: diag glx display=NULL");
        return;
    }

    log_line("host-x11-egl-smoke: phase=glx_query_extension status=BEGIN");
    extension_present = glXQueryExtension(dpy, &error_base, &event_base);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_query_extension present=%d error_base=%d event_base=%d\n",
            extension_present, error_base, event_base);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_query_extension result=%s present=%d error_base=%d event_base=%d\n",
            extension_present ? "PASS" : "FAIL", extension_present,
            error_base, event_base);
    if (extension_present) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_query_extension status=PASS present=%d error_base=%d event_base=%d\n",
                extension_present, error_base, event_base);
    }
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_query_version status=BEGIN");
    version_ok = glXQueryVersion(dpy, &major, &minor);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_query_version ok=%d major=%d minor=%d\n",
            version_ok, major, minor);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_query_version result=%s ok=%d major=%d minor=%d\n",
            version_ok ? "PASS" : "FAIL", version_ok, major, minor);
    if (version_ok) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_query_version status=PASS ok=%d major=%d minor=%d\n",
                version_ok, major, minor);
    }
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_client_vendor status=BEGIN");
    client_vendor = glXGetClientString(dpy, GLX_VENDOR);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_client_vendor result=%s vendor=%s\n",
            client_vendor ? "PASS" : "FAIL", safe_str(client_vendor));
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_client_version status=BEGIN");
    client_version = glXGetClientString(dpy, GLX_VERSION);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_client_version result=%s version=%s\n",
            client_version ? "PASS" : "FAIL", safe_str(client_version));
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_client_extensions status=BEGIN");
    client_extensions = glXGetClientString(dpy, GLX_EXTENSIONS);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_client_extensions result=%s extensions=%s\n",
            client_extensions ? "PASS" : "FAIL",
            safe_str(client_extensions));
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_client vendor=%s version=%s extensions=%s\n",
            safe_str(client_vendor),
            safe_str(client_version),
            safe_str(client_extensions));
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_client_strings result=%s vendor=%s version=%s\n",
            client_version ? "PASS" : "FAIL", safe_str(client_vendor),
            safe_str(client_version));
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_server_vendor status=BEGIN");
    server_vendor = glXQueryServerString(dpy, screen, GLX_VENDOR);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_server_vendor result=%s vendor=%s\n",
            server_vendor ? "PASS" : "FAIL", safe_str(server_vendor));
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_server_version status=BEGIN");
    server_version = glXQueryServerString(dpy, screen, GLX_VERSION);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_server_version result=%s version=%s\n",
            server_version ? "PASS" : "FAIL", safe_str(server_version));
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_server_extensions status=BEGIN");
    server_extensions = glXQueryServerString(dpy, screen, GLX_EXTENSIONS);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_server_extensions result=%s extensions=%s\n",
            server_extensions ? "PASS" : "FAIL",
            safe_str(server_extensions));
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_server vendor=%s version=%s extensions=%s\n",
            safe_str(server_vendor),
            safe_str(server_version),
            safe_str(server_extensions));
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_server_strings result=%s vendor=%s version=%s\n",
            server_version ? "PASS" : "FAIL", safe_str(server_vendor),
            safe_str(server_version));
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_fbconfigs_query status=BEGIN");
    configs = glXGetFBConfigs(dpy, screen, &fbconfig_count);
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_fbconfigs_query status=PASS count=%d ptr=%p\n",
            fbconfig_count, (void *)configs);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_fbconfigs count=%d ptr=%p\n",
            fbconfig_count, (void *)configs);
    fprintf(stderr,
            "host-x11-egl-smoke: diag glx_fbconfigs result=%s count=%d ptr=%p\n",
            fbconfig_count > 0 && configs ? "PASS" : "FAIL",
            fbconfig_count, (void *)configs);
    if (fbconfig_count > 0 && configs) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fbconfigs status=PASS count=%d ptr=%p\n",
                fbconfig_count, (void *)configs);
    }
    if (configs) {
        int i;
        int limit = fbconfig_count < 4 ? fbconfig_count : 4;

        for (i = 0; i < limit; i++) {
            fprintf(stderr,
                    "host-x11-egl-smoke: diag glx_fbconfig index=%d fbconfig_id=0x%x visual_id=0x%x render_type=0x%x drawable_type=0x%x red_size=%d green_size=%d blue_size=%d alpha_size=%d doublebuffer=%d\n",
                    i,
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_FBCONFIG_ID),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_VISUAL_ID),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_RENDER_TYPE),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_DRAWABLE_TYPE),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_RED_SIZE),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_GREEN_SIZE),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_BLUE_SIZE),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_ALPHA_SIZE),
                    glx_fbconfig_attr_or_neg1(dpy, configs[i],
                                              GLX_DOUBLEBUFFER));
        }
        XFree(configs);
    }
    fflush(stderr);
}

static const char *
egl_error_name(EGLint err)
{
    switch (err) {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:
        return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    default:
        return "EGL_UNKNOWN";
    }
}

static void
log_egl_fallback(const char *what)
{
    EGLint err = eglGetError();
    fprintf(stderr,
            "host-x11-egl-smoke: %s status=FALLBACK egl_error=0x%x %s fallback=glx\n",
            what, err, egl_error_name(err));
    fprintf(stderr,
            "host-x11-egl-smoke: phase=%s status=FALLBACK egl_error=0x%x error_name=%s fallback=glx\n",
            what, err, egl_error_name(err));
    fflush(stderr);
}

static void
draw(struct app *app, const char *mode)
{
    const float *c = colors[app->color_index %
                            (int)(sizeof(colors) / sizeof(colors[0]))];

    if (!app->gl.viewport || !app->gl.clear_color || !app->gl.clear) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=draw status=FAIL mode=%s reason=missing-gl-dispatch source=%s\n",
                mode, safe_str(app->gl.source));
        fflush(stderr);
        app->running = 0;
        return;
    }

    app->gl.viewport(0, 0, WIN_W, WIN_H);
    app->gl.clear_color(c[0], c[1], c[2], 1.0f);
    app->gl.clear(GL_COLOR_BUFFER_BIT);
    if (app->use_glx) {
        log_line("host-x11-egl-smoke: phase=glx_swap_buffers status=BEGIN");
        glXSwapBuffers(app->dpy, app->win);
        log_line("host-x11-egl-smoke: phase=glx_swap_buffers status=PASS");
    } else {
        if (!eglSwapBuffers(app->egl_display, app->egl_surface)) {
            log_egl_fallback("eglSwapBuffers");
            fprintf(stderr,
                    "host-x11-egl-smoke: phase=draw status=FAIL mode=%s reason=eglSwapBuffers\n",
                    mode);
            fflush(stderr);
            app->running = 0;
            return;
        }
    }
    app->frame++;
    fprintf(stderr,
            "host-x11-egl-smoke: frame=%d mode=%s color=%d rgb=%.2f,%.2f,%.2f\n",
            app->frame, mode, app->color_index, c[0], c[1], c[2]);
    fprintf(stderr,
            "host-x11-egl-smoke: phase=draw status=PASS frame=%d mode=%s color=%d\n",
            app->frame, mode, app->color_index);
    fflush(stderr);
}

static int
create_default_window(struct app *app)
{
    XSetWindowAttributes attrs;
    XSizeHints hints;

    memset(&attrs, 0, sizeof(attrs));
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                       ButtonPressMask;
    app->win = XCreateWindow(app->dpy, RootWindow(app->dpy, app->screen),
                             250, 160, WIN_W, WIN_H, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWEventMask, &attrs);
    if (!app->win) {
        log_line("host-x11-egl-smoke: XCreateWindow failed status=FAIL");
        log_line("host-x11-egl-smoke: phase=x11_connect status=FAIL reason=XCreateWindow");
        return -1;
    }

    XStoreName(app->dpy, app->win, "XV6-X11-EGL-PROOF");
    memset(&hints, 0, sizeof(hints));
    hints.flags = PPosition | PSize | PMinSize;
    hints.x = 250;
    hints.y = 160;
    hints.width = WIN_W;
    hints.height = WIN_H;
    hints.min_width = WIN_W;
    hints.min_height = WIN_H;
    XSetWMNormalHints(app->dpy, app->win, &hints);

    app->wm_protocols = XInternAtom(app->dpy, "WM_PROTOCOLS", False);
    app->wm_delete = XInternAtom(app->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(app->dpy, app->win, &app->wm_delete, 1);

    XMapWindow(app->dpy, app->win);
    XFlush(app->dpy);
    fprintf(stderr,
            "host-x11-egl-smoke: connected display=%s screen=%d window=0x%lx size=%dx%d\n",
            DisplayString(app->dpy), app->screen, app->win, WIN_W, WIN_H);
    fprintf(stderr,
            "host-x11-egl-smoke: phase=x11_connect status=PASS display=%s screen=%d window=0x%lx width=%d height=%d\n",
            DisplayString(app->dpy), app->screen, app->win, WIN_W, WIN_H);
    fflush(stderr);
    return 0;
}

static int
setup_x11(struct app *app)
{
    app->dpy = XOpenDisplay(NULL);
    if (!app->dpy) {
        log_line("host-x11-egl-smoke: XOpenDisplay failed status=FAIL");
        log_line("host-x11-egl-smoke: phase=x11_connect status=FAIL reason=XOpenDisplay");
        return -1;
    }
    app->screen = DefaultScreen(app->dpy);
    return create_default_window(app);
}

static void
destroy_window_only(struct app *app)
{
    if (app->dpy && app->win) {
        XDestroyWindow(app->dpy, app->win);
        app->win = 0;
    }
    if (app->dpy && app->colormap) {
        XFreeColormap(app->dpy, app->colormap);
        app->colormap = 0;
    }
    if (app->visual) {
        XFree(app->visual);
        app->visual = NULL;
    }
    if (app->dpy)
        XSync(app->dpy, False);
}

static int
setup_egl(struct app *app)
{
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE,
    };
    static const EGLint context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    EGLConfig config = NULL;
    EGLint num_configs = 0;
    EGLint major = 0;
    EGLint minor = 0;

    log_egl_no_display_extensions();
    app->egl_display = eglGetDisplay((EGLNativeDisplayType)app->dpy);
    if (app->egl_display == EGL_NO_DISPLAY) {
        log_egl_fallback("eglGetDisplay");
        return -1;
    }
    if (!eglInitialize(app->egl_display, &major, &minor)) {
        log_egl_fallback("eglInitialize");
        return -1;
    }
    fprintf(stderr,
            "host-x11-egl-smoke: phase=egl_initialize status=PASS major=%d minor=%d fallback=0\n",
            major, minor);
    log_egl_display_strings(app->egl_display);
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        log_egl_fallback("eglBindAPI");
        return -1;
    }
    if (!eglChooseConfig(app->egl_display, config_attrs, &config, 1,
                         &num_configs) || num_configs < 1) {
        log_egl_fallback("eglChooseConfig");
        return -1;
    }
    log_egl_config(app->egl_display, config);
    app->egl_context = eglCreateContext(app->egl_display, config,
                                        EGL_NO_CONTEXT, context_attrs);
    if (app->egl_context == EGL_NO_CONTEXT) {
        log_egl_fallback("eglCreateContext");
        return -1;
    }
    app->egl_surface = eglCreateWindowSurface(
        app->egl_display, config, (EGLNativeWindowType)app->win, NULL);
    if (app->egl_surface == EGL_NO_SURFACE) {
        log_egl_fallback("eglCreateWindowSurface");
        return -1;
    }
    if (!eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context)) {
        log_egl_fallback("eglMakeCurrent");
        return -1;
    }
    set_linked_gl_api(app);

    fprintf(stderr,
            "host-x11-egl-smoke: egl ready version=%d.%d vendor=%s renderer=%s gl_version=%s\n",
            major, minor,
            safe_str(eglQueryString(app->egl_display, EGL_VENDOR)),
            safe_str((const char *)glGetString(GL_RENDERER)),
            safe_str((const char *)glGetString(GL_VERSION)));
    fprintf(stderr,
            "host-x11-egl-smoke: phase=gl_strings status=%s api=egl vendor=%s renderer=%s gl_version=%s\n",
            glGetString(GL_VERSION) ? "PASS" : "FAIL",
            safe_str((const char *)glGetString(GL_VENDOR)),
            safe_str((const char *)glGetString(GL_RENDERER)),
            safe_str((const char *)glGetString(GL_VERSION)));
    fflush(stderr);
    return 0;
}

static void
probe_egl_initialize_for_glx_fallback(struct app *app)
{
    EGLDisplay egl_display;
    EGLint major = 0;
    EGLint minor = 0;

    log_egl_no_display_extensions();
    egl_display = eglGetDisplay((EGLNativeDisplayType)app->dpy);
    if (egl_display == EGL_NO_DISPLAY) {
        EGLint err = eglGetError();
        fprintf(stderr,
                "host-x11-egl-smoke: phase=egl_initialize status=FALLBACK reason=eglGetDisplay egl_error=0x%x error_name=%s fallback=glx\n",
                err, egl_error_name(err));
        fflush(stderr);
        return;
    }

    if (!eglInitialize(egl_display, &major, &minor)) {
        EGLint err = eglGetError();
        fprintf(stderr,
                "host-x11-egl-smoke: phase=egl_initialize status=FALLBACK reason=eglInitialize egl_error=0x%x error_name=%s fallback=glx\n",
                err, egl_error_name(err));
        fflush(stderr);
        return;
    }

    fprintf(stderr,
            "host-x11-egl-smoke: phase=egl_initialize status=PASS major=%d minor=%d fallback=glx\n",
            major, minor);
    log_egl_display_strings(egl_display);
    eglTerminate(egl_display);
    fflush(stderr);
}

static int
setup_glx(struct app *app)
{
    XSetWindowAttributes attrs;
    XSizeHints hints;
    int glx_attrs[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        None,
    };
    int major = 0;
    int minor = 0;
    int direct = 0;
    struct gl_string_results gl_strings;

    destroy_window_only(app);
    log_glx_diagnostics(app->dpy, app->screen);
    log_line("host-x11-egl-smoke: phase=glx_choose_visual status=BEGIN");
    app->visual = glXChooseVisual(app->dpy, app->screen, glx_attrs);
    if (!app->visual) {
        log_line("host-x11-egl-smoke: glx_choose_visual missing status=FAIL");
        log_line("host-x11-egl-smoke: phase=glx_choose_visual status=FAIL reason=not_reported");
        return -1;
    }
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_choose_visual status=PASS visual_id=0x%lx depth=%d class=%d\n",
            app->visual->visualid, app->visual->depth, app->visual->class);

    memset(&attrs, 0, sizeof(attrs));
    log_line("host-x11-egl-smoke: phase=glx_colormap_create status=BEGIN");
    app->colormap = XCreateColormap(app->dpy,
                                    RootWindow(app->dpy, app->screen),
                                    app->visual->visual, AllocNone);
    if (!app->colormap) {
        log_line("host-x11-egl-smoke: phase=glx_colormap_create status=FAIL reason=not_reported");
        return -1;
    }
    log_line("host-x11-egl-smoke: phase=glx_colormap_create status=PASS");
    attrs.colormap = app->colormap;
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                       ButtonPressMask;
    log_line("host-x11-egl-smoke: phase=glx_window_create status=BEGIN");
    app->win = XCreateWindow(app->dpy, RootWindow(app->dpy, app->screen),
                             250, 160, WIN_W, WIN_H, 0,
                             app->visual->depth, InputOutput,
                             app->visual->visual, CWColormap | CWEventMask,
                             &attrs);
    if (!app->win) {
        log_line("host-x11-egl-smoke: glx_window_create missing status=FAIL");
        log_line("host-x11-egl-smoke: phase=glx_window_create status=FAIL reason=not_reported");
        return -1;
    }
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_window_create status=PASS window=0x%lx depth=%d\n",
            app->win, app->visual->depth);
    fflush(stderr);
    XStoreName(app->dpy, app->win, "XV6-X11-EGL-GLX-PROOF");
    memset(&hints, 0, sizeof(hints));
    hints.flags = PPosition | PSize | PMinSize;
    hints.x = 250;
    hints.y = 160;
    hints.width = WIN_W;
    hints.height = WIN_H;
    hints.min_width = WIN_W;
    hints.min_height = WIN_H;
    XSetWMNormalHints(app->dpy, app->win, &hints);
    XSetWMProtocols(app->dpy, app->win, &app->wm_delete, 1);
    log_line("host-x11-egl-smoke: phase=glx_window_map status=BEGIN");
    XMapWindow(app->dpy, app->win);
    log_line("host-x11-egl-smoke: phase=glx_window_map status=PASS");
    log_line("host-x11-egl-smoke: phase=glx_window_flush status=BEGIN");
    XFlush(app->dpy);
    log_line("host-x11-egl-smoke: phase=glx_window_flush status=PASS");
    fprintf(stderr,
            "host-x11-egl-smoke: glx_window display=%s screen=%d window=0x%lx size=%dx%d\n",
            DisplayString(app->dpy), app->screen, app->win, WIN_W, WIN_H);
    fflush(stderr);

    log_line("host-x11-egl-smoke: phase=glx_context_query_version status=BEGIN");
    if (!glXQueryVersion(app->dpy, &major, &minor)) {
        log_line("host-x11-egl-smoke: glx_query_version missing status=FAIL");
        log_line("host-x11-egl-smoke: phase=glx_context_query_version status=FAIL reason=query_failed");
        return -1;
    }
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_context_query_version status=PASS major=%d minor=%d\n",
            major, minor);
    fflush(stderr);
    log_line("host-x11-egl-smoke: phase=glx_create_context status=BEGIN");
    app->glx_context = glXCreateContext(app->dpy, app->visual, NULL, True);
    if (!app->glx_context) {
        log_line("host-x11-egl-smoke: glx_create_context missing status=FAIL");
        log_line("host-x11-egl-smoke: phase=glx_create_context status=FAIL reason=not_reported");
        return -1;
    }
    log_line("host-x11-egl-smoke: phase=glx_is_direct status=BEGIN");
    direct = glXIsDirect(app->dpy, app->glx_context);
    app->glx_direct = direct;
    app->glx_direct_available = 1;
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_is_direct status=PASS direct=%d\n",
            direct);
    fflush(stderr);
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_create_context status=PASS direct=%d\n",
            direct);
    fflush(stderr);
    dump_gl_loader_maps("glx_before_make_current");
    log_line("host-x11-egl-smoke: phase=glx_make_current status=BEGIN");
    if (!glXMakeCurrent(app->dpy, app->win, app->glx_context)) {
        log_line("host-x11-egl-smoke: glx_make_current missing status=FAIL");
        log_line("host-x11-egl-smoke: phase=glx_make_current status=FAIL reason=not_reported");
        return -1;
    }
    log_line("host-x11-egl-smoke: phase=glx_make_current status=PASS");
    app->use_glx = 1;
    if (set_glx_gl_api(app) != 0)
        return -1;
    log_dladdr_symbol("glXMakeCurrent", (const void *)glXMakeCurrent);
    log_dladdr_symbol("glGetString", (const void *)glGetString);
    log_dladdr_symbol("glGetString_glx", (const void *)app->gl.get_string);
    log_glx_current_bindings(app->dpy);
    log_glx_query_context_attrs(app->dpy, app->glx_context, direct);
    dump_gl_loader_maps("glx_after_make_current");
    log_gl_get_string_diagnostics(app, &gl_strings);

    fprintf(stderr,
            "host-x11-egl-smoke: glx ready version=%d.%d vendor=%s renderer=%s gl_version=%s\n",
            major, minor, safe_str((const char *)gl_strings.vendor),
            safe_str((const char *)gl_strings.renderer),
            safe_str((const char *)gl_strings.version));
    fprintf(stderr,
            "host-x11-egl-smoke: phase=gl_strings status=%s api=glx vendor=%s renderer=%s gl_version=%s\n",
            gl_strings.version ? "PASS" : "FAIL",
            safe_str((const char *)gl_strings.vendor),
            safe_str((const char *)gl_strings.renderer),
            safe_str((const char *)gl_strings.version));
    fflush(stderr);
    if (!gl_strings.version)
        return -1;
    return 0;
}

static void
send_wm_delete(struct app *app)
{
    XEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = app->win;
    ev.xclient.message_type = app->wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = app->wm_delete;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(app->dpy, app->win, False, NoEventMask, &ev);
    XFlush(app->dpy);
    log_line("host-x11-egl-smoke: wm_delete_sent");
}

static void
handle_event(struct app *app, XEvent *ev)
{
    if (ev->type == MapNotify) {
        log_line("host-x11-egl-smoke: map_notify");
        draw(app, "launch");
    } else if (ev->type == ConfigureNotify) {
        fprintf(stderr,
                "host-x11-egl-smoke: configure_notify x=%d y=%d w=%d h=%d\n",
                ev->xconfigure.x, ev->xconfigure.y,
                ev->xconfigure.width, ev->xconfigure.height);
        fflush(stderr);
    } else if (ev->type == Expose) {
        draw(app, "expose");
    } else if (ev->type == ButtonPress) {
        log_line("host-x11-egl-smoke: button_press focus");
    } else if (ev->type == KeyPress) {
        KeySym sym = XLookupKeysym(&ev->xkey, 0);
        fprintf(stderr,
                "host-x11-egl-smoke: key_press keycode=%u keysym=0x%lx\n",
                ev->xkey.keycode, (unsigned long)sym);
        fflush(stderr);
        if (sym == XK_Escape) {
            send_wm_delete(app);
        } else {
            app->color_index++;
            draw(app, "input");
            log_line("host-x11-egl-smoke: input_ready");
        }
    } else if (ev->type == ClientMessage) {
        if ((Atom)ev->xclient.data.l[0] == app->wm_delete) {
            log_line("host-x11-egl-smoke: wm_delete_received");
            app->running = 0;
        }
    }
}

static void
cleanup(struct app *app)
{
    if (app->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        if (app->egl_surface != EGL_NO_SURFACE)
            eglDestroySurface(app->egl_display, app->egl_surface);
        if (app->egl_context != EGL_NO_CONTEXT)
            eglDestroyContext(app->egl_display, app->egl_context);
        eglTerminate(app->egl_display);
    }
    if (app->glx_context) {
        glXMakeCurrent(app->dpy, None, NULL);
        glXDestroyContext(app->dpy, app->glx_context);
    }
    if (app->dpy && app->win)
        XDestroyWindow(app->dpy, app->win);
    if (app->dpy && app->colormap)
        XFreeColormap(app->dpy, app->colormap);
    if (app->visual)
        XFree(app->visual);
    if (app->dpy)
        XCloseDisplay(app->dpy);
}

static int
glx_probe_only_requested(int argc, char **argv)
{
    const char *mode = getenv("HOST_X11_EGL_SMOKE_MODE");
    int i;

    if (mode && strcmp(mode, "glx-probe") == 0)
        return 1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--glx-probe-only") == 0)
            return 1;
    }
    return 0;
}

static int
glx_fps_requested(int argc, char **argv)
{
    const char *mode = getenv("HOST_X11_EGL_SMOKE_MODE");
    int i;

    if (mode && strcmp(mode, "glx-fps") == 0)
        return 1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--glx-fps") == 0)
            return 1;
    }
    return 0;
}

static const char *
glx_fps_variant_kind_name(enum glx_fps_variant_kind kind)
{
    switch (kind) {
    case GLX_FPS_VARIANT_FINISH_BEFORE_SWAP:
        return "finish-before-swap";
    case GLX_FPS_VARIANT_SWAP_ONLY:
        return "swap-only";
    case GLX_FPS_VARIANT_SWAP_INTERVAL0_SWAP_ONLY:
        return "swap-interval0-swap-only";
    case GLX_FPS_VARIANT_SWAP_INTERVAL0_OML_STATE_SWAP_ONLY:
        return "swap-interval0-oml-state-swap-only";
    case GLX_FPS_VARIANT_OML_QUEUE3_SWAP_ONLY:
        return "oml-queue3-swap-only";
    case GLX_FPS_VARIANT_OML_QUEUE_DEPTH_SWAP_ONLY:
        return "oml-queue-depth-swap-only";
    case GLX_FPS_VARIANT_OML_QUEUE_DEPTH_FLUSH_SWAP_TIMING:
        return "oml-queue-depth-flush-swap-timing";
    case GLX_FPS_VARIANT_OML_QUEUE_DEPTH_ISSUE_STATE_TIMING:
        return "oml-queue-depth-issue-state-timing";
    case GLX_FPS_VARIANT_BASELINE:
    default:
        return "baseline";
    }
}

static const char *
glx_swap_interval_api_name(enum glx_swap_interval_api api)
{
    switch (api) {
    case GLX_SWAP_INTERVAL_API_EXT:
        return "EXT";
    case GLX_SWAP_INTERVAL_API_MESA:
        return "MESA";
    case GLX_SWAP_INTERVAL_API_SGI:
        return "SGI";
    case GLX_SWAP_INTERVAL_API_NONE:
    default:
        return "none";
    }
}

static const char *
glx_swap_interval_status_name(enum glx_swap_interval_status status)
{
    switch (status) {
    case GLX_SWAP_INTERVAL_STATUS_PASS:
        return "PASS";
    case GLX_SWAP_INTERVAL_STATUS_FAIL:
        return "FAIL";
    case GLX_SWAP_INTERVAL_STATUS_UNAVAILABLE:
    default:
        return "UNAVAILABLE";
    }
}

static int
parse_decimal_range(const char *value, int min, int max, int *out)
{
    int n = 0;

    if (!value || value[0] == '\0')
        return 0;
    if (value[0] == '0' && value[1] != '\0')
        return 0;
    for (size_t i = 0; value[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)value[i];

        if (!isdigit(ch))
            return 0;
        n = n * 10 + (int)(ch - '0');
        if (n > max)
            return 0;
    }
    if (n < min)
        return 0;
    if (out)
        *out = n;
    return 1;
}

static int
read_glx_fps_oml_queue_depth(struct glx_fps_variant *variant)
{
    const char *raw = getenv("HOST_X11_EGL_GLX_FPS_OML_QUEUE_DEPTH");
    char token[64];
    int depth = 0;

    if (parse_decimal_range(raw, 1, GLX_FPS_OML_QUEUE_DEPTH_MAX,
                            &depth)) {
        variant->queue_depth = depth;
        return 0;
    }

    copy_log_value(token, sizeof(token), raw);
    variant->invalid = 1;
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_fps_variant status=FAIL source=env env=HOST_X11_EGL_GLX_FPS_OML_QUEUE_DEPTH requested=%s reason=invalid-oml-queue-depth min=1 max=%d\n",
            token[0] ? token : "(empty)", GLX_FPS_OML_QUEUE_DEPTH_MAX);
    fflush(stderr);
    return -1;
}

static int
read_glx_fps_oml_issue_state_sample_interval(
    struct glx_fps_variant *variant)
{
    const char *raw =
        getenv("HOST_X11_EGL_GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL");
    char token[64];
    int interval = 0;

    if (!raw)
        return 0;
    if (parse_decimal_range(raw, 1,
                            GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL_MAX,
                            &interval)) {
        variant->issue_state_sample_interval = interval;
        return 0;
    }

    copy_log_value(token, sizeof(token), raw);
    variant->invalid = 1;
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_fps_variant status=FAIL source=env env=HOST_X11_EGL_GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL requested=%s reason=invalid-oml-issue-state-sample-interval min=1 max=%d\n",
            token[0] ? token : "(empty)",
            GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL_MAX);
    fflush(stderr);
    return -1;
}

static void
read_glx_fps_variant(struct glx_fps_variant *variant)
{
    const char *raw = getenv("HOST_X11_EGL_GLX_FPS_VARIANT");

    memset(variant, 0, sizeof(*variant));
    variant->kind = GLX_FPS_VARIANT_BASELINE;
    variant->queue_depth = GLX_FPS_OML_QUEUE_DEPTH;
    variant->issue_state_sample_interval = 1;
    variant->name = glx_fps_variant_kind_name(variant->kind);
    if (!raw || raw[0] == '\0')
        return;

    variant->requested = 1;
    copy_log_value(variant->raw, sizeof(variant->raw), raw);
    if (strcmp(raw, "baseline") == 0) {
        variant->kind = GLX_FPS_VARIANT_BASELINE;
    } else if (strcmp(raw, "finish-before-swap") == 0) {
        variant->kind = GLX_FPS_VARIANT_FINISH_BEFORE_SWAP;
    } else if (strcmp(raw, "swap-only") == 0) {
        variant->kind = GLX_FPS_VARIANT_SWAP_ONLY;
    } else if (strcmp(raw, "swap-interval0-swap-only") == 0) {
        variant->kind = GLX_FPS_VARIANT_SWAP_INTERVAL0_SWAP_ONLY;
    } else if (strcmp(raw, "swap-interval0-oml-state-swap-only") == 0) {
        variant->kind = GLX_FPS_VARIANT_SWAP_INTERVAL0_OML_STATE_SWAP_ONLY;
    } else if (strcmp(raw, "oml-queue3-swap-only") == 0) {
        variant->kind = GLX_FPS_VARIANT_OML_QUEUE3_SWAP_ONLY;
        variant->queue_depth = GLX_FPS_OML_QUEUE_DEPTH;
    } else if (strcmp(raw, "oml-queue-depth-swap-only") == 0) {
        variant->kind = GLX_FPS_VARIANT_OML_QUEUE_DEPTH_SWAP_ONLY;
        if (read_glx_fps_oml_queue_depth(variant) != 0) {
            variant->kind = GLX_FPS_VARIANT_BASELINE;
        }
    } else if (strcmp(raw, "oml-queue-depth-flush-swap-timing") == 0) {
        variant->kind = GLX_FPS_VARIANT_OML_QUEUE_DEPTH_FLUSH_SWAP_TIMING;
        if (read_glx_fps_oml_queue_depth(variant) != 0) {
            variant->kind = GLX_FPS_VARIANT_BASELINE;
        }
    } else if (strcmp(raw, "oml-queue-depth-issue-state-timing") == 0) {
        variant->kind = GLX_FPS_VARIANT_OML_QUEUE_DEPTH_ISSUE_STATE_TIMING;
        if (read_glx_fps_oml_queue_depth(variant) != 0) {
            variant->kind = GLX_FPS_VARIANT_BASELINE;
        } else if (read_glx_fps_oml_issue_state_sample_interval(variant)
                   != 0) {
            variant->kind = GLX_FPS_VARIANT_BASELINE;
        }
    } else {
        variant->invalid = 1;
        fprintf(stderr,
                "host-x11-egl-smoke: diag glx_fps_variant status=FAIL raw=\"%s\" reason=invalid-value\n",
                variant->raw);
        fflush(stderr);
        variant->kind = GLX_FPS_VARIANT_BASELINE;
    }
    variant->name = variant->invalid ? "invalid" :
                    glx_fps_variant_kind_name(variant->kind);
}

static int
glx_fps_variant_active(const struct glx_fps_variant *variant)
{
    return variant->requested || variant->kind != GLX_FPS_VARIANT_BASELINE;
}

static int
x11_connect_only_requested(int argc, char **argv)
{
    const char *mode = getenv("HOST_X11_EGL_SMOKE_MODE");
    int i;

    if (mode && strcmp(mode, "x11-connect") == 0)
        return 1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--x11-connect-only") == 0)
            return 1;
    }
    return 0;
}

static int
drain_x11_events_for_fps(struct app *app)
{
    while (XPending(app->dpy) > 0) {
        XEvent ev;

        XNextEvent(app->dpy, &ev);
        if (ev.type == ClientMessage &&
            (Atom)ev.xclient.data.l[0] == app->wm_delete) {
            log_line("host-x11-egl-smoke: wm_delete_received");
            app->running = 0;
            return -1;
        }
        if (ev.type == MapNotify)
            log_line("host-x11-egl-smoke: phase=glx_fps_map status=PASS");
    }
    return 0;
}

static int
ensure_glx_fps_finish(struct app *app)
{
    if (app->gl.source && strcmp(app->gl.source, "glx-proc") == 0)
        app->gl.finish = glx_get_gl_finish_proc();
    else if (!app->gl.finish)
        app->gl.finish = glx_get_gl_finish_proc();
    if (app->gl.finish) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_gl_finish_dispatch status=PASS source=%s finish=%p\n",
                safe_str(app->gl.source), (void *)app->gl.finish);
        fflush(stderr);
        return 0;
    }

    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_gl_finish_dispatch status=FAIL reason=missing-gl-finish-dispatch source=%s finish=%p\n",
            safe_str(app->gl.source), (void *)app->gl.finish);
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_fps_draw status=FAIL reason=missing-gl-finish-dispatch source=%s\n",
            safe_str(app->gl.source));
    fflush(stderr);
    return -1;
}

static int
issue_glx_fps_draw_work(struct app *app, int frame,
                        const struct glx_fps_variant *variant,
                        struct glx_fps_timing *timing)
{
    const float t = (float)(frame % 120) / 119.0f;
    int issue_gl = 1;
    int64_t gl_issue_start_ns;
    int64_t gl_issue_end_ns;
    int64_t gl_error_start_ns;
    int64_t gl_error_end_ns;
    int64_t gl_issue_ns;
    int64_t gl_error_ns;
    int64_t gl_finish_start_ns;
    int64_t gl_finish_end_ns;
    GLenum err;

    if (!app->gl.viewport || !app->gl.clear_color || !app->gl.clear ||
        !app->gl.get_error) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_draw status=FAIL reason=missing-gl-dispatch source=%s\n",
                safe_str(app->gl.source));
        fflush(stderr);
        return -1;
    }

    if ((variant->kind == GLX_FPS_VARIANT_SWAP_ONLY ||
         variant->kind == GLX_FPS_VARIANT_SWAP_INTERVAL0_SWAP_ONLY ||
         variant->kind ==
             GLX_FPS_VARIANT_SWAP_INTERVAL0_OML_STATE_SWAP_ONLY ||
         variant->kind == GLX_FPS_VARIANT_OML_QUEUE3_SWAP_ONLY ||
         variant->kind == GLX_FPS_VARIANT_OML_QUEUE_DEPTH_SWAP_ONLY ||
         variant->kind == GLX_FPS_VARIANT_OML_QUEUE_DEPTH_FLUSH_SWAP_TIMING ||
         variant->kind == GLX_FPS_VARIANT_OML_QUEUE_DEPTH_ISSUE_STATE_TIMING) &&
        frame > 1)
        issue_gl = 0;

    if (issue_gl) {
        gl_issue_start_ns = monotonic_ns();
        app->gl.viewport(0, 0, WIN_W, WIN_H);
        app->gl.clear_color(0.05f + 0.20f * t, 0.18f + 0.50f * (1.0f - t),
                            0.32f + 0.36f * t, 1.0f);
        app->gl.clear(GL_COLOR_BUFFER_BIT);
        gl_issue_end_ns = monotonic_ns();
        gl_error_start_ns = monotonic_ns();
        err = app->gl.get_error();
        gl_error_end_ns = monotonic_ns();
        gl_issue_ns = elapsed_ns(gl_issue_start_ns, gl_issue_end_ns);
        gl_error_ns = elapsed_ns(gl_error_start_ns, gl_error_end_ns);
        add_timing_sample(&timing->gl_issue_total_ns,
                          &timing->max_gl_issue_ns, gl_issue_ns);
        add_timing_sample(&timing->gl_error_total_ns,
                          &timing->max_gl_error_ns, gl_error_ns);
        add_timing_sample(&timing->draw_total_ns, &timing->max_draw_ns,
                          gl_issue_ns + gl_error_ns);
        if (err != GL_NO_ERROR) {
            fprintf(stderr,
                    "host-x11-egl-smoke: phase=glx_fps_draw status=FAIL frame=%d gl_error=0x%x error_name=%s\n",
                    frame, err, gl_error_name(err));
            fflush(stderr);
            return -1;
        }
    } else {
        timing->swap_only_skipped_draw_frames++;
    }

    if (variant->kind == GLX_FPS_VARIANT_FINISH_BEFORE_SWAP) {
        if (!app->gl.finish) {
            fprintf(stderr,
                    "host-x11-egl-smoke: phase=glx_fps_draw status=FAIL reason=missing-gl-finish-dispatch source=%s\n",
                    safe_str(app->gl.source));
            fflush(stderr);
            return -1;
        }
        gl_finish_start_ns = monotonic_ns();
        app->gl.finish();
        gl_finish_end_ns = monotonic_ns();
        add_timing_sample(&timing->gl_finish_before_swap_total_ns,
                          &timing->max_gl_finish_before_swap_ns,
                          elapsed_ns(gl_finish_start_ns, gl_finish_end_ns));
    }

    return 0;
}

static void
issue_glx_fps_swap(struct app *app, struct glx_fps_timing *timing)
{
    int64_t swap_start_ns;
    int64_t swap_end_ns;

    swap_start_ns = monotonic_ns();
    glXSwapBuffers(app->dpy, app->win);
    swap_end_ns = monotonic_ns();
    add_timing_sample(&timing->swap_total_ns, &timing->max_swap_ns,
                      elapsed_ns(swap_start_ns, swap_end_ns));
    add_timing_sample(&timing->plain_swap_issue_total_ns,
                      &timing->max_plain_swap_ns,
                      elapsed_ns(swap_start_ns, swap_end_ns));
    app->frame++;
}

static int
glx_fps_setup_swap_interval0(struct app *app, struct glx_fps_timing *timing)
{
    const char *extensions;
    PFNGLXSWAPINTERVALEXTPROC swap_interval_ext;
    PFNGLXSWAPINTERVALMESAPROC swap_interval_mesa;
    PFNGLXGETSWAPINTERVALMESAPROC get_swap_interval_mesa;
    PFNGLXSWAPINTERVALSGIPROC swap_interval_sgi;
    int has_ext;
    int has_mesa;
    int has_sgi;
    int can_get_mesa_interval;
    int tried = 0;

    timing->swap_interval_requested = 0;
    timing->swap_interval_set_api = GLX_SWAP_INTERVAL_API_NONE;
    timing->swap_interval_set_status = GLX_SWAP_INTERVAL_STATUS_UNAVAILABLE;
    timing->swap_interval_before = -1;
    timing->swap_interval_after = -1;

    extensions = glXQueryExtensionsString(app->dpy, app->screen);
    has_ext = extension_list_has_token(extensions, "GLX_EXT_swap_control");
    has_mesa = extension_list_has_token(extensions, "GLX_MESA_swap_control");
    has_sgi = extension_list_has_token(extensions, "GLX_SGI_swap_control");
    swap_interval_ext = glx_get_swap_interval_ext_proc();
    swap_interval_mesa = glx_get_swap_interval_mesa_proc();
    get_swap_interval_mesa = glx_get_get_swap_interval_mesa_proc();
    swap_interval_sgi = glx_get_swap_interval_sgi_proc();
    can_get_mesa_interval = has_mesa && get_swap_interval_mesa;

    if (can_get_mesa_interval)
        timing->swap_interval_before = get_swap_interval_mesa();

    if (has_ext && swap_interval_ext) {
        tried = 1;
        timing->swap_interval_set_api = GLX_SWAP_INTERVAL_API_EXT;
        timing->swap_interval_set_status = GLX_SWAP_INTERVAL_STATUS_PASS;
        swap_interval_ext(app->dpy, app->win, 0);
        XSync(app->dpy, False);
        if (can_get_mesa_interval) {
            timing->swap_interval_after = get_swap_interval_mesa();
            if (timing->swap_interval_after != 0)
                timing->swap_interval_set_status =
                    GLX_SWAP_INTERVAL_STATUS_FAIL;
        }
        if (timing->swap_interval_set_status ==
            GLX_SWAP_INTERVAL_STATUS_PASS)
            goto out;
    }

    if (has_mesa && swap_interval_mesa) {
        int rc;

        tried = 1;
        timing->swap_interval_set_api = GLX_SWAP_INTERVAL_API_MESA;
        rc = swap_interval_mesa(0);
        XSync(app->dpy, False);
        timing->swap_interval_set_status =
            rc == 0 ? GLX_SWAP_INTERVAL_STATUS_PASS :
                      GLX_SWAP_INTERVAL_STATUS_FAIL;
        if (can_get_mesa_interval) {
            timing->swap_interval_after = get_swap_interval_mesa();
            if (timing->swap_interval_after != 0)
                timing->swap_interval_set_status =
                    GLX_SWAP_INTERVAL_STATUS_FAIL;
        }
        if (timing->swap_interval_set_status ==
            GLX_SWAP_INTERVAL_STATUS_PASS)
            goto out;
    }

    if (has_sgi && swap_interval_sgi) {
        int rc;

        tried = 1;
        timing->swap_interval_set_api = GLX_SWAP_INTERVAL_API_SGI;
        rc = swap_interval_sgi(0);
        XSync(app->dpy, False);
        timing->swap_interval_set_status =
            rc == 0 ? GLX_SWAP_INTERVAL_STATUS_PASS :
                      GLX_SWAP_INTERVAL_STATUS_FAIL;
        if (can_get_mesa_interval) {
            timing->swap_interval_after = get_swap_interval_mesa();
            if (timing->swap_interval_after != 0)
                timing->swap_interval_set_status =
                    GLX_SWAP_INTERVAL_STATUS_FAIL;
        }
    }

out:
    if (!tried) {
        timing->swap_interval_set_api = GLX_SWAP_INTERVAL_API_NONE;
        timing->swap_interval_set_status =
            GLX_SWAP_INTERVAL_STATUS_UNAVAILABLE;
        if (can_get_mesa_interval)
            timing->swap_interval_after = get_swap_interval_mesa();
    } else if (!can_get_mesa_interval) {
        timing->swap_interval_after = -1;
    }

    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_fps_swap_interval status=%s requested=0 set_api=%s before=%d after=%d ext_present=%d mesa_present=%d sgi_present=%d ext_proc=%p mesa_proc=%p mesa_get_proc=%p sgi_proc=%p\n",
            glx_swap_interval_status_name(timing->swap_interval_set_status),
            glx_swap_interval_api_name(timing->swap_interval_set_api),
            timing->swap_interval_before, timing->swap_interval_after,
            has_ext, has_mesa, has_sgi, (void *)swap_interval_ext,
            (void *)swap_interval_mesa, (void *)get_swap_interval_mesa,
            (void *)swap_interval_sgi);
    fflush(stderr);

    return timing->swap_interval_set_status == GLX_SWAP_INTERVAL_STATUS_PASS ?
           0 : -1;
}

static int
draw_glx_fps_frame(struct app *app, int frame,
                   const struct glx_fps_variant *variant,
                   struct glx_fps_timing *timing)
{
    if (issue_glx_fps_draw_work(app, frame, variant, timing) != 0)
        return -1;
    issue_glx_fps_swap(app, timing);
    return 0;
}

static int
setup_glx_plain_oml(struct app *app, struct glx_oml_api *oml,
                    struct glx_fps_timing *timing)
{
    const char *extensions;
    int extension_present;
    int64_t ust = 0;
    int64_t msc = 0;
    int64_t sbc = 0;

    memset(oml, 0, sizeof(*oml));
    extensions = glXQueryExtensionsString(app->dpy, app->screen);
    extension_present = extension_list_has_token(extensions,
                                                 "GLX_OML_sync_control");
    oml->get_sync_values = glx_get_sync_values_oml_proc();
    if (!extension_present || !oml->get_sync_values) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_plain_oml_setup status=FAIL reason=missing-oml-get-sync plain_oml_available=0 extension_present=%d get_sync=%p\n",
                extension_present, (void *)oml->get_sync_values);
        fflush(stderr);
        return -1;
    }

    if (!oml->get_sync_values(app->dpy, app->win, &ust, &msc, &sbc)) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_plain_oml_setup status=FAIL reason=plain-oml-setup plain_oml_available=1 get_sync_values=0\n");
        fflush(stderr);
        return -2;
    }

    timing->plain_oml_available = 1;
    timing->oml_available = 1;
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_fps_plain_oml_setup status=PASS plain_oml_available=1 initial_ust=%" PRId64 " initial_msc=%" PRId64 " initial_sbc=%" PRId64 "\n",
            ust, msc, sbc);
    fflush(stderr);
    return 0;
}

static int
setup_glx_oml_queue(struct app *app, struct glx_oml_api *oml,
                    struct glx_fps_timing *timing, int queue_depth)
{
    const char *extensions;
    int extension_present;
    int64_t ust = 0;
    int64_t msc = 0;
    int64_t sbc = 0;

    memset(oml, 0, sizeof(*oml));
    timing->oml_queue_depth = queue_depth;
    extensions = glXQueryExtensionsString(app->dpy, app->screen);
    extension_present = extension_list_has_token(extensions,
                                                 "GLX_OML_sync_control");
    if (!app->gl.flush)
        app->gl.flush = glx_get_gl_flush_proc();
    oml->get_sync_values = glx_get_sync_values_oml_proc();
    oml->swap_buffers_msc = glx_get_swap_buffers_msc_oml_proc();
    oml->wait_for_sbc = glx_get_wait_for_sbc_oml_proc();
    if (!extension_present || !oml->get_sync_values ||
        !oml->swap_buffers_msc || !oml->wait_for_sbc || !app->gl.flush) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_oml_setup status=FAIL reason=missing-oml oml_available=0 extension_present=%d get_sync=%p swap_msc=%p wait_sbc=%p gl_flush=%p\n",
                extension_present, (void *)oml->get_sync_values,
                (void *)oml->swap_buffers_msc, (void *)oml->wait_for_sbc,
                (void *)app->gl.flush);
        fflush(stderr);
        return -1;
    }

    if (!oml->get_sync_values(app->dpy, app->win, &ust, &msc, &sbc)) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_oml_setup status=FAIL reason=oml-setup oml_available=1 get_sync_values=0\n");
        fflush(stderr);
        return -2;
    }

    timing->oml_available = 1;
    timing->oml_last_ust = ust;
    timing->oml_last_msc = msc;
    timing->oml_last_sbc = sbc;
    fprintf(stderr,
            "host-x11-egl-smoke: phase=glx_fps_oml_setup status=PASS oml_available=1 oml_queue_depth=%d initial_ust=%" PRId64 " initial_msc=%" PRId64 " initial_sbc=%" PRId64 " gl_flush_before_swap=1\n",
            queue_depth, ust, msc, sbc);
    fflush(stderr);
    return 0;
}

static int
complete_glx_oml_sbc(struct app *app, const struct glx_oml_api *oml,
                     struct glx_fps_timing *timing, int draining,
                     int64_t *pending_sbc, int *pending_count)
{
    int64_t target_sbc;
    int64_t ust = 0;
    int64_t msc = 0;
    int64_t completed_sbc = 0;
    int64_t wait_start_ns;
    int64_t wait_end_ns;
    int completed_count = 0;
    int i;

    if (*pending_count <= 0)
        return 0;

    target_sbc = pending_sbc[0];
    wait_start_ns = monotonic_ns();
    if (!oml->wait_for_sbc(app->dpy, app->win, target_sbc, &ust, &msc,
                           &completed_sbc)) {
        wait_end_ns = monotonic_ns();
        if (draining)
            timing->oml_drain_wait_total_ns +=
                elapsed_ns(wait_start_ns, wait_end_ns);
        else
            timing->oml_wait_total_ns += elapsed_ns(wait_start_ns,
                                                   wait_end_ns);
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_oml_wait status=FAIL reason=oml-wait target_sbc=%" PRId64 " completed_sbc=%" PRId64 " draining=%d\n",
                target_sbc, completed_sbc, draining);
        fflush(stderr);
        return -1;
    }
    wait_end_ns = monotonic_ns();
    if (draining)
        timing->oml_drain_wait_total_ns +=
            elapsed_ns(wait_start_ns, wait_end_ns);
    else
        timing->oml_wait_total_ns += elapsed_ns(wait_start_ns, wait_end_ns);

    if (completed_sbc < target_sbc) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_oml_wait status=FAIL reason=oml-sbc-before-target target_sbc=%" PRId64 " completed_sbc=%" PRId64 " draining=%d\n",
                target_sbc, completed_sbc, draining);
        fflush(stderr);
        return -1;
    }

    while (completed_count < *pending_count &&
           pending_sbc[completed_count] <= completed_sbc) {
        completed_count++;
    }
    if (completed_count <= 0) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_oml_wait status=FAIL reason=oml-completion-unmatched target_sbc=%" PRId64 " completed_sbc=%" PRId64 " pending=%d draining=%d\n",
                target_sbc, completed_sbc, *pending_count, draining);
        fflush(stderr);
        return -1;
    }
    for (i = completed_count; i < *pending_count; i++)
        pending_sbc[i - completed_count] = pending_sbc[i];
    *pending_count -= completed_count;

    timing->oml_sbc_completed += completed_count;
    timing->oml_last_ust = ust;
    timing->oml_last_msc = msc;
    timing->oml_last_sbc = completed_sbc;
    return 0;
}

static int
run_glx_fps_plain_oml_loop(struct app *app,
                           const struct glx_fps_variant *variant,
                           struct glx_fps_timing *timing,
                           int64_t start_ns, int64_t *now_ns,
                           const char **failure_reason)
{
    struct glx_oml_api oml;
    int setup_rc;

    setup_rc = setup_glx_plain_oml(app, &oml, timing);
    if (setup_rc != 0) {
        *failure_reason = setup_rc == -1 ? "missing-oml-get-sync" :
                                            "plain-oml-setup";
        return -1;
    }

    while (app->running && app->frame < GLX_FPS_MAX_FRAMES &&
           *now_ns - start_ns < GLX_FPS_TARGET_NS) {
        int frame = app->frame + 1;
        int event_rc;
        int64_t event_start_ns;
        int64_t event_end_ns;
        int64_t sync_before_start_ns;
        int64_t sync_before_end_ns;
        int64_t swap_start_ns;
        int64_t swap_end_ns;
        int64_t sync_after_start_ns;
        int64_t sync_after_end_ns;
        int64_t xsync_start_ns;
        int64_t xsync_end_ns;
        int64_t before_ust = 0;
        int64_t before_msc = 0;
        int64_t before_sbc = 0;
        int64_t after_ust = 0;
        int64_t after_msc = 0;
        int64_t after_sbc = 0;
        int64_t sbc_delta;
        int64_t msc_delta;
        int64_t swap_ns;

        event_start_ns = monotonic_ns();
        event_rc = drain_x11_events_for_fps(app);
        event_end_ns = monotonic_ns();
        add_timing_sample(&timing->event_total_ns, &timing->max_event_ns,
                          elapsed_ns(event_start_ns, event_end_ns));
        if (event_rc != 0)
            break;

        if (issue_glx_fps_draw_work(app, frame, variant, timing) != 0) {
            *failure_reason = "draw";
            return -1;
        }

        sync_before_start_ns = monotonic_ns();
        if (!oml.get_sync_values(app->dpy, app->win, &before_ust,
                                 &before_msc, &before_sbc)) {
            sync_before_end_ns = monotonic_ns();
            timing->plain_oml_get_sync_before_total_ns +=
                elapsed_ns(sync_before_start_ns, sync_before_end_ns);
            fprintf(stderr,
                    "host-x11-egl-smoke: phase=glx_fps_plain_oml_result status=FAIL reason=plain-oml-get-sync-before frame=%d samples=%d\n",
                    frame, timing->plain_oml_samples);
            fflush(stderr);
            *failure_reason = "plain-oml-get-sync-before";
            return -1;
        }
        sync_before_end_ns = monotonic_ns();
        timing->plain_oml_get_sync_before_total_ns +=
            elapsed_ns(sync_before_start_ns, sync_before_end_ns);

        swap_start_ns = monotonic_ns();
        glXSwapBuffers(app->dpy, app->win);
        swap_end_ns = monotonic_ns();
        swap_ns = elapsed_ns(swap_start_ns, swap_end_ns);
        add_timing_sample(&timing->swap_total_ns, &timing->max_swap_ns,
                          swap_ns);
        add_timing_sample(&timing->plain_swap_issue_total_ns,
                          &timing->max_plain_swap_ns, swap_ns);

        sync_after_start_ns = monotonic_ns();
        if (!oml.get_sync_values(app->dpy, app->win, &after_ust,
                                 &after_msc, &after_sbc)) {
            sync_after_end_ns = monotonic_ns();
            timing->plain_oml_get_sync_after_total_ns +=
                elapsed_ns(sync_after_start_ns, sync_after_end_ns);
            fprintf(stderr,
                    "host-x11-egl-smoke: phase=glx_fps_plain_oml_result status=FAIL reason=plain-oml-get-sync-after frame=%d samples=%d before_sbc=%" PRId64 "\n",
                    frame, timing->plain_oml_samples, before_sbc);
            fflush(stderr);
            *failure_reason = "plain-oml-get-sync-after";
            return -1;
        }
        sync_after_end_ns = monotonic_ns();
        timing->plain_oml_get_sync_after_total_ns +=
            elapsed_ns(sync_after_start_ns, sync_after_end_ns);

        xsync_start_ns = monotonic_ns();
        XSync(app->dpy, False);
        xsync_end_ns = monotonic_ns();
        add_timing_sample(&timing->plain_post_swap_xsync_total_ns,
                          &timing->plain_post_swap_xsync_max_ns,
                          elapsed_ns(xsync_start_ns, xsync_end_ns));

        sbc_delta = after_sbc - before_sbc;
        msc_delta = after_msc - before_msc;
        if (sbc_delta < 0 || msc_delta < 0) {
            fprintf(stderr,
                    "host-x11-egl-smoke: phase=glx_fps_plain_oml_result status=FAIL reason=plain-oml-state-regressed frame=%d before_sbc=%" PRId64 " after_sbc=%" PRId64 " before_msc=%" PRId64 " after_msc=%" PRId64 "\n",
                    frame, before_sbc, after_sbc, before_msc, after_msc);
            fflush(stderr);
            *failure_reason = "plain-oml-state-regressed";
            return -1;
        }

        timing->plain_oml_samples++;
        if (timing->plain_oml_samples == 1) {
            timing->plain_oml_before_first_sbc = before_sbc;
            timing->plain_oml_after_first_sbc = after_sbc;
        }
        timing->plain_oml_before_last_sbc = before_sbc;
        timing->plain_oml_after_last_sbc = after_sbc;
        timing->plain_oml_post_swap_sbc_delta_total += sbc_delta;
        if (timing->plain_oml_samples == 1 ||
            sbc_delta > timing->plain_oml_post_swap_sbc_delta_max)
            timing->plain_oml_post_swap_sbc_delta_max = sbc_delta;
        timing->plain_oml_post_swap_msc_delta_total += msc_delta;
        if (timing->plain_oml_samples == 1 ||
            msc_delta > timing->plain_oml_post_swap_msc_delta_max)
            timing->plain_oml_post_swap_msc_delta_max = msc_delta;
        timing->oml_last_ust = after_ust;
        timing->oml_last_msc = after_msc;
        timing->oml_last_sbc = after_sbc;
        app->frame++;
        *now_ns = monotonic_ns();
        if (*now_ns == 0)
            break;
    }

    return 0;
}

static int
run_glx_fps_oml_queue_loop(struct app *app,
                           const struct glx_fps_variant *variant,
                           struct glx_fps_timing *timing,
                           int64_t start_ns, int64_t *now_ns,
                           const char **failure_reason)
{
    struct glx_oml_api oml;
    int64_t pending_sbc[GLX_FPS_OML_QUEUE_DEPTH_MAX];
    int pending_count = 0;
    int setup_rc;

    memset(pending_sbc, 0, sizeof(pending_sbc));
    timing->oml_issue_state_sample_interval =
        variant->issue_state_sample_interval;
    setup_rc = setup_glx_oml_queue(app, &oml, timing,
                                   variant->queue_depth);
    if (setup_rc != 0) {
        *failure_reason = setup_rc == -1 ? "missing-oml" : "oml-setup";
        return -1;
    }

    while (app->running && app->frame < GLX_FPS_MAX_FRAMES &&
           *now_ns - start_ns < GLX_FPS_TARGET_NS) {
        int event_rc;
        int64_t event_start_ns;
        int64_t event_end_ns;

        event_start_ns = monotonic_ns();
        event_rc = drain_x11_events_for_fps(app);
        event_end_ns = monotonic_ns();
        add_timing_sample(&timing->event_total_ns, &timing->max_event_ns,
                          elapsed_ns(event_start_ns, event_end_ns));
        if (event_rc != 0)
            break;

        while (pending_count < variant->queue_depth &&
               app->frame < GLX_FPS_MAX_FRAMES &&
               *now_ns - start_ns < GLX_FPS_TARGET_NS) {
            int frame = app->frame + 1;
            int64_t flush_start_ns;
            int64_t flush_end_ns;
            int64_t issue_start_ns;
            int64_t issue_end_ns;
            int64_t sync_before_start_ns = 0;
            int64_t sync_before_end_ns = 0;
            int64_t sync_after_start_ns = 0;
            int64_t sync_after_end_ns = 0;
            int64_t flush_ns;
            int64_t swap_msc_ns;
            int64_t before_ust = 0;
            int64_t before_msc = 0;
            int64_t before_sbc = 0;
            int64_t after_ust = 0;
            int64_t after_msc = 0;
            int64_t after_sbc = 0;
            int sample_issue_state;
            int64_t issued_sbc;

            if (issue_glx_fps_draw_work(app, frame, variant, timing) != 0) {
                *failure_reason = "draw";
                return -1;
            }
            flush_start_ns = monotonic_ns();
            app->gl.flush();
            flush_end_ns = monotonic_ns();
            timing->oml_gl_flush_before_swap = 1;
            sample_issue_state =
                variant->kind ==
                GLX_FPS_VARIANT_OML_QUEUE_DEPTH_ISSUE_STATE_TIMING &&
                timing->oml_sbc_issued %
                    variant->issue_state_sample_interval == 0;
            if (sample_issue_state) {
                sync_before_start_ns = monotonic_ns();
                if (!oml.get_sync_values(app->dpy, app->win, &before_ust,
                                         &before_msc, &before_sbc)) {
                    sync_before_end_ns = monotonic_ns();
                    timing->oml_get_sync_before_total_ns +=
                        elapsed_ns(sync_before_start_ns,
                                   sync_before_end_ns);
                    fprintf(stderr,
                            "host-x11-egl-smoke: phase=glx_fps_oml_issue_state status=FAIL reason=oml-get-sync-before frame=%d pending=%d samples=%d\n",
                            frame, pending_count,
                            timing->oml_issue_state_samples);
                    fflush(stderr);
                    *failure_reason = "oml-get-sync-before";
                    return -1;
                }
                sync_before_end_ns = monotonic_ns();
                timing->oml_get_sync_before_total_ns +=
                    elapsed_ns(sync_before_start_ns, sync_before_end_ns);
            }
            issue_start_ns = monotonic_ns();
            issued_sbc = oml.swap_buffers_msc(app->dpy, app->win, 0, 0, 0);
            issue_end_ns = monotonic_ns();
            if (sample_issue_state) {
                int64_t sbc_lag;
                int64_t msc_delta;

                sync_after_start_ns = monotonic_ns();
                if (!oml.get_sync_values(app->dpy, app->win, &after_ust,
                                         &after_msc, &after_sbc)) {
                    sync_after_end_ns = monotonic_ns();
                    timing->oml_get_sync_after_total_ns +=
                        elapsed_ns(sync_after_start_ns,
                                   sync_after_end_ns);
                    fprintf(stderr,
                            "host-x11-egl-smoke: phase=glx_fps_oml_issue_state status=FAIL reason=oml-get-sync-after frame=%d issued_sbc=%" PRId64 " pending=%d samples=%d\n",
                            frame, issued_sbc, pending_count,
                            timing->oml_issue_state_samples);
                    fflush(stderr);
                    *failure_reason = "oml-get-sync-after";
                    return -1;
                }
                sync_after_end_ns = monotonic_ns();
                timing->oml_get_sync_after_total_ns +=
                    elapsed_ns(sync_after_start_ns, sync_after_end_ns);
                timing->oml_issue_state_samples++;
                if (timing->oml_issue_state_samples == 1) {
                    timing->oml_issue_state_first_sample_frame = frame;
                    timing->oml_issue_state_first_sample_sbc = issued_sbc;
                }
                timing->oml_issue_state_last_sample_frame = frame;
                timing->oml_issue_state_last_sample_sbc = issued_sbc;
                if (after_sbc >= issued_sbc)
                    timing->oml_post_issue_sbc_completed_count++;
                sbc_lag = issued_sbc > after_sbc ? issued_sbc - after_sbc : 0;
                if (sbc_lag > timing->oml_post_issue_sbc_lag_max)
                    timing->oml_post_issue_sbc_lag_max = sbc_lag;
                msc_delta = after_msc - before_msc;
                timing->oml_post_issue_msc_delta_total += msc_delta;
                if (timing->oml_issue_state_samples == 1 ||
                    msc_delta > timing->oml_post_issue_msc_delta_max)
                    timing->oml_post_issue_msc_delta_max = msc_delta;
            }
            flush_ns = elapsed_ns(flush_start_ns, flush_end_ns);
            swap_msc_ns = elapsed_ns(issue_start_ns, issue_end_ns);
            add_timing_sample(&timing->swap_total_ns, &timing->max_swap_ns,
                              swap_msc_ns);
            timing->oml_gl_flush_total_ns += flush_ns;
            timing->oml_swap_msc_issue_total_ns += swap_msc_ns;
            if (variant->kind ==
                GLX_FPS_VARIANT_OML_QUEUE_DEPTH_FLUSH_SWAP_TIMING)
                timing->oml_issue_total_ns += flush_ns + swap_msc_ns;
            else
                timing->oml_issue_total_ns += swap_msc_ns;
            if (issued_sbc <= 0) {
                fprintf(stderr,
                        "host-x11-egl-smoke: phase=glx_fps_oml_issue status=FAIL reason=oml-issue frame=%d issued_sbc=%" PRId64 " pending=%d\n",
                        frame, issued_sbc, pending_count);
                fflush(stderr);
                *failure_reason = "oml-issue";
                return -1;
            }
            pending_sbc[pending_count++] = issued_sbc;
            timing->oml_sbc_issued++;
            timing->oml_last_sbc = issued_sbc;
            if (pending_count > timing->oml_max_pending_sbc)
                timing->oml_max_pending_sbc = pending_count;
            app->frame++;
            *now_ns = monotonic_ns();
        }

        if (pending_count > 0 &&
            complete_glx_oml_sbc(app, &oml, timing, 0, pending_sbc,
                                 &pending_count) != 0) {
            *failure_reason = "oml-wait";
            return -1;
        }
        *now_ns = monotonic_ns();
        if (*now_ns == 0)
            break;
    }

    while (pending_count > 0) {
        if (complete_glx_oml_sbc(app, &oml, timing, 1, pending_sbc,
                                 &pending_count) != 0) {
            *failure_reason = "oml-drain";
            return -1;
        }
    }
    *now_ns = monotonic_ns();
    return 0;
}

static void
log_glx_fps_timing(const char *status, const char *result_status,
                   const struct glx_fps_variant *variant,
                   const struct glx_fps_timing *timing, int frames)
{
    double avg_swap_ms = 0.0;
    double plain_swap_avg_ms = 0.0;
    const int variant_active = glx_fps_variant_active(variant);

    if (frames > 0)
        avg_swap_ms = ns_to_ms(timing->swap_total_ns) / (double)frames;
    if (frames > 0)
        plain_swap_avg_ms =
            ns_to_ms(timing->plain_swap_issue_total_ns) / (double)frames;

    if (variant_active) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_timing status=%s result_status=%s frames=%d event_total_ms=%.3f draw_total_ms=%.3f swap_total_ms=%.3f final_xsync_ms=%.3f max_swap_ms=%.3f max_draw_ms=%.3f max_event_ms=%.3f avg_swap_ms=%.3f variant=%s gl_finish_before_swap_total_ms=%.3f max_gl_finish_before_swap_ms=%.3f swap_only_skipped_draw_frames=%d invalid_variant=%d oml_available=%d oml_queue_depth=%d oml_sbc_issued=%" PRId64 " oml_sbc_completed=%" PRId64 " oml_max_pending_sbc=%d oml_issue_total_ms=%.3f oml_wait_total_ms=%.3f oml_drain_wait_total_ms=%.3f oml_gl_flush_before_swap=%d oml_last_ust=%" PRId64 " oml_last_msc=%" PRId64 " oml_last_sbc=%" PRId64 " oml_gl_flush_total_ms=%.3f oml_swap_msc_issue_total_ms=%.3f oml_get_sync_before_total_ms=%.3f oml_get_sync_after_total_ms=%.3f oml_issue_state_sample_interval=%d oml_issue_state_sampled_ratio=%d/%" PRId64 " oml_issue_state_first_sample_frame=%d oml_issue_state_last_sample_frame=%d oml_issue_state_first_sample_sbc=%" PRId64 " oml_issue_state_last_sample_sbc=%" PRId64 " oml_issue_state_samples=%d oml_post_issue_sbc_completed_count=%d oml_post_issue_sbc_lag_max=%" PRId64 " oml_post_issue_msc_delta_total=%" PRId64 " oml_post_issue_msc_delta_max=%" PRId64 " swap_interval_requested=%d swap_interval_set_api=%s swap_interval_set_status=%s swap_interval_before=%d swap_interval_after=%d plain_swap_issue_total_ms=%.3f plain_swap_avg_ms=%.3f plain_swap_max_ms=%.3f plain_oml_available=%d plain_oml_samples=%d plain_oml_get_sync_before_total_ms=%.3f plain_oml_get_sync_after_total_ms=%.3f plain_oml_before_first_sbc=%" PRId64 " plain_oml_before_last_sbc=%" PRId64 " plain_oml_after_first_sbc=%" PRId64 " plain_oml_after_last_sbc=%" PRId64 " plain_oml_post_swap_sbc_delta_total=%" PRId64 " plain_oml_post_swap_sbc_delta_max=%" PRId64 " plain_oml_post_swap_msc_delta_total=%" PRId64 " plain_oml_post_swap_msc_delta_max=%" PRId64 " plain_post_swap_xsync_total_ms=%.3f plain_post_swap_xsync_max_ms=%.3f\n",
                status, result_status, frames,
                ns_to_ms(timing->event_total_ns),
                ns_to_ms(timing->draw_total_ns),
                ns_to_ms(timing->swap_total_ns),
                ns_to_ms(timing->final_xsync_ns),
                ns_to_ms(timing->max_swap_ns),
                ns_to_ms(timing->max_draw_ns),
                ns_to_ms(timing->max_event_ns), avg_swap_ms,
                variant->name,
                ns_to_ms(timing->gl_finish_before_swap_total_ns),
                ns_to_ms(timing->max_gl_finish_before_swap_ns),
                timing->swap_only_skipped_draw_frames, variant->invalid,
                timing->oml_available, timing->oml_queue_depth,
                timing->oml_sbc_issued, timing->oml_sbc_completed,
                timing->oml_max_pending_sbc,
                ns_to_ms(timing->oml_issue_total_ns),
                ns_to_ms(timing->oml_wait_total_ns),
                ns_to_ms(timing->oml_drain_wait_total_ns),
                timing->oml_gl_flush_before_swap,
                timing->oml_last_ust, timing->oml_last_msc,
                timing->oml_last_sbc,
                ns_to_ms(timing->oml_gl_flush_total_ns),
                ns_to_ms(timing->oml_swap_msc_issue_total_ns),
                ns_to_ms(timing->oml_get_sync_before_total_ns),
                ns_to_ms(timing->oml_get_sync_after_total_ns),
                timing->oml_issue_state_sample_interval,
                timing->oml_issue_state_samples, timing->oml_sbc_issued,
                timing->oml_issue_state_first_sample_frame,
                timing->oml_issue_state_last_sample_frame,
                timing->oml_issue_state_first_sample_sbc,
                timing->oml_issue_state_last_sample_sbc,
                timing->oml_issue_state_samples,
                timing->oml_post_issue_sbc_completed_count,
                timing->oml_post_issue_sbc_lag_max,
                timing->oml_post_issue_msc_delta_total,
                timing->oml_post_issue_msc_delta_max,
                timing->swap_interval_requested,
                glx_swap_interval_api_name(timing->swap_interval_set_api),
                glx_swap_interval_status_name(
                    timing->swap_interval_set_status),
                timing->swap_interval_before, timing->swap_interval_after,
                ns_to_ms(timing->plain_swap_issue_total_ns),
                plain_swap_avg_ms, ns_to_ms(timing->max_plain_swap_ns),
                timing->plain_oml_available, timing->plain_oml_samples,
                ns_to_ms(timing->plain_oml_get_sync_before_total_ns),
                ns_to_ms(timing->plain_oml_get_sync_after_total_ns),
                timing->plain_oml_before_first_sbc,
                timing->plain_oml_before_last_sbc,
                timing->plain_oml_after_first_sbc,
                timing->plain_oml_after_last_sbc,
                timing->plain_oml_post_swap_sbc_delta_total,
                timing->plain_oml_post_swap_sbc_delta_max,
                timing->plain_oml_post_swap_msc_delta_total,
                timing->plain_oml_post_swap_msc_delta_max,
                ns_to_ms(timing->plain_post_swap_xsync_total_ns),
                ns_to_ms(timing->plain_post_swap_xsync_max_ns));
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_phase_timing status=%s result_status=%s frames=%d event_total_ms=%.3f gl_issue_total_ms=%.3f gl_error_total_ms=%.3f draw_total_ms=%.3f swap_total_ms=%.3f final_xsync_ms=%.3f max_gl_issue_ms=%.3f max_gl_error_ms=%.3f max_swap_ms=%.3f max_event_ms=%.3f variant=%s gl_finish_before_swap_total_ms=%.3f max_gl_finish_before_swap_ms=%.3f swap_only_skipped_draw_frames=%d invalid_variant=%d oml_available=%d oml_queue_depth=%d oml_sbc_issued=%" PRId64 " oml_sbc_completed=%" PRId64 " oml_max_pending_sbc=%d oml_issue_total_ms=%.3f oml_wait_total_ms=%.3f oml_drain_wait_total_ms=%.3f oml_gl_flush_before_swap=%d oml_last_ust=%" PRId64 " oml_last_msc=%" PRId64 " oml_last_sbc=%" PRId64 " oml_gl_flush_total_ms=%.3f oml_swap_msc_issue_total_ms=%.3f oml_get_sync_before_total_ms=%.3f oml_get_sync_after_total_ms=%.3f oml_issue_state_sample_interval=%d oml_issue_state_sampled_ratio=%d/%" PRId64 " oml_issue_state_first_sample_frame=%d oml_issue_state_last_sample_frame=%d oml_issue_state_first_sample_sbc=%" PRId64 " oml_issue_state_last_sample_sbc=%" PRId64 " oml_issue_state_samples=%d oml_post_issue_sbc_completed_count=%d oml_post_issue_sbc_lag_max=%" PRId64 " oml_post_issue_msc_delta_total=%" PRId64 " oml_post_issue_msc_delta_max=%" PRId64 " swap_interval_requested=%d swap_interval_set_api=%s swap_interval_set_status=%s swap_interval_before=%d swap_interval_after=%d plain_swap_issue_total_ms=%.3f plain_swap_avg_ms=%.3f plain_swap_max_ms=%.3f plain_oml_available=%d plain_oml_samples=%d plain_oml_get_sync_before_total_ms=%.3f plain_oml_get_sync_after_total_ms=%.3f plain_oml_before_first_sbc=%" PRId64 " plain_oml_before_last_sbc=%" PRId64 " plain_oml_after_first_sbc=%" PRId64 " plain_oml_after_last_sbc=%" PRId64 " plain_oml_post_swap_sbc_delta_total=%" PRId64 " plain_oml_post_swap_sbc_delta_max=%" PRId64 " plain_oml_post_swap_msc_delta_total=%" PRId64 " plain_oml_post_swap_msc_delta_max=%" PRId64 " plain_post_swap_xsync_total_ms=%.3f plain_post_swap_xsync_max_ms=%.3f\n",
                status, result_status, frames,
                ns_to_ms(timing->event_total_ns),
                ns_to_ms(timing->gl_issue_total_ns),
                ns_to_ms(timing->gl_error_total_ns),
                ns_to_ms(timing->draw_total_ns),
                ns_to_ms(timing->swap_total_ns),
                ns_to_ms(timing->final_xsync_ns),
                ns_to_ms(timing->max_gl_issue_ns),
                ns_to_ms(timing->max_gl_error_ns),
                ns_to_ms(timing->max_swap_ns),
                ns_to_ms(timing->max_event_ns), variant->name,
                ns_to_ms(timing->gl_finish_before_swap_total_ns),
                ns_to_ms(timing->max_gl_finish_before_swap_ns),
                timing->swap_only_skipped_draw_frames, variant->invalid,
                timing->oml_available, timing->oml_queue_depth,
                timing->oml_sbc_issued, timing->oml_sbc_completed,
                timing->oml_max_pending_sbc,
                ns_to_ms(timing->oml_issue_total_ns),
                ns_to_ms(timing->oml_wait_total_ns),
                ns_to_ms(timing->oml_drain_wait_total_ns),
                timing->oml_gl_flush_before_swap,
                timing->oml_last_ust, timing->oml_last_msc,
                timing->oml_last_sbc,
                ns_to_ms(timing->oml_gl_flush_total_ns),
                ns_to_ms(timing->oml_swap_msc_issue_total_ns),
                ns_to_ms(timing->oml_get_sync_before_total_ns),
                ns_to_ms(timing->oml_get_sync_after_total_ns),
                timing->oml_issue_state_sample_interval,
                timing->oml_issue_state_samples, timing->oml_sbc_issued,
                timing->oml_issue_state_first_sample_frame,
                timing->oml_issue_state_last_sample_frame,
                timing->oml_issue_state_first_sample_sbc,
                timing->oml_issue_state_last_sample_sbc,
                timing->oml_issue_state_samples,
                timing->oml_post_issue_sbc_completed_count,
                timing->oml_post_issue_sbc_lag_max,
                timing->oml_post_issue_msc_delta_total,
                timing->oml_post_issue_msc_delta_max,
                timing->swap_interval_requested,
                glx_swap_interval_api_name(timing->swap_interval_set_api),
                glx_swap_interval_status_name(
                    timing->swap_interval_set_status),
                timing->swap_interval_before, timing->swap_interval_after,
                ns_to_ms(timing->plain_swap_issue_total_ns),
                plain_swap_avg_ms, ns_to_ms(timing->max_plain_swap_ns),
                timing->plain_oml_available, timing->plain_oml_samples,
                ns_to_ms(timing->plain_oml_get_sync_before_total_ns),
                ns_to_ms(timing->plain_oml_get_sync_after_total_ns),
                timing->plain_oml_before_first_sbc,
                timing->plain_oml_before_last_sbc,
                timing->plain_oml_after_first_sbc,
                timing->plain_oml_after_last_sbc,
                timing->plain_oml_post_swap_sbc_delta_total,
                timing->plain_oml_post_swap_sbc_delta_max,
                timing->plain_oml_post_swap_msc_delta_total,
                timing->plain_oml_post_swap_msc_delta_max,
                ns_to_ms(timing->plain_post_swap_xsync_total_ns),
                ns_to_ms(timing->plain_post_swap_xsync_max_ns));
    } else {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_timing status=%s result_status=%s frames=%d event_total_ms=%.3f draw_total_ms=%.3f swap_total_ms=%.3f final_xsync_ms=%.3f max_swap_ms=%.3f max_draw_ms=%.3f max_event_ms=%.3f avg_swap_ms=%.3f\n",
                status, result_status, frames,
                ns_to_ms(timing->event_total_ns),
                ns_to_ms(timing->draw_total_ns),
                ns_to_ms(timing->swap_total_ns),
                ns_to_ms(timing->final_xsync_ns),
                ns_to_ms(timing->max_swap_ns),
                ns_to_ms(timing->max_draw_ns),
                ns_to_ms(timing->max_event_ns), avg_swap_ms);
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_phase_timing status=%s result_status=%s frames=%d event_total_ms=%.3f gl_issue_total_ms=%.3f gl_error_total_ms=%.3f draw_total_ms=%.3f swap_total_ms=%.3f final_xsync_ms=%.3f max_gl_issue_ms=%.3f max_gl_error_ms=%.3f max_swap_ms=%.3f max_event_ms=%.3f\n",
                status, result_status, frames,
                ns_to_ms(timing->event_total_ns),
                ns_to_ms(timing->gl_issue_total_ns),
                ns_to_ms(timing->gl_error_total_ns),
                ns_to_ms(timing->draw_total_ns),
                ns_to_ms(timing->swap_total_ns),
                ns_to_ms(timing->final_xsync_ns),
                ns_to_ms(timing->max_gl_issue_ns),
                ns_to_ms(timing->max_gl_error_ns),
                ns_to_ms(timing->max_swap_ns),
                ns_to_ms(timing->max_event_ns));
    }
    fflush(stderr);
}

static void
log_glx_fps_result(struct app *app, const char *status, const char *reason,
                   const GLubyte *vendor, const GLubyte *renderer,
                   const GLubyte *version,
                   const struct glx_fps_variant *variant, int frames,
                   double elapsed_seconds,
                   const struct glx_fps_timing *timing)
{
    char vendor_buf[256];
    char renderer_buf[256];
    char version_buf[256];
    double fps = 0.0;
    double plain_swap_avg_ms = 0.0;
    const int variant_active = glx_fps_variant_active(variant);

    if (elapsed_seconds > 0.0)
        fps = (double)frames / elapsed_seconds;
    if (timing && frames > 0)
        plain_swap_avg_ms =
            ns_to_ms(timing->plain_swap_issue_total_ns) / (double)frames;
    copy_log_value(vendor_buf, sizeof(vendor_buf), (const char *)vendor);
    copy_log_value(renderer_buf, sizeof(renderer_buf), (const char *)renderer);
    copy_log_value(version_buf, sizeof(version_buf), (const char *)version);

    if (variant_active) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_result status=%s mode=glx-fps reason=%s renderer=\"%s\" vendor=\"%s\" gl_version=\"%s\" direct_available=%d direct=%d frames=%d elapsed_seconds=%.6f fps=%.3f target_seconds=%.3f max_frames=%d swap_only_skipped_draw_frames=%d variant=%s invalid_variant=%d oml_available=%d oml_queue_depth=%d oml_sbc_issued=%" PRId64 " oml_sbc_completed=%" PRId64 " oml_max_pending_sbc=%d oml_issue_total_ms=%.3f oml_wait_total_ms=%.3f oml_drain_wait_total_ms=%.3f oml_gl_flush_before_swap=%d oml_last_ust=%" PRId64 " oml_last_msc=%" PRId64 " oml_last_sbc=%" PRId64 " oml_gl_flush_total_ms=%.3f oml_swap_msc_issue_total_ms=%.3f oml_get_sync_before_total_ms=%.3f oml_get_sync_after_total_ms=%.3f oml_issue_state_sample_interval=%d oml_issue_state_sampled_ratio=%d/%" PRId64 " oml_issue_state_first_sample_frame=%d oml_issue_state_last_sample_frame=%d oml_issue_state_first_sample_sbc=%" PRId64 " oml_issue_state_last_sample_sbc=%" PRId64 " oml_issue_state_samples=%d oml_post_issue_sbc_completed_count=%d oml_post_issue_sbc_lag_max=%" PRId64 " oml_post_issue_msc_delta_total=%" PRId64 " oml_post_issue_msc_delta_max=%" PRId64 " swap_interval_requested=%d swap_interval_set_api=%s swap_interval_set_status=%s swap_interval_before=%d swap_interval_after=%d plain_swap_issue_total_ms=%.3f plain_swap_avg_ms=%.3f plain_swap_max_ms=%.3f plain_oml_available=%d plain_oml_samples=%d plain_oml_get_sync_before_total_ms=%.3f plain_oml_get_sync_after_total_ms=%.3f plain_oml_before_first_sbc=%" PRId64 " plain_oml_before_last_sbc=%" PRId64 " plain_oml_after_first_sbc=%" PRId64 " plain_oml_after_last_sbc=%" PRId64 " plain_oml_post_swap_sbc_delta_total=%" PRId64 " plain_oml_post_swap_sbc_delta_max=%" PRId64 " plain_oml_post_swap_msc_delta_total=%" PRId64 " plain_oml_post_swap_msc_delta_max=%" PRId64 " plain_post_swap_xsync_total_ms=%.3f plain_post_swap_xsync_max_ms=%.3f\n",
                status, safe_str(reason), renderer_buf, vendor_buf,
                version_buf, app->glx_direct_available, app->glx_direct,
                frames, elapsed_seconds, fps,
                (double)GLX_FPS_TARGET_NS / 1000000000.0,
                GLX_FPS_MAX_FRAMES,
                timing ? timing->swap_only_skipped_draw_frames : 0,
                variant->name, variant->invalid,
                timing ? timing->oml_available : 0,
                timing ? timing->oml_queue_depth : 0,
                timing ? timing->oml_sbc_issued : 0,
                timing ? timing->oml_sbc_completed : 0,
                timing ? timing->oml_max_pending_sbc : 0,
                timing ? ns_to_ms(timing->oml_issue_total_ns) : 0.0,
                timing ? ns_to_ms(timing->oml_wait_total_ns) : 0.0,
                timing ? ns_to_ms(timing->oml_drain_wait_total_ns) : 0.0,
                timing ? timing->oml_gl_flush_before_swap : 0,
                timing ? timing->oml_last_ust : 0,
                timing ? timing->oml_last_msc : 0,
                timing ? timing->oml_last_sbc : 0,
                timing ? ns_to_ms(timing->oml_gl_flush_total_ns) : 0.0,
                timing ? ns_to_ms(timing->oml_swap_msc_issue_total_ns) : 0.0,
                timing ? ns_to_ms(timing->oml_get_sync_before_total_ns) : 0.0,
                timing ? ns_to_ms(timing->oml_get_sync_after_total_ns) : 0.0,
                timing ? timing->oml_issue_state_sample_interval : 0,
                timing ? timing->oml_issue_state_samples : 0,
                timing ? timing->oml_sbc_issued : 0,
                timing ? timing->oml_issue_state_first_sample_frame : 0,
                timing ? timing->oml_issue_state_last_sample_frame : 0,
                timing ? timing->oml_issue_state_first_sample_sbc : 0,
                timing ? timing->oml_issue_state_last_sample_sbc : 0,
                timing ? timing->oml_issue_state_samples : 0,
                timing ? timing->oml_post_issue_sbc_completed_count : 0,
                timing ? timing->oml_post_issue_sbc_lag_max : 0,
                timing ? timing->oml_post_issue_msc_delta_total : 0,
                timing ? timing->oml_post_issue_msc_delta_max : 0,
                timing ? timing->swap_interval_requested : 0,
                glx_swap_interval_api_name(
                    timing ? timing->swap_interval_set_api :
                    GLX_SWAP_INTERVAL_API_NONE),
                glx_swap_interval_status_name(
                    timing ? timing->swap_interval_set_status :
                    GLX_SWAP_INTERVAL_STATUS_UNAVAILABLE),
                timing ? timing->swap_interval_before : -1,
                timing ? timing->swap_interval_after : -1,
                timing ? ns_to_ms(timing->plain_swap_issue_total_ns) : 0.0,
                plain_swap_avg_ms,
                timing ? ns_to_ms(timing->max_plain_swap_ns) : 0.0,
                timing ? timing->plain_oml_available : 0,
                timing ? timing->plain_oml_samples : 0,
                timing ? ns_to_ms(
                    timing->plain_oml_get_sync_before_total_ns) : 0.0,
                timing ? ns_to_ms(
                    timing->plain_oml_get_sync_after_total_ns) : 0.0,
                timing ? timing->plain_oml_before_first_sbc : 0,
                timing ? timing->plain_oml_before_last_sbc : 0,
                timing ? timing->plain_oml_after_first_sbc : 0,
                timing ? timing->plain_oml_after_last_sbc : 0,
                timing ? timing->plain_oml_post_swap_sbc_delta_total : 0,
                timing ? timing->plain_oml_post_swap_sbc_delta_max : 0,
                timing ? timing->plain_oml_post_swap_msc_delta_total : 0,
                timing ? timing->plain_oml_post_swap_msc_delta_max : 0,
                timing ? ns_to_ms(
                    timing->plain_post_swap_xsync_total_ns) : 0.0,
                timing ? ns_to_ms(
                    timing->plain_post_swap_xsync_max_ns) : 0.0);
    } else {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps_result status=%s mode=glx-fps reason=%s renderer=\"%s\" vendor=\"%s\" gl_version=\"%s\" direct_available=%d direct=%d frames=%d elapsed_seconds=%.6f fps=%.3f target_seconds=%.3f max_frames=%d\n",
                status, safe_str(reason), renderer_buf, vendor_buf,
                version_buf, app->glx_direct_available, app->glx_direct,
                frames, elapsed_seconds, fps,
                (double)GLX_FPS_TARGET_NS / 1000000000.0,
                GLX_FPS_MAX_FRAMES);
    }
    fflush(stderr);
}

static int
run_glx_fps(struct app *app)
{
    const GLubyte *vendor = NULL;
    const GLubyte *renderer = NULL;
    const GLubyte *version = NULL;
    int64_t start_ns;
    int64_t now_ns;
    struct glx_fps_timing timing;
    struct glx_fps_variant variant;
    const char *loop_failure_reason = NULL;
    int loop_failed = 0;
    int rc = 1;

    memset(&timing, 0, sizeof(timing));
    timing.swap_interval_set_api = GLX_SWAP_INTERVAL_API_NONE;
    timing.swap_interval_set_status = GLX_SWAP_INTERVAL_STATUS_UNAVAILABLE;
    timing.swap_interval_before = -1;
    timing.swap_interval_after = -1;
    read_glx_fps_variant(&variant);
    if (variant.kind == GLX_FPS_VARIANT_SWAP_INTERVAL0_SWAP_ONLY ||
        variant.kind == GLX_FPS_VARIANT_SWAP_INTERVAL0_OML_STATE_SWAP_ONLY)
        timing.swap_interval_requested = 0;
    if (variant.invalid) {
        log_glx_fps_result(app, "FAIL", "invalid_variant", NULL, NULL, NULL,
                           &variant, 0, 0.0, &timing);
        goto out;
    }
    if (setup_x11(app) != 0) {
        log_glx_fps_result(app, "FAIL", "x11_setup", NULL, NULL, NULL,
                           &variant, 0, 0.0, &timing);
        goto out;
    }
    probe_egl_initialize_for_glx_fallback(app);
    if (setup_glx(app) != 0) {
        log_glx_fps_result(app, "FAIL", "glx_setup", NULL, NULL, NULL,
                           &variant, app->frame, 0.0, &timing);
        goto out;
    }
    if (variant.kind == GLX_FPS_VARIANT_FINISH_BEFORE_SWAP &&
        ensure_glx_fps_finish(app) != 0) {
        log_glx_fps_result(app, "FAIL", "gl_finish_dispatch", NULL, NULL,
                           NULL, &variant, app->frame, 0.0, &timing);
        goto out;
    }

    vendor = app->gl.get_string(GL_VENDOR);
    renderer = app->gl.get_string(GL_RENDERER);
    version = app->gl.get_string(GL_VERSION);
    if (glx_fps_variant_active(&variant)) {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps status=BEGIN mode=glx-fps target_seconds=%.3f max_frames=%d renderer=\"%s\" vendor=\"%s\" gl_version=\"%s\" direct_available=%d direct=%d variant=%s invalid_variant=%d\n",
                (double)GLX_FPS_TARGET_NS / 1000000000.0,
                GLX_FPS_MAX_FRAMES, safe_str((const char *)renderer),
                safe_str((const char *)vendor),
                safe_str((const char *)version), app->glx_direct_available,
                app->glx_direct, variant.name, variant.invalid);
    } else {
        fprintf(stderr,
                "host-x11-egl-smoke: phase=glx_fps status=BEGIN mode=glx-fps target_seconds=%.3f max_frames=%d renderer=\"%s\" vendor=\"%s\" gl_version=\"%s\" direct_available=%d direct=%d\n",
                (double)GLX_FPS_TARGET_NS / 1000000000.0,
                GLX_FPS_MAX_FRAMES, safe_str((const char *)renderer),
                safe_str((const char *)vendor),
                safe_str((const char *)version), app->glx_direct_available,
                app->glx_direct);
    }
    fflush(stderr);

    if ((variant.kind == GLX_FPS_VARIANT_SWAP_INTERVAL0_SWAP_ONLY ||
         variant.kind == GLX_FPS_VARIANT_SWAP_INTERVAL0_OML_STATE_SWAP_ONLY) &&
        glx_fps_setup_swap_interval0(app, &timing) != 0) {
        loop_failed = 1;
        loop_failure_reason = "swap_interval_setup";
    }

    XSync(app->dpy, False);
    app->running = 1;
    start_ns = monotonic_ns();
    now_ns = start_ns;
    if (!loop_failed) {
        if (variant.kind ==
            GLX_FPS_VARIANT_SWAP_INTERVAL0_OML_STATE_SWAP_ONLY) {
            if (run_glx_fps_plain_oml_loop(app, &variant, &timing,
                                           start_ns, &now_ns,
                                           &loop_failure_reason) != 0)
                loop_failed = 1;
        } else if (variant.kind == GLX_FPS_VARIANT_OML_QUEUE3_SWAP_ONLY ||
            variant.kind == GLX_FPS_VARIANT_OML_QUEUE_DEPTH_SWAP_ONLY ||
            variant.kind ==
                GLX_FPS_VARIANT_OML_QUEUE_DEPTH_FLUSH_SWAP_TIMING ||
            variant.kind ==
                GLX_FPS_VARIANT_OML_QUEUE_DEPTH_ISSUE_STATE_TIMING) {
            if (run_glx_fps_oml_queue_loop(app, &variant, &timing, start_ns,
                                           &now_ns, &loop_failure_reason)
                != 0)
                loop_failed = 1;
        } else {
            while (app->running && app->frame < GLX_FPS_MAX_FRAMES &&
                   now_ns - start_ns < GLX_FPS_TARGET_NS) {
                int event_rc;
                int64_t event_start_ns;
                int64_t event_end_ns;

                event_start_ns = monotonic_ns();
                event_rc = drain_x11_events_for_fps(app);
                event_end_ns = monotonic_ns();
                add_timing_sample(&timing.event_total_ns,
                                  &timing.max_event_ns,
                                  elapsed_ns(event_start_ns, event_end_ns));
                if (event_rc != 0)
                    break;
                if (draw_glx_fps_frame(app, app->frame + 1, &variant,
                                       &timing) != 0)
                    break;
                now_ns = monotonic_ns();
                if (now_ns == 0)
                    break;
            }
        }
    }
    {
        int64_t xsync_start_ns;
        int64_t xsync_end_ns;

        xsync_start_ns = monotonic_ns();
        XSync(app->dpy, False);
        xsync_end_ns = monotonic_ns();
        timing.final_xsync_ns = elapsed_ns(xsync_start_ns, xsync_end_ns);
        now_ns = xsync_end_ns;
    }
    if (now_ns <= start_ns)
        now_ns = start_ns + 1;

    if (app->frame > 0 && app->running && version && !loop_failed) {
        rc = 0;
        log_glx_fps_timing("PASS", "PASS", &variant, &timing, app->frame);
        log_glx_fps_result(app, "PASS", "complete", vendor, renderer,
                           version, &variant, app->frame,
                           (double)(now_ns - start_ns) / 1000000000.0,
                           &timing);
    } else {
        log_glx_fps_timing("INFO", "FAIL", &variant, &timing, app->frame);
        log_glx_fps_result(app, "FAIL",
                           loop_failure_reason ? loop_failure_reason :
                           (app->running ? "no_frames_or_gl_version"
                                         : "window_closed"),
                           vendor, renderer, version, &variant, app->frame,
                           (double)(now_ns - start_ns) / 1000000000.0,
                           &timing);
    }

out:
    fprintf(stderr,
            "host-x11-egl-smoke: phase=result status=%s mode=glx-fps frame=%d use_glx=%d\n",
            rc == 0 ? "PASS" : "FAIL", app->frame, app->use_glx);
    fflush(stderr);
    cleanup(app);
    log_line("host-x11-egl-smoke: exited mode=glx-fps");
    return rc;
}

static int
run_x11_connect_only(struct app *app)
{
    int rc = 0;

    if (setup_x11(app) != 0)
        rc = 1;

    fprintf(stderr,
            "host-x11-egl-smoke: phase=result status=%s mode=x11-connect\n",
            rc == 0 ? "PASS" : "FAIL");
    fflush(stderr);
    cleanup(app);
    log_line("host-x11-egl-smoke: exited mode=x11-connect");
    return rc;
}

static int
run_glx_probe_only(struct app *app)
{
    int rc = 0;

    if (setup_x11(app) != 0) {
        rc = 1;
        goto out;
    }
    probe_egl_initialize_for_glx_fallback(app);
    if (setup_glx(app) != 0) {
        rc = 1;
        goto out;
    }
    draw(app, "glx-probe");

out:
    fprintf(stderr,
            "host-x11-egl-smoke: phase=result status=%s mode=glx-probe frame=%d use_glx=%d\n",
            rc == 0 ? "PASS" : "FAIL", app->frame, app->use_glx);
    fflush(stderr);
    cleanup(app);
    log_line("host-x11-egl-smoke: exited mode=glx-probe");
    return rc;
}

int
main(int argc, char **argv)
{
    struct app app;
    int x11_connect_only;
    int glx_probe_only;
    int glx_fps;

    memset(&app, 0, sizeof(app));
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_context = EGL_NO_CONTEXT;
    app.egl_surface = EGL_NO_SURFACE;
    app.running = 1;
    set_linked_gl_api(&app);
    x11_connect_only = x11_connect_only_requested(argc, argv);
    glx_probe_only = glx_probe_only_requested(argc, argv);
    glx_fps = glx_fps_requested(argc, argv);

    if (x11_connect_only) {
        log_start("x11-connect");
        return run_x11_connect_only(&app);
    }

    log_start(glx_fps ? "glx-fps" :
              (glx_probe_only ? "glx-probe" : "interactive"));
    if (glx_fps)
        return run_glx_fps(&app);
    if (glx_probe_only)
        return run_glx_probe_only(&app);

    if (setup_x11(&app) != 0) {
        cleanup(&app);
        log_line("host-x11-egl-smoke: phase=result status=FAIL mode=interactive");
        return 1;
    }
    if (setup_egl(&app) != 0 && setup_glx(&app) != 0) {
        cleanup(&app);
        log_line("host-x11-egl-smoke: phase=result status=FAIL mode=interactive");
        return 1;
    }
    draw(&app, "launch");

    while (app.running) {
        XEvent ev;
        XNextEvent(app.dpy, &ev);
        handle_event(&app, &ev);
    }

    cleanup(&app);
    log_line("host-x11-egl-smoke: phase=result status=PASS mode=interactive");
    log_line("host-x11-egl-smoke: exited");
    return 0;
}
