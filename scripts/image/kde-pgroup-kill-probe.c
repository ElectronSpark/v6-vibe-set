#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void child_main(int ready_fd, pid_t pgid)
{
    int err = 0;
    char byte = 'R';

    if (setpgid(0, pgid) < 0)
        err = errno;
    if (write(ready_fd, &byte, 1) != 1)
        _exit(120);
    close(ready_fd);
    if (err != 0)
        _exit(100);
    for (;;)
        pause();
}

static void cleanup_children(pid_t *children, int count, pid_t pgid)
{
    if (pgid > 0)
        kill(-pgid, SIGKILL);
    for (int i = 0; i < count; i++) {
        if (children[i] > 0)
            kill(children[i], SIGKILL);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

int main(void)
{
    pid_t children[3] = {0};
    int ready[2];
    pid_t pgid = -1;
    int reaped = 0;
    int signaled = 0;
    int kill_errno = 0;

    if (pipe(ready) < 0) {
        printf("kde_pgroup_kill_probe status=FAIL phase=pipe errno=%d %s\n",
               errno, strerror(errno));
        return 1;
    }

    for (int i = 0; i < 3; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            printf("kde_pgroup_kill_probe status=FAIL phase=fork index=%d errno=%d %s\n",
                   i, errno, strerror(errno));
            cleanup_children(children, i, pgid);
            return 1;
        }
        if (pid == 0) {
            close(ready[0]);
            child_main(ready[1], i == 0 ? 0 : pgid);
        }
        children[i] = pid;
        if (i == 0)
            pgid = pid;
        if (setpgid(pid, pgid) < 0) {
            printf("kde_pgroup_kill_probe status=FAIL phase=setpgid-parent index=%d pid=%ld pgid=%ld errno=%d %s\n",
                   i, (long)pid, (long)pgid, errno, strerror(errno));
            cleanup_children(children, i + 1, pgid);
            return 1;
        }
    }

    close(ready[1]);
    for (int i = 0; i < 3; i++) {
        char byte;

        if (read(ready[0], &byte, 1) != 1) {
            printf("kde_pgroup_kill_probe status=FAIL phase=child-ready index=%d errno=%d %s\n",
                   i, errno, strerror(errno));
            cleanup_children(children, 3, pgid);
            close(ready[0]);
            return 1;
        }
    }
    close(ready[0]);

    printf("kde_pgroup_kill_probe phase=before-kill pgid=%ld children=%ld,%ld,%ld\n",
           (long)pgid, (long)children[0], (long)children[1],
           (long)children[2]);
    fflush(stdout);

    errno = 0;
    if (kill(-pgid, SIGTERM) < 0)
        kill_errno = errno;

    printf("kde_pgroup_kill_probe phase=after-kill ret=%d errno=%d %s\n",
           kill_errno == 0 ? 0 : -1, kill_errno, strerror(kill_errno));
    fflush(stdout);

    time_t deadline = time(NULL) + 10;
    while (reaped < 3 && time(NULL) <= deadline) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            reaped++;
            if (WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM)
                signaled++;
            printf("kde_pgroup_kill_probe phase=reap pid=%ld status=0x%x reaped=%d signaled=%d\n",
                   (long)pid, status, reaped, signaled);
            continue;
        }
        if (pid < 0 && errno == ECHILD)
            break;
        usleep(100000);
    }

    if (kill_errno == 0 && reaped == 3 && signaled == 3) {
        printf("kde_pgroup_kill_probe status=PASS reaped=%d signaled=%d\n",
               reaped, signaled);
        return 0;
    }

    printf("kde_pgroup_kill_probe status=FAIL reaped=%d signaled=%d kill_errno=%d\n",
           reaped, signaled, kill_errno);
    cleanup_children(children, 3, pgid);
    return 1;
}
