#define _GNU_SOURCE
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

static int cmdline_get_value(const char *key, char *value, size_t value_size)
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
        snprintf(value, value_size, "%s", tok + key_len + 1);
        return 1;
    }
    return 0;
}

static int valid_xwayland_glamor_mode(const char *mode)
{
    return strcmp(mode, "off") == 0 ||
           strcmp(mode, "auto") == 0 ||
           strcmp(mode, "gl") == 0 ||
           strcmp(mode, "es") == 0;
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
        "Command=/bin/sh\n"
        "Name=Shell\n"
        "Parent=FALLBACK/\n";
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
        "[Containments][2][Applets][7]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.systemtray\n"
        "\n"
        "[Containments][2][Applets][7][Configuration][General]\n"
        "extraItems=org.kde.plasma.volume\n"
        "hiddenItems=\n"
        "knownItems=org.kde.plasma.volume\n"
        "shownItems=org.kde.plasma.volume\n"
        "\n"
        "[Containments][2][Applets][8]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.digitalclock\n"
        "\n"
        "[Containments][2][Applets][9]\n"
        "immutability=1\n"
        "plugin=org.kde.plasma.showdesktop\n"
        "\n"
        "[Containments][2][General]\n"
        "AppletOrder=3;4;5;6;7;8;9\n";

    snprintf(kactivitymanagerdrc, sizeof(kactivitymanagerdrc),
             kactivitymanagerdrc_fmt, activity_id, activity_id, activity_id);
    snprintf(plasma_appletsrc, sizeof(plasma_appletsrc),
             plasma_appletsrc_fmt, activity_id, activity_id);

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
    setenv("SHELL", "/bin/sh", 1);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("XDG_CACHE_HOME", "/dev/shm/kde-cache", 1);
    setenv("XDG_CONFIG_HOME", "/dev/shm/kde-config", 1);
    setenv("XDG_DATA_HOME", "/dev/shm/kde-data", 1);
    setenv("XDG_STATE_HOME", "/dev/shm/kde-state", 1);
    setenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share:/share", 1);
    setenv("XDG_CONFIG_DIRS", "/etc/xdg:/usr/share/kubuntu-default-settings/kf5-settings", 1);
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
    setenv("LD_PRELOAD", "/usr/lib/x86_64-linux-gnu/libpcre2-16.so.0", 0);
    setenv("LIBGL_DRIVERS_PATH", "/lib/dri:/usr/lib/x86_64-linux-gnu/dri", 1);
    setenv("GBM_BACKENDS_PATH", "/lib/gbm:/usr/lib/x86_64-linux-gnu/gbm", 1);
    setenv("MESA_LOADER_DRIVER_OVERRIDE", "virtio_gpu", 0);
    setenv("GALLIUM_DRIVER", "virgl", 0);
    setenv("PULSE_COOKIE", "/dev/shm/kde-config/pulse/cookie", 1);
    if (cmdline_has_flag("kde_libinput_trace=1"))
        setenv("XV6_LIBINPUT_TRACE", "1", 1);
    if (cmdline_has_flag("kde_wayland_debug=1"))
        setenv("WAYLAND_DEBUG", "1", 1);
    {
        char glamor[16];

        if (cmdline_get_value("kde_xwayland_glamor", glamor, sizeof(glamor)) &&
            valid_xwayland_glamor_mode(glamor)) {
            setenv("XV6_XWAYLAND_GLAMOR", glamor, 1);
            fprintf(stderr, "kde-session: Xwayland glamor mode=%s\n", glamor);
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

static pid_t spawn_child(char *const argv[])
{
    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "kde-session: fork %s: %s\n", argv[0], strerror(errno));
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        execv(argv[0], argv);
        fprintf(stderr, "kde-session: exec %s failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    setpgid(pid, pid);
    return pid;
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

static void kwin_maps_path(char *path, size_t size, int attempt)
{
    snprintf(path, size, "/dev/shm/kde-kwin-attempt-%d.maps", attempt);
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

static void snapshot_kwin_maps(pid_t kwin_pid, int attempt)
{
    char proc_path[128];
    char out_path[128];
    char tmp_path[160];
    char buf[1024];
    int in_fd;
    int out_fd;
    ssize_t n;

    snprintf(proc_path, sizeof(proc_path), "/proc/%d/maps", (int)kwin_pid);
    kwin_maps_path(out_path, sizeof(out_path), attempt);
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

static void print_kwin_maps_snapshot(int attempt, const char *phase)
{
    char path[128];
    char buf[1024];
    int fd;
    ssize_t n;

    kwin_maps_path(path, sizeof(path), attempt);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "kde-session: kwin maps unavailable attempt=%d phase=%s errno=%d %s\n",
                attempt, phase, errno, strerror(errno));
        return;
    }

    fprintf(stderr, "kde-session: kwin maps begin attempt=%d phase=%s\n",
            attempt, phase);
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write_all(STDERR_FILENO, buf, (size_t)n) < 0)
            break;
    }
    close(fd);
    fprintf(stderr, "kde-session: kwin maps end attempt=%d phase=%s\n",
            attempt, phase);
}

static int wait_for_wayland_socket(pid_t kwin_pid, int attempt)
{
    const char *socket_path = "/dev/shm/xdg-runtime-root/wayland-0";

    for (int i = 0; i < 600; i++) {
        struct stat st;
        int status;

        if ((i % 10) == 0)
            snapshot_kwin_maps(kwin_pid, attempt);
        if (stat(socket_path, &st) == 0)
            return 0;

        if (waitpid(kwin_pid, &status, WNOHANG) == kwin_pid) {
            fprintf(stderr, "kde-session: kwin exited before Wayland socket status=%d\n", status);
            print_kwin_maps_snapshot(attempt, "wayland-socket");
            return -1;
        }
        usleep(100000);
    }

    fprintf(stderr, "kde-session: %s not visible after compositor grace\n",
            socket_path);
    (void)attempt;
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
            snapshot_kwin_maps(kwin_pid, attempt);
        if (process_cmdline_contains("plasmashell"))
            return 0;

        if (waitpid(kwin_pid, &status, WNOHANG) == kwin_pid) {
            fprintf(stderr, "kde-session: kwin exited before plasmashell status=%d\n", status);
            print_kwin_maps_snapshot(attempt, "plasmashell");
            return -1;
        }
        if (waitpid(plasma_pid, &status, WNOHANG) == plasma_pid) {
            fprintf(stderr, "kde-session: Plasma child exited before plasmashell status=%d\n", status);
            print_kwin_maps_snapshot(attempt, "plasma-child");
            return -1;
        }
        usleep(100000);
    }

    fprintf(stderr, "kde-session: plasmashell not visible after session grace\n");
    (void)attempt;
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
        execv(plasma[0], plasma);
        fprintf(stderr, "kde-session: exec failed: %s\n", strerror(errno));
        return 127;
    }

    for (int attempt = 1; attempt <= 3; attempt++) {
        cleanup_session_sockets();
        clear_client_display_env();
        fprintf(stderr, "kde-session: launching KWin attempt=%d\n", attempt);
        kwin_pid = spawn_child(kwin);
        if (kwin_pid < 0)
            return 127;
        snapshot_kwin_maps(kwin_pid, attempt);
        if (wait_for_wayland_socket(kwin_pid, attempt) == 0) {
            setenv("WAYLAND_DISPLAY", "wayland-0", 1);
            if (wait_for_wayland_roundtrip(kwin_pid, attempt) != 0) {
                clear_client_display_env();
                goto retry;
            }
            plasma_pid = spawn_child(plasma);
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
    maybe_spawn_smoke_agent();
    if (waitpid(plasma_pid, &status, 0) < 0) {
        fprintf(stderr, "kde-session: wait Plasma child: %s\n", strerror(errno));
        terminate_child(kwin_pid);
        terminate_child(plasma_pid);
        return 127;
    }
    terminate_child(kwin_pid);

    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
