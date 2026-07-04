#define _GNU_SOURCE

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NM_STATE_CONNECTED_GLOBAL 70u
#define NM_CONNECTIVITY_FULL 4u
#define NM_DEVICE_TYPE_ETHERNET 1u
#define NM_DEVICE_STATE_ACTIVATED 100u
#define NM_ACTIVE_CONNECTION_STATE_ACTIVATED 2u

static const char manager_path[] = "/org/freedesktop/NetworkManager";
static const char device_path[] = "/org/freedesktop/NetworkManager/Devices/0";
static const char active_path[] = "/org/freedesktop/NetworkManager/ActiveConnection/0";
static const char settings_path[] = "/org/freedesktop/NetworkManager/Settings";
static const char connection_path[] = "/org/freedesktop/NetworkManager/Settings/0";

static const char manager_xml[] =
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
    "  <interface name='org.freedesktop.NetworkManager'>"
    "    <method name='GetDevices'><arg type='ao' direction='out'/></method>"
    "    <method name='GetAllDevices'><arg type='ao' direction='out'/></method>"
    "    <method name='GetDeviceByIpIface'>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='o' direction='out'/>"
    "    </method>"
    "    <method name='CheckConnectivity'><arg type='u' direction='out'/></method>"
    "    <property name='Devices' type='ao' access='read'/>"
    "    <property name='AllDevices' type='ao' access='read'/>"
    "    <property name='ActiveConnections' type='ao' access='read'/>"
    "    <property name='PrimaryConnection' type='o' access='read'/>"
    "    <property name='ActivatingConnection' type='o' access='read'/>"
    "    <property name='Startup' type='b' access='read'/>"
    "    <property name='NetworkingEnabled' type='b' access='readwrite'/>"
    "    <property name='WirelessEnabled' type='b' access='readwrite'/>"
    "    <property name='WirelessHardwareEnabled' type='b' access='read'/>"
    "    <property name='WwanEnabled' type='b' access='readwrite'/>"
    "    <property name='WwanHardwareEnabled' type='b' access='read'/>"
    "    <property name='WimaxEnabled' type='b' access='readwrite'/>"
    "    <property name='WimaxHardwareEnabled' type='b' access='read'/>"
    "    <property name='State' type='u' access='read'/>"
    "    <property name='Connectivity' type='u' access='read'/>"
    "    <property name='ConnectivityCheckAvailable' type='b' access='read'/>"
    "    <property name='ConnectivityCheckEnabled' type='b' access='readwrite'/>"
    "    <property name='Version' type='s' access='read'/>"
    "    <signal name='StateChanged'><arg type='u'/></signal>"
    "  </interface>"
    "</node>";

static const char device_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.NetworkManager.Device'>"
    "    <method name='Disconnect'/>"
    "    <property name='Udi' type='s' access='read'/>"
    "    <property name='Path' type='s' access='read'/>"
    "    <property name='Interface' type='s' access='read'/>"
    "    <property name='IpInterface' type='s' access='read'/>"
    "    <property name='Driver' type='s' access='read'/>"
    "    <property name='DriverVersion' type='s' access='read'/>"
    "    <property name='FirmwareVersion' type='s' access='read'/>"
    "    <property name='Capabilities' type='u' access='read'/>"
    "    <property name='Ip4Address' type='u' access='read'/>"
    "    <property name='State' type='u' access='read'/>"
    "    <property name='StateReason' type='(uu)' access='read'/>"
    "    <property name='ActiveConnection' type='o' access='read'/>"
    "    <property name='DeviceType' type='u' access='read'/>"
    "    <property name='Managed' type='b' access='readwrite'/>"
    "    <property name='Autoconnect' type='b' access='readwrite'/>"
    "    <property name='FirmwareMissing' type='b' access='read'/>"
    "    <property name='NmPluginMissing' type='b' access='read'/>"
    "    <property name='Metered' type='u' access='read'/>"
    "    <property name='LldpNeighbors' type='aa{sv}' access='read'/>"
    "    <property name='Real' type='b' access='read'/>"
    "    <property name='Ip4Config' type='o' access='read'/>"
    "    <property name='Dhcp4Config' type='o' access='read'/>"
    "    <property name='Ip6Config' type='o' access='read'/>"
    "    <property name='Dhcp6Config' type='o' access='read'/>"
    "  </interface>"
    "  <interface name='org.freedesktop.NetworkManager.Device.Wired'>"
    "    <property name='HwAddress' type='s' access='read'/>"
    "    <property name='PermHwAddress' type='s' access='read'/>"
    "    <property name='Speed' type='u' access='read'/>"
    "    <property name='S390Subchannels' type='as' access='read'/>"
    "    <property name='Carrier' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const char active_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.NetworkManager.Connection.Active'>"
    "    <property name='Connection' type='o' access='read'/>"
    "    <property name='SpecificObject' type='o' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Uuid' type='s' access='read'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='Devices' type='ao' access='read'/>"
    "    <property name='State' type='u' access='read'/>"
    "    <property name='StateFlags' type='u' access='read'/>"
    "    <property name='Default' type='b' access='read'/>"
    "    <property name='Default6' type='b' access='read'/>"
    "    <property name='Vpn' type='b' access='read'/>"
    "    <property name='Master' type='o' access='read'/>"
    "  </interface>"
    "</node>";

static const char settings_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.NetworkManager.Settings'>"
    "    <method name='ListConnections'><arg type='ao' direction='out'/></method>"
    "    <property name='Hostname' type='s' access='readwrite'/>"
    "    <property name='CanModify' type='b' access='read'/>"
    "  </interface>"
    "</node>";

static const char connection_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.NetworkManager.Settings.Connection'>"
    "    <method name='GetSettings'><arg type='a{sa{sv}}' direction='out'/></method>"
    "    <method name='GetSecrets'>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='a{sa{sv}}' direction='out'/>"
    "    </method>"
    "    <property name='Unsaved' type='b' access='read'/>"
    "    <property name='Filename' type='s' access='read'/>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *manager_info;
static GDBusNodeInfo *device_info;
static GDBusNodeInfo *active_info;
static GDBusNodeInfo *settings_info;
static GDBusNodeInfo *connection_info;

static const char *guest_iface(void)
{
    return access("/sys/class/net/eth0", F_OK) == 0 ? "eth0" : "net0";
}

static GVariant *object_path_array(const char *path)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("ao"));
    g_variant_builder_add(&builder, "o", path);
    return g_variant_builder_end(&builder);
}

static GVariant *empty_string_array(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    return g_variant_builder_end(&builder);
}

static GVariant *empty_lldp_array(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
    return g_variant_builder_end(&builder);
}

static void add_prop(GVariantBuilder *props, const char *name, GVariant *value)
{
    g_variant_builder_add(props, "{sv}", name, value);
}

static void add_manager_iface(GVariantBuilder *ifaces)
{
    GVariantBuilder props;

    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_prop(&props, "Devices", object_path_array(device_path));
    add_prop(&props, "AllDevices", object_path_array(device_path));
    add_prop(&props, "ActiveConnections", object_path_array(active_path));
    add_prop(&props, "PrimaryConnection", g_variant_new_object_path(active_path));
    add_prop(&props, "ActivatingConnection", g_variant_new_object_path("/"));
    add_prop(&props, "Startup", g_variant_new_boolean(FALSE));
    add_prop(&props, "NetworkingEnabled", g_variant_new_boolean(TRUE));
    add_prop(&props, "WirelessEnabled", g_variant_new_boolean(FALSE));
    add_prop(&props, "WirelessHardwareEnabled", g_variant_new_boolean(FALSE));
    add_prop(&props, "WwanEnabled", g_variant_new_boolean(FALSE));
    add_prop(&props, "WwanHardwareEnabled", g_variant_new_boolean(FALSE));
    add_prop(&props, "WimaxEnabled", g_variant_new_boolean(FALSE));
    add_prop(&props, "WimaxHardwareEnabled", g_variant_new_boolean(FALSE));
    add_prop(&props, "State", g_variant_new_uint32(NM_STATE_CONNECTED_GLOBAL));
    add_prop(&props, "Connectivity", g_variant_new_uint32(NM_CONNECTIVITY_FULL));
    add_prop(&props, "ConnectivityCheckAvailable", g_variant_new_boolean(FALSE));
    add_prop(&props, "ConnectivityCheckEnabled", g_variant_new_boolean(FALSE));
    add_prop(&props, "Version", g_variant_new_string("xv6-networkmanager-shim"));
    g_variant_builder_add(ifaces, "{sa{sv}}", "org.freedesktop.NetworkManager", &props);
}

static void add_device_iface(GVariantBuilder *ifaces)
{
    GVariantBuilder props;
    const char *iface = guest_iface();

    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_prop(&props, "Udi", g_variant_new_string(""));
    add_prop(&props, "Path", g_variant_new_string("/sys/class/net/eth0"));
    add_prop(&props, "Interface", g_variant_new_string(iface));
    add_prop(&props, "IpInterface", g_variant_new_string(iface));
    add_prop(&props, "Driver", g_variant_new_string("xv6"));
    add_prop(&props, "DriverVersion", g_variant_new_string(""));
    add_prop(&props, "FirmwareVersion", g_variant_new_string(""));
    add_prop(&props, "Capabilities", g_variant_new_uint32(0));
    add_prop(&props, "Ip4Address", g_variant_new_uint32(0));
    add_prop(&props, "State", g_variant_new_uint32(NM_DEVICE_STATE_ACTIVATED));
    add_prop(&props, "StateReason", g_variant_new("(uu)", NM_DEVICE_STATE_ACTIVATED, 0u));
    add_prop(&props, "ActiveConnection", g_variant_new_object_path(active_path));
    add_prop(&props, "DeviceType", g_variant_new_uint32(NM_DEVICE_TYPE_ETHERNET));
    add_prop(&props, "Managed", g_variant_new_boolean(TRUE));
    add_prop(&props, "Autoconnect", g_variant_new_boolean(TRUE));
    add_prop(&props, "FirmwareMissing", g_variant_new_boolean(FALSE));
    add_prop(&props, "NmPluginMissing", g_variant_new_boolean(FALSE));
    add_prop(&props, "Metered", g_variant_new_uint32(0));
    add_prop(&props, "LldpNeighbors", empty_lldp_array());
    add_prop(&props, "Real", g_variant_new_boolean(TRUE));
    add_prop(&props, "Ip4Config", g_variant_new_object_path("/"));
    add_prop(&props, "Dhcp4Config", g_variant_new_object_path("/"));
    add_prop(&props, "Ip6Config", g_variant_new_object_path("/"));
    add_prop(&props, "Dhcp6Config", g_variant_new_object_path("/"));
    g_variant_builder_add(ifaces, "{sa{sv}}", "org.freedesktop.NetworkManager.Device", &props);

    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_prop(&props, "HwAddress", g_variant_new_string("52:54:00:12:34:56"));
    add_prop(&props, "PermHwAddress", g_variant_new_string("52:54:00:12:34:56"));
    add_prop(&props, "Speed", g_variant_new_uint32(1000));
    add_prop(&props, "S390Subchannels", empty_string_array());
    add_prop(&props, "Carrier", g_variant_new_boolean(TRUE));
    g_variant_builder_add(ifaces, "{sa{sv}}",
                          "org.freedesktop.NetworkManager.Device.Wired", &props);
}

static void add_active_iface(GVariantBuilder *ifaces)
{
    GVariantBuilder props;

    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_prop(&props, "Connection", g_variant_new_object_path(connection_path));
    add_prop(&props, "SpecificObject", g_variant_new_object_path("/"));
    add_prop(&props, "Id", g_variant_new_string("xv6 wired"));
    add_prop(&props, "Uuid", g_variant_new_string("00000000-0000-4000-8000-000000000001"));
    add_prop(&props, "Type", g_variant_new_string("802-3-ethernet"));
    add_prop(&props, "Devices", object_path_array(device_path));
    add_prop(&props, "State", g_variant_new_uint32(NM_ACTIVE_CONNECTION_STATE_ACTIVATED));
    add_prop(&props, "StateFlags", g_variant_new_uint32(0));
    add_prop(&props, "Default", g_variant_new_boolean(TRUE));
    add_prop(&props, "Default6", g_variant_new_boolean(FALSE));
    add_prop(&props, "Vpn", g_variant_new_boolean(FALSE));
    add_prop(&props, "Master", g_variant_new_object_path("/"));
    g_variant_builder_add(ifaces, "{sa{sv}}",
                          "org.freedesktop.NetworkManager.Connection.Active", &props);
}

static void add_settings_iface(GVariantBuilder *ifaces)
{
    GVariantBuilder props;

    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_prop(&props, "Hostname", g_variant_new_string("xv6"));
    add_prop(&props, "CanModify", g_variant_new_boolean(FALSE));
    g_variant_builder_add(ifaces, "{sa{sv}}",
                          "org.freedesktop.NetworkManager.Settings", &props);
}

static void add_connection_iface(GVariantBuilder *ifaces)
{
    GVariantBuilder props;

    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    add_prop(&props, "Unsaved", g_variant_new_boolean(FALSE));
    add_prop(&props, "Filename", g_variant_new_string(""));
    g_variant_builder_add(ifaces, "{sa{sv}}",
                          "org.freedesktop.NetworkManager.Settings.Connection",
                          &props);
}

static GVariant *property_for(const char *interface_name, const char *property_name)
{
    if (g_strcmp0(interface_name, "org.freedesktop.NetworkManager") == 0) {
        if (g_strcmp0(property_name, "Devices") == 0)
            return object_path_array(device_path);
        if (g_strcmp0(property_name, "AllDevices") == 0)
            return object_path_array(device_path);
        if (g_strcmp0(property_name, "ActiveConnections") == 0)
            return object_path_array(active_path);
        if (g_strcmp0(property_name, "PrimaryConnection") == 0)
            return g_variant_new_object_path(active_path);
        if (g_strcmp0(property_name, "ActivatingConnection") == 0)
            return g_variant_new_object_path("/");
        if (g_strcmp0(property_name, "Startup") == 0)
            return g_variant_new_boolean(FALSE);
        if (g_strcmp0(property_name, "NetworkingEnabled") == 0 ||
            g_strcmp0(property_name, "ConnectivityCheckEnabled") == 0)
            return g_variant_new_boolean(g_strcmp0(property_name, "NetworkingEnabled") == 0);
        if (g_strcmp0(property_name, "WirelessEnabled") == 0 ||
            g_strcmp0(property_name, "WirelessHardwareEnabled") == 0 ||
            g_strcmp0(property_name, "WwanEnabled") == 0 ||
            g_strcmp0(property_name, "WwanHardwareEnabled") == 0 ||
            g_strcmp0(property_name, "WimaxEnabled") == 0 ||
            g_strcmp0(property_name, "WimaxHardwareEnabled") == 0 ||
            g_strcmp0(property_name, "ConnectivityCheckAvailable") == 0)
            return g_variant_new_boolean(FALSE);
        if (g_strcmp0(property_name, "State") == 0)
            return g_variant_new_uint32(NM_STATE_CONNECTED_GLOBAL);
        if (g_strcmp0(property_name, "Connectivity") == 0)
            return g_variant_new_uint32(NM_CONNECTIVITY_FULL);
        if (g_strcmp0(property_name, "Version") == 0)
            return g_variant_new_string("xv6-networkmanager-shim");
    }

    if (g_strcmp0(interface_name, "org.freedesktop.NetworkManager.Device") == 0) {
        const char *iface = guest_iface();
        if (g_strcmp0(property_name, "Udi") == 0 ||
            g_strcmp0(property_name, "DriverVersion") == 0 ||
            g_strcmp0(property_name, "FirmwareVersion") == 0)
            return g_variant_new_string("");
        if (g_strcmp0(property_name, "Path") == 0)
            return g_variant_new_string("/sys/class/net/eth0");
        if (g_strcmp0(property_name, "Interface") == 0 ||
            g_strcmp0(property_name, "IpInterface") == 0)
            return g_variant_new_string(iface);
        if (g_strcmp0(property_name, "Driver") == 0)
            return g_variant_new_string("xv6");
        if (g_strcmp0(property_name, "Capabilities") == 0 ||
            g_strcmp0(property_name, "Ip4Address") == 0 ||
            g_strcmp0(property_name, "Metered") == 0)
            return g_variant_new_uint32(0);
        if (g_strcmp0(property_name, "State") == 0)
            return g_variant_new_uint32(NM_DEVICE_STATE_ACTIVATED);
        if (g_strcmp0(property_name, "StateReason") == 0)
            return g_variant_new("(uu)", NM_DEVICE_STATE_ACTIVATED, 0u);
        if (g_strcmp0(property_name, "ActiveConnection") == 0)
            return g_variant_new_object_path(active_path);
        if (g_strcmp0(property_name, "DeviceType") == 0)
            return g_variant_new_uint32(NM_DEVICE_TYPE_ETHERNET);
        if (g_strcmp0(property_name, "Managed") == 0 ||
            g_strcmp0(property_name, "Autoconnect") == 0 ||
            g_strcmp0(property_name, "Real") == 0)
            return g_variant_new_boolean(TRUE);
        if (g_strcmp0(property_name, "FirmwareMissing") == 0 ||
            g_strcmp0(property_name, "NmPluginMissing") == 0)
            return g_variant_new_boolean(FALSE);
        if (g_strcmp0(property_name, "LldpNeighbors") == 0)
            return empty_lldp_array();
        if (g_strcmp0(property_name, "Ip4Config") == 0 ||
            g_strcmp0(property_name, "Dhcp4Config") == 0 ||
            g_strcmp0(property_name, "Ip6Config") == 0 ||
            g_strcmp0(property_name, "Dhcp6Config") == 0)
            return g_variant_new_object_path("/");
    }

    if (g_strcmp0(interface_name,
                  "org.freedesktop.NetworkManager.Device.Wired") == 0) {
        if (g_strcmp0(property_name, "HwAddress") == 0 ||
            g_strcmp0(property_name, "PermHwAddress") == 0)
            return g_variant_new_string("52:54:00:12:34:56");
        if (g_strcmp0(property_name, "Speed") == 0)
            return g_variant_new_uint32(1000);
        if (g_strcmp0(property_name, "S390Subchannels") == 0)
            return empty_string_array();
        if (g_strcmp0(property_name, "Carrier") == 0)
            return g_variant_new_boolean(TRUE);
    }

    if (g_strcmp0(interface_name,
                  "org.freedesktop.NetworkManager.Connection.Active") == 0) {
        if (g_strcmp0(property_name, "Connection") == 0)
            return g_variant_new_object_path(connection_path);
        if (g_strcmp0(property_name, "SpecificObject") == 0 ||
            g_strcmp0(property_name, "Master") == 0)
            return g_variant_new_object_path("/");
        if (g_strcmp0(property_name, "Id") == 0)
            return g_variant_new_string("xv6 wired");
        if (g_strcmp0(property_name, "Uuid") == 0)
            return g_variant_new_string("00000000-0000-4000-8000-000000000001");
        if (g_strcmp0(property_name, "Type") == 0)
            return g_variant_new_string("802-3-ethernet");
        if (g_strcmp0(property_name, "Devices") == 0)
            return object_path_array(device_path);
        if (g_strcmp0(property_name, "State") == 0)
            return g_variant_new_uint32(NM_ACTIVE_CONNECTION_STATE_ACTIVATED);
        if (g_strcmp0(property_name, "StateFlags") == 0)
            return g_variant_new_uint32(0);
        if (g_strcmp0(property_name, "Default") == 0)
            return g_variant_new_boolean(TRUE);
        if (g_strcmp0(property_name, "Default6") == 0 ||
            g_strcmp0(property_name, "Vpn") == 0)
            return g_variant_new_boolean(FALSE);
    }

    if (g_strcmp0(interface_name,
                  "org.freedesktop.NetworkManager.Settings") == 0) {
        if (g_strcmp0(property_name, "Hostname") == 0)
            return g_variant_new_string("xv6");
        if (g_strcmp0(property_name, "CanModify") == 0)
            return g_variant_new_boolean(FALSE);
    }

    if (g_strcmp0(interface_name,
                  "org.freedesktop.NetworkManager.Settings.Connection") == 0) {
        if (g_strcmp0(property_name, "Unsaved") == 0)
            return g_variant_new_boolean(FALSE);
        if (g_strcmp0(property_name, "Filename") == 0)
            return g_variant_new_string("");
    }

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
    (void)user_data;
    value = property_for(interface_name, property_name);
    if (value)
        return value;

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "xv6-networkmanager-shim property %s.%s unavailable",
                interface_name, property_name);
    return NULL;
}

static gboolean handle_set_property(GDBusConnection *connection,
                                    const char *sender,
                                    const char *object_path,
                                    const char *interface_name,
                                    const char *property_name,
                                    GVariant *value,
                                    GError **error,
                                    void *user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)property_name;
    (void)value;
    (void)error;
    (void)user_data;
    return TRUE;
}

static GVariant *connection_settings(void)
{
    GVariantBuilder settings;
    GVariantBuilder section;

    g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sa{sv}}"));

    g_variant_builder_init(&section, G_VARIANT_TYPE("a{sv}"));
    add_prop(&section, "id", g_variant_new_string("xv6 wired"));
    add_prop(&section, "uuid",
             g_variant_new_string("00000000-0000-4000-8000-000000000001"));
    add_prop(&section, "type", g_variant_new_string("802-3-ethernet"));
    g_variant_builder_add(&settings, "{sa{sv}}", "connection", &section);

    g_variant_builder_init(&section, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&settings, "{sa{sv}}", "802-3-ethernet", &section);

    g_variant_builder_init(&section, G_VARIANT_TYPE("a{sv}"));
    add_prop(&section, "method", g_variant_new_string("auto"));
    g_variant_builder_add(&settings, "{sa{sv}}", "ipv4", &section);

    g_variant_builder_init(&section, G_VARIANT_TYPE("a{sv}"));
    add_prop(&section, "method", g_variant_new_string("ignore"));
    g_variant_builder_add(&settings, "{sa{sv}}", "ipv6", &section);

    return g_variant_builder_end(&settings);
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
    (void)interface_name;
    (void)parameters;
    (void)user_data;

    if (g_strcmp0(object_path, manager_path) == 0 &&
        g_strcmp0(method_name, "GetManagedObjects") == 0) {
        GVariantBuilder objects;
        GVariantBuilder ifaces;

        g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        add_manager_iface(&ifaces);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", manager_path, &ifaces);
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        add_device_iface(&ifaces);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", device_path, &ifaces);
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        add_active_iface(&ifaces);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", active_path, &ifaces);
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        add_settings_iface(&ifaces);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", settings_path, &ifaces);
        g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
        add_connection_iface(&ifaces);
        g_variant_builder_add(&objects, "{oa{sa{sv}}}", connection_path, &ifaces);
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(a{oa{sa{sv}}})", &objects));
        return;
    }

    if (g_strcmp0(object_path, manager_path) == 0 &&
        (g_strcmp0(method_name, "GetDevices") == 0 ||
         g_strcmp0(method_name, "GetAllDevices") == 0)) {
        GVariantBuilder devices;

        g_variant_builder_init(&devices, G_VARIANT_TYPE("ao"));
        g_variant_builder_add(&devices, "o", device_path);
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(ao)", &devices));
        return;
    }

    if (g_strcmp0(object_path, manager_path) == 0 &&
        g_strcmp0(method_name, "GetDeviceByIpIface") == 0) {
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(o)", device_path));
        return;
    }

    if (g_strcmp0(object_path, manager_path) == 0 &&
        g_strcmp0(method_name, "CheckConnectivity") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(u)", NM_CONNECTIVITY_FULL));
        return;
    }

    if (g_strcmp0(object_path, settings_path) == 0 &&
        g_strcmp0(method_name, "ListConnections") == 0) {
        GVariantBuilder connections;

        g_variant_builder_init(&connections, G_VARIANT_TYPE("ao"));
        g_variant_builder_add(&connections, "o", connection_path);
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(ao)", &connections));
        return;
    }

    if (g_strcmp0(object_path, connection_path) == 0 &&
        g_strcmp0(method_name, "GetSettings") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@a{sa{sv}})", connection_settings()));
        return;
    }

    if (g_strcmp0(object_path, connection_path) == 0 &&
        g_strcmp0(method_name, "GetSecrets") == 0) {
        GVariantBuilder secrets;

        g_variant_builder_init(&secrets, G_VARIANT_TYPE("a{sa{sv}}"));
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(a{sa{sv}})", &secrets));
        return;
    }

    if (g_strcmp0(method_name, "Disconnect") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED,
                                          "xv6-networkmanager-shim does not implement %s on %s",
                                          method_name, object_path);
}

static const GDBusInterfaceVTable vtable = {
    handle_method_call,
    handle_get_property,
    handle_set_property,
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

        fprintf(stderr, "xv6-networkmanager-shim: system bus %s: %s\n",
                address, error ? error->message : "unknown error");
        g_clear_error(&error);
        usleep(100000);
    }

    return NULL;
}

static int request_name(GDBusConnection *bus)
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
        g_variant_new("(su)", "org.freedesktop.NetworkManager", 0),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "xv6-networkmanager-shim: RequestName: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return 0;
    }

    g_variant_get(reply, "(u)", &status);
    g_variant_unref(reply);
    if (status != 1 && status != 4) {
        fprintf(stderr, "xv6-networkmanager-shim: RequestName status=%u\n",
                status);
        return 0;
    }

    return 1;
}

static GDBusNodeInfo *node_info_from_xml(const char *xml, const char *label)
{
    GError *error = NULL;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &error);

    if (!info) {
        fprintf(stderr, "xv6-networkmanager-shim: %s introspection: %s\n",
                label, error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    return info;
}

static int register_object(GDBusConnection *bus, const char *path,
                           GDBusNodeInfo *info)
{
    GError *error = NULL;

    for (unsigned int i = 0; info->interfaces[i]; i++) {
        guint id = g_dbus_connection_register_object(
            bus, path, info->interfaces[i], &vtable, NULL, NULL, &error);
        if (!id) {
            fprintf(stderr, "xv6-networkmanager-shim: register %s/%s: %s\n",
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

    fprintf(stderr, "xv6-networkmanager-shim: start\n");
    setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:abstract=xv6_system_bus", 0);

    manager_info = node_info_from_xml(manager_xml, "manager");
    device_info = node_info_from_xml(device_xml, "device");
    active_info = node_info_from_xml(active_xml, "active-connection");
    settings_info = node_info_from_xml(settings_xml, "settings");
    connection_info = node_info_from_xml(connection_xml, "connection");
    if (!manager_info || !device_info || !active_info ||
        !settings_info || !connection_info)
        return 1;

    bus = connect_system_bus_with_retry();
    if (!bus) {
        fprintf(stderr, "xv6-networkmanager-shim: system bus unavailable\n");
        return 1;
    }

    if (!register_object(bus, manager_path, manager_info) ||
        !register_object(bus, device_path, device_info) ||
        !register_object(bus, active_path, active_info) ||
        !register_object(bus, settings_path, settings_info) ||
        !register_object(bus, connection_path, connection_info))
        return 1;

    if (!request_name(bus))
        return 1;

    fprintf(stderr, "xv6-networkmanager-shim: acquired org.freedesktop.NetworkManager iface=%s\n",
            guest_iface());
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
