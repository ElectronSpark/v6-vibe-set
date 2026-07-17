#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int cmdline_has(const char *needle)
{
    FILE *f = fopen("/proc/cmdline", "r");
    char buf[1024];
    int found = 0;

    if (!f)
        return 0;
    if (fgets(buf, sizeof(buf), f))
        found = strstr(buf, needle) != NULL;
    fclose(f);
    return found;
}

static void set_default_env(void)
{
    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("SHELL", "/bin/bash", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp/xdg-runtime-root", 0);
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 0);
}

static void wait_for_login1(void)
{
    for (int attempt = 0; attempt < 100; attempt++) {
        if (access("/tmp/xv6-login1-ready", R_OK) == 0)
            return;
        usleep(100000);
    }

    fprintf(stderr, "xv6-desktop-session: login1 readiness timed out; continuing\n");
}

int main(void)
{
    char *argv_kde[] = { "/bin/kde-session", NULL };

    set_default_env();

    if (cmdline_has("desktop=0")) {
        fprintf(stderr, "xv6-desktop-session: desktop disabled by desktop=0\n");
        return 0;
    }

    fprintf(stderr, "xv6-desktop-session: selected desktop=kde\n");
    wait_for_login1();
    execv(argv_kde[0], argv_kde);

    fprintf(stderr, "xv6-desktop-session: exec failed: %s\n", strerror(errno));
    return 127;
}
