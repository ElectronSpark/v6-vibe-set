#define _GNU_SOURCE

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char bluez_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg type='a{oa{sa{sv}}}' direction='out'/>"
    "    </method>"
    "    <signal name='InterfacesAdded'>"
    "      <arg type='o'/>"
    "      <arg type='a{sa{sv}}'/>"
    "    </signal>"
    "    <signal name='InterfacesRemoved'>"
    "      <arg type='o'/>"
    "      <arg type='as'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *bluez_info;

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
    (void)parameters;
    (void)user_data;

    if (g_strcmp0(method_name, "GetManagedObjects") == 0) {
        GVariantBuilder objects;

        g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(a{oa{sa{sv}}})", &objects));
        return;
    }

    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED,
                                          "xv6-bluez-shim does not implement %s",
                                          method_name);
}

static const GDBusInterfaceVTable bluez_vtable = {
    handle_method_call,
    NULL,
    NULL,
    { 0 }
};

static GDBusConnection *connect_system_bus_with_retry(void)
{
    GError *error = NULL;
    const char *address = getenv("DBUS_SYSTEM_BUS_ADDRESS");

    if (!address || !address[0])
        address = "unix:abstract=xv6_system_bus";

    for (int attempt = 0; attempt < 3; attempt++) {
        GDBusConnection *bus = g_dbus_connection_new_for_address_sync(
            address,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
            NULL, NULL, &error);
        if (bus)
            return bus;

        fprintf(stderr, "xv6-bluez-shim: system bus %s: %s\n",
                address, error ? error->message : "unknown error");
        g_clear_error(&error);
    }

    return NULL;
}

static int request_bluez_name(GDBusConnection *bus)
{
    GError *error = NULL;
    GVariant *reply;
    unsigned int status = 0;

    reply = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "RequestName",
        g_variant_new("(su)", "org.bluez", 0),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "xv6-bluez-shim: RequestName: %s\n", error->message);
        g_clear_error(&error);
        return 0;
    }

    g_variant_get(reply, "(u)", &status);
    g_variant_unref(reply);
    if (status != 1 && status != 4) {
        fprintf(stderr, "xv6-bluez-shim: RequestName status=%u\n", status);
        return 0;
    }

    return 1;
}

int main(void)
{
    GMainLoop *loop;
    GError *error = NULL;
    GDBusConnection *bus;
    guint root_reg_id;
    guint org_reg_id;

    fprintf(stderr, "xv6-bluez-shim: start\n");
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);
    bluez_info = g_dbus_node_info_new_for_xml(bluez_xml, &error);
    if (!bluez_info) {
        fprintf(stderr, "xv6-bluez-shim: introspection: %s\n", error->message);
        return 1;
    }

    bus = connect_system_bus_with_retry();
    if (!bus) {
        fprintf(stderr, "xv6-bluez-shim: system bus unavailable\n");
        return 1;
    }

    root_reg_id = g_dbus_connection_register_object(
        bus, "/", bluez_info->interfaces[0],
        &bluez_vtable, NULL, NULL, &error);
    if (!root_reg_id) {
        fprintf(stderr, "xv6-bluez-shim: register /: %s\n", error->message);
        return 1;
    }
    org_reg_id = g_dbus_connection_register_object(
        bus, "/org/bluez", bluez_info->interfaces[0],
        &bluez_vtable, NULL, NULL, &error);
    if (!org_reg_id) {
        fprintf(stderr, "xv6-bluez-shim: register /org/bluez: %s\n",
                error->message);
        return 1;
    }

    if (!request_bluez_name(bus))
        return 1;

    fprintf(stderr, "xv6-bluez-shim: acquired org.bluez\n");
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
