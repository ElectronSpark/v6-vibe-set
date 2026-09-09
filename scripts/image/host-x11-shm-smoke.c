// Tiny host-built X11/MIT-SHM ABI probe for the imported-GUI proof lane.
//
// This intentionally uses the conventional Xlib XShm path rather than a
// toolkit.  It proves the Linux-facing pieces real X11 toolkits depend on:
// SysV shmget/shmat/shmctl IPC_RMID-while-attached semantics, Xwayland
// MIT-SHM attach, shared-memory image upload, key input, and WM_DELETE exit.

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shm.h>

typedef struct {
    XID shmseg;
    int shmid;
    char *shmaddr;
    Bool readOnly;
} XShmSegmentInfo;

extern Bool XShmQueryExtension(Display *display);
extern Status XShmQueryVersion(Display *display, int *major, int *minor,
                               Bool *pixmaps);
extern XImage *XShmCreateImage(Display *display, Visual *visual,
                               unsigned int depth, int format, char *data,
                               XShmSegmentInfo *shminfo, unsigned int width,
                               unsigned int height);
extern Status XShmAttach(Display *display, XShmSegmentInfo *shminfo);
extern Status XShmDetach(Display *display, XShmSegmentInfo *shminfo);
extern Status XShmPutImage(Display *display, Drawable d, GC gc,
                           XImage *image, int src_x, int src_y, int dst_x,
                           int dst_y, unsigned int width,
                           unsigned int height, Bool send_event);

enum {
    WIN_X = 260,
    WIN_Y = 170,
    WIN_W = 640,
    WIN_H = 300,
};

struct app {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    Atom wm_protocols;
    Atom wm_delete_window;
    XImage *image;
    XShmSegmentInfo shminfo;
    int shm_marked_removed;
    int delay_rmid;
    int key_presses;
    int done;
};

static int x_error_seen;
static int x_error_code;
static int x_error_request;
static int x_error_minor;
static unsigned long x_error_resource;

static void
log_line(const char *line)
{
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
}

static int
x_error_handler(Display *display, XErrorEvent *ev)
{
    (void)display;
    x_error_seen = 1;
    x_error_code = ev->error_code;
    x_error_request = ev->request_code;
    x_error_minor = ev->minor_code;
    x_error_resource = ev->resourceid;
    return 0;
}

static void
set_net_wm_name(struct app *app, const char *name)
{
    Atom net_wm_name = XInternAtom(app->dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(app->dpy, "UTF8_STRING", False);

    if (net_wm_name != None && utf8 != None)
        XChangeProperty(app->dpy, app->win, net_wm_name, utf8, 8,
                        PropModeReplace, (const unsigned char *)name,
                        (int)strlen(name));
}

static unsigned long
rgb_pixel(int input_mode, int x, int y)
{
    unsigned int r = input_mode ? 0x12 : 0x18;
    unsigned int g = input_mode ? 0x2f : 0x2c;
    unsigned int b = input_mode ? 0x55 : 0x22;

    if (x >= 34 && x < (input_mode ? 570 : 470) &&
        y >= 42 && y < 130) {
        r = input_mode ? 0x42 : 0x87;
        g = input_mode ? 0xc6 : 0xd6;
        b = input_mode ? 0xff : 0x6b;
    } else if (x >= 34 && x < (input_mode ? 450 : 324) &&
               y >= 166 && y < 236) {
        r = input_mode ? 0xf2 : 0x56;
        g = input_mode ? 0xc9 : 0xcc;
        b = input_mode ? 0x4c : 0xf2;
    } else if (((x / 24) + (y / 24)) % 7 == 0) {
        r += input_mode ? 0x18 : 0x10;
        g += input_mode ? 0x20 : 0x18;
        b += input_mode ? 0x10 : 0x20;
    }
    return ((unsigned long)r << 16) | ((unsigned long)g << 8) | b;
}

static void
fill_image(struct app *app, int input_mode)
{
    for (int y = 0; y < app->image->height; y++) {
        for (int x = 0; x < app->image->width; x++)
            XPutPixel(app->image, x, y, rgb_pixel(input_mode, x, y));
    }
}

static void
draw_scene(struct app *app, int input_mode)
{
    fill_image(app, input_mode);
    x_error_seen = 0;
    XShmPutImage(app->dpy, app->win, app->gc, app->image, 0, 0, 0, 0,
                 WIN_W, WIN_H, False);
    XSync(app->dpy, False);
    if (x_error_seen) {
        fprintf(stderr,
                "host-x11-shm-smoke: xshm_put_image xerror=%d request=%d minor=%d resource=0x%lx status=FAIL\n",
                x_error_code, x_error_request, x_error_minor,
                x_error_resource);
        fflush(stderr);
        return;
    }

    fprintf(stderr, "host-x11-shm-smoke: xshm_put_image mode=%s\n",
            input_mode ? "input" : "launch");
    fflush(stderr);
    if (app->delay_rmid && !app->shm_marked_removed) {
        if (shmctl(app->shminfo.shmid, IPC_RMID, NULL) != 0) {
            fprintf(stderr,
                    "host-x11-shm-smoke: ipc_rmid_marked errno=%d status=FAIL\n",
                    errno);
            fflush(stderr);
            return;
        }
        app->shm_marked_removed = 1;
        log_line("host-x11-shm-smoke: ipc_rmid_marked status=PASS");
    }
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
    ev.xclient.data.l[0] = (long)app->wm_delete_window;
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(app->dpy, app->win, False, NoEventMask, &ev);
    XFlush(app->dpy);
    log_line("host-x11-shm-smoke: wm_delete_sent");
}

static int
setup_shm_image(struct app *app)
{
    int major = 0;
    int minor = 0;
    Bool pixmaps = False;
    size_t bytes;

    if (!XShmQueryExtension(app->dpy)) {
        log_line("host-x11-shm-smoke: xshm_extension missing");
        return -1;
    }
    if (!XShmQueryVersion(app->dpy, &major, &minor, &pixmaps)) {
        log_line("host-x11-shm-smoke: xshm_version failed");
        return -1;
    }
    fprintf(stderr,
            "host-x11-shm-smoke: xshm_version major=%d minor=%d pixmaps=%d\n",
            major, minor, pixmaps ? 1 : 0);
    fflush(stderr);

    app->image = XShmCreateImage(app->dpy, DefaultVisual(app->dpy, app->screen),
                                 (unsigned int)DefaultDepth(app->dpy,
                                                            app->screen),
                                 ZPixmap, NULL, &app->shminfo, WIN_W, WIN_H);
    if (!app->image) {
        log_line("host-x11-shm-smoke: xshm_create_image failed");
        return -1;
    }

    bytes = (size_t)app->image->bytes_per_line * (size_t)app->image->height;
    app->shminfo.shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
    if (app->shminfo.shmid < 0) {
        fprintf(stderr, "host-x11-shm-smoke: shmget failed errno=%d\n", errno);
        fflush(stderr);
        return -1;
    }
    fprintf(stderr, "host-x11-shm-smoke: shmget shmid=%d bytes=%zu\n",
            app->shminfo.shmid, bytes);
    fflush(stderr);

    app->shminfo.shmaddr = shmat(app->shminfo.shmid, NULL, 0);
    if (app->shminfo.shmaddr == (char *)-1) {
        fprintf(stderr, "host-x11-shm-smoke: shmat failed errno=%d\n", errno);
        fflush(stderr);
        return -1;
    }
    app->shminfo.readOnly = False;
    app->image->data = app->shminfo.shmaddr;
    memset(app->image->data, 0, bytes);
    fprintf(stderr, "host-x11-shm-smoke: shmat addr=%p\n",
            app->shminfo.shmaddr);
    fflush(stderr);
    fprintf(stderr,
            "host-x11-shm-smoke: image width=%d height=%d depth=%d format=%d bpp=%d bytes_per_line=%d bytes=%zu\n",
            app->image->width, app->image->height, app->image->depth,
            app->image->format, app->image->bits_per_pixel,
            app->image->bytes_per_line, bytes);
    fflush(stderr);

    x_error_seen = 0;
    if (!XShmAttach(app->dpy, &app->shminfo)) {
        log_line("host-x11-shm-smoke: xshm_attach returned false");
        return -1;
    }
    XSync(app->dpy, False);
    if (x_error_seen) {
        fprintf(stderr,
                "host-x11-shm-smoke: xshm_attach xerror=%d request=%d\n",
                x_error_code, x_error_request);
        fflush(stderr);
        return -1;
    }
    log_line("host-x11-shm-smoke: xshm_attach status=PASS");
    fprintf(stderr, "host-x11-shm-smoke: xshm_segment xid=0x%lx shmid=%d\n",
            app->shminfo.shmseg, app->shminfo.shmid);
    fflush(stderr);

    if (app->delay_rmid) {
        log_line("host-x11-shm-smoke: ipc_rmid_deferred status=PASS");
        return 0;
    }

    if (shmctl(app->shminfo.shmid, IPC_RMID, NULL) != 0) {
        fprintf(stderr,
                "host-x11-shm-smoke: ipc_rmid_marked status=FAIL errno=%d\n",
                errno);
        fflush(stderr);
        return -1;
    }
    app->shm_marked_removed = 1;
    log_line("host-x11-shm-smoke: ipc_rmid_marked status=PASS");
    return 0;
}

static int
setup(struct app *app)
{
    XSetWindowAttributes attrs;
    const char *title = "XV6-X11-SHM-PROOF";

    memset(app, 0, sizeof(*app));
    app->shminfo.shmid = -1;
    app->delay_rmid = getenv("HOST_X11_SHM_DELAY_RMID") != NULL;
    app->dpy = XOpenDisplay(NULL);
    if (!app->dpy) {
        log_line("host-x11-shm-smoke: connect failed");
        return -1;
    }
    XSetErrorHandler(x_error_handler);
    app->screen = DefaultScreen(app->dpy);
    fprintf(stderr,
            "host-x11-shm-smoke: connected root=0x%lx size=%dx%d depth=%d\n",
            RootWindow(app->dpy, app->screen),
            DisplayWidth(app->dpy, app->screen),
            DisplayHeight(app->dpy, app->screen),
            DefaultDepth(app->dpy, app->screen));
    fflush(stderr);

    attrs.background_pixel = 0x101010;
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                       StructureNotifyMask | PropertyChangeMask;
    app->win = XCreateWindow(app->dpy, RootWindow(app->dpy, app->screen),
                             WIN_X, WIN_Y, WIN_W, WIN_H, 0,
                             CopyFromParent, InputOutput, CopyFromParent,
                             CWBackPixel | CWEventMask, &attrs);
    app->gc = XCreateGC(app->dpy, app->win, 0, NULL);
    XStoreName(app->dpy, app->win, title);
    set_net_wm_name(app, title);

    app->wm_protocols = XInternAtom(app->dpy, "WM_PROTOCOLS", False);
    app->wm_delete_window = XInternAtom(app->dpy, "WM_DELETE_WINDOW", False);
    if (app->wm_protocols == None || app->wm_delete_window == None) {
        log_line("host-x11-shm-smoke: atom setup failed");
        return -1;
    }
    XSetWMProtocols(app->dpy, app->win, &app->wm_delete_window, 1);

    if (setup_shm_image(app) < 0)
        return -1;

    XMapWindow(app->dpy, app->win);
    XMoveResizeWindow(app->dpy, app->win, WIN_X, WIN_Y, WIN_W, WIN_H);
    XFlush(app->dpy);
    log_line("host-x11-shm-smoke: setup complete");
    return 0;
}

static void
cleanup(struct app *app)
{
    if (app->dpy && app->image) {
        XShmDetach(app->dpy, &app->shminfo);
        XSync(app->dpy, False);
    }
    if (app->shminfo.shmaddr && app->shminfo.shmaddr != (char *)-1)
        shmdt(app->shminfo.shmaddr);
    if (app->shminfo.shmid >= 0 && !app->shm_marked_removed)
        shmctl(app->shminfo.shmid, IPC_RMID, NULL);
    if (app->image)
        XDestroyImage(app->image);
    if (app->dpy && app->win)
        XDestroyWindow(app->dpy, app->win);
    if (app->dpy && app->gc)
        XFreeGC(app->dpy, app->gc);
    if (app->dpy)
        XCloseDisplay(app->dpy);
}

static void
handle_event(struct app *app, XEvent *event)
{
    switch (event->type) {
    case Expose:
        draw_scene(app, app->key_presses > 0);
        break;
    case MapNotify:
        log_line("host-x11-shm-smoke: map_notify");
        draw_scene(app, 0);
        break;
    case ConfigureNotify:
        fprintf(stderr,
                "host-x11-shm-smoke: configure_notify x=%d y=%d w=%d h=%d\n",
                event->xconfigure.x, event->xconfigure.y,
                event->xconfigure.width, event->xconfigure.height);
        fflush(stderr);
        break;
    case ButtonPress:
        XSetInputFocus(app->dpy, app->win, RevertToPointerRoot, CurrentTime);
        XFlush(app->dpy);
        log_line("host-x11-shm-smoke: button_press focus");
        break;
    case KeyPress:
        app->key_presses++;
        fprintf(stderr,
                "host-x11-shm-smoke: key_press keycode=%u state=0x%x count=%d\n",
                event->xkey.keycode, event->xkey.state, app->key_presses);
        fflush(stderr);
        if (event->xkey.keycode == 9) {
            send_wm_delete(app);
        } else {
            draw_scene(app, 1);
            log_line("host-x11-shm-smoke: input_ready");
        }
        break;
    case ClientMessage:
        if ((Atom)event->xclient.message_type == app->wm_protocols &&
            (Atom)event->xclient.data.l[0] == app->wm_delete_window) {
            log_line("host-x11-shm-smoke: wm_delete_received");
            app->done = 1;
        }
        break;
    default:
        break;
    }
}

int
main(void)
{
    struct app app;
    XEvent event;

    log_line("host-x11-shm-smoke: start");
    if (setup(&app) < 0) {
        cleanup(&app);
        return 2;
    }

    while (!app.done) {
        XNextEvent(app.dpy, &event);
        handle_event(&app, &event);
    }

    cleanup(&app);
    log_line("host-x11-shm-smoke: exited");
    return 0;
}
