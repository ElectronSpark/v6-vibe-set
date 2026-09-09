#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static const char *fourcc_name(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_XRGB8888:
        return "XRGB8888";
    case DRM_FORMAT_ARGB8888:
        return "ARGB8888";
    case DRM_FORMAT_XBGR8888:
        return "XBGR8888";
    case DRM_FORMAT_ABGR8888:
        return "ABGR8888";
    case DRM_FORMAT_RGB565:
        return "RGB565";
    case DRM_FORMAT_C8:
        return "C8";
    default:
        return "UNKNOWN";
    }
}

static void print_connector_summary(int fd, drmModeResPtr res, const char *path)
{
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnectorPtr conn = drmModeGetConnector(fd, res->connectors[i]);
        if (!conn) {
            printf("kde_drm_probe %s connector id=%u get=NULL errno=%d %s\n",
                   path, res->connectors[i], errno, strerror(errno));
            continue;
        }

        printf("kde_drm_probe %s connector id=%u connection=%d modes=%d encoder=%u mm=%ux%u\n",
               path, conn->connector_id, conn->connection, conn->count_modes,
               conn->encoder_id, conn->mmWidth, conn->mmHeight);
        for (int m = 0; m < conn->count_modes && m < 4; m++) {
            const drmModeModeInfo *mode = &conn->modes[m];
            printf("kde_drm_probe %s connector id=%u mode%d=%ux%u@%u name=%s type=0x%x flags=0x%x\n",
                   path, conn->connector_id, m, mode->hdisplay,
                   mode->vdisplay, mode->vrefresh, mode->name,
                   mode->type, mode->flags);
        }
        drmModeFreeConnector(conn);
    }
}

static void print_crtc_summary(int fd, drmModeResPtr res, const char *path)
{
    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc) {
            printf("kde_drm_probe %s crtc id=%u get=NULL errno=%d %s\n",
                   path, res->crtcs[i], errno, strerror(errno));
            continue;
        }

        printf("kde_drm_probe %s crtc id=%u fb=%u geom=%ux%u+%u+%u mode_valid=%d gamma=%d\n",
               path, crtc->crtc_id, crtc->buffer_id, crtc->width,
               crtc->height, crtc->x, crtc->y, crtc->mode_valid,
               crtc->gamma_size);
        if (crtc->mode_valid) {
            printf("kde_drm_probe %s crtc id=%u mode=%ux%u@%u name=%s\n",
                   path, crtc->crtc_id, crtc->mode.hdisplay,
                   crtc->mode.vdisplay, crtc->mode.vrefresh,
                   crtc->mode.name);
        }
        drmModeFreeCrtc(crtc);
    }
}

static int probe_crtc_gamma(int fd, drmModeResPtr res, const char *path)
{
    int failed = 0;

    for (int i = 0; i < res->count_crtcs; i++) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        uint16_t *red;
        uint16_t *green;
        uint16_t *blue;
        int rc;

        if (!crtc)
            continue;
        if (crtc->gamma_size == 0) {
            printf("kde_drm_probe %s crtc id=%u gamma_guard=FAIL size=0\n",
                   path, res->crtcs[i]);
            failed = 1;
            drmModeFreeCrtc(crtc);
            continue;
        }
        red = calloc(crtc->gamma_size, sizeof(*red));
        green = calloc(crtc->gamma_size, sizeof(*green));
        blue = calloc(crtc->gamma_size, sizeof(*blue));
        if (!red || !green || !blue) {
            printf("kde_drm_probe %s crtc id=%u gamma_guard=FAIL alloc size=%d\n",
                   path, crtc->crtc_id, crtc->gamma_size);
            failed = 1;
            free(red);
            free(green);
            free(blue);
            drmModeFreeCrtc(crtc);
            continue;
        }

        errno = 0;
        rc = drmModeCrtcGetGamma(fd, crtc->crtc_id, crtc->gamma_size,
                                 red, green, blue);
        printf("kde_drm_probe %s crtc id=%u getgamma=%d size=%d first=%04x/%04x/%04x last=%04x/%04x/%04x errno=%d %s\n",
               path, crtc->crtc_id, rc, crtc->gamma_size,
               red[0], green[0], blue[0],
               red[crtc->gamma_size - 1],
               green[crtc->gamma_size - 1],
               blue[crtc->gamma_size - 1],
               errno, strerror(errno));
        if (rc != 0) {
            failed = 1;
        } else {
            errno = 0;
            rc = drmModeCrtcSetGamma(fd, crtc->crtc_id,
                                     crtc->gamma_size, red, green, blue);
            printf("kde_drm_probe %s crtc id=%u setgamma=%d errno=%d %s\n",
                   path, crtc->crtc_id, rc, errno, strerror(errno));
            if (rc != 0)
                failed = 1;
        }

        free(red);
        free(green);
        free(blue);
        drmModeFreeCrtc(crtc);
    }
    printf("kde_drm_probe %s gamma_guard=%s\n", path,
           failed ? "FAIL" : "PASS");
    return failed;
}

static int format_is_8888(uint32_t format)
{
    return format == DRM_FORMAT_XRGB8888 || format == DRM_FORMAT_ARGB8888 ||
           format == DRM_FORMAT_XBGR8888 || format == DRM_FORMAT_ABGR8888;
}

static int inspect_plane_fb(int fd, const char *path, drmModePlanePtr plane)
{
    int failed = 0;

    if (plane->fb_id == 0)
        return 0;

    errno = 0;
    drmModeFB2Ptr fb2 = drmModeGetFB2(fd, plane->fb_id);
    if (fb2) {
        printf("kde_drm_probe %s plane id=%u fb2 id=%u size=%ux%u format=%s/0x%08x pitch0=%u modifier=0x%lx flags=0x%x\n",
               path, plane->plane_id, fb2->fb_id, fb2->width, fb2->height,
               fourcc_name(fb2->pixel_format), fb2->pixel_format,
               fb2->pitches[0], (unsigned long)fb2->modifier, fb2->flags);
        if (!format_is_8888(fb2->pixel_format)) {
            printf("kde_drm_probe %s plane id=%u fb2 format_guard=FAIL expected=8888 actual=%s/0x%08x\n",
                   path, plane->plane_id, fourcc_name(fb2->pixel_format),
                   fb2->pixel_format);
            failed = 1;
        }
        if (fb2->pitches[0] < fb2->width * 4) {
            printf("kde_drm_probe %s plane id=%u fb2 pitch_guard=FAIL width=%u pitch=%u\n",
                   path, plane->plane_id, fb2->width, fb2->pitches[0]);
            failed = 1;
        }
        drmModeFreeFB2(fb2);
        return failed;
    }

    printf("kde_drm_probe %s plane id=%u fb2=NULL errno=%d %s\n",
           path, plane->plane_id, errno, strerror(errno));
    errno = 0;
    drmModeFBPtr fb = drmModeGetFB(fd, plane->fb_id);
    if (!fb) {
        printf("kde_drm_probe %s plane id=%u fb=NULL errno=%d %s fb_info=UNAVAILABLE\n",
               path, plane->plane_id, errno, strerror(errno));
        return 0;
    }

    printf("kde_drm_probe %s plane id=%u fb id=%u size=%ux%u pitch=%u bpp=%u depth=%u\n",
           path, plane->plane_id, fb->fb_id, fb->width, fb->height,
           fb->pitch, fb->bpp, fb->depth);
    if (fb->bpp < 32 || fb->depth < 24 || fb->pitch < fb->width * 4) {
        printf("kde_drm_probe %s plane id=%u fb legacy_guard=FAIL bpp=%u depth=%u pitch=%u width=%u\n",
               path, plane->plane_id, fb->bpp, fb->depth, fb->pitch,
               fb->width);
        failed = 1;
    }
    drmModeFreeFB(fb);
    return failed;
}

static void probe_node(const char *path)
{
    struct stat st;
    int fd;
    int failed = 0;

    if (stat(path, &st) != 0) {
        printf("kde_drm_probe %s stat=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        return;
    }

    printf("kde_drm_probe %s mode=%o chr=%d rdev=%u:%u\n",
           path, (unsigned)st.st_mode, S_ISCHR(st.st_mode),
           major(st.st_rdev), minor(st.st_rdev));

    fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        printf("kde_drm_probe %s open=-1 errno=%d %s\n",
               path, errno, strerror(errno));
        return;
    }
    printf("kde_drm_probe %s open=%d\n", path, fd);

    errno = 0;
    drmVersionPtr version = drmGetVersion(fd);
    if (!version) {
        printf("kde_drm_probe %s drmGetVersion=NULL errno=%d %s\n",
               path, errno, strerror(errno));
    } else {
        printf("kde_drm_probe %s drmGetVersion=%s %d.%d.%d\n",
               path, version->name ? version->name : "",
               version->version_major, version->version_minor,
               version->version_patchlevel);
        drmFreeVersion(version);
    }

    errno = 0;
    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        printf("kde_drm_probe %s drmModeGetResources=NULL errno=%d %s\n",
               path, errno, strerror(errno));
    } else {
        printf("kde_drm_probe %s drmModeGetResources crtcs=%d connectors=%d encoders=%d fbs=%d\n",
               path, res->count_crtcs, res->count_connectors,
               res->count_encoders, res->count_fbs);
        print_connector_summary(fd, res, path);
        print_crtc_summary(fd, res, path);
        if (strcmp(path, "/dev/dri/card0") == 0)
            failed |= probe_crtc_gamma(fd, res, path);
        drmModeFreeResources(res);
    }

    errno = 0;
    int rc = drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    printf("kde_drm_probe %s setcap_universal=%d errno=%d %s\n",
           path, rc, errno, strerror(errno));

    errno = 0;
    rc = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    printf("kde_drm_probe %s setcap_atomic=%d errno=%d %s\n",
           path, rc, errno, strerror(errno));

    errno = 0;
    rc = drmSetClientCap(fd, DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT, 1);
    printf("kde_drm_probe %s setcap_cursor_hotspot=%d errno=%d %s\n",
           path, rc, errno, strerror(errno));

    errno = 0;
    drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
    if (!planes) {
        printf("kde_drm_probe %s drmModeGetPlaneResources=NULL errno=%d %s\n",
               path, errno, strerror(errno));
    } else {
        printf("kde_drm_probe %s drmModeGetPlaneResources planes=%u\n",
               path, planes->count_planes);
        for (uint32_t i = 0; i < planes->count_planes; i++) {
            drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[i]);
            if (!plane) {
                printf("kde_drm_probe %s plane id=%u get=NULL errno=%d %s\n",
                       path, planes->planes[i], errno, strerror(errno));
                failed = 1;
                continue;
            }
            printf("kde_drm_probe %s plane id=%u crtc=%u fb=%u possible_crtcs=0x%x formats=%u\n",
                   path, plane->plane_id, plane->crtc_id, plane->fb_id,
                   plane->possible_crtcs, plane->count_formats);
            int has_8888 = 0;
            for (uint32_t f = 0; f < plane->count_formats; f++) {
                uint32_t format = plane->formats[f];
                has_8888 |= format_is_8888(format);
                printf("kde_drm_probe %s plane id=%u format%u=%s/0x%08x\n",
                       path, plane->plane_id, f, fourcc_name(format), format);
            }
            if (!has_8888) {
                printf("kde_drm_probe %s plane id=%u format_guard=FAIL no_8888_format\n",
                       path, plane->plane_id);
                failed = 1;
            }
            failed |= inspect_plane_fb(fd, path, plane);
            drmModeFreePlane(plane);
        }
        drmModeFreePlaneResources(planes);
    }
    printf("kde_drm_probe %s color_guard=%s\n", path,
           failed ? "FAIL" : "PASS");
    close(fd);
}

int main(void)
{
    drmDevicePtr devices[8];
    int ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("kde_drm_probe start\n");
    probe_node("/dev/dri/renderD128");
    probe_node("/dev/dri/card0");

    printf("kde_drm_probe before_drmGetDevices2\n");
    errno = 0;
    ret = drmGetDevices2(0, devices, 8);
    printf("kde_drm_probe drmGetDevices2=%d errno=%d %s\n",
           ret, errno, strerror(errno));
    if (ret > 0) {
        for (int i = 0; i < ret; i++) {
            printf("kde_drm_probe device%d available=0x%x primary=%s render=%s\n",
                   i, devices[i]->available_nodes,
                   devices[i]->nodes[DRM_NODE_PRIMARY] ?
                       devices[i]->nodes[DRM_NODE_PRIMARY] : "",
                   devices[i]->nodes[DRM_NODE_RENDER] ?
                       devices[i]->nodes[DRM_NODE_RENDER] : "");
        }
        drmFreeDevices(devices, ret);
    }

    printf("kde_drm_probe done\n");
    return 0;
}
