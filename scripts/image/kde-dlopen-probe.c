#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct wl_display;

typedef struct wl_display *(*wl_display_create_fn)(void);
typedef void (*wl_display_destroy_fn)(struct wl_display *);
typedef int (*wl_display_init_shm_fn)(struct wl_display *);

static void set_kde_library_path(void)
{
    setenv("LD_LIBRARY_PATH",
           "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:/usr/lib:"
           "/lib/x86_64-linux-gnu:/lib",
           1);
}

static void exec_kwin_help(void)
{
    set_kde_library_path();
    setenv("LD_PRELOAD",
           "/usr/lib/x86_64-linux-gnu/libKF5Codecs.so.5:"
           "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0",
           1);
    setenv("QT_QPA_PLATFORM", "wayland", 1);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    execl("/usr/bin/kwin_wayland", "kwin_wayland", "--help", NULL);
    perror("kde_dlopen_probe exec kwin_wayland");
    _exit(127);
}

static int probe_symbol(void *handle, const char *symbol)
{
    void *ptr;

    dlerror();
    ptr = dlsym(handle, symbol);
    if (!ptr) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlsym=%s ptr=NULL error=%s\n",
               symbol, err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlsym=%s ptr=%p\n", symbol, ptr);
    return 0;
}

static int probe_library(const char *name, int flags)
{
    void *handle;
    int failed = 0;

    dlerror();
    handle = dlopen(name, flags);
    if (!handle) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=%s handle=NULL error=%s\n",
               name, err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=%s handle=%p\n", name, handle);
    if (name[0] == 'l' && name[1] == 'i' && name[2] == 'b' &&
        name[3] == 'p' && name[4] == 'c' && name[5] == 'r')
        failed |= probe_symbol(handle, "pcre2_code_free_16");
    dlclose(handle);
    return failed;
}

static int check_qtwidgets_allwidgets(void *handle, const char *phase)
{
    static const char all_widgets_sym[] =
        "_ZN14QWidgetPrivate10allWidgetsE";
    void *sym;
    uint64_t value = 0;

    dlerror();
    sym = dlsym(handle, all_widgets_sym);
    if (!sym) {
        const char *err = dlerror();
        printf("kde_dlopen_probe phase=%s dlsym=%s ptr=NULL error=%s\n",
               phase, all_widgets_sym, err ? err : "");
        return 1;
    }

    memcpy(&value, sym, sizeof(value));
    printf("kde_dlopen_probe phase=%s qtwidgets_allwidgets ptr=%p value=0x%016llx status=%s\n",
           phase, sym, (unsigned long long)value,
           value == 0 ? "PASS" : "FAIL");
    return value == 0 ? 0 : 1;
}

static void *open_qtwidgets(void)
{
    void *handle;

    dlerror();
    handle = dlopen("libQt5Widgets.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libQt5Widgets.so.5 handle=NULL error=%s\n",
               err ? err : "");
        return NULL;
    }
    printf("kde_dlopen_probe dlopen=libQt5Widgets.so.5 handle=%p\n", handle);
    return handle;
}

static int probe_configwidgets(void)
{
    void *codecs;
    void *configwidgets;
    void *sym;
    int failed = 0;

    dlerror();
    configwidgets = dlopen("libKF5ConfigWidgets.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!configwidgets) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 direct_handle=NULL error=%s\n",
               err ? err : "");
        failed = 1;
    } else {
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 direct_handle=%p\n",
               configwidgets);
        dlclose(configwidgets);
    }

    dlerror();
    codecs = dlopen("libKF5Codecs.so.5", RTLD_NOW | RTLD_NOLOAD);
    if (codecs) {
        printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 noload_handle=%p\n",
               codecs);
        dlclose(codecs);
    } else {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 noload_handle=NULL error=%s\n",
               err ? err : "");
    }

    dlerror();
    codecs = dlopen("libKF5Codecs.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!codecs) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 handle=NULL error=%s\n",
               err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libKF5Codecs.so.5 handle=%p\n", codecs);

    dlerror();
    sym = dlsym(codecs, "_ZNK9KCharsets12codecForNameERK7QStringRb");
    if (!sym) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlsym=KCharsets::codecForName(QString,bool&) ptr=NULL error=%s\n",
               err ? err : "");
        failed = 1;
    } else {
        printf("kde_dlopen_probe dlsym=KCharsets::codecForName(QString,bool&) ptr=%p\n",
               sym);
    }

    dlerror();
    configwidgets = dlopen("libKF5ConfigWidgets.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!configwidgets) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 handle=NULL error=%s\n",
               err ? err : "");
        failed = 1;
    } else {
        printf("kde_dlopen_probe dlopen=libKF5ConfigWidgets.so.5 handle=%p\n",
               configwidgets);
        dlclose(configwidgets);
    }

    dlclose(codecs);
    return failed;
}

static int probe_wayland_shm_after_kwin(void *qtwidgets)
{
    void *kwin;
    void *server;
    wl_display_create_fn wl_display_create;
    wl_display_destroy_fn wl_display_destroy;
    wl_display_init_shm_fn wl_display_init_shm;
    struct wl_display *display;
    int rc;

    dlerror();
    kwin = dlopen("libkwin.so.5", RTLD_NOW | RTLD_GLOBAL);
    if (!kwin) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libkwin.so.5 handle=NULL error=%s\n",
               err ? err : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libkwin.so.5 handle=%p\n", kwin);
    if (qtwidgets)
        (void)check_qtwidgets_allwidgets(qtwidgets, "after-libkwin");

    dlerror();
    server = dlopen("libwayland-server.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!server) {
        const char *err = dlerror();
        printf("kde_dlopen_probe dlopen=libwayland-server.so.0 handle=NULL error=%s\n",
               err ? err : "");
        dlclose(kwin);
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libwayland-server.so.0 handle=%p\n",
           server);
    if (qtwidgets)
        (void)check_qtwidgets_allwidgets(qtwidgets, "after-wayland-server");

    wl_display_create =
        (wl_display_create_fn)dlsym(server, "wl_display_create");
    wl_display_destroy =
        (wl_display_destroy_fn)dlsym(server, "wl_display_destroy");
    wl_display_init_shm =
        (wl_display_init_shm_fn)dlsym(server, "wl_display_init_shm");
    if (!wl_display_create || !wl_display_destroy || !wl_display_init_shm) {
        printf("kde_dlopen_probe wayland_shm_symbols=FAIL\n");
        dlclose(server);
        dlclose(kwin);
        return 1;
    }

    display = wl_display_create();
    if (!display) {
        printf("kde_dlopen_probe wayland_display_create=FAIL\n");
        dlclose(server);
        dlclose(kwin);
        return 1;
    }

    rc = wl_display_init_shm(display);
    printf("kde_dlopen_probe wayland_shm_init=%s rc=%d\n",
           rc == 0 ? "PASS" : "FAIL", rc);
    wl_display_destroy(display);

    dlclose(server);
    dlclose(kwin);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    int failed = 0;
    void *qtwidgets;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && strcmp(argv[1], "--kwin-help") == 0)
        exec_kwin_help();

    set_kde_library_path();
    printf("kde_dlopen_probe ld_library_path=%s\n", getenv("LD_LIBRARY_PATH"));
    failed |= probe_library("libQt5Core.so.5", RTLD_NOW | RTLD_GLOBAL);
    qtwidgets = open_qtwidgets();
    if (!qtwidgets)
        failed = 1;
    else
        failed |= check_qtwidgets_allwidgets(qtwidgets, "after-qtwidgets");
    failed |= probe_configwidgets();
    failed |= probe_library("libpcre2-16.so.0", RTLD_NOW | RTLD_GLOBAL);
    failed |= probe_wayland_shm_after_kwin(qtwidgets);
    if (qtwidgets)
        dlclose(qtwidgets);
    printf("kde_dlopen_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
