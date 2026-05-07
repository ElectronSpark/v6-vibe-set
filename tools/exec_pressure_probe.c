#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define MAX_CHUNKS 1024

static int wait_status(pid_t pid)
{
    int status = 0;

    if (waitpid(pid, &status, 0) != pid)
        return 127;
    if (!WIFEXITED(status))
        return 126;
    return WEXITSTATUS(status);
}

int main(int argc, char **argv)
{
    size_t chunk_size = 256 * 1024;
    int leave_chunks = 4;
    void *chunks[MAX_CHUNKS];
    int count = 0;

    if (argc > 1)
        chunk_size = (size_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        leave_chunks = atoi(argv[2]);
    if (chunk_size == 0)
        chunk_size = 4096;

    memset(chunks, 0, sizeof(chunks));
    while (count < MAX_CHUNKS) {
        char *p = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            break;

        for (size_t off = 0; off < chunk_size; off += 4096)
            p[off] = (char)count;
        chunks[count++] = p;
    }

    fprintf(stderr,
            "exec_pressure_probe: allocated=%d chunk=%lu errno=%d (%s)\n",
            count, (unsigned long)chunk_size, errno, strerror(errno));

    for (int i = 0; i < count - leave_chunks; i++) {
        munmap(chunks[i], chunk_size);
        chunks[i] = NULL;
    }

    pid_t pid = fork();
    if (pid == 0) {
        char *child_argv[] = {"/bin/ps", "-l", NULL};
        execv(child_argv[0], child_argv);
        fprintf(stderr,
                "exec_pressure_probe: child exec failed errno=%d (%s)\n",
                errno, strerror(errno));
        _exit(125);
    }
    if (pid < 0) {
        fprintf(stderr, "exec_pressure_probe: fork failed errno=%d (%s)\n",
                errno, strerror(errno));
        return 2;
    }

    int status = wait_status(pid);
    for (int i = 0; i < count; i++) {
        if (chunks[i] != NULL)
            munmap(chunks[i], chunk_size);
    }

    if (status != 0) {
        fprintf(stderr, "exec_pressure_probe: FAIL child status=%d\n", status);
        return 1;
    }

    fprintf(stderr, "exec_pressure_probe: PASS\n");
    return 0;
}
