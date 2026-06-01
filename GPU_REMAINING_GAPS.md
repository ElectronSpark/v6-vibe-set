# GPU Plan: virtio-gpu 3D (virgl) under KVM acceleration

Last updated: 2026-06-01

## Mission

The active goal is **hardware-accelerated 3D from the xv6 guest through
virtio-gpu virgl on a KVM/QEMU host** — real OpenGL/GLES rendering executed by
the host's GL stack (virglrenderer) on behalf of the guest, surfaced to guest
userspace (Mesa's `virgl` Gallium driver) and ultimately to the Wayland desktop
and WebKit.

What "genuinely accelerated" means here, stated so success cannot be faked:

1. **Real host GL execution, not software rasterization in the guest.** Draw and
   compute work is encoded into virgl command buffers, submitted over the
   virtio-gpu control queue, and executed by host virglrenderer against a real
   host GL/EGL context. Evidence must be a value or pixel that could only come
   from host GL executing the work (a `glReadPixels` result from a host-rendered
   FBO, a host-signalled virtio-gpu fence), never a counter set in guest
   software.
2. **Invoke the virtio-gpu 3D / virgl transport.** The adapter reaches the guest
   as a `virtio-gpu` PCI device with the `VIRTIO_GPU_F_VIRGL` feature and a
   virgl capset; the in-tree driver (`kernel/kernel/virtio_gpu.c`) drives the
   3D control commands (`CREATE_CONTEXT`, `RESOURCE_CREATE_3D`,
   `TRANSFER_*_HOST_3D`, `SUBMIT_3D`) with real host fences.
3. **Fail closed when virgl is absent.** If the host did not negotiate
   `VIRTIO_GPU_F_VIRGL` or expose a virgl capset (e.g. QEMU without
   `virtio-gpu-gl`, or a host with no GL), every 3D path must reject and the
   backend must fall back to the dumb-buffer / software render node — never
   advertise `OPENGL_SUBMIT` or fabricate a fence.

> The previously-active **Hyper-V GPU-P / DXG (D3D12)** ladder is **complete and
> hardware-proven** (real RTX 4060 compute + offscreen GL in-guest). It is
> preserved as **Appendix A** for reference. The Nouveau / DDA route remains out
> of scope (see end). This plan now centers the **KVM virgl** path, which is the
> portable acceleration route for non-Hyper-V hosts.

## Status summary (virtio-gpu virgl on KVM)

| Capability | State | Notes |
|---|---|---|
| 2D scanout / display (virtio-gpu) | **Working** | `RESOURCE_CREATE_2D` + `SET_SCANOUT` + `TRANSFER_TO_HOST_2D` + flush back the framebuffer; desktop displays |
| virgl capset detection + 3D context | **Kernel code present** | `virtio_gpu_query_capsets`, `CREATE_CONTEXT`, `virtio_gpu_has_virgl()` gate |
| 3D resource / transfer / submit + fence | **Kernel code present** | `RESOURCE_CREATE_3D`, `TRANSFER_*_HOST_3D`, `SUBMIT_3D`, sync + async fences; `/dev/gpu0` `FB_GPU_VIRGL_*` ioctls |
| Kernel virgl ioctl self-test | **To re-verify under KVM** | `user/programs/virgltest` exercises submit / fence / negative paths |
| Mesa `virgl` GL consumer in-guest | **To verify under KVM** | `gldemo` / `mesaglfeature` ran on the DXG `d3d12` Gallium driver; need the same on the `virgl` driver |
| Backend flag `OPENGL_SUBMIT` | **Set when `virtio_gpu_has_virgl()`** | `fb_drm_core_kms.c` `gpu_backend_fill` — but see boot-race blocker B1 |
| WebKit / Skia GL via virgl | **Blocked** | `SkiaGPUWorker` SIGSEGV at GL-context creation under `virtio-gpu-gl` (blocker B2) |
| Host requirement | **Host GL / `/dev/dri` needed** | QEMU `virtio-gpu-gl`; without host GL, virgl falls back to software (`check-gui-accel.sh`) |

2026-06-01 update: xv6 now proves the Mesa Wayland demo is rendering through
`renderer=virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))` on WSL/QEMU and
shows the rendered sphere on screen. Alpine `kmscube` on the same host reaches
about 85 FPS because it page-flips a virgl-rendered resource fullscreen. xv6's
windowed compositor path still cannot claim native GPU-present credit:
`VIRGL_CCMD_RESOURCE_COPY_REGION` from the client resource into the desktop
scanout is accepted by QEMU/virglrenderer, but validation reads back nonblack
source pixels and black destination pixels. The kernel now detects that failed
GPU-side copy and falls back automatically to the readback/CPU-present lane
instead of leaving a blank window. That fallback is correct and visible at about
40-52 FPS; the remaining smoothness gap is a real virgl compositor pass or a
safe full-size KMS-style page-flip/direct-scanout path.

Honesty gate: **keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT == 0` unless a real virgl GL
render *and* an on-screen present are proven in one lineage on a KVM host whose
virgl is backed by real host GL.** A host that silently falls back to software
GL must not flip the flag.

## Verified code state — virtio-gpu virgl (2026-05-30 source audit)

`kernel/kernel/virtio_gpu.c` (standalone TU, ~3.3K lines) is the KVM/QEMU GPU
driver. Independent of a fresh runtime capture, the source establishes:

- **Device + queues: implemented.** virtio-pci discovery, control + cursor
  virtqueues, IRQ completion with a polled fallback, and fence-id tracking
  (`virtio_gpu_intr`, `virtio_gpu_complete_pending_locked`, async submit ring).
- **2D scanout: implemented.** `RESOURCE_CREATE_2D`, `SET_SCANOUT`,
  `TRANSFER_TO_HOST_2D`, and resource flush back the framebuffer (this is what
  the desktop currently displays).
- **virgl 3D: implemented (control path).** `virtio_gpu_query_capsets` finds the
  virgl capset (`virtio_gpu: virgl capset ready id=.. version=.. size=..`);
  `CREATE_CONTEXT`, `RESOURCE_CREATE_3D`, `TRANSFER_TO/FROM_HOST_3D`, and
  `SUBMIT_3D` marshal real commands with optional `VIRTIO_GPU_FLAG_FENCE` and
  sync **or** async fence completion. `virtio_gpu_has_virgl()` returns true only
  when initialized **and** a virgl capset is present.
- **Userspace ABI: present.** `/dev/gpu0` exposes `FB_GPU_VIRGL_SUBMIT`, context
  create/destroy, fence wait, and resource create/destroy/transfer. The in-tree
  self-test `user/programs/virgltest` covers async submit + fence wait, sync
  submit, a forced-failure negative path, and failed-context rejection.
- **Backend advertise: implemented.** When `virtio_gpu_has_virgl()` is true,
  `fb_drm_core_kms.c` `gpu_backend_fill` reports `FB_GPU_BACKEND_VIRGL` and sets
  `FB_GPU_BACKEND_F_VIRGL_OPENGL | FB_GPU_BACKEND_F_OPENGL_SUBMIT` (renderer
  string "OpenGL via virtio-gpu virgl").

### Known blockers (must fix before claiming KVM acceleration)

- **B1 — virgl-ready boot race.** `fbdevinit` prints
  `GPU: virgl unavailable; exposing dumb-buffer DRM only` because
  `virtio_gpu_has_virgl()` is still false when the framebuffer initializes, yet
  the capset becomes ready moments later (`virtio_gpu: virgl capset ready`). Any
  consumer that latches the GPU backend flag once at init can miss virgl, or —
  worse — see `OPENGL_SUBMIT` flip on after a software decision was already
  taken. The backend capability must be evaluated **after** capset
  initialization completes (or be re-queried lazily), not latched early.
- **B2 — WebKit/Skia GL-context crash under `virtio-gpu-gl`.** With
  `webkit_accel=1`, the WebProcess `SkiaGPUWorker` thread takes a fatal NULL
  deref (`cr2=0x28`) at GL/EGL context creation and the page never loads;
  software mode (`webkit_accel=0`) renders fine. Root-cause whether this is the
  guest Mesa `virgl` EGL path, a missing host GL capability, or B1 handing
  WebKit a half-ready backend.

---

## Active plan — virtio 3D (virgl) under KVM (do this top to bottom)

Each item lists **what to build/verify**, **which files**, and the **runtime
evidence** that lets you check the box. Build-success alone is never enough: you
need runtime evidence from a KVM host with virgl, plus a passing fail-closed
negative (a no-virgl image must fall back to the dumb buffer and never advertise
`OPENGL_SUBMIT`).

### Section V0. Host + launch prerequisites (KVM/QEMU)

- [ ] **V0.1 Confirm the host can run virgl.** Host has a usable GL/EGL stack and
  a `/dev/dri` render node; QEMU launches with `virtio-gpu-gl` (or
  `virtio-vga-gl-primary`). `scripts/check-gui-accel.sh` must not warn
  "no host /dev/dri nodes are visible". Record the host GL renderer string.
- [ ] **V0.2 Boot xv6 under KVM with virtio-gpu-gl** via `scripts/run-qemu.sh`
  (`QEMU_GPU=virtio-gpu-gl`) and capture the serial log showing
  `virtio_gpu: virgl capset ready id=.. version=.. size=..`. Fail-closed check:
  a plain `virtio-gpu` (no `-gl`) launch must log `no virgl capset found` and
  `GPU: virgl unavailable; exposing dumb-buffer DRM only`.

### Section V1. Fix the virgl-ready boot race (blocker B1)

- [ ] **V1.1 Evaluate the GPU backend capability after capset init, not before.**
  Files: `kernel/kernel/dev/fb/fb_init_panic.c` (the early
  `virtio_gpu_has_virgl()` print) and `fb_drm_core_kms.c` `gpu_backend_fill`.
  Ensure `virtio_gpu_query_capsets` has completed before any consumer latches
  the backend flag, or make `gpu_backend_fill` reflect late capset readiness.
  Evidence: a boot where the framebuffer no longer prints "virgl unavailable"
  while virgl is in fact present, and `fbstat` reports the `virgl` backend with
  `OPENGL_SUBMIT` consistently across reads.

### Section V2. Kernel virgl ioctl self-test under KVM

- [ ] **V2.1 Run `virgltest` in-guest on the virtio-gpu-gl host.** It must pass
  sync submit, async submit + fence wait, and the two negative paths
  (`FB_GPU_VIRGL_SUBMIT_FORCE_FAIL` rejected; a failed context rejects later
  submits). Evidence: `virgltest: async-submit queued ... final_signaled` past
  `initial_signaled` with a real host-advanced fence, plus the negative paths
  failing closed. A no-virgl image must make `virgltest` fail closed at the open
  or capset gate.

### Section V3. Mesa virgl GL consumer in-guest

- [ ] **V3.1 Offscreen GLES render via the Mesa `virgl` Gallium driver.** Run
  `gldemo` (offscreen GLES2 FBO triangle + `glReadPixels`) with
  `GALLIUM_DRIVER=virgl` and verify the pixels came from host GL, not softpipe.
  Evidence: the renderer string identifies virgl / host GL and the readback
  center/corner pixels match the drawn triangle. (`gldemo`/`mesaglfeature`
  already pass on the DXG `d3d12` Gallium driver; this proves the `virgl` path.)
- [ ] **V3.2 Broaden coverage** with `mesaglfeature` (shader compile/link, VBO,
  texture sampling, FBO depth/stencil, blending, depth-test) on
  `GALLIUM_DRIVER=virgl`, with two-size readback verification.

### Section V4. On-screen present via virgl

- [x] **V4.1 Host-GL render visible on screen, fallback present only.** The
  Mesa Wayland demo renders with the `virgl` Gallium driver and the host NVIDIA
  D3D12 renderer, and the sphere is visible in QEMU. The visible lane currently
  uses the validated readback/CPU-present fallback, so this does **not** grant
  native present credit.
- [ ] **V4.2 Native windowed GPU present.** Implement a real virgl compositor
  pass, or an equally safe full-size page-flip/direct-scanout path. Evidence:
  nonblack destination pixels after GPU-side composition into the desktop
  scanout, no readback/CPU copy in the present path, stable QEMU window size,
  and a sustained post-warmup FPS closer to the Alpine `kmscube` control.

### Section V5. WebKit / Skia GL via virgl (blocker B2)

- [ ] **V5.1 Root-cause and fix the `SkiaGPUWorker` GL-context crash.** Determine
  whether the NULL deref is the guest Mesa `virgl` EGL path, a missing host GL
  capability, or B1 handing WebKit a half-ready backend. Files: the WebKit GPU
  policy/selection in `ports/wayland/src/desktop.c`, the Mesa virgl EGL port,
  and the backend flag from V1. Evidence: `webkit_accel=1` loads a page and
  renders a GPU-composited frame without `SkiaGPUWorker` SIGSEGV; the
  fail-closed software fallback still works when virgl is absent.

### Section V6. Backend flag + consumers (gated on V1–V4)

- [ ] **V6.1 Keep `OPENGL_SUBMIT` honest.** Advertise it only when a real virgl
  GL submit + present lineage is proven (V3 render + V4 present). Until then,
  gate it so consumers do not select the accelerated path on a host that
  silently falls back to software GL. Evidence: `fbstat` /
  `scripts/check-gui-accel.sh` report virgl only when host GL is real; otherwise
  the dumb-buffer render node.

### virtio virgl dependency graph

```
V0 host GL + virtio-gpu-gl launch
  -> V1 fix virgl-ready boot race (B1)
       -> V2 kernel virgl ioctl self-test (virgltest)
            -> V3 Mesa virgl GL consumer (gldemo/mesaglfeature)
                 -> V4 on-screen present via virgl
                      -> V5 WebKit/Skia GL via virgl (B2)
                           -> V6 honest backend flag + consumers
```

---

## Appendix A: Hyper-V GPU-P / DXG ladder (COMPLETE — reference)

The DXG / D3D12 ladder below is **complete and hardware-proven** on a Hyper-V
GPU-P host (real RTX 4060: in-guest `d3d12probe` compute PASS and offscreen GLES
via Mesa's `d3d12` Gallium driver). It is retained for reference and for any
future Hyper-V GPU-P host; it is **not** the active KVM virgl plan above. The
same honesty gates apply (never report a synthetic fence/readback as success).

### Section G0. Host GPU-P Prerequisites (Windows host, not guest code)

- [x] **G0.1 Confirm and pin the GPU-P partition. — DONE 2026-05-29.**
  - `Get-VMGpuPartitionAdapter -VMName xv6-os-hyperv` shows the `GPUPARAV`
    InstancePath for `VEN_10DE&DEV_28A0` (already true on this host).
  - Record `MinPartitionVRAM/MaxPartitionVRAM`, compute, and encode allotments.
  - Evidence (captured 2026-05-29): adapter record is
    `INSTANCE=\\?\PCI#VEN_10DE&DEV_28A0&SUBSYS_13B61462&REV_A1#4&2961dbb7&0&0008#{064092b3-625e-43bf-9eb5-dc845897dd59}\GPUPARAV`
    with `VM_STATE=Running`. (The VRAM/compute/encode allotment properties are
    not surfaced by name on this Windows build — the `GPUPARAV` partition path
    plus a Running VM is the partition confirmation.) The VM boots with the
    partition attached and the in-guest `dxgprobe`/`d3d12probe` runs below
    enumerate the partitioned RTX 4060.
- [x] **G0.2 Stage the host-driver user-mode components the guest will need.**
  - DONE 2026-05-29. GPU-P relies on host-driver UMD/KMD libraries surfaced
    into the guest (the WSLg `/usr/lib/wsl/lib` model: `libd3d12.so`,
    `libdxcore.so`, the NVIDIA UMD). `scripts/stage-gpup-umd.sh` copies the
    runtime from the host `/usr/lib/wsl/lib` into
    `rootfs-overlay/usr/lib/wsl/lib` (gitignored proprietary blobs), writes
    `/etc/ld.so.conf.d/gpup-wsl.conf` + `/etc/profile.d/gpup-d3d12.sh`, and
    stages `/bin/d3d12probe`.
  - Evidence: `stage-gpup-umd.sh` lists the 5 core libs staged; the userspace
    stack is host-validated (see critical-path step 2 — `d3d12probe` PASS on the
    RTX 4060). Remaining: list them from the *running xv6 guest* and confirm the
    same PASS there. If a future host lacks the runtime, the script prints
    `missing` and exits non-zero — fail closed, do not proceed to G3.

### Section G1. Bring up the DXG VMBus channel and adapter — KERNEL CODE PRESENT

> Audit 2026-05-29: implemented in the kernel and forwarded to the host. These
> items flip to `[x]` only once a GPU-P hardware boot logs the real negotiated
> version + real adapter LUID. **Both captured in-guest 2026-05-29** — see the
> `dxgprobe --qai-admission` evidence below (`tmp/dxgprobe-inguest-qai.log`).

- [x] **G1.1 Open the DXG VMBus channel and negotiate the interface version. — DONE (in-guest 2026-05-29).**
  - Files: `kernel/kernel/dev/hyperv/hyperv_dxg_device.c`,
    `kernel/kernel/dev/hyperv/hyperv_defs_state.c` (`HV_DXG_*`,
    `HV_DXGK_VMBCOMMAND_*`).
  - Verify the channel opens and `HV_DXG_VMBUS_INTERFACE_VERSION` (40) is
    accepted by the host (fall back to OLD/last-compatible as the wire allows).
  - Evidence: the in-guest `dxgprobe --qai-admission` run could not have
    enumerated the host adapter at all unless the DXG VMBus channel opened and
    the interface version was accepted — it returned a real partitioned
    adapter: `qai_admission: enum adapters2 layout=list-first count=1` then
    `selected_vendor=0x10de selected_device=0x28a0
    selected_name=NVIDIA_GeForce_RTX_4060_Laptop_GPU hardware_rc=0 status=PASS`.
    The channel fails closed when the GPU-P endpoint is absent (no-GPU image
    reports `adapter_present=0`).
- [x] **G1.2 Open the adapter and read real adapter identity. — DONE (in-guest 2026-05-29).**
  - Issue `HV_DXGK_VMBCOMMAND_OPENADAPTER` + `QUERYADAPTERINFO`; capture the
    real adapter LUID, driver/UMD version, and feature levels from the host.
  - Evidence (in-guest, `tmp/dxgprobe-inguest-qai.log`): both the enum2 and
    enum3 routes selected the **real RTX 4060 LUID** and
    `OPENADAPTERFROMLUID` succeeded from inside the guest —
    `qai_admission_route route=direct-openadapterfromluid open_adapter=PASS
    luid=3:4f94fc65 handle=0x40000081` and
    `direct_luid=3:4f94fc65 list_luid=3:4f94fc65 direct_list_luid_match=1`
    (`vendor=0x10de device=0x28a0`, name `NVIDIA GeForce RTX 4060 Laptop GPU`).
    `QUERYADAPTERINFO` type 27 round-trips (`rc=0 status=PASS`). On a no-GPU
    image it reports `adapter_present=0 status=PASS_FAILCLOSED`.

### Section G2. Process / device / context lifetime

- [x] **G2.1 Create the DXG process, device, and context objects. — DONE (in-guest 2026-05-29).**
  - Files: `kernel/kernel/dev/hyperv/hyperv_dxg_handle_manager.c`,
    `hyperv_dxg_ioctls.c`; ABI `kernel/kernel/inc/uabi/d3dkmthk.h`. Model
    ownership on WSL `dxgkrnl` (`dxgprocess`/`dxgdevice`/`dxgcontext`).
  - Commands: `CREATEPROCESS`, `CREATEDEVICE`, `CREATECONTEXTVIRTUAL`.
  - Evidence (in-guest, `tmp/dxgprobe-inguest-qai.log`): real host handles came
    back and round-tripped through the guest process handle table —
    `selected_handle=0x40000001` (enum2), `0x40000041` (enum3),
    `handle=0x40000081` (direct OPENADAPTERFROMLUID), and `handle=0x400000c1`
    (create-adapter-list). The keystone `d3d12probe` PASS additionally created
    the device + context and submitted to host device `0x40000000` (see G4.1),
    proving the full process/device/context lifetime against the real GPU. The
    destroy path is clean (no leaked host objects reported).

### Section G3. Real allocations, residency, and GPUVA

- [x] **G3.1 Create allocations and make them resident on the real GPU. — DONE (in-guest 2026-05-29).**
  - Commands: `CREATEALLOCATION`, `CREATEPAGINGQUEUE`, `MAKERESIDENT`,
    `RESERVE/MAP/FREEGPUVIRTUALADDRESS`.
  - Evidence: the in-guest `d3d12probe` run created host-backed allocations and
    mapped them; the serial log shows real `hyperv-dxg: lock2 host ...
    forwarded` and `unlock2 host ... forwarded=1` transitions against the host
    device `0x40000000` (RTX 4060), not synthetic ones
    (`tmp/d3d12probe-inguest-pass.log`).

### Section G4. Real submission + hardware fence (keystone proof) — HARDWARE-PROVEN IN-GUEST 2026-05-29

> Audit 2026-05-29: `LX_DXSUBMITCOMMAND(TOHWQUEUE)`, the wait/signal sync-object
> ioctls, and `_D3DDDI_MONITORED_FENCE` all forward real packets to the host;
> the kernel never fabricates a fence. The keystone client `d3d12probe` linking
> the staged NVIDIA UMD now PASSES **inside the xv6 GPU-P guest** against the
> real RTX 4060 — the GPU genuinely executed the submitted copy and signalled
> the monitored fence.

- [x] **G4.1 Submit a real command and observe a real monitored-fence signal. — DONE (in-guest 2026-05-29).**
  - Commands: `CREATESYNCOBJECT`, `SUBMITCOMMAND`,
    `SIGNALSYNCOBJECT` / `WAITFORSYNCOBJECTFROMCPU`.
  - The command buffer must be produced by a real UMD, not by hand. **The
    keystone client now exists: `user/programs/d3d12probe/d3d12probe.cpp`.** It
    links the staged GPU-PV runtime (`libd3d12`/`libdxcore` → NVIDIA UMD),
    records a GPU copy-engine command buffer (UPLOAD→DEFAULT→READBACK), submits
    it, waits on a GPU-signalled fence, and verifies the GPU-copied bytes.
  - Host status (2026-05-29): PASS on the real RTX 4060 on the WSL host.
  - **In-guest status (2026-05-29): PASS.** Booting `xv6-os-hyperv` (RTX 4060
    GPU-P) and running `/bin/d3d12probe` printed `D3D12PROBE: adapter[0] "NVIDIA
    GeForce RTX 4060 Laptop GPU" hw=1 vram=7957MiB` then `D3D12PROBE: PASS GPU
    copied 16384 bytes, fence signalled`. SUBMITCOMMAND + monitored fence routed
    through the xv6 DXG forwarding path (serial log shows create-sync / lock2 /
    `unlock2 ... forwarded=1` to host device `0x40000000`). Evidence:
    `tmp/d3d12probe-inguest-pass.log`.
  - **This proves the host GPU is genuinely doing work driven from xv6.**

### Section G5. Present / display handoff — INVESTIGATION COMPLETE (2026-05-29)

- [x] **G5.1 Investigate host-composition / cross-VM present options. — DONE (verdict: no native present on a plain GPU-P guest).**
  - **Findings (from source audit + WSL/WDDM knowledge):**
    1. **WSL `dxgkrnl` has no scanout/display-bind ioctl or VMBus sender.**
       Confirmed again from `fb_dxg_present.c`: the display-bind provider
       (`fb_dxg_present_provider_submit_display_bind` →
       `hyperv_dxg_display_bind_submit`) is structurally complete but stays
       fail-closed on three gates that no host ABI satisfies: `no_host_abi=1`
       (host resource-scanout-bind ABI absent), `no_sender=1` (no GPU-P/WSLg
       display-bind packet sender), `no_completion=1` (no display-completion
       demux). These are host-side absences, not guest bugs.
    2. **`SHAREOBJECTWITHHOST` / `CREATENTSHAREDOBJECT` exist but are for compute
       resource sharing, not scanout.** They hand an NT-shared resource to
       *another DXG process*, not to a host display compositor. They do not
       constitute a present path.
    3. **WSLg shows frames via a host-side Weston/RDP-RAIL agent**, fed through
       WSLg-specific plumbing (the `/mnt/wslg` socket + RDP). A plain Hyper-V
       GPU-P guest does **not** have that host compositor agent, and adding one
       is explicitly disallowed by the honesty gate ("do not add custom host
       tools as the acceptance path").
    4. **Blit-present to the synthvid console** would require first reading the
       GPU-rendered allocation back to guest memory and memcpy'ing it to the
       Hyper-V synthetic framebuffer. The render would be real GPU, but with no
       native present ABI the "present" is a CPU readback+blit, which the
       honesty gate gives **zero display credit**.
  - **Verdict: native display/scanout from a plain Hyper-V GPU-P guest is NOT
    reachable** without either WSLg's host agent or a custom host tool (both
    out of scope). **Scope is therefore compute / offscreen only.** Display
    credit is abandoned on this host; `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (which
    specifically promises a *presented* GL frame) stays `0` permanently here.
  - Display-bind code stays fail-closed exactly as it is — do not weaken its
    gates to manufacture a present.

- [x] **G5.2 — N/A on this host.** No present transport is reachable (see G5.1),
  so no present code is written. The forward path is compute/offscreen (G3/G4)
  validated by a real client, surfaced via a compute-capability bit (G6.1),
  not via the OpenGL-submit/present flag.

### Section G6. Backend flag + consumers (gated on G4, and G5 if present is in scope)

- [x] **G6.1 Decide what `FB_GPU_BACKEND_F_OPENGL_SUBMIT` means for GPU-P. — DONE (2026-05-29, compute-only).**
  - G5 concluded present is unreachable on a plain GPU-P guest, so a separate
    honest capability bit was added: `FB_GPU_BACKEND_F_GPU_COMPUTE` (0x0400) in
    `kernel/kernel/inc/dev/fb.h` means "real GPU compute (D3D12/D3DKMT submit +
    hardware monitored-fence) is reachable", proven in-guest by `d3d12probe`.
    `FB_GPU_BACKEND_F_OPENGL_SUBMIT` stays `0` for the DXG backend (it
    specifically promises a *presented* GL frame, which G5 cannot satisfy here).
  - File: `kernel/kernel/dev/fb/fb_drm_core_kms.c` (`gpu_backend_fill`): the bit
    is set only when `hyperv_dxg_d3dkmt_ready()` is true at runtime (reachable
    D3DKMT path), never on build-success alone.
  - Evidence: `fbstat`/`dxgprobe` report `GPU_COMPUTE` honestly; `OPENGL_SUBMIT`
    remains `0`.
- [x] **G6.2 Route a real consumer (Mesa D3D12 / dzn, or a compute client). — DONE (2026-05-29, compute client).**
  - The real consumer is `user/programs/d3d12probe` linking the staged GPU-PV
    runtime (NVIDIA D3D12 UMD). It submitted a real GPU copy-engine command
    buffer that completed on the host RTX 4060 via the G1–G4 contract (in-guest
    PASS, `tmp/d3d12probe-inguest-pass.log`). On a no-GPU image it fails closed
    (`D3D12PROBE: ERROR no hardware D3D12 adapter`, non-zero exit).
  - `dxgprobe` now decodes and reports the capability honestly:
    `qai_admission_backend ... backend_gpu_compute=<0|1>`
    (`user/programs/dxgprobe/dxgprobe.c` `probe_backend_opengl_submit_flag`),
    while `backend_opengl_submit` stays `0`. Mesa `dzn` is not required (the
    native NVIDIA UMD builds real command buffers).
- [x] **G6.3 Broaden the OpenGL/GLES consumer coverage on the real GPU. — DONE (in-guest 2026-05-29).**
  - Beyond the compute client, Mesa's **d3d12 Gallium driver** runs GLES2/3 on
    the real RTX 4060 via the staged GPU-PV UMD (libdxcore/libd3d12 + NVIDIA UMD)
    over `/dev/dxg` — **no `/dev/dri` render node**. Three GL consumers are now
    proven in-guest, in increasing breadth:
    - `gldemo` — offscreen GLES2 FBO triangle + `glReadPixels` verify.
    - `mesawlegl` — on-screen spherical-poly demo (GPU render + blit-present).
    - `mesaglfeature` — **broad GLES2 feature probe**: shader compile/link, VBO,
      texture sampling, FBO with `DEPTH24_STENCIL8`, viewport/scissor, blending
      (`GL_ONE,GL_ONE`), and depth-test, with two-size readback verification.
  - Fix that enabled the broad probe in-guest: `mesaglfeature` `main()` had
    forced `MESA_LOADER_DRIVER_OVERRIDE=softpipe` + `LIBGL_ALWAYS_SOFTWARE=1`
    unconditionally, which overrode the shell's `GALLIUM_DRIVER=d3d12` and
    fail-closed in `require_d3d12_renderer()`. It now branches on a requested
    GPU path (env `XV6_MESAGLFEATURE_REQUIRE_D3D12` / `GALLIUM_DRIVER=d3d12` /
    `MESA_LOADER_DRIVER_OVERRIDE=d3d12`): GPU mode leaves the software override
    unset; otherwise it keeps the softpipe software smoke default.
  - Evidence (in-guest, bare `/bin/mesaglfeature`, no env prefix):
    `mode=gpu-d3d12 (hardware render)`,
    `renderer=D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)` OpenGL ES 3.1,
    `D3D12 renderer confirmed`, `pass size=32x32 center=80,160,240,255
    corner=0,0,0,255`, `pass size=64x32 ...`, `mesaglfeature: ok`.
    `tmp/mesaglfeature-inguest.log` (host smoke: `tmp/mglf-host.log`).
  - Honesty unchanged: this is **offscreen GPU render + CPU readback**, not a
    native present. `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (which promises a
    *presented* GL frame) stays `0` on this host (G5).

### GPU-P / DXG dependency graph

```
G0 host GPU-P + UMD staged
  -> G1 DXG channel + adapter identity
       -> G2 process/device/context
            -> G3 allocations + residency + GPUVA
                 -> G4 real submit + HW fence  (keystone real-GPU proof)
                      -> G5 present (INVESTIGATE first; may be compute-only)
                           -> G6 backend capability flag + real consumer
```

---

## Out of scope: Nouveau / DDA (dropped 2026-05-29)

The DDA + Nouveau-on-raw-silicon route is **abandoned as a goal** on this host
and is no longer planned work. It is physically unreachable here (GPU-P exposes
no MMIO BARs; true DDA fails with `0xC035001E` on this consumer RTX 4060 Laptop
GPU). The scaffold files (`kernel/kernel/dev/fb/fb_nouveau.c` and the
`nouveau_*` stats) stay in the tree **fail-closed** for a hypothetical
DDA-capable host, but no further Nouveau bring-up is on this plan. Do not spend
effort implementing Nouveau MMIO/VRAM/firmware/channel/submit paths.

## Next focus: virtio virgl bring-up + WebKit

The active engineering goal is the **KVM virgl ladder above (V0–V6)**: confirm
host GL + `virtio-gpu-gl`, fix the virgl-ready boot race (B1), prove the kernel
virgl ioctls and a Mesa `virgl` GL consumer in-guest, then reach an on-screen
present and unblock WebKit/Skia GL (B2). The browser path — getting **WebKit** to
load a functional YouTube and play video smoothly — depends on V5 for the
accelerated route; the software route already works and is tracked in
`YOUTUBE_KERNEL_GAP_REPORT.md`.
