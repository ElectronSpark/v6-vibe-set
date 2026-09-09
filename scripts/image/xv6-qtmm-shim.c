#if !defined(__x86_64__)
#error "xv6 Qt5Multimedia shim is x86_64-only"
#endif

#define SYS_write 1
#define SYS_close 3
#define SYS_getpid 39
#define SYS_openat 257
#define AT_FDCWD -100
#define O_WRONLY 1
#define O_CREAT 64
#define O_APPEND 1024

typedef __SIZE_TYPE__ size_t;

static long xsyscall0(long n)
{
    long ret;

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n)
                     : "rcx", "r11", "memory");
    return ret;
}

static long xsyscall1(long n, long a1)
{
    long ret;

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1)
                     : "rcx", "r11", "memory");
    return ret;
}

static long xsyscall3(long n, long a1, long a2, long a3)
{
    long ret;

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

static long xsyscall4(long n, long a1, long a2, long a3, long a4)
{
    long ret;
    register long r10 __asm__("r10") = a4;

    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static char *append_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *append_long(char *p, long value)
{
    char tmp[32];
    int n = 0;
    unsigned long v;

    if (value < 0) {
        *p++ = '-';
        v = (unsigned long)(-value);
    } else {
        v = (unsigned long)value;
    }
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && n < (int)sizeof(tmp));
    while (n > 0)
        *p++ = tmp[--n];
    return p;
}

static void log_call(const char *name)
{
    static const char path[] = "/tmp/xv6-qtmm-shim-calls.log";
    char line[256];
    char *p = line;
    long fd;
    long pid;

    fd = xsyscall4(SYS_openat, AT_FDCWD, (long)path,
                   O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    pid = xsyscall0(SYS_getpid);
    p = append_str(p, "qtmm_shim_call pid=");
    p = append_long(p, pid);
    p = append_str(p, " symbol=");
    p = append_str(p, name);
    p = append_str(p, " risk=unsafe_load_only\n");
    xsyscall3(SYS_write, fd, (long)line, (long)(p - line));
    xsyscall1(SYS_close, fd);
}

__attribute__((visibility("default")))
void qtmm_QMediaPlayer_play(void *self) __asm__("_ZN12QMediaPlayer4playEv");
void qtmm_QMediaPlayer_play(void *self)
{
    (void)self;
    log_call("QMediaPlayer::play");
}

__attribute__((visibility("default")))
void qtmm_QMediaContent_ctor(void *self) __asm__("_ZN13QMediaContentC1Ev");
void qtmm_QMediaContent_ctor(void *self)
{
    (void)self;
    log_call("QMediaContent::QMediaContent");
}

__attribute__((visibility("default")))
void qtmm_QMediaPlayer_setMedia(void *self, const void *content,
                                void *device)
    __asm__("_ZN12QMediaPlayer8setMediaERK13QMediaContentP9QIODevice");
void qtmm_QMediaPlayer_setMedia(void *self, const void *content,
                                void *device)
{
    (void)self;
    (void)content;
    (void)device;
    log_call("QMediaPlayer::setMedia");
}

__attribute__((visibility("default")))
void qtmm_QMediaContent_dtor(void *self) __asm__("_ZN13QMediaContentD1Ev");
void qtmm_QMediaContent_dtor(void *self)
{
    (void)self;
    log_call("QMediaContent::~QMediaContent");
}

__attribute__((visibility("default")))
void qtmm_QMediaPlayer_ctor(void *self, void *parent, unsigned int flags)
    __asm__("_ZN12QMediaPlayerC1EP7QObject6QFlagsINS_4FlagEE");
void qtmm_QMediaPlayer_ctor(void *self, void *parent, unsigned int flags)
{
    (void)self;
    (void)parent;
    (void)flags;
    log_call("QMediaPlayer::QMediaPlayer");
}

__attribute__((visibility("default")))
void *qtmm_QMediaPlayer_mediaStream(const void *self)
    __asm__("_ZNK12QMediaPlayer11mediaStreamEv");
void *qtmm_QMediaPlayer_mediaStream(const void *self)
{
    (void)self;
    log_call("QMediaPlayer::mediaStream");
    return 0;
}
