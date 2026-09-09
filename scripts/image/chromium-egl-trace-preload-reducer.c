#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                    const EGLint *attrib_list);

/*
 * No-boot regression reducer for chromium-egl-trace-preload.c.  Build this
 * source once with TRACE_FAKE_PROVIDER as a shared object and once as the
 * caller linked against that object; see the focused check beside the source
 * in active-work-plan.md.
 */

#ifdef TRACE_FAKE_PROVIDER

static EGLDisplay core_display(void)
{
    return (EGLDisplay)(uintptr_t)0x11111111U;
}

static EGLDisplay ext_display(void)
{
    return (EGLDisplay)(uintptr_t)0x22222222U;
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display,
                                 const EGLAttrib *attrib_list)
{
    (void)platform;
    (void)native_display;
    if (attrib_list == NULL || attrib_list[0] != (EGLAttrib)0x3456 ||
        attrib_list[1] != (EGLAttrib)UINT64_C(0x1122334455667788) ||
        attrib_list[2] != EGL_NONE)
        return EGL_NO_DISPLAY;
    return core_display();
}

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                    const EGLint *attrib_list)
{
    (void)platform;
    (void)native_display;
    if (attrib_list == NULL || attrib_list[0] != 0x4567 ||
        attrib_list[1] != 0x55667788 || attrib_list[2] != EGL_NONE)
        return EGL_NO_DISPLAY;
    return ext_display();
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    (void)dpy;
    (void)major;
    (void)minor;
    return EGL_FALSE;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                         EGLint config_size, EGLint *num_config)
{
    (void)dpy;
    (void)configs;
    (void)config_size;
    (void)num_config;
    return EGL_FALSE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                           EGLConfig *configs, EGLint config_size,
                           EGLint *num_config)
{
    (void)dpy;
    (void)attrib_list;
    (void)configs;
    (void)config_size;
    (void)num_config;
    return EGL_FALSE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                              EGLint attribute, EGLint *value)
{
    (void)dpy;
    (void)config;
    (void)attribute;
    (void)value;
    return EGL_FALSE;
}

EGLint eglGetError(void)
{
    return EGL_BAD_ATTRIBUTE;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    if (name == NULL)
        return NULL;
    if (strcmp(name, "eglGetPlatformDisplay") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetPlatformDisplay;
    if (strcmp(name, "eglGetPlatformDisplayEXT") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetPlatformDisplayEXT;
    if (strcmp(name, "eglInitialize") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglInitialize;
    if (strcmp(name, "eglGetConfigs") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetConfigs;
    if (strcmp(name, "eglChooseConfig") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglChooseConfig;
    if (strcmp(name, "eglGetConfigAttrib") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetConfigAttrib;
    if (strcmp(name, "eglGetError") == 0)
        return (__eglMustCastToProperFunctionPointerType)eglGetError;
    return NULL;
}

#else

typedef EGLDisplay (*core_platform_display_fn_t)(EGLenum, void *,
                                                 const EGLAttrib *);
typedef EGLDisplay (*ext_platform_display_fn_t)(EGLenum, void *,
                                                const EGLint *);
typedef void (*reducer_lock_fn_t)(void);

static int reduce_platform_lists(void)
{
    const EGLAttrib core_attrs[] = {
        (EGLAttrib)0x3456,
        (EGLAttrib)UINT64_C(0x1122334455667788),
        EGL_NONE,
    };
    const EGLint ext_attrs[] = {0x4567, 0x55667788, EGL_NONE};
    core_platform_display_fn_t core_proc;
    ext_platform_display_fn_t ext_proc;

    if (eglGetPlatformDisplay(0x31d8, NULL, core_attrs) !=
        (EGLDisplay)(uintptr_t)0x11111111U)
        return 11;
    if (eglGetPlatformDisplayEXT(0x31d8, NULL, ext_attrs) !=
        (EGLDisplay)(uintptr_t)0x22222222U)
        return 12;
    core_proc = (core_platform_display_fn_t)
        eglGetProcAddress("eglGetPlatformDisplay");
    ext_proc = (ext_platform_display_fn_t)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (core_proc == NULL || core_proc(0x31d8, NULL, core_attrs) !=
        (EGLDisplay)(uintptr_t)0x11111111U)
        return 13;
    if (ext_proc == NULL || ext_proc(0x31d8, NULL, ext_attrs) !=
        (EGLDisplay)(uintptr_t)0x22222222U)
        return 14;
    return 0;
}

static int reduce_poison_outputs(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    void *poison;
    int failed = 0;

    if (page_size <= 0)
        return 20;
    poison = mmap(NULL, (size_t)page_size, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (poison == MAP_FAILED)
        return 21;
    failed |= eglInitialize((EGLDisplay)(uintptr_t)1, poison, poison) !=
              EGL_FALSE;
    failed |= eglGetConfigs((EGLDisplay)(uintptr_t)1, poison, 1, poison) !=
              EGL_FALSE;
    failed |= eglChooseConfig((EGLDisplay)(uintptr_t)1, NULL, poison, 1,
                              poison) != EGL_FALSE;
    failed |= eglGetConfigAttrib((EGLDisplay)(uintptr_t)1,
                                 (EGLConfig)(uintptr_t)2,
                                 EGL_CONFIG_ID, poison) != EGL_FALSE;
    (void)munmap(poison, (size_t)page_size);
    return failed ? 22 : 0;
}

static int reduce_fork_held_lock(void)
{
    reducer_lock_fn_t lock_fn;
    reducer_lock_fn_t unlock_fn;
    pid_t child;
    int status;

    lock_fn = (reducer_lock_fn_t)dlsym(RTLD_DEFAULT,
                                       "chromium_egl_trace_reducer_lock");
    unlock_fn = (reducer_lock_fn_t)dlsym(RTLD_DEFAULT,
                                         "chromium_egl_trace_reducer_unlock");
    if (lock_fn == NULL || unlock_fn == NULL)
        return 30;
    lock_fn();
    child = fork();
    if (child < 0) {
        unlock_fn();
        return 31;
    }
    if (child == 0) {
        alarm(3);
        _exit(eglGetError() == EGL_BAD_ATTRIBUTE ? 0 : 32);
    }
    if (waitpid(child, &status, 0) != child) {
        unlock_fn();
        return 33;
    }
    unlock_fn();
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 34;
}

static pthread_barrier_t race_barrier;

static void *race_thread(void *unused)
{
    (void)unused;
    (void)pthread_barrier_wait(&race_barrier);
    (void)eglGetError();
    return NULL;
}

static int reduce_provider_race(void)
{
    enum { THREADS = 64 };
    pthread_t threads[THREADS];

    if (pthread_barrier_init(&race_barrier, NULL, THREADS) != 0)
        return 40;
    for (size_t i = 0; i < THREADS; i++) {
        if (pthread_create(&threads[i], NULL, race_thread, NULL) != 0)
            return 41;
    }
    for (size_t i = 0; i < THREADS; i++) {
        if (pthread_join(threads[i], NULL) != 0)
            return 42;
    }
    (void)pthread_barrier_destroy(&race_barrier);
    return 0;
}

static int reduce_oversized_maps(void)
{
    enum { PAGES = 26000 };
    long page_size = sysconf(_SC_PAGESIZE);
    size_t length;
    char *area;

    if (page_size <= 0)
        return 50;
    length = (size_t)page_size * PAGES;
    area = mmap(NULL, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED)
        return 51;
    for (size_t page = 0; page < PAGES; page += 2) {
        if (mprotect(area + page * (size_t)page_size, (size_t)page_size,
                     PROT_READ) != 0) {
            (void)munmap(area, length);
            return 52;
        }
    }
    (void)eglGetError();
    (void)munmap(area, length);
    return 0;
}

int main(int argc, char **argv)
{
    int rc;

    if (argc != 2) {
        fprintf(stderr, "usage: %s ext|poison|fork|race|maps\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "ext") == 0)
        rc = reduce_platform_lists();
    else if (strcmp(argv[1], "poison") == 0)
        rc = reduce_poison_outputs();
    else if (strcmp(argv[1], "fork") == 0)
        rc = reduce_fork_held_lock();
    else if (strcmp(argv[1], "race") == 0)
        rc = reduce_provider_race();
    else if (strcmp(argv[1], "maps") == 0)
        rc = reduce_oversized_maps();
    else
        rc = 3;
    if (rc != 0)
        fprintf(stderr, "reducer=%s rc=%d\n", argv[1], rc);
    return rc;
}

#endif
