#include <gio/gio.h>
#include <libsoup/soup.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    GMainLoop *loop;
    GInputStream *stream;
    guint timeout_id;
    unsigned skips;
    int exit_code;
} TestState;

static const char *default_url =
    "https://accounts.google.com/ServiceLogin?service=youtube&uilel=3&passive=true"
    "&continue=https%3A%2F%2Fwww.youtube.com%2Fsignin%3Faction_handle_signin%3Dtrue"
    "%26app%3Ddesktop%26hl%3Den%26next%3D%252Fsignin_passive%26feature%3Dpassive&hl=en";

static gboolean timeout_cb(gpointer user_data)
{
    TestState *state = user_data;
    fprintf(stderr, "souptest: TIMEOUT waiting for skip/read completion after %u skip callbacks\n", state->skips);
    state->exit_code = 2;
    g_main_loop_quit(state->loop);
    return G_SOURCE_REMOVE;
}

static void skip_cb(GObject *source, GAsyncResult *result, gpointer user_data);

static void schedule_skip(TestState *state)
{
    g_input_stream_skip_async(state->stream, 32768, G_PRIORITY_DEFAULT, NULL, skip_cb, state);
}

static void skip_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    TestState *state = user_data;
    GError *error = NULL;
    gssize skipped = g_input_stream_skip_finish(G_INPUT_STREAM(source), result, &error);
    state->skips++;
    if (error) {
        fprintf(stderr, "souptest: skip error domain=%s code=%d message=%s\n",
            g_quark_to_string(error->domain), error->code, error->message);
        g_error_free(error);
        state->exit_code = 3;
        g_main_loop_quit(state->loop);
        return;
    }
    fprintf(stderr, "souptest: skip callback #%u skipped=%zd\n", state->skips, skipped);
    if (skipped > 0) {
        schedule_skip(state);
        return;
    }
    fprintf(stderr, "souptest: drained redirect body\n");
    state->exit_code = 0;
    g_main_loop_quit(state->loop);
}

static void got_headers_cb(SoupMessage *message, gpointer user_data)
{
    const char *location = soup_message_headers_get_one(soup_message_get_response_headers(message), "Location");
    goffset content_length = soup_message_headers_get_content_length(soup_message_get_response_headers(message));
    fprintf(stderr, "souptest: got-headers status=%u len=%lld location=%s\n",
        soup_message_get_status(message), (long long)content_length, location ? location : "(none)");
}

static void send_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    TestState *state = user_data;
    GError *error = NULL;
    GInputStream *stream = soup_session_send_finish(SOUP_SESSION(source), result, &error);
    if (error) {
        fprintf(stderr, "souptest: send error domain=%s code=%d message=%s\n",
            g_quark_to_string(error->domain), error->code, error->message);
        g_error_free(error);
        state->exit_code = 4;
        g_main_loop_quit(state->loop);
        return;
    }
    state->stream = stream;
    fprintf(stderr, "souptest: send finished; starting redirect-body drain\n");
    schedule_skip(state);
}

int main(int argc, char **argv)
{
    const char *url = argc > 1 ? argv[1] : default_url;
    unsigned timeout_seconds = argc > 2 ? (unsigned)atoi(argv[2]) : 20;
    TestState state;
    memset(&state, 0, sizeof(state));

    state.loop = g_main_loop_new(NULL, FALSE);
    state.exit_code = 1;
    state.timeout_id = g_timeout_add_seconds(timeout_seconds, timeout_cb, &state);

    SoupSession *session = soup_session_new();
    SoupMessage *message = soup_message_new("GET", url);
    if (!message) {
        fprintf(stderr, "souptest: failed to create message\n");
        return 5;
    }
    soup_message_set_flags(message, soup_message_get_flags(message) | SOUP_MESSAGE_NO_REDIRECT | SOUP_MESSAGE_COLLECT_METRICS);
    g_signal_connect(message, "got-headers", G_CALLBACK(got_headers_cb), NULL);

    fprintf(stderr, "souptest: sending %s\n", url);
    soup_session_send_async(session, message, G_PRIORITY_DEFAULT, NULL, send_cb, &state);
    g_main_loop_run(state.loop);

    if (state.timeout_id)
        g_source_remove(state.timeout_id);
    if (state.stream) {
        g_input_stream_close(state.stream, NULL, NULL);
        g_object_unref(state.stream);
    }
    g_object_unref(message);
    g_object_unref(session);
    g_main_loop_unref(state.loop);
    fprintf(stderr, "souptest: exit=%d\n", state.exit_code);
    return state.exit_code;
}
