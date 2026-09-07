/*
 * Build from the repository root:
 *   cc -O2 -Wall -Wextra -Werror -pthread -idirafter kernel/kernel/inc \
 *     scripts/image/virgl-fence-order-reducer.c \
 *     -o /tmp/virgl-fence-order-reducer
 *
 * Run in a virgl guest booted with virtio_gpu_fence_order_probe=1.
 * The one-shot kernel probe parks prepared A until synchronous B completes.
 * Both contexts must belong to this process and the same open /dev/gpu0 file.
 * Publication order is therefore B, A, regardless of thread scheduling.
 */
#define _POSIX_C_SOURCE 200809L
#define ON_HOST_OS 1

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../../kernel/kernel/inc/dev/fb.h"

_Static_assert(sizeof(uint64) == 8, "FB ABI requires 64-bit uint64");
_Static_assert(sizeof(struct fb_gpu_virgl_ctx) == 72, "context ABI size");
_Static_assert(sizeof(struct fb_gpu_virgl_submit) == 48, "submit ABI size");
_Static_assert(offsetof(struct fb_gpu_virgl_submit, cmd) == 16,
               "submit command ABI offset");
_Static_assert(sizeof(struct fb_gpu_virgl_fence) == 24, "fence ABI size");

struct submission {
    int fd;
    int result;
    int error;
    struct fb_gpu_virgl_submit request;
};

static void timeout_handler(int signal_number)
{
    static const char message[] =
        "RESULT FAIL test=virgl-fence-order stage=timeout limit_seconds=30\n";

    ssize_t written;

    (void)signal_number;
    written = write(STDOUT_FILENO, message, sizeof(message) - 1);
    (void)written;
    _exit(124);
}

static void *submit_nop(void *argument)
{
    struct submission *submission = argument;
    uint32 nop = 0; /* VIRGL_CMD0(VIRGL_CCMD_NOP, 0, 0). */

    submission->request.cmd_size = sizeof(nop);
    submission->request.cmd = (uint64)(uintptr_t)&nop;
    submission->result = ioctl(submission->fd, FB_GPU_VIRGL_SUBMIT,
                               &submission->request);
    submission->error = submission->result < 0 ? errno : 0;
    return NULL;
}

int main(int argc, char **argv)
{
    struct sigaction action = {0};
    struct fb_gpu_virgl_ctx contexts[2] = {0};
    struct fb_gpu_virgl_fence wait_request = {0};
    struct submission a = {.fd = -1, .result = -1};
    struct submission b = {.fd = -1, .result = -1};
    const char *failure = NULL;
    unsigned int cleanup_errors = 0;
    pthread_t a_thread;
    int fd = -1;
    int error;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Run with no arguments in a virgl guest booted with "
             "virtio_gpu_fence_order_probe=1; one run per boot, 30-second limit.");
        return 0;
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--help]\n", argv[0]);
        return 2;
    }
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    action.sa_handler = timeout_handler;
    if (sigemptyset(&action.sa_mask) != 0 ||
        sigaction(SIGALRM, &action, NULL) != 0) {
        printf("RESULT FAIL test=virgl-fence-order stage=alarm errno=%d\n",
               errno);
        return 1;
    }
    alarm(30);

    fd = open("/dev/gpu0", O_RDWR);
    if (fd < 0) {
        printf("CHECK FAIL stage=open errno=%d\n", errno);
        failure = "open";
        goto out;
    }
    for (unsigned int i = 0; i < 2; i++) {
        (void)snprintf(contexts[i].debug_name, sizeof(contexts[i].debug_name),
                       "virgl-fence-%c", i == 0 ? 'A' : 'B');
        if (ioctl(fd, FB_GPU_VIRGL_CTX_CREATE, &contexts[i]) < 0) {
            printf("CHECK FAIL stage=context-create index=%u errno=%d\n",
                   i, errno);
            failure = "context-create";
            goto out;
        }
        if (contexts[i].ctx_id == 0) {
            printf("CHECK FAIL stage=context-id index=%u\n", i);
            failure = "context-id";
            goto out;
        }
    }
    if (contexts[0].ctx_id == contexts[1].ctx_id) {
        failure = "context-identity";
        /* Avoid issuing two destroys for a malformed duplicate result. */
        contexts[1].ctx_id = 0;
        goto out;
    }

    a.fd = b.fd = fd;
    a.request.ctx_id = contexts[0].ctx_id;
    a.request.flags = FB_GPU_VIRGL_SUBMIT_ASYNC;
    b.request.ctx_id = contexts[1].ctx_id;
    printf("START test=virgl-fence-order a_ctx=%u b_ctx=%u limit_seconds=30\n",
           a.request.ctx_id, b.request.ctx_id);
    error = pthread_create(&a_thread, NULL, submit_nop, &a);
    if (error != 0) {
        printf("CHECK FAIL stage=thread-create error=%d\n", error);
        failure = "thread-create";
        goto out;
    }
    (void)submit_nop(&b);
    error = pthread_join(a_thread, NULL);
    if (error != 0) {
        /* Do not destroy contexts while an unjoined submit may still use them. */
        printf("RESULT FAIL test=virgl-fence-order stage=thread-join error=%d\n",
               error);
        _exit(1);
    }
    printf("SUBMITS a_result=%d a_errno=%d a_fence=%lu a_signaled=%lu "
           "b_result=%d b_errno=%d b_fence=%lu b_signaled=%lu\n",
           a.result, a.error, a.request.fence, a.request.signaled,
           b.result, b.error, b.request.fence, b.request.signaled);

    if (a.result < 0 || b.result < 0)
        failure = "submit";
    else if (a.request.fence == 0 || b.request.fence == 0)
        failure = "zero-fence";
    else if (b.request.signaled < b.request.fence)
        failure = "b-incomplete";
    else if (a.request.fence <= b.request.fence)
        failure = "publication-order";

    /* Drain A even when the publication-order assertion failed. */
    if (a.result >= 0 && a.request.fence != 0) {
        wait_request.flags = FB_GPU_VIRGL_FENCE_WAIT;
        wait_request.wait_for = a.request.fence;
        if (ioctl(fd, FB_GPU_VIRGL_FENCE, &wait_request) < 0) {
            printf("CHECK FAIL stage=a-wait errno=%d\n", errno);
            if (failure == NULL)
                failure = "a-wait";
        } else if (wait_request.signaled < a.request.fence) {
            printf("CHECK FAIL stage=a-incomplete signaled=%lu expected=%lu\n",
                   wait_request.signaled, a.request.fence);
            if (failure == NULL)
                failure = "a-incomplete";
        }
    }

out:
    for (unsigned int i = 0; i < 2; i++) {
        if (contexts[i].ctx_id != 0 &&
            ioctl(fd, FB_GPU_VIRGL_CTX_DESTROY, &contexts[i]) < 0) {
            printf("CHECK FAIL stage=context-destroy index=%u ctx=%u errno=%d\n",
                   i, contexts[i].ctx_id, errno);
            cleanup_errors++;
        }
    }
    if (fd >= 0 && close(fd) != 0) {
        printf("CHECK FAIL stage=close errno=%d\n", errno);
        cleanup_errors++;
    }
    if (failure == NULL && cleanup_errors != 0)
        failure = "cleanup";
    alarm(0);
    printf("RESULT %s test=virgl-fence-order stage=%s "
           "a_fence=%lu b_fence=%lu a_final_signaled=%lu cleanup_errors=%u\n",
           failure == NULL ? "PASS" : "FAIL",
           failure == NULL ? "complete" : failure,
           a.request.fence, b.request.fence, wait_request.signaled,
           cleanup_errors);
    return failure == NULL ? 0 : 1;
}
