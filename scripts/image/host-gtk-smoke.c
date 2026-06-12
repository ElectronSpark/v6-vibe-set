// Tiny host-built GTK app for the imported-GUI proof lane.
//
// This deliberately avoids GTK development headers so it can be built on
// hosts that have the GTK runtime but not libgtk-3-dev installed.  It links
// against the host GTK shared objects directly and uses only ABI-stable
// pointer-sized GTK/GObject entry points.

#include <stdio.h>
#include <stdlib.h>

typedef void *gpointer;
typedef const void *gconstpointer;
typedef int gboolean;
typedef unsigned int guint;
typedef long gssize;

enum {
    GTK_WINDOW_TOPLEVEL = 0,
    GTK_ORIENTATION_VERTICAL = 1,
    GDK_KEY_Escape = 0xff1b,
};

struct GdkEventKey {
    int type;
    void *window;
    signed char send_event;
    unsigned int time;
    unsigned int state;
    unsigned int keyval;
};

extern int gtk_init_check(int *argc, char ***argv);
extern void gtk_main(void);
extern void gtk_main_quit(void);
extern void *gtk_window_new(int type);
extern void gtk_window_set_title(void *window, const char *title);
extern void gtk_window_set_default_size(void *window, int width, int height);
extern void gtk_window_set_decorated(void *window, int setting);
extern void *gtk_box_new(int orientation, int spacing);
extern void *gtk_label_new(const char *text);
extern void *gtk_entry_new(void);
extern void gtk_entry_set_text(void *entry, const char *text);
extern const char *gtk_entry_get_text(void *entry);
extern void gtk_container_add(void *container, void *widget);
extern void gtk_box_pack_start(void *box, void *child, int expand, int fill,
                               guint padding);
extern void gtk_widget_show_all(void *widget);
extern void gtk_widget_grab_focus(void *widget);
extern void g_signal_connect_data(gpointer instance, const char *detailed_signal,
                                  gpointer c_handler, gpointer data,
                                  gpointer destroy_data, int connect_flags);

static void on_changed(void *entry, gpointer data)
{
    const char *text = gtk_entry_get_text(entry);
    (void)data;
    fprintf(stderr, "host-gtk-smoke: changed text=%s\n", text ? text : "");
    fflush(stderr);
}

static gboolean on_key_press(void *widget, struct GdkEventKey *event,
                             gpointer data)
{
    (void)widget;
    (void)data;
    if (event && event->keyval == GDK_KEY_Escape) {
        fprintf(stderr, "host-gtk-smoke: escape exit\n");
        fflush(stderr);
        gtk_main_quit();
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    void *window;
    void *box;
    void *label;
    void *entry;

    setenv("GDK_BACKEND", "wayland", 0);
    fprintf(stderr, "host-gtk-smoke: start\n");
    fflush(stderr);

    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "host-gtk-smoke: gtk_init_check failed\n");
        return 2;
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(window, "Host GTK Smoke");
    gtk_window_set_default_size(window, 560, 220);
    gtk_window_set_decorated(window, 0);
    g_signal_connect_data(window, "destroy", gtk_main_quit, NULL, NULL, 0);
    g_signal_connect_data(window, "key-press-event", on_key_press, NULL, NULL, 0);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    label = gtk_label_new("Host-built GTK/Wayland smoke");
    entry = gtk_entry_new();
    gtk_entry_set_text(entry, "ready");
    g_signal_connect_data(entry, "changed", on_changed, NULL, NULL, 0);
    g_signal_connect_data(entry, "key-press-event", on_key_press, NULL, NULL, 0);

    gtk_container_add(window, box);
    gtk_box_pack_start(box, label, 0, 0, 0);
    gtk_box_pack_start(box, entry, 0, 0, 0);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(entry);

    fprintf(stderr, "host-gtk-smoke: ready\n");
    fflush(stderr);
    gtk_main();
    fprintf(stderr, "host-gtk-smoke: exited\n");
    fflush(stderr);
    return 0;
}
