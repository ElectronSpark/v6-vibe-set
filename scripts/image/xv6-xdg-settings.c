#include <stdio.h>
#include <string.h>

static const char *default_browser = "xv6-wayland-chromium.desktop";

static const char *base_name(const char *path)
{
    const char *slash;

    if (path == NULL)
        return "";
    slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int is_browser_property(const char *property)
{
    return strcmp(property, "default-web-browser") == 0 ||
           strcmp(property, "default-url-scheme-handler") == 0;
}

static int is_chromium_desktop_file(const char *value)
{
    const char *name = base_name(value);

    return strcmp(name, default_browser) == 0 ||
           strcmp(name, "chromium-browser.desktop") == 0 ||
           strcmp(name, "chromium.desktop") == 0 ||
           strcmp(name, "google-chrome.desktop") == 0 ||
           strcmp(name, "google-chrome-stable.desktop") == 0 ||
           strcmp(name, "com.google.Chrome.desktop") == 0;
}

static void usage(void)
{
    fputs("Usage: xdg-settings {get|check|set} property [subproperty] [value]\n",
          stderr);
}

int main(int argc, char **argv)
{
    const char *op;
    const char *property;
    const char *value;

    if (argc == 2 &&
        (strcmp(argv[1], "--version") == 0 ||
         strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "--manual") == 0)) {
        puts("xv6-xdg-settings 1.0");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        puts("Known properties:");
        puts("  default-web-browser");
        puts("  default-url-scheme-handler");
        return 0;
    }
    if (argc < 3) {
        usage();
        return 1;
    }

    op = argv[1];
    property = argv[2];
    if (!is_browser_property(property))
        return strcmp(op, "set") == 0 ? 0 : 1;

    if (strcmp(op, "get") == 0) {
        puts(default_browser);
        return 0;
    }

    if (strcmp(op, "check") == 0) {
        value = argv[argc - 1];
        return is_chromium_desktop_file(value) ? 0 : 1;
    }

    if (strcmp(op, "set") == 0)
        return 0;

    usage();
    return 1;
}
