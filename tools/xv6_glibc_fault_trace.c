#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>
#include <wchar.h>

#include "uabi/d3dkmthk.h"

typedef void *(*memset_fn)(void *, int, size_t);
typedef void *(*malloc_fn)(size_t);
typedef void *(*calloc_fn)(size_t, size_t);
typedef void *(*realloc_fn)(void *, size_t);
typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
typedef int (*munmap_fn)(void *, size_t);
typedef wchar_t *(*wmemset_fn)(wchar_t *, wchar_t, size_t);
typedef int (*shm_open_fn)(const char *, int, mode_t);
typedef int (*ftruncate_fn)(int, off_t);
typedef int (*fxstat_fn)(int, int, struct stat *);
typedef int (*ioctl_fn)(int, unsigned long, void *);

static __thread int in_trace;
static ioctl_fn real_ioctl;
static volatile unsigned long last_dxg_nr;
static volatile int last_dxg_rc;
static volatile uint32_t last_submit_queue;
static volatile uint64_t last_submit_fence;
static volatile uint64_t last_submit_cmd;
static volatile uint32_t last_submit_len;
static volatile uint32_t last_submit_priv;
static volatile uint32_t last_lock_allocation;
static volatile uint64_t last_lock_data;
static volatile uint32_t last_map_allocation;
static volatile uint64_t last_map_va;
static volatile uint64_t last_map_fence;
static volatile uint64_t last_make_fence;
static int trace_submit_only;
static int trace_compact;
static uint32_t trace_head_limit;

static uintptr_t trace_fault_pc(void *ctx)
{
#if defined(__x86_64__) && defined(REG_RIP)
    ucontext_t *uc = (ucontext_t *)ctx;

    return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#else
    (void)ctx;
    return 0;
#endif
}

static void trace_signal_handler(int sig, siginfo_t *info, void *ctx)
{
    uintptr_t pc = trace_fault_pc(ctx);
    Dl_info dli;

    memset(&dli, 0, sizeof(dli));
    dladdr((void *)pc, &dli);
    dprintf(2,
            "dxgfault: signal=%d pc=0x%lx fault=0x%lx object=%s base=0x%lx offset=0x%lx symbol=%s\n",
            sig, (unsigned long)pc,
            (unsigned long)(uintptr_t)(info ? info->si_addr : NULL),
            dli.dli_fname ? dli.dli_fname : "(unknown)",
            (unsigned long)(uintptr_t)dli.dli_fbase,
            dli.dli_fbase ? (unsigned long)(pc - (uintptr_t)dli.dli_fbase) : 0,
            dli.dli_sname ? dli.dli_sname : "(unknown)");
    dprintf(2,
            "dxgfault: last_dxg nr=0x%lx rc=%d submit_queue=0x%x submit_fence=%lu submit_cmd=0x%lx submit_len=%u submit_priv=%u lock_alloc=0x%x lock_data=0x%lx map_alloc=0x%x map_va=0x%lx map_fence=%lu make_fence=%lu\n",
            last_dxg_nr, last_dxg_rc, last_submit_queue,
            (unsigned long)last_submit_fence, (unsigned long)last_submit_cmd,
            last_submit_len, last_submit_priv, last_lock_allocation,
            (unsigned long)last_lock_data, last_map_allocation,
            (unsigned long)last_map_va, (unsigned long)last_map_fence,
            (unsigned long)last_make_fence);
    _exit(128 + sig);
}

static void trace_msg(const char *op, uintptr_t a, uintptr_t b, uintptr_t c,
                      uintptr_t ret, int err)
{
    if (in_trace)
        return;
    in_trace = 1;
    fprintf(stderr,
            "glibcfault: %s a=0x%lx b=0x%lx c=0x%lx ret=0x%lx errno=%d caller=%p\n",
            op, (unsigned long)a, (unsigned long)b, (unsigned long)c,
            (unsigned long)ret, err, __builtin_return_address(0));
    in_trace = 0;
}

static unsigned long trace_req_nr(unsigned long request)
{
    return (request >> _IOC_NRSHIFT) & ((1U << _IOC_NRBITS) - 1U);
}

static unsigned long trace_req_type(unsigned long request)
{
    return (request >> _IOC_TYPESHIFT) & ((1U << _IOC_TYPEBITS) - 1U);
}

static unsigned long trace_req_size(unsigned long request)
{
    return (request >> _IOC_SIZESHIFT) & ((1U << _IOC_SIZEBITS) - 1U);
}

static int trace_interesting_nr(unsigned long nr)
{
    switch (nr) {
    case 0x0f:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x3a:
    case 0x3b:
        return 1;
    default:
        return 0;
    }
}

static int trace_should_log(unsigned long request, void *arg)
{
    unsigned long nr = trace_req_nr(request);

    if (trace_req_type(request) != 0x47 || in_trace)
        return 0;
    if (!trace_submit_only)
        return 1;
    if (!trace_interesting_nr(nr))
        return 0;
    if (nr == 0x3a) {
        struct d3dkmt_waitforsynchronizationobjectfromcpu *a = arg;

        return a != NULL && a->async_event != 0;
    }
    return 1;
}

static uint32_t trace_bits32(const void *ptr)
{
    uint32_t value;

    memcpy(&value, ptr, sizeof(value));
    return value;
}

static void trace_dump_head(const char *name, uint64 ptr, uint32 size)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)ptr;
    uint32 limit = trace_head_limit != 0 ? trace_head_limit : 64;
    uint32 n = size < limit ? size : limit;

    if (ptr == 0 || size == 0 || in_trace || trace_compact)
        return;
    in_trace = 1;
    dprintf(2, "dxgtrace: %s size=%u head=", name, size);
    for (uint32 i = 0; i < n; i++)
        dprintf(2, "%02x", p[i]);
    dprintf(2, "\n");
    in_trace = 0;
}

static void trace_dump_u32_array(const char *name, uint64 ptr, uint32 count)
{
    const uint32_t *p = (const uint32_t *)(uintptr_t)ptr;
    uint32 n = count < 8 ? count : 8;

    if (ptr == 0 || count == 0 || in_trace)
        return;
    in_trace = 1;
    dprintf(2, "dxgtrace: %s count=%u", name, count);
    for (uint32 i = 0; i < n; i++)
        dprintf(2, " [%u]=0x%x", i, p[i]);
    if (count > n)
        dprintf(2, " ...");
    dprintf(2, "\n");
    in_trace = 0;
}

static void trace_dump_u64_array(const char *name, uint64 ptr, uint32 count)
{
    const uint64_t *p = (const uint64_t *)(uintptr_t)ptr;
    uint32 n = count < 8 ? count : 8;

    if (ptr == 0 || count == 0 || in_trace)
        return;
    in_trace = 1;
    dprintf(2, "dxgtrace: %s count=%u", name, count);
    for (uint32 i = 0; i < n; i++)
        dprintf(2, " [%u]=0x%lx", i, (unsigned long)p[i]);
    if (count > n)
        dprintf(2, " ...");
    dprintf(2, "\n");
    in_trace = 0;
}

static void trace_dxg_before(unsigned long request, void *arg)
{
    unsigned long nr = trace_req_nr(request);

    if (trace_req_type(request) != 0x47 || arg == NULL)
        return;

    switch (nr) {
    case 0x01: {
        struct d3dkmt_openadapterfromluid *a = arg;
        dprintf(2, "dxgtrace: open_adapter_luid luid=%x:%x\n",
                a->adapter_luid.b, a->adapter_luid.a);
        break;
    }
    case 0x02: {
        struct d3dkmt_createdevice *a = arg;
        dprintf(2, "dxgtrace: create_device adapter=0x%x flags=0x%x\n",
                a->adapter.v, trace_bits32(&a->flags));
        break;
    }
    case 0x04: {
        struct d3dkmt_createcontextvirtual *a = arg;
        dprintf(2,
                "dxgtrace: create_context device=0x%x node=%u engine=%u flags=0x%x hint=%u priv=%u\n",
                a->device.v, a->node_ordinal, a->engine_affinity,
                a->flags.value, a->client_hint, a->priv_drv_data_size);
        trace_dump_head("create_context_priv", a->priv_drv_data,
                        a->priv_drv_data_size);
        break;
    }
    case 0x06: {
        struct d3dkmt_createallocation *a = arg;
        const struct d3dddi_allocationinfo2 *info =
            (const struct d3dddi_allocationinfo2 *)(uintptr_t)a->allocation_info;

        dprintf(2,
                "dxgtrace: create_allocation device=0x%x resource=0x%x count=%u runtime=%u priv=%u flags=0x%x\n",
                a->device.v, a->resource.v, a->alloc_count,
                a->private_runtime_data_size, a->priv_drv_data_size,
                a->flags.value);
        trace_dump_head("create_allocation_runtime",
                        a->private_runtime_data,
                        a->private_runtime_data_size);
        trace_dump_head("create_allocation_priv", a->priv_drv_data,
                        a->priv_drv_data_size);
        if (info != NULL && a->alloc_count != 0) {
            dprintf(2,
                    "dxgtrace: create_allocation alloc0 handle=0x%x sysmem=0x%lx priv=%u flags=0x%x gpuva=0x%lx pri=0x%lx\n",
                    info[0].allocation.v, info[0].sysmem,
                    info[0].priv_drv_data_size, info[0].flags.value,
                    info[0].gpu_virtual_address, info[0].unused);
            trace_dump_head("create_allocation_alloc_priv",
                            info[0].priv_drv_data,
                            info[0].priv_drv_data_size);
        }
        break;
    }
    case 0x07: {
        struct d3dkmt_createpagingqueue *a = arg;
        dprintf(2, "dxgtrace: create_paging_queue device=0x%x priority=%d\n",
                a->device.v, a->priority);
        break;
    }
    case 0x08: {
        struct d3dddi_reservegpuvirtualaddress *a = arg;
        dprintf(2,
                "dxgtrace: reserve_gpu_va adapter=0x%x base=0x%lx min=0x%lx max=0x%lx size=0x%lx type=%u\n",
                a->adapter.v, a->base_address, a->minimum_address,
                a->maximum_address, a->size, a->reservation_type);
        break;
    }
    case 0x09: {
        struct d3dkmt_queryadapterinfo *a = arg;
        dprintf(2, "dxgtrace: query_adapter adapter=0x%x type=%u size=%u\n",
                a->adapter.v, a->type, a->private_data_size);
        break;
    }
    case 0x0b: {
        struct d3dddi_makeresident *a = arg;
        dprintf(2, "dxgtrace: make_resident paging=0x%x count=%u flags=0x%x\n",
                a->paging_queue.v, a->alloc_count, a->flags.value);
        trace_dump_u32_array("make_resident_allocations",
                             a->allocation_list, a->alloc_count);
        break;
    }
    case 0x0c: {
        struct d3dddi_mapgpuvirtualaddress *a = arg;
        dprintf(2,
                "dxgtrace: map_gpu_va paging=0x%x alloc=0x%x base=0x%lx min=0x%lx max=0x%lx offset=0x%lx pages=0x%lx prot=0x%lx dprot=0x%lx\n",
                a->paging_queue.v, a->allocation.v, a->base_address,
                a->minimum_address, a->maximum_address, a->offset_in_pages,
                a->size_in_pages, a->protection.value,
                a->driver_protection);
        break;
    }
    case 0x10: {
        struct d3dkmt_createsynchronizationobject2 *a = arg;
        dprintf(2,
                "dxgtrace: create_sync device=0x%x type=%u flags=0x%x init=%lu cpu=0x%lx gpu=0x%lx affinity=%u shared=0x%x\n",
                a->device.v, a->info.type, trace_bits32(&a->info.flags),
                a->info.monitored_fence.initial_fence_value,
                a->info.monitored_fence.fence_cpu_virtual_address,
                a->info.monitored_fence.fence_gpu_virtual_address,
                a->info.monitored_fence.engine_affinity,
                a->info.shared_handle.v);
        break;
    }
    case 0x18: {
        struct d3dkmt_createhwqueue *a = arg;
        dprintf(2,
                "dxgtrace: create_hwqueue context=0x%x flags=0x%x priv=%u\n",
                a->context.v, a->flags.value, a->priv_drv_data_size);
        trace_dump_head("create_hwqueue_priv", a->priv_drv_data,
                        a->priv_drv_data_size);
        break;
    }
    case 0x0f: {
        struct d3dkmt_submitcommand *a = arg;
        last_submit_queue = a->broadcast_context_count != 0 ?
                            a->broadcast_context[0].v : 0;
        last_submit_fence = 0;
        last_submit_cmd = a->command_buffer;
        last_submit_len = a->command_length;
        last_submit_priv = a->priv_drv_data_size;
        dprintf(2,
                "dxgtrace: submit_command cmd=0x%lx len=%u flags=0x%x contexts=%u priv=%u primaries=%u histories=%u present=0x%lx\n",
                a->command_buffer, a->command_length, a->flags.value,
                a->broadcast_context_count, a->priv_drv_data_size,
                a->num_primaries, a->num_history_buffers,
                a->present_history_token);
        for (uint32 i = 0;
             i < a->broadcast_context_count && i < D3DDDI_MAX_BROADCAST_CONTEXT;
             i++)
            dprintf(2, "dxgtrace: submit_command_context[%u]=0x%x\n",
                    i, a->broadcast_context[i].v);
        for (uint32 i = 0;
             i < a->num_primaries && i < D3DDDI_MAX_WRITTEN_PRIMARIES;
             i++)
            dprintf(2, "dxgtrace: submit_command_primary[%u]=0x%x\n",
                    i, a->written_primaries[i].v);
        trace_dump_u32_array("submit_command_histories",
                             a->history_buffer_array,
                             a->num_history_buffers);
        trace_dump_head("submit_command_priv", a->priv_drv_data,
                        a->priv_drv_data_size);
        break;
    }
    case 0x25: {
        struct d3dkmt_lock2 *a = arg;
        dprintf(2, "dxgtrace: lock2 device=0x%x alloc=0x%x flags=0x%x data=0x%lx\n",
                a->device.v, a->allocation.v, a->flags.value, a->data);
        break;
    }
    case 0x34: {
        struct d3dkmt_submitcommandtohwqueue *a = arg;
        last_submit_queue = a->hwqueue.v;
        last_submit_fence = a->hwqueue_progress_fence_id;
        last_submit_cmd = a->command_buffer;
        last_submit_len = a->command_length;
        last_submit_priv = a->priv_drv_data_size;
        dprintf(2,
                "dxgtrace: submit_hwqueue queue=0x%x fence=%lu cmd=0x%lx len=%u priv=%u primaries=%u\n",
                a->hwqueue.v, a->hwqueue_progress_fence_id,
                a->command_buffer, a->command_length,
                a->priv_drv_data_size, a->num_primaries);
        trace_dump_head("submit_hwqueue_priv", a->priv_drv_data,
                        a->priv_drv_data_size);
        break;
    }
    case 0x35: {
        struct d3dkmt_submitsignalsyncobjectstohwqueue *a = arg;
        dprintf(2, "dxgtrace: signal_hwqueue hwqueues=%u objects=%u flags=0x%x\n",
                a->hwqueue_count, a->object_count, a->flags.value);
        break;
    }
    case 0x36: {
        struct d3dkmt_submitwaitforsyncobjectstohwqueue *a = arg;
        dprintf(2, "dxgtrace: wait_hwqueue queue=0x%x objects=%u\n",
                a->hwqueue.v, a->object_count);
        break;
    }
    case 0x37: {
        struct d3dkmt_unlock2 *a = arg;
        dprintf(2, "dxgtrace: unlock2 device=0x%x alloc=0x%x\n",
                a->device.v, a->allocation.v);
        break;
    }
    case 0x3a: {
        struct d3dkmt_waitforsynchronizationobjectfromcpu *a = arg;
        dprintf(2,
                "dxgtrace: wait_cpu device=0x%x objects=%u async=0x%lx flags=0x%x\n",
                a->device.v, a->object_count, a->async_event,
                a->flags.value);
        trace_dump_u32_array("wait_cpu_objects", a->objects,
                             a->object_count);
        trace_dump_u64_array("wait_cpu_fences", a->fence_values,
                             a->object_count);
        break;
    }
    case 0x3b: {
        struct d3dkmt_waitforsynchronizationobjectfromgpu *a = arg;
        dprintf(2,
                "dxgtrace: wait_gpu context=0x%x objects=%u fence=%lu legacy=%u\n",
                a->context.v, a->object_count, a->fence_value,
                a->object_count == 1 && a->monitored_fence_values == 0);
        trace_dump_u32_array("wait_gpu_objects", a->objects,
                             a->object_count);
        trace_dump_u64_array("wait_gpu_fences",
                             a->monitored_fence_values, a->object_count);
        break;
    }
    case 0x33: {
        struct d3dkmt_signalsynchronizationobjectfromgpu2 *a = arg;
        dprintf(2,
                "dxgtrace: signal_gpu2 contexts=%u objects=%u flags=0x%x cpu_event=0x%lx\n",
                a->context_count, a->object_count, a->flags.value,
                a->cpu_event_handle);
        trace_dump_u32_array("signal_gpu2_contexts", a->contexts,
                             a->context_count);
        trace_dump_u32_array("signal_gpu2_objects", a->objects,
                             a->object_count);
        trace_dump_u64_array("signal_gpu2_fences",
                             a->monitored_fence_values, a->object_count);
        break;
    }
    default:
        break;
    }
}

static void trace_dxg_after(unsigned long request, void *arg, int rc)
{
    unsigned long nr = trace_req_nr(request);

    if (trace_req_type(request) != 0x47 || arg == NULL)
        return;

    switch (nr) {
    case 0x02: {
        struct d3dkmt_createdevice *a = arg;
        dprintf(2,
                "dxgtrace: -> create_device rc=%d device=0x%x cmd=0x%lx cmd_size=%u alloc=0x%lx patch=0x%lx\n",
                rc, a->device.v, a->command_buffer,
                a->command_buffer_size, a->allocation_list,
                a->patch_location_list);
        break;
    }
    case 0x04: {
        struct d3dkmt_createcontextvirtual *a = arg;
        dprintf(2, "dxgtrace: -> create_context rc=%d context=0x%x\n",
                rc, a->context.v);
        trace_dump_head("create_context_priv_out", a->priv_drv_data,
                        a->priv_drv_data_size);
        break;
    }
    case 0x06: {
        struct d3dkmt_createallocation *a = arg;
        const struct d3dddi_allocationinfo2 *info =
            (const struct d3dddi_allocationinfo2 *)(uintptr_t)a->allocation_info;

        dprintf(2,
                "dxgtrace: -> create_allocation rc=%d resource=0x%x global=0x%x flags=0x%x\n",
                rc, a->resource.v, a->global_share.v, a->flags.value);
        if (info != NULL && a->alloc_count != 0) {
            dprintf(2,
                    "dxgtrace: -> create_allocation alloc0 handle=0x%x priv=%u flags=0x%x gpuva=0x%lx\n",
                    info[0].allocation.v, info[0].priv_drv_data_size,
                    info[0].flags.value, info[0].gpu_virtual_address);
            trace_dump_head("create_allocation_alloc_priv_out",
                            info[0].priv_drv_data,
                            info[0].priv_drv_data_size);
        }
        break;
    }
    case 0x07: {
        struct d3dkmt_createpagingqueue *a = arg;
        dprintf(2,
                "dxgtrace: -> create_paging_queue rc=%d queue=0x%x sync=0x%x fence_cpu=0x%lx\n",
                rc, a->paging_queue.v, a->sync_object.v,
                a->fence_cpu_virtual_address);
        break;
    }
    case 0x08: {
        struct d3dddi_reservegpuvirtualaddress *a = arg;
        dprintf(2, "dxgtrace: -> reserve_gpu_va rc=%d va=0x%lx fence=%lu\n",
                rc, a->virtual_address, a->paging_fence_value);
        break;
    }
    case 0x09: {
        struct d3dkmt_queryadapterinfo *a = arg;
        dprintf(2, "dxgtrace: -> query_adapter rc=%d type=%u size=%u\n",
                rc, a->type, a->private_data_size);
        break;
    }
    case 0x10: {
        struct d3dkmt_createsynchronizationobject2 *a = arg;
        dprintf(2,
                "dxgtrace: -> create_sync rc=%d handle=0x%x type=%u shared=0x%x cpu=0x%lx gpu=0x%lx\n",
                rc, a->sync_object.v, a->info.type,
                a->info.shared_handle.v,
                a->info.monitored_fence.fence_cpu_virtual_address,
                a->info.monitored_fence.fence_gpu_virtual_address);
        break;
    }
    case 0x0b: {
        struct d3dddi_makeresident *a = arg;
        last_make_fence = a->paging_fence_value;
        dprintf(2, "dxgtrace: -> make_resident rc=%d errno=%d fence=%lu trim=%lu\n",
                rc, errno, a->paging_fence_value, a->num_bytes_to_trim);
        break;
    }
    case 0x0c: {
        struct d3dddi_mapgpuvirtualaddress *a = arg;
        last_map_allocation = a->allocation.v;
        last_map_va = a->virtual_address;
        last_map_fence = a->paging_fence_value;
        dprintf(2,
                "dxgtrace: -> map_gpu_va rc=%d pages=0x%lx va=0x%lx fence=%lu\n",
                rc, a->size_in_pages, a->virtual_address,
                a->paging_fence_value);
        break;
    }
    case 0x18: {
        struct d3dkmt_createhwqueue *a = arg;
        dprintf(2,
                "dxgtrace: -> create_hwqueue rc=%d queue=0x%x fence=0x%x fence_cpu=0x%lx fence_gpu=0x%lx\n",
                rc, a->queue.v, a->queue_progress_fence.v,
                a->queue_progress_fence_cpu_va,
                a->queue_progress_fence_gpu_va);
        trace_dump_head("create_hwqueue_priv_out", a->priv_drv_data,
                        a->priv_drv_data_size);
        break;
    }
    case 0x25: {
        struct d3dkmt_lock2 *a = arg;
        last_lock_allocation = a->allocation.v;
        last_lock_data = a->data;
        dprintf(2, "dxgtrace: -> lock2 rc=%d data=0x%lx\n", rc, a->data);
        trace_dump_head("lock2_data", a->data, 64);
        break;
    }
    case 0x0f:
        dprintf(2, "dxgtrace: -> submit_command rc=%d\n", rc);
        break;
    case 0x34:
        dprintf(2, "dxgtrace: -> submit_hwqueue rc=%d\n", rc);
        break;
    case 0x35:
        dprintf(2, "dxgtrace: -> signal_hwqueue rc=%d\n", rc);
        break;
    case 0x36:
        dprintf(2, "dxgtrace: -> wait_hwqueue rc=%d\n", rc);
        break;
    case 0x37:
        dprintf(2, "dxgtrace: -> unlock2 rc=%d\n", rc);
        break;
    case 0x3a:
        dprintf(2, "dxgtrace: -> wait_cpu rc=%d\n", rc);
        break;
    case 0x3b:
        dprintf(2, "dxgtrace: -> wait_gpu rc=%d\n", rc);
        break;
    case 0x33:
        dprintf(2, "dxgtrace: -> signal_gpu2 rc=%d\n", rc);
        break;
    default:
        break;
    }
}

__attribute__((constructor))
static void trace_loaded(void)
{
    struct sigaction sa;
    const char *head_limit_env;

    trace_submit_only = getenv("XV6_DXG_TRACE_SUBMIT_ONLY") != NULL;
    trace_compact = getenv("XV6_DXG_TRACE_COMPACT") != NULL;
    head_limit_env = getenv("XV6_DXG_TRACE_HEAD_BYTES");
    if (head_limit_env != NULL && head_limit_env[0] != '\0') {
        unsigned long value = strtoul(head_limit_env, NULL, 0);

        if (value > 0 && value <= 65536)
            trace_head_limit = (uint32_t)value;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = trace_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    trace_msg("loaded", (uintptr_t)getpid(), 0, 0, 0, errno);
}

void *memset(void *s, int c, size_t n)
{
    static memset_fn real_memset;

    if (!real_memset)
        real_memset = (memset_fn)dlsym(RTLD_NEXT, "memset");
    if (s == NULL && n != 0) {
        trace_msg("memset_null", (uintptr_t)s, (uintptr_t)c, n, 0, errno);
        return s;
    }
    return real_memset(s, c, n);
}

void *__memset_avx2_unaligned_erms(void *s, int c, size_t n)
{
    return memset(s, c, n);
}

void *__memset_avx2_unaligned(void *s, int c, size_t n)
{
    return memset(s, c, n);
}

void *__memset_erms(void *s, int c, size_t n)
{
    return memset(s, c, n);
}

wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n)
{
    static wmemset_fn real_wmemset;

    if (!real_wmemset)
        real_wmemset = (wmemset_fn)dlsym(RTLD_NEXT, "wmemset");
    if (s == NULL && n != 0) {
        trace_msg("wmemset_null", (uintptr_t)s, (uintptr_t)c, n, 0, errno);
        return s;
    }
    return real_wmemset(s, c, n);
}

void *malloc(size_t n)
{
    static malloc_fn real_malloc;
    void *ret;

    if (!real_malloc)
        real_malloc = (malloc_fn)dlsym(RTLD_NEXT, "malloc");
    ret = real_malloc(n);
    if (ret == NULL && n != 0)
        trace_msg("malloc_fail", n, 0, 0, 0, errno);
    return ret;
}

void *calloc(size_t nmemb, size_t size)
{
    static calloc_fn real_calloc;
    void *ret;

    if (!real_calloc)
        real_calloc = (calloc_fn)dlsym(RTLD_NEXT, "calloc");
    ret = real_calloc(nmemb, size);
    if (ret == NULL && nmemb != 0 && size != 0)
        trace_msg("calloc_fail", nmemb, size, 0, 0, errno);
    return ret;
}

void *realloc(void *ptr, size_t size)
{
    static realloc_fn real_realloc;
    void *ret;

    if (!real_realloc)
        real_realloc = (realloc_fn)dlsym(RTLD_NEXT, "realloc");
    ret = real_realloc(ptr, size);
    if (ret == NULL && size != 0)
        trace_msg("realloc_fail", (uintptr_t)ptr, size, 0, 0, errno);
    return ret;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    static mmap_fn real_mmap;
    void *ret;

    if (!real_mmap)
        real_mmap = (mmap_fn)dlsym(RTLD_NEXT, "mmap");
    ret = real_mmap(addr, length, prot, flags, fd, offset);
    if (ret == MAP_FAILED)
        trace_msg("mmap_fail", (uintptr_t)addr, length,
                  ((uintptr_t)(uint32_t)flags << 32) | (uint32_t)prot,
                  (uintptr_t)ret, errno);
    else if (length <= 0x20000)
        trace_msg("mmap_ok", (uintptr_t)addr, length,
                  ((uintptr_t)(uint32_t)flags << 32) | (uint32_t)prot,
                  (uintptr_t)ret, fd);
    return ret;
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd,
             off64_t offset)
{
    return mmap(addr, length, prot, flags, fd, (off_t)offset);
}

int munmap(void *addr, size_t length)
{
    static munmap_fn real_munmap;

    if (!real_munmap)
        real_munmap = (munmap_fn)dlsym(RTLD_NEXT, "munmap");
    return real_munmap(addr, length);
}

int shm_open(const char *name, int oflag, mode_t mode)
{
    static shm_open_fn real_shm_open;
    int ret;

    if (!real_shm_open)
        real_shm_open = (shm_open_fn)dlsym(RTLD_NEXT, "shm_open");
    ret = real_shm_open(name, oflag, mode);
    trace_msg(ret < 0 ? "shm_open_fail" : "shm_open_ok",
              (uintptr_t)name, (uintptr_t)(uint32_t)oflag, mode, ret, errno);
    return ret;
}

int ftruncate(int fd, off_t length)
{
    static ftruncate_fn real_ftruncate;
    int ret;

    if (!real_ftruncate)
        real_ftruncate = (ftruncate_fn)dlsym(RTLD_NEXT, "ftruncate");
    ret = real_ftruncate(fd, length);
    trace_msg(ret < 0 ? "ftruncate_fail" : "ftruncate_ok",
              fd, (uintptr_t)length, 0, ret, errno);
    return ret;
}

int __fxstat(int ver, int fd, struct stat *st)
{
    static fxstat_fn real_fxstat;
    int ret;

    if (!real_fxstat)
        real_fxstat = (fxstat_fn)dlsym(RTLD_NEXT, "__fxstat");
    ret = real_fxstat(ver, fd, st);
    trace_msg(ret < 0 ? "fxstat_fail" : "fxstat_ok",
              ver, fd, st ? (uintptr_t)st->st_size : 0, ret, errno);
    return ret;
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    int ret;

    if (!real_ioctl)
        real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (trace_should_log(request, arg)) {
        in_trace = 1;
        last_dxg_nr = trace_req_nr(request);
        dprintf(2, "dxgtrace: ioctl nr=0x%lx size=0x%lx fd=%d\n",
                trace_req_nr(request), trace_req_size(request), fd);
        in_trace = 0;
        trace_dxg_before(request, arg);
    }
    ret = real_ioctl(fd, request, arg);
    if (trace_should_log(request, arg)) {
        last_dxg_nr = trace_req_nr(request);
        last_dxg_rc = ret;
        trace_dxg_after(request, arg, ret);
    }
    return ret;
}
