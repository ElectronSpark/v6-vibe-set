#define _GNU_SOURCE

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char item_path[] = "/StatusNotifierItem";
static const char watcher_name[] = "org.kde.StatusNotifierWatcher";
static const char watcher_path[] = "/StatusNotifierWatcher";
static const char watcher_iface[] = "org.kde.StatusNotifierWatcher";

static const char item_xml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierItem'>"
    "    <method name='ContextMenu'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='i' direction='in'/>"
    "    </method>"
    "    <method name='Activate'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='i' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='i' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg type='i' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "    </method>"
    "    <property name='Category' type='s' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Title' type='s' access='read'/>"
    "    <property name='Status' type='s' access='read'/>"
    "    <property name='WindowId' type='i' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "    <property name='IconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='OverlayIconName' type='s' access='read'/>"
    "    <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='AttentionIconName' type='s' access='read'/>"
    "    <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
    "    <property name='AttentionMovieName' type='s' access='read'/>"
    "    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "    <property name='ItemIsMenu' type='b' access='read'/>"
    "    <property name='Menu' type='o' access='read'/>"
    "    <signal name='NewTitle'/>"
    "    <signal name='NewIcon'/>"
    "    <signal name='NewAttentionIcon'/>"
    "    <signal name='NewOverlayIcon'/>"
    "    <signal name='NewToolTip'/>"
    "    <signal name='NewStatus'>"
    "      <arg type='s'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *item_info;
static char bus_name[128];

static GVariant *empty_icon_pixmaps(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
    return g_variant_builder_end(&builder);
}

static GVariant *property_for(const char *property_name)
{
    if (g_strcmp0(property_name, "Category") == 0)
        return g_variant_new_string("Communications");
    if (g_strcmp0(property_name, "Id") == 0)
        return g_variant_new_string("xv6-network-status");
    if (g_strcmp0(property_name, "Title") == 0)
        return g_variant_new_string("xv6 network");
    if (g_strcmp0(property_name, "Status") == 0)
        return g_variant_new_string("Active");
    if (g_strcmp0(property_name, "WindowId") == 0)
        return g_variant_new_int32(0);
    if (g_strcmp0(property_name, "IconName") == 0)
        return g_variant_new_string("network-wired-activated");
    if (g_strcmp0(property_name, "IconPixmap") == 0 ||
        g_strcmp0(property_name, "OverlayIconPixmap") == 0 ||
        g_strcmp0(property_name, "AttentionIconPixmap") == 0)
        return empty_icon_pixmaps();
    if (g_strcmp0(property_name, "OverlayIconName") == 0 ||
        g_strcmp0(property_name, "AttentionIconName") == 0 ||
        g_strcmp0(property_name, "AttentionMovieName") == 0)
        return g_variant_new_string("");
    if (g_strcmp0(property_name, "ToolTip") == 0)
        return g_variant_new("(s@a(iiay)ss)", "network-wired-activated",
                             empty_icon_pixmaps(), "Wired connected",
                             "xv6 network online");
    if (g_strcmp0(property_name, "ItemIsMenu") == 0)
        return g_variant_new_boolean(FALSE);
    if (g_strcmp0(property_name, "Menu") == 0)
        return g_variant_new_object_path("/");
    return NULL;
}

static GVariant *handle_get_property(GDBusConnection *connection,
                                     const char *sender,
                                     const char *object_path,
                                     const char *interface_name,
                                     const char *property_name,
                                     GError **error,
                                     void *user_data)
{
    GVariant *value;

    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)user_data;

    value = property_for(property_name);
    if (value)
        return value;

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "xv6-network-status-sni property %s unavailable",
                property_name);
    return NULL;
}

static void handle_method_call(GDBusConnection *connection,
                               const char *sender,
                               const char *object_path,
                               const char *interface_name,
                               const char *method_name,
                               GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               void *user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)method_name;
    (void)parameters;
    (void)user_data;

    g_dbus_method_invocation_return_value(invocation, NULL);
}

static const GDBusInterfaceVTable item_vtable = {
    handle_method_call,
    handle_get_property,
    NULL,
    { 0 }
};

static GDBusConnection *connect_session_bus_with_retry(void)
{
    GError *error = NULL;
    const char *address = getenv("DBUS_SESSION_BUS_ADDRESS");

    if (!address || !address[0])
        address = "unix:abstract=xv6_session_bus";

    for (int attempt = 0; attempt < 50; attempt++) {
        GDBusConnection *bus = g_dbus_connection_new_for_address_sync(
            address,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
            NULL, NULL, &error);
        if (bus)
            return bus;

        fprintf(stderr, "xv6-network-status-sni: session bus %s: %s\n",
                address, error ? error->message : "unknown error");
        g_clear_error(&error);
        usleep(100000);
    }

    return NULL;
}

static int request_item_name(GDBusConnection *bus)
{
    GError *error = NULL;
    GVariant *reply;
    unsigned int status = 0;

    snprintf(bus_name, sizeof(bus_name),
             "org.kde.StatusNotifierItem-%ld-1", (long)getpid());
    reply = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "RequestName",
        g_variant_new("(su)", bus_name, 0),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "xv6-network-status-sni: RequestName: %s\n",
                error->message);
        g_clear_error(&error);
        return 0;
    }

    g_variant_get(reply, "(u)", &status);
    g_variant_unref(reply);
    if (status != 1 && status != 4) {
        fprintf(stderr, "xv6-network-status-sni: RequestName status=%u\n",
                status);
        return 0;
    }

    return 1;
}

static int register_with_watcher(GDBusConnection *bus)
{
    GError *error = NULL;
    GVariant *reply;

    for (int attempt = 0; attempt < 100; attempt++) {
        reply = g_dbus_connection_call_sync(
            bus, watcher_name, watcher_path, watcher_iface,
            "RegisterStatusNotifierItem",
            g_variant_new("(s)", bus_name),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            1000,
            NULL,
            &error);
        if (reply) {
            g_variant_unref(reply);
            fprintf(stderr,
                    "xv6-network-status-sni: registered %s icon=network-wired-activated\n",
                    bus_name);
            return 1;
        }

        if (attempt == 0 || attempt == 20 || attempt == 60) {
            fprintf(stderr,
                    "xv6-network-status-sni: watcher registration pending: %s\n",
                    error ? error->message : "unknown error");
        }
        g_clear_error(&error);
        usleep(100000);
    }

    return 0;
}

int main(void)
{
    GMainLoop *loop;
    GError *error = NULL;
    GDBusConnection *bus;
    guint reg_id;

    fprintf(stderr, "xv6-network-status-sni: start\n");
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 0);

    item_info = g_dbus_node_info_new_for_xml(item_xml, &error);
    if (!item_info) {
        fprintf(stderr, "xv6-network-status-sni: introspection: %s\n",
                error->message);
        return 1;
    }

    bus = connect_session_bus_with_retry();
    if (!bus) {
        fprintf(stderr, "xv6-network-status-sni: session bus unavailable\n");
        return 1;
    }
    if (!request_item_name(bus))
        return 1;

    reg_id = g_dbus_connection_register_object(
        bus, item_path, item_info->interfaces[0],
        &item_vtable, NULL, NULL, &error);
    if (!reg_id) {
        fprintf(stderr, "xv6-network-status-sni: register item: %s\n",
                error->message);
        return 1;
    }

    if (!register_with_watcher(bus)) {
        fprintf(stderr,
                "xv6-network-status-sni: StatusNotifierWatcher unavailable\n");
        return 1;
    }

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
