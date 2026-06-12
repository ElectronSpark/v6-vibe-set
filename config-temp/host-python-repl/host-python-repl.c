#define _GNU_SOURCE
#include <Python.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "xv6_draw.h"
#include "xv6_present_buffer.h"

#define WIN_W 760
#define WIN_H 480
#define MAX_LINES 96
#define LINE_LEN 112
#define INPUT_LEN 256

struct app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct wl_surface *surface;
    struct xdg_wm_base *wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct xv6_present_buffer buffer;
    PyObject *run_func;
    char lines[MAX_LINES][LINE_LEN];
    int line_count;
    char input[INPUT_LEN];
    size_t input_len;
    uint32_t mods;
    int configured;
    int dirty;
    int running;
    int python_ready;
};

static void append_line(struct app *app, const char *line)
{
    if (app->line_count == MAX_LINES) {
        memmove(app->lines, app->lines + 1,
                sizeof(app->lines[0]) * (MAX_LINES - 1));
        app->line_count--;
    }
    snprintf(app->lines[app->line_count], LINE_LEN, "%s", line ? line : "");
    app->line_count++;
    app->dirty = 1;
}

static void append_wrapped(struct app *app, const char *text)
{
    char line[LINE_LEN];
    size_t len = 0;

    if (!text || !text[0]) {
        append_line(app, "");
        return;
    }

    for (const char *p = text; ; p++) {
        char ch = *p;

        if (ch == '\r')
            continue;
        if (ch == '\n' || ch == '\0' || len + 1 >= sizeof(line)) {
            line[len] = '\0';
            append_line(app, line);
            len = 0;
            if (ch == '\0')
                break;
            if (ch != '\n')
                line[len++] = ch;
            continue;
        }
        line[len++] = ch;
    }
}

static int add_python_path(PyConfig *config, const wchar_t *path)
{
    PyStatus status = PyWideStringList_Append(&config->module_search_paths,
                                              path);

    return PyStatus_Exception(status) ? -1 : 0;
}

static int init_python(struct app *app)
{
    static const char *bootstrap =
        "import sys, io, traceback\n"
        "def __xv6_host_gui_repl_run(src):\n"
        "    oldout, olderr = sys.stdout, sys.stderr\n"
        "    buf = io.StringIO()\n"
        "    sys.stdout = sys.stderr = buf\n"
        "    try:\n"
        "        try:\n"
        "            code = compile(src, '<host-gui-repl>', 'eval')\n"
        "        except SyntaxError:\n"
        "            code = compile(src, '<host-gui-repl>', 'exec')\n"
        "            exec(code, globals())\n"
        "        else:\n"
        "            result = eval(code, globals())\n"
        "            if result is not None:\n"
        "                print(repr(result))\n"
        "    except Exception:\n"
        "        traceback.print_exc()\n"
        "    finally:\n"
        "        sys.stdout, sys.stderr = oldout, olderr\n"
        "    return buf.getvalue()\n";
    PyConfig config;
    PyStatus status;
    PyObject *main_mod;
    PyObject *globals;
    PyObject *compiled;

    fprintf(stderr, "host-python-repl: initializing Python\n");
    PyConfig_InitPythonConfig(&config);
    status = PyConfig_SetString(&config, &config.home, L"/");
    if (PyStatus_Exception(status))
        goto fail;
    status = PyConfig_SetString(&config, &config.program_name,
                                L"host-python-repl");
    if (PyStatus_Exception(status))
        goto fail;
    config.site_import = 0;
    config.user_site_directory = 0;
    config.module_search_paths_set = 1;
    if (add_python_path(&config, L"/lib/python312.zip") != 0 ||
        add_python_path(&config, L"/lib/python3.12") != 0 ||
        add_python_path(&config, L"/lib/python3.12/lib-dynload") != 0)
        goto fail;

    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status))
        goto fail;
    PyConfig_Clear(&config);

    main_mod = PyImport_AddModule("__main__");
    globals = main_mod ? PyModule_GetDict(main_mod) : NULL;
    compiled = globals ? PyRun_String(bootstrap, Py_file_input, globals,
                                      globals) : NULL;
    if (!compiled) {
        PyErr_Clear();
        return -1;
    }
    Py_DECREF(compiled);

    app->run_func = PyDict_GetItemString(globals, "__xv6_host_gui_repl_run");
    if (!app->run_func)
        return -1;
    Py_INCREF(app->run_func);
    app->python_ready = 1;
    fprintf(stderr, "host-python-repl: Python ready\n");
    return 0;

fail:
    PyConfig_Clear(&config);
    fprintf(stderr, "host-python-repl: Python config/init failed\n");
    return -1;
}

static void run_code(struct app *app, const char *code)
{
    PyObject *result;
    const char *out;
    char prompt[LINE_LEN];

    if (!code || !code[0])
        return;

    snprintf(prompt, sizeof(prompt), ">>> %s", code);
    append_wrapped(app, prompt);
    fprintf(stderr, "host-python-repl: eval %s\n", code);

    if (!app->run_func) {
        append_line(app, "Python is not ready.");
        return;
    }

    result = PyObject_CallFunction(app->run_func, "s", code);
    if (!result) {
        PyErr_Clear();
        append_line(app, "internal Python call failed");
        return;
    }
    out = PyUnicode_AsUTF8(result);
    if (out && out[0])
        append_wrapped(app, out);
    Py_DECREF(result);
}

static void render(struct app *app)
{
    uint32_t *fb = app->buffer.pixels;
    int w = app->buffer.width;
    int h = app->buffer.height;
    int line_y;
    int visible_lines;
    int start;
    char prompt[LINE_LEN];

    if (!fb)
        return;

    draw_rect(fb, w, h, 0, 0, w, h, 0xff15171bu);
    draw_rect(fb, w, h, 0, 0, w, 44, 0xff20455au);
    draw_rect(fb, w, h, 0, 44, w, 2, 0xfff1c84bu);
    draw_string(fb, w, h, 18, 11, "Host Python REPL", 0xffffffffu, 1);
    draw_string(fb, w, h, 580, 13,
                app->python_ready ? "Python ready" : "Starting Python",
                app->python_ready ? 0xffa4f0b4u : 0xffffdc72u, 1);

    draw_rect(fb, w, h, 14, 58, w - 28, h - 116, 0xff202329u);
    draw_rect(fb, w, h, 14, h - 48, w - 28, 34, 0xfff6f0d2u);
    draw_rect(fb, w, h, 15, h - 47, w - 30, 32, 0xff111316u);

    visible_lines = (h - 138) / 18;
    start = app->line_count > visible_lines ?
            app->line_count - visible_lines : 0;
    line_y = 66;
    for (int i = start; i < app->line_count; i++, line_y += 18)
        draw_string(fb, w, h, 24, line_y, app->lines[i], 0xfff0f3f5u, 1);

    snprintf(prompt, sizeof(prompt), ">>> %.100s_", app->input);
    draw_string(fb, w, h, 24, h - 39, prompt, 0xffffffffu, 1);

    wl_surface_attach(app->surface, app->buffer.wl_buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, WIN_W, WIN_H);
    wl_surface_commit(app->surface);
    wl_display_flush(app->display);
    app->dirty = 0;
}

static char key_to_char(uint32_t key, uint32_t mods)
{
    int shift = mods & 1;

    if (key >= 2 && key <= 11) {
        const char *digits = "1234567890";
        const char *shifted = "!@#$%^&*()";
        return shift ? shifted[key - 2] : digits[key - 2];
    }
    if (key >= 16 && key <= 25) {
        const char *row = "qwertyuiop";
        char c = row[key - 16];
        return shift ? (char)(c - 'a' + 'A') : c;
    }
    if (key >= 30 && key <= 38) {
        const char *row = "asdfghjkl";
        char c = row[key - 30];
        return shift ? (char)(c - 'a' + 'A') : c;
    }
    if (key >= 44 && key <= 50) {
        const char *row = "zxcvbnm";
        char c = row[key - 44];
        return shift ? (char)(c - 'a' + 'A') : c;
    }

    switch (key) {
    case 12: return shift ? '_' : '-';
    case 13: return shift ? '+' : '=';
    case 26: return shift ? '{' : '[';
    case 27: return shift ? '}' : ']';
    case 39: return shift ? ':' : ';';
    case 40: return shift ? '"' : '\'';
    case 41: return shift ? '~' : '`';
    case 43: return shift ? '|' : '\\';
    case 51: return shift ? '<' : ',';
    case 52: return shift ? '>' : '.';
    case 53: return shift ? '?' : '/';
    case 57: return ' ';
    default: return 0;
    }
}

static void handle_key(struct app *app, uint32_t key)
{
    char ch;

    switch (key) {
    case 1:
        app->running = 0;
        return;
    case 14:
        if (app->input_len > 0) {
            app->input[--app->input_len] = '\0';
            app->dirty = 1;
        }
        return;
    case 28:
        run_code(app, app->input);
        app->input_len = 0;
        app->input[0] = '\0';
        app->dirty = 1;
        return;
    default:
        break;
    }

    ch = key_to_char(key, app->mods);
    if (ch && app->input_len + 1 < sizeof(app->input)) {
        app->input[app->input_len++] = ch;
        app->input[app->input_len] = '\0';
        app->dirty = 1;
    }
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base,
                             uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    struct app *app = data;

    xdg_surface_ack_configure(surface, serial);
    app->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                   int32_t width, int32_t height,
                                   struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct app *app = data;

    (void)toplevel;
    app->running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
    (void)sx;
    (void)sy;
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)sx;
    (void)sy;
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state)
{
    (void)data;
    (void)pointer;
    (void)serial;
    (void)time;
    (void)button;
    (void)state;
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
    (void)data;
    (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                                uint32_t axis_source)
{
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                              uint32_t time, uint32_t axis)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                  uint32_t axis, int32_t discrete)
{
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size)
{
    (void)data;
    (void)keyboard;
    (void)format;
    (void)size;
    if (fd >= 0)
        close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state)
{
    struct app *app = data;

    (void)keyboard;
    (void)serial;
    (void)time;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
        handle_key(app, key);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group)
{
    struct app *app = data;

    (void)keyboard;
    (void)serial;
    (void)mods_latched;
    (void)mods_locked;
    (void)group;
    app->mods = mods_depressed;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay)
{
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    struct app *app = data;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !app->keyboard) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void shm_format(void *data, struct wl_shm *shm, uint32_t format)
{
    (void)data;
    (void)shm;
    (void)format;
}

static const struct wl_shm_listener shm_listener = {
    .format = shm_format,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct app *app = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(
            registry, name, &wl_shm_interface, version < 1 ? version : 1);
        wl_shm_add_listener(app->shm, &shm_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(
            registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface, version < 2 ? version : 2);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static int map_window(struct app *app)
{
    int dispatches = 0;

    fprintf(stderr, "host-python-repl: creating Wayland toplevel\n");
    fflush(stderr);
    app->surface = wl_compositor_create_surface(app->compositor);
    app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
    xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
    app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
    xdg_toplevel_add_listener(app->toplevel, &xdg_toplevel_listener, app);
    xdg_toplevel_set_title(app->toplevel, "Host Python REPL");
    xdg_toplevel_set_app_id(app->toplevel, "host-python-repl");
    wl_surface_commit(app->surface);
    wl_display_flush(app->display);

    while (!app->configured) {
        if (wl_display_dispatch(app->display) < 0)
            return -1;
        dispatches++;
        if (dispatches > 32) {
            fprintf(stderr, "host-python-repl: xdg configure timeout\n");
            return -1;
        }
    }
    fprintf(stderr, "host-python-repl: xdg configured dispatches=%d\n",
            dispatches);
    fflush(stderr);

    if (xv6_present_buffer_init_shm_format(&app->buffer, WIN_W, WIN_H,
                                           app->shm,
                                           WL_SHM_FORMAT_XRGB8888) != 0)
        return -1;
    fprintf(stderr, "host-python-repl: shm buffer ready %dx%d\n",
            WIN_W, WIN_H);
    fflush(stderr);
    return 0;
}

static void cleanup(struct app *app)
{
    if (app->run_func)
        Py_DECREF(app->run_func);
    if (Py_IsInitialized())
        Py_Finalize();
    if (app->keyboard)
        wl_keyboard_destroy(app->keyboard);
    if (app->pointer)
        wl_pointer_destroy(app->pointer);
    if (app->seat)
        wl_seat_destroy(app->seat);
    if (app->toplevel)
        xdg_toplevel_destroy(app->toplevel);
    if (app->xdg_surface)
        xdg_surface_destroy(app->xdg_surface);
    if (app->surface)
        wl_surface_destroy(app->surface);
    xv6_present_buffer_destroy(&app->buffer);
    if (app->shm)
        wl_shm_destroy(app->shm);
    if (app->wm_base)
        xdg_wm_base_destroy(app->wm_base);
    if (app->compositor)
        wl_compositor_destroy(app->compositor);
    if (app->registry)
        wl_registry_destroy(app->registry);
    if (app->display)
        wl_display_disconnect(app->display);
}

int main(int argc, char **argv)
{
    struct app app;

    (void)argc;
    (void)argv;
    memset(&app, 0, sizeof(app));
    app.running = 1;
    app.buffer.fd = -1;
    app.buffer.fb_fd = -1;
    setenv("XDG_RUNTIME_DIR", "/tmp", 0);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
    fprintf(stderr, "host-python-repl: main start XDG_RUNTIME_DIR=%s WAYLAND_DISPLAY=%s\n",
            getenv("XDG_RUNTIME_DIR"), getenv("WAYLAND_DISPLAY"));
    fflush(stderr);

    append_line(&app, "Host-built GUI Python REPL");
    append_line(&app, "Starting embedded Python...");

    app.display = wl_display_connect(NULL);
    if (!app.display) {
        fprintf(stderr, "host-python-repl: wl_display_connect failed\n");
        return 1;
    }
    fprintf(stderr, "host-python-repl: wl_display_connect ok\n");
    fflush(stderr);
    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);
    wl_display_roundtrip(app.display);
    fprintf(stderr,
            "host-python-repl: globals compositor=%d shm=%d wm_base=%d seat=%d keyboard=%d pointer=%d\n",
            !!app.compositor, !!app.shm, !!app.wm_base, !!app.seat,
            !!app.keyboard, !!app.pointer);
    fflush(stderr);

    if (!app.compositor || !app.shm || !app.wm_base || !app.seat ||
        !app.keyboard) {
        fprintf(stderr,
                "host-python-repl: missing globals compositor=%d shm=%d wm_base=%d seat=%d keyboard=%d\n",
                !!app.compositor, !!app.shm, !!app.wm_base, !!app.seat,
                !!app.keyboard);
        cleanup(&app);
        return 2;
    }

    if (map_window(&app) != 0) {
        fprintf(stderr, "host-python-repl: window setup failed errno=%d (%s)\n",
                errno, strerror(errno));
        cleanup(&app);
        return 3;
    }

    render(&app);
    if (init_python(&app) == 0) {
        append_line(&app, "Python ready. Type expressions, then press Enter.");
        run_code(&app, "1 + 2");
    } else {
        append_line(&app, "Python failed to initialize. See log.");
    }
    render(&app);
    fprintf(stderr, "host-python-repl: mapped Wayland window\n");

    while (app.running) {
        if (app.dirty)
            render(&app);
        if (wl_display_dispatch(app.display) < 0)
            break;
    }

    fprintf(stderr, "host-python-repl: exiting\n");
    cleanup(&app);
    return 0;
}
