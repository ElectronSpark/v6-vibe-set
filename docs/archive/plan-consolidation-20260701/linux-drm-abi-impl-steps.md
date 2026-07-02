# Linux DRM/GPU ABI — Step-by-Step Implementation Guide

Last updated: 2026-06-07

This document is a historical implementation companion to the archived
`docs/archive/plan-consolidation-20260701/linux-drm-abi-compat-plan.md`.
Current direction lives in `docs/active-work-plan.md`; this file lists the
older API step inventory for reference. It is written so a less-capable model
(or a new contributor) can execute one numbered step at a time without having
to design anything, but do not treat the 2026-06-07 ordering as current without
checking the active plan first.

## How to use this document

1. Work **top to bottom**. Steps are dependency-ordered.
2. Each step has: **File(s)**, **Function/Symbol**, **Do**, **Done when**.
3. After every step: build, then run the matching test from §12.
4. Never weaken an honesty/fail-closed gate to make a test pass (see the
   memory rules in `/memories/repo/`). If a feature is not real, the ioctl must
   still return a correct Linux errno, not a fake success.
5. One step = one small commit. Keep `FB_GPU_*` private ioctls working; new
   work is additive.

### Build & boot commands (from repo root)

```sh
# Reuse the existing toolchain (do NOT rebuild it ~30 min):
mkdir -p build-x86_64/toolchain
cp -al build-toolchain-x86_64/* build-x86_64/toolchain/
cmake -S . -B build-x86_64 -DXV6_ARCH=x86_64
cmake --build build-x86_64 --target world -j"$(nproc)"

# Kernel-only change → rebuild kernel + image (image rebuild REQUIRED):
cmake --build build-x86_64 --target xv6-kernel-x86 -j"$(nproc)"
cmake --build build-x86_64 --target xv6-images -j"$(nproc)"

# Boot GUI:
bash scripts/launch/launch-gui.sh x86_64 >/tmp/xv6-stdio.log 2>&1
# Guest serial/debugcon is mirrored to /tmp/xv6-debugcon.log — grep THAT file.
```

Key paths:
- Kernel DRM/GPU sources: `kernel/kernel/dev/fb/` and `kernel/kernel/virtio_gpu.c`
- DRM core: `kernel/kernel/dev/drm_core.c`, `kernel/kernel/inc/dev/drm_core.h`
- UAPI numbers/structs: `kernel/kernel/inc/uabi/drm.h`
- Ioctl router: `kernel/kernel/dev/fb/fb_drm_dispatch.c`
  (function `gpu_drm_ioctl_handle`, table `gpu_drm_ioctls[]`)
- KMS logic: `kernel/kernel/dev/fb/fb_drm_core_kms.c`
- KMS atomic/page-flip/FB: `kernel/kernel/dev/fb/fb_kms_atomic.c`
- GEM/BO/dma-buf: `kernel/kernel/dev/fb/fb_bo_shmem_dmabuf.c`
- syncobj/PRIME/virtgpu user ioctls: `kernel/kernel/dev/fb/fb_syncobj_prime_virtgpu.c`
- Fence/dma-buf fds: `kernel/kernel/dev/fb/fb_fd_sync.c`

> **Note:** `kernel/` is a git submodule. Commit inside it, then commit the
> submodule pointer bump in the parent repo.

---

## 1. Complete API inventory (status + action)

Status legend: **OK** = Linux-compatible for the xv6-supported backend;
**FAIL-CLOSED** = intentionally unsupported/out-of-scope and returns a Linux
errno instead of fake success; **OPTIONAL-HOST** = kernel code is present and
honestly gated, but the current host backend refuses an optional zero-copy
optimization while the Alpine-validated transfer model remains the working
path.
The table below reflects the 2026-06-07 audit in
[`docs/linux-drm-abi-audit.md`](./linux-drm-abi-audit.md); the numbered steps
remain below as implementation history and reproducible validation guidance.

### 1.1 DRM core ioctls (`drm_core.c`, `fb_drm_dispatch.c`)

| Ioctl | Status | Action | Where |
|---|---|---|---|
| `DRM_IOCTL_VERSION` | OK | Audit name/date/desc 2-pass length probe | `gpu_drm_version` (kms.c:455) |
| `DRM_IOCTL_GET_MAGIC` / `AUTH_MAGIC` | OK | none | `drm_core_*` |
| `DRM_IOCTL_GET_CLIENT` | OK | none | `drm_core_get_client` |
| `DRM_IOCTL_GET_UNIQUE` / `SET_VERSION` | OK | Audit struct fields vs libdrm | dispatch.c |
| `DRM_IOCTL_GET_CAP` | OK (truthful) | Keep caps==behavior invariant | `gpu_drm_get_cap` (kms.c:539) |
| `DRM_IOCTL_SET_CLIENT_CAP` | OK | none | `drm_core_set_client_cap` |
| `DRM_IOCTL_SET_MASTER` / `DROP_MASTER` | OK | none | `drm_core_*_master` |
| Legacy (`ADD_MAP`, `*_BUFS`, `*_CTX`, `DMA`, `LOCK`, AGP, SG) | FAIL-CLOSED | leave as-is (legacy, not needed) | dispatch.c |

### 1.2 GEM / dumb / PRIME

| Ioctl | Status | Action | Where |
|---|---|---|---|
| `DRM_IOCTL_GEM_CLOSE` | OK | per-file handle table + BO refcount landed | dispatch.c:~98 |
| `DRM_IOCTL_GEM_FLINK` | OK | global flink names landed | dispatch.c:~113 |
| `DRM_IOCTL_GEM_OPEN` | OK | open-by-name landed | dispatch.c:~125 |
| `DRM_IOCTL_MODE_CREATE_DUMB` | OK | none | `gpu_drm_create_dumb` |
| `DRM_IOCTL_MODE_MAP_DUMB` | OK | none | dispatch.c |
| `DRM_IOCTL_MODE_DESTROY_DUMB` | OK | none | dispatch.c |
| `DRM_IOCTL_PRIME_HANDLE_TO_FD` | OK | generic dma-buf export landed | `gpu_drm_prime_handle_to_fd` |
| `DRM_IOCTL_PRIME_FD_TO_HANDLE` | OK | generic dma-buf import + per-file handles landed | `gpu_drm_prime_fd_to_handle` |

### 1.3 KMS modesetting (`fb_drm_core_kms.c`, `fb_kms_atomic.c`)

| Ioctl | Status | Action | Where |
|---|---|---|---|
| `MODE_GETRESOURCES` | OK | cursor plane included | `gpu_drm_mode_getresources` |
| `MODE_GETCRTC` / `SETCRTC` | OK | none | kms.c / atomic.c:258 |
| `MODE_GETCONNECTOR` | OK | connector mode list exposed | `gpu_drm_mode_getconnector` |
| `MODE_GETENCODER` | OK | none | `gpu_drm_mode_getencoder` |
| `MODE_GETPLANE` / `GETPLANERESOURCES` | OK | primary + cursor planes exposed | kms/properties fragments |
| `MODE_SETPLANE` | OK | cursor plane set/hide path landed | `gpu_drm_mode_setplane` |
| `MODE_GETPROPERTY` / `GETPROPBLOB` | OK | none | kms.c |
| `MODE_OBJ_GETPROPERTIES` | OK | none | kms.c:1861 |
| `MODE_OBJ_SETPROPERTY` | OK | property validation/update wired to KMS state | kms properties |
| `MODE_ADDFB2` / `RMFB` / `CLOSEFB` | OK | none | atomic.c |
| `MODE_ADDFB` (legacy) | OK | maps to ADDFB2-compatible path | dispatch.c |
| `MODE_GETFB` / `GETFB2` | OK | none | kms.c:1606/1631 |
| `MODE_PAGE_FLIP` | OK (event) | real out-fence on completion (Step 3.3) | atomic.c:299 |
| `MODE_DIRTYFB` | OK | feed damage to RESOURCE_FLUSH (Step 5.4) | kms.c:1691 |
| `MODE_ATOMIC` | OK | atomic check/rollback/commit + out-fence path landed | atomic.c |
| `MODE_CREATEPROPBLOB` | OK | writable blobs landed | dispatch/KMS properties |
| `MODE_DESTROYPROPBLOB` | OK | destroy + post-destroy rejection landed | dispatch/KMS properties |
| `MODE_CURSOR` / `CURSOR2` | OK | virtio cursor queue path landed | `gpu_drm_mode_cursor` |
| `MODE_GETGAMMA` / `SETGAMMA` | FAIL-CLOSED | leave (no gamma HW) | `gpu_drm_mode_gamma` |
| `MODE_CREATE_LEASE` & friends | FAIL-CLOSED | leave (no lease/multi-seat backend) | `gpu_drm_mode_lease_fail_closed` |
| `WAIT_VBLANK` | OK | present-driven zero/query paths; real-present validation covered by GUI validators | `gpu_drm_wait_vblank` |
| `CRTC_GET_SEQUENCE` / `QUEUE_SEQUENCE` | OK / FAIL-CLOSED | get sequence works; unsupported queue requests reject honestly | kms.c |

### 1.4 DRM syncobj / fences (`fb_syncobj_prime_virtgpu.c`, `fb_fd_sync.c`)

| Ioctl | Status | Action | Where |
|---|---|---|---|
| `SYNCOBJ_CREATE` / `DESTROY` | OK | reback on dma_fence (Step 1.2) | `gpu_syncobj_create/destroy` |
| `SYNCOBJ_WAIT` / `TIMELINE_WAIT` | OK | reback on dma_fence (Step 1.2) | `gpu_syncobj_wait_common` |
| `SYNCOBJ_SIGNAL` / `TIMELINE_SIGNAL` | OK | reback on dma_fence (Step 1.2) | `gpu_syncobj_array_signal_reset` |
| `SYNCOBJ_RESET` | OK | none | same |
| `SYNCOBJ_QUERY` | OK | LAST_SUBMITTED semantics landed | `gpu_syncobj_query` |
| `SYNCOBJ_TRANSFER` | OK (proxy) | verify under fence model (Step 1.2) | `gpu_syncobj_transfer` |
| `SYNCOBJ_HANDLE_TO_FD` / `FD_TO_HANDLE` | OK | real sync_file fd path landed | `gpu_syncobj_handle_to_fd/fd_to_handle` |
| `SYNCOBJ_EVENTFD` | OK | fence callback + eventfd signaling landed | `gpu_syncobj_eventfd` |

### 1.5 virtio-gpu UAPI (`fb_drm_dispatch.c`, `fb_syncobj_prime_virtgpu.c`)

| Ioctl | Status | Action | Where |
|---|---|---|---|
| `VIRTGPU_GETPARAM` | OK / OPTIONAL-HOST | `RESOURCE_BLOB=1`; `HOST_VISIBLE=0` unless a host-visible blob maps successfully | dispatch.c |
| `VIRTGPU_GET_CAPS` | OK | none | dispatch.c |
| `VIRTGPU_CONTEXT_INIT` | OK | none | dispatch.c:368 |
| `VIRTGPU_RESOURCE_CREATE` | OK | none | `gpu_drm_virtgpu_resource_create` |
| `VIRTGPU_RESOURCE_INFO` | OK | reports resource/blob metadata | dispatch.c |
| `VIRTGPU_RESOURCE_CREATE_BLOB` | OK / OPTIONAL-HOST | guest blobs real; host-visible blobs gated by host map probe | virtgpu resource path |
| `VIRTGPU_TRANSFER_TO/FROM_HOST` | OK | none | `gpu_drm_virtgpu_transfer` |
| `VIRTGPU_WAIT` | OK | waits per-resource fence | dispatch.c |
| `VIRTGPU_MAP` | OK / OPTIONAL-HOST | dumb/guest offsets work; host-visible offset returns only after successful `RESOURCE_MAP_BLOB` | dispatch.c |
| `VIRTGPU_EXECBUFFER` | OK | BO list + in/out fence fds landed; virgl lane validated under `-gl` | dispatch.c |

### 1.6 virtio-gpu transport (`virtio_gpu.c`) — feature flags

| Feature | Status | Action |
|---|---|---|
| `VIRTIO_GPU_F_VIRGL` / `EDID` / `CONTEXT_INIT` | OK | none |
| `VIRTIO_GPU_F_RESOURCE_BLOB` | OK | negotiated when host offers it; guest blob create validated |
| Host-visible shmem region (`VIRTIO_GPU_SHM_ID_HOST_VISIBLE`) | OPTIONAL-HOST | SHM/BAR + map/unmap code present; current host refuses mappable blobs, so `HOST_VISIBLE=0` |

### 1.7 fbdev (`fb.h`, `fb_device_ioctl.c`)

| Ioctl | Status | Action |
|---|---|---|
| `FBIOGET_VSCREENINFO` / `FSCREENINFO` / `PUT_VSCREENINFO` | OK | audited enough for current fbdev consumers; private ioctls remain additive |

---

## 2. Phase 0 — Audit & test harness (do first, low risk)

### Step 0.1 — Create the ABI test program
- **File (new):** `user/programs/drmabitest/drmabitest.c` (+ CMake entry mirroring
  `user/programs/linuxsyscallabitest/`).
- **Do:** Open `/dev/dri/card0` and `/dev/dri/renderD128`. For each ioctl in §1,
  call it with valid and invalid inputs; print `name: ret=<n> errno=<e>`.
  Start with read-only ones: `VERSION`, `GET_CAP` (loop all `DRM_CAP_*`),
  `MODE_GETRESOURCES`, `MODE_GETCONNECTOR`, `MODE_GETPLANERESOURCES`.
- **Done when:** the program builds and prints a status line per ioctl in-guest.

### Step 0.2 — Snapshot the baseline
- **Do:** Run `drmabitest` in-guest, save output to `docs/linux-drm-abi-audit.md`
  (a table: ioctl, ret, errno, expected). This is the regression baseline.
- **Done when:** every ioctl from §1 has a recorded current result.

### Step 0.3 — fbdev struct audit
- **File:** `kernel/kernel/inc/dev/fb.h`, `fb_device_ioctl.c` (`fb_ioctl`).
- **Do:** Compare the `fb_var_screeninfo`/`fb_fix_screeninfo` returned fields
  (xres, yres, bits_per_pixel, RGBA bitfield offsets/lengths, `line_length`,
  `smem_len`) against Linux `<linux/fb.h>`. Fix any mismatched offset/length.
- **Done when:** a Linux `fbset`-style reader sees correct RGBA layout.

---

## 3. Phase 1 — dma_fence core (HIGHEST LEVERAGE)

This unifies syncobj, dma-buf, sync_file, and atomic fences. Do it before
Phases 2–4 because they depend on real fences.

### Step 1.1 — Add the `dma_fence` type
- **File (new):** `kernel/kernel/inc/dev/dma_fence.h` + `kernel/kernel/dev/fb/dma_fence.c`.
  Add the `.c` to `kernel/kernel/dev/fb/CMakeLists.txt` (or the fb module list).
- **Do:** Define:
  ```c
  struct dma_fence {
      uint64 context;          // unique per fence timeline
      uint64 seqno;
      int    signaled;         // 0/1
      int    error;            // 0 or -errno
      uint32 refs;
      struct list callbacks;   // dma_fence_cb list
      spinlock_t lock;
  };
  struct dma_fence_cb { struct list node; void (*fn)(struct dma_fence*, void*); void *arg; };
  ```
  Implement: `dma_fence_init`, `dma_fence_get`/`put` (refcount),
  `dma_fence_signal` (set signaled, run callbacks, wake waiters),
  `dma_fence_add_callback`, `dma_fence_is_signaled`,
  `dma_fence_wait(fence, timeout_ns)` (sleep on a wait channel until signaled).
  Use existing `sched_timer_add` for the timeout (see how
  `gpu_syncobj_wait_common` already does timed sleeps).
- **Done when:** a unit smoke (kernel boot self-test) creates a fence, adds a
  callback, signals it, and the callback ran exactly once.

### Step 1.2 — Reback syncobj on dma_fence
- **File:** `fb_syncobj_prime_virtgpu.c`, `fb_internal.c` (struct
  `fb_gpu_syncobj_state_entry`).
- **Do:** Replace the `reservation_fence`/`timeline_value` counter logic so each
  syncobj state owns (or points to) a `struct dma_fence` per timeline point.
  - `SIGNAL`/`TIMELINE_SIGNAL` → `dma_fence_signal` the point's fence.
  - `WAIT`/`TIMELINE_WAIT` → `dma_fence_wait` on the relevant fences.
  - `RESET` → drop the fence, install a fresh unsignaled one.
  Keep the existing transfer/proxy behavior but resolve it through the fence
  (a transfer attaches the source fence to the destination point).
- **Done when:** `drmabitest` syncobj create→signal→wait still passes; a wait on
  an unsignaled timeline point blocks until signaled by another thread.

### Step 1.3 — Fix SYNCOBJ_QUERY semantics
- **File:** `fb_syncobj_prime_virtgpu.c` (`gpu_syncobj_query`).
- **Do:** When `DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED` is set, return the last
  *submitted* point; otherwise the last *signaled* point. Today it always
  returns `timeline_value`.
- **Done when:** query of a timeline with submitted-but-unsignaled points
  returns the correct value per flag.

### Step 1.4 — Make exported syncobj/PRIME fds real sync_files
- **File:** `fb_fd_sync.c` (`fb_syncobj_file_ops`), `fb_syncobj_prime_virtgpu.c`
  (`gpu_syncobj_handle_to_fd` / `fd_to_handle`).
- **Do:** Back the exported fd by a `dma_fence`. Implement `.poll` to return
  `POLLIN` once the fence is signaled (register a `dma_fence_cb` that wakes the
  poll waitqueue). On `FD_TO_HANDLE`, wrap the imported fence into a new syncobj
  state.
- **Done when:** export an fd, `poll()` blocks, signal the source syncobj, the
  `poll()` returns `POLLIN`.

### Step 1.5 — Implement SYNCOBJ_EVENTFD
- **File:** `fb_syncobj_prime_virtgpu.c` (`gpu_syncobj_eventfd`; implemented in
  Phase 1).
- **Do:** Look up the eventfd from `req.fd` (via the vfs fd table / eventfd
  signal path). Register a `dma_fence_cb` on the syncobj's fence that writes the
  eventfd counter when the fence signals. If `WAIT_AVAILABLE` is set, arm when a
  fence is merely attached.
- **Done when:** an armed eventfd becomes readable after the syncobj signals.

---

## 4. Phase 2 — Per-file GEM handles + dma-buf generalization

### Step 2.1 — Add a per-file handle table
- **File:** `kernel/kernel/inc/dev/drm_core.h` (add to `struct drm_core_file` or a
  driver sub-struct in `fb_gpu_render_owner`), `fb_bo_shmem_dmabuf.c`.
- **Do:** Add a small map `handle(uint32) → struct fb_gpu_bo_entry*` per owner
  (reuse `xarray.h` or a fixed array + free list). The BO keeps a **global**
  refcount; the per-file map holds one reference per handle.
- **Done when:** two owners can hold different handle numbers for the same
  underlying BO; closing one owner's handle does not free the BO if the other
  still references it.

### Step 2.2 — PRIME import creates a NEW per-file handle
- **File:** `fb_syncobj_prime_virtgpu.c` (`gpu_drm_prime_fd_to_handle`).
- **Do:** On import, allocate a fresh handle **in the importing owner's table**
  pointing at the same BO (bump global refcount). Do not reuse the exporter's
  handle number.
- **Done when:** `HANDLE_TO_FD` in process A, `FD_TO_HANDLE` in process B yields
  a B-local handle that maps to the same pixels.

### Step 2.3 — GEM_CLOSE drops only the file's reference
- **File:** `fb_drm_dispatch.c` (`DRM_IOCTL_GEM_CLOSE` case, ~line 98).
- **Do:** Remove the handle from the owner's table and drop one global ref;
  free the BO only when the global refcount hits zero.
- **Done when:** closing an imported handle in B leaves A's handle valid.

### Step 2.4 — Implement GEM_FLINK / GEM_OPEN
- **File:** `fb_bo_shmem_dmabuf.c` (add a global name→BO table),
  `fb_drm_dispatch.c` (`GEM_FLINK` ~line 113, `GEM_OPEN` ~line 125).
- **Do:** `FLINK` assigns/returns a unique global name for a BO. `OPEN` looks up
  the name, creates a new per-file handle (Step 2.1), returns handle + size.
- **Done when:** `FLINK` in A, `OPEN` in B shares the BO by name.

### Step 2.5 — Generic dma_buf object + ops
- **File:** `fb_bo_shmem_dmabuf.c`, `fb_fd_sync.c`.
- **Do:** Introduce `struct dma_buf { const struct dma_buf_ops *ops; void *priv;
  uint64 size; struct dma_fence *resv_excl; ... }` with ops
  `{ get_pages, put_pages, mmap }`. Make the fb BO one exporter. Change
  `FD_TO_HANDLE` to accept any fd whose file-ops is the dma_buf ops (not a
  hard exporter-tag check), obtaining pages through `ops->get_pages`.
- **Done when:** a non-fb dma_buf (even a test exporter) can be imported; the
  existing local fb→fb path still works.

### Step 2.6 — dma-buf mmap
- **File:** `fb_fd_sync.c` (`fb_dmabuf_file_ops`).
- **Do:** Implement `.mmap` on the PRIME fd so `mmap(dmabuf_fd, ...)` maps the
  BO pages directly (with `DONTFORK|DONTDUMP`, per the runtime memory note).
- **Done when:** `gbm_bo_map`-style `mmap(fd)` returns writable pixels.

---

## 5. Phase 3 — Real KMS atomic + blobs + cursor

### Step 3.1 — Writable property blobs
- **File:** `fb_drm_dispatch.c` (`MODE_CREATEPROPBLOB` ~line 232,
  `MODE_DESTROYPROPBLOB` ~line 250), `fb_drm_core_kms.c` (blob storage +
  `gpu_drm_mode_getblob`).
- **Do:** Add a small blob table `{id, length, data}`. `CREATEPROPBLOB` copies
  user data in, allocates an id (above the reserved built-in ids), returns it.
  `GETPROPBLOB` returns user blobs too. `DESTROYPROPBLOB` frees a user blob
  (reject built-in ids, as it already does).
- **Done when:** create a MODE_ID blob, read it back identical, destroy it.

### Step 3.2 — Flip-driven vblank/sequence
- **File:** `fb_drm_core_kms.c` (`gpu_kms_sample_vblank_locked`,
  `gpu_drm_wait_vblank`, `gpu_drm_crtc_get_sequence`,
  `gpu_drm_crtc_queue_sequence`).
- **Do:** Increment the CRTC sequence + timestamp at **present completion**
  (when the scanout actually flips — hook the virtio-gpu `RESOURCE_FLUSH`/
  present-done path in `fb_scanout.c`) instead of from synthetic ticks. Make
  `WAIT_VBLANK`/`QUEUE_SEQUENCE` block until that real sequence advances.
- **Done when:** `WAIT_VBLANK` wakes once per actual present, and vblank
  timestamps are monotonic and match present cadence.

### Step 3.3 — Real out-fence + in-fence in page-flip/atomic
- **File:** `fb_kms_atomic.c` (`gpu_drm_mode_page_flip` ~line 299,
  `gpu_drm_mode_atomic` ~line 379).
- **Do:** Use the Phase 1 `dma_fence`:
  - **In-fence:** if `IN_FENCE_FD` (atomic) is given, `dma_fence_wait` on it
    before applying the flip (instead of rejecting with `-EOPNOTSUPP`).
  - **Out-fence:** create a `dma_fence` for the flip, return it via
    `OUT_FENCE_PTR` as a sync_file fd, and `dma_fence_signal` it at present
    completion (Step 3.2 hook).
- **Done when:** a compositor's atomic commit with in/out fences completes and
  the out-fence signals exactly once per flip.

### Step 3.4 — Real atomic check/commit split
- **File:** `fb_kms_atomic.c` (`gpu_drm_mode_atomic`),
  `fb_drm_core_kms.c` (`gpu_kms_validate_prop_locked`).
- **Do:** Build a transient state set for the commit (per plane/CRTC/connector).
  Run a **check** pass that validates the whole set with no side effects
  (`TEST_ONLY` returns here). Only if check passes, run **commit** that applies
  all objects together; on any failure, discard the transient state (rollback)
  and return the errno **without** having touched `fb_state`. Support
  `ATOMIC_NONBLOCK` real async (apply, return immediately, signal out-fence on
  completion).
- **Done when:** a failing `TEST_ONLY` commit changes nothing; a multi-object
  commit either fully applies or fully fails.

### Step 3.5 — Cursor plane wired to virtio-gpu cursor queue
- **File:** `fb_drm_core_kms.c` (add a `GPU_DRM_CURSOR_PLANE_ID`, extend
  `getresources`/`getplaneresources`/`getplane`), `fb_kms_atomic.c`/`kms.c`
  (`gpu_drm_mode_setplane`, `gpu_drm_mode_cursor`).
- **Do:** Add a second plane of type `DRM_PLANE_TYPE_CURSOR`. Implement
  `SETPLANE` and `MODE_CURSOR`/`CURSOR2` by calling the existing
  `FB_GPU_SET_CURSOR`/`FB_GPU_MOVE_CURSOR` virtio cursor path
  (`fb_device_ioctl.c`). Report `DRM_CLIENT_CAP_UNIVERSAL_PLANES` truthfully.
- **Done when:** `modetest -P` / a compositor sets a hardware cursor and it
  moves on screen.

### Step 3.6 — Legacy ADDFB → ADDFB2 shim
- **File:** `fb_drm_dispatch.c` (`MODE_ADDFB` case, currently `-EOPNOTSUPP`).
- **Do:** Translate the single-plane legacy `drm_mode_fb_cmd` (depth/bpp) into an
  `ADDFB2` call (pick `DRM_FORMAT_XRGB8888`/`ARGB8888` from depth/bpp) and reuse
  `gpu_drm_mode_addfb2`.
- **Done when:** an `ADDFB` from a legacy client returns a valid fb id.

### Step 3.7 — (optional) Real connector mode list
- **File:** `fb_drm_core_kms.c` (`gpu_drm_mode_getconnector`).
- **Do:** Feed the virtio-gpu `GET_EDID` / `GET_DISPLAY_INFO` data into a small
  list of modes instead of a single generated mode.
- **Done when:** `drm_info` shows >1 selectable mode with a PREFERRED flag.

---

## 6. Phase 4 — Complete the virtio-gpu UAPI

### Step 4.1 — EXECBUFFER: BO list + in/out fences
- **File:** `fb_drm_dispatch.c` (`DRM_IOCTL_VIRTGPU_EXECBUFFER`, ~line 498),
  `virtio_gpu.c` (`virtio_gpu_user_submit`).
- **Do:** Parse `req.bo_handles`/`req.num_bo_handles` and pass them to the
  submit for residency. Honor `req.fence_fd`/`VIRTGPU_EXECBUF_FENCE_FD_IN/OUT`:
  wait the in-fence before submit; create a `dma_fence` (Phase 1) for the submit
  and return it as `fence_fd` (sync_file) when out-fence is requested. Today the
  call passes `NULL, 0` for the BO list and ignores fences.
- **Done when:** Mesa explicit-sync submit (in/out fence fds) round-trips.

### Step 4.2 — VIRTGPU_WAIT waits the BO's fence
- **File:** `fb_drm_dispatch.c` (`DRM_IOCTL_VIRTGPU_WAIT`).
- **Do:** Look up the resource by `req.handle` and wait on **that resource's**
  last-submit fence, not the global fence (today it calls
  `virtio_gpu_user_fence(0, ...)`). Respect `VIRTGPU_WAIT_NOWAIT` (return
  `-EBUSY` if unsignaled).
- **Done when:** `WAIT` on a specific resource returns only after that
  resource's work completes.

### Step 4.3 — Bridge virtgpu resources to PRIME export
- **File:** `fb_syncobj_prime_virtgpu.c`, `fb_bo_shmem_dmabuf.c`.
- **Do:** Allow `PRIME_HANDLE_TO_FD` on a virtgpu resource handle (so a
  GL-rendered buffer can be exported and re-imported by the KMS node via
  `ADDFB2`). Make the resource handle namespace and the BO handle namespace
  interoperate through the per-file table (Phase 2).
- **Done when:** render on `renderD128`, export fd, `ADDFB2`+`PAGE_FLIP` on
  `card0` shows the rendered frame (the desktop hand-off path).

---

## 7. Phase 5 — Blob / host-visible resources (performance)

### Step 5.1 — Negotiate RESOURCE_BLOB + real blob create
- **File:** `virtio_gpu.c` (feature negotiation, add
  `VIRTIO_GPU_F_RESOURCE_BLOB`; add `VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB`),
  `fb_syncobj_prime_virtgpu.c` (`gpu_drm_virtgpu_resource_create` blob branch,
  formerly faked as a 2D resource), `fb_drm_dispatch.c`
  (`VIRTGPU_GETPARAM`: report `RESOURCE_BLOB=1` once real).
- **Do:** Send the real blob-create command with `blob_mem`/`blob_flags`/
  `blob_id`; track blob resources distinctly from 2D/3D ones.
- **Done when:** `GETPARAM(RESOURCE_BLOB)==1` and a blob resource is created via
  the real command (verify in the virtio trace).

### Step 5.2 — Host-visible region + MAP_BLOB
- **File:** `virtio_gpu.c` (add the host-visible shmem region
  `VIRTIO_GPU_SHM_ID_HOST_VISIBLE` + `RESOURCE_MAP_BLOB`/`UNMAP_BLOB`),
  `fb_drm_dispatch.c` (`VIRTGPU_MAP` returns the real mmap offset for blobs),
  `VIRTGPU_GETPARAM` (`HOST_VISIBLE=1`).
- **Do:** Map host-visible blob memory into the guest and return its offset from
  `VIRTGPU_MAP` so userspace gets zero-copy access (no `TRANSFER_TO_HOST`).
- **Done when:** a mappable blob is written by the guest and the host sees it
  without an explicit transfer.

### Step 5.3 — Default to resource-bind scanout (no readback)
- **File:** `fb_scanout.c` (`fb_blit_from_bo_format`).
- **Do:** When the presented FB is a virgl resource, prefer
  `virtio_gpu_bind_resource_scanout` (bind, no copy) over
  `copy_resource_to_scanout` (readback). Fall back to CPU copy only for shm.
- **Done when:** GL present traces show bind, not per-frame readback, and fps
  rises (see alpine-virgl handoff plan for the cadence target).

### Step 5.4 — Damage-aware present
- **File:** `fb_drm_core_kms.c` (`gpu_drm_mode_dirtyfb`), `fb_scanout.c`.
- **Do:** Pass the `MODE_DIRTYFB` (and atomic `FB_DAMAGE_CLIPS`) rectangles into
  `VIRTIO_GPU_CMD_RESOURCE_FLUSH` as partial rects instead of full-frame.
- **Done when:** partial-damage present transfers only dirty rows.

---

## 8. Phase 6 — Structural cleanup (after the above works)

### Step 6.1 — Unified shmem BO allocator
- **Do:** Extract one shmem-style allocator (alloc pages, mmap, refcount, free)
  used by GEM/dumb (`fb_bo_shmem_dmabuf.c`), virtgpu resources (`virtio_gpu.c`),
  and dma_buf. Remove the triplicated page-array logic. Apply
  `DONTFORK|DONTDUMP` uniformly (prevents the COW-divergence bug in the runtime
  notes).
- **Done when:** all three callers use the shared allocator; tests still pass.

### Step 6.2 — Split large files
- **Do:** Once abstractions land, split `fb_drm_core_kms.c` (~2300 LOC) by
  concern (objects / properties / atomic) and `virtio_gpu.c` (~7400 LOC) by
  concern (transport / resource / 3D). Mechanical move only, no behavior change.
- **Done when:** files build identically with smaller units.

### Step 6.3 — Retire TTM naming
- **Do:** Replace the metadata-only "TTM" naming with the real shmem allocator
  semantics (or clearly comment it as sysmem-only) so the names match behavior.

---

## 9. Quick reference — files you will edit most

| File | What lives there |
|---|---|
| `fb_drm_dispatch.c` | ioctl switch (`gpu_drm_ioctl_handle`) + table |
| `fb_drm_core_kms.c` + KMS fragments | KMS objects, properties, caps, version, blobs, vblank |
| `fb_kms_atomic.c` | atomic commit, page-flip, fb lifecycle, events |
| `fb_bo_shmem_dmabuf.c` | GEM/BO handle table, dma-buf wrapper, reservations |
| `fb_syncobj_prime_virtgpu.c` | syncobj, PRIME, virtgpu resource ioctls |
| `fb_fd_sync.c` | fence/dma-buf custom fd file-ops (poll/mmap/release) |
| `fb_scanout.c` | BO→framebuffer present, virgl scanout, format convert |
| `virtio_gpu.c` + virtio fragments | virtio-gpu transport, 2D/3D/blob commands, async ring |
| `inc/uabi/drm.h` | ioctl numbers + compat structs |
| `inc/dev/drm_core.h` | `drm_core_file`/`drm_core_device` |
| `inc/dev/fb.h` | `FB_GPU_*` private ioctls, fbdev defines |

---

## 10. Dependency order (one-line summary)

```
Phase 0 (audit + drmabitest)
  → Phase 1 (dma_fence)            # everything below needs real fences
    → Phase 2 (per-file GEM, dma_buf)
    → Phase 3 (atomic, blobs, cursor, vblank)   # 3.3 needs 1.x; 3.2 feeds 3.3
      → Phase 4 (virtgpu EXECBUFFER fences, PRIME bridge)  # needs 1.x + 2.x
        → Phase 5 (blob/host-visible, zero-copy, damage)   # perf
          → Phase 6 (cleanup)
```

---

## 11. Hard rules (do not violate)

1. **Caps must equal behavior.** If `GET_CAP` returns 1 for a feature, its ioctl
   must work; if it returns 0, userland will not call it. Never lie.
2. **Fail-closed honesty gates** (DXG/native-present credit, `FB_GPU_BACKEND_F_*`)
   stay as-is. This plan does not touch the Hyper-V DXG present path.
3. **Keep `FB_GPU_*` private ioctls working** — new DRM work is additive.
4. **Test in-guest, not from source reading** — trace the running path and grep
   `/tmp/xv6-debugcon.log` (see `/memories/repo/xv6-os-runtime.md`).
5. **Image rebuild is required** after kernel changes, or the old kernel boots.

---

## 12. Per-phase test checklist

| Phase | In-guest test |
|---|---|
| 0 | `drmabitest` prints a line per ioctl; baseline saved |
| 1 | syncobj signal/wait across threads; exported fd `poll()` wakes on signal; eventfd fires |
| 2 | PRIME export A → import B → same pixels; FLINK/OPEN share; `mmap(dmabuf_fd)` writable |
| 3 | `modetest` runs; `CREATEPROPBLOB` round-trips; atomic `TEST_ONLY` no-ops; cursor moves; out-fence signals |
| 4 | Mesa explicit-sync EXECBUFFER round-trips; render on renderD128 → ADDFB2/flip on card0 shows frame |
| 5 | `GETPARAM(RESOURCE_BLOB)==1`; mappable blob zero-copy; present trace shows bind + partial damage |
| 6 | all prior tests still pass after refactor |

Use `modetest`, `kmscube`, `drm_info` (libdrm) and the existing Wayland/WebKit
GL apps as end-to-end integration tests. Compare trace shape + on-screen output
+ guest framebuffer samples — never counters alone.
