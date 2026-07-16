/* Trace QEMU's SDL logical-window versus GL-drawable geometry.
 *
 * This interposer deliberately preserves every SDL return value and argument.
 * It needs no SDL development headers because the intercepted ABI surface is
 * limited to opaque window pointers and integer geometry.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct SDL_Window SDL_Window;

static SDL_Window *(*real_create_window)(const char *, int, int, int, int,
                                          uint32_t);
static void (*real_set_window_size)(SDL_Window *, int, int);
static void (*real_get_window_size)(SDL_Window *, int *, int *);
static void (*real_get_drawable_size)(SDL_Window *, int *, int *);
static void *(*real_create_context)(SDL_Window *);
static uint32_t (*real_get_window_flags)(SDL_Window *);
static const char *(*real_get_video_driver)(void);
static void (*real_swap_window)(SDL_Window *);
static int (*real_set_fullscreen)(SDL_Window *, uint32_t);
static int trace_fd = -2;
static int gl_context_ready;

struct observed_window {
    SDL_Window *window;
    int logical_w;
    int logical_h;
    int drawable_w;
    int drawable_h;
};

static struct observed_window observed[8];

static void *next_symbol(const char *name)
{
    return dlsym(RTLD_NEXT, name);
}

static void resolve_symbols(void)
{
    if (real_create_window != NULL)
        return;
    *(void **)(&real_create_window) = next_symbol("SDL_CreateWindow");
    *(void **)(&real_set_window_size) = next_symbol("SDL_SetWindowSize");
    *(void **)(&real_get_window_size) = next_symbol("SDL_GetWindowSize");
    *(void **)(&real_get_drawable_size) =
        next_symbol("SDL_GL_GetDrawableSize");
    *(void **)(&real_create_context) = next_symbol("SDL_GL_CreateContext");
    *(void **)(&real_get_window_flags) = next_symbol("SDL_GetWindowFlags");
    *(void **)(&real_get_video_driver) =
        next_symbol("SDL_GetCurrentVideoDriver");
    *(void **)(&real_swap_window) = next_symbol("SDL_GL_SwapWindow");
    *(void **)(&real_set_fullscreen) = next_symbol("SDL_SetWindowFullscreen");
}

static int get_trace_fd(void)
{
    const char *path;

    if (trace_fd != -2)
        return trace_fd;
    path = getenv("XV6_SDL_GEOMETRY_TRACE_LOG");
    if (path == NULL || path[0] == '\0') {
        trace_fd = -1;
        return trace_fd;
    }
    trace_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    return trace_fd;
}

static void trace_line(const char *fmt, ...)
{
    char line[768];
    va_list ap;
    int fd;
    int len;
    ssize_t written;

    fd = get_trace_fd();
    if (fd < 0)
        return;
    va_start(ap, fmt);
    len = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (len < 0)
        return;
    if ((size_t)len >= sizeof(line))
        len = (int)sizeof(line) - 1;
    written = write(fd, line, (size_t)len);
    (void)written;
}

static void read_geometry(SDL_Window *window, int *lw, int *lh,
                          int *dw, int *dh)
{
    *lw = *lh = *dw = *dh = -1;
    resolve_symbols();
    if (window != NULL && real_get_window_size != NULL)
        real_get_window_size(window, lw, lh);
    if (gl_context_ready && window != NULL && real_get_drawable_size != NULL)
        real_get_drawable_size(window, dw, dh);
}

static void snapshot(const char *op, SDL_Window *window,
                     int requested_w, int requested_h)
{
    int lw, lh, dw, dh;
    uint32_t flags = 0;
    const char *driver = "unknown";

    read_geometry(window, &lw, &lh, &dw, &dh);
    if (real_get_window_flags != NULL && window != NULL)
        flags = real_get_window_flags(window);
    if (real_get_video_driver != NULL) {
        const char *found = real_get_video_driver();
        if (found != NULL)
            driver = found;
    }
    trace_line("sdl_geometry op=%s pid=%ld window=%p requested=%dx%d "
               "logical=%dx%d drawable=%dx%d flags=0x%x driver=%s\n",
               op, (long)getpid(), (void *)window,
               requested_w, requested_h, lw, lh, dw, dh, flags, driver);
}

static void snapshot_if_changed(const char *op, SDL_Window *window,
                                int lw, int lh)
{
    int dw = -1, dh = -1;
    unsigned int slot = 0;

    resolve_symbols();
    if (gl_context_ready && window != NULL && real_get_drawable_size != NULL)
        real_get_drawable_size(window, &dw, &dh);
    for (unsigned int i = 0; i < sizeof(observed) / sizeof(observed[0]); i++) {
        if (observed[i].window == window || observed[i].window == NULL) {
            slot = i;
            break;
        }
    }
    if (observed[slot].window == window &&
        observed[slot].logical_w == lw && observed[slot].logical_h == lh &&
        observed[slot].drawable_w == dw && observed[slot].drawable_h == dh)
        return;
    observed[slot].window = window;
    observed[slot].logical_w = lw;
    observed[slot].logical_h = lh;
    observed[slot].drawable_w = dw;
    observed[slot].drawable_h = dh;
    snapshot(op, window, lw, lh);
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             uint32_t flags)
{
    SDL_Window *window;

    resolve_symbols();
    if (real_create_window == NULL)
        return NULL;
    trace_line("sdl_geometry op=create-call pid=%ld requested=%dx%d "
               "position=%d,%d flags=0x%x title=%s\n",
               (long)getpid(), w, h, x, y, flags,
               title != NULL ? title : "");
    window = real_create_window(title, x, y, w, h, flags);
    snapshot("create-return", window, w, h);
    return window;
}

void *SDL_GL_CreateContext(SDL_Window *window)
{
    void *context;

    resolve_symbols();
    if (real_create_context == NULL)
        return NULL;
    context = real_create_context(window);
    if (context != NULL)
        gl_context_ready = 1;
    snapshot("gl-context-return", window, 0, 0);
    return context;
}

void SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
    resolve_symbols();
    snapshot("set-size-before", window, w, h);
    if (real_set_window_size != NULL)
        real_set_window_size(window, w, h);
    snapshot("set-size-after", window, w, h);
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    int lw = -1, lh = -1;

    resolve_symbols();
    if (real_get_window_size != NULL)
        real_get_window_size(window, &lw, &lh);
    if (w != NULL)
        *w = lw;
    if (h != NULL)
        *h = lh;
    snapshot_if_changed("get-size-change", window, lw, lh);
}

void SDL_GL_SwapWindow(SDL_Window *window)
{
    int lw = -1, lh = -1;

    resolve_symbols();
    if (real_get_window_size != NULL)
        real_get_window_size(window, &lw, &lh);
    snapshot_if_changed("swap-geometry-change", window, lw, lh);
    if (real_swap_window != NULL)
        real_swap_window(window);
}

int SDL_SetWindowFullscreen(SDL_Window *window, uint32_t flags)
{
    int result = -1;

    resolve_symbols();
    snapshot("fullscreen-before", window, (int)flags, 0);
    if (real_set_fullscreen != NULL)
        result = real_set_fullscreen(window, flags);
    snapshot("fullscreen-after", window, (int)flags, result);
    return result;
}
