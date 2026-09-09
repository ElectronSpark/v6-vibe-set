#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int get_u32_result(GVariant *dict, const char *key, uint32_t *out)
{
    GVariant *value = g_variant_lookup_value(dict, key, NULL);

    if (!value)
        return -1;
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32)) {
        *out = g_variant_get_uint32(value);
        g_variant_unref(value);
        return 0;
    }
    g_variant_unref(value);
    return -1;
}

static int get_string_result(GVariant *dict, const char *key, const char **out)
{
    GVariant *value = g_variant_lookup_value(dict, key, NULL);

    if (!value)
        return -1;
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
        *out = g_variant_get_string(value, NULL);
        return 0;
    }
    g_variant_unref(value);
    return -1;
}

static ssize_t read_with_timeout(int fd, uint8_t *buf, size_t count, int timeout_ms)
{
    size_t off = 0;

    while (off < count) {
        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN | POLLHUP,
        };
        int ready = poll(&pfd, 1, timeout_ms);

        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ready == 0)
            break;
        if (!(pfd.revents & (POLLIN | POLLHUP)))
            break;

        ssize_t n = read(fd, buf + off, count - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }

    return (ssize_t)off;
}

static void print_fail(const char *step, const char *detail)
{
    fprintf(stderr, "kde_kwin_screenshot_probe fail_step=%s detail=%s\n", step, detail);
    printf("kde_kwin_screenshot_probe result=FAIL\n");
}

static int dbus_name_has_owner(GDBusConnection *bus, const char *name)
{
    GError *error = NULL;
    GVariant *reply;
    gboolean owned = FALSE;

    reply = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "NameHasOwner",
        g_variant_new("(s)", name),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        3000,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "kde_kwin_screenshot_probe name_owner_query=%s error=%s\n",
                name, error ? error->message : "unknown");
        g_clear_error(&error);
        return 0;
    }

    g_variant_get(reply, "(b)", &owned);
    g_variant_unref(reply);
    return owned ? 1 : 0;
}

static void print_kde_bus_names(GDBusConnection *bus)
{
    GError *error = NULL;
    GVariant *reply;
    char **names = NULL;

    reply = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ListNames",
        NULL,
        G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NONE,
        3000,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "kde_kwin_screenshot_probe list_names_error=%s\n",
                error ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }

    g_variant_get(reply, "(^as)", &names);
    fprintf(stderr, "kde_kwin_screenshot_probe bus_names=");
    for (char **p = names; p && *p; p++) {
        if (strstr(*p, "org.kde") || strstr(*p, "KWin"))
            fprintf(stderr, "%s,", *p);
    }
    fprintf(stderr, "\n");
    g_strfreev(names);
    g_variant_unref(reply);
}

static void print_string_array_property(GDBusConnection *bus, const char *property)
{
    GError *error = NULL;
    GVariant *reply;
    GVariant *value = NULL;
    GVariant *array = NULL;
    char **items = NULL;
    gsize count = 0;

    reply = g_dbus_connection_call_sync(
        bus,
        "org.kde.KWin",
        "/Effects",
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", "org.kde.kwin.Effects", property),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "kde_kwin_screenshot_probe property=%s error=%s\n",
                property, error ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }

    g_variant_get(reply, "(@v)", &value);
    array = g_variant_get_variant(value);
    if (!g_variant_is_of_type(array, G_VARIANT_TYPE_STRING_ARRAY)) {
        fprintf(stderr, "kde_kwin_screenshot_probe property=%s type=%s unexpected\n",
                property, g_variant_get_type_string(array));
        g_variant_unref(array);
        g_variant_unref(value);
        g_variant_unref(reply);
        return;
    }

    items = g_variant_dup_strv(array, &count);
    fprintf(stderr, "kde_kwin_screenshot_probe property=%s count=%zu values=",
            property, (size_t)count);
    for (gsize i = 0; i < count && i < 32; i++)
        fprintf(stderr, "%s,", items[i]);
    if (count > 32)
        fprintf(stderr, "...");
    fprintf(stderr, "\n");

    g_strfreev(items);
    g_variant_unref(array);
    g_variant_unref(value);
    g_variant_unref(reply);
}

static void try_load_one_effect(GDBusConnection *bus, const char *effect_name)
{
    GError *error = NULL;
    GVariant *reply;
    gboolean supported = FALSE;
    gboolean loaded = FALSE;

    reply = g_dbus_connection_call_sync(
        bus,
        "org.kde.KWin",
        "/Effects",
        "org.kde.kwin.Effects",
        "isEffectSupported",
        g_variant_new("(s)", effect_name),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "kde_kwin_screenshot_probe effect_supported_error=%s\n",
                error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        g_variant_get(reply, "(b)", &supported);
        g_variant_unref(reply);
    }

    reply = g_dbus_connection_call_sync(
        bus,
        "org.kde.KWin",
        "/Effects",
        "org.kde.kwin.Effects",
        "isEffectLoaded",
        g_variant_new("(s)", effect_name),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "kde_kwin_screenshot_probe effect_loaded_error=%s\n",
                error ? error->message : "unknown");
        g_clear_error(&error);
    } else {
        g_variant_get(reply, "(b)", &loaded);
        g_variant_unref(reply);
    }

    if (!loaded) {
        reply = g_dbus_connection_call_sync(
            bus,
            "org.kde.KWin",
            "/Effects",
            "org.kde.kwin.Effects",
            "loadEffect",
            g_variant_new("(s)", effect_name),
            G_VARIANT_TYPE("(b)"),
            G_DBUS_CALL_FLAGS_NONE,
            5000,
            NULL,
            &error);
        if (!reply) {
            fprintf(stderr, "kde_kwin_screenshot_probe effect_load_error=%s\n",
                    error ? error->message : "unknown");
            g_clear_error(&error);
        } else {
            g_variant_get(reply, "(b)", &loaded);
            g_variant_unref(reply);
        }
    }

    fprintf(stderr, "kde_kwin_screenshot_probe effect name=%s supported=%d loaded=%d\n",
            effect_name, supported ? 1 : 0, loaded ? 1 : 0);
}

static void try_load_screenshot_effect(GDBusConnection *bus)
{
    print_string_array_property(bus, "listOfEffects");
    print_string_array_property(bus, "loadedEffects");
    try_load_one_effect(bus, "screenshot");
    if (!dbus_name_has_owner(bus, "org.kde.KWin.ScreenShot2"))
        try_load_one_effect(bus, "kwin4_effect_screenshot");
    print_string_array_property(bus, "loadedEffects");
}

int main(void)
{
    GError *error = NULL;
    GDBusConnection *bus = NULL;
    GUnixFDList *fd_list = NULL;
    GUnixFDList *out_fd_list = NULL;
    GVariantBuilder options;
    GVariant *reply = NULL;
    GVariant *results = NULL;
    const char *type = NULL;
    int pipefd[2] = { -1, -1 };
    int fd_index;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t format = 0;
    size_t expected;
    uint8_t *pixels = NULL;
    ssize_t got;
    unsigned long nonblack = 0;
    unsigned long colorful = 0;
    unsigned long alpha_nonzero = 0;
    unsigned long low_bit_detail = 0;
    unsigned int unique_buckets = 0;
    unsigned int unique_r = 0;
    unsigned int unique_g = 0;
    unsigned int unique_b = 0;
    uint8_t seen[256] = { 0 };
    uint8_t seen_r[256] = { 0 };
    uint8_t seen_g[256] = { 0 };
    uint8_t seen_b[256] = { 0 };

    if (!getenv("DBUS_SESSION_BUS_ADDRESS"))
        setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 1);
    if (!getenv("XDG_DATA_DIRS"))
        setenv("XDG_DATA_DIRS", "/usr/share:/share", 1);

    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!bus) {
        print_fail("session-bus", error ? error->message : "unknown");
        g_clear_error(&error);
        return 1;
    }

    if (!dbus_name_has_owner(bus, "org.kde.KWin.ScreenShot2")) {
        try_load_screenshot_effect(bus);
        for (int i = 0; i < 20 && !dbus_name_has_owner(bus, "org.kde.KWin.ScreenShot2"); i++)
            usleep(100000);
    }

    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        print_fail("pipe2", strerror(errno));
        g_object_unref(bus);
        return 1;
    }

    fd_list = g_unix_fd_list_new();
    fd_index = g_unix_fd_list_append(fd_list, pipefd[1], &error);
    if (fd_index < 0) {
        print_fail("fd-list", error ? error->message : "unknown");
        g_clear_error(&error);
        close(pipefd[0]);
        close(pipefd[1]);
        g_object_unref(bus);
        return 1;
    }

    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    reply = g_dbus_connection_call_with_unix_fd_list_sync(
        bus,
        "org.kde.KWin.ScreenShot2",
        "/org/kde/KWin/ScreenShot2",
        "org.kde.KWin.ScreenShot2",
        "CaptureWorkspace",
        g_variant_new("(a{sv}h)", &options, fd_index),
        G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE,
        15000,
        fd_list,
        &out_fd_list,
        NULL,
        &error);
    close(pipefd[1]);
    pipefd[1] = -1;

    if (!reply) {
        if (!dbus_name_has_owner(bus, "org.kde.KWin.ScreenShot2"))
            print_kde_bus_names(bus);
        print_fail("dbus-call", error ? error->message : "unknown");
        g_clear_error(&error);
        close(pipefd[0]);
        g_object_unref(fd_list);
        g_object_unref(bus);
        return 1;
    }

    g_variant_get(reply, "(@a{sv})", &results);
    if (get_string_result(results, "type", &type) < 0 || strcmp(type, "raw") != 0) {
        print_fail("result-type", "missing-or-not-raw");
        goto fail;
    }
    if (get_u32_result(results, "width", &width) < 0 ||
        get_u32_result(results, "height", &height) < 0 ||
        get_u32_result(results, "stride", &stride) < 0 ||
        get_u32_result(results, "format", &format) < 0) {
        print_fail("result-metadata", "missing geometry, stride, or format");
        goto fail;
    }
    if (width < 640 || height < 480 || stride < width * 4) {
        print_fail("result-geometry", "unexpectedly small or narrow stride");
        goto fail;
    }
    if (height > SIZE_MAX / stride) {
        print_fail("result-geometry", "size overflow");
        goto fail;
    }

    expected = (size_t)height * stride;
    pixels = malloc(expected);
    if (!pixels) {
        print_fail("alloc", strerror(errno));
        goto fail;
    }

    got = read_with_timeout(pipefd[0], pixels, expected, 15000);
    if (got < 0) {
        print_fail("read", strerror(errno));
        goto fail;
    }
    if ((size_t)got != expected) {
        char detail[128];
        snprintf(detail, sizeof(detail), "got=%zd expected=%zu", got, expected);
        print_fail("read-size", detail);
        goto fail;
    }

    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = pixels + (size_t)y * stride;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            uint8_t a = row[x * 4 + 3];
            unsigned int bucket = ((unsigned int)(r & 0xe0)) |
                                  ((unsigned int)(g & 0xe0) >> 3) |
                                  ((unsigned int)(b & 0xc0) >> 6);

            if (r || g || b)
                nonblack++;
            if ((abs((int)r - (int)g) > 8) ||
                (abs((int)g - (int)b) > 8) ||
                (abs((int)r - (int)b) > 8))
                colorful++;
            if (a)
                alpha_nonzero++;
            if ((r & 0x07) || (g & 0x03) || (b & 0x07))
                low_bit_detail++;
            if (!seen[bucket]) {
                seen[bucket] = 1;
                unique_buckets++;
            }
            if (!seen_r[r]) {
                seen_r[r] = 1;
                unique_r++;
            }
            if (!seen_g[g]) {
                seen_g[g] = 1;
                unique_g++;
            }
            if (!seen_b[b]) {
                seen_b[b] = 1;
                unique_b++;
            }
        }
    }

    printf("kde_kwin_screenshot_probe type=%s width=%u height=%u stride=%u format=%u "
           "bytes=%zu nonblack=%lu colorful=%lu alpha_nonzero=%lu "
           "low_bit_detail=%lu unique_buckets=%u unique_rgb=%u/%u/%u\n",
           type, width, height, stride, format, expected, nonblack, colorful,
           alpha_nonzero, low_bit_detail, unique_buckets, unique_r, unique_g,
           unique_b);

    if (nonblack < (unsigned long)width * height / 5) {
        print_fail("pixels", "too-few-nonblack");
        goto fail;
    }
    if (alpha_nonzero < (unsigned long)width * height / 2) {
        print_fail("pixels", "too-few-alpha");
        goto fail;
    }
    if (unique_buckets < 24 || colorful < (unsigned long)width * height / 20) {
        print_fail("pixels", "low-color-detail");
        goto fail;
    }
    if (low_bit_detail < (unsigned long)width * height / 100 ||
        unique_r < 40 || unique_g < 60 || unique_b < 40) {
        print_fail("pixels", "low-channel-bit-depth");
        goto fail;
    }

    printf("kde_kwin_screenshot_probe result=PASS\n");

    free(pixels);
    close(pipefd[0]);
    if (out_fd_list)
        g_object_unref(out_fd_list);
    g_variant_unref(results);
    g_variant_unref(reply);
    g_object_unref(fd_list);
    g_object_unref(bus);
    return 0;

fail:
    free(pixels);
    if (pipefd[0] >= 0)
        close(pipefd[0]);
    if (pipefd[1] >= 0)
        close(pipefd[1]);
    if (out_fd_list)
        g_object_unref(out_fd_list);
    if (results)
        g_variant_unref(results);
    if (reply)
        g_variant_unref(reply);
    if (fd_list)
        g_object_unref(fd_list);
    if (bus)
        g_object_unref(bus);
    return 1;
}
