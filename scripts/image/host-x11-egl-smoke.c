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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WIN_W 640
#define WIN_H 320

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
    int use_glx;
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
log_egl_unavailable(const char *what)
{
    EGLint err = eglGetError();
    fprintf(stderr, "host-x11-egl-smoke: %s unavailable egl_error=0x%x %s fallback=glx\n",
            what, err, egl_error_name(err));
    fflush(stderr);
}

static void
draw(struct app *app, const char *mode)
{
    const float *c = colors[app->color_index %
                            (int)(sizeof(colors) / sizeof(colors[0]))];

    glViewport(0, 0, WIN_W, WIN_H);
    glClearColor(c[0], c[1], c[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (app->use_glx) {
        glXSwapBuffers(app->dpy, app->win);
    } else {
        if (!eglSwapBuffers(app->egl_display, app->egl_surface)) {
            log_egl_unavailable("eglSwapBuffers");
            app->running = 0;
            return;
        }
    }
    app->frame++;
    fprintf(stderr,
            "host-x11-egl-smoke: frame=%d mode=%s color=%d rgb=%.2f,%.2f,%.2f\n",
            app->frame, mode, app->color_index, c[0], c[1], c[2]);
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
    fflush(stderr);
    return 0;
}

static int
setup_x11(struct app *app)
{
    app->dpy = XOpenDisplay(NULL);
    if (!app->dpy) {
        log_line("host-x11-egl-smoke: XOpenDisplay failed status=FAIL");
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
    EGLConfig config;
    EGLint num_configs = 0;
    EGLint major = 0;
    EGLint minor = 0;

    app->egl_display = eglGetDisplay((EGLNativeDisplayType)app->dpy);
    if (app->egl_display == EGL_NO_DISPLAY) {
        log_egl_unavailable("eglGetDisplay");
        return -1;
    }
    if (!eglInitialize(app->egl_display, &major, &minor)) {
        log_egl_unavailable("eglInitialize");
        return -1;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        log_egl_unavailable("eglBindAPI");
        return -1;
    }
    if (!eglChooseConfig(app->egl_display, config_attrs, &config, 1,
                         &num_configs) || num_configs < 1) {
        log_egl_unavailable("eglChooseConfig");
        return -1;
    }
    app->egl_context = eglCreateContext(app->egl_display, config,
                                        EGL_NO_CONTEXT, context_attrs);
    if (app->egl_context == EGL_NO_CONTEXT) {
        log_egl_unavailable("eglCreateContext");
        return -1;
    }
    app->egl_surface = eglCreateWindowSurface(
        app->egl_display, config, (EGLNativeWindowType)app->win, NULL);
    if (app->egl_surface == EGL_NO_SURFACE) {
        log_egl_unavailable("eglCreateWindowSurface");
        return -1;
    }
    if (!eglMakeCurrent(app->egl_display, app->egl_surface, app->egl_surface,
                        app->egl_context)) {
        log_egl_unavailable("eglMakeCurrent");
        return -1;
    }

    fprintf(stderr,
            "host-x11-egl-smoke: egl ready version=%d.%d vendor=%s renderer=%s gl_version=%s\n",
            major, minor,
            eglQueryString(app->egl_display, EGL_VENDOR),
            glGetString(GL_RENDERER),
            glGetString(GL_VERSION));
    fflush(stderr);
    return 0;
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

    destroy_window_only(app);
    app->visual = glXChooseVisual(app->dpy, app->screen, glx_attrs);
    if (!app->visual) {
        log_line("host-x11-egl-smoke: glx_choose_visual missing status=FAIL");
        return -1;
    }

    memset(&attrs, 0, sizeof(attrs));
    app->colormap = XCreateColormap(app->dpy,
                                    RootWindow(app->dpy, app->screen),
                                    app->visual->visual, AllocNone);
    attrs.colormap = app->colormap;
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                       ButtonPressMask;
    app->win = XCreateWindow(app->dpy, RootWindow(app->dpy, app->screen),
                             250, 160, WIN_W, WIN_H, 0,
                             app->visual->depth, InputOutput,
                             app->visual->visual, CWColormap | CWEventMask,
                             &attrs);
    if (!app->win) {
        log_line("host-x11-egl-smoke: glx_window_create missing status=FAIL");
        return -1;
    }
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
    XMapWindow(app->dpy, app->win);
    XFlush(app->dpy);
    fprintf(stderr,
            "host-x11-egl-smoke: glx_window display=%s screen=%d window=0x%lx size=%dx%d\n",
            DisplayString(app->dpy), app->screen, app->win, WIN_W, WIN_H);
    fflush(stderr);

    if (!glXQueryVersion(app->dpy, &major, &minor)) {
        log_line("host-x11-egl-smoke: glx_query_version missing status=FAIL");
        return -1;
    }
    app->glx_context = glXCreateContext(app->dpy, app->visual, NULL, True);
    if (!app->glx_context) {
        log_line("host-x11-egl-smoke: glx_create_context missing status=FAIL");
        return -1;
    }
    if (!glXMakeCurrent(app->dpy, app->win, app->glx_context)) {
        log_line("host-x11-egl-smoke: glx_make_current missing status=FAIL");
        return -1;
    }
    app->use_glx = 1;
    fprintf(stderr,
            "host-x11-egl-smoke: glx ready version=%d.%d vendor=%s renderer=%s gl_version=%s\n",
            major, minor, glGetString(GL_VENDOR), glGetString(GL_RENDERER),
            glGetString(GL_VERSION));
    fflush(stderr);
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

int
main(void)
{
    struct app app;

    memset(&app, 0, sizeof(app));
    app.egl_display = EGL_NO_DISPLAY;
    app.egl_context = EGL_NO_CONTEXT;
    app.egl_surface = EGL_NO_SURFACE;
    app.running = 1;

    log_line("host-x11-egl-smoke: start");
    if (setup_x11(&app) != 0) {
        cleanup(&app);
        return 1;
    }
    if (setup_egl(&app) != 0 && setup_glx(&app) != 0) {
        cleanup(&app);
        return 1;
    }
    draw(&app, "launch");

    while (app.running) {
        XEvent ev;
        XNextEvent(app.dpy, &ev);
        handle_event(&app, &ev);
    }

    cleanup(&app);
    log_line("host-x11-egl-smoke: exited");
    return 0;
}
