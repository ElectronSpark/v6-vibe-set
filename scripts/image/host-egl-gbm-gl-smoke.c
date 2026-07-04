// Xwayland-style EGL/GBM reducer.
//
// This host-built probe avoids X11 entirely and checks the GLAMOR substrate
// Xwayland uses before it can advertise useful accelerated X11 behavior:
// render node -> GBM device -> EGL display/context/surface -> glGetString().

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_NO_CONFIG_MESA
#define EGL_NO_CONFIG_MESA ((EGLConfig)0)
#endif

#ifndef EGL_OPENGL_BIT
#define EGL_OPENGL_BIT 0x0008
#endif

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

#ifndef EGL_CONFORMANT
#define EGL_CONFORMANT 0x3042
#endif

#ifndef EGL_CONTEXT_OPENGL_NO_ERROR_KHR
#define EGL_CONTEXT_OPENGL_NO_ERROR_KHR 0x31B3
#endif

#ifndef EGL_CONTEXT_PRIORITY_LEVEL_IMG
#define EGL_CONTEXT_PRIORITY_LEVEL_IMG 0x3100
#endif

#ifndef EGL_CONTEXT_PRIORITY_HIGH_IMG
#define EGL_CONTEXT_PRIORITY_HIGH_IMG 0x3101
#endif

#ifndef GBM_FORMAT_ARGB8888
#define GBM_FORMAT_ARGB8888 0x34325241
#endif

#ifndef GBM_BO_USE_RENDERING
#define GBM_BO_USE_RENDERING (1 << 2)
#endif

#ifndef DRM_IOCTL_VIRTGPU_GETPARAM
#define DRM_IOCTL_VIRTGPU_GETPARAM 0xc0106443UL
#endif

#ifndef VIRTGPU_PARAM_3D_FEATURES
#define VIRTGPU_PARAM_3D_FEATURES 1
#endif
#ifndef VIRTGPU_PARAM_CAPSET_QUERY_FIX
#define VIRTGPU_PARAM_CAPSET_QUERY_FIX 2
#endif
#ifndef VIRTGPU_PARAM_RESOURCE_BLOB
#define VIRTGPU_PARAM_RESOURCE_BLOB 3
#endif
#ifndef VIRTGPU_PARAM_HOST_VISIBLE
#define VIRTGPU_PARAM_HOST_VISIBLE 4
#endif
#ifndef VIRTGPU_PARAM_CROSS_DEVICE
#define VIRTGPU_PARAM_CROSS_DEVICE 5
#endif
#ifndef VIRTGPU_PARAM_CONTEXT_INIT
#define VIRTGPU_PARAM_CONTEXT_INIT 6
#endif
#ifndef VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs
#define VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs 7
#endif
#ifndef VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME
#define VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME 8
#endif
#ifndef VIRTGPU_PARAM_GUEST_VRAM
#define VIRTGPU_PARAM_GUEST_VRAM 9
#endif

struct drm_virtgpu_getparam_local {
    uint64_t param;
    uint64_t value;
};

static const char *render_node = "/dev/dri/renderD128";

enum smoke_api {
    SMOKE_API_GLES,
    SMOKE_API_GLES3,
    SMOKE_API_GL,
};

static const char *
smoke_api_name(enum smoke_api api)
{
    if (api == SMOKE_API_GLES3)
        return "gles3";
    return api == SMOKE_API_GL ? "gl" : "gles";
}

static EGLenum
smoke_egl_api(enum smoke_api api)
{
    return api == SMOKE_API_GL ? EGL_OPENGL_API : EGL_OPENGL_ES_API;
}

static const char *
smoke_egl_api_name(enum smoke_api api)
{
    return api == SMOKE_API_GL ? "EGL_OPENGL_API" : "EGL_OPENGL_ES_API";
}

static EGLint
smoke_renderable_type(enum smoke_api api)
{
    if (api == SMOKE_API_GLES3)
        return EGL_OPENGL_ES3_BIT;
    return api == SMOKE_API_GL ? EGL_OPENGL_BIT : EGL_OPENGL_ES2_BIT;
}

static EGLint
smoke_context_client_version(enum smoke_api api)
{
    return api == SMOKE_API_GLES3 ? 3 : 2;
}

static const char *
safe_str(const char *s)
{
    return s ? s : "(null)";
}

static int
has_extension(const char *extensions, const char *needle)
{
    size_t needle_len;
    const char *p;

    if (!extensions || !needle || !needle[0])
        return 0;
    needle_len = strlen(needle);
    for (p = extensions; (p = strstr(p, needle)) != NULL; p += needle_len) {
        int left_ok = p == extensions || p[-1] == ' ';
        int right_ok = p[needle_len] == '\0' || p[needle_len] == ' ';

        if (left_ok && right_ok)
            return 1;
    }
    return 0;
}

static const char *
egl_error_name(EGLint err)
{
    switch (err) {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:
        return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    default:
        return "EGL_UNKNOWN";
    }
}

static void
phase_fail(const char *phase, enum smoke_api api, const char *reason)
{
    EGLint err = eglGetError();

    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=%s status=FAIL api=%s reason=%s egl_error=0x%x error_name=%s\n",
            phase, smoke_api_name(api), reason, err, egl_error_name(err));
    fflush(stderr);
}

static void
clear_egl_error(void)
{
    while (eglGetError() != EGL_SUCCESS)
        ;
}

static const char *
virtgpu_param_name(uint64_t param)
{
    switch (param) {
    case VIRTGPU_PARAM_3D_FEATURES:
        return "VIRTGPU_PARAM_3D_FEATURES";
    case VIRTGPU_PARAM_CAPSET_QUERY_FIX:
        return "VIRTGPU_PARAM_CAPSET_QUERY_FIX";
    case VIRTGPU_PARAM_RESOURCE_BLOB:
        return "VIRTGPU_PARAM_RESOURCE_BLOB";
    case VIRTGPU_PARAM_HOST_VISIBLE:
        return "VIRTGPU_PARAM_HOST_VISIBLE";
    case VIRTGPU_PARAM_CROSS_DEVICE:
        return "VIRTGPU_PARAM_CROSS_DEVICE";
    case VIRTGPU_PARAM_CONTEXT_INIT:
        return "VIRTGPU_PARAM_CONTEXT_INIT";
    case VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs:
        return "VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs";
    case VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME:
        return "VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME";
    case VIRTGPU_PARAM_GUEST_VRAM:
        return "VIRTGPU_PARAM_GUEST_VRAM";
    default:
        return "UNKNOWN";
    }
}

static void
log_virtgpu_getparams(int fd)
{
    for (uint64_t param = 1; param <= 12; param++) {
        struct drm_virtgpu_getparam_local req;
        uint64_t value = 0;
        int ret;
        int saved_errno;

        memset(&req, 0, sizeof(req));
        req.param = param;
        req.value = (uint64_t)(uintptr_t)&value;
        errno = 0;
        ret = ioctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, &req);
        saved_errno = errno;
        if (ret == 0) {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=virtgpu_getparam status=PASS param=%lu name=%s ret=%d errno=%d value=%lu\n",
                    (unsigned long)param, virtgpu_param_name(param), ret,
                    saved_errno, (unsigned long)value);
        } else {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=virtgpu_getparam status=FAIL param=%lu name=%s ret=%d errno=%d value=NA\n",
                    (unsigned long)param, virtgpu_param_name(param), ret,
                    saved_errno);
        }
    }
}

static EGLint
egl_config_attr_or_neg1(EGLDisplay display, EGLConfig config, EGLint attr)
{
    EGLint value = -1;

    if (display == EGL_NO_DISPLAY || !config)
        return -1;
    if (!eglGetConfigAttrib(display, config, attr, &value))
        return -1;
    return value;
}

static void
log_config_detail(EGLDisplay display, EGLConfig config, int index)
{
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: diag egl_config index=%d config_id=%d renderable=0x%x conformant=0x%x surface=0x%x native_visual_id=0x%x red=%d green=%d blue=%d alpha=%d caveat=0x%x samples=%d\n",
            index,
            egl_config_attr_or_neg1(display, config, EGL_CONFIG_ID),
            egl_config_attr_or_neg1(display, config, EGL_RENDERABLE_TYPE),
            egl_config_attr_or_neg1(display, config, EGL_CONFORMANT),
            egl_config_attr_or_neg1(display, config, EGL_SURFACE_TYPE),
            egl_config_attr_or_neg1(display, config, EGL_NATIVE_VISUAL_ID),
            egl_config_attr_or_neg1(display, config, EGL_RED_SIZE),
            egl_config_attr_or_neg1(display, config, EGL_GREEN_SIZE),
            egl_config_attr_or_neg1(display, config, EGL_BLUE_SIZE),
            egl_config_attr_or_neg1(display, config, EGL_ALPHA_SIZE),
            egl_config_attr_or_neg1(display, config, EGL_CONFIG_CAVEAT),
            egl_config_attr_or_neg1(display, config, EGL_SAMPLES));
}

static void
log_chromium_config_shape(EGLDisplay display, const char *profile,
                          EGLint renderable, EGLint surface_type)
{
    EGLint attrs[] = {
        EGL_BUFFER_SIZE, 32,
        EGL_ALPHA_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_RENDERABLE_TYPE, renderable,
        EGL_SURFACE_TYPE, surface_type,
        EGL_NONE,
    };
    EGLConfig config = NULL;
    EGLint count = 0;
    EGLint chosen_count = 0;
    EGLint first_config_id = -1;
    EGLint first_surface = -1;
    EGLint first_renderable = -1;
    EGLint first_alpha = -1;
    EGLint first_buffer = -1;
    EGLint err;

    clear_egl_error();
    if (!eglChooseConfig(display, attrs, NULL, 0, &count)) {
        err = eglGetError();
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_chromium_config_shape status=FAIL profile=%s renderable=0x%x surface=0x%x buffer_size=32 alpha=8 red=8 green=8 blue=8 count=%d egl_error=0x%x error_name=%s reason=validate\n",
                profile, renderable, surface_type, count, err,
                egl_error_name(err));
        return;
    }
    err = eglGetError();

    if (count > 0 &&
        eglChooseConfig(display, attrs, &config, 1, &chosen_count) &&
        chosen_count > 0 &&
        config != NULL) {
        first_config_id =
            egl_config_attr_or_neg1(display, config, EGL_CONFIG_ID);
        first_surface =
            egl_config_attr_or_neg1(display, config, EGL_SURFACE_TYPE);
        first_renderable =
            egl_config_attr_or_neg1(display, config, EGL_RENDERABLE_TYPE);
        first_alpha =
            egl_config_attr_or_neg1(display, config, EGL_ALPHA_SIZE);
        first_buffer =
            egl_config_attr_or_neg1(display, config, EGL_BUFFER_SIZE);
    }

    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_chromium_config_shape status=PASS profile=%s renderable=0x%x surface=0x%x buffer_size=32 alpha=8 red=8 green=8 blue=8 count=%d egl_error=0x%x error_name=%s first_config_id=%d first_renderable=0x%x first_surface=0x%x first_buffer=%d first_alpha=%d\n",
            profile, renderable, surface_type, count, err,
            egl_error_name(err), first_config_id, first_renderable,
            first_surface, first_buffer, first_alpha);
}

static int
choose_chromium_config(EGLDisplay display, EGLint renderable,
                       EGLint surface_type, EGLConfig *config_out,
                       EGLint *count_out)
{
    EGLint attrs[] = {
        EGL_BUFFER_SIZE, 32,
        EGL_ALPHA_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_RENDERABLE_TYPE, renderable,
        EGL_SURFACE_TYPE, surface_type,
        EGL_NONE,
    };
    EGLint count = 0;
    EGLint chosen_count = 0;
    EGLConfig config = NULL;

    *config_out = NULL;
    *count_out = 0;
    clear_egl_error();
    if (!eglChooseConfig(display, attrs, NULL, 0, &count)) {
        *count_out = count;
        return 0;
    }
    *count_out = count;
    if (count <= 0)
        return 1;
    if (eglChooseConfig(display, attrs, &config, 1, &chosen_count) &&
        chosen_count > 0) {
        *config_out = config;
    }
    return 1;
}

static void
log_chromium_config_attempt(EGLDisplay display, struct gbm_device *gbm,
                            const char *profile, EGLint renderable,
                            EGLint surface_type, const char *surface_kind,
                            const EGLint *context_attrs,
                            int context_client_version,
                            const char *context_attr_profile)
{
    static const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE,
    };
    EGLConfig config = NULL;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    struct gbm_surface *gbm_surface = NULL;
    EGLint count = 0;
    EGLint err = EGL_SUCCESS;
    int choose_ok;
    int made_current = 0;
    const char *status = "FAIL";
    const char *reason = "unknown";

    choose_ok = choose_chromium_config(display, renderable, surface_type,
                                       &config, &count);
    if (!choose_ok) {
        err = eglGetError();
        reason = "eglChooseConfig";
        goto out;
    }
    err = eglGetError();
    if (count <= 0 || !config) {
        reason = "no_config";
        goto out;
    }

    if (strcmp(surface_kind, "pbuffer") == 0) {
        clear_egl_error();
        surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
        if (surface == EGL_NO_SURFACE) {
            err = eglGetError();
            reason = "eglCreatePbufferSurface";
            goto out;
        }
    } else if (strcmp(surface_kind, "gbm-window") == 0) {
        gbm_surface = gbm_surface_create(gbm, 16, 16, GBM_FORMAT_ARGB8888,
                                         GBM_BO_USE_RENDERING);
        if (!gbm_surface) {
            reason = "gbm_surface_create";
            goto out;
        }
        clear_egl_error();
        surface = eglCreateWindowSurface(display, config,
                                         (EGLNativeWindowType)gbm_surface,
                                         NULL);
        if (surface == EGL_NO_SURFACE) {
            err = eglGetError();
            reason = "eglCreateWindowSurface";
            goto out;
        }
    } else if (strcmp(surface_kind, "surfaceless") == 0) {
        surface = EGL_NO_SURFACE;
    } else {
        reason = "unsupported_surface_kind";
        goto out;
    }

    clear_egl_error();
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    if (context == EGL_NO_CONTEXT) {
        err = eglGetError();
        reason = "eglCreateContext";
        goto out;
    }

    clear_egl_error();
    if (!eglMakeCurrent(display, surface, surface, context)) {
        err = eglGetError();
        reason = "eglMakeCurrent";
        goto out;
    }
    made_current = 1;
    status = "PASS";
    reason = "ok";
    err = eglGetError();

out:
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_chromium_context_attempt status=%s profile=%s renderable=0x%x surface=0x%x surface_kind=%s context_attr_profile=%s count=%d config_id=%d context_client_version=%d egl_error=0x%x error_name=%s reason=%s\n",
            status, profile, renderable, surface_type, surface_kind,
            context_attr_profile, count,
            egl_config_attr_or_neg1(display, config, EGL_CONFIG_ID),
            context_client_version, err, egl_error_name(err), reason);
    if (made_current)
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT)
        eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(display, surface);
    if (gbm_surface)
        gbm_surface_destroy(gbm_surface);
    clear_egl_error();
}

static void
log_chromium_config_attempts(EGLDisplay display, struct gbm_device *gbm)
{
    static const EGLint es3_base_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    static const EGLint es2_base_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    static const EGLint es3_robust_no_reset_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_CONTEXT_OPENGL_ROBUST_ACCESS, EGL_TRUE,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY,
        EGL_NO_RESET_NOTIFICATION,
        EGL_NONE,
    };
    static const EGLint es3_robust_lose_reset_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_CONTEXT_OPENGL_ROBUST_ACCESS, EGL_TRUE,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY,
        EGL_LOSE_CONTEXT_ON_RESET,
        EGL_NONE,
    };
    static const EGLint es3_no_error_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_CONTEXT_OPENGL_NO_ERROR_KHR, EGL_TRUE,
        EGL_NONE,
    };
    static const EGLint es3_priority_high_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_CONTEXT_PRIORITY_LEVEL_IMG, EGL_CONTEXT_PRIORITY_HIGH_IMG,
        EGL_NONE,
    };
    static const EGLint es3_chrome_combo_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_CONTEXT_OPENGL_ROBUST_ACCESS, EGL_TRUE,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY,
        EGL_NO_RESET_NOTIFICATION,
        EGL_CONTEXT_OPENGL_NO_ERROR_KHR, EGL_TRUE,
        EGL_NONE,
    };

    log_chromium_config_attempt(display, gbm,
                                "chromium_offscreen_pbuffer_es3",
                                EGL_OPENGL_ES3_BIT, EGL_PBUFFER_BIT,
                                "pbuffer", es3_base_attrs, 3, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_offscreen_pbuffer_es2",
                                EGL_OPENGL_ES2_BIT, EGL_PBUFFER_BIT,
                                "pbuffer", es2_base_attrs, 2, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_native_window_pbuffer_es3",
                                EGL_OPENGL_ES3_BIT,
                                EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                                "gbm-window", es3_base_attrs, 3, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_native_window_pbuffer_es2",
                                EGL_OPENGL_ES2_BIT,
                                EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                                "gbm-window", es2_base_attrs, 2, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_window_only_gbm_surface_es3",
                                EGL_OPENGL_ES3_BIT, EGL_WINDOW_BIT,
                                "gbm-window", es3_base_attrs, 3, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_window_only_gbm_surface_es2",
                                EGL_OPENGL_ES2_BIT, EGL_WINDOW_BIT,
                                "gbm-window", es2_base_attrs, 2, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_window_only_surfaceless_es3",
                                EGL_OPENGL_ES3_BIT, EGL_WINDOW_BIT,
                                "surfaceless", es3_base_attrs, 3, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_window_only_surfaceless_es2",
                                EGL_OPENGL_ES2_BIT, EGL_WINDOW_BIT,
                                "surfaceless", es2_base_attrs, 2, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_surfaceless_es3",
                                EGL_OPENGL_ES3_BIT, EGL_DONT_CARE,
                                "surfaceless", es3_base_attrs, 3, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_surfaceless_es2",
                                EGL_OPENGL_ES2_BIT, EGL_DONT_CARE,
                                "surfaceless", es2_base_attrs, 2, "base");
    log_chromium_config_attempt(display, gbm,
                                "chromium_surfaceless_es3_robust_no_reset",
                                EGL_OPENGL_ES3_BIT, EGL_DONT_CARE,
                                "surfaceless", es3_robust_no_reset_attrs, 3,
                                "robust_no_reset");
    log_chromium_config_attempt(display, gbm,
                                "chromium_surfaceless_es3_robust_lose_reset",
                                EGL_OPENGL_ES3_BIT, EGL_DONT_CARE,
                                "surfaceless", es3_robust_lose_reset_attrs, 3,
                                "robust_lose_reset");
    log_chromium_config_attempt(display, gbm,
                                "chromium_surfaceless_es3_no_error",
                                EGL_OPENGL_ES3_BIT, EGL_DONT_CARE,
                                "surfaceless", es3_no_error_attrs, 3,
                                "no_error");
    log_chromium_config_attempt(display, gbm,
                                "chromium_surfaceless_es3_priority_high",
                                EGL_OPENGL_ES3_BIT, EGL_DONT_CARE,
                                "surfaceless", es3_priority_high_attrs, 3,
                                "priority_high");
    log_chromium_config_attempt(display, gbm,
                                "chromium_surfaceless_es3_chrome_combo",
                                EGL_OPENGL_ES3_BIT, EGL_DONT_CARE,
                                "surfaceless", es3_chrome_combo_attrs, 3,
                                "chrome_combo");
}

static void
log_chromium_config_shapes(EGLDisplay display)
{
    log_chromium_config_shape(display, "chromium_offscreen_pbuffer_es3",
                              EGL_OPENGL_ES3_BIT, EGL_PBUFFER_BIT);
    log_chromium_config_shape(display, "chromium_offscreen_pbuffer_es2",
                              EGL_OPENGL_ES2_BIT, EGL_PBUFFER_BIT);
    log_chromium_config_shape(display, "chromium_native_window_pbuffer_es3",
                              EGL_OPENGL_ES3_BIT,
                              EGL_WINDOW_BIT | EGL_PBUFFER_BIT);
    log_chromium_config_shape(display, "chromium_native_window_pbuffer_es2",
                              EGL_OPENGL_ES2_BIT,
                              EGL_WINDOW_BIT | EGL_PBUFFER_BIT);
    log_chromium_config_shape(display, "chromium_window_only_es3",
                              EGL_OPENGL_ES3_BIT, EGL_WINDOW_BIT);
    log_chromium_config_shape(display, "chromium_window_only_es2",
                              EGL_OPENGL_ES2_BIT, EGL_WINDOW_BIT);
    log_chromium_config_shape(display, "chromium_surfaceless_es3",
                              EGL_OPENGL_ES3_BIT, EGL_DONT_CARE);
    log_chromium_config_shape(display, "chromium_surfaceless_es2",
                              EGL_OPENGL_ES2_BIT, EGL_DONT_CARE);
}

static void
log_config_summary(EGLDisplay display, enum smoke_api api, EGLint total_configs)
{
    EGLConfig *configs;
    EGLint got_configs = 0;
    EGLint desired_renderable = smoke_renderable_type(api);
    int renderable_match = 0;
    int conformant_match = 0;
    int es2_renderable = 0;
    int es3_renderable = 0;
    int pbuffer = 0;
    int window = 0;
    int rgb888 = 0;
    int exact = 0;
    int es2_pbuffer_rgb = 0;
    int es3_window_rgb = 0;
    int sampled = 0;

    if (total_configs <= 0)
        return;
    configs = calloc((size_t)total_configs, sizeof(configs[0]));
    if (!configs) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_config_summary status=FAIL api=%s reason=calloc count=%d\n",
                smoke_api_name(api), total_configs);
        return;
    }
    clear_egl_error();
    if (!eglGetConfigs(display, configs, total_configs, &got_configs)) {
        EGLint err = eglGetError();

        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_config_summary status=FAIL api=%s reason=eglGetConfigs egl_error=0x%x error_name=%s\n",
                smoke_api_name(api), err, egl_error_name(err));
        free(configs);
        return;
    }

    for (EGLint i = 0; i < got_configs; i++) {
        EGLint renderable = egl_config_attr_or_neg1(display, configs[i],
                                                    EGL_RENDERABLE_TYPE);
        EGLint conformant = egl_config_attr_or_neg1(display, configs[i],
                                                    EGL_CONFORMANT);
        EGLint surface = egl_config_attr_or_neg1(display, configs[i],
                                                 EGL_SURFACE_TYPE);
        EGLint red = egl_config_attr_or_neg1(display, configs[i],
                                             EGL_RED_SIZE);
        EGLint green = egl_config_attr_or_neg1(display, configs[i],
                                               EGL_GREEN_SIZE);
        EGLint blue = egl_config_attr_or_neg1(display, configs[i],
                                              EGL_BLUE_SIZE);
        int has_rgb888 = red >= 8 && green >= 8 && blue >= 8;
        int has_pbuffer = (surface & EGL_PBUFFER_BIT) != 0;
        int has_window = (surface & EGL_WINDOW_BIT) != 0;
        int has_es2 = (renderable & EGL_OPENGL_ES2_BIT) != 0;
        int has_es3 = (renderable & EGL_OPENGL_ES3_BIT) != 0;
        int has_desired = (renderable & desired_renderable) != 0;

        if (has_desired)
            renderable_match++;
        if (conformant & desired_renderable)
            conformant_match++;
        if (has_es2)
            es2_renderable++;
        if (has_es3)
            es3_renderable++;
        if (has_pbuffer)
            pbuffer++;
        if (has_window)
            window++;
        if (has_rgb888)
            rgb888++;
        if (has_es2 && has_pbuffer && has_rgb888)
            es2_pbuffer_rgb++;
        if (has_es3 && has_window && has_rgb888)
            es3_window_rgb++;
        if (has_desired && has_pbuffer && has_rgb888)
            exact++;
        if (sampled < 12 &&
            (i < 4 || has_desired || (has_pbuffer && has_rgb888))) {
            log_config_detail(display, configs[i], i);
            sampled++;
        }
    }

    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_config_summary status=PASS api=%s total=%d desired_renderable=0x%x renderable_match=%d conformant_match=%d es2_renderable=%d es3_renderable=%d pbuffer=%d window=%d rgb888=%d exact_pbuffer_rgb=%d es2_pbuffer_rgb=%d es3_window_rgb=%d sampled=%d\n",
            smoke_api_name(api), got_configs, desired_renderable,
            renderable_match, conformant_match, es2_renderable,
            es3_renderable, pbuffer, window, rgb888, exact,
            es2_pbuffer_rgb, es3_window_rgb, sampled);
    log_chromium_config_shapes(display);
    free(configs);
}

static int
parse_api_value(const char *value, enum smoke_api *api)
{
    if (!value || !value[0])
        return 0;
    if (strcmp(value, "gles") == 0 || strcmp(value, "gles2") == 0 ||
        strcmp(value, "es") == 0 || strcmp(value, "es2") == 0) {
        *api = SMOKE_API_GLES;
        return 1;
    }
    if (strcmp(value, "gles3") == 0 || strcmp(value, "es3") == 0) {
        *api = SMOKE_API_GLES3;
        return 1;
    }
    if (strcmp(value, "gl") == 0 || strcmp(value, "opengl") == 0) {
        *api = SMOKE_API_GL;
        return 1;
    }
    return 0;
}

static int
parse_bool_value(const char *value, int *out)
{
    if (!value || !value[0])
        return 0;
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
        strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "no") == 0 || strcmp(value, "off") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int
select_options(int argc, char **argv, enum smoke_api *api, int *require_config,
               int *chromium_attempts)
{
    const char *env_api = getenv("HOST_EGL_GBM_SMOKE_API");
    const char *env_require_config =
        getenv("HOST_EGL_GBM_SMOKE_REQUIRE_CONFIG");
    const char *env_chromium_attempts =
        getenv("HOST_EGL_GBM_SMOKE_CHROMIUM_ATTEMPTS");

    *api = SMOKE_API_GLES;
    *require_config = 0;
    *chromium_attempts = 0;
    if (env_api && env_api[0] && !parse_api_value(env_api, api)) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=api_select status=FAIL source=env name=HOST_EGL_GBM_SMOKE_API value=%s reason=unsupported_api\n",
                env_api);
        return 0;
    }
    if (env_require_config && env_require_config[0] &&
        !parse_bool_value(env_require_config, require_config)) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=option_select status=FAIL source=env name=HOST_EGL_GBM_SMOKE_REQUIRE_CONFIG value=%s reason=unsupported_bool\n",
                env_require_config);
        return 0;
    }
    if (env_chromium_attempts && env_chromium_attempts[0] &&
        !parse_bool_value(env_chromium_attempts, chromium_attempts)) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=option_select status=FAIL source=env name=HOST_EGL_GBM_SMOKE_CHROMIUM_ATTEMPTS value=%s reason=unsupported_bool\n",
                env_chromium_attempts);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        const char *value = NULL;

        if (strncmp(argv[i], "--api=", 6) == 0) {
            value = argv[i] + 6;
        } else if (strcmp(argv[i], "--api") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "host-egl-gbm-gl-smoke: phase=api_select status=FAIL source=argv option=--api reason=missing_value\n");
                return 0;
            }
            value = argv[++i];
        } else if (strcmp(argv[i], "--require-config") == 0) {
            *require_config = 1;
            continue;
        } else if (strcmp(argv[i], "--chromium-attempts") == 0) {
            *chromium_attempts = 1;
            continue;
        } else if (strncmp(argv[i], "--chromium-attempts=", 20) == 0) {
            value = argv[i] + 20;
            if (!parse_bool_value(value, chromium_attempts)) {
                fprintf(stderr,
                        "host-egl-gbm-gl-smoke: phase=option_select status=FAIL source=argv option=--chromium-attempts value=%s reason=unsupported_bool\n",
                        value);
                return 0;
            }
            continue;
        } else if (strncmp(argv[i], "--require-config=", 17) == 0) {
            value = argv[i] + 17;
            if (!parse_bool_value(value, require_config)) {
                fprintf(stderr,
                        "host-egl-gbm-gl-smoke: phase=option_select status=FAIL source=argv option=--require-config value=%s reason=unsupported_bool\n",
                        value);
                return 0;
            }
            continue;
        } else {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=api_select status=FAIL source=argv option=%s reason=unknown_option\n",
                    argv[i]);
            return 0;
        }

        if (!parse_api_value(value, api)) {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=api_select status=FAIL source=argv option=--api value=%s reason=unsupported_api\n",
                    value);
            return 0;
        }
    }
    return 1;
}

static EGLDisplay
get_gbm_display(struct gbm_device *gbm)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;

    get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
            "eglGetPlatformDisplay");
    if (get_platform_display) {
        return get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    }

    get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
            "eglGetPlatformDisplayEXT");
    if (get_platform_display) {
        return get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    }

    return eglGetDisplay((EGLNativeDisplayType)gbm);
}

int
main(int argc, char **argv)
{
    static const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE,
    };
    EGLint gles_context_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    static const EGLint gl_context_attrs[] = {
        EGL_NONE,
    };
    enum smoke_api api;
    EGLint config_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, 0,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE,
    };
    const EGLint *context_attrs;
    int fd = -1;
    struct gbm_device *gbm = NULL;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = NULL;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLint major = 0;
    EGLint minor = 0;
    EGLint nconfigs = 0;
    EGLint total_configs = 0;
    const char *extensions;
    const char *version;
    int have_configless;
    int have_surfaceless;
    int require_config;
    int chromium_attempts;
    int use_configless = 0;
    int use_surfaceless = 0;
    int rc = 1;

    if (!select_options(argc, argv, &api, &require_config,
                        &chromium_attempts)) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=result status=FAIL exit_status=2\n");
        return 2;
    }
    config_attrs[3] = smoke_renderable_type(api);
    gles_context_attrs[1] = smoke_context_client_version(api);
    context_attrs = api == SMOKE_API_GL ? gl_context_attrs : gles_context_attrs;

    fprintf(stderr, "host-egl-gbm-gl-smoke: phase=start status=BEGIN api=%s\n",
            smoke_api_name(api));
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: diag env EGL_PLATFORM=%s HOST_EGL_GBM_SMOKE_API=%s HOST_EGL_GBM_SMOKE_REQUIRE_CONFIG=%s HOST_EGL_GBM_SMOKE_CHROMIUM_ATTEMPTS=%s GALLIUM_DRIVER=%s MESA_LOADER_DRIVER_OVERRIDE=%s LIBGL_DRIVERS_PATH=%s GBM_BACKENDS_PATH=%s VIRGL_DEBUG=%s\n",
            safe_str(getenv("EGL_PLATFORM")),
            safe_str(getenv("HOST_EGL_GBM_SMOKE_API")),
            safe_str(getenv("HOST_EGL_GBM_SMOKE_REQUIRE_CONFIG")),
            safe_str(getenv("HOST_EGL_GBM_SMOKE_CHROMIUM_ATTEMPTS")),
            safe_str(getenv("GALLIUM_DRIVER")),
            safe_str(getenv("MESA_LOADER_DRIVER_OVERRIDE")),
            safe_str(getenv("LIBGL_DRIVERS_PATH")),
            safe_str(getenv("GBM_BACKENDS_PATH")),
            safe_str(getenv("VIRGL_DEBUG")));
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: diag option require_config=%d chromium_attempts=%d\n",
            require_config, chromium_attempts);
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: diag egl_no_display_extensions=%s\n",
            safe_str(eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)));
    clear_egl_error();

    fd = open(render_node, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("host-egl-gbm-gl-smoke: open render node");
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=render_node status=FAIL api=%s path=%s\n",
                smoke_api_name(api), render_node);
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=result status=FAIL api=%s exit_status=1\n",
                smoke_api_name(api));
        return 1;
    }
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=render_node status=PASS api=%s path=%s fd=%d\n",
            smoke_api_name(api), render_node, fd);
    log_virtgpu_getparams(fd);

    gbm = gbm_create_device(fd);
    if (!gbm) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=gbm_create_device status=FAIL api=%s\n",
                smoke_api_name(api));
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=result status=FAIL api=%s exit_status=1\n",
                smoke_api_name(api));
        close(fd);
        return 1;
    }
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=gbm_create_device status=PASS api=%s backend=%s\n",
            smoke_api_name(api), safe_str(gbm_device_get_backend_name(gbm)));

    display = get_gbm_display(gbm);
    if (display == EGL_NO_DISPLAY) {
        phase_fail("egl_get_display", api, "no_display");
        goto fail;
    }
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_get_display status=PASS api=%s\n",
            smoke_api_name(api));

    if (!eglInitialize(display, &major, &minor)) {
        phase_fail("egl_initialize", api, "eglInitialize");
        goto fail;
    }
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_initialize status=PASS api=%s major=%d minor=%d vendor=%s version=%s client_apis=%s extensions=%s\n",
            smoke_api_name(api), major, minor,
            safe_str(eglQueryString(display, EGL_VENDOR)),
            safe_str(eglQueryString(display, EGL_VERSION)),
            safe_str(eglQueryString(display, EGL_CLIENT_APIS)),
            safe_str(eglQueryString(display, EGL_EXTENSIONS)));
    extensions = eglQueryString(display, EGL_EXTENSIONS);
    have_configless =
        has_extension(extensions, "EGL_MESA_configless_context") ||
        has_extension(extensions, "EGL_KHR_no_config_context");
    have_surfaceless =
        has_extension(extensions, "EGL_KHR_surfaceless_context");
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_extension_probe status=PASS api=%s configless=%d surfaceless=%d mesa_configless=%d khr_no_config=%d khr_surfaceless=%d\n",
            smoke_api_name(api), have_configless, have_surfaceless,
            has_extension(extensions, "EGL_MESA_configless_context"),
            has_extension(extensions, "EGL_KHR_no_config_context"),
            has_extension(extensions, "EGL_KHR_surfaceless_context"));

    if (!eglBindAPI(smoke_egl_api(api))) {
        phase_fail("egl_bind_api", api, smoke_egl_api_name(api));
        goto fail;
    }
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_bind_api status=PASS api=%s egl_api=%s\n",
            smoke_api_name(api), smoke_egl_api_name(api));

    clear_egl_error();
    if (eglGetConfigs(display, NULL, 0, &total_configs)) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_get_configs status=PASS api=%s count=%d\n",
                smoke_api_name(api), total_configs);
        log_config_summary(display, api, total_configs);
        if (chromium_attempts)
            log_chromium_config_attempts(display, gbm);
    } else {
        EGLint err = eglGetError();

        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_get_configs status=FAIL api=%s egl_error=0x%x error_name=%s\n",
                smoke_api_name(api), err, egl_error_name(err));
    }

    clear_egl_error();
    if (!eglChooseConfig(display, config_attrs, &config, 1, &nconfigs)) {
        EGLint err = eglGetError();

        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_choose_config status=FAIL api=%s reason=eglChooseConfig renderable=0x%x egl_error=0x%x error_name=%s\n",
                smoke_api_name(api), config_attrs[3], err,
                egl_error_name(err));
        goto fail;
    }

    if (nconfigs < 1) {
        EGLint err = eglGetError();

        if (require_config) {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=egl_choose_config status=FAIL api=%s reason=no_config_required renderable=0x%x egl_error=0x%x error_name=%s require_config=1 total_configs=%d\n",
                    smoke_api_name(api), config_attrs[3], err,
                    egl_error_name(err), total_configs);
            goto fail;
        }
        if (!have_configless) {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=egl_choose_config status=FAIL api=%s reason=no_config renderable=0x%x egl_error=0x%x error_name=%s configless=0\n",
                    smoke_api_name(api), config_attrs[3], err,
                    egl_error_name(err));
            goto fail;
        }
        use_configless = 1;
        config = EGL_NO_CONFIG_MESA;
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_choose_config status=PASS api=%s count=0 reason=no_config renderable=0x%x egl_error=0x%x error_name=%s fallback=configless\n",
                smoke_api_name(api), config_attrs[3], err,
                egl_error_name(err));
    } else {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_choose_config status=PASS api=%s count=%d renderable=0x%x\n",
                smoke_api_name(api), nconfigs, config_attrs[3]);
    }

    if (use_configless) {
        if (!have_surfaceless) {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=egl_create_surface status=FAIL api=%s reason=configless_requires_surfaceless surfaceless=0\n",
                    smoke_api_name(api));
            goto fail;
        }
        use_surfaceless = 1;
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=egl_surface_selection status=PASS api=%s kind=surfaceless reason=configless_context fallback=surfaceless\n",
                smoke_api_name(api));
    } else {
        surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
        if (surface == EGL_NO_SURFACE) {
            EGLint err = eglGetError();

            if (!have_surfaceless) {
                fprintf(stderr,
                        "host-egl-gbm-gl-smoke: phase=egl_create_surface status=FAIL api=%s reason=eglCreatePbufferSurface egl_error=0x%x error_name=%s surfaceless=0\n",
                        smoke_api_name(api), err, egl_error_name(err));
                goto fail;
            }
            use_surfaceless = 1;
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=egl_surface_selection status=PASS api=%s kind=surfaceless reason=pbuffer_failed egl_error=0x%x error_name=%s fallback=surfaceless\n",
                    smoke_api_name(api), err, egl_error_name(err));
        } else {
            fprintf(stderr,
                    "host-egl-gbm-gl-smoke: phase=egl_create_surface status=PASS api=%s kind=pbuffer\n",
                    smoke_api_name(api));
        }
    }

    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    if (context == EGL_NO_CONTEXT) {
        phase_fail("egl_create_context", api, "eglCreateContext");
        goto fail;
    }
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_create_context status=PASS api=%s context_attrs=%s config=%s\n",
            smoke_api_name(api),
            api == SMOKE_API_GL ? "none" :
            (api == SMOKE_API_GLES3 ? "client_version=3" :
             "client_version=2"),
            use_configless ? "configless" : "chosen");

    if (!eglMakeCurrent(display,
                        use_surfaceless ? EGL_NO_SURFACE : surface,
                        use_surfaceless ? EGL_NO_SURFACE : surface,
                        context)) {
        phase_fail("egl_make_current", api, "eglMakeCurrent");
        goto fail;
    }
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=egl_make_current status=PASS api=%s surface=%s\n",
            smoke_api_name(api), use_surfaceless ? "surfaceless" : "pbuffer");

    version = (const char *)glGetString(GL_VERSION);
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=gl_strings status=%s api=%s vendor=%s renderer=%s version=%s\n",
            version ? "PASS" : "FAIL",
            smoke_api_name(api),
            safe_str((const char *)glGetString(GL_VENDOR)),
            safe_str((const char *)glGetString(GL_RENDERER)),
            safe_str(version));
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=gl_extension_probe status=%s api=%s gl_oes_surfaceless_context=%d\n",
            version ? "PASS" : "FAIL", smoke_api_name(api),
            has_extension((const char *)glGetString(GL_EXTENSIONS),
                          "GL_OES_surfaceless_context"));
    rc = version ? 0 : 1;
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=result status=%s api=%s exit_status=%d\n",
            version ? "PASS" : "FAIL", smoke_api_name(api), rc);

    if (display != EGL_NO_DISPLAY)
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT)
        eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(display, surface);
    if (display != EGL_NO_DISPLAY)
        eglTerminate(display);
    if (gbm)
        gbm_device_destroy(gbm);
    if (fd >= 0)
        close(fd);
    return rc;

fail:
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: phase=result status=FAIL api=%s exit_status=1\n",
            smoke_api_name(api));
    if (display != EGL_NO_DISPLAY)
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT)
        eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(display, surface);
    if (display != EGL_NO_DISPLAY)
        eglTerminate(display);
    if (gbm)
        gbm_device_destroy(gbm);
    if (fd >= 0)
        close(fd);
    return 1;
}
