#define _GNU_SOURCE

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char rtkit_path[] = "/org/freedesktop/RealtimeKit1";
static const char power_path[] = "/net/hadess/PowerProfiles";
static const char upower_power_path[] =
    "/org/freedesktop/UPower/PowerProfiles";
static const char udisks_path[] = "/org/freedesktop/UDisks2";
static const char upower_path[] = "/org/freedesktop/UPower";
static const char upower_display_path[] =
    "/org/freedesktop/UPower/devices/DisplayDevice";

static const char rtkit_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.RealtimeKit1'>"
    "    <method name='MakeThreadRealtime'>"
    "      <arg type='t' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='MakeThreadRealtimeWithPID'>"
    "      <arg type='t' direction='in'/>"
    "      <arg type='t' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='MakeThreadHighPriority'>"
    "      <arg type='t' direction='in'/>"
    "      <arg type='i' direction='in'/>"
    "    </method>"
    "    <method name='MakeThreadHighPriorityWithPID'>"
    "      <arg type='t' direction='in'/>"
    "      <arg type='t' direction='in'/>"
    "      <arg type='i' direction='in'/>"
    "    </method>"
    "    <property name='RTTimeUSecMax' type='x' access='read'/>"
    "    <property name='MaxRealtimePriority' type='i' access='read'/>"
    "    <property name='MinNiceLevel' type='i' access='read'/>"
    "  </interface>"
    "</node>";

static const char power_xml[] =
    "<node>"
    "  <interface name='net.hadess.PowerProfiles'>"
    "    <method name='HoldProfile'>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='u' direction='out'/>"
    "    </method>"
    "    <method name='ReleaseProfile'>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <property name='ActiveProfile' type='s' access='read'/>"
    "    <property name='Profiles' type='aa{sv}' access='read'/>"
    "    <property name='Actions' type='as' access='read'/>"
    "    <property name='PerformanceDegraded' type='s' access='read'/>"
    "  </interface>"
    "</node>";

static const char upower_power_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.UPower.PowerProfiles'>"
    "    <method name='HoldProfile'>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='u' direction='out'/>"
    "    </method>"
    "    <method name='ReleaseProfile'>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <property name='ActiveProfile' type='s' access='read'/>"
    "    <property name='Profiles' type='aa{sv}' access='read'/>"
    "    <property name='Actions' type='as' access='read'/>"
    "    <property name='PerformanceDegraded' type='s' access='read'/>"
    "  </interface>"
    "</node>";

static const char object_manager_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
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

static const char upower_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.UPower'>"
    "    <method name='EnumerateDevices'>"
    "      <arg name='devices' type='ao' direction='out'/>"
    "    </method>"
    "    <method name='GetDisplayDevice'>"
    "      <arg name='device' type='o' direction='out'/>"
    "    </method>"
    "    <method name='GetCriticalAction'>"
    "      <arg name='action' type='s' direction='out'/>"
    "    </method>"
    "    <property name='DaemonVersion' type='s' access='read'/>"
    "    <property name='OnBattery' type='b' access='read'/>"
    "    <property name='LidIsClosed' type='b' access='read'/>"
    "    <property name='LidIsPresent' type='b' access='read'/>"
    "    <property name='CriticalAction' type='s' access='read'/>"
    "  </interface>"
    "</node>";

static const char upower_device_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.UPower.Device'>"
    "    <property name='NativePath' type='s' access='read'/>"
    "    <property name='Vendor' type='s' access='read'/>"
    "    <property name='Model' type='s' access='read'/>"
    "    <property name='Serial' type='s' access='read'/>"
    "    <property name='UpdateTime' type='t' access='read'/>"
    "    <property name='Type' type='u' access='read'/>"
    "    <property name='PowerSupply' type='b' access='read'/>"
    "    <property name='Online' type='b' access='read'/>"
    "    <property name='IsPresent' type='b' access='read'/>"
    "    <property name='State' type='u' access='read'/>"
    "    <property name='Percentage' type='d' access='read'/>"
    "    <property name='IconName' type='s' access='read'/>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *rtkit_info;
static GDBusNodeInfo *power_info;
static GDBusNodeInfo *upower_power_info;
static GDBusNodeInfo *object_manager_info;
static GDBusNodeInfo *upower_info;
static GDBusNodeInfo *upower_device_info;

static GVariant *empty_string_array(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    return g_variant_builder_end(&builder);
}

static GVariant *empty_object_path_array(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("ao"));
    return g_variant_builder_end(&builder);
}

static GVariant *empty_managed_objects(void)
{
    GVariantBuilder objects;

    g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
    return g_variant_builder_end(&objects);
}

static GVariant *power_profiles(void)
{
    GVariantBuilder profiles;
    GVariantBuilder profile;

    g_variant_builder_init(&profiles, G_VARIANT_TYPE("aa{sv}"));
    g_variant_builder_init(&profile, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&profile, "{sv}", "Profile",
                          g_variant_new_string("balanced"));
    g_variant_builder_add(&profile, "{sv}", "Driver",
                          g_variant_new_string("xv6"));
    g_variant_builder_add(&profiles, "a{sv}", &profile);
    return g_variant_builder_end(&profiles);
}

static GVariant *handle_get_property(GDBusConnection *connection,
                                     const char *sender,
                                     const char *object_path,
                                     const char *interface_name,
                                     const char *property_name,
                                     GError **error,
                                     void *user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)user_data;

    if (g_strcmp0(interface_name, "org.freedesktop.RealtimeKit1") == 0) {
        if (g_strcmp0(property_name, "RTTimeUSecMax") == 0)
            return g_variant_new_int64(0);
        if (g_strcmp0(property_name, "MaxRealtimePriority") == 0)
            return g_variant_new_int32(0);
        if (g_strcmp0(property_name, "MinNiceLevel") == 0)
            return g_variant_new_int32(0);
    }

    if (g_strcmp0(interface_name, "net.hadess.PowerProfiles") == 0 ||
        g_strcmp0(interface_name, "org.freedesktop.UPower.PowerProfiles") == 0) {
        if (g_strcmp0(property_name, "ActiveProfile") == 0)
            return g_variant_new_string("balanced");
        if (g_strcmp0(property_name, "Profiles") == 0)
            return power_profiles();
        if (g_strcmp0(property_name, "Actions") == 0)
            return empty_string_array();
        if (g_strcmp0(property_name, "PerformanceDegraded") == 0)
            return g_variant_new_string("");
    }

    if (g_strcmp0(interface_name, "org.freedesktop.UPower") == 0) {
        if (g_strcmp0(property_name, "DaemonVersion") == 0)
            return g_variant_new_string("xv6-upower-shim");
        if (g_strcmp0(property_name, "OnBattery") == 0 ||
            g_strcmp0(property_name, "LidIsClosed") == 0 ||
            g_strcmp0(property_name, "LidIsPresent") == 0)
            return g_variant_new_boolean(FALSE);
        if (g_strcmp0(property_name, "CriticalAction") == 0)
            return g_variant_new_string("PowerOff");
    }

    if (g_strcmp0(interface_name, "org.freedesktop.UPower.Device") == 0) {
        if (g_strcmp0(property_name, "NativePath") == 0 ||
            g_strcmp0(property_name, "Vendor") == 0 ||
            g_strcmp0(property_name, "Model") == 0 ||
            g_strcmp0(property_name, "Serial") == 0)
            return g_variant_new_string("");
        if (g_strcmp0(property_name, "UpdateTime") == 0)
            return g_variant_new_uint64(0);
        if (g_strcmp0(property_name, "Type") == 0)
            return g_variant_new_uint32(1);
        if (g_strcmp0(property_name, "PowerSupply") == 0 ||
            g_strcmp0(property_name, "Online") == 0 ||
            g_strcmp0(property_name, "IsPresent") == 0)
            return g_variant_new_boolean(TRUE);
        if (g_strcmp0(property_name, "State") == 0)
            return g_variant_new_uint32(2);
        if (g_strcmp0(property_name, "Percentage") == 0)
            return g_variant_new_double(100.0);
        if (g_strcmp0(property_name, "IconName") == 0)
            return g_variant_new_string("battery-full-charged");
    }

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "xv6 optional service property %s.%s unavailable",
                interface_name, property_name);
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
    (void)parameters;
    (void)user_data;

    if (g_strcmp0(interface_name, "org.freedesktop.DBus.ObjectManager") == 0 &&
        g_strcmp0(method_name, "GetManagedObjects") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@a{oa{sa{sv}}})",
                                      empty_managed_objects()));
        return;
    }

    if (g_strcmp0(interface_name, "org.freedesktop.UPower") == 0 &&
        g_strcmp0(method_name, "EnumerateDevices") == 0) {
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(@ao)", empty_object_path_array()));
        return;
    }
    if (g_strcmp0(interface_name, "org.freedesktop.UPower") == 0 &&
        g_strcmp0(method_name, "GetDisplayDevice") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(o)", upower_display_path));
        return;
    }
    if (g_strcmp0(interface_name, "org.freedesktop.UPower") == 0 &&
        g_strcmp0(method_name, "GetCriticalAction") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(s)", "PowerOff"));
        return;
    }

    if (g_strcmp0(method_name, "HoldProfile") == 0) {
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(u)", 1u));
        return;
    }
    if (g_strcmp0(method_name, "ReleaseProfile") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }
    if (g_str_has_prefix(method_name, "MakeThread")) {
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                              G_DBUS_ERROR_NOT_SUPPORTED,
                                              "Realtime scheduling is unavailable on xv6");
        return;
    }

    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED,
                                          "xv6 optional services shim does not implement %s",
                                          method_name);
}

static const GDBusInterfaceVTable vtable = {
    handle_method_call,
    handle_get_property,
    NULL,
    { 0 }
};

static GDBusConnection *connect_system_bus_with_retry(void)
{
    GError *error = NULL;
    const char *address = getenv("DBUS_SYSTEM_BUS_ADDRESS");

    if (!address || !address[0])
        address = "unix:abstract=xv6_system_bus";

    for (int attempt = 0; attempt < 20; attempt++) {
        GDBusConnection *bus = g_dbus_connection_new_for_address_sync(
            address,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
            NULL, NULL, &error);
        if (bus)
            return bus;
        fprintf(stderr, "xv6-desktop-optional-services-shim: system bus %s: %s\n",
                address, error ? error->message : "unknown error");
        g_clear_error(&error);
        usleep(100000);
    }
    return NULL;
}

static int request_name(GDBusConnection *bus, const char *name)
{
    GError *error = NULL;
    GVariant *reply;
    unsigned int status = 0;

    reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", name, 0),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (!reply) {
        fprintf(stderr, "xv6-desktop-optional-services-shim: RequestName %s: %s\n",
                name, error ? error->message : "unknown error");
        g_clear_error(&error);
        return 0;
    }

    g_variant_get(reply, "(u)", &status);
    g_variant_unref(reply);
    return status == 1 || status == 4;
}

static GDBusNodeInfo *node_info_from_xml(const char *xml, const char *label)
{
    GError *error = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &error);

    if (!info) {
        fprintf(stderr, "xv6-desktop-optional-services-shim: %s introspection: %s\n",
                label, error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    return info;
}

static int register_one(GDBusConnection *bus, const char *path,
                        GDBusNodeInfo *info)
{
    GError *error = NULL;

    for (unsigned int i = 0; info->interfaces[i]; i++) {
        guint id = g_dbus_connection_register_object(
            bus, path, info->interfaces[i], &vtable, NULL, NULL, &error);
        if (!id) {
            fprintf(stderr, "xv6-desktop-optional-services-shim: register %s/%s: %s\n",
                    path, info->interfaces[i]->name,
                    error ? error->message : "unknown error");
            g_clear_error(&error);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    GMainLoop *loop;
    GDBusConnection *bus;

    fprintf(stderr, "xv6-desktop-optional-services-shim: start\n");
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);

    rtkit_info = node_info_from_xml(rtkit_xml, "rtkit");
    power_info = node_info_from_xml(power_xml, "power-profiles");
    upower_power_info = node_info_from_xml(upower_power_xml,
                                           "upower-power-profiles");
    object_manager_info = node_info_from_xml(object_manager_xml,
                                             "object-manager");
    upower_info = node_info_from_xml(upower_xml, "upower");
    upower_device_info = node_info_from_xml(upower_device_xml,
                                            "upower-device");
    if (!rtkit_info || !power_info || !upower_power_info ||
        !object_manager_info || !upower_info || !upower_device_info)
        return 1;

    bus = connect_system_bus_with_retry();
    if (!bus)
        return 1;

    if (!register_one(bus, rtkit_path, rtkit_info) ||
        !register_one(bus, power_path, power_info) ||
        !register_one(bus, upower_power_path, upower_power_info) ||
        !register_one(bus, udisks_path, object_manager_info) ||
        !register_one(bus, upower_path, object_manager_info) ||
        !register_one(bus, upower_path, upower_info) ||
        !register_one(bus, upower_display_path, upower_device_info))
        return 1;

    if (!request_name(bus, "org.freedesktop.RealtimeKit1") ||
        !request_name(bus, "net.hadess.PowerProfiles") ||
        !request_name(bus, "org.freedesktop.UPower.PowerProfiles") ||
        !request_name(bus, "org.freedesktop.UDisks2") ||
        !request_name(bus, "org.freedesktop.UPower"))
        return 1;

    fprintf(stderr,
            "xv6-desktop-optional-services-shim: acquired rtkit, power-profiles, upower-power-profiles, udisks2 and upower\n");
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
