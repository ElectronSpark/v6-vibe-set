#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

struct global_info {
    uint32_t name;
    uint32_t version;
};

struct probe_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct global_info compositor;
    struct global_info shm_info;
    struct global_info seat;
    struct global_info wm_base;
    struct global_info dmabuf;
    struct global_info explicit_sync;
    struct global_info pointer_gestures;
    uint32_t globals;
    uint32_t shm_formats;
    int shm_argb8888;
    int shm_xrgb8888;
    int bind_shm;
    int make_buffer;
};

struct sync_state {
    int done;
    uint32_t callback_data;
};

static const char *probe_phase = "startup";

static void timeout_handler(int signo)
{
    (void)signo;
    fprintf(stderr, "kde_wayland_registry_probe timeout phase=%s\n", probe_phase);
    _exit(2);
}

static void set_phase(const char *phase)
{
    probe_phase = phase;
    fprintf(stderr, "kde_wayland_registry_probe phase=%s\n", phase);
}

static long now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static const char *shm_format_name(uint32_t format)
{
    switch (format) {
    case WL_SHM_FORMAT_ARGB8888:
        return "ARGB8888";
    case WL_SHM_FORMAT_XRGB8888:
        return "XRGB8888";
    default:
        return "other";
    }
}

static void print_display_fd_info(struct wl_display *display)
{
    int fd = wl_display_get_fd(display);
    struct ucred cred;
    socklen_t len = sizeof(cred);

    if (fd < 0) {
        fprintf(stderr, "kde_wayland_registry_probe display_fd=%d\n", fd);
        return;
    }

    memset(&cred, 0, sizeof(cred));
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
        fprintf(stderr,
                "kde_wayland_registry_probe display_fd=%d peer_pid=%d "
                "peer_uid=%d peer_gid=%d\n",
                fd, cred.pid, cred.uid, cred.gid);
    } else {
        fprintf(stderr,
                "kde_wayland_registry_probe display_fd=%d peercred_errno=%d %s\n",
                fd, errno, strerror(errno));
    }
}

static void sync_done(void *data, struct wl_callback *callback, uint32_t callback_data)
{
    struct sync_state *state = data;

    state->done = 1;
    state->callback_data = callback_data;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
    sync_done,
};

static int bounded_roundtrip(struct wl_display *display, const char *phase,
                             int timeout_ms)
{
    struct sync_state sync = { 0, 0 };
    struct wl_callback *callback;
    struct pollfd pfd;
    long deadline;
    int fd;

    set_phase(phase);
    callback = wl_display_sync(display);
    if (!callback) {
        fprintf(stderr,
                "kde_wayland_registry_probe %s sync-create failed errno=%d %s\n",
                phase, errno, strerror(errno));
        return -1;
    }
    wl_callback_add_listener(callback, &sync_listener, &sync);

    fd = wl_display_get_fd(display);
    deadline = now_ms() + timeout_ms;

    while (!sync.done) {
        long remaining = deadline - now_ms();
        int ret;

        if (remaining <= 0) {
            fprintf(stderr,
                    "kde_wayland_registry_probe timeout phase=%s sync_done=%d fd=%d\n",
                    phase, sync.done, fd);
            wl_callback_destroy(callback);
            return 2;
        }

        while (wl_display_prepare_read(display) != 0) {
            ret = wl_display_dispatch_pending(display);
            if (ret < 0) {
                fprintf(stderr,
                        "kde_wayland_registry_probe %s dispatch-pending failed "
                        "errno=%d %s\n",
                        phase, errno, strerror(errno));
                wl_callback_destroy(callback);
                return -1;
            }
            if (sync.done)
                return 0;
        }

        ret = wl_display_flush(display);
        if (ret < 0 && errno != EAGAIN) {
            int saved = errno;
            wl_display_cancel_read(display);
            fprintf(stderr,
                    "kde_wayland_registry_probe %s flush failed errno=%d %s\n",
                    phase, saved, strerror(saved));
            wl_callback_destroy(callback);
            return -1;
        }

        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        ret = poll(&pfd, 1, remaining > 250 ? 250 : (int)remaining);
        if (ret < 0) {
            int saved = errno;
            wl_display_cancel_read(display);
            if (saved == EINTR)
                continue;
            fprintf(stderr,
                    "kde_wayland_registry_probe %s poll failed errno=%d %s\n",
                    phase, saved, strerror(saved));
            wl_callback_destroy(callback);
            return -1;
        }
        if (ret == 0) {
            wl_display_cancel_read(display);
            continue;
        }
        if (!(pfd.revents & POLLIN)) {
            wl_display_cancel_read(display);
            fprintf(stderr,
                    "kde_wayland_registry_probe %s poll revents=0x%x without POLLIN\n",
                    phase, pfd.revents);
            wl_callback_destroy(callback);
            return -1;
        }

        ret = wl_display_read_events(display);
        if (ret < 0) {
            fprintf(stderr,
                    "kde_wayland_registry_probe %s read-events failed errno=%d %s\n",
                    phase, errno, strerror(errno));
            wl_callback_destroy(callback);
            return -1;
        }
        ret = wl_display_dispatch_pending(display);
        if (ret < 0) {
            fprintf(stderr,
                    "kde_wayland_registry_probe %s dispatch-after-read failed "
                    "errno=%d %s\n",
                    phase, errno, strerror(errno));
            wl_callback_destroy(callback);
            return -1;
        }
    }

    fprintf(stderr, "kde_wayland_registry_probe %s done callback_data=%u\n",
            phase, sync.callback_data);
    return 0;
}

static void shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
    struct probe_state *state = data;
    (void)shm;

    state->shm_formats++;
    if (format == WL_SHM_FORMAT_ARGB8888)
        state->shm_argb8888 = 1;
    if (format == WL_SHM_FORMAT_XRGB8888)
        state->shm_xrgb8888 = 1;
    printf("kde_wayland_registry_probe shm_format format=0x%x name=%s\n",
           format, shm_format_name(format));
}

static const struct wl_shm_listener shm_listener = {
    shm_format,
};

static void remember_global(struct global_info *info, uint32_t name,
                            uint32_t version)
{
    if (info->name == 0) {
        info->name = name;
        info->version = version;
    }
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version)
{
    struct probe_state *state = data;

    state->globals++;
    printf("kde_wayland_registry_probe global name=%u interface=%s version=%u\n",
           name, interface, version);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        remember_global(&state->compositor, name, version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        uint32_t bind_version = version < 2 ? version : 2;

        remember_global(&state->shm_info, name, version);
        if (!state->shm && bind_version >= 1) {
            state->shm = wl_registry_bind(registry, name, &wl_shm_interface,
                                          bind_version);
            wl_shm_add_listener(state->shm, &shm_listener, state);
            state->bind_shm = 1;
            printf("kde_wayland_registry_probe bind interface=wl_shm "
                   "name=%u version=%u\n",
                   name, bind_version);
        }
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        remember_global(&state->seat, name, version);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        remember_global(&state->wm_base, name, version);
    } else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
        remember_global(&state->dmabuf, name, version);
    } else if (strcmp(interface, "zwp_linux_explicit_synchronization_v1") == 0) {
        remember_global(&state->explicit_sync, name, version);
    } else if (strcmp(interface, "zwp_pointer_gestures_v1") == 0) {
        remember_global(&state->pointer_gestures, name, version);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data;
    (void)registry;
    printf("kde_wayland_registry_probe global_remove name=%u\n", name);
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove,
};

static int create_shm_file(size_t size)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char path[256];
    int fd;

    if (!runtime || !runtime[0])
        runtime = "/tmp";
    snprintf(path, sizeof(path), "%s/kde-wayland-registry-probe-XXXXXX",
             runtime);
    fd = mkstemp(path);
    if (fd < 0) {
        fprintf(stderr,
                "kde_wayland_registry_probe shm_file mkstemp path=%s errno=%d %s\n",
                path, errno, strerror(errno));
        return -1;
    }
    unlink(path);
    if (ftruncate(fd, (off_t)size) < 0) {
        fprintf(stderr,
                "kde_wayland_registry_probe shm_file ftruncate size=%zu errno=%d %s\n",
                size, errno, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static int create_tiny_shm_buffer(struct probe_state *state)
{
    enum { WIDTH = 1, HEIGHT = 1, STRIDE = 4, SIZE = WIDTH * HEIGHT * STRIDE };
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    uint32_t *pixels;
    int fd;
    int ret;

    if (!state->shm) {
        fprintf(stderr, "kde_wayland_registry_probe shm_buffer missing wl_shm\n");
        return -1;
    }

    set_phase("shm-file");
    fd = create_shm_file(SIZE);
    if (fd < 0)
        return -1;

    pixels = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) {
        fprintf(stderr,
                "kde_wayland_registry_probe shm_buffer mmap errno=%d %s\n",
                errno, strerror(errno));
        close(fd);
        return -1;
    }
    pixels[0] = 0xff204060u;
    munmap(pixels, SIZE);

    set_phase("shm-create-pool");
    pool = wl_shm_create_pool(state->shm, fd, SIZE);
    if (!pool) {
        fprintf(stderr, "kde_wayland_registry_probe shm_buffer create_pool failed\n");
        close(fd);
        return -1;
    }
    buffer = wl_shm_pool_create_buffer(pool, 0, WIDTH, HEIGHT, STRIDE,
                                       WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    if (!buffer) {
        fprintf(stderr, "kde_wayland_registry_probe shm_buffer create_buffer failed\n");
        return -1;
    }

    ret = bounded_roundtrip(state->display, "roundtrip-shm-buffer", 10000);
    wl_buffer_destroy(buffer);
    if (ret != 0) {
        fprintf(stderr,
                "kde_wayland_registry_probe shm_buffer roundtrip failed result=%d\n",
                ret);
        return -1;
    }

    state->make_buffer = 1;
    printf("kde_wayland_registry_probe shm_buffer status=PASS bytes=%d "
           "format=XRGB8888\n",
           SIZE);
    return 0;
}

int main(int argc, char **argv)
{
    struct probe_state state;
    int no_buffer = 0;
    int ret = 1;
    int roundtrip_ret;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-buffer") == 0)
            no_buffer = 1;
    }

    memset(&state, 0, sizeof(state));
    signal(SIGALRM, timeout_handler);
    alarm(90);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    fprintf(stderr, "kde_wayland_registry_probe env runtime=%s display=%s\n",
            getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "",
            getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "");

    set_phase("connect");
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "kde_wayland_registry_probe connect failed errno=%d %s\n",
                errno, strerror(errno));
        return 1;
    }
    print_display_fd_info(state.display);

    set_phase("registry");
    state.registry = wl_display_get_registry(state.display);
    if (!state.registry) {
        fprintf(stderr, "kde_wayland_registry_probe no registry\n");
        goto out;
    }
    wl_registry_add_listener(state.registry, &registry_listener, &state);

    roundtrip_ret = bounded_roundtrip(state.display, "roundtrip-registry", 30000);
    if (roundtrip_ret != 0) {
        fprintf(stderr,
                "kde_wayland_registry_probe roundtrip-registry failed result=%d\n",
                roundtrip_ret);
        ret = roundtrip_ret > 0 ? roundtrip_ret : 1;
        goto out;
    }
    roundtrip_ret = bounded_roundtrip(state.display, "roundtrip-formats", 10000);
    if (roundtrip_ret != 0) {
        fprintf(stderr,
                "kde_wayland_registry_probe roundtrip-formats failed result=%d\n",
                roundtrip_ret);
        ret = roundtrip_ret > 0 ? roundtrip_ret : 1;
        goto out;
    }

    set_phase("validate");
    if (!state.compositor.name || !state.shm_info.name || !state.seat.name ||
        !state.wm_base.name) {
        fprintf(stderr,
                "kde_wayland_registry_probe missing_globals compositor=%u "
                "wl_shm=%u wl_seat=%u xdg_wm_base=%u\n",
                state.compositor.name, state.shm_info.name, state.seat.name,
                state.wm_base.name);
        ret = 3;
        goto out;
    }
    if (!state.shm || !state.bind_shm) {
        fprintf(stderr, "kde_wayland_registry_probe wl_shm bind missing\n");
        ret = 4;
        goto out;
    }
    if (!state.shm_argb8888 || !state.shm_xrgb8888) {
        fprintf(stderr,
                "kde_wayland_registry_probe required_shm_formats_missing "
                "argb8888=%d xrgb8888=%d formats=%u\n",
                state.shm_argb8888, state.shm_xrgb8888, state.shm_formats);
        ret = 5;
        goto out;
    }
    if (!no_buffer && create_tiny_shm_buffer(&state) != 0) {
        ret = 6;
        goto out;
    }

    printf("kde_wayland_registry_probe result=PASS globals=%u "
           "wl_compositor=%u wl_compositor_version=%u "
           "wl_shm=%u wl_shm_version=%u shm_bound=%d shm_formats=%u "
           "shm_argb8888=%d shm_xrgb8888=%d shm_buffer=%d "
           "wl_seat=%u wl_seat_version=%u "
           "xdg_wm_base=%u xdg_wm_base_version=%u "
           "zwp_linux_dmabuf_v1=%u zwp_linux_dmabuf_v1_version=%u "
           "zwp_linux_explicit_synchronization_v1=%u "
           "zwp_linux_explicit_synchronization_v1_version=%u "
           "zwp_pointer_gestures_v1=%u zwp_pointer_gestures_v1_version=%u\n",
           state.globals, state.compositor.name, state.compositor.version,
           state.shm_info.name, state.shm_info.version, state.bind_shm,
           state.shm_formats, state.shm_argb8888, state.shm_xrgb8888,
           state.make_buffer, state.seat.name, state.seat.version,
           state.wm_base.name, state.wm_base.version, state.dmabuf.name,
           state.dmabuf.version, state.explicit_sync.name,
           state.explicit_sync.version, state.pointer_gestures.name,
           state.pointer_gestures.version);
    alarm(0);
    ret = 0;

out:
    if (state.shm)
        wl_shm_destroy(state.shm);
    if (state.registry)
        wl_registry_destroy(state.registry);
    if (state.display)
        wl_display_disconnect(state.display);
    return ret;
}
