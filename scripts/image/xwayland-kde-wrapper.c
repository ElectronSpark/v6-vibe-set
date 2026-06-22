#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
set_default_env(const char *name, const char *value)
{
    if (getenv(name) == NULL)
        setenv(name, value, 1);
}

int
main(int argc, char **argv)
{
    const char *real = "/bin/Xwayland.real";
    const char *glamor_env = getenv("XV6_XWAYLAND_GLAMOR");
    const char *glamor = (glamor_env && glamor_env[0] != '\0') ? glamor_env : "off";
    int extra = 2;
    int pos = 0;

    set_default_env("EGL_PLATFORM", "wayland");
    set_default_env("LIBGL_ALWAYS_SOFTWARE", "0");
    set_default_env("GALLIUM_DRIVER", "virgl");
    set_default_env("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu");
    set_default_env("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri");
    set_default_env("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm");

    if (strcmp(glamor, "auto") != 0)
        extra += 2;

    char **child = calloc((size_t)argc + (size_t)extra + 1, sizeof(*child));
    if (!child) {
        perror("calloc");
        return 127;
    }

    child[pos++] = (char *)real;
    if (strcmp(glamor, "auto") != 0) {
        child[pos++] = "-glamor";
        child[pos++] = (char *)glamor;
    }
    child[pos++] = "-xkbdir";
    child[pos++] = "/usr/share/X11/xkb";
    for (int i = 1; i < argc; i++)
        child[pos++] = argv[i];

    fprintf(stderr,
            "Xwayland KDE wrapper: EGL_PLATFORM=%s GALLIUM_DRIVER=%s "
            "MESA_LOADER_DRIVER_OVERRIDE=%s glamor=%s\n",
            getenv("EGL_PLATFORM") ? getenv("EGL_PLATFORM") : "(unset)",
            getenv("GALLIUM_DRIVER") ? getenv("GALLIUM_DRIVER") : "(unset)",
            getenv("MESA_LOADER_DRIVER_OVERRIDE") ? getenv("MESA_LOADER_DRIVER_OVERRIDE") : "(unset)",
            glamor);

    execv(real, child);
    fprintf(stderr, "Xwayland KDE wrapper: execv(%s) failed: %s\n",
            real, strerror(errno));
    return 127;
}
