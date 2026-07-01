#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int symbol_from_override(const char *name, void *sym)
{
    Dl_info info;

    memset(&info, 0, sizeof(info));
    if (dladdr(sym, &info) == 0 || !info.dli_fname) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=FAIL name=%s reason=dladdr\n",
               name);
        return 0;
    }

    printf("KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=%s lib=%s sym=%p\n",
           name, info.dli_fname, sym);
    if (!strstr(info.dli_fname, "libxv6-ifunc-memcpy.so")) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=FAIL name=%s reason=not_override lib=%s\n",
               name, info.dli_fname);
        return 0;
    }
    return 1;
}

static int maps_contains(const char *needle)
{
    FILE *f;
    char line[1024];

    f = fopen("/proc/self/maps", "r");
    if (!f) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_MAP status=FAIL reason=maps_open\n");
        return 0;
    }
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) {
            printf("KDE_QTQML_IFUNC_STARTUP_PROBE_MAP status=PASS line=%s", line);
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    printf("KDE_QTQML_IFUNC_STARTUP_PROBE_MAP status=FAIL reason=missing needle=%s\n",
           needle);
    return 0;
}

int main(void)
{
    void *qtqml;
    void *memcpy_sym;
    void *memmove_sym;
    void *floor_sym;
    void *ceil_sym;
    const char *err;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("KDE_QTQML_IFUNC_STARTUP_PROBE_START pid=%ld\n", (long)getpid());

    dlerror();
    qtqml = dlopen("libQt5Qml.so.5", RTLD_NOW | RTLD_NOLOAD);
    err = dlerror();
    if (!qtqml) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=qtqml_not_preloaded error=%s\n",
               err ? err : "none");
        return 2;
    }

    dlerror();
    memcpy_sym = dlsym(RTLD_DEFAULT, "memcpy");
    err = dlerror();
    if (!memcpy_sym) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=memcpy_dlsym error=%s\n",
               err ? err : "none");
        return 3;
    }
    if (!symbol_from_override("memcpy", memcpy_sym)) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=memcpy_not_override\n");
        return 7;
    }

    dlerror();
    memmove_sym = dlsym(RTLD_DEFAULT, "memmove");
    err = dlerror();
    if (!memmove_sym) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=memmove_dlsym error=%s\n",
               err ? err : "none");
        return 10;
    }
    if (!symbol_from_override("memmove", memmove_sym)) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=memmove_not_override\n");
        return 11;
    }

    dlerror();
    floor_sym = dlsym(RTLD_DEFAULT, "floor");
    err = dlerror();
    if (!floor_sym) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=floor_dlsym error=%s\n",
               err ? err : "none");
        return 5;
    }
    if (!symbol_from_override("floor", floor_sym)) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=floor_not_override\n");
        return 8;
    }

    dlerror();
    ceil_sym = dlsym(RTLD_DEFAULT, "ceil");
    err = dlerror();
    if (!ceil_sym) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=ceil_dlsym error=%s\n",
               err ? err : "none");
        return 6;
    }
    if (!symbol_from_override("ceil", ceil_sym)) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=ceil_not_override\n");
        return 9;
    }

    if (!maps_contains("libQt5Qml.so.5")) {
        printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=FAIL reason=qtqml_maps_missing\n");
        return 4;
    }

    printf("KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=PASS qtqml=%p memcpy=%p memmove=%p floor=%p ceil=%p\n",
           qtqml, memcpy_sym, memmove_sym, floor_sym, ceil_sym);
    printf("__KDE_QTQML_IFUNC_PASS__\n");
    return 0;
}
