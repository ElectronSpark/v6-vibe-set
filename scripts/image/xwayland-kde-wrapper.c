#include <errno.h>
#include <ctype.h>
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

static int
env_enabled(const char *value)
{
    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

static const char *
env_value(const char *name)
{
    const char *value = getenv(name);

    return value ? value : "(unset)";
}

static int
valid_decimal_range(const char *value, int min, int max)
{
    int n = 0;

    if (!value || value[0] == '\0')
        return 0;
    if (value[0] == '0' && value[1] != '\0')
        return 0;

    for (size_t i = 0; value[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)value[i];

        if (!isdigit(ch))
            return 0;
        n = n * 10 + (int)(ch - '0');
        if (n > max)
            return 0;
    }

    return n >= min;
}

static const char *
diagnostic_decimal_env(const char *name, const char *accepted)
{
    const char *value = getenv(name);

    if (!value)
        return "(unset)";
    if (!accepted)
        return "(invalid)";
    return value;
}

static void
log_final_argv(char **child, int count)
{
    const char *glamor = "(absent)";
    const char *verbose = "(absent)";
    const char *audit = "(absent)";
    const char *glx_plus = "no";
    const char *glx_minus = "no";

    for (int i = 0; i < count; i++) {
        if (strcmp(child[i], "-glamor") == 0 && i + 1 < count) {
            glamor = child[i + 1];
        } else if (strcmp(child[i], "-verbose") == 0 && i + 1 < count) {
            verbose = child[i + 1];
        } else if (strcmp(child[i], "-audit") == 0 && i + 1 < count) {
            audit = child[i + 1];
        } else if (strcmp(child[i], "+extension") == 0 && i + 1 < count &&
                   strcmp(child[i + 1], "GLX") == 0) {
            glx_plus = "yes";
        } else if (strcmp(child[i], "-extension") == 0 && i + 1 < count &&
                   strcmp(child[i + 1], "GLX") == 0) {
            glx_minus = "yes";
        }
    }

    fprintf(stderr,
            "Xwayland KDE wrapper: final argv argc=%d glamor=%s verbose=%s "
            "audit=%s +extension_GLX=%s -extension_GLX=%s\n",
            count, glamor, verbose, audit, glx_plus, glx_minus);
}

int
main(int argc, char **argv)
{
    const char *real = "/bin/Xwayland.real";
    const char *glamor_env = getenv("XV6_XWAYLAND_GLAMOR");
    const char *glamor = (glamor_env && glamor_env[0] != '\0') ? glamor_env : "off";
    const char *virgl_debug = getenv("XV6_XWAYLAND_VIRGL_DEBUG");
    const char *verbose_env = getenv("XV6_XWAYLAND_VERBOSE");
    const char *audit_env = getenv("XV6_XWAYLAND_AUDIT");
    const char *enable_glx_env = getenv("XV6_XWAYLAND_ENABLE_GLX");
    const char *verbose = NULL;
    const char *audit = NULL;
    const int loader_debug = env_enabled(getenv("XV6_XWAYLAND_LOADER_DEBUG"));
    const int enable_glx = env_enabled(enable_glx_env);
    int extra = 2;
    int pos = 0;

    set_default_env("EGL_PLATFORM", "wayland");
    set_default_env("LIBGL_ALWAYS_SOFTWARE", "0");
    set_default_env("GALLIUM_DRIVER", "virgl");
    set_default_env("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu");
    set_default_env("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri");
    set_default_env("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm");
    if (virgl_debug && virgl_debug[0] != '\0')
        setenv("VIRGL_DEBUG", virgl_debug, 1);
    if (loader_debug) {
        set_default_env("LIBGL_DEBUG", "verbose");
        set_default_env("EGL_LOG_LEVEL", "debug");
        set_default_env("MESA_DEBUG", "context");
    }
    if (verbose_env && valid_decimal_range(verbose_env, 0, 9))
        verbose = verbose_env;
    else if (verbose_env)
        fprintf(stderr,
                "Xwayland KDE wrapper: ignoring invalid XV6_XWAYLAND_VERBOSE\n");
    if (audit_env && valid_decimal_range(audit_env, 0, 9))
        audit = audit_env;
    else if (audit_env)
        fprintf(stderr,
                "Xwayland KDE wrapper: ignoring invalid XV6_XWAYLAND_AUDIT\n");

    if (strcmp(glamor, "auto") != 0)
        extra += 2;
    if (verbose)
        extra += 2;
    if (audit)
        extra += 2;
    if (enable_glx)
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
    if (verbose) {
        child[pos++] = "-verbose";
        child[pos++] = (char *)verbose;
    }
    if (audit) {
        child[pos++] = "-audit";
        child[pos++] = (char *)audit;
    }
    if (enable_glx) {
        child[pos++] = "+extension";
        child[pos++] = "GLX";
    }
    child[pos++] = "-xkbdir";
    child[pos++] = "/usr/share/X11/xkb";
    for (int i = 1; i < argc; i++)
        child[pos++] = argv[i];

    fprintf(stderr,
            "Xwayland KDE wrapper: EGL_PLATFORM=%s GALLIUM_DRIVER=%s "
            "MESA_LOADER_DRIVER_OVERRIDE=%s glamor=%s loader_debug=%s "
            "enable_glx=%s XV6_XWAYLAND_VERBOSE=%s XV6_XWAYLAND_AUDIT=%s "
            "VIRGL_DEBUG=%s LIBGL_DRIVERS_PATH=%s GBM_BACKENDS_PATH=%s "
            "LD_LIBRARY_PATH=%s\n",
            env_value("EGL_PLATFORM"),
            env_value("GALLIUM_DRIVER"),
            env_value("MESA_LOADER_DRIVER_OVERRIDE"),
            glamor,
            loader_debug ? "1" : "0",
            enable_glx_env ? enable_glx_env : "0",
            diagnostic_decimal_env("XV6_XWAYLAND_VERBOSE", verbose),
            diagnostic_decimal_env("XV6_XWAYLAND_AUDIT", audit),
            env_value("VIRGL_DEBUG"),
            env_value("LIBGL_DRIVERS_PATH"),
            env_value("GBM_BACKENDS_PATH"),
            env_value("LD_LIBRARY_PATH"));
    log_final_argv(child, pos);

    execv(real, child);
    fprintf(stderr, "Xwayland KDE wrapper: execv(%s) failed: %s\n",
            real, strerror(errno));
    return 127;
}
