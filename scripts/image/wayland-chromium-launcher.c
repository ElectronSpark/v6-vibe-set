#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *app_root = "/opt/host-gui/wayland-chromium";
static const char *chrome_dir =
    "/opt/host-gui/wayland-chromium/chrome-linux64";
static const char *chrome_bin =
    "/opt/host-gui/wayland-chromium/chrome-linux64/chrome";
static const char *launcher_log_path = "/host-gui-wayland-chromium.log";
static const char *launcher_log_compat_path =
    "/tmp/host-gui-wayland-chromium.log";
static const char *chrome_log_path = "/chrome_debug.log";
static const char *chrome_log_compat_path = "/tmp/chrome_debug.log";
static const char *egl_trace_preload_path =
    "/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so";
static const char *egl_trace_log_path = "/chromium-egl-trace.log";

static void set_default_env(const char *name, const char *value)
{
    const char *existing = getenv(name);

    if (!existing || !existing[0])
        setenv(name, value, 1);
}

static void touch_artifact_path(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd >= 0)
        close(fd);
}

static void ensure_compat_link(const char *link_path, const char *target_path)
{
    touch_artifact_path(target_path);
    unlink(link_path);
    if (symlink(target_path, link_path) < 0) {
        int saved_errno = errno;

        if (link(target_path, link_path) < 0)
            fprintf(stderr,
                    "wayland-chromium-launcher: compat link %s -> %s "
                    "failed: symlink errno=%d hardlink errno=%d %s\n",
                    link_path, target_path, saved_errno, errno,
                    strerror(errno));
    }
}

static void prepare_log_paths(void)
{
    ensure_compat_link(launcher_log_compat_path, launcher_log_path);
    ensure_compat_link(chrome_log_compat_path, chrome_log_path);
}

static void unlink_egl_trace_logs(const char *path)
{
    const char *base;
    const char *slash;
    char dir_path[256];
    char prefix[256];
    DIR *dir;
    struct dirent *de;
    size_t dir_len;
    size_t base_len;

    if (!path || !path[0])
        return;
    unlink(path);

    slash = strrchr(path, '/');
    base = slash ? slash + 1 : path;
    dir_len = slash ? (size_t)(slash - path) : 1;
    if (dir_len == 0)
        dir_len = 1;
    if (dir_len >= sizeof(dir_path))
        return;
    if (slash) {
        memcpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
    } else {
        snprintf(dir_path, sizeof(dir_path), "%s", ".");
    }

    base_len = strlen(base);
    if (base_len >= sizeof(prefix))
        return;
    snprintf(prefix, sizeof(prefix), "%s", base);
    if (base_len > 4 && strcmp(prefix + base_len - 4, ".log") == 0)
        prefix[base_len - 4] = '\0';

    dir = opendir(dir_path);
    if (!dir)
        return;
    while ((de = readdir(dir)) != NULL) {
        char full[512];
        size_t name_len = strlen(de->d_name);
        size_t prefix_len = strlen(prefix);

        if (name_len <= prefix_len + 4)
            continue;
        if (strncmp(de->d_name, prefix, prefix_len) != 0)
            continue;
        if (de->d_name[prefix_len] != '.')
            continue;
        if (strcmp(de->d_name + name_len - 4, ".log") != 0)
            continue;
        snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
        unlink(full);
    }
    closedir(dir);
}

static void redirect_log(void)
{
    int null_fd = open("/dev/null", O_RDONLY);

    if (null_fd >= 0) {
        dup2(null_fd, STDIN_FILENO);
        if (null_fd > STDERR_FILENO)
            close(null_fd);
    }

    int fd = open(launcher_log_path,
                  O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0)
        return;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

static void log_escaped_string(FILE *out, const char *s)
{
    static const char hex[] = "0123456789abcdef";

    if (!s) {
        fputs("(null)", out);
        return;
    }

    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;

        if (c == '\\' || c == '"') {
            fputc('\\', out);
            fputc(c, out);
        } else if (c >= 0x20 && c <= 0x7e) {
            fputc(c, out);
        } else {
            fputs("\\x", out);
            fputc(hex[c >> 4], out);
            fputc(hex[c & 0xf], out);
        }
    }
}

static void log_final_argv(char **argv, int argc, const char *backend,
                           const char *multiprocess, int use_x11,
                           int use_multiprocess)
{
    fprintf(stderr,
            "wayland-chromium-launcher: launch_marker pid=%ld ppid=%ld "
            "backend=\"",
            (long)getpid(), (long)getppid());
    log_escaped_string(stderr, backend);
    fputs("\" multiprocess=\"", stderr);
    log_escaped_string(stderr, multiprocess);
    fprintf(stderr, "\" use_x11=%d use_multiprocess=%d\n",
            use_x11, use_multiprocess);

    fprintf(stderr, "wayland-chromium-launcher: final_argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        fprintf(stderr, "wayland-chromium-launcher: argv_%d=\"", i);
        log_escaped_string(stderr, argv[i]);
        fputs("\"\n", stderr);
    }
    fprintf(stderr, "wayland-chromium-launcher: child_exec path=\"");
    log_escaped_string(stderr, chrome_bin);
    fputs("\"\n", stderr);
}

static void log_env_value(const char *name)
{
    const char *value = getenv(name);

    fprintf(stderr, "wayland-chromium-launcher: env %s=\"", name);
    log_escaped_string(stderr, value ? value : "(unset)");
    fputs("\"\n", stderr);
}

static void log_graphics_env(void)
{
    log_env_value("WAYLAND_DISPLAY");
    log_env_value("DISPLAY");
    log_env_value("XDG_RUNTIME_DIR");
    log_env_value("OZONE_PLATFORM");
    log_env_value("EGL_PLATFORM");
    log_env_value("GALLIUM_DRIVER");
    log_env_value("MESA_LOADER_DRIVER_OVERRIDE");
    log_env_value("LIBGL_DRIVERS_PATH");
    log_env_value("GBM_BACKENDS_PATH");
    log_env_value("WAYLAND_CHROMIUM_WAYLAND_DEBUG");
    log_env_value("WAYLAND_DEBUG");
    log_env_value("WAYLAND_CHROMIUM_EGL_TRACE");
    log_env_value("WAYLAND_CHROMIUM_AUTO_GL_FLAGS");
    log_env_value("CHROMIUM_EGL_TRACE");
    log_env_value("CHROMIUM_EGL_TRACE_LOG");
    log_env_value("LD_LIBRARY_PATH");
    log_env_value("LD_PRELOAD");
    log_env_value("LIBVA_DRIVERS_PATH");
    log_env_value("LIBVA_DRIVER_NAME");
    log_env_value("WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION");
    log_env_value("SIMDUTF_FORCE_IMPLEMENTATION");
    log_env_value("WAYLAND_CHROMIUM_ENABLE_DAV1D");
    log_env_value("WAYLAND_CHROMIUM_ENABLE_VAAPI");
}

static int env_enabled(const char *name)
{
    const char *value = getenv(name);

    return value && value[0] && strcmp(value, "0") != 0 &&
        strcasecmp(value, "false") != 0 &&
        strcasecmp(value, "no") != 0 &&
        strcasecmp(value, "off") != 0;
}

static int env_disabled_value(const char *value)
{
    return value && value[0] &&
        (strcmp(value, "0") == 0 ||
         strcasecmp(value, "false") == 0 ||
         strcasecmp(value, "no") == 0 ||
         strcasecmp(value, "off") == 0);
}

static void enable_egl_trace_preload(void)
{
    const char *old_preload;
    char preload[1024];

    if (!env_enabled("WAYLAND_CHROMIUM_EGL_TRACE"))
        return;

    setenv("CHROMIUM_EGL_TRACE", "1", 1);
    set_default_env("CHROMIUM_EGL_TRACE_LOG", egl_trace_log_path);
    unlink_egl_trace_logs(getenv("CHROMIUM_EGL_TRACE_LOG"));

    old_preload = getenv("LD_PRELOAD");
    if (old_preload && old_preload[0]) {
        snprintf(preload, sizeof(preload), "%s:%s", egl_trace_preload_path,
                 old_preload);
    } else {
        snprintf(preload, sizeof(preload), "%s", egl_trace_preload_path);
    }
    setenv("LD_PRELOAD", preload, 1);
}

static void enable_wayland_debug(void)
{
    if (!env_enabled("WAYLAND_CHROMIUM_WAYLAND_DEBUG"))
        return;

    setenv("WAYLAND_DEBUG", "client", 1);
}

static void apply_simdutf_force_implementation(void)
{
    const char *value = getenv("WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION");

    if (value && value[0]) {
        setenv("SIMDUTF_FORCE_IMPLEMENTATION", value, 1);
        return;
    }
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

static int extra_flags_contain_prefix(const char *flags, const char *prefix)
{
    const char *p = flags;
    size_t prefix_len = strlen(prefix);

    while (p && *p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (*p == '\0')
            break;
        if (strncmp(p, prefix, prefix_len) == 0)
            return 1;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
    }
    return 0;
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
    int use_multiprocess = !env_disabled_value(multiprocess);
    int enable_vaapi =
        !env_disabled_value(getenv("WAYLAND_CHROMIUM_ENABLE_VAAPI"));
    int auto_gl_flags =
        !env_disabled_value(getenv("WAYLAND_CHROMIUM_AUTO_GL_FLAGS"));
    int extra_has_use_gl =
        extra_flags_contain_prefix(extra_flags, "--use-gl=");
    int extra_has_use_angle =
        extra_flags_contain_prefix(extra_flags, "--use-angle=");
    int idx = 0;
    static char disable_features_arg[512];

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
        set_default_env("EGL_PLATFORM", "x11");
    } else {
        set_default_env("WAYLAND_DISPLAY", "wayland-0");
        set_default_env("OZONE_PLATFORM", "wayland");
        set_default_env("EGL_PLATFORM", "surfaceless");
    }
    set_default_env("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt");
    set_default_env("CHROME_LOG_FILE", chrome_log_path);
    set_default_env("NO_AT_BRIDGE", "1");
    set_default_env("GTK_MODULES", "");
    set_default_env("GSETTINGS_SCHEMA_DIR", "/share/glib-2.0/schemas");
    set_default_env("GIO_MODULE_DIR", "/lib/gio/modules");
    set_default_env("GIO_USE_TLS", "gnutls");
    set_default_env("ALSA_CONFIG_PATH", "/usr/share/alsa/alsa.conf");
    if (enable_vaapi) {
        set_default_env("LIBVA_DRIVERS_PATH", "/lib/dri");
        set_default_env("LIBVA_DRIVER_NAME", "virtio_gpu");
    } else {
        unsetenv("LIBVA_DRIVERS_PATH");
        unsetenv("LIBVA_DRIVER_NAME");
    }
    set_default_env("XV6_GTK_DISABLE_ACCESSIBILITY", "1");
    set_default_env("XV6_DRM_TRACE", "1");
    set_default_env("DBUS_SESSION_BUS_ADDRESS",
                    "unix:abstract=xv6_session_bus");
    set_default_env("DBUS_SYSTEM_BUS_ADDRESS",
                    "unix:abstract=xv6_system_bus");
    apply_simdutf_force_implementation();
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
           "/lib:/lib64:/lib/x86_64-linux-gnu:"
           "/usr/lib/x86_64-linux-gnu:/usr/lib:/usr/lib64:"
           "/opt/host-gui/wayland-chromium/chrome-linux64",
           1);
    unsetenv("LD_PRELOAD");
    enable_wayland_debug();
    enable_egl_trace_preload();

    prepare_log_paths();
    redirect_log();
    fprintf(stderr, "wayland-chromium-launcher: starting argc=%d\n", argc);
    fflush(stderr);

    if (chdir(chrome_dir) != 0)
        fprintf(stderr, "wayland-chromium-launcher: chdir %s failed: %s\n",
                chrome_dir, strerror(errno));

    append_arg(child_argv, &idx, MAX_ARGS, chrome_bin);
    append_arg(child_argv, &idx, MAX_ARGS,
               use_x11 ? "--ozone-platform=x11" : "--ozone-platform=wayland");
    if (enable_vaapi) {
        append_arg(child_argv, &idx, MAX_ARGS,
                   "--enable-features=UseOzonePlatform,AcceleratedVideoDecodeLinuxGL,"
                   "VaapiIgnoreDriverChecks,VaapiOnNvidiaGPUs,"
                   "VaapiVideoEncoder,CanvasOopRasterization");
    } else {
        append_arg(child_argv, &idx, MAX_ARGS,
                   "--enable-features=UseOzonePlatform,CanvasOopRasterization");
    }
    append_arg(child_argv, &idx, MAX_ARGS, "--ignore-gpu-blocklist");
    append_arg(child_argv, &idx, MAX_ARGS, "--enable-gpu-rasterization");
    if (!enable_vaapi) {
        append_arg(child_argv, &idx, MAX_ARGS,
                   "--disable-accelerated-video-decode");
        append_arg(child_argv, &idx, MAX_ARGS,
                   "--disable-accelerated-video-encode");
    }
    if (!use_x11 && auto_gl_flags) {
        if (!extra_has_use_gl)
            append_arg(child_argv, &idx, MAX_ARGS, "--use-gl=egl-angle");
        if (!extra_has_use_angle)
            append_arg(child_argv, &idx, MAX_ARGS, "--use-angle=opengles");
    }
    append_arg(child_argv, &idx, MAX_ARGS, "--no-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-setuid-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-seccomp-filter-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-gpu-sandbox");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-dev-shm-usage");
    append_arg(child_argv, &idx, MAX_ARGS, "--disable-vulkan");
    append_arg(child_argv, &idx, MAX_ARGS, "--use-vulkan=disabled");
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
    snprintf(disable_features_arg, sizeof(disable_features_arg),
             "--disable-features=AccessibilityService,Crashpad,MediaRouter,"
             "OptimizationHints,CalculateNativeWinOcclusion,Vulkan,"
             "DefaultANGLEVulkan,VulkanFromANGLE,%s"
             "UseChromeOSDirectVideoDecoder,UseFreedesktopSecretPortal%s",
             enable_vaapi ? "" :
             "AcceleratedVideoDecodeLinuxGL,VaapiIgnoreDriverChecks,"
             "AcceleratedVideoDecodeLinuxZeroCopyGL,VaapiOnNvidiaGPUs,"
             "VaapiVideoDecoder,VaapiVideoEncoder,",
             env_enabled("WAYLAND_CHROMIUM_ENABLE_DAV1D") ? "" :
             ",Dav1dVideoDecoder,WebRtcHwAv1Decoding");
    append_arg(child_argv, &idx, MAX_ARGS, disable_features_arg);
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

    log_final_argv(child_argv, idx, backend, multiprocess, use_x11,
                   use_multiprocess);
    log_graphics_env();
    fprintf(stderr, "wayland-chromium-launcher: exec %s root=%s\n",
            chrome_bin, app_root);
    fflush(stderr);
    execv(chrome_bin, child_argv);
    fprintf(stderr, "wayland-chromium-launcher: exec failed: %s\n",
            strerror(errno));
    return 127;
}
