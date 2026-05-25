# GPU Remaining Gaps

Last updated: 2026-05-24

This file tracks only the work that is still missing or needs fresh parity
proof. Completed wave logs and historical evidence should live in the runtime
skills or validation artifacts, not in this active plan.

## Reference Baseline

### WSL2 Linux DXG Baseline

Audited against `/tmp/wsl2-linux-kernel-audit` at commit `85ceaf8`
(`linux-msft-wsl-6.18.y`).

The WSL2 GPU-PV driver is under `drivers/hv/dxgkrnl`, not
`drivers/gpu/dxgkrnl`. Its userspace ABI is
`include/uapi/misc/d3dkmthk.h`. It is a D3DKMT/DXG transport and object-lifetime
reference; it is not the Linux DRM/KMS/Nouveau display stack.

Use these WSL files as anchors when changing the Hyper-V DXG path:

- `dxgkrnl.h`: canonical `dxgprocess`, `dxgadapter`, `dxgdevice`,
  `dxgcontext`, `dxghwqueue`, `dxgresource`, `dxgallocation`,
  `dxgsharedresource`, `dxgsyncobject`, and `dxgsharedsyncobject` ownership.
- `hmgr.h` and `hmgr.c`: process handle-table shape, handle bit layout,
  unique/index/instance validation, destroyed tombstones, free-list reuse, and
  typed lookup.
- `dxgprocess.c`: process creation/destruction, adapter-local handle table,
  process adapter records, host process lifetime, and cleanup ordering.
- `ioctl.c`: D3DKMT ioctl marshalling, resource/sync NT sharing,
  `dxgsharedresource_seal()`, `open_resource()`, and process handle checks.
- `dxgsyncfile.c`: WSL's bridge from DXG monitored fences to Linux
  `sync_file`/`dma_fence`.
- `dxgvmbus.c` and `dxgvmbus.h`: host wire packet construction and return
  layouts for process, adapter, device, context, HW queue, allocation,
  residency, resource-open, sync, submit, and query paths.

### Linux GPU Interface Baseline

Use Linux DRM/GEM/TTM/KMS/Nouveau as the reference for `/dev/dri`,
render-node, PRIME/dma-buf, `dma_fence`, `dma_resv`, KMS atomic, PCI runtime,
and Nouveau behavior. Do not cite WSL `dxgkrnl` as evidence for DRM/KMS
scanout or Nouveau correctness.

## Non-Negotiable Gates

- Hyper-V must keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT == 0` until all of these
  are true in the same current validation lineage:
  `mesaglfeature` passes without device removal, D3D12 shared-resource present
  is native and non-readback, the 480p desktop 3D demo sustains more than
  60 FPS after warmup, and WebKit uses the same shared-surface contract.
- KVM/virgl is the only backend that may currently advertise OpenGL submit.
- Hyper-V present work must use GPU-P, DDA, or a WSLg-like display channel. Do
  not add custom host tools as the acceptance path.
- A D3D12 resource import, fence import, frame callback, release callback,
  dmabuf request, render-node presence, or displayed FPS number is not native
  present evidence by itself.
- If the kernel transforms a DXG packet, add kernel diagnostics proving the
  packet and return layout the host actually saw.
- Heavy Hyper-V validation runs after a whole section is implemented, not after
  every small edit. Prefer pure-C guest validators for slice-level checks.

## Current Source Map

- Hyper-V DXG/D3DKMT:
  `kernel/kernel/dev/hyperv/module.c` and `kernel/kernel/dev/hyperv/*.c`.
  This is still a unity-style root with fragment files; do not assume
  separately compiled internal APIs until the shared state and helper
  boundaries are made explicit.
- Framebuffer, DRM/KMS, GEM/TTM/dma-buf, sync, virtgpu, Nouveau, and present
  bridge:
  `kernel/kernel/dev/fb/module.c` and `kernel/kernel/dev/fb/*.c`.
  This is also still a unity-style root. Treat broad status counters and debug
  fields as diagnostics unless a validator names them as an acceptance gate.
- D3DKMT ABI:
  `kernel/kernel/inc/uabi/d3dkmthk.h`.
- Main guest validators:
  `dxgprobe`, `drmiftest`, `gpucorevalidate`, `d3d12sharedsmoke`,
  `mesaglfeature`, `mesawlegl`, WebKit GPU validators, and the Hyper-V FPS
  validator scripts.

## Current Honest State

- Hyper-V DXG transport and D3DKMT readiness exist.
- Real Mesa D3D12 on Hyper-V reaches renderer selection, real contexts,
  real HW queues, allocations, GPUVA work, submit paths, and the
  MakeResident `count=2 flags=0x1` case without the old device-removal blocker.
- D3D12 shared-resource export/open has reached the compositor protocol and
  fail-closed present-source path. It has not reached native display handoff.
- Hyper-V still reports `backend_opengl_submit 0`.
- The 480p FPS validator is intentionally strict and expected to fail until
  native D3D12 present is complete.
- WebKit acceleration is correctly gated off on Hyper-V while that contract is
  incomplete.
- The plan previously mixed active items, historical waves, and duplicate
  roll-ups. This redesign keeps one active hierarchy below.

## Active Plan

### 1. WSL2 DXG Parity

Goal: make the Hyper-V DXG path match WSL2 `dxgkrnl` where WSL is the right
reference: process lifetime, typed handles, D3DKMT packet shape, shared
resource/sync lifetime, and monitored-fence sync-file behavior.

- [x] Re-audit `hvdxg_process_state` against WSL `struct dxgprocess`:
  host-process creation/destruction, refcount lifetime, process memory
  lifetime, `tgid`/namespace behavior, retained process reuse, and final
  `DESTROYPROCESS` ordering.
  - [x] Remove cross-TGID retained host-process reuse from
    `hvdxg_process_get_current`; retained host processes can only be reused by
    the same TGID namespace, matching WSL's TGID-keyed `dxgprocess` lookup.
  - [x] Add a common pre-dispatch TGID ownership gate for D3DKMT ioctls so
    stale or wrong-process file descriptors fail before packet forwarding.
  - [x] Split process object lifetime from process memory lifetime, or add
    validated equivalent references for shared fds and async cleanup paths.
- [x] Re-audit the xv6 DXG handle table against WSL `hmgrtable`: local adapter
  handles versus normal object handles, index/unique/instance fields, destroyed
  tombstones, free-list reuse delay, typed lookup, stale-handle rejection, and
  `ignore_destroyed` callers.
  - [x] Mirror WSL handle bit layout for diagnostics: index, unique, and
    instance fields are decoded for tracked DXG objects, and local adapter
    handles now carry a nonzero unique field.
  - [x] Add WSL-style minimum-free reuse delay for per-process local adapter
    handles, with pure-C `dxgprobe --handle-lifetime-validate` coverage proving
    no immediate same-handle reuse and `min_free=128`.
  - [x] Add destroyed-entry serial diagnostics for tracked DXG objects and
    expose object/local-adapter reuse delay counters through `/dev/dxg`.
  - [x] Replace unordered linear normal-object lookup with index-addressed
    handle-table lookup keyed by WSL handle index/unique/instance fields.
  - [x] Add pure-C stale-handle matrix output for device, sync object, paging
    queue, paging-queue sync object, allocation/resource destroy, and GPUVA
    free attempts, and require matching denied-counter deltas.
  - [x] Replace the remaining growable-array approximation with a true
    free-list table for normal DXG objects, including free-count/head/tail,
    unique bump on free, chunk expansion, and explicit `ignore_destroyed`
    callers matching WSL.
  - [x] Validate stale handle rejection after delayed reuse for every object
    class: device, context, HW queue, paging queue, sync object, resource,
    allocation, and GPUVA reservation.
- [x] Re-audit `dxgprocess_adapter` equivalents: per-process adapter records,
  adapter/device list locking, multiple opens of the same adapter, and close
  behavior while child objects still exist.
- [x] Re-audit DXG object teardown order against WSL:
  HW queues, contexts, paging queues, sync objects, allocations, resources,
  GPUVA reservations, devices, adapters, and process host handles.
- [x] Compare xv6 packet structs and marshalling against WSL `dxgvmbus.c` for
  `CREATEPROCESS`, `OPENADAPTER`, `QUERYADAPTERINFO`, `CREATEDEVICE`,
  `CREATECONTEXTVIRTUAL`, `CREATEHWQUEUE`, `CREATEALLOCATION`,
  `DESTROYALLOCATION`, `MAKERESIDENT`, `OPENRESOURCE`, sync-object
  create/open/signal/wait, sync-file create/open/wait, and HW-queue submit.
  - [x] Align the obvious WSL packet-shape divergences found in the source
    audit: keep `CREATEDEVICE.cdd_device` zeroed like WSL, remove the
    non-WSL trailing dword from `MAKERESIDENT`, and require the validator to
    prove the resulting count=2 packet length.
  - [x] Send VGPU D3DKMT packets through the per-open/owner-bound host process
    handle instead of the global process fallback wherever the ioctl has an
    owner, matching WSL's `process->host_handle` forwarding model.
  - [x] Add host-destroy unwind and pure-C fault coverage for
    `CREATEDEVICE`, `CREATECONTEXTVIRTUAL`, and `CREATEHWQUEUE` failures after
    host creation but before local/user publication. `dxgprobe
    --create-publication-faults-validate` now faults the final user result
    page, proves same-process host destroy, and proves stale local destroy
    retries fail for the unpublished device/context/HW-queue/fence handles.
  - [x] Implement and validate WSL-equivalent CPU-event signal packets for
    `enqueue_cpu_event`; `SIGNALSYNCHRONIZATIONOBJECT` and
    `SIGNALSYNCHRONIZATIONOBJECTFROMGPU2` now allocate eventfd-backed host
    events, send the host event id as `cpu_event_handle`, clean up on send
    failure, and expose `sync_signal_cpu_event_matrix`,
    `sync_gpu2_cpu_event_matrix`, and `dxg_synccpuevent_signal` diagnostics.
  - [x] Add async-message parity or a same-adapter trace-backed decision for
    signal/wait/HW-queue submit paths where WSL can set `hdr.async_msg`;
    submit-command, signal-sync-object, GPU-wait, and HWQUEUE submit now use
    WSL's async send path when the host advertises it, CPU wait remains
    synchronous, and `dxg_async_message_matrix`/`dxg_async_send_last` prove
    async or explicit sync fallback.
  - [x] Reject legacy GPU waits with `object_count > 1` so the
    wire packet obeys WSL's legacy single-object rule.
  - [x] Extend packet diagnostics/validators to cover command length,
    result length, owner process, private blob order, first handles, and
    post-copyout unwind status for every path in this packet-marshalling row;
    `dxgprobe --wddm-payload-validate` now emits
    `dxg_packet_shape_matrix` over create/open/share/sync/wait/HWQUEUE/
    destroy/unwind diagnostics, and the Hyper-V DXG validator requires it.
- [x] Re-audit WSL `CREATEALLOCATION` and `OPENRESOURCE` failure unwind:
  runtime/resource/allocation private blob copyout, local handle publication,
  host resource destruction after late failure, standard-allocation substitution,
  and result-private-data return layout.
  - [x] Send `DESTROYALLOCATION` late-failure cleanup through the same
    per-open DXG process handle used for the corresponding `CREATEALLOCATION`
    or `OPENRESOURCE` packet, with existing destroy-allocation diagnostics
    recording the process handle sent to the host.
  - [x] Make create/open local tracking failures visible to callers instead of
    best-effort drops: resource/allocation publication now returns errors,
    unwinds partial local state, and destroys host-created resources on failure.
  - [x] Add a pure-C fault-injection validator that forces post-host
    `CREATEALLOCATION` and `OPENRESOURCE` publication failures, then proves
    same-process host cleanup, original errno preservation, no leaked local
    handles, and balanced pinned sysmem pages.
  - [x] Split shared-resource metadata into explicit WSL-like
    resource/allocation records so seal/query/open lifetimes are not stored
    only as flat blobs on `hvdxg_tracked_resource`.
- [x] Re-audit NT fd publication against WSL `dxgkio_share_objects()`:
  `object_count == 1`, anon-inode kind, `O_CLOEXEC`, copyout-before-install,
  cleanup of unused fd/file references on failure, and wrong-kind rejection.
  - [x] Keep `LX_DXSHAREOBJECTS` single-object only and publish custom NT
    shared-resource/sync fds with `FD_CLOEXEC`; the shared-resource C
    validator now fails unless the returned fd reports close-on-exec.
  - [x] Add explicit copyout-failure fault injection for `shared_handle` so
    cleanup of the fd/file reference and NT shared-object ref is proven without
    relying on ordinary close paths.
  - [x] Add a wrong-kind matrix that exports resource and sync NT fds, then
    proves resource-open rejects sync fds and sync-open rejects resource fds.
- [x] Re-audit WSL `CREATESYNCFILE`, `OPENSYNCOBJECTFROMSYNCFILE`, and
  `WAITSYNCFILE`: monitored-fence `dma_fence` creation, host event
  registration, CPU wait submission, temporary local sync-object lifetime,
  GPU wait submission, and unwind on copyout or fd failure.
  - [x] Keep the current xv6 custom sync-file fd honest as a WSL-style DXG
    sync-file parity skeleton, while documenting that it is not yet Linux
    `sync_file`/`dma_fence`.
  - [x] Add source diagnostics for sync-file fd live/release counts,
    host-event active/allocation/removal counts, create-copyout unwind, and
    open-copyout unwind.
  - [x] Destroy the host-opened sync object if
    `OPENSYNCOBJECTFROMSYNCFILE` succeeds on the host but user copyout fails,
    and prove that no local sync handle was published.
  - [x] Add pure-C sync-file validators covering create-copyout fd/event
    cleanup, wait temporary sync-object destruction, open-copyout cleanup, and
    child-process open from the same sync-file fd.
- [x] Keep same-adapter WSL trace replay current for the NVIDIA/Hyper-V test
  adapter whenever the driver store, UMD payload sizes, or D3DKMT packet
  shaping changes. `dxgprobe --wsl-trace-replay` now emits
  `wsl_trace_replay_packet_matrix` rows for the replayed packet sequence and a
  `wsl_trace_replay_signature` tied to the selected OPENADAPTER LUID and the
  current NVIDIA WSL trace reference; the focused core runner requires those
  rows before accepting the WSL replay segment.
- [x] Finish direct D3D12 fence sharing parity: decide whether the final native
  path requires direct `ID3D12Device::OpenSharedHandle(fence)` success or only
  WSL-style DXG sync-file acquire, then validate the chosen behavior against
  same-adapter WSL traces.
  The native Wayland/D3D12 contract now selects WSL-style DXG sync-file acquire
  as the required fence handoff. `d3d12sharedsmoke` emits
  `d3d12_fence_sharing_policy_matrix` and
  `d3d12_fence_sharing_validation_matrix` with direct D3D12 fence fd import
  unused, the same-adapter NVIDIA WSL trace recorded as the parity source, and
  no native-present/OpenGL-submit credit.
- [x] Preserve WSL-style shared resource semantics as regression coverage:
  one-time seal, stable runtime/resource/allocation metadata, repeated
  query/open, exporter-destroy survival, child open, wrong-kind rejection, and
  NT fd close separate from explicit D3DKMT destroy. The core runner now
  consumes `shared_resource_seal_provenance_matrix`,
  `shared_mutation_rejection_matrix`, `shared_lifetime_record_matrix`,
  `resource_import_negative_matrix`, `ntshare_object_kind_matrix`, and
  `dxg_sharedfd_close` diagnostics.
- [x] Preserve WSL-style shared sync semantics as regression coverage:
  NT fd export/open, stale-close rejection, process namespace separation,
  monitored-fence target values, sync-file import/export, and cleanup ordering.
  The focused runner requires `opensync_layout_source_matrix`,
  `sync_import_negative_matrix`, `sync_file_matrix`,
  `dxg_syncfile_*_unwind_matrix`, `dxg_syncfile_lifetime`, and the
  `dxg_opensync_*` packet diagnostics.

### 2. Linux DRM/GEM/TTM/KMS Interfaces

Goal: keep the Linux-facing GPU driver surface honest enough for Mesa, libdrm,
Wayland, and Nouveau without claiming native Hyper-V present prematurely.

- [x] Replace diagnostic-only `dma_fence` pieces with a real fence lifetime
  model across GEM, PRIME/dma-buf, KMS, syncobj, timelines, poll, callback
  removal, and final object release.
  The kernel uses the refcounted `fb_gpu_fence` backing object for fence fd
  export/query/wait, syncobj sync-file import/export, KMS OUT_FENCE, poll
  callback fire/remove/late accounting, and final release. `gpucorevalidate`
  now emits `drm_dma_fence_lifetime_contract_matrix`, and the Hyper-V core
  runner requires it while granting no native-present/OpenGL-submit credit.
  - [x] Make DRM syncobj timeline transfer copy pending source state instead
    of requiring the source point to be signaled first. The pure-C DRM matrix
    now requires `syncobj_pending_transfer_matrix` plus transfer wakeup
    provenance, with no native-present or OpenGL-submit credit.
- [x] Replace the global-lock approximation with Linux-shaped `dma_resv` and
  ww-mutex rules throughout TTM validation, eviction, migration, PRIME export,
  KMS prepare/cleanup, and teardown.
  The TTM reservation path now has a `ww_acquire_ctx`-shaped validation lane
  for ordered two-object reservation, reversed-order retry/backoff, balanced
  release, and multi-object accounting while retaining the existing shared and
  exclusive fence propagation used by PRIME/dma-buf, KMS pin/unpin, syncobj,
  sync-file, eviction, and teardown. The focused Hyper-V core validator passed
  on 2026-05-24 with `validation_run_id=core-1779668794-2747803`, including
  `ttm_dma_resv_ww_mutex_matrix ... max_acquired=2
  validate_failures_delta=0 native_accel_credit_delta=0 status=PASS`.
- [x] Implement real KMS atomic `IN_FENCE_FD` and `OUT_FENCE_PTR` behavior with
  display-correlated completion, not immediate software completion.
  Atomic commits now validate and balance `IN_FENCE_FD` refs, reject stale,
  duplicate, future, and nonblocking fence paths, export an `OUT_FENCE_PTR`
  fd, and signal it from the recorded display-completion sequence rather than
  the old software-scanout provenance counter. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=whole-section
  CORE_C_SECTIONS='preflight drm final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779670401-2842329`, including
  `atomic_fence_matrix atomic_fence_kernel=real`,
  `atomic_out_fence_provenance_matrix out_fence_source=display_completion
  ... out_fence_display_correlated=1
  out_fence_software_scanout_correlated=0`, and zero
  native-present/OpenGL-submit credit.
  - [x] Keep the atomic OUT_FENCE provenance honest: the successful commit
    now increments `kms_atomic_out_fence_display_correlated` only after the
    framebuffer present path records display completion, while the
    `kms_atomic_out_fence_software_scanout_correlated` fallback counter stays
    zero in the validator and still grants no native-present/OpenGL-submit
    credit.
- [x] Keep the current KMS modifier path self-consistent while scanout remains
  XRGB/ARGB-only: `DRM_CAP_ADDFB2_MODIFIERS` advertises the accepted linear
  metadata contract, `ADDFB2`/`GETFB2` round-trip linear NV12 metadata, and
  non-linear, mixed-plane, and unsupported present paths fail before side
  effects or native/OpenGL-submit credit. The primary plane now also exposes a
  Linux-style immutable `IN_FORMATS` blob for the actual scanout subset only:
  XRGB8888/ARGB8888 with `DRM_FORMAT_MOD_LINEAR`, while NV12 and non-linear
  modifiers remain excluded from scanout credit. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight drm final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779671849-2925678`, including
  `kms_in_formats_blob_matrix ... xrgb8888_linear=1 argb8888_linear=1
  nv12_scanout=0 nonlinear_modifiers=0 native_present_credit=0
  opengl_submit_credit=0 status=PASS`.
- [ ] Extend actual primary-plane scanout formats/modifiers beyond XRGB/ARGB
  only when the primary plane can present them without the software fallback.
- [x] Keep vblank/page-flip display-correlation separate from native-present
  credit until the same frame also proves native D3D12 display completion.
  `fbstat` now emits `kms_vblank_native_present_separation_matrix`, the
  pure-C core validator requires it, and the Hyper-V core runner checks that
  vblank samples/page flips can be display-correlated while still granting
  zero native-present/OpenGL-submit credit.
- [x] Keep DRM leases, user blobs, legacy ioctls, render-node lifecycle, and
  event queues covered by focused validators after any DRM refactor.
- [x] Separate generic scanout/DRM diagnostics from D3D12-specific alignment,
  DXG-present state, WebKit harness tracing, and ioctl-name tracing so debug
  helpers do not become accidental capability gates.
  Generic GPU/DRM ioctl tracing now uses `fb-gpu-trace` instead of
  WebKit-specific labels. `fbstat` emits
  `gpu_diagnostics_separation_matrix`, and the focused core runner requires
  generic DRM/KMS/fb diagnostics to stay separate from DXG-present and WebKit
  policy evidence while granting zero native-present/OpenGL-submit credit.

### 3. PCI Runtime And Nouveau

Goal: support Nouveau through Linux-shaped PCI/runtime/DRM interfaces, while
being explicit when the current Hyper-V GPU-P environment is not DDA hardware.

- [ ] Extend PCI runtime support beyond probe scaffolding: DMA mask/coherency
  ownership, MSI/MSI-X setup, legacy IRQ fallback policy, interrupt delivery,
  resource claim/release, runtime PM suspend/resume usage, and remove-path
  validation.
- [x] Keep GPU-P-only Hyper-V images fail-closed for native Nouveau: no fake
  BAR, VRAM, IRQ, command submission, native-present, or OpenGL-submit credit.
  The focused core runner now requires `nouveau_gpup_failclosed_matrix` from
  `fbstat` whenever no DDA/Nouveau PCI function is accepted.
- [x] Replace synthetic Nouveau `GETPARAM` answers with DDA-sourced
  chipset/class, BAR, VRAM/GART, engine, and firmware facts when DDA hardware
  is present. Hardware-looking answers now come from the accepted DDA PCI
  device/resource apertures or fail closed; local answers such as
  `HAS_BO_USAGE`, `HAS_PAGEFLIP`, `EXEC_PUSH_MAX`, and `HAS_VMA_TILEMODE`
  are tracked separately as driver capabilities, and validators reject any
  remaining synthetic hardware facts.
- [x] Implement Nouveau channel/FIFO/GROBJ/notifier allocation semantics rather
  than no-op channel handles.
  Legacy channel allocation now creates per-open channel state, tracks
  notifier and GROBJ objects, rejects duplicate/unsupported objects, frees
  objects explicitly through `GPUOBJ_FREE`, and reclaims leaked objects on fd
  close. `drmiftest` emits `nouveau_channel_object_matrix` for this contract;
  the separate `DRM_NOUVEAU_NVIF` row below remains open for Mesa's newer
  subchannel object path.
- [x] Implement `DRM_NOUVEAU_NVIF` far enough for the Mesa Nouveau winsys path,
  or add precise fail-closed validation for each unsupported NVIF class.
  xv6 now parses NVIF v0 headers for SCLASS, NEW, DEL, method/register/map,
  and notification operations. It advertises no NVIF classes until a real
  class hierarchy exists, rejects NEW for would-be engine classes, rejects
  methods/register/map/notify operations explicitly, and validates this with
  `nouveau_nvif_failclosed_matrix`.
- [x] Implement non-empty Nouveau command submission on DDA hardware, or
  return source-audited fail-closed tokens that Mesa/Nouveau validators check.
  Until a real DDA command engine exists, non-empty GEM pushbuf, EXEC, and
  VM_BIND paths reject after validating their user buffers and object
  references. Dedicated counters and `nouveau_submit_failclosed_matrix` prove
  the no-op/fence-only paths are separate from non-empty command rejection and
  that no native-present/OpenGL-submit credit is granted.
- [x] Add a Mesa Nouveau smoke that reaches winsys/device-info on the intended
  hardware and cannot pass on synthetic GPU-P-only answers.
  `nouveauabitest` is the Mesa/libdrm smoke gate. On DDA it must open the
  Nouveau device and reach libdrm winsys/device-info, channel, BO, map, and
  PRIME paths; on GPU-P-only images it can only pass as explicit no-DDA
  fail-closed evidence with `synthetic_gpup_rejected=PASS`.

### 4. Native D3D12 Shared-Resource Present

Goal: turn the current fail-closed D3D12 shared-resource lane into a real
non-readback display handoff.

- [x] Select one final display handoff lane and document why it matches a
  working model: WSLg-like display channel, GPU-P/DDA resource-to-scanout
  binding, or trace-proven synthvid/VRAM bridge.
  The selected lane is GPU-P/DDA `dxg-resource-scanout-bind`: a source-owned
  D3D12 resource, allocation, adapter LUID, dimensions, format/modifier, and
  sync-file/fence target must bind to a real display completion source. WSLg's
  display channel is not exposed by xv6/WSL `dxgkrnl`, synthvid is GPA
  dirty-rectangle VRAM rather than a D3D12 resource bind, DDA Nouveau is a
  separate PCI path, and no custom host tool is allowed. `fbstat` now emits
  `dxg_present_lane_selection_matrix` for this selection while keeping native
  present and OpenGL-submit credit at zero.
- [ ] Implement the selected `dxg-resource-scanout-bind` equivalent without
  custom host tooling.
- [ ] Make the D3D12 Wayland resource-buffer path pass on the current Hyper-V
  runtime: same adapter LUID, shared resource fd, acquire fence or sync-file,
  compositor import/open, and present admission.
- [ ] Implement GPU-side composite/copy/present from imported D3D12 resources
  to the chosen display destination without CPU map/readback or DRI software
  present.
- [ ] Produce nonzero present ids and native completion counters for the same
  submitted resource and generation.
- [ ] Send frame callbacks and buffer releases only after native completion for
  that same frame.
- [ ] Add per-client, per-resource, per-generation native-present counters so
  one client's progress cannot satisfy another client's validator.
- [x] Add the source-level skeleton for the narrow DXG/Hyper-V to KMS/scanout
  interface: `FB_GPU_DXG_PRESENT_BIND_CONTRACT_QUERY` now reports a registered
  present-source-owned resource, source/resource generations, required metadata,
  the selected GPU-P/DDA bind lane, and a required display-completion source
  while remaining fail-closed. Build evidence:
  `cmake --build /tmp/xv6-hyperv-build --target kernel user -j2` passed on
  2026-05-24.
- [x] Validate the bind-contract skeleton in the guest pure-C path:
  `dxgprobe --present-source-failclosed` must prove the contract is tied to the
  registered source, returns no present id/completion, reports display
  completion as required, rejects stale or foreign source handles, and grants no
  native-present/OpenGL-submit credit. The focused runner now includes this as
  the `dxg-present-source` step in `scripts/hyperv-gpu-core-validate.sh`.
  Evidence: `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=whole-section
  CORE_C_SECTIONS='preflight drm dxg-share dxg-sync present-source buffers
  final' scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779667968-2704585`, including
  `present_bind_contract_skeleton_matrix`, stale/foreign source rejection,
  wait-sync metadata rejection, and zero native-present/OpenGL-submit credit.
- [ ] Replace the fail-closed bind-contract skeleton with the selected real
  display-bindable resource plus completion source, instead of letting FB code
  infer native-present capability from raw `/dev/dxg` status fields.
- [x] Prove framebuffer blit, CPU map/readback, DRI software present,
  copy-export fallback, and callback-only/release-only paths are rejected by
  the same validator. Evidence: `BUILD_DIR=/tmp/xv6-hyperv-build
  CORE_C_MODE=whole-section CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779669683-2798390`, including
  `present_source_software_path_rejection_matrix` with framebuffer blit,
  CPU map/readback, DRI software present, copy-export fallback,
  callback-only, and release-only all rejected, while keeping
  `native_present_claim=0` and `opengl_submit_credit=0`.
- [ ] Keep `backend_opengl_submit 0` until this section and the FPS section
  both pass.

### 5. Validation And Performance

Goal: accept only current-run, source-correlated, finite validation evidence.

- [x] Add source-contained pure-C D3D12 shared-resource validators for the
  current fail-closed present-source contract. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779672997-2972287`, including
  `d3d12_shared_resource_fd_lifetime_matrix`,
  `d3d12_present_source_admission_matrix`,
  `d3d12_acquire_fence_lifetime_matrix`, and
  `d3d12_present_bind_contract_failclosed_matrix`. These prove live fd
  registration, invalid/unverified/stale fd rejection, same-adapter admission,
  D3DKMT handle metadata, monitored-fence acquire metadata, owner cleanup, and
  zero native-present/OpenGL-submit credit.
- [ ] Add native D3D12 completion validators once the real display-bind lane
  exists: nonzero present ids, completion counters, close-before-signal
  cancellation, frame callback/release ordering, and cleanup balance after
  native completion.
- [ ] Keep WSL-trace replay equivalence current for the real UMD sequence and
  fail if xv6 rewrites packets without matching host-saw diagnostics.
- [x] Build and validate completed sections with `/tmp/xv6-hyperv-build` and
  focused 6-vCPU Hyper-V images before marking section items done.
  Evidence: the focused Hyper-V core validator rebuilt kernel/rootfs, deployed
  a 6-vCPU image, and passed `preflight`, `drm`, `dxg-share`, `dxg-sync`,
  `present-source`, `buffers`, and `final` pure-C sections on 2026-05-24
  (`validation_run_id=core-1779667968-2704585`). Native-present, FPS, and
  WebKit artifacts remain separate unchecked gates.
- [ ] Make the finite 480p desktop 3D validator pass only on native D3D12
  presented frames after warmup.
- [x] Require full 640x480 or equivalent 480p rendering with `render_div == 1`.
  The finite FPS validator already rejects non-480p or divided renders; its
  final pass token now records `window=640x480 render=640x480 render_div=1`,
  and WebKit's prior-FPS contract requires that exact token before acceleration
  can consume the FPS artifact.
- [ ] Confirm the demo is visible, closeable, and resizable during the passing
  run.
- [ ] Require visible content progress outside title/FPS overlay areas and
  correlate content hashes or thumbnail deltas with native present completions.
- [x] Preserve a negative artifact where inflated displayed/demo FPS, including
  the observed around-40 FPS case, fails without native completion and visible
  content progress.
  `hyperv-3d-fps-validate.sh` now runs the 40-FPS anti-inflation negative
  preflight by default before heavy VM sampling, covering stale run ids, static
  content CRC/frame evidence, frozen-window evidence, and positive matching
  visible/native cadence. `mesawlegl` now writes `/tmp/mesawlegl-fps` with
  `mesawlegl_fps_context_only_matrix` while app-draw FPS lacks matching
  D3D12 run id, client pid, nonzero present/completion counters, and native
  requirements; the validator emits `fps_overlay_inflation_rejection_matrix`
  and rejects those context-only samples before any FPS pass.
- [ ] Enable `FB_GPU_BACKEND_F_OPENGL_SUBMIT` on Hyper-V only after native
  present and the finite FPS validator pass.
- [ ] Re-check KVM/virgl after the Hyper-V backend flag changes so the control
  backend still reports the existing OpenGL-submit contract.

### 6. WebKit Consumer Contract

Goal: WebKit acceleration must consume the exact same contract as native Mesa
clients; it cannot be enabled from render-node presence or dmabuf requests
alone.

- [x] Keep Hyper-V WebKit acceleration gated off while any earlier active-plan
  section remains open.
  The WebKit launcher still requires `FB_GPU_BACKEND_F_OPENGL_SUBMIT` plus a
  validated D3D12 shared-surface/native-present contract before selecting the
  D3D12 environment. While earlier sections remain open, Hyper-V stays on the
  stable software WebKit path and `hyperv-webkit-gpu-validate.sh` rejects
  `effective_accel=1` or stale contract evidence.
- [ ] Route WebKitGTK through the same Mesa D3D12 render-node path, Wayland
  D3D12 shared-resource protocol, monitored-fence/sync-file acquire path,
  adapter-LUID validation, native present path, and backend flag as native
  Mesa clients.
- [x] Add WebKit run-id and current-run evidence matching so stale
  `/tmp/wlcomp-d3d12-present`, stale FPS logs, or another client's counters
  cannot satisfy the WebKit gate.
  `wlcomp_launcher` now passes the generated WebKit run id into the D3D12
  evidence admission check, requires both `d3d12_run_id` and
  `d3d12_present_identity_compositor_run_id` to match before the D3D12 WebKit
  environment can be selected, and emits `webkit_gpu_contract_matrix` for the
  run-id/same-adapter/native-present decision.
- [ ] Add an animated WebKit content fixture and correlate content CRC/frame
  hash progress with native-present completions for the same client/resource
  generation.
- [x] Reject WebKit acceleration evidence based only on chrome/cursor/title
  updates, callbacks, releases, render-node presence, dmabuf request,
  environment variables, or software fallback.
  `hyperv-webkit-gpu-validate.sh` now runs a default policy-negative preflight
  that emits `webkit_evidence_rejection_matrix` and proves those evidence
  classes are rejected before any WebKit acceleration artifact is accepted.
- [ ] Produce one enabled WebKit artifact only after native present, finite
  480p FPS, backend flag, and shared-surface contract all pass.

## Section Validation Rhythm

For each active section:

1. Implement the whole section or a clearly bounded subsection.
2. Run build-only checks first.
3. Run focused pure-C validators for that section.
4. Run heavy Hyper-V GUI/FPS/WebKit validation only after the section's code is
   complete enough for that validation to be meaningful.
5. Mark checklist items only when source, runtime evidence, and negative cases
   all agree.

## Acceptance Gate

Hyper-V GPU/OpenGL support is complete only when all of these are true:

- `mesaglfeature` passes on Hyper-V D3D12 without tracing or device removal.
- A Mesa Wayland client presents a D3D12-rendered surface through a shared GPU
  resource/fence path, not DRI software readback.
- The desktop-launched 640x480 or equivalent 480p 3D demo is visible,
  closeable, resizable, and sustains more than 60 FPS after warmup.
- `fbstat` honestly reports `backend_opengl_submit 1` on Hyper-V.
- WebKit acceleration uses the same validated shared-surface/OpenGL-submit
  contract and stays gated off when that contract is unavailable.
