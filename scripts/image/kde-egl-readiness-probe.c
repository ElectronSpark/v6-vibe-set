// kde-egl-readiness-probe: minimal DRI2/virgl EGL screen readiness predicate.
//
// U1 residual (see docs/active-work-plan.md). Right after an fs.img rebuild the
// FIRST EGL/DRI2 screen creation on the virtio-gpu/virgl render node can fail
// transiently:
//   - kwin (GBM path): "Could not create gbm device" / "Could not initialize
//     egl" / "Failed to find a working setup for new outputs!" -> kwin RETRIES
//     its output init 1-3x and recovers.
//   - plasmashell (Qt-Wayland path): "libEGL warning: egl: failed to create
//     dri2 screen" x4 + "Failed to initialize EGL display 3001" -> Qt has NO
//     retry and drops to the QtQuick software backend.
//
// This helper reproduces exactly the operation that transiently fails: open the
// render node, create a GBM device, get the GBM-platform EGL display, and drive
// eglInitialize + eglCreateContext + eglMakeCurrent (which forces mesa to build
// the DRI2/virgl screen). Success means the substrate can currently hand out a
// working accelerated screen; the session launcher retries this until it passes
// before exec'ing plasmashell, so plasmashell no longer races kwin's own
// output-init retry.
//
// Deliberately does NOT call gbm_bo_create: drmgpuprobe showed standalone
// gbm_bo_create fails EINVAL on USE_WRITE|LINEAR while the mesa EGL/platform_drm
// gbm path is unaffected (plan U1 out-of-scope note). eglCreateContext +
// surfaceless eglMakeCurrent exercises the screen without any bo allocation.
//
// Exit 0 = ready (PASS). Nonzero = not ready / error (FAIL). Prints exactly one
// "kde-egl-readiness-probe: phase=result status=..." line for the launcher and
// archives to grep.

#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

static const char *render_node_default = "/dev/dri/renderD128";

static EGLDisplay get_gbm_display(struct gbm_device *gbm)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;

    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
        "eglGetPlatformDisplay");
    if (get_platform_display)
        return get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);

    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
        "eglGetPlatformDisplayEXT");
    if (get_platform_display)
        return get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);

    return eglGetDisplay((EGLNativeDisplayType)gbm);
}

static int try_context(EGLDisplay display, EGLConfig config, int es_version,
                       const char **reason)
{
    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, es_version, EGL_NONE };
    EGLContext context;

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attrs);
    if (context == EGL_NO_CONTEXT) {
        *reason = "eglCreateContext";
        return 0;
    }

    /*
     * Surfaceless make-current forces the DRI2/virgl screen to fully realize
     * without allocating a gbm bo, then confirms a live GL renderer string.
     */
    if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        *reason = "eglMakeCurrent";
        eglDestroyContext(display, context);
        return 0;
    }

    {
        const char *renderer = (const char *)glGetString(GL_RENDERER);

        printf("kde-egl-readiness-probe: phase=renderer es=%d renderer=%s\n",
               es_version, renderer ? renderer : "(null)");
    }

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    return 1;
}

int main(int argc, char **argv)
{
    const char *node = render_node_default;
    const char *env_node = getenv("KDE_EGL_READINESS_RENDER_NODE");
    const char *reason = "unknown";
    int fd = -1;
    struct gbm_device *gbm = NULL;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLint major = 0, minor = 0;
    EGLint config_count = 0;
    EGLConfig config = NULL;
    int ok = 0;
    static const EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1 && argv[1][0])
        node = argv[1];
    else if (env_node && env_node[0])
        node = env_node;

    fd = open(node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        reason = "open_render_node";
        goto done;
    }

    gbm = gbm_create_device(fd);
    if (!gbm) {
        reason = "gbm_create_device";
        goto done;
    }

    display = get_gbm_display(gbm);
    if (display == EGL_NO_DISPLAY) {
        reason = "eglGetPlatformDisplay";
        goto done;
    }

    if (!eglInitialize(display, &major, &minor)) {
        reason = "eglInitialize";
        goto done;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        reason = "eglBindAPI";
        goto done;
    }

    if (!eglChooseConfig(display, config_attrs, &config, 1, &config_count) ||
        config_count < 1) {
        reason = "eglChooseConfig";
        goto done;
    }

    /* Prefer ES3 (what kwin/plasmashell get), fall back to ES2. */
    if (try_context(display, config, 3, &reason) ||
        try_context(display, config, 2, &reason)) {
        ok = 1;
    }

done:
    if (display != EGL_NO_DISPLAY) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglTerminate(display);
    }
    if (gbm)
        gbm_device_destroy(gbm);
    if (fd >= 0)
        close(fd);
    eglReleaseThread();

    if (ok) {
        printf("kde-egl-readiness-probe: phase=result status=PASS node=%s "
               "egl=%d.%d\n", node, major, minor);
        return 0;
    }
    printf("kde-egl-readiness-probe: phase=result status=FAIL node=%s "
           "reason=%s egl_error=0x%x errno=%d %s\n",
           node, reason, (unsigned)eglGetError(), errno, strerror(errno));
    return 1;
}
