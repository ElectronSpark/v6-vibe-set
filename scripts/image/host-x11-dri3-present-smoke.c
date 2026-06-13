// Tiny host-built X11 DRI3/Present ABI probe for the imported-GUI lane.
//
// This avoids browser/toolkit policy and targets the kernel-facing ABI:
// Xwayland extension discovery, DRI3 DRM fd passing over the X11 AF_UNIX
// connection, DRM ioctl validation on that fd, Present completion events,
// visible pixmap presentation, key input, and WM_DELETE exit.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <drm/drm.h>

typedef uint32_t xcb_present_event_t;
typedef uint32_t xcb_xfixes_region_t;
typedef uint32_t xcb_randr_crtc_t;
typedef uint32_t xcb_sync_fence_t;

typedef struct {
    xcb_window_t window;
    xcb_pixmap_t pixmap;
    uint32_t serial;
    xcb_xfixes_region_t valid;
    xcb_xfixes_region_t update;
    int16_t x_off;
    int16_t y_off;
    xcb_randr_crtc_t target_crtc;
    xcb_sync_fence_t wait_fence;
    xcb_sync_fence_t idle_fence;
    uint32_t options;
    uint64_t target_msc;
    uint64_t divisor;
    uint64_t remainder;
    uint32_t notifies_len;
    const void *notifies;
} xcb_present_pixmap_args_t;

typedef struct {
    unsigned int sequence;
} xcb_dri3_query_version_cookie_t;

typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    uint32_t length;
    uint32_t major_version;
    uint32_t minor_version;
} xcb_dri3_query_version_reply_t;

typedef struct {
    unsigned int sequence;
} xcb_dri3_open_cookie_t;

typedef struct {
    uint8_t response_type;
    uint8_t nfd;
    uint16_t sequence;
    uint32_t length;
    uint8_t pad0[24];
} xcb_dri3_open_reply_t;

typedef struct {
    unsigned int sequence;
} xcb_present_query_version_cookie_t;

typedef struct {
    uint8_t response_type;
    uint8_t pad0;
    uint16_t sequence;
    uint32_t length;
    uint32_t major_version;
    uint32_t minor_version;
} xcb_present_query_version_reply_t;

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
    uint32_t full_sequence;
    uint64_t msc;
} xcb_present_complete_notify_event_t;

extern xcb_dri3_query_version_cookie_t xcb_dri3_query_version(
    xcb_connection_t *c, uint32_t major_version, uint32_t minor_version);
extern xcb_dri3_query_version_reply_t *xcb_dri3_query_version_reply(
    xcb_connection_t *c, xcb_dri3_query_version_cookie_t cookie,
    xcb_generic_error_t **e);
extern xcb_dri3_open_cookie_t xcb_dri3_open(
    xcb_connection_t *c, xcb_drawable_t drawable, uint32_t provider);
extern xcb_dri3_open_reply_t *xcb_dri3_open_reply(
    xcb_connection_t *c, xcb_dri3_open_cookie_t cookie,
    xcb_generic_error_t **e);
extern int *xcb_dri3_open_reply_fds(xcb_connection_t *c,
                                    xcb_dri3_open_reply_t *reply);

extern xcb_present_query_version_cookie_t xcb_present_query_version(
    xcb_connection_t *c, uint32_t major_version, uint32_t minor_version);
extern xcb_present_query_version_reply_t *xcb_present_query_version_reply(
    xcb_connection_t *c, xcb_present_query_version_cookie_t cookie,
    xcb_generic_error_t **e);
extern xcb_void_cookie_t xcb_present_select_input_checked(
    xcb_connection_t *c, xcb_present_event_t eid, xcb_window_t window,
    uint32_t event_mask);
extern xcb_void_cookie_t xcb_present_pixmap_checked(
    xcb_connection_t *c, xcb_window_t window, xcb_pixmap_t pixmap,
    uint32_t serial, xcb_xfixes_region_t valid, xcb_xfixes_region_t update,
    int16_t x_off, int16_t y_off, xcb_randr_crtc_t target_crtc,
    xcb_sync_fence_t wait_fence, xcb_sync_fence_t idle_fence,
    uint32_t options, uint64_t target_msc, uint64_t divisor,
    uint64_t remainder, uint32_t notifies_len, const void *notifies);

enum {
    WIN_X = 250,
    WIN_Y = 160,
    WIN_W = 640,
    WIN_H = 320,
    MAX_HELD_PIXMAPS = 8,
    PRESENT_EVENT_MASK_COMPLETE_NOTIFY = 2,
    PRESENT_COMPLETE_KIND_PIXMAP = 0,
};

struct app {
    xcb_connection_t *c;
    xcb_screen_t *screen;
    xcb_window_t win;
    xcb_gcontext_t gc;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_present_event_t present_eid;
    uint8_t present_major_opcode;
    uint32_t serial;
    uint32_t completed_serial;
    xcb_pixmap_t held_pixmaps[MAX_HELD_PIXMAPS];
    size_t held_pixmap_count;
    int mapped;
    int configured;
    int key_presses;
    int done;
};

static void
log_line(const char *line)
{
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
}

static xcb_atom_t
intern_atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_cookie_t cookie;
    xcb_intern_atom_reply_t *reply;
    xcb_atom_t atom = XCB_ATOM_NONE;

    cookie = xcb_intern_atom(c, 0, (uint16_t)strlen(name), name);
    reply = xcb_intern_atom_reply(c, cookie, NULL);
    if (reply) {
        atom = reply->atom;
        free(reply);
    }
    return atom;
}

static void
set_text_property(struct app *app, xcb_atom_t property, xcb_atom_t type,
                  const char *value)
{
    xcb_change_property(app->c, XCB_PROP_MODE_REPLACE, app->win, property,
                        type, 8, (uint32_t)strlen(value), value);
}

static void
set_fg(struct app *app, uint32_t color)
{
    xcb_change_gc(app->c, app->gc, XCB_GC_FOREGROUND, &color);
}

static int
query_extension(struct app *app, const char *name, uint8_t *major_opcode)
{
    xcb_query_extension_cookie_t cookie;
    xcb_query_extension_reply_t *reply;

    cookie = xcb_query_extension(app->c, (uint16_t)strlen(name), name);
    reply = xcb_query_extension_reply(app->c, cookie, NULL);
    if (!reply) {
        fprintf(stderr, "host-x11-dri3-present-smoke: %s_query failed\n",
                name);
        fflush(stderr);
        return -1;
    }

    fprintf(stderr,
            "host-x11-dri3-present-smoke: extension name=%s present=%u major_opcode=%u first_event=%u first_error=%u\n",
            name, reply->present, reply->major_opcode, reply->first_event,
            reply->first_error);
    fflush(stderr);
    if (major_opcode)
        *major_opcode = reply->major_opcode;
    int present = reply->present != 0;
    free(reply);
    return present ? 0 : -1;
}

static int
query_protocol_versions(struct app *app)
{
    xcb_generic_error_t *err = NULL;
    xcb_dri3_query_version_reply_t *dri3;
    xcb_present_query_version_reply_t *present;

    dri3 = xcb_dri3_query_version_reply(
        app->c, xcb_dri3_query_version(app->c, 1, 2), &err);
    if (!dri3 || err) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: dri3_version status=FAIL error=%u\n",
                err ? err->error_code : 0);
        fflush(stderr);
        free(err);
        free(dri3);
        return -1;
    }
    fprintf(stderr,
            "host-x11-dri3-present-smoke: dri3_version major=%u minor=%u\n",
            dri3->major_version, dri3->minor_version);
    fflush(stderr);
    free(dri3);

    present = xcb_present_query_version_reply(
        app->c, xcb_present_query_version(app->c, 1, 2), &err);
    if (!present || err) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: present_version status=FAIL error=%u\n",
                err ? err->error_code : 0);
        fflush(stderr);
        free(err);
        free(present);
        return -1;
    }
    fprintf(stderr,
            "host-x11-dri3-present-smoke: present_version major=%u minor=%u\n",
            present->major_version, present->minor_version);
    fflush(stderr);
    free(present);
    return 0;
}

static int
validate_dri3_fd(struct app *app)
{
    xcb_generic_error_t *err = NULL;
    xcb_dri3_open_reply_t *reply;
    int *fds;
    int fd = -1;
    struct stat st;
    struct drm_version ver;
    char name[64];
    char date[64];
    char desc[128];

    reply = xcb_dri3_open_reply(app->c, xcb_dri3_open(app->c, app->win, 0),
                                &err);
    if (!reply || err) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: dri3_open status=FAIL error=%u\n",
                err ? err->error_code : 0);
        fflush(stderr);
        free(err);
        free(reply);
        return -1;
    }
    fds = xcb_dri3_open_reply_fds(app->c, reply);
    if (!fds || reply->nfd < 1) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: dri3_open no_fd nfd=%u status=FAIL\n",
                reply->nfd);
        fflush(stderr);
        free(reply);
        return -1;
    }

    fd = fds[0];
    if (fstat(fd, &st) != 0) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: dri3_fd fstat_errno=%d status=FAIL\n",
                errno);
        fflush(stderr);
        close(fd);
        free(reply);
        return -1;
    }

    memset(&ver, 0, sizeof(ver));
    memset(name, 0, sizeof(name));
    memset(date, 0, sizeof(date));
    memset(desc, 0, sizeof(desc));
    ver.name_len = sizeof(name) - 1;
    ver.name = name;
    ver.date_len = sizeof(date) - 1;
    ver.date = date;
    ver.desc_len = sizeof(desc) - 1;
    ver.desc = desc;

    if (ioctl(fd, DRM_IOCTL_VERSION, &ver) != 0) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: dri3_fd drm_version_errno=%d mode=%o rdev=%llu status=FAIL\n",
                errno, (unsigned int)st.st_mode,
                (unsigned long long)st.st_rdev);
        fflush(stderr);
        close(fd);
        free(reply);
        return -1;
    }

    fprintf(stderr,
            "host-x11-dri3-present-smoke: dri3_open_fd status=PASS nfd=%u fd=%d mode=%o rdev=%llu drm_name=%s version=%d.%d.%d\n",
            reply->nfd, fd, (unsigned int)st.st_mode,
            (unsigned long long)st.st_rdev, name, ver.version_major,
            ver.version_minor, ver.version_patchlevel);
    fflush(stderr);
    close(fd);
    free(reply);
    return 0;
}

static void
hold_pixmap(struct app *app, xcb_pixmap_t pixmap)
{
    if (app->held_pixmap_count < MAX_HELD_PIXMAPS) {
        app->held_pixmaps[app->held_pixmap_count++] = pixmap;
        return;
    }
    xcb_free_pixmap(app->c, app->held_pixmaps[0]);
    memmove(&app->held_pixmaps[0], &app->held_pixmaps[1],
            sizeof(app->held_pixmaps[0]) * (MAX_HELD_PIXMAPS - 1));
    app->held_pixmaps[MAX_HELD_PIXMAPS - 1] = pixmap;
}

static void
present_scene(struct app *app, int input_mode)
{
    xcb_pixmap_t pixmap;
    xcb_rectangle_t rect;
    xcb_generic_error_t *err;
    xcb_void_cookie_t cookie;
    const char *label = input_mode ? "DRI3 PRESENT INPUT" : "DRI3 PRESENT";

    pixmap = xcb_generate_id(app->c);
    xcb_create_pixmap(app->c, app->screen->root_depth, pixmap, app->win,
                      WIN_W, WIN_H);

    set_fg(app, input_mode ? 0x122f55 : 0x18341f);
    rect.x = 0;
    rect.y = 0;
    rect.width = WIN_W;
    rect.height = WIN_H;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, input_mode ? 0x42c6ff : 0x87d66b);
    rect.x = 34;
    rect.y = 42;
    rect.width = input_mode ? 560 : 470;
    rect.height = 96;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, input_mode ? 0xf2c94c : 0x56ccf2);
    rect.x = 34;
    rect.y = 182;
    rect.width = input_mode ? 430 : 310;
    rect.height = 78;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, 0xffffff);
    xcb_image_text_8(app->c, (uint8_t)strlen(label), pixmap, app->gc,
                     48, 300, label);

    app->serial++;
    cookie = xcb_present_pixmap_checked(app->c, app->win, pixmap, app->serial,
                                        0, 0, 0, 0, 0, 0, 0, 0,
                                        0, 0, 0, 0, NULL);
    err = xcb_request_check(app->c, cookie);
    if (err) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: present_pixmap serial=%u error=%u status=FAIL\n",
                app->serial, err->error_code);
        fflush(stderr);
        free(err);
        xcb_free_pixmap(app->c, pixmap);
        return;
    }

    hold_pixmap(app, pixmap);
    fprintf(stderr,
            "host-x11-dri3-present-smoke: present_pixmap mode=%s serial=%u\n",
            input_mode ? "input" : "launch", app->serial);
    fflush(stderr);
    xcb_flush(app->c);
}

static void
send_wm_delete(struct app *app)
{
    xcb_client_message_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = app->win;
    ev.type = app->wm_protocols;
    ev.data.data32[0] = app->wm_delete_window;
    ev.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(app->c, 0, app->win, XCB_EVENT_MASK_NO_EVENT,
                   (const char *)&ev);
    xcb_flush(app->c);
    log_line("host-x11-dri3-present-smoke: wm_delete_sent");
}

static int
setup(struct app *app)
{
    xcb_screen_iterator_t iter;
    uint32_t values[3];
    uint32_t mask;
    xcb_atom_t net_wm_name;
    xcb_atom_t utf8_string;
    xcb_generic_error_t *err;

    memset(app, 0, sizeof(*app));
    app->c = xcb_connect(NULL, NULL);
    if (!app->c || xcb_connection_has_error(app->c)) {
        log_line("host-x11-dri3-present-smoke: connect failed");
        return -1;
    }

    iter = xcb_setup_roots_iterator(xcb_get_setup(app->c));
    app->screen = iter.data;
    if (!app->screen) {
        log_line("host-x11-dri3-present-smoke: no screen");
        return -1;
    }

    fprintf(stderr,
            "host-x11-dri3-present-smoke: connected root=0x%x size=%ux%u depth=%u\n",
            app->screen->root, app->screen->width_in_pixels,
            app->screen->height_in_pixels, app->screen->root_depth);
    fflush(stderr);

    if (query_extension(app, "DRI3", NULL) < 0)
        return -1;
    if (query_extension(app, "Present", &app->present_major_opcode) < 0)
        return -1;
    if (query_protocol_versions(app) < 0)
        return -1;

    app->wm_protocols = intern_atom(app->c, "WM_PROTOCOLS");
    app->wm_delete_window = intern_atom(app->c, "WM_DELETE_WINDOW");
    utf8_string = intern_atom(app->c, "UTF8_STRING");
    net_wm_name = intern_atom(app->c, "_NET_WM_NAME");
    if (!app->wm_protocols || !app->wm_delete_window ||
        !utf8_string || !net_wm_name) {
        log_line("host-x11-dri3-present-smoke: atom setup failed");
        return -1;
    }

    app->win = xcb_generate_id(app->c);
    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = 0x101010;
    values[1] = XCB_EVENT_MASK_EXPOSURE |
                XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(app->c, app->screen->root_depth, app->win,
                      app->screen->root, WIN_X, WIN_Y, WIN_W - 80,
                      WIN_H - 50, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      app->screen->root_visual, mask, values);

    app->gc = xcb_generate_id(app->c);
    values[0] = 0xffffff;
    values[1] = 0x101010;
    xcb_create_gc(app->c, app->gc, app->win,
                  XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, values);

    set_text_property(app, XCB_ATOM_WM_NAME, XCB_ATOM_STRING,
                      "XV6-X11-DRI3-PRESENT-PROOF");
    set_text_property(app, net_wm_name, utf8_string,
                      "XV6-X11-DRI3-PRESENT-PROOF");
    xcb_change_property(app->c, XCB_PROP_MODE_REPLACE, app->win,
                        app->wm_protocols, XCB_ATOM_ATOM, 32, 1,
                        &app->wm_delete_window);

    app->present_eid = xcb_generate_id(app->c);
    err = xcb_request_check(app->c,
                            xcb_present_select_input_checked(
                                app->c, app->present_eid, app->win,
                                PRESENT_EVENT_MASK_COMPLETE_NOTIFY));
    if (err) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: present_select_input error=%u status=FAIL\n",
                err->error_code);
        fflush(stderr);
        free(err);
        return -1;
    }
    log_line("host-x11-dri3-present-smoke: present_select_input status=PASS");

    xcb_map_window(app->c, app->win);
    values[0] = WIN_X;
    values[1] = WIN_Y;
    values[2] = WIN_W;
    xcb_configure_window(app->c, app->win,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                         XCB_CONFIG_WINDOW_WIDTH,
                         values);
    values[0] = WIN_H;
    xcb_configure_window(app->c, app->win, XCB_CONFIG_WINDOW_HEIGHT, values);
    xcb_flush(app->c);
    log_line("host-x11-dri3-present-smoke: setup complete");
    return 0;
}

static void
cleanup(struct app *app)
{
    if (app->c) {
        for (size_t i = 0; i < app->held_pixmap_count; i++)
            xcb_free_pixmap(app->c, app->held_pixmaps[i]);
        if (app->gc)
            xcb_free_gc(app->c, app->gc);
        if (app->win)
            xcb_destroy_window(app->c, app->win);
        xcb_flush(app->c);
        xcb_disconnect(app->c);
    }
}

static void
handle_present_event(struct app *app, xcb_ge_generic_event_t *ge)
{
    if (ge->extension != app->present_major_opcode ||
        ge->event_type != 1)
        return;

    xcb_present_complete_notify_event_t *ev =
        (xcb_present_complete_notify_event_t *)ge;
    if (ev->kind == PRESENT_COMPLETE_KIND_PIXMAP) {
        app->completed_serial = ev->serial;
        fprintf(stderr,
                "host-x11-dri3-present-smoke: present_complete serial=%u mode=%u msc=%llu\n",
                ev->serial, ev->mode, (unsigned long long)ev->msc);
        fflush(stderr);
    }
}

static void
handle_event(struct app *app, xcb_generic_event_t *event)
{
    uint8_t type = event->response_type & ~0x80;

    switch (type) {
    case XCB_GE_GENERIC:
        handle_present_event(app, (xcb_ge_generic_event_t *)event);
        break;
    case XCB_EXPOSE:
        present_scene(app, app->key_presses > 0);
        break;
    case XCB_MAP_NOTIFY:
        app->mapped = 1;
        log_line("host-x11-dri3-present-smoke: map_notify");
        if (validate_dri3_fd(app) == 0)
            present_scene(app, 0);
        break;
    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t *ev =
            (xcb_configure_notify_event_t *)event;
        app->configured = 1;
        fprintf(stderr,
                "host-x11-dri3-present-smoke: configure_notify x=%d y=%d w=%u h=%u\n",
                ev->x, ev->y, ev->width, ev->height);
        fflush(stderr);
        break;
    }
    case XCB_BUTTON_PRESS:
        xcb_set_input_focus(app->c, XCB_INPUT_FOCUS_POINTER_ROOT, app->win,
                            XCB_CURRENT_TIME);
        xcb_flush(app->c);
        log_line("host-x11-dri3-present-smoke: button_press focus");
        break;
    case XCB_KEY_PRESS: {
        xcb_key_press_event_t *ev = (xcb_key_press_event_t *)event;
        app->key_presses++;
        fprintf(stderr,
                "host-x11-dri3-present-smoke: key_press keycode=%u state=0x%x count=%d\n",
                ev->detail, ev->state, app->key_presses);
        fflush(stderr);
        if (ev->detail == 9) {
            send_wm_delete(app);
        } else {
            present_scene(app, 1);
            log_line("host-x11-dri3-present-smoke: input_ready");
        }
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *ev = (xcb_client_message_event_t *)event;
        if (ev->type == app->wm_protocols &&
            ev->data.data32[0] == app->wm_delete_window) {
            log_line("host-x11-dri3-present-smoke: wm_delete_received");
            app->done = 1;
        }
        break;
    }
    default:
        break;
    }
}

int
main(void)
{
    struct app app;
    xcb_generic_event_t *event;

    log_line("host-x11-dri3-present-smoke: start");
    if (setup(&app) < 0) {
        cleanup(&app);
        return 2;
    }

    while (!app.done) {
        event = xcb_wait_for_event(app.c);
        if (!event) {
            log_line("host-x11-dri3-present-smoke: event eof");
            cleanup(&app);
            return 3;
        }
        handle_event(&app, event);
        free(event);
    }

    cleanup(&app);
    log_line("host-x11-dri3-present-smoke: exited");
    return 0;
}
