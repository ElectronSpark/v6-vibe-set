#include <efi.h>
#include <efilib.h>

#define SETUP_SECTS         0x1f1
#define HDR_VERSION         0x206
#define CODE32_START        0x214
#define RAMDISK_IMAGE       0x218
#define RAMDISK_SIZE        0x21c
#define CMDLINE_PTR         0x228
#define EXT_RAMDISK_IMAGE   0x0c0
#define EXT_RAMDISK_SIZE    0x0c4
#define EXT_CMDLINE_PTR     0x0c8
#define E820_COUNT          0x1e8
#define E820_TABLE          0x2d0
#define E820_MAX            128

#define VIDEO_TYPE_EFI      0x70
#define VIDEO_CAP_64BIT_BASE 0x2
#define CMDLINE_MAX         1024

#ifndef XV6_HYPERV_CMDLINE
#define XV6_HYPERV_CMDLINE \
    "BOOT_IMAGE=/xv6.bin root=/dev/disk0p2 netsurf=0 webkit=0 glsmoke=0"
#endif

static EFI_HANDLE g_image;
static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

static EFI_GUID acpi20_table_guid =
    {0x8868e871, 0xe4f1, 0x11d3,
     {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
static EFI_GUID acpi10_table_guid =
    {0xeb9d2d30, 0x2d88, 0x11d3,
     {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};

static void *mem_copy(void *dst, const void *src, UINTN n)
{
    UINT8 *d = dst;
    const UINT8 *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static void *mem_set(void *dst, int c, UINTN n)
{
    UINT8 *d = dst;
    while (n--)
        *d++ = (UINT8)c;
    return dst;
}

static int mem_equal(const void *a, const void *b, UINTN n)
{
    const UINT8 *pa = a;
    const UINT8 *pb = b;
    for (UINTN i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return 0;
    }
    return 1;
}

static UINTN str_len8(const CHAR8 *s)
{
    UINTN n = 0;
    while (s[n])
        n++;
    return n;
}

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b)
{
    const UINT8 *pa = (const UINT8 *)a;
    const UINT8 *pb = (const UINT8 *)b;
    for (UINTN i = 0; i < sizeof(EFI_GUID); i++) {
        if (pa[i] != pb[i])
            return 0;
    }
    return 1;
}

static void append_str8(CHAR8 *dst, UINTN dst_size, const char *src)
{
    UINTN len = str_len8(dst);
    UINTN i = 0;

    if (dst_size == 0 || len >= dst_size - 1)
        return;
    while (src[i] && len + 1 < dst_size)
        dst[len++] = src[i++];
    dst[len] = '\0';
}

static void append_hex64(CHAR8 *dst, UINTN dst_size, UINT64 value)
{
    static const CHAR8 hex[] = "0123456789abcdef";
    int started = 0;

    append_str8(dst, dst_size, "0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        UINT8 digit = (UINT8)((value >> shift) & 0xf);
        if (digit || started || shift == 0) {
            char ch[2] = { (char)hex[digit], '\0' };
            append_str8(dst, dst_size, ch);
            started = 1;
        }
    }
}

static void append_dec(CHAR8 *dst, UINTN dst_size, UINT32 value)
{
    char buf[11];
    UINTN i = sizeof(buf);

    buf[--i] = '\0';
    do {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && i > 0);
    append_str8(dst, dst_size, &buf[i]);
}

static UINT64 find_acpi_rsdp(void)
{
    EFI_CONFIGURATION_TABLE *ct = g_st->ConfigurationTable;
    UINT64 acpi10 = 0;

    for (UINTN i = 0; i < g_st->NumberOfTableEntries; i++) {
        if (guid_equal(&ct[i].VendorGuid, &acpi20_table_guid))
            return (UINT64)(UINTN)ct[i].VendorTable;
        if (guid_equal(&ct[i].VendorGuid, &acpi10_table_guid))
            acpi10 = (UINT64)(UINTN)ct[i].VendorTable;
    }
    return acpi10;
}

static UINT8 rd8(const UINT8 *p, UINTN off) { return p[off]; }

static UINT16 rd16(const UINT8 *p, UINTN off)
{
    return (UINT16)p[off] | ((UINT16)p[off + 1] << 8);
}

static UINT32 rd32(const UINT8 *p, UINTN off)
{
    return (UINT32)p[off] | ((UINT32)p[off + 1] << 8) |
           ((UINT32)p[off + 2] << 16) | ((UINT32)p[off + 3] << 24);
}

static UINT64 rd64(const UINT8 *p, UINTN off)
{
    return (UINT64)rd32(p, off) | ((UINT64)rd32(p, off + 4) << 32);
}

static void wr8(UINT8 *p, UINTN off, UINT8 v) { p[off] = v; }

static void wr16(UINT8 *p, UINTN off, UINT16 v)
{
    p[off] = v & 0xff;
    p[off + 1] = v >> 8;
}

static void wr32(UINT8 *p, UINTN off, UINT32 v)
{
    p[off] = v & 0xff;
    p[off + 1] = (v >> 8) & 0xff;
    p[off + 2] = (v >> 16) & 0xff;
    p[off + 3] = (v >> 24) & 0xff;
}

static void wr64(UINT8 *p, UINTN off, UINT64 v)
{
    wr32(p, off, (UINT32)v);
    wr32(p, off + 4, (UINT32)(v >> 32));
}

static int acpi_sig_eq(const UINT8 *p, const char *sig)
{
    return p[0] == (UINT8)sig[0] && p[1] == (UINT8)sig[1] &&
           p[2] == (UINT8)sig[2] && p[3] == (UINT8)sig[3];
}

static int acpi_checksum_ok(const UINT8 *p, UINT32 len)
{
    UINT8 sum = 0;

    if (p == NULL || len == 0)
        return 0;
    for (UINT32 i = 0; i < len; i++)
        sum = (UINT8)(sum + p[i]);
    return sum == 0;
}

static UINT32 count_madt_cpus(const UINT8 *madt)
{
    UINT32 len;
    UINT32 off = 44;
    UINT32 count = 0;

    if (madt == NULL || !acpi_sig_eq(madt, "APIC"))
        return 0;
    len = rd32(madt, 4);
    if (len < off || !acpi_checksum_ok(madt, len))
        return 0;

    while (off + 2 <= len) {
        UINT8 type = madt[off];
        UINT8 elen = madt[off + 1];

        if (elen < 2 || off + elen > len)
            break;
        if (type == 0 && elen >= 8) {
            UINT32 flags = rd32(madt, off + 4);
            if (flags & 0x3)
                count++;
        } else if (type == 9 && elen >= 16) {
            UINT32 flags = rd32(madt, off + 12);
            if (flags & 0x3)
                count++;
        }
        off += elen;
    }

    return count;
}

static UINT32 count_acpi_cpus(UINT64 rsdp_addr)
{
    const UINT8 *rsdp = (const UINT8 *)(UINTN)rsdp_addr;
    const UINT8 *root;
    UINT32 root_len;
    UINT32 entries;
    int use_xsdt;

    if (rsdp == NULL || !mem_equal(rsdp, "RSD PTR ", 8) ||
        !acpi_checksum_ok(rsdp, 20))
        return 0;

    use_xsdt = rsdp[15] >= 2 && rd64(rsdp, 24) != 0 &&
               rd32(rsdp, 20) >= 36 &&
               acpi_checksum_ok(rsdp, rd32(rsdp, 20));
    root = (const UINT8 *)(UINTN)(use_xsdt ? rd64(rsdp, 24) :
                                  (UINT64)rd32(rsdp, 16));
    if (root == NULL)
        return 0;
    root_len = rd32(root, 4);
    if (root_len < 36 || !acpi_checksum_ok(root, root_len))
        return 0;

    if (use_xsdt) {
        if (!acpi_sig_eq(root, "XSDT"))
            return 0;
        entries = (root_len - 36) / 8;
        for (UINT32 i = 0; i < entries; i++) {
            const UINT8 *hdr = (const UINT8 *)(UINTN)rd64(root, 36 + i * 8);
            if (hdr && acpi_sig_eq(hdr, "APIC"))
                return count_madt_cpus(hdr);
        }
    } else {
        if (!acpi_sig_eq(root, "RSDT"))
            return 0;
        entries = (root_len - 36) / 4;
        for (UINT32 i = 0; i < entries; i++) {
            const UINT8 *hdr = (const UINT8 *)(UINTN)rd32(root, 36 + i * 4);
            if (hdr && acpi_sig_eq(hdr, "APIC"))
                return count_madt_cpus(hdr);
        }
    }

    return 0;
}

/*
 * Scan one ACPI table blob (DSDT/SSDT) for a QWord Address Space Descriptor
 * (large resource tag 0x8A) describing a memory producer window above 4 GiB.
 * On Hyper-V Gen2 the VMBus/PCI root _CRS exposes the high-MMIO aperture this
 * way; the same descriptors back the window the host sets via
 * -HighMemoryMappedIoSpace, from which assigned-device (DDA) BARs are
 * allocated. Returns the largest qualifying window found; values are copied
 * verbatim from the real ACPI descriptors. Mirrors the kernel scan but runs
 * here, where ACPI-reclaim memory is still valid under EFI boot services.
 */
static void scan_table_high_mmio(const UINT8 *tbl, UINT64 *best_base,
                                 UINT64 *best_size)
{
    UINT32 len;

    if (tbl == NULL)
        return;
    len = rd32(tbl, 4);
    if (len < 36 + 46 || !acpi_checksum_ok(tbl, len))
        return;

    for (UINT32 i = 36; i + 46 <= len; i++) {
        UINT16 dlen;
        UINT8 res_type;
        UINT64 addr_min;
        UINT64 addr_max;
        UINT64 addr_len;

        if (tbl[i] != 0x8A)
            continue;
        dlen = rd16(tbl, i + 1);
        if (dlen < 43 || (UINT64)i + 3 + dlen > len)
            continue;
        res_type = tbl[i + 3];
        if (res_type != 0) /* 0 == memory range */
            continue;
        addr_min = rd64(tbl, i + 14);
        addr_max = rd64(tbl, i + 22);
        addr_len = rd64(tbl, i + 38);
        if (addr_len == 0 && addr_max >= addr_min)
            addr_len = addr_max - addr_min + 1;
        if (addr_len == 0 || addr_min < 0x100000000ULL)
            continue;
        if ((addr_min & 0xFFFULL) != 0)
            continue;
        if (addr_max >= addr_min && addr_len > (addr_max - addr_min + 1))
            continue;
        if (addr_len > *best_size) {
            *best_base = addr_min;
            *best_size = addr_len;
        }
    }
}

/*
 * Walk XSDT/RSDT to find the FACP (-> DSDT) and every SSDT, scanning each for
 * the high-MMIO producer window. Returns the largest window via out params.
 */
static int find_acpi_high_mmio(UINT64 rsdp_addr, UINT64 *out_base,
                               UINT64 *out_size)
{
    const UINT8 *rsdp = (const UINT8 *)(UINTN)rsdp_addr;
    const UINT8 *root;
    UINT32 root_len;
    UINT32 entries;
    UINT64 best_base = 0;
    UINT64 best_size = 0;
    int use_xsdt;

    *out_base = 0;
    *out_size = 0;
    if (rsdp == NULL || !mem_equal(rsdp, "RSD PTR ", 8) ||
        !acpi_checksum_ok(rsdp, 20))
        return 0;

    use_xsdt = rsdp[15] >= 2 && rd64(rsdp, 24) != 0 &&
               rd32(rsdp, 20) >= 36 &&
               acpi_checksum_ok(rsdp, rd32(rsdp, 20));
    root = (const UINT8 *)(UINTN)(use_xsdt ? rd64(rsdp, 24) :
                                  (UINT64)rd32(rsdp, 16));
    if (root == NULL)
        return 0;
    root_len = rd32(root, 4);
    if (root_len < 36 || !acpi_checksum_ok(root, root_len))
        return 0;
    if (use_xsdt ? !acpi_sig_eq(root, "XSDT") : !acpi_sig_eq(root, "RSDT"))
        return 0;

    entries = use_xsdt ? (root_len - 36) / 8 : (root_len - 36) / 4;
    for (UINT32 i = 0; i < entries; i++) {
        const UINT8 *hdr = use_xsdt ?
            (const UINT8 *)(UINTN)rd64(root, 36 + i * 8) :
            (const UINT8 *)(UINTN)rd32(root, 36 + i * 4);

        if (hdr == NULL)
            continue;
        if (acpi_sig_eq(hdr, "SSDT")) {
            scan_table_high_mmio(hdr, &best_base, &best_size);
        } else if (acpi_sig_eq(hdr, "FACP")) {
            UINT32 facp_len = rd32(hdr, 4);
            UINT64 dsdt_addr = 0;
            const UINT8 *dsdt;

            if (facp_len >= 148)
                dsdt_addr = rd64(hdr, 140);
            if (dsdt_addr == 0 && facp_len >= 44)
                dsdt_addr = (UINT64)rd32(hdr, 40);
            dsdt = (const UINT8 *)(UINTN)dsdt_addr;
            if (dsdt != NULL && acpi_sig_eq(dsdt, "DSDT"))
                scan_table_high_mmio(dsdt, &best_base, &best_size);
        }
    }

    if (best_size == 0)
        return 0;
    *out_base = best_base;
    *out_size = best_size;
    return 1;
}

/*
 * Discover the high-MMIO aperture from the live UEFI memory map. Hyper-V Gen2
 * firmware describes the assigned-device (DDA) MMIO gap as EfiMemoryMappedIO
 * descriptors; the configured -HighMemoryMappedIoSpace window appears here as
 * one or more MMIO regions above 4 GiB. This is authoritative firmware data
 * (no AML evaluation needed) and is available while boot services are live.
 * Returns the largest qualifying region. Also logs every >=4 GiB MMIO/reserved
 * region to the UEFI console for diagnostics.
 */
static int find_uefi_high_mmio(UINT64 *out_base, UINT64 *out_size)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    EFI_STATUS st;
    UINT64 best_base = 0;
    UINT64 best_size = 0;

    *out_base = 0;
    *out_size = 0;

    st = uefi_call_wrapper(g_bs->GetMemoryMap, 5, &map_size, map, &map_key,
                           &desc_size, &desc_ver);
    if (st != EFI_BUFFER_TOO_SMALL || desc_size == 0)
        return 0;
    map_size += desc_size * 16;
    st = uefi_call_wrapper(g_bs->AllocatePool, 3, EfiLoaderData, map_size,
                           (void **)&map);
    if (EFI_ERROR(st))
        return 0;
    st = uefi_call_wrapper(g_bs->GetMemoryMap, 5, &map_size, map, &map_key,
                           &desc_size, &desc_ver);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(g_bs->FreePool, 1, map);
        return 0;
    }

    UINTN count = map_size / desc_size;
    for (UINTN i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *d =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
        UINT64 start = d->PhysicalStart;
        UINT64 size = d->NumberOfPages * 4096ULL;

        if (size == 0 || start < 0x100000000ULL)
            continue;
        if (d->Type != EfiMemoryMappedIO &&
            d->Type != EfiMemoryMappedIOPortSpace &&
            d->Type != EfiReservedMemoryType)
            continue;
        Print(L"xv6 loader: UEFI MMIO region type=%d base=0x%lx size=0x%lx\r\n",
              d->Type, start, size);
        if (size > best_size) {
            best_base = start;
            best_size = size;
        }
    }
    uefi_call_wrapper(g_bs->FreePool, 1, map);

    if (best_size == 0)
        return 0;
    *out_base = best_base;
    *out_size = best_size;
    return 1;
}

static EFI_STATUS open_root(EFI_FILE_PROTOCOL **root)
{
    EFI_LOADED_IMAGE *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_STATUS st;

    st = uefi_call_wrapper(g_bs->HandleProtocol, 3, g_image,
                           &LoadedImageProtocol, (void **)&loaded);
    if (EFI_ERROR(st))
        return st;

    st = uefi_call_wrapper(g_bs->HandleProtocol, 3, loaded->DeviceHandle,
                           &FileSystemProtocol, (void **)&fs);
    if (EFI_ERROR(st))
        return st;

    return uefi_call_wrapper(fs->OpenVolume, 2, fs, root);
}

static EFI_STATUS read_file(EFI_FILE_PROTOCOL *root, CHAR16 *path, UINT8 **buf,
                            UINTN *size)
{
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN info_size = 0;
    EFI_STATUS st;

    st = uefi_call_wrapper(root->Open, 5, root, &file, path,
                           EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st))
        return st;

    st = uefi_call_wrapper(file->GetInfo, 4, file, &GenericFileInfo,
                           &info_size, NULL);
    if (st != EFI_BUFFER_TOO_SMALL) {
        uefi_call_wrapper(file->Close, 1, file);
        return st;
    }

    st = uefi_call_wrapper(g_bs->AllocatePool, 3, EfiLoaderData, info_size,
                           (void **)&info);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(file->Close, 1, file);
        return st;
    }

    st = uefi_call_wrapper(file->GetInfo, 4, file, &GenericFileInfo,
                           &info_size, info);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(g_bs->FreePool, 1, info);
        uefi_call_wrapper(file->Close, 1, file);
        return st;
    }

    *size = (UINTN)info->FileSize;
    uefi_call_wrapper(g_bs->FreePool, 1, info);

    st = uefi_call_wrapper(g_bs->AllocatePool, 3, EfiLoaderData, *size,
                           (void **)buf);
    if (EFI_ERROR(st)) {
        uefi_call_wrapper(file->Close, 1, file);
        return st;
    }

    st = uefi_call_wrapper(file->Read, 3, file, size, *buf);
    uefi_call_wrapper(file->Close, 1, file);
    return st;
}

static EFI_STATUS alloc_pages_below(UINT64 max, UINTN bytes,
                                    EFI_PHYSICAL_ADDRESS *addr)
{
    UINTN pages = EFI_SIZE_TO_PAGES(bytes);
    *addr = max;
    return uefi_call_wrapper(g_bs->AllocatePages, 4, AllocateMaxAddress,
                             EfiLoaderData, pages, addr);
}

static void fill_screen_info(UINT8 *bp)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS st;

    st = uefi_call_wrapper(g_bs->LocateProtocol, 3, &GraphicsOutputProtocol,
                           NULL, (void **)&gop);
    if (EFI_ERROR(st) || gop == NULL || gop->Mode == NULL ||
        gop->Mode->Info == NULL)
        return;

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    UINT32 width = info->HorizontalResolution;
    UINT32 height = info->VerticalResolution;
    UINT32 pitch = info->PixelsPerScanLine * 4;
    UINT64 base = gop->Mode->FrameBufferBase;
    UINT64 size = gop->Mode->FrameBufferSize;

    if (width == 0 || height == 0 || pitch < width * 4 || base == 0)
        return;
    if (size == 0 || size > 256ULL * 1024 * 1024)
        size = (UINT64)pitch * height;

    wr8(bp, 0x0f, VIDEO_TYPE_EFI);
    wr16(bp, 0x12, (UINT16)width);
    wr16(bp, 0x14, (UINT16)height);
    wr16(bp, 0x16, 32);
    wr32(bp, 0x18, (UINT32)base);
    wr32(bp, 0x1c, (UINT32)size);
    wr16(bp, 0x24, (UINT16)pitch);

    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        wr8(bp, 0x27, 0);
        wr8(bp, 0x29, 8);
        wr8(bp, 0x2b, 16);
    } else {
        wr8(bp, 0x27, 16);
        wr8(bp, 0x29, 8);
        wr8(bp, 0x2b, 0);
    }
    wr8(bp, 0x26, 8);
    wr8(bp, 0x28, 8);
    wr8(bp, 0x2a, 8);
    wr8(bp, 0x2c, 8);
    wr8(bp, 0x2d, 24);
    if (base >> 32) {
        wr32(bp, 0x36, VIDEO_CAP_64BIT_BASE);
        wr32(bp, 0x3a, (UINT32)(base >> 32));
    }

    volatile UINT32 *fb = (volatile UINT32 *)(UINTN)base;
    for (UINT32 y = 0; y < height; y++) {
        volatile UINT32 *row = (volatile UINT32 *)((UINT8 *)fb +
                                                   (UINTN)y * pitch);
        for (UINT32 x = 0; x < width; x++)
            row[x] = 0x00202060u;
    }
}

static UINT32 e820_type(UINT32 type)
{
    switch (type) {
    case EfiConventionalMemory:
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return 1;
    case EfiACPIReclaimMemory:
        return 3;
    case EfiACPIMemoryNVS:
        return 4;
    default:
        return 2;
    }
}

static EFI_STATUS fill_e820_and_get_key(UINT8 *bp, UINTN *map_key)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    EFI_STATUS st;

    st = uefi_call_wrapper(g_bs->GetMemoryMap, 5, &map_size, map, map_key,
                           &desc_size, &desc_ver);
    if (st != EFI_BUFFER_TOO_SMALL)
        return st;

    map_size += desc_size * 16;
    st = uefi_call_wrapper(g_bs->AllocatePool, 3, EfiLoaderData, map_size,
                           (void **)&map);
    if (EFI_ERROR(st))
        return st;

    st = uefi_call_wrapper(g_bs->GetMemoryMap, 5, &map_size, map, map_key,
                           &desc_size, &desc_ver);
    if (EFI_ERROR(st))
        return st;

    UINT8 *e820 = bp + E820_TABLE;
    UINTN count = map_size / desc_size;
    UINTN out = 0;
    for (UINTN i = 0; i < count && out < E820_MAX; i++) {
        EFI_MEMORY_DESCRIPTOR *d =
            (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
        UINT64 start = d->PhysicalStart;
        UINT64 size = d->NumberOfPages * 4096ULL;
        if (size == 0)
            continue;
        wr64(e820, out * 20 + 0, start);
        wr64(e820, out * 20 + 8, size);
        wr32(e820, out * 20 + 16, e820_type(d->Type));
        out++;
    }
    wr8(bp, E820_COUNT, (UINT8)out);
    return EFI_SUCCESS;
}

typedef void (EFIAPI *handover_entry_t)(EFI_HANDLE image,
                                        EFI_SYSTEM_TABLE *st,
                                        void *boot_params);

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    g_image = ImageHandle;
    g_st = SystemTable;
    g_bs = SystemTable->BootServices;

    EFI_FILE_PROTOCOL *root = NULL;
    UINT8 *kernel = NULL, *rootfs = NULL;
    UINTN kernel_size = 0, rootfs_size = 0;
    EFI_STATUS st;

    Print(L"xv6 EFI loader: opening ESP\r\n");
    st = open_root(&root);
    if (EFI_ERROR(st))
        return st;

    st = read_file(root, L"\\xv6.bin", &kernel, &kernel_size);
    if (EFI_ERROR(st))
        return st;
    st = read_file(root, L"\\rootfs.img", &rootfs, &rootfs_size);
    if (EFI_ERROR(st)) {
        rootfs = NULL;
        rootfs_size = 0;
    }

    UINTN setup_sects = rd8(kernel, SETUP_SECTS);
    if (setup_sects == 0)
        setup_sects = 4;
    UINTN setup_size = (setup_sects + 1) * 512;
    UINT32 code32 = rd32(kernel, CODE32_START);
    UINT16 version = rd16(kernel, HDR_VERSION);
    if (kernel_size <= setup_size || code32 == 0 ||
        rd32(kernel, 0x202) != 0x53726448) {
        Print(L"xv6 EFI loader: bad Linux boot image\r\n");
        return EFI_LOAD_ERROR;
    }

    EFI_PHYSICAL_ADDRESS bp_addr;
    st = alloc_pages_below(0xffffffffULL, 4096, &bp_addr);
    if (EFI_ERROR(st))
        return st;
    UINT8 *bp = (UINT8 *)(UINTN)bp_addr;
    mem_set(bp, 0, 4096);
    mem_copy(bp, kernel, setup_size < 4096 ? setup_size : 4096);

    CHAR8 cmdline[CMDLINE_MAX];
    mem_set(cmdline, 0, sizeof(cmdline));
    append_str8(cmdline, sizeof(cmdline), XV6_HYPERV_CMDLINE);
    UINT64 rsdp = find_acpi_rsdp();
    if (rsdp != 0) {
        UINT32 acpi_cpus = count_acpi_cpus(rsdp);
        UINT64 mmio_base = 0;
        UINT64 mmio_size = 0;
        append_str8(cmdline, sizeof(cmdline), " acpi_rsdp=");
        append_hex64(cmdline, sizeof(cmdline), rsdp);
        if (acpi_cpus != 0) {
            append_str8(cmdline, sizeof(cmdline), " acpi_cpus=");
            append_dec(cmdline, sizeof(cmdline), acpi_cpus);
        }
        /*
         * Discover the high-MMIO aperture for assigned (DDA) device BARs and
         * pass it on the cmdline (the kernel cannot read ACPI-reclaim memory or
         * the UEFI memory map once it boots). Prefer the live UEFI memory map
         * (authoritative firmware data); fall back to a static ACPI QWord scan.
         */
        if (find_uefi_high_mmio(&mmio_base, &mmio_size) ||
            find_acpi_high_mmio(rsdp, &mmio_base, &mmio_size)) {
            append_str8(cmdline, sizeof(cmdline), " acpi_high_mmio_base=");
            append_hex64(cmdline, sizeof(cmdline), mmio_base);
            append_str8(cmdline, sizeof(cmdline), " acpi_high_mmio_size=");
            append_hex64(cmdline, sizeof(cmdline), mmio_size);
        }
    }
    EFI_PHYSICAL_ADDRESS cmd_addr;
    st = alloc_pages_below(0xffffffffULL, str_len8(cmdline) + 1, &cmd_addr);
    if (EFI_ERROR(st))
        return st;
    mem_copy((void *)(UINTN)cmd_addr, cmdline, str_len8(cmdline) + 1);
    wr32(bp, CMDLINE_PTR, (UINT32)cmd_addr);
    if (version >= 0x0203)
        wr32(bp, EXT_CMDLINE_PTR, (UINT32)(cmd_addr >> 32));

    if (rootfs != NULL && rootfs_size != 0) {
        EFI_PHYSICAL_ADDRESS rd_addr;
        st = alloc_pages_below(0xffffffffULL, rootfs_size, &rd_addr);
        if (EFI_ERROR(st)) {
            Print(L"xv6 EFI loader: cannot allocate rootfs (%lu bytes): %r\r\n",
                  (UINT64)rootfs_size, st);
            return st;
        }
        mem_copy((void *)(UINTN)rd_addr, rootfs, rootfs_size);
        wr32(bp, RAMDISK_IMAGE, (UINT32)rd_addr);
        wr32(bp, RAMDISK_SIZE, (UINT32)rootfs_size);
        if (version >= 0x0203) {
            wr32(bp, EXT_RAMDISK_IMAGE, (UINT32)(rd_addr >> 32));
            wr32(bp, EXT_RAMDISK_SIZE, (UINT32)(rootfs_size >> 32));
        }
    }

    fill_screen_info(bp);

    UINTN map_key = 0;
    st = fill_e820_and_get_key(bp, &map_key);
    if (EFI_ERROR(st))
        return st;
    st = uefi_call_wrapper(g_bs->ExitBootServices, 2, ImageHandle, map_key);
    if (EFI_ERROR(st)) {
        st = fill_e820_and_get_key(bp, &map_key);
        if (EFI_ERROR(st))
            return st;
        st = uefi_call_wrapper(g_bs->ExitBootServices, 2, ImageHandle,
                               map_key);
        if (EFI_ERROR(st))
            return st;
    }

    mem_copy((void *)(UINTN)code32, kernel + setup_size,
             kernel_size - setup_size);

    handover_entry_t entry = (handover_entry_t)(UINTN)(code32 + 0x200);
    entry(ImageHandle, SystemTable, bp);
    for (;;)
        ;
}
