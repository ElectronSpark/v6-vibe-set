/*
 * kwin-ioctl-trace-preload.c
 *
 * Repo-owned LD_PRELOAD ioctl-timing shim for kwin_wayland. Replaces the
 * throwaway per-worker versions with a single reproducible source that this
 * week's three separate hand-rolled shims were converging on.
 *
 * It interposes libc ioctl()/open()/openat()/read()/close() (all resolved via
 * dlsym(RTLD_NEXT, ...)) and CLOCK_MONOTONIC-stamps the DRM KMS ioctls that
 * pace the compositor's present loop:
 *   - DRM_IOCTL_MODE_PAGE_FLIP   (legacy flip; logs flags + fb_id)
 *   - DRM_IOCTL_MODE_ATOMIC      (atomic commit; logs flags)
 *   - DRM_IOCTL_MODE_CURSOR      (legacy cursor)
 *   - DRM_IOCTL_MODE_CURSOR2     (cursor + hotspot)
 * plus read() on DRM fds, to timestamp flip-complete / vblank event reads
 * (EVTREAD lines carry the delta since the last flip submit on that fd) --
 * this pairing of submit-side and completion-side timestamps is what made the
 * vblank-pacing A/B decisive.
 *
 * DRM-fd discovery closes the inherited-fd blind spot (U2 root cause):
 * kwin_wayland INHERITS its /dev/dri fd across fork/exec from kde-session, so
 * the open-family wrappers never see the open and read() tracking stayed blind
 * (ZERO EVTREAD lines despite flags=0x1 on every flip). Two seeders cover it:
 * (1) classify-on-first-ioctl -- the ioctl wrapper marks any fd receiving a
 * DRM-type ('d'=0x64) ioctl as a DRM fd (zero scanning, zero startup cost,
 * works without procfs; this is the robust primary mechanism); and (2) a
 * constructor /proc/self/fd scan that seeds fds already open at preload time.
 * A phase=seed banner and per-fd DRMFD_SEEDED lines record which fired so the
 * A/B can confirm the blind spot is closed from the log alone.
 *
 * Modeled on the sibling repo-owned kwin shim
 * scripts/image/kwin-alloc-trace-preload.c (env-gated file log, constructor
 * banner, best-effort destructor summary, __thread recursion guard,
 * fail-open-to-passthrough on every error) and the interposition conventions
 * in scripts/image/konsole-wayland-event-trace-preload.c (variadic open/ioctl
 * forwarding, dlsym RTLD_NEXT, low-overhead per-line writes).
 *
 * DRM ioctl numbers and the few struct field offsets are defined locally
 * rather than pulling the guest header, because this shim is compiled with the
 * HOST toolchain (it is an LD_PRELOAD into the host-ABI kwin_wayland, not guest
 * code). The values are the fixed, architecture-independent DRM UABI numbers
 * and mirror kernel/kernel/inc/uabi/drm.h exactly:
 *     DRM_IOCTL_MODE_CURSOR     0xc01c64a3
 *     DRM_IOCTL_MODE_PAGE_FLIP  0xc01864b0
 *     DRM_IOCTL_MODE_CURSOR2    0xc02464bb
 *     DRM_IOCTL_MODE_ATOMIC     0xc03864bc
 *     DRM_EVENT_VBLANK 0x01, DRM_EVENT_FLIP_COMPLETE 0x02
 * (see kernel/kernel/inc/uabi/drm.h:60,70,81,82,236,237 and the *_compat
 * struct layouts at :808 and :836).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* --- DRM UABI constants (mirror kernel/kernel/inc/uabi/drm.h) ------------- */
#define KWIN_DRM_IOCTL_MODE_CURSOR    0xc01c64a3UL
#define KWIN_DRM_IOCTL_MODE_PAGE_FLIP 0xc01864b0UL
#define KWIN_DRM_IOCTL_MODE_CURSOR2   0xc02464bbUL
#define KWIN_DRM_IOCTL_MODE_ATOMIC    0xc03864bcUL

#define KWIN_DRM_EVENT_VBLANK         0x01U
#define KWIN_DRM_EVENT_FLIP_COMPLETE  0x02U

/*
 * _IOC type field. Every DRM ioctl (the four KMS ops above plus GEM, the
 * GET-family, WAIT_VBLANK, etc.) carries the ASCII 'd' (0x64) type byte in bits 8..15 of
 * the request number -- this is the fixed, arch-independent DRM UABI ioctl
 * type (Linux _IOC(dir,type,nr,size), _IOC_TYPESHIFT=8). Used by the
 * classify-on-first-ioctl seeding path below.
 */
#define KWIN_IOC_TYPESHIFT            8
#define KWIN_IOC_TYPEMASK             0xffUL
#define KWIN_DRM_IOC_TYPE             0x64UL  /* 'd' */

/* struct drm_mode_crtc_page_flip (uabi/drm.h:808): crtc_id, fb_id, flags,
 * reserved, user_data. Only fb_id (offset 4) and flags (offset 8) are read. */
struct kwin_drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

/* struct drm_mode_atomic (uabi/drm.h:845): flags is the first field. */
struct kwin_drm_mode_atomic {
    uint32_t flags;
    uint32_t count_objs;
    uint64_t objs_ptr;
    uint64_t count_props_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint64_t reserved;
    uint64_t user_data;
};

/* struct drm_event / drm_event_vblank (uabi/drm.h:831,836). */
struct kwin_drm_event {
    uint32_t type;
    uint32_t length;
};

struct kwin_drm_event_vblank {
    struct kwin_drm_event base;
    uint64_t user_data;
    uint32_t tv_sec;
    uint32_t tv_usec;
    uint32_t sequence;
    uint32_t crtc_id;
};

/* --- shim state ---------------------------------------------------------- */
#define KWIN_TRACE_MAX_FD 4096

enum kwin_drm_op {
    KWIN_OP_PAGE_FLIP = 0,
    KWIN_OP_ATOMIC,
    KWIN_OP_CURSOR,
    KWIN_OP_CURSOR2,
    KWIN_OP_COUNT
};

typedef int (*open_fn_t)(const char *, int, ...);
typedef int (*openat_fn_t)(int, const char *, int, ...);
typedef int (*ioctl_fn_t)(int, unsigned long, ...);
typedef ssize_t (*read_fn_t)(int, void *, size_t);
typedef int (*close_fn_t)(int);

static open_fn_t real_open;
static open_fn_t real_open64;
static openat_fn_t real_openat;
static openat_fn_t real_openat64;
static ioctl_fn_t real_ioctl;
static read_fn_t real_read;
static close_fn_t real_close;

static int log_fd = -1;
static unsigned long long min_us;
static int resolved;
static int resolving;
static __thread int in_hook;

/* 1 => fd refers to a /dev/dri node. Plain byte array: a store is atomic on
 * x86_64 and a stale bit only costs a spurious/absent EVTREAD line, never a
 * crash. */
static unsigned char drm_fd[KWIN_TRACE_MAX_FD];
/* last flip/atomic submit timestamp per fd, for EVTREAD since-submit deltas. */
static uint64_t last_submit_us[KWIN_TRACE_MAX_FD];
/* last timestamp per op, for gap-since-last-same-op. */
static uint64_t op_last_us[KWIN_OP_COUNT];

static unsigned long long op_calls[KWIN_OP_COUNT];
static unsigned long long ioctl_calls;
static unsigned long long evtread_lines;
static unsigned long long flip_complete_events;
/* DRM fds discovered by the two blind-spot-closing seeders (see below):
 * proc_seeded_fds = pre-existing fds found by the constructor /proc/self/fd
 * scan; ioctl_seeded_fds = inherited fds first classified when they received
 * a DRM-type ioctl. Either being >0 in the log proves the inherited-fd blind
 * spot is closed for that run. */
static unsigned long long proc_seeded_fds;
static unsigned long long ioctl_seeded_fds;

static uint64_t now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static unsigned long long parse_env_ull(const char *name,
                                        unsigned long long fallback)
{
    const char *value = getenv(name);
    char *end = NULL;
    unsigned long long parsed;

    if (!value || !value[0])
        return fallback;
    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return fallback;
    return parsed;
}

/*
 * Build the whole line in a thread-local stack buffer and emit it with a single
 * write() to the O_APPEND log fd. Rationale (see self-review in the plan): a
 * lone write() of a sub-PIPE_BUF buffer to an O_APPEND file does not interleave
 * across kwin's threads, and staying unbuffered means no trace is lost when the
 * harness SIGKILLs kwin -- which it does. Volume is low (~1 flip per vblank).
 */
static void trace_line(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;
    int saved_errno = errno;

    if (log_fd < 0)
        return;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        errno = saved_errno;
        return;
    }
    if ((size_t)n >= sizeof(buf))
        n = (int)sizeof(buf) - 1;
    buf[n++] = '\n';
    {
        ssize_t w = write(log_fd, buf, (size_t)n);

        (void)w;
    }
    errno = saved_errno;
}

static void resolve_symbols(void)
{
    if (resolved || resolving)
        return;
    resolving = 1;
    in_hook++;
    if (!real_open)
        real_open = (open_fn_t)dlsym(RTLD_NEXT, "open");
    if (!real_open64)
        real_open64 = (open_fn_t)dlsym(RTLD_NEXT, "open64");
    if (!real_openat)
        real_openat = (openat_fn_t)dlsym(RTLD_NEXT, "openat");
    if (!real_openat64)
        real_openat64 = (openat_fn_t)dlsym(RTLD_NEXT, "openat64");
    if (!real_ioctl)
        real_ioctl = (ioctl_fn_t)dlsym(RTLD_NEXT, "ioctl");
    if (!real_read)
        real_read = (read_fn_t)dlsym(RTLD_NEXT, "read");
    if (!real_close)
        real_close = (close_fn_t)dlsym(RTLD_NEXT, "close");
    in_hook--;
    resolved = 1;
    resolving = 0;
}

static int path_is_dri(const char *path)
{
    return path && strncmp(path, "/dev/dri/", 9) == 0;
}

static void mark_dri_fd(int fd, const char *path)
{
    if (fd < 0 || fd >= KWIN_TRACE_MAX_FD)
        return;
    if (path_is_dri(path)) {
        drm_fd[fd] = 1;
        last_submit_us[fd] = 0;
    }
}

static int open_needs_mode(int flags)
{
    if (flags & O_CREAT)
        return 1;
#ifdef O_TMPFILE
    if ((flags & O_TMPFILE) == O_TMPFILE)
        return 1;
#endif
    return 0;
}

/*
 * Mark an already-open fd as a DRM fd (the open-family wrappers never saw it,
 * so drm_fd[]/last_submit_us[] were never seeded). Idempotent: only resets the
 * submit timestamp the first time an fd is classified.
 */
static int mark_drm_fd_seeded(int fd)
{
    if (fd < 0 || fd >= KWIN_TRACE_MAX_FD || drm_fd[fd])
        return 0;
    drm_fd[fd] = 1;
    last_submit_us[fd] = 0;
    return 1;
}

/*
 * Whether a request number is a DRM ioctl by its _IOC type byte ('d'=0x64).
 * Broader than drm_op_for(): it matches EVERY DRM ioctl kwin issues on its fd
 * (GETRESOURCES, WAIT_VBLANK, GEM_*, the four KMS ops, ...), so a DRM fd is
 * classified on the very first DRM ioctl -- typically well before its first
 * flip-complete read.
 */
static int ioctl_is_drm_type(unsigned long request)
{
    return ((request >> KWIN_IOC_TYPESHIFT) & KWIN_IOC_TYPEMASK)
               == KWIN_DRM_IOC_TYPE;
}

/*
 * Constructor seeder: scan /proc/self/fd and classify any fd whose symlink
 * target is under /dev/dri/. This closes the inherited-fd blind spot for fds
 * present at preload time. This guest DOES expose /proc/<tgid>/fd/<n> symlinks
 * whose readlink resolves char devices to "/dev/<devtmpfs-name>", and the DRM
 * nodes are registered as "dri/card0"/"dri/renderD128" -- so readlink yields
 * exactly "/dev/dri/card0" etc. and path_is_dri() matches (verified against
 * kernel/kernel/vfs/procfs/inode.c:875-928 PROC_FD_ENTRY readlink and
 * kernel/kernel/dev/fb/fb_init_panic.c:31,51 devnames).
 *
 * A bounded readlink loop over fds 0..255 is used instead of opendir/readdir:
 * it needs no DIR buffer, pulls in no extra libc machinery, is a one-time
 * startup cost, and 256 comfortably covers a compositor's low fd range
 * (observed inherited DRM fd = 21). If /proc is unavailable every readlink
 * simply fails and this returns 0 -- classify-on-first-ioctl then covers it.
 */
#define KWIN_SEED_SCAN_MAX_FD 256

static int seed_drm_fds_from_proc(void)
{
    int fd;
    int seeded = 0;
    char linkpath[64];
    char target[128];

    for (fd = 0; fd < KWIN_SEED_SCAN_MAX_FD && fd < KWIN_TRACE_MAX_FD; fd++) {
        ssize_t n;

        (void)snprintf(linkpath, sizeof(linkpath), "/proc/self/fd/%d", fd);
        n = readlink(linkpath, target, sizeof(target) - 1);
        if (n <= 0)
            continue;
        target[n] = '\0';
        if (path_is_dri(target) && mark_drm_fd_seeded(fd)) {
            seeded++;
            trace_line("kwin-ioctl-trace: op=DRMFD_SEEDED pid=%d fd=%d "
                       "via=proc target=%s",
                       (int)getpid(), fd, target);
        }
    }
    return seeded;
}

__attribute__((constructor)) static void kwin_ioctl_trace_init(void)
{
    const char *path = getenv("KWIN_IOCTL_TRACE_LOG");

    if (!path || !path[0])
        path = "/kde-kwin-ioctl-trace.log";
    resolve_symbols();
    min_us = parse_env_ull("KWIN_IOCTL_TRACE_MIN_US", 0);
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    trace_line("kwin-ioctl-trace: phase=begin pid=%d log=%s min_us=%llu",
               (int)getpid(), path, min_us);

    /*
     * Close the EVTREAD blind spot (U2 root cause): kwin_wayland INHERITS its
     * DRM fd across fork/exec from kde-session, so this child's open-family
     * wrappers never saw the /dev/dri open and read() tracking stayed blind
     * (ZERO EVTREAD lines despite flags=0x1 on every flip). Seed pre-existing
     * DRM fds from /proc now, and classify-on-first-ioctl (in the ioctl
     * wrapper) catches any inherited fd from its first DRM ioctl regardless of
     * procfs support. The banner records both so the next A/B can confirm the
     * blind spot is closed from the log alone.
     */
    in_hook++;
    proc_seeded_fds = (unsigned long long)seed_drm_fds_from_proc();
    in_hook--;
    trace_line("kwin-ioctl-trace: phase=seed pid=%d proc_seeded_fds=%llu "
               "classify_on_first_ioctl=active scan_max_fd=%d",
               (int)getpid(), proc_seeded_fds, KWIN_SEED_SCAN_MAX_FD);
}

__attribute__((destructor)) static void kwin_ioctl_trace_fini(void)
{
    /* Best-effort only: kwin is frequently SIGKILLed, so nothing may consume
     * this. The per-call lines are the authoritative output. */
    trace_line("kwin-ioctl-trace: phase=summary pid=%d ioctl_calls=%llu "
               "page_flip=%llu atomic=%llu cursor=%llu cursor2=%llu "
               "evtread=%llu flip_complete=%llu proc_seeded_fds=%llu "
               "ioctl_seeded_fds=%llu",
               (int)getpid(),
               ioctl_calls,
               op_calls[KWIN_OP_PAGE_FLIP],
               op_calls[KWIN_OP_ATOMIC],
               op_calls[KWIN_OP_CURSOR],
               op_calls[KWIN_OP_CURSOR2],
               evtread_lines,
               flip_complete_events,
               proc_seeded_fds,
               ioctl_seeded_fds);
    if (log_fd >= 0)
        close(log_fd);
    log_fd = -1;
}

/* --- open family: track /dev/dri fds ------------------------------------- */
int open(const char *path, int flags, ...)
{
    int needs_mode = open_needs_mode(flags);
    mode_t mode = 0;
    int fd;
    int saved_errno;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (!resolved)
        resolve_symbols();
    if (real_open)
        fd = needs_mode ? real_open(path, flags, mode)
                        : real_open(path, flags);
    else
        fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    saved_errno = errno;
    if (fd >= 0 && !in_hook)
        mark_dri_fd(fd, path);
    errno = saved_errno;
    return fd;
}

int open64(const char *path, int flags, ...)
{
    int needs_mode = open_needs_mode(flags);
    mode_t mode = 0;
    int fd;
    int saved_errno;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (!resolved)
        resolve_symbols();
    if (real_open64)
        fd = needs_mode ? real_open64(path, flags, mode)
                        : real_open64(path, flags);
    else
        fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    saved_errno = errno;
    if (fd >= 0 && !in_hook)
        mark_dri_fd(fd, path);
    errno = saved_errno;
    return fd;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    int needs_mode = open_needs_mode(flags);
    mode_t mode = 0;
    int fd;
    int saved_errno;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (!resolved)
        resolve_symbols();
    if (real_openat)
        fd = needs_mode ? real_openat(dirfd, path, flags, mode)
                        : real_openat(dirfd, path, flags);
    else
        fd = (int)syscall(SYS_openat, dirfd, path, flags, mode);
    saved_errno = errno;
    if (fd >= 0 && !in_hook)
        mark_dri_fd(fd, path);
    errno = saved_errno;
    return fd;
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    int needs_mode = open_needs_mode(flags);
    mode_t mode = 0;
    int fd;
    int saved_errno;

    if (needs_mode) {
        va_list ap;

        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if (!resolved)
        resolve_symbols();
    if (real_openat64)
        fd = needs_mode ? real_openat64(dirfd, path, flags, mode)
                        : real_openat64(dirfd, path, flags);
    else
        fd = (int)syscall(SYS_openat, dirfd, path, flags, mode);
    saved_errno = errno;
    if (fd >= 0 && !in_hook)
        mark_dri_fd(fd, path);
    errno = saved_errno;
    return fd;
}

int close(int fd)
{
    if (!resolved)
        resolve_symbols();
    if (fd >= 0 && fd < KWIN_TRACE_MAX_FD) {
        drm_fd[fd] = 0;
        last_submit_us[fd] = 0;
    }
    if (real_close)
        return real_close(fd);
    return (int)syscall(SYS_close, fd);
}

/* --- ioctl: the primary instrument --------------------------------------- */
static enum kwin_drm_op drm_op_for(unsigned long request, int *is_op)
{
    *is_op = 1;
    switch (request) {
    case KWIN_DRM_IOCTL_MODE_PAGE_FLIP:
        return KWIN_OP_PAGE_FLIP;
    case KWIN_DRM_IOCTL_MODE_ATOMIC:
        return KWIN_OP_ATOMIC;
    case KWIN_DRM_IOCTL_MODE_CURSOR:
        return KWIN_OP_CURSOR;
    case KWIN_DRM_IOCTL_MODE_CURSOR2:
        return KWIN_OP_CURSOR2;
    default:
        break;
    }
    *is_op = 0;
    return KWIN_OP_COUNT;
}

static const char *drm_op_name(enum kwin_drm_op op)
{
    switch (op) {
    case KWIN_OP_PAGE_FLIP:
        return "PAGE_FLIP";
    case KWIN_OP_ATOMIC:
        return "ATOMIC";
    case KWIN_OP_CURSOR:
        return "CURSOR";
    case KWIN_OP_CURSOR2:
        return "CURSOR2";
    default:
        return "UNKNOWN";
    }
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    int is_op;
    enum kwin_drm_op op;
    uint64_t begin;
    uint64_t end;
    uint64_t prev;
    uint64_t gap;
    int ret;
    int saved_errno;

    /* Recover the single variadic argument and forward it verbatim. Every
     * libc ioctl caller passes exactly one third argument (a pointer or an
     * integer widened to a pointer slot); this is the canonical interposition
     * shape. */
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (!resolved)
        resolve_symbols();
    if (!real_ioctl) {
        /* Fail open: forward via raw syscall so the host never loses an
         * ioctl just because dlsym has not resolved yet. */
        long r = syscall(SYS_ioctl, fd, request, arg);
        return (int)r;
    }

    /*
     * Classify-on-first-ioctl: any DRM-type ('d') ioctl on an untracked fd
     * marks it a DRM fd permanently, so read() on it thereafter emits EVTREAD
     * lines. This is what closes the inherited-fd blind spot in the hot path:
     * kwin issues DRM ioctls (flips and others) on its inherited fd long
     * before we would otherwise see it, and this needs no scanning, no
     * startup cost, and works even without procfs. The guard skips our own
     * internal calls (in_hook). One DRMFD_SEEDED line is logged per fd so the
     * A/B can see the blind spot close from the log.
     */
    if (!in_hook && ioctl_is_drm_type(request) &&
        fd >= 0 && fd < KWIN_TRACE_MAX_FD && !drm_fd[fd] &&
        mark_drm_fd_seeded(fd)) {
        __sync_add_and_fetch(&ioctl_seeded_fds, 1);
        trace_line("kwin-ioctl-trace: op=DRMFD_SEEDED pid=%d fd=%d "
                   "via=ioctl request=0x%lx",
                   (int)getpid(), fd, request);
    }

    drm_op_for(request, &is_op);
    if (!is_op || in_hook || log_fd < 0)
        return real_ioctl(fd, request, arg);

    op = drm_op_for(request, &is_op);
    in_hook++;
    begin = now_us();
    ret = real_ioctl(fd, request, arg);
    saved_errno = errno;
    end = now_us();

    __sync_add_and_fetch(&ioctl_calls, 1);
    __sync_add_and_fetch(&op_calls[op], 1);
    prev = __atomic_exchange_n(&op_last_us[op], end, __ATOMIC_RELAXED);
    gap = (prev && end >= prev) ? end - prev : 0;

    if ((op == KWIN_OP_PAGE_FLIP || op == KWIN_OP_ATOMIC) &&
        fd >= 0 && fd < KWIN_TRACE_MAX_FD)
        __atomic_store_n(&last_submit_us[fd], end, __ATOMIC_RELAXED);

    if ((uint64_t)(end - begin) >= min_us) {
        if (op == KWIN_OP_PAGE_FLIP && arg) {
            const struct kwin_drm_mode_crtc_page_flip *flip = arg;

            trace_line("kwin-ioctl-trace: op=PAGE_FLIP pid=%d fd=%d "
                       "duration_us=%llu gap_us=%llu ret=%d errno=%d "
                       "flags=0x%x fb_id=%u",
                       (int)getpid(), fd,
                       (unsigned long long)(end - begin),
                       (unsigned long long)gap, ret,
                       ret < 0 ? saved_errno : 0,
                       flip->flags, flip->fb_id);
        } else if (op == KWIN_OP_ATOMIC && arg) {
            const struct kwin_drm_mode_atomic *atomic = arg;

            trace_line("kwin-ioctl-trace: op=ATOMIC pid=%d fd=%d "
                       "duration_us=%llu gap_us=%llu ret=%d errno=%d "
                       "flags=0x%x",
                       (int)getpid(), fd,
                       (unsigned long long)(end - begin),
                       (unsigned long long)gap, ret,
                       ret < 0 ? saved_errno : 0,
                       atomic->flags);
        } else {
            trace_line("kwin-ioctl-trace: op=%s pid=%d fd=%d "
                       "duration_us=%llu gap_us=%llu ret=%d errno=%d",
                       drm_op_name(op), (int)getpid(), fd,
                       (unsigned long long)(end - begin),
                       (unsigned long long)gap, ret,
                       ret < 0 ? saved_errno : 0);
        }
    }

    in_hook--;
    errno = saved_errno;
    return ret;
}

/* --- read: timestamp flip-complete / vblank events on DRM fds ------------ */
static void trace_drm_events(int fd, const void *buf, ssize_t n,
                             uint64_t begin, uint64_t end)
{
    size_t off = 0;
    uint64_t submit;

    if (n < (ssize_t)sizeof(struct kwin_drm_event))
        return;
    submit = (fd >= 0 && fd < KWIN_TRACE_MAX_FD)
                 ? __atomic_load_n(&last_submit_us[fd], __ATOMIC_RELAXED)
                 : 0;

    while (off + sizeof(struct kwin_drm_event) <= (size_t)n) {
        const struct kwin_drm_event *ev =
            (const struct kwin_drm_event *)((const char *)buf + off);
        uint32_t length = ev->length;
        uint32_t type = ev->type;
        unsigned since = (submit && end >= submit)
                             ? (unsigned)(end - submit) : 0;

        if (length < sizeof(struct kwin_drm_event) ||
            off + length > (size_t)n)
            break;

        if (type == KWIN_DRM_EVENT_FLIP_COMPLETE ||
            type == KWIN_DRM_EVENT_VBLANK) {
            unsigned seq = 0;
            unsigned crtc = 0;

            if (length >= sizeof(struct kwin_drm_event_vblank)) {
                const struct kwin_drm_event_vblank *vb =
                    (const struct kwin_drm_event_vblank *)ev;

                seq = vb->sequence;
                crtc = vb->crtc_id;
            }
            if (type == KWIN_DRM_EVENT_FLIP_COMPLETE)
                __sync_add_and_fetch(&flip_complete_events, 1);
            __sync_add_and_fetch(&evtread_lines, 1);
            trace_line("kwin-ioctl-trace: op=EVTREAD pid=%d fd=%d "
                       "duration_us=%llu evt_type=%u evt=%s "
                       "since_submit_us=%u seq=%u crtc=%u",
                       (int)getpid(), fd,
                       (unsigned long long)(end - begin), type,
                       type == KWIN_DRM_EVENT_FLIP_COMPLETE ? "FLIP_COMPLETE"
                                                            : "VBLANK",
                       since, seq, crtc);
        }
        off += length;
    }
}

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t ret;
    int saved_errno;
    uint64_t begin;
    uint64_t end;

    if (!resolved)
        resolve_symbols();
    if (!real_read)
        return (ssize_t)syscall(SYS_read, fd, buf, count);

    /* Strict passthrough for the overwhelmingly common non-DRM read: a single
     * array-index load, no timestamps, no logging. */
    if (fd < 0 || fd >= KWIN_TRACE_MAX_FD || !drm_fd[fd] || in_hook ||
        log_fd < 0)
        return real_read(fd, buf, count);

    in_hook++;
    begin = now_us();
    ret = real_read(fd, buf, count);
    saved_errno = errno;
    end = now_us();
    if (ret > 0)
        trace_drm_events(fd, buf, ret, begin, end);
    in_hook--;
    errno = saved_errno;
    return ret;
}
