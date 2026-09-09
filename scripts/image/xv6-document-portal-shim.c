#define _GNU_SOURCE

#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char documents_name[] = "org.freedesktop.portal.Documents";
static const char documents_path[] = "/org/freedesktop/portal/documents";
static const char mount_point[] = "/run/user/0/doc/";

static const char documents_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.portal.Documents'>"
    "    <property name='version' type='u' access='read'/>"
    "    <method name='GetMountPoint'>"
    "      <arg type='ay' name='path' direction='out'/>"
    "    </method>"
    "    <method name='Add'>"
    "      <annotation name='org.gtk.GDBus.C.UnixFD' value='true'/>"
    "      <arg type='h' name='o_path_fd' direction='in'/>"
    "      <arg type='b' name='reuse_existing' direction='in'/>"
    "      <arg type='b' name='persistent' direction='in'/>"
    "      <arg type='s' name='doc_id' direction='out'/>"
    "    </method>"
    "    <method name='AddNamed'>"
    "      <annotation name='org.gtk.GDBus.C.UnixFD' value='true'/>"
    "      <arg type='h' name='o_path_parent_fd' direction='in'/>"
    "      <arg type='ay' name='filename' direction='in'/>"
    "      <arg type='b' name='reuse_existing' direction='in'/>"
    "      <arg type='b' name='persistent' direction='in'/>"
    "      <arg type='s' name='doc_id' direction='out'/>"
    "    </method>"
    "    <method name='AddFull'>"
    "      <annotation name='org.gtk.GDBus.C.UnixFD' value='true'/>"
    "      <arg type='ah' name='o_path_fds' direction='in'/>"
    "      <arg type='u' name='flags' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='as' name='permissions' direction='in'/>"
    "      <arg type='as' name='doc_ids' direction='out'/>"
    "      <arg type='a{sv}' name='extra_out' direction='out'/>"
    "    </method>"
    "    <method name='AddNamedFull'>"
    "      <annotation name='org.gtk.GDBus.C.UnixFD' value='true'/>"
    "      <arg type='h' name='o_path_fd' direction='in'/>"
    "      <arg type='ay' name='filename' direction='in'/>"
    "      <arg type='u' name='flags' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='as' name='permissions' direction='in'/>"
    "      <arg type='s' name='doc_id' direction='out'/>"
    "      <arg type='a{sv}' name='extra_out' direction='out'/>"
    "    </method>"
    "    <method name='GrantPermissions'>"
    "      <arg type='s' name='doc_id' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='as' name='permissions' direction='in'/>"
    "    </method>"
    "    <method name='RevokePermissions'>"
    "      <arg type='s' name='doc_id' direction='in'/>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='as' name='permissions' direction='in'/>"
    "    </method>"
    "    <method name='Delete'>"
    "      <arg type='s' name='doc_id' direction='in'/>"
    "    </method>"
    "    <method name='Lookup'>"
    "      <arg type='ay' name='filename' direction='in'/>"
    "      <arg type='s' name='doc_id' direction='out'/>"
    "    </method>"
    "    <method name='Info'>"
    "      <arg type='s' name='doc_id' direction='in'/>"
    "      <arg type='ay' name='path' direction='out'/>"
    "      <arg type='a{sas}' name='apps' direction='out'/>"
    "    </method>"
    "    <method name='List'>"
    "      <arg type='s' name='app_id' direction='in'/>"
    "      <arg type='a{say}' name='docs' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *documents_info;

static GVariant *empty_variant_dict(void)
{
    GVariantBuilder builder;

    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    return g_variant_builder_end(&builder);
}

static void complete_add_full(GVariant *parameters,
                              GDBusMethodInvocation *invocation)
{
    GVariantBuilder doc_ids;
    GVariantBuilder extra;
    GVariant *handles;
    gsize n_handles;

    handles = g_variant_get_child_value(parameters, 0);
    n_handles = g_variant_n_children(handles);

    g_variant_builder_init(&doc_ids, G_VARIANT_TYPE("as"));
    for (gsize i = 0; i < n_handles; i++)
        g_variant_builder_add(&doc_ids, "s", "");

    g_variant_builder_init(&extra, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&extra, "{sv}", "mountpoint",
                          g_variant_new_bytestring(mount_point));

    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(asa{sv})", &doc_ids, &extra));
    g_variant_unref(handles);
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

    if (g_strcmp0(interface_name, documents_name) == 0 &&
        g_strcmp0(property_name, "version") == 0)
        return g_variant_new_uint32(3);

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                "xv6 document portal property %s.%s unavailable",
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
    (void)user_data;

    if (g_strcmp0(method_name, "GetMountPoint") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@ay)",
                                      g_variant_new_bytestring(mount_point)));
        return;
    }

    if (g_strcmp0(method_name, "AddFull") == 0) {
        complete_add_full(parameters, invocation);
        return;
    }

    if (g_strcmp0(method_name, "Add") == 0 ||
        g_strcmp0(method_name, "AddNamed") == 0) {
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)", ""));
        return;
    }

    if (g_strcmp0(method_name, "AddNamedFull") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(s@a{sv})", "", empty_variant_dict()));
        return;
    }

    if (g_strcmp0(method_name, "Lookup") == 0) {
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)", ""));
        return;
    }

    if (g_strcmp0(method_name, "Info") == 0) {
        GVariantBuilder apps;

        g_variant_builder_init(&apps, G_VARIANT_TYPE("a{sas}"));
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@aya{sas})",
                                      g_variant_new_bytestring(""), &apps));
        return;
    }

    if (g_strcmp0(method_name, "List") == 0) {
        GVariantBuilder docs;

        g_variant_builder_init(&docs, G_VARIANT_TYPE("a{say}"));
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(a{say})", &docs));
        return;
    }

    if (g_strcmp0(method_name, "GrantPermissions") == 0 ||
        g_strcmp0(method_name, "RevokePermissions") == 0 ||
        g_strcmp0(method_name, "Delete") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    g_dbus_method_invocation_return_error(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
        "xv6 document portal shim does not implement %s", method_name);
}

static const GDBusInterfaceVTable documents_vtable = {
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

    for (int attempt = 0; attempt < 20; attempt++) {
        GDBusConnection *bus = g_dbus_connection_new_for_address_sync(
            address,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
            G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
            NULL, NULL, &error);
        if (bus)
            return bus;

        fprintf(stderr, "xv6-document-portal-shim: session bus %s: %s\n",
                address, error ? error->message : "unknown error");
        g_clear_error(&error);
        usleep(100000);
    }

    return NULL;
}

static int request_documents_name(GDBusConnection *bus)
{
    GError *error = NULL;
    GVariant *reply;
    unsigned int status = 0;

    reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", documents_name, 0),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    if (!reply) {
        fprintf(stderr, "xv6-document-portal-shim: RequestName: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return 0;
    }

    g_variant_get(reply, "(u)", &status);
    g_variant_unref(reply);
    if (status != 1 && status != 4) {
        fprintf(stderr, "xv6-document-portal-shim: RequestName status=%u\n",
                status);
        return 0;
    }

    return 1;
}

int main(void)
{
    GMainLoop *loop;
    GError *error = NULL;
    GDBusConnection *bus;
    guint reg_id;

    fprintf(stderr, "xv6-document-portal-shim: start\n");
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:abstract=xv6_session_bus", 0);
    g_mkdir_with_parents(mount_point, 0700);

    documents_info = g_dbus_node_info_new_for_xml(documents_xml, &error);
    if (!documents_info) {
        fprintf(stderr, "xv6-document-portal-shim: introspection: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return 1;
    }

    bus = connect_session_bus_with_retry();
    if (!bus) {
        fprintf(stderr, "xv6-document-portal-shim: session bus unavailable\n");
        return 1;
    }

    reg_id = g_dbus_connection_register_object(
        bus, documents_path, documents_info->interfaces[0],
        &documents_vtable, NULL, NULL, &error);
    if (!reg_id) {
        fprintf(stderr, "xv6-document-portal-shim: register %s: %s\n",
                documents_path, error ? error->message : "unknown error");
        g_clear_error(&error);
        return 1;
    }

    if (!request_documents_name(bus))
        return 1;

    fprintf(stderr, "xv6-document-portal-shim: acquired %s mount=%s\n",
            documents_name, mount_point);
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
