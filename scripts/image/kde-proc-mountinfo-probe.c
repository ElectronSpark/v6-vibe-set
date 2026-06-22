#ifndef GLIB_VERSION_MIN_REQUIRED
#define GLIB_VERSION_MIN_REQUIRED GLIB_VERSION_2_80
#endif
#ifndef GLIB_VERSION_MAX_ALLOWED
#define GLIB_VERSION_MAX_ALLOWED GLIB_VERSION_2_80
#endif

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixmounts.h>
#include <glib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char *step, const char *detail)
{
    printf("kde_proc_mountinfo_probe fail_step=%s detail=%s\n",
           step, detail ? detail : "");
    printf("kde_proc_mountinfo_probe result=FAIL\n");
    return 1;
}

static int check_posix_read(void)
{
    char buf[512];
    int fd = open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "open errno=%d %s",
                 errno, strerror(errno));
        return fail("posix-open", detail);
    }

    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    int saved_errno = errno;
    close(fd);
    if (n <= 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "read n=%ld errno=%d %s",
                 (long)n, saved_errno, strerror(saved_errno));
        return fail("posix-read", detail);
    }
    buf[n] = '\0';
    if (strstr(buf, " - ") == NULL || strstr(buf, " / ") == NULL)
        return fail("posix-parse", "missing mountinfo separators");

    printf("kde_proc_mountinfo_probe posix bytes=%ld first=%.*s\n",
           (long)n, 80, buf);
    return 0;
}

static int check_inotify(void)
{
    int fd = inotify_init1(IN_CLOEXEC);
    if (fd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "inotify_init1 errno=%d %s",
                 errno, strerror(errno));
        return fail("inotify-init", detail);
    }

    int wd = inotify_add_watch(fd, "/proc/self/mountinfo", IN_MODIFY);
    int saved_errno = errno;
    close(fd);
    if (wd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "add_watch errno=%d %s",
                 saved_errno, strerror(saved_errno));
        return fail("inotify-watch", detail);
    }

    printf("kde_proc_mountinfo_probe inotify wd=%d\n", wd);
    return 0;
}

static int check_inotify_epoll(void)
{
    int ifd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ifd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "inotify_init1 errno=%d %s",
                 errno, strerror(errno));
        return fail("inotify-epoll-init", detail);
    }

    int wd = inotify_add_watch(ifd, "/proc/self/mountinfo", IN_MODIFY);
    if (wd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "add_watch errno=%d %s",
                 errno, strerror(errno));
        close(ifd);
        return fail("inotify-epoll-watch", detail);
    }

    int efd = epoll_create1(EPOLL_CLOEXEC);
    if (efd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "epoll_create1 errno=%d %s",
                 errno, strerror(errno));
        close(ifd);
        return fail("inotify-epoll-create", detail);
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = ifd;
    int ret = epoll_ctl(efd, EPOLL_CTL_ADD, ifd, &ev);
    int saved_errno = errno;
    close(efd);
    close(ifd);
    if (ret != 0) {
        char detail[128];
        snprintf(detail, sizeof(detail), "epoll_ctl ret=%d errno=%d %s",
                 ret, saved_errno, strerror(saved_errno));
        return fail("inotify-epoll-ctl", detail);
    }

    printf("kde_proc_mountinfo_probe inotify_epoll wd=%d\n", wd);
    return 0;
}

static int check_mountinfo_epoll(void)
{
    int fd = open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "open errno=%d %s",
                 errno, strerror(errno));
        return fail("mountinfo-epoll-open", detail);
    }

    int efd = epoll_create1(EPOLL_CLOEXEC);
    if (efd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "epoll_create1 errno=%d %s",
                 errno, strerror(errno));
        close(fd);
        return fail("mountinfo-epoll-create", detail);
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    int ret = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
    int saved_errno = errno;
    close(efd);
    close(fd);
    if (ret != 0) {
        char detail[128];
        snprintf(detail, sizeof(detail), "epoll_ctl ret=%d errno=%d %s",
                 ret, saved_errno, strerror(saved_errno));
        return fail("mountinfo-epoll-ctl", detail);
    }

    printf("kde_proc_mountinfo_probe mountinfo_epoll=PASS\n");
    return 0;
}

static int check_mount_state_paths(void)
{
    struct stat st;

    if (stat("/run/mount", &st) != 0 || !S_ISDIR(st.st_mode)) {
        char detail[96];
        snprintf(detail, sizeof(detail), "stat /run/mount errno=%d %s",
                 errno, strerror(errno));
        return fail("run-mount", detail);
    }

    int fd = open("/etc/mtab", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        char detail[96];
        snprintf(detail, sizeof(detail), "open /etc/mtab errno=%d %s",
                 errno, strerror(errno));
        return fail("etc-mtab", detail);
    }
    close(fd);

    printf("kde_proc_mountinfo_probe mount_state_paths=PASS\n");
    return 0;
}

static int check_gio_channel(void)
{
    GError *error = NULL;
    GIOChannel *channel =
        g_io_channel_new_file("/proc/self/mountinfo", "r", &error);
    if (channel == NULL) {
        char detail[256];
        snprintf(detail, sizeof(detail), "domain=%s code=%d message=%s",
                 error ? g_quark_to_string(error->domain) : "",
                 error ? error->code : 0,
                 error && error->message ? error->message : "");
        g_clear_error(&error);
        return fail("gio-channel-open", detail);
    }

    gchar *line = NULL;
    gsize length = 0;
    GIOStatus status =
        g_io_channel_read_line(channel, &line, &length, NULL, &error);
    if (status != G_IO_STATUS_NORMAL || line == NULL || length == 0) {
        char detail[256];
        snprintf(detail, sizeof(detail), "status=%d domain=%s code=%d message=%s",
                 status,
                 error ? g_quark_to_string(error->domain) : "",
                 error ? error->code : 0,
                 error && error->message ? error->message : "");
        g_free(line);
        g_clear_error(&error);
        g_io_channel_shutdown(channel, TRUE, NULL);
        g_io_channel_unref(channel);
        return fail("gio-channel-read", detail);
    }

    printf("kde_proc_mountinfo_probe gio line=%.*s\n", 80, line);
    g_free(line);
    g_io_channel_shutdown(channel, TRUE, NULL);
    g_io_channel_unref(channel);
    return 0;
}

static int check_gunix_mounts(void)
{
    guint64 time_read = 0;
    GList *mounts = g_unix_mounts_get(&time_read);
    int count = g_list_length(mounts);
    if (count <= 0) {
        g_list_free_full(mounts, (GDestroyNotify)g_unix_mount_free);
        return fail("gunix-mounts", "empty mount list");
    }

    gboolean saw_root = FALSE;
    for (GList *l = mounts; l != NULL; l = l->next) {
        GUnixMountEntry *entry = l->data;
        const char *path = g_unix_mount_get_mount_path(entry);
        if (path != NULL && strcmp(path, "/") == 0) {
            saw_root = TRUE;
            break;
        }
    }
    g_list_free_full(mounts, (GDestroyNotify)g_unix_mount_free);
    if (!saw_root)
        return fail("gunix-mounts-root", "root mount missing");

    GUnixMountMonitor *monitor = g_unix_mount_monitor_get();
    if (monitor == NULL)
        return fail("gunix-monitor", "monitor is NULL");
    g_object_unref(monitor);

    printf("kde_proc_mountinfo_probe gunix count=%d time=%llu\n",
           count, (unsigned long long)time_read);
    return 0;
}

static int check_libmount_monitor(void)
{
    typedef void *(*mnt_new_monitor_fn)(void);
    typedef int (*mnt_monitor_enable_kernel_fn)(void *, int);
    typedef int (*mnt_monitor_enable_userspace_fn)(void *, int, const char *);
    typedef int (*mnt_monitor_get_fd_fn)(void *);
    typedef void (*mnt_unref_monitor_fn)(void *);

    void *lib = dlopen("libmount.so.1", RTLD_NOW | RTLD_LOCAL);
    if (lib == NULL)
        return fail("libmount-dlopen", dlerror());

    mnt_new_monitor_fn mnt_new_monitor =
        (mnt_new_monitor_fn)dlsym(lib, "mnt_new_monitor");
    mnt_monitor_enable_kernel_fn mnt_monitor_enable_kernel =
        (mnt_monitor_enable_kernel_fn)dlsym(lib, "mnt_monitor_enable_kernel");
    mnt_monitor_enable_userspace_fn mnt_monitor_enable_userspace =
        (mnt_monitor_enable_userspace_fn)dlsym(lib, "mnt_monitor_enable_userspace");
    mnt_monitor_get_fd_fn mnt_monitor_get_fd =
        (mnt_monitor_get_fd_fn)dlsym(lib, "mnt_monitor_get_fd");
    mnt_unref_monitor_fn mnt_unref_monitor =
        (mnt_unref_monitor_fn)dlsym(lib, "mnt_unref_monitor");

    if (mnt_new_monitor == NULL || mnt_monitor_enable_kernel == NULL ||
        mnt_monitor_enable_userspace == NULL || mnt_monitor_get_fd == NULL ||
        mnt_unref_monitor == NULL) {
        dlclose(lib);
        return fail("libmount-symbols", "missing symbol");
    }

    void *monitor = mnt_new_monitor();
    if (monitor == NULL) {
        dlclose(lib);
        return fail("libmount-new-monitor", "NULL");
    }

    int ret_kernel = mnt_monitor_enable_kernel(monitor, 1);
    int errno_kernel = errno;
    int fd_kernel = mnt_monitor_get_fd(monitor);
    int ret_userspace = mnt_monitor_enable_userspace(monitor, 1, NULL);
    int errno_userspace = errno;
    int fd_all = mnt_monitor_get_fd(monitor);
    mnt_unref_monitor(monitor);
    dlclose(lib);

    if (ret_kernel != 0) {
        char detail[128];
        snprintf(detail, sizeof(detail), "kernel ret=%d fd=%d errno=%d %s",
                 ret_kernel, fd_kernel, errno_kernel, strerror(errno_kernel));
        return fail("libmount-kernel-monitor", detail);
    }
    if (ret_userspace != 0 || fd_all < 0) {
        char detail[128];
        snprintf(detail, sizeof(detail), "userspace ret=%d fd=%d errno=%d %s",
                 ret_userspace, fd_all, errno_userspace,
                 strerror(errno_userspace));
        return fail("libmount-userspace-monitor", detail);
    }

    printf("kde_proc_mountinfo_probe libmount kernel_ret=%d kernel_fd=%d all_fd=%d\n",
           ret_kernel, fd_kernel, fd_all);
    return 0;
}

int main(void)
{
    if (check_posix_read() != 0)
        return 1;
    if (check_inotify() != 0)
        return 1;
    if (check_inotify_epoll() != 0)
        return 1;
    if (check_mountinfo_epoll() != 0)
        return 1;
    if (check_mount_state_paths() != 0)
        return 1;
    if (check_gio_channel() != 0)
        return 1;
    if (check_gunix_mounts() != 0)
        return 1;
    if (check_libmount_monitor() != 0)
        return 1;

    printf("kde_proc_mountinfo_probe result=PASS\n");
    return 0;
}
