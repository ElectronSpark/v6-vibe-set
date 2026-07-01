#!/usr/bin/env bash
# Capture a Linux KVM+virgl control for Chromium-shaped EGL/GBM configs.
#
# This is a host-side comparison tool. It boots Alpine Linux with
# virtio-vga-gl, installs Mesa development packages, compiles a tiny GBM/EGL
# probe inside the Linux guest, and records whether Chromium-style pbuffer,
# window+pbuffer, window-only, and surfaceless config/context profiles exist.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${ROOT}"

ALPINE_VERSION="${ALPINE_VERSION:-3.23.4}"
ALPINE_FLAVOR="${ALPINE_FLAVOR:-standard}"
ALPINE_ARCH="${ALPINE_ARCH:-x86_64}"
CAPTURE_DIR="${LINUX_GBM_SHAPE_DIR:-${ROOT}/build-x86_64/linux-virgl-gbm-chromium-shape/$(date -u +%Y%m%dT%H%M%SZ)}"
WORK_DIR="${LINUX_GBM_SHAPE_WORK_DIR:-/tmp/linux-virgl-gbm-proof}"
ISO="${ALPINE_ISO:-${WORK_DIR}/alpine-${ALPINE_FLAVOR}-${ALPINE_VERSION}-${ALPINE_ARCH}.iso}"
ISO_URL="${ALPINE_ISO_URL:-https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/releases/${ALPINE_ARCH}/$(basename "${ISO}")}"
BOOT_DIR="${WORK_DIR}/boot"
KERNEL="${BOOT_DIR}/vmlinuz-lts"
INITRD="${BOOT_DIR}/initramfs-lts"
TRACE_EVENTS="${CAPTURE_DIR}/qemu-trace-events"
QEMU_TRACE="${CAPTURE_DIR}/qemu-virtio-gpu.trace"
SERIAL_LOG="${CAPTURE_DIR}/serial.log"
HOST_LOG="${CAPTURE_DIR}/host.log"
STATUS_LOG="${CAPTURE_DIR}/status.log"
VERDICT_LOG="${CAPTURE_DIR}/linux-vm-gbm-chromium-shape-verdict.log"
PROBE_C="${CAPTURE_DIR}/linux-gbm-chromium-shape.c"
EXPECT_SCRIPT="${CAPTURE_DIR}/run-linux-gbm-chromium-shape.expect"
DISPLAY_BACKEND="${LINUX_GBM_SHAPE_QEMU_DISPLAY:-gtk,gl=on,show-cursor=off}"
QEMU_DEVICE="${LINUX_GBM_SHAPE_QEMU_DEVICE:-virtio-vga-gl,xres=1280,yres=800}"

mkdir -p "${CAPTURE_DIR}" "${WORK_DIR}" "${BOOT_DIR}"

need()
{
    command -v "$1" >/dev/null 2>&1 || {
        echo "linux-virgl-gbm-chromium-shape: missing required tool: $1" >&2
        exit 1
    }
}

need base64
need curl
need expect
need qemu-system-x86_64

if [[ ! -f "${ISO}" ]]; then
    echo "linux-virgl-gbm-chromium-shape: downloading ${ISO_URL}"
    curl -fL "${ISO_URL}" -o "${ISO}"
fi

if [[ ! -f "${KERNEL}" || ! -f "${INITRD}" ]]; then
    need xorriso
    rm -rf "${BOOT_DIR}"
    mkdir -p "${BOOT_DIR}"
    xorriso -osirrox on -indev "${ISO}" \
        -extract /boot/vmlinuz-lts "${KERNEL}" \
        -extract /boot/initramfs-lts "${INITRD}" \
        >"${CAPTURE_DIR}/xorriso-extract.log" 2>&1
fi

cat >"${TRACE_EVENTS}" <<'EOF_TRACE'
virtio_gpu_features
virtio_gpu_cmd_get_display_info
virtio_gpu_cmd_get_edid
virtio_gpu_cmd_set_scanout
virtio_gpu_cmd_res_create_3d
virtio_gpu_cmd_res_create_blob
virtio_gpu_cmd_res_flush
virtio_gpu_cmd_ctx_create
virtio_gpu_cmd_ctx_submit
virtio_gpu_fence_ctrl
virtio_gpu_fence_resp
EOF_TRACE

cat >"${PROBE_C}" <<'EOF_C'
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

#ifndef EGL_CONFORMANT
#define EGL_CONFORMANT 0x3042
#endif

#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR 0x31BD
#endif

#ifndef EGL_CONTEXT_OPENGL_ROBUST_ACCESS_KHR
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_KHR 0x31B2
#endif

#ifndef EGL_NO_RESET_NOTIFICATION_KHR
#define EGL_NO_RESET_NOTIFICATION_KHR 0x31BE
#endif

#ifndef EGL_LOSE_CONTEXT_ON_RESET_KHR
#define EGL_LOSE_CONTEXT_ON_RESET_KHR 0x31BF
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

typedef EGLDisplay (*egl_get_platform_display_ext_fn)(EGLenum, void *,
                                                      const EGLint *);

struct profile {
    const char *name;
    EGLint renderable;
    EGLint surface;
    int client_version;
    const char *surface_kind;
    const char *context_attr_profile;
};

static const char *egl_error_name(EGLint err)
{
    switch (err) {
    case EGL_SUCCESS: return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
    default: return "EGL_UNKNOWN";
    }
}

static const char *safe_str(const char *s)
{
    return s ? s : "(null)";
}

static int has_extension(const char *extensions, const char *needle)
{
    size_t n;
    const char *p;

    if (!extensions || !needle || !needle[0])
        return 0;
    n = strlen(needle);
    for (p = extensions; (p = strstr(p, needle)) != NULL; p += n) {
        int left = p == extensions || p[-1] == ' ';
        int right = p[n] == '\0' || p[n] == ' ';
        if (left && right)
            return 1;
    }
    return 0;
}

static void clear_egl_error(void)
{
    while (eglGetError() != EGL_SUCCESS)
        ;
}

static const char *virtgpu_param_name(uint64_t param)
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

static void log_virtgpu_getparams(int fd)
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
            printf("linux-gbm-shape: phase=virtgpu_getparam status=PASS param=%lu name=%s ret=%d errno=%d value=%lu\n",
                   (unsigned long)param, virtgpu_param_name(param), ret,
                   saved_errno, (unsigned long)value);
        } else {
            printf("linux-gbm-shape: phase=virtgpu_getparam status=FAIL param=%lu name=%s ret=%d errno=%d value=NA\n",
                   (unsigned long)param, virtgpu_param_name(param), ret,
                   saved_errno);
        }
    }
}

static EGLint attr_or_neg1(EGLDisplay display, EGLConfig config, EGLint attr)
{
    EGLint value = -1;

    if (!config)
        return -1;
    if (!eglGetConfigAttrib(display, config, attr, &value))
        return -1;
    return value;
}

static int choose_profile(EGLDisplay display, const struct profile *profile,
                          EGLConfig *config_out, EGLint *count_out)
{
    EGLint attrs[] = {
        EGL_BUFFER_SIZE, 32,
        EGL_ALPHA_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_RENDERABLE_TYPE, profile->renderable,
        EGL_SURFACE_TYPE, profile->surface,
        EGL_NONE,
    };
    EGLint count = 0;
    EGLint chosen = 0;
    EGLConfig config = NULL;

    clear_egl_error();
    if (!eglChooseConfig(display, attrs, NULL, 0, &count)) {
        *count_out = count;
        *config_out = NULL;
        return 0;
    }
    if (count > 0 &&
        eglChooseConfig(display, attrs, &config, 1, &chosen) &&
        chosen > 0) {
        *config_out = config;
    } else {
        *config_out = NULL;
    }
    *count_out = count;
    return 1;
}

static void log_profile_shape(EGLDisplay display, const struct profile *profile)
{
    EGLConfig config = NULL;
    EGLint count = 0;
    int ok = choose_profile(display, profile, &config, &count);
    EGLint err = eglGetError();

    printf("linux-gbm-shape: phase=chromium_config status=%s profile=%s "
           "renderable=0x%x surface=0x%x buffer_size=32 alpha=8 red=8 "
           "green=8 blue=8 count=%d egl_error=0x%x error_name=%s "
           "first_config_id=%d first_renderable=0x%x first_surface=0x%x "
           "first_buffer=%d first_alpha=%d\n",
           ok ? "PASS" : "FAIL", profile->name, profile->renderable,
           profile->surface, count, err, egl_error_name(err),
           attr_or_neg1(display, config, EGL_CONFIG_ID),
           attr_or_neg1(display, config, EGL_RENDERABLE_TYPE),
           attr_or_neg1(display, config, EGL_SURFACE_TYPE),
           attr_or_neg1(display, config, EGL_BUFFER_SIZE),
           attr_or_neg1(display, config, EGL_ALPHA_SIZE));
}

static int try_context(EGLDisplay display, struct gbm_device *gbm,
                       const struct profile *profile)
{
    EGLConfig config = NULL;
    EGLint count = 0;
    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, profile->client_version,
        EGL_NONE,
    };
    EGLint robust_no_reset_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, profile->client_version,
        EGL_CONTEXT_OPENGL_ROBUST_ACCESS_KHR, EGL_TRUE,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
        EGL_NO_RESET_NOTIFICATION_KHR,
        EGL_NONE,
    };
    EGLint robust_lose_reset_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, profile->client_version,
        EGL_CONTEXT_OPENGL_ROBUST_ACCESS_KHR, EGL_TRUE,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
        EGL_LOSE_CONTEXT_ON_RESET_KHR,
        EGL_NONE,
    };
    EGLint no_error_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, profile->client_version,
        EGL_CONTEXT_OPENGL_NO_ERROR_KHR, EGL_TRUE,
        EGL_NONE,
    };
    EGLint priority_high_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, profile->client_version,
        EGL_CONTEXT_PRIORITY_LEVEL_IMG, EGL_CONTEXT_PRIORITY_HIGH_IMG,
        EGL_NONE,
    };
    EGLint chrome_combo_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, profile->client_version,
        EGL_CONTEXT_OPENGL_ROBUST_ACCESS_KHR, EGL_TRUE,
        EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR,
        EGL_LOSE_CONTEXT_ON_RESET_KHR,
        EGL_CONTEXT_OPENGL_NO_ERROR_KHR, EGL_TRUE,
        EGL_CONTEXT_PRIORITY_LEVEL_IMG, EGL_CONTEXT_PRIORITY_HIGH_IMG,
        EGL_NONE,
    };
    EGLint *selected_ctx_attrs = ctx_attrs;
    EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE,
    };
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    struct gbm_surface *gbm_surface = NULL;
    int pass = 0;
    EGLint err;
    const char *reason = "ok";

    if (strcmp(profile->context_attr_profile, "robust_no_reset") == 0)
        selected_ctx_attrs = robust_no_reset_attrs;
    else if (strcmp(profile->context_attr_profile, "robust_lose_reset") == 0)
        selected_ctx_attrs = robust_lose_reset_attrs;
    else if (strcmp(profile->context_attr_profile, "no_error") == 0)
        selected_ctx_attrs = no_error_attrs;
    else if (strcmp(profile->context_attr_profile, "priority_high") == 0)
        selected_ctx_attrs = priority_high_attrs;
    else if (strcmp(profile->context_attr_profile, "chrome_combo") == 0)
        selected_ctx_attrs = chrome_combo_attrs;

    if (!choose_profile(display, profile, &config, &count)) {
        reason = "choose_failed";
        goto out;
    }
    if (count <= 0 || !config) {
        reason = "no_config";
        goto out;
    }
    context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                               selected_ctx_attrs);
    if (context == EGL_NO_CONTEXT) {
        reason = "eglCreateContext";
        goto out;
    }
    if (strcmp(profile->surface_kind, "pbuffer") == 0) {
        surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
        if (surface == EGL_NO_SURFACE) {
            reason = "eglCreatePbufferSurface";
            goto out;
        }
    } else if (strcmp(profile->surface_kind, "gbm-window") == 0) {
        gbm_surface = gbm_surface_create(gbm, 16, 16, GBM_FORMAT_ARGB8888,
                                         GBM_BO_USE_RENDERING);
        if (!gbm_surface) {
            reason = "gbm_surface_create";
            goto out;
        }
        surface = eglCreateWindowSurface(display, config,
                                         (EGLNativeWindowType)gbm_surface,
                                         NULL);
        if (surface == EGL_NO_SURFACE) {
            reason = "eglCreateWindowSurface";
            goto out;
        }
    }
    if (!eglMakeCurrent(display, surface, surface, context)) {
        reason = "eglMakeCurrent";
        goto out;
    }
    pass = 1;
    printf("linux-gbm-shape: phase=gl_identity status=PASS profile=%s "
           "vendor=%s renderer=%s version=%s\n",
           profile->name, safe_str((const char *)glGetString(GL_VENDOR)),
           safe_str((const char *)glGetString(GL_RENDERER)),
           safe_str((const char *)glGetString(GL_VERSION)));

out:
    err = eglGetError();
    printf("linux-gbm-shape: phase=context_attempt status=%s profile=%s "
           "renderable=0x%x surface=0x%x surface_kind=%s "
           "context_attr_profile=%s count=%d config_id=%d "
           "context_client_version=%d egl_error=0x%x error_name=%s "
           "reason=%s\n",
           pass ? "PASS" : "FAIL", profile->name, profile->renderable,
           profile->surface, profile->surface_kind,
           profile->context_attr_profile, count,
           attr_or_neg1(display, config, EGL_CONFIG_ID),
           profile->client_version, err, egl_error_name(err), reason);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface != EGL_NO_SURFACE)
        eglDestroySurface(display, surface);
    if (context != EGL_NO_CONTEXT)
        eglDestroyContext(display, context);
    if (gbm_surface)
        gbm_surface_destroy(gbm_surface);
    return pass;
}

int main(void)
{
    static const struct profile profiles[] = {
        {"chromium_offscreen_pbuffer_es3", EGL_OPENGL_ES3_BIT, EGL_PBUFFER_BIT, 3, "pbuffer", "base"},
        {"chromium_offscreen_pbuffer_es2", EGL_OPENGL_ES2_BIT, EGL_PBUFFER_BIT, 2, "pbuffer", "base"},
        {"chromium_native_window_pbuffer_es3", EGL_OPENGL_ES3_BIT, EGL_PBUFFER_BIT | EGL_WINDOW_BIT, 3, "gbm-window", "base"},
        {"chromium_native_window_pbuffer_es2", EGL_OPENGL_ES2_BIT, EGL_PBUFFER_BIT | EGL_WINDOW_BIT, 2, "gbm-window", "base"},
        {"chromium_window_only_gbm_surface_es3", EGL_OPENGL_ES3_BIT, EGL_WINDOW_BIT, 3, "gbm-window", "base"},
        {"chromium_window_only_gbm_surface_es2", EGL_OPENGL_ES2_BIT, EGL_WINDOW_BIT, 2, "gbm-window", "base"},
        {"chromium_window_only_surfaceless_es3", EGL_OPENGL_ES3_BIT, EGL_WINDOW_BIT, 3, "surfaceless", "base"},
        {"chromium_window_only_surfaceless_es2", EGL_OPENGL_ES2_BIT, EGL_WINDOW_BIT, 2, "surfaceless", "base"},
        {"chromium_surfaceless_es3", EGL_OPENGL_ES3_BIT, EGL_DONT_CARE, 3, "surfaceless", "base"},
        {"chromium_surfaceless_es2", EGL_OPENGL_ES2_BIT, EGL_DONT_CARE, 2, "surfaceless", "base"},
        {"chromium_surfaceless_es3_robust_no_reset", EGL_OPENGL_ES3_BIT, EGL_DONT_CARE, 3, "surfaceless", "robust_no_reset"},
        {"chromium_surfaceless_es3_robust_lose_reset", EGL_OPENGL_ES3_BIT, EGL_DONT_CARE, 3, "surfaceless", "robust_lose_reset"},
        {"chromium_surfaceless_es3_no_error", EGL_OPENGL_ES3_BIT, EGL_DONT_CARE, 3, "surfaceless", "no_error"},
        {"chromium_surfaceless_es3_priority_high", EGL_OPENGL_ES3_BIT, EGL_DONT_CARE, 3, "surfaceless", "priority_high"},
        {"chromium_surfaceless_es3_chrome_combo", EGL_OPENGL_ES3_BIT, EGL_DONT_CARE, 3, "surfaceless", "chrome_combo"},
    };
    int fd = -1;
    struct gbm_device *gbm = NULL;
    EGLDisplay display = EGL_NO_DISPLAY;
    egl_get_platform_display_ext_fn get_platform_display = NULL;
    EGLint major = 0;
    EGLint minor = 0;
    EGLint config_count = 0;
    EGLConfig configs[512];
    int total = 0, pbuffer = 0, window = 0, rgb888 = 0;
    int es2 = 0, es3 = 0, exact_pbuffer_rgb = 0, es2_pbuffer_rgb = 0;
    int es3_window_rgb = 0;
    int attempts = 0, pass = 0, fail = 0;
    const char *display_extensions;
    const char *egl_extensions;

    fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open renderD128");
        return 1;
    }
    log_virtgpu_getparams(fd);
    gbm = gbm_create_device(fd);
    if (!gbm) {
        fprintf(stderr, "gbm_create_device failed\n");
        return 1;
    }

    display_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_platform_display =
        (egl_get_platform_display_ext_fn)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display &&
        has_extension(display_extensions, "EGL_EXT_platform_base")) {
        display = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    }
    if (display == EGL_NO_DISPLAY)
        display = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
        EGLint err = eglGetError();

        fprintf(stderr, "eglInitialize failed error=0x%x %s\n", err,
                egl_error_name(err));
        return 1;
    }
    egl_extensions = eglQueryString(display, EGL_EXTENSIONS);
    printf("linux-gbm-shape: phase=egl_initialize status=PASS major=%d minor=%d "
           "vendor=%s version=%s client_apis=%s surfaceless=%d\n",
           major, minor, safe_str(eglQueryString(display, EGL_VENDOR)),
           safe_str(eglQueryString(display, EGL_VERSION)),
           safe_str(eglQueryString(display, EGL_CLIENT_APIS)),
           has_extension(egl_extensions, "EGL_KHR_surfaceless_context"));
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        EGLint err = eglGetError();

        fprintf(stderr, "eglBindAPI failed error=0x%x %s\n", err,
                egl_error_name(err));
        return 1;
    }
    if (!eglGetConfigs(display, configs, 512, &config_count)) {
        EGLint err = eglGetError();

        fprintf(stderr, "eglGetConfigs failed error=0x%x %s\n", err,
                egl_error_name(err));
        return 1;
    }
    for (int i = 0; i < config_count; i++) {
        EGLint renderable = attr_or_neg1(display, configs[i], EGL_RENDERABLE_TYPE);
        EGLint surface = attr_or_neg1(display, configs[i], EGL_SURFACE_TYPE);
        EGLint red = attr_or_neg1(display, configs[i], EGL_RED_SIZE);
        EGLint green = attr_or_neg1(display, configs[i], EGL_GREEN_SIZE);
        EGLint blue = attr_or_neg1(display, configs[i], EGL_BLUE_SIZE);
        EGLint alpha = attr_or_neg1(display, configs[i], EGL_ALPHA_SIZE);
        int has_rgb = red >= 8 && green >= 8 && blue >= 8 && alpha >= 8;

        total++;
        if (surface & EGL_PBUFFER_BIT)
            pbuffer++;
        if (surface & EGL_WINDOW_BIT)
            window++;
        if (has_rgb)
            rgb888++;
        if (renderable & EGL_OPENGL_ES2_BIT)
            es2++;
        if (renderable & EGL_OPENGL_ES3_BIT)
            es3++;
        if ((surface & EGL_PBUFFER_BIT) && has_rgb)
            exact_pbuffer_rgb++;
        if ((surface & EGL_PBUFFER_BIT) && (renderable & EGL_OPENGL_ES2_BIT) && has_rgb)
            es2_pbuffer_rgb++;
        if ((surface & EGL_WINDOW_BIT) && (renderable & EGL_OPENGL_ES3_BIT) && has_rgb)
            es3_window_rgb++;
    }
    printf("linux-gbm-shape: phase=config_summary status=PASS total=%d "
           "es2_renderable=%d es3_renderable=%d pbuffer=%d window=%d "
           "rgb888=%d exact_pbuffer_rgb=%d es2_pbuffer_rgb=%d "
           "es3_window_rgb=%d\n",
           total, es2, es3, pbuffer, window, rgb888, exact_pbuffer_rgb,
           es2_pbuffer_rgb, es3_window_rgb);
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++)
        log_profile_shape(display, &profiles[i]);
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        attempts++;
        if (try_context(display, gbm, &profiles[i]))
            pass++;
        else
            fail++;
    }
    printf("LINUX_GBM_SHAPE_RESULT status=COMPLETE attempts=%d pass=%d fail=%d\n",
           attempts, pass, fail);
    eglTerminate(display);
    gbm_device_destroy(gbm);
    close(fd);
    return 0;
}
EOF_C

PROBE_B64="$(base64 "${PROBE_C}")"

cat >"${EXPECT_SCRIPT}" <<EOF_EXPECT
#!/usr/bin/expect -f
set timeout 1800
match_max 6000000
log_file -a "${SERIAL_LOG}"

spawn qemu-system-x86_64 \\
  -enable-kvm -cpu host -smp 4 -m 4096 \\
  -kernel "${KERNEL}" -initrd "${INITRD}" \\
  -append "modules=loop,squashfs,sd-mod,usb-storage quiet console=ttyS0" \\
  -cdrom "${ISO}" -boot d \\
  -vga none -device "${QEMU_DEVICE}" \\
  -display "${DISPLAY_BACKEND}" \\
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \\
  -serial stdio -monitor none -no-reboot \\
  -trace "events=${TRACE_EVENTS},file=${QEMU_TRACE}"

expect {
  -re "localhost login:" { send "root\r" }
  -re "alpine login:" { send "root\r" }
  timeout { puts "LINUX_GBM_SHAPE_FAIL login-timeout"; exit 2 }
}
expect {
  -re "localhost:~#" {}
  -re "~ #" {}
  -re "# " {}
  timeout { puts "LINUX_GBM_SHAPE_FAIL prompt-timeout"; exit 3 }
}
proc cmd {s {t 900}} {
  set timeout \$t
  send -- "\$s\r"
  expect {
    -re "LGBM_CTRL# " {}
    timeout { puts "LINUX_GBM_SHAPE_FAIL command-timeout command=\$s"; exit 4 }
  }
}
send -- "export PS1='LGBM_CTRL# '\r"
expect -re "LGBM_CTRL# "
cmd "date -Iseconds; uname -a; cat /etc/alpine-release" 60
cmd "ip link set eth0 up || true; udhcpc -i eth0 -q -n || true; ip addr show eth0" 120
cmd "printf '%s\\n' https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/main https://dl-cdn.alpinelinux.org/alpine/v${ALPINE_VERSION%.*}/community > /etc/apk/repositories; apk update" 300
cmd "apk add --no-cache build-base mesa-dev mesa-dri-gallium mesa-egl mesa-gbm mesa-gles libdrm-dev eudev hwdata pciutils" 1200
cmd "apk info -vv mesa-dev mesa-dri-gallium mesa-egl mesa-gbm mesa-gles libdrm | sed 's/^/LINUX_GBM_PACKAGE /'" 60
cmd "modprobe virtio_gpu || true; mkdir -p /tmp/runtime-root; chmod 700 /tmp/runtime-root; export XDG_RUNTIME_DIR=/tmp/runtime-root; ls -l /dev/dri; lspci -nnk | grep -A4 -Ei 'vga|display|virtio' || true" 120
cmd "cat > /tmp/linux-gbm-chromium-shape.c.b64 <<'B64_EOF'
${PROBE_B64}
B64_EOF
base64 -d /tmp/linux-gbm-chromium-shape.c.b64 > /tmp/linux-gbm-chromium-shape.c
cc -O2 -Wall -Wextra -o /tmp/linux-gbm-chromium-shape /tmp/linux-gbm-chromium-shape.c -lEGL -lgbm -lGLESv2" 300
cmd "export XDG_RUNTIME_DIR=/tmp/runtime-root; export GALLIUM_DRIVER=virgl; /tmp/linux-gbm-chromium-shape; printf 'LINUX_GBM_SHAPE_EXIT=%s\\n' \$?" 180
cmd "dmesg | grep -Ei 'virtio_gpu|virgl|drm|cap set|features' | tail -160" 120
send -- "poweroff -f\r"
expect {
  eof {}
  timeout { puts "LINUX_GBM_SHAPE_WARN poweroff-timeout" }
}
EOF_EXPECT
chmod +x "${EXPECT_SCRIPT}"

echo "linux-virgl-gbm-chromium-shape: capture dir ${CAPTURE_DIR}"
if "${EXPECT_SCRIPT}" >"${HOST_LOG}" 2>&1; then
    echo "expect_exit=0" >"${STATUS_LOG}"
else
    rc=$?
    echo "expect_exit=${rc}" >"${STATUS_LOG}"
fi

ctx_submit="$(grep -c 'virtio_gpu_cmd_ctx_submit' "${QEMU_TRACE}" 2>/dev/null || true)"
res_flush="$(grep -c 'virtio_gpu_cmd_res_flush' "${QEMU_TRACE}" 2>/dev/null || true)"
config_summary="$(grep -a 'linux-gbm-shape: phase=config_summary status=PASS' "${SERIAL_LOG}" | tail -1 || true)"
egl_initialize="$(grep -a 'linux-gbm-shape: phase=egl_initialize status=PASS' "${SERIAL_LOG}" | tail -1 || true)"
gl_identity="$(grep -a 'linux-gbm-shape: phase=gl_identity status=PASS' "${SERIAL_LOG}" | head -1 || true)"
linux_mesa_packages="$(grep -a '^LINUX_GBM_PACKAGE ' "${SERIAL_LOG}" | sort -u | tr '\n' ';' | sed 's/;$//' || true)"
shape_result="$(grep -a 'LINUX_GBM_SHAPE_RESULT status=COMPLETE' "${SERIAL_LOG}" | tail -1 || true)"
shape_exit="$(grep -a 'LINUX_GBM_SHAPE_EXIT=' "${SERIAL_LOG}" | tail -1 || true)"
xv6_mesa_version="$(
    sed -n 's/.*"version": "\([^"]*\)".*/\1/p' \
        build-x86_64/ports/mesa-build/meson-info/intro-projectinfo.json \
        2>/dev/null | head -1 || true
)"
xv6_mesa_head="$(git -C ports/mesa/src rev-parse --short=12 HEAD 2>/dev/null || true)"
xv6_mesa_head_gl="${xv6_mesa_head:0:10}"
config_count_comparable="no"
config_count_comparable_reason="mesa_identity_mismatch"
if [[ -n "${xv6_mesa_version}" && -n "${xv6_mesa_head}" &&
      "${gl_identity}" == *"Mesa ${xv6_mesa_version}"* &&
      "${gl_identity}" == *"${xv6_mesa_head_gl}"* ]]; then
    config_count_comparable="yes"
    config_count_comparable_reason="mesa_identity_match"
elif [[ -z "${xv6_mesa_version}" || -z "${xv6_mesa_head}" ||
        -z "${gl_identity}" ]]; then
    config_count_comparable_reason="missing_mesa_identity"
fi

{
    if [[ -n "${shape_result}" && "${shape_exit}" == *"LINUX_GBM_SHAPE_EXIT=0"* ]]; then
        echo "LINUX-VM-GBM-CHROMIUM-SHAPE: PASS"
    else
        echo "LINUX-VM-GBM-CHROMIUM-SHAPE: FAIL"
    fi
    echo "artifact=${CAPTURE_DIR}"
    cat "${STATUS_LOG}"
    echo "shape_exit=${shape_exit:-missing}"
    echo "shape_result=${shape_result:-missing}"
    echo "xv6_mesa_reference=version=${xv6_mesa_version:-missing} head=${xv6_mesa_head:-missing} gl_head=${xv6_mesa_head_gl:-missing}"
    echo "linux_mesa_packages=${linux_mesa_packages:-missing}"
    echo "linux_egl_initialize=${egl_initialize:-missing}"
    echo "linux_gl_identity=${gl_identity:-missing}"
    echo "config_count_comparable=${config_count_comparable} reason=${config_count_comparable_reason}"
    echo "config_summary=${config_summary:-missing}"
    grep -a 'linux-gbm-shape: phase=chromium_config ' "${SERIAL_LOG}" | sed 's/^/config: /'
    grep -a 'linux-gbm-shape: phase=context_attempt ' "${SERIAL_LOG}" | sed 's/^/context: /'
    echo "virtio_gpu_trace_ctx_submit=${ctx_submit}"
    echo "virtio_gpu_trace_res_flush=${res_flush}"
} >"${VERDICT_LOG}"

cat "${VERDICT_LOG}"
grep -q '^LINUX-VM-GBM-CHROMIUM-SHAPE: PASS$' "${VERDICT_LOG}"
