#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    struct wl_callback *frame_callback;
    uint32_t compositor_version;
    uint32_t configure_serial;
    int configured;
    int toplevel_configures;
    int frame_done;
    int buffer_released;
    int close_requested;
    long start_ms;
    long registry_ms;
    long globals_ms;
    long configure_ms;
    long first_commit_ms;
    long frame_done_ms;
    long buffer_release_ms;
};

struct qdbus_sidecar {
    pthread_t thread;
    int socket_fd;
    int event_fd;
    int stop;
    int connected;
    int polls;
    int socket_events;
    int eventfd_events;
    int read_bytes;
    long start_ms;
    long connect_ms;
    long first_socket_ms;
    long first_eventfd_ms;
};

static const int width = 160;
static const int height = 96;
static const int stride = 160 * 4;
static const int shm_size = 160 * 96 * 4;

static long now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static long since_start(const struct app_state *app)
{
    return now_ms() - app->start_ms;
}

static void log_phase(const struct app_state *app, const char *phase,
                      const char *status, const char *detail)
{
    printf("kde_wayland_activation_pty_probe phase=%s status=%s since_start_ms=%ld %s\n",
           phase, status, since_start(app), detail ? detail : "");
    fflush(stdout);
}

static int parse_bool_env(const char *name, int def)
{
    const char *value = getenv(name);

    if (!value || !*value)
        return def;
    if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0)
        return 1;
    if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0)
        return 0;
    return def;
}

static int connect_dbus_address(const char *address)
{
    const char *key;
    char value[108];
    struct sockaddr_un sun;
    int fd;

    if (!address || strncmp(address, "unix:", 5) != 0)
        return -1;
    key = strstr(address + 5, "abstract=");
    if (!key)
        key = strstr(address + 5, "path=");
    if (!key)
        return -1;

    memset(value, 0, sizeof(value));
    if (sscanf(key, "abstract=%107[^,]", value) == 1) {
        size_t value_len = strlen(value);

        if (value_len > sizeof(sun.sun_path) - 2)
            return -1;
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0)
            return -1;
        memset(&sun, 0, sizeof(sun));
        sun.sun_family = AF_UNIX;
        sun.sun_path[0] = '\0';
        memcpy(sun.sun_path + 1, value, value_len);
        if (connect(fd, (struct sockaddr *)&sun,
                    (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 +
                                value_len)) < 0 &&
            errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
        return fd;
    }
    if (sscanf(key, "path=%107[^,]", value) == 1) {
        size_t value_len = strlen(value);

        if (value_len >= sizeof(sun.sun_path))
            return -1;
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0)
            return -1;
        memset(&sun, 0, sizeof(sun));
        sun.sun_family = AF_UNIX;
        memcpy(sun.sun_path, value, value_len + 1);
        if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0 &&
            errno != EINPROGRESS) {
            close(fd);
            return -1;
        }
        return fd;
    }
    return -1;
}

static void *qdbus_sidecar_thread(void *arg)
{
    struct qdbus_sidecar *sidecar = arg;
    char buf[256];
    int sent_auth = 0;

    while (!sidecar->stop) {
        struct pollfd pfds[2];
        int ret;

        pfds[0].fd = sidecar->socket_fd;
        pfds[0].events = POLLIN | POLLOUT | POLLERR | POLLHUP;
        pfds[0].revents = 0;
        pfds[1].fd = sidecar->event_fd;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;
        ret = poll(pfds, 2, 100);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (ret == 0)
            continue;
        sidecar->polls++;
        if (pfds[0].revents) {
            sidecar->socket_events++;
            if (sidecar->first_socket_ms == 0)
                sidecar->first_socket_ms = now_ms() - sidecar->start_ms;
            if ((pfds[0].revents & POLLOUT) && !sent_auth) {
                static const char auth[] =
                    "\0AUTH EXTERNAL 30\r\nNEGOTIATE_UNIX_FD\r\nBEGIN\r\n";
                (void)send(sidecar->socket_fd, auth, sizeof(auth) - 1,
                           MSG_NOSIGNAL);
                sent_auth = 1;
            }
            if (pfds[0].revents & POLLIN) {
                ssize_t n = read(sidecar->socket_fd, buf, sizeof(buf));
                if (n > 0)
                    sidecar->read_bytes += (int)n;
            }
        }
        if (pfds[1].revents & POLLIN) {
            uint64_t value = 0;
            ssize_t n;
            sidecar->eventfd_events++;
            if (sidecar->first_eventfd_ms == 0)
                sidecar->first_eventfd_ms = now_ms() - sidecar->start_ms;
            n = read(sidecar->event_fd, &value, sizeof(value));
            (void)n;
        }
    }
    return NULL;
}

static int start_qdbus_sidecar(struct qdbus_sidecar *sidecar)
{
    const char *address = getenv("DBUS_SESSION_BUS_ADDRESS");

    memset(sidecar, 0, sizeof(*sidecar));
    sidecar->socket_fd = -1;
    sidecar->event_fd = -1;
    sidecar->start_ms = now_ms();
    sidecar->socket_fd = connect_dbus_address(address);
    if (sidecar->socket_fd < 0) {
        printf("kde_wayland_activation_pty_probe phase=qdbus-sidecar status=SKIP reason=connect errno=%d %s address=%s\n",
               errno, strerror(errno), address ? address : "(unset)");
        return 0;
    }
    sidecar->connected = 1;
    sidecar->connect_ms = now_ms() - sidecar->start_ms;
    sidecar->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (sidecar->event_fd < 0) {
        printf("kde_wayland_activation_pty_probe phase=qdbus-sidecar status=SKIP reason=eventfd errno=%d %s\n",
               errno, strerror(errno));
        close(sidecar->socket_fd);
        sidecar->socket_fd = -1;
        return 0;
    }
    if (pthread_create(&sidecar->thread, NULL, qdbus_sidecar_thread, sidecar) != 0) {
        printf("kde_wayland_activation_pty_probe phase=qdbus-sidecar status=SKIP reason=pthread errno=%d %s\n",
               errno, strerror(errno));
        close(sidecar->socket_fd);
        close(sidecar->event_fd);
        sidecar->socket_fd = -1;
        sidecar->event_fd = -1;
        return 0;
    }
    printf("kde_wayland_activation_pty_probe phase=qdbus-sidecar status=PASS connect_ms=%ld socket_fd=%d event_fd=%d\n",
           sidecar->connect_ms, sidecar->socket_fd, sidecar->event_fd);
    return 0;
}

static void stop_qdbus_sidecar(struct qdbus_sidecar *sidecar)
{
    uint64_t one = 1;

    if (!sidecar->connected)
        return;
    sidecar->stop = 1;
    if (sidecar->event_fd >= 0) {
        ssize_t n = write(sidecar->event_fd, &one, sizeof(one));
        (void)n;
    }
    pthread_join(sidecar->thread, NULL);
    printf("kde_wayland_activation_pty_probe phase=qdbus-sidecar-summary status=PASS polls=%d socket_events=%d eventfd_events=%d read_bytes=%d first_socket_ms=%ld first_eventfd_ms=%ld\n",
           sidecar->polls, sidecar->socket_events, sidecar->eventfd_events,
           sidecar->read_bytes, sidecar->first_socket_ms,
           sidecar->first_eventfd_ms);
    close(sidecar->socket_fd);
    close(sidecar->event_fd);
}

static int qdbus_prepty_checkpoint(struct qdbus_sidecar *sidecar,
                                   int timeout_ms, int require_qdbus,
                                   long *checkpoint_ms)
{
    long deadline;
    uint64_t one = 1;

    *checkpoint_ms = -1;
    if (!sidecar->connected) {
        printf("kde_wayland_activation_pty_probe phase=qdbus-prepty status=%s reason=not-connected require_qdbus=%d\n",
               require_qdbus ? "FAIL" : "SKIP", require_qdbus);
        return require_qdbus ? -1 : 0;
    }

    if (sidecar->event_fd >= 0) {
        ssize_t n = write(sidecar->event_fd, &one, sizeof(one));
        (void)n;
    }

    deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        if (sidecar->read_bytes > 0 && sidecar->eventfd_events > 0) {
            *checkpoint_ms = now_ms() - sidecar->start_ms;
            printf("kde_wayland_activation_pty_probe phase=qdbus-prepty status=PASS checkpoint_ms=%ld socket_events=%d eventfd_events=%d read_bytes=%d first_socket_ms=%ld first_eventfd_ms=%ld\n",
                   *checkpoint_ms, sidecar->socket_events,
                   sidecar->eventfd_events, sidecar->read_bytes,
                   sidecar->first_socket_ms, sidecar->first_eventfd_ms);
            return 0;
        }
        usleep(10000);
    }

    printf("kde_wayland_activation_pty_probe phase=qdbus-prepty status=%s reason=timeout timeout_ms=%d socket_events=%d eventfd_events=%d read_bytes=%d first_socket_ms=%ld first_eventfd_ms=%ld require_qdbus=%d\n",
           require_qdbus ? "FAIL" : "SKIP", timeout_ms,
           sidecar->socket_events, sidecar->eventfd_events,
           sidecar->read_bytes, sidecar->first_socket_ms,
           sidecar->first_eventfd_ms, require_qdbus);
    return require_qdbus ? -1 : 0;
}

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
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
    struct app_state *app = data;

    app->configured = 1;
    app->configure_serial = serial;
    if (app->configure_ms == 0)
        app->configure_ms = since_start(app);
    xdg_surface_ack_configure(surface, serial);
    printf("kde_wayland_activation_pty_probe phase=xdg-configure status=PASS since_start_ms=%ld serial=%u toplevel_configures=%d\n",
           app->configure_ms, serial, app->toplevel_configures);
    fflush(stdout);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t w, int32_t h, struct wl_array *states)
{
    struct app_state *app = data;
    (void)toplevel;
    (void)states;
    app->toplevel_configures++;
    printf("kde_wayland_activation_pty_probe phase=toplevel-configure status=PASS since_start_ms=%ld width=%d height=%d count=%d\n",
           since_start(app), w, h, app->toplevel_configures);
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app_state *app = data;
    (void)toplevel;
    app->close_requested = 1;
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel,
                                      int32_t w, int32_t h)
{
    struct app_state *app = data;
    (void)toplevel;
    printf("kde_wayland_activation_pty_probe phase=toplevel-configure-bounds status=PASS since_start_ms=%ld width=%d height=%d\n",
           since_start(app), w, h);
}

static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel,
                                     struct wl_array *caps)
{
    struct app_state *app = data;
    (void)toplevel;
    printf("kde_wayland_activation_pty_probe phase=toplevel-wm-capabilities status=PASS since_start_ms=%ld bytes=%zu\n",
           since_start(app), caps ? caps->size : 0);
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure,
    toplevel_close,
    toplevel_configure_bounds,
    toplevel_wm_capabilities,
};

static void frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
    struct app_state *app = data;
    (void)time;
    app->frame_done = 1;
    if (app->frame_done_ms == 0)
        app->frame_done_ms = since_start(app);
    if (callback)
        wl_callback_destroy(callback);
    app->frame_callback = NULL;
    printf("kde_wayland_activation_pty_probe phase=frame-done status=PASS since_start_ms=%ld\n",
           app->frame_done_ms);
}

static const struct wl_callback_listener frame_listener = {
    frame_done,
};

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    struct app_state *app = data;
    (void)buffer;
    app->buffer_released = 1;
    if (app->buffer_release_ms == 0)
        app->buffer_release_ms = since_start(app);
    printf("kde_wayland_activation_pty_probe phase=buffer-release status=PASS since_start_ms=%ld\n",
           app->buffer_release_ms);
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app_state *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0 && !app->compositor) {
        uint32_t bind_version = version < 4 ? version : 4;
        if (bind_version < 1)
            bind_version = 1;
        app->compositor = wl_registry_bind(registry, name,
                                           &wl_compositor_interface,
                                           bind_version);
        app->compositor_version = bind_version;
        printf("kde_wayland_activation_pty_probe phase=registry-global status=PASS interface=%s name=%u version=%u bind_version=%u\n",
               interface, name, version, bind_version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0 && !app->shm) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        printf("kde_wayland_activation_pty_probe phase=registry-global status=PASS interface=%s name=%u version=%u bind_version=1\n",
               interface, name, version);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0 && !app->wm_base) {
        uint32_t bind_version = version < 5 ? version : 5;
        if (bind_version < 1)
            bind_version = 1;
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface,
                                        bind_version);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
        printf("kde_wayland_activation_pty_probe phase=registry-global status=PASS interface=%s name=%u version=%u bind_version=%u\n",
               interface, name, version, bind_version);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove,
};

static int dispatch_until(struct app_state *app, int timeout_ms,
                          int (*done)(const struct app_state *))
{
    int fd = wl_display_get_fd(app->display);
    long deadline = now_ms() + timeout_ms;

    while (!done(app)) {
        int ret;
        long remaining = deadline - now_ms();

        if (remaining <= 0)
            return 2;
        ret = wl_display_dispatch_pending(app->display);
        if (ret < 0)
            return -1;
        if (done(app))
            return 0;
        while (wl_display_prepare_read(app->display) != 0) {
            ret = wl_display_dispatch_pending(app->display);
            if (ret < 0)
                return -1;
            if (done(app))
                return 0;
        }
        ret = wl_display_flush(app->display);
        if (ret < 0 && errno != EAGAIN) {
            wl_display_cancel_read(app->display);
            return -1;
        }
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        if (remaining > 100)
            remaining = 100;
        ret = poll(&pfd, 1, (int)remaining);
        if (ret < 0) {
            wl_display_cancel_read(app->display);
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ret == 0) {
            wl_display_cancel_read(app->display);
            continue;
        }
        if (!(pfd.revents & POLLIN)) {
            wl_display_cancel_read(app->display);
            return -1;
        }
        if (wl_display_read_events(app->display) < 0)
            return -1;
    }
    return 0;
}

static int globals_done(const struct app_state *app)
{
    return app->compositor && app->shm && app->wm_base;
}

static int configured_done(const struct app_state *app)
{
    return app->configured;
}

static int frame_or_release_done(const struct app_state *app)
{
    return app->frame_done || app->buffer_released;
}

static int create_shm_buffer(struct app_state *app)
{
    char template[] = "/dev/shm/kde-wayland-activation-pty-probe.XXXXXX";
    int fd;
    void *map;
    uint32_t *pixels;
    struct wl_shm_pool *pool;

    fd = mkstemp(template);
    if (fd < 0)
        return -1;
    unlink(template);
    if (ftruncate(fd, shm_size) < 0) {
        close(fd);
        return -1;
    }
    map = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return -1;
    }
    pixels = map;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            pixels[y * width + x] = 0xff203040u ^ ((uint32_t)x << 8) ^ (uint32_t)y;
    }
    pool = wl_shm_create_pool(app->shm, fd, shm_size);
    app->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
                                            WL_SHM_FORMAT_XRGB8888);
    wl_buffer_add_listener(app->buffer, &buffer_listener, app);
    wl_shm_pool_destroy(pool);
    munmap(map, shm_size);
    close(fd);
    return app->buffer ? 0 : -1;
}

static int open_probe_pty(struct app_state *app, long *pty_ms_out,
                          char *name, size_t name_size)
{
    int master = -1;
    int slave = -1;
    struct winsize ws = {
        .ws_row = 24,
        .ws_col = 80,
        .ws_xpixel = width,
        .ws_ypixel = height,
    };

    if (openpty(&master, &slave, name, NULL, &ws) < 0)
        return -1;
    *pty_ms_out = since_start(app);
    if (name_size > 0)
        name[name_size - 1] = '\0';
    close(slave);
    close(master);
    return 0;
}

static void cleanup_app(struct app_state *app)
{
    if (app->frame_callback)
        wl_callback_destroy(app->frame_callback);
    if (app->buffer)
        wl_buffer_destroy(app->buffer);
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->shm)
        wl_shm_destroy(app->shm);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
}

static void alarm_handler(int signo)
{
    (void)signo;
    _exit(124);
}

int main(void)
{
    struct app_state app;
    struct qdbus_sidecar sidecar;
    long pty_ms = -1;
    long qdbus_prepty_ms = -1;
    char pty_name[128] = "";
    int ret;
    int wait_timeout_ms = 5000;
    int require_frame = parse_bool_env("KDE_WAYLAND_ACTIVATION_PTY_REQUIRE_FRAME", 1);
    int require_qdbus = parse_bool_env("KDE_WAYLAND_ACTIVATION_PTY_REQUIRE_QDBUS", 1);

    memset(&app, 0, sizeof(app));
    app.start_ms = now_ms();
    signal(SIGALRM, alarm_handler);
    alarm(20);

    printf("kde_wayland_activation_pty_probe phase=start status=BEGIN require_frame=%d require_qdbus=%d WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s DBUS_SESSION_BUS_ADDRESS=%s\n",
           require_frame, require_qdbus,
           getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)",
           getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)",
           getenv("DBUS_SESSION_BUS_ADDRESS") ? getenv("DBUS_SESSION_BUS_ADDRESS") : "(unset)");
    start_qdbus_sidecar(&sidecar);

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        log_phase(&app, "connect", "FAIL", "reason=wl_display_connect");
        stop_qdbus_sidecar(&sidecar);
        return 2;
    }
    log_phase(&app, "connect", "PASS", "");

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    ret = wl_display_roundtrip(app.display);
    app.registry_ms = since_start(&app);
    printf("kde_wayland_activation_pty_probe phase=registry-roundtrip status=%s since_start_ms=%ld ret=%d errno=%d %s\n",
           ret >= 0 ? "PASS" : "FAIL", app.registry_ms, ret, errno, strerror(errno));
    if (ret < 0)
        goto fail;

    ret = dispatch_until(&app, wait_timeout_ms, globals_done);
    app.globals_ms = since_start(&app);
    printf("kde_wayland_activation_pty_probe phase=globals status=%s since_start_ms=%ld compositor=%d shm=%d wm_base=%d ret=%d errno=%d %s\n",
           ret == 0 ? "PASS" : "FAIL", app.globals_ms, !!app.compositor,
           !!app.shm, !!app.wm_base, ret, errno, strerror(errno));
    if (ret != 0 || !globals_done(&app))
        goto fail;

    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.toplevel, &toplevel_listener, &app);
    xdg_toplevel_set_title(app.toplevel, "kde-wayland-activation-pty-probe");
    xdg_toplevel_set_app_id(app.toplevel, "kde-wayland-activation-pty-probe");
    wl_surface_commit(app.surface);
    app.first_commit_ms = since_start(&app);
    log_phase(&app, "empty-commit", "PASS", "");

    ret = dispatch_until(&app, wait_timeout_ms, configured_done);
    printf("kde_wayland_activation_pty_probe phase=configure-wait status=%s since_start_ms=%ld ret=%d configured=%d serial=%u errno=%d %s\n",
           ret == 0 ? "PASS" : "FAIL", since_start(&app), ret,
           app.configured, app.configure_serial, errno, strerror(errno));
    if (ret != 0 || !app.configured)
        goto fail;

    if (create_shm_buffer(&app) < 0) {
        printf("kde_wayland_activation_pty_probe phase=shm-buffer status=FAIL errno=%d %s\n",
               errno, strerror(errno));
        goto fail;
    }
    app.frame_callback = wl_surface_frame(app.surface);
    wl_callback_add_listener(app.frame_callback, &frame_listener, &app);
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    if (app.compositor_version >= 4)
        wl_surface_damage_buffer(app.surface, 0, 0, width, height);
    else
        wl_surface_damage(app.surface, 0, 0, width, height);
    wl_surface_commit(app.surface);
    printf("kde_wayland_activation_pty_probe phase=buffer-commit status=PASS since_start_ms=%ld width=%d height=%d compositor_version=%u\n",
           since_start(&app), width, height, app.compositor_version);

    ret = dispatch_until(&app, wait_timeout_ms, frame_or_release_done);
    printf("kde_wayland_activation_pty_probe phase=frame-wait status=%s since_start_ms=%ld ret=%d frame_done=%d frame_done_ms=%ld buffer_released=%d buffer_release_ms=%ld errno=%d %s\n",
           ret == 0 ? "PASS" : (require_frame ? "FAIL" : "TIMEOUT"),
           since_start(&app), ret, app.frame_done, app.frame_done_ms,
           app.buffer_released, app.buffer_release_ms, errno, strerror(errno));
    if (require_frame && ret != 0)
        goto fail;

    if (qdbus_prepty_checkpoint(&sidecar, wait_timeout_ms, require_qdbus,
                                &qdbus_prepty_ms) != 0)
        goto fail;

    if (open_probe_pty(&app, &pty_ms, pty_name, sizeof(pty_name)) < 0) {
        printf("kde_wayland_activation_pty_probe phase=pty-open status=FAIL since_start_ms=%ld errno=%d %s\n",
               since_start(&app), errno, strerror(errno));
        goto fail;
    }
    printf("kde_wayland_activation_pty_probe phase=pty-open status=PASS since_start_ms=%ld pty_name=%s\n",
           pty_ms, pty_name);

    stop_qdbus_sidecar(&sidecar);
    printf("kde_wayland_activation_pty_probe phase=result status=PASS total_ms=%ld registry_ms=%ld globals_ms=%ld first_commit_ms=%ld configure_ms=%ld frame_done_ms=%ld buffer_release_ms=%ld pty_open_ms=%ld qdbus_connected=%d qdbus_socket_events=%d qdbus_eventfd_events=%d qdbus_read_bytes=%d qdbus_first_socket_ms=%ld qdbus_first_eventfd_ms=%ld qdbus_prepty_ms=%ld\n",
           since_start(&app), app.registry_ms, app.globals_ms,
           app.first_commit_ms, app.configure_ms, app.frame_done_ms,
           app.buffer_release_ms, pty_ms, sidecar.connected,
           sidecar.socket_events, sidecar.eventfd_events, sidecar.read_bytes,
           sidecar.first_socket_ms, sidecar.first_eventfd_ms,
           qdbus_prepty_ms);
    cleanup_app(&app);
    return 0;

fail:
    stop_qdbus_sidecar(&sidecar);
    printf("kde_wayland_activation_pty_probe phase=result status=FAIL total_ms=%ld registry_ms=%ld globals_ms=%ld first_commit_ms=%ld configure_ms=%ld frame_done_ms=%ld buffer_release_ms=%ld pty_open_ms=%ld qdbus_prepty_ms=%ld\n",
           since_start(&app), app.registry_ms, app.globals_ms,
           app.first_commit_ms, app.configure_ms, app.frame_done_ms,
           app.buffer_release_ms, pty_ms, qdbus_prepty_ms);
    cleanup_app(&app);
    return 1;
}
