#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CMDLINE_MAX 4096

static int trace_enabled(void)
{
    const char *value = getenv("KDE_PROCESS_PROBE_TRACE");

    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

static void trace_step(const char *phase, const char *pid, const char *leaf)
{
    if (!trace_enabled())
        return;
    fprintf(stderr, "kde_process_probe trace phase=%s", phase);
    if (pid)
        fprintf(stderr, " pid=%s", pid);
    if (leaf)
        fprintf(stderr, " leaf=%s", leaf);
    fputc('\n', stderr);
    fflush(stderr);
}

struct kde_roles {
    int proc_tasks;
    int kwin;
    int plasmashell;
    int kactivitymanagerd;
    int krunner;
    int kded5;
    int xwayland;
    int login1;
    int dbus;
    int systemsettings;
    int konsole;
    int dolphin;
    int kate;
    int kwrite;
    int chromium;
    int pipewire;
    int pipewire_pulse;
    int wireplumber;
    int pulseaudio;
};

static int is_pid_dir(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;

    if (*p < '0' || *p > '9')
        return 0;
    while (*p) {
        if (*p < '0' || *p > '9')
            return 0;
        p++;
    }
    return 1;
}

static int read_proc_file(const char *pid, const char *leaf, char *buf,
                          size_t cap)
{
    char path[128];
    int fd;
    ssize_t n;

    if (cap == 0)
        return -1;
    snprintf(path, sizeof(path), "/proc/%s/%s", pid, leaf);
    trace_step("open-before", pid, leaf);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        trace_step("open-failed", pid, leaf);
        return -1;
    }
    trace_step("read-before", pid, leaf);
    n = read(fd, buf, cap - 1);
    trace_step("read-after", pid, leaf);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    return (int)n;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

static void strip_newline(char *text)
{
    char *nl = strchr(text, '\n');

    if (nl)
        *nl = '\0';
}

static void copy_name(char *dst, size_t cap, const char *src)
{
    if (cap == 0)
        return;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static int read_process_name(const char *pid, char *name, size_t cap)
{
    char raw[CMDLINE_MAX];
    int n;
    const char *base;

    n = read_proc_file(pid, "cmdline", raw, sizeof(raw));
    if (n > 0 && raw[0] != '\0') {
        base = base_name(raw);
        copy_name(name, cap, base);
        return 0;
    }

    n = read_proc_file(pid, "comm", raw, sizeof(raw));
    if (n > 0) {
        strip_newline(raw);
        copy_name(name, cap, raw);
        return 0;
    }
    return -1;
}

static void classify_process(const char *name, struct kde_roles *roles)
{
    if (strcmp(name, "kwin_wayland") == 0)
        roles->kwin++;
    else if (strcmp(name, "plasmashell") == 0)
        roles->plasmashell++;
    else if (strcmp(name, "kactivitymanagerd") == 0)
        roles->kactivitymanagerd++;
    else if (strcmp(name, "krunner") == 0)
        roles->krunner++;
    else if (strcmp(name, "kded5") == 0)
        roles->kded5++;
    else if (strcmp(name, "Xwayland") == 0 ||
             strcmp(name, "Xwayland.real") == 0)
        roles->xwayland++;
    else if (strcmp(name, "xv6-login1-shim") == 0)
        roles->login1++;
    else if (strcmp(name, "dbus-daemon") == 0 ||
             strcmp(name, "dbus-daemon-host") == 0)
        roles->dbus++;
    else if (strcmp(name, "systemsettings") == 0)
        roles->systemsettings++;
    else if (strcmp(name, "konsole") == 0)
        roles->konsole++;
    else if (strcmp(name, "dolphin") == 0)
        roles->dolphin++;
    else if (strcmp(name, "kate") == 0)
        roles->kate++;
    else if (strcmp(name, "kwrite") == 0)
        roles->kwrite++;
    else if (strcmp(name, "chrome") == 0 ||
             strcmp(name, "chromium") == 0 ||
             strcmp(name, "wayland-chromium") == 0)
        roles->chromium++;
    else if (strcmp(name, "pipewire") == 0)
        roles->pipewire++;
    else if (strcmp(name, "pipewire-pulse") == 0)
        roles->pipewire_pulse++;
    else if (strcmp(name, "wireplumber") == 0)
        roles->wireplumber++;
    else if (strcmp(name, "pulseaudio") == 0)
        roles->pulseaudio++;
}

static int has_arg(int argc, char **argv, const char *needle)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], needle) == 0)
            return 1;
    }
    return 0;
}

static void read_uptime(char *buf, size_t cap)
{
    int fd;
    ssize_t n;
    char *space;

    if (cap == 0)
        return;
    snprintf(buf, cap, "unknown");
    fd = open("/proc/uptime", O_RDONLY);
    if (fd < 0)
        return;
    n = read(fd, buf, cap - 1);
    close(fd);
    if (n <= 0) {
        snprintf(buf, cap, "unknown");
        return;
    }
    buf[n] = '\0';
    space = strchr(buf, ' ');
    if (space)
        *space = '\0';
    space = strchr(buf, '\n');
    if (space)
        *space = '\0';
}

int main(int argc, char **argv)
{
    struct kde_roles roles;
    trace_step("opendir-before", NULL, NULL);
    DIR *d = opendir("/proc");
    struct dirent *de;
    char name[256];
    char uptime[64];
    int require_xwayland = has_arg(argc, argv, "--require-xwayland");
    int require_full = has_arg(argc, argv, "--require-full");
    int require_systemsettings = has_arg(argc, argv, "--require-systemsettings");
    int require_konsole = has_arg(argc, argv, "--require-konsole");
    int require_dolphin = has_arg(argc, argv, "--require-dolphin");
    int require_kate = has_arg(argc, argv, "--require-kate");
    int require_apps = has_arg(argc, argv, "--require-apps");
    int require_chromium = has_arg(argc, argv, "--require-chromium");
    int require_audio = has_arg(argc, argv, "--require-audio");
    int ok;

    if (!d) {
        perror("opendir /proc");
        return 1;
    }
    trace_step("opendir-after", NULL, NULL);
    memset(&roles, 0, sizeof(roles));
    while ((de = readdir(d)) != NULL) {
        if (!is_pid_dir(de->d_name))
            continue;
        roles.proc_tasks++;
        trace_step("pid-before", de->d_name, NULL);
        if (read_process_name(de->d_name, name, sizeof(name)) == 0)
            classify_process(name, &roles);
        trace_step("pid-after", de->d_name, NULL);
    }
    closedir(d);

    ok = roles.kwin > 0 && roles.plasmashell > 0;
    if (require_xwayland)
        ok = ok && roles.xwayland > 0;
    if (require_full) {
        ok = ok && roles.kactivitymanagerd > 0 && roles.krunner > 0 &&
             roles.kded5 > 0 && roles.login1 > 0 && roles.dbus >= 2;
    }
    if (require_systemsettings)
        ok = ok && roles.systemsettings > 0;
    if (require_konsole)
        ok = ok && roles.konsole > 0;
    if (require_dolphin)
        ok = ok && roles.dolphin > 0;
    if (require_kate)
        ok = ok && (roles.kate > 0 || roles.kwrite > 0);
    if (require_apps) {
        ok = ok && roles.konsole > 0 && roles.dolphin > 0 &&
             (roles.kate > 0 || roles.kwrite > 0);
    }
    if (require_chromium)
        ok = ok && roles.chromium > 0;
    if (require_audio) {
        ok = ok && roles.pipewire > 0 &&
             (roles.pipewire_pulse > 0 || roles.pulseaudio > 0);
    }

    trace_step("uptime-before", NULL, NULL);
    read_uptime(uptime, sizeof(uptime));
    trace_step("uptime-after", NULL, NULL);
    printf("kde_process_probe uptime_s=%s proc_tasks=%d kwin=%d plasmashell=%d "
           "kactivitymanagerd=%d krunner=%d kded5=%d xwayland=%d "
           "login1=%d dbus=%d systemsettings=%d konsole=%d dolphin=%d "
           "kate=%d kwrite=%d chromium=%d pipewire=%d pipewire_pulse=%d wireplumber=%d "
           "pulseaudio=%d require_xwayland=%d require_full=%d "
           "require_systemsettings=%d require_konsole=%d require_dolphin=%d "
           "require_kate=%d require_apps=%d require_chromium=%d "
           "require_audio=%d status=%s\n",
           uptime, roles.proc_tasks, roles.kwin, roles.plasmashell,
           roles.kactivitymanagerd, roles.krunner, roles.kded5,
           roles.xwayland, roles.login1, roles.dbus, roles.systemsettings,
           roles.konsole, roles.dolphin, roles.kate, roles.kwrite,
           roles.chromium, roles.pipewire, roles.pipewire_pulse, roles.wireplumber,
           roles.pulseaudio, require_xwayland, require_full,
           require_systemsettings, require_konsole, require_dolphin,
           require_kate, require_apps, require_chromium, require_audio,
           ok ? "PASS" : "WAIT");

    return ok ? 0 : 2;
}
