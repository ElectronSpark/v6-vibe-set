#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DT_RELACOUNT
#define DT_RELACOUNT 0x6ffffff9
#endif

#define DEFAULT_CHROME "/opt/host-gui/wayland-chromium/chrome-linux64/chrome"

static const char *chrome_chain_paths[] = {
    "/opt/host-gui/wayland-chromium/chrome-linux64/chrome",
    "/opt/host-gui/wayland-chromium/chrome-linux64/chrome_crashpad_handler",
    "/opt/host-gui/wayland-chromium/chrome-linux64/libEGL.so",
    "/opt/host-gui/wayland-chromium/chrome-linux64/libGLESv2.so",
    "/opt/host-gui/wayland-chromium/chrome-linux64/libvk_swiftshader.so",
    "/opt/host-gui/wayland-chromium/chrome-linux64/libvulkan.so.1",
    "/opt/host-gui/wayland-chromium/chrome-linux64/WidevineCdm/_platform_specific/linux_x64/libwidevinecdm.so",
    "/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so",
    "/usr/lib/x86_64-linux-gnu/libdl.so.2",
    "/usr/lib/x86_64-linux-gnu/libpthread.so.0",
    "/usr/lib/x86_64-linux-gnu/libglib-2.0.so.0",
    "/usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0",
    "/usr/lib/x86_64-linux-gnu/libnspr4.so",
    "/usr/lib/x86_64-linux-gnu/libnss3.so",
    "/usr/lib/x86_64-linux-gnu/libnssutil3.so",
    "/usr/lib/x86_64-linux-gnu/libsmime3.so",
    "/usr/lib/x86_64-linux-gnu/libgio-2.0.so.0",
    "/usr/lib/x86_64-linux-gnu/libatk-1.0.so.0",
    "/usr/lib/x86_64-linux-gnu/libatk-bridge-2.0.so.0",
    "/usr/lib/x86_64-linux-gnu/libdbus-1.so.3",
    "/usr/lib/x86_64-linux-gnu/libcups.so.2",
    "/usr/lib/x86_64-linux-gnu/libexpat.so.1",
    "/usr/lib/x86_64-linux-gnu/libxcb.so.1",
    "/usr/lib/x86_64-linux-gnu/libxkbcommon.so.0",
    "/usr/lib/x86_64-linux-gnu/libasound.so.2",
    "/usr/lib/x86_64-linux-gnu/libX11.so.6",
    "/usr/lib/x86_64-linux-gnu/libXext.so.6",
    "/usr/lib/x86_64-linux-gnu/libcairo.so.2",
    "/usr/lib/x86_64-linux-gnu/libpango-1.0.so.0",
    "/usr/lib/x86_64-linux-gnu/libudev.so.1",
    "/usr/lib/x86_64-linux-gnu/libm.so.6",
    "/usr/lib/x86_64-linux-gnu/libXcomposite.so.1",
    "/usr/lib/x86_64-linux-gnu/libXdamage.so.1",
    "/usr/lib/x86_64-linux-gnu/libXfixes.so.3",
    "/usr/lib/x86_64-linux-gnu/libXrandr.so.2",
    "/usr/lib/x86_64-linux-gnu/libatspi.so.0",
    "/usr/lib/x86_64-linux-gnu/libgcc_s.so.1",
    "/usr/lib/x86_64-linux-gnu/libc.so.6",
    "/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
    "/lib/x86_64-linux-gnu/libgbm.so.1",
    "/lib/libgbm.so.1",
    "/lib64/ld-linux-x86-64.so.2",
};

struct elf_image {
    int fd;
    const char *path;
    struct stat st;
    Elf64_Ehdr eh;
    Elf64_Phdr *phdrs;
};

static int read_exact(int fd, void *buf, size_t len, off_t off)
{
    char *p = buf;
    size_t done = 0;

    while (done < len) {
        ssize_t n = pread(fd, p + done, len - done, off + (off_t)done);
        if (n < 0)
            return -errno;
        if (n == 0)
            return -EIO;
        done += (size_t)n;
    }
    return 0;
}

static int load_elf(struct elf_image *img, const char *path)
{
    memset(img, 0, sizeof(*img));
    img->fd = -1;
    img->path = path;

    img->fd = open(path, O_RDONLY);
    if (img->fd < 0)
        return -errno;
    if (fstat(img->fd, &img->st) < 0)
        return -errno;
    if (read_exact(img->fd, &img->eh, sizeof(img->eh), 0) != 0)
        return -EIO;
    if (memcmp(img->eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        img->eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        img->eh.e_phentsize != sizeof(Elf64_Phdr) ||
        img->eh.e_phnum == 0)
        return -ENOEXEC;

    size_t phdr_bytes = (size_t)img->eh.e_phnum * sizeof(Elf64_Phdr);
    img->phdrs = calloc(1, phdr_bytes);
    if (img->phdrs == NULL)
        return -ENOMEM;
    return read_exact(img->fd, img->phdrs, phdr_bytes, (off_t)img->eh.e_phoff);
}

static void close_elf(struct elf_image *img)
{
    free(img->phdrs);
    img->phdrs = NULL;
    if (img->fd >= 0)
        close(img->fd);
    img->fd = -1;
}

static int vaddr_to_offset(const struct elf_image *img, uint64_t vaddr,
                           uint64_t len, uint64_t *off_out)
{
    for (int i = 0; i < img->eh.e_phnum; i++) {
        const Elf64_Phdr *ph = &img->phdrs[i];

        if (ph->p_type != PT_LOAD)
            continue;
        if (vaddr < ph->p_vaddr)
            continue;
        if (len > ph->p_filesz)
            continue;
        if (vaddr - ph->p_vaddr > ph->p_filesz - len)
            continue;
        *off_out = ph->p_offset + (vaddr - ph->p_vaddr);
        return 0;
    }
    return -ENOENT;
}

static int find_dynamic(const struct elf_image *img, uint64_t *rela_off,
                        uint64_t *rela_size, uint64_t *rela_ent,
                        uint64_t *rela_count)
{
    uint64_t rela_vaddr = 0;
    uint64_t dyn_off = 0;
    uint64_t dyn_size = 0;

    for (int i = 0; i < img->eh.e_phnum; i++) {
        const Elf64_Phdr *ph = &img->phdrs[i];

        if (ph->p_type == PT_DYNAMIC) {
            dyn_off = ph->p_offset;
            dyn_size = ph->p_filesz;
            break;
        }
    }
    if (dyn_size == 0 || dyn_size % sizeof(Elf64_Dyn) != 0)
        return -ENOENT;

    for (uint64_t off = dyn_off; off < dyn_off + dyn_size;
         off += sizeof(Elf64_Dyn)) {
        Elf64_Dyn dyn;
        int ret = read_exact(img->fd, &dyn, sizeof(dyn), (off_t)off);

        if (ret != 0)
            return ret;
        if (dyn.d_tag == DT_NULL)
            break;
        switch (dyn.d_tag) {
        case DT_RELA:
            rela_vaddr = dyn.d_un.d_ptr;
            break;
        case DT_RELASZ:
            *rela_size = dyn.d_un.d_val;
            break;
        case DT_RELAENT:
            *rela_ent = dyn.d_un.d_val;
            break;
        case DT_RELACOUNT:
            *rela_count = dyn.d_un.d_val;
            break;
        default:
            break;
        }
    }

    if (rela_vaddr == 0 || *rela_size == 0 || *rela_ent != sizeof(Elf64_Rela) ||
        *rela_count == 0)
        return -EINVAL;
    return vaddr_to_offset(img, rela_vaddr, *rela_size, rela_off);
}

static int verify_rela_bytes(const char *mode, const unsigned char *base,
                             uint64_t count, uint64_t file_off)
{
    uint64_t first_bad = UINT64_MAX;
    uint64_t first_info = 0;

    for (uint64_t i = 0; i < count; i++) {
        const Elf64_Rela *rela = (const Elf64_Rela *)(base + i * sizeof(*rela));
        uint32_t type = (uint32_t)ELF64_R_TYPE(rela->r_info);

        if (type != R_X86_64_RELATIVE) {
            first_bad = i;
            first_info = rela->r_info;
            break;
        }
    }

    if (first_bad != UINT64_MAX) {
        printf("CHROME_RELA_PROBE_%s status=FAIL first_bad=%" PRIu64
               " file_off=0x%" PRIx64 " r_info=0x%" PRIx64
               " type=%" PRIu32 "\n",
               mode, first_bad,
               file_off + first_bad * (uint64_t)sizeof(Elf64_Rela),
               first_info, (uint32_t)ELF64_R_TYPE(first_info));
        return -1;
    }

    printf("CHROME_RELA_PROBE_%s status=PASS checked=%" PRIu64
           " first_file_off=0x%" PRIx64 " last_file_off=0x%" PRIx64 "\n",
           mode, count, file_off,
           file_off + (count - 1) * (uint64_t)sizeof(Elf64_Rela));
    return 0;
}

static int verify_read(const struct elf_image *img, uint64_t off, uint64_t count)
{
    uint64_t bytes = count * (uint64_t)sizeof(Elf64_Rela);
    unsigned char *buf = malloc((size_t)bytes);
    int ret;

    if (buf == NULL)
        return -ENOMEM;
    ret = read_exact(img->fd, buf, (size_t)bytes, (off_t)off);
    if (ret != 0) {
        free(buf);
        return ret;
    }
    ret = verify_rela_bytes("READ", buf, count, off);
    free(buf);
    return ret;
}

static int verify_mmap(const struct elf_image *img, uint64_t off, uint64_t count)
{
    long page_size = sysconf(_SC_PAGESIZE);
    uint64_t bytes = count * (uint64_t)sizeof(Elf64_Rela);
    uint64_t map_off;
    uint64_t delta;
    uint64_t map_len;
    void *map;
    int ret;

    if (page_size <= 0)
        page_size = 4096;
    map_off = off & ~((uint64_t)page_size - 1);
    delta = off - map_off;
    map_len = delta + bytes;

    map = mmap(NULL, (size_t)map_len, PROT_READ, MAP_PRIVATE, img->fd,
               (off_t)map_off);
    if (map == MAP_FAILED)
        return -errno;
    ret = verify_rela_bytes("MMAP", (const unsigned char *)map + delta,
                            count, off);
    munmap(map, (size_t)map_len);
    return ret;
}

static int probe_path(const char *path, int *checked_out, int *skipped_out)
{
    struct elf_image img;
    uint64_t rela_off = 0;
    uint64_t rela_size = 0;
    uint64_t rela_ent = 0;
    uint64_t rela_count = 0;
    int ret;

    ret = load_elf(&img, path);
    if (ret != 0) {
        printf("CHROME_RELA_PROBE_FAIL phase=load path=%s ret=%d errno=%d %s\n",
               path, ret, errno, strerror(errno));
        return 1;
    }

    ret = find_dynamic(&img, &rela_off, &rela_size, &rela_ent, &rela_count);
    if (ret != 0) {
        printf("CHROME_RELA_PROBE_SKIP phase=dynamic path=%s ret=%d\n",
               path, ret);
        close_elf(&img);
        if (skipped_out != NULL)
            *skipped_out += 1;
        return 0;
    }
    if (checked_out != NULL)
        *checked_out += 1;

    printf("CHROME_RELA_PROBE_META path=%s size=%lld rela_off=0x%" PRIx64
           " relasz=%" PRIu64 " relaent=%" PRIu64
           " relacount=%" PRIu64 "\n",
           path, (long long)img.st.st_size, rela_off, rela_size, rela_ent,
           rela_count);

    ret = verify_read(&img, rela_off, rela_count);
    if (ret == 0)
        ret = verify_mmap(&img, rela_off, rela_count);

    close_elf(&img);
    if (ret != 0) {
        printf("CHROME_RELA_PROBE_FAIL ret=%d\n", ret);
        return 1;
    }

    printf("CHROME_RELA_PROBE_PASS\n");
    return 0;
}

static int probe_chrome_chain(void)
{
    int checked = 0;
    int skipped = 0;
    int missing = 0;
    int failed = 0;

    for (size_t i = 0; i < sizeof(chrome_chain_paths) / sizeof(chrome_chain_paths[0]);
         i++) {
        struct stat st;
        const char *path = chrome_chain_paths[i];

        if (stat(path, &st) < 0) {
            printf("CHROME_RELA_PROBE_MISSING path=%s errno=%d %s\n",
                   path, errno, strerror(errno));
            missing++;
            continue;
        }
        if (probe_path(path, &checked, &skipped) != 0)
            failed++;
    }

    printf("CHROME_RELA_PROBE_CHAIN_RESULT checked=%d skipped=%d missing=%d failed=%d\n",
           checked, skipped, missing, failed);
    if (failed != 0)
        return 1;
    printf("CHROME_RELA_PROBE_CHAIN_PASS\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--chrome-chain") == 0)
        return probe_chrome_chain();

    if (argc <= 1)
        return probe_path(DEFAULT_CHROME, NULL, NULL);

    int failed = 0;
    for (int i = 1; i < argc; i++) {
        int checked = 0;
        int skipped = 0;

        if (probe_path(argv[i], &checked, &skipped) != 0)
            failed++;
    }
    return failed == 0 ? 0 : 1;
}
