#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct task_counts {
    int total;
    int stopped;
    int running;
    int sleeping;
    int other;
};

static int ready_fd = -1;
static int use_sigtimedwait_worker = 0;
static int exit_worker_go[2] = {-1, -1};

static void *worker_main(void *arg)
{
    int tid = (int)syscall(SYS_gettid);
    (void)arg;

    if (ready_fd >= 0 && write(ready_fd, &tid, sizeof(tid)) != sizeof(tid))
        _exit(121);
    for (;;)
        pause();
    return NULL;
}

static void *sigtimedwait_worker_main(void *arg)
{
    int tid = (int)syscall(SYS_gettid);
    sigset_t set;
    (void)arg;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    if (ready_fd >= 0 && write(ready_fd, &tid, sizeof(tid)) != sizeof(tid))
        _exit(122);
    for (;;) {
        int sig = sigtimedwait(&set, NULL, NULL);
        if (sig < 0 && errno == EINTR)
            continue;
    }
    return NULL;
}

static void *exit_visibility_worker_main(void *arg)
{
    int tid = (int)syscall(SYS_gettid);
    char ch;
    (void)arg;

    if (ready_fd >= 0 && write(ready_fd, &tid, sizeof(tid)) != sizeof(tid))
        _exit(123);
    if (exit_worker_go[0] >= 0)
        while (read(exit_worker_go[0], &ch, 1) < 0 && errno == EINTR)
            ;
    return NULL;
}

static void child_main(int fd)
{
    pthread_t threads[3];

    ready_fd = fd;
    for (int i = 0; i < 3; i++) {
        void *(*fn)(void *) =
            (i == 0 && use_sigtimedwait_worker) ? sigtimedwait_worker_main :
                                                   worker_main;
        int ret = pthread_create(&threads[i], NULL, fn, NULL);
        if (ret != 0)
            _exit(100 + i);
    }

    for (;;)
        pause();
}

static int run_proc_task_exit_visibility(void)
{
    pthread_t thread;
    int ready[2];
    int procfd;
    int tid = 0;
    char rel[64];
    struct stat st;
    int disappeared_at = -1;

    if (pipe(ready) < 0 || pipe(exit_worker_go) < 0) {
        printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=pipe errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    ready_fd = ready[1];

    int ret = pthread_create(&thread, NULL, exit_visibility_worker_main, NULL);
    if (ret != 0) {
        printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=pthread_create ret=%d\n",
               ret);
        return 1;
    }

    if (read(ready[0], &tid, sizeof(tid)) != sizeof(tid)) {
        printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=ready errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }
    close(ready[1]);
    ready_fd = -1;
    close(ready[0]);

    procfd = open("/proc", O_RDONLY | O_DIRECTORY);
    if (procfd < 0) {
        printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=open-proc errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }

    snprintf(rel, sizeof(rel), "self/task/%d/", tid);
    errno = 0;
    if (fstatat(procfd, rel, &st, 0) < 0) {
        printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=pre-fstatat tid=%d errno=%d %s\n",
               tid, errno, strerror(errno));
        close(procfd);
        return 1;
    }
    printf("kde_thread_group_stop_probe phase=pre-fstatat mode=proc-task-exit-visibility tid=%d status=present\n",
           tid);

    if (write(exit_worker_go[1], "x", 1) != 1) {
        printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=release errno=%d %s\n",
               errno, strerror(errno));
        close(procfd);
        return 1;
    }
    close(exit_worker_go[0]);
    close(exit_worker_go[1]);

    ret = pthread_join(thread, NULL);
    if (ret != 0) {
        printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=pthread_join ret=%d\n",
               ret);
        close(procfd);
        return 1;
    }

    for (int i = 0; i < 30; i++) {
        errno = 0;
        if (fstatat(procfd, rel, &st, 0) < 0) {
            if (errno == ENOENT) {
                disappeared_at = i;
                printf("kde_thread_group_stop_probe phase=post-fstatat mode=proc-task-exit-visibility tid=%d iter=%d errno=%d status=gone\n",
                       tid, i, errno);
                break;
            }
            printf("kde_thread_group_stop_probe status=FAIL mode=proc-task-exit-visibility phase=post-fstatat tid=%d iter=%d errno=%d %s\n",
                   tid, i, errno, strerror(errno));
            close(procfd);
            return 1;
        }
        printf("kde_thread_group_stop_probe phase=post-fstatat mode=proc-task-exit-visibility tid=%d iter=%d status=present\n",
               tid, i);
        usleep(100000);
    }

    close(procfd);
    if (disappeared_at >= 0) {
        printf("kde_thread_group_stop_probe status=PASS mode=proc-task-exit-visibility tid=%d disappeared_at=%d\n",
               tid, disappeared_at);
        return 0;
    }

    printf("kde_thread_group_stop_probe status=FAIL reason=stale-proc-task-entry mode=proc-task-exit-visibility tid=%d iterations=30\n",
           tid);
    return 1;
}

static int read_task_state(pid_t pid, const char *tid_name, char *state_out)
{
    char path[320];
    char line[256];
    FILE *f;

    snprintf(path, sizeof(path), "/proc/%ld/task/%s/status", (long)pid,
             tid_name);
    f = fopen(path, "r");
    if (f == NULL)
        return -1;

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "State:", 6) == 0) {
            char *p = line + 6;
            while (*p == ' ' || *p == '\t')
                p++;
            *state_out = *p != '\0' ? *p : '?';
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return -1;
}

static unsigned long read_status_kb(const char *key)
{
    FILE *f;
    char line[256];
    size_t key_len = strlen(key);
    unsigned long value = 0;

    f = fopen("/proc/self/status", "r");
    if (f == NULL)
        return 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, key, key_len) == 0) {
            sscanf(line + key_len, "%lu", &value);
            break;
        }
    }
    fclose(f);
    return value;
}

static long elapsed_ms(struct timespec start, struct timespec end)
{
    long sec = (long)(end.tv_sec - start.tv_sec);
    long nsec = end.tv_nsec - start.tv_nsec;

    return sec * 1000 + nsec / 1000000;
}

static void print_sparse_status(const char *phase, long elapsed)
{
    printf("kde_thread_group_stop_probe phase=%s mode=sparse-mmap-mprotect "
           "elapsed_ms=%ld VmSize=%lu VmRSS=%lu VmPTE=%lu\n",
           phase, elapsed, read_status_kb("VmSize:"),
           read_status_kb("VmRSS:"), read_status_kb("VmPTE:"));
}

static int run_sparse_mmap_mprotect(int argc, char **argv)
{
    const size_t page_size = 4096;
    unsigned long gb = 16;
    size_t len;
    void *addr;
    struct timespec t0;
    struct timespec t1;

    if (argc > 2)
        gb = strtoul(argv[2], NULL, 0);
    if (gb == 0 || gb > 2048) {
        printf("kde_thread_group_stop_probe status=FAIL mode=sparse-mmap-mprotect "
               "phase=args gb=%lu\n",
               gb);
        return 1;
    }

    len = (size_t)gb * 1024ULL * 1024ULL * 1024ULL;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    print_sparse_status("start", 0);

    addr = mmap(NULL, len, PROT_NONE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (addr == MAP_FAILED) {
        printf("kde_thread_group_stop_probe status=FAIL mode=sparse-mmap-mprotect "
               "phase=mmap gb=%lu errno=%d %s\n",
               gb, errno, strerror(errno));
        return 1;
    }
    print_sparse_status("after-prot-none-noreserve", elapsed_ms(t0, t1));

    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (mprotect(addr, page_size, PROT_READ | PROT_WRITE) != 0) {
        printf("kde_thread_group_stop_probe status=FAIL mode=sparse-mmap-mprotect "
               "phase=mprotect-head errno=%d %s\n",
               errno, strerror(errno));
        munmap(addr, len);
        return 1;
    }
    ((volatile char *)addr)[0] = 7;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    print_sparse_status("after-touch-one-page", elapsed_ms(t0, t1));

    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (mprotect(addr, len, PROT_READ | PROT_WRITE) != 0) {
        printf("kde_thread_group_stop_probe status=FAIL mode=sparse-mmap-mprotect "
               "phase=mprotect-all errno=%d %s\n",
               errno, strerror(errno));
        munmap(addr, len);
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    print_sparse_status("after-mprotect-all", elapsed_ms(t0, t1));

    munmap(addr, len);
    print_sparse_status("after-munmap", 0);
    printf("kde_thread_group_stop_probe status=PASS mode=sparse-mmap-mprotect "
           "gb=%lu\n",
           gb);
    return 0;
}

static int count_task_states(pid_t pid, struct task_counts *counts)
{
    char path[64];
    DIR *dir;
    struct dirent *de;

    memset(counts, 0, sizeof(*counts));
    snprintf(path, sizeof(path), "/proc/%ld/task", (long)pid);
    dir = opendir(path);
    if (dir == NULL)
        return -1;

    while ((de = readdir(dir)) != NULL) {
        char state = '?';

        if (de->d_name[0] == '.')
            continue;
        if (read_task_state(pid, de->d_name, &state) < 0)
            continue;

        counts->total++;
        if (state == 'T')
            counts->stopped++;
        else if (state == 'R')
            counts->running++;
        else if (state == 'S')
            counts->sleeping++;
        else
            counts->other++;
        printf("kde_thread_group_stop_probe phase=task tid=%s state=%c\n",
               de->d_name, state);
    }

    closedir(dir);
    return counts->total > 0 ? 0 : -1;
}

static void cleanup_child(pid_t child)
{
    if (child > 0) {
        kill(child, SIGCONT);
        kill(child, SIGKILL);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

int main(int argc, char **argv)
{
    int ready[2];
    pid_t child;
    int ready_count = 0;
    int worker_tids[3] = {0};
    struct task_counts counts;
    const char *mode = "kill";

    if (argc > 1 && strcmp(argv[1], "--tgkill") == 0)
        mode = "tgkill";
    if (argc > 1 && strcmp(argv[1], "--tgkill-sigtimedwait") == 0) {
        mode = "tgkill-sigtimedwait";
        use_sigtimedwait_worker = 1;
    }
    if (argc > 1 && strcmp(argv[1], "--proc-task-exit-visibility") == 0)
        return run_proc_task_exit_visibility();
    if (argc > 1 && strcmp(argv[1], "--sparse-mmap-mprotect") == 0)
        return run_sparse_mmap_mprotect(argc, argv);

    if (pipe(ready) < 0) {
        printf("kde_thread_group_stop_probe status=FAIL phase=pipe errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }

    child = fork();
    if (child < 0) {
        printf("kde_thread_group_stop_probe status=FAIL phase=fork errno=%d %s\n",
               errno, strerror(errno));
        close(ready[0]);
        close(ready[1]);
        return 1;
    }
    if (child == 0) {
        close(ready[0]);
        child_main(ready[1]);
        _exit(0);
    }

    close(ready[1]);
    while (ready_count < 3) {
        int tid = 0;
        ssize_t n = read(ready[0], &tid, sizeof(tid));

        if (n == sizeof(tid)) {
            worker_tids[ready_count] = tid;
            ready_count++;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        printf("kde_thread_group_stop_probe status=FAIL phase=ready count=%d n=%ld errno=%d %s\n",
               ready_count, (long)n, errno, strerror(errno));
        close(ready[0]);
        cleanup_child(child);
        return 1;
    }
    close(ready[0]);

    printf("kde_thread_group_stop_probe phase=before-stop mode=%s child=%ld worker_ready=%d target_tid=%d\n",
           mode, (long)child, ready_count, worker_tids[0]);
    fflush(stdout);

    if (strcmp(mode, "tgkill") == 0 ||
        strcmp(mode, "tgkill-sigtimedwait") == 0) {
        if (syscall(SYS_tgkill, child, worker_tids[0], SIGSTOP) < 0) {
            printf("kde_thread_group_stop_probe status=FAIL phase=tgkill-stop errno=%d %s\n",
                   errno, strerror(errno));
            cleanup_child(child);
            return 1;
        }
    } else {
        if (kill(child, SIGSTOP) < 0) {
            printf("kde_thread_group_stop_probe status=FAIL phase=kill-stop errno=%d %s\n",
                   errno, strerror(errno));
            cleanup_child(child);
            return 1;
        }
    }

    usleep(250000);
    if (count_task_states(child, &counts) < 0) {
        printf("kde_thread_group_stop_probe status=FAIL phase=count errno=%d %s\n",
               errno, strerror(errno));
        cleanup_child(child);
        return 1;
    }

    printf("kde_thread_group_stop_probe phase=after-stop mode=%s total=%d stopped=%d running=%d sleeping=%d other=%d\n",
           mode, counts.total, counts.stopped, counts.running, counts.sleeping,
           counts.other);

    cleanup_child(child);
    if (counts.total >= 4 && counts.stopped == counts.total) {
        printf("kde_thread_group_stop_probe status=PASS mode=%s total=%d stopped=%d\n",
               mode, counts.total, counts.stopped);
        return 0;
    }

    printf("kde_thread_group_stop_probe status=FAIL reason=partial-thread-group-stop mode=%s total=%d stopped=%d running=%d sleeping=%d other=%d\n",
           mode, counts.total, counts.stopped, counts.running, counts.sleeping,
           counts.other);
    return 1;
}
