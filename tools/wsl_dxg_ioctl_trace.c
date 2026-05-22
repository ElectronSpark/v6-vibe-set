#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define ON_HOST_OS 1
#include "kernel/kernel/inc/uabi/d3dkmthk.h"

#define DXGTRACE_FULL_HEX_MAX 4096U
#define DXGTRACE_QUERY_ADAPTER_TYPE0_SNAPSHOT_BYTES 32U

static int (*real_ioctl_fn)(int fd, unsigned long request, void *arg);

static unsigned long req_nr(unsigned long request)
{
    return (request >> _IOC_NRSHIFT) & ((1U << _IOC_NRBITS) - 1U);
}

static unsigned long req_type(unsigned long request)
{
    return (request >> _IOC_TYPESHIFT) & ((1U << _IOC_TYPEBITS) - 1U);
}

static unsigned long req_size(unsigned long request)
{
    return (request >> _IOC_SIZESHIFT) & ((1U << _IOC_SIZEBITS) - 1U);
}

static void dump_head(const char *name, uint64 ptr, uint32 size)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)ptr;
    uint32 n = size < 96 ? size : 96;

    if (ptr == 0 || size == 0)
        return;
    dprintf(2, "dxgtrace: %s size=%u head=", name, size);
    for (uint32 i = 0; i < n; i++)
        dprintf(2, "%02x", p[i]);
    dprintf(2, "\n");
}

static uint32_t fnv1a_hash(const unsigned char *p, uint32 size)
{
    uint32_t hash = 2166136261U;

    if (p == NULL)
        return 0;
    for (uint32 i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 16777619U;
    }
    return hash;
}

static void dump_full_hex(const char *name, uint64 ptr, uint32 size)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)ptr;
    uint32 n = size < DXGTRACE_FULL_HEX_MAX ? size : DXGTRACE_FULL_HEX_MAX;

    if (ptr == 0 || size == 0)
        return;
    dprintf(2,
            "dxgtrace: %s_full size=%u dumped=%u truncated=%u hash_dumped=0x%08x hex=",
            name, size, n, size > n, fnv1a_hash(p, n));
    for (uint32 i = 0; i < n; i++)
        dprintf(2, "%02x", p[i]);
    dprintf(2, "\n");
}

static void dump_full_hex_alloc(const char *name, const char *phase,
                                uint32 index, uint64 ptr, uint32 size)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)ptr;
    uint32 n = size < DXGTRACE_FULL_HEX_MAX ? size : DXGTRACE_FULL_HEX_MAX;

    if (ptr == 0 || size == 0)
        return;
    dprintf(2,
            "dxgtrace: %s_full phase=%s index=%u size=%u dumped=%u truncated=%u hash_dumped=0x%08x hex=",
            name, phase, index, size, n, size > n, fnv1a_hash(p, n));
    for (uint32 i = 0; i < n; i++)
        dprintf(2, "%02x", p[i]);
    dprintf(2, "\n");
}

static void dump_hex_prefix(const unsigned char *p, uint32 len)
{
    for (uint32 i = 0; i < len; i++)
        dprintf(2, "%02x", p[i]);
}

static void dump_query_adapter_type0_snapshot(
    const struct d3dkmt_queryadapterinfo *a, const char *name,
    int has_rc, int rc)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)a->private_data;
    uint32 size = a->private_data_size;
    uint32 head_len = 0;
    uint32 tail_len = 0;

    if (p != NULL) {
        head_len = size < DXGTRACE_QUERY_ADAPTER_TYPE0_SNAPSHOT_BYTES ?
                   size : DXGTRACE_QUERY_ADAPTER_TYPE0_SNAPSHOT_BYTES;
        tail_len = head_len;
    }

    if (has_rc)
        dprintf(2,
                "dxgtrace: %s rc=%d adapter=0x%x size:%u hash:%08x head_len:%u head:",
                name, rc, a->adapter.v, size, fnv1a_hash(p, size),
                head_len);
    else
        dprintf(2,
                "dxgtrace: %s adapter=0x%x size:%u hash:%08x head_len:%u head:",
                name, a->adapter.v, size, fnv1a_hash(p, size), head_len);
    if (head_len != 0)
        dump_hex_prefix(p, head_len);
    dprintf(2, " tail_len:%u tail:", tail_len);
    if (tail_len != 0)
        dump_hex_prefix(p + size - tail_len, tail_len);
    dprintf(2, "\n");
}

static void dump_u32_array(const char *name, uint64 ptr, uint32 count)
{
    const uint32_t *p = (const uint32_t *)(uintptr_t)ptr;
    uint32 n = count < 8 ? count : 8;

    if (ptr == 0 || count == 0)
        return;
    dprintf(2, "dxgtrace: %s count=%u", name, count);
    for (uint32 i = 0; i < n; i++)
        dprintf(2, " [%u]=0x%x", i, p[i]);
    if (count > n)
        dprintf(2, " ...");
    dprintf(2, "\n");
}

static void dump_u64_array(const char *name, uint64 ptr, uint32 count)
{
    const uint64_t *p = (const uint64_t *)(uintptr_t)ptr;
    uint32 n = count < 8 ? count : 8;

    if (ptr == 0 || count == 0)
        return;
    dprintf(2, "dxgtrace: %s count=%u", name, count);
    for (uint32 i = 0; i < n; i++)
        dprintf(2, " [%u]=0x%lx", i, p[i]);
    if (count > n)
        dprintf(2, " ...");
    dprintf(2, "\n");
}

static void dump_utf16_ascii_prefix(const char *name, uint64 ptr, uint32 size)
{
    const uint16_t *p = (const uint16_t *)(uintptr_t)ptr;
    uint32 count = size / sizeof(uint16_t);
    uint32 start = 0;

    if (ptr == 0 || size < sizeof(uint32))
        return;
    if (size >= sizeof(uint32))
        start = sizeof(uint32) / sizeof(uint16_t);
    dprintf(2, "dxgtrace: %s text=", name);
    for (uint32 i = start; i < count && i < start + 180; i++) {
        uint16_t ch = p[i];

        if (ch == 0)
            break;
        if (ch >= 32 && ch < 127)
            dprintf(2, "%c", (char)ch);
        else
            dprintf(2, "?");
    }
    dprintf(2, "\n");
}

static void dump_utf16_ascii_text(const char *name, uint64 ptr, uint32 size)
{
    const uint16_t *p = (const uint16_t *)(uintptr_t)ptr;
    uint32 count = size / sizeof(uint16_t);

    if (ptr == 0 || size < sizeof(uint16_t))
        return;
    dprintf(2, "dxgtrace: %s text=", name);
    for (uint32 i = 0; i < count && i < 180; i++) {
        uint16_t ch = p[i];

        if (ch == 0)
            break;
        if (ch >= 32 && ch < 127)
            dprintf(2, "%c", (char)ch);
        else
            dprintf(2, "?");
    }
    dprintf(2, "\n");
}

static uint32_t read_le32(const unsigned char *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int known_display_vendor(uint32_t vendor)
{
    return vendor == 0x8086U || vendor == 0x10deU || vendor == 0x1002U;
}

static void dump_adapter_hardware_id(uint64 ptr, uint32 size)
{
    const unsigned char *p = (const unsigned char *)(uintptr_t)ptr;
    uint32_t word0;
    uint32_t word1;
    uint32_t word2;
    uint32_t word3 = 0;
    uint32_t vendor;
    uint32_t device;
    uint32_t subvendor = 0;

    if (ptr == 0 || size < 12)
        return;
    word0 = read_le32(p);
    word1 = read_le32(p + 4);
    word2 = read_le32(p + 8);
    if (size >= 16)
        word3 = read_le32(p + 12);
    if (known_display_vendor(word0)) {
        vendor = word0;
        device = word1;
        subvendor = word2;
    } else {
        vendor = word1;
        device = word2;
        subvendor = word3;
    }
    dprintf(2,
            "dxgtrace: query_adapter_hardware vendor=0x%x device=0x%x subvendor=0x%x raw0=0x%x raw1=0x%x raw2=0x%x raw3=0x%x\n",
            vendor, device, subvendor, word0, word1, word2, word3);
}

static void dump_allocation_info(const char *phase,
                                 const struct d3dkmt_createallocation *a)
{
    const struct d3dddi_allocationinfo2 *info =
        (const struct d3dddi_allocationinfo2 *)(uintptr_t)a->allocation_info;
    uint32 n = a->alloc_count < 8 ? a->alloc_count : 8;

    if (a->allocation_info == 0 || a->alloc_count == 0)
        return;
    for (uint32 i = 0; i < n; i++) {
        dprintf(2,
                "dxgtrace: %s alloc[%u] handle=0x%x sysmem=0x%lx priv=%u flags=0x%x gpuva=0x%lx pri=0x%lx reserved=%lx,%lx,%lx,%lx,%lx\n",
                phase, i, info[i].allocation.v, info[i].sysmem,
                info[i].priv_drv_data_size, info[i].flags.value,
                info[i].gpu_virtual_address, info[i].unused,
                info[i].reserved[0], info[i].reserved[1],
                info[i].reserved[2], info[i].reserved[3],
                info[i].reserved[4]);
        dump_head("create_allocation_alloc_priv",
                  info[i].priv_drv_data, info[i].priv_drv_data_size);
        dump_full_hex_alloc("create_allocation_alloc_priv", phase, i,
                            info[i].priv_drv_data,
                            info[i].priv_drv_data_size);
    }
    if (a->alloc_count > n)
        dprintf(2, "dxgtrace: %s alloc[...] total=%u\n", phase,
                a->alloc_count);
}

static uint32_t bits32(const void *ptr)
{
    uint32_t value;

    memcpy(&value, ptr, sizeof(value));
    return value;
}

__attribute__((constructor))
static void log_trace_environment(void)
{
    const char *adapter = getenv("MESA_D3D12_DEFAULT_ADAPTER_NAME");

    if (adapter != NULL && adapter[0] != 0)
        dprintf(2, "dxgtrace: trace_env default_adapter=%s\n", adapter);
}

static void log_before(unsigned long request, void *arg)
{
    unsigned long nr = req_nr(request);

    if (req_type(request) != 0x47 || arg == NULL)
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
                a->adapter.v, bits32(&a->flags));
        break;
    }
    case 0x04: {
        struct d3dkmt_createcontextvirtual *a = arg;
        dprintf(2,
                "dxgtrace: create_context device=0x%x node=%u engine=%u flags=0x%x hint=%u priv=%u\n",
                a->device.v, a->node_ordinal, a->engine_affinity,
                a->flags.value, a->client_hint, a->priv_drv_data_size);
        dump_head("create_context_priv", a->priv_drv_data,
                  a->priv_drv_data_size);
        break;
    }
    case 0x06: {
        struct d3dkmt_createallocation *a = arg;
        dprintf(2,
                "dxgtrace: create_allocation device=0x%x resource=0x%x alloc_count=%u runtime=%u priv=%u flags=0x%x rt_resource=0x%lx\n",
                a->device.v, a->resource.v, a->alloc_count,
                a->private_runtime_data_size, a->priv_drv_data_size,
                a->flags.value, a->private_runtime_resource_handle);
        dump_head("create_allocation_runtime", a->private_runtime_data,
                  a->private_runtime_data_size);
        dump_full_hex("create_allocation_runtime", a->private_runtime_data,
                      a->private_runtime_data_size);
        dump_head("create_allocation_priv", a->priv_drv_data,
                  a->priv_drv_data_size);
        dump_full_hex("create_allocation_priv", a->priv_drv_data,
                      a->priv_drv_data_size);
        dump_allocation_info("create_allocation_in", a);
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
        if (a->type == _KMTQAITYPE_UMDRIVERPRIVATE)
            dump_query_adapter_type0_snapshot(a, "query_adapter_type0_in",
                                              0, 0);
        break;
    }
    case 0x0b: {
        struct d3dddi_makeresident *a = arg;
        dprintf(2,
                "dxgtrace: make_resident paging=0x%x count=%u flags=0x%x\n",
                a->paging_queue.v, a->alloc_count, a->flags.value);
        dump_u32_array("make_resident_allocations",
                       a->allocation_list, a->alloc_count);
        break;
    }
    case 0x0c: {
        struct d3dddi_mapgpuvirtualaddress *a = arg;
        dprintf(2,
                "dxgtrace: map_gpu_va paging=0x%x alloc=0x%x base=0x%lx min=0x%lx max=0x%lx offset=0x%lx size_pages=0x%lx prot=0x%lx dprot=0x%lx\n",
                a->paging_queue.v, a->allocation.v, a->base_address,
                a->minimum_address, a->maximum_address, a->offset_in_pages,
                a->size_in_pages, a->protection.value,
                a->driver_protection);
        break;
    }
    case 0x0d: {
        struct d3dkmt_escape *a = arg;
        dprintf(2,
                "dxgtrace: escape adapter=0x%x device=0x%x context=0x%x type=%u priv=%u flags=0x%x\n",
                a->adapter.v, a->device.v, a->context.v, a->type,
                a->priv_drv_data_size, a->flags.value);
        dump_head("escape_priv", a->priv_drv_data, a->priv_drv_data_size);
        break;
    }
    case 0x18: {
        struct d3dkmt_createhwqueue *a = arg;
        dprintf(2,
                "dxgtrace: create_hwqueue context=0x%x flags=0x%x priv=%u\n",
                a->context.v, a->flags.value, a->priv_drv_data_size);
        dump_head("create_hwqueue_priv", a->priv_drv_data,
                  a->priv_drv_data_size);
        break;
    }
    case 0x10: {
        struct d3dkmt_createsynchronizationobject2 *a = arg;
        dprintf(2,
                "dxgtrace: create_sync device=0x%x type=%u flags=0x%x shared=0x%x cpu_event=0x%lx\n",
                a->device.v, a->info.type, a->info.flags.value,
                a->info.shared_handle.v, a->info.cpu_notification.event);
        break;
    }
    case 0x0f: {
        struct d3dkmt_submitcommand *a = arg;
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
        dump_u32_array("submit_command_histories", a->history_buffer_array,
                       a->num_history_buffers);
        dump_head("submit_command_priv", a->priv_drv_data,
                  a->priv_drv_data_size);
        break;
    }
    case 0x33: {
        struct d3dkmt_signalsynchronizationobjectfromgpu2 *a = arg;
        dprintf(2,
                "dxgtrace: signal_gpu2 objects=%u flags=0x%x contexts=%u fence=0x%lx\n",
                a->object_count, a->flags.value, a->context_count,
                a->monitored_fence_values);
        dump_u32_array("signal_gpu2_objects", a->objects, a->object_count);
        dump_u32_array("signal_gpu2_contexts", a->contexts, a->context_count);
        if (!a->flags.enqueue_cpu_event)
            dump_u64_array("signal_gpu2_fences",
                           a->monitored_fence_values, a->object_count);
        break;
    }
    case 0x25: {
        struct d3dkmt_lock2 *a = arg;
        dprintf(2,
                "dxgtrace: lock2 device=0x%x alloc=0x%x flags=0x%x data=0x%lx\n",
                a->device.v, a->allocation.v, a->flags.value,
                a->data);
        break;
    }
    case 0x37: {
        struct d3dkmt_unlock2 *a = arg;
        dprintf(2, "dxgtrace: unlock2 device=0x%x alloc=0x%x\n",
                a->device.v, a->allocation.v);
        break;
    }
    case 0x05: {
        struct d3dkmt_destroycontext *a = arg;
        dprintf(2, "dxgtrace: destroy_context context=0x%x\n",
                a->context.v);
        break;
    }
    case 0x13: {
        struct d3dkmt_destroyallocation2 *a = arg;
        dprintf(2,
                "dxgtrace: destroy_allocation device=0x%x resource=0x%x count=%u flags=0x%x\n",
                a->device.v, a->resource.v, a->alloc_count,
                a->flags.value);
        dump_u32_array("destroy_allocation_handles", a->allocations,
                       a->alloc_count);
        break;
    }
    case 0x15: {
        struct d3dkmt_closeadapter *a = arg;
        dprintf(2, "dxgtrace: close_adapter adapter=0x%x\n",
                a->adapter_handle.v);
        break;
    }
    case 0x19: {
        struct d3dkmt_destroydevice *a = arg;
        dprintf(2, "dxgtrace: destroy_device device=0x%x\n",
                a->device.v);
        break;
    }
    case 0x1b: {
        struct d3dkmt_destroyhwqueue *a = arg;
        dprintf(2, "dxgtrace: destroy_hwqueue queue=0x%x\n",
                a->queue.v);
        break;
    }
    case 0x1c: {
        struct d3dddi_destroypagingqueue *a = arg;
        dprintf(2, "dxgtrace: destroy_paging_queue queue=0x%x\n",
                a->paging_queue.v);
        break;
    }
    case 0x1d: {
        struct d3dkmt_destroysynchronizationobject *a = arg;
        dprintf(2, "dxgtrace: destroy_sync object=0x%x\n",
                a->sync_object.v);
        break;
    }
    case 0x1e: {
        struct d3dkmt_evict *a = arg;
        dprintf(2,
                "dxgtrace: evict device=0x%x count=%u flags=0x%x\n",
                a->device.v, a->alloc_count, a->flags.value);
        dump_u32_array("evict_allocations", a->allocations,
                       a->alloc_count);
        break;
    }
    case 0x1f: {
        struct d3dkmt_flushheaptransitions *a = arg;
        dprintf(2, "dxgtrace: flush_heap adapter=0x%x\n",
                a->adapter.v);
        break;
    }
    case 0x20: {
        struct d3dkmt_freegpuvirtualaddress *a = arg;
        dprintf(2,
                "dxgtrace: free_gpu_va adapter=0x%x base=0x%lx size=0x%lx\n",
                a->adapter.v, a->base_address, a->size);
        break;
    }
    case 0x24: {
        struct d3dkmt_invalidatecache *a = arg;
        dprintf(2,
                "dxgtrace: invalidate_cache device=0x%x allocation=0x%x offset=0x%lx length=0x%lx\n",
                a->device.v, a->allocation.v, a->offset, a->length);
        break;
    }
    case 0x3a: {
        struct d3dkmt_waitforsynchronizationobjectfromcpu *a = arg;
        dprintf(2,
                "dxgtrace: wait_cpu device=0x%x objects=%u async=0x%lx flags=0x%x\n",
                a->device.v, a->object_count, a->async_event,
                a->flags.value);
        dump_u32_array("wait_cpu_objects", a->objects, a->object_count);
        dump_u64_array("wait_cpu_fences", a->fence_values,
                       a->object_count);
        break;
    }
    case 0x3b: {
        struct d3dkmt_waitforsynchronizationobjectfromgpu *a = arg;
        dprintf(2, "dxgtrace: wait_gpu context=0x%x objects=%u fence=0x%lx\n",
                a->context.v, a->object_count, a->monitored_fence_values);
        dump_u32_array("wait_gpu_objects", a->objects, a->object_count);
        dump_u64_array("wait_gpu_fences", a->monitored_fence_values,
                       a->object_count);
        break;
    }
    case 0x3f: {
        struct d3dkmt_shareobjects *a = arg;
        dprintf(2,
                "dxgtrace: share_objects count=%u desired=0x%x objects=0x%lx attr=0x%lx shared=0x%lx\n",
                a->object_count, a->desired_access, a->objects,
                a->object_attr, a->shared_handle);
        dump_u32_array("share_objects_handles", a->objects,
                       a->object_count);
        dump_head("share_objects_attr", a->object_attr,
                  a->object_count * 8);
        break;
    }
    case 0x41: {
        struct d3dkmt_queryresourceinfofromnthandle *a = arg;
        dprintf(2,
                "dxgtrace: query_resource_nt device=0x%x nt=0x%lx runtime=%u total_priv=%u res_priv=%u allocs=%u\n",
                a->device.v, a->nt_handle, a->private_runtime_data_size,
                a->total_priv_drv_data_size,
                a->resource_priv_drv_data_size, a->allocation_count);
        break;
    }
    case 0x42: {
        struct d3dkmt_openresourcefromnthandle *a = arg;
        dprintf(2,
                "dxgtrace: open_resource_nt device=0x%x nt=0x%lx allocs=%u runtime=%d res_priv=%u total_priv=%u\n",
                a->device.v, a->nt_handle, a->allocation_count,
                a->private_runtime_data_size,
                a->resource_priv_drv_data_size,
                a->total_priv_drv_data_size);
        dump_head("open_resource_runtime", a->private_runtime_data,
                  a->private_runtime_data_size > 0 ?
                  (uint32)a->private_runtime_data_size : 0);
        dump_head("open_resource_priv", a->resource_priv_drv_data,
                  a->resource_priv_drv_data_size);
        dump_head("open_resource_total_priv", a->total_priv_drv_data,
                  a->total_priv_drv_data_size);
        break;
    }
    case 0x34: {
        struct d3dkmt_submitcommandtohwqueue *a = arg;
        dprintf(2,
                "dxgtrace: submit_hwqueue queue=0x%x fence=%lu cmd=0x%lx len=%u priv=%u primaries=%u\n",
                a->hwqueue.v, a->hwqueue_progress_fence_id,
                a->command_buffer, a->command_length,
                a->priv_drv_data_size, a->num_primaries);
        dump_head("submit_hwqueue_priv", a->priv_drv_data,
                  a->priv_drv_data_size);
        break;
    }
    case 0x35: {
        struct d3dkmt_submitsignalsyncobjectstohwqueue *a = arg;
        dprintf(2,
                "dxgtrace: signal_hwqueue hwqueues=%u objects=%u flags=0x%x\n",
                a->hwqueue_count, a->object_count, a->flags.value);
        break;
    }
    case 0x36: {
        struct d3dkmt_submitwaitforsyncobjectstohwqueue *a = arg;
        dprintf(2, "dxgtrace: wait_hwqueue queue=0x%x objects=%u\n",
                a->hwqueue.v, a->object_count);
        break;
    }
    default:
        break;
    }
}

static void log_after(unsigned long request, void *arg, int rc)
{
    unsigned long nr = req_nr(request);

    if (req_type(request) != 0x47 || arg == NULL)
        return;

    switch (nr) {
    case 0x09: {
        struct d3dkmt_queryadapterinfo *a = arg;
        dprintf(2,
                "dxgtrace: -> query_adapter rc=%d type=%u size=%u\n",
                rc, a->type, a->private_data_size);
        if (a->type == _KMTQAITYPE_UMDRIVERPRIVATE)
            dump_query_adapter_type0_snapshot(a, "query_adapter_type0_out",
                                              1, rc);
        if (rc == 0)
            dump_head("query_adapter_out", a->private_data,
                      a->private_data_size);
        if (rc == 0 && a->type == _KMTQAITYPE_UMDRIVERNAME)
            dump_utf16_ascii_prefix("query_adapter_umdrivername",
                                    a->private_data,
                                    a->private_data_size);
        if (rc == 0 && ((uint32_t)a->type == 31U))
            dump_adapter_hardware_id(a->private_data,
                                     a->private_data_size);
        if (rc == 0 &&
            (a->type == _KMTQAITYPE_DRIVER_DESCRIPTION ||
             a->type == _KMTQAITYPE_DRIVER_DESCRIPTION_RENDER))
            dump_utf16_ascii_text("query_adapter_description",
                                  a->private_data,
                                  a->private_data_size);
        break;
    }
    case 0x02: {
        struct d3dkmt_createdevice *a = arg;
        dprintf(2,
                "dxgtrace: -> create_device rc=%d device=0x%x cmd=0x%lx cmd_size=%u alloc=0x%lx patch=0x%lx\n",
                rc, a->device.v, a->command_buffer, a->command_buffer_size,
                a->allocation_list, a->patch_location_list);
        break;
    }
    case 0x04: {
        struct d3dkmt_createcontextvirtual *a = arg;
        dprintf(2, "dxgtrace: -> create_context rc=%d context=0x%x\n",
                rc, a->context.v);
        dump_head("create_context_priv_out", a->priv_drv_data,
                  a->priv_drv_data_size);
        break;
    }
    case 0x06: {
        struct d3dkmt_createallocation *a = arg;
        dprintf(2,
                "dxgtrace: -> create_allocation rc=%d resource=0x%x global=0x%x flags=0x%x\n",
                rc, a->resource.v, a->global_share.v, a->flags.value);
        dump_allocation_info("create_allocation_out", a);
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
        dprintf(2,
                "dxgtrace: -> reserve_gpu_va rc=%d va=0x%lx fence=%lu\n",
                rc, a->virtual_address, a->paging_fence_value);
        break;
    }
    case 0x0b: {
        struct d3dddi_makeresident *a = arg;
        dprintf(2,
                "dxgtrace: -> make_resident rc=%d fence=%lu trim=%lu\n",
                rc, a->paging_fence_value, a->num_bytes_to_trim);
        break;
    }
    case 0x0c: {
        struct d3dddi_mapgpuvirtualaddress *a = arg;
        dprintf(2,
                "dxgtrace: -> map_gpu_va rc=%d size_pages=0x%lx va=0x%lx fence=%lu\n",
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
        dump_head("create_hwqueue_priv_out", a->priv_drv_data,
                  a->priv_drv_data_size);
        break;
    }
    case 0x10: {
        struct d3dkmt_createsynchronizationobject2 *a = arg;
        dprintf(2,
                "dxgtrace: -> create_sync rc=%d object=0x%x shared=0x%x fence_cpu=0x%lx fence_gpu=0x%lx\n",
                rc, a->sync_object.v, a->info.shared_handle.v,
                a->info.monitored_fence.fence_cpu_virtual_address,
                a->info.monitored_fence.fence_gpu_virtual_address);
        break;
    }
    case 0x0f:
        dprintf(2, "dxgtrace: -> submit_command rc=%d\n", rc);
        break;
    case 0x33:
        dprintf(2, "dxgtrace: -> signal_gpu2 rc=%d\n", rc);
        break;
    case 0x25: {
        struct d3dkmt_lock2 *a = arg;
        dprintf(2, "dxgtrace: -> lock2 rc=%d data=0x%lx\n", rc,
                a->data);
        dump_head("lock2_data", a->data, 64);
        break;
    }
    case 0x37: {
        struct d3dkmt_unlock2 *a = arg;
        dprintf(2, "dxgtrace: -> unlock2 rc=%d device=0x%x alloc=0x%x\n",
                rc, a->device.v, a->allocation.v);
        break;
    }
    case 0x05: {
        struct d3dkmt_destroycontext *a = arg;
        dprintf(2, "dxgtrace: -> destroy_context rc=%d context=0x%x\n",
                rc, a->context.v);
        break;
    }
    case 0x13: {
        struct d3dkmt_destroyallocation2 *a = arg;
        dprintf(2,
                "dxgtrace: -> destroy_allocation rc=%d device=0x%x resource=0x%x count=%u\n",
                rc, a->device.v, a->resource.v, a->alloc_count);
        break;
    }
    case 0x15: {
        struct d3dkmt_closeadapter *a = arg;
        dprintf(2, "dxgtrace: -> close_adapter rc=%d adapter=0x%x\n",
                rc, a->adapter_handle.v);
        break;
    }
    case 0x19: {
        struct d3dkmt_destroydevice *a = arg;
        dprintf(2, "dxgtrace: -> destroy_device rc=%d device=0x%x\n",
                rc, a->device.v);
        break;
    }
    case 0x1b: {
        struct d3dkmt_destroyhwqueue *a = arg;
        dprintf(2, "dxgtrace: -> destroy_hwqueue rc=%d queue=0x%x\n",
                rc, a->queue.v);
        break;
    }
    case 0x1c: {
        struct d3dddi_destroypagingqueue *a = arg;
        dprintf(2,
                "dxgtrace: -> destroy_paging_queue rc=%d queue=0x%x\n",
                rc, a->paging_queue.v);
        break;
    }
    case 0x1d: {
        struct d3dkmt_destroysynchronizationobject *a = arg;
        dprintf(2, "dxgtrace: -> destroy_sync rc=%d object=0x%x\n",
                rc, a->sync_object.v);
        break;
    }
    case 0x1e: {
        struct d3dkmt_evict *a = arg;
        dprintf(2, "dxgtrace: -> evict rc=%d trim=%lu\n",
                rc, a->num_bytes_to_trim);
        break;
    }
    case 0x1f: {
        struct d3dkmt_flushheaptransitions *a = arg;
        dprintf(2, "dxgtrace: -> flush_heap rc=%d adapter=0x%x\n",
                rc, a->adapter.v);
        break;
    }
    case 0x20: {
        struct d3dkmt_freegpuvirtualaddress *a = arg;
        dprintf(2,
                "dxgtrace: -> free_gpu_va rc=%d adapter=0x%x base=0x%lx size=0x%lx\n",
                rc, a->adapter.v, a->base_address, a->size);
        break;
    }
    case 0x24: {
        struct d3dkmt_invalidatecache *a = arg;
        dprintf(2,
                "dxgtrace: -> invalidate_cache rc=%d device=0x%x allocation=0x%x\n",
                rc, a->device.v, a->allocation.v);
        break;
    }
    case 0x34:
        dprintf(2, "dxgtrace: -> submit_hwqueue rc=%d\n", rc);
        break;
    case 0x35:
        dprintf(2, "dxgtrace: -> signal_hwqueue rc=%d\n", rc);
        break;
    case 0x36:
        dprintf(2, "dxgtrace: -> wait_hwqueue rc=%d\n", rc);
        break;
    case 0x3a:
        dprintf(2, "dxgtrace: -> wait_cpu rc=%d\n", rc);
        break;
    case 0x3b:
        dprintf(2, "dxgtrace: -> wait_gpu rc=%d\n", rc);
        break;
    case 0x3f: {
        struct d3dkmt_shareobjects *a = arg;
        uint64_t out = 0;

        if (a->shared_handle != 0)
            memcpy(&out, (const void *)(uintptr_t)a->shared_handle,
                   sizeof(out));
        dprintf(2, "dxgtrace: -> share_objects rc=%d shared=0x%lx\n",
                rc, out);
        break;
    }
    case 0x41: {
        struct d3dkmt_queryresourceinfofromnthandle *a = arg;
        dprintf(2,
                "dxgtrace: -> query_resource_nt rc=%d runtime=%u total_priv=%u res_priv=%u allocs=%u\n",
                rc, a->private_runtime_data_size,
                a->total_priv_drv_data_size,
                a->resource_priv_drv_data_size, a->allocation_count);
        break;
    }
    case 0x42: {
        struct d3dkmt_openresourcefromnthandle *a = arg;
        dprintf(2,
                "dxgtrace: -> open_resource_nt rc=%d resource=0x%x keyed=0x%x sync=0x%x\n",
                rc, a->resource.v, a->keyed_mutex.v,
                a->sync_object.v);
        break;
    }
    default:
        break;
    }
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    int rc;

    if (real_ioctl_fn == NULL)
        real_ioctl_fn = dlsym(RTLD_NEXT, "ioctl");

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (req_type(request) == 0x47)
        dprintf(2, "dxgtrace: ioctl nr=0x%lx size=0x%lx\n",
                req_nr(request), req_size(request));
    log_before(request, arg);
    rc = real_ioctl_fn(fd, request, arg);
    log_after(request, arg, rc);
    return rc;
}
