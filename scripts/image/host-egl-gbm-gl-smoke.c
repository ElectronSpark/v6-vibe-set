// Xwayland-style EGL/GBM reducer.
//
// This host-built probe avoids X11 entirely and checks the GLAMOR substrate
// Xwayland uses before it can advertise useful accelerated X11 behavior:
// render node -> GBM device -> EGL display/context/surface -> glGetString().

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static const char *render_node = "/dev/dri/renderD128";

enum smoke_api {
    SMOKE_API_GLES,
    SMOKE_API_GL,
};

static const char *
smoke_api_name(enum smoke_api api)
{
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
    return api == SMOKE_API_GL ? EGL_OPENGL_BIT : EGL_OPENGL_ES2_BIT;
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
    if (strcmp(value, "gl") == 0 || strcmp(value, "opengl") == 0) {
        *api = SMOKE_API_GL;
        return 1;
    }
    return 0;
}

static int
select_api(int argc, char **argv, enum smoke_api *api)
{
    const char *env_api = getenv("HOST_EGL_GBM_SMOKE_API");

    *api = SMOKE_API_GLES;
    if (env_api && env_api[0] && !parse_api_value(env_api, api)) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=api_select status=FAIL source=env name=HOST_EGL_GBM_SMOKE_API value=%s reason=unsupported_api\n",
                env_api);
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
    static const EGLint gles_context_attrs[] = {
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
    const char *extensions;
    const char *version;
    int have_configless;
    int have_surfaceless;
    int use_configless = 0;
    int use_surfaceless = 0;
    int rc = 1;

    if (!select_api(argc, argv, &api)) {
        fprintf(stderr,
                "host-egl-gbm-gl-smoke: phase=result status=FAIL exit_status=2\n");
        return 2;
    }
    config_attrs[3] = smoke_renderable_type(api);
    context_attrs = api == SMOKE_API_GL ? gl_context_attrs : gles_context_attrs;

    fprintf(stderr, "host-egl-gbm-gl-smoke: phase=start status=BEGIN api=%s\n",
            smoke_api_name(api));
    fprintf(stderr,
            "host-egl-gbm-gl-smoke: diag env EGL_PLATFORM=%s HOST_EGL_GBM_SMOKE_API=%s GALLIUM_DRIVER=%s MESA_LOADER_DRIVER_OVERRIDE=%s LIBGL_DRIVERS_PATH=%s GBM_BACKENDS_PATH=%s VIRGL_DEBUG=%s\n",
            safe_str(getenv("EGL_PLATFORM")),
            safe_str(getenv("HOST_EGL_GBM_SMOKE_API")),
            safe_str(getenv("GALLIUM_DRIVER")),
            safe_str(getenv("MESA_LOADER_DRIVER_OVERRIDE")),
            safe_str(getenv("LIBGL_DRIVERS_PATH")),
            safe_str(getenv("GBM_BACKENDS_PATH")),
            safe_str(getenv("VIRGL_DEBUG")));
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
            api == SMOKE_API_GL ? "none" : "client_version=2",
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
