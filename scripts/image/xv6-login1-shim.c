#define _GNU_SOURCE

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static const char manager_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.login1.Manager'>"
    "    <method name='CanSuspend'><arg type='s' direction='out'/></method>"
    "    <method name='CanHibernate'><arg type='s' direction='out'/></method>"
    "    <method name='CanPowerOff'><arg type='s' direction='out'/></method>"
    "    <method name='CanReboot'><arg type='s' direction='out'/></method>"
    "    <method name='Inhibit'>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='h' direction='out'/>"
    "    </method>"
    "    <method name='GetSessionByPID'>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='o' direction='out'/>"
    "    </method>"
    "    <method name='GetSession'>"
    "      <arg type='s' direction='in'/>"
    "      <arg type='o' direction='out'/>"
    "    </method>"
    "    <method name='GetUser'>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='o' direction='out'/>"
    "    </method>"
    "    <signal name='PrepareForSleep'><arg type='b'/></signal>"
    "    <signal name='PrepareForShutdown'><arg type='b'/></signal>"
    "  </interface>"
    "</node>";

static const char session_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.login1.Session'>"
    "    <method name='TakeControl'><arg type='b' direction='in'/></method>"
    "    <method name='ReleaseControl'/>"
    "    <method name='TakeDevice'>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='h' direction='out'/>"
    "      <arg type='b' direction='out'/>"
    "    </method>"
    "    <method name='ReleaseDevice'>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='PauseDeviceComplete'>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='Activate'/>"
    "    <method name='Lock'/>"
    "    <method name='Unlock'/>"
    "    <method name='SetIdleHint'><arg type='b' direction='in'/></method>"
    "    <property name='Active' type='b' access='read'/>"
    "    <property name='Id' type='s' access='read'/>"
    "    <property name='Remote' type='b' access='read'/>"
    "    <property name='State' type='s' access='read'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='Class' type='s' access='read'/>"
    "    <property name='Name' type='s' access='read'/>"
    "    <property name='User' type='(uo)' access='read'/>"
    "    <property name='Seat' type='(so)' access='read'/>"
    "    <property name='Display' type='s' access='read'/>"
    "    <property name='Desktop' type='s' access='read'/>"
    "    <property name='VTNr' type='u' access='read'/>"
    "  </interface>"
    "</node>";

static GDBusNodeInfo *manager_info;
static GDBusNodeInfo *session_info;

static const char *node_for_dev(unsigned int major_num, unsigned int minor_num)
{
    if (major_num == 226 && minor_num == 0)
        return "/dev/dri/card0";
    if (major_num == 226 && minor_num == 128)
        return "/dev/dri/renderD128";
    if (major_num == 13 && minor_num == 64)
        return "/dev/input/event0";
    if (major_num == 13 && minor_num == 65)
        return "/dev/input/event1";
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

    if (g_strcmp0(method_name, "CanSuspend") == 0 ||
        g_strcmp0(method_name, "CanHibernate") == 0 ||
        g_strcmp0(method_name, "CanPowerOff") == 0 ||
        g_strcmp0(method_name, "CanReboot") == 0) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", "no"));
        return;
    }
    if (g_strcmp0(method_name, "Inhibit") == 0) {
        GUnixFDList *fd_list;
        GError *error = NULL;
        int pipefd[2];
        int fd_index;

        if (pipe2(pipefd, O_CLOEXEC) < 0) {
            g_dbus_method_invocation_return_error(invocation, G_IO_ERROR,
                                                  g_io_error_from_errno(errno),
                                                  "pipe2: %s", strerror(errno));
            return;
        }

        fd_list = g_unix_fd_list_new();
        fd_index = g_unix_fd_list_append(fd_list, pipefd[0], &error);
        close(pipefd[0]);
        close(pipefd[1]);
        if (fd_index < 0) {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
            g_object_unref(fd_list);
            return;
        }

        g_dbus_method_invocation_return_value_with_unix_fd_list(
            invocation, g_variant_new("(h)", fd_index), fd_list);
        g_object_unref(fd_list);
        return;
    }
    if (g_strcmp0(method_name, "GetSessionByPID") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(o)", "/org/freedesktop/login1/session/_31"));
        return;
    }
    if (g_strcmp0(method_name, "GetSession") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(o)", "/org/freedesktop/login1/session/_31"));
        return;
    }
    if (g_strcmp0(method_name, "GetUser") == 0) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(o)", "/org/freedesktop/login1/user/_0"));
        return;
    }

    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED,
                                          "xv6-login1-shim does not implement %s",
                                          method_name);
}

static void handle_session_method_call(GDBusConnection *connection,
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

    if (g_strcmp0(method_name, "TakeDevice") == 0) {
        unsigned int major_num;
        unsigned int minor_num;
        const char *node;
        GUnixFDList *fd_list;
        GError *error = NULL;
        int fd;
        int fd_index;

        g_variant_get(parameters, "(uu)", &major_num, &minor_num);
        node = node_for_dev(major_num, minor_num);
        if (!node) {
            g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                                  G_DBUS_ERROR_NOT_SUPPORTED,
                                                  "unsupported device %u:%u",
                                                  major_num, minor_num);
            return;
        }

        fd = open(node, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            g_dbus_method_invocation_return_error(invocation, G_IO_ERROR,
                                                  g_io_error_from_errno(errno),
                                                  "open %s: %s", node,
                                                  strerror(errno));
            return;
        }

        fd_list = g_unix_fd_list_new();
        fd_index = g_unix_fd_list_append(fd_list, fd, &error);
        close(fd);
        if (fd_index < 0) {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
            g_object_unref(fd_list);
            return;
        }

        g_dbus_method_invocation_return_value_with_unix_fd_list(
            invocation, g_variant_new("(hb)", fd_index, FALSE), fd_list);
        g_object_unref(fd_list);
        return;
    }

    if (g_strcmp0(method_name, "TakeControl") == 0 ||
        g_strcmp0(method_name, "ReleaseControl") == 0 ||
        g_strcmp0(method_name, "ReleaseDevice") == 0 ||
        g_strcmp0(method_name, "PauseDeviceComplete") == 0 ||
        g_strcmp0(method_name, "Activate") == 0 ||
        g_strcmp0(method_name, "Lock") == 0 ||
        g_strcmp0(method_name, "Unlock") == 0 ||
        g_strcmp0(method_name, "SetIdleHint") == 0) {
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_NOT_SUPPORTED,
                                          "xv6-login1-shim does not implement %s",
                                          method_name);
}

static GVariant *handle_session_get_property(GDBusConnection *connection,
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
    (void)interface_name;
    (void)error;
    (void)user_data;

    if (g_strcmp0(property_name, "Active") == 0)
        return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "Id") == 0)
        return g_variant_new_string("1");
    if (g_strcmp0(property_name, "Remote") == 0)
        return g_variant_new_boolean(FALSE);
    if (g_strcmp0(property_name, "State") == 0)
        return g_variant_new_string("active");
    if (g_strcmp0(property_name, "Type") == 0)
        return g_variant_new_string("wayland");
    if (g_strcmp0(property_name, "Class") == 0)
        return g_variant_new_string("user");
    if (g_strcmp0(property_name, "Name") == 0)
        return g_variant_new_string("root");
    if (g_strcmp0(property_name, "User") == 0)
        return g_variant_new("(uo)", 0, "/org/freedesktop/login1/user/_0");
    if (g_strcmp0(property_name, "Seat") == 0)
        return g_variant_new("(so)", "seat0", "/org/freedesktop/login1/seat/seat0");
    if (g_strcmp0(property_name, "Display") == 0)
        return g_variant_new_string("");
    if (g_strcmp0(property_name, "Desktop") == 0)
        return g_variant_new_string("KDE");
    if (g_strcmp0(property_name, "VTNr") == 0)
        return g_variant_new_uint32(1);
    return NULL;
}

static const GDBusInterfaceVTable manager_vtable = {
    handle_method_call,
    NULL,
    NULL,
    { 0 }
};

static const GDBusInterfaceVTable session_vtable = {
    handle_session_method_call,
    handle_session_get_property,
    NULL,
    { 0 }
};

static GDBusConnection *connect_system_bus_with_retry(void)
{
    GError *error = NULL;

    for (int attempt = 0; attempt < 100; attempt++) {
        GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus)
            return bus;

        if (attempt == 0 || attempt == 19 || attempt == 49 || attempt == 99)
            fprintf(stderr, "xv6-login1-shim: waiting for system bus: %s\n",
                    error ? error->message : "unknown error");
        g_clear_error(&error);
        usleep(100000);
    }

    return NULL;
}

static int request_login1_name(GDBusConnection *bus)
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
        g_variant_new("(su)", "org.freedesktop.login1", 0),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (!reply) {
        fprintf(stderr, "xv6-login1-shim: RequestName: %s\n", error->message);
        g_clear_error(&error);
        return 0;
    }

    g_variant_get(reply, "(u)", &status);
    g_variant_unref(reply);
    if (status != 1 && status != 4) {
        fprintf(stderr, "xv6-login1-shim: RequestName status=%u\n", status);
        return 0;
    }

    return 1;
}

static void write_ready_file(void)
{
    FILE *file = fopen("/tmp/xv6-login1-ready", "w");
    if (!file) {
        fprintf(stderr, "xv6-login1-shim: ready marker: %s\n", strerror(errno));
        return;
    }
    fputs("ready\n", file);
    fclose(file);
}

int main(void)
{
    GMainLoop *loop;
    GError *error = NULL;
    GDBusConnection *bus;
    guint manager_reg_id;
    guint session_reg_id;

    fprintf(stderr, "xv6-login1-shim: start\n");
    unlink("/tmp/xv6-login1-ready");
    manager_info = g_dbus_node_info_new_for_xml(manager_xml, &error);
    if (!manager_info) {
        fprintf(stderr, "xv6-login1-shim: manager introspection: %s\n", error->message);
        return 1;
    }
    session_info = g_dbus_node_info_new_for_xml(session_xml, &error);
    if (!session_info) {
        fprintf(stderr, "xv6-login1-shim: session introspection: %s\n", error->message);
        return 1;
    }

    bus = connect_system_bus_with_retry();
    if (!bus) {
        fprintf(stderr, "xv6-login1-shim: system bus unavailable\n");
        return 1;
    }

    manager_reg_id = g_dbus_connection_register_object(
        bus, "/org/freedesktop/login1", manager_info->interfaces[0],
        &manager_vtable, NULL, NULL, &error);
    if (!manager_reg_id) {
        fprintf(stderr, "xv6-login1-shim: register manager: %s\n", error->message);
        return 1;
    }
    session_reg_id = g_dbus_connection_register_object(
        bus, "/org/freedesktop/login1/session/_31", session_info->interfaces[0],
        &session_vtable, NULL, NULL, &error);
    if (!session_reg_id) {
        fprintf(stderr, "xv6-login1-shim: register session: %s\n", error->message);
        return 1;
    }

    if (!request_login1_name(bus))
        return 1;

    write_ready_file();
    fprintf(stderr, "xv6-login1-shim: acquired org.freedesktop.login1\n");

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
