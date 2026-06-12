#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"

#define WIN_W 520
#define WIN_H 320

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLSurface egl_surface;
    int running;
    int configured;
    int color_index;
    int frame;
};

static const float colors[][3] = {
    {0.05f, 0.10f, 0.80f},
    {0.80f, 0.10f, 0.10f},
    {0.05f, 0.65f, 0.20f},
};

static void draw(struct app *app)
{
    const float *c = colors[app->color_index %
                            (int)(sizeof(colors) / sizeof(colors[0]))];

    glViewport(0, 0, WIN_W, WIN_H);
    glClearColor(c[0], c[1], c[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!eglSwapBuffers(app->egl_display, app->egl_surface)) {
        fprintf(stderr, "host-wlegl-smoke: eglSwapBuffers failed err=0x%x\n",
                eglGetError());
        fflush(stderr);
        app->running = 0;
        return;
    }
    app->frame++;
    fprintf(stderr, "host-wlegl-smoke: frame=%d color=%d rgb=%.2f,%.2f,%.2f\n",
            app->frame, app->color_index, c[0], c[1], c[2]);
    fflush(stderr);
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
    (void)sx;
    (void)sy;
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)sx;
    (void)sy;
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    struct app *app = data;

    (void)pointer;
    (void)serial;
    (void)time;
    if (state) {
        app->color_index++;
        fprintf(stderr, "host-wlegl-smoke: button=%u color=%d\n", button,
                app->color_index);
        fflush(stderr);
        draw(app);
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data;
    (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete)
{
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t value120)
{
    (void)data;
    (void)pointer;
    (void)axis;
    (void)value120;
}

static void pointer_axis_relative_direction(void *data,
                                            struct wl_pointer *pointer,
                                            uint32_t axis, uint32_t direction)
{
    (void)data;
    (void)pointer;
    (void)axis;
    (void)direction;
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_axis,
    pointer_frame,
    pointer_axis_source,
    pointer_axis_stop,
    pointer_axis_discrete,
    pointer_axis_value120,
    pointer_axis_relative_direction,
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size)
{
    (void)data;
    (void)keyboard;
    (void)format;
    (void)size;
    if (fd >= 0)
        close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state)
{
    struct app *app = data;

    (void)keyboard;
    (void)serial;
    (void)time;
    if (state && (key == 1 || key == 9)) {
        fprintf(stderr, "host-wlegl-smoke: escape exit key=%u\n", key);
        fflush(stderr);
        app->running = 0;
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)mods_depressed;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
    keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              enum wl_seat_capability caps)
{
    struct app *app = data;

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                         uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app *app = data;

    xdg_surface_ack_configure(surface, serial);
    app->configured = 1;
    fprintf(stderr, "host-wlegl-smoke: configured serial=%u\n", serial);
    fflush(stderr);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app *app = data;

    (void)toplevel;
    app->running = 0;
}

static void toplevel_configure_bounds(void *data,
                                      struct xdg_toplevel *toplevel,
                                      int32_t width, int32_t height)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}

static void toplevel_wm_capabilities(void *data,
                                     struct xdg_toplevel *toplevel,
                                     struct wl_array *capabilities)
{
    (void)data;
    (void)toplevel;
    (void)capabilities;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure,
    toplevel_close,
    toplevel_configure_bounds,
    toplevel_wm_capabilities,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;

    if (strcmp(interface, "wl_compositor") == 0) {
        app->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface,
                                           version < 4 ? version : 4);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        app->wm_base = wl_registry_bind(registry, name,
                                        &xdg_wm_base_interface,
                                        version < 5 ? version : 5);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, "wl_seat") == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface,
                                     version < 5 ? version : 5);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove,
};

static int init_wayland(struct app *app)
{
    app->display = wl_display_connect(NULL);
    if (!app->display) {
        fprintf(stderr, "host-wlegl-smoke: wl_display_connect failed\n");
        return -1;
    }
    app->registry = wl_display_get_registry(app->display);
    wl_registry_add_listener(app->registry, &registry_listener, app);
    wl_display_roundtrip(app->display);
    wl_display_roundtrip(app->display);

    if (!app->compositor || !app->wm_base) {
        fprintf(stderr,
                "host-wlegl-smoke: missing globals compositor=%p wm_base=%p\n",
                (void *)app->compositor, (void *)app->wm_base);
        return -1;
    }

    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base,
                                                   app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, "Host WLEGL Smoke");
    xdg_toplevel_set_app_id(app->toplevel, "host-wlegl-smoke");
    wl_surface_commit(app->surface);

    while (!app->configured) {
        if (wl_display_dispatch(app->display) < 0) {
            fprintf(stderr, "host-wlegl-smoke: configure dispatch failed\n");
            return -1;
        }
    }
    return 0;
}

static int init_egl(struct app *app)
{
    EGLint major = 0, minor = 0, ncfg = 0;
    EGLConfig config;
    static const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    static const EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };

    app->egl_window = wl_egl_window_create(app->surface, WIN_W, WIN_H);
    if (!app->egl_window) {
        fprintf(stderr, "host-wlegl-smoke: wl_egl_window_create failed\n");
        return -1;
    }
    app->egl_display = eglGetDisplay((EGLNativeDisplayType)app->display);
    if (app->egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "host-wlegl-smoke: eglGetDisplay failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    if (!eglInitialize(app->egl_display, &major, &minor)) {
        fprintf(stderr, "host-wlegl-smoke: eglInitialize failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    if (!eglChooseConfig(app->egl_display, attrs, &config, 1, &ncfg) ||
        ncfg < 1) {
        fprintf(stderr, "host-wlegl-smoke: eglChooseConfig failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "host-wlegl-smoke: eglBindAPI failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    app->egl_context = eglCreateContext(app->egl_display, config,
                                        EGL_NO_CONTEXT, ctx_attrs);
    if (app->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "host-wlegl-smoke: eglCreateContext failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    app->egl_surface = eglCreateWindowSurface(app->egl_display, config,
                                             (EGLNativeWindowType)app->egl_window,
                                             NULL);
    if (app->egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr,
                "host-wlegl-smoke: eglCreateWindowSurface failed err=0x%x\n",
                eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context)) {
        fprintf(stderr, "host-wlegl-smoke: eglMakeCurrent failed err=0x%x\n",
                eglGetError());
        return -1;
    }

    fprintf(stderr, "host-wlegl-smoke: egl ready %d.%d vendor=%s renderer=%s\n",
            major, minor, glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    fflush(stderr);
    return 0;
}

int main(void)
{
    struct app app;
    FILE *pidf;

    memset(&app, 0, sizeof(app));
    app.running = 1;
    setenv("XDG_RUNTIME_DIR", getenv("XDG_RUNTIME_DIR") ?
           getenv("XDG_RUNTIME_DIR") : "/tmp", 1);
    setenv("WAYLAND_DISPLAY", getenv("WAYLAND_DISPLAY") ?
           getenv("WAYLAND_DISPLAY") : "wayland-0", 1);

    fprintf(stderr, "host-wlegl-smoke: start\n");
    fflush(stderr);
    if (init_wayland(&app) != 0 || init_egl(&app) != 0)
        return 2;

    draw(&app);
    fprintf(stderr, "host-wlegl-smoke: ready frame=%d color=%d\n", app.frame,
            app.color_index);
    fflush(stderr);
    pidf = fopen("/tmp/host-wlegl-smoke.pid", "w");
    if (pidf) {
        fprintf(pidf, "%ld\n", (long)getpid());
        fclose(pidf);
    }

    while (app.running) {
        if (access("/tmp/host-wlegl-smoke-exit", F_OK) == 0) {
            unlink("/tmp/host-wlegl-smoke-exit");
            app.running = 0;
            break;
        }
        if (access("/tmp/host-wlegl-smoke-toggle", F_OK) == 0) {
            unlink("/tmp/host-wlegl-smoke-toggle");
            app.color_index++;
            fprintf(stderr, "host-wlegl-smoke: control color=%d\n",
                    app.color_index);
            fflush(stderr);
            draw(&app);
        }
        wl_display_dispatch_pending(app.display);
        wl_display_flush(app.display);
        usleep(100000);
    }

    fprintf(stderr, "host-wlegl-smoke: exited frame=%d color=%d\n", app.frame,
            app.color_index);
    fflush(stderr);
    return 0;
}
