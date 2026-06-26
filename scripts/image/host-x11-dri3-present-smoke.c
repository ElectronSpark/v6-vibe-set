// Tiny host-built X11 DRI3/Present ABI probe for the imported-GUI lane.
//
// This avoids browser/toolkit policy and targets the kernel-facing ABI:
// Xwayland extension discovery, DRI3 DRM fd passing over the X11 AF_UNIX
// connection, DRM ioctl validation on that fd, Present completion events,
// visible pixmap presentation, key input, and WM_DELETE exit.

#define _GNU_SOURCE
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#define DRM_IOCTL_BASE 'd'

struct drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    size_t name_len;
    char *name;
    size_t date_len;
    char *date;
    size_t desc_len;
    char *desc;
};

#define DRM_IOCTL_VERSION _IOWR(DRM_IOCTL_BASE, 0x00, struct drm_version)

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
extern xcb_void_cookie_t xcb_present_pixmap(
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
    PRESENT_FPS_PIXMAPS = 3,
    PRESENT_FPS_QUEUE3_DEPTH = 3,
    PRESENT_FPS_QUEUE_DEPTH_MAX = 8,
    PRESENT_FPS_MAX_FRAMES = 300,
    PRESENT_FPS_TARGET_SECONDS = 5,
    PRESENT_EVENT_MASK_COMPLETE_NOTIFY = 2,
    PRESENT_COMPLETE_KIND_PIXMAP = 0,
    PRESENT_COMPLETE_MODE_COPY = 0,
    PRESENT_COMPLETE_MODE_FLIP = 1,
    PRESENT_COMPLETE_MODE_SKIP = 2,
    PRESENT_COMPLETE_MODE_SUBOPTIMAL_COPY = 3,
    PRESENT_COMPLETE_NOTIFY_EXTRA_WORDS = 2,
};

enum present_fps_variant {
    PRESENT_FPS_VARIANT_BASELINE,
    PRESENT_FPS_VARIANT_UNCHECKED,
    PRESENT_FPS_VARIANT_QUEUE3_UNCHECKED,
    PRESENT_FPS_VARIANT_QUEUE_UNCHECKED,
};

struct dri3_fd_info {
    int valid;
    unsigned int mode;
    unsigned long long rdev;
    char drm_name[64];
    int version_major;
    int version_minor;
    int version_patchlevel;
};

struct present_fps_stats {
    uint32_t serial_first;
    uint32_t serial_last;
    int issued;
    int completed;
    double present_request_total_ms;
    double request_check_total_ms;
    double flush_total_ms;
    double event_wait_total_ms;
    double completion_total_ms;
    double max_completion_ms;
    uint64_t first_msc;
    uint64_t last_msc;
    int complete_copy;
    int complete_flip;
    int complete_skip;
    int complete_suboptimal_copy;
    int last_complete_mode;
    int queue_depth;
    int max_outstanding;
    int drain_waits;
    double drain_wait_total_ms;
};

struct present_fps_config {
    enum present_fps_variant variant;
    const char *variant_name;
    int request_check_enabled;
    int queue_depth;
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

static double
now_seconds(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static double
elapsed_ms(double start)
{
    return (now_seconds() - start) * 1000.0;
}

static void
copy_log_token(char *dst, size_t dst_size, const char *src)
{
    size_t i;

    if (dst_size == 0)
        return;
    if (!src)
        src = "";
    for (i = 0; i + 1 < dst_size && src[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)src[i];

        dst[i] = (ch <= ' ' || ch == 0x7f) ? '_' : (char)ch;
    }
    dst[i] = '\0';
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
read_present_fps_queue_depth(struct present_fps_config *config)
{
    const char *raw = getenv("HOST_X11_PRESENT_FPS_QUEUE_DEPTH");
    char token[64];
    int depth = 0;

    if (parse_decimal_range(raw, 1, PRESENT_FPS_QUEUE_DEPTH_MAX,
                            &depth)) {
        config->queue_depth = depth;
        return 0;
    }

    copy_log_token(token, sizeof(token), raw);
    fprintf(stderr,
            "host-x11-dri3-present-smoke: phase=present_fps_variant status=FAIL source=env env=HOST_X11_PRESENT_FPS_QUEUE_DEPTH requested=%s reason=invalid-queue-depth min=1 max=%d\n",
            token[0] ? token : "(empty)", PRESENT_FPS_QUEUE_DEPTH_MAX);
    fflush(stderr);
    return -1;
}

static int
present_fps_variant_from_env(struct present_fps_config *config)
{
    const char *value = getenv("HOST_X11_PRESENT_FPS_VARIANT");
    char token[64];

    config->variant = PRESENT_FPS_VARIANT_BASELINE;
    config->variant_name = "baseline";
    config->request_check_enabled = 1;
    config->queue_depth = 1;

    if (!value || value[0] == '\0' || strcmp(value, "baseline") == 0)
        return 0;
    if (strcmp(value, "unchecked") == 0) {
        config->variant = PRESENT_FPS_VARIANT_UNCHECKED;
        config->variant_name = "unchecked";
        config->request_check_enabled = 0;
        return 0;
    }
    if (strcmp(value, "queue3-unchecked") == 0) {
        config->variant = PRESENT_FPS_VARIANT_QUEUE3_UNCHECKED;
        config->variant_name = "queue3-unchecked";
        config->request_check_enabled = 0;
        config->queue_depth = PRESENT_FPS_QUEUE3_DEPTH;
        return 0;
    }
    if (strcmp(value, "queue-unchecked") == 0) {
        config->variant = PRESENT_FPS_VARIANT_QUEUE_UNCHECKED;
        config->variant_name = "queue-unchecked";
        config->request_check_enabled = 0;
        return read_present_fps_queue_depth(config);
    }

    copy_log_token(token, sizeof(token), value);
    fprintf(stderr,
            "host-x11-dri3-present-smoke: phase=present_fps_variant status=FAIL source=env env=HOST_X11_PRESENT_FPS_VARIANT requested=%s reason=invalid-value\n",
            token[0] ? token : "(empty)");
    fflush(stderr);
    return -1;
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
validate_dri3_fd(struct app *app, struct dri3_fd_info *info)
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

    if (info) {
        memset(info, 0, sizeof(*info));
        info->valid = 1;
        info->mode = (unsigned int)st.st_mode;
        info->rdev = (unsigned long long)st.st_rdev;
        snprintf(info->drm_name, sizeof(info->drm_name), "%s", name);
        info->version_major = ver.version_major;
        info->version_minor = ver.version_minor;
        info->version_patchlevel = ver.version_patchlevel;
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
    if (ge->length < PRESENT_COMPLETE_NOTIFY_EXTRA_WORDS) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: present_complete status=FAIL reason=short-event length=%u\n",
                ge->length);
        fflush(stderr);
        return;
    }

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
        if (validate_dri3_fd(app, NULL) == 0)
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

static int
present_fps_wait_initial_window(struct app *app)
{
    double deadline = now_seconds() + 5.0;

    while ((!app->mapped || !app->configured) && now_seconds() < deadline) {
        xcb_generic_event_t *event = xcb_poll_for_event(app->c);

        if (!event) {
            usleep(10000);
            continue;
        }
        handle_event(app, event);
        free(event);
    }

    if (!app->mapped || !app->configured) {
        fprintf(stderr,
                "host-x11-dri3-present-smoke: phase=present_fps_setup status=FAIL mapped=%d configured=%d reason=window-not-ready\n",
                app->mapped, app->configured);
        fflush(stderr);
        return -1;
    }
    fprintf(stderr,
            "host-x11-dri3-present-smoke: phase=present_fps_setup status=PASS mapped=%d configured=%d\n",
            app->mapped, app->configured);
    fflush(stderr);
    return 0;
}

static void
draw_present_fps_frame(struct app *app, xcb_pixmap_t pixmap, int frame)
{
    xcb_rectangle_t rect;
    uint32_t bg = 0x17212b + (uint32_t)((frame & 7) * 0x00050301);
    uint32_t a = 0x2dd4bf + (uint32_t)((frame & 15) * 0x00010100);
    uint32_t b = 0xf59e0b + (uint32_t)((frame & 31) * 0x00000102);

    set_fg(app, bg);
    rect.x = 0;
    rect.y = 0;
    rect.width = WIN_W;
    rect.height = WIN_H;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, a);
    rect.x = 24 + (int16_t)((frame * 11) % 160);
    rect.y = 34;
    rect.width = 140;
    rect.height = 92;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, b);
    rect.x = 64;
    rect.y = 158 + (int16_t)((frame * 7) % 72);
    rect.width = 420;
    rect.height = 52;
    xcb_poly_fill_rectangle(app->c, pixmap, app->gc, 1, &rect);

    set_fg(app, 0xffffff);
    xcb_image_text_8(app->c, 16, pixmap, app->gc, 36, 292,
                     "PRESENT FPS ONLY");
}

static int
present_fps_wait_complete(struct app *app, uint32_t serial,
                          struct present_fps_stats *stats,
                          uint64_t *msc_out, uint32_t *serial_out,
                          double *wait_ms_out)
{
    double wait_start = now_seconds();

    for (;;) {
        xcb_generic_event_t *event = xcb_wait_for_event(app->c);
        uint8_t type;

        if (!event) {
            log_line("host-x11-dri3-present-smoke: phase=present_fps_event status=FAIL reason=eof");
            return -1;
        }

        type = event->response_type & ~0x80;
        if (type == XCB_GE_GENERIC) {
            xcb_ge_generic_event_t *ge = (xcb_ge_generic_event_t *)event;

            if (ge->extension == app->present_major_opcode &&
                ge->event_type == 1) {
                xcb_present_complete_notify_event_t *ev =
                    (xcb_present_complete_notify_event_t *)ge;

                if (ge->length < PRESENT_COMPLETE_NOTIFY_EXTRA_WORDS) {
                    fprintf(stderr,
                            "host-x11-dri3-present-smoke: phase=present_fps_event status=FAIL reason=short-present-complete length=%u\n",
                            ge->length);
                    fflush(stderr);
                    free(event);
                    return -1;
                }
                if (ev->kind == PRESENT_COMPLETE_KIND_PIXMAP &&
                    ev->event == app->present_eid) {
                    app->completed_serial = ev->serial;
                    if (ev->serial >= serial) {
                        double wait_ms = elapsed_ms(wait_start);

                        switch (ev->mode) {
                        case PRESENT_COMPLETE_MODE_COPY:
                            stats->complete_copy++;
                            break;
                        case PRESENT_COMPLETE_MODE_FLIP:
                            stats->complete_flip++;
                            break;
                        case PRESENT_COMPLETE_MODE_SKIP:
                            stats->complete_skip++;
                            break;
                        case PRESENT_COMPLETE_MODE_SUBOPTIMAL_COPY:
                            stats->complete_suboptimal_copy++;
                            break;
                        default:
                            break;
                        }
                        stats->last_complete_mode = ev->mode;
                        stats->event_wait_total_ms += wait_ms;
                        if (msc_out)
                            *msc_out = ev->msc;
                        if (serial_out)
                            *serial_out = ev->serial;
                        if (wait_ms_out)
                            *wait_ms_out = wait_ms;
                        free(event);
                        return 0;
                    }
                }
            }
        } else if (type == XCB_CONFIGURE_NOTIFY) {
            xcb_configure_notify_event_t *ev =
                (xcb_configure_notify_event_t *)event;
            app->configured = 1;
            (void)ev;
        } else if (type == XCB_MAP_NOTIFY) {
            app->mapped = 1;
        }
        free(event);
    }
}

static int
present_fps_issue_frame(struct app *app, xcb_pixmap_t pixmap,
                        struct present_fps_stats *stats,
                        const struct present_fps_config *config,
                        uint32_t *serial_out, double *frame_start_out)
{
    xcb_generic_error_t *err;
    xcb_void_cookie_t cookie;
    double t;

    draw_present_fps_frame(app, pixmap, stats->issued);
    app->serial++;
    if (stats->serial_first == 0)
        stats->serial_first = app->serial;
    stats->serial_last = app->serial;
    if (frame_start_out)
        *frame_start_out = now_seconds();

    t = now_seconds();
    if (config->request_check_enabled)
        cookie = xcb_present_pixmap_checked(app->c, app->win, pixmap,
                                            app->serial, 0, 0, 0, 0, 0,
                                            0, 0, 0, 0, 0, 0, 0, NULL);
    else
        cookie = xcb_present_pixmap(app->c, app->win, pixmap,
                                    app->serial, 0, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, NULL);
    stats->present_request_total_ms += elapsed_ms(t);
    stats->issued++;
    if (stats->issued - stats->completed > stats->max_outstanding)
        stats->max_outstanding = stats->issued - stats->completed;

    t = now_seconds();
    xcb_flush(app->c);
    stats->flush_total_ms += elapsed_ms(t);

    if (config->request_check_enabled) {
        t = now_seconds();
        err = xcb_request_check(app->c, cookie);
        stats->request_check_total_ms += elapsed_ms(t);
        if (err) {
            fprintf(stderr,
                    "host-x11-dri3-present-smoke: phase=present_fps_request status=FAIL serial=%u error=%u\n",
                    app->serial, err->error_code);
            fflush(stderr);
            free(err);
            return -1;
        }
    }

    if (serial_out)
        *serial_out = app->serial;
    return 0;
}

static void
present_fps_finish_completion(struct present_fps_stats *stats,
                              double frame_start, uint64_t msc)
{
    double completion_ms;

    stats->completed++;
    completion_ms = elapsed_ms(frame_start);
    stats->completion_total_ms += completion_ms;
    if (completion_ms > stats->max_completion_ms)
        stats->max_completion_ms = completion_ms;
    if (stats->completed == 1)
        stats->first_msc = msc;
    stats->last_msc = msc;
}

static int
present_fps_find_free_slot(const uint32_t *slot_serials, int depth)
{
    for (int i = 0; i < depth; i++) {
        if (slot_serials[i] == 0)
            return i;
    }
    return -1;
}

static int
present_fps_find_serial_slot(const uint32_t *slot_serials, int depth,
                             uint32_t serial)
{
    for (int i = 0; i < depth; i++) {
        if (slot_serials[i] == serial)
            return i;
    }
    return -1;
}

static uint32_t
present_fps_oldest_slot_serial(const uint32_t *slot_serials, int depth)
{
    uint32_t oldest = 0;

    for (int i = 0; i < depth; i++) {
        if (slot_serials[i] != 0 &&
            (oldest == 0 || slot_serials[i] < oldest))
            oldest = slot_serials[i];
    }
    return oldest;
}

static void
present_fps_log_result(const char *status, const char *reason,
                       const struct app *app,
                       const struct present_fps_stats *stats,
                       const struct dri3_fd_info *fd_info,
                       const struct present_fps_config *config,
                       double elapsed_seconds)
{
    int outstanding = stats->issued - stats->completed;
    double fps = elapsed_seconds > 0.0
        ? (double)stats->completed / elapsed_seconds
        : 0.0;
    double avg_completion = stats->completed > 0
        ? stats->completion_total_ms / (double)stats->completed
        : 0.0;
    uint64_t msc_delta = stats->last_msc >= stats->first_msc
        ? stats->last_msc - stats->first_msc
        : 0;

    fprintf(stderr,
            "host-x11-dri3-present-smoke: phase=present_fps_result status=%s%s%s "
            "present_only=1 gl_context=0 gl_draw_total_ms=0 gl_swap_total_ms=0 "
            "frames=%d elapsed_seconds=%.6f fps=%.3f serial_first=%u serial_last=%u "
            "issued=%d completed=%d outstanding=%d "
            "queue_depth=%d max_outstanding=%d drain_wait_total_ms=%.3f drain_waits=%d "
            "variant=%s request_check_enabled=%d "
            "complete_copy=%d complete_flip=%d complete_skip=%d "
            "complete_suboptimal_copy=%d last_complete_mode=%d "
            "present_request_total_ms=%.3f request_check_total_ms=%.3f flush_total_ms=%.3f "
            "event_wait_total_ms=%.3f completion_total_ms=%.3f max_completion_ms=%.3f "
            "avg_completion_ms=%.3f first_msc=%llu last_msc=%llu msc_delta=%llu "
            "present_major_opcode=%u dri3_fd_valid=%d dri3_fd_mode=%o dri3_fd_rdev=%llu "
            "dri3_drm_name=%s dri3_drm_version=%d.%d.%d\n",
            status, reason ? " reason=" : "", reason ? reason : "",
            stats->completed, elapsed_seconds, fps, stats->serial_first,
            stats->serial_last, stats->issued, stats->completed, outstanding,
            stats->queue_depth, stats->max_outstanding,
            stats->drain_wait_total_ms, stats->drain_waits,
            config->variant_name, config->request_check_enabled,
            stats->complete_copy, stats->complete_flip, stats->complete_skip,
            stats->complete_suboptimal_copy, stats->last_complete_mode,
            stats->present_request_total_ms, stats->request_check_total_ms,
            stats->flush_total_ms, stats->event_wait_total_ms,
            stats->completion_total_ms, stats->max_completion_ms,
            avg_completion, (unsigned long long)stats->first_msc,
            (unsigned long long)stats->last_msc, (unsigned long long)msc_delta,
            app->present_major_opcode, fd_info->valid, fd_info->mode,
            fd_info->rdev, fd_info->valid ? fd_info->drm_name : "(none)",
            fd_info->version_major, fd_info->version_minor,
            fd_info->version_patchlevel);
    fflush(stderr);
}

static int
run_present_fps(void)
{
    struct app app;
    struct dri3_fd_info fd_info;
    struct present_fps_stats stats;
    struct present_fps_config config;
    xcb_pixmap_t pixmaps[PRESENT_FPS_QUEUE_DEPTH_MAX];
    int pixmap_count = PRESENT_FPS_PIXMAPS;
    double start;
    int rc = 1;

    memset(&fd_info, 0, sizeof(fd_info));
    memset(&stats, 0, sizeof(stats));
    memset(&config, 0, sizeof(config));
    memset(pixmaps, 0, sizeof(pixmaps));
    stats.last_complete_mode = -1;

    if (present_fps_variant_from_env(&config) < 0)
        return 2;
    stats.queue_depth = config.queue_depth;
    if (config.queue_depth > pixmap_count)
        pixmap_count = config.queue_depth;

    log_line("host-x11-dri3-present-smoke: start");
    log_line("host-x11-dri3-present-smoke: phase=start status=BEGIN mode=present-fps");
    fprintf(stderr,
            "host-x11-dri3-present-smoke: phase=present_fps status=BEGIN target_seconds=%d max_frames=%d pixmaps=%d variant=%s request_check_enabled=%d queue_depth=%d\n",
            PRESENT_FPS_TARGET_SECONDS, PRESENT_FPS_MAX_FRAMES,
            pixmap_count, config.variant_name,
            config.request_check_enabled, config.queue_depth);
    fflush(stderr);
    if (setup(&app) < 0) {
        present_fps_log_result("FAIL", "setup", &app, &stats, &fd_info,
                               &config, 0.0);
        cleanup(&app);
        return 2;
    }

    if (present_fps_wait_initial_window(&app) < 0) {
        present_fps_log_result("FAIL", "window-not-ready", &app, &stats,
                               &fd_info, &config, 0.0);
        cleanup(&app);
        return 3;
    }
    if (validate_dri3_fd(&app, &fd_info) < 0) {
        present_fps_log_result("FAIL", "dri3-fd", &app, &stats, &fd_info,
                               &config, 0.0);
        cleanup(&app);
        return 4;
    }

    for (int i = 0; i < pixmap_count; i++) {
        pixmaps[i] = xcb_generate_id(app.c);
        xcb_create_pixmap(app.c, app.screen->root_depth, pixmaps[i],
                          app.win, WIN_W, WIN_H);
        hold_pixmap(&app, pixmaps[i]);
    }

    start = now_seconds();
    if (config.variant == PRESENT_FPS_VARIANT_QUEUE3_UNCHECKED ||
        config.variant == PRESENT_FPS_VARIANT_QUEUE_UNCHECKED) {
        uint32_t slot_serials[PRESENT_FPS_QUEUE_DEPTH_MAX];
        double slot_starts[PRESENT_FPS_QUEUE_DEPTH_MAX];
        int outstanding = 0;

        memset(slot_serials, 0, sizeof(slot_serials));
        memset(slot_starts, 0, sizeof(slot_starts));
        while (stats.issued < PRESENT_FPS_MAX_FRAMES &&
               now_seconds() - start < (double)PRESENT_FPS_TARGET_SECONDS) {
            while (outstanding < config.queue_depth &&
                   stats.issued < PRESENT_FPS_MAX_FRAMES &&
                   now_seconds() - start < (double)PRESENT_FPS_TARGET_SECONDS) {
                uint32_t serial = 0;
                int slot = present_fps_find_free_slot(slot_serials,
                                                      config.queue_depth);

                if (slot < 0)
                    break;
                if (present_fps_issue_frame(&app, pixmaps[slot], &stats,
                                            &config, &serial,
                                            &slot_starts[slot]) < 0) {
                    present_fps_log_result("FAIL", "present-request", &app,
                                           &stats, &fd_info, &config,
                                           now_seconds() - start);
                    rc = 5;
                    goto out;
                }
                slot_serials[slot] = serial;
                outstanding++;
                if (outstanding > stats.max_outstanding)
                    stats.max_outstanding = outstanding;
            }

            if (outstanding > 0) {
                uint32_t completed_serial = 0;
                uint32_t expected_serial =
                    present_fps_oldest_slot_serial(slot_serials,
                                                   config.queue_depth);
                uint64_t msc = 0;
                int slot;

                if (present_fps_wait_complete(&app, expected_serial, &stats,
                                              &msc, &completed_serial,
                                              NULL) < 0) {
                    present_fps_log_result("FAIL", "present-complete", &app,
                                           &stats, &fd_info, &config,
                                           now_seconds() - start);
                    rc = 6;
                    goto out;
                }
                slot = present_fps_find_serial_slot(slot_serials,
                                                    config.queue_depth,
                                                    completed_serial);
                if (slot < 0) {
                    fprintf(stderr,
                            "host-x11-dri3-present-smoke: phase=present_fps_queue status=FAIL reason=unexpected-complete serial=%u expected_min=%u outstanding=%d\n",
                            completed_serial, expected_serial, outstanding);
                    fflush(stderr);
                    present_fps_log_result("FAIL", "unexpected-complete",
                                           &app, &stats, &fd_info, &config,
                                           now_seconds() - start);
                    rc = 6;
                    goto out;
                }
                slot_serials[slot] = 0;
                outstanding--;
                present_fps_finish_completion(&stats, slot_starts[slot], msc);
            }
        }

        while (outstanding > 0) {
            uint32_t completed_serial = 0;
            uint32_t expected_serial =
                present_fps_oldest_slot_serial(slot_serials,
                                               config.queue_depth);
            uint64_t msc = 0;
            double wait_ms = 0.0;
            int slot;

            if (present_fps_wait_complete(&app, expected_serial, &stats,
                                          &msc, &completed_serial,
                                          &wait_ms) < 0) {
                fprintf(stderr,
                        "host-x11-dri3-present-smoke: phase=present_fps_drain status=FAIL reason=complete-wait expected_min=%u outstanding=%d\n",
                        expected_serial, outstanding);
                fflush(stderr);
                present_fps_log_result("FAIL", "drain-complete", &app,
                                       &stats, &fd_info, &config,
                                       now_seconds() - start);
                rc = 6;
                goto out;
            }
            stats.drain_wait_total_ms += wait_ms;
            stats.drain_waits++;
            slot = present_fps_find_serial_slot(slot_serials,
                                                config.queue_depth,
                                                completed_serial);
            if (slot < 0) {
                fprintf(stderr,
                        "host-x11-dri3-present-smoke: phase=present_fps_drain status=FAIL reason=unexpected-complete serial=%u expected_min=%u outstanding=%d\n",
                        completed_serial, expected_serial, outstanding);
                fflush(stderr);
                present_fps_log_result("FAIL", "drain-unexpected-complete",
                                       &app, &stats, &fd_info, &config,
                                       now_seconds() - start);
                rc = 6;
                goto out;
            }
            slot_serials[slot] = 0;
            outstanding--;
            present_fps_finish_completion(&stats, slot_starts[slot], msc);
        }
    } else {
        while (stats.issued < PRESENT_FPS_MAX_FRAMES &&
               now_seconds() - start < (double)PRESENT_FPS_TARGET_SECONDS) {
            xcb_pixmap_t pixmap = pixmaps[stats.issued % pixmap_count];
            uint64_t msc = 0;
            uint32_t serial = 0;
            double frame_start = 0.0;

            if (present_fps_issue_frame(&app, pixmap, &stats, &config,
                                        &serial, &frame_start) < 0) {
                present_fps_log_result("FAIL", "present-request", &app,
                                       &stats, &fd_info, &config,
                                       now_seconds() - start);
                rc = 5;
                goto out;
            }
            if (stats.max_outstanding < 1)
                stats.max_outstanding = 1;

            if (present_fps_wait_complete(&app, serial, &stats, &msc,
                                          NULL, NULL) < 0) {
                present_fps_log_result("FAIL", "present-complete", &app,
                                       &stats, &fd_info, &config,
                                       now_seconds() - start);
                rc = 6;
                goto out;
            }

            present_fps_finish_completion(&stats, frame_start, msc);
        }
    }

    present_fps_log_result(stats.completed > 0 ? "PASS" : "FAIL",
                           stats.completed > 0 ? NULL : "zero-frames",
                           &app, &stats, &fd_info, &config,
                           now_seconds() - start);
    rc = stats.completed > 0 ? 0 : 7;

out:
    cleanup(&app);
    log_line("host-x11-dri3-present-smoke: exited");
    return rc;
}

int
main(int argc, char **argv)
{
    struct app app;
    xcb_generic_event_t *event;

    if (argc > 1) {
        if (argc == 2 && strcmp(argv[1], "--present-fps") == 0)
            return run_present_fps();
        fprintf(stderr,
                "host-x11-dri3-present-smoke: phase=argv status=FAIL arg=%s\n",
                argv[1] ? argv[1] : "(null)");
        fflush(stderr);
        return 2;
    }

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
