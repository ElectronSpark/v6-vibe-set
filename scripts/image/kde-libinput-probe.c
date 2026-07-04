#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <libinput.h>
#include <libudev.h>

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 800
#define DEFAULT_TIMEOUT_MS 1000
#define MAX_EVENT_DEVS 8
#define R9_PREFIX "kde_libinput_probe"

struct r9_options {
    int enabled;
    int width;
    int height;
    int timeout_ms;
};

struct r9_counts {
    int event_devices_open;
    int abs_ioctl_attempts;
    int abs_ioctl_ok;
    int evdev_abs_samples;
    int mouse_samples;
    int libinput_events;
    int libinput_abs_samples;
    int fatal_errors;
};

struct event_dev {
    char path[32];
    int fd;
};

struct xv6_mouse_event {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
    uint8_t flags;
    int8_t dz;
    uint8_t pad[1];
};

typedef char xv6_mouse_event_must_be_8_bytes[
    sizeof(struct xv6_mouse_event) == 8 ? 1 : -1];

struct libinput_probe {
    struct udev *udev;
    struct libinput *li;
    int fd;
    int available;
    int setup_rc;
    int setup_errno;
    const char *reason;
};

static int default_probe(void)
{
    int fd0 = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    int fd1 = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);

    printf("kde_libinput_probe event0=%d event1=%d\n", fd0, fd1);
    if (fd0 >= 0)
        close(fd0);
    if (fd1 >= 0)
        close(fd1);
    return (fd0 >= 0 || fd1 >= 0) ? 0 : 1;
}

static long long now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int parse_positive_int(const char *text, const char *name, int *out)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || !text[0] || (end && *end) || value <= 0 ||
        value > INT_MAX) {
        printf("%s r9_arg_error name=%s value=%s\n", R9_PREFIX, name, text);
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int parse_options(int argc, char **argv, struct r9_options *opts)
{
    const char *env = getenv("KDE_LIBINPUT_PROBE_R9_CURSOR");

    opts->enabled = env && strcmp(env, "1") == 0;
    opts->width = DEFAULT_WIDTH;
    opts->height = DEFAULT_HEIGHT;
    opts->timeout_ms = DEFAULT_TIMEOUT_MS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--r9-cursor-contract") == 0) {
            opts->enabled = 1;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], "width", &opts->width) != 0)
                return -1;
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], "height", &opts->height) != 0)
                return -1;
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            if (parse_positive_int(argv[++i], "timeout_ms",
                                   &opts->timeout_ms) != 0)
                return -1;
        } else {
            printf("%s r9_arg_error name=unknown value=%s\n",
                   R9_PREFIX, argv[i]);
            return -1;
        }
    }
    return 0;
}

static const char *axis_name(unsigned int code)
{
    if (code == ABS_X)
        return "ABS_X";
    if (code == ABS_Y)
        return "ABS_Y";
    return "ABS_UNKNOWN";
}

static void print_absinfo(const char *path, int fd, unsigned int code,
                          struct r9_counts *counts)
{
    struct input_absinfo ai;
    int rc;
    int err;

    memset(&ai, 0, sizeof(ai));
    errno = 0;
    rc = ioctl(fd, EVIOCGABS(code), &ai);
    err = rc < 0 ? errno : 0;
    counts->abs_ioctl_attempts++;
    if (rc == 0)
        counts->abs_ioctl_ok++;

    printf("%s r9_evdev_abs path=%s op=EVIOCGABS(%s) rc=%d errno=%d "
           "value=%d min=%d max=%d fuzz=%d flat=%d resolution=%d\n",
           R9_PREFIX, path, axis_name(code), rc, err, ai.value, ai.minimum,
           ai.maximum, ai.fuzz, ai.flat, ai.resolution);
}

static int open_event_devices(struct event_dev devs[MAX_EVENT_DEVS],
                              struct r9_counts *counts)
{
    int opened = 0;

    for (int i = 0; i < MAX_EVENT_DEVS; i++) {
        int err;

        snprintf(devs[i].path, sizeof(devs[i].path), "/dev/input/event%d", i);
        errno = 0;
        devs[i].fd = open(devs[i].path, O_RDONLY | O_NONBLOCK);
        err = devs[i].fd < 0 ? errno : 0;
        printf("%s r9_event_device path=%s fd=%d errno=%d\n",
               R9_PREFIX, devs[i].path, devs[i].fd, err);
        if (devs[i].fd < 0)
            continue;
        opened++;
        counts->event_devices_open++;
        print_absinfo(devs[i].path, devs[i].fd, ABS_X, counts);
        print_absinfo(devs[i].path, devs[i].fd, ABS_Y, counts);
    }
    return opened;
}

static void close_event_devices(struct event_dev devs[MAX_EVENT_DEVS])
{
    for (int i = 0; i < MAX_EVENT_DEVS; i++) {
        if (devs[i].fd >= 0) {
            close(devs[i].fd);
            devs[i].fd = -1;
        }
    }
}

static void drain_event_device(struct event_dev *dev, struct r9_counts *counts)
{
    for (;;) {
        struct input_event ev;
        ssize_t n;

        errno = 0;
        n = read(dev->fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_ABS && (ev.code == ABS_X || ev.code == ABS_Y)) {
                counts->evdev_abs_samples++;
                printf("%s r9_evdev_sample path=%s type=EV_ABS code=%s "
                       "value=%d sec=%lld usec=%lld\n",
                       R9_PREFIX, dev->path, axis_name(ev.code), ev.value,
                       (long long)ev.input_event_sec,
                       (long long)ev.input_event_usec);
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        if (n < 0) {
            printf("%s r9_evdev_read_status path=%s rc=%zd errno=%d\n",
                   R9_PREFIX, dev->path, n, errno);
            return;
        }
        if (n > 0) {
            printf("%s r9_evdev_read_status path=%s rc=%zd errno=0 "
                   "reason=short_read\n",
                   R9_PREFIX, dev->path, n);
        }
        return;
    }
}

static int open_mouse_device(void)
{
    int fd;
    int err;

    errno = 0;
    fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
    err = fd < 0 ? errno : 0;
    printf("%s r9_mouse_device path=/dev/mouse fd=%d errno=%d\n",
           R9_PREFIX, fd, err);
    return fd;
}

static void drain_mouse_device(int fd, struct r9_counts *counts)
{
    for (;;) {
        struct xv6_mouse_event ev;
        ssize_t n;

        errno = 0;
        n = read(fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            counts->mouse_samples++;
            printf("%s r9_mouse_sample path=/dev/mouse x=%u y=%u buttons=%u "
                   "flags=%u dz=%d\n",
                   R9_PREFIX, (unsigned int)(uint16_t)ev.dx,
                   (unsigned int)(uint16_t)ev.dy, (unsigned int)ev.buttons,
                   (unsigned int)ev.flags, (int)ev.dz);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        if (n < 0) {
            printf("%s r9_mouse_read_status path=/dev/mouse rc=%zd errno=%d\n",
                   R9_PREFIX, n, errno);
            return;
        }
        if (n > 0) {
            printf("%s r9_mouse_read_status path=/dev/mouse rc=%zd errno=0 "
                   "reason=short_read\n",
                   R9_PREFIX, n);
        }
        return;
    }
}

static int open_restricted(const char *path, int flags, void *user_data)
{
    (void)user_data;
    return open(path, flags);
}

static void close_restricted(int fd, void *user_data)
{
    (void)user_data;
    close(fd);
}

static const struct libinput_interface li_interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static void setup_libinput_probe(struct libinput_probe *probe)
{
    memset(probe, 0, sizeof(*probe));
    probe->fd = -1;
    probe->setup_rc = -1;
    probe->reason = "not_started";

    errno = 0;
    probe->udev = udev_new();
    if (!probe->udev) {
        probe->setup_errno = errno ? errno : ENOMEM;
        probe->reason = "udev_new_failed";
        printf("%s r9_libinput_status rc=-1 errno=%d reason=%s\n",
               R9_PREFIX, probe->setup_errno, probe->reason);
        return;
    }

    errno = 0;
    probe->li = libinput_udev_create_context(&li_interface, NULL, probe->udev);
    if (!probe->li) {
        probe->setup_errno = errno ? errno : ENODEV;
        probe->reason = "create_context_failed";
        printf("%s r9_libinput_status rc=-1 errno=%d reason=%s\n",
               R9_PREFIX, probe->setup_errno, probe->reason);
        return;
    }

    errno = 0;
    probe->setup_rc = libinput_udev_assign_seat(probe->li, "seat0");
    if (probe->setup_rc < 0) {
        probe->setup_errno = errno ? errno : EIO;
        probe->reason = "assign_seat_failed";
        printf("%s r9_libinput_status rc=%d errno=%d reason=%s\n",
               R9_PREFIX, probe->setup_rc, probe->setup_errno,
               probe->reason);
        return;
    }

    probe->fd = libinput_get_fd(probe->li);
    if (probe->fd < 0) {
        probe->setup_errno = errno ? errno : ENODEV;
        probe->reason = "get_fd_failed";
        printf("%s r9_libinput_status rc=-1 errno=%d reason=%s\n",
               R9_PREFIX, probe->setup_errno, probe->reason);
        return;
    }

    probe->available = 1;
    probe->setup_rc = 0;
    probe->setup_errno = 0;
    probe->reason = "ready";
    printf("%s r9_libinput_status rc=0 errno=0 reason=ready fd=%d\n",
           R9_PREFIX, probe->fd);
}

static void cleanup_libinput_probe(struct libinput_probe *probe)
{
    if (probe->li)
        probe->li = libinput_unref(probe->li);
    if (probe->udev)
        probe->udev = udev_unref(probe->udev);
    probe->fd = -1;
    probe->available = 0;
}

static void drain_libinput_probe(struct libinput_probe *probe,
                                 const struct r9_options *opts,
                                 struct r9_counts *counts)
{
    int rc;

    if (!probe->available)
        return;

    errno = 0;
    rc = libinput_dispatch(probe->li);
    if (rc < 0) {
        printf("%s r9_libinput_status rc=%d errno=%d reason=dispatch_failed\n",
               R9_PREFIX, rc, errno);
        return;
    }

    for (;;) {
        struct libinput_event *event = libinput_get_event(probe->li);
        enum libinput_event_type type;

        if (!event)
            return;
        counts->libinput_events++;
        type = libinput_event_get_type(event);
        if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
            struct libinput_event_pointer *pointer =
                libinput_event_get_pointer_event(event);
            double x =
                libinput_event_pointer_get_absolute_x_transformed(
                    pointer, (uint32_t)opts->width);
            double y =
                libinput_event_pointer_get_absolute_y_transformed(
                    pointer, (uint32_t)opts->height);

            counts->libinput_abs_samples++;
            printf("%s r9_libinput_absolute_sample type=POINTER_MOTION_ABSOLUTE "
                   "x=%.3f y=%.3f width=%d height=%d\n",
                   R9_PREFIX, x, y, opts->width, opts->height);
        }
        libinput_event_destroy(event);
    }
}

enum poll_kind {
    POLL_KIND_EVENT,
    POLL_KIND_MOUSE,
    POLL_KIND_LIBINPUT,
};

struct poll_map {
    enum poll_kind kind;
    int index;
};

static int build_pollfds(const struct event_dev devs[MAX_EVENT_DEVS],
                         int mouse_fd, const struct libinput_probe *probe,
                         struct pollfd *pfds, struct poll_map *map,
                         int max_fds)
{
    int n = 0;

    for (int i = 0; i < MAX_EVENT_DEVS && n < max_fds; i++) {
        if (devs[i].fd < 0)
            continue;
        pfds[n].fd = devs[i].fd;
        pfds[n].events = POLLIN;
        pfds[n].revents = 0;
        map[n].kind = POLL_KIND_EVENT;
        map[n].index = i;
        n++;
    }
    if (mouse_fd >= 0 && n < max_fds) {
        pfds[n].fd = mouse_fd;
        pfds[n].events = POLLIN;
        pfds[n].revents = 0;
        map[n].kind = POLL_KIND_MOUSE;
        map[n].index = -1;
        n++;
    }
    if (probe->available && probe->fd >= 0 && n < max_fds) {
        pfds[n].fd = probe->fd;
        pfds[n].events = POLLIN;
        pfds[n].revents = 0;
        map[n].kind = POLL_KIND_LIBINPUT;
        map[n].index = -1;
        n++;
    }
    return n;
}

static void drain_all_samples(struct event_dev devs[MAX_EVENT_DEVS],
                              int mouse_fd, struct libinput_probe *probe,
                              const struct r9_options *opts,
                              struct r9_counts *counts)
{
    for (int i = 0; i < MAX_EVENT_DEVS; i++) {
        if (devs[i].fd >= 0)
            drain_event_device(&devs[i], counts);
    }
    if (mouse_fd >= 0)
        drain_mouse_device(mouse_fd, counts);
    drain_libinput_probe(probe, opts, counts);
}

static void sample_devices(struct event_dev devs[MAX_EVENT_DEVS],
                           int mouse_fd, struct libinput_probe *probe,
                           const struct r9_options *opts,
                           struct r9_counts *counts)
{
    long long deadline = now_ms() + opts->timeout_ms;

    drain_all_samples(devs, mouse_fd, probe, opts, counts);
    while (now_ms() < deadline) {
        struct pollfd pfds[MAX_EVENT_DEVS + 2];
        struct poll_map map[MAX_EVENT_DEVS + 2];
        int remaining = (int)(deadline - now_ms());
        int nfds;
        int rc;

        if (remaining < 0)
            remaining = 0;
        nfds = build_pollfds(devs, mouse_fd, probe, pfds, map,
                             (int)(sizeof(pfds) / sizeof(pfds[0])));
        if (nfds == 0)
            return;

        errno = 0;
        rc = poll(pfds, (nfds_t)nfds, remaining);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0) {
            printf("%s r9_poll_status rc=%d errno=%d\n",
                   R9_PREFIX, rc, errno);
            return;
        }
        if (rc == 0)
            return;

        for (int i = 0; i < nfds; i++) {
            if ((pfds[i].revents & (POLLIN | POLLERR | POLLHUP)) == 0)
                continue;
            if (map[i].kind == POLL_KIND_EVENT)
                drain_event_device(&devs[map[i].index], counts);
            else if (map[i].kind == POLL_KIND_MOUSE)
                drain_mouse_device(mouse_fd, counts);
            else if (map[i].kind == POLL_KIND_LIBINPUT)
                drain_libinput_probe(probe, opts, counts);
        }
    }
}

static const char *summary_result(const struct r9_counts *counts)
{
    if (counts->fatal_errors)
        return "FAIL";
    if (counts->abs_ioctl_attempts > 0)
        return "PASS";
    return "NO_SAMPLES";
}

static int run_r9_cursor_contract(const struct r9_options *opts)
{
    struct event_dev devs[MAX_EVENT_DEVS];
    struct r9_counts counts;
    struct libinput_probe libinput_probe;
    int mouse_fd;
    const char *li_reason;

    memset(&counts, 0, sizeof(counts));
    for (int i = 0; i < MAX_EVENT_DEVS; i++)
        devs[i].fd = -1;

    printf("%s r9_cursor_contract enabled=1 width=%d height=%d timeout_ms=%d\n",
           R9_PREFIX, opts->width, opts->height, opts->timeout_ms);

    open_event_devices(devs, &counts);
    mouse_fd = open_mouse_device();
    setup_libinput_probe(&libinput_probe);
    sample_devices(devs, mouse_fd, &libinput_probe, opts, &counts);

    if (libinput_probe.available) {
        li_reason = counts.libinput_abs_samples > 0 ?
            "absolute_samples" : "no_absolute_samples";
        printf("%s r9_libinput_status rc=0 errno=0 reason=%s "
               "events=%d absolute_samples=%d\n",
               R9_PREFIX, li_reason, counts.libinput_events,
               counts.libinput_abs_samples);
    }

    if (mouse_fd >= 0)
        close(mouse_fd);
    cleanup_libinput_probe(&libinput_probe);
    close_event_devices(devs);

    printf("%s r9_summary event_devices_open=%d abs_ioctl_attempts=%d "
           "abs_ioctl_ok=%d evdev_abs_samples=%d mouse_samples=%d "
           "libinput_events=%d libinput_abs_samples=%d result=%s\n",
           R9_PREFIX, counts.event_devices_open, counts.abs_ioctl_attempts,
           counts.abs_ioctl_ok, counts.evdev_abs_samples,
           counts.mouse_samples, counts.libinput_events,
           counts.libinput_abs_samples, summary_result(&counts));

    return counts.fatal_errors ? 1 : 0;
}

int main(int argc, char **argv)
{
    struct r9_options opts;

    if (parse_options(argc, argv, &opts) != 0)
        return 2;
    if (!opts.enabled)
        return default_probe();
    return run_r9_cursor_contract(&opts);
}
