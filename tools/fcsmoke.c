#include <errno.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void *thread_main(void *arg)
{
    (void)arg;
    fprintf(stderr, "fcsmoke: child thread running\n");
    return (void *)0x1234;
}

static void *nested_thread_main(void *arg)
{
    (void)arg;
    fprintf(stderr, "fcsmoke: nested child running\n");
    return (void *)0x5678;
}

static void *fc_thread_main(void *arg)
{
    pthread_t nested;
    void *nested_result = NULL;
    int ret;

    (void)arg;
    fprintf(stderr, "fcsmoke: FcInit worker before nested pthread\n");
    ret = pthread_create(&nested, NULL, nested_thread_main, NULL);
    fprintf(stderr, "fcsmoke: nested pthread_create ret=%d errno=%d (%s)\n",
            ret, errno, strerror(errno));
    if (ret == 0) {
        ret = pthread_join(nested, &nested_result);
        fprintf(stderr,
                "fcsmoke: nested pthread_join ret=%d result=%p errno=%d (%s)\n",
                ret, nested_result, errno, strerror(errno));
    }

    fprintf(stderr, "fcsmoke: FcInit worker before FcInit\n");
    if (!FcInit()) {
        fprintf(stderr, "fcsmoke: FcInit worker failed\n");
        return (void *)6;
    }
    fprintf(stderr, "fcsmoke: FcInit worker ok\n");
    return NULL;
}

static gpointer glib_thread_main(gpointer arg)
{
    (void)arg;
    fprintf(stderr, "fcsmoke: GLib thread running\n");
    return (gpointer)0x9abc;
}

int main(void)
{
    pthread_t thread;
    pthread_t fc_thread;
    void *thread_result = NULL;
    void *fc_thread_result = NULL;
    GThread *glib_thread;
    gpointer glib_result;
    size_t page = 4096;
    size_t map_size = 256 * 1024;
    void *map;
    int ret;

    fprintf(stderr, "fcsmoke: begin pid=%ld\n", (long)getpid());

    map = mmap(NULL, map_size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    fprintf(stderr, "fcsmoke: mmap guard map=%p errno=%d (%s)\n",
            map, errno, strerror(errno));
    if (map == MAP_FAILED)
        return 2;

    ret = mprotect((char *)map + page, map_size - page,
                   PROT_READ | PROT_WRITE);
    fprintf(stderr, "fcsmoke: mprotect ret=%d errno=%d (%s)\n",
            ret, errno, strerror(errno));
    if (ret != 0)
        return 3;

    ret = pthread_create(&thread, NULL, thread_main, NULL);
    fprintf(stderr, "fcsmoke: pthread_create ret=%d errno=%d (%s)\n",
            ret, errno, strerror(errno));
    if (ret != 0)
        return 4;

    ret = pthread_join(thread, &thread_result);
    fprintf(stderr, "fcsmoke: pthread_join ret=%d result=%p errno=%d (%s)\n",
            ret, thread_result, errno, strerror(errno));
    if (ret != 0)
        return 5;

    fprintf(stderr, "fcsmoke: before FcInit\n");
    if (!FcInit()) {
        fprintf(stderr, "fcsmoke: FcInit failed\n");
        return 6;
    }
    fprintf(stderr, "fcsmoke: FcInit ok\n");

    ret = pthread_create(&fc_thread, NULL, fc_thread_main, NULL);
    fprintf(stderr, "fcsmoke: FcInit worker create ret=%d errno=%d (%s)\n",
            ret, errno, strerror(errno));
    if (ret != 0)
        return 7;
    ret = pthread_join(fc_thread, &fc_thread_result);
    fprintf(stderr,
            "fcsmoke: FcInit worker join ret=%d result=%p errno=%d (%s)\n",
            ret, fc_thread_result, errno, strerror(errno));
    if (ret != 0 || fc_thread_result != NULL)
        return 8;

    glib_thread = g_thread_new("[fcsmoke] glib", glib_thread_main, NULL);
    fprintf(stderr, "fcsmoke: g_thread_new returned %p\n", glib_thread);
    glib_result = g_thread_join(glib_thread);
    fprintf(stderr, "fcsmoke: g_thread_join result=%p\n", glib_result);
    if (glib_result != (gpointer)0x9abc)
        return 9;

    fprintf(stderr, "fcsmoke: before pango font map\n");
    PangoFontMap *font_map = pango_cairo_font_map_get_default();
    fprintf(stderr, "fcsmoke: pango font map=%p\n", font_map);
    if (font_map == NULL)
        return 10;
    PangoContext *context = pango_font_map_create_context(font_map);
    fprintf(stderr, "fcsmoke: pango context=%p\n", context);
    if (context == NULL)
        return 11;
    g_object_unref(context);

    FcFini();

    munmap(map, map_size);
    fprintf(stderr, "fcsmoke: complete\n");
    return 0;
}
