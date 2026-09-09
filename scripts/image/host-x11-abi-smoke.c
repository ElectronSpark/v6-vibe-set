// Tiny host-built XCB/X11 ABI probe for the imported-GUI proof lane.
//
// The program intentionally avoids toolkit code.  It exercises the Linux/X11
// ABI path that matters to xv6: AF_UNIX/XCB transport, window map/configure,
// properties/selection ownership, pixmap-backed drawing, key input, and a
// WM_DELETE_WINDOW client-message exit.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

enum {
    WIN_X = 220,
    WIN_Y = 160,
    WIN_W = 640,
    WIN_H = 300,
};

struct app {
    xcb_connection_t *c;
    xcb_screen_t *screen;
    xcb_window_t win;
    xcb_gcontext_t gc;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_window;
    xcb_atom_t selection_atom;
    xcb_atom_t utf8_string;
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

static void
draw_scene(struct app *app, int input_mode)
{
    xcb_pixmap_t pixmap;
    xcb_rectangle_t rect;
    const char *label = input_mode ? "X11 ABI INPUT" : "X11 ABI READY";

    pixmap = xcb_generate_id(app->c);
    xcb_create_pixmap(app->c, app->screen->root_depth, pixmap, app->win,
                      WIN_W, WIN_H);

    set_fg(app, input_mode ? 0x14345a : 0x24331f);
    rect.x = 0;
    rect.y = 0;
    rect.width = WIN_W;
    rect.height = WIN_H;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, input_mode ? 0x42c6ff : 0x87d66b);
    rect.x = 34;
    rect.y = 42;
    rect.width = input_mode ? 530 : 430;
    rect.height = 88;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, input_mode ? 0xf2c94c : 0x56ccf2);
    rect.x = 34;
    rect.y = 166;
    rect.width = input_mode ? 420 : 290;
    rect.height = 70;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, 0xffffff);
    xcb_image_text_8(app->c, (uint8_t)strlen(label), pixmap, app->gc,
                     48, 282, label);

    xcb_copy_area(app->c, pixmap, app->win, app->gc, 0, 0, 0, 0, WIN_W, WIN_H);
    xcb_free_pixmap(app->c, pixmap);
    xcb_flush(app->c);

    fprintf(stderr, "host-x11-abi-smoke: pixmap_draw mode=%s\n",
            input_mode ? "input" : "launch");
    fflush(stderr);
}

static void
query_mit_shm(struct app *app)
{
    static const char name[] = "MIT-SHM";
    xcb_query_extension_cookie_t cookie;
    xcb_query_extension_reply_t *reply;

    cookie = xcb_query_extension(app->c, sizeof(name) - 1, name);
    reply = xcb_query_extension_reply(app->c, cookie, NULL);
    if (!reply) {
        log_line("host-x11-abi-smoke: mit_shm_query failed");
        return;
    }

    fprintf(stderr,
            "host-x11-abi-smoke: mit_shm_present=%u major_opcode=%u first_event=%u first_error=%u\n",
            reply->present, reply->major_opcode, reply->first_event,
            reply->first_error);
    fflush(stderr);
    free(reply);
}

static void
claim_selection(struct app *app)
{
    xcb_get_selection_owner_cookie_t cookie;
    xcb_get_selection_owner_reply_t *reply;

    xcb_set_selection_owner(app->c, app->win, app->selection_atom,
                            XCB_CURRENT_TIME);
    cookie = xcb_get_selection_owner(app->c, app->selection_atom);
    xcb_flush(app->c);
    reply = xcb_get_selection_owner_reply(app->c, cookie, NULL);
    if (!reply) {
        log_line("host-x11-abi-smoke: selection_owner failed");
        return;
    }

    fprintf(stderr,
            "host-x11-abi-smoke: selection_owner owner=0x%x expected=0x%x status=%s\n",
            reply->owner, app->win,
            reply->owner == app->win ? "PASS" : "FAIL");
    fflush(stderr);
    free(reply);
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
    log_line("host-x11-abi-smoke: wm_delete_sent");
}

static int
setup(struct app *app)
{
    xcb_screen_iterator_t iter;
    uint32_t values[3];
    uint32_t mask;
    xcb_atom_t net_wm_name;

    memset(app, 0, sizeof(*app));
    app->c = xcb_connect(NULL, NULL);
    if (!app->c || xcb_connection_has_error(app->c)) {
        log_line("host-x11-abi-smoke: connect failed");
        return -1;
    }

    iter = xcb_setup_roots_iterator(xcb_get_setup(app->c));
    app->screen = iter.data;
    if (!app->screen) {
        log_line("host-x11-abi-smoke: no screen");
        return -1;
    }

    fprintf(stderr,
            "host-x11-abi-smoke: connected root=0x%x size=%ux%u depth=%u\n",
            app->screen->root, app->screen->width_in_pixels,
            app->screen->height_in_pixels, app->screen->root_depth);
    fflush(stderr);
    query_mit_shm(app);

    app->wm_protocols = intern_atom(app->c, "WM_PROTOCOLS");
    app->wm_delete_window = intern_atom(app->c, "WM_DELETE_WINDOW");
    app->selection_atom = intern_atom(app->c, "XV6_X11_ABI_SELECTION");
    app->utf8_string = intern_atom(app->c, "UTF8_STRING");
    net_wm_name = intern_atom(app->c, "_NET_WM_NAME");
    if (!app->wm_protocols || !app->wm_delete_window ||
        !app->selection_atom || !app->utf8_string || !net_wm_name) {
        log_line("host-x11-abi-smoke: atom setup failed");
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
                      "XV6-X11-ABI-PROOF");
    set_text_property(app, net_wm_name, app->utf8_string,
                      "XV6-X11-ABI-PROOF");
    xcb_change_property(app->c, XCB_PROP_MODE_REPLACE, app->win,
                        app->wm_protocols, XCB_ATOM_ATOM, 32, 1,
                        &app->wm_delete_window);
    claim_selection(app);

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
    xcb_set_input_focus(app->c, XCB_INPUT_FOCUS_POINTER_ROOT, app->win,
                        XCB_CURRENT_TIME);
    xcb_flush(app->c);
    log_line("host-x11-abi-smoke: setup complete");
    return 0;
}

static void
handle_event(struct app *app, xcb_generic_event_t *generic)
{
    uint8_t type = generic->response_type & ~0x80;

    switch (type) {
    case XCB_EXPOSE:
        draw_scene(app, app->key_presses > 0);
        break;
    case XCB_MAP_NOTIFY:
        app->mapped = 1;
        log_line("host-x11-abi-smoke: map_notify");
        draw_scene(app, 0);
        break;
    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t *ev =
            (xcb_configure_notify_event_t *)generic;
        app->configured = 1;
        fprintf(stderr,
                "host-x11-abi-smoke: configure_notify x=%d y=%d w=%u h=%u\n",
                ev->x, ev->y, ev->width, ev->height);
        fflush(stderr);
        break;
    }
    case XCB_BUTTON_PRESS:
        xcb_set_input_focus(app->c, XCB_INPUT_FOCUS_POINTER_ROOT, app->win,
                            XCB_CURRENT_TIME);
        xcb_flush(app->c);
        log_line("host-x11-abi-smoke: button_press focus");
        break;
    case XCB_KEY_PRESS: {
        xcb_key_press_event_t *ev = (xcb_key_press_event_t *)generic;
        app->key_presses++;
        fprintf(stderr,
                "host-x11-abi-smoke: key_press detail=%u state=0x%x count=%d\n",
                ev->detail, ev->state, app->key_presses);
        fflush(stderr);
        if (ev->detail == 9) {
            send_wm_delete(app);
        } else {
            draw_scene(app, 1);
            log_line("host-x11-abi-smoke: input_ready");
        }
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *ev =
            (xcb_client_message_event_t *)generic;
        if (ev->type == app->wm_protocols &&
            ev->data.data32[0] == app->wm_delete_window) {
            log_line("host-x11-abi-smoke: wm_delete_received");
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

    log_line("host-x11-abi-smoke: start");
    if (setup(&app) < 0)
        return 2;

    while (!app.done) {
        event = xcb_wait_for_event(app.c);
        if (!event) {
            log_line("host-x11-abi-smoke: event eof");
            return 3;
        }
        handle_event(&app, event);
        free(event);
    }

    xcb_destroy_window(app.c, app.win);
    xcb_free_gc(app.c, app.gc);
    xcb_disconnect(app.c);
    log_line("host-x11-abi-smoke: exited");
    return 0;
}
