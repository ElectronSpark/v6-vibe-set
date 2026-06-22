#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

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
        printf("kde_dlopen_probe dlopen=%s handle=NULL error=%s\n",
               name, dlerror() ? dlerror() : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=%s handle=%p\n", name, handle);
    if (name[0] == 'l' && name[1] == 'i' && name[2] == 'b' &&
        name[3] == 'p' && name[4] == 'c' && name[5] == 'r')
        failed |= probe_symbol(handle, "pcre2_code_free_16");
    dlclose(handle);
    return failed;
}

static int probe_wayland_shm_after_kwin(void)
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
        printf("kde_dlopen_probe dlopen=libkwin.so.5 handle=NULL error=%s\n",
               dlerror() ? dlerror() : "");
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libkwin.so.5 handle=%p\n", kwin);

    dlerror();
    server = dlopen("libwayland-server.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!server) {
        printf("kde_dlopen_probe dlopen=libwayland-server.so.0 handle=NULL error=%s\n",
               dlerror() ? dlerror() : "");
        dlclose(kwin);
        return 1;
    }
    printf("kde_dlopen_probe dlopen=libwayland-server.so.0 handle=%p\n",
           server);

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

int main(void)
{
    int failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    set_kde_library_path();
    printf("kde_dlopen_probe ld_library_path=%s\n", getenv("LD_LIBRARY_PATH"));
    failed |= probe_library("libQt5Core.so.5", RTLD_NOW | RTLD_GLOBAL);
    failed |= probe_library("libpcre2-16.so.0", RTLD_NOW | RTLD_GLOBAL);
    failed |= probe_wayland_shm_after_kwin();
    printf("kde_dlopen_probe result=%s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
