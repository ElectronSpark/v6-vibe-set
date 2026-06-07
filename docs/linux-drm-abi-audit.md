# Linux DRM ABI Baseline Audit

## Current refresh — 2026-06-07 Phase 6 + virgl validator + launcher blob probe

Build:

- `cmake --build build-x86_64 --target kernel user rootfs image -j"$(nproc)"`
  completed and regenerated `build-x86_64/kernel/build/kernel/xv6.bin` plus
  `build-x86_64/fs.img`; a follow-up `user rootfs image` rebuild staged the
  framebuffer sample probe in `drmabitest`.
- After Phase 6 structural cleanup, `cmake --build build-x86_64 --target kernel
  image -j"$(nproc)"` completed after each kernel commit and regenerated the
  multiboot `xv6.bin` plus `build-x86_64/fs.img`.
- After kernel `7b1af61`, `cmake --build build-x86_64 --target kernel image
  -j"$(nproc)"` completed and regenerated the multiboot `xv6.bin` plus
  `build-x86_64/fs.img`.
- After kernel `4ad498f`, `cmake --build build-x86_64 --target kernel image
  -j2` completed, `port-mesa` was rebuilt after removing temporary local
  diagnostics, and `cmake --build build-x86_64 --target image -j2`
  regenerated the exact validation `fs.img`.
- After launcher commit work, no kernel/rootfs rebuild was needed. The corrected
  script path was validated with `QEMU_DRY_RUN=1` and a headless boot using
  `DISPLAY_MODE=nographic USE_KVM=1 QEMU_GPU=virtio-gpu
  QEMU_VIRTIO_GPU_BLOB=auto QEMU_VIRTIO_GPU_HOSTMEM=32M
  QEMU_VIRTIO_GPU_MAX_HOSTMEM=32M`.
- For the external libdrm tool sweep, `ports/libdrm` was rebuilt with
  `install-test-programs=true` and `tests=true`, then
  `cmake --build build-x86_64/ports --target port-libdrm-clean -j2`,
  `cmake --build build-x86_64/ports --target port-libdrm -j2`, and
  `cmake --build build-x86_64 --target image -j2` regenerated the validation
  image.
- For upstream `drm_info`, `ports/json-c` and `ports/drm_info` were built and
  staged, then `cmake --build build-x86_64 --target image -j2` regenerated the
  validation image after the kernel object-property metadata fix.
- After hardening `mesaglsmoke`, `cmake --build build-x86_64/ports --target
  port-wayland -j2` rebuilt the Wayland validation clients, then
  `cmake --build build-x86_64 --target image -j2` regenerated the exact
  `fs.img` used by the passing GPU validator run.

Phase 6 cleanup commits validated in this refresh:

- Kernel `60ce6dd` shares GPU shmem page allocation.
- Kernel `0c6fc08` splits KMS and virtgpu implementation fragments.
- Kernel `1b3f1b8` renames the BO backing file to
  `fb_bo_shmem_dmabuf.c` and documents retained `FB_GPU_TTM_*` labels as
  compatibility metadata names.
- Kernel `7b1af61` negotiates virtio feature word 1 so
  `VIRTIO_F_VERSION_1` is accepted when QEMU/vhost-user offers a modern
  virtio-gpu device.
- Kernel `4ad498f` fixes `FB_GPU_VIRGL_RESOURCE_EXPORT_FD` for DRM render
  owners by looking up and dropping the transient BO through the owner-local
  handle table. Before this, Mesa's Wayland linux-dmabuf path failed
  `__DRI_IMAGE_ATTRIB_FD` with `-ENOENT` while the KMS-handle fallback could
  still succeed.
- Kernel `d122470` fixes KMS object-property metadata so `DRM_MODE_PROP_OBJECT`
  properties report their target object type in `values[0]`, matching Linux
  userspace expectations for properties such as `CRTC_ID` and `FB_ID`.
- Ports `832ba2f` adds upstream `drm_info` plus its static `json-c`
  dependency as xv6 validation ports.
- Ports `23c60af` makes `mesaglsmoke` render into an explicit GLES framebuffer,
  fail nonzero on GL/readback errors, and require at least one presented frame.
- Parent submodule bumps through the parent commit that records kernel
  `d122470`, ports `832ba2f`, and the follow-up ports validation hardening.

Boot:

- Manual non-GL blob path with KVM + udmabuf:
  `qemu-system-x86_64 -enable-kvm ... -machine pc,vmport=off,memory-backend=xv6ram ... -device virtio-gpu-pci,xres=1280,yres=800,blob=true,hostmem=32M,max_hostmem=32M ... -object memory-backend-memfd,id=xv6ram,size=4G,share=on`.
- Runtime log showed `/dev/gpu0`, `/dev/dri/card0`, and
  `/dev/dri/renderD128` registered, no panic/trap, and:
  `virtio_gpu: host visible shm id=1 ... len=0x2000000`,
  `features0=0x3000000a features1=0x101 driver_features0=0xa
  driver_features1=0x1 scanouts=1 capsets=0`,
  `GPU: virgl unavailable; exposing dumb-buffer DRM only`.
- The launcher dry-run now includes the full non-GL blob contract:
  `-machine pc,vmport=off,memory-backend=xv6mem0`,
  `-object memory-backend-memfd,id=xv6mem0,size=4G,share=on`, and
  `-device virtio-gpu-pci,...,blob=true,hostmem=32M,max_hostmem=32M`.

Probe highlights from `/bin/drmabitest`:

| Probe | card0 | renderD128 | Current result |
|---|---:|---:|---|
| `DRM_IOCTL_VIRTGPU_GETPARAM[RESOURCE_BLOB=3]` | `0/0 value=1` | `0/0 value=1` | guest blob resources honestly advertised |
| `DRM_IOCTL_VIRTGPU_GETPARAM[HOST_VISIBLE=4]` | `0/0 value=0` | `0/0 value=0` | fail-closed on this non-virgl transport |
| `DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB.valid` | `0/0 bo=5 res=4 size=4096 blob_mem=1` | `0/0 bo=5 res=5 size=4096 blob_mem=1` | real guest blob command reached virtio-gpu |
| `DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB.host_visible` | `create=-1 advertised=0 mmap_ok=0` | `create=-1 advertised=0 mmap_ok=0` | mappable HOST3D blob rejected while HOST_VISIBLE is not advertised |
| `DRM_IOCTL_MODE_ATOMIC.fences` | `atomic=0 out_fence=6 poll=0 dirty=0` | KMS denied | valid out-fence returned; clipped `DIRTYFB` positive path exercised on card0 |

Display/runtime evidence:

- `fbstat`: `virtio_failures 0`, `display_presents 243`,
  `display_completions 243`, `partial_blits 230`, `virgl_bo_presents 0`,
  `virtio_capsets 0`, `virtio_virgl 0`.
- `drmabitest` framebuffer sample from `/dev/fb0`:
  `ff000055 ff030055 ff060055 ff090055 ff0c0055 ff0f0055 ff120055 ff150055 ff180055 ff1b0055 ff1e0055 ff210055 ff240055 ff270055 ff2a0055 ff2d0055`.
- Current post-`drm_info` regression log
  `/tmp/xv6-drmabitest-post-drm-info-grep-pass.log` reached
  `drmabitest: end` with `expect_rc=0`; it shows
  `RESOURCE_BLOB=1` on both nodes, `HOST_VISIBLE=0` on both nodes, real
  guest-blob creates, HOST_VISIBLE blob create rejected while unadvertised, a
  card0 atomic commit with a nonzero out-fence, the `/dev/fb0` pixel sample,
  `virtio_failures 0`, and `virtio_timeouts 0`.
- External libdrm tools from the rebuilt port pass against the same image:
  headless `modetest -c` discovers and opens `/dev/dri/card0`, prints
  `Connectors:`, and exits `0`; `drmdevice` lists `nodes[0] /dev/dri/card0`
  plus `nodes[2] /dev/dri/renderD128` and exits `0`; explicit
  `modetest -D /dev/dri/card0 -p` prints `CRTCs:` and `Planes:` and exits `0`.
  The same run logged no `failed to open device` / `no device found` messages,
  no panic or fatal fault, and `virtio_failures 0`, `virtio_timeouts 0`.
- Upstream `drm_info /dev/dri/card0` from ports `832ba2f` passes in a headless
  `virtio-gpu` boot with blob auto-wiring enabled. Runtime log
  `/tmp/xv6-drm-info-fixed-marker.log` shows `Node: /dev/dri/card0`,
  `Connectors`, `CRTCs`, `Planes`, `"CRTC_ID" (atomic): object CRTC = ...`,
  `"FB_ID" (atomic): object framebuffer = 0`, `virtio_failures 0`,
  `virtio_timeouts 0`, and the `__DRMINFO_OK__` marker. `/tmp/xv6-debugcon.log`
  contains no panic, fatal page fault, coredump, or virtio-gpu timeout marker.

Virgl / Mesa validator evidence from the current run
`GPU_VALIDATE_TIMEOUT=240s GPU_VALIDATE_SECONDS=1 GPU_VALIDATE_3D_SECONDS=1 bash scripts/gpu/gpu-validate.sh`:

- `gpu-validate: PASS`.
- Re-run after the launcher fix still passed. The virgl path logged:
  `run-qemu: disabling virtio-gpu blob: this QEMU's virgl path is incompatible
  with blob resources`, proving blob is only disabled on the host-incompatible
  `-gl` lane.
- `virtio_gpu: initialized ... features0=0x30000003 features1=0x101
  driver_features0=0x3 driver_features1=0x1 scanouts=1 capsets=2`.
- `virtio_gpu: virgl capset ready id=1 version=1 size=308` and
  `virtio_gpu: virgl capset ready id=2 version=2 size=1384`.
- `wlcomp: linux-dmabuf enabled (virgl)` and multiple
  `wlcomp: dmabuf create_params ...` rows.
- Stock Mesa virgl Wayland EGL completed:
  `mesawlegl_completion_matrix loop=1 frames=12 seconds=1 ... status=0` and
  `mesawlegl[1]: complete frames=12 seconds=1 status=0 ...`.
- The legacy xv6 GPU-buffer Mesa smoke path also completed after the explicit
  framebuffer fix:
  `mesaglsmoke: EGL 1.5 GL OpenGL ES 3.1 ... renderer=virgl ... buffer=xv6-gpu-bo`
  and `mesaglsmoke[1]: complete frames=4 seconds=1 status=0`.
- Virgl resource, copy, scanout-copy, clear-source, render-bind,
  async-submit, invalid-submit, bad-submit, and dma-buf import probes all
  emitted their `__GPUV_*_DONE_0__` markers in
  `build-x86_64/gpu-validate.log`.
- Virgl resource PRIME identity stayed intact:
  `virgltest: dmabuf-resource-import ok ... imported_resource=73`.
- Backend separation remained honest:
  `backend virgl flags 0x27 renderer OpenGL via virtio-gpu virgl` and
  `opengl_submit_backend_separation_matrix ... status=PASS`.
- Leak/failure checks: `bo_handles 7`, `bo_fd_live 0`,
  `virtio_failures 0`, `virtio_timeouts 0`.

Outstanding host limitation for the required virgl/blob proof, rechecked after
Phase 6 on 2026-06-07:

- QEMU 9.0.2 device list has `virtio-gpu-gl-pci` and `virtio-gpu-pci`, but no
  rutabaga device.
- `-display egl-headless -device virtio-gpu-gl-pci,blob=true,...` fails with
  `egl: no drm render node available`.
- `-display gtk,gl=on -device virtio-gpu-gl-pci,blob=true,...` fails with
  `blobs and virgl are not compatible (yet)`.
- `-display gtk,gl=on -device virtio-vga-gl,blob=true,...` fails with the same
  `blobs and virgl are not compatible (yet)` rejection.
- `/usr/lib/qemu/vhost-user-gpu` is installed and reports `render-node` plus
  `virgl` in `--print-capabilities`. With `VIRTIO_F_VERSION_1` negotiated, the
  non-virgl `vhost-user-gpu-pci` path boots to userspace, but exposes no SHM
  window and no 3D capsets (`features0=0x30000002 features1=0x101
  driver_features0=0x2 driver_features1=0x1 scanouts=1 capsets=0`).
- The vhost-user virgl path remains host-blocked: `egl-headless,gl=on` fails
  before boot with `egl: no drm render node available`, while `gtk,gl=es`
  makes the helper log `Failed to initialize virgl`; xv6 then sees
  `scanouts=0 capsets=0` and virtio-gpu command timeouts.
- Therefore the current host can validate guest blobs and fail-closed
  HOST_VISIBLE behavior, but cannot complete the Step 5.2/5.3 virgl +
  mappable MAP_BLOB zero-copy proof.

## Previous baseline — 2026-06-06

Captured: 2026-06-06, x86_64 GUI boot, `/dev/dri/card0` and
`/dev/dri/renderD128`.

Probe: `/bin/drmabitest` from user submodule commit `af572bd`
(`Add DRM ABI ioctl probe`).

Build notes:

- `cmake -S . -B build-x86_64 -DXV6_ARCH=x86_64` completed.
- `cmake --build build-x86_64 --target user -j$(nproc)` completed.
- `cmake --build build-x86_64 --target image -j$(nproc)` reached the known
  NetSurf root-owned build directory blocker:
  `netsurf-libwapcaplet ... Permission denied`.
- The image used for this baseline was refreshed directly with
  `scripts/image/make-rootfs.sh build-x86_64/sysroot build-x86_64/fs.img auto`,
  which wrote `build-x86_64/fs.img` as a 2176 MiB ext4 image from the staged
  sysroot.

Runtime evidence:

- Boot command: `AUTO_BUILD=0 bash scripts/launch/launch-gui.sh x86_64`.
- Guest log source: `/tmp/xv6-debugcon.log`.
- GUI trace included virgl framebuffer page flips:
  `wlcomp: virgl framebuffer flip render_res=...` and async page-flip
  completions.
- `drmabitest` reached `drmabitest: end` and exited with wait status 0.
- Live scanout sample:
  `fb_sample_current screen=1280x800 pitch=512 rect=0,0 128x128 total=16384 nonzero=16384 nonblack=16384 avg_rgb=62,43,29 hash=0x9b5a7ff068c03c18 center=0xff0a1534 corners=0xff08102c,0xff09112d,0xff0f1d3f,0xff101e40`.
- Coverage check:
  `comm -23 <(rg -o 'DRM_IOCTL_[A-Z0-9_]+' docs/linux-drm-abi-impl-steps.md | sort -u) <(rg -o 'DRM_IOCTL_[A-Z0-9_]+' /tmp/xv6-debugcon.log | sort -u)`
  returned no missing ioctl names.

`ret/errno` uses the xv6 user convention reported by `drmabitest`: successful
calls are `0/0`; failures are returned as negative syscall values with
`errno=-ret`.

| Probe | card0 ret/errno | renderD128 ret/errno | Expected baseline |
|---|---:|---:|---|
| `DRM_IOCTL_ADD_BUFS` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_ADD_CTX` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_ADD_MAP` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_ACQUIRE` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_ALLOC` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_BIND` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_ENABLE` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_FREE` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_INFO` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_RELEASE` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AGP_UNBIND` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_AUTH_MAGIC.self` | `0/0` | `-1/1` | card auth succeeds on primary; render node denies auth ioctl |
| `DRM_IOCTL_CRTC_GET_SEQUENCE` | `0/0` | `-1/1` | card node only for KMS/display; render node denied |
| `DRM_IOCTL_CRTC_QUEUE_SEQUENCE.relative` | `-1/1` | `-1/1` | not implemented yet; render node denied |
| `DRM_IOCTL_DMA` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_DROP_MASTER` | `0/0` | `-1/1` | primary accepts; render node denied |
| `DRM_IOCTL_FINISH` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_FREE_BUFS` | `-1/1` | `-1/1` | fail-closed or stub unless later phase implements it |
| `DRM_IOCTL_GEM_CLOSE.imported` | `0/0` | `0/0` | imported PRIME handle close succeeds |
| `DRM_IOCTL_GEM_CLOSE.invalid` | `-1/1` | `-1/1` | invalid handle rejected |
| `DRM_IOCTL_GEM_FLINK.invalid` | `-1/1` | `-1/1` | stub until Step 2.4 |
| `DRM_IOCTL_GEM_FLINK.valid` | `-1/1` | `-1/1` | stub until Step 2.4 |
| `DRM_IOCTL_GEM_OPEN.invalid` | `-1/1` | `-1/1` | stub until Step 2.4 |
| `DRM_IOCTL_GET_CAP[ADDFB2_MODIFIERS]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[ASYNC_PAGE_FLIP]` | `0/0` | `0/0` | capability query succeeds; value is 0 |
| `DRM_IOCTL_GET_CAP[ATOMIC_ASYNC_PAGE_FLIP]` | `0/0` | `0/0` | capability query succeeds; value is 0 |
| `DRM_IOCTL_GET_CAP[CRTC_IN_VBLANK_EVENT]` | `0/0` | `0/0` | capability query succeeds; value is 0 |
| `DRM_IOCTL_GET_CAP[CURSOR_HEIGHT]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[CURSOR_WIDTH]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[DUMB_BUFFER]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[DUMB_PREFERRED_DEPTH]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[DUMB_PREFER_SHADOW]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[INVALID]` | `-1/1` | `-1/1` | invalid cap rejected |
| `DRM_IOCTL_GET_CAP[PAGE_FLIP_TARGET]` | `0/0` | `0/0` | capability query succeeds; value is 0 |
| `DRM_IOCTL_GET_CAP[PRIME]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[SYNCOBJ]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[SYNCOBJ_TIMELINE]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[TIMESTAMP_MONOTONIC]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CAP[VBLANK_HIGH_CRTC]` | `0/0` | `0/0` | capability query succeeds |
| `DRM_IOCTL_GET_CLIENT[0]` | `0/0` | `0/0` | client query succeeds |
| `DRM_IOCTL_GET_CTX` | `0/0` | `-1/1` | legacy stub-like primary behavior; render node denied |
| `DRM_IOCTL_GET_MAGIC` | `0/0` | `-1/1` | primary magic succeeds; render node denied |
| `DRM_IOCTL_GET_MAP` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_GET_SAREA_CTX` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_GET_STATS` | `0/0` | `0/0` | stats query succeeds |
| `DRM_IOCTL_GET_UNIQUE.fill` | `0/0` | `0/0` | two-pass unique fill succeeds |
| `DRM_IOCTL_GET_UNIQUE.probe` | `0/0` | `0/0` | two-pass unique probe succeeds |
| `DRM_IOCTL_INFO_BUFS` | `0/0` | `-1/1` | legacy primary behavior; render node denied |
| `DRM_IOCTL_LOCK` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_MAP_BUFS` | `0/0` | `-1/1` | legacy primary behavior; render node denied |
| `DRM_IOCTL_MARK_BUFS` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_MODE_ADDFB.invalid` | `-1/1` | `-1/1` | invalid input rejected; render node denied |
| `DRM_IOCTL_MODE_ADDFB2.invalid` | `-1/1` | `-1/1` | invalid input rejected; render node denied |
| `DRM_IOCTL_MODE_ADDFB2.valid` | `0/0` | `-1/1` | valid primary framebuffer add succeeds; render node denied |
| `DRM_IOCTL_MODE_ATOMIC.empty_test_only` | `0/0` | `-1/1` | empty TEST_ONLY succeeds on card; render node denied |
| `DRM_IOCTL_MODE_CLOSEFB.invalid` | `-1/1` | `-1/1` | invalid framebuffer rejected; render node denied |
| `DRM_IOCTL_MODE_CLOSEFB.valid` | `0/0` | `-` | valid close succeeds on card |
| `DRM_IOCTL_MODE_CREATEPROPBLOB.invalid` | `-1/1` | `-1/1` | stub until Step 3.1 |
| `DRM_IOCTL_MODE_CREATE_DUMB.valid` | `0/0` | `0/0` | dumb BO create succeeds |
| `DRM_IOCTL_MODE_CREATE_LEASE.invalid` | `-1/1` | `-1/1` | fail-closed leasing ioctl |
| `DRM_IOCTL_MODE_CURSOR.invalid` | `-1/1` | `-1/1` | cursor not wired yet; render node denied |
| `DRM_IOCTL_MODE_CURSOR2.invalid` | `-1/1` | `-1/1` | cursor not wired yet; render node denied |
| `DRM_IOCTL_MODE_DESTROYPROPBLOB.invalid` | `-1/1` | `-1/1` | stub until Step 3.1 |
| `DRM_IOCTL_MODE_DESTROY_DUMB.invalid` | `-1/1` | `-1/1` | invalid handle rejected |
| `DRM_IOCTL_MODE_DESTROY_DUMB.valid` | `0/0` | `0/0` | dumb BO destroy succeeds |
| `DRM_IOCTL_MODE_DIRTYFB.invalid` | `-1/1` | `-1/1` | invalid framebuffer rejected; render node denied |
| `DRM_IOCTL_MODE_GETCONNECTOR.fill` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETCONNECTOR.probe` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETCRTC` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETENCODER` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETFB.valid` | `0/0` | `-` | valid card framebuffer query succeeds |
| `DRM_IOCTL_MODE_GETFB2.valid` | `0/0` | `-` | valid card framebuffer query succeeds |
| `DRM_IOCTL_MODE_GETGAMMA.invalid` | `-1/1` | `-1/1` | no gamma hardware; render node denied |
| `DRM_IOCTL_MODE_GETPLANE.fill` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETPLANE.probe` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETPLANERESOURCES.fill` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETPLANERESOURCES.probe` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETPROPBLOB.invalid` | `-1/1` | `-1/1` | invalid blob rejected; render node denied |
| `DRM_IOCTL_MODE_GETPROPERTY.fill` | `0/0` | `-` | property query succeeds on discovered property |
| `DRM_IOCTL_MODE_GETPROPERTY.invalid` | `-1/1` | `-1/1` | invalid property rejected; render node denied |
| `DRM_IOCTL_MODE_GETPROPERTY.probe` | `0/0` | `-` | property probe succeeds on discovered property |
| `DRM_IOCTL_MODE_GETRESOURCES.fill` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GETRESOURCES.probe` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_GET_LEASE.invalid` | `-1/1` | `-1/1` | fail-closed leasing ioctl |
| `DRM_IOCTL_MODE_LIST_LESSEES.invalid` | `-1/1` | `-1/1` | fail-closed leasing ioctl |
| `DRM_IOCTL_MODE_MAP_DUMB.valid` | `0/0` | `0/0` | dumb BO map offset succeeds |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES.connector.fill` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES.connector.probe` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES.crtc.fill` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES.crtc.probe` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES.invalid` | `-1/1` | `-1/1` | invalid object rejected; render node denied |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES.plane.fill` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES.plane.probe` | `0/0` | `-1/1` | card query succeeds; render node denied |
| `DRM_IOCTL_MODE_OBJ_SETPROPERTY.invalid` | `-1/1` | `-1/1` | invalid property set rejected; render node denied |
| `DRM_IOCTL_MODE_PAGE_FLIP.invalid` | `-1/1` | `-1/1` | invalid flip rejected; render node denied |
| `DRM_IOCTL_MODE_REVOKE_LEASE.invalid` | `-1/1` | `-1/1` | fail-closed leasing ioctl |
| `DRM_IOCTL_MODE_RMFB.after_closefb` | `-1/1` | `-` | closefb already dropped the framebuffer |
| `DRM_IOCTL_MODE_SETCRTC.invalid` | `-1/1` | `-1/1` | invalid modeset rejected; render node denied |
| `DRM_IOCTL_MODE_SETGAMMA.invalid` | `-1/1` | `-1/1` | no gamma hardware; render node denied |
| `DRM_IOCTL_MODE_SETPLANE.invalid` | `-1/1` | `-1/1` | stub until Step 3.5 |
| `DRM_IOCTL_MOD_CTX` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_NEW_CTX` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_PRIME_FD_TO_HANDLE.valid` | `0/0` | `0/0` | local PRIME import succeeds |
| `DRM_IOCTL_PRIME_HANDLE_TO_FD.valid` | `0/0` | `0/0` | local PRIME export succeeds |
| `DRM_IOCTL_RES_CTX` | `0/0` | `-1/1` | legacy primary behavior; render node denied |
| `DRM_IOCTL_RM_CTX` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_RM_MAP` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_SET_CLIENT_CAP[2]` | `0/0` | `0/0` | universal planes client cap accepted |
| `DRM_IOCTL_SET_CLIENT_CAP[3]` | `0/0` | `0/0` | atomic client cap accepted |
| `DRM_IOCTL_SET_CLIENT_CAP[4294967295]` | `-1/1` | `-1/1` | invalid client cap rejected |
| `DRM_IOCTL_SET_CLIENT_CAP[4]` | `0/0` | `0/0` | aspect ratio client cap accepted |
| `DRM_IOCTL_SET_CLIENT_CAP[5]` | `0/0` | `0/0` | writeback connector cap accepted |
| `DRM_IOCTL_SET_CLIENT_NAME` | `0/0` | `0/0` | client name accepted |
| `DRM_IOCTL_SET_MASTER` | `0/0` | `-1/1` | primary accepts; render node denied |
| `DRM_IOCTL_SET_SAREA_CTX` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_SET_VERSION` | `0/0` | `0/0` | set-version compatibility call succeeds |
| `DRM_IOCTL_SG_ALLOC` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_SG_FREE` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_SWITCH_CTX` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_SYNCOBJ_CREATE.signaled` | `0/0` | `0/0` | signaled syncobj create succeeds |
| `DRM_IOCTL_SYNCOBJ_DESTROY` | `0/0` | `0/0` | syncobj destroy succeeds |
| `DRM_IOCTL_SYNCOBJ_DESTROY.imported` | `0/0` | `0/0` | imported syncobj destroy succeeds |
| `DRM_IOCTL_SYNCOBJ_EVENTFD.invalid_fd` | `-1/1` | `-1/1` | invalid eventfd rejected; real eventfd support is Step 1.5 |
| `DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE.sync_file` | `0/0` | `0/0` | current sync-file import path succeeds |
| `DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD.sync_file` | `0/0` | `0/0` | current sync-file export path succeeds |
| `DRM_IOCTL_SYNCOBJ_QUERY` | `0/0` | `0/0` | query succeeds for signaled point |
| `DRM_IOCTL_SYNCOBJ_RESET` | `0/0` | `0/0` | reset succeeds |
| `DRM_IOCTL_SYNCOBJ_SIGNAL` | `0/0` | `0/0` | signal succeeds |
| `DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL.point2` | `0/0` | `0/0` | timeline signal succeeds |
| `DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT.point1` | `0/0` | `0/0` | timeline wait succeeds for signaled point |
| `DRM_IOCTL_SYNCOBJ_TRANSFER.self` | `0/0` | `0/0` | self transfer succeeds |
| `DRM_IOCTL_SYNCOBJ_WAIT.signaled` | `0/0` | `0/0` | wait succeeds for signaled syncobj |
| `DRM_IOCTL_UNLOCK` | `-1/1` | `-1/1` | fail-closed legacy ioctl |
| `DRM_IOCTL_VERSION.fill` | `0/0` | `0/0` | two-pass version fill succeeds |
| `DRM_IOCTL_VERSION.probe` | `0/0` | `0/0` | two-pass version probe succeeds |
| `DRM_IOCTL_VIRTGPU_CONTEXT_INIT.empty` | `0/0` | `0/0` | empty context init accepted |
| `DRM_IOCTL_VIRTGPU_EXECBUFFER.invalid` | `-1/1` | `-1/1` | invalid execbuffer rejected |
| `DRM_IOCTL_VIRTGPU_GETPARAM[1]` | `-1/1` | `-1/1` | current baseline rejects param |
| `DRM_IOCTL_VIRTGPU_GETPARAM[2]` | `-1/1` | `-1/1` | current baseline rejects param |
| `DRM_IOCTL_VIRTGPU_GETPARAM[3]` | `-1/1` | `-1/1` | current baseline rejects param |
| `DRM_IOCTL_VIRTGPU_GETPARAM[4294967295]` | `-1/1` | `-1/1` | invalid param rejected |
| `DRM_IOCTL_VIRTGPU_GETPARAM[4]` | `-1/1` | `-1/1` | current baseline rejects param |
| `DRM_IOCTL_VIRTGPU_GETPARAM[6]` | `-1/1` | `-1/1` | current baseline rejects param |
| `DRM_IOCTL_VIRTGPU_GETPARAM[7]` | `-1/1` | `-1/1` | current baseline rejects param |
| `DRM_IOCTL_VIRTGPU_GETPARAM[8]` | `-1/1` | `-1/1` | current baseline rejects param |
| `DRM_IOCTL_VIRTGPU_GET_CAPS.invalid` | `0/0` | `0/0` | zero-size invalid probe currently succeeds |
| `DRM_IOCTL_VIRTGPU_MAP.invalid` | `-1/1` | `-1/1` | invalid handle rejected |
| `DRM_IOCTL_VIRTGPU_RESOURCE_CREATE.invalid` | `-1/1` | `-1/1` | invalid resource create rejected |
| `DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB.invalid` | `-1/1` | `-1/1` | invalid blob create rejected |
| `DRM_IOCTL_VIRTGPU_RESOURCE_INFO.invalid` | `-1/1` | `-1/1` | invalid resource query rejected |
| `DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST.invalid` | `-1/1` | `-1/1` | invalid transfer rejected |
| `DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST.invalid` | `-1/1` | `-1/1` | invalid transfer rejected |
| `DRM_IOCTL_VIRTGPU_WAIT.invalid` | `0/0` | `0/0` | zero handle wait currently succeeds |
| `DRM_IOCTL_WAIT_VBLANK.zero` | `0/0` | `-1/1` | card wait succeeds; render node denied |

## Runtime validation addendum — upstream kmscube (2026-06-07)

Built and staged upstream kmscube commit
`f60e50e887d3c49e91ac9b06d8199b36152632fa` through the new `ports/kmscube`
port, with libpng disabled by port patch so the xv6 sysroot does not need to
link static libpng/zlib into the validator.

Fresh image rebuild:

```sh
cmake --build build-x86_64/ports --target port-kmscube-clean -j2
cmake --build build-x86_64/ports --target port-kmscube -j2
cmake --build build-x86_64 --target image -j2
```

Runtime evidence under `DISPLAY_MODE=gtk USE_KVM=1
QEMU_GPU=virtio-vga-gl-primary QEMU_NET=0`:

- `/tmp/xv6-kmscube-realmarker.log`: `kmscube -D /dev/dri/renderD128 -O -v
  256x256 -c 4 -N` reached EGL 1.5 / OpenGL ES 3.1 through stock Mesa virgl,
  reported renderer `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`, and
  rendered 3 frames.
- `/tmp/xv6-kmscube-kms-fbstat.log`: `kmscube -D /dev/dri/card0 -c 3 -N`
  reached the same renderer through the KMS primary node and rendered frames.
- `/tmp/xv6-kmscube-kms-echo-fbstat.log`: a KMS-only follow-up completed
  `fbstat` with `backend virgl`, `backend_opengl_submit 1`, `virtio_failures
  0`, and `virtio_timeouts 0`.

Harness caveats: xv6 `sh` does not provide an `export` builtin, `>/tmp/file`
must be written with a space as `> /tmp/file`, and QEMU GTK/WSLg interleaves
serial shell echoes, Mesa logs, and diagnostics. Validation markers must be
printed in a way that the echoed input cannot satisfy (for example
`echo __KMS_''OK__`), and renderer/frame evidence should be read from the
captured transcript.
