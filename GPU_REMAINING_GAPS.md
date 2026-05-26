# GPU Remaining Gaps

Last updated: 2026-05-26

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

### Current Dependency Graph

The remaining unchecked work is intentionally ordered around one root
dependency:

1. `dxg-resource-scanout-bind` or an equivalent GPU-P/DDA display-bind
   transport must produce a nonzero present id and display-completion counter
   for the same D3D12 resource generation.
2. The compositor may then unblock callbacks/releases and grant visible content
   and FPS credit only for that completed resource generation.
3. Hyper-V may advertise `FB_GPU_BACKEND_F_OPENGL_SUBMIT` only after the native
   present path and finite 480p FPS validator pass.
4. WebKit acceleration may turn on only after it consumes that exact same
   shared-resource, sync-file, native-present, FPS, and backend-flag contract.

Until item 1 exists, later validators should be strict, source-correlated, and
fail-closed; they should not be reworded into "done" by accepting import-only,
callback-only, title-only, or readback evidence.

### Holistic Work Breakdown

This is the active skeleton for the remaining work. Later sections keep the
detailed source/evidence history, but new implementation should move through
these chunks in order:

1. **Host Display-Bind Source**
   - Find or prove absent a non-custom GPU-P/DXG, WSLg-like, or DDA bridge
     that can bind a guest D3D12 resource/allocation/fence to host scanout.
   - If a source exists, document its sender packet, return layout,
     completion payload, resource-generation identity, and same-adapter
     requirements against WSL/Linux source or same-adapter traces.
   - If the source is absent, keep the provider fail-closed and keep the
     missing-host-ABI matrix as the checked result for this chunk; do not turn
     synthvid, present-history enum names, or DDA PCI display presence into
     native-present credit.
2. **Kernel Provider Replacement**
   - Replace `hyperv_dxg_display_bind_submit()` only after chunk 1 has a
     real sender/completion contract.
   - Preserve the existing source/resource-generation accept gate, typed
     DXG fd pinning, sync-file acquire metadata, and zero-id stale-source
     cleanup.
   - Add kernel-side host-saw diagnostics for every display-bind packet or
     completion payload the provider sends or consumes.
3. **Compositor Native Handoff**
   - Unblock D3D12 resource-buffer callbacks/releases only for nonzero
     display-bind present/completed ids from the same current run, source, and
     resource generation.
   - Populate compositor-owned visible content CRC/frame/hash fields from
     the native handoff, not from app-loop telemetry or title/FPS overlays.
4. **Finite 480p FPS**
   - Run the 480p demo through the native handoff after warmup, with
     visible, closeable, and resizable lifecycle evidence.
   - Require effective FPS to be clamped by native completion cadence and
     compositor-owned content cadence; the around-40 inflated/readback case
     must remain a negative artifact.
5. **Backend Flag and Controls**
   - Set Hyper-V `FB_GPU_BACKEND_F_OPENGL_SUBMIT` only after chunks 1-4
     pass in one current validation lineage.
   - Re-run the KVM/virgl control path to prove it remains the permitted
     OpenGL-submit backend while Hyper-V changes are isolated.
6. **WebKit Contract**
   - Route WebKitGTK through the same Mesa D3D12 shared-resource,
     sync-file, native-present, FPS, and backend-flag contract.
   - Produce the single enabled WebKit artifact only after the same current
     validation lineage proves native present, finite 480p FPS, backend flag,
     and compositor-owned content identity.
7. **Whole-Plan Dependency Skeleton**
   - Keep the root display-bind dependency, native-completion validators,
     finite 480p gate, demo interaction gate, backend OpenGL-submit gate,
     KVM/virgl recheck, WebKit route, WebKit content, and final WebKit artifact
     in one current-run dependency row so later sections cannot open from
     isolated partial evidence.
  - `dxgprobe` and `gpucorevalidate` now emit
     `gpu_remaining_plan_dependency_skeleton_matrix`, requiring the WSL
     display-bind ioctl and in-band present-history completion handler to be
     absent, GPU-P sender/completion contracts to be zero, DDA/Nouveau import,
     scanout-bind, and hardware-flip completion to be absent, and all native
     present/OpenGL/WebKit credit to remain zero while the root display-bind
     gate is closed. The focused core runner requires this row before any
     later native-present/FPS/WebKit evidence can be trusted.
   - The skeleton is backed by source-audited negative-proof rows:
     `wsl_dxg_uapi_namespace_negative_matrix`,
     `wsl_dxg_adapter_display_caps_negative_matrix`, and
     `dda_nouveau_non_readback_display_proof_matrix`. The pure-C validators
     now also require `wsl_submit_present_fields_not_bind_matrix`,
     `wsl_stdalloc_and_alloc_flags_not_bind_matrix`,
     `wsl_trace_display_bind_negative_matrix`, and
     `provider_credit_gate_negative_matrix`. The current skeleton also
     requires `host_display_bind_source_catalog_matrix`, so WSL submit
     metadata, present-history fields, written primaries, standard-allocation
     private data, allocation flags, trace-visible resource sharing, sync-file
     plumbing, provider invocation counters, WSLg-channel absence, synthvid
     GPA-dirty evidence, and DDA/Nouveau display separation cannot become
     native-present credit without a real host ABI, sender, and
     display-completion contract.
     These follow the WSL `dxgkrnl` UAPI/display-cap clearing and Linux
     Nouveau DRM/KMS shape: WSL enum, adapter, standard-allocation,
     submit/present, trace replay, and present-history telemetry fields are
     not a display-bind ABI; DDA PCI BAR/DMA/IRQ acceptance is not
     non-readback display proof until Nouveau display creation, non-virtual
     connectors, hardware vblank IRQs, and hardware page-flip completions all
     correlate through the KMS `NOUVEAU_HW` lane.

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
  - [x] Move `LX_DXOPENRESOURCEFROMNTHANDLE` local resource/allocation graph
    publication before any user handle copyout, matching WSL's
    assign-before-expose ordering. If local tracking or a late copyout fails,
    xv6 now unwinds the tracked resource/allocation state and destroys the
    host-opened resource through the same owner-bound process handle before any
    stale handle can be considered published.
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
  - [x] Make sync-object tracking failures first-class and apply WSL-style
    track-before-copyout ordering to sync open paths. `hvdxg_track_sync()` now
    returns errors, `OPENSYNCOBJECTFROMNTHANDLE2` and
    `OPENSYNCOBJECTFROMSYNCFILE` track the opened sync object before copying
    the handle back to userspace, and failures unwind both the local handle
    table entry and host-opened sync object.
  - [x] Extend WSL assign-before-expose ordering to the host-create paths:
    `CREATEDEVICE`, `CREATECONTEXTVIRTUAL`, `CREATEALLOCATION`,
    `CREATESYNCOBJECT`, `CREATEPAGINGQUEUE`, and `CREATEHWQUEUE` now commit
    local dxgprocess/object-table state before copying newly created handles
    to userspace, and late failures untrack local state plus destroy the
    host-created object where a host destroy command exists.
  - [x] Add WSL-like monitored-fence stop semantics for tracked sync objects:
    tracked sync rows now record fence map size and owning VM, kernel fence
    aliases are PFNMAP mappings, `hvdxg_untrack_sync()` unmaps both user and
    kernel fence aliases, and `dxg_fence_map` exposes unmap attempt/success/
    failure diagnostics.
  - [x] Add pure-C sync-file validators covering create-copyout fd/event
    cleanup, wait temporary sync-object destruction, open-copyout cleanup, and
    child-process open from the same sync-file fd.
- [x] Replace the by-value shared-resource fd clone with a WSL-style parent
  shared-resource object:
  - [x] Add parent-resource scaffold diagnostics while preserving existing
    clone semantics: resource fds now carry parent id, fd refs, parent refs,
    opened-child counters, creator/opened child snapshots, and
    `dxg_sharedresource_parent` status output.
  - [x] Add a refcounted parent resource object with fd refs, host NT refs,
    sealed generation, private-data ownership, allocation metadata, and an
    opened-resource list.
  - [x] Link creator and opener `hvdxg_tracked_resource` children to that
    parent instead of deep-cloning fd-private resource state.
  - [x] Move seal/query/open metadata reads to the parent and keep per-open
    children responsible only for process-local handles and cleanup.
  - [x] Thread parent id/ref/opened-child evidence through display-bind pin
    snapshots, fail-closed provider validation, `FB_GPU_GET_STATS`, and the
    pure-C display-bind diagnostics.
  - [x] Update display-bind snapshots to pin the real parent object and
    validate the matching opened child/resource generation before accepting
    native-present credit.
  - [x] Preserve the WSL shared-resource NT/global-share metadata on the
    parent resource and every opened child. The display-bind pin path now
    rejects with explicit `dxg_display_bind_pin_diag` fields if the typed fd,
    opened child, parent id, global share, or sealed generation diverge, so a
    future sleepable display-bind provider cannot silently pin a by-value clone
    or a child stripped of its parent sharing state.
- [x] Finish WSL sealed-allocation metadata parity:
  - [x] Add sealed allocation `num_pages` and `cached` metadata to shared
    allocation records.
  - [x] Populate the metadata from create/open allocation state and expose it
    in shared-resource model diagnostics.
  - [x] Preserve sealed allocation metadata across query/open/exporter-destroy
    lifetimes on the real WSL-style parent object.
  - [x] Add pure-C validators for duplicate fd/open-child close ordering,
    sealed `num_pages`/`cached` stability, and parent/child ref balance.
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
- [x] Promote WSL lifetime parity rows into the top-level Hyper-V DXG
  validator after the display-bind metadata boundary. `hyperv-dxg-validate.sh`
  now runs and requires process/object handle lifetime rows, shared-resource
  parent lifetime and sealed-allocation metadata rows, sync-file create/open
  unwind rows, and `OPENRESOURCE` parent rollback fields
  (`parent_same`, `parent_refs_balanced`, `parent_child_unlinked`, and
  `sealed_generation_coherent`) while keeping all native-present/OpenGL-submit
  credit gated.
  Evidence: focused Hyper-V C validators on the 6-vCPU image passed
  `shared_resource_parent_lifetime_matrix` with `fd_refs=1/1/0`,
  `shared_resource_seal_provenance_matrix ... no_present_credit=1 status=PASS`,
  `dxg_syncfile_create_unwind_matrix ... status=PASS`,
  `dxg_syncfile_open_unwind_matrix ... status=PASS`, and the process/handle
  lifetime rows required by the runner.

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
- [x] Extend actual primary-plane scanout formats/modifiers beyond XRGB/ARGB
  only when the primary plane can present them without the software fallback.
  The primary plane now advertises XRGB8888, ARGB8888, XBGR8888, and ABGR8888
  linear through the shared GETPLANE/IN_FORMATS table. XBGR/ABGR scanout uses
  an explicit CPU R/B conversion in the framebuffer blit path, while NV12
  remains metadata-only and fail-closed for primary scanout/direct FB_ID
  property changes. Evidence: full focused Hyper-V core validation passed on
  2026-05-24 with `validation_run_id=core-1779706363-461479`, including
  `kms_primary_scanout_format_mod_matrix`,
  `kms_primary_scanout_actual_format_matrix`, and
  `kms_present_completion_failclosed_matrix`.
  - [x] Keep GBM scanout support aligned with the KMS primary-plane contract:
    `xv6-gbm` now rejects `GBM_BO_USE_SCANOUT` for `GBM_FORMAT_NV12`, while
    leaving NV12 available for metadata/import-render paths.
- [x] Keep vblank/page-flip display-correlation separate from native-present
  credit until the same frame also proves native D3D12 display completion.
  `fbstat` now emits `kms_vblank_native_present_separation_matrix`, the
  pure-C core validator requires it, and the Hyper-V core runner checks that
  vblank samples/page flips can be display-correlated while still granting
  zero native-present/OpenGL-submit credit.
  - [x] Add explicit page-flip/vblank provenance counters so software display
    completion is not mistaken for native hardware: validators now require
    `page_flip_events_native_hw=0` and `vblank_source_native_hw=0` while the
    current KMS path remains software/fail-closed.
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
  - [x] Stop reporting generic `display_last_complete` as
    `d3d12_native_present_credit`. The matrix now prints
    `generic_display_last_complete` separately and keeps
    `d3d12_native_present_credit=0`.
- [x] Add Linux-shaped fail-closed mismatch rows for KMS/Nouveau/TTM/GEM/GPUVM
  interfaces that are not native DDA hardware yet.
  `fbstat`, `gpucorevalidate`, and the focused runner now require diagnostic
  rows for Nouveau KMS registration, Nouveau vblank IRQ source, primary-plane
  modifier policy, CPU-converted scanout separation, GEM framebuffer plane refs,
  atomic plane-state and prepare/cleanup lifecycle, page-flip feature gates,
  TTM real-move backend, Nouveau GEM mmap backing, Nouveau GPUVM mapping, and
  DXG sync-file admission versus KMS completion. These rows are explicitly
  zero-credit and fail closed until real DDA/Nouveau display, TTM, and engine
  evidence exists.

### 3. PCI Runtime And Nouveau

Goal: support Nouveau through Linux-shaped PCI/runtime/DRM interfaces, while
being explicit when the current Hyper-V GPU-P environment is not DDA hardware.

- [x] Extend PCI runtime support beyond probe scaffolding: DMA mask/coherency
  ownership, MSI/MSI-X setup, legacy IRQ fallback policy, interrupt delivery,
  resource claim/release, runtime PM suspend/resume usage, and remove-path
  validation.
  The PCI core now has Linux-shaped wrappers for DMA masks, BAR mmap,
  MSI/MSI-X, IRQ request/free, and runtime PM; Nouveau uses that surface while
  GPU-P-only Hyper-V remains fail-closed with no fake BAR/DMA/IRQ/native-present
  or OpenGL-submit credit. Evidence: `BUILD_DIR=/tmp/xv6-hyperv-build
  CORE_C_MODE=sections CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-25 with
  `validation_run_id=core-1779714735-891533`.
- [x] Add explicit GPU-P-only PCI/Nouveau runtime contract diagnostics.
  `fbstat` and `gpucorevalidate` now emit
  `nouveau_pci_runtime_contract_matrix`, which records DMA/coherent masks,
  BAR claims, MSI/MSI-X setup, legacy IRQ fallback, IRQ handler/delivery,
  runtime PM, remove-path state, and zero native-present/OpenGL-submit credit.
  On GPU-P-only Hyper-V this row must pass with no fabricated BAR/DMA/IRQ
  state; accepted DDA hardware remains diagnostic until real IRQ/runtime-PM and
  native engine evidence exist. Validated on 2026-05-24 with
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight final' scripts/hyperv-gpu-core-validate.sh`,
  `validation_run_id=core-1779674606-3042655`.
- [x] Split the PCI/Nouveau runtime TODO into a fail-closed interface matrix
  that mirrors the Linux driver layers without fabricating GPU-P hardware.
  `fbstat`, `gpucorevalidate`, and the focused runner now require
  `nouveau_pci_runtime_interface_matrix` with resource-tree ownership,
  DMA-mapping API, MSI/MSI-X programming, legacy IRQ fallback, IRQ delivery,
  runtime PM, remove/hot-remove, native engine, native-present credit, and
  OpenGL-submit credit fields. On GPU-P-only Hyper-V this must pass as
  `GPU_P_FAIL_CLOSED`/deferred/absent; a real DDA/Nouveau function remains
  diagnostic until hardware proves those interfaces. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779679708-3257998`.
- [x] Enforce PCI claim-before-iomap resource ownership and real DDA legacy
  IRQ handler arm/disarm for Nouveau without fabricating GPU-P hardware.
  The PCI core now records claim, release, iomap, owner-mismatch,
  unclaimed-iomap, and unclaimed-release counters; Nouveau DDA probe claims
  BARs before mapping, fails closed if the legacy IRQ vector/handler cannot be
  armed, unregisters the handler on remove/unwind, and only increments
  delivery-claimed when the handler actually fires. `fbstat` and
  `gpucorevalidate` expose those counters through
  `nouveau_pci_dma_resource_matrix` and
  `nouveau_pci_runtime_interface_matrix`; GPU-P-only Hyper-V remains
  fail-closed with zero fake resource, IRQ, native-present, or OpenGL-submit
  credit. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight final' scripts/hyperv-gpu-core-validate.sh`
  passed on 2026-05-25 with
  `validation_run_id=core-1779689395-3730908`.
- [x] Add Linux-shaped PCI DMA mask and streaming map diagnostics for
  DDA/Nouveau without granting GPU-P fake DMA state.
  The PCI core now tracks requested/effective streaming and coherent DMA mask
  bits, 64-to-32 fallback counters, and direct `pci_dma_map_single()` /
  `pci_dma_unmap_single()` calls. Nouveau probes those APIs only after a real
  BAR-backed DDA function is enabled and bus mastering is set; GPU-P-only
  Hyper-V remains `GPU_P_FAIL_CLOSED` with zero DMA mask/map attempts.
  `fbstat` and `gpucorevalidate` expose the fields through
  `nouveau_pci_dma_resource_matrix`,
  `nouveau_pci_runtime_contract_matrix`, and
  `nouveau_pci_runtime_interface_matrix`. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight final' scripts/hyperv-gpu-core-validate.sh`
  passed on 2026-05-25 with
  `validation_run_id=core-1779702023-161062`.
- [x] Add Linux-shaped MSI/MSI-X fail-closed and IRQ provenance diagnostics
  for DDA/Nouveau without granting GPU-P fake interrupt state.
  The PCI core now tracks IRQ-vector allocation attempts/failures,
  MSI/MSI-X requests that remain unsupported, and legacy IRQ request/grant
  counts. Nouveau publishes those fields plus handler invocation, device-cause,
  ack, and spurious counters through `nouveau_pci_dma_resource_matrix`,
  `nouveau_pci_runtime_contract_matrix`,
  `nouveau_pci_runtime_interface_matrix`, and the focused
  `nouveau_pci_irq_provenance_matrix`. GPU-P-only Hyper-V passes only when
  there is a real DDA/Nouveau reject reason or no DDA/Nouveau PCI candidate at
  all, with zero fabricated IRQ/native-present/OpenGL-submit credit. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight final' scripts/hyperv-gpu-core-validate.sh`
  passed on 2026-05-25 with
  `validation_run_id=core-1779703653-280807`.
- [x] Add Linux-shaped PCI runtime-PM/remove provenance diagnostics for
  DDA/Nouveau without granting GPU-P fake hot-remove or teardown state.
  The PCI core now records resume-before-remove attempts, PM barriers,
  hot-remove events, and removed state; Nouveau publishes teardown-phase
  counters for BAR unmaps, IRQ unregister, vector free, bus-master clear,
  device disable, and drvdata clear through
  `nouveau_pci_remove_pm_matrix`. Placeholder IRQ handler entries no longer
  claim delivery until a real device cause and ack path exists. GPU-P-only
  Hyper-V passes only with zero fabricated remove/PM/IRQ/native-present/
  OpenGL-submit state. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight final' scripts/hyperv-gpu-core-validate.sh`
  passed on 2026-05-25 with
  `validation_run_id=core-1779704735-342800`.
- [x] Tighten the accepted-DDA PCI runtime skeleton so it no longer only
  flips diagnostic booleans: runtime suspend now saves the PCI command
  register and disables memory/I/O/bus-master decode, runtime resume restores
  that command state before the driver resume callback, and the Nouveau IRQ
  handler reads `NV_PMC_INTR_0` gated by `NV_PMC_INTR_EN_0` before acking and
  claiming delivery. GPU-P-only boots still report zero fabricated BAR/DMA/IRQ
  state. Evidence: `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight final' scripts/hyperv-gpu-core-validate.sh`
  passed on 2026-05-25 with
  `validation_run_id=core-1779707157-496144`.
- [x] Add the Linux-shaped PCI wrapper surface Nouveau expects before deeper
  driver porting: `dma_set_mask_and_coherent()`, `pci_enable_msi()`,
  `pci_enable_msix_range()`, `pci_request_irq()`, `pci_free_irq()`,
  `pci_mmap_bar()`, `pm_runtime_resume_and_get()`, `pm_runtime_put()`,
  no-resume/no-idle refs, and `pm_runtime_barrier()`. Nouveau now uses the
  wrapper surface for DMA mask setup, MSI/MSI-X fail-closed probes, and IRQ
  registration while GPU-P-only Hyper-V still reports no fabricated BAR/DMA/IRQ
  or native-present/OpenGL-submit credit. This is an interface skeleton, not a
  claim that DDA hardware validation or native present is complete.
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
  close. Channel allocation and VM init now publish owner state only after
  successful ioctl copyout, so an `-EFAULT` user ABI failure cannot leave an
  active channel or VM marker behind. `drmiftest` emits
  `nouveau_channel_object_matrix` for this contract;
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
- [x] Add a kernel-owned native-display readiness and KMS present-discriminator
  skeleton before any DDA/Nouveau native-present claims.
  The stats ABI now has explicit Nouveau display readiness fields for DDA
  native display presence, display-create attempts/successes, heads,
  connectors, vblank support/IRQs, hardware page-flip completions, and reject
  reasons. KMS native-present lanes are separately named as none, dumb,
  synthvid, or Nouveau hardware, with zero-credit reject reasons required on
  GPU-P-only Hyper-V. `fbstat`, `gpucorevalidate`, and the focused runner now
  require `native_display_readiness_failclosed_matrix`,
  `nouveau_display_failclosed_matrix`, and
  `kms_present_discriminator_failclosed_matrix`; validation passed on
  2026-05-25 with `validation_run_id=core-1779720971-1215307`.

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
  Fresh source audit on 2026-05-25 found no existing WSL `dxgkrnl`, Linux
  Hyper-V DRM/synthvid, or current DDA/Nouveau host ABI that can honestly bind
  a D3D12 resource/allocation/fence to host scanout and return display
  completion. The current implementation must remain fail-closed until a
  documented GPU-P/DXG display-bind packet exists or the separate DDA/Nouveau
  native display path can provide equivalent non-readback completion.
  A follow-up WSL2 audit on 2026-05-26 confirmed the same root gap against
  `drivers/hv/dxgkrnl` and `include/uapi/misc/d3dkmthk.h`: WSL has
  shared-resource, sync-file, submit, standard-allocation, and present-history
  telemetry primitives, but no Linux UAPI ioctl, VMBus sender, or implemented
  host-to-VM demux that binds a D3D12 resource/allocation/fence to scanout and
  returns source/resource-correlated present completion. `VM_PKT_COMP`
  transaction replies and `PROPAGATEPRESENTHISTORYTOKEN` remain zero-credit
  telemetry until a provider-owned sender/completion contract is documented.
  `dxg_native_present_lane_rejection_matrix` now names each rejected lane
  separately so future work cannot treat WSL present-history enum knowledge,
  synthvid/GPA dirty rectangles, Linux Hyper-V DRM shadow blits, or a separate
  DDA/Nouveau PCI path as D3D12 native-present credit without a real sender and
  completion contract. The candidate IDs and rejection reasons are now
  kernel-owned stats as well as user-space validator text: the kernel records
  the WSL VMBus enum namespace, the absence of a Linux display-bind ioctl,
  sender/resource-bind/completion contracts, and explicit zero-credit reject
  reasons before any validator can consume the row.
  The fail-closed proof now also records that the WSL ioctl namespace was
  audited, no display-bind ioctl is present, standard-allocation payloads are
  private driver data rather than scanout binding, synthvid is only a GPA dirty
  rectangle display path, and DDA/Nouveau PCI display presence is separate from
  D3D12 resource import, scanout bind, and hardware flip completion.
  `gpu_remaining_holistic_skeleton_matrix` names the open plan count,
  ordered chunks (`display_bind,native_completion,fps,backend,webkit`), selected
  GPU-P/DDA lane, display-bind-provider completion authority, and every
  downstream gate with zero native-present/OpenGL/WebKit credit until a real
  sender and display completion source replace the fail-closed provider.
  `host_display_bind_source_catalog_matrix` now ties those audited sources
  together as the root source catalog: selected source missing, WSLg channel
  absent, no custom host tool, no GPU-P/DXG sender/completion contract, no
  synthvid D3D12 bind, no DDA D3D12 import/scanout/hardware-flip completion,
  provider fail-closed, and zero native-present/OpenGL/WebKit credit.
  The Hyper-V-owned provider boundary is now a generic
  `hyperv_dxg_display_bind_submit()` slot backed by the current fail-closed
  implementation. Kernel stats and pure-C validators require the provider's
  returned `pin_revalidated`, `no_host_abi`, `no_sender`, and `no_completion`
  scalars, so a future implementation must replace the sender/completion
  contract deliberately instead of deriving credit from DXG readiness.
  - [x] Move the selected scanout-bind provider slot under Hyper-V ownership
    while keeping it fail-closed. `fb_dxg_present.c` now delegates the
    source/resource-generation snapshot to
    `hyperv_dxg_display_bind_submit_failclosed()`, which revalidates the
    WSL-style pinned `/dev/dxg` + `anon_inode:dxgresource` metadata and
    returns explicit `no_host_abi`, `no_sender`, and `no_completion`
    diagnostics with zero present/completed ids and zero native-present or
    OpenGL-submit credit.
  - [x] Add the future-success accept shape without enabling today's
    fail-closed path. `fb_dxg_present.c` now accepts a provider result only
    when the provider returns success, a real transport, scanout-bind
    operation, host ABI present, sender present, display completion present,
    revalidated pins, no negative provider diagnostics, matching
    source/resource generations, a nonzero present id, completed >= present
    id, and display completion source. The current
    `hyperv_dxg_display_bind_submit()` implementation still returns zero ids
    plus `no_host_abi`, `no_sender`, and `no_completion`, so Hyper-V receives
    no native-present or OpenGL-submit credit. Evidence:
    `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
    CORE_C_SECTIONS='preflight present-source final'
    scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-25 with
    `validation_run_id=core-1779750432-2876003`.
  - [x] Expose the source/resource-generation-preserving query and
    future-success validator shape for the fail-closed provider boundary.
    `FB_GPU_DXG_PRESENT_BIND_CONTRACT_QUERY` and the provider result now
    report matching source/resource generations, provider status/block reason,
    completion source, dirty sequence/rect counts, host-ABI/sender/completion
    presence, negative provider diagnostics, and pin revalidation. `fbstat`,
    `dxgprobe`, and `gpucorevalidate` require
    `d3d12_display_bind_success_shape_matrix` and
    `d3d12_display_bind_query_fields_matrix`, accepting today's fail-closed
    zero-credit state while defining the complete future-success shape. A
    fresh WSL2 source comparison against local 6.6.87 and public 6.18
    confirmed WSL has D3DKMT shared-resource, present-history, and sync-file
    primitives but no Linux UAPI display-bind ioctl or exposed display
    completion handler, so the root host ABI item remains open. Evidence:
    `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
    CORE_C_SECTIONS='preflight present-source final'
    scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-25 with
    `validation_run_id=core-1779751975-2934585`.
  - [ ] When a real GPU-P/DDA sender is found, replace the current
    synchronous fail-closed pending ledger with provider-owned pending packet
    publication: publish-before-send, transport pending id, command id,
    transaction id, channel, completion-demux registration, resolve/cancel
    result, and ref release must be visible in a pure-C
    `d3d12_display_bind_provider_pending_publication_matrix`.
  - [ ] Add stale async completion proof for the real sender: unregister or
    owner close while a provider pending record exists must cancel the pending
    object, keep present/completed ids zero, reject any late completion as
    stale/after-release, and leave no native-present/OpenGL-submit credit.
  - [x] Add the pure-C future contract row for provider-owned pending packet
    publication before a real sender is wired.
    `d3d12_display_bind_provider_pending_publication_matrix` is now backed by
    provider-owned kernel stats for publication attempts,
    publish-before-send, transport pending id, command id, transaction id,
    channel, completion-demux registration, resolve/cancel, and ref-release
    fields. Today it must report `PASS_FAILCLOSED` with publication attempts
    advancing, sender-owned fields at zero, `provider_no_host_abi=1`,
    `provider_no_sender=1`, `provider_no_completion=1`, zero
    present/completed ids, zero native-present credit, and zero OpenGL-submit
    credit. This checked row is a gate for the future provider, not evidence
    that the unchecked real-sender item is complete.
  - [x] Make the fail-closed provider pending/stale diagnostics provider-owned
    instead of inferred by user-space.
    The Hyper-V provider result and `fb_gpu_stats` now expose owner,
    source, and resource generations for the provider-owned pending object,
    plus no-host-ABI cancel/ref-release fields and stale-after-release reject
    accounting. `dxgprobe`, `fbstat`, and `gpucorevalidate` require the
    provider/pending generation fields to match each other, prove sender-owned
    publish-before-send/transport/command/channel fields stay zero while the
    host ABI is absent, and keep stale/late completion paths at zero
    native-present/OpenGL-submit/WebKit credit. Evidence:
    `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
    CORE_C_SECTIONS='present-source' VALIDATION_RUN_ID=provider-pending-failclosed-3
    scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-26.
  - [x] Make the DDA/Nouveau display split explicit as zero-credit D3D12
    evidence. A DDA-backed Nouveau PCI display path can only count as a native
    display lane after it also proves a D3D12 shared-resource import,
    scanout-bind, and hardware flip completion contract. `fb_nouveau.c` now
    includes `NO_NOUVEAU_DISPLAY` in the fail-closed reject mask, and
    `fbstat`, `dxgprobe`, `gpucorevalidate`, and the focused runner require
    `d3d12_dda_nouveau_separate_display_not_bind_matrix` so DDA PCI display
    presence cannot be confused with a D3D12 resource-to-scanout bind.
  - [x] Make host-to-VM present-history completion observable before any
    future native-present credit. The Hyper-V DXG receive path now recognizes
    `PROPAGATEPRESENTHISTORYTOKEN`, records packet count, command id, payload
    length, and payload head bytes in `/dev/dxg`, and the present-source C
    validators require
    `dxg_host_to_vm_presenthistory_completion_matrix` with zero present-history
    packets, zero completion contracts, zero native-present credit, and zero
    OpenGL-submit credit on the current fail-closed path. The validators also
    emit `dxg_presenthistory_telemetry_not_completion_matrix`, matching WSL's
    enum-only present-history source audit by naming the Linux in-band display
    completion handler as absent instead of treating packet telemetry as a
    display-bind completion contract.
- [x] Make the selected bind lane's missing host ABI explicit and validator
  owned instead of implicit in `/dev/dxg` readiness. `dxgprobe` and
  `gpucorevalidate` now emit and require
  `dxg_resource_scanout_bind_host_abi_matrix`, proving the selected lane is
  GPU-P/DXG scanout-bind, no custom host tool is used, WSL dxgkrnl exposes no
  display-bind ioctl, synthvid remains GPA-dirty-only, D3DKMT handles and the
  same-adapter shared resource are only admission evidence, and no display
  target, present id, native-present credit, or OpenGL-submit credit is granted.
  Evidence: `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779675508-3085051`.
- [x] Add a first-class scanout-bind skeleton beneath the selected lane rather
  than treating the missing transport as only a bind-contract query result.
  The kernel now records `dxg_scanout_bind_*` attempts, rejects,
  weak-evidence rejects, completion polls, last source/resource generation,
  present id, completion id, dirty sequence, and dirty-rect count. `fbstat`,
  `dxgprobe`, `gpucorevalidate`, and the focused runner require
  `dxg_scanout_bind_skeleton_matrix` so D3DKMT handle readiness, shared-resource
  metadata, query calls, or callback-only evidence cannot accidentally earn
  native-present or OpenGL-submit credit before a real GPU-P/DDA display-bind
  transport exists. Evidence: `BUILD_DIR=/tmp/xv6-hyperv-build
  CORE_C_MODE=sections CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-25 with
  `validation_run_id=core-1779708956-589366`.
- [x] Add a canonical display-bind backend boundary that every later consumer
  must use instead of inferring native present from loose DXG readiness.
  The kernel stats ABI now mirrors the selected bind backend, transport,
  operation, metadata/lifetime requirements, block reason, display-completion
  source, source/resource generations, and present/completed ids as
  `dxg_display_bind_*`. `fbstat`, `gpucorevalidate`, and the focused runner
  require `d3d12_display_bind_backend_boundary_matrix`, while `wlcomp`, the
  FPS validator, and the WebKit gate consume the same `display_bind_*` keys
  from `/tmp/wlcomp-d3d12-present` and log matrices. Consumer/user-facing
  evidence normalizes the Hyper-V GPU-P bind lane to
  `display_bind_backend=gpup_dxg_scanout_bind`,
  keeps `display_bind_transport=gpu-p-dxg-resource-scanout-bind`, and keeps
  `display_bind_completion_source=display`, while still accepting older log
  spellings where validators need historical compatibility. The current Hyper-V
  path remains fail-closed with zero ids, zero native-present credit, zero
  OpenGL-submit credit, and no custom host tooling.
- [x] Add a source-local display-bind provider boundary and ledger before
  wiring any host sender.
  `fb_dxg_present.c` now builds an internal display-bind request/result for
  the selected GPU-P/DDA lane and records the result on the owning present
  source with source/resource generations, present/completed ids, completion
  source, status, and block reason. The provider currently returns the same
  `EOPNOTSUPP` no-transport/no-completion result because there is still no
  documented non-custom GPU-P/DDA display-bind packet. This keeps later work
  focused on replacing the provider backend instead of inferring credit from
  D3DKMT handles, sync-files, or synthvid dirty rectangles.
- [x] Split the display-bind provider call away from the framebuffer lock and
  revalidate the source/resource generation before recording a result.
  The selected bind provider now receives a source snapshot, runs outside
  `fb_state.lock`, then reacquires the lock and accepts the result only if the
  present source handle, source generation, and resource generation still match.
  `fbstat`, `dxgprobe`, and `gpucorevalidate` expose
  `provider_submits`, `lock_dropped_submits`, `revalidate_attempts`,
  `revalidate_successes`, and `revalidate_failures`, keeping the future
  sleepable GPU-P/DDA sender WSL-style without granting native-present credit.
- [x] Prove the future display-bind sender receives a complete source-owned
  request before it is allowed to fail closed.
  `hyperv_dxg_display_bind_submit_failclosed()` now validates that the
  provider request carries the device, resource, allocation, dimensions,
  format/modifier, adapter LUID, source/resource generations, and sync/fence
  metadata required for a WSL-style source-owned bind attempt. `fbstat`,
  `dxgprobe`, `gpucorevalidate`, and the focused runner require
  `d3d12_display_bind_request_metadata_matrix` with
  `request_metadata_complete=1`, `request_sync_metadata_complete=1`,
  `missing_metadata=0x0`, zero present/completed ids, zero native-present
  credit, and zero OpenGL-submit credit. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-25 with
  `validation_run_id=core-1779758881-3213422`.
- [x] Add a WSL-style pending display-bind request lifetime ledger before any
  sleepable host sender exists.
  `fb_dxg_present.c` now publishes a source-owned pending request before
  dropping `fb_state.lock`, preserves source/resource generations on that
  pending object, resolves it fail-closed when the provider returns
  `EOPNOTSUPP`, and cancels outstanding pending requests during unregister or
  owner close. `dxgprobe` separates the lifecycle proof from the live-source
  generation proof: `d3d12_display_bind_pending_lifetime_matrix` requires
  created requests to drain to zero active entries with zero completions,
  nonzero failclosed resolves, zero native-present credit, and zero
  OpenGL-submit credit, while
  `d3d12_display_bind_generation_revalidation_matrix` requires the provider
  lock-drop revalidation to preserve the live bind-contract source/resource
  generations and pinned resource generation. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  VALIDATION_RUN_ID=core-display-bind-pending-lifetime
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-26.
- [x] Keep WSL present-history style command IDs as explicit rejected
  candidates until a source-backed sender and completion contract exists.
  The DXG present path now exposes `dxg_scanout_bind_candidate_command_matrix`
  and `dxg_scanout_bind_weak_evidence_matrix`: WSL guest-to-host enum IDs
  34/35/38 plus the host-to-VM `PROPAGATEPRESENTHISTORYTOKEN` completion
  enum ID 1 are known, but sender/completion contracts remain zero. D3DKMT
  handle readiness, same-adapter resources, sync-file acquire, and synthvid
  GPA-dirty evidence are rejected as weak evidence, and native-present/
  OpenGL-submit credit stays zero.
  - [x] Keep validators synchronized with that four-command diagnostic set:
    core, DXG, FPS, and WebKit validators now require `presenthistory=34`,
    `redirected_flip_fence=35`, `blt=38`, and
    `propagate_presenthistory=1`/`cmds_known=4` while still requiring zero
    sender/resource-bind/display-completion contracts.
- [x] Restore the WSL-equivalent standard-allocation surface ABI skeleton
  before adding any native display-bind behavior. The Hyper-V DXG VMBus
  standard-allocation command now carries the same shared-primary, shadow,
  staging, and GDI surface union shape used by WSL2 `dxgkrnl`; `dxgprobe`,
  `gpucorevalidate`, and the focused core runner require
  `wsl_standard_alloc_surface_abi_matrix` to prove the surface layouts are
  present and still grant zero native-present/OpenGL-submit credit. This keeps
  WSL private-driver-data parity separate from the still-missing
  `dxg-resource-scanout-bind` host ABI.
- [x] Add a zero-credit native-completion matrix before any real display-bind
  implementation. `dxgprobe`, `fbstat`, `gpucorevalidate`, and the focused
  runner now require `d3d12_native_completion_zero_credit_matrix` and
  `d3d12_display_bind_absent_matrix`: display bind is absent, transport is
  absent, present ids and completion counters are zero, callback/release
  ordering is blocked/deferred, per-client generation matching is required,
  and no native-present/OpenGL-submit credit is granted. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779679708-3257998`.
- [x] Add the commit-result copyout skeleton required by any future nonzero
  `present_id/completed` path. `FB_GPU_DXG_PRESENT_SOURCE_COMMIT` now copies
  the commit struct back to userspace only on success; the current fail-closed
  path still returns an errno and preserves zero present/completion credit.
  `dxgprobe`, `gpucorevalidate`, and the focused runner require
  `d3d12_present_commit_result_copyout_contract_matrix` so the missing real
  host bind cannot be hidden behind an ioctl ABI that would discard success
  results.
- [x] Replace present-source resource-fd provenance with a typed WSL-style
  DXG shared-resource admission snapshot: the FB present source must prove the
  fd is `anon_inode:dxgresource`, the shared-resource metadata is sealed, the
  allocation/resource handles match the D3DKMT register payload, the sealed
  generation feeds the bind-contract resource generation, stale fds/sources are
  rejected, and no native-present/OpenGL-submit credit is granted until the
  real display-bind transport exists. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-25 with
  `validation_run_id=core-1779681659-3341588`.
- [x] Pin WSL-style display-bind source lifetime at registration time.
  `fb_dxg_present.c` now pins the owning `/dev/dxg` fd and typed
  `anon_inode:dxgresource` fd for every verified source before a future
  sleepable display-bind provider can drop framebuffer locks. The pin snapshot
  records resource generation, process generation, process refs, sealed
  shared-resource metadata, and balanced unpin cleanup on source unregister or
  owner close. The intentionally unverified `resource_fd=-1` negative lane
  still registers and fails closed at commit with
  `RESOURCE_FD_UNVERIFIED`, so validator evidence continues to prove the
  correct rejection point. Evidence: `d3d12_display_bind_pin_lifetime_matrix`
  passed with `pin_attempts=2`, `pin_successes=2`, `pin_failures=0`,
  `unpins=2`, pinned dxg/resource fd evidence, nonzero process/resource
  generations, and zero native-present/OpenGL-submit credit in
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` on 2026-05-25 with
  `validation_run_id=core-1779731829-1780191`, then re-validated after
  parent/global-share propagation and detailed pin diagnostics with
  `validation_run_id=core-1779742515-2470561`.
- [x] Seal display-bind metadata on unregister and owner close so stale source
  handles cannot retain native-present ids after lifetime teardown.
  `fb_dxg_present.c` now clears source/global display-bind ids and completion
  status during explicit source release and owner cleanup, records
  stale-source, generation, completion, late-completion, and after-close query
  counters, and keeps native-present/OpenGL/WebKit credit at zero for every
  stale query. `dxgprobe`, `fbstat`, `gpucorevalidate`, and the focused runner
  require `d3d12_display_bind_stale_source_zero_credit_matrix`. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-25 with
  `validation_run_id=core-1779754227-3036304`; the present-source row showed
  `release_sources_delta=3`, `after_close_queries=3`,
  `stale_source_rejects=3`, `release_clears=3`, zero after-close ids, and
  zero native-present/OpenGL/WebKit credit.
- [x] Add a WSL-style sync-file acquire pre-open guard for the present-source
  path: export a monitored fence to a sync-file fd, reopen it to a D3DKMT sync
  object before present admission, reject wrong fd kinds, preserve fence value
  metadata, and keep native-present/OpenGL-submit credit at zero until a real
  display-bind transport exists.
  Evidence: `d3d12_present_syncfile_preopen_matrix` and final zero-credit
  checks passed in
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight present-source final'
  scripts/hyperv-gpu-core-validate.sh` on 2026-05-25 with
  `validation_run_id=core-1779683691-3428894`.
- [x] Make the D3D12 Wayland resource-buffer path pass on the current Hyper-V
  runtime: same adapter LUID, shared resource fd, acquire fence or sync-file,
  compositor import/open, and present admission.
  The runtime path now exports a real D3D12 shared resource, creates a DXG
  sync-file acquire fd with `LX_DXCREATESYNCFILE`, validates direct
  `LX_DXOPENSYNCOBJECTFROMSYNCFILE`, and sends that fd through the Wayland
  resource-buffer protocol. `wlcomp` opens the resource and acquire fence on
  the same adapter, registers the compositor-opened resource/allocation as a
  DXG present source, then fails closed only at the selected but still-missing
  GPU-P/DDA `dxg-resource-scanout-bind` host ABI. The kernel admission check
  now validates per-open resource handles through the compositor `/dev/dxg`
  owner table plus the shared fd's canonical global share, instead of requiring
  the fd's creator-side handles to equal `OPENRESOURCEFROMNTHANDLE` results.
  Evidence: `XV6_WLCOMP_D3D12_RUN_ID=opened d3d12sharedsmoke --runtime
  --allow-failclosed-present` passed on the focused 6-vCPU Hyper-V image on
  2026-05-25, including
  `d3d12_fence_sharing_validation_matrix`,
  `d3d12_wayland_dxg_syncfile_acquire_matrix`,
  `d3d12_wayland_resource_buffer_admission_matrix`,
  `d3d12_wayland_present_failclosed_identity_matrix`,
  `d3d12_failclosed_lifecycle_matrix`, and
  `d3d12_wayland_resource_buffer_runtime_matrix`, all with
  `native_present_claim=0` and `opengl_submit_credit=0`.
- [x] Add compositor-side admission and fail-closed identity matrices for that
  path without granting native-present credit. `wlcomp` now emits
  `d3d12_wayland_resource_buffer_admission_matrix` after same-LUID
  resource/fence import and protocol acceptance, and
  `d3d12_wayland_present_failclosed_identity_matrix` when the imported buffer
  reaches GPU-copy proof but still lacks native display completion. The DXG and
  WebKit validators classify those rows as intermediate evidence, not as native
  present. Build evidence:
  `cmake --build /tmp/xv6-hyperv-build/ports --target port-wayland -j2`
  passed on 2026-05-24.
- [ ] Replace the fail-closed bind-contract skeleton with a real selected
  display-bind source. This is the unchecked root dependency for this section:
  do not treat it as closed until a source-owned D3D12 resource/allocation,
  adapter LUID, dimensions/format/modifier, and sync-file/fence target are
  consumed by a GPU-P/DDA display-bind lane that returns display-correlated
  completion.
  Recommended disjoint chunks:
  1. Host ABI discovery/proof: identify or add the narrow packet/protocol
     boundary and keep the existing fail-closed matrix green while it is absent.
  2. Kernel bind path: wire the source/admission record into a real
     scanout-bind attempt, preserving source/resource generation, dirty metadata,
     and zero-credit rejection counters on every failure path.
  3. Compositor handoff: replace the current GPU-copy-only proof with a
     display-bind submission path that still avoids CPU map/readback and DRI
     software present.
  4. Completion/lifetime: return nonzero present ids and completed ids for the
     same submitted resource generation, then release buffers and send frame
     callbacks only after that native completion.
  5. Validators/credit: add the native completion validators and only then let
     FPS, backend OpenGL-submit, and WebKit gates consume the evidence.
- [x] Add per-client, per-resource, per-generation native-present counters so
  one client's progress cannot satisfy another client's validator.
  `wlcomp` records `d3d12_client_native_present_*`,
  `d3d12_resource_native_present_*`, and
  `d3d12_resource_generation_native_present_*` counters in
  `/tmp/wlcomp-d3d12-present`; `d3d12sharedsmoke` validates that attempts,
  completions, rejects, and resource generations match the current client and
  buffer generation before any native-present credit is accepted. The current
  Hyper-V path remains zero-credit because native display completion is absent.
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
- [x] Keep `backend_opengl_submit 0` until this section and the FPS section
  both pass. Hyper-V still reports `backend_opengl_submit 0` and
  `backend_opengl_submit_gate closed`; the focused core validator rejects any
  premature `backend_opengl_submit 1` claim while native present/FPS/WebKit
  gates remain open. Evidence:
  `BUILD_DIR=/tmp/xv6-hyperv-build CORE_C_MODE=sections
  CORE_C_SECTIONS='preflight final'
  scripts/hyperv-gpu-core-validate.sh` passed on 2026-05-24 with
  `validation_run_id=core-1779676616-3128276`, including
  `hyperv_opengl_submit_gate_matrix ... backend_gate=closed status=PASS`.
  Source now also emits `opengl_submit_backend_separation_matrix` so the next
  focused rerun can prove Hyper-V has DXG transport and D3DKMT while
  `backend_opengl_submit=0`; KVM/virgl remains the only allowed OpenGL-submit
  backend.

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
  A pure-C fail-closed row,
  `d3d12_native_completion_future_contract_matrix`, now names these required
  checks before the real lane exists: nonzero present id, completed >= present
  id, same resource generation, callback/release after completion,
  close-before-signal cancellation, and cleanup balance. It grants zero native
  present or OpenGL-submit credit until the display-bind gate opens.
  `gpucorevalidate` also emits `d3d12_native_completion_not_kms_matrix`, which
  proves generic `FB_GPU_DISPLAY_WAIT`, KMS vblank, KMS OUT_FENCE, page-flip,
  and software-blit progress remain zero-credit unless the future D3D12
  display-bind completion path supplies nonzero source/resource-correlated
  present/completed ids.
  The current pure-C validators additionally emit and require
  `d3d12_display_bind_id_shape_matrix`,
  `d3d12_provider_credit_gate_matrix`, and
  `wsl_standard_alloc_not_display_bind_matrix`. These fail if display-bind ids
  are forged/stale, if native/OpenGL credit appears while provider
  `no_host_abi/no_sender/no_completion` diagnostics remain set, or if WSL
  standard-allocation private metadata is treated as a scanout bind. Evidence:
  focused 6-vCPU Hyper-V validation passed on 2026-05-25 with
  `validation_run_id=core-1779750432-2876003`.
  `d3d12_native_completion_lifetime_matrix` now also accepts the preflight
  fail-closed state where no provider submit has happened yet, then requires
  `provider_no_completion=1` after a fail-closed provider submit or a real
  display completion after a future successful provider submit. Evidence:
  focused 6-vCPU Hyper-V validation passed on 2026-05-25 with
  `validation_run_id=core-1779754227-3036304`.
  The skeleton now also separates completion authority from progress telemetry.
  `d3d12_completion_source_authority_matrix` names the only acceptable D3D12
  native-completion authority as the display-bind provider and keeps NTSTATUS,
  present-history telemetry, sync-file fences, callback release, and KMS
  vblank/page-flip evidence at zero credit.
  `native_present_completion_source_namespace_matrix` keeps D3D12 display-bind
  completion ids, Nouveau KMS vblank/page-flip sequences, and IRQ-cause counters
  in separate namespaces so a later DDA/Nouveau display path cannot be mistaken
  for D3D12 resource scanout-bind completion.
- [x] Keep WSL-trace replay equivalence current for the real UMD sequence and
  fail if xv6 rewrites packets without matching host-saw diagnostics.
  The same-adapter NVIDIA WSL replay now uses the full-private trace
  `/tmp/xv6-wsl-probe/mesaglfeature-nvidia-fullpriv-20260525-034404.trace`:
  3200-byte DX12 context private data, 594-byte queue-allocation private data,
  WSL map-before-resident order, `LX_DXMAKERESIDENT flags=0x1`, 124-byte HW
  queue private data with the live allocation handle at offset `0x24`, and
  1880-byte HW queue submit private data. The kernel records host-saw packet
  diagnostics for the transformed/forwarded packets, and the cleanup path now
  accepts WSL-style local-adapter aliases against the tracked host-adapter
  GPUVA range before sending `FREEGPUVIRTUALADDRESS`. Evidence:
  focused 6-vCPU Hyper-V run on 2026-05-25 printed
  `wsl_trace_replay_host_saw_matrix ... make_flags:0x1 ... submit_cmd_len:4096
  submit_priv:1880 ... status=PASS` and
  `wsl_trace_replay_signature ... equivalence=wsl_private_hwqueue_submit_success
  ... packets:10/10 ... status=PASS`.
- [x] Build and validate completed sections with `/tmp/xv6-hyperv-build` and
  focused 6-vCPU Hyper-V images before marking section items done.
  Evidence: the focused Hyper-V core validator rebuilt kernel/rootfs, deployed
  a 6-vCPU image, and passed `preflight`, `drm`, `dxg-share`, `dxg-sync`,
  `present-source`, `buffers`, and `final` pure-C sections on 2026-05-24
  (`validation_run_id=core-1779667968-2704585`). Native-present, FPS, and
  WebKit artifacts remain separate unchecked gates.
- [ ] Make the finite 480p desktop 3D validator pass only on native D3D12
  presented frames after warmup.
  Skeleton/preflight evidence is now explicit: `hyperv-3d-fps-validate.sh`
  emits `fps_native_present_gate_skeleton_matrix` and
  `fps_visible_content_preflight_matrix` before heavy FPS acceptance, with the
  gate closed until native D3D12 present completions and visible content
  progress are both present. It also emits
  `fps_demo_interaction_gate_matrix` and
  `fps_visible_native_content_gate_matrix`, so visible/closeable/resizable demo
  evidence and content-progress-native-present correlation are required before
  the finite FPS gate can open.
  `mesawlegl` and the FPS validator now require exact native/content identity:
  content progress state must be `NATIVE_PRESENT_COMPLETE`, visible content
  progress must be `NATIVE_PRESENT_COMPLETE`, content progress must be
  compositor-owned, content present/completed ids must match the canonical DXG
  and display-bind ids, display-bind completion sources must all be native
  display sources, final handoff ids must match, and buffer/resource
  generations must match. `wlcomp` now exports final-handoff resource
  generation and content-progress current-run/identity fields, and
  `hyperv-3d-fps-validate.sh` requires exact sample-window and visual-window
  identity before accepting finite FPS. Lightweight anti-inflation selftest
  evidence:
  `FPS_ANTI_INFLATION_SELFTEST=1 VALIDATION_RUN_ID=selftest-identity
  scripts/hyperv-3d-fps-validate.sh` passed on 2026-05-25.
- [x] Require full 640x480 or equivalent 480p rendering with `render_div == 1`.
  The finite FPS validator already rejects non-480p or divided renders; its
  final pass token now records `window=640x480 render=640x480 render_div=1`,
  and WebKit's prior-FPS contract requires that exact token before acceleration
  can consume the FPS artifact.
- [ ] Confirm the demo is visible, closeable, and resizable during the passing
  run.
  - [x] Add compositor-owned lifecycle evidence separate from app-loop FPS:
    `wlcomp` now appends `wayland_demo_visible_close_resize_matrix` rows to
    `/tmp/mesawlegl-fps` with current validation run id, client pid, mapped
    state, closeable path, resize request/configure/ack or buffer-size-change
    state, final window geometry, and zero native/OpenGL-submit credit.
  - [x] Make the finite FPS launch exercise resize instead of relying on a
    disabled-resize workaround: `hyperv-3d-fps-validate.sh` starts
    `mesademo` with `--resize-every=${DEMO_RESIZE_EVERY:-120}`, and
    `mesademo` forwards that option to `mesawlegl`.
  - [x] Tighten `mesawlegl_demo_interaction_matrix` so source-side
    `resizable_demo` requires an actual resize count, and its native/content
    interaction fields come from the same strict D3D12 evidence reader used by
    FPS samples.
  - [x] Add a lightweight `fps_demo_interaction_selftest_matrix` so the FPS
    validator proves stale run ids, missing resize evidence, source-only
    lifecycle evidence, callback-only evidence, and forged display-bind ids are
    rejected with zero native-present/OpenGL-submit credit.
- [x] Require visible content progress outside title/FPS overlay areas and
  correlate content hashes or thumbnail deltas with native present completions.
  The finite FPS gate now requires outside-overlay thumbnail CRC/progression,
  compositor-owned content CRC, frame, and frame-hash progress, matching
  run/resource/generation/completion ids, and content-frame FPS above 60 in
  both the sample and visual windows before a passing FPS artifact can be
  consumed by WebKit or OpenGL-submit.
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
  The validator now also samples multiple visual windows and rejects "moved
  briefly, then froze" evidence with `fps_frozen_window_rejection_matrix` and
  `fps_sustained_post_warmup_progress_matrix`. Lightweight evidence:
  `FPS_ANTI_INFLATION_SELFTEST=1 VALIDATION_RUN_ID=selftest-sustained-freeze
  scripts/hyperv-3d-fps-validate.sh` passed on 2026-05-25.
  - [x] Persist forged display-bind negative evidence into the FPS log and
    split finite-FPS credit from OpenGL-submit credit. The validator now writes
    `fps_forged_display_bind_negative_matrix` durably during the preflight,
    computes outside-overlay CRC transitions before sustained-window checks,
    and keeps `opengl_submit_credit=0` unless finite native-present FPS and
    `backend_opengl_submit=1` are both present.
- [x] Add a present/FPS provenance skeleton so visible demo FPS is explicitly
  zero-credit unless it is backed by current-run native D3D12 display
  completions. `wlcomp` now emits
  `d3d12_wayland_present_fps_provenance_matrix`, and `mesawlegl` emits
  `mesawlegl_fps_present_credit_matrix` with
  `effective_presented_fps=0.000`, `visible_fps_ignored=1`, and
  `native_present_credit=0` on the current fail-closed Hyper-V path.
- [x] Promote the Wayland present/FPS provenance row into the file-backed
  D3D12 evidence contract, not just stderr. `/tmp/wlcomp-d3d12-present` now
  records `d3d12_wayland_present_fps_provenance_matrix` plus scalar
  `d3d12_fps_provenance_*` keys, so FPS and WebKit validators can reject
  visible app-loop FPS unless the same resource/generation has a native
  display completion.
- [x] Make D3D12 display-bind evidence self-contained for FPS/WebKit
  consumers. `/tmp/wlcomp-d3d12-present`, the compositor stderr matrices, and
  `mesawlegl_fps_present_credit_matrix` now carry canonical
  `display_bind_backend=gpup_dxg_scanout_bind`,
  `display_bind_transport=gpu-p-dxg-resource-scanout-bind`,
  present/completed ids, resource generation, and completion source.
  Fail-closed rows report `display_bind_completion_source=missing` with zero
  ids. `mesawlegl` computes `effective_presented_fps` from native completion
  deltas instead of app-loop FPS, and the FPS/WebKit preflights require
  display-bind, content-progress, and callback/release correlation.
- [x] Add a fail-closed content-progress provenance skeleton to the same
  D3D12 evidence contract. `/tmp/wlcomp-d3d12-present` now records
  `d3d12_wayland_content_progress_matrix` plus
  `d3d12_content_progress_*` scalar keys; on the current Hyper-V path it
  reports `d3d12_content_progress_state=DEFERRED`,
  `d3d12_visible_content_progress=DEFERRED`,
  `d3d12_content_progress_requires_native_present=1`,
  `d3d12_visible_content_requires_native_present_completion=1`,
  `d3d12_visible_content_credit_before_native_present=0`, and zero
  native-present/OpenGL-submit credit. This does not close the later
  content-hash/thumbnail-progress requirement; it makes visible-content
  credit explicitly impossible before native display completion.
  - [x] Emit explicit zero-credit content sample fields so consumers cannot
    confuse missing data with passing data:
    `d3d12_present_content_crc`, `d3d12_visible_content_crc`,
    `d3d12_present_content_frame`, `d3d12_visible_content_frame`,
    `d3d12_present_frame_hash`, and `d3d12_visible_frame_hash` are present in
    the compositor evidence and remain zero until a real native-present
    content source exists.
  - [x] Add compositor-owned content sample bookkeeping to the D3D12 buffer
    lifetime. `wlcomp` now resets per-buffer content CRC/frame/hash sample
    fields on each D3D12 shared-resource commit, records the display-bind
    present/completed/resource generation on native completion, and has the
    evidence writer consume only those compositor-owned fields. The sampler
    still reports `source_owned=0` and grants no visible/native content credit
    until a non-readback native-present content sampler exists.
  - [x] Keep the FPS demo source-side content telemetry separate from native
    proof. `mesawlegl` now writes `client_content_hash`,
    `client_content_frame`, `content_region=client-content-no-title-fps`, and
    `mesawlegl_demo_interaction_matrix`, while still requiring compositor
    native-present content fields before granting effective presented FPS.
  - [x] Remove the loose compositor stderr-only content-credit claim: the
    display-completion log now carries explicit zero CRC/frame/hash content
    fields and reports `PASS_FAILCLOSED` until real compositor-owned content
    samples exist.
  - [x] Tighten the finite FPS/WebKit handoff gates so content progress
    requires compositor-owned frame-hash progress as well as CRC/frame
    counters. `hyperv-3d-fps-validate.sh` now rejects both sample-window and
    visual-window evidence unless `d3d12_present_frame_hash`/
    `d3d12_visible_frame_hash` advance with the same native-present evidence,
    and `wlcomp_launcher` refuses WebKit acceleration unless the same
    frame-hash evidence is present.
  - [x] Clamp finite-FPS acceptance to compositor-owned content cadence, not
    only native/display completion counters. `effective_presented_fps` now
    takes the minimum of native present FPS, display completion FPS,
    visual-window native FPS, sample-window content-frame FPS, and
    visual-window content-frame FPS; WebKit's prior-FPS gate requires those
    content-frame FPS values to exceed 60 as well. This specifically rejects
    the "looks one digit but reports around 40" class of inflated artifacts.
  - [x] Split compositor-owned visible content from client/app diagnostics in
    the final gates. FPS, `wlcomp_launcher`, and `webkitgpusmoke` now require
    `d3d12_visible_content_crc`, `d3d12_visible_content_frame`,
    `d3d12_visible_frame_hash`, and
    `d3d12_content_progress_source_owned=1`; client-side hashes remain
    diagnostic context and cannot open FPS/WebKit credit.
  - [x] Reject aliased or mixed-record display-bind completion evidence in the
    finite FPS consumers.
    `hyperv-3d-fps-validate.sh` now requires coherent same-line
    `display_bind_backend`, `display_bind_transport`, present id, completed
    id, resource generation, and exact
    `display_bind_completion_source=display` tuples in both sample and visual
    windows. It rejects case aliases such as `DISPLAY`, numeric/enum-style
    aliases, generic `completion_source` without a display-bind tuple, and
    mixed tuples where completion does not cover the same present id. Evidence:
    `FPS_ANTI_INFLATION_SELFTEST=1
    VALIDATION_RUN_ID=selftest-consumer-exact
    scripts/hyperv-3d-fps-validate.sh` passed on 2026-05-26.
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
  The WebKit launcher now reads the same `/tmp/wlcomp-d3d12-present` evidence
  keys as native Mesa clients and emits `webkit_gpu_contract_matrix` even on
  the current fail-closed Hyper-V path. It still refuses acceleration unless
  OpenGL-submit, same-run identity, same adapter, no readback,
  shared-resource/fence evidence, native present completion, callback/release
  ordering, and native-present-complete content progress all pass together.
  - [x] Add a pure-C WebKit in-process consumer gate row so
    `webkitgpusmoke` reports the backend OpenGL-submit bit, display-bind ids,
    native-present ids, resource generation, shared-resource/fence identity,
    compositor-owned content identity, and zero-credit rejection before the
    shell validator decides whether a downstream FPS/WebKit artifact may open.
    The current Hyper-V D3D12 contract-negative validator requires this row to
    keep `backend_opengl_submit=0`, present/completed ids at zero,
    `gate=closed`, and native-present/OpenGL/WebKit credit at zero.
  - [x] Harden WebKit and launcher consumers to require exact provider-owned
    display-bind evidence before any D3D12/WebKit credit.
    `wlcomp_launcher`, `desktop`, and `webkitgpusmoke` no longer normalize
    `host-display-channel`, enum names, numeric completion sources, alternate
    backend names, or transport aliases into the canonical display-bind
    contract. WebKit D3D12 credit now requires real
    `display_bind_backend=gpup_dxg_scanout_bind`,
    `display_bind_transport=gpu-p-dxg-resource-scanout-bind`,
    exact `display_bind_completion_source=display`, nonzero ids, matching
    native/final/content present and completed ids, matching resource
    generation, and backend OpenGL-submit on the same validated contract.
    The compositor helper now routes raw present-source completion checks
    through the stricter provider-complete predicate instead of treating raw
    `present_id/completed` or syncfile/generic completion progress as native
    display-bind proof.
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
  `hyperv-webkit-gpu-validate.sh` now adds
  `webkit_animated_content_native_present_gate_matrix`, which requires the
  animated fixture, content CRC progress, frame-hash progress, same
  client/resource/generation identity, prior native-present FPS contract,
  backend OpenGL-submit, and native present completion. Current Hyper-V keeps
  this gate closed with zero WebKit acceleration credit.
  The validator also deletes stale D3D12/WebKit policy artifacts before the
  liveness-only fixture and requires any generated policy to remain
  `effective_accel=0`, `validated_shared_surface=0`, `d3d12_present=0`, and
  `gpu_contract=none` while native present is absent.
  - [x] Stage a deterministic animated-content fixture at
    `/share/webkit/webkit-animated-content-native-present.html`; the smoke
    probe recognizes its advancing frame title as fixture liveness only.
  - [x] Require canonical final display-bind names in the WebKit validator and
    launcher before any D3D12 WebKit environment is selected:
    `display_bind_backend=gpup_dxg_scanout_bind` and
    `display_bind_transport=gpu-p-dxg-resource-scanout-bind`.
  - [x] Run the animated fixture in the Hyper-V WebKit validator as
    liveness-only evidence while native present is still fail-closed.
    `hyperv-webkit-gpu-validate.sh` emits
    `webkit_animated_content_fixture_liveness_matrix` with title-frame
    progress accepted only as fixture liveness, missing CRC/hash/native
    completion called out explicitly, and zero native-present, OpenGL-submit,
    or WebKit acceleration credit.
  - [x] Tighten the pure-C WebKit smoke contract so content progress requires
    nonzero content CRC, frame counter, frame hash,
    `NATIVE_PRESENT_COMPLETE` content states, and explicit visible/native
    content credits before `webkit_gpu_contract_matrix ok=1`.
  - [x] Replace the fixture-liveness-only check with compositor-owned content
    CRC/frame/hash correlation for the same WebKit run id, client pid,
    resource generation, and native display completion.
    `webkitgpusmoke` now parses and prints compositor run id, client pid,
    client-buffer id, manager resource id, buffer generation, content
    present/completed ids, and content resource generation before allowing
    `ok=1`. `hyperv-webkit-gpu-validate.sh` requires compositor-owned visible
    CRC/frame/hash fields, `d3d12_content_progress_source_owned=1`, current
    WebKit run-id identity, callback/release ordering, and matching native
    present/completion/resource-generation evidence before opening the
    animated-content acceleration gate; title-only fixture liveness remains
    zero-credit.
  - [x] Require final-handoff identity before any WebKit animated-content gate
    can open. The WebKit validator now checks the exact current run id,
    content/display-bind/final-handoff present ids, completed ids, and
    resource generation before emitting an enabled gate; fail-closed Hyper-V
    remains zero-credit.
- [x] Reject WebKit acceleration evidence based only on chrome/cursor/title
  updates, callbacks, releases, render-node presence, dmabuf request,
  environment variables, or software fallback.
  `hyperv-webkit-gpu-validate.sh` now runs a default policy-negative preflight
  that emits `webkit_evidence_rejection_matrix` and proves those evidence
  classes are rejected before any WebKit acceleration artifact is accepted.
  It also requires
  `webkit_stale_display_bind_evidence_rejection_matrix`, so stale or
  after-close display-bind evidence cannot open WebKit acceleration while the
  canonical display-bind ids and native-present credit remain zero.
  - [x] Extend the WebKit policy-negative preflight to reject plausible but
    incomplete lineage: nonzero display/native ids with
    `backend_opengl_submit=0`, stale D3D12 run ids, and stale FPS artifact run
    ids. The downstream WebKit consumer gate now reports separate
    `backend_zero_rejected`, `display_bind_completion_source_rejected`,
    `native_present_ids_zero_rejected`, and `lineage_rejected` fields before
    any enabled artifact can open.
  - [x] Harden the C WebKit evidence readers so policy and D3D12 evidence keys
    are token-boundary and line-aware. `wlcomp_launcher` and
    `webkitgpusmoke` now reject prefix/suffix substring matches, malformed
    numeric tokens, truncating string tokens, and the old permissive backend
    alias that treated `gpu-p-dxg-resource-scanout-bind` as a backend instead
    of the canonical transport.
  - [x] Harden pure-C GPU validator evidence matching so source-audited rows
    cannot pass by substring coincidence. `gpucorevalidate` now requires
    whitespace/line-bounded output tokens while preserving existing `field=`
    prefix probes, and `dxgprobe` parses the `dxg_host_to_vm_last=` line with
    line-scoped fields and whole-token numeric values before emitting
    present-history telemetry/completion matrices.
  - [x] Make shell WebKit downstream validation line-scoped for provider-owned
    display-bind evidence.
    `hyperv-webkit-gpu-validate.sh` now requires one provider-owned
    display-bind record with the exact backend, transport, completion source,
    nonzero ids/resource generation, backend OpenGL-submit, and a matching
    native completion id before open WebKit rows can be emitted. It no longer
    falls back to synthesized native completion ids, and commit-accepted checks
    no longer accept final-handoff-only host-display aliases. Evidence:
    `WEBKIT_GPU_VALIDATE_MODE=preflight-stale-negative
    VALIDATION_RUN_ID=webkit-consumer-exact-stale
    BUILD_DIR=/tmp/xv6-hyperv-build
    scripts/hyperv-webkit-gpu-validate.sh` passed on 2026-05-26.
- [ ] Produce one enabled WebKit artifact only after native present, finite
  480p FPS, backend flag, and shared-surface contract all pass.
  `webkit_enabled_artifact_contract_matrix` now names the only accepted future
  enabled shape: current-run D3D12 display-bind completion, finite 480p FPS,
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT`, shared-resource/fence identity, and
  compositor-owned content CRC/frame/hash identity all from the same validation
  lineage.

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
