#define ON_HOST_OS 1

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "kernel/kernel/inc/uabi/d3dkmthk.h"

static int do_ioctl(int fd, unsigned long req, void *arg, const char *name)
{
    int rc = ioctl(fd, req, arg);

    if (rc < 0)
        printf("%s failed rc=%d errno=%d (%s)\n", name, rc, errno,
               strerror(errno));
    return rc;
}

static void dump_bytes(const char *name, const unsigned char *buf,
                       unsigned int len)
{
    unsigned int n = len < 32 ? len : 32;

    printf("%s len=%u head=", name, len);
    for (unsigned int i = 0; i < n; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

static void query_raw(int fd, struct d3dkmthandle adapter, unsigned int type,
                      unsigned int size)
{
    unsigned char data[8192];
    struct d3dkmt_queryadapterinfo q;

    if (size > sizeof(data))
        size = sizeof(data);
    memset(data, 0, sizeof(data));
    memset(&q, 0, sizeof(q));
    q.adapter = adapter;
    q.type = (enum kmtqueryadapterinfotype)type;
    q.private_data = (uint64)data;
    q.private_data_size = size;
    if (do_ioctl(fd, LX_DXQUERYADAPTERINFO, &q, "query_adapter_info") == 0) {
        char label[64];

        snprintf(label, sizeof(label), "adapter_raw type=%u out_size=%u", type,
                 q.private_data_size);
        dump_bytes(label, data, size);
    } else {
        printf("adapter_raw type=%u requested=%u out_size=%u\n", type, size,
               q.private_data_size);
    }
}

static void query_adapter_type(int fd, struct d3dkmthandle adapter,
                               enum kmtqueryadapterinfotype type,
                               const char *name)
{
    struct d3dkmt_adaptertype adapter_type;
    struct d3dkmt_queryadapterinfo q;

    memset(&adapter_type, 0, sizeof(adapter_type));
    memset(&q, 0, sizeof(q));
    q.adapter = adapter;
    q.type = type;
    q.private_data = (uint64)&adapter_type;
    q.private_data_size = sizeof(adapter_type);
    if (do_ioctl(fd, LX_DXQUERYADAPTERINFO, &q, name) == 0) {
        printf("%s value=0x%x render=%u display=%u software=%u pv=%u compute=%u out_size=%u\n",
               name, adapter_type.value, adapter_type.render_supported,
               adapter_type.display_supported, adapter_type.software_device,
               adapter_type.paravirtualized, adapter_type.compute_only,
               q.private_data_size);
    }
}

static void query_vidmem(int fd, struct d3dkmthandle adapter,
                         enum d3dkmt_memory_segment_group group,
                         const char *name)
{
    struct d3dkmt_queryvideomemoryinfo info;

    memset(&info, 0, sizeof(info));
    info.adapter = adapter;
    info.memory_segment_group = group;
    if (do_ioctl(fd, LX_DXQUERYVIDEOMEMORYINFO, &info, name) == 0) {
        printf("%s budget=%lu usage=%lu reservation=%lu available=%lu phys=%u\n",
               name, info.budget, info.current_usage,
               info.current_reservation, info.available_for_reservation,
               info.physical_adapter_index);
    }
}

static unsigned int query_umdriver_private(int fd, struct d3dkmthandle adapter,
                                           unsigned char *out,
                                           unsigned int out_size)
{
    unsigned int sizes[] = { 64, 256, 1024, 4096 };
    unsigned int best = 0;

    for (unsigned int i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        struct d3dkmt_queryadapterinfo q;
        unsigned int size = sizes[i] < out_size ? sizes[i] : out_size;

        memset(out, 0, out_size);
        memset(&q, 0, sizeof(q));
        q.adapter = adapter;
        q.type = _KMTQAITYPE_UMDRIVERPRIVATE;
        q.private_data = (uint64)out;
        q.private_data_size = size;
        if (do_ioctl(fd, LX_DXQUERYADAPTERINFO, &q, "umdriver_private") == 0) {
            dump_bytes("umdriver_private", out, size);
            best = size;
        } else {
            printf("umdriver_private requested=%u out_size=%u\n", size,
                   q.private_data_size);
        }
    }
    return best;
}

static void destroy_context(int fd, struct d3dkmthandle context)
{
    struct d3dkmt_destroycontext d;

    if (context.v == 0)
        return;
    memset(&d, 0, sizeof(d));
    d.context = context;
    (void)do_ioctl(fd, LX_DXDESTROYCONTEXT, &d, "destroy_context");
}

static int try_context(int fd, struct d3dkmthandle device, const char *name,
                       unsigned int node, unsigned int engine,
                       enum d3dkmt_clienthint hint,
                       struct d3dddi_createcontextflags flags, void *priv,
                       unsigned int priv_size,
                       struct d3dkmthandle *selected)
{
    struct d3dkmt_createcontextvirtual c;

    memset(&c, 0, sizeof(c));
    c.device = device;
    c.node_ordinal = node;
    c.engine_affinity = engine;
    c.client_hint = hint;
    c.flags = flags;
    c.priv_drv_data = (uint64)priv;
    c.priv_drv_data_size = priv_size;
    if (do_ioctl(fd, LX_DXCREATECONTEXTVIRTUAL, &c, name) == 0 &&
        c.context.v != 0) {
        printf("context_ok %s context=0x%x node=%u engine=%u hint=%u flags=0x%x priv=%u\n",
               name, c.context.v, node, engine, hint, flags.value, priv_size);
        if (selected != NULL && selected->v == 0)
            *selected = c.context;
        else
            destroy_context(fd, c.context);
        return 0;
    }
    printf("context_fail %s context=0x%x node=%u engine=%u hint=%u flags=0x%x priv=%u\n",
           name, c.context.v, node, engine, hint, flags.value, priv_size);
    return -1;
}

static void probe_contexts(int fd, struct d3dkmthandle device,
                           unsigned char *umpriv, unsigned int umpriv_size)
{
    static unsigned char mesa_like_private[] = {
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x1c, 0x0c, 0x00, 0x00,
    };
    struct d3dddi_createcontextflags flags;
    struct d3dkmthandle selected;

    memset(&selected, 0, sizeof(selected));
    memset(&flags, 0, sizeof(flags));
    try_context(fd, device, "dx12_empty_e0", 0, 0, _D3DKMT_CLIENTHINT_DX12,
                flags, NULL, 0, &selected);
    try_context(fd, device, "dx12_empty_e1", 0, 1, _D3DKMT_CLIENTHINT_DX12,
                flags, NULL, 0, &selected);
    try_context(fd, device, "opengl_empty_e0", 0, 0,
                _D3DKMT_CLIENTHINT_OPENGL, flags, NULL, 0, &selected);
    try_context(fd, device, "dx12_mesa_private_e0", 0, 0,
                _D3DKMT_CLIENTHINT_DX12, flags, mesa_like_private,
                sizeof(mesa_like_private), &selected);
    try_context(fd, device, "dx12_mesa_private_e1", 0, 1,
                _D3DKMT_CLIENTHINT_DX12, flags, mesa_like_private,
                sizeof(mesa_like_private), &selected);
    if (umpriv_size != 0) {
        try_context(fd, device, "dx12_umprivate_e0", 0, 0,
                    _D3DKMT_CLIENTHINT_DX12, flags, umpriv, umpriv_size,
                    &selected);
        try_context(fd, device, "dx12_umprivate_e1", 0, 1,
                    _D3DKMT_CLIENTHINT_DX12, flags, umpriv, umpriv_size,
                    &selected);
    }
    flags.hw_queue_supported = 1;
    try_context(fd, device, "dx12_hwqueue_empty_e0", 0, 0,
                _D3DKMT_CLIENTHINT_DX12, flags, NULL, 0, &selected);
    try_context(fd, device, "dx12_hwqueue_private_e0", 0, 0,
                _D3DKMT_CLIENTHINT_DX12, flags, mesa_like_private,
                sizeof(mesa_like_private), &selected);
    memset(&flags, 0, sizeof(flags));
    flags.synchronization_only = 1;
    try_context(fd, device, "sync_empty", 0, 0, _D3DKMT_CLIENTHNT_UNKNOWN,
                flags, NULL, 0, &selected);

    if (selected.v != 0) {
        struct d3dkmt_submitcommand submit;
        struct d3dkmt_createhwqueue hwq;
        struct d3dkmt_destroyhwqueue destroy_hwq;

        memset(&submit, 0, sizeof(submit));
        submit.broadcast_context_count = 1;
        submit.broadcast_context[0] = selected;
        printf("submit_against_selected context=0x%x\n", selected.v);
        if (do_ioctl(fd, LX_DXSUBMITCOMMAND, &submit, "submit_empty") == 0)
            printf("submit_empty ok context=0x%x\n", selected.v);

        memset(&hwq, 0, sizeof(hwq));
        hwq.context = selected;
        if (do_ioctl(fd, LX_DXCREATEHWQUEUE, &hwq,
                     "create_hwqueue_empty") == 0 &&
            hwq.queue.v != 0) {
            struct d3dkmt_submitcommandtohwqueue submit_hwq;

            printf("hwqueue_ok queue=0x%x fence=0x%x fence_cpu=0x%lx fence_gpu=0x%lx\n",
                   hwq.queue.v, hwq.queue_progress_fence.v,
                   hwq.queue_progress_fence_cpu_va,
                   hwq.queue_progress_fence_gpu_va);
            memset(&submit_hwq, 0, sizeof(submit_hwq));
            submit_hwq.hwqueue = hwq.queue;
            submit_hwq.hwqueue_progress_fence_id = 1;
            if (do_ioctl(fd, LX_DXSUBMITCOMMANDTOHWQUEUE, &submit_hwq,
                         "submit_hwqueue_empty") == 0)
                printf("submit_hwqueue_empty ok queue=0x%x\n", hwq.queue.v);

            memset(&destroy_hwq, 0, sizeof(destroy_hwq));
            destroy_hwq.queue = hwq.queue;
            (void)do_ioctl(fd, LX_DXDESTROYHWQUEUE, &destroy_hwq,
                           "destroy_hwqueue");
        } else {
            printf("hwqueue_fail queue=0x%x fence=0x%x\n", hwq.queue.v,
                   hwq.queue_progress_fence.v);
        }
        destroy_context(fd, selected);
    }
}

static void probe_device(int fd, struct d3dkmthandle adapter,
                         unsigned char *umpriv, unsigned int umpriv_size)
{
    struct d3dkmt_createdevice create;
    struct d3dkmt_createpagingqueue paging;
    struct d3dddi_destroypagingqueue destroy_paging;
    struct d3dkmt_destroydevice destroy;

    memset(&create, 0, sizeof(create));
    create.adapter = adapter;
    if (do_ioctl(fd, LX_DXCREATEDEVICE, &create, "create_device") < 0 ||
        create.device.v == 0) {
        printf("device_fail handle=0x%x\n", create.device.v);
        return;
    }
    printf("device_ok handle=0x%x command_buffer=0x%lx command_size=%u alloc_list=0x%lx patch_list=0x%lx\n",
           create.device.v, create.command_buffer, create.command_buffer_size,
           create.allocation_list, create.patch_location_list);

    memset(&paging, 0, sizeof(paging));
    paging.device = create.device;
    if (do_ioctl(fd, LX_DXCREATEPAGINGQUEUE, &paging, "create_paging_queue") ==
        0) {
        printf("paging_queue_ok queue=0x%x sync=0x%x fence_cpu=0x%lx\n",
               paging.paging_queue.v, paging.sync_object.v,
               paging.fence_cpu_virtual_address);
        memset(&destroy_paging, 0, sizeof(destroy_paging));
        destroy_paging.paging_queue = paging.paging_queue;
        (void)do_ioctl(fd, LX_DXDESTROYPAGINGQUEUE, &destroy_paging,
                       "destroy_paging_queue");
    }

    probe_contexts(fd, create.device, umpriv, umpriv_size);

    memset(&destroy, 0, sizeof(destroy));
    destroy.device = create.device;
    (void)do_ioctl(fd, LX_DXDESTROYDEVICE, &destroy, "destroy_device");
}

int main(void)
{
    struct d3dkmt_adapterinfo adapters[D3DKMT_ADAPTERS_MAX];
    struct d3dkmt_enumadapters3 enum3;
    int fd = open("/dev/dxg", O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        perror("open /dev/dxg");
        return 1;
    }

    memset(adapters, 0, sizeof(adapters));
    memset(&enum3, 0, sizeof(enum3));
    enum3.filter.include_compute_only = 1;
    enum3.filter.include_display_only = 1;
    enum3.adapter_count = D3DKMT_ADAPTERS_MAX;
    enum3.adapters = (uint64)adapters;
    if (do_ioctl(fd, LX_DXENUMADAPTERS3, &enum3, "enum_adapters3") < 0) {
        struct d3dkmt_enumadapters2 enum2;

        memset(&enum2, 0, sizeof(enum2));
        enum2.num_adapters = D3DKMT_ADAPTERS_MAX;
        enum2.adapters = (uint64)adapters;
        if (do_ioctl(fd, LX_DXENUMADAPTERS2, &enum2, "enum_adapters2") < 0)
            return 1;
        enum3.adapter_count = enum2.num_adapters;
    }

    printf("adapter_count=%u\n", enum3.adapter_count);
    for (unsigned int i = 0; i < enum3.adapter_count; i++) {
        struct d3dkmt_openadapterfromluid open_luid;
        struct d3dkmt_closeadapter close_adapter;
        unsigned char umpriv[4096];
        unsigned int umpriv_size;

        printf("adapter[%u] enum_handle=0x%x luid=%x:%x sources=%u\n", i,
               adapters[i].adapter_handle.v, adapters[i].adapter_luid.b,
               adapters[i].adapter_luid.a, adapters[i].num_sources);
        memset(&open_luid, 0, sizeof(open_luid));
        open_luid.adapter_luid = adapters[i].adapter_luid;
        if (do_ioctl(fd, LX_DXOPENADAPTERFROMLUID, &open_luid,
                     "open_adapter_from_luid") < 0)
            continue;
        printf("adapter[%u] open_handle=0x%x\n", i,
               open_luid.adapter_handle.v);

        query_adapter_type(fd, open_luid.adapter_handle,
                           _KMTQAITYPE_ADAPTERTYPE, "adapter_type");
        query_adapter_type(fd, open_luid.adapter_handle,
                           _KMTQAITYPE_ADAPTERTYPE_RENDER,
                           "adapter_type_render");
        query_vidmem(fd, open_luid.adapter_handle,
                     _D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL, "vidmem_local");
        query_vidmem(fd, open_luid.adapter_handle,
                     _D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                     "vidmem_nonlocal");
        umpriv_size =
            query_umdriver_private(fd, open_luid.adapter_handle, umpriv,
                                   sizeof(umpriv));
        query_raw(fd, open_luid.adapter_handle, 1, 524);
        query_raw(fd, open_luid.adapter_handle, 3, 24);
        query_raw(fd, open_luid.adapter_handle, 17, 12);
        query_raw(fd, open_luid.adapter_handle, 30, 4);
        query_raw(fd, open_luid.adapter_handle, 56, 4);
        query_raw(fd, open_luid.adapter_handle, 60, 80);
        query_raw(fd, open_luid.adapter_handle, 61, 56);
        query_raw(fd, open_luid.adapter_handle, 62, 64);
        query_raw(fd, open_luid.adapter_handle, 65, 8192);
        query_raw(fd, open_luid.adapter_handle, 66, 8192);
        probe_device(fd, open_luid.adapter_handle, umpriv, umpriv_size);

        memset(&close_adapter, 0, sizeof(close_adapter));
        close_adapter.adapter_handle = open_luid.adapter_handle;
        (void)do_ioctl(fd, LX_DXCLOSEADAPTER, &close_adapter,
                       "close_adapter");
    }

    close(fd);
    return 0;
}
