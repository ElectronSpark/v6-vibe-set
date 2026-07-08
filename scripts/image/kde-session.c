#define _GNU_SOURCE
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define X11_EGL_SESSION_LOG "/host-gui-host-x11-egl-smoke.log"
#define X11_EGL_PRESENT_TRACE_LOG "/host-gui-host-x11-present-trace.log"
#define X11_EGL_PRESENT_TRACE_PRELOAD "/opt/host-gui/host-x11-egl-smoke/lib/host-x11-present-trace-preload.so"
#define KWIN_ALLOC_TRACE_PRELOAD "/opt/xv6-kde-abi-libs/kwin-alloc-trace-preload.so"
#define KWIN_COMPAT_PRELOAD \
    "/usr/lib/x86_64-linux-gnu/libKF5Codecs.so.5:" \
    "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0"
#define KWIN_ALLOC_TRACE_LOG "/kde-kwin-alloc-trace.log"
#define KWIN_IOCTL_TRACE_PRELOAD "/opt/xv6-kde-abi-libs/kwin-ioctl-trace-preload.so"
#define KWIN_IOCTL_TRACE_LOG "/kde-kwin-ioctl-trace.log"
#define KWIN_SESSION_LOG "/kde-session-kwin.log"
#define PLASMA_SESSION_LOG "/kde-session-plasma-child.log"
#define NETWORK_SNI_LOG "/kde-network-status-sni.log"
#define PRE_KWIN_LIBINPUT_LOG "/kde-pre-kwin-libinput.log"
#define X11_EGL_SMOKE_CHILD_TIMEOUT_MS 35000
#define X11_EGL_SMOKE_TRACE_CHILD_TIMEOUT_MS 85000
#define X11_EGL_SESSION_PROBE_TIMEOUT_MS 90000
#define X11_EGL_CHILD_LD_PRELOAD_ENV "HOST_X11_EGL_CHILD_LD_PRELOAD"
#define X11_EGL_AUTH_PATH_MAX 512
#define X11_EGL_XWAYLAND_ARGV_MAX 1024
#define KDE_DESKTOP_WAKE_DEFAULT_DELAY_MS 2500
#define KDE_DESKTOP_WAKE_MAX_DELAY_MS 30000

static char x11_egl_active_session_probe_mode[64];

struct x11_egl_xwayland_auth {
    int found;
    pid_t pid;
    char argv[X11_EGL_XWAYLAND_ARGV_MAX];
    char auth_path[X11_EGL_AUTH_PATH_MAX];
    char display[64];
};

struct x11_egl_xwayland_auth_discovery {
    struct x11_egl_xwayland_auth best;
    struct x11_egl_xwayland_auth display0;
    struct x11_egl_xwayland_auth display1;
};

static void mkdir_one(const char *path, mode_t mode)
{
    if (mkdir(path, mode) < 0 && errno != EEXIST)
        fprintf(stderr, "kde-session: mkdir %s: %s\n", path, strerror(errno));
    if (chmod(path, mode) < 0)
        fprintf(stderr, "kde-session: chmod %s: %s\n", path, strerror(errno));
}

static int cmdline_has_flag(const char *flag)
{
    FILE *fp;
    char buf[4096];
    char *save = NULL;
    char *tok;

    fp = fopen("/proc/cmdline", "r");
    if (!fp)
        return 0;
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    for (tok = strtok_r(buf, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
        if (strcmp(tok, flag) == 0)
            return 1;
    }
    return 0;
}

static int env_bool_enabled(const char *name)
{
    const char *value = getenv(name);

    return value &&
           (strcmp(value, "1") == 0 ||
            strcmp(value, "true") == 0 ||
            strcmp(value, "yes") == 0 ||
            strcmp(value, "on") == 0);
}

static int cmdline_get_value_status(const char *key, char *value,
                                    size_t value_size)
{
    FILE *fp;
    char buf[4096];
    char *save = NULL;
    char *tok;
    size_t key_len = strlen(key);

    if (value_size == 0)
        return 0;

    fp = fopen("/proc/cmdline", "r");
    if (!fp)
        return 0;
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    for (tok = strtok_r(buf, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
        if (strncmp(tok, key, key_len) != 0 || tok[key_len] != '=')
            continue;
        if (strlen(tok + key_len + 1) >= value_size)
            return -1;
        snprintf(value, value_size, "%s", tok + key_len + 1);
        return 1;
    }
    return 0;
}

static int cmdline_get_value(const char *key, char *value, size_t value_size)
{
    return cmdline_get_value_status(key, value, value_size) > 0;
}

static int valid_xwayland_glamor_mode(const char *mode)
{
    return strcmp(mode, "off") == 0 ||
           strcmp(mode, "auto") == 0 ||
           strcmp(mode, "gl") == 0 ||
           strcmp(mode, "es") == 0;
}

static int valid_decimal_range(const char *value, int min, int max)
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

static int valid_xwayland_virgl_debug(const char *value)
{
    size_t len;

    if (!value || value[0] == '\0')
        return 0;

    len = strlen(value);
    if (len > 127)
        return 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)value[i];

        if (isalnum(ch) || ch == ',' || ch == '_' || ch == '-')
            continue;
        return 0;
    }
    return 1;
}

static int valid_x11_egl_glx_fps_variant(const char *value)
{
    return value &&
           (strcmp(value, "baseline") == 0 ||
            strcmp(value, "finish-before-swap") == 0 ||
            strcmp(value, "swap-only") == 0 ||
            strcmp(value, "swap-interval0-swap-only") == 0 ||
            strcmp(value, "swap-interval0-oml-state-swap-only") == 0 ||
            strcmp(value,
                   "swap-interval0-oml-state-sampled-swap-only") == 0 ||
            strcmp(value,
                   "swap-interval0-oml-state-sampled-after-swap-only") == 0 ||
            strcmp(value, "oml-queue3-swap-only") == 0 ||
            strcmp(value, "oml-queue-depth-swap-only") == 0 ||
            strcmp(value, "oml-queue-depth-flush-swap-timing") == 0 ||
            strcmp(value, "oml-queue-depth-issue-state-timing") == 0);
}

static int valid_x11_present_fps_variant(const char *value)
{
    return value &&
           (strcmp(value, "baseline") == 0 ||
            strcmp(value, "unchecked") == 0 ||
            strcmp(value, "queue3-unchecked") == 0 ||
            strcmp(value, "queue-unchecked") == 0);
}

static void write_config_file(const char *path, const char *contents)
{
    FILE *fp;

    fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "kde-session: open %s: %s\n", path, strerror(errno));
        return;
    }
    if (fputs(contents, fp) < 0)
        fprintf(stderr, "kde-session: write %s: %s\n", path, strerror(errno));
    if (fclose(fp) < 0)
        fprintf(stderr, "kde-session: close %s: %s\n", path, strerror(errno));
}

static void seed_pulse_cookie_file(const char *path)
{
    unsigned char cookie[256];
    struct stat st;
    int fd;

    if (stat(path, &st) == 0 && st.st_size == (off_t)sizeof(cookie)) {
        chmod(path, 0600);
        return;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        fprintf(stderr, "kde-session: open %s: %s\n", path, strerror(errno));
        return;
    }

    for (size_t i = 0; i < sizeof(cookie); i++)
        cookie[i] = (unsigned char)(0x5a ^ (i * 37u) ^ (i >> 1));

    size_t done = 0;
    while (done < sizeof(cookie)) {
        ssize_t n = write(fd, cookie + done, sizeof(cookie) - done);
        if (n <= 0) {
            fprintf(stderr, "kde-session: write %s: %s\n",
                    path, n < 0 ? strerror(errno) : "short write");
            break;
        }
        done += (size_t)n;
    }
    if (close(fd) < 0)
        fprintf(stderr, "kde-session: close %s: %s\n", path, strerror(errno));
    chmod(path, 0600);
}

static void seed_pulse_cookie(void)
{
    mkdir_one("/root/.config", 0700);
    mkdir_one("/root/.config/pulse", 0700);
    mkdir_one("/dev/shm/kde-config/pulse", 0700);
    seed_pulse_cookie_file("/dev/shm/kde-config/pulse/cookie");
    seed_pulse_cookie_file("/root/.config/pulse/cookie");
    seed_pulse_cookie_file("/root/.pulse-cookie");
}

static void seed_kde_config(void)
{
    static const char activity_id[] = "7d2c85d8-99a1-45bc-a37d-0fcd7c80f101";
    static const char kwinrc[] =
        "[Compositing]\n"
        "Backend=OpenGL\n"
        "Enabled=true\n"
        "GLPlatformInterface=egl\n"
        "GLStrictBinding=false\n"
        "HiddenPreviews=4\n"
        "OpenGLIsUnsafe=false\n"
        "WindowsBlockCompositing=false\n"
        "\n"
        "[KDE]\n"
        "AnimationDurationFactor=0\n"
        "\n"
        "[Plugins]\n"
        "blendchangesEnabled=false\n"
        "blurEnabled=false\n"
        "colorpickerEnabled=false\n"
        "contrastEnabled=false\n"
        "desktopgridEnabled=false\n"
        "diminactiveEnabled=false\n"
        "dimscreenEnabled=false\n"
        "fallapartEnabled=false\n"
        "glideEnabled=false\n"
        "highlightwindowEnabled=false\n"
        "kscreenEnabled=false\n"
        "kwin4_effect_blendEnabled=false\n"
        "kwin4_effect_blurEnabled=false\n"
        "kwin4_effect_dialogparentEnabled=false\n"
        "kwin4_effect_fadeEnabled=false\n"
        "kwin4_effect_fadedesktopEnabled=false\n"
        "kwin4_effect_fadingpopupsEnabled=false\n"
        "kwin4_effect_fullscreenEnabled=false\n"
        "kwin4_effect_loginEnabled=false\n"
        "kwin4_effect_logoutEnabled=false\n"
        "kwin4_effect_maximizeEnabled=false\n"
        "kwin4_effect_morphingpopupsEnabled=false\n"
        "kwin4_effect_outputlocatorEnabled=false\n"
        "kwin4_effect_scaleEnabled=false\n"
        "kwin4_effect_sessionquitEnabled=false\n"
        "kwin4_effect_screenshotEnabled=true\n"
        "kwin4_effect_squashEnabled=false\n"
        "kwin4_effect_translucencyEnabled=false\n"
        "kwin4_effect_windowapertureEnabled=false\n"
        "magiclampEnabled=false\n"
        "magnifierEnabled=false\n"
        "mouseclickEnabled=false\n"
        "mousemarkEnabled=false\n"
        "overviewEnabled=false\n"
        "presentwindowsEnabled=false\n"
        "screenshotEnabled=true\n"
        "sheetEnabled=false\n"
        "showfpsEnabled=false\n"
        "showpaintEnabled=false\n"
        "slideEnabled=false\n"
        "slidebackEnabled=false\n"
        "slidingpopupsEnabled=false\n"
        "startupfeedbackEnabled=false\n"
        "thumbnailasideEnabled=false\n"
        "tileseditorEnabled=false\n"
        "touchpointsEnabled=false\n"
        "trackmouseEnabled=false\n"
        "windowviewEnabled=false\n"
        "wobblywindowsEnabled=false\n"
        "zoomEnabled=false\n"
        "\n"
        "[Windows]\n"
        "ElectricBorderMaximize=false\n"
        "ElectricBorderTiling=false\n"
        "Placement=Smart\n"
        "\n"
        "[Tiling][4c85f5bb-4dcb-5bec-8433-5c0e6860d679]\n"
        "tiles={\"layoutDirection\":\"horizontal\",\"tiles\":[{\"width\":0.25},{\"width\":0.5},{\"width\":0.25}]}\n"
        "\n"
        "[org.kde.kdecoration2]\n"
        "BorderSize=No Borders\n"
        "BorderSizeAuto=false\n"
        "ButtonsOnLeft=\n"
        "ButtonsOnRight=IAX\n";
    static const char kdeglobals[] =
        "[KDE]\n"
        "AnimationDurationFactor=0\n"
        "SingleClick=true\n"
        "\n"
        "[General]\n"
        "BrowserApplication=org.kde.dolphin.desktop\n"
        "TerminalApplication=xv6-terminal.desktop\n";
    static const char mimeapps[] =
        "[Default Applications]\n"
        "inode/directory=org.kde.dolphin.desktop\n"
        "x-scheme-handler/file=xv6-file-scheme-handler.desktop\n"
        "\n"
        "[Added Associations]\n"
        "inode/directory=org.kde.dolphin.desktop;\n"
        "x-scheme-handler/file=xv6-file-scheme-handler.desktop;\n";
    static const char user_dirs[] =
        "XDG_DESKTOP_DIR=\"/root/Desktop\"\n"
        "XDG_DOWNLOAD_DIR=\"/root/Downloads\"\n"
        "XDG_TEMPLATES_DIR=\"/root/Templates\"\n"
        "XDG_PUBLICSHARE_DIR=\"/root/Public\"\n"
        "XDG_DOCUMENTS_DIR=\"/root/Documents\"\n"
        "XDG_MUSIC_DIR=\"/root/Music\"\n"
        "XDG_PICTURES_DIR=\"/root/Pictures\"\n"
        "XDG_VIDEOS_DIR=\"/root/Videos\"\n";
    char kactivitymanagerdrc[512];
    char plasma_appletsrc[4096];
    const char *systemtray_block = "";
    const char *applet_order = "3;4;5;6;8;9";
    static const char kactivitymanagerdrc_fmt[] =
        "[activities]\n"
        "%s=Desktop\n"
        "current=%s\n"
        "\n"
        "[main]\n"
        "currentActivity=%s\n";
    static const char ksmserverrc[] =
        "[KSplash]\n"
        "Engine=none\n"
        "Theme=None\n";
    static const char klipperrc[] =
        "[General]\n"
        "AutoStart=false\n";
    static const char kded5rc[] =
        "[Module-bluedevil]\n"
        "autoload=false\n"
        "\n"
        "[Module-kded_bolt]\n"
        "autoload=false\n"
        "\n"
        "[Module-freespacenotifier]\n"
        "autoload=false\n"
        "\n"
        "[Module-kscreen]\n"
        "autoload=false\n"
        "\n"
        "[Module-kscreen::osdService]\n"
        "autoload=false\n";
    static const char kwalletrc[] =
        "[Wallet]\n"
        "Enabled=false\n"
        "First Use=false\n";
    static const char konsolerc[] =
        "[Desktop Entry]\n"
        "DefaultProfile=Shell.profile\n";
    static const char konsole_shell_profile[] =
        "[Appearance]\n"
        "ColorScheme=WhiteOnBlack\n"
        "Font=DejaVu Sans Mono,12,-1,5,50,0,0,0,0,0\n"
        "\n"
        "[General]\n"
        "Command=/bin/bash\n"
        "Name=Shell\n"
        "Parent=FALLBACK/\n";
    static const char plasma_systemtray_block[] =
        "[Containments][2][Applets][7]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.systemtray\n"
        "\n"
        "[Containments][2][Applets][7][Configuration][General]\n"
        "extraItems=\n"
        "hiddenItems=\n"
        "knownItems=xv6-network-status\n"
        "shownItems=xv6-network-status\n"
        "\n";
    static const char plasma_appletsrc_fmt[] =
        "[Containments][1]\n"
        "activityId=%s\n"
        "formfactor=0\n"
        "immutability=1\n"
        "lastScreen=0\n"
        "location=0\n"
        "plugin=org.kde.plasma.folder\n"
        "wallpaperplugin=org.kde.image\n"
        "\n"
        "[Containments][1][Wallpaper][org.kde.image][General]\n"
        "FillMode=2\n"
        "Image=file:///usr/share/wallpapers/Next/contents/images/1280x800.png\n"
        "\n"
        "[Containments][2]\n"
        "activityId=%s\n"
        "formfactor=2\n"
        "immutability=1\n"
        "lastScreen=0\n"
        "location=4\n"
        "plugin=org.kde.panel\n"
        "\n"
        "[Containments][2][Applets][3]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.kickoff\n"
        "\n"
        "[Containments][2][Applets][3][Configuration][Shortcuts]\n"
        "global=Alt+F1\n"
        "\n"
        "[Containments][2][Applets][3][Configuration][General]\n"
        "favorites=xv6-terminal.desktop,org.kde.konsole.desktop,org.kde.dolphin.desktop,systemsettings.desktop,org.kde.kate.desktop\n"
        "favoritesPortedToKAstats=false\n"
        "\n"
        "[Containments][2][Applets][4]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.pager\n"
        "\n"
        "[Containments][2][Applets][5]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.icontasks\n"
        "\n"
        "[Containments][2][Applets][5][Configuration][General]\n"
        "launchers=applications:xv6-terminal.desktop,applications:systemsettings.desktop,applications:org.kde.dolphin.desktop,applications:org.kde.konsole.desktop,applications:org.kde.kate.desktop\n"
        "\n"
        "[Containments][2][Applets][6]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.marginsseparator\n"
        "\n"
        "%s"
        "[Containments][2][Applets][8]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.digitalclock\n"
        "\n"
        "[Containments][2][Applets][9]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.showdesktop\n"
        "\n"
        "[Containments][2][General]\n"
        "AppletOrder=%s\n";

    if (cmdline_has_flag("kde_plasma_systemtray=1")) {
        systemtray_block = plasma_systemtray_block;
        applet_order = "3;4;5;6;7;8;9";
    } else if (cmdline_has_flag("kde_plasma_systemtray=0")) {
        fprintf(stderr,
                "kde-session: Plasma system tray applet disabled by "
                "kde_plasma_systemtray=0\n");
    } else {
        fprintf(stderr,
                "kde-session: Plasma system tray applet disabled by default; "
                "enable with kde_plasma_systemtray=1\n");
    }

    snprintf(kactivitymanagerdrc, sizeof(kactivitymanagerdrc),
             kactivitymanagerdrc_fmt, activity_id, activity_id, activity_id);
    snprintf(plasma_appletsrc, sizeof(plasma_appletsrc),
             plasma_appletsrc_fmt, activity_id, activity_id, systemtray_block,
             applet_order);

    write_config_file("/dev/shm/kde-config/kwinrc", kwinrc);
    write_config_file("/dev/shm/kde-config/kdeglobals", kdeglobals);
    write_config_file("/dev/shm/kde-config/mimeapps.list", mimeapps);
    write_config_file("/dev/shm/kde-config/user-dirs.dirs", user_dirs);
    write_config_file("/dev/shm/kde-config/kactivitymanagerdrc", kactivitymanagerdrc);
    write_config_file("/dev/shm/kde-config/ksmserverrc", ksmserverrc);
    write_config_file("/dev/shm/kde-config/klipperrc", klipperrc);
    write_config_file("/dev/shm/kde-config/kded5rc", kded5rc);
    write_config_file("/dev/shm/kde-config/kwalletrc", kwalletrc);
    write_config_file("/dev/shm/kde-config/konsolerc", konsolerc);
    write_config_file("/dev/shm/kde-data/konsole/Shell.profile",
                      konsole_shell_profile);
    write_config_file("/root/.config/kwalletrc", kwalletrc);
    write_config_file("/root/.config/konsolerc", konsolerc);
    write_config_file("/root/.local/share/konsole/Shell.profile",
                      konsole_shell_profile);
    write_config_file("/dev/shm/kde-config/plasma-org.kde.plasma.desktop-appletsrc",
                      plasma_appletsrc);
    fprintf(stderr, "kde-session: seeded KWin OpenGL/effects guardrails\n");
}

static void set_kde_env(void)
{
    mkdir_one("/tmp", 01777);
    mkdir_one("/tmp/.X11-unix", 01777);
    mkdir_one("/dev/shm/xdg-runtime-root", 0700);
    mkdir_one("/dev/shm/kde-cache", 0700);
    mkdir_one("/dev/shm/kde-cache/thumbnails", 0700);
    mkdir_one("/dev/shm/kde-config", 0700);
    mkdir_one("/dev/shm/kde-data", 0700);
    mkdir_one("/dev/shm/kde-data/konsole", 0700);
    mkdir_one("/dev/shm/kde-data/klipper", 0700);
    mkdir_one("/dev/shm/kde-state", 0700);
    mkdir_one("/root/.config", 0700);
    mkdir_one("/root/.local", 0755);
    mkdir_one("/root/.local/share", 0755);
    mkdir_one("/root/.local/share/konsole", 0700);
    mkdir_one("/root/Desktop", 0755);
    mkdir_one("/root/Downloads", 0755);
    mkdir_one("/root/Templates", 0755);
    mkdir_one("/root/Public", 0755);
    mkdir_one("/root/Documents", 0755);
    mkdir_one("/root/Music", 0755);
    mkdir_one("/root/Pictures", 0755);
    mkdir_one("/root/Videos", 0755);
    mkdir_one("/root/.local/share/Trash", 0700);
    mkdir_one("/root/.local/share/Trash/files", 0700);
    mkdir_one("/root/.local/share/Trash/info", 0700);

    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("SHELL", "/bin/bash", 1);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("XDG_CACHE_HOME", "/dev/shm/kde-cache", 1);
    setenv("XDG_CONFIG_HOME", "/dev/shm/kde-config", 1);
    setenv("XDG_DATA_HOME", "/dev/shm/kde-data", 1);
    setenv("XDG_STATE_HOME", "/dev/shm/kde-state", 1);
    setenv("XDG_DATA_DIRS", "/usr/share:/share", 1);
    setenv("XDG_CONFIG_DIRS", "/etc/xdg", 1);
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("XDG_SESSION_ID", "1", 1);
    setenv("XDG_SEAT", "seat0", 1);
    setenv("XDG_VTNR", "1", 1);
    setenv("KDE_FULL_SESSION", "true", 1);
    setenv("KDE_SESSION_VERSION", "5", 1);
    setenv("KWIN_COMPOSE", "O2ES", 1);
    setenv("KWIN_OPENGL_INTERFACE", "egl", 1);
    setenv("KWIN_DRM_DEVICES", "/dev/dri/card0", 1);
    setenv("QT_QPA_PLATFORM", "wayland", 1);
    setenv("QT_LOGGING_RULES", "kf.*.debug=false;qt.qpa.*.debug=false", 0);
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 0);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    setenv("LD_LIBRARY_PATH",
           "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:"
           "/lib/x86_64-linux-gnu:/usr/lib:/lib",
           1);
    setenv("LD_PRELOAD",
           "/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so:"
           "/usr/lib/x86_64-linux-gnu/libKF5Codecs.so.5:"
           "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0",
           0);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri", 1);
    setenv("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm", 1);
    setenv("LIBGL_ALWAYS_SOFTWARE", "0", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 0);
    setenv("GALLIUM_DRIVER", "virgl", 0);
    setenv("PULSE_COOKIE", "/dev/shm/kde-config/pulse/cookie", 1);
    if (cmdline_has_flag("kde_libinput_trace=1"))
        setenv("XV6_LIBINPUT_TRACE", "1", 1);
    if (cmdline_has_flag("kde_wayland_debug=1"))
        setenv("WAYLAND_DEBUG", "1", 1);
    if (cmdline_has_flag("kde_xwayland_loader_debug=1")) {
        setenv("XV6_XWAYLAND_LOADER_DEBUG", "1", 1);
        fprintf(stderr, "kde-session: Xwayland loader debug enabled\n");
    }
    if (cmdline_has_flag("kde_xwayland_enable_glx=1")) {
        setenv("XV6_XWAYLAND_ENABLE_GLX", "1", 1);
        fprintf(stderr, "kde-session: Xwayland GLX extension enable requested\n");
    }
    {
        char glamor[16];

        if (cmdline_get_value("kde_xwayland_glamor", glamor, sizeof(glamor)) &&
            valid_xwayland_glamor_mode(glamor)) {
            setenv("XV6_XWAYLAND_GLAMOR", glamor, 1);
            fprintf(stderr, "kde-session: Xwayland glamor mode=%s\n", glamor);
        }
    }
    {
        char virgl_debug[128];

        if (cmdline_get_value("kde_xwayland_virgl_debug", virgl_debug,
                              sizeof(virgl_debug))) {
            if (valid_xwayland_virgl_debug(virgl_debug)) {
                setenv("XV6_XWAYLAND_VIRGL_DEBUG", virgl_debug, 1);
                fprintf(stderr, "kde-session: Xwayland VIRGL_DEBUG=%s\n",
                        virgl_debug);
            } else {
                fprintf(stderr,
                        "kde-session: ignoring invalid Xwayland VIRGL_DEBUG value\n");
            }
        }
    }
    {
        char verbose[16];

        int verbose_status = cmdline_get_value_status("kde_xwayland_verbose",
                                                      verbose,
                                                      sizeof(verbose));

        if (verbose_status != 0) {
            if (verbose_status > 0 && valid_decimal_range(verbose, 0, 9)) {
                setenv("XV6_XWAYLAND_VERBOSE", verbose, 1);
                fprintf(stderr, "kde-session: Xwayland verbose=%s\n",
                        verbose);
            } else {
                fprintf(stderr,
                        "kde-session: ignoring invalid Xwayland verbose value\n");
            }
        }
    }
    {
        char audit[16];
        int audit_status = cmdline_get_value_status("kde_xwayland_audit",
                                                    audit, sizeof(audit));

        if (audit_status != 0) {
            if (audit_status > 0 && valid_decimal_range(audit, 0, 9)) {
                setenv("XV6_XWAYLAND_AUDIT", audit, 1);
                fprintf(stderr, "kde-session: Xwayland audit=%s\n", audit);
            } else {
                fprintf(stderr,
                        "kde-session: ignoring invalid Xwayland audit value\n");
            }
        }
    }
    seed_pulse_cookie();
    seed_kde_config();
}

static void clear_client_display_env(void)
{
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("DISPLAY");
}

static int is_executable(const char *path)
{
    return access(path, X_OK) == 0;
}

static int child_console_logs_enabled(void)
{
    return cmdline_has_flag("kde_child_console_logs=1");
}

static void redirect_stdio_to_log(const char *path)
{
    int fd;

    if (!path || child_console_logs_enabled())
        return;

    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0)
        return;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

static pid_t spawn_child_logged(char *const argv[], const char *log_path)
{
    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "kde-session: fork %s: %s\n", argv[0], strerror(errno));
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        redirect_stdio_to_log(log_path);
        execv(argv[0], argv);
        fprintf(stderr, "kde-session: exec %s failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);
    return pid;
}

static pid_t spawn_child(char *const argv[])
{
    return spawn_child_logged(argv, NULL);
}

static int pre_kwin_libinput_probe_enabled(void)
{
    return cmdline_has_flag("kde_pre_kwin_libinput_probe=1") ||
           cmdline_has_flag("kde_pre_kwin_libinput_probe_only=1") ||
           env_bool_enabled("KDE_PRE_KWIN_LIBINPUT_PROBE");
}

static int pre_kwin_libinput_probe_only_enabled(void)
{
    return cmdline_has_flag("kde_pre_kwin_libinput_probe_only=1") ||
           env_bool_enabled("KDE_PRE_KWIN_LIBINPUT_PROBE_ONLY");
}

static int kwin_alloc_trace_enabled(void)
{
    static int initialized;
    static int enabled;

    if (!initialized) {
        enabled = cmdline_has_flag("kde_kwin_alloc_trace=1") &&
                  access(KWIN_ALLOC_TRACE_PRELOAD, R_OK) == 0;
        initialized = 1;
        if (cmdline_has_flag("kde_kwin_alloc_trace=1")) {
            fprintf(stderr, "kde-session: KWin alloc trace %s path=%s log=%s\n",
                    enabled ? "enabled" : "unavailable",
                    KWIN_ALLOC_TRACE_PRELOAD, KWIN_ALLOC_TRACE_LOG);
        }
    }
    return enabled;
}

static int kwin_ioctl_trace_enabled(void)
{
    static int initialized;
    static int enabled;

    if (!initialized) {
        enabled = cmdline_has_flag("kde_kwin_ioctl_trace=1") &&
                  access(KWIN_IOCTL_TRACE_PRELOAD, R_OK) == 0;
        initialized = 1;
        if (cmdline_has_flag("kde_kwin_ioctl_trace=1")) {
            fprintf(stderr, "kde-session: KWin ioctl trace %s path=%s log=%s\n",
                    enabled ? "enabled" : "unavailable",
                    KWIN_IOCTL_TRACE_PRELOAD, KWIN_IOCTL_TRACE_LOG);
        }
    }
    return enabled;
}

static int kwin_ld_preload_mode(void)
{
    static int initialized;
    static int mode;

    if (!initialized) {
        if (cmdline_has_flag("kde_kwin_inherit_ld_preload=1"))
            mode = 0;
        else if (cmdline_has_flag("kde_kwin_compat_ld_preload=1"))
            mode = 1;
        else
            mode = 2;
        initialized = 1;
        if (mode == 0) {
            fprintf(stderr,
                    "kde-session: KWin inherited LD_PRELOAD enabled by cmdline\n");
        } else if (mode == 1) {
            fprintf(stderr,
                    "kde-session: KWin inherited LD_PRELOAD compat enabled by cmdline compat=%s\n",
                    KWIN_COMPAT_PRELOAD);
        } else {
            fprintf(stderr,
                    "kde-session: KWin inherited LD_PRELOAD disabled by default\n");
        }
    }
    return mode;
}

static pid_t spawn_kwin_child(char *const argv[], int attempt)
{
    const char *old_preload = getenv("LD_PRELOAD");
    char old_preload_buf[1024];
    char base_preload[1408];
    char trace_preload[1792];
    char attempt_buf[32];
    int had_preload = old_preload != NULL;
    int preload_mode = kwin_ld_preload_mode();
    int alloc_trace = kwin_alloc_trace_enabled();
    int ioctl_trace = kwin_ioctl_trace_enabled();
    pid_t pid;

    if (preload_mode == 0 && !alloc_trace && !ioctl_trace)
        return spawn_child_logged(argv, KWIN_SESSION_LOG);

    if (old_preload)
        snprintf(old_preload_buf, sizeof(old_preload_buf), "%s", old_preload);
    else
        old_preload_buf[0] = '\0';

    /* Build the base preload tail (existing alloc-trace/compat/inherit logic). */
    if (alloc_trace && old_preload_buf[0] && preload_mode == 0) {
        snprintf(base_preload, sizeof(base_preload), "%s:%s",
                 KWIN_ALLOC_TRACE_PRELOAD, old_preload_buf);
    } else if (alloc_trace && preload_mode == 1) {
        snprintf(base_preload, sizeof(base_preload), "%s:%s",
                 KWIN_ALLOC_TRACE_PRELOAD, KWIN_COMPAT_PRELOAD);
    } else if (alloc_trace) {
        snprintf(base_preload, sizeof(base_preload), "%s",
                 KWIN_ALLOC_TRACE_PRELOAD);
    } else if (preload_mode == 1) {
        snprintf(base_preload, sizeof(base_preload), "%s",
                 KWIN_COMPAT_PRELOAD);
    } else if (preload_mode == 0 && old_preload_buf[0]) {
        snprintf(base_preload, sizeof(base_preload), "%s", old_preload_buf);
    } else {
        base_preload[0] = '\0';
    }

    /* Prepend the ioctl trace preload if enabled (composes with the above). */
    if (ioctl_trace && base_preload[0]) {
        snprintf(trace_preload, sizeof(trace_preload), "%s:%s",
                 KWIN_IOCTL_TRACE_PRELOAD, base_preload);
    } else if (ioctl_trace) {
        snprintf(trace_preload, sizeof(trace_preload), "%s",
                 KWIN_IOCTL_TRACE_PRELOAD);
    } else {
        snprintf(trace_preload, sizeof(trace_preload), "%s", base_preload);
    }

    if (trace_preload[0]) {
        setenv("LD_PRELOAD", trace_preload, 1);
    } else {
        unsetenv("LD_PRELOAD");
    }
    snprintf(attempt_buf, sizeof(attempt_buf), "%d", attempt);
    if (alloc_trace) {
        setenv("KWIN_ALLOC_TRACE_LOG", KWIN_ALLOC_TRACE_LOG, 1);
        setenv("KWIN_ALLOC_TRACE_ATTEMPT", attempt_buf, 1);
    }
    if (ioctl_trace) {
        setenv("KWIN_IOCTL_TRACE_LOG", KWIN_IOCTL_TRACE_LOG, 1);
    }
    pid = spawn_child_logged(argv, KWIN_SESSION_LOG);
    if (had_preload)
        setenv("LD_PRELOAD", old_preload_buf, 1);
    else
        unsetenv("LD_PRELOAD");
    unsetenv("KWIN_ALLOC_TRACE_LOG");
    unsetenv("KWIN_ALLOC_TRACE_ATTEMPT");
    unsetenv("KWIN_IOCTL_TRACE_LOG");
    return pid;
}

static void restore_ld_preload(const char *old_preload, int had_preload)
{
    if (had_preload)
        setenv("LD_PRELOAD", old_preload, 1);
    else
        unsetenv("LD_PRELOAD");
}

static void apply_kwin_ld_preload_policy_no_trace(void)
{
    int preload_mode = kwin_ld_preload_mode();

    if (preload_mode == 1)
        setenv("LD_PRELOAD", KWIN_COMPAT_PRELOAD, 1);
    else if (preload_mode == 2)
        unsetenv("LD_PRELOAD");
}

static int maybe_run_pre_kwin_libinput_probe(void)
{
    char *argv[] = {
        "/bin/kde-libinput-probe",
        "--r9-cursor-contract",
        "--timeout-ms",
        "1",
        NULL
    };
    const char *old_preload = getenv("LD_PRELOAD");
    char old_preload_buf[1024];
    int had_preload = old_preload != NULL;
    pid_t pid;
    int status = 0;

    if (!pre_kwin_libinput_probe_enabled())
        return 0;

    if (!is_executable(argv[0])) {
        fprintf(stderr,
                "kde-session: pre-kwin-libinput-probe status=SKIP reason=missing path=%s log=%s\n",
                argv[0], PRE_KWIN_LIBINPUT_LOG);
        return pre_kwin_libinput_probe_only_enabled() ? 127 : 0;
    }

    if (old_preload)
        snprintf(old_preload_buf, sizeof(old_preload_buf), "%s", old_preload);
    else
        old_preload_buf[0] = '\0';

    unlink(PRE_KWIN_LIBINPUT_LOG);
    apply_kwin_ld_preload_policy_no_trace();
    pid = spawn_child_logged(argv, PRE_KWIN_LIBINPUT_LOG);
    restore_ld_preload(old_preload_buf, had_preload);
    if (pid < 0) {
        fprintf(stderr,
                "kde-session: pre-kwin-libinput-probe status=FAIL reason=fork log=%s\n",
                PRE_KWIN_LIBINPUT_LOG);
        return 127;
    }

    for (int waited_ms = 0; waited_ms < 5000; waited_ms += 100) {
        pid_t got = waitpid(pid, &status, WNOHANG);

        if (got == pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                fprintf(stderr,
                        "kde-session: pre-kwin-libinput-probe status=PASS exit_status=0 log=%s\n",
                        PRE_KWIN_LIBINPUT_LOG);
                return 0;
            } else if (WIFEXITED(status)) {
                fprintf(stderr,
                        "kde-session: pre-kwin-libinput-probe status=FAIL exit_status=%d log=%s\n",
                        WEXITSTATUS(status), PRE_KWIN_LIBINPUT_LOG);
                return WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr,
                        "kde-session: pre-kwin-libinput-probe status=FAIL signal=%d log=%s\n",
                        WTERMSIG(status), PRE_KWIN_LIBINPUT_LOG);
                return 128 + WTERMSIG(status);
            } else {
                fprintf(stderr,
                        "kde-session: pre-kwin-libinput-probe status=FAIL wait_status=%d log=%s\n",
                        status, PRE_KWIN_LIBINPUT_LOG);
                return 126;
            }
        }
        if (got < 0 && errno != EINTR) {
            fprintf(stderr,
                    "kde-session: pre-kwin-libinput-probe status=FAIL reason=wait errno=%d %s log=%s\n",
                    errno, strerror(errno), PRE_KWIN_LIBINPUT_LOG);
            return 127;
        }
        usleep(100000);
    }

    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    fprintf(stderr,
            "kde-session: pre-kwin-libinput-probe status=TIMEOUT timeout_ms=5000 log=%s\n",
            PRE_KWIN_LIBINPUT_LOG);
    return 124;
}

static int process_cmdline_contains(const char *needle)
{
    DIR *dir = opendir("/proc");
    struct dirent *de;

    if (dir == NULL)
        return 0;

    while ((de = readdir(dir)) != NULL) {
        char path[320];
        char buf[512];
        int all_digits = 1;
        int fd;
        ssize_t n;

        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;

        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0)
            continue;
        buf[n] = '\0';
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\0')
                buf[i] = ' ';
        }
        if (strstr(buf, needle) != NULL) {
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);
    return 0;
}

static void kwin_proc_snapshot_path(char *path, size_t size, int attempt,
                                    const char *name)
{
    snprintf(path, size, "/dev/shm/kde-kwin-attempt-%d.%s", attempt, name);
}

static int kwin_proc_snapshot_enabled(void)
{
    static int initialized;
    static int enabled;

    if (!initialized) {
        enabled = !cmdline_has_flag("kde_kwin_proc_snapshot=0");
        initialized = 1;
        if (!enabled)
            fprintf(stderr, "kde-session: KWin proc snapshots disabled by cmdline\n");
    }

    return enabled;
}

static int write_all(int fd, const char *buf, size_t size)
{
    size_t off = 0;

    while (off < size) {
        ssize_t n = write(fd, buf + off, size - off);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        off += (size_t)n;
    }
    return 0;
}

static void snapshot_kwin_proc_file(pid_t kwin_pid, int attempt,
                                    const char *name)
{
    char proc_path[128];
    char out_path[128];
    char tmp_path[160];
    char buf[1024];
    int in_fd;
    int out_fd;
    ssize_t n;

    snprintf(proc_path, sizeof(proc_path), "/proc/%d/%s", (int)kwin_pid, name);
    kwin_proc_snapshot_path(out_path, sizeof(out_path), attempt, name);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", out_path);

    in_fd = open(proc_path, O_RDONLY | O_CLOEXEC);
    if (in_fd < 0)
        return;
    out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (out_fd < 0) {
        close(in_fd);
        return;
    }

    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        if (write_all(out_fd, buf, (size_t)n) < 0)
            break;
    }

    close(out_fd);
    close(in_fd);
    rename(tmp_path, out_path);
}

static void snapshot_kwin_proc(pid_t kwin_pid, int attempt)
{
    if (!kwin_proc_snapshot_enabled())
        return;

    snapshot_kwin_proc_file(kwin_pid, attempt, "maps");
    snapshot_kwin_proc_file(kwin_pid, attempt, "status");
    snapshot_kwin_proc_file(kwin_pid, attempt, "stat");
    snapshot_kwin_proc_file(kwin_pid, attempt, "statm");
    snapshot_kwin_proc_file(kwin_pid, attempt, "wchan");
    snapshot_kwin_proc_file(kwin_pid, attempt, "syscall");
    snapshot_kwin_proc_file(kwin_pid, attempt, "stack");
}

static void print_kwin_proc_snapshot_file(int attempt, const char *phase,
                                          const char *name)
{
    char path[128];
    char buf[1024];
    int fd;
    ssize_t n;

    kwin_proc_snapshot_path(path, sizeof(path), attempt, name);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "kde-session: kwin %s unavailable attempt=%d phase=%s errno=%d %s\n",
                name, attempt, phase, errno, strerror(errno));
        return;
    }

    fprintf(stderr, "kde-session: kwin %s begin attempt=%d phase=%s\n",
            name, attempt, phase);
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write_all(STDERR_FILENO, buf, (size_t)n) < 0)
            break;
    }
    close(fd);
    fprintf(stderr, "kde-session: kwin %s end attempt=%d phase=%s\n",
            name, attempt, phase);
}

static int read_small_text_file(const char *path, char *buf, size_t size)
{
    int fd;
    ssize_t n;

    if (size == 0)
        return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        buf[0] = '\0';
        return 0;
    }
    n = read(fd, buf, size - 1);
    close(fd);
    if (n <= 0) {
        buf[0] = '\0';
        return 0;
    }
    buf[n] = '\0';
    return (int)n;
}

static void trim_newline(char *buf)
{
    if (buf == NULL)
        return;
    for (char *p = buf; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            *p = '\0';
            return;
        }
    }
}

static void print_kwin_wait_fd_target(pid_t kwin_pid, int fd)
{
    char path[128];
    char target[512];
    ssize_t n;

    snprintf(path, sizeof(path), "/proc/%d/fd/%d", (int)kwin_pid, fd);
    n = readlink(path, target, sizeof(target) - 1);
    if (n < 0) {
        fprintf(stderr,
                "kde-session: kwin wait-fd pid=%d fd=%d readlink_errno=%d %s\n",
                (int)kwin_pid, fd, errno, strerror(errno));
        return;
    }
    target[n] = '\0';
    fprintf(stderr, "kde-session: kwin wait-fd pid=%d fd=%d target=%s\n",
            (int)kwin_pid, fd, target);
}

static void print_kwin_fd_summary(pid_t kwin_pid, int attempt,
                                  const char *phase)
{
    char fd_dir[128];
    DIR *dir;
    struct dirent *de;
    int total = 0;
    int socket_count = 0;
    int anon_count = 0;
    int dri_count = 0;
    int event_count = 0;
    int emitted = 0;

    snprintf(fd_dir, sizeof(fd_dir), "/proc/%d/fd", (int)kwin_pid);
    dir = opendir(fd_dir);
    if (dir == NULL) {
        fprintf(stderr,
                "kde-session: kwin fd-summary attempt=%d phase=%s pid=%d "
                "opendir_errno=%d %s\n",
                attempt, phase, (int)kwin_pid, errno, strerror(errno));
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char link_path[512];
        char target[512];
        ssize_t n;

        if (de->d_name[0] == '.')
            continue;
        total++;
        snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir, de->d_name);
        n = readlink(link_path, target, sizeof(target) - 1);
        if (n < 0)
            continue;
        target[n] = '\0';
        if (strstr(target, "socket:") != NULL)
            socket_count++;
        if (strstr(target, "anon_inode") != NULL)
            anon_count++;
        if (strstr(target, "/dev/dri") != NULL)
            dri_count++;
        if (strstr(target, "event") != NULL ||
            strstr(target, "timerfd") != NULL ||
            strstr(target, "signalfd") != NULL)
            event_count++;
        if (emitted < 64 &&
            (strstr(target, "socket:") != NULL ||
             strstr(target, "anon_inode") != NULL ||
             strstr(target, "/dev/dri") != NULL ||
             strstr(target, "/dev/input") != NULL ||
             strstr(target, "pipe:") != NULL)) {
            fprintf(stderr,
                    "kde-session: kwin fd attempt=%d phase=%s pid=%d fd=%s "
                    "target=%s\n",
                    attempt, phase, (int)kwin_pid, de->d_name, target);
            emitted++;
        }
    }
    closedir(dir);

    fprintf(stderr,
            "kde-session: kwin fd-summary attempt=%d phase=%s pid=%d "
            "total=%d sockets=%d anon_inode=%d dri=%d event_like=%d "
            "emitted=%d\n",
            attempt, phase, (int)kwin_pid, total, socket_count, anon_count,
            dri_count, event_count, emitted);
}

static void print_kwin_thread_wait_detail(pid_t kwin_pid, const char *tid_name,
                                          const char *syscall_buf)
{
    unsigned long long nr, arg0, arg1, arg2, arg3, arg4, arg5, sp, pc;
    char mem_path[384];
    int memfd;

    if (sscanf(syscall_buf, "%llu %llx %llx %llx %llx %llx %llx %llx %llx",
               &nr, &arg0, &arg1, &arg2, &arg3, &arg4, &arg5, &sp, &pc) != 9)
        return;

    if (nr == 7 || nr == 271) {
        struct local_pollfd {
            int fd;
            short events;
            short revents;
        } fds[16];
        unsigned long long nfds = arg1;

        snprintf(mem_path, sizeof(mem_path), "/proc/%d/task/%s/mem",
                 (int)kwin_pid, tid_name);
        memfd = open(mem_path, O_RDONLY | O_CLOEXEC);
        if (memfd < 0) {
            fprintf(stderr,
                    "kde-session: kwin wait-detail pid=%d tid=%s syscall=%s "
                    "fds_addr=0x%llx nfds=%llu mem_open_errno=%d %s\n",
                    (int)kwin_pid, tid_name, nr == 7 ? "poll" : "ppoll",
                    arg0, arg1, errno, strerror(errno));
            return;
        }
        if (nfds > 16)
            nfds = 16;
        errno = 0;
        ssize_t n = pread(memfd, fds, nfds * sizeof(fds[0]), (off_t)arg0);
        fprintf(stderr,
                "kde-session: kwin wait-detail pid=%d tid=%s syscall=%s "
                "fds_addr=0x%llx nfds=%llu read=%ld errno=%d %s\n",
                (int)kwin_pid, tid_name, nr == 7 ? "poll" : "ppoll",
                arg0, arg1, (long)n, errno, strerror(errno));
        if (n > 0) {
            int got = (int)(n / (ssize_t)sizeof(fds[0]));
            for (int i = 0; i < got; i++) {
                fprintf(stderr,
                        "kde-session: kwin pollfd pid=%d tid=%s idx=%d fd=%d "
                        "events=0x%x revents=0x%x\n",
                        (int)kwin_pid, tid_name, i, fds[i].fd,
                        (unsigned short)fds[i].events,
                        (unsigned short)fds[i].revents);
                if (fds[i].fd >= 0)
                    print_kwin_wait_fd_target(kwin_pid, fds[i].fd);
            }
        }
        close(memfd);
    } else if (nr == 202) {
        unsigned int word = 0;

        snprintf(mem_path, sizeof(mem_path), "/proc/%d/task/%s/mem",
                 (int)kwin_pid, tid_name);
        memfd = open(mem_path, O_RDONLY | O_CLOEXEC);
        if (memfd < 0) {
            fprintf(stderr,
                    "kde-session: kwin wait-detail pid=%d tid=%s syscall=futex "
                    "uaddr=0x%llx op=0x%llx val=0x%llx mem_open_errno=%d %s\n",
                    (int)kwin_pid, tid_name, arg0, arg1, arg2, errno,
                    strerror(errno));
            return;
        }
        errno = 0;
        ssize_t n = pread(memfd, &word, sizeof(word), (off_t)arg0);
        fprintf(stderr,
                "kde-session: kwin wait-detail pid=%d tid=%s syscall=futex "
                "uaddr=0x%llx op=0x%llx val=0x%llx word_read=%ld "
                "word=0x%x errno=%d %s\n",
                (int)kwin_pid, tid_name, arg0, arg1, arg2, (long)n, word,
                errno, strerror(errno));
        close(memfd);
    } else if (nr == 232 || nr == 281) {
        fprintf(stderr,
                "kde-session: kwin wait-detail pid=%d tid=%s syscall=%s "
                "epfd=%llu events=0x%llx maxevents=%llu timeout=0x%llx\n",
                (int)kwin_pid, tid_name, nr == 232 ? "epoll_wait" : "epoll_pwait",
                arg0, arg1, arg2, arg3);
        print_kwin_wait_fd_target(kwin_pid, (int)arg0);
    }
}

static void print_kwin_thread_snapshot(pid_t kwin_pid, int attempt,
                                       const char *phase)
{
    char task_dir[128];
    DIR *dir;
    struct dirent *de;
    int emitted = 0;

    snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", (int)kwin_pid);
    dir = opendir(task_dir);
    if (dir == NULL) {
        fprintf(stderr,
                "kde-session: kwin task-summary attempt=%d phase=%s pid=%d "
                "opendir_errno=%d %s\n",
                attempt, phase, (int)kwin_pid, errno, strerror(errno));
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        char path[384];
        char stat_buf[512];
        char wchan_buf[128];
        char syscall_buf[256];
        char stack_buf[512];
        int all_digits = 1;

        if (de->d_name[0] == '.')
            continue;
        for (const char *p = de->d_name; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
        }
        if (!all_digits)
            continue;
        if (emitted >= 64) {
            fprintf(stderr,
                    "kde-session: kwin task-summary attempt=%d phase=%s "
                    "pid=%d truncated_after=%d\n",
                    attempt, phase, (int)kwin_pid, emitted);
            break;
        }

        snprintf(path, sizeof(path), "/proc/%d/task/%s/stat",
                 (int)kwin_pid, de->d_name);
        read_small_text_file(path, stat_buf, sizeof(stat_buf));
        trim_newline(stat_buf);
        snprintf(path, sizeof(path), "/proc/%d/task/%s/wchan",
                 (int)kwin_pid, de->d_name);
        read_small_text_file(path, wchan_buf, sizeof(wchan_buf));
        trim_newline(wchan_buf);
        snprintf(path, sizeof(path), "/proc/%d/task/%s/syscall",
                 (int)kwin_pid, de->d_name);
        read_small_text_file(path, syscall_buf, sizeof(syscall_buf));
        trim_newline(syscall_buf);
        snprintf(path, sizeof(path), "/proc/%d/task/%s/stack",
                 (int)kwin_pid, de->d_name);
        read_small_text_file(path, stack_buf, sizeof(stack_buf));
        trim_newline(stack_buf);

        fprintf(stderr,
                "kde-session: kwin task attempt=%d phase=%s pid=%d tid=%s "
                "stat=\"%s\" wchan=\"%s\" syscall=\"%s\" stack=\"%s\"\n",
                attempt, phase, (int)kwin_pid, de->d_name, stat_buf,
                wchan_buf, syscall_buf, stack_buf);
        print_kwin_thread_wait_detail(kwin_pid, de->d_name, syscall_buf);
        emitted++;
    }
    closedir(dir);

    fprintf(stderr,
            "kde-session: kwin task-summary attempt=%d phase=%s pid=%d "
            "emitted=%d\n",
            attempt, phase, (int)kwin_pid, emitted);
}

static void print_kwin_proc_snapshot(int attempt, const char *phase)
{
    if (!kwin_proc_snapshot_enabled()) {
        fprintf(stderr,
                "kde-session: kwin proc snapshot skipped attempt=%d phase=%s reason=disabled\n",
                attempt, phase);
        return;
    }

    print_kwin_proc_snapshot_file(attempt, phase, "status");
    print_kwin_proc_snapshot_file(attempt, phase, "stat");
    print_kwin_proc_snapshot_file(attempt, phase, "statm");
    print_kwin_proc_snapshot_file(attempt, phase, "wchan");
    print_kwin_proc_snapshot_file(attempt, phase, "syscall");
    print_kwin_proc_snapshot_file(attempt, phase, "stack");
    print_kwin_proc_snapshot_file(attempt, phase, "maps");
}

static void print_kwin_blocked_snapshot(pid_t kwin_pid, int attempt,
                                        const char *phase)
{
    print_kwin_proc_snapshot(attempt, phase);
    print_kwin_fd_summary(kwin_pid, attempt, phase);
    print_kwin_thread_snapshot(kwin_pid, attempt, phase);
}

static int wait_for_wayland_socket(pid_t kwin_pid, int attempt)
{
    const char *socket_path = "/dev/shm/xdg-runtime-root/wayland-0";

    for (int i = 0; i < 600; i++) {
        struct stat st;
        int status;

        if ((i % 10) == 0)
            snapshot_kwin_proc(kwin_pid, attempt);
        if (stat(socket_path, &st) == 0)
            return 0;

        if (waitpid(kwin_pid, &status, WNOHANG) == kwin_pid) {
            fprintf(stderr, "kde-session: kwin exited before Wayland socket status=%d\n", status);
            print_kwin_blocked_snapshot(kwin_pid, attempt, "wayland-socket");
            return -1;
        }
        usleep(100000);
    }

    fprintf(stderr, "kde-session: %s not visible after compositor grace\n",
            socket_path);
    snapshot_kwin_proc(kwin_pid, attempt);
    print_kwin_blocked_snapshot(kwin_pid, attempt, "wayland-socket-timeout");
    return -1;
}

static void note_xwayland_state(pid_t kwin_pid, int attempt)
{
    (void)kwin_pid;
    (void)attempt;

    if (process_cmdline_contains("Xwayland"))
        fprintf(stderr, "kde-session: Xwayland is running\n");
    else
        fprintf(stderr, "kde-session: Xwayland deferred until first X11 client\n");
}

static int wait_for_plasmashell(pid_t kwin_pid, pid_t plasma_pid, int attempt)
{
    for (int i = 0; i < 900; i++) {
        int status;

        if ((i % 10) == 0)
            snapshot_kwin_proc(kwin_pid, attempt);
        if (process_cmdline_contains("plasmashell"))
            return 0;

        if (waitpid(kwin_pid, &status, WNOHANG) == kwin_pid) {
            fprintf(stderr, "kde-session: kwin exited before plasmashell status=%d\n", status);
            print_kwin_proc_snapshot(attempt, "plasmashell");
            return -1;
        }
        if (waitpid(plasma_pid, &status, WNOHANG) == plasma_pid) {
            fprintf(stderr, "kde-session: Plasma child exited before plasmashell status=%d\n", status);
            print_kwin_proc_snapshot(attempt, "plasma-child");
            return -1;
        }
        usleep(100000);
    }

    fprintf(stderr, "kde-session: plasmashell not visible after session grace\n");
    snapshot_kwin_proc(kwin_pid, attempt);
    print_kwin_proc_snapshot(attempt, "plasmashell-timeout");
    return -1;
}

static void terminate_child(pid_t pid)
{
    int status;

    if (pid <= 0)
        return;
    if (waitpid(pid, &status, WNOHANG) == pid)
        return;
    kill(-pid, SIGTERM);
    kill(pid, SIGTERM);
    for (int i = 0; i < 50; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return;
        usleep(100000);
    }
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}

static pid_t maybe_spawn_smoke_agent(void)
{
    char *agent_plain[] = { "/bin/kde-smoke-agent", NULL };
    char *agent_chromium[] = {
        "/bin/kde-smoke-agent",
        "--require-chromium",
        NULL
    };

    if (!cmdline_has_flag("kde_smoke_agent=1"))
        return -1;
    if (!is_executable("/bin/kde-smoke-agent")) {
        fprintf(stderr, "kde-session: smoke agent unavailable\n");
        return -1;
    }

    fprintf(stderr, "kde-session: launching smoke agent\n");
    if (cmdline_has_flag("kde_smoke_require_chromium=1"))
        return spawn_child(agent_chromium);
    return spawn_child(agent_plain);
}

static pid_t maybe_spawn_network_status_sni(void)
{
    char *argv[] = { "/bin/xv6-network-status-sni", NULL };

    if (cmdline_has_flag("kde_network_status_sni=0"))
        return -1;
    if (!cmdline_has_flag("kde_network_status_sni=1")) {
        fprintf(stderr,
                "kde-session: network status SNI disabled by default; "
                "enable with kde_network_status_sni=1\n");
        return -1;
    }
    if (!is_executable(argv[0])) {
        fprintf(stderr, "kde-session: network status SNI unavailable\n");
        return -1;
    }

    fprintf(stderr, "kde-session: launching network status SNI\n");
    return spawn_child_logged(argv, NETWORK_SNI_LOG);
}

static int desktop_wake_delay_ms(void)
{
    char value[32];
    int rc = cmdline_get_value_status("kde_desktop_wake_delay_ms", value,
                                      sizeof(value));

    if (rc > 0 && valid_decimal_range(value, 0,
                                      KDE_DESKTOP_WAKE_MAX_DELAY_MS))
        return atoi(value);
    if (rc != 0)
        fprintf(stderr,
                "kde-session: ignoring invalid kde_desktop_wake_delay_ms\n");
    return KDE_DESKTOP_WAKE_DEFAULT_DELAY_MS;
}

static int wait_for_exec_child(pid_t pid, const char *label)
{
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        fprintf(stderr, "kde-session: wait %s: %s\n", label, strerror(errno));
        return -1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    fprintf(stderr, "kde-session: %s exited status=%d\n", label, status);
    return -1;
}

static int run_mouseinject_abs(int x, int y, int buttons)
{
    char xbuf[32];
    char ybuf[32];
    char buttonsbuf[32];
    char *argv[] = { "/bin/mouseinject", xbuf, ybuf, buttonsbuf, NULL };
    pid_t pid;

    snprintf(xbuf, sizeof(xbuf), "%d", x);
    snprintf(ybuf, sizeof(ybuf), "%d", y);
    snprintf(buttonsbuf, sizeof(buttonsbuf), "%d", buttons);

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "kde-session: fork mouseinject: %s\n",
                strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr, "kde-session: exec %s failed: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }
    return wait_for_exec_child(pid, "mouseinject");
}

static void run_desktop_wake_child(int delay_ms)
{
    if (!is_executable("/bin/mouseinject")) {
        fprintf(stderr, "kde-session: desktop wake unavailable\n");
        _exit(127);
    }

    usleep((useconds_t)delay_ms * 1000U);
    fprintf(stderr, "kde-session: desktop wake click start delay_ms=%d\n",
            delay_ms);
    if (run_mouseinject_abs(32768, 32768, 0) != 0 ||
        run_mouseinject_abs(32768, 32768, 1) != 0 ||
        run_mouseinject_abs(32768, 32768, 0) != 0) {
        fprintf(stderr, "kde-session: desktop wake click failed\n");
        _exit(1);
    }
    fprintf(stderr, "kde-session: desktop wake click done\n");
    _exit(0);
}

static pid_t maybe_spawn_desktop_wake(void)
{
    int delay_ms;
    pid_t pid;

    if (!cmdline_has_flag("kde_desktop_wake=1"))
        return -1;

    delay_ms = desktop_wake_delay_ms();
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "kde-session: fork desktop wake: %s\n",
                strerror(errno));
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        run_desktop_wake_child(delay_ms);
    }
    setpgid(pid, pid);
    fprintf(stderr, "kde-session: launching desktop wake delay_ms=%d\n",
            delay_ms);
    return pid;
}

static void reap_desktop_wake(pid_t pid)
{
    int status;

    if (pid <= 0)
        return;
    if (waitpid(pid, &status, WNOHANG) == pid)
        return;
}

static void x11_egl_logf(const char *fmt, ...)
{
    int fd;
    va_list ap;

    fd = open(X11_EGL_SESSION_LOG,
              O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0)
        return;
    (void)lseek(fd, 0, SEEK_END);
    va_start(ap, fmt);
    vdprintf(fd, fmt, ap);
    va_end(ap);
    fsync(fd);
    close(fd);
}

static int proc_name_all_digits(const char *name)
{
    if (!name || name[0] == '\0')
        return 0;
    for (const char *p = name; *p; p++) {
        if (!isdigit((unsigned char)*p))
            return 0;
    }
    return 1;
}

static void x11_egl_copy_token(char *dst, size_t dst_size, const char *tok)
{
    size_t len;

    if (dst_size == 0)
        return;
    if (!tok)
        tok = "";

    len = strnlen(tok, dst_size - 1);
    memcpy(dst, tok, len);
    dst[len] = '\0';
}

static void x11_egl_append_token(char *dst, size_t dst_size, const char *tok)
{
    size_t len;
    size_t tok_len;

    if (dst_size == 0 || !tok || tok[0] == '\0')
        return;

    len = strnlen(dst, dst_size);
    if (len >= dst_size)
        return;

    tok_len = strnlen(tok, dst_size - len - 1);
    memcpy(dst + len, tok, tok_len);
    dst[len + tok_len] = '\0';
}

static void x11_egl_append_argv_token(char *argv, size_t argv_size,
                                      const char *tok)
{
    size_t len;

    if (argv_size == 0 || !tok || tok[0] == '\0')
        return;
    len = strlen(argv);
    if (len > 0 && len + 1 < argv_size) {
        argv[len++] = ' ';
        argv[len] = '\0';
    }
    if (len + 1 < argv_size)
        x11_egl_append_token(argv + len, argv_size - len, tok);
}

static int x11_egl_parse_xwayland_cmdline(char *buf, ssize_t n,
                                          struct x11_egl_xwayland_auth *info)
{
    int matched = 0;
    int want_auth_value = 0;
    char *p = buf;
    char *end = buf + n;

    while (p < end) {
        char *tok;
        char saved = '\0';

        while (p < end &&
               (*p == '\0' || isspace((unsigned char)*p)))
            p++;
        if (p >= end)
            continue;

        tok = p;
        while (p < end && *p != '\0' && !isspace((unsigned char)*p))
            p++;
        if (p < end) {
            saved = *p;
            *p = '\0';
        }

        x11_egl_append_argv_token(info->argv, sizeof(info->argv), tok);
        if (strstr(tok, "Xwayland") || strstr(tok, "xwayland-kde-wrapper"))
            matched = 1;

        if (want_auth_value) {
            if (info->auth_path[0] == '\0')
                x11_egl_copy_token(info->auth_path, sizeof(info->auth_path),
                                   tok);
            want_auth_value = 0;
        } else if (strcmp(tok, "-auth") == 0) {
            want_auth_value = 1;
        } else if (strncmp(tok, "-auth=", 6) == 0 && tok[6] != '\0') {
            x11_egl_copy_token(info->auth_path, sizeof(info->auth_path),
                               tok + 6);
        }

        if (tok[0] == ':' && info->display[0] == '\0')
            x11_egl_copy_token(info->display, sizeof(info->display), tok);

        if (p < end) {
            *p = saved;
            p++;
        }
    }

    return matched;
}

static void x11_egl_copy_auth_info(struct x11_egl_xwayland_auth *dst,
                                   const struct x11_egl_xwayland_auth *src)
{
    dst->found = src->found;
    dst->pid = src->pid;
    x11_egl_copy_token(dst->argv, sizeof(dst->argv), src->argv);
    x11_egl_copy_token(dst->auth_path, sizeof(dst->auth_path),
                       src->auth_path);
    x11_egl_copy_token(dst->display, sizeof(dst->display), src->display);
}

static int x11_egl_display_is_preflight_candidate(const char *display)
{
    return strcmp(display, ":0") == 0 || strcmp(display, ":1") == 0;
}

static int x11_egl_xwayland_auth_rank(
    const struct x11_egl_xwayland_auth *info)
{
    int readable;

    if (!info->found)
        return -1;
    if (info->auth_path[0] == '\0')
        return 0;

    readable = access(info->auth_path, R_OK) == 0;
    if (readable && x11_egl_display_is_preflight_candidate(info->display))
        return 4;
    if (readable && info->display[0] != '\0')
        return 3;
    if (readable)
        return 2;
    return 1;
}

static void x11_egl_consider_auth_candidate(
    struct x11_egl_xwayland_auth *best, int *best_rank,
    const struct x11_egl_xwayland_auth *cand)
{
    int cand_rank = x11_egl_xwayland_auth_rank(cand);

    if (!best->found || cand_rank > *best_rank) {
        x11_egl_copy_auth_info(best, cand);
        *best_rank = cand_rank;
    }
}

static void discover_xwayland_auth(
    struct x11_egl_xwayland_auth_discovery *discovery)
{
    DIR *dir;
    struct dirent *de;
    int best_rank = -1;
    int display0_rank = -1;
    int display1_rank = -1;

    memset(discovery, 0, sizeof(*discovery));
    dir = opendir("/proc");
    if (!dir) {
        x11_egl_logf("host-x11-egl-smoke: diag xwayland_auth proc_open_failed errno=%d %s\n",
                     errno, strerror(errno));
        return;
    }

    while ((de = readdir(dir)) != NULL) {
        struct x11_egl_xwayland_auth cand;
        char path[320];
        char buf[4096];
        int fd;
        ssize_t n;

        if (!proc_name_all_digits(de->d_name))
            continue;

        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0)
            continue;
        buf[n] = '\0';

        memset(&cand, 0, sizeof(cand));
        cand.pid = (pid_t)atoi(de->d_name);
        if (!x11_egl_parse_xwayland_cmdline(buf, n, &cand))
            continue;
        cand.found = 1;

        x11_egl_consider_auth_candidate(&discovery->best, &best_rank, &cand);
        if (strcmp(cand.display, ":0") == 0)
            x11_egl_consider_auth_candidate(&discovery->display0,
                                            &display0_rank, &cand);
        else if (strcmp(cand.display, ":1") == 0)
            x11_egl_consider_auth_candidate(&discovery->display1,
                                            &display1_rank, &cand);
    }

    closedir(dir);
}

static void log_x11_egl_auth_candidate(const char *label, const char *path)
{
    struct stat st;
    int stat_errno = 0;
    int access_errno = 0;
    int stat_ok = 0;
    int readable = 0;

    if (!path || path[0] == '\0') {
        x11_egl_logf("host-x11-egl-smoke: diag auth_candidate label=%s path=(unset) stat=SKIP readable=SKIP\n",
                     label);
        return;
    }

    if (stat(path, &st) == 0) {
        stat_ok = 1;
    } else {
        stat_errno = errno;
    }
    if (access(path, R_OK) == 0) {
        readable = 1;
    } else {
        access_errno = errno;
    }

    x11_egl_logf("host-x11-egl-smoke: diag auth_candidate label=%s path=%s stat=%s stat_errno=%d %s readable=%s access_errno=%d %s\n",
                 label, path, stat_ok ? "OK" : "FAIL", stat_errno,
                 stat_ok ? "ok" : strerror(stat_errno),
                 readable ? "YES" : "NO", access_errno,
                 readable ? "ok" : strerror(access_errno));
}

static void log_x11_egl_auth_discovery(
    const struct x11_egl_xwayland_auth_discovery *discovery)
{
    const char *home = getenv("HOME");
    char home_xauthority[X11_EGL_AUTH_PATH_MAX];

    x11_egl_logf("host-x11-egl-smoke: diag xwayland_auth label=best pid=%d display=%s auth_path=%s argv=%s\n",
                 discovery->best.found ? (int)discovery->best.pid : -1,
                 discovery->best.display[0] ? discovery->best.display : "(unset)",
                 discovery->best.auth_path[0] ? discovery->best.auth_path : "(unset)",
                 discovery->best.argv[0] ? discovery->best.argv : "(not-found)");
    x11_egl_logf("host-x11-egl-smoke: diag xwayland_auth label=display0 pid=%d display=%s auth_path=%s argv=%s\n",
                 discovery->display0.found ? (int)discovery->display0.pid : -1,
                 discovery->display0.display[0] ? discovery->display0.display : "(unset)",
                 discovery->display0.auth_path[0] ? discovery->display0.auth_path : "(unset)",
                 discovery->display0.argv[0] ? discovery->display0.argv : "(not-found)");
    x11_egl_logf("host-x11-egl-smoke: diag xwayland_auth label=display1 pid=%d display=%s auth_path=%s argv=%s\n",
                 discovery->display1.found ? (int)discovery->display1.pid : -1,
                 discovery->display1.display[0] ? discovery->display1.display : "(unset)",
                 discovery->display1.auth_path[0] ? discovery->display1.auth_path : "(unset)",
                 discovery->display1.argv[0] ? discovery->display1.argv : "(not-found)");
    log_x11_egl_auth_candidate("env_XAUTHORITY", getenv("XAUTHORITY"));
    log_x11_egl_auth_candidate("xwayland_auth", discovery->best.auth_path);
    log_x11_egl_auth_candidate("xwayland_auth_best",
                               discovery->best.auth_path);
    log_x11_egl_auth_candidate("xwayland_auth_display0",
                               discovery->display0.auth_path);
    log_x11_egl_auth_candidate("xwayland_auth_display1",
                               discovery->display1.auth_path);
    if (home && home[0] != '\0') {
        snprintf(home_xauthority, sizeof(home_xauthority), "%s/.Xauthority",
                 home);
        log_x11_egl_auth_candidate("home_Xauthority", home_xauthority);
    } else {
        log_x11_egl_auth_candidate("home_Xauthority", NULL);
    }
}

static int x11_egl_auth_path_readable(const char *path)
{
    return path && path[0] != '\0' && access(path, R_OK) == 0;
}

static int x11_egl_present_trace_preload_available(void)
{
    return access(X11_EGL_PRESENT_TRACE_PRELOAD, R_OK) == 0;
}

static const struct x11_egl_xwayland_auth *x11_egl_auth_info_for_display(
    const struct x11_egl_xwayland_auth_discovery *discovery,
    const char *display)
{
    if (strcmp(display, ":0") == 0 && discovery->display0.found)
        return &discovery->display0;
    if (strcmp(display, ":1") == 0 && discovery->display1.found)
        return &discovery->display1;
    if (discovery->best.found)
        return &discovery->best;
    return NULL;
}

static const char *x11_egl_auth_for_display(
    const struct x11_egl_xwayland_auth_discovery *discovery,
    const char *display)
{
    const struct x11_egl_xwayland_auth *info =
        x11_egl_auth_info_for_display(discovery, display);

    if (!info)
        return NULL;
    if (!x11_egl_auth_path_readable(info->auth_path))
        return NULL;
    if (info->display[0] == '\0') {
        x11_egl_logf("host-x11-egl-smoke: diag xwayland_auth auth_without_display requested_display=%s auth_path=%s action=preserve-env\n",
                     display, info->auth_path);
        return NULL;
    }
    if (strcmp(info->display, display) != 0) {
        x11_egl_logf("host-x11-egl-smoke: diag xwayland_auth display_mismatch requested_display=%s xwayland_display=%s auth_path=%s action=preserve-env\n",
                     display, info->display, info->auth_path);
        return NULL;
    }
    return info->auth_path;
}

static int run_logged_shell(const char *label, const char *cmd)
{
    pid_t pid;
    int status;

    x11_egl_logf("host-x11-egl-smoke: diag %s\n", label);
    pid = fork();
    if (pid < 0) {
        x11_egl_logf("host-x11-egl-smoke: diag %s fork_failed errno=%d %s\n",
                     label, errno, strerror(errno));
        return 127;
    }
    if (pid == 0) {
        int fd = open(X11_EGL_SESSION_LOG,
                      O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0) {
            (void)lseek(fd, 0, SEEK_END);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO)
                close(fd);
        }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        x11_egl_logf("host-x11-egl-smoke: diag %s wait_failed errno=%d %s\n",
                     label, errno, strerror(errno));
        return 127;
    }
    x11_egl_logf("host-x11-egl-smoke: diag %s exit_status=%d\n",
                 label, WIFEXITED(status) ? WEXITSTATUS(status) : 128);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

static int x11_egl_smoke_status_code(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 128;
}

static void x11_egl_session_terminal(const char *probe_mode,
                                     const char *status, int exit_status,
                                     const char *reason)
{
    const char *mode = probe_mode ? probe_mode : "glx-probe";

    if (reason && reason[0]) {
        x11_egl_logf("host-x11-egl-smoke: phase=session_probe status=%s mode=session probe_mode=%s exit_status=%d reason=%s\n",
                     status, mode, exit_status, reason);
        fprintf(stderr,
                "kde-session: x11-egl-session-probe status=%s exit_status=%d probe_mode=%s reason=%s\n",
                status, exit_status, mode, reason);
    } else {
        x11_egl_logf("host-x11-egl-smoke: phase=session_probe status=%s mode=session probe_mode=%s exit_status=%d\n",
                     status, mode, exit_status);
        fprintf(stderr,
                "kde-session: x11-egl-session-probe status=%s exit_status=%d probe_mode=%s\n",
                status, exit_status, mode);
    }
    fflush(stderr);
}

static int wait_host_x11_egl_smoke(pid_t pid, const char *display,
                                   const char *mode, int timeout_ms)
{
    int status;
    int waited_ms = 0;

    for (;;) {
        pid_t got = waitpid(pid, &status, WNOHANG);

        if (got == pid) {
            int exit_status = x11_egl_smoke_status_code(status);

            x11_egl_logf("host-x11-egl-smoke: phase=%s status=%s mode=%s display=%s exit_status=%d\n",
                         mode, exit_status == 0 ? "PASS" : "FAIL", mode,
                         display, exit_status);
            return exit_status;
        }
        if (got < 0) {
            if (errno == EINTR)
                continue;
            x11_egl_logf("host-x11-egl-smoke: phase=%s status=FAIL mode=%s display=%s exit_status=127 reason=wait errno=%d %s\n",
                         mode, mode, display, errno, strerror(errno));
            return 127;
        }
        if (waited_ms >= timeout_ms)
            break;
        usleep(100000);
        waited_ms += 100;
    }

    x11_egl_logf("host-x11-egl-smoke: phase=%s status=TIMEOUT mode=%s display=%s exit_status=124 timeout_ms=%d\n",
                 mode, mode, display, timeout_ms);
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return 124;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

static int run_host_x11_egl_smoke(const char *display, const char *mode,
                                  const char *program, const char *arg,
                                  const char *auth_path,
                                  const char *glx_fps_variant,
                                  const char *glx_fps_oml_queue_depth,
                                  const char *glx_fps_oml_issue_state_sample_interval,
                                  const char *glx_fps_target_ms,
                                  const char *glx_fps_max_frames,
                                  const char *present_fps_variant,
                                  const char *present_fps_queue_depth,
                                  const char *child_ld_preload)
{
    pid_t pid;
    int timeout_ms = X11_EGL_SMOKE_CHILD_TIMEOUT_MS;

    if (strcmp(mode, "glx-fps") == 0 && child_ld_preload &&
        child_ld_preload[0])
        timeout_ms = X11_EGL_SMOKE_TRACE_CHILD_TIMEOUT_MS;

    if (strcmp(mode, "glx-fps") == 0) {
        x11_egl_logf("host-x11-egl-smoke: diag launch mode=%s program=%s display=%s xauthority=%s glx_fps_variant=%s glx_fps_oml_queue_depth=%s glx_fps_oml_issue_state_sample_interval=%s glx_fps_target_ms=%s glx_fps_max_frames=%s process_ld_preload=(unset) child_ld_preload=%s timeout_ms=%d\n",
                     mode, program, display,
                     auth_path && auth_path[0] ? auth_path : "(preserve)",
                     glx_fps_variant && glx_fps_variant[0]
                         ? glx_fps_variant
                         : "(unset)",
                     glx_fps_oml_queue_depth && glx_fps_oml_queue_depth[0]
                         ? glx_fps_oml_queue_depth
                         : "(unset)",
                     glx_fps_oml_issue_state_sample_interval &&
                             glx_fps_oml_issue_state_sample_interval[0]
                         ? glx_fps_oml_issue_state_sample_interval
                         : "(unset)",
                     glx_fps_target_ms && glx_fps_target_ms[0]
                         ? glx_fps_target_ms
                         : "(unset)",
                     glx_fps_max_frames && glx_fps_max_frames[0]
                         ? glx_fps_max_frames
                         : "(unset)",
                     child_ld_preload && child_ld_preload[0]
                         ? child_ld_preload
                         : "(unset)",
                     timeout_ms);
    } else if (present_fps_variant && present_fps_variant[0]) {
        x11_egl_logf("host-x11-egl-smoke: diag launch mode=%s program=%s display=%s xauthority=%s present_fps_variant=%s present_fps_queue_depth=%s\n",
                     mode, program, display,
                     auth_path && auth_path[0] ? auth_path : "(preserve)",
                     present_fps_variant,
                     present_fps_queue_depth && present_fps_queue_depth[0]
                         ? present_fps_queue_depth
                         : "(unset)");
    } else {
        x11_egl_logf("host-x11-egl-smoke: diag launch mode=%s program=%s display=%s xauthority=%s\n",
                     mode, program, display,
                     auth_path && auth_path[0] ? auth_path : "(preserve)");
    }
    pid = fork();
    if (pid < 0) {
        x11_egl_logf("host-x11-egl-smoke: phase=%s status=FAIL mode=%s display=%s exit_status=127 reason=fork errno=%d %s\n",
                     mode, mode, display, errno, strerror(errno));
        return 127;
    }
    if (pid == 0) {
        int fd = open(X11_EGL_SESSION_LOG,
                      O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        setpgid(0, 0);
        if (fd >= 0) {
            (void)lseek(fd, 0, SEEK_END);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO)
                close(fd);
        }
        setenv("DISPLAY", display, 1);
        if (auth_path && auth_path[0])
            setenv("XAUTHORITY", auth_path, 1);
        setenv("HOST_X11_EGL_SMOKE_MODE", mode, 1);
        setenv("HOST_X11_EGL_SMOKE_LOG", X11_EGL_SESSION_LOG, 1);
        setenv("HOST_X11_PRESENT_TRACE_LOG", X11_EGL_PRESENT_TRACE_LOG, 1);
        if (glx_fps_variant && glx_fps_variant[0])
            setenv("HOST_X11_EGL_GLX_FPS_VARIANT", glx_fps_variant, 1);
        else
            unsetenv("HOST_X11_EGL_GLX_FPS_VARIANT");
        if (glx_fps_oml_queue_depth && glx_fps_oml_queue_depth[0])
            setenv("HOST_X11_EGL_GLX_FPS_OML_QUEUE_DEPTH",
                   glx_fps_oml_queue_depth, 1);
        else
            unsetenv("HOST_X11_EGL_GLX_FPS_OML_QUEUE_DEPTH");
        if (glx_fps_oml_issue_state_sample_interval &&
            glx_fps_oml_issue_state_sample_interval[0])
            setenv("HOST_X11_EGL_GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL",
                   glx_fps_oml_issue_state_sample_interval, 1);
        else
            unsetenv("HOST_X11_EGL_GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL");
        if (glx_fps_target_ms && glx_fps_target_ms[0])
            setenv("HOST_X11_EGL_GLX_FPS_TARGET_MS",
                   glx_fps_target_ms, 1);
        else
            unsetenv("HOST_X11_EGL_GLX_FPS_TARGET_MS");
        if (glx_fps_max_frames && glx_fps_max_frames[0])
            setenv("HOST_X11_EGL_GLX_FPS_MAX_FRAMES",
                   glx_fps_max_frames, 1);
        else
            unsetenv("HOST_X11_EGL_GLX_FPS_MAX_FRAMES");
        if (present_fps_variant && present_fps_variant[0])
            setenv("HOST_X11_PRESENT_FPS_VARIANT", present_fps_variant, 1);
        else
            unsetenv("HOST_X11_PRESENT_FPS_VARIANT");
        if (present_fps_queue_depth && present_fps_queue_depth[0])
            setenv("HOST_X11_PRESENT_FPS_QUEUE_DEPTH",
                   present_fps_queue_depth, 1);
        else
            unsetenv("HOST_X11_PRESENT_FPS_QUEUE_DEPTH");
        unsetenv("WAYLAND_DISPLAY");
        unsetenv("LD_PRELOAD");
        if (child_ld_preload && child_ld_preload[0])
            setenv(X11_EGL_CHILD_LD_PRELOAD_ENV, child_ld_preload, 1);
        else
            unsetenv(X11_EGL_CHILD_LD_PRELOAD_ENV);
        execl(program, base_name(program), arg, (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid);
    return wait_host_x11_egl_smoke(pid, display, mode, timeout_ms);
}

static void run_x11_egl_session_probe(const char *probe_mode)
{
    static const char *displays[] = { ":0", ":1" };
    const char *selected = NULL;
    const char *run_mode = "glx-probe";
    const char *run_arg = "--glx-probe-only";
    const char *run_program = "/bin/host-x11-egl-smoke";
    char glx_fps_variant[64];
    char glx_fps_variant_raw[64];
    char glx_fps_oml_queue_depth[16];
    char glx_fps_oml_queue_depth_raw[16];
    char glx_fps_oml_issue_state_sample_interval[16];
    char glx_fps_oml_issue_state_sample_interval_raw[16];
    char glx_fps_target_ms[16];
    char glx_fps_target_ms_raw[16];
    char glx_fps_max_frames[16];
    char glx_fps_max_frames_raw[16];
    char present_fps_variant[64];
    char present_fps_variant_raw[64];
    char present_fps_queue_depth[16];
    char present_fps_queue_depth_raw[16];
    int glx_fps_variant_status = 0;
    int glx_fps_variant_env_set = 0;
    int glx_fps_oml_queue_depth_status = 0;
    int glx_fps_oml_queue_depth_env_set = 0;
    int glx_fps_oml_issue_state_sample_interval_status = 0;
    int glx_fps_oml_issue_state_sample_interval_env_set = 0;
    int glx_fps_target_ms_status = 0;
    int glx_fps_target_ms_env_set = 0;
    int glx_fps_max_frames_status = 0;
    int glx_fps_max_frames_env_set = 0;
    int present_fps_variant_status = 0;
    int present_fps_variant_env_set = 0;
    int present_fps_queue_depth_status = 0;
    int present_fps_queue_depth_env_set = 0;
    int glx_present_trace_requested = 0;
    int glx_present_trace_available = 0;
    struct x11_egl_xwayland_auth_discovery xwayland_auth;
    int glx_rc;
    FILE *fp;

    glx_fps_variant[0] = '\0';
    glx_fps_variant_raw[0] = '\0';
    glx_fps_oml_queue_depth[0] = '\0';
    glx_fps_oml_queue_depth_raw[0] = '\0';
    glx_fps_oml_issue_state_sample_interval[0] = '\0';
    glx_fps_oml_issue_state_sample_interval_raw[0] = '\0';
    glx_fps_target_ms[0] = '\0';
    glx_fps_target_ms_raw[0] = '\0';
    glx_fps_max_frames[0] = '\0';
    glx_fps_max_frames_raw[0] = '\0';
    present_fps_variant[0] = '\0';
    present_fps_variant_raw[0] = '\0';
    present_fps_queue_depth[0] = '\0';
    present_fps_queue_depth_raw[0] = '\0';
    if (probe_mode && strcmp(probe_mode, "glx-fps") == 0) {
        run_mode = "glx-fps";
        run_arg = "--glx-fps";
        glx_fps_variant_status =
            cmdline_get_value_status("kde_x11_egl_glx_fps_variant",
                                     glx_fps_variant,
                                     sizeof(glx_fps_variant));
        if (glx_fps_variant_status > 0) {
            x11_egl_copy_token(glx_fps_variant_raw,
                               sizeof(glx_fps_variant_raw),
                               glx_fps_variant);
            if (valid_x11_egl_glx_fps_variant(glx_fps_variant)) {
                glx_fps_variant_env_set = 1;
            } else {
                x11_egl_copy_token(glx_fps_variant,
                                   sizeof(glx_fps_variant), "");
                glx_fps_variant_status = -2;
            }
        }
        glx_fps_oml_queue_depth_status =
            cmdline_get_value_status("kde_x11_egl_glx_fps_oml_queue_depth",
                                     glx_fps_oml_queue_depth,
                                     sizeof(glx_fps_oml_queue_depth));
        if (glx_fps_oml_queue_depth_status > 0) {
            x11_egl_copy_token(glx_fps_oml_queue_depth_raw,
                               sizeof(glx_fps_oml_queue_depth_raw),
                               glx_fps_oml_queue_depth);
            if (valid_decimal_range(glx_fps_oml_queue_depth, 1, 8)) {
                glx_fps_oml_queue_depth_env_set = 1;
            } else {
                x11_egl_copy_token(glx_fps_oml_queue_depth,
                                   sizeof(glx_fps_oml_queue_depth), "");
                glx_fps_oml_queue_depth_status = -2;
            }
        }
        glx_fps_oml_issue_state_sample_interval_status =
            cmdline_get_value_status(
                "kde_x11_egl_glx_fps_oml_issue_state_sample_interval",
                glx_fps_oml_issue_state_sample_interval,
                sizeof(glx_fps_oml_issue_state_sample_interval));
        if (glx_fps_oml_issue_state_sample_interval_status > 0) {
            x11_egl_copy_token(
                glx_fps_oml_issue_state_sample_interval_raw,
                sizeof(glx_fps_oml_issue_state_sample_interval_raw),
                glx_fps_oml_issue_state_sample_interval);
            if (valid_decimal_range(
                    glx_fps_oml_issue_state_sample_interval, 1, 64)) {
                glx_fps_oml_issue_state_sample_interval_env_set = 1;
            } else {
                x11_egl_copy_token(
                    glx_fps_oml_issue_state_sample_interval,
                    sizeof(glx_fps_oml_issue_state_sample_interval), "");
                glx_fps_oml_issue_state_sample_interval_status = -2;
            }
        }
        glx_fps_target_ms_status =
            cmdline_get_value_status("kde_x11_egl_glx_fps_target_ms",
                                     glx_fps_target_ms,
                                     sizeof(glx_fps_target_ms));
        if (glx_fps_target_ms_status > 0) {
            x11_egl_copy_token(glx_fps_target_ms_raw,
                               sizeof(glx_fps_target_ms_raw),
                               glx_fps_target_ms);
            if (valid_decimal_range(glx_fps_target_ms, 100, 60000)) {
                glx_fps_target_ms_env_set = 1;
            } else {
                x11_egl_copy_token(glx_fps_target_ms,
                                   sizeof(glx_fps_target_ms), "");
                glx_fps_target_ms_status = -2;
            }
        }
        glx_fps_max_frames_status =
            cmdline_get_value_status("kde_x11_egl_glx_fps_max_frames",
                                     glx_fps_max_frames,
                                     sizeof(glx_fps_max_frames));
        if (glx_fps_max_frames_status > 0) {
            x11_egl_copy_token(glx_fps_max_frames_raw,
                               sizeof(glx_fps_max_frames_raw),
                               glx_fps_max_frames);
            if (valid_decimal_range(glx_fps_max_frames, 1, 10000)) {
                glx_fps_max_frames_env_set = 1;
            } else {
                x11_egl_copy_token(glx_fps_max_frames,
                                   sizeof(glx_fps_max_frames), "");
                glx_fps_max_frames_status = -2;
            }
        }
        glx_present_trace_requested =
            cmdline_has_flag("kde_x11_egl_glx_present_trace=1");
        glx_present_trace_available =
            glx_present_trace_requested &&
            x11_egl_present_trace_preload_available();
    } else if (probe_mode && strcmp(probe_mode, "present-fps") == 0) {
        run_mode = "present-fps";
        run_arg = "--present-fps";
        run_program = "/bin/host-x11-dri3-present-smoke";
        present_fps_variant_status =
            cmdline_get_value_status("kde_x11_present_fps_variant",
                                     present_fps_variant,
                                     sizeof(present_fps_variant));
        if (present_fps_variant_status > 0) {
            x11_egl_copy_token(present_fps_variant_raw,
                               sizeof(present_fps_variant_raw),
                               present_fps_variant);
            if (valid_x11_present_fps_variant(present_fps_variant)) {
                present_fps_variant_env_set = 1;
            } else {
                x11_egl_copy_token(present_fps_variant,
                                   sizeof(present_fps_variant), "");
                present_fps_variant_status = -2;
            }
        }
        present_fps_queue_depth_status =
            cmdline_get_value_status("kde_x11_present_fps_queue_depth",
                                     present_fps_queue_depth,
                                     sizeof(present_fps_queue_depth));
        if (present_fps_queue_depth_status > 0) {
            x11_egl_copy_token(present_fps_queue_depth_raw,
                               sizeof(present_fps_queue_depth_raw),
                               present_fps_queue_depth);
            if (valid_decimal_range(present_fps_queue_depth, 1, 8)) {
                present_fps_queue_depth_env_set = 1;
            } else {
                x11_egl_copy_token(present_fps_queue_depth,
                                   sizeof(present_fps_queue_depth), "");
                present_fps_queue_depth_status = -2;
            }
        }
    }

    fp = fopen(X11_EGL_SESSION_LOG, "w");
    if (fp) {
        fprintf(fp, "host-x11-egl-smoke: phase=prelaunch_prompt_sync status=PASS mode=session probe_mode=%s\n",
                run_mode);
        fprintf(fp, "host-x11-egl-smoke: phase=x11_preflight status=BEGIN mode=session probe_mode=%s\n",
                run_mode);
        fprintf(fp, "host-x11-egl-smoke: diag preflight_env\n");
        fprintf(fp, "DISPLAY=%s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
        fprintf(fp, "XAUTHORITY=%s\n", getenv("XAUTHORITY") ? getenv("XAUTHORITY") : "(unset)");
        fprintf(fp, "HOME=%s\n", getenv("HOME") ? getenv("HOME") : "(unset)");
        fprintf(fp, "XDG_RUNTIME_DIR=%s\n", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
        fprintf(fp, "probe_XDG_RUNTIME_DIR=%s\n", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
        fprintf(fp, "probe_LD_PRELOAD=(unset)\n");
        if (strcmp(run_mode, "glx-fps") == 0) {
            fprintf(fp, "probe_glx_present_trace=%d\n",
                    glx_present_trace_requested);
            fprintf(fp, "probe_glx_present_trace_preload=%s\n",
                    X11_EGL_PRESENT_TRACE_PRELOAD);
            fprintf(fp, "probe_glx_present_trace_child_ld_preload=%s\n",
                    glx_present_trace_available
                        ? X11_EGL_PRESENT_TRACE_PRELOAD
                        : "(unset)");
            fprintf(fp, "probe_glx_present_trace_available=%d\n",
                    glx_present_trace_available);
            fprintf(fp, "probe_glx_present_trace_invalid=%d\n",
                    glx_present_trace_requested &&
                    !glx_present_trace_available);
            fprintf(fp, "probe_glx_fps_variant=%s\n",
                    glx_fps_variant_env_set ? glx_fps_variant :
                    (glx_fps_variant_status < 0 ? "invalid" : "baseline"));
            fprintf(fp, "probe_glx_fps_variant_env_set=%d\n",
                    glx_fps_variant_env_set);
            fprintf(fp, "probe_glx_fps_variant_invalid=%d\n",
                    glx_fps_variant_status < 0);
            if (glx_fps_variant_status == -2)
                fprintf(fp, "probe_glx_fps_variant_requested=%s\n",
                        glx_fps_variant_raw[0]
                            ? glx_fps_variant_raw
                            : "(empty)");
            fprintf(fp, "probe_glx_fps_oml_queue_depth=%s\n",
                    glx_fps_oml_queue_depth_env_set
                        ? glx_fps_oml_queue_depth
                        : (glx_fps_oml_queue_depth_status < 0
                               ? "invalid"
                               : "(unset)"));
            fprintf(fp, "probe_glx_fps_oml_queue_depth_env_set=%d\n",
                    glx_fps_oml_queue_depth_env_set);
            fprintf(fp, "probe_glx_fps_oml_queue_depth_invalid=%d\n",
                    glx_fps_oml_queue_depth_status < 0);
            if (glx_fps_oml_queue_depth_status == -2)
                fprintf(fp,
                        "probe_glx_fps_oml_queue_depth_requested=%s\n",
                        glx_fps_oml_queue_depth_raw[0]
                            ? glx_fps_oml_queue_depth_raw
                            : "(empty)");
            fprintf(fp,
                    "probe_glx_fps_oml_issue_state_sample_interval=%s\n",
                    glx_fps_oml_issue_state_sample_interval_env_set
                        ? glx_fps_oml_issue_state_sample_interval
                        : (glx_fps_oml_issue_state_sample_interval_status < 0
                               ? "invalid"
                               : "(unset)"));
            fprintf(fp,
                    "probe_glx_fps_oml_issue_state_sample_interval_env_set=%d\n",
                    glx_fps_oml_issue_state_sample_interval_env_set);
            fprintf(fp,
                    "probe_glx_fps_oml_issue_state_sample_interval_invalid=%d\n",
                    glx_fps_oml_issue_state_sample_interval_status < 0);
            if (glx_fps_oml_issue_state_sample_interval_status == -2)
                fprintf(fp,
                        "probe_glx_fps_oml_issue_state_sample_interval_requested=%s\n",
                        glx_fps_oml_issue_state_sample_interval_raw[0]
                            ? glx_fps_oml_issue_state_sample_interval_raw
                            : "(empty)");
            fprintf(fp, "probe_glx_fps_target_ms=%s\n",
                    glx_fps_target_ms_env_set
                        ? glx_fps_target_ms
                        : (glx_fps_target_ms_status < 0
                               ? "invalid"
                               : "(unset)"));
            fprintf(fp, "probe_glx_fps_target_ms_env_set=%d\n",
                    glx_fps_target_ms_env_set);
            fprintf(fp, "probe_glx_fps_target_ms_invalid=%d\n",
                    glx_fps_target_ms_status < 0);
            if (glx_fps_target_ms_status == -2)
                fprintf(fp, "probe_glx_fps_target_ms_requested=%s\n",
                        glx_fps_target_ms_raw[0]
                            ? glx_fps_target_ms_raw
                            : "(empty)");
            fprintf(fp, "probe_glx_fps_max_frames=%s\n",
                    glx_fps_max_frames_env_set
                        ? glx_fps_max_frames
                        : (glx_fps_max_frames_status < 0
                               ? "invalid"
                               : "(unset)"));
            fprintf(fp, "probe_glx_fps_max_frames_env_set=%d\n",
                    glx_fps_max_frames_env_set);
            fprintf(fp, "probe_glx_fps_max_frames_invalid=%d\n",
                    glx_fps_max_frames_status < 0);
            if (glx_fps_max_frames_status == -2)
                fprintf(fp, "probe_glx_fps_max_frames_requested=%s\n",
                        glx_fps_max_frames_raw[0]
                            ? glx_fps_max_frames_raw
                            : "(empty)");
        }
        if (strcmp(run_mode, "present-fps") == 0) {
            fprintf(fp, "probe_present_fps_variant=%s\n",
                    present_fps_variant_env_set ? present_fps_variant :
                    (present_fps_variant_status < 0 ? "invalid" : "baseline"));
            fprintf(fp, "probe_present_fps_variant_env_set=%d\n",
                    present_fps_variant_env_set);
            fprintf(fp, "probe_present_fps_variant_invalid=%d\n",
                    present_fps_variant_status < 0);
            if (present_fps_variant_status == -2)
                fprintf(fp, "probe_present_fps_variant_requested=%s\n",
                        present_fps_variant_raw[0]
                            ? present_fps_variant_raw
                            : "(empty)");
            fprintf(fp, "probe_present_fps_queue_depth=%s\n",
                    present_fps_queue_depth_env_set
                        ? present_fps_queue_depth
                        : (present_fps_queue_depth_status < 0
                               ? "invalid"
                               : "(unset)"));
            fprintf(fp, "probe_present_fps_queue_depth_env_set=%d\n",
                    present_fps_queue_depth_env_set);
            fprintf(fp, "probe_present_fps_queue_depth_invalid=%d\n",
                    present_fps_queue_depth_status < 0);
            if (present_fps_queue_depth_status == -2)
                fprintf(fp, "probe_present_fps_queue_depth_requested=%s\n",
                        present_fps_queue_depth_raw[0]
                            ? present_fps_queue_depth_raw
                            : "(empty)");
        }
        fflush(fp);
        fsync(fileno(fp));
        fclose(fp);
    }
    fp = fopen(X11_EGL_PRESENT_TRACE_LOG, "w");
    if (fp)
        fclose(fp);
    if (strcmp(run_mode, "glx-fps") == 0) {
        if (glx_fps_variant_env_set) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_variant source=cmdline selected=%s env=HOST_X11_EGL_GLX_FPS_VARIANT\n",
                         glx_fps_variant);
        } else if (glx_fps_variant_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_variant status=FAIL source=cmdline requested=%s env_set=0 reason=invalid-value\n",
                         glx_fps_variant_raw[0]
                             ? glx_fps_variant_raw
                             : "(empty)");
        } else if (glx_fps_variant_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_variant status=FAIL source=cmdline env_set=0 reason=value-too-long\n");
        } else {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_variant source=cmdline selected=baseline env_set=0 reason=unset\n");
        }
        if (glx_fps_oml_queue_depth_env_set) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_queue_depth source=cmdline selected=%s env=HOST_X11_EGL_GLX_FPS_OML_QUEUE_DEPTH\n",
                         glx_fps_oml_queue_depth);
        } else if (glx_fps_oml_queue_depth_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_queue_depth status=FAIL source=cmdline requested=%s env_set=0 reason=invalid-value min=1 max=8\n",
                         glx_fps_oml_queue_depth_raw[0]
                             ? glx_fps_oml_queue_depth_raw
                             : "(empty)");
        } else if (glx_fps_oml_queue_depth_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_queue_depth status=FAIL source=cmdline env_set=0 reason=value-too-long min=1 max=8\n");
        } else {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_queue_depth source=cmdline selected=default env_set=0 reason=unset\n");
        }
        if (glx_fps_oml_issue_state_sample_interval_env_set) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_issue_state_sample_interval source=cmdline selected=%s env=HOST_X11_EGL_GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL\n",
                         glx_fps_oml_issue_state_sample_interval);
        } else if (glx_fps_oml_issue_state_sample_interval_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_issue_state_sample_interval status=FAIL source=cmdline requested=%s env_set=0 reason=invalid-value min=1 max=64\n",
                         glx_fps_oml_issue_state_sample_interval_raw[0]
                             ? glx_fps_oml_issue_state_sample_interval_raw
                             : "(empty)");
        } else if (glx_fps_oml_issue_state_sample_interval_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_issue_state_sample_interval status=FAIL source=cmdline env_set=0 reason=value-too-long min=1 max=64\n");
        } else {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_oml_issue_state_sample_interval source=cmdline selected=default env_set=0 reason=unset\n");
        }
        if (glx_fps_target_ms_env_set) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_target_ms source=cmdline selected=%s env=HOST_X11_EGL_GLX_FPS_TARGET_MS\n",
                         glx_fps_target_ms);
        } else if (glx_fps_target_ms_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_target_ms status=FAIL source=cmdline requested=%s env_set=0 reason=invalid-value min=100 max=60000\n",
                         glx_fps_target_ms_raw[0]
                             ? glx_fps_target_ms_raw
                             : "(empty)");
        } else if (glx_fps_target_ms_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_target_ms status=FAIL source=cmdline env_set=0 reason=value-too-long min=100 max=60000\n");
        } else {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_target_ms source=cmdline selected=default env_set=0 reason=unset\n");
        }
        if (glx_fps_max_frames_env_set) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_max_frames source=cmdline selected=%s env=HOST_X11_EGL_GLX_FPS_MAX_FRAMES\n",
                         glx_fps_max_frames);
        } else if (glx_fps_max_frames_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_max_frames status=FAIL source=cmdline requested=%s env_set=0 reason=invalid-value min=1 max=10000\n",
                         glx_fps_max_frames_raw[0]
                             ? glx_fps_max_frames_raw
                             : "(empty)");
        } else if (glx_fps_max_frames_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_max_frames status=FAIL source=cmdline env_set=0 reason=value-too-long min=1 max=10000\n");
        } else {
            x11_egl_logf("host-x11-egl-smoke: diag glx_fps_max_frames source=cmdline selected=default env_set=0 reason=unset\n");
        }
        if (glx_fps_variant_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_variant status=FAIL mode=session reason=invalid-value requested=%s\n",
                         glx_fps_variant_raw[0]
                             ? glx_fps_variant_raw
                             : "(empty)");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-variant");
            sync();
            return;
        }
        if (glx_fps_variant_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_variant status=FAIL mode=session reason=value-too-long\n");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-variant");
            sync();
            return;
        }
        if (glx_fps_oml_queue_depth_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_variant status=FAIL mode=session reason=invalid-oml-queue-depth requested=%s min=1 max=8\n",
                         glx_fps_oml_queue_depth_raw[0]
                             ? glx_fps_oml_queue_depth_raw
                             : "(empty)");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-oml-queue-depth");
            sync();
            return;
        }
        if (glx_fps_oml_queue_depth_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_variant status=FAIL mode=session reason=oml-queue-depth-value-too-long min=1 max=8\n");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-oml-queue-depth");
            sync();
            return;
        }
        if (glx_fps_oml_issue_state_sample_interval_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_variant status=FAIL mode=session reason=invalid-oml-issue-state-sample-interval requested=%s min=1 max=64\n",
                         glx_fps_oml_issue_state_sample_interval_raw[0]
                             ? glx_fps_oml_issue_state_sample_interval_raw
                             : "(empty)");
            x11_egl_session_terminal(
                run_mode, "FAIL", 2,
                "invalid-glx-fps-oml-issue-state-sample-interval");
            sync();
            return;
        }
        if (glx_fps_oml_issue_state_sample_interval_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_variant status=FAIL mode=session reason=oml-issue-state-sample-interval-value-too-long min=1 max=64\n");
            x11_egl_session_terminal(
                run_mode, "FAIL", 2,
                "invalid-glx-fps-oml-issue-state-sample-interval");
            sync();
            return;
        }
        if (glx_fps_target_ms_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_config status=FAIL mode=session reason=invalid-target-ms requested=%s min=100 max=60000\n",
                         glx_fps_target_ms_raw[0]
                             ? glx_fps_target_ms_raw
                             : "(empty)");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-target-ms");
            sync();
            return;
        }
        if (glx_fps_target_ms_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_config status=FAIL mode=session reason=target-ms-value-too-long min=100 max=60000\n");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-target-ms");
            sync();
            return;
        }
        if (glx_fps_max_frames_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_config status=FAIL mode=session reason=invalid-max-frames requested=%s min=1 max=10000\n",
                         glx_fps_max_frames_raw[0]
                             ? glx_fps_max_frames_raw
                             : "(empty)");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-max-frames");
            sync();
            return;
        }
        if (glx_fps_max_frames_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_fps_config status=FAIL mode=session reason=max-frames-value-too-long min=1 max=10000\n");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-glx-fps-max-frames");
            sync();
            return;
        }
        if (glx_present_trace_requested && !glx_present_trace_available) {
            x11_egl_logf("host-x11-egl-smoke: phase=glx_present_trace status=FAIL mode=session reason=preload-unavailable path=%s\n",
                         X11_EGL_PRESENT_TRACE_PRELOAD);
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "glx-present-trace-preload-unavailable");
            sync();
            return;
        }
    }
    if (strcmp(run_mode, "present-fps") == 0) {
        if (present_fps_variant_env_set) {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_variant source=cmdline selected=%s env=HOST_X11_PRESENT_FPS_VARIANT\n",
                         present_fps_variant);
        } else if (present_fps_variant_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_variant status=FAIL source=cmdline requested=%s env_set=0 reason=invalid-value\n",
                         present_fps_variant_raw[0]
                             ? present_fps_variant_raw
                             : "(empty)");
        } else if (present_fps_variant_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_variant status=FAIL source=cmdline env_set=0 reason=value-too-long\n");
        } else {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_variant source=cmdline selected=baseline env_set=0 reason=unset\n");
        }
        if (present_fps_queue_depth_env_set) {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_queue_depth source=cmdline selected=%s env=HOST_X11_PRESENT_FPS_QUEUE_DEPTH\n",
                         present_fps_queue_depth);
        } else if (present_fps_queue_depth_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_queue_depth status=FAIL source=cmdline requested=%s env_set=0 reason=invalid-value min=1 max=8\n",
                         present_fps_queue_depth_raw[0]
                             ? present_fps_queue_depth_raw
                             : "(empty)");
        } else if (present_fps_queue_depth_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_queue_depth status=FAIL source=cmdline env_set=0 reason=value-too-long min=1 max=8\n");
        } else {
            x11_egl_logf("host-x11-egl-smoke: diag present_fps_queue_depth source=cmdline selected=default env_set=0 reason=unset\n");
        }
        if (present_fps_variant_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: phase=present_fps_variant status=FAIL mode=session reason=invalid-value requested=%s\n",
                         present_fps_variant_raw[0]
                             ? present_fps_variant_raw
                             : "(empty)");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-present-fps-variant");
            sync();
            return;
        }
        if (present_fps_variant_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: phase=present_fps_variant status=FAIL mode=session reason=value-too-long\n");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-present-fps-variant");
            sync();
            return;
        }
        if (present_fps_queue_depth_status == -2) {
            x11_egl_logf("host-x11-egl-smoke: phase=present_fps_variant status=FAIL mode=session reason=invalid-queue-depth requested=%s min=1 max=8\n",
                         present_fps_queue_depth_raw[0]
                             ? present_fps_queue_depth_raw
                             : "(empty)");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-present-fps-queue-depth");
            sync();
            return;
        }
        if (present_fps_queue_depth_status < 0) {
            x11_egl_logf("host-x11-egl-smoke: phase=present_fps_variant status=FAIL mode=session reason=queue-depth-value-too-long min=1 max=8\n");
            x11_egl_session_terminal(run_mode, "FAIL", 2,
                                     "invalid-present-fps-queue-depth");
            sync();
            return;
        }
    }

    run_logged_shell("preflight_x11_unix",
                     "ls -ld /tmp/.X11-unix /tmp/.X11-unix/* || true");
    run_logged_shell("preflight_ps", "ps || true");
    run_logged_shell("preflight_kde_process_probe",
                     "/bin/kde-process-probe || true");
    discover_xwayland_auth(&xwayland_auth);
    log_x11_egl_auth_discovery(&xwayland_auth);
    x11_egl_logf("host-x11-egl-smoke: phase=x11_preflight_diag status=PASS mode=session probe_mode=%s\n",
                 run_mode);

    for (size_t i = 0; i < sizeof(displays) / sizeof(displays[0]); i++) {
        const char *auth_path;
        int rc;

        x11_egl_logf("host-x11-egl-smoke: phase=x11_preflight_candidate status=BEGIN display=%s\n",
                     displays[i]);
        auth_path = x11_egl_auth_for_display(&xwayland_auth, displays[i]);
        rc = run_host_x11_egl_smoke(displays[i], "x11-connect",
                                    "/bin/host-x11-egl-smoke",
                                    "--x11-connect-only",
                                    auth_path, NULL, NULL, NULL, NULL, NULL,
                                    NULL, NULL, NULL);
        if (rc != 0 && !auth_path) {
            x11_egl_logf("host-x11-egl-smoke: diag xwayland_auth rediscover_after_candidate_fail display=%s exit_status=%d\n",
                         displays[i], rc);
            discover_xwayland_auth(&xwayland_auth);
            log_x11_egl_auth_discovery(&xwayland_auth);
            auth_path = x11_egl_auth_for_display(&xwayland_auth,
                                                 displays[i]);
            if (auth_path) {
                x11_egl_logf("host-x11-egl-smoke: phase=x11_preflight_candidate_retry status=BEGIN display=%s reason=auth-discovered\n",
                             displays[i]);
                rc = run_host_x11_egl_smoke(displays[i], "x11-connect",
                                            "/bin/host-x11-egl-smoke",
                                            "--x11-connect-only",
                                            auth_path, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL);
                x11_egl_logf("host-x11-egl-smoke: phase=x11_preflight_candidate_retry status=%s display=%s exit_status=%d\n",
                             rc == 0 ? "PASS" : "FAIL", displays[i], rc);
            }
        }
        x11_egl_logf("host-x11-egl-smoke: phase=x11_preflight_candidate status=%s display=%s exit_status=%d\n",
                     rc == 0 ? "PASS" : "FAIL", displays[i], rc);
        if (rc == 0) {
            selected = displays[i];
            break;
        }
    }

    if (!selected) {
        x11_egl_logf("host-x11-egl-smoke: phase=x11_preflight status=FAIL reason=no-display-candidate label=x11-egl-XOpenDisplay-preflight-failed displays=:0,:1\n");
        x11_egl_session_terminal(run_mode, "FAIL", 1, "no-display-candidate");
        return;
    }

    x11_egl_logf("host-x11-egl-smoke: phase=x11_preflight status=PASS selected_display=%s\n",
                 selected);
    x11_egl_logf("host-x11-egl-smoke: phase=%s status=BEGIN mode=session display=%s\n",
                 run_mode, selected);
    glx_rc = run_host_x11_egl_smoke(selected, run_mode, run_program, run_arg,
                                    x11_egl_auth_for_display(&xwayland_auth,
                                                             selected),
                                    glx_fps_variant_env_set
                                        ? glx_fps_variant
                                        : NULL,
                                    glx_fps_oml_queue_depth_env_set
                                        ? glx_fps_oml_queue_depth
                                        : NULL,
                                    glx_fps_oml_issue_state_sample_interval_env_set
                                        ? glx_fps_oml_issue_state_sample_interval
                                        : NULL,
                                    glx_fps_target_ms_env_set
                                        ? glx_fps_target_ms
                                        : NULL,
                                    glx_fps_max_frames_env_set
                                        ? glx_fps_max_frames
                                        : NULL,
                                    present_fps_variant_env_set
                                        ? present_fps_variant
                                        : NULL,
                                    present_fps_queue_depth_env_set
                                        ? present_fps_queue_depth
                                        : NULL,
                                    glx_present_trace_available
                                        ? X11_EGL_PRESENT_TRACE_PRELOAD
                                        : NULL);
    x11_egl_session_terminal(run_mode, glx_rc == 0 ? "PASS" : "FAIL",
                             glx_rc, NULL);
    sync();
}

static int x11_egl_session_probe_mode(char *mode, size_t mode_size)
{
    char value[64];
    int rc;

    if (mode_size == 0)
        return 0;
    mode[0] = '\0';

    rc = cmdline_get_value_status("kde_x11_egl_session_probe", value,
                                  sizeof(value));
    if (rc <= 0)
        return rc;
    if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0)
        return 0;
    if (strcmp(value, "1") == 0 || strcmp(value, "glx-probe") == 0) {
        snprintf(mode, mode_size, "%s", "glx-probe");
        return 1;
    }
    if (strcmp(value, "glx-fps") == 0) {
        snprintf(mode, mode_size, "%s", "glx-fps");
        return 1;
    }
    if (strcmp(value, "present-fps") == 0) {
        snprintf(mode, mode_size, "%s", "present-fps");
        return 1;
    }

    x11_egl_session_terminal("invalid", "FAIL", 2, "invalid-mode");
    return -1;
}

static pid_t maybe_spawn_x11_egl_session_probe(void)
{
    char probe_mode[64];
    pid_t pid;
    int mode_rc;

    mode_rc = x11_egl_session_probe_mode(probe_mode, sizeof(probe_mode));
    if (mode_rc <= 0)
        return -1;
    snprintf(x11_egl_active_session_probe_mode,
             sizeof(x11_egl_active_session_probe_mode), "%s", probe_mode);
    if (!is_executable("/bin/host-x11-egl-smoke")) {
        x11_egl_session_terminal(probe_mode, "FAIL", 127,
                                 strcmp(probe_mode, "present-fps") == 0
                                     ? "missing-preflight-binary"
                                     : "missing-binary");
        return -1;
    }
    if (strcmp(probe_mode, "present-fps") == 0 &&
        !is_executable("/bin/host-x11-dri3-present-smoke")) {
        x11_egl_session_terminal(probe_mode, "FAIL", 127,
                                 "missing-binary");
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        x11_egl_logf("host-x11-egl-smoke: phase=session_probe status=FAIL reason=fork errno=%d %s\n",
                     errno, strerror(errno));
        x11_egl_session_terminal(probe_mode, "FAIL", 127, "fork");
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        sleep(3);
        run_x11_egl_session_probe(probe_mode);
        _exit(0);
    }
    setpgid(pid, pid);
    return pid;
}

static void reap_x11_egl_session_probe(pid_t pid)
{
    int status;

    if (pid <= 0)
        return;
    for (int waited_ms = 0; waited_ms < X11_EGL_SESSION_PROBE_TIMEOUT_MS;
         waited_ms += 100) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return;
        usleep(100000);
    }
    x11_egl_session_terminal(x11_egl_active_session_probe_mode[0]
                                 ? x11_egl_active_session_probe_mode
                                 : "unknown",
                             "TIMEOUT", 124, "session-watchdog");
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
}

static int wait_for_wayland_roundtrip(pid_t kwin_pid, int attempt)
{
    (void)kwin_pid;

    /*
     * KWin exposes the Wayland socket before the compositor is ready for all
     * client teardown paths.  A pre-Plasma xv6 probe that connects, times out,
     * and disconnects can destabilize KWin before the real session starts.
     * Keep startup non-invasive here; the smoke test runs the full Wayland
     * seat/roundtrip probe after Plasma is alive, where a failure represents a
     * real desktop ABI gap instead of launcher-induced damage.
     */
    fprintf(stderr,
            "kde-session: deferring Wayland roundtrip validation until Plasma probes attempt=%d\n",
            attempt);
    return 0;
}

static void unlink_prefix(const char *dirpath, const char *prefix)
{
    DIR *dir = opendir(dirpath);
    struct dirent *de;
    size_t prefix_len = strlen(prefix);

    if (dir == NULL)
        return;

    while ((de = readdir(dir)) != NULL) {
        char path[512];

        if (strncmp(de->d_name, prefix, prefix_len) != 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", dirpath, de->d_name);
        unlink(path);
    }
    closedir(dir);
}

static void cleanup_session_sockets(void)
{
    unlink_prefix("/dev/shm/xdg-runtime-root", "wayland-");
    unlink_prefix("/tmp/.X11-unix", "X");
}

int main(void)
{
    int status;
    pid_t kwin_pid;
    pid_t plasma_pid;
    pid_t network_sni_pid = -1;
    pid_t x11_egl_probe_pid = -1;
    pid_t desktop_wake_pid = -1;
    char *kwin[] = {
        "/usr/bin/kwin_wayland",
        "--xwayland",
        "--no-lockscreen",
        NULL
    };
    char *plasma[] = { "/bin/kde-plasma-session-child", NULL };

    set_kde_env();
    fprintf(stderr, "kde-session: starting KDE Plasma desktop\n");

    if (!is_executable(kwin[0])) {
        fprintf(stderr, "kde-session: kwin_wayland unavailable (%s), running Plasma child directly\n",
                strerror(errno));
        redirect_stdio_to_log(PLASMA_SESSION_LOG);
        execv(plasma[0], plasma);
        fprintf(stderr, "kde-session: exec failed: %s\n", strerror(errno));
        return 127;
    }
    if (kwin_alloc_trace_enabled())
        unlink(KWIN_ALLOC_TRACE_LOG);
    {
        int pre_kwin_probe_rc = maybe_run_pre_kwin_libinput_probe();

        if (pre_kwin_libinput_probe_only_enabled()) {
            fprintf(stderr,
                    "kde-session: pre-kwin-libinput-probe-only status=%s rc=%d\n",
                    pre_kwin_probe_rc == 0 ? "PASS" : "FAIL",
                    pre_kwin_probe_rc);
            return pre_kwin_probe_rc == 0 ? 0 : 127;
        }
    }

    for (int attempt = 1; attempt <= 3; attempt++) {
        cleanup_session_sockets();
        clear_client_display_env();
        fprintf(stderr, "kde-session: launching KWin attempt=%d\n", attempt);
        kwin_pid = spawn_kwin_child(kwin, attempt);
        if (kwin_pid < 0)
            return 127;
        snapshot_kwin_proc(kwin_pid, attempt);
        if (wait_for_wayland_socket(kwin_pid, attempt) == 0) {
            setenv("WAYLAND_DISPLAY", "wayland-0", 1);
            if (wait_for_wayland_roundtrip(kwin_pid, attempt) != 0) {
                clear_client_display_env();
                goto retry;
            }
            plasma_pid = spawn_child_logged(plasma, PLASMA_SESSION_LOG);
            if (plasma_pid < 0) {
                terminate_child(kwin_pid);
                clear_client_display_env();
                return 127;
            }
            if (wait_for_plasmashell(kwin_pid, plasma_pid, attempt) == 0) {
                note_xwayland_state(kwin_pid, attempt);
                goto running;
            }
            terminate_child(plasma_pid);
            clear_client_display_env();
        }

retry:
        fprintf(stderr, "kde-session: KWin startup attempt=%d failed, retrying\n",
                attempt);
        terminate_child(kwin_pid);
        clear_client_display_env();
        sleep(1);
    }

    fprintf(stderr, "kde-session: KWin startup failed after retries\n");
    return 127;

running:
    fprintf(stderr, "kde-session: KWin and plasmashell are running\n");
    desktop_wake_pid = maybe_spawn_desktop_wake();
    network_sni_pid = maybe_spawn_network_status_sni();
    x11_egl_probe_pid = maybe_spawn_x11_egl_session_probe();
    maybe_spawn_smoke_agent();
    reap_desktop_wake(desktop_wake_pid);
    reap_x11_egl_session_probe(x11_egl_probe_pid);
    if (waitpid(plasma_pid, &status, 0) < 0) {
        fprintf(stderr, "kde-session: wait Plasma child: %s\n", strerror(errno));
        terminate_child(kwin_pid);
        terminate_child(plasma_pid);
        terminate_child(desktop_wake_pid);
        terminate_child(network_sni_pid);
        return 127;
    }
    terminate_child(desktop_wake_pid);
    terminate_child(network_sni_pid);
    terminate_child(kwin_pid);

    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
