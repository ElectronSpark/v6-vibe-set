#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *app_root = "/opt/host-gui/wayland-chromium";
static const char *chrome_dir =
    "/opt/host-gui/wayland-chromium/chrome-linux64";
static const char *chrome_bin =
    "/opt/host-gui/wayland-chromium/chrome-linux64/chrome";

static void set_default_env(const char *name, const char *value)
{
    const char *existing = getenv(name);

    if (!existing || !existing[0])
        setenv(name, value, 1);
}

static void redirect_log(void)
{
    int null_fd = open("/dev/null", O_RDONLY);

    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        if (null_fd > STDERR_FILENO)
            close(null_fd);
    }

    int fd = open("/tmp/host-gui-wayland-chromium.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0)
        return;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

static void append_arg(char **argv, int *idx, int max, const char *arg)
{
    if (*idx + 1 < max)
        argv[(*idx)++] = (char *)arg;
}

static void append_extra_flags(char **argv, int *idx, int max, char *flags)
{
    char *p = flags;

    while (p && *p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (*p == '\0')
            break;
        append_arg(argv, idx, max, p);
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
        if (*p == '\0')
            break;
        *p++ = '\0';
    }
}

int main(int argc, char **argv)
{
    enum { MAX_ARGS = 96 };
    char *child_argv[MAX_ARGS];
    const char *backend = getenv("WAYLAND_CHROMIUM_BACKEND");
    const char *multiprocess = getenv("WAYLAND_CHROMIUM_MULTIPROCESS");
    const char *alsa_output_device = getenv("WAYLAND_CHROMIUM_ALSA_OUTPUT_DEVICE");
    char *extra_flags = getenv("WAYLAND_CHROMIUM_EXTRA_FLAGS");
    int use_x11 = backend && strcmp(backend, "x11") == 0;
    int use_multiprocess = multiprocess && strcmp(multiprocess, "1") == 0;
    int idx = 0;

    set_default_env("XDG_RUNTIME_DIR", "/tmp");
    set_default_env("XDG_DATA_DIRS", "/share:/usr/share");
    set_default_env("XDG_CONFIG_DIRS", "/etc/xdg");
    set_default_env("TMPDIR", "/tmp");
    set_default_env("TEMP", "/tmp");
    set_default_env("TMP", "/tmp");
    set_default_env("XCURSOR_PATH", "/share/icons");
    set_default_env("XCURSOR_THEME", "Adwaita");
    set_default_env("XCURSOR_SIZE", "24");
    if (use_x11) {
        setenv("DISPLAY", ":0", 1);
        unsetenv("WAYLAND_DISPLAY");
        setenv("OZONE_PLATFORM", "x11", 1);
    } else {
        set_default_env("WAYLAND_DISPLAY", "wayland-0");
        set_default_env("OZONE_PLATFORM", "wayland");
    }
    set_default_env("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt");
    set_default_env("CHROME_LOG_FILE", "/tmp/chrome_debug.log");
    set_default_env("NO_AT_BRIDGE", "1");
    set_default_env("GTK_MODULES", "");
    set_default_env("GSETTINGS_SCHEMA_DIR", "/share/glib-2.0/schemas");
    set_default_env("GIO_MODULE_DIR", "/lib/gio/modules");
    set_default_env("GIO_USE_TLS", "gnutls");
    set_default_env("ALSA_CONFIG_PATH", "/usr/share/alsa/alsa.conf");
    set_default_env("LIBVA_DRIVERS_PATH", "/lib/dri");
    set_default_env("LIBVA_DRIVER_NAME", "virtio_gpu");
    set_default_env("XV6_GTK_DISABLE_ACCESSIBILITY", "1");
    set_default_env("XV6_DRM_TRACE", "1");
    set_default_env("DBUS_SESSION_BUS_ADDRESS",
                    "unix:abstract=xv6_session_bus");
    set_default_env("DBUS_SYSTEM_BUS_ADDRESS",
                    "unix:abstract=xv6_system_bus");
    /*
     * Keep the imported non-graphics closure coherent. Chromium dlopens GTK,
     * ATK and GLib modules during startup; mixing host AT-SPI/ATK bridge
     * libraries with guest GLib/ATK registers duplicate GObject types before
     * the browser surface is created. Leave Chromium's own directory last so
     * its bundled EGL/GLES/SwiftShader libraries do not shadow the guest
     * Wayland/Mesa/DRM stack.
     */
    setenv("LD_LIBRARY_PATH",
           "/opt/host-gui/wayland-chromium/lib:"
           "/lib:/lib64:/usr/lib:/usr/lib64:"
           "/opt/host-gui/wayland-chromium/chrome-linux64",
           1);

    redirect_log();
    fprintf(stderr, "wayland-chromium-launcher: starting argc=%d\n", argc);
    fflush(stderr);

    if (chdir(chrome_dir) != 0)
        fprintf(stderr, "wayland-chromium-launcher: chdir %s failed: %s\n",
                chrome_dir, strerror(errno));

    append_arg(child_argv, &idx, MAX_ARGS, chrome_bin);
    append_arg(child_argv, &idx, MAX_ARGS,
               use_x11 ? "--ozone-platform=x11" : "--ozone-platform=wayland");
    append_arg(child_argv, &idx, MAX_ARGS,
               "--enable-features=UseOzonePlatform,AcceleratedVideoDecodeLinuxGL,"
               "VaapiIgnoreDriverChecks,VaapiOnNvidiaGPUs,"
               "VaapiVideoEncoder,CanvasOopRasterization");
    append_arg(child_argv, &idx, MAX_ARGS, "--ignore-gpu-blocklist");
    append_arg(child_argv, &idx, MAX_ARGS, "--enable-gpu-rasterization");
    append_arg(child_argv, &idx, MAX_ARGS, "--no-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-setuid-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-seccomp-filter-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-gpu-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-dev-shm-usage");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-vulkan");
    if (!use_multiprocess) {
        append_arg(child_argv, &idx, MAX_ARGS, "--disable-gpu");
        append_arg(child_argv, &idx, MAX_ARGS, "--in-process-gpu");
        append_arg(child_argv, &idx, MAX_ARGS, "--single-process");
        append_arg(child_argv, &idx, MAX_ARGS, "--no-zygote");
    }
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-breakpad");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-crashpad");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-crash-reporter");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-quic");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-component-update");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-background-networking");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-sync");
    append_arg(child_argv, &idx, MAX_ARGS, "--no-proxy-server");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-renderer-accessibility");
    append_arg(child_argv, &idx, MAX_ARGS, "--no-first-run");
    append_arg(child_argv, &idx, MAX_ARGS, "--no-default-browser-check");
    append_arg(child_argv, &idx, MAX_ARGS, "--password-store=basic");
    append_arg(child_argv, &idx, MAX_ARGS,
               "--disable-features=AccessibilityService,Crashpad,MediaRouter,"
               "OptimizationHints,CalculateNativeWinOcclusion,"
               "UseChromeOSDirectVideoDecoder,UseFreedesktopSecretPortal");
    append_arg(child_argv, &idx, MAX_ARGS,
               "--user-data-dir=/tmp/wayland-chromium-profile");
    append_arg(child_argv, &idx, MAX_ARGS, "--enable-logging=stderr");
    if (!alsa_output_device || !alsa_output_device[0])
        alsa_output_device = "default";
    if (strcmp(alsa_output_device, "none") != 0) {
        static char alsa_output_arg[128];

        snprintf(alsa_output_arg, sizeof(alsa_output_arg),
                 "--alsa-output-device=%s", alsa_output_device);
        append_arg(child_argv, &idx, MAX_ARGS, alsa_output_arg);
    }
    append_extra_flags(child_argv, &idx, MAX_ARGS, extra_flags);

    for (int i = 1; i < argc && idx + 1 < MAX_ARGS; i++)
        child_argv[idx++] = argv[i];
    if (argc == 1)
        append_arg(child_argv, &idx, MAX_ARGS, "about:blank");
    child_argv[idx] = NULL;

    fprintf(stderr, "wayland-chromium-launcher: exec %s root=%s\n",
            chrome_bin, app_root);
    fflush(stderr);
    execv(chrome_bin, child_argv);
    fprintf(stderr, "wayland-chromium-launcher: exec failed: %s\n",
            strerror(errno));
    return 127;
}
