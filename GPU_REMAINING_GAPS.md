# GPU Remaining Gaps

Last updated: 2026-05-21

This file is now scoped to work that is still missing. Completed baseline
capabilities were moved into the skill docs, mainly
`.github/skills/xv6-debug-gui-runtime/SKILL.md` and
`.github/skills/xv6-os-debugging/SKILL.md`.

## Source Audit

Checked against the current dirty tree on May 17, 2026:

- `kernel/kernel/dev/hyperv_input.c`
- `kernel/kernel/dev/fb.c`
- `kernel/kernel/inc/uabi/d3dkmthk.h`
- `user/programs/dxgprobe/dxgprobe.c`
- `ports/wayland/src/desktop.c`
- `ports/wayland/src/desktop_clients.inc`
- `ports/wayland/src/mesawlegl.c`
- `ports/wayland/src/mesademo.c`
- `ports/wayland/src/gl_fps_overlay.c`
- `ports/wayland/src/wlcomp_dmabuf.inc`
- `ports/wayland/src/wlcomp_buffer_shm.inc`
- `ports/mesa/src/src/gallium/frontends/dri/drisw.c`
- `ports/mesa/src/src/gallium/drivers/d3d12/d3d12_resource.cpp`
- `scripts/hyperv-dxg-validate.sh`
- `scripts/hyperv-gpu-stress.sh`
- `scripts/hyperv-webkit-gpu-validate.sh`
- `scripts/hyperv-3d-visual-check.sh`

The current code already has substantial Hyper-V DXG and general GPU substrate
coverage: DXG transport, D3DKMT readiness reporting, stable `dxgprobe`
coverage, existing-sysmem pin tracking, late allocation unwind, NT resource and
sync-object fd export/open, render-node/GBM/PRIME-style BO fd support, linear
ARGB/XRGB/NV12 linux-dmabuf import, explicit-sync release objects, compositor
GPU BO present/direct-scanout paths, display completion accounting, virgl
OpenGL-submit on KVM, WebKit fallback gating, and 3D-demo visual/FPS overlay
smokes. Those are no longer listed as remaining gaps here.

## Current Honest State

- Hyper-V can expose `/dev/dri/renderD128`, DXG transport, and D3DKMT readiness.
- Hyper-V must still report `FB_GPU_BACKEND_F_OPENGL_SUBMIT == 0`.
  `fb.c` currently sets `FB_GPU_BACKEND_F_OPENGL_SUBMIT` only for virgl.
  Keep that invariant until the non-readback D3D12 present path and 480p
  >60 FPS demo are validated.
- Latest validator evidence in
  `/tmp/xv6-hyperv-build/hyperv-dxg-validate.log` shows the Mesa D3D12 renderer
  now selects successfully: `mesaglfeature: D3D12 renderer confirmed
  renderer=D3D12 (Microsoft Hyper-V GPU-PV Render Driver)`, both 32x32 and
  64x32 draw/readback cases pass, and `mesaglfeature: ok` is present. The WDDM
  trace compare also passes against the same-adapter NVIDIA WSL trace. Mesa-side
  adapter identity reporting still has a shifted vendor/device value until the
  app/Mesa worker fixes it; do not use that cosmetic mismatch to mark native
  present or WebKit complete. These results close the renderer/payload parity
  gate, but not native present.
- Hyper-V vPCI now reaches the WSL2 DXG PCI capability path. Central evidence
  shows the vPCI offer/protocol/config window working:
  `hyperv_pci_bus=cfg:1 ... d0:1/1/0/0 query:1/1/0
  rel:2/56/1/28/1 child:1 ... first:1414:008e ... bdf:255:0:1`.
  The WSL2 guestcaps/LUID path is also verified:
  `dxg_pci_guestcaps=... writes:1 found:1 verified:1 source:2 ...
  readback:<LUID low> ret:0 bdf:ff0001`, and
  `dxg_identity=pci_source:hyperv-vpci guestcaps:0/1/1 ...` reports
  `host_vgpu_luid == pci_luid`.
- Basic pure-C NT resource sharing works, and Wave25 restores pure-C
  WSL-natural sync import. Historical evidence covered `share_resource_nt ok`,
  `query_resource_nt ok`, `open_resource_nt ok`, and `share_sync_nt ok`; Wave24
  exposed a pure-C OPENSYNC `STATUS_INVALID_PARAMETER` blocker, and Wave25
  fixed it for default/WSL-natural and `0x13` pure-C sync-open variants. Invalid
  open flags `0x483` still fail and are tracked separately from valid sync
  import.
- Current top blocker moved past resource NT export: Wave38 strict-present
  evidence shows runtime `CreateSharedHandle(resource)` succeeds and resource
  `OpenSharedHandle` succeeds. `d3d12sharedsmoke --runtime --require-present`
  now fails at `stage=present-failed-before-success`, with direct D3D12
  `OpenSharedHandle(fence)` still returning `0xffffffff80070057`/`EINVAL` and
  no `/tmp/wlcomp-d3d12-present` artifact in that run. Kernel diagnostics show
  NT-share success and runtime shared allocation metadata
  (`dxg_ntshared_wsl_exact=ext32_zero_luid_natural:1/a0`,
  `dxg_ntshared_envelope ... ret:0/status:0x40000180/raw:0x40000180`,
  `dxg_d3d12_shared_alloc=seen:1 ... runtime:264 res_priv:0 alloc_priv:594
  out_priv:594`). This keeps native Wayland present, FPS, and WebKit
  acceleration unchecked.
- Wave39 strict-present evidence moves the compositor path further:
  `/tmp/xv6-hyperv-build/wave39-d3d12-require-present-short.log` reaches the
  real D3D12 runtime path, passes resource export/open, passes fence export,
  bypasses direct D3D12 fence open as expected, and passes pure-DXG syncfile
  acquire import. `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log`
  proves Wayland commit, same-LUID compositor resource/fence import, fence
  target `current=1`, accepted D3D12 buffer protocol, compositor-owned D3D12
  GPU copy completion with no CPU/readback, and fail-closed present-source
  rejection. Native display handoff remains missing:
  `d3d12_display_handoff_implemented=0`, commit `errno=95`,
  `d3d12_display_handoff_requires_kernel_host_protocol=1`, and
  `backend_opengl_submit 0`.
- Wave40 command-runner evidence: the image was rebuilt/deployed after the
  shell prefix fix, and `FOO=bar fbstat` executed `fbstat` in the guest. This
  closes the prefix-command execution concern for validation launches.
- Wave41 build/deploy succeeded after the kernel/user/ports changes.
  `dxgprobe --residency-batch-validate` now produces
  `residency_batch_matrix ... requested_count=2 requested_flags=0x1
  actual_count=2 actual_flags=0x1 rc=259 status=PASS`, and `/dev/dxg` reports
  `dxg_makeresident_shape=cmd:52 wsl_cmd:52 result:24 actual:24 owner_ok:2
  tracked:2 order:1`. This is the current runtime proof for the
  MakeResident `count=2 flags=0x1` matrix and diagnostics.
- Wave40 strict-present evidence repeats the Wave39 import/acquire/copy lane on
  the rebuilt image: resource export/open PASS, fence export PASS, DXG syncfile
  acquire import PASS, Wayland commit, same-LUID resource/fence import,
  compositor-owned D3D12 GPU copy, then fail-closed present-source commit with
  `errno=95` and `backend_opengl_submit 0`. Kernel diagnostics now name the
  missing host ABI as `dxg-resource-scanout-bind` and keep fake display
  counters/OpenGL-submit enablement disabled.
- Wave41 strict-present still fail-closes at the named missing host ABI:
  kernel log `fb: dxg present-source commit blocked: missing host
  ABI=dxg-resource-scanout-bind ... 256x256 pitch:1024 fmt:0x34325241`, evidence
  `d3d12_present_errno=95`, `present_id=0`, `completed=0`,
  callbacks/releases blocked, no readback, and `backend_opengl_submit 0`.
- Wave42 validator evidence: `d3d12sharedsmoke --present-evidence-selftest`
  passes positive and negative cases, including rejection of
  import-only/copy-only/fail-closed/incomplete-present-id evidence. The
  `dxgprobe --fb-existing-sysmem` lane remains staging/fail-closed:
  pin proof passes, but `fb_bo_present_cpu_blit=1`,
  `native_host_display_handoff=0`, and `d3d12_copy_possible=0`; `/dev/dxg`
  shows existing-sysmem pin/set OK for FB BO pages while
  `dxg_existing_sysmem_target=pfnmap_pages:0 ... vram:0`, so the direct
  synthvid/VRAM path remains untested.
- Wave43 direct scanout existing-sysmem evidence closes only the VRAM mapping
  validation slice: `dxgprobe --scanout-existing-sysmem` maps scanout VA
  `0x7fffff2a0000`, size `3145728`, GPA `0xc0000000`, 1024x768 pitch 4096,
  creates allocation/resource, proves `va_align64k=1`, `pin_proof=PASS`,
  and `/dev/dxg` reports `dxg_existing_sysmem_target=pfnmap_pages:768
  pfnmap_ok:1 vram:1 vram_gpa:0xc0000000 vram_size:8388608`. It still reports
  `native_host_display_handoff=0`, `d3d12_copy_possible=0`, and
  `status=FAIL_CLOSED_NO_D3D12_COPY`.
- Wave40 source guardrails: the compositor now releases/callbacks only after
  register success, commit success, nonzero `present_id`, and
  `completed >= present_id`; the FPS/WebKit scripts now use
  `effective_presented_fps` based on native/display/visual presented evidence.
  These are source/validator hardening items, not proof of native display
  handoff, >60 FPS, or WebKit acceleration.
- The same validator log shows NV12 linux-dmabuf explicit-sync now passes
  through xv6-gbm: `dmabufsmoke` uses `backend=xv6-gbm`, logs two-plane NV12
  metadata, presents `format=NV12 planes=2`, and receives an explicit-sync
  release. This is dmabuf/compositor substrate evidence, not D3D12 native
  present evidence.
- WebKit acceleration and `webkit_dmabuf=1` are correctly gated on
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT`; this is a safety gate, not completion of
  the accelerated WebKit surface contract.
- Fresh WSL-comparison work found one earlier-than-export active slice on the
  current dirty tree: after switching the v40 OpenAdapter path to WSL's zero
  guest-LUID wire layout and removing the non-WSL global `LX_ISFEATUREENABLED`
  fallback, the `d3d12sharedsmoke --device-admission-matrix` probe reached
  DXCore adapter selection but `D3D12CreateDevice` failed with
  `0xffffffff887a0004` before any kernel `LX_DXCREATEDEVICE`. LD_DEBUG showed
  `libd3d12core.so` trying the broken path `ib/libnvwgf2umx.so`. The current
  source now rewrites NVIDIA `KMTQAITYPE_UMDRIVERNAME` to the same Windows
  DriverStore-style name WSL exposes and stages
  `/usr/lib/wsl/drivers/nvmi.inf_amd64_9a9d1548c06ce277/libnvwgf2umx.so`; this
  still needs live validation before the work can return to the NT-export
  blocker.
- Historical sixth-wave note: after QAI admission fixes, clean runtime-first
  validation had reached Phase 1 but failed resource NT export. Wave38
  supersedes that state for the resource fd path: resource NT export and open
  now succeed, while native-present handoff remains open.

## Priority Plan: Linux GPU Driver Interfaces First

The active implementation order is changed as of 2026-05-21.  Before returning
to Hyper-V native-present closure, build the missing Linux-style GPU driver
substrate that an in-kernel Nouveau port expects: DRM core, GEM, TTM, KMS,
DMA-buf/PRIME, sync objects/fences, PCI/runtime driver binding, then the
Nouveau DRM driver itself.  Linux UAPI headers and the Linux DRM/Nouveau driver
model are the reference shape; xv6 may keep smaller internals, but the exposed
contracts should be traceably compatible.

### Linux DRM Core And UAPI Contract

- [x] Split DRM ioctl definitions and structs out of `fb.c` into shared UAPI
  headers (`drm.h`, `drm_mode.h`, `drm_fourcc.h`, and `nouveau_drm.h` subsets)
  so kernel code, libdrm, Mesa, and pure-C validators compile against one ABI.
  Initial shared `uabi/drm.h` is consumed by `fb.c`, `drmiftest`, and
  `drmprimeprobe`; validated on focused Hyper-V with OpenGL-submit still gated.
- [ ] Add a DRM core device layer independent of framebuffer fallback:
  `drm_device`, `drm_driver`, `drm_file`, per-open magic/auth/master state,
  render-vs-primary node policy, and driver-private callbacks.
- [ ] Move `/dev/dri/card0` and `/dev/dri/renderD128` dispatch from the fb
  facade into the DRM core while keeping `/dev/gpu0` and `/dev/fb0`
  compatibility as wrappers.
- [ ] Implement Linux-compatible ioctl dispatch tables with common DRM core
  commands, permission checks, driver-private command ranges, copyin/copyout
  validation, and unknown-command diagnostics.
- [ ] Add `/proc` or `/dev` diagnostics proving node type, driver name,
  feature flags, open-file counts, master/auth state, GEM object counts, TTM
  memory usage, KMS object counts, and driver-private ioctl counters.
- [x] Add a pure-C `drmiftest` validator covering version, unique, caps,
  set-client-cap, get-client/stats, magic/auth, master/drop-master, primary
  and render-node permission differences, and unknown ioctl rejection.
  It now also covers primary-only KMS framebuffer/page-flip/atomic scaffolding
  and validates Hyper-V remains `backend_opengl_submit 0`.

### GEM, PRIME, DMA-Buf, Fences, And Sync Objects

- [ ] Promote the current BO/fd objects into a GEM object layer with per-file
  handle tables, global object references, mmap offsets, close-on-file-release
  cleanup, and no stale-handle access across render opens.
- [ ] Keep existing FB/GBM ioctls backed by GEM rather than parallel BO logic.
- [ ] Implement GEM dumb buffer create/map/destroy on top of GEM with stable
  mmap offsets and Linux-compatible size, pitch, bpp, and alignment semantics.
- [ ] Implement PRIME export/import through shared GEM references with
  `drmPrimeHandleToFD`/`drmPrimeFDToHandle` semantics and fd lifetime tests.
- [ ] Add dma-buf style metadata for format, modifier, plane count, offsets,
  strides, and implicit/explicit fence attachment; keep existing Wayland
  linux-dmabuf import wired to that metadata.
- [x] Implement DRM syncobj and timeline syncobj UAPI enough for Mesa/Nouveau:
  create, destroy, handle-to-fd, fd-to-handle, wait, reset, signal, timeline
  wait/signal, and diagnostics.
  Wave62 evidence: focused Hyper-V `drmiftest; drmprimeprobe; fbstat` passes
  binary syncobj create/wait/reset/signal, fd export/import after original
  handle destruction, timeline signal/query/wait, cleanup with
  `syncobj_live 0`, and keeps `backend_opengl_submit 0`.
- [x] Add pure-C validators for GEM lifetime, PRIME cross-open sharing,
  mmap-offset isolation, syncobj binary/timeline behavior, and fence fd poll.
  Syncobj binary/timeline coverage is now included in `drmiftest`; GEM mmap
  offset isolation is also validated after Wave62's follow-up (`MAP_DUMB`
  succeeds only for the owning DRM file and rejects foreign/stale handles).
  Focused Hyper-V validation also ran `drmprimeprobe` for PRIME export/import,
  stale handle, imported handle, and closed fd rejection, plus `gpubuftest 1`
  for FB/GBM BO lifetime, BO fd import, fence fd poll readiness, closed-fd
  rejection, and pending-fence nonreadiness while keeping
  `backend_opengl_submit 0`.

### TTM Memory Manager

- [ ] Add a TTM-like memory manager with resource managers for system memory,
  GART/TT, VRAM, and stolen/scanout memory, even when a backend maps some
  domains onto the same physical allocator initially.
- [ ] Add buffer-object placement policy, validation, eviction, pinning,
  reservation/ww-mutex equivalent rules, and LRU accounting.
- [ ] Add TT page population/unpopulation with page pinning, scatter-gather
  metadata, DMA address placeholders, and cleanup-time leak diagnostics.
- [ ] Add move paths for system-to-VRAM, VRAM-to-system, and no-op same-domain
  moves with explicit fail-closed diagnostics where hardware copy engines are
  not wired yet.
- [ ] Add TTM validators for placement selection, domain migration,
  pin/unpin, eviction ordering, reservation deadlock avoidance, and leak-free
  file/process teardown.

### KMS, Modesetting, Planes, And Atomic State

- [ ] Replace hardcoded single-mode DRM KMS responses with KMS object models:
  mode_config, connectors, encoders, CRTCs, planes, framebuffers, properties,
  blobs, and leases/placeholders where Linux clients expect them.
- [ ] Implement `GETRESOURCES`, `GETCONNECTOR`, `GETENCODER`, `GETCRTC`,
  `GETPLANERESOURCES`, `GETPLANE`, `GETPROPERTY`, `GETPROPBLOB`,
  `ADDFB2`, `RMFB`, `SETCRTC`, page-flip, and vblank/event delivery.
  Wave62 adds a primary plane object: `GETPLANERESOURCES` now returns one
  plane, `GETPLANE` reports XRGB8888/ARGB8888 support and render nodes reject
  it. `drmiftest` validates this on focused Hyper-V with
  `backend_opengl_submit 0`. Property/blob enumeration and vblank/event
  delivery remain open.
- [ ] Add atomic modesetting objects and ioctls enough for modern Mesa/Wayland
  clients: object property enumeration, atomic check, atomic commit, out-fence,
  nonblocking commit rejection or completion.
- [ ] Wire KMS scanout to existing framebuffer/virtio/synthvid paths without
  claiming Hyper-V OpenGL submit until native present and FPS gates pass.
- [ ] Add pure-C KMS validators for resource enumeration, dumb-buffer
  framebuffer creation, page flip event delivery, atomic check/commit, and
  invalid-object rejection.

### PCI Driver Model And Nouveau Port

- [ ] Add a Linux-like PCI driver registration layer with vendor/device/class
  matching, BAR mapping helpers, IRQ/MSI placeholders, power/runtime hooks,
  driver private data, and clean remove paths.
- [ ] Add an in-kernel Nouveau driver skeleton registered through the DRM core,
  initially probe-only on NVIDIA PCI devices and disabled on Hyper-V DXG.
- [ ] Implement Nouveau UAPI command decoding for `GETPARAM`, `CHANNEL_ALLOC`,
  `CHANNEL_FREE`, `GEM_NEW`, `GEM_INFO`, `GEM_CPU_PREP`, `GEM_CPU_FINI`,
  `GEM_PUSHBUF`, `VM_INIT`, `VM_BIND`, and `EXEC`, with unsupported paths
  fail-closed and counted.
  Wave63 progress: the xv6 Nouveau skeleton now accepts the libdrm/native-NVIDIA
  probe path through `VM_INIT`, no-op `VM_BIND`, `CHANNEL_ALLOC`, `GEM_NEW`,
  `GEM_INFO`, `GEM_CPU_PREP`, `GEM_CPU_FINI`, no-op `GEM_PUSHBUF`, no-op
  `EXEC`, `GEM_CLOSE`, and `CHANNEL_FREE`. Hyper-V DXG still returns
  unsupported on Nouveau `GETPARAM`, so the path stays fail-closed there.
- [ ] Implement Nouveau device discovery, chipset/class reporting, VRAM/GART
  sizing, BAR aperture mapping, PTIMER reads or emulation, and graph-unit
  reporting matched to Linux/libdrm expectations.
- [ ] Port Nouveau memory management onto the TTM layer, including VRAM/GART
  domain placement, tiling flags, map handles, presumed offsets, and
  relocation/pushbuf validation.
- [ ] Port Nouveau channel/fifo and pushbuffer submission enough to run a
  deterministic no-op or fence-only submit on supported hardware, then expand
  to Mesa Nouveau command streams.
  Wave63 progress: deterministic zero-push/no-reloc `GEM_PUSHBUF`, zero-op
  `VM_BIND`, and zero-push `EXEC` return success on the native-NVIDIA skeleton.
  Non-empty pushbuffers, relocations, VM bind ops, waits/signals, and real FIFO
  submission remain fail-closed.
- [ ] Add Nouveau validators: libdrm-nouveau open/getparam, channel allocate,
  GEM new/info/map/CPU prep/fini, PRIME sharing, pushbuf validation, VM bind,
  and fail-closed unsupported-class behavior.
  Wave63 progress: `drmiftest` now validates the native-NVIDIA no-op Nouveau
  sequence when Nouveau is present and validates the Hyper-V fail-closed path
  otherwise. PRIME sharing, map writes, unsupported-class coverage, and
  non-empty pushbuf/VM-bind validation remain open.
  Wave64 progress: added `nouveauabitest`, a staged host-glibc guest validator
  linked against `libdrm_nouveau`/`libdrm`. It opens `/dev/dri/renderD128` and
  uses the real libdrm-nouveau `nouveau_device_wrap` path. Focused Hyper-V
  evidence passes with `nouveauabitest: nouveau absent fail-closed ok
  driver=xv6_gpu ret=-19`, followed by `drmiftest: ok` and
  `backend_opengl_submit 0`. On native NVIDIA PCI, the same validator is ready
  to exercise libdrm getparam/client/BO/map/PRIME paths.
  Wave65 progress: the native-positive `nouveauabitest` path now also creates
  a `NOUVEAU_FIFO_CHANNEL_CLASS` object through libdrm, constructs a pushbuf,
  and kicks the deterministic no-op push path before BO/map/PRIME coverage.
  Focused Hyper-V evidence remains fail-closed with
  `nouveauabitest: nouveau absent fail-closed ok driver=xv6_gpu ret=-19`,
  `drmiftest: ok`, and `backend_opengl_submit 0`.

### Port Integration And Acceptance

- [x] Build libdrm with Nouveau enabled against xv6 headers, without local
  struct drift or private duplicated ioctl numbers.
  Wave63 evidence: `/tmp/xv6-hyperv-build/ports` target `port-libdrm` built
  with Meson `Nouveau: true` and staged `libdrm_nouveau.so`,
  `libdrm_nouveau.a`, `libdrm_nouveau.pc`, and `nouveau/nouveau.h` into the
  Hyper-V sysroot. Focused Hyper-V `drmiftest; fbstat` then passed with
  `drmiftest: nouveau absent fail-closed ok` and
  `backend_opengl_submit 0`.
- [ ] Enable Mesa Nouveau winsys/driver build only after the kernel Nouveau
  probe and GEM/TTM/KMS validators pass.
- [ ] Add CI/host scripts that build `/tmp/xv6-hyperv-build`, create focused
  test images, and run the pure-C DRM/GEM/TTM/KMS/Nouveau validators before
  any browser or FPS claim.
- [ ] Preserve the existing invariant that Hyper-V reports
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT == 0` until the separate native-present,
  480p >60 FPS, and WebKit shared-surface gates are truly proven.

## Dependency Split

- Active prerequisite on the current dirty tree: keep WSL-equivalent NVIDIA UMD
  loader/device-admission behavior stable after the WSL OpenAdapter, feature
  query, enum-EIO, and QAI admission corrections. Direct runtime-first
  admission is restored, but the list-path admission matrix still needs
  follow-up.
- Current blocker after direct admission and resource NT export are restored:
  compositor/native-present evidence. Wave38 proves resource export/open and
  fence export, but strict present still fails before success, the direct D3D12
  fence-open path remains WSL-reproducible/expected-failing, and the run lacks
  `/tmp/wlcomp-d3d12-present` proof.
- Wave39 proves the compositor import/acquire/GPU-copy/fail-closed evidence
  bundle now exists. The remaining blocker is the real kernel/host display
  handoff behind the present-source commit path, plus callback/release and
  native completion evidence after that handoff exists.
- Wave40/Wave41 name the missing handoff ABI as `dxg-resource-scanout-bind`.
  The display-handoff decision tree remains open: WSLg-like user/display
  channel, synthvid VRAM-backed D3D12 existing-sysmem target, or new host helper
  protocol behind `FB_GPU_DXG_PRESENT_SOURCE_COMMIT`.
- Existing-sysmem comparison update: WSL comparison says `existing_sysmem` is a
  D3DKMT backing-store path, not a display-present path by itself. It can
  pin/send guest-memory PFNs, but it does not create an `ID3D12Resource` over
  synthvid VRAM; WSLg display remains Weston/RDP/system-memory interop.
- Wave44 dispatch structure: keep display handoff split into runtime-created
  shared-resource present, host/display helper protocol, WSLg-like user/display
  channel, and a rejected raw allocation-to-`ID3D12Resource` path. Wave43 closes
  only the direct scanout/VRAM existing-sysmem PFN registration proof; it does
  not close the D3D12 COM-resource bridge or native display completion.
- Wave44 validation artifacts record the fail-closed scanout bridge boundary:
  `/tmp/xv6-hyperv-build/wave44-scanout-d3d12-bridge.log` proves PFN/VRAM
  registration for the scanout target while preserving the explicit
  raw-existing-sysmem-not-`ID3D12Resource` contract, and
  `/tmp/xv6-hyperv-build/wave44-d3d12-require-present.log` keeps strict present
  fail-closed with no native completion or backend flag enablement.
- Wave45 validation artifacts record lifetime/provenance hardening:
  `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log` proves WSL-style seal
  verification diagnostics with tracked allocation/private-size checks, retained
  process namespace clearing diagnostics, exporter-close shared-resource
  lifetime matrix PASS, and scanout existing-sysmem pin-lifetime matrix
  PASS_FAIL_CLOSED. `/tmp/xv6-hyperv-build/wave45-d3d12-require-present.log`
  proves strict-present parser/provenance live evidence while keeping native
  present, FPS, WebKit, and backend flag gates open.
- Follow-up closure reports (Linnaeus/Huygens/Confucius/McClintock) narrow or
  close source/trace-only rows for QAI type30/type31/type48 WSL parity,
  Wave47 WSL resource/fence `CreateSharedHandle` traces, Wave45 NT-share
  seal/lifetime and resource destroy-close layout parity, direct D3D12 fence
  `OpenSharedHandle` as a WSL-reproducible expected-fail lane, import-negative
  source rows, and Wayland Phase 2 negative source modes. Runtime-required
  coordinator artifacts remain open until run.
- Wave48 coordinator artifacts close the runtime import-negative and
  fail-closed-present accounting slices without claiming native display
  handoff: `/tmp/xv6-hyperv-build/wave48-import-negative.log` selects the
  NVIDIA adapter (`adapter_type 0x2091`), reports import-negative PASS,
  OPENSYNC source matrix PASS, and `backend_opengl_submit 0`;
  `/tmp/xv6-hyperv-build/wave48-present-selftest.log` reports present selftest
  `failures=0` plus Phase 2 negatives; and
  `/tmp/xv6-hyperv-build/wave48-d3d12-present-artifact.log` proves runtime
  resource/fence import, GPU copy, selected final handoff lane, missing kernel
  ABI, `present_id=0/completed=0`, callbacks/releases blocked, no CPU/readback,
  and `backend_opengl_submit 0`.
- Wave49 coordinator artifacts refresh the runtime-negative and fail-closed
  strict-present accounting: `/tmp/xv6-hyperv-build/wave49-runtime-negative-submissions.log`
  reports `runtime-negative-submission-matrix` PASS for bad-luid,
  bad-dimensions, bad-format, missing-fence, stale-fence, and CPU fallback
  guard, with `summary pass=6 fail=0 native_present_claim=0` and
  `backend_opengl_submit 0`; `/tmp/xv6-hyperv-build/wave49-d3d12-require-present-alone.log`
  plus `/tmp/xv6-hyperv-build/wave49-d3d12-present-artifact-clean.log` reach
  import/GPU copy, name the missing helper as
  `host-display-helper/resource-scanout-bind` with candidate commands 34/35/38,
  and still report `present_id=0/completed=0`, callbacks/releases blocked, no
  readback, and `backend_opengl_submit 0`.
- Wave50 source-accounting pass adds passive host-helper contract diagnostics
  (version, metadata, transport-present=0, operation, lifetime, source-live,
  requires-completion), current-run identity and callback/release same-frame
  evidence keys in the compositor/smoke path, script consumption of
  final-handoff/current-run provenance, and `dxgprobe` matrices for adapter-list
  parity, NT-share object kind, and destroy separation. These are source
  accounting items only: real host-helper transport, native-present completion,
  FPS, WebKit, and backend enablement remain open.
- Wave50 runtime prep confirms the dxgprobe matrices pass and the
  identity/helper fields are live in `/tmp/wlcomp-d3d12-present`, but the strict
  present path regressed to `protocol_accepted` with zero GPU-copy and zero
  present-source commit attempts. Treat this as protocol-only intermediate
  evidence; native-present and refreshed fail-closed-present runtime closure
  remain open until Wave51 restores the copy/commit path.
- Wave54 planning state: Wave51 had valid fail-closed GPU-copy/commit evidence,
  but Wave53 is still protocol/intermediate with zero GPU-copy and zero
  present-source commit attempts. WSL/Linux examples prove resource sharing,
  PFN/backing-store registration, synthvid GPA/dirty display, and WSLg-style
  remoting, but no public guest-only D3D12-resource-to-scanout ioctl. Final
  native handoff likely needs either a host helper/display channel or a clearly
  proven synthvid/VRAM D3D12 bridge.
- Wave54 accounting artifacts restore the strict-present terminal fail-closed
  path without closing native present: `/tmp/xv6-hyperv-build/wave54-d3d12-require-present.log`
  and `/tmp/xv6-hyperv-build/wave54-d3d12-present-artifact.log` show GPU copy
  completes, DXG present-source register/commit ioctl entries are `1/1` with
  zero copyin failures, and commit rejects `EOPNOTSUPP` because the host helper
  ABI is missing. `/tmp/xv6-hyperv-build/wave54-boot-fbstat.log` keeps
  `backend_opengl_submit 0`. `/tmp/xv6-hyperv-build/wave54-dxgprobe-purec.log`
  passes `adapter_list_parity_matrix`, `opensync_layout_source_matrix`,
  `ntshare_object_kind_matrix`, and `destroy_separation_matrix`; import-negative
  rows still FAIL and remain open.
- Wave55 accounting splits the import-negative state:
  `/tmp/xv6-hyperv-build/wave55-import-negative.log` proves
  `resource_import_negative_matrix` PASS, but `sync_import_negative_matrix` and
  `opensyncobject_source_matrix` still FAIL because child/foreign-process device
  sync open is accepted. Strict-present remains terminal fail-closed in
  `/tmp/xv6-hyperv-build/wave55-d3d12-require-present.log` and
  `/tmp/xv6-hyperv-build/wave55-d3d12-present-artifact.log`: query
  ioctl/support is compiled, query is skipped after commit failure rather than
  missing in the kernel, GPU copy completes, the missing host helper is
  validated, and `backend_opengl_submit 0` remains in
  `/tmp/xv6-hyperv-build/wave55-boot-fbstat.log`.
- Wave56 import-negative accounting:
  `/tmp/xv6-hyperv-build/wave56-import-negative.log` keeps resource negatives
  PASS and adds child isolation diagnostics. WSL comparison says accepting a
  child-owned `/dev/dxg` fd with the same numeric device handle is not a
  mismatch. Keep the inherited `/dev/dxg` TGID guard proof open until
  `dxgprobe` has a dedicated row for it.
- Wave57 import-negative accounting:
  `/tmp/xv6-hyperv-build/wave57-import-negative.log` proves
  `resource_import_negative_matrix`, `sync_import_negative_matrix`,
  `opensyncobject_source_matrix`, `opensync_child_own_dxg_matrix`,
  `opensync_child_inherited_parent_dxg_matrix`, `ntshare_object_kind_matrix`,
  and `destroy_separation_matrix` all PASS. Same-numeric child-owned
  `/dev/dxg` remains WSL-consistent; inherited parent `/dev/dxg` is rejected by
  the TGID namespace guard with `kernel_namespace_diag_present=1` and reject
  count. Native present, direct D3D12 fence import, FPS, WebKit, and backend
  enablement remain open.
- Separate work required after export/open: Wayland must import/open the
  runtime D3D12 resource and acquire sync on the compositor device, validate
  adapter LUID and fence target semantics, reject bad-LUID and CPU/readback
  fallbacks, and then present/composite on the GPU.
- Separate validation after native present works: the 480p FPS gate must count
  native D3D12 present completions correlated with display completions and
  visible thumbnail progress, with `backend_opengl_submit 1` only after that
  path passes. This phase must specifically address the observed inflated FPS
  report where the UI showed about 40 FPS while the real visible rate appeared
  single-digit.
- Separate WebKit work after the native path passes: WebKit may use the D3D12
  shared-surface contract only through the same validated native Mesa/compositor
  path. Render-node presence, environment selection, dmabuf request state, or
  stale `/tmp/wlcomp-d3d12-present` evidence cannot satisfy the contract.

## Granular Active Plan

### Current Wave Owner Map

- Kernel NT-share ordering/diagnostics owns Phase 1 host-NT-first ordering,
  `CREATENTSHAREDOBJECT` diagnostics, and one `/dev/dxg` artifact tying object
  handles to host return status.
- `d3d12sharedsmoke` WSL-shape resource case owns the runtime resource, heap,
  fence, residency, and heap-first case matrix in one smoke artifact.
- WSL QAI/list-path comparison owns the same-adapter WSL QAI trace and one
  xv6-vs-WSL comparison artifact for direct and adapter-list admission.
- Compositor import validation owns Phase 2/3 same-LUID resource/fence import,
  native-present counters, and one fresh compositor artifact.
- Validation scripts/FPS/WebKit gate owns Phase 4/5 anti-inflation, stale
  evidence rejection, and one fresh validator artifact per gate.

### Open Dependency Ledger

This ledger is the handoff index for the remaining unchecked work. Do not close
any item here from source intent alone; each close needs the named artifact or a
current source patch that directly proves the line text.

#### Ledger A. QAI And Admission

- [ ] Evidence placeholder: one current xv6 admission artifact with direct and
  `CreateAdapterList(D3D12_GRAPHICS)` admission, QAI type0/type27/type48 rows,
  adapter counts, selected LUIDs, and `backend_opengl_submit 0`.
- [ ] Evidence placeholder: one same-adapter WSL QAI/list-path trace with the
  same requested QAI shapes and adapter filtering path.
- [ ] Dependency: keep QAI trace-equivalence separate from Phase 1 export
  because Wave33/Wave36/Wave37 already retired device admission as the current
  blocker.
- [ ] Close criterion: xv6 and WSL-shaped QAI/list rows are matched or any
  remaining divergence is classified as non-blocking with an artifact path.

#### Ledger B. Resource Export Metadata

- [ ] Evidence placeholder: one current strict-present or export-only artifact
  showing runtime, resource, allocation, private-size, allocation-size, and
  tracked-allocation diagnostics for the exported resource.
- [ ] Evidence placeholder: one artifact showing the canonical shared-resource
  record used for fd publish, query/open, import, refcount, and close paths.
- [ ] Evidence placeholder: one same-adapter WSL resource creation/export trace
  for the matching heap/resource/app-sync shape.
- [ ] Dependency: compare metadata provenance only after resource export/open is
  known-good; do not let q38/d3c provenance block native-present tracking.
- [ ] Close criterion: metadata fields either match WSL or are explicitly proven
  irrelevant to the already-working resource export/open/import path.

#### Ledger C. NT-Share And Sealing

- [x] Evidence closed by `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log`:
  WSL-style seal verification diagnostic includes tracked allocation and
  private-size verification.
- [x] Evidence closed by `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log`:
  exporter-close shared-resource lifetime matrix PASS.
- [ ] Evidence placeholder: same-adapter WSL ordering trace proving whether WSL
  seals before or after host NT-share for the matching runtime resource.
- [ ] Dependency: keep seal-after-success ordering separate from local metadata
  lifetime, because xv6 resource export/open already works.
- [ ] Close criterion: WSL host-NT-first/seal-after-success ordering is proven
  or the documented alternative is validated by a same-adapter WSL trace.

#### Ledger D. Sync Import And Fence Semantics

- [ ] Evidence placeholder: one artifact proving whether direct D3D12
  `OpenSharedHandle(fence)` is required by the final native-present path or can
  remain bypassed by syncfile acquire.
- [ ] Evidence placeholder: same-adapter WSL `OPENSYNCOBJECTFROMNTHANDLE2`
  trace for the pure-C sync fd and the D3D12 fence fd shapes.
- [ ] Dependency: keep syncfile acquire PASS separate from direct D3D12 fence
  import, and keep both separate from native display completion.
- [ ] Close criterion: final path either uses syncfile acquire with target
  satisfaction or proves direct D3D12 fence import/wait works with WSL-matched
  fields.

#### Ledger E. Destroy Close And Process Lifetime

- [x] Evidence closed by `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log`:
  retained process namespace clearing diagnostic exists.
- [ ] Evidence placeholder: same-adapter WSL close/destroy trace for resource,
  sync/fence fd, process lifetime, and explicit D3DKMT destroy ordering.
- [ ] Dependency: keep resource fd destroy PASS separate from sync/fence and
  process-lifetime parity.
- [ ] Close criterion: xv6 close/destroy behavior is WSL-matched or any
  difference is proven harmless with no stale host destroy dispatch.

#### Ledger F. existing_sysmem And Display Boundary

- [x] Evidence closed by `/tmp/xv6-hyperv-build/wave44-scanout-d3d12-bridge.log`:
  raw existing_sysmem/PFN/scanout GPA is not an `ID3D12Resource` bridge.
- [x] Evidence closed by `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log`:
  scanout existing-sysmem pin-lifetime matrix is PASS_FAIL_CLOSED.
- [ ] Evidence placeholder: direct `FB_GPU_SCANOUT_MAP`/VRAM artifact with a
  real D3D12 resource/copy bridge, not only PFN registration.
- [x] Dependency: WSL comparison says existing_sysmem is backing-store/PFN
  registration; treat synthvid dirty rect as display VRAM/dirty, not D3D12 COM.
- [ ] Close criterion: a chosen handoff lane demonstrates D3D12-written content
  reaches display scanout without CPU/readback and with native completion.

#### Ledger G. Wayland Native Present

##### Ledger G1. Present-Source Provenance Diagnostics

- [x] Evidence placeholder: one current `/tmp/wlcomp-d3d12-present` artifact
  with client id, resource id, resource generation, compositor run id, source
  live state, operation, helper contract version, and missing/present transport
  state for the same frame.
- [ ] Close criterion: provenance fields identify one current run and cannot be
  satisfied by stale protocol-only or fail-closed artifacts.
  Wave61 evidence:
  `/tmp/xv6-hyperv-build/wave61-present-run2.log` and
  `/tmp/xv6-hyperv-build/wave61-present-evidence.log` provide current
  fail-closed provenance/identity keys, adapter/register flags, callback and
  release blocked keys, `d3d12_present_source_no_host_helper=1`,
  `no_display_handoff=1`, `no_present_completion=1`,
  `d3d12_native_present_requirements_satisfied=0`, `strict_pass=0`, commit
  `errno/status=95`, and `backend_opengl_submit 0`. This closes only the
  observability slice, not the native-present close criterion.

##### Ledger G2. DXG Resource Export Descriptor/Query

- [ ] Evidence placeholder: one current artifact with the D3D12 resource export
  descriptor, dimensions, format, pitch/layout, allocation/private-data sizes,
  LUID, NT-share/open state, and query behavior after the present attempt.
- [ ] Close criterion: the descriptor/query record proves the host-helper input
  resource is the same D3D12 destination/resource that GPU copy completed.

##### Ledger G3. Synthvid/VRAM Existing-Sysmem Bridge

- [ ] Evidence placeholder: one artifact proving a synthvid/VRAM
  existing-sysmem target becomes a usable D3D12 resource/copy destination, not
  only PFN/backing-store registration.
- [ ] Negative close criterion: if the bridge is rejected, record a current
  artifact proving it remains PFN/backing-store only and cannot be native
  present evidence.

##### Ledger G4. Host-Helper Transport ABI Stub

- [x] Evidence placeholder: one source/runtime artifact showing the host-helper
  ABI stub reports version, operation, source handle, metadata, lifetime,
  transport-present state, and failure reason without faking completion.
- [x] Close criterion: transport-absent fail-closed behavior is distinguishable
  from malformed metadata, source-not-live, helper rejection, and timeout.
  Wave61 `fbstat` evidence records named provenance, metadata, lifetime,
  candidate-helper, reject, and gate rows while the strict run reports
  no-host-helper/no-display-handoff/no-present-completion and terminal commit
  `EOPNOTSUPP`/95. The real host-helper implementation remains open.

##### Ledger G5. Real Host Helper Or Display Service

- [ ] Evidence placeholder: one implementation/runtime artifact where a real
  host helper, WSLg-like display channel, or proven synthvid/VRAM bridge accepts
  the committed D3D12 resource.
- [ ] Close criterion: the helper/service produces a nonzero present id and a
  host-visible completion for that committed resource.

##### Ledger G6. Completion, Callback, And Release

- [ ] Evidence placeholder: one fresh compositor/strict-smoke/`fbstat` bundle
  where successful present-source commit, nonzero `present_id`,
  `completed >= present_id`, frame callback, and buffer release all describe
  the same client/resource/generation.
- [ ] Dependency: resource export/open, syncfile acquire, same-LUID import, and
  GPU-copy proof are already separate closed prerequisites; do not relitigate
  them as native-present proof.

##### Ledger G7. FPS/WebKit Gates After Native Present

- [ ] Evidence placeholder: one downstream record that Phase 4 FPS and Phase 5
  WebKit remain blocked until Ledger G6 succeeds.
- [ ] Close criterion: native display handoff succeeds and all compositor,
  strict smoke, `fbstat`, FPS, and WebKit evidence describe the same frame
  contract without CPU/readback fallback.

#### Ledger H. FPS Gate

- [ ] Evidence placeholder: one finite post-warmup 480p validator artifact with
  `effective_presented_fps > 60`, native/display/visual presented evidence,
  run id, resource generation, and thumbnail progress outside overlays.
- [ ] Dependency: Phase 3 native-present completion must be closed first.
- [ ] Close criterion: displayed/demo FPS is context only; acceptance comes from
  native completion, display/visual progress, and matching run identity.

#### Ledger I. WebKit Gate

- [ ] Evidence placeholder: one gated-off WebKit artifact while any Phase 0-4
  dependency is open.
- [ ] Evidence placeholder: one enabled WebKit artifact after native present and
  FPS pass, using the same D3D12 shared-resource, monitored-fence,
  adapter-LUID, and destination-present handoff contract as native Mesa.
- [ ] Dependency: WebKit cannot advance from render-node, dmabuf request,
  callback-only, release-only, or stale `/tmp/wlcomp-d3d12-present` evidence.
- [ ] Close criterion: WebKit content CRC/frame-hash progress correlates with
  native-present completions from the same run.

### Phase 0: WSL Parity Recheck Before More Present Work

#### 0A. WSL Wire Shape Baseline

- [x] Compare WSL `OPENADAPTER` v40 wire layout against xv6.
- [x] Keep `OPENADAPTER.guest_adapter_luid` zero on the v40 wire.
- [x] Preserve xv6's internal adapter-LUID fallback only after the host
  response, not in the packet sent to the host.
- [x] Compare WSL `LX_ISFEATUREENABLED` behavior for zero or invalid adapter
  handles.
- [x] Remove the xv6-only global `LX_ISFEATUREENABLED` fallback.
- [x] Verify invalid or zero feature-query adapter handles fail before any
  global host command is sent.

#### 0B. QueryAdapterInfo Evidence

- [x] Add kernel-side `QueryAdapterInfo` payload history.
- [x] Record host-returned payload bytes before xv6 rewrites or userspace
  copyout.
- [x] Include QAI type, requested size, returned length, return status, route,
  adapter handle, host handle, and first payload bytes in `/dev/dxg`.
- [x] Capture a fresh full QAI history from the current focused Hyper-V image.
  Evidence: `/tmp/xv6-hyperv-build/xv6-qai-matrix-dxg.log` contains
  `dxg_queryadapter_payload_history` and per-entry payload heads.
- [x] Compare current xv6 WSL-shaped QAI ordering against same-adapter WSL for
  type55, type0, and type27 on direct and list admission paths.
- [x] Compare type48 only with WSL-shaped request sizes; do not treat generic
  type48 probe failures as an admission blocker.
  Wave36 evidence: `/tmp/xv6-hyperv-build/wave36-qai-wsl-shaped.log` closes
  WSL-shaped type55/type27/type0 direct/list blockers. Type48 remains
  trace-equivalence work only when probed with WSL-shaped sizes.
  Wave37 evidence:
  `/tmp/xv6-hyperv-build/wave37-qai-summary-type0.log` prints
  `qai-equivalence-summary` with direct/list LUID match, separates generic
  type55/type27/type0 `EINVAL`, and proves WSL-shaped direct+list type55/type27
  PASS plus WSL-shaped direct+list type0 PASS with request/result size 46739.
  Generic type48 remains FAIL and is kept as trace-equivalence-only work.
  Linnaeus closure report narrows type48 to WSL-shaped trace equivalence only;
  generic type48 probe failures remain non-blocking.

#### 0C. Adapter Admission Matrix

- [x] Add `d3d12sharedsmoke --device-admission-matrix`.
- [x] Test direct `GetAdapterByLuid` admission.
- [x] Test WSL-style `CreateAdapterList(D3D12_GRAPHICS)` admission.
- [x] Prove both paths select the same adapter LUID on the current host.
- [x] Prove both paths fail at `D3D12CreateDevice` before kernel
  `LX_DXCREATEDEVICE` on the pre-driverstore image.
- [x] Re-run the matrix on the driverstore-staged image.
- [x] Confirm whether `D3D12CreateDevice` now succeeds, reaches kernel
  `LX_DXCREATEDEVICE`, or still fails in userspace.
  Evidence: `/tmp/xv6-hyperv-build/phase0-device-admission-matrix-short-20260521.txt`
  shows direct `GetAdapterByLuid` `create_hr=0x0`, while the
  `CreateAdapterList(D3D12_GRAPHICS)` path still fails in userspace with
  `0xffffffff887a0004`.
  Wave27 evidence: `/tmp/xv6-hyperv-build/wave27-device-admission-matrix.log`
  supersedes the old list-path failure. Both direct `GetAdapterByLuid` and
  `CreateAdapterList(D3D12_GRAPHICS)` create D3D12 devices with
  `create_hr=0x0` on the same LUID `00000001:8a17a4bf`.
  Wave33 LD_DEBUG admission evidence:
  `/tmp/xv6-hyperv-build/wave33-lddebug-device-admission.log` confirms the
  guest shell accepts the `LD_DEBUG=... d3d12sharedsmoke
  --device-admission-matrix` form, and both direct `GetAdapterByLuid` and
  `CreateAdapterList(D3D12_GRAPHICS)` report `adapter_hr=0x0`,
  `create_hr=0x0`, and `device_luid_match=1`.

#### 0D. NVIDIA UMD Loader Parity

- [x] Use LD_DEBUG to prove the pre-driverstore failure is a UMD loader path
  issue.
- [x] Record that `libd3d12core.so` tried `ib/libnvwgf2umx.so`, which is not a
  valid WSL driver-store UMD path.
- [x] Compare WSL NVIDIA UMD discovery against xv6's type-1
  `KMTQAITYPE_UMDRIVERNAME` handling.
- [x] Stage
  `/usr/lib/wsl/drivers/nvmi.inf_amd64_9a9d1548c06ce277/libnvwgf2umx.so` into
  the xv6 rootfs.
- [x] Rewrite NVIDIA `KMTQAITYPE_UMDRIVERNAME` to the Windows DriverStore-style
  path that WSL's D3D12 core maps to `/usr/lib/wsl/drivers/<FileRepository>`.
- [x] Re-run LD_DEBUG on the focused Hyper-V image.
- [x] Prove `libnvwgf2umx.so` is opened from `/usr/lib/wsl/drivers/...`.
- [x] Prove LD_DEBUG shows a generated link map for the NVIDIA UMD.
- [x] Prove the broken `ib/libnvwgf2umx.so` probe no longer determines the
  device-admission result.
  Wave33 evidence:
  `/tmp/xv6-hyperv-build/wave33-lddebug-files-device-admission.log` shows
  `/lib/libd3d12core.so` dynamically loads the NVIDIA UMD from
  `/usr/lib/wsl/drivers/nvmi.inf_amd64_9a9d1548c06ce277/libnvwgf2umx.so`,
  creates a link map, and calls UMD init/fini. Combined with
  `/tmp/xv6-hyperv-build/wave33-lddebug-device-admission.log`, both direct and
  list-path device admission succeed, so the old broken `ib/libnvwgf2umx.so`
  probe no longer determines admission.

#### 0E. Type-0 Private Payload Follow-Up

- [x] If QAI type 0 starts appearing, capture the exact xv6 request size,
  result size, return status, and payload head.
- [x] Compare xv6 type-0 behavior against WSL
  `dxgvmb_send_query_adapter_info`.
- [x] Remove any non-WSL short-result rejection if it blocks the real UMD.
- [x] Preserve kernel diagnostics proving the host-returned type-0 payload
  before any normalization.
  Stale/closed: the early type0-after-type55 `-75` blocker was superseded by
  the Wave 4+ evidence where the type0 payload cache is populated and used
  (`type0_cache:1/46739`, type0 `len=46739 ret=0`). Keep type27/type57 and
  list-path matrix work separate.

#### 0F. Phase-Exit Validation

##### 0F1. Explicit Mesa D3D12 Runtime Gate

- [x] Restore the current-tree `mesaglfeature` D3D12 pass with no device
  removal.
- [x] Capture `/dev/dxg` immediately after the restored pass.
- [x] Confirm Hyper-V still reports `backend_opengl_submit 0`.

##### 0F2. Device-Admission Blocker Retired

- [x] Move to Phase 1 only after current-tree D3D12 device admission is
  working or the remaining failure has a WSL-matched QAI/device-create packet
  diagnosis.
  Wave 10 validation note:
  `/tmp/xv6-hyperv-build/wave10-mesaglfeature-dxg.log` shows default
  `mesaglfeature` passed, but it used softpipe
  (`MESA_LOADER_DRIVER_OVERRIDE=softpipe`, `LIBGL_ALWAYS_SOFTWARE=1`), so it
  does not close D3D12 renderer or OpenGL-submit gates. The explicit command
  with `GALLIUM_DRIVER=d3d12 MESA_LOADER_DRIVER_OVERRIDE=d3d12
  LIBGL_ALWAYS_SOFTWARE=0 mesaglfeature` reached the DXCore/D3DKMT path in
  `/tmp/xv6-hyperv-build/wave10-post-d3d12-probe-dxg.log`, but D3D12 adapter
  lists had count `0`; D3DKMT fallback selected LUID `00000001:7e098ee2`;
  factory `CreateDevice` failed generic/fl11 with `0x80004005`; EGL setup
  failed `0x3001`.
- [x] Record that the default Wave10 `mesaglfeature` pass was softpipe-only.
- [x] Prove explicit D3D12 `mesaglfeature` reaches DXCore/D3DKMT fallback path.
- [x] Make explicit D3D12 `mesaglfeature` create a D3D12 device instead of
  failing factory `CreateDevice` with `0x80004005`.
- [x] Make explicit D3D12 `mesaglfeature` complete EGL setup without
  `0x3001`.
  Wave33 device-admission note: direct and list-path D3D12 device admission are
  currently closed by the LD_DEBUG matrix (`create_hr=0x0`, LUID match). Keep
  the explicit Mesa D3D12 `mesaglfeature` device/EGL gate separate.
  Wave38 evidence:
  `/tmp/xv6-hyperv-build/wave38-mesaglfeature-mrdiag.log` shows explicit
  `GALLIUM_DRIVER=d3d12 MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA mesaglfeature`
  reaches the D3D12 renderer, passes 32x32 and 64x32 FBO readbacks, prints
  `mesaglfeature: ok`, and reports no device removal.
  `/tmp/xv6-hyperv-build/wave38-post-mesaglfeature-fbstat-dxg.log` is the
  immediate post-pass `/dev/dxg`/fbstat capture and still reports
  `backend_opengl_submit 0`.

#### 0G. Adapter Enumeration EIO Recovery

- [x] Reproduce the current DXG adapter-record enumeration `errno=5` failure
  with a fresh `/dev/dxg` capture.
- [x] Compare the enum-EIO path against the earlier first-wave direct
  `GetAdapterByLuid` create-device success and type-0/QAI success evidence.
- [x] Identify whether the EIO is from adapter-record enumeration,
  adapter-list construction, DXCore discovery, or an earlier DXG open/probe
  regression.
- [x] Restore enumeration enough to rerun Phase 1 runtime
  `CreateSharedHandle(resource)` reproduction.
  Failure evidence: channels were open and the VM did not freeze, but
  `dxg_open_createprocess` failed, `dxg_early_bind` showed
  `enumadapters2 ret:-5`, D3DKMT readiness stayed pending, and
  `backend_d3dkmt` remained `0`.
  Validation evidence: enum-EIO recovery was built and validated on
  `/tmp/xv6-hyperv-build/xv6-process-adopt-dxg.vhdx`; `dxgprobe` enum succeeds
  and `backend_d3dkmt=1`.

#### 0H. DXCore Admission After Enum Recovery

- [x] Diagnose why DXCore `GetAdapterByLuid` now returns `hr=0x80070057`.
- [x] Capture a clean admission-matrix artifact after QAI cache retry.
- [x] Diagnose why the `CreateAdapterList(D3D12_GRAPHICS)` path still fails
  `D3D12CreateDevice` with `0x887a0004`.
- [x] Prove list admission succeeds after the remaining type0/list-path fix.
- [x] Compare QAI type15 zero-length success, type30 `-75`, and type31 fallback
  against same-adapter WSL behavior.
  Stale/closed: this older suspect bundle is no longer the active blocker after
  later QAI/cache/admission evidence restored direct runtime admission; do not
  use it to close the type27/type57 or list-path matrix items.
- [x] Restore DXCore admission enough to reach the Phase 1 runtime
  `CreateSharedHandle(resource)` reproduction again.
  Direct-admission evidence: after QAI admission fixes, clean runtime-first
  validation reached `D3D12CreateDevice` success, D3D12 resource-create
  success, and the Phase 1 `CreateSharedHandle(resource)` failure again. The
  list-path admission matrix remains open. QAI suspects from the failed matrix
  were type15 zero-length success, type30 `-75`, and type31 fallback.
  Fresh orchestrator evidence:
  `/tmp/xv6-hyperv-build/wave-admission-matrix-dxg.log` shows the direct path
  succeeds, the list path still fails with `0x887a0004`,
  `backend_opengl_submit` remains `0`, and QAI cache stores remain `0`.
  Kernel and rootfs builds passed for this orchestrator wave, but the failed
  list path keeps the open admission-diagnosis items unchecked.
  Wave 2 evidence:
  `/tmp/xv6-hyperv-build/wave2-admission-matrix-dxg.log` shows kernel/rootfs
  passed after QAI cache and direct WSL-shape changes. QAI cache stores and
  hits are now nonzero, type1/type15/type30/type31 behavior improved, and
  direct admission still succeeds. List admission still fails with
  `0x887a0004`, `backend_opengl_submit` remains `0`, and type0 after type55
  returns `-75`, so list-path and type0-parity items stay open.
  Wave 3 evidence:
  `/tmp/xv6-hyperv-build/wave3-admission-matrix-clean-dxg.log` keeps the same
  split: direct admission works, list admission still fails with `0x887a0004`,
  type0 after type55 still returns `-75` with `len 0` despite retry, QAI source
  counters include alias cache stores/hits, and `backend_opengl_submit` remains
  `0`.
  Wave 4 evidence:
  kernel/rootfs builds passed with
  `cmake --build /tmp/xv6-hyperv-build --target kernel -j2` and
  `cmake --build /tmp/xv6-hyperv-build --target rootfs -j2`. The image
  `/tmp/xv6-hyperv-build/xv6-wave4-noext-ntshare.vhdx` was deployed to
  `C:\Temp\xv6-hyperv.vhdx`; the VM used 6 vCPUs, booted with
  `acpi_cpus=6`, and still reported `backend_opengl_submit 0`.
  `/tmp/xv6-hyperv-build/wave4-clean-admission-matrix-dxg.log` shows direct
  admission still succeeds. The old list-path `0x887a0004` failure is gone, but
  `CreateAdapterList(D3D12_GRAPHICS)` still fails with `0x80004005`. Type0
  payload cache is populated and used (`type0_cache:1/46739`, type0
  `len=46739 ret=0`), while type27 still returns `ret:-75 len:0`.
  Wave 5 evidence:
  `/tmp/xv6-hyperv-build/wave5-admission-matrix-dxg.log` shows the QAI type27
  command is now unpadded (`cmd_len:36 wire_len:52`, private head 4), but the
  host still returns zero-length `-75`. Direct admission still succeeds, list
  admission still fails with `0x80004005`, and `backend_opengl_submit` remains
  `0`.
  Wave27 evidence: the list-path device-admission blocker is stale/closed:
  `CreateAdapterList(D3D12_GRAPHICS)` now creates a D3D12 device with the same
  selected/device LUID as the direct path. Manual generic QAI probes for
  type55/type0/type27 still return `EINVAL`, so this closes list-path device
  admission only, not WSL-exact QAI trace equivalence.
- [x] Populate and validate the QAI alias cache so the admission artifact no
  longer reports `stores=0`.
- [x] Capture and use the type0 payload cache for the clean admission run.
- [x] Prove the QAI type27 packet is sent unpadded.
- [x] Diagnose type0-after-type55 `-75` parity and root cause against WSL.
  Stale/closed by later type0-cache evidence: type0 returns `len=46739 ret=0`
  in the clean admission artifact, so the earlier `-75/len 0` diagnosis is no
  longer an active blocker.
- [x] Diagnose type27 `ret:-75 len:0` as the current list-path blocker after
  the Wave 4 type0 cache fix.
  Next evidence to close: a clean admission-matrix artifact where type27 is
  WSL-matched or no longer blocks `CreateAdapterList(D3D12_GRAPHICS)`, and list
  admission reaches `D3D12CreateDevice` success.
  Wave36 closeout: `/tmp/xv6-hyperv-build/wave36-qai-wsl-shaped.log` resolves
  the WSL-shaped type27/type55/type0 direct/list blocker. Keep only
  trace-equivalence follow-up items that require WSL-shaped request sizes.

#### 0I. WSL QAI And List-Path Comparison

##### 0I1. Non-Blocking Same-Adapter WSL Trace Capture

- [x] Capture a same-adapter WSL QAI/list-path trace for direct
  `GetAdapterByLuid` admission.
- [x] Capture a same-adapter WSL QAI/list-path trace for
  `CreateAdapterList(D3D12_GRAPHICS)` admission.
- [ ] Compare xv6 type15 zero-length success against the WSL trace.
- [x] Compare xv6 type30 `-75` against the WSL trace.
- [x] Compare xv6 type31 fallback against the WSL trace.
- [x] Produce one comparison artifact that explains whether the remaining
  list-path failure is QAI ordering, QAI payload shape, adapter-list selection,
  or a later device-create issue.
  Current artifact note:
  `/tmp/xv6-hyperv-build/wave-admission-matrix-dxg.log` is not enough to close
  this bucket. It confirms direct admission success and list-path failure
  `0x887a0004`, but QAI cache stores remain `0`, so the next source task is to
  fix alias-cache population before relying on list-path/QAI comparison output.
  Wave 2 artifact note:
  `/tmp/xv6-hyperv-build/wave2-admission-matrix-dxg.log` confirms the QAI
  alias cache now stores and hits, but it does not close this bucket: list
  admission still fails with `0x887a0004`, and type0 after type55 still returns
  `-75`.
  Wave 3 artifact note:
  `/tmp/xv6-hyperv-build/wave3-admission-matrix-clean-dxg.log` proves the
  retry/cache path is active, but still leaves type0/list-path parity open:
  type0 returns `-75/len 0`, and list admission fails `0x887a0004`.
  Wave 4 artifact note:
  `/tmp/xv6-hyperv-build/wave4-clean-admission-matrix-dxg.log` closes the old
  type0-cache evidence gap: type0 cache contains a 46739-byte payload and type0
  returns `len=46739 ret=0`. This does not close list admission because the
  list path now fails later with `0x80004005`, and type27 still reports
  `ret:-75 len:0`.
  Wave 5 artifact note:
  `/tmp/xv6-hyperv-build/wave5-admission-matrix-dxg.log` closes only the type27
  padding question: the type27 command is unpadded (`cmd_len:36 wire_len:52`,
  private head 4). It does not close type27 success or list admission because
  the host still returns zero-length `-75`, and list admission still fails
  `0x80004005`.
  Wave 10 list-default artifact note:
  `/tmp/xv6-hyperv-build/wave10-wsl-list-default-dxg.log` shows
  `d3d12sharedsmoke --runtime --case=wsl-success-shape-wsl-list-default` fails
  in userspace admission before `CreateDevice` or `CreateSharedHandle`: WSL-list
  adapter count is `0`, the stage is `userspace-admission`, and the HRESULT is
  `0x80004005`. This does not generate runtime private blobs or NT-share
  envelope evidence. The direct DXG-LUID path remains proven by
  `/tmp/xv6-hyperv-build/wave10-direct-natural32-dxg.log`.
  Wave26v4/WSL comparison note from Linnaeus: type48 failures match
  WSL-like registry misses and are not the primary list blocker. Keep type27,
  type57, and the list-path matrix open.
  Wave36 evidence: `/tmp/xv6-hyperv-build/wave36-qai-wsl-shaped.log` closes
  the WSL-shaped direct/list type55/type27/type0 blocker and leaves type48 as
  trace-equivalence-only work with WSL-shaped sizes, not a generic-probe
  admission blocker.
  Wave37 evidence: `/tmp/xv6-hyperv-build/wave37-qai-summary-type0.log`
  confirms the same split in summary form: WSL-shaped direct/list type55 and
  type27 PASS, WSL-shaped direct/list type0 PASS with req/result 46739, generic
  type55/type27/type0 `EINVAL` separated, and generic type48 still FAIL.
  Linnaeus closure report closes type30/type31 WSL parity. Type15 remains open
  until a separate report or artifact covers that row.

##### 0I2. Type-Specific QAI Trace Rows

- [x] Capture the exact same-adapter WSL type55->type0 request size, result
  size, return status, and payload route.
- [x] Compare the xv6 retry/cache route against WSL for type0 immediately after
  type55.
  Stale/closed: type0 retry/cache is no longer an active blocker after the
  cached 46739-byte payload path returns success.
- [x] Capture the exact same-adapter WSL type27 request size, result size,
  return status, and payload route.
- [x] Compare the xv6 type27 route and zero-length `-75` result against WSL.
- [x] Produce the first passing list-path device-admission matrix, keeping
  type27/type55/type0 trace equivalence separate.
- [x] Diagnose why D3D12 `CreateAdapterList(D3D12_GRAPHICS)` returns adapter
  count `0` on xv6 when the direct DXG-LUID path succeeds.

##### 0I3. Adapter Filtering And Mesa List Diagnostics

- [x] Add `dxgprobe adapter_list_parity_matrix` for source-accounted list-path
  parity checks.
- [x] Validate `dxgprobe adapter_list_parity_matrix` passes in Wave50 runtime
  prep.
- [x] Validate `dxgprobe adapter_list_parity_matrix` passes in Wave54 pure-C
  artifact.
- [ ] Compare xv6 list-default adapter filtering against same-adapter WSL
  DXCore enumeration.
- [ ] Diagnose why explicit Mesa D3D12 DXCore adapter-list counts are `0` even
  though D3DKMT fallback can select the correct LUID.
  Wave27 evidence: list-path device admission now succeeds; both admission
  paths create devices with LUID `00000001:8a17a4bf`. Type57
  `adaptertype-render` returns success (`rc=0 size=4 head=93230000`). Manual
  generic QAI probes for type55/type0/type27 return `EINVAL`, so do not mark
  WSL-exact type55/type0/type27 trace equivalence closed.
  Wave33 evidence: LD_DEBUG admission keeps both direct and list-path
  `D3D12CreateDevice` paths passing with matching LUIDs, but diagnostic QAI
  probes for type27/type55/type0 still return `EINVAL`. Keep WSL comparison
  items open.
- [x] Record type57 `adaptertype-render` success for direct and list-path
  admission.

#### 0J. Wave 7 Type27/List-Path Evidence Split

##### 0J1. Type27 Packet Parity

- [x] Capture the current xv6 type27 request/response as a standalone artifact
  with command length, wire length, private head, return status, and payload
  length.
- [x] Capture the matching same-adapter WSL type27 request/response with the
  same fields.
- [x] Compare whether type27 is expected to return data, zero-length success,
  or zero-length failure for the selected NVIDIA adapter.

##### 0J2. Admission Matrix Explanation

- [x] Identify whether the remaining list-path `0x80004005` happens before or
  after the type27 response is consumed by DXCore.
- [x] Produce a clean admission matrix where direct admission still succeeds,
  list admission behavior is explained, and `backend_opengl_submit` remains
  separate from admission evidence.
  Wave36 evidence: the WSL-shaped QAI artifact plus Wave33 LD_DEBUG admission
  logs close the former type27/list-path blocker. Direct and
  `CreateAdapterList(D3D12_GRAPHICS)` device admission both pass with matching
  LUIDs; `backend_opengl_submit` remains separate.
  Wave37 evidence:
  `/tmp/xv6-hyperv-build/wave37-qai-summary-type0.log` keeps direct/list LUID
  matched and proves WSL-shaped type0 returns the 46739-byte payload on both
  admission paths.

#### 0K. QAI/List-Path Owner Queue

Owner hint: QAI/list-path comparison worker.

- [x] Produce one WSL trace artifact for direct DXG-LUID admission that includes
  QAI type ordering, payload sizes, return statuses, and adapter-count output.
- [x] Produce one WSL trace artifact for `CreateAdapterList(D3D12_GRAPHICS)`
  admission with the same QAI and adapter-count fields.
- [x] Produce one xv6 admission artifact from the same build/image that includes
  direct admission, list admission, type0 cache, type27 result, and
  `backend_opengl_submit 0`.
  Wave27 artifact note: the admission artifact includes direct/list admission
  success and generic QAI probe results; generic type55/type0/type27 probes
  return `EINVAL`, so exact WSL trace equivalence remains open.
- [x] Explain whether the list-path adapter-count `0`/`0x80004005` failure is
  caused by QAI type27, DXCore filtering, Mesa adapter-list use, or a later
  D3D12 device-create step.
  Stale/closed after Wave33: this is no longer the active list-path blocker.
  The LD_DEBUG matrix shows `CreateAdapterList(D3D12_GRAPHICS)` reaches
  `create_hr=0x0` with a matching LUID. Keep WSL-shaped type27/type55/type0
  trace comparison open as non-blocking evidence work.
- [x] Keep the Phase 0 exit gate open until explicit D3D12 `mesaglfeature`
  creates a D3D12 device and completes EGL setup without device removal while
  Hyper-V still reports `backend_opengl_submit 0`.
  Wave38 closes this Phase 0 runtime gate for the explicit NVIDIA D3D12
  `mesaglfeature` run while preserving `backend_opengl_submit 0`.

### Phase 1: Runtime Shared-Resource NT Export

#### 1A. Failure Reproduction

- [x] Retire the historical resource-export failure as the current blocker.
- [x] Require `d3d12sharedsmoke --runtime --require-present` to reach
  `CreateSharedHandle(resource)`.
- [x] Record the runtime HRESULT and kernel host return in `/dev/dxg`.
- [x] Confirm the failure is not earlier device admission, UMD load, or
  renderer selection.
  Historical validation note: clean runtime-first validation reached this
  phase: `D3D12CreateDevice` succeeded, resource creation succeeded, and
  `CreateSharedHandle(resource)` failed with `hr=0x80004005`. Kernel
  `CREATENTSHAREDOBJECT` returned `-75` with process/resource/allocation
  ownership matching and runtime metadata present. This is no longer the
  current blocker after Wave38.
  Wave33 stale/closed note: resource NT-share export/import is no longer the
  current blocker. Later Wave10-Wave33 evidence proves valid resource NT export
  and import, and Wave33 strict-present reaches shared-resource import and D3D12
  GPU copy. Keep process lifetime/WSL handle-table parity and direct D3D12
  fence semantics separate.
  Wave38 strict-present update:
  `/tmp/xv6-hyperv-build/wave38-d3d12-require-present.log` supersedes the old
  resource-export failure: runtime `CreateSharedHandle(resource)` succeeds
  (`fd=37`, `handle=0x25`), `OpenSharedHandle(resource)` succeeds
  (`desc=256x256 fmt=87 flags=0x20 layout=0 samples=1`), and
  `CreateSharedHandle(fence)` succeeds (`fd=38`, `handle=0x26`,
  `flags=SHARED`). The strict run now fails at
  `stage=present-failed-before-success`, with no `/tmp/wlcomp-d3d12-present`
  artifact and no native-present proof.
- [x] Capture a WSL-shape shared-heap export attempt that reaches kernel
  NT-share.
  Wave 3 evidence:
  `/tmp/xv6-hyperv-build/wave3-shared-heap-snapshot.log` reaches
  `CreateSharedHandle(resource)` and kernel NT-share; the host returns
  `create_ret:-75`, with `dxg_ntshared_create_attempts=count:1`.
- [x] Capture a WSL-shape app-sync zero-heap export attempt that fails before
  kernel NT-share.
  Wave 3 evidence:
  `/tmp/xv6-hyperv-build/wave3-appsync-export-snapshot.log` fails in userspace
  with `0x80070057`, records `dxg_ntshared_create_attempts=count:0`, and does
  not reach kernel NT-share.
  Next evidence to close the remaining reproduction boxes: a current-tree run
  that reaches `CreateSharedHandle(resource)` for the target shape, records
  both the userspace HRESULT and kernel host return, and proves the failure is
  not list admission, UMD load, renderer selection, or a pre-NT userspace
  parameter rejection.

#### 1B. WSL Trace Collection

##### 1B1. Resource/Heap/Fence Creation Trace

- [ ] Capture a same-adapter WSL trace for D3D12 resource creation.
- [ ] Capture WSL heap creation and placed-resource behavior when used.
- [ ] Capture WSL fence creation and share/open behavior.
- [ ] Capture WSL residency immediately before shared-handle export.

##### 1B2. Shared-Handle Export Trace

- [x] Capture WSL `CreateSharedHandle(resource)`.
- [ ] Capture WSL `CreateSharedHandle(heap)` if issued.
- [x] Capture WSL `CreateSharedHandle(fence)` if issued.
  Wave47 WSL trace report closes the resource and fence `CreateSharedHandle`
  capture rows. Heap trace remains open unless the matching WSL path issues it.

##### 1B3. Variant Parameter Capture

- [ ] Capture WSL shared-heap resource export parameters matching the xv6
  shared-heap variant.
- [ ] Capture WSL app-sync/zero-heap export parameters matching the xv6
  app-sync variant.
  Next evidence to close: a same-adapter WSL trace that names the resource
  creation flags, heap flags, app-sync state, residency/fence state, and
  `CreateSharedHandle` result for each variant.

#### 1C. NT-Share Packet Comparison

##### 1C1. WSL Packet And Ordering Parity

- [x] Add `dxgprobe ntshare_object_kind_matrix` for source-accounted NT-share
  object-kind checks.
- [x] Validate `dxgprobe ntshare_object_kind_matrix` passes in Wave50 runtime
  prep.
- [x] Validate `dxgprobe ntshare_object_kind_matrix` passes in Wave54 pure-C
  artifact.
- [ ] Compare xv6 `LX_DXCREATENTSHAREDOBJECT` command length against WSL.
- [ ] Compare object kind and userspace object handle against WSL.
- [ ] Compare resolved owner process, device, resource, and allocation metadata.
- [ ] Compare return layout, status, and NT-handle output layout.
- [ ] Compare whether WSL dispatches host NT-share before any kernel-side
  runtime-resource sealing decision.
- [ ] Verify xv6 does not treat local pre-NT sealing as the success criterion
  unless same-adapter WSL evidence proves that ordering.

##### 1C2. Diagnostic Coverage For Every NT-Share Attempt

- [ ] Make diagnostics show the userspace object handle for every NT-share
  attempt.
- [ ] Make diagnostics show the resolved typed object-table entry for every
  NT-share attempt.
- [ ] Produce one `/dev/dxg` artifact with process, device, resource,
  allocation, userspace object handle, command length, wire layout, host return,
  and NT-handle output fields for the failed or successful export.
  Current evidence: the shared-heap variant provides the first Wave 3 artifact
  that reaches kernel NT-share and records the host `-75`; the app-sync
  zero-heap variant fails before this section can be compared.
  Wave 4 evidence:
  `/tmp/xv6-hyperv-build/wave4-noext-ntshare-dxg.log` proves the strict WSL
  no-extension 24-byte `CREATENTSHAREDOBJECT` form is sent first
  (`a0 wire:24 ext:0`, `first_label:2`), and the host still returns `-75`.
  This closes only the no-ext-wire proof, not Phase 1 export.
  Wave 5 evidence:
  `/tmp/xv6-hyperv-build/wave5-wslshape-sharedheap-short-dxg.log` confirms the
  WSL-shape shared-heap direct resource still reaches the no-extension 24-byte
  NT-share first, and now proves the passed object handle is the resource/shared
  owner (`pass_is_resource:1 pass_is_shared_owner:1`). The host still returns
  `-75`; actual heap flags are `0x85`, with `prealloc_info=1`.
  Wave 7 evidence:
  `/tmp/xv6-hyperv-build/wave7-direct-dxg.log` shows the direct committed
  `heap_flags=0x1` case reaches `CreateSharedHandle(resource)`, fails
  `0x80004005`, and sends WSL no-extension 24-byte and ext24 NT-share attempts;
  both return `-75`. The pre-NT object classes and owners are good, but Phase 1
  export remains open because no valid NT fd is returned.
  Wave 8 evidence:
  `/tmp/xv6-hyperv-build/wave8-direct-dxg.log`,
  `/tmp/xv6-hyperv-build/wave8-no-clear-value-dxg.log`,
  `/tmp/xv6-hyperv-build/wave8-initial-rt-dxg.log`, and
  `/tmp/xv6-hyperv-build/wave8-reserve-low-va-dxg.log` all reach
  `CreateSharedHandle(resource)`, fail userspace with `hr=0x80004005`, and
  send no-extension 24-byte plus ext24 NT-share attempts that return `-75`.
  Local process/object WSL-model diagnostics remain clean in these export
  failures, and `backend_opengl_submit` remains `0`.
  Wave 9/10 source-audit evidence from Linnaeus: WSL's natural
  `CREATENTSHAREDOBJECT` base envelope is 32 bytes with object offset 24. The
  vmbus-version >=40 extended form is 48 bytes with zero LUID. WSL selects one
  envelope by version; it does not send xv6's current packed 24-byte/object
  offset 20 form followed by retry variants.
  Wave 10 validation evidence:
  build/rootfs/image passed, and
  `/tmp/xv6-hyperv-build/xv6-wave10-ntshare-natural32.vhdx` was deployed with
  6 vCPUs. `/tmp/xv6-hyperv-build/wave10-direct-natural32-dxg.log` shows
  `d3d12sharedsmoke --runtime --case=wsl-success-shape-direct` succeeds
  export-only: `CreateSharedHandle(resource)` returns `hr=0`, `fd=23`,
  `handle=0x17`. Kernel NT-share uses one natural WSL envelope:
  `count=1`, `cmd=32`, `off=24`, `wire=48`, `ext=1`, `eoff=16`, `len=8`,
  `ret=0`, `raw/status=0x400000c0`; `dxg_ntshared_envelope` reports
  WSL natural32/global/process_host/object/result4, retry attempted `0`, and
  `alt_policy:no_layout_retry`. Metadata seals `0->1`, while
  `backend_opengl_submit` remains `0`.
  Next evidence to close: a WSL NT-share packet for the same shared-heap shape
  and a matching xv6 artifact that either succeeds or explains the host `-75`
  against WSL fields.
- [x] Prove strict WSL no-extension 24-byte `CREATENTSHAREDOBJECT` is sent
  first for the shared-heap export attempt.
- [x] Prove the NT-share object handle is the resource/shared-owner handle for
  the shared-heap export attempt.
- [x] Prove Wave 7 direct committed export reaches NT-share with good pre-NT
  object classes and owners.
- [x] Prove Wave 7 direct committed export sends WSL no-ext 24-byte and ext24
  NT-share attempts before host `-75`.
- [x] Prove Wave 8 direct/no-clear/initial-rt/reserve-low-VA cases all reach
  NT-share and fail host no-ext24/ext24 attempts with `-75`.
- [x] Prove Wave 8 local process/object WSL-model diagnostics stay clean across
  the direct/no-clear/initial-rt/reserve-low-VA export failures.
- [x] Audit WSL `CREATENTSHAREDOBJECT` source envelope sizes and object
  offsets.
- [x] Confirm WSL sends one NT-share envelope selected by vmbus version, not
  xv6-style packed/retry variants.
- [x] Replace the normal xv6 NT-share path with WSL natural 32-byte base
  envelope, object offset 24.
- [x] Use the WSL vmbus-version >=40 48-byte zero-LUID extended envelope when
  the negotiated version requires it.
- [x] Remove packed 24-byte/object-offset-20 normal-path dispatch from runtime
  resource export.
- [x] Remove retry-envelope dispatch from the normal runtime export path; keep
  experiments behind an explicit diagnostic mode if still needed.
- [x] Add `/dev/dxg` NT-share envelope diagnostics that print selected
  envelope kind, command length, wire length, extension state, object offset,
  LUID fields, vmbus version, and retry count.
- [x] Validate the new `/dev/dxg` envelope line shows exactly one selected WSL
  envelope for a runtime `CreateSharedHandle(resource)` attempt.
- [x] Re-run direct committed export after envelope replacement and keep host
  return status separate from envelope-parity proof.
- [x] Prove direct committed runtime resource export returns a valid NT fd.
- [x] Prove runtime resource metadata seals after successful host NT-share.
- [x] Explain host `-75` after the proven no-ext 24-byte first attempt by
  comparing object/resource/allocation/export parameters against a successful
  same-adapter WSL export.
- [x] Compare the complete Wave 8 NT-share envelope against a successful WSL
  export, including command route, object handle, owner process, resource
  handle, allocation handle, result length, and retry ordering.

#### 1C2. Runtime Private Blob And Resource-Class Comparison

##### 1C2A. Runtime Private Blob Capture

- [ ] Capture the xv6 runtime private blob for the shared-heap resource that
  reaches no-ext NT-share.
- [ ] Capture the matching same-adapter WSL runtime private blob for a
  successful shared-heap resource export.
- [ ] Compare runtime-private blob length, hash, ADVN/private head, and any
  heap/resource exportability fields.
- [ ] Compare resource private data and per-allocation private data separately
  from runtime private data.
- [ ] Prove the object passed to NT-share remains the resource/shared-owner
  handle after any blob or heap-flag changes.
- [ ] Keep this comparison separate from host NT-share success; matching blobs
  alone do not close Phase 1 export.
  Wave 7 evidence:
  `/tmp/xv6-hyperv-build/wave7-direct-dxg.log` keeps runtime private
  q38/d3c at `0xd00000000/0xd`. The placed shared-heap artifact
  `/tmp/xv6-hyperv-build/wave7-placed-shared-heap-dxg.log` records a shared
  allocation with q38/d3c `2/0`, but it fails before NT-share.
  Wave 8 evidence:
  no-clear changes q50/q58 to zero, but q38/d3c remains
  `0xd00000000/0xd`; initial state `4` does not change q38/d3c; reserve-low-VA
  moves the COM resource pointer above 4G (`resource_ptr_high=0x00007fff`), but
  q38/d3c still remains `0xd00000000/0xd`. Therefore q38 is not simply the COM
  resource pointer high bits.
- [x] Capture Wave 7 direct committed runtime-private q38/d3c values.
- [x] Capture Wave 7 placed shared-heap q38/d3c values from the shared
  allocation artifact.
- [x] Prove no-clear zeroes q50/q58 without changing q38/d3c.
- [x] Prove initial render-target state `4` does not change q38/d3c.
- [x] Prove reserve-low-VA moves the COM resource pointer above 4G without
  changing q38/d3c.
- [x] Rule out q38 as a simple copy of COM resource pointer high bits for the
  Wave 8 reserve-low-VA case.

##### 1C2B. q38/d3c Provenance, Non-Blocking

- [ ] Compare direct committed q38/d3c `0xd00000000/0xd` against same-adapter
  WSL successful export.
- [ ] Compare placed shared-heap q38/d3c `2/0` against same-adapter WSL placed
  shared-heap creation/export.
- [ ] Identify whether q38/d3c values are encoded by runtime private blob,
  resource private data, allocation private data, heap flags, or app-requested
  export shape.
- [ ] Determine whether q38/d3c provenance comes from a DXCore/D3D12 runtime
  token, an NVIDIA private allocation token, or an adapter/session capability.
- [ ] Capture a same-adapter WSL runtime-private blob with q38/d3c for the
  matching direct/no-clear/initial-rt/reserve-low-VA cases.
- [ ] If WSL differs, add one targeted q38/d3c action artifact and rerun the
  same export case without changing other axes.
- [ ] Validate q38/d3c on the successful Wave10 export against a same-adapter
  WSL successful export.
  Wave33 classification: q38/d3c provenance remains useful comparison work, but
  it must not block export/import tracking because later artifacts prove
  successful resource NT export/import and strict-present copy progress.

#### 1D. Runtime Resource Sealing And Ordering

##### 1D1. Host-NT-First Ordering

- [ ] Identify the exact point where WSL has enough metadata to export a
  runtime-created D3D12 resource.
- [ ] Follow WSL host-NT-first/seal-after-success ordering unless validation
  proves WSL seals before the host NT-share call.

##### 1D2. Canonical Shared-Resource Record

- [ ] Define one canonical shared-resource record for runtime private data,
  resource private data, per-allocation private data, allocation sizes/flags,
  owner process/device, local resource handle, exported fd, and NT handle.
- [ ] Validate exact local handle admission before sealing: reject wrong kind,
  stale handle, wrong process, wrong device, and missing allocation ownership
  before the canonical record can publish an fd.
- [ ] Prove all query/open/import paths read from the canonical sealed metadata
  record rather than rebuilding metadata from mutable local handle state.
- [ ] Add close/refcount/import-count diagnostics that show exporter close,
  child/import opens, repeated opens, and final destroy use the same canonical
  record.
- [ ] Compare the canonical record fields and lifetime transitions against a
  same-adapter WSL trace for the matching runtime resource export/open/import.
  Linnaeus Wave63 note: keep these as open granular proof points until a
  Wave61/Wave62-or-newer artifact explicitly proves this canonical-record
  shape. Older Wave45 lifetime diagnostics remain useful background but do not
  close the new exact-record checklist by themselves.

##### 1D3. Seal-Before-Fd-Publish And Mutation Rejection

- [ ] Prove the canonical record is fully sealed before the shared fd is
  published to userspace.
- [ ] Prove no mutation after seal: same-owner add-allocation, private-data
  rewrite, size/flag rewrite, and owner rewrite attempts must be denied and
  counted.
- [ ] Prove fd publish cannot race with incomplete sealing by recording seal
  generation, fd publish generation, and first query/open generation.
- [ ] Prove failed seal/admission paths do not publish an fd and do not leave a
  reusable partial canonical record.

##### 1D4. Metadata Sealing

- [x] Seal runtime private data for the resource.
- [x] Seal resource private data.
- [x] Seal per-allocation private data.
- [x] Seal allocation flags and sizes.
- [x] Add WSL-style seal verification diagnostics that check tracked allocation
  presence and private-data sizes.

##### 1D5. Owner And Lifetime Parity

- [ ] Preserve owner process and owner device.
- [x] Preserve shared-owner lifetime across exporter fd close and explicit
  destroy in the shared-resource lifetime matrix.
- [ ] Fix any `alloc_owner:0x0/0` diagnostics before NT-share dispatch.
- [x] Fix stale `sealed:0` diagnostics for the validated shared-resource
  lifetime/export path.
  Ordering note: kernel pre-NT sealing is disputed. Do not mark or implement
  pre-NT sealing as the acceptance condition unless a same-adapter WSL
  validation proves WSL seals before host NT-share. The default target is WSL
  host-NT-first/seal-after-success ordering.
  Next evidence to close: WSL ordering proof for shared-heap export showing
  whether host NT-share is attempted before local sealing, plus an xv6 artifact
  with matching owner/resource/allocation metadata and ordering.
  Wave45 evidence:
  `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log` proves the WSL-style seal
  verification diagnostic with tracked allocation and private-size checks, and
  the exporter-close shared-resource lifetime matrix passes. This closes the
  validated resource metadata/lifetime slice only; WSL ordering comparison and
  native-present handoff stay open.

#### 1E. Export Case Matrix

##### 1E1. Historical Case Reruns

- [ ] Re-run committed-resource export.
- [ ] Re-run placed-resource/shared-heap export.
- [ ] Re-run fence-only export.
- [ ] Re-run make-resident-before-export.
- [ ] Re-run heap-first export.
- [ ] Tie every failed case to a host status and packet dump.
- [ ] Keep successful cases separate from present/import evidence.
- [ ] Produce one `d3d12sharedsmoke` artifact that covers WSL-shape resource,
  heap, fence, make-resident, and heap-first cases with the same adapter and
  build provenance.
  Current artifact note:
  `/tmp/xv6-hyperv-build/wave-wsl-shape-export-dxg.log` shows the WSL-shape
  runtime/list path fails in userspace admission with adapter count `0` and
  does not reach `CreateSharedHandle`. This does not prove the Phase 1 export
  failure or any NT-share packet behavior, so the case-matrix boxes stay open.
- [x] Add a direct-adapter WSL-shape resource export case so direct admission
  can reach the runtime export path without being blocked by the list-path
  adapter-count `0` failure.
  Wave 2 direct-case evidence:
  `/tmp/xv6-hyperv-build/wave2-wsl-shape-direct-clean-snapshot.log` shows the
  direct WSL-shape clean-first path creates the D3D12 device and resource, then
  `CreateSharedHandle` fails in userspace with `0x80070057` before kernel
  NT-share. `dxg_ntshared_create_attempts` remains `0`, and
  `dxg_d3d12_shared_alloc` remains `seen:0`; this does not close Phase 1
  export or native-present work.
- [ ] Add WSL-shape exportability variants for the direct path so the failure
  can be separated between resource creation parameters, shareability flags,
  residency, heap/resource shape, and NT-export dispatch.
- [ ] Compare successful WSL resource creation and export parameters against
  the xv6 direct WSL-shape case.

##### 1E2. Captured Variant Rows

- [x] Capture the shared-heap WSL-shape variant far enough to reach kernel
  NT-share.
  Wave 3 evidence:
  `/tmp/xv6-hyperv-build/wave3-shared-heap-snapshot.log` reaches NT-share and
  records host `-75`; this is a transport/host-rejection fact only, not a
  successful export.
- [x] Capture the strict no-extension 24-byte NT-share variant far enough to
  prove it is sent first.
  Wave 4 evidence:
  `/tmp/xv6-hyperv-build/wave4-noext-ntshare-dxg.log` records `a0 wire:24
  ext:0`, `first_label:2`, and host `-75`. Phase 1 export remains open because
  no valid NT fd is returned.
  Wave 5 evidence:
  `/tmp/xv6-hyperv-build/wave5-wslshape-sharedheap-short-dxg.log` preserves the
  no-extension 24-byte-first behavior and proves the object-handle/resource
  class (`pass_is_resource:1 pass_is_shared_owner:1`). The host still returns
  `-75`; actual heap flags are `0x85`, with `prealloc_info=1`.
  Wave 7 evidence:
  `/tmp/xv6-hyperv-build/wave7-direct-dxg.log` covers the direct committed
  `heap_flags=0x1` case: it reaches `CreateSharedHandle(resource)`, sends
  no-ext 24-byte and ext24 NT-share attempts, and still receives host `-75`.
  `/tmp/xv6-hyperv-build/wave7-no-heap-flags-dxg.log` fails earlier with
  `0x80070057` and records no NT-share/no shared allocation.
  `/tmp/xv6-hyperv-build/wave7-placed-shared-heap-dxg.log` also fails earlier
  with `0x80070057` and no NT-share, though it records a shared allocation with
  q38/d3c `2/0`. All Wave 7 artifacts keep `backend_opengl_submit 0`.
  Wave 8 evidence:
  direct, no-clear-value, initial-rt, and reserve-low-VA axis rows all reach
  `CreateSharedHandle(resource)`, fail `0x80004005`, and get host `-75` from
  no-ext24/ext24 NT-share attempts. No-clear-value changes q50/q58 to zero;
  initial-rt uses initial state `4`; reserve-low-VA moves the COM resource
  object above 4G; none of these changes alter q38/d3c.
  Wave 10 evidence: direct committed natural32 export returns `hr=0`, `fd=23`,
  `handle=0x17` using the WSL-selected envelope and no layout retry.
- [x] Capture the app-sync zero-heap WSL-shape variant far enough to prove it
  fails before kernel NT-share.
  Wave 3 evidence:
  `/tmp/xv6-hyperv-build/wave3-appsync-export-snapshot.log` fails with
  `0x80070057` in userspace and leaves NT-share attempts at `0`.
- [x] Capture the direct committed `heap_flags=0x1` axis row through NT-share.
- [x] Capture the no-heap-flags axis row as an early userspace failure.
- [x] Capture the placed shared-heap axis row as an early userspace failure
  with shared-allocation q38/d3c evidence.
- [x] Capture the no-clear-value axis row through NT-share.
- [x] Capture the initial-rt axis row through NT-share.
- [x] Capture the reserve-low-VA axis row through NT-share.
- [x] Capture the direct committed natural32 axis row as a successful export.

##### 1E3. Remaining Variant Axes

- [ ] Add a WSL-shape variant that changes one exportability axis at a time
  after shared-heap, starting with heap flags/resource flags.
- [ ] Add a WSL-shape variant that changes app-sync/fence state independently
  from heap/resource shape.
- [ ] Add a WSL-shape variant that changes residency/export ordering
  independently from heap/resource/app-sync shape.
- [ ] Compare every xv6 variant against a successful WSL resource
  creation/export parameter capture.
  Next evidence to close: one artifact per variant showing whether it fails in
  userspace, reaches kernel NT-share, or returns a valid NT fd, plus the
  same-adapter WSL parameter trace for the matching successful export.

#### 1G. Wave 7 Export-Case Axis Matrix

##### 1G1. Axis Matrix Rows To Add

- [ ] Create a one-axis-per-run matrix for heap flags, starting from the
  observed `actual_heap_flags=0x85`.
- [ ] Create a one-axis-per-run matrix for resource flags, keeping heap flags
  fixed.
- [ ] Create a one-axis-per-run matrix for `prealloc_info`, keeping heap and
  resource flags fixed.
- [ ] Create a one-axis-per-run matrix for app-sync/fence state, keeping
  heap/resource/prealloc shape fixed.
- [ ] Create a one-axis-per-run matrix for residency/export ordering, keeping
  heap/resource/prealloc/app-sync shape fixed.
- [ ] For every row, record whether the run fails in userspace, reaches kernel
  NT-share with host `-75`, or returns a valid NT fd.
- [ ] Pair every row with the nearest successful WSL resource creation/export
  parameter trace before declaring an axis WSL-matched.
  Wave 7 axis evidence:
  direct committed `heap_flags=0x1` reaches NT-share and returns host `-75`;
  no-heap-flags fails in userspace with `0x80070057` and no NT-share/no shared
  allocation; placed shared heap fails in userspace with `0x80070057`, no
  NT-share, and shared-allocation q38/d3c `2/0`.
  Wave 8 axis evidence:
  direct, no-clear-value, initial-rt, and reserve-low-VA all reach NT-share;
  all fail with userspace `0x80004005` and host no-ext24/ext24 `-75`.
  Wave 10 axis evidence: direct committed natural32 succeeds export-only with
  `hr=0`, `fd=23`, `handle=0x17`, one selected WSL envelope, and no retry.
- [x] Record Wave 7 direct committed heap-flags axis result.
- [x] Record Wave 7 no-heap-flags axis result.
- [x] Record Wave 7 placed shared-heap axis result.
- [x] Record Wave 8 no-clear-value axis result.
- [x] Record Wave 8 initial-rt axis result.
- [x] Record Wave 8 reserve-low-VA axis result.
- [x] Record Wave 10 direct committed natural32 successful export result.

##### 1G2. Paired WSL Traces

- [ ] Add a paired WSL trace for the direct committed `heap_flags=0x1` row.
- [ ] Add a paired WSL trace for the no-heap-flags row.
- [ ] Add a paired WSL trace for the placed shared-heap row.
- [ ] Add a paired WSL trace for the no-clear-value row.
- [ ] Add a paired WSL trace for the initial-rt row.
- [ ] Add a paired WSL trace for the reserve-low-VA row.

#### 1H. Actual Heap Flags Mismatch Investigation

- [ ] Determine the intended WSL heap flags for the matching shared-heap export
  case.
- [ ] Compare intended WSL heap flags against xv6 `actual_heap_flags=0x85`.
- [ ] Identify whether `0x85` comes from app code, Mesa/D3D12 runtime, QAI
  payload interpretation, or kernel metadata rewriting.
- [ ] Capture one artifact showing heap flags at resource creation, prealloc
  capture, NT-share dispatch, and `/dev/dxg` summary.
- [ ] If flags differ from WSL, rerun the same export case after the flags are
  corrected and keep the export result separate from flag-parity proof.
  Wave 7 evidence: direct committed `heap_flags=0x1` reaches NT-share and host
  `-75`; no-heap-flags fails before shared allocation; placed shared heap fails
  before NT-share but records shared-allocation q38/d3c `2/0`.
- [x] Capture direct committed heap-flags artifact for the Wave 7 axis matrix.
- [x] Capture no-heap-flags artifact for the Wave 7 axis matrix.
- [x] Capture placed shared-heap artifact for the Wave 7 axis matrix.
- [ ] Compare Wave 7 `heap_flags=0x1` and earlier `actual_heap_flags=0x85`
  against the matching WSL heap flags.
- [ ] Decide whether the next heap-flags action should target app-requested
  flags, D3D12 runtime flags, or kernel-captured metadata.

#### 1I. Phase-Exit Validation

- [x] Close this phase only when the real D3D12 runtime
  `CreateSharedHandle(resource)` returns a valid NT fd.
- [x] Query the exported resource fd from a separate open.
- [x] Open the exported resource fd from a separate process when applicable.
- [x] Prove no CPU/readback fallback is used to satisfy the export validator.
  Next evidence to close: a valid runtime resource NT fd plus query/open proof
  from separate opens/processes, with no CPU/readback fallback and no native
  present/FPS/WebKit claim bundled into the export-only artifact.
  Wave12 import-contract evidence:
  `/tmp/xv6-hyperv-build/wave12-resource-import-retry-dxg.log` shows
  import-contract built and ran after retry. Resource export PASS, resource
  import PASS, fence export PASS, fence import FAIL, fence wait FAIL, and
  `dxg_query` FAIL/skipped (`dxg_query_attempted=0`). `backend_opengl_submit`
  remains `0`.
  Wave33 strict-present evidence closes the no-CPU-readback duplicate:
  shared-resource import, dxg monitored acquire, and D3D12 GPU copy complete
  with no CPU readback. This remains export/import evidence only; no native
  display completion is claimed.
  Wave38 strict-present evidence confirms resource NT-share success:
  `runtime CreateSharedHandle(resource) ok fd=37 handle=0x25`, resource
  `OpenSharedHandle` succeeds, `/dev/dxg` reports
  `dxg_ntshared_wsl_exact=ext32_zero_luid_natural:1/a0`,
  `dxg_ntshared_envelope ... ret:0/status:0x40000180/raw:0x40000180`, and
  `dxg_d3d12_shared_alloc=seen:1 ... runtime:264 res_priv:0 alloc_priv:594
  out_priv:594`.
- [ ] Validate list-default export path reaches `CreateDevice` and
  `CreateSharedHandle(resource)` before judging NT-share envelope parity.
- [ ] After list-default reaches runtime export, validate it uses the same
  natural32 envelope and succeeds or has a separate diagnosed blocker.
- [ ] Validate successful export does not depend on stale q38/d3c assumptions.

#### 1J. Shared Resource Open And Import Follow-Up

- [x] Query the successful exported resource fd from a separate `/dev/dxg` open.
- [x] Open the successful exported resource fd from a separate process.
- [x] Import/open the successful exported resource fd on the compositor's own
  D3D12 device.
- [x] Validate the imported resource dimensions, format, allocation count, and
  adapter LUID.
- [x] Create or export the matching acquire fence for the successful resource.
- [x] Open/import the matching fence on the compositor side.
- [x] Prove fence target satisfaction before compositor use.
- [ ] Keep resource open/import/fence validation separate from native-present
  and FPS/WebKit gates.
  Wave38 split: D3D12 fence NT export succeeds, but direct D3D12
  `OpenSharedHandle(fence)` still fails with `hr=0xffffffff80070057`
  (`errno=22`). Treat this as the known WSL-reproducible/expected-failing
  direct D3D12 fence-open lane unless a native path later requires it.
  Wave11 source-comparison note: `OPENRESOURCE`, `OPENSYNCOBJECT`, and
  `DESTROYNTSHAREDOBJECT` still need WSL-natural layout fixes before the
  successful exported resource can be treated as a complete import/open path.
  The `d3d12sharedsmoke` import-contract case was added in source but has not
  been built or validated yet.
- [x] Replace `OPENRESOURCE` handling with the WSL-natural layout.
- [ ] Replace `OPENSYNCOBJECT` handling with the WSL-natural layout.
- [ ] Replace `DESTROYNTSHAREDOBJECT` handling with the WSL-natural layout.
- [x] Build and validate the new `d3d12sharedsmoke` import-contract case.
- [x] Use the import-contract case to prove exported resource query/open works
  after the WSL-natural open layouts are fixed.
- [ ] Use the import-contract case to prove exported fence/sync-object open
  works after the WSL-natural open layouts are fixed.
  Wave12 resource-open evidence: `dxg_openresource_envelope` reports WSL-natural
  `OPENRESOURCE` `cmd:48 wire:64 ext:1 ret:0` with `out_res` and `out_alloc`.
  Wave12 fence-open evidence: `dxg_opensync_envelope` is all zero, so fence
  import failed before kernel `OPENSYNCOBJECT`. Wave12 destroy evidence:
  `dxg_sharedfd_close` / destroy-NT still returns `-75` with
  `cmd:32 wire:48 ext:1 result:4`.
  Wave14 evidence: resource export/import PASS and fence export PASS remain
  intact, but fence import still fails before kernel `OPENSYNCOBJECT`
  (`dxg_opensync_gate=count:0`). Sharedsync export now has nonzero
  global/host NT handles, destroy-NT natural close still returns `-75`, and
  `backend_opengl_submit` remains `0`.
  Wave15 evidence: kernel/rootfs build passed after one `d3d12sharedsmoke`
  prototype fix, and `/tmp/xv6-hyperv-build/xv6-wave15-fence-admission.vhdx`
  was deployed with 6 vCPUs. Normal import-contract artifact
  `/tmp/xv6-hyperv-build/wave15-import-contract-dxg.log` shows resource export
  PASS, resource import PASS, fence export PASS, and fence import FAIL. Fence
  fd `fstat` succeeds, the fd is a character device, `fcntl_flags=0x1`, and
  sharedsync export records `monitor:1`, `cloexec:1`, and nonzero
  global/host_nt. `OpenSharedHandle(fence)` fails `hr=0x80070057`; the last
  kernel ioctl is `nr=65 LX_DXQUERYRESOURCEINFOFROMNTHANDLE ret=-22`, while
  `dxg_opensync_gate=count:0`, so `OPENSYNCOBJECT` is still not reached.
  `backend_opengl_submit` remains `0`. Independent-device artifact
  `/tmp/xv6-hyperv-build/wave15-after-independent-dxg.log` fails earlier at
  `D3D12CreateDevice 0x80004005`.
  Wave16 evidence: `/tmp/xv6-hyperv-build/wave16-import-contract-dxg.log`
  preserves resource export PASS, resource import PASS, fence export PASS, and
  fence import FAIL. It proves the WSL-like fd fops split is present:
  sharedsync/sync fd uses `fops:1`, while resource import fd reports
  `fd_fops:2`. Query-resource-on-sync-fd returns `-EINVAL`/`ret:-22` before
  `OPENSYNCOBJECT`, with `dxg_queryresource_nt ... kind:1 fops:1 sync_probe:1`.
  `backend_opengl_submit` remains `0`.
  Wave16 variant note:
  `/tmp/xv6-hyperv-build/wave16-after-variants-dxg.log` is unreliable as a
  combined artifact because one path prints usage after the fence-first run.
  `/tmp/xv6-hyperv-build/wave16-fence-dup-dxg.log` and the fence-first portion
  of the combined artifact both fail earlier at `D3D12CreateDevice
  0x80004005`, so they do not test fence admission.
  Wave18 evidence: `/tmp/xv6-hyperv-build/wave18-import-contract-dxg.log`
  proves VFS custom fd identity is implemented and visible to userspace:
  resource fd `readlink_target=anon_inode:dxgresource`, fence fd
  `readlink_target=anon_inode:dxgsyncobj`, both `mode=0100600` and `is_chr=0`.
  Kernel `dxg_sharedfd_shape` reports `generic_custom_fd:0 vfs_name:1
  anon_inode:1`, while normal import-contract still fails fence import at the
  same D3D12 `OpenSharedHandle(fence)` admission point.
  Wave22 evidence:
  `/tmp/xv6-hyperv-build/wave22-import-contract-runtime-device-smoke.log`
  preserves resource export PASS, resource import PASS, and fence export PASS.
  D3D12 `OpenSharedHandle(fence)` still fails with `0x80070057`, matching the
  previously captured WSL-reproducible path. Direct dxg fence import now reaches
  `LX_DXOPENSYNCOBJECTFROMNTHANDLE2` for both imported-resource-device and
  runtime-device override roles, but both return `errno=22` /
  `STATUS_INVALID_PARAMETER`; Phase 1 fence import therefore remains open.
  `/tmp/xv6-hyperv-build/wave22-post-dxg.log` records the host envelope:
  `dxg_opensync_envelope ... ret:-22 status:0xc000000d ... flags:0x13`, and
  `dxg_opensync_shape` proves user flags are forwarded exactly as
  `user_flags:0x13 wire_flags:0x13 forced:0`.
- [ ] Diagnose why fence import fails before kernel `OPENSYNCOBJECT` dispatch.
- [x] Add userspace-side logging for fence fd kind, fence handle, fence value,
  and import preconditions before the `OPENSYNCOBJECT` ioctl.
- [x] Produce a retry artifact where `dxg_opensync_envelope` is nonzero for the
  fence import attempt.
- [ ] Compare `DESTROYNTSHAREDOBJECT` close path result against WSL for resource
  and sync shared fds.
- [ ] Fix or explain destroy-NT close returning `-75` with `cmd32 wire48 ext1
  result4`.
- [ ] Keep destroy-NT close parity separate from resource import success.

#### 1K. Fence OpenSharedHandle Admission

- [ ] Capture the userspace `OpenSharedHandle` call path for the exported fence
  fd before any kernel ioctl.
- [x] Log the fence fd kind, sharedsync kind, global handle, host NT handle, and
  target fence value at the userspace admission point.
- [x] Explain why Wave15 reaches `LX_DXQUERYRESOURCEINFOFROMNTHANDLE ret=-22`
  on the fence fd before `OPENSYNCOBJECT`.
  Wave18 WSL live trace proves the same D3D12 fence-open path queries resource
  info on the sync fd, receives `-EINVAL`, returns `0x80070057`, and does not
  call `OPENSYNCOBJECT`.
- [ ] Add a negative userspace-admission artifact for wrong fd kind or missing
  sharedsync metadata.
- [x] Produce a direct dxg sync-open artifact where userspace reaches the kernel
  `OPENSYNCOBJECTFROMNTHANDLE2` ioctl without relying on D3D12
  `OpenSharedHandle(fence)`.

#### 1L. Kernel Sharedsync Fd Semantics

- [x] Verify the exported fence fd records the sharedsync object kind expected
  by `OPENSYNCOBJECT`.
- [x] Verify the exported fence fd carries nonzero global and host NT handles
  through fd lookup and close.
- [x] Verify sharedsync fd metadata survives resource import, fence export, and
  fence import attempts without being cleared or retyped.
- [x] Add `/dev/dxg` diagnostics for sharedsync fd refs, owner process, device,
  global handle, host NT handle, and fence target value.
- [x] Verify sharedsync export records monitored-fence and close-on-exec state.
- [x] Verify exported resource and fence fds expose WSL-like anon-inode VFS
  identity instead of character-device identity.
  Wave18 evidence: d3d12sharedsmoke sees `anon_inode:dxgresource` and
  `anon_inode:dxgsyncobj` readlink targets, `mode=0100600`, and `is_chr=0`;
  kernel `dxg_sharedfd_shape` confirms `vfs_name:1 anon_inode:1`.
  Wave22 evidence keeps the same fd identity in
  `/tmp/xv6-hyperv-build/wave22-import-contract-runtime-device-smoke.log` and
  `/tmp/xv6-hyperv-build/wave22-post-dxg.log`: resource fd
  `anon_inode:dxgresource`, fence fd `anon_inode:dxgsyncobj`,
  `generic_custom_fd:0 vfs_name:1 anon_inode:1`.
- [ ] Keep sharedsync fd metadata proof separate from fence import success.

#### 1L2. Resource Query On Sync Fd Behavior

- [x] Determine whether D3D12 `OpenSharedHandle(fence)` should call
  `LX_DXQUERYRESOURCEINFOFROMNTHANDLE` on a sync/fence NT fd.
- [x] Capture same-adapter WSL behavior for resource-query-on-sync-fd,
  including HRESULT and kernel return status.
- [x] Decide whether xv6 should reject resource query on sync fd earlier in
  userspace, translate it to sync-open metadata, or forward a WSL-matched
  kernel query.
- [x] Add a diagnostic that distinguishes resource NT fd, sync NT fd, and
  unknown char-device fd at the query-resource-from-NT-handle path.
- [x] Document that the chosen behavior no longer requires D3D12 fence
  `OpenSharedHandle` to reach `OPENSYNCOBJECT` for this WSL-matched failure
  shape.
  Wave16 evidence: normal import-contract proves resource and sync fd fops are
  distinguished (`fd_fops:2` for resource import, `fops:1` for sync/sharedsync)
  and query-resource-on-sync-fd returns `ret:-22` with `sync_probe:1` before
  `OPENSYNCOBJECT`.
- [x] Add diagnostics distinguishing resource NT fd and sync NT fd at the
  query-resource-from-NT-handle path.
- [x] Decide whether `ret:-22` on sync fd is the intended WSL-compatible
  response or should be translated into the fence-open path.
- [x] Document that D3D12 `OpenSharedHandle(fence)` for this shape is expected
  not to reach `OPENSYNCOBJECT` when it follows the WSL-reproduced
  query-resource-on-sync path.
  Wave18 WSL live trace: the same D3D12
  `OpenSharedHandle(fence, IID_ID3D12Fence)` path returns `0x80070057` after
  `QUERYRESOURCEINFOFROMNTHANDLE -EINVAL` and does not call
  `OPENSYNCOBJECT`. Therefore this D3D12 fence-open failure is WSL-reproducible
  and should not block on fake success; direct dxg sync-open validation remains
  separate.
- [x] Produce a direct dxg sync-open artifact that reaches
  `OPENSYNCOBJECTFROMNTHANDLE2` for the same exported shared sync fd.

#### 1L3. WSL Return/Error Parity For Fence Admission

- [x] Capture WSL HRESULT for `OpenSharedHandle(fence)` with the same monitored
  fence shape.
- [x] Capture WSL kernel return for any pre-`OPENSYNCOBJECT` query/resource-info
  call on the fence fd.
- [x] Compare xv6 `hr=0x80070057` and kernel `ret=-22` against WSL.
- [x] If WSL also returns an error, document whether the import-contract should
  expect fence import failure for this shape.
- [ ] If WSL succeeds, identify the first xv6/WSL divergence before
  `OPENSYNCOBJECT`.
  Wave18 WSL live trace matches xv6 for this D3D12 fence admission path:
  `OpenSharedHandle(fence, IID_ID3D12Fence)` returns `0x80070057` after
  `QUERYRESOURCEINFOFROMNTHANDLE -EINVAL` and does not call
  `OPENSYNCOBJECT`.

#### 1L4. OPENSYNCOBJECT Retry Artifact

- [x] Produce a fresh import-contract run where fence admission reaches kernel
  `OPENSYNCOBJECT`.
- [x] Require `dxg_opensync_gate=count>0` in that retry artifact.
- [x] Record `dxg_opensync_envelope` command length, wire length, extension
  state, result length, host return, global handle, host NT handle, and output
  sync handle.
- [ ] Keep OPENSYNCOBJECT retry proof separate from fence wait success.
  Wave16 note: fence-dup and fence-first variants do not satisfy this retry
  bucket because both fail before fence admission at `D3D12CreateDevice
  0x80004005`; the combined after-variants artifact is not reliable enough to
  close any `OPENSYNCOBJECT` item.
  Wave22 direct dxg evidence satisfies the reach-host retry shape:
  `dxg_opensync_gate=count:2` and `dxg_opensync_envelope=route:global cmd:40
  wire:56 ext:1 ... ret:-22 status:0xc000000d ... flags:0x13 out_sync:0x0`.
  This does not close fence import or fence wait because the host returns
  `STATUS_INVALID_PARAMETER`.

#### 1L5. Direct WSL Sync-Open Validation

- [x] Implement a direct dxg `OPENSYNCOBJECTFROMNTHANDLE2` fence-import path for
  the exported shared sync fd, separate from D3D12 `OpenSharedHandle(fence)`.
- [x] Validate the direct sync-open path with a fresh artifact that records the
  input shared sync fd identity, `OPENSYNCOBJECTFROMNTHANDLE2` wire envelope,
  host return, and output sync handle.
- [x] Prove direct sync-open forwards user flags exactly as requested.
- [ ] Diagnose why imported-resource-device direct sync-open returns
  `STATUS_INVALID_PARAMETER`.
- [ ] Diagnose why runtime-device direct sync-open returns
  `STATUS_INVALID_PARAMETER`.
- [ ] Keep direct dxg sync-open success separate from the WSL-reproducible D3D12
  `OpenSharedHandle(fence)` `0x80070057` result.
  Wave22 evidence: the smoke artifact records direct dxg attempts for
  `device_role=imported_resource_device` and `device_role=runtime_device_override`,
  both with `flags=0x13 shared=1 nt_security=1 no_signal=1`, both failing
  `errno=22`. `/tmp/xv6-hyperv-build/wave22-post-dxg.log` records
  `dxg_opensync_shape ... user_flags:0x13 wire_flags:0x13 forced:0`, so the
  current blocker is host `STATUS_INVALID_PARAMETER`, not missing ioctl reach
  or flag rewriting.
- [x] Add flags-variant control for direct dxg sync-open attempts.
- [x] Add compact import-case aliases for the flags/device variants.
- [x] Validate kernel `dxg_opensync_target` diagnostics.
- [x] Prove the `0x483` flags variant reaches imported-resource-device direct
  sync-open and still fails host `STATUS_INVALID_PARAMETER`.
- [x] Prove the `0x483` flags variant reaches runtime-device direct sync-open
  and still fails host `STATUS_INVALID_PARAMETER`.
- [x] Prove source flags, source device, and adapter match diagnostics are
  present for the runtime-device `0x483` attempt.
  Wave23 evidence:
  `/tmp/xv6-hyperv-build/wave23-import-flags483-smoke.log` uses the compact
  alias `d3d12sharedsmoke --runtime --case=import --dxgflags=0x483` and sends
  the imported-resource-device direct dxg attempt with `flags=0x483`, which
  fails `errno=22`. `/tmp/xv6-hyperv-build/wave23-flags483-post-dxg.log`
  records `dxg_opensync_envelope ... status:0xc000000d ... flags:0x483`,
  `dxg_opensync_shape ... user_flags:0x483 wire_flags:0x483 forced:0`, and
  `dxg_opensync_target ... adapter_match:1`.
  Wave24 evidence:
  `/tmp/xv6-hyperv-build/wave24-runtime-device-flags483-smoke.log` uses the
  compact alias `--case=import --dxgdev=0x40000000 --dxgflags=0x483` and sends
  both imported-resource-device and runtime-device override direct dxg attempts;
  both fail `errno=22`. The post-dxg artifact records runtime-device target
  diagnostics with `same:1 adapter_match:1 source_flags:0x483 source_type:5
  monitor:1 ... status:0xc000000d`.

#### 1M. WSL OPENSYNCOBJECT Comparison

- [ ] Capture same-adapter WSL `OPENSYNCOBJECT` wire layout for an NT-shared
  monitored fence.
- [ ] Compare xv6 `OPENSYNCOBJECT` command length, wire length, extension
  fields, object/global handle fields, and result layout against WSL.
- [x] Produce an xv6 artifact where `dxg_opensync_envelope` is nonzero and the
  host return is recorded.
- [ ] If host returns failure, tie it to WSL field differences instead of
  source flags/device/adapter-match diagnostics or userspace pre-ioctl
  admission.
- [ ] Keep WSL `OPENSYNCOBJECT` parity separate from resource `OPENRESOURCE`
  success.
  Wave22 artifact:
  `/tmp/xv6-hyperv-build/wave22-post-dxg.log` records nonzero
  `dxg_opensync_envelope` with host `status:0xc000000d`; WSL field comparison
  remains open.
  Wave24 artifact:
  `/tmp/xv6-hyperv-build/wave24-runtime-device-flags483-post-dxg.log` narrows
  the host failure further: `dxg_opensync_target` shows the runtime-device
  direct attempt has `same:1`, `adapter_match:1`, `source_flags:0x483`, and
  source type/monitor/global/NT metadata, yet the host still returns
  `STATUS_INVALID_PARAMETER`.

#### 1M1. WSL Wire Comparison

- [ ] Capture same-adapter WSL `OPENSYNCOBJECTFROMNTHANDLE2` for a pure-C
  D3DKMT shared sync object.
- [ ] Capture same-adapter WSL `OPENSYNCOBJECTFROMNTHANDLE2` for the D3D12
  shared fence shape.
- [ ] Compare command id, command length, wire length, extension header,
  result length, device handle, process handle, global/shared handle, NT handle
  source, flags, and output sync layout.
- [ ] Decide whether xv6 should use `host_shared_handle`, `host_nt`, or another
  WSL-derived handle field as the `global` input.
- [ ] Keep WSL wire comparison separate from the fact that xv6 now reaches the
  host and records diagnostics.

#### 1M2. Host Shared Handle Vs NT-Handle Semantics

- [ ] Determine whether OPENSYNC should pass `host_shared_handle` or `host_nt`
  for pure-C shared sync open.
- [ ] Determine whether D3D12 fence direct import uses the same handle source as
  pure-C shared sync open.
- [ ] Add one targeted artifact that swaps only the candidate handle source and
  records host status.
- [ ] Preserve the cached NT-share entry and fd identity diagnostics for every
  handle-source variant.
- [ ] Treat `STATUS_INVALID_PARAMETER` with both preserved metadata and same
  adapter as a host-contract mismatch until WSL comparison proves otherwise.

#### 1M3. Pure-C Sync Matrix

- [x] Capture a fresh pure-C `dxgprobe --sync-only` artifact after the current
  OPENSYNC diagnostics landed.
- [x] Prove pure-C resource share/query/open still passes in the same artifact.
- [x] Prove pure-C `share_sync_nt` still passes in the same artifact.
- [x] Prove pure-C `open_sync_nt` fails at host OPENSYNC
  `STATUS_INVALID_PARAMETER`.
- [x] Prove the pure-C failing sync-open attempt has same process, same device,
  same adapter, monitored type `5`, preserved flags `0x3`, global
  `0x40000100`, and NT handle `0x40000140`.
- [x] Add a pure-C matrix row varying only OPENSYNC flags while keeping the same
  shared sync object and device.
- [x] Validate `dxgprobe opensync_layout_source_matrix` passes in Wave54 pure-C
  artifact.
- [ ] Add a pure-C matrix row varying only target device/process selection while
  keeping flags and handle source fixed.
- [x] Classify child-owned same-numeric device sync-open acceptance as
  WSL-compatible.
- [x] Add inherited `/dev/dxg` TGID guard coverage for target device/process
  sync-open isolation.
- [ ] Add a pure-C matrix row varying only handle source
  (`host_shared_handle` vs `host_nt`) while keeping flags and device fixed.
- [ ] Keep pure-C sync import success separate from D3D12 fence import success.
  Wave25 pure-C matrix evidence:
  `/tmp/xv6-hyperv-build/wave25-dxgprobe-sync-default.log` shows default
  `dxgprobe --sync-only` now has `share_sync_nt ok`, `open_sync_nt ok`, and
  `open_sync_nt_child ok`. The post-dxg artifact records WSL-natural OPENSYNC
  offsets (`off_dev:24 off_global:28 off_flags:36`) and successful
  `dxg_opensync_envelope ... ret:0 status:0x0 ... flags:0x3`.
  `/tmp/xv6-hyperv-build/wave25-dxgprobe-sync-scf13.log` proves create/open
  flags `0x13` succeeds, and
  `/tmp/xv6-hyperv-build/wave25-dxgprobe-sync-sof13.log` proves
  `--scf=0x3 --sof=0x13` succeeds. Invalid open flags remain separate:
  `/tmp/xv6-hyperv-build/wave25-dxgprobe-sync-sof483.log` fails
  `open_sync_nt` with `open_flags=0x483`, and
  `/tmp/xv6-hyperv-build/wave25-dxgprobe-sync-matrix-post-dxg.log` records
  host `STATUS_INVALID_PARAMETER` for that invalid/open-flags case.
  Fresh Wave25 discriminator:
  `/tmp/xv6-hyperv-build/wave25-fresh-dxgprobe-sync-scf483-sof13.log` reaches
  sync creation/share with source flags `0x483`, then fails OPENSYNC even when
  open flags are valid `0x13`. The dxg artifact
  `/tmp/xv6-hyperv-build/wave25-fresh-dxgprobe-sync-scf483-sof13-dxg.log`
  proves WSL-natural offsets (`off_dev:24 off_global:28 off_flags:36`), same
  process/device/adapter, `source_flags:0x483`, `user_flags:0x13`, and host
  `STATUS_INVALID_PARAMETER`.
  Wave24 fresh pure-C evidence:
  `/tmp/xv6-hyperv-build/wave24-fresh-dxgprobe-sync-only-dxg.log` shows
  resource share/query/open PASS and `share_sync_nt` PASS, but `open_sync_nt`
  FAIL. `/dev/dxg` records `dxg_opensync_envelope ... ret:-22
  status:0xc000000d ... flags:0x3`, `dxg_opensync_shape ...
  user_flags:0x3 wire_flags:0x3 forced:0`, and `dxg_opensync_target ...
  same:1 adapter_match:1 source_flags:0x3 source_type:5 monitor:1
  global:0x40000100 nt:0x40000140 status:0xc000000d`. This proves direct
  D3D12 fence import is not the only issue; pure D3DKMT shared sync import is
  still a Phase 1 blocker.
  Wave55 import-negative evidence initially kept target device/process
  isolation open because child/foreign-process device sync open was accepted.
  Wave56 narrows this: same-numeric child-owned device acceptance is
  WSL-compatible, while inherited `/dev/dxg` TGID guard proof still needs a
  dedicated row.
  Wave57 closes the inherited TGID guard row:
  `/tmp/xv6-hyperv-build/wave57-import-negative.log` reports
  `opensync_child_inherited_parent_dxg_matrix` PASS with
  `kernel_namespace_diag_present=1` and reject count.

#### 1M4. Kernel Fix And Evidence

- [x] Implement the minimal OPENSYNC handle-source/wire fix indicated by the
  WSL comparison and pure-C matrix.
- [x] Re-run pure-C `dxgprobe --sync-only` and require resource
  share/query/open PASS, `share_sync_nt` PASS, and `open_sync_nt` PASS.
- [ ] Re-run D3D12 import-contract direct sync-open and require host OPENSYNC
  no longer returns `STATUS_INVALID_PARAMETER` for the same object shape.
- [x] Preserve `dxg_opensync_target`, `dxg_opensync_shape`, and
  `dxg_opensync_envelope` in the post-fix artifact.
- [ ] Do not advance native present, FPS, or WebKit gates from OPENSYNC-only
  evidence.
  Wave25 build/deploy evidence: `/tmp/xv6-hyperv-build/xv6-wave25-opensync-layout-fix.vhdx`
  was built from `/tmp/xv6-hyperv-build` and deployed with 6 vCPUs and 3GB
  startup memory. The pure-C fix is WSL-natural OPENSYNC command offsets only:
  default/`0x13` pure-C sync-open variants pass, while invalid/open flags
  `0x483` still fail as a separate classification.
  The fresh `--scf=0x483 --sof=0x13` discriminator shows source flags `0x483`
  are sufficient to reproduce the host failure even with WSL-natural layout and
  valid open flags.

#### 1M5. D3D12 Fence Parity Note

- [x] Keep D3D12 `OpenSharedHandle(fence)` `0x80070057` documented as
  WSL-reproducible for the resource-query-on-sync path.
- [x] Keep direct D3D12 fence import blocked until the pure-C shared sync matrix
  either passes or identifies a D3D12-specific remaining mismatch.
- [x] After pure-C `open_sync_nt` passes, re-run D3D12 direct fence import for
  imported-resource-device and runtime-device targets.
- [ ] Require D3D12 fence wait/target proof only after direct fence import
  returns a valid sync handle.
  Wave25 D3D12 evidence:
  `/tmp/xv6-hyperv-build/wave25-d3d12-import-flags13.log` still fails direct
  fence import on the imported-resource device with open flags `0x13`.
  `/tmp/xv6-hyperv-build/wave25-d3d12-import-runtime-device-flags13.log` still
  fails both imported-resource-device and runtime-device direct fence import
  with open flags `0x13`. The post-dxg artifact records D3D12 source sync flags
  `0x483` and host `STATUS_INVALID_PARAMETER`; therefore D3D12 fence direct
  import remains open even though pure-C WSL-natural sync import is fixed.
  Fresh Wave25 discriminator evidence means D3D12
  `CreateSharedHandle(fence)` direct OPENSYNC should be tracked as a
  WSL-reproduced/expected-fail path when the source sync flags are `0x483`.
  The remaining implementation path should not fake D3D12 fence import success;
  it should route compositor acquire through WSL-compatible sync-file or pure
  dxg monitored-sync semantics.
  Wave38 strict-present evidence repeats the expected-failing direct D3D12
  fence-open result: `OpenSharedHandle(fence)` returns
  `hr=0xffffffff80070057` / `errno=22` even though fence
  `CreateSharedHandle` succeeds.

#### 1M6. Sync File And Compositor Acquire Path

- [x] Implement WSL-compatible sync_file ioctls for exporting/importing acquire
  fences where D3D12 shared-fence OPENSYNC is expected to fail with source
  flags `0x483`.
- [x] Add a pure dxg monitored-sync acquire path that uses the valid
  WSL-natural sync-open shape instead of the D3D12 `0x483` source-sync shape.
- [x] Prove default `dxgprobe --syncfile` creates a sync_file, opens it in the
  parent process, and opens it in the child process.
- [x] Prove `dxgprobe --syncfile --scf=0x13` creates and opens the sync_file
  with source flags `0x13`.
- [x] Prove sync_file wait succeeds when a usable context is found.
- [x] Prove wait is reported as `not_attempted_no_context`, not as a failure,
  when no context is supplied.
- [x] Prove `/dev/dxg` records open-from-syncfile shape with nonzero CPU/GPU
  fence addresses.
- [x] Teach the compositor import path to accept acquire synchronization via
  sync_file or pure dxg monitored sync without treating D3D12 fence direct
  import failure as fatal for the resource import contract.
- [x] Produce a validation artifact showing resource export/import PASS,
  acquire synchronization via sync_file or pure dxg monitored sync, and
  `backend_opengl_submit 0` until native present is separately validated.
- [x] Prove syncfile acquire import and compositor GPU-copy proof in the strict
  present path without claiming display handoff.
- [x] Keep sync_file/compositor-acquire evidence separate from native present,
  FPS, and WebKit gates.
  Wave26v2 build/deploy evidence: kernel/rootfs and hyperv-image builds passed
  from `/tmp/xv6-hyperv-build`, the archived image is
  `/tmp/xv6-hyperv-build/xv6-wave26-syncfile-acquire-v2.vhdx`, and it was
  deployed to Hyper-V with 6 vCPUs and 3GB startup memory. Boot/fbstat still
  reports `backend_opengl_submit 0`.
  Wave26v2 sync_file evidence:
  `/tmp/xv6-hyperv-build/wave26v2-dxgprobe-syncfile-default.log` shows default
  `dxgprobe --syncfile` create PASS, open-from-syncfile PASS, and child open
  PASS. Wait is `not_attempted_no_context`, with matrix `wait_rc=-2
  wait_state=not_attempted_no_context`, so it is not a failure.
  `/tmp/xv6-hyperv-build/wave26v2-dxgprobe-syncfile-scf13.log` shows
  `--scf=0x13` create/open/child-open PASS with source flags `0x13`.
  `/tmp/xv6-hyperv-build/wave26v2-dxgprobe-syncfile-try-context.log` finds
  usable context `0x40000c80`, reports `sync_file_wait ok ... rc=0`, and still
  has open/child-open PASS. The dxg artifacts record `dxg_syncfile_last ...
  ret:0 ... source_flags:0x3/0x13 open_flags:0x13 ... cpu/gpu nonzero`.
  Wave26v4 through Wave31b stale-item update: compositor acquire via dxg
  syncfile is now validated in the Wayland/native-present path, resource
  export/import remains PASS, and `backend_opengl_submit` remains `0`. This
  does not close native display handoff, frame callback/release, FPS, or
  WebKit gates.
  Wave33 evidence closes the strict-path proof: resource NT-share/open/import,
  dxg monitored acquire, and compositor-owned D3D12 GPU copy complete, but
  present-source commit still fails closed and `backend_opengl_submit` remains
  `0`.

#### 1N. Destroy-NT Close Parity

- [x] Capture same-adapter WSL `DESTROYNTSHAREDOBJECT` behavior for resource
  NT fds.
- [ ] Capture same-adapter WSL `DESTROYNTSHAREDOBJECT` behavior for sync/fence
  NT fds.
- [x] Compare xv6 natural close `cmd32 wire48 ext1 result4` against WSL close
  layout and expected result length.
- [x] Determine whether the resource-fd close `-75` path remains a current
  host contract mismatch.
- [x] Fix resource-fd destroy close `-75` for the validated resource fd path.
- [x] Add retained process namespace clearing diagnostics for process-lifetime
  close parity.
- [ ] Resolve any remaining process-lifetime close parity mismatch after the
  retained namespace diagnostic is compared with WSL.
- [ ] Resolve sync/fence NT fd close parity separately from the validated
  resource fd path.
- [ ] Compare pure-C shared sync fd close/destroy behavior against D3D12 fence
  fd close/destroy behavior.
- [x] Add `dxgprobe destroy_separation_matrix` for source-accounted destroy
  separation checks.
- [x] Validate `dxgprobe destroy_separation_matrix` passes in Wave50 runtime
  prep.
- [x] Validate `dxgprobe destroy_separation_matrix` passes in Wave54 pure-C
  artifact.
- [x] Prove WSL-natural destroy-sync cleanup for sync_file/pure dxg monitored
  sync objects.
- [x] Keep destroy-NT close parity separate from OPENSYNC open failure and
  resource `OPENRESOURCE` success.
  Wave26v2 destroy evidence: `/dev/dxg` records WSL-natural destroy-sync
  cleanup, including `dxg_destroy_last ... sync_len:8 sync_ret:0
  sync_cmd_len:32 sync_wire:48 sync_ext:1 sync_eoff:16`; the try-context dxg
  artifact records `dxg_host_cmd_counts ... destroysync:10`.
  Wave36 evidence closes only the validated resource-fd close path:
  `/tmp/xv6-hyperv-build/wave36-qai-wsl-shaped.log` reports resource
  `destroy_ret:0`. Process lifetime remains open because xv6 still reports
  `destroyprocess:0`/suppression.
  Wave45 evidence:
  `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log` adds the retained process
  namespace clearing diagnostic. Keep final process-lifetime parity open until
  the diagnostic is matched against WSL behavior.
  Wave45 source/layout parity report closes the resource NT-fd destroy layout
  comparison and separation from OPENSYNC/resource-open status. Sync/fence close
  and process-lifetime runtime parity remain open.

#### 1O. Import-Contract Revalidation

- [x] Re-run import-contract after fence userspace admission reaches the kernel
  ioctl.
- [x] Require resource export PASS and resource import PASS in the same fresh
  run.
- [x] Require fence export PASS in the same fresh run.
- [x] Require fence export PASS and syncfile-acquire fence import PASS in the
  same fresh run.
- [ ] Require fence wait PASS or a host-status diagnosis tied to WSL
  `OPENSYNCOBJECT` fields.
- [x] Require direct D3D12 `OpenSharedHandle(fence)` success only if the chosen
  native path proves it is required; current evidence treats the `0x80070057`
  path as WSL-reproducible and bypassed by syncfile acquire.
- [x] Require `dxg_query` to be attempted or explicitly proven irrelevant to
  the import contract.
- [x] Preserve `backend_opengl_submit 0` until compositor import and native
  present are validated.
  Wave22 import-contract result: `resource_export=PASS resource_import=PASS
  fence_export=PASS d3d12_fence_import=FAIL fence_wait=FAIL dxg_query=PASS
  dxg_query_attempted=1`. Direct dxg fence import is attempted for both device
  roles but fails host `STATUS_INVALID_PARAMETER`, so Phase 1 stays open.
  Wave23/Wave24 evidence preserves resource export/import/fence export PASS
  while testing `0x483` direct dxg sync-open variants. Both the imported-device
  and runtime-device attempts still fail `STATUS_INVALID_PARAMETER`, so direct
  fence import, fence wait, and all native-present gates remain open.
- [x] Re-run pure-C sync-only after the kernel OPENSYNC fix and require
  `open_sync_nt` PASS before treating D3D12 direct fence import as the only
  remaining sync blocker.
- [x] Re-run the D3D12 import path after pure-C sync import passes and require
  resource export/import PASS, fence export PASS, syncfile-acquire import PASS,
  and fence target diagnosis in one fresh artifact.
- [x] Decide whether a D3D12 direct fence import PASS is required by the final
  native path, or keep it documented as a WSL-reproducible expected-fail lane.
  Huygens/WSL comparison report records direct D3D12
  `OpenSharedHandle(fence)` as WSL-reproducible expected-fail for the current
  path; any future path that truly requires direct D3D12 fence import should
  add a new implementation item.
- [x] Keep the resource import and syncfile-acquire fence contract explained by
  current artifacts before advancing native present work.
- [ ] If a future native path requires direct D3D12 fence import, track that
  work separately from resource export/import, syncfile acquire, and
  native-present gates.
  Wave25 evidence makes D3D12 direct fence import the remaining sync-open
  blocker after pure-C valid sync-open passes; Hyper-V still reports
  `backend_opengl_submit 0`.
  Wave38 evidence: backend flag discipline is preserved; strict-present still
  reports `backend_opengl_submit 0`.
  Wave39 evidence:
  `/tmp/xv6-hyperv-build/wave39-d3d12-require-present-short.log` and
  `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log` prove resource
  export/open PASS, fence export PASS, direct D3D12 fence open bypassed as
  expected, pure-DXG syncfile acquire import PASS
  (`kernel_dxg_sync_import=PASS`), and compositor-side same-LUID fence import
  with target `current=1`.

#### 1P. Export/Import/Fence Owner Queue

Owner hints: export case worker owns resource NT fd; import-contract worker owns
resource/fence open; direct sync-open worker owns `OPENSYNCOBJECTFROMNTHANDLE2`;
close-parity worker owns destroy-NT behavior.

- [x] Produce one current direct resource-export artifact proving valid NT fd,
  natural WSL NT-share envelope, resource metadata seal-after-success, and no
  native-present/FPS/WebKit claim.
- [x] Produce one WSL-style seal verification artifact with tracked allocation
  and private-size verification.
- [x] Produce one exporter-close shared-resource lifetime matrix artifact.
- [x] Produce one resource query/open artifact from a separate `/dev/dxg` open
  using the successful exported resource fd.
- [x] Produce one compositor-device resource-import artifact that records
  adapter LUID, dimensions, format, allocation count, and private-data size.
- [x] Produce one direct sync-open artifact for the exported shared sync fd
  using `OPENSYNCOBJECTFROMNTHANDLE2`, with wire envelope and host return.
- [x] Produce one syncfile-acquire fence wait/target artifact for the compositor
  import lane.
- [ ] Produce one direct D3D12 fence wait/target artifact only if native path
  requirements make direct D3D12 fence open relevant.
- [x] Produce one destroy-NT close-parity artifact for the resource fd path and
  decide whether current close `-75` is still present.
- [ ] Produce one destroy-NT close-parity artifact for sync/fence fd and
  process-lifetime paths.
- [x] Keep Phase 1 closed only for export/import/fence contract evidence; do
  not use it to advance native present, FPS, or WebKit gates.
  Wave33 closes the resource export/open/import owner-queue artifacts. Wave36
  closes the resource-fd destroy close artifact with `destroy_ret:0`. Display
  handoff/native present and process lifetime remain separate.
  Wave38 update: resource NT export/open and fence NT export are explicitly
  proven in the strict-present run. Direct D3D12 fence open remains the
  expected-failing lane; compositor/native-present evidence is still missing.
  Wave39 update: syncfile-acquire fence import/target evidence is now present
  in the compositor lane. This closes the import/acquire contract slice only;
  native display handoff, callback/release, FPS, WebKit, and backend flag gates
  stay open.
  Wave45 update:
  `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log` closes the seal diagnostic
  and exporter-close lifetime matrix artifacts. These are resource lifetime
  proofs only, not present/FPS/WebKit evidence.

### Phase 2: Wayland Import And Fence Contract

#### 2A. Client Submission Metadata

- [x] Send the successful D3D12 runtime resource fd through
  `xv6_gpu_buffer_manager`.
- [x] Include source adapter LUID.
- [x] Include dimensions.
- [x] Include format.
- [x] Include allocation count.
- [x] Include total private-data size.
- [x] Include acquire fence fd.
- [x] Include acquire fence target value.
  Prep-patch evidence: protocol/client/compositor paths in
  `ports/wayland/src/wlcomp_gpu_sync.inc`,
  `ports/wayland/src/xv6_present_buffer.c`,
  `ports/wayland/src/xv6_egl_gles.c`, and
  `ports/wayland/src/d3d12sharedsmoke.c`.
  Wave39 evidence confirms the runtime resource fd and acquire fence fd reach
  the compositor in the strict-present path; callback/release and native display
  handoff remain separate open gates.

#### 2B. Compositor Resource Import

- [x] Import/open the resource on the compositor's own `/dev/dxg` process.
- [x] Use the compositor's own D3D12 device for import.
- [x] Reject any path that reuses the client resource handle directly.
- [x] Verify source adapter LUID matches the compositor adapter LUID.
- [x] Verify imported dimensions and format match the submitted metadata.
  Wave26v4 evidence:
  `/tmp/xv6-hyperv-build/wave26v4-d3d12-syncfile-acquire.log` shows
  D3D12 resource NT export/import PASS through the Wayland submission path:
  `CreateSharedHandle(resource) ok`, `OpenSharedHandle(resource) ok`,
  compositor `query resource ok`, `open resource ok`, matching LUID
  `00000001:89a64172`, size `256x256`, DXGI format `87`, allocation count `1`,
  and total private data `594`.
  Wave39 evidence:
  `/tmp/xv6-hyperv-build/wave39-d3d12-require-present-short.log` and
  `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log` repeat the current
  strict runtime path: resource export/open PASS, Wayland commit reaches the
  compositor, the compositor accepts the D3D12 buffer protocol, imports the
  resource on the same LUID, and keeps native display handoff separate.

#### 2C. Compositor Fence Import

- [x] Import/open the acquire sync/fence on the compositor side.
- [x] Verify fence CPU mapping.
- [x] Verify fence GPU mapping.
- [x] Verify current fence value.
- [x] Wait for or observe the submitted fence target value.
- [x] Record fence target satisfaction in compositor diagnostics.
  Wave26v4 evidence: D3D12 direct fence import is bypassed as expected for the
  `0x483` source-fence path; the dxg-syncfile acquire fd imports successfully
  (`kernel_dxg_sync_import=PASS`), compositor fence import succeeds with
  nonzero CPU/GPU mappings, and the compositor observes `d3d12 shared buffer
  fence target=1 current=1`.
  Wave39 evidence: direct D3D12 fence open is bypassed as expected, the
  pure-DXG syncfile acquire path reports `dxg-syncfile-acquire import
  validation ok` and `kernel_dxg_sync_import=PASS`, and the compositor imports
  the same-LUID fence and observes the submitted target already satisfied
  (`current=1`).

#### 2D. Negative Validators

##### 2D1. Metadata Mismatch Rejection

- [x] Reject bad-LUID resource submissions.
- [x] Reject wrong-size resource submissions.
- [x] Reject wrong-format resource submissions.
- [x] Reject missing-fence submissions when explicit sync is required.

##### 2D2. Stale/Fallback Rejection

- [x] Reject stale resource fd submissions.
- [x] Reject stale fence fd submissions.
- [x] Reject CPU-mappable fallback paths.
  Prep-patch evidence: `ports/wayland/src/wlcomp_gpu_sync.inc` rejects missing
  source-LUID and missing fence-target contracts; `scripts/hyperv-dxg-validate.sh`
  includes the `d3d12sharedsmoke --bad-luid` validator path.
  Confucius/McClintock source-mode reports close the import-negative source
  rows for wrong size, wrong format, stale fds, and CPU-mappable fallbacks.
  Runtime negative validation bundles remain separate open artifacts below.

#### 2E. Release Lifetime

##### 2E1. Metadata Lifetime

- [x] Hold submitted resource metadata until compositor import finishes.
- [x] Hold submitted fence metadata until target handling finishes.

##### 2E2. Callback/Release Lifetime

- [ ] Fire frame callbacks only after import and fence handling.
- [ ] Release buffers only after import and fence handling.
- [x] Add the compositor-side completion predicate that permits release/callback
  only after present-source register success, commit success, nonzero
  `present_id`, and `completed >= present_id`.
- [ ] Ensure failed imports release resources with an explicit failure path.
  Wave26v4 note: metadata survives through import and fence target handling,
  but `d3d12sharedsmoke` reports committed shared resource `buffer release=0`;
  frame callback and buffer-release completion remain open.
  Wave39 note: import and fence handling now reach the compositor and GPU-copy
  proof, but the fail-closed present path still blocks callbacks/releases until
  native display handoff exists.
  Wave40 source update: the compositor predicate now prevents callback/release
  until register success, commit success, a nonzero `present_id`, and
  completion at or beyond that present id. Runtime callback/release evidence
  remains open until the host display handoff succeeds.

#### 2F. Phase-Exit Validation

- [x] Close this phase only when `d3d12sharedsmoke --require-present` logs fresh
  resource import.
- [x] Require fresh fence import in the same run.
- [x] Require fence target satisfaction in the same run.
- [ ] Require frame callback for the same submitted frame.
- [ ] Require buffer release for the same submitted frame.
  Wave39 update: the same-run strict artifacts satisfy fresh resource import,
  fence import, and target satisfaction again. Frame callback and buffer
  release remain open because the present path fail-closes before native
  success.
  Wave40 update: callback/release gating is stricter in source, but the strict
  runtime still fail-closes at present-source commit `errno=95`, so callback and
  release remain unchecked.

#### 2G. Compositor Import Validation Artifact

##### 2G1. Successful Import Artifact

- [x] Produce one fresh compositor artifact for a successful runtime resource
  fd imported on the compositor's own `/dev/dxg` process.
- [x] Tie the artifact to the source adapter LUID, compositor adapter LUID,
  dimensions, format, allocation count, and private-data size.
- [x] Include the acquire-fence fd, fence target value, and observed fence value
  for the same frame.

##### 2G2. Callback/Release And Negative Evidence

- [ ] Include frame callback for the same frame.
- [ ] Include buffer release for the same frame.
- [x] Include negative evidence for bad-LUID and missing-fence rejection in the
  same validation bundle.
- [ ] Keep this artifact separate from Phase 3 native-present completion
  evidence.
  Wave39 update: the fresh present evidence bundle covers successful
  Wayland commit, compositor import, fence import, and fence target satisfaction
  while preserving native-present completion as a separate open Phase 3 gate.
  Wave48 update: `/tmp/xv6-hyperv-build/wave48-present-selftest.log` reports
  present selftest `failures=0` and Phase 2 negatives, while
  `/tmp/xv6-hyperv-build/wave48-import-negative.log` reports import-negative
  PASS on the selected NVIDIA adapter. Frame callback/release remain open
  because the handoff still fail-closes before native completion.

#### 2H. Compositor Import Owner Queue

Owner hint: compositor import worker.

- [x] Land the successful Phase 1 resource fd through the Wayland submission
  path instead of a local smoke-only path.
- [x] Validate compositor-side `/dev/dxg` import on a distinct process/device
  from the client exporter.
- [x] Validate acquire-fence fd import and target-value satisfaction for the
  same submitted frame.
- [x] Produce a negative bundle for bad LUID, wrong dimensions, wrong format,
  stale resource fd, stale fence fd, and CPU/readback fallback rejection.
- [x] Re-run the resource import-negative matrix after Wave54 and require PASS.
- [x] Classify same-numeric child-owned `/dev/dxg` device sync-open acceptance
  as WSL-compatible, not a mismatch.
- [x] Add a `dxgprobe` row proving inherited `/dev/dxg` TGID guard behavior for
  sync import.
- [x] Add a `dxgprobe` row proving inherited `/dev/dxg` TGID guard behavior for
  OPENSYNC object source selection.
- [x] Keep Phase 2 evidence limited to import/fence/frame callback/release;
  native non-readback D3D12 present remains Phase 3.
  Wave39 evidence keeps protocol/import/acquire acceptance separate from
  native display handoff: the D3D12 buffer is accepted and imported, but the
  present path fail-closes with display handoff unimplemented.
  Wave48 evidence closes the runtime negative bundle:
  `/tmp/xv6-hyperv-build/wave48-import-negative.log` reports import-negative
  PASS, and `/tmp/xv6-hyperv-build/wave48-present-selftest.log` records Phase 2
  negatives with present selftest `failures=0`.
  Wave49 refreshes this with
  `/tmp/xv6-hyperv-build/wave49-runtime-negative-submissions.log`:
  bad-luid, bad-dimensions, bad-format, missing-fence, stale-fence, and CPU
  fallback guard all PASS, with `summary pass=6 fail=0 native_present_claim=0`.
  Wave55 update: `/tmp/xv6-hyperv-build/wave55-import-negative.log` closes
  `resource_import_negative_matrix`, but `sync_import_negative_matrix` and
  `opensyncobject_source_matrix` still FAIL because child/foreign-process
  device sync open is accepted.
  Wave56 update: `/tmp/xv6-hyperv-build/wave56-import-negative.log` adds child
  isolation diagnostics and reclassifies same-numeric child-owned device
  acceptance as WSL-compatible. The remaining open proof is an inherited
  `/dev/dxg` TGID guard row in `dxgprobe`.
  Wave57 update: `/tmp/xv6-hyperv-build/wave57-import-negative.log` closes the
  remaining inherited TGID guard proof. `resource_import_negative_matrix`,
  `sync_import_negative_matrix`, `opensyncobject_source_matrix`,
  `opensync_child_own_dxg_matrix`, and
  `opensync_child_inherited_parent_dxg_matrix` all PASS; inherited parent
  `/dev/dxg` is rejected by the TGID namespace guard.

#### 2I. Frame Callback And Buffer Release Evidence

- [ ] Record frame callback for the same client/resource/generation/run id as
  the successful D3D12 resource import.
- [ ] Record buffer release for the same client/resource/generation/run id as
  the successful D3D12 resource import.
- [ ] Correlate frame callback and buffer release with the imported resource
  handle, acquire sync handle, and fence target.
- [x] Add a negative artifact proving import-only success cannot satisfy Phase
  2 when callback or release is missing.
  Wave26v4 blocker: the import/acquire path succeeds, but
  `d3d12sharedsmoke` reports committed shared resource `buffer release=0`, so
  frame callback and release evidence remain open.
  Wave39 blocker: the import/acquire path and compositor GPU copy succeed, but
  the fail-closed present path blocks callbacks/releases. Keep these unchecked
  until a native handoff completion produces the callback and release for the
  same run id.
  Wave48 selftest closes the negative-artifact slice:
  `/tmp/xv6-hyperv-build/wave48-present-selftest.log` reports Phase 2 negatives
  and present selftest `failures=0`. Positive callback/release evidence remains
  open until native handoff completion produces them.
  Wave49 runtime-negative submissions keep the same boundary honest:
  `native_present_claim=0`, callbacks/releases remain blocked in the strict
  present artifact, and no positive Phase 2 exit is claimed.

### Phase 3: Native Hyper-V Presentation/Composite

#### 3A. Architecture Choice

##### 3A1. Native-Path Decision Record

- [x] Choose the current no-readback intermediate mechanism: compositor-owned
  D3D12 copy/composite into a D3D12 destination plus fail-closed present-source
  commit.
- [x] Choose the final host display handoff mechanism behind the present-source
  commit path.
- [x] Document whether the current path is compositor D3D12 copy/composite or an
  equivalent WSLg-style host compositor/remoting participant.
  Wave39 documents and validates the current intermediate lane: same-LUID
  import, syncfile acquire, compositor-owned D3D12 copy, and fail-closed
  present-source commit. The final host display handoff implementation remains
  open.
  Wave48 selects the final handoff lane in
  `/tmp/xv6-hyperv-build/wave48-d3d12-present-artifact.log`, but still reports
  missing kernel ABI, `present_id=0/completed=0`, blocked callbacks/releases,
  and `backend_opengl_submit 0`; implementation and completion stay open.

##### 3A2. Backend Flag Preconditions

- [ ] Document why synthvid framebuffer dirty rects alone do not satisfy native
  D3D12 present.
- [x] Record the required evidence before any backend flag can change.
  Required evidence is native display handoff commit success, native present
  completions, frame callback/release for the same submitted frame, no
  CPU/readback fallback, and a passing 480p FPS validator. Wave39 explicitly
  keeps `backend_opengl_submit 0`.

#### 3B. GPU Copy/Composite Implementation

##### 3B1. Proven GPU-Copy Path

- [x] Create or reuse a compositor D3D12 device on the matched adapter.
- [x] Import the client resource into that compositor device.
- [x] Submit a native D3D12 GPU copy/composite from the imported resource.

##### 3B2. Remaining Copy Diagnostics And Fallback Rejection

- [ ] Record the command queue/context/hwqueue used for the native copy or
  composite.
- [x] Record a completion fence for the native copy or composite.
- [x] Correlate the native copy/composite completion with the imported resource
  and acquire sync target.
- [x] Avoid CPU mapping the D3D12 resource.
- [x] Avoid Mesa DRI software present in the Wave39 strict-present proof.
- [ ] Keep proving Mesa DRI software present is not used in the final native
  display handoff artifact.
- [x] Avoid framebuffer readback as the claimed native-present path.
  Wave26v4 note: resource and acquire sync import complete, but no native
  copy/composite completion artifact exists yet.
  Wave28 evidence: D3D12 native-present attempts now get past the missing
  target gate and start GPU copy/present work, but the path rejects at
  `OpenExistingHeapFromAddress*` with `hr=0x80004001`. No GPU copy or present
  completion is recorded.
  Wave29 evidence: same-LUID resource/fence import remains good and the
  page-backed FB BO target remains active (`bo_allocs=1`, `bo_presents`
  advancing). Host-pointer framebuffer heap import is now proven unsupported
  and cached-rejected: `OpenExistingHeapFromAddress1` and
  `OpenExistingHeapFromAddress` return `0x80004001`, then subsequent attempts
  fail closed with cached rejects. The new blocker is a D3D12-resource-backed
  display destination/handoff ABI, not more host-pointer heap retries.
  Wave30 evidence:
  `/tmp/xv6-hyperv-build/wave30-d3d12-syncfile-require-present.log` and
  `/tmp/xv6-hyperv-build/wave30-d3d12-syncfile-require-present-dxg.log` prove
  same-LUID import and dxg syncfile acquire remain good, a compositor-owned
  D3D12 texture target is created, and the GPU copy is submitted, waited, and
  completed (`submits=1`, `waits=1`, `copy_fence=1`,
  `d3d12_gpu_copy_completes=1`,
  `d3d12_gpu_copy_composite_completes=1`) with
  `d3d12_no_readback=1`. Keep display handoff open:
  `d3d12_display_handoff_implemented=0`, `d3d12_gpu_present_completes=0`, and
  `backend_opengl_submit 0`.
  Wave39 evidence:
  `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log` repeats the
  complete GPU-copy/composite proof in the strict-present path:
  `d3d12_compositor_gpu_copy_submits=1`,
  `d3d12_compositor_gpu_copy_waits=1`, `d3d12_gpu_copy_completes=1`, and no
  CPU/readback. The same artifact still fail-closes native display handoff
  (`d3d12_display_handoff_implemented=0`) and keeps
  `backend_opengl_submit 0`.

#### 3B2. Role-Less Validator Surface

- [x] Map and damage the role-less D3D12 validator surface far enough to
  produce native-present attempts.
- [x] Provide a page-backed `FB_GPU_BO_CREATE` target for the role-less
  validator surface.
- [ ] Convert role-less validator surface attempts into completed native D3D12
  presents.
- [ ] Keep role-less validator-surface attempts separate from frame callback,
  buffer release, and FPS acceptance.
  Wave27 evidence: the role-less D3D12 validator surface now maps/damages
  enough to produce native-present attempts. `/tmp/wlcomp-d3d12-present` still
  reports `present_rejected`, `d3d12_gpu_present_completes=0`,
  `d3d12_buffer_release_observed=0`, `d3d12_frame_callback_observed=0`, and a
  missing page-backed `FB_GPU_BO_CREATE` target, so completion/release/callback
  gates remain open.
  Wave28 evidence: the page-backed FB BO target is active (`bo_allocs=1`,
  `bo_presents` advancing), and D3D12 native-present attempts pass the previous
  missing-target gate. The path still rejects at
  `OpenExistingHeapFromAddress*` with `0x80004001`; `d3d12_gpu_copy_completes=0`,
  `d3d12_gpu_present_completes=0`, and no frame callback/release are observed.
  Wave29 evidence: `bo_allocs=1` and `bo_presents` continue advancing, and the
  path records `d3d12_fb_heap_open_supported=0`, `d3d12_fb_heap_open_notimpl=1`,
  and `d3d12_fb_heap_open_cached_rejects=6`. Keep native present open because
  `d3d12_gpu_copy_completes=0`, `d3d12_gpu_present_completes=0`,
  `d3d12_buffer_release_observed=0`, and `d3d12_frame_callback_observed=0`.

#### 3C. Native Present Diagnostics

##### 3C1. Completion Counters

- [x] Count native D3D12 present attempts.
- [ ] Count native D3D12 present completions.
- [x] Count native D3D12 copy/composite attempts.
- [x] Count native D3D12 copy/composite completions.

##### 3C2. Protocol And Release Counters

- [ ] Count protocol acceptance separately.
- [ ] Count frame callbacks separately.
- [ ] Count buffer releases separately.

##### 3C3. Fallback Counters

- [ ] Count framebuffer blits separately.
- [ ] Count partial framebuffer updates separately.
- [x] Count CPU/readback fallback attempts separately.
- [x] Expose the fail-closed present counters through compositor logs or a
  present evidence artifact.
- [ ] Expose successful native-present completion counters through compositor
  logs or `/tmp/wlcomp-d3d12-present`.
  Wave26v4 note: protocol/import counters advance, but there is no native
  present completion artifact and buffer release is `0`.
  Wave27 note: present attempts now appear, but they are rejected; completions,
  frame callback, and buffer-release observations remain zero.
  Wave49 runtime-negative artifact closes the CPU fallback guard/counter slice:
  the runtime-negative-submission matrix reports CPU fallback guard PASS while
  preserving `native_present_claim=0` and `backend_opengl_submit 0`.
  Wave28 note: `d3d12_gpu_copy_composite_starts=5` and
  `d3d12_gpu_present_starts=5`, but `d3d12_gpu_copy_completes=0`,
  `d3d12_gpu_present_completes=0`, `d3d12_buffer_release_observed=0`, and
  `d3d12_frame_callback_observed=0`.
  Wave29 note: starts advance to `d3d12_gpu_copy_composite_starts=7` and
  `d3d12_gpu_present_starts=7`, but copy/present completions, buffer release,
  and frame callback remain `0`. Same-LUID resource and fence import remain
  good (`d3d12_present_same_luid=1`, wait confirmed).
  Wave30 note: copy/composite completion counters now advance
  (`d3d12_gpu_copy_completes=1`,
  `d3d12_gpu_copy_composite_completes=1`) for a same-LUID resource/fence
  sequence, but native display present remains open:
  `d3d12_display_handoff_implemented=0`,
  `d3d12_gpu_present_completes=0`,
  `d3d12_native_present_completions=0`,
  `d3d12_buffer_release_observed=0`,
  `d3d12_frame_callback_observed=0`, and `backend_opengl_submit 0`.
  Wave39 note: fail-closed present evidence now exists in
  `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log`. It records
  accepted D3D12 protocol/import, `d3d12_evidence_stage=present_rejected`,
  `d3d12_present_path=fail-closed`, copy submit/wait/complete counters, and
  blocked callbacks/releases. Native present completions remain `0`.

#### 3C2. D3D12 Destination GPU-Copy Proof

- [x] Create or import a D3D12-resource-backed display destination rather than
  a host-pointer framebuffer heap.
- [x] Prove the destination resource can be opened or shared on the compositor
  D3D12 device.
- [x] Execute a GPU copy/composite from the imported client resource into the
  D3D12 destination resource.
- [x] Record `d3d12_gpu_copy_completes>0` for the same client/resource/fence
  sequence.
- [x] Record a copy completion fence and correlate it with the imported acquire
  fence target.
- [x] Keep destination GPU-copy proof separate from display scanout or WebKit
  acceleration.
  Wave30 evidence: destination GPU-copy proof is satisfied by the
  compositor-owned D3D12 texture target and same-run source resource/acquire
  fence sequence in
  `/tmp/xv6-hyperv-build/wave30-d3d12-syncfile-require-present.log`. This does
  not close native display present because `display_handoff=0` and
  `d3d12_display_handoff_implemented=0`.

#### 3C3. D3D12-Resource To Display Handoff ABI

##### 3C3-0. Wave58 Native-Present Blocker Ladder

###### 3C3-0A. Present-Source Provenance Diagnostics

- [x] Emit present-source provenance for every strict-present run: run id,
  client id, resource id, resource generation, source-live state, operation,
  helper contract version, transport-present state, and failure reason.
- [ ] Validate provenance against a current artifact and reject stale
  `/tmp/wlcomp-d3d12-present` records.
  Wave61 artifacts:
  `/tmp/xv6-hyperv-build/wave61-present-run2.log` and
  `/tmp/xv6-hyperv-build/wave61-present-evidence.log` validate the current
  strict-present provenance/evidence keys for the fail-closed path:
  no-host-helper, no display handoff, no present completion, blocked
  callback/release keys, adapter/provenance/register flags, commit status 95,
  `d3d12_native_present_requirements_satisfied=0`, `strict_pass=0`, and
  `backend_opengl_submit 0`.

###### 3C3-0B. DXG Resource Export Descriptor/Query

- [ ] Record the exported/committed DXG resource descriptor used by the handoff:
  dimensions, format, pitch/layout, sample count, allocation count,
  private-data sizes, LUID, resource handle, and NT-share/open handle.
- [ ] Record query behavior after present-source commit failure or success, and
  distinguish "query skipped after commit failure" from "query ioctl missing".
- [ ] Prove the descriptor belongs to the same resource that completed GPU copy
  in the compositor.

###### 3C3-0C. Synthvid VRAM Existing-Sysmem Bridge Proof Or Negative

- [ ] Attempt a D3D12 copy into a synthvid/VRAM existing-sysmem target only with
  explicit evidence that the target is a D3D12 resource/copy destination.
- [ ] If the bridge remains impossible, capture a negative artifact proving
  existing-sysmem is still PFN/backing-store only and not native present.

###### 3C3-0D. Host-Helper Transport ABI Stub

- [x] Define the host-helper ABI stub fields for version, operation, resource
  descriptor, synchronization, source lifetime, transport-present state,
  present id, completion id, and error code.
- [x] Fix Wave58 ABI drift: Wayland fallback
  `fb_gpu_dxg_present_source_register` layout must match the kernel UABI.
- [x] Rebuild/revalidate after the ABI drift fix and require REGISTER success
  followed by COMMIT `EOPNOTSUPP` while the host helper transport is still
  absent.
- [x] Close only with a Wave60 artifact showing
  `dxg_present_register_successes=1` and
  `dxg_present_commit_attempts=1` with commit `errno=95`.
- [x] Validate fail-closed stub behavior for transport absent without producing
  native-present completions, callbacks, releases, FPS credit, WebKit credit,
  or `backend_opengl_submit=1`.
  Wave58 regression evidence:
  `/tmp/xv6-hyperv-build/wave58-after-present-ping.log` shows the Wayland
  fallback register layout lagged the kernel UABI, causing REGISTER `errno=22`
  and preventing the expected COMMIT/`EOPNOTSUPP` fail-closed evidence.
  Wave59 evidence:
  `/tmp/xv6-hyperv-build/wave59-present-recover.log` still fails REGISTER with
  `errno=22`; `dxg_present_last_flags` equals `adapter_luid_low`, proving the
  Wayland fallback still places LUID before flags while the kernel expects
  flags/present_source in the stable prefix.
  Wave60 recovery evidence:
  `/tmp/xv6-hyperv-build/wave60-present-recover.log` closes the ABI drift
  regression with `dxg_present_register_successes=1`,
  `dxg_present_commit_attempts=1`, `dxg_present_last_ret=95`,
  `d3d12_dxg_present_source_register_successes=1`,
  `d3d12_dxg_present_source_commit_errno/status=95`,
  `d3d12_present_source_registered=1`,
  `d3d12_present_source_commit_rejected_eopnotsupp=1`,
  `present_evidence=present_fail_closed_terminal`, and
  `backend_opengl_submit 0`. This does not close native present.
  Wave61 evidence:
  `/tmp/xv6-hyperv-build/wave61-present-run2.log` and
  `/tmp/xv6-hyperv-build/wave61-present-evidence.log` close the fail-closed
  stub observability slice: `d3d12_present_source_no_host_helper=1`,
  `no_display_handoff=1`, `no_present_completion=1`, callback/release blocked
  keys present, adapter/provenance/register flags present,
  `d3d12_native_present_requirements_satisfied=0`, `strict_pass=0`, commit
  `errno/status=95`, and `backend_opengl_submit 0`. `fbstat` also reports the
  named provenance, metadata, lifetime, candidate, reject, and gate rows.

###### 3C3-0E. Real Host Helper Or Display Service

- [ ] Implement or integrate a real host helper, WSLg-like display channel, or
  proven synthvid/VRAM bridge that can consume the D3D12 destination resource.
- [ ] Prove the service accepts the resource descriptor and acquire sync from
  the compositor without CPU/readback fallback.

###### 3C3-0F. Completion, Callback, And Release Proof

- [ ] Produce one successful native-present artifact with nonzero `present_id`
  and `completed >= present_id`.
- [ ] Tie frame callback and buffer release to the same client/resource/run id
  as the successful native-present completion.

###### 3C3-0G. FPS/WebKit Gates After Native Present

- [ ] Keep Phase 4 FPS blocked until 3C3-0F has a successful native-present
  artifact and the finite 480p validator reports `effective_presented_fps > 60`.
- [ ] Keep Phase 5 WebKit blocked until the same native-present/FPS contract is
  proven for WebKit content with matching run/resource identity.

##### 3C3-1. Decision Tree Inputs

- [x] Name the missing kernel/host display handoff ABI in diagnostics.
- [ ] Capture a WSLg/Linux reference note proving resource sharing exists but
  is not a guest-only D3D12-resource-to-scanout ioctl.
- [ ] Capture enough WSLg same-adapter display/user-channel evidence to decide
  whether xv6 should emulate a remoting/display-channel path.
- [ ] Capture enough Linux synthvid VRAM GPA evidence to decide whether a
  VRAM-backed D3D12 existing-sysmem display target can be bound directly.
- [ ] Capture enough Hyper-V host-helper evidence to decide whether a new
  helper protocol must bind a DXG resource to synthvid/scanout.
- [x] Record the current missing-helper diagnostic name and candidate command
  set for the selected handoff lane.
- [x] Keep the WSL conclusion recorded in the final handoff decision: `existing_sysmem`
  is a D3DKMT backing-store/PFN registration path, not a D3D12 COM-resource
  display bridge.
- [x] Record the WSL/Linux decision table conclusion for represented display
  paths.
  Wave41 evidence names the currently missing host ABI as
  `dxg-resource-scanout-bind`. Linux comparison found synthvid exposes a VRAM
  GPA-backed display aperture, which keeps the VRAM-backed existing-sysmem
  target lane plausible but unproven.
  WSL comparison caveat: `existing_sysmem` only pins/sends backing-store PFNs;
  it is not WSLg's display-present path and does not itself create an
  `ID3D12Resource` over synthvid VRAM.
  Wave45 decision table conclusion: WSLg user/display channel is represented;
  synthvid dirty rect is represented as display VRAM/dirty, not a D3D12 COM
  bridge; direct runtime D3D12 scanout/helper is not represented in WSL/Linux.
  Wave49 diagnostic update: the strict-present artifact names the missing path
  as `host-display-helper/resource-scanout-bind` and records candidate commands
  34/35/38. This closes diagnostic/dependency naming only; implementation,
  host completion, callbacks, releases, FPS, WebKit, and backend enablement
  remain open.
  Wave50 planning note: WSL/Linux still do not provide a guest-only DXG-resource
  scanout path to copy directly; xv6 must either implement a host/display helper
  or choose a represented WSLg-like/display-channel lane.

##### 3C3-2. Option A: Runtime-Created Shared Resource Present

- [ ] Define whether the source object is the client runtime-created shared
  resource or the compositor-owned D3D12 destination texture.
- [ ] Bind the selected runtime-created D3D12 resource or destination texture to
  the present-source path.
- [ ] Preserve resource export/open, syncfile acquire, same-LUID import, and
  GPU-copy proof in the same runtime-created-resource artifact.
- [ ] Define how `dxg-resource-scanout-bind` or an equivalent host path consumes
  the runtime-created D3D12 resource without CPU readback.
- [ ] Validate one frame reaches host display completion with callback and
  release counters.

##### 3C3-3. Option B: WSLg-Like User/Display Channel

- [ ] Identify the WSLg display/remoting channel used for presenting guest GPU
  content to the Windows host.
- [ ] Determine whether xv6 can reuse an existing WSLg-like user/display
  channel or must implement an equivalent userspace helper.
- [ ] Define the transport payload for a D3D12 copied destination resource,
  acquire sync, dimensions, format, pitch, and present id.
- [ ] Map the D3D12 copied destination resource into the WSLg-like channel
  without CPU readback.
- [ ] Validate one frame reaches the host display path with native completion,
  callback, and release counters.

##### 3C3-4. Option C: Synthvid VRAM-Backed Existing-Sysmem Target

- [ ] Map the Linux synthvid VRAM GPA finding to xv6's synthvid framebuffer
  aperture.
- [x] Record that Linux synthvid dirty rect maps to display VRAM/dirty semantics,
  not a D3D12 COM-resource bridge.
- [x] Add kernel testability for a 64K-aligned FB scanout map usable by the
  existing-sysmem experiment.
- [x] Add kernel testability for PFNMAP/MMIO pinning through
  `SETEXISTINGSYSMEMPAGES`.
- [x] Add `/dev/dxg` diagnostics for `dxg_existing_sysmem_target`,
  `pfnmap_ok`, `vram_gpa`, and `vram_size`.
- [ ] Allocate or expose a synthvid VRAM-backed target suitable for actual
  D3D12 existing-sysmem/resource import.
- [ ] Prove D3D12 can copy into the VRAM-backed target without host-pointer heap
  `OpenExistingHeapFromAddress*`.
- [ ] Prove `FB_BO_PRESENT` or equivalent scanout observes the D3D12-written
  VRAM-backed target as a native display update.
- [ ] Prove the VRAM-backed existing-sysmem lane creates a usable D3D12 resource
  rather than only pinning guest backing-store PFNs.
- [x] Validate direct `FB_GPU_SCANOUT_MAP` or VRAM existing-sysmem import where
  `/dev/dxg` reports nonzero `pfnmap_pages` and `vram:1`.
- [x] Reject raw allocation/PFN registration as sufficient to create an
  `ID3D12Resource` over synthvid VRAM.
- [x] Record the scanout bridge fail-closed boundary with PFN/VRAM proof and no
  D3D12 copy bridge.
- [x] Record scanout existing-sysmem pin-lifetime matrix as PASS_FAIL_CLOSED.
- [ ] Keep this lane open only for a future explicit D3D12 resource/copy bridge
  into the VRAM-backed target.
  Source/testability update: kernel support now exposes the 64K-aligned FB
  scanout map, PFNMAP/MMIO pinning through `SETEXISTINGSYSMEMPAGES`, and
  `/dev/dxg` diagnostics for target kind, PFNMAP success, VRAM GPA, and VRAM
  size. Present-source commit still fail-closes; no native-present completion
  is proven.
  Wave43 evidence: `dxgprobe --scanout-existing-sysmem` reports `map_rc=0`,
  `scanout_va=0x7fffff2a0000`, `scanout_size=3145728`, `gpa=0xc0000000`,
  `gpa_size=3145728`, `width=1024`, `height=768`, `pitch=4096`, `create_rc=0`,
  `allocation=0x40000080`, `resource=0x40000040`, `sysmem=0x7fffff2a0000`,
  `va_align64k=1`, `should_show_pfnmap_pages=1`, `should_show_vram=1`,
  `pin_proof=PASS`, `native_host_display_handoff=0`,
  `d3d12_copy_possible=0`, and `status=FAIL_CLOSED_NO_D3D12_COPY`. `/dev/dxg`
  reports `dxg_existing_sysmem_target=pfnmap_pages:768 pfnmap_ok:1 vram:1
  vram_gpa:0xc0000000 vram_size:8388608`.
  Wave44 conclusion: existing-sysmem validates backing-store/PFN registration
  only. It is not by itself a D3D12 COM-resource bridge or a display-present
  path.
  Wave44 validation:
  `/tmp/xv6-hyperv-build/wave44-scanout-d3d12-bridge.log` proves the
  scanout-existing-sysmem PFN/VRAM boundary and keeps the D3D12 bridge
  fail-closed. This closes only the raw existing-sysmem-not-`ID3D12Resource`
  contract and scanout bridge boundary, not native present.
  Wave45 evidence:
  `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log` records the scanout
  existing-sysmem pin-lifetime matrix as PASS_FAIL_CLOSED: PFN/VRAM pin
  lifetime is proven, but D3D12 COM-resource bridge and native display handoff
  remain absent.

##### 3C3-5. Option D: New Host/Display Helper Protocol

##### 3C3-5A. Known Inputs And Negative Boundary

- [x] Record that the current host-helper transport is absent; strict present
  fail-closes before native completion.
- [x] Add passive host-helper contract diagnostics for contract version,
  metadata, transport-present=0, operation, lifetime, source-live, and
  requires-completion.
- [x] Record the currently selected helper name
  `host-display-helper/resource-scanout-bind`.
- [x] Record candidate helper command ids 34/35/38 as diagnostics only.
- [x] Record that WSL/Linux do not provide a guest-only DXG-resource scanout
  path to clone directly.
- [x] Capture the first Wave50 host-helper artifact when another agent reports
  a source or runtime result.
- [ ] Record the Wave51 restored fail-closed GPU-copy/commit artifact path once
  available in this plan.
- [ ] Record the Wave53 protocol-only regression artifact path with
  `protocol_accepted`, zero GPU-copy, and zero present-source commit attempts.
  Wave50 runtime prep proves helper/identity fields are live in
  `/tmp/wlcomp-d3d12-present`, but the strict-present run stops at
  `protocol_accepted` with zero GPU-copy and zero commit attempts. Keep this as
  protocol-only evidence, not fail-closed-present or native-present completion.
  Wave54 planning note: use Wave51 as the last reported good fail-closed
  copy/commit shape and Wave53 as the current regression/intermediate state
  until fresh artifact paths are recorded.
  Wave54 accounting update: `/tmp/xv6-hyperv-build/wave54-d3d12-require-present.log`
  and `/tmp/xv6-hyperv-build/wave54-d3d12-present-artifact.log` supersede the
  Wave53 regression for the current tree by reaching GPU copy and terminal
  fail-closed present-source register/commit again.

##### 3C3-5B. Protocol Shape

- [ ] Define the kernel/host transport for
  `FB_GPU_DXG_PRESENT_SOURCE_COMMIT`.
- [ ] Define how the host helper consumes, scans out, or remotes the committed
  D3D12 destination resource.
- [ ] Ensure the handoff ABI exposes enough metadata for dimensions, pitch,
  format, allocation count, LUID, synchronization, and present id.
- [ ] Define the host-visible lifetime/ownership rules for committed display
  resources and acquire/release synchronization.
- [ ] Define failure reporting for transport absent, source not live, metadata
  mismatch, helper rejection, and completion timeout.

##### 3C3-5C. Implementation And Runtime Proof

- [ ] Implement and validate `dxg-resource-scanout-bind` or rename the ABI once
  a different proven host-helper path exists.
- [x] Restore strict-present to reach GPU-copy and fail-closed commit attempts
  after the Wave53 protocol-only regression.
- [ ] Prove transport-present changes from `0` only when a real helper/display
  channel exists.
- [ ] Prove one helper-backed frame has nonzero `present_id` and
  `completed >= present_id`.
- [ ] Prove callback and buffer release fire for that same helper-backed frame.
- [x] Treat direct runtime D3D12 scanout/helper as an xv6-specific host-helper
  path because it is not represented in WSL/Linux.

##### 3C3-6. Rejected Raw Allocation-To-ID3D12Resource Path

- [x] Record that existing-sysmem/PFN registration does not create an
  `ID3D12Resource` over synthvid VRAM.
- [x] Reject raw host allocation handle, PFN list, or scanout GPA alone as
  native-present evidence.
- [ ] Keep raw allocation-to-`ID3D12Resource` experiments out of the final
  handoff path unless future WSL/Linux evidence contradicts the current
  conclusion.
  Wave44 artifact:
  `/tmp/xv6-hyperv-build/wave44-scanout-d3d12-bridge.log` validates this
  rejection contract explicitly: raw scanout allocation/PFN/VRAM registration
  is not accepted as a D3D12 COM-resource bridge or native display handoff.

##### 3C3-7. Handoff Wiring And Backend Gate

- [x] Reject host-pointer framebuffer heap import as an implementation path for
  the current display handoff.
- [ ] Revisit host-pointer or existing-sysmem framebuffer heap import only if
  later WSL/Linux evidence proves a supported display-present variant.
- [ ] Wire the compositor/native-present path to use the D3D12 display
  destination instead of `OpenExistingHeapFromAddress*`.
- [ ] Select exactly one validated handoff lane from runtime-created shared
  resource present, WSLg-like channel, synthvid VRAM-backed target with an
  explicit D3D12 bridge, or new host/display helper protocol options.
- [x] Preserve `backend_opengl_submit 0` through the Wave39 fail-closed
  present-source evidence.
- [ ] Preserve `backend_opengl_submit 0` until the handoff produces native
  present completions.
  Wave30 next blocker: GPU copy into a compositor-owned D3D12 texture now
  completes, but display handoff is explicitly unimplemented
  (`d3d12_display_handoff_implemented=0`), so the handoff ABI and completion
  path remain the next Phase 3 dependency.
  Wave40 update: kernel diagnostics name the missing ABI as
  `dxg-resource-scanout-bind`, keep fake display counters disabled, and keep
  OpenGL-submit disabled.
  Wave41 evidence strengthens this diagnostic with the strict-present kernel
  log: `fb: dxg present-source commit blocked: missing host
  ABI=dxg-resource-scanout-bind ... 256x256 pitch:1024 fmt:0x34325241`.
  Existing-sysmem lane update: WSL comparison confirms existing-sysmem is
  backing-store, not display-present, and the new kernel diagnostics are for
  testability only until runtime proof shows a native display update.

#### 3C3A. Fail-Closed DXG Present-Source ABI Probe

- [x] Call the DXG present-source ABI after the D3D12 GPU copy completes.
- [x] Register the present source successfully.
- [x] Attempt one commit through the present-source ABI.
- [x] Fail the commit closed with expected `EOPNOTSUPP` while real display
  handoff is still missing.
- [x] Prove query support is compiled and skipped after commit failure rather
  than missing in the kernel.
- [ ] Implement the real display handoff behind the present-source ABI.
  Wave31 evidence: `wlcomp` now reaches the fail-closed DXG present-source ABI
  after the D3D12 GPU copy. Register succeeds, one commit is attempted, and the
  commit fails with expected `EOPNOTSUPP`. This proves the probe path is wired
  without claiming native display present. Display handoff remains missing and
  `backend_opengl_submit` remains `0`.
  Wave39 evidence:
  `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log` refreshes this
  fail-closed proof with `d3d12_dxg_present_source_register_successes=1`,
  `d3d12_dxg_present_source_commit_attempts=1`,
  `d3d12_dxg_present_source_commit_successes=0`,
  `d3d12_dxg_present_source_commit_errno=95`,
  `d3d12_display_handoff_implemented=0`, and
  `d3d12_display_handoff_requires_kernel_host_protocol=1`.
  Wave40 evidence repeats the same fail-closed point after the shell prefix
  rebuild/deploy: register/copy/import succeed, commit fails with `errno=95`,
  and the missing host ABI is `dxg-resource-scanout-bind`.
  Wave41 strict-present still reports `d3d12_present_errno=95`,
  `present_id=0`, `completed=0`, callbacks/releases blocked, no readback, and
  `backend_opengl_submit 0`; do not use it to close positive present
  completion.
  Wave48 fail-closed artifact repeats the selected final handoff lane with
  runtime resource/fence import, GPU copy, missing kernel ABI, `present_id=0`,
  `completed=0`, callbacks/releases blocked, no CPU/readback, and
  `backend_opengl_submit 0`.
  Wave49 clean strict-present artifact reaches import/GPU copy again, names the
  missing helper as `host-display-helper/resource-scanout-bind` with candidate
  commands 34/35/38, and still reports `present_id=0/completed=0`, blocked
  callbacks/releases, no readback, and `backend_opengl_submit 0`.
  Wave54 strict-present artifacts restore this terminal fail-closed path:
  GPU copy completes, DXG present-source register/commit ioctl entries are
  `1/1`, copyin failures are `0`, and commit rejects `EOPNOTSUPP` because the
  host helper ABI is still missing. This closes the regression recovery only,
  not native present.
  Wave55 strict-present artifacts keep the same terminal fail-closed state and
  add query accounting: query ioctl/support is compiled, and query is skipped
  after commit failure rather than being absent from the kernel.

#### 3C3B. GPU-to-FB-BO Sysmem Staging Is Not Final Native Present

##### 3C3B-1. Proven Intermediate Sysmem Lane

- [x] Allocate or import an existing-sysmem FB BO staging target.
- [x] Create/export/import the existing-sysmem FB BO staging target.
- [x] Add testability for synthvid/VRAM-backed existing-sysmem targets in the
  kernel diagnostics.
- [ ] Attempt D3D12 GPU copy into the FB BO existing-sysmem target when the host
  supports the required path.
- [ ] Attempt D3D12 GPU copy into a synthvid/VRAM-backed existing-sysmem target
  once runtime validation exists.
- [x] Present the staged target through `FB_BO_PRESENT`.
- [x] Prove the FB BO existing-sysmem lane is staging/fail-closed rather than
  native display handoff.

##### 3C3B-2. Explicit Non-Acceptance As Native Evidence

- [x] Record that the staged path has no D3D12 copy and no native handoff.
- [x] Explicitly reject GPU-to-FB-BO sysmem staging as native-present evidence.
- [x] Explicitly reject GPU-to-FB-BO sysmem staging as 480p FPS gate evidence.
- [x] Explicitly reject GPU-to-FB-BO sysmem staging as WebKit acceleration
  evidence.
- [x] Build and validate the pending `wlcomp` diagnostics for display target
  kind and required host protocol.
- [x] Fix and validate the `/dev/dxg` `dxg_existing_sysmem` summary diagnostic.
- [x] Add compositor target-kind, dirty-rect, and no-readback evidence guards
  for a future kernel success.
  Pending Confucius evidence keys: `d3d12_display_target_kind=none` and
  `d3d12_display_target_requires_kernel_host_protocol=1` should make the
  missing final display handoff explicit once built and run. Until then, treat
  them as pending validation, not evidence that closes native present.
  Wave32 note: the first `dxgprobe --fb-existing-sysmem` run is fail-closed.
  Existing-sysmem allocation appears to succeed according to
  `dxg_allocation_priv`, but the `dxg_existing_sysmem` summary is broken/zero
  and `FB_GPU_BO_PRESENT` returns `-1`. No sysmem-staging validator item is
  closed by this run.
  Wave33 evidence:
  `/tmp/xv6-hyperv-build/wave33-fb-existing-sysmem.log` shows
  `dxgprobe --fb-existing-sysmem` now fail-closes with `present_rc=0` and
  `pin_proof=PASS`, so the intermediate existing-sysmem FB BO staging target
  and `FB_BO_PRESENT` path are proven. The run still performs no D3D12 copy,
  and `/dev/dxg` `dxg_existing_sysmem` summary remains zero, so no native
  present, FPS, or WebKit evidence is closed by this lane.
  Wave34 update:
  `/tmp/xv6-hyperv-build/wave34-d3d12-syncfile-require-present.log` validates
  the Confucius display-target diagnostics: `d3d12_display_target_kind=none`
  and `d3d12_display_target_requires_kernel_host_protocol=1`. Wave34 still
  shows the sysmem `dxg_existing_sysmem` summary has a PFN argument shift, so
  the `/dev/dxg` summary diagnostic remains open until Wave35.
  Wave35 evidence:
  `/tmp/xv6-hyperv-build/wave35-fb-existing-sysmem.log` closes the sysmem
  summary diagnostic: `present_rc=0`, `present_errno=0`, `pin_proof=PASS`,
  and `status=FAIL_CLOSED_NO_D3D12_COPY`. `/dev/dxg` now reports
  `dxg_existing_sysmem=attempts:1 path:1 standard:1 writable:1 ... size:65536
  pages:16 first_pfn:0x7c803 last_pfn:0x7c81a pin_ret:0 set_ret:0 pin_ok:1
  set_ok:1 total_pages:16`. This still does not close D3D12 copy into FB BO,
  real native handoff, FPS, or WebKit gates.
  Latest existing-sysmem lane update: WSL comparison says existing-sysmem is a
  D3DKMT backing-store path, not a display-present path by itself. Kernel
  source now adds 64K-aligned FB scanout-map testability, PFNMAP/MMIO pinning
  through `SETEXISTINGSYSMEMPAGES`, and `/dev/dxg` diagnostics
  (`dxg_existing_sysmem_target`, `pfnmap_ok`, `vram_gpa`, `vram_size`).
  Compositor source now adds target-kind, dirty-rect, and no-readback evidence
  guards for a future kernel success. Present-source commit remains
  fail-closed, so real native handoff, positive completion, FPS/WebKit, and
  backend flag gates remain open.
  Wave42 runtime evidence: `dxgprobe --fb-existing-sysmem` remains
  staging/fail-closed. Pin proof passes and existing-sysmem pin/set is OK for
  FB BO pages, but `fb_bo_present_cpu_blit=1`,
  `native_host_display_handoff=0`, `d3d12_copy_possible=0`, and `/dev/dxg`
  reports `dxg_existing_sysmem_target=pfnmap_pages:0 ... vram:0`. This closes
  only the FB-BO-staging proof; direct `FB_GPU_SCANOUT_MAP`/VRAM
  existing-sysmem validation remains open.
  Wave43 update: direct scanout/VRAM existing-sysmem validation now passes with
  nonzero PFNMAP pages and `vram:1`, but it remains fail-closed with no D3D12
  copy bridge and no native host display handoff.

#### 3C4. Destination-Resource Validators

- [x] Add a validator artifact proving the destination D3D12 resource exists
  and is not a host-pointer heap.
- [x] Require same-LUID source resource, acquire fence, and destination resource
  in the same run.
- [x] Require copy/composite completion into the destination resource before
  accepting any native-present completion.
- [x] Reject import/fence/acquire/copy-only evidence when present-source commit
  fail-closes.
- [x] Reject import-only, fence-only, copy-only, fail-closed, incomplete
  present-id, start-only, callback-only, and release-only evidence in the
  present-evidence selftest.
- [ ] Prove the same rejection rules are exercised by the full runtime
  present/FPS validator artifact.
- [ ] Require no CPU map/readback, DRI software present, framebuffer blit-only,
  or host-pointer heap fallback in the destination proof.
  Wave30 validator note: the artifact proves a non-host-pointer
  compositor-owned D3D12 texture destination, same-LUID source resource/acquire
  fence, and completed copy before any native-present completion is accepted.
  Leave callback/release/display acceptance and broader no-fallback validator
  gates open until display handoff exists.
  Wave33 strict-present evidence:
  `/tmp/xv6-hyperv-build/wave33-d3d12-syncfile-require-present.log` and
  `/tmp/xv6-hyperv-build/wave33-d3d12-syncfile-require-present-dxg.log` prove
  shared-resource import, fence import, dxg monitored acquire, D3D12 GPU copy,
  and no CPU readback, then fail-close the present-source commit with
  `EOPNOTSUPP`. Native completion, buffer release, and frame callback remain
  absent, so the broad callback/release and no-fallback validators stay open.
  Wave34 strict-present evidence:
  `/tmp/xv6-hyperv-build/wave34-d3d12-syncfile-require-present.log` and
  `/tmp/xv6-hyperv-build/wave34-d3d12-syncfile-require-present-dxg.log` repeat
  resource/fence import success, dxg monitored acquire, compositor-owned D3D12
  copy, and no CPU readback. They also print the full
  `d3d12_present_missing=... host display handoff needs kernel/host protocol`
  diagnostic, `display_target_kind=none`, present-source register success,
  commit `errno=95`, and `backend_opengl_submit=0`. This closes diagnostics
  only; real handoff/completion remains open.
  Wave39 strict-present evidence:
  `/tmp/xv6-hyperv-build/wave39-d3d12-require-present-short.log` reaches the
  real D3D12 runtime path, proves resource export/open and fence export, and
  uses the pure-DXG syncfile acquire path. The paired evidence file proves
  Wayland commit, same-LUID compositor resource/fence import, fence target
  satisfaction, D3D12 protocol acceptance, completed compositor-owned D3D12 GPU
  copy, no CPU/readback, and fail-closed present-source rejection. This closes
  the import/acquire/copy/fail-closed evidence slice only; display handoff,
  native completion, callback/release, FPS, and WebKit remain open.
  Wave42 selftest evidence: `d3d12sharedsmoke --present-evidence-selftest`
  passes positive and negative cases, including import-only, copy-only,
  fail-closed, and incomplete-present-id rejection. This validates the rejection
  logic but does not prove a successful native display handoff.

#### 3D. Backend Flag Gate

- [ ] Keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT` disabled while native completions
  are zero.
- [ ] Enable `FB_GPU_BACKEND_F_OPENGL_SUBMIT` only after the native-present
  validator passes.
- [ ] Require `backend_opengl_submit 1` only after native display handoff commit
  success and completion counters are nonzero.
- [ ] Require `backend_opengl_submit 1` only after frame callback/release and
  480p FPS validator evidence agree with native present completions.
- [ ] Verify `fbstat` reports the flag consistently after enabling.
- [ ] Verify virgl/KVM behavior is not regressed by the Hyper-V gate.
  Wave 10 validation note: default `mesaglfeature` softpipe success and
  explicit D3D12 EGL failure both leave `backend_opengl_submit 0`; neither
  closes the backend flag gate.
  Wave33 note: `/tmp/xv6-hyperv-build/wave33-boot-fbstat.log` and the strict
  present artifacts keep `backend_opengl_submit 0`; do not enable the backend
  flag until real native present completions exist.
  Wave34 note: strict-present diagnostics still report
  `backend_opengl_submit=0`.
  Wave39 note: `fbstat` in
  `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log` reports
  `dxg_present_register_attempts 1`, `dxg_present_register_successes 1`,
  `dxg_present_commit_attempts 1`, `dxg_present_commit_rejects 1`,
  `dxg_present_host_handoff_missing 1`, and `backend_opengl_submit 0`.
  Wave40 strict-present evidence keeps the same backend discipline:
  present-source commit fail-closes with `errno=95`, no fake display counters
  are reported, and `backend_opengl_submit` remains `0`.
  Wave41 strict-present again keeps `backend_opengl_submit 0` while the
  present-source commit is blocked on `dxg-resource-scanout-bind`.
  Wave54 boot fbstat keeps `backend_opengl_submit 0` after restoring terminal
  fail-closed GPU-copy/commit evidence.

#### 3E. Phase-Exit Validation

##### 3E1. Fresh Evidence Sources

- [ ] Require `fbstat` native-present evidence.
- [ ] Require compositor native-present evidence.
- [ ] Require strict smoke native-present evidence.
- [x] Require `fbstat` fail-closed present-source evidence while native handoff
  is intentionally missing.
- [x] Require compositor fail-closed present-source evidence while native
  handoff is intentionally missing.
- [x] Require strict smoke fail-closed evidence while native handoff is
  intentionally missing.
  Wave39 evidence supplies the fail-closed artifact bundle across strict smoke,
  compositor evidence, and `fbstat`. The native-present evidence bullets remain
  unchecked until commit success/completions exist.

##### 3E2. Same-Frame Completion Requirements

- [x] Require no CPU readback evidence in the same run.
- [ ] Require frame callback and buffer release for the same submitted frame.
- [x] Require native copy/composite completion for the same imported resource.
- [ ] Close this phase only when all three evidence sources describe the same
  fresh presented frame sequence.
  Wave39 update: all three fail-closed evidence sources describe the same
  rejected present lane, but not a successful presented frame sequence.

#### 3F. Native Present Artifact Split

##### 3F1. Correlated Artifact Inputs

- [ ] Produce one fresh `/tmp/wlcomp-d3d12-present` artifact with native D3D12
  present attempts and completions for imported compositor resources.
- [x] Produce one fresh present evidence artifact showing fail-closed D3D12
  present attempts for imported compositor resources.
- [x] Produce one strict-present parser/provenance live artifact.
- [x] Correlate the fail-closed compositor artifact with `fbstat` backend and
  present-source counters from the same run.
- [x] Re-run the fail-closed strict-present artifact after the shell prefix
  command execution fix is rebuilt/deployed.
- [ ] Correlate a successful native-present compositor artifact with `fbstat`
  backend and native-present completion counters from the same run.
- [x] Print and correlate the new `dxg_present_*` fields in `fbstat`.
- [x] Correlate the strict smoke artifact with the same fail-closed frame
  sequence.
- [ ] Correlate the strict smoke artifact with the same successful native
  present frame sequence.
- [x] Include blocked frame callback and buffer release counters in the same
  fail-closed artifact.
- [ ] Include successful frame callback and buffer release counters in the same
  native-present artifact.
- [x] Include native copy/composite attempt and completion counters in the same
  artifact.
- [x] Include strict compositor evidence fields for fail-closed present id and
  completed counters.
  Wave39 evidence:
  `/tmp/xv6-hyperv-build/wave39-d3d12-require-present-short.log` plus
  `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log` provide the fresh
  fail-closed bundle. The artifact proves attempts, import/acquire, copy
  completion, present-source register/commit reject, blocked callback/release,
  and matching `fbstat` `dxg_present_*` fields. It does not prove successful
  native display presentation.
  Wave40 evidence repeats the strict-present fail-closed bundle after the shell
  prefix fix rebuild/deploy and proves prefixed guest commands work with
  `FOO=bar fbstat`.
  Wave41 fail-closed bundle records `present_id=0` and `completed=0`; keep
  successful native-present artifact requirements open.
  Wave44 artifact:
  `/tmp/xv6-hyperv-build/wave44-d3d12-require-present.log` preserves the
  stricter compositor evidence fields and keeps the artifact fail-closed rather
  than treating incomplete `present_id`/`completed` state as success.
  Wave45 artifact:
  `/tmp/xv6-hyperv-build/wave45-d3d12-require-present.log` proves the
  strict-present parser/provenance fields are live. It remains fail-closed and
  does not close successful native-present evidence.
  Wave48 artifact:
  `/tmp/xv6-hyperv-build/wave48-d3d12-present-artifact.log` refreshes the
  fail-closed runtime evidence with resource/fence import, GPU copy, selected
  final handoff lane, missing kernel ABI, `present_id=0/completed=0`, blocked
  callbacks/releases, no CPU/readback, and `backend_opengl_submit 0`.
  Wave49 artifact:
  `/tmp/xv6-hyperv-build/wave49-d3d12-present-artifact-clean.log` refreshes the
  same fail-closed runtime evidence and sharpens the dependency to
  `host-display-helper/resource-scanout-bind` candidate commands 34/35/38.

##### 3F2. Fallback Rejection In Artifact

- [ ] Prove CPU map/readback, DRI software present, framebuffer blit-only, and
  partial-present counters did not satisfy the native-present gate.
- [ ] Keep native-present completion evidence separate from Phase 4 FPS
  throughput evidence.
  Wave31b evidence:
  `/tmp/xv6-hyperv-build/wave31b-d3d12-syncfile-require-present-dxg.log`
  shows `fbstat` now prints the `dxg_present_*` fields: one register
  attempt/success, one commit attempt/reject, `host_handoff_missing=1`, last
  handles matching `wlcomp`, and `backend_opengl_submit=0`. This closes only
  the fbstat diagnostic-print gap, not native present completion.

#### 3G. Future-Present Counter Contract

- [x] Define current fail-closed present-source counter names for attempts,
  commit rejects, and host handoff missing.
- [ ] Define compositor counter names for successful native D3D12 present
  completions and skipped imports after host handoff exists.
- [x] Define separate counters for protocol acceptance, fence-target
  satisfaction, and blocked frame callback/release in the fail-closed artifact.
- [x] Define fail-closed `present_id` and `completed` evidence fields so
  incomplete present ids cannot satisfy native-present acceptance.
- [ ] Define separate counters for successful frame callbacks and buffer
  releases after native handoff exists.
- [ ] Define separate counters for framebuffer blit, partial-present, CPU map,
  and DRI software-present fallback paths.
- [ ] Require every future-present artifact to include a monotonic sequence or
  timestamp window tying attempts, completions, and failures to the same run.
- [ ] Require `backend_opengl_submit` to remain `0` until completion counters
  and no-fallback counters satisfy the native-present gate.

#### 3H. Validator Counter Identity Hardening

- [x] Add source-level current-run identity keys for the compositor/smoke
  present evidence path.
- [x] Add source-level callback/release same-frame evidence keys.
- [ ] Add per-client native-present counters so one client's progress cannot
  satisfy another client's validator run.
- [ ] Add per-resource native-present counters keyed by resource handle or
  exported-resource generation.
- [ ] Add resource-generation counters that increment across destroy/recreate
  so stale resource evidence cannot satisfy a fresh run.
- [x] Include client id, resource id, resource generation, and compositor run id
  in `/tmp/wlcomp-d3d12-present`.
- [ ] Require validator matching on client/resource/generation before accepting
  native-present attempts or completions.
  Wave50 source accounting adds current-run identity and callback/release
  same-frame evidence keys, but native-present completion counters and runtime
  acceptance artifacts remain open until the host handoff succeeds.
  Wave50 runtime prep proves the identity/helper fields are emitted in
  `/tmp/wlcomp-d3d12-present`; it does not yet prove successful native-present
  counters because the strict-present path regressed before GPU-copy/commit.
  Wave61 runtime evidence refreshes the fail-closed identity/provenance keys in
  `/tmp/wlcomp-d3d12-present` and keeps acceptance false with
  `d3d12_native_present_requirements_satisfied=0`, `strict_pass=0`, and
  `backend_opengl_submit 0`. Matching-based native-present acceptance remains
  open until a completion exists.

#### 3I. Native Present Owner Queue

Owner hints: compositor GPU-present worker owns the implementation; validator
worker owns counter identity and stale-evidence rejection.

- [x] Select the current non-readback D3D12 import/acquire/copy/fail-closed
  mechanism and name the expected artifacts for that path.
- [ ] Select and implement the final native display handoff mechanism behind
  the present-source commit path.
- [x] Implement imported-resource GPU copy/composite without CPU map or readback
  in the strict-present proof.
- [ ] Prove the final native display handoff artifact avoids DRI software
  present, framebuffer blit-only, and partial-present fallback.
- [ ] Expose per-client/per-resource/per-generation attempts, completions,
  failures, and skipped-import counters.
- [ ] Produce one fresh artifact tying native-present completions to `fbstat`,
  compositor counters, and strict smoke evidence from the same frame sequence.
- [x] Add `fbstat` output for the new `dxg_present_*` diagnostics.
- [ ] Keep `backend_opengl_submit 0` until native non-readback D3D12 shared-
  resource present works and the native-present validator accepts the run.
  Wave31b update: the `fbstat` `dxg_present_*` diagnostic-print gap is closed.
  The fields report the fail-closed path, including register success, commit
  reject, matching handles, `host_handoff_missing=1`, and
  `backend_opengl_submit=0`; real handoff/native-present acceptance remains
  open.

### Phase 4: 480p FPS Gate Without Inflation

#### 4A0. Current Dependency Guard

- [x] Record that Wave48/Wave49 leave `backend_opengl_submit 0` and
  `present_id=0/completed=0`, so FPS remains blocked by Phase 3 native-present
  completion.
- [x] Record that Wave53 protocol-only evidence with zero GPU-copy/commit
  attempts cannot satisfy FPS preconditions.
- [x] Keep Phase 4 as a runtime throughput gate, not a place to re-accept
  import-only, copy-only, fail-closed, callback-only, release-only, or
  displayed/demo-FPS-only evidence.
- [x] Update scripts to consume final-handoff/current-run provenance while
  preserving the fail-closed FPS gate.

#### 4A. Inflation Audit

- [x] Re-audit the FPS overlay against the observed inflation report.
- [x] Re-audit the FPS validator against the observed inflation report.
- [x] Treat displayed FPS around 40 as a failure if visible progress is
  single-digit.
- [x] Identify every counter that can advance without a completed native D3D12
  present.
- [x] Exclude those counters from the acceptance metric.
  Evidence: first-wave audit left `ports/wayland/src/gl_fps_overlay.c`
  unchanged and hardened `scripts/hyperv-3d-fps-validate.sh` so app/demo,
  callback, compositor, and display counters are context only unless completed
  native D3D12 presents and visible scene progress agree.

#### 4B. Acceptance Metric

- [x] Use completed native D3D12 present counters as the primary metric.
- [x] Use `effective_presented_fps` derived from native/display/visual
  presented evidence instead of display/demo FPS alone.
- [x] Require valid `present_id` and `completed >= present_id` before
  fail-closed or incomplete-present-id evidence can contribute to effective FPS.
- [x] Measure over a finite post-warmup elapsed window.
- [x] Require the elapsed sample to cover most of the configured window.
- [x] Keep demo-reported FPS as context only.
- [x] Keep compositor FPS as context only.
- [x] Reject a pass if native-present completions and visible progress do not
  agree.
  Evidence: `scripts/hyperv-3d-fps-validate.sh` now brackets both the sample
  window and the thumbnail window with `/tmp/wlcomp-d3d12-present` counters.
  Wave40 source update: FPS/WebKit scripts now compute
  `effective_presented_fps` from native/display/visual presented evidence. This
  hardens the metric but does not close the >60 FPS gate until runtime evidence
  exists.
  Wave44 source/artifact update: stricter present-id/completed gates reject
  incomplete-present-id evidence before it can inflate
  `effective_presented_fps`.

#### 4C. Render Size And Scene Validity

- [x] Require full 640x480 or equivalent 480p rendering.
- [x] Require `render_div == 1`.
- [x] Reject cropped render targets.
- [x] Reject overlay-only progress.
- [x] Reject title-bar-only progress.
- [ ] Confirm the demo is visible, closeable, and resizable.
  Evidence: `scripts/hyperv-3d-fps-validate.sh` rejects sub-480p
  window/render dimensions, `render_div != 1`, and pixel progress limited to
  the title/FPS-overlay band.

#### 4D. Visual Progress Evidence

- [x] Capture multiple Hyper-V thumbnails after warmup.
- [x] Compare visible progress outside the title area.
- [x] Compare visible progress outside the FPS overlay area.
- [x] Require frame-to-frame pixel progression in the rendered scene.
- [x] Save enough diagnostic context to explain any FPS/visual mismatch.
  Evidence: `scripts/hyperv-3d-fps-validate.sh` captures repeated Hyper-V
  raw thumbnails, ignores the title/FPS-overlay band, logs changed-pixel
  samples, visual cadence, and visual-window native-present counters.

#### 4E. Phase-Exit Validation

##### 4E1. Throughput And Visibility

- [ ] Sustain more than 60 native D3D12 presented frames per second after
  warmup.
- [ ] Prove the run used a 480p render target.
- [ ] Prove visible scene progress matches the native-present counter.

##### 4E2. Backend Flag And Final Pass

- [ ] Prove `backend_opengl_submit 1` is honestly enabled.
- [ ] Close this phase only when the strict finite GUI validator passes on
  Hyper-V.

#### 4F. FPS Validator Artifact Split

##### 4F1. Finite Window Artifact

- [ ] Produce one fresh `scripts/hyperv-3d-fps-validate.sh` artifact with a
  finite post-warmup elapsed window and completed native D3D12 present
  counters as the primary metric.
- [ ] Include thumbnail pixel progress outside the title and FPS overlay bands
  from the same sample window.

##### 4F2. Context-Only Counters And Negative Case

- [ ] Include demo FPS, compositor FPS, callbacks, releases, and display
  completions as context only, not acceptance.
- [x] Include `effective_presented_fps` as the acceptance-facing FPS field once
  native/display/visual evidence is available.
- [x] Reject incomplete `present_id`/`completed` evidence from
  `effective_presented_fps`.
- [ ] Reject a run where displayed/demo FPS is near 40 but visible progress is
  single-digit or native-present completions do not agree.
- [ ] Keep FPS throughput evidence separate from WebKit consumer evidence.

#### 4G. Wave 7 FPS Anti-Inflation Evidence

- [ ] Capture the finite post-warmup sample window start/end time and elapsed
  duration in the validator artifact.
- [ ] Capture native D3D12 present completion counter deltas from the same
  post-warmup window.
- [ ] Capture thumbnail pixel-progress deltas outside title and FPS overlay
  regions from the same window.
- [ ] Record demo FPS, compositor FPS, callbacks, releases, and display
  completions as context-only fields in the same artifact.
- [ ] Add or preserve a negative artifact where high displayed/demo FPS with
  single-digit visible progress fails the gate.
- [ ] Keep FPS anti-inflation evidence blocked on Phase 3 native-present
  completion evidence; it must not advance WebKit or backend flags by itself.

#### 4H. Validator Freshness And Content Identity

##### 4H1. Run Identity

- [ ] Add a run-id nonce generated by the FPS validator and propagated into
  compositor/native-present artifacts.
- [ ] Reject `/tmp/wlcomp-d3d12-present` entries with missing or mismatched run
  id.

##### 4H2. Content Identity

- [ ] Record content CRC or equivalent frame-content hash for the sampled
  thumbnail region outside title/FPS overlays.
- [ ] Require content CRC changes to correlate with native-present completion
  deltas during the same finite sample window.
- [ ] Add a stale-evidence negative case where an old run id or old content CRC
  cannot satisfy the FPS gate.

#### 4I. FPS Anti-Inflation Owner Queue

Owner hint: validation scripts/FPS worker.

- [ ] Produce a post-warmup finite-window artifact after Phase 3 native-present
  completion evidence exists.
- [ ] Require completed native D3D12 present deltas from the same run id and
  resource generation as the thumbnail sample.
- [ ] Require thumbnail pixel progress outside title/FPS overlay bands during
  the same elapsed window.
- [ ] Preserve a negative artifact where displayed/demo FPS around 40 fails
  when visible progress or native-present completions are single-digit.
- [ ] Keep the 480p demo gate open until the native-present path sustains more
  than 60 FPS after warmup and `backend_opengl_submit 1` is honestly enabled.

#### 4J. Final 480p Destination-Present Gate

##### 4J1. Destination-Present Preconditions

- [ ] Run the 480p FPS validator only after destination-resource GPU-copy
  completions and native-present completions are nonzero.
- [ ] Require the 480p run to use the D3D12-resource-backed display destination
  rather than a host-pointer heap or CPU/readback fallback.
- [ ] Require the 480p run to report `effective_presented_fps > 60` from
  native/display/visual presented evidence.

##### 4J2. Same-Resource User-Visible Progress

- [ ] Require frame callback and buffer release for the same presented resource
  generation.
- [ ] Require visible thumbnail progress to correlate with completed
  destination-resource presents.
- [ ] Keep displayed/demo FPS as context until the destination-present
  completion gate passes.

### Phase 5: WebKit Shared-Surface Consumer

#### 5A. Gate Discipline

##### 5A0. Current Blocker Accounting

- [x] Record that WebKit remains blocked by Phase 3 native-present completion
  and Phase 4 FPS acceptance, not by render-node discovery alone.
- [x] Record that Wave48/Wave49 keep `backend_opengl_submit 0`; WebKit must
  remain gated off until that backend flag is enabled by the native-present/FPS
  contract.
- [x] Record that Wave53 protocol-only evidence with zero GPU-copy/commit
  attempts cannot enable WebKit acceleration.
- [x] Update scripts to consume final-handoff/current-run provenance while
  preserving the fail-closed WebKit gate.

##### 5A1. Prerequisite Phase Gates

- [ ] Keep Hyper-V WebKit acceleration gated off while Phase 0 is open.
- [ ] Keep Hyper-V WebKit acceleration gated off while Phase 1 is open.
- [ ] Keep Hyper-V WebKit acceleration gated off while Phase 2 is open.
- [ ] Keep Hyper-V WebKit acceleration gated off while Phase 3 is open.
- [ ] Keep Hyper-V WebKit acceleration gated off while Phase 4 is open.

##### 5A2. Virgl Separation

- [ ] Keep virgl WebKit acceleration behavior separate from the Hyper-V gate.

#### 5B. Shared-Surface Routing

##### 5B1. Required Shared-Surface Path

- [ ] Route WebKitGTK through the same Mesa D3D12 path as native Mesa clients.
- [ ] Use the same D3D12 shared-resource Wayland protocol.
- [ ] Use the same monitored-fence contract.
- [ ] Use the same adapter-LUID validation.

##### 5B2. Disallowed Soft Evidence

- [ ] Reject a separate software fallback after the GPU contract gate passes.
- [ ] Reject dmabuf-request-only evidence.
- [ ] Reject render-node-presence-only evidence.

#### 5C. WebKit Validator Evidence

##### 5C1. Fresh Shared-Surface Evidence

- [ ] Require fresh D3D12 shared-resource import evidence.
- [ ] Require fresh fence import evidence.
- [ ] Require fresh fence target evidence.
- [ ] Require fresh native-present evidence.

##### 5C2. Current-Run Identity

- [ ] Require matching client and compositor adapter LUIDs.
- [ ] Require the evidence to be from the current WebKit run.

#### 5D. Negative Evidence Rejection

##### 5D1. Stale Or Software Rejection

- [ ] Reject stale `/tmp/wlcomp-d3d12-present`.
- [ ] Reject DRI software readback.
- [ ] Reject copy-export fallback.
- [ ] Reject CPU map/readback.

##### 5D2. Callback/Release-Only Rejection

- [ ] Reject callback-only evidence.
- [ ] Reject release-only evidence.

#### 5E. Phase-Exit Validation

##### 5E1. Gate Behavior

- [ ] Close this phase only when WebKit uses the same validated shared-surface
  OpenGL-submit contract as native Mesa clients.
- [ ] Prove WebKit stays gated off when that contract is unavailable.
- [ ] Prove WebKit turns on only after the native-present and FPS gates pass.

##### 5E2. Validator Tie-In

- [ ] Capture validator logs tying WebKit acceleration to the same fresh native
  D3D12 present evidence.
- [x] Update WebKit/FPS validator logic to consume `effective_presented_fps`
  instead of trusting displayed/demo FPS alone.
- [x] Update WebKit/FPS validator logic to reject incomplete
  `present_id`/`completed` evidence before accepting effective presented FPS.
  Wave40 source update: FPS/WebKit scripts now use `effective_presented_fps`
  based on native/display/visual presented evidence. WebKit remains gated until
  native present and FPS runtime artifacts pass.
  Wave44 source update: the same anti-inflation gates reject fail-closed or
  incomplete-present-id artifacts for WebKit/FPS acceptance.

#### 5F. WebKit Gate Artifact Split

##### 5F1. Gated-Off Artifact

- [ ] Produce one fresh WebKit validator artifact proving Hyper-V remains gated
  off while any Phase 0/1/2/3/4 contract is open.

##### 5F2. Enabled Artifact

- [ ] After the earlier phases pass, produce one WebKit run artifact proving it
  uses the same D3D12 shared-resource, monitored-fence, adapter-LUID, and
  native-present path as native Mesa clients.

##### 5F3. Stale/Soft Evidence Rejection

- [ ] Reject stale `/tmp/wlcomp-d3d12-present` and stale FPS evidence before
  WebKit preflight.
- [ ] Reject render-node-only, dmabuf-request-only, environment-only, callback-
  only, and release-only evidence.
- [ ] Keep WebKit acceleration evidence separate from the native Mesa/FPS gates
  while still requiring those gates as prerequisites.

#### 5G. WebKit Animated-Content Correlation

##### 5G1. Animated Content Fixture

- [ ] Add a WebKit animated-content sample that changes pixels inside the
  accelerated content area.

##### 5G2. Content/Native-Present Correlation

- [ ] Correlate WebKit content CRC/frame hash changes with native-present
  completions from the same run id.
- [ ] Require WebKit client/resource/generation identifiers to match the
  compositor native-present counters.

##### 5G3. Non-Content Rejection

- [ ] Reject WebKit acceleration evidence when only chrome, cursor, callbacks,
  or releases change.
- [ ] Keep WebKit animated-content proof blocked until Phase 3 native-present
  and Phase 4 FPS anti-inflation gates pass.

#### 5H. WebKit Contract Owner Queue

Owner hints: WebKit launcher/gating worker owns environment and gate behavior;
WebKit validator worker owns animated-content correlation.

- [ ] Keep Hyper-V WebKit acceleration disabled until Phases 0 through 4 have
  current passing artifacts from the same native shared-surface contract.
- [ ] Route WebKit through the same D3D12 shared-resource, monitored-fence,
  adapter-LUID, and native-present path used by native Mesa clients.
- [ ] Produce one gated-off artifact while any prerequisite phase remains open.
- [ ] Produce one enabled WebKit artifact only after native present and FPS
  gates pass, with matching client/resource/generation/run-id counters.
- [ ] Require the enabled WebKit artifact to include `effective_presented_fps`
  from the same native/display/visual evidence stream.
- [ ] Require WebKit animated-content CRC/frame-hash progress inside the
  accelerated content area, not chrome/cursor/callback/release-only progress.

#### 5I. Final WebKit Destination-Present Gate

- [ ] Keep Hyper-V WebKit disabled until the D3D12-resource-backed display
  destination path has native-present completions.
- [ ] Require WebKit to use the same destination-resource handoff ABI as native
  Mesa clients.
- [ ] Require WebKit frame callback/release/content-hash evidence from the same
  run as native-present completion counters.
- [ ] Reject WebKit acceleration evidence if it relies on host-pointer
  framebuffer heap import, CPU readback, DRI software present, or stale native
  present artifacts.

## Remaining Checklist

### DXG Kernel Semantics

Summary note: this section mirrors the granular active plan above. Treat the
unchecked bullets here as roll-up gates, not separate evidence shortcuts.

- [x] Restore or replace the missing WSL2 PCI guestcaps capability path.
  - Current source state: WSL's dxgkrnl reports `guest_caps.wsl2 = 1` through
    the Microsoft GPU-PV PCI function before reading the VMBus interface
    version, host caps, channel GUID, and host vGPU LUID. xv6 has matching
    source hooks in `kernel/kernel/pci.c` and
    `kernel/kernel/dev/hyperv_input.c`.
  - Verified evidence: the Hyper-V vPCI offer/protocol/config window now works:
    `hyperv_pci_bus=cfg:1 ... d0:1/1/0/0 query:1/1/0
    rel:2/56/1/28/1 child:1 ... first:1414:008e ... bdf:255:0:1`.
    The DXG PCI guestcaps write is observed and verified:
    `dxg_pci_guestcaps=... writes:1 found:1 verified:1 source:2 ...
    readback:<LUID low> ret:0 bdf:ff0001`, and
    `dxg_identity=pci_source:hyperv-vpci guestcaps:0/1/1 ...` reports that
    `host_vgpu_luid` matches the PCI LUID.
  - This closes the WSL2 PCI guestcaps/LUID identity item. It does not close
    D3D12 runtime resource NT export, native present, FPS, or WebKit.

- [x] Replace growable per-open trackers with a WSL-style `dxgprocess` object
  graph and typed handle table.
  - Current source state: `/dev/dxg` opens now bind to a process-scoped
    `hvdxg_process_state` keyed by `tgid`, with a shared typed handle table for
    devices, contexts, HW queues, paging queues, sync objects, allocations,
    resources, and GPUVA reservations. The metadata arrays are payload caches
    behind typed process-table ownership checks rather than the ownership
    boundary. The object table decodes WSL `hmgrtable` handle fields (index,
    unique, instance), keeps destroyed entries as stale tombstones instead of
    swapping them away immediately, and can reuse a destroyed slot when the host
    recycles the same handle index with a new unique value. Individual fd close
    now follows WSL's `dxgprocess` lifetime model: it drops the process ref
    instead of destroying process-owned host objects before the last process
    reference or explicit D3DKMT destroy.
  - New validator coverage: on the focused Hyper-V image,
    `dxgprobe --handle-lifetime-validate` created a device on a temporary
    same-process fd, closed that creator fd, and destroyed the still-live device
    from the surviving fd. It also destroyed a device from a second same-process
    fd, proved the original fd's stale destroy was rejected, then created and
    destroyed sync, paging-queue, allocation/resource, GPUVA, and device
    handles and proved stale destroy/free attempts were rejected before host
    dispatch. `/dev/dxg` reported object-table denials rising `0->8`, max
    tracked entries `10`, generation `10`, drops `0`, process reuses across
    same-process opens, and cleanup successes equal attempts; `fbstat` still
    reported `backend_opengl_submit 0`.
  - Process isolation is covered by `dxgprobe --owner-isolation`, which execs a
    child with the parent fd closed and proves a fresh process cannot destroy
    the parent's device handle.
  - This closes the process/object/handle-table semantics item. The exact host
    private payload equivalence and native present path remain separate open
    items below.

- [x] Match WSL resource/shared-resource sealing and metadata lifetime exactly.
  - Current source state: resource and sync NT fd export/open paths exist,
    clone tracked metadata, and hold the VFS file reference while query/open
    consumes the metadata. Shared resources now seal once, preserve runtime,
    resource, and per-allocation private metadata plus allocation sizes/flags,
    reject same-owner mutation after sealing, support repeated same-process and
    child-process opens, and keep the shared fd metadata usable after the
    original owner destroys its resource.
  - Proven on the focused Hyper-V image built from `/tmp/xv6-hyperv-build`:
    `dxgprobe --shared-lifetime-validate` created a cross-adapter shared
    resource, exported it as an NT fd, queried/opened it twice in the parent,
    verified sealed-resource add-allocation was denied by the kernel
    diagnostic counter, opened it from a child process, destroyed the original
    owner resource, and opened it again through the shared fd. `/dev/dxg`
    reported `dxg_sharedresource_lifetime=seals:1 reuses:8 denied:1
    open_tracked:4`; `fbstat` still reported `backend_opengl_submit 0`.
  - This closes the WSL-equivalent shared-resource metadata/lifetime piece; it
    does not by itself close the native D3D12 Wayland present path.
  - Wave45 reinforcement: `/tmp/xv6-hyperv-build/wave45-dxg-lifetime.log`
    proves WSL-style seal verification diagnostics include tracked allocation
    and private-size verification, and the exporter-close shared-resource
    lifetime matrix passes.

- [x] Prove basic pure-C NT resource and sync-object sharing.
  - Verified evidence: pure-C `dxgprobe` NT resource sharing now reports
    `share_resource_nt ok`, `query_resource_nt ok`, `open_resource_nt ok`, and
    child-process resource open success. Sync-object NT sharing also reports
    `share_sync_nt ok`, `open_sync_nt ok`, and child sync open success.
  - This closes the basic kernel/host NT-sharing contract for non-runtime
    pure-C resources and sync objects. It does not close
    `ID3D12Device::CreateSharedHandle(resource)` on the real D3D12 runtime
    resource.

- [x] Verify every WDDM private payload and return layout against the real UMD
  sequence.
  - Current source state: the UAPI surface includes the relevant context,
    allocation, GPUVA, residency, sync, shared-object, HW-queue, submit,
    sync-file, and process-enumeration ioctls. The kernel forwards large
    `CREATECONTEXTVIRTUAL`, `CREATEHWQUEUE`, allocation private data, and HW
    queue submit payloads far enough for real D3D12 work in observed runs. The
    `/dev/dxg` diagnostics now retain the full observed
    `CREATECONTEXTVIRTUAL` private payload (`3200` bytes in the current
    NVIDIA run), full `CREATEHWQUEUE` private payload (`124` bytes), and a
    larger HW-queue submit private-data head instead of only the old 8-byte
    summary. `dxgprobe --wddm-payload-validate` now reads those kernel-side
    diagnostics after a real `mesaglfeature` UMD run and fails unless the
    context, HW-queue create, and HW-queue submit private payload lengths and
    captured heads are nonzero. It also parses the kernel's forwarded
    `dxg_residency_last` packet and rejects the run unless the real second-FBO
    `LX_DXMAKERESIDENT` evidence is present as `count=2`, `flags=0x1`, nonzero
    input allocation handles, and identical wire allocation order.
    `scripts/hyperv-dxg-validate.sh` now runs that check immediately after
    `mesad3d12probe`, which wraps `mesaglfeature` with
    `GALLIUM_DRIVER=d3d12`, `MESA_LOADER_DRIVER_OVERRIDE=d3d12`,
    `LIBGL_ALWAYS_SOFTWARE=0`, and an explicit D3D12-renderer requirement.
    The validator rejects `softpipe`/`llvmpipe` output instead of allowing a
    software `mesaglfeature` pass to satisfy the WDDM gate. It also requires
    `wddm_layout_validate ok` for allocation return private data, GPUVA fence
    value, sync-object fence CPU/GPU mappings, lock/unlock return layout,
    destroy cleanup packets, and WSL-layout process creation, then captures
    `/dev/dxg`.
  - Earlier validator evidence closed the broad WDDM payload-layout item: the
    centralized Hyper-V run
    reported `mesaglfeature: D3D12 renderer confirmed renderer=D3D12
    (Microsoft Hyper-V GPU-PV Render Driver)`, both `mesaglfeature: pass
    size=32x32` and `pass size=64x32`, and `mesaglfeature: ok`. The
    second-FBO residency packet is now accepted as expected pending work:
    `makeresident ... flags=0x1 count=2 ... status=0x103 ret=259
    host_ret=0 pending_ok=1`.
  - The same run passed same-adapter WSL trace comparison with
    `wddm_trace_compare ok
    trace=/tmp/xv6-wsl-probe/mesaglfeature-nvidia-live.trace
    kernel=/tmp/xv6-hyperv-build/hyperv-dxg-validate.log context_priv=3200
    hwqueue_priv=124 submit_priv=1880 allocation_runtime=0
    allocation_priv=594 make_count=1 map_pages=272 cleanup_mask=0xff`.
    The stale lock/unlock and close-adapter mismatches are gone.
  - Current Wave33-Wave36 note: those artifacts only show MakeResident
    `count=1`; do not use this older summary to close the current
    MakeResident `count=2 flags=0x1` revalidation gate below.

- [x] Revalidate the real Mesa/NVIDIA second-FBO MakeResident
  `count=2 flags=0x1` path on the current focused image.
  - Earlier focused-image evidence:
    `dxgprobe --residency-batch-validate` forwards a WSL-sized
    `LX_DXMAKERESIDENT` batch with `count=2`, `flags=0x1`, `device=0`, and
    allocation order preserved, and the host returns `STATUS_PENDING` with a
    signaled paging fence.
  - Kernel-side diagnostics now print and expose through `/dev/dxg` the packet
    length, device, paging queue, flags, count, input and wire allocation order,
    host status, return value, fence, and trim count. The real Mesa second-FBO
    path also reaches `glFinish` with `count=2`, `flags=0x1`, host
    `STATUS_PENDING`, and no D3D12 device removal.
  - The current focused image no longer hangs in the tiny D3D12 Wayland smoke:
    `mesawlegl --d3d12 --frames=3 --size=320x240` completes, but it still
    refuses native present after `CreateSharedHandle` fails and therefore does
    not satisfy the 480p >60 FPS acceptance gate.
  - Historical open note: Wave33-Wave36 only showed MakeResident `count=1`, so
    the `count=2 flags=0x1` evidence stayed open until the Wave40 matrix.
  - Wave38 diagnostic note:
    `/tmp/xv6-hyperv-build/wave38-post-mesaglfeature-fbstat-dxg.log` fixes the
    malformed MakeResident diagnostic shape:
    `dxg_makeresident_shape=cmd:48 wsl_cmd:48 result:24 actual:24 owner_ok:1`.
    `dxg_residency_last` is sane in that run, but it only shows `count:1`; the
    later Wave40 matrix closes the `count=2 flags=0x1` revalidation.
  - Wave40 update: the user validator worker added and ran the dxgprobe
    MakeResident matrix with target `count=2 flags=0x1`, closing the current
    focused-image revalidation gate. This does not advance native present,
    FPS, backend flag, or WebKit gates.
  - Wave41 evidence is the current exact proof:
    `dxgprobe --residency-batch-validate` reports
    `residency_batch_matrix ... requested_count=2 requested_flags=0x1
    actual_count=2 actual_flags=0x1 rc=259 status=PASS`, and `/dev/dxg`
    reports `dxg_makeresident_shape=cmd:52 wsl_cmd:52 result:24 actual:24
    owner_ok:2 tracked:2 order:1`.

### Hyper-V OpenGL Present Path

- [x] Stop presenting Hyper-V D3D12 through the DRI software-present/readback
  lane.
  - Current source state: `mesademo` selects `GALLIUM_DRIVER=d3d12` on Hyper-V,
    `mesawlegl` uses a native Wayland EGL surface, and Mesa's D3D12 swrast
    presenter now defaults to requiring the native shared-resource path. If
    `resource_get_handle(... WINSYS_HANDLE_TYPE_FD ...)` fails, it logs
    `d3d12 native present unavailable; refusing DRI software/readback present`
    and returns without calling `flush_frontbuffer`. The diagnostic escape
    hatch is `XV6_D3D12_REQUIRE_NATIVE_PRESENT=0`.
  - Historical evidence on the focused Hyper-V image built from
    `/tmp/xv6-hyperv-build`: `mesawlegl --d3d12 --frames=3 --size=320x240`
    reached real Mesa D3D12, failed `CreateSharedHandle` with `hr=0x80004005`
    on a shared-heap simultaneous-access render target, and then refused the
    DRI readback lane. `/dev/dxg` showed the D3D12 shared createallocation
    succeeded (`flags=0x47`, `runtime=264`, `alloc_priv=594`,
    `out_priv=594`) and the NT export no-completed through both 32-byte WSL
    layout and fallback layout. Wave38 supersedes the resource-export failure,
    but this remains the evidence that the DRI readback lane is fail-closed.
  - This closes the dishonest readback-present path only. The native
    compositor-visible shared-resource present path remains open below.
  - Wave38 strict-present supersedes the old resource-export blocker for the
    smoke path: `d3d12sharedsmoke --runtime --require-present` now exports the
    runtime resource and opens it, and exports the fence. This item remains
    closed only for rejecting DRI software/readback present.

- [ ] Make the D3D12 shared-resource Wayland buffer path pass on the current
  Hyper-V runtime.
  - Current source state: `xv6_gpu_buffer_manager` protocol version 5 now has a
    D3D12 NT shared-resource buffer type, and `wlcomp` stores D3D12 resource fd,
    optional fence fd, compositor DXG fd, D3D12 device/resource handles, format,
    allocation count, total private-data size, source adapter LUID, and matched
    compositor adapter LUID on a non-mappable `wlcomp_buffer`. D3D12 shared
    buffers are deliberately kept out of the existing CPU/mappable framebuffer
    BO paint path.
  - Current evidence: Wave38 strict-present proves the resource-export/open
    slice on the current runtime:
    `/tmp/xv6-hyperv-build/wave38-d3d12-require-present.log` shows runtime
    `CreateSharedHandle(resource)` succeeds (`fd=37`, `handle=0x25`),
    `OpenSharedHandle(resource)` succeeds
    (`desc=256x256 fmt=87 flags=0x20 layout=0 samples=1`), and
    `CreateSharedHandle(fence)` succeeds (`fd=38`, `handle=0x26`,
    `flags=SHARED`). `/dev/dxg` records WSL-exact NT-share success
    (`dxg_ntshared_wsl_exact=ext32_zero_luid_natural:1/a0`,
    `dxg_ntshared_envelope ... ret:0/status:0x40000180/raw:0x40000180`) plus
    runtime shared-allocation metadata
    (`dxg_d3d12_shared_alloc=seen:1 ... runtime:264 res_priv:0 alloc_priv:594
    out_priv:594`).
  - Remaining blockers for this path: the strict run fails at
    `stage=present-failed-before-success`, direct D3D12
    `OpenSharedHandle(fence)` still returns `0xffffffff80070057`/`errno=22`
    and remains WSL-repro/expected-failing unless the chosen native path needs
    it, `/tmp/wlcomp-d3d12-present` was missing in the Wave38 run, and no
    compositor/native-present attempt, completion, frame callback, buffer
    release, or FPS/WebKit evidence was produced.

- [x] Extend Wayland protocol/glue for D3D12 resource type and fences.
  - Current source state: private `xv6_gpu_buffer_manager` version 5 advertises
    `create_d3d12_resource_buffer` and
    `create_d3d12_resource_buffer_with_fence`, plus LUID-carrying variants of
    both requests, and a v5
    `create_d3d12_resource_buffer_with_fence_value_luid` request for explicit
    monitored-fence target values. The requests distinguish D3D12 NT
    shared-resource fds from framebuffer BO handles and carry width, height,
    format, allocation count, total private-data size, optional acquire fence
    fd, optional source adapter LUID, and optional fence value. Client-side
    hand-written protocol copies in `xv6_present_buffer.c`, `xv6_egl_gles.c`,
    `d3d12sharedsmoke`, and Mesa's Wayland DRI loader match the advertised ABI;
    non-D3D12 framebuffer clients can still use the older subset.
  - Proven on the focused Hyper-V image built from `/tmp/xv6-hyperv-build`:
    `d3d12sharedsmoke` exported and queried a real DXG shared resource, committed
    it over the new Wayland request, and the compositor logged the D3D12 buffer
    metadata. This closes the protocol/glue contract only; it does not close
    compositor-side D3D12 import or GPU present.

- [ ] Implement GPU-side present/composite for D3D12 resources on Hyper-V.
  - Current source state: `FB_GPU_BO_PRESENT`, direct scanout, and compositor
    GPU compose speed up framebuffer BO presentation. They do not make a D3D12
    render target a native compositor input. The compositor explicitly refuses
    to treat an imported D3D12 resource as frame-callback-ready until a real
    D3D12 present/composite path is added.
  - Latest evidence: Wave38 moves past resource export/open and fence export,
    but no accepted D3D12 present/composite proof exists. The strict smoke fails
    at `stage=present-failed-before-success`, `/tmp/wlcomp-d3d12-present` was
    missing, and `fbstat` must still report `backend_opengl_submit 0`.
  - Still missing after resource export/open: compositor-side D3D12 copy/present
    from the shared resource, or an equivalent WSLg-style host compositor path,
    with no repeated CPU texture readback/copy. Import success, frame
    callbacks, release fences, or shared metadata alone must not be accepted as
    GPU-side present/composite proof.

### Validation

- [ ] Add pure-C D3D12 shared-resource export/import/open/present validation.
  - Current source state: `dxgprobe` now validates resource and sync NT fd
    export/query/open across parent/child opens, and `d3d12sharedsmoke` now
    validates the next pure-C contract slice: create a real DXG shared resource,
    query its sealed metadata, create/share a real monitored-fence sync object,
    send both fds through Wayland as a D3D12 resource buffer, and prove the
    compositor opens both the resource and fence on its own `/dev/dxg` device.
    The focused Hyper-V image logs `shared fence=... cpu=... gpu=...` and
    `wlcomp: d3d12 shared buffer ... source_luid=... matched_luid=...
    opened_fence=... fence_cpu=... fence_gpu=...`
    with `/dev/dxg` showing `dxg_sharedresource_lifetime=seals:1 reuses:3
    denied:0 open_tracked:1`. `dxgprobe --shared-private-validate` also embeds
    the captured real Mesa/NVIDIA shared-resource private payload sizes
    (`runtime=264`, `alloc_priv=594`) and verifies that replaying those bytes
    without the surrounding D3D12 UMD-created runtime state fails at
    `LX_DXCREATEALLOCATION` (`ret=-5`), while the real Mesa path creates the
    resource and fails later at NT export. The validation script now also runs
    the bad-LUID D3D12 smoke through the explicit `--bad-luid` argument and
    requires the compositor to reject it. `d3d12sharedsmoke --require-present`
    now requests a frame callback and waits for both callback and buffer
    release; `scripts/hyperv-3d-fps-validate.sh` fails unless that strict
    native-present smoke logs `present validation ok`. The Wayland private
    protocol is now version 5 for D3D12 buffers and `d3d12sharedsmoke` uses the
    adapter-LUID plus monitored-fence-value request, with the validator requiring
    the compositor log `d3d12 shared buffer fence target=1 current=1`.
  - Verified base coverage: `share_resource_nt ok`, `query_resource_nt ok`,
    `open_resource_nt ok`, child resource open success, `share_sync_nt ok`,
    `open_sync_nt ok`, and child sync open success.
  - Wave38 strict-present closes the real D3D12 runtime resource NT export and
    resource-open slice for this validator, and proves fence NT export. It does
    not close direct D3D12 fence open, compositor/native-present, frame
    callback, release, FPS, or WebKit gates.
  - Wave39 strict-present closes the compositor import/open, fence import,
    pure-DXG syncfile acquire, protocol acceptance, compositor-owned D3D12 GPU
    copy, no CPU/readback, and fail-closed present evidence slices for this
    validator. Evidence:
    `/tmp/xv6-hyperv-build/wave39-d3d12-require-present-short.log` and
    `/tmp/xv6-hyperv-build/wave39-d3d12-present-evidence.log`.
  - Still missing: real display handoff behind the present-source commit path,
    native present completion, frame callback and buffer release tied to native
    completion, FPS, and WebKit. The latest evidence still lacks
    `d3d12sharedsmoke: present validation ok`.

- [x] Resolve historical real D3D12 runtime `CreateSharedHandle(resource)`
  `-75`.
  - Wave38 evidence:
    `/tmp/xv6-hyperv-build/wave38-d3d12-require-present.log` shows
    `d3d12sharedsmoke --runtime --require-present` now succeeds at runtime
    `CreateSharedHandle(resource)` (`fd=37`, `handle=0x25`) and
    `OpenSharedHandle(resource)` (`desc=256x256 fmt=87 flags=0x20 layout=0
    samples=1`).
  - `/dev/dxg` proves the matching NT-share path and runtime metadata:
    `dxg_ntshared_wsl_exact=ext32_zero_luid_natural:1/a0`,
    `dxg_ntshared_envelope ... ret:0/status:0x40000180/raw:0x40000180`, and
    `dxg_d3d12_shared_alloc=seen:1 ... runtime:264 res_priv:0 alloc_priv:594
    out_priv:594`.
  - This closes resource NT export/open only. The strict run still fails at
    `stage=present-failed-before-success`, direct D3D12
    `OpenSharedHandle(fence)` still returns `0xffffffff80070057`/`errno=22`,
    `/tmp/wlcomp-d3d12-present` was missing, and native present/FPS/WebKit stay
    open.

- [x] Make the pure-C WSL-trace replay validator pass as an equivalence test.
  - Current source state: `dxgprobe --wsl-trace-replay` exists and replays an
    ordered device, paging queue, allocation, residency, GPUVA, lock, context,
    HW queue, submit, and teardown sequence.
  - Proven on the focused Hyper-V image: the validator now requires successful
    device, paging queue, allocation, residency, paging-fence progress, GPUVA,
    lock, context, HW queue, and cleanup, and treats the intentionally
    synthetic empty context submit and fake HW-queue packet as expected host
    `STATUS_INVALID_PARAMETER` negative cases. `/dev/dxg` confirms the host
    status for those packets while the positive setup path succeeds.

- [x] Add a finite GUI performance validator that fails below 60 FPS after
  warmup.
  - Current source state: `scripts/hyperv-3d-fps-validate.sh` warms up the
    focused Hyper-V desktop, runs the screenshot visual validator, and samples
    a finite post-warmup window. The primary FPS gate is native D3D12 present
    completions from `/tmp/wlcomp-d3d12-present` correlated with display
    completions from `fbstat`; the demo-owned `/tmp/mesawlegl-fps`
    `visible_fps` value is an anti-inflation cross-check rather than the
    acceptance metric. The demo probe records window size, render size, render
    divisor, native-present count, and its measurement source; the validator
    rejects sub-480p render targets, any `render_div != 1`, and sources other
    than completed native D3D12 presents. It also captures several Hyper-V
    thumbnails and requires repeated visible pixel progression outside the FPS
    overlay/title crop, so callback-only or overlay-only progress cannot pass.
    It still requires multiple fresh `callback_seq` values as a liveness
    sanity check, clears `/tmp/wlcomp-d3d12-present` before the native-present
    smoke and demo sample, refuses too-short warmup/sample windows, verifies the
    measured elapsed sample covers most of the configured window, logs
    `/tmp/wlcomp-fps` only as compositor context, and requires
    `backend_opengl_submit 1` so Hyper-V cannot pass before the validated
    native-present/shared-surface path and backend flag are honestly enabled.
    The desktop Hyper-V D3D12 launch environment no longer forces
    `XV6_MESAWLEGL_RENDER_DIV=2`; the validator and launcher now agree that the
    demo must render the full 640x480 surface for the 480p gate.
  - Validation: `bash -n scripts/hyperv-3d-fps-validate.sh` passes. The runtime
    validator scaffold is intentionally checked in but expected to fail on
    Hyper-V until the native D3D12 shared present path is completed and
    `FB_GPU_BACKEND_F_OPENGL_SUBMIT` is enabled.

- [ ] Make Hyper-V pass the finite 480p GUI/3D FPS gate with native D3D12
  presented frames.
  - Current source state: the FPS validator is intentionally strict: it requires
    full 480p rendering, finite post-warmup measurement, visible pixel
    progression, and `backend_opengl_submit 1`. It rejects callback-only,
    release-only, CPU/readback, state-only, import-only, and fence-only paths.
  - Latest validator evidence: compositor sampling remains far below the
    acceptance target (`/tmp/wlcomp-fps` samples near 1 FPS in the latest log),
    and Hyper-V still reports `backend_opengl_submit 0`. The FPS item cannot be
    checked until native D3D12 shared present/composite is visible and the demo
    sustains more than 60 FPS after warmup.

### WebKit Consumer Contract

- [ ] Implement the durable WebKitGTK accelerated-surface contract.
  - Current source state: WebKit acceleration and `webkit_dmabuf=1` are gated
    on `FB_GPU_BACKEND_F_OPENGL_SUBMIT`, so Hyper-V falls back safely while
    virgl can opt in. The desktop policy log now records the stronger
    `shared_surface` and `d3d12_present` contract bits next to
    `opengl_submit`; `scripts/hyperv-webkit-gpu-validate.sh` requires both to
    stay `0` on Hyper-V while the native shared-surface path is unavailable.
    The WebKit launcher paths now choose `GALLIUM_DRIVER=d3d12` with the same
    Mesa Wayland D3D12 shared-resource environment used by native Mesa clients
    when DXG transport, a render node, and `FB_GPU_BACKEND_F_OPENGL_SUBMIT` are
    all available; virgl remains the accelerated env only for virgl-backed
    systems. A requested WebKit dmabuf path now records requested vs effective
    dmabuf separately and cannot select a software fallback environment after
    passing the GPU contract gate. The WebKit GPU smoke rejects D3D12
    environments unless the launcher selected `d3d12-shared-surface`, native
    present remains required, copy-export fallback is disabled, and the backend
    reports the same `shared_surface`/`d3d12_present`/`opengl_submit` contract.
    The Hyper-V WebKit validator now also requires compositor native-present
    evidence from `/tmp/wlcomp-d3d12-present`, including shared resource,
    allocation metadata, fence, fence target, release fence, and matching source
    versus compositor adapter LUID. It rejects DRI software/readback, blocked
    callback/release-only paths, copy-export, CPU map/readback, partial-present
    counters, and a disabled native-present requirement. The validator now also
    requires the prior DXG/native-present and FPS/OpenGL-submit contract logs to
    be recent, and clears `/tmp/wlcomp-d3d12-present` before the WebKit
    preflight so stale native-present evidence cannot satisfy the WebKit GPU
    contract. Latest evidence still keeps Hyper-V gated off: the D3D12 renderer
    and present acceptance gates have not passed, and `fbstat` continues to
    report `backend_opengl_submit 0`.
  - Still missing: WebKit using the same render-node, D3D12/dmabuf/shared
    resource, and fence path as native Mesa clients in a passing Hyper-V run.
    The launch environment is now aligned, but an environment variable or
    render-node existence alone is not accepted evidence.

## Acceptance Gate

Hyper-V GPU/OpenGL support is complete only when all of the following are true:

- `mesaglfeature` passes on Hyper-V D3D12 without tracing or device removal.
- A Mesa Wayland client presents a D3D12-rendered surface through a shared GPU
  resource/fence path, not through DRI software readback.
- The desktop-launched 640x480 or equivalent 480p 3D demo is visible, closeable,
  resizable, and sustains more than 60 FPS after warmup.
- `fbstat` can honestly report `backend_opengl_submit 1` on Hyper-V.
- WebKit acceleration uses that same validated OpenGL-submit/shared-surface
  contract and stays gated off when the contract is unavailable.
