#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

struct probe_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    uint32_t seat_name;
    uint32_t seat_version;
    uint32_t caps;
    int got_caps;
    int pointer_events;
    int keyboard_events;
};

struct sync_state {
    int done;
    uint32_t callback_data;
};

static const char *probe_phase = "startup";
static pid_t compositor_pid = -1;

static void timeout_handler(int signo)
{
    (void)signo;
    fprintf(stderr, "kde_wayland_seat_probe timeout phase=%s\n", probe_phase);
    _exit(2);
}

static void set_phase(const char *phase)
{
    probe_phase = phase;
    fprintf(stderr, "kde_wayland_seat_probe phase=%s\n", phase);
}

static int read_text_file(const char *path, char *buf, size_t size)
{
    int fd;
    ssize_t n;

    if (size == 0)
        return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        buf[0] = '\0';
        return 0;
    }
    n = read(fd, buf, size - 1);
    close(fd);
    if (n <= 0) {
        buf[0] = '\0';
        return 0;
    }
    buf[n] = '\0';
    return (int)n;
}

static void dump_proc_file(pid_t pid, const char *rel)
{
    char path[160];
    char buf[1536];
    int n;

    snprintf(path, sizeof(path), "/proc/%d/%s", pid, rel);
    n = read_text_file(path, buf, sizeof(buf));
    fprintf(stderr,
            "kde_wayland_seat_probe proc pid=%d file=%s bytes=%d begin\n",
            pid, rel, n);
    if (n > 0)
        fwrite(buf, 1, (size_t)n, stderr);
    if (n == 0 || buf[n - 1] != '\n')
        fputc('\n', stderr);
    fprintf(stderr, "kde_wayland_seat_probe proc end\n");
}

static void dump_fd_targets(pid_t pid)
{
    char dirpath[96];
    DIR *dir;
    struct dirent *de;

    snprintf(dirpath, sizeof(dirpath), "/proc/%d/fd", pid);
    dir = opendir(dirpath);
    if (!dir) {
        fprintf(stderr,
                "kde_wayland_seat_probe fd pid=%d opendir_errno=%d %s\n",
                pid, errno, strerror(errno));
        return;
    }
    while ((de = readdir(dir)) != NULL) {
        char linkpath[160];
        char target[256];
        int all_digits = 1;
        ssize_t n;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;
        snprintf(linkpath, sizeof(linkpath), "/proc/%d/fd/%s",
                 pid, de->d_name);
        n = readlink(linkpath, target, sizeof(target) - 1);
        if (n < 0) {
            fprintf(stderr,
                    "kde_wayland_seat_probe fd pid=%d fd=%s errno=%d %s\n",
                    pid, de->d_name, errno, strerror(errno));
            continue;
        }
        target[n] = '\0';
        fprintf(stderr, "kde_wayland_seat_probe fd pid=%d fd=%s target=%s\n",
                pid, de->d_name, target);
    }
    closedir(dir);
}

static void dump_task_state(pid_t pid)
{
    char dirpath[96];
    DIR *dir;
    struct dirent *de;

    if (pid <= 0)
        return;
    dump_proc_file(pid, "stat");
    dump_proc_file(pid, "status");
    dump_proc_file(pid, "wchan");
    dump_proc_file(pid, "syscall");
    dump_proc_file(pid, "stack");
    dump_fd_targets(pid);

    snprintf(dirpath, sizeof(dirpath), "/proc/%d/task", pid);
    dir = opendir(dirpath);
    if (!dir)
        return;
    while ((de = readdir(dir)) != NULL) {
        char rel[96];
        int all_digits = 1;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;
        snprintf(rel, sizeof(rel), "task/%s/stat", de->d_name);
        dump_proc_file(pid, rel);
        snprintf(rel, sizeof(rel), "task/%s/wchan", de->d_name);
        dump_proc_file(pid, rel);
        snprintf(rel, sizeof(rel), "task/%s/syscall", de->d_name);
        dump_proc_file(pid, rel);
        snprintf(rel, sizeof(rel), "task/%s/stack", de->d_name);
        dump_proc_file(pid, rel);
    }
    closedir(dir);
}

static long now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void print_display_fd_info(struct wl_display *display)
{
    int fd = wl_display_get_fd(display);
    struct ucred cred;
    socklen_t len = sizeof(cred);

    if (fd < 0) {
        fprintf(stderr, "kde_wayland_seat_probe display_fd=%d\n", fd);
        return;
    }

    memset(&cred, 0, sizeof(cred));
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
        fprintf(stderr,
                "kde_wayland_seat_probe display_fd=%d peer_pid=%d peer_uid=%d peer_gid=%d\n",
                fd, cred.pid, cred.uid, cred.gid);
        compositor_pid = cred.pid;
    } else {
        fprintf(stderr,
                "kde_wayland_seat_probe display_fd=%d peercred_errno=%d %s\n",
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

static int bounded_roundtrip(struct wl_display *display, const char *phase, int timeout_ms)
{
    struct sync_state sync = { 0, 0 };
    struct wl_callback *callback;
    struct pollfd pfd;
    long deadline;
    int fd;

    set_phase(phase);
    callback = wl_display_sync(display);
    if (!callback) {
        fprintf(stderr, "kde_wayland_seat_probe %s sync-create failed errno=%d %s\n",
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
            int pending = -1;

            if (fd >= 0)
                (void)ioctl(fd, FIONREAD, &pending);
            fprintf(stderr,
                    "kde_wayland_seat_probe timeout phase=%s sync_done=%d fd=%d pending_read=%d\n",
                    phase, sync.done, fd, pending);
            dump_task_state(compositor_pid);
            wl_callback_destroy(callback);
            return 2;
        }

        while (wl_display_prepare_read(display) != 0) {
            ret = wl_display_dispatch_pending(display);
            if (ret < 0) {
                fprintf(stderr,
                        "kde_wayland_seat_probe %s dispatch-pending failed errno=%d %s\n",
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
                    "kde_wayland_seat_probe %s flush failed errno=%d %s\n",
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
                    "kde_wayland_seat_probe %s poll failed errno=%d %s\n",
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
                    "kde_wayland_seat_probe %s poll revents=0x%x without POLLIN\n",
                    phase, pfd.revents);
            wl_callback_destroy(callback);
            return -1;
        }

        ret = wl_display_read_events(display);
        if (ret < 0) {
            fprintf(stderr,
                    "kde_wayland_seat_probe %s read-events failed errno=%d %s\n",
                    phase, errno, strerror(errno));
            wl_callback_destroy(callback);
            return -1;
        }
        ret = wl_display_dispatch_pending(display);
        if (ret < 0) {
            fprintf(stderr,
                    "kde_wayland_seat_probe %s dispatch-after-read failed errno=%d %s\n",
                    phase, errno, strerror(errno));
            wl_callback_destroy(callback);
            return -1;
        }
    }

    fprintf(stderr, "kde_wayland_seat_probe %s done callback_data=%u\n",
            phase, sync.callback_data);
    return 0;
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)serial;
    (void)surface;
    (void)sx;
    (void)sy;
    state->pointer_events++;
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)serial;
    (void)surface;
    state->pointer_events++;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)time;
    (void)sx;
    (void)sy;
    state->pointer_events++;
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t button_state)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)serial;
    (void)time;
    (void)button;
    (void)button_state;
    state->pointer_events++;
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
    state->pointer_events++;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    struct probe_state *state = data;
    (void)pointer;
    state->pointer_events++;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)source;
    state->pointer_events++;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                              uint32_t axis)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)time;
    (void)axis;
    state->pointer_events++;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                  int32_t discrete)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)axis;
    (void)discrete;
    state->pointer_events++;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis,
                                  int32_t value120)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)axis;
    (void)value120;
    state->pointer_events++;
}

static void pointer_axis_relative_direction(void *data, struct wl_pointer *pointer,
                                            uint32_t axis, uint32_t direction)
{
    struct probe_state *state = data;
    (void)pointer;
    (void)axis;
    (void)direction;
    state->pointer_events++;
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

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
                            int32_t fd, uint32_t size)
{
    struct probe_state *state = data;
    (void)keyboard;
    (void)format;
    (void)size;
    if (fd >= 0)
        close(fd);
    state->keyboard_events++;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys)
{
    struct probe_state *state = data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
    state->keyboard_events++;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface)
{
    struct probe_state *state = data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    state->keyboard_events++;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t key_state)
{
    struct probe_state *state = data;
    (void)keyboard;
    (void)serial;
    (void)time;
    (void)key;
    (void)key_state;
    state->keyboard_events++;
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group)
{
    struct probe_state *state = data;
    (void)keyboard;
    (void)serial;
    (void)mods_depressed;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
    state->keyboard_events++;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate,
                                 int32_t delay)
{
    struct probe_state *state = data;
    (void)keyboard;
    (void)rate;
    (void)delay;
    state->keyboard_events++;
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
    keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct probe_state *state = data;
    state->caps = caps;
    state->got_caps = 1;

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
        state->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(state->pointer, &pointer_listener, state);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && state->pointer) {
        wl_pointer_release(state->pointer);
        state->pointer = NULL;
    }

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !state->keyboard) {
        state->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && state->keyboard) {
        wl_keyboard_release(state->keyboard);
        state->keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    printf("kde_wayland_seat_probe seat_name=%s\n", name ? name : "");
}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version)
{
    struct probe_state *state = data;

    if (strcmp(interface, wl_seat_interface.name) == 0 && !state->seat) {
        uint32_t bind_version = version < 8 ? version : 8;
        if (bind_version < 1)
            bind_version = 1;
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, bind_version);
        state->seat_name = name;
        state->seat_version = bind_version;
        wl_seat_add_listener(state->seat, &seat_listener, state);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    struct probe_state *state = data;
    (void)registry;
    if (state->seat_name == name) {
        state->seat_name = 0;
        state->caps = 0;
    }
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_remove,
};

int main(int argc, char **argv)
{
    struct probe_state state;
    int pointer_version;
    int roundtrip_only = 0;
    int roundtrip_timeout_ms;
    int ret = 1;
    int roundtrip_ret;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--roundtrip-only") == 0)
            roundtrip_only = 1;
    }

    memset(&state, 0, sizeof(state));
    signal(SIGALRM, timeout_handler);
    alarm(120);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    fprintf(stderr, "kde_wayland_seat_probe env runtime=%s display=%s\n",
            getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "",
            getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "");

    set_phase("connect");
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "kde_wayland_seat_probe connect failed: %s\n", strerror(errno));
        return 1;
    }
    print_display_fd_info(state.display);

    set_phase("registry");
    state.registry = wl_display_get_registry(state.display);
    if (!state.registry) {
        fprintf(stderr, "kde_wayland_seat_probe no registry\n");
        goto out;
    }
    wl_registry_add_listener(state.registry, &registry_listener, &state);

    roundtrip_timeout_ms = roundtrip_only ? 5000 : 30000;

    roundtrip_ret = bounded_roundtrip(state.display, "roundtrip-1",
                                      roundtrip_timeout_ms);
    if (roundtrip_ret != 0) {
        fprintf(stderr, "kde_wayland_seat_probe roundtrip-1 failed result=%d\n",
                roundtrip_ret);
        ret = roundtrip_ret > 0 ? roundtrip_ret : 1;
        goto out;
    }
    roundtrip_ret = bounded_roundtrip(state.display, "roundtrip-2",
                                      roundtrip_timeout_ms);
    if (roundtrip_ret != 0) {
        fprintf(stderr, "kde_wayland_seat_probe roundtrip-2 failed result=%d\n",
                roundtrip_ret);
        ret = roundtrip_ret > 0 ? roundtrip_ret : 1;
        goto out;
    }
    if (roundtrip_only) {
        printf("kde_wayland_seat_probe roundtrip_only=PASS\n");
        alarm(0);
        ret = 0;
        goto out;
    }
    roundtrip_ret = bounded_roundtrip(state.display, "roundtrip-3", 30000);
    if (roundtrip_ret != 0) {
        fprintf(stderr, "kde_wayland_seat_probe roundtrip-3 failed result=%d\n",
                roundtrip_ret);
        ret = roundtrip_ret > 0 ? roundtrip_ret : 1;
        goto out;
    }

    set_phase("validate");
    if (!state.seat) {
        fprintf(stderr, "kde_wayland_seat_probe no wl_seat\n");
        goto out;
    }
    if (!state.got_caps) {
        fprintf(stderr, "kde_wayland_seat_probe no seat capabilities\n");
        goto out;
    }
    if (!(state.caps & WL_SEAT_CAPABILITY_POINTER)) {
        fprintf(stderr, "kde_wayland_seat_probe no pointer capability caps=0x%x\n",
                state.caps);
        goto out;
    }
    if (!(state.caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
        fprintf(stderr, "kde_wayland_seat_probe no keyboard capability caps=0x%x\n",
                state.caps);
        goto out;
    }
    if (!state.pointer) {
        fprintf(stderr, "kde_wayland_seat_probe pointer capability without object\n");
        goto out;
    }

    pointer_version = wl_proxy_get_version((struct wl_proxy *)state.pointer);
    if (pointer_version <= 0) {
        fprintf(stderr, "kde_wayland_seat_probe invalid pointer version=%d\n",
                pointer_version);
        goto out;
    }

    printf("kde_wayland_seat_probe caps=0x%x seat_version=%u pointer_version=%d "
           "pointer_events=%d keyboard_events=%d\n",
           state.caps, state.seat_version, pointer_version,
           state.pointer_events, state.keyboard_events);
    alarm(0);
    ret = 0;

out:
    if (state.pointer)
        wl_pointer_release(state.pointer);
    if (state.keyboard)
        wl_keyboard_release(state.keyboard);
    if (state.seat)
        wl_seat_release(state.seat);
    if (state.registry)
        wl_registry_destroy(state.registry);
    wl_display_disconnect(state.display);
    return ret;
}
