#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void mkdir_one(const char *path, mode_t mode)
{
    if (mkdir(path, mode) < 0 && errno != EEXIST)
        printf("kde_kwriteconfig_probe mkdir path=%s errno=%d %s\n",
               path, errno, strerror(errno));
    if (chmod(path, mode) < 0)
        printf("kde_kwriteconfig_probe chmod path=%s errno=%d %s\n",
               path, errno, strerror(errno));
}

static void dump_file(const char *path)
{
    char buf[512];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;

    if (fd < 0) {
        printf("kde_kwriteconfig_probe file path=%s open_errno=%d %s\n",
               path, errno, strerror(errno));
        return;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("kde_kwriteconfig_probe file path=%s read_errno=%d %s\n",
               path, errno, strerror(errno));
        close(fd);
        return;
    }
    buf[n] = '\0';
    close(fd);
    printf("kde_kwriteconfig_probe file path=%s bytes=%ld begin\n%s\nkde_kwriteconfig_probe file end\n",
           path, (long)n, buf);
}

static int file_contains(const char *path, const char *needle)
{
    char buf[4096];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;
    size_t used = 0;
    size_t needle_len = strlen(needle);

    if (fd < 0) {
        printf("kde_kwriteconfig_probe verify path=%s open_errno=%d %s\n",
               path, errno, strerror(errno));
        return 0;
    }

    while ((n = read(fd, buf + used, sizeof(buf) - 1 - used)) > 0) {
        used += (size_t)n;
        buf[used] = '\0';
        if (strstr(buf, needle)) {
            close(fd);
            return 1;
        }
        if (used > needle_len) {
            size_t keep = needle_len - 1;
            memmove(buf, buf + used - keep, keep);
            used = keep;
        }
    }

    if (n < 0)
        printf("kde_kwriteconfig_probe verify path=%s read_errno=%d %s\n",
               path, errno, strerror(errno));
    close(fd);
    return 0;
}

static int verify_key(const char *path, const char *key)
{
    char needle[160];

    snprintf(needle, sizeof(needle), "%s=true", key);
    if (file_contains(path, needle)) {
        printf("kde_kwriteconfig_probe verify path=%s key=%s status=PASS\n",
               path, key);
        return 0;
    }

    printf("kde_kwriteconfig_probe verify path=%s key=%s status=FAIL missing=%s\n",
           path, key, needle);
    return 1;
}

static void dump_proc_file(pid_t pid, const char *name)
{
    char path[128];
    char buf[512];
    int fd;
    ssize_t n;

    snprintf(path, sizeof(path), "/proc/%d/%s", (int)pid, name);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        printf("kde_kwriteconfig_probe proc path=%s open_errno=%d %s\n",
               path, errno, strerror(errno));
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("kde_kwriteconfig_probe proc path=%s read_errno=%d %s\n",
               path, errno, strerror(errno));
        close(fd);
        return;
    }
    buf[n] = '\0';
    close(fd);
    printf("kde_kwriteconfig_probe proc path=%s bytes=%ld begin\n%s\nkde_kwriteconfig_probe proc end\n",
           path, (long)n, buf);
}

static void set_kde_env(void)
{
    mkdir_one("/tmp", 01777);
    mkdir_one("/dev/shm/kde-config", 0700);
    mkdir_one("/dev/shm/kde-cache", 0700);
    mkdir_one("/dev/shm/xdg-runtime-root", 0700);
    mkdir_one("/dev/shm/kde-data", 0700);
    mkdir_one("/dev/shm/kde-state", 0700);

    setenv("HOME", "/root", 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    setenv("SHELL", "/bin/sh", 1);
    setenv("XDG_RUNTIME_DIR", "/dev/shm/xdg-runtime-root", 1);
    setenv("XDG_CACHE_HOME", "/dev/shm/kde-cache", 1);
    setenv("XDG_CONFIG_HOME", "/dev/shm/kde-config", 1);
    setenv("XDG_DATA_HOME", "/dev/shm/kde-data", 1);
    setenv("XDG_STATE_HOME", "/dev/shm/kde-state", 1);
    setenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share:/share", 1);
    setenv("XDG_CONFIG_DIRS", "/etc/xdg:/usr/share/kubuntu-default-settings/kf5-settings", 1);
    setenv("XDG_CURRENT_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_DESKTOP", "KDE", 1);
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("QT_QPA_PLATFORM", "wayland", 1);
    setenv("LD_LIBRARY_PATH",
           "/opt/xv6-kde-abi-libs:/usr/lib/x86_64-linux-gnu:/usr/lib:"
           "/lib/x86_64-linux-gnu:/lib",
           1);
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
}

static int run_kwriteconfig(const char *file, const char *key)
{
    char *argv[] = {
        "/usr/bin/kwriteconfig5",
        "--file", (char *)file,
        "--group", "General",
        "--key", (char *)key,
        "true",
        NULL
    };
    pid_t pid = fork();
    int status = 0;

    if (pid < 0) {
        printf("kde_kwriteconfig_probe fork errno=%d %s\n", errno, strerror(errno));
        return 127;
    }

    if (pid == 0) {
        execv(argv[0], argv);
        printf("kde_kwriteconfig_probe exec errno=%d %s\n", errno, strerror(errno));
        _exit(127);
    }

    for (int i = 0; i < 150; i++) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            if (WIFEXITED(status)) {
                printf("kde_kwriteconfig_probe file=%s child exited status=%d\n",
                       file, WEXITSTATUS(status));
                return WEXITSTATUS(status);
            }
            if (WIFSIGNALED(status)) {
                printf("kde_kwriteconfig_probe file=%s child signaled sig=%d\n",
                       file, WTERMSIG(status));
                return 128 + WTERMSIG(status);
            }
        } else if (ret < 0) {
            printf("kde_kwriteconfig_probe waitpid errno=%d %s\n",
                   errno, strerror(errno));
            return 127;
        }
        usleep(100000);
    }

    printf("kde_kwriteconfig_probe file=%s child timeout; killing pid=%d\n",
           file, (int)pid);
    dump_proc_file(pid, "status");
    dump_proc_file(pid, "wchan");
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return 124;
}

static int run_and_verify(const char *file, const char *key)
{
    char path[256];
    int failed = 0;
    int ret;

    snprintf(path, sizeof(path), "/dev/shm/kde-config/%s", file);
    ret = run_kwriteconfig(file, key);
    failed |= ret != 0;
    dump_file(path);
    failed |= verify_key(path, key);
    return failed;
}

int main(void)
{
    int failed = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    set_kde_env();
    unlink("/dev/shm/kde-config/xv6-kde-kwriteconfigrc");
    unlink("/dev/shm/kde-config/xv6-kde-kwriteconfigrc.lock");

    failed |= run_and_verify("xv6-kde-kwriteconfigrc", "Probe");
    failed |= run_and_verify("kwinrc", "Xv6LiveKwinrcProbe");
    failed |= run_and_verify("kglobalshortcutsrc", "Xv6LiveShortcutsProbe");
    failed |= run_and_verify("kactivitymanagerdrc", "Xv6LiveActivityProbe");

    printf("kde_kwriteconfig_probe result=%d\n", failed);
    return failed ? 1 : 0;
}
