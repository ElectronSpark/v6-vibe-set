# Historical GPU, WSL and Hyper-V module map

Historical reference moved from the former GUI runtime entrypoint. Original entrypoint body lines 509–1032.
Read only when investigating the named legacy subsystem or interpreting old
receipts. The preserved text below contains dated status, retired `wlcomp` and
WebKit launch paths, and experimental prescriptions; it is not current runtime
policy or authorization to execute those prescriptions. Current scope and
[the active plan](../../../../docs/active-work-plan.md) take precedence. Confirm
symbols and paths in current source before use; paths below retain their
original repository/submodule context. Do not restore removed compositor,
userland patches, workarounds, or acceleration claims from this history.

Lifetime, readiness, source-authority, current-run identity, fail-closed and
anti-inflation invariants remain useful when the matching path exists. They do
not transfer a Hyper-V native-present contract to virgl or prove completion of
current KDE/browser gates. Current instructions are in [SKILL.md](../SKILL.md).

## Contents

- [Source layout](#source-layout).
- [WSL and Linux reference split](#wsl-and-linux-reference-split).
- [GPU module index](#gpu-module-index).
- [Hyper-V module index](#hyper-v-module-index).

<!-- BEGIN PRESERVED HISTORICAL BODY -->
### Source Layout

- GPU/framebuffer implementation now enters through
  `kernel/dev/fb/module.c`, with owned fragments under `kernel/dev/fb/`.
  Route scanout/fbdev changes to `fb_scanout.c`, BO/GEM/TTM/dmabuf changes
  to `fb_bo_ttm_dmabuf.c`, DRM/KMS changes to `fb_drm_core_kms.c` or
  `fb_kms_atomic.c`, syncobj/PRIME/virtgpu changes to
  `fb_syncobj_prime_virtgpu.c`, Hyper-V present bridge changes to
  `fb_dxg_present.c`, and Nouveau compatibility changes to
  `fb_nouveau.c`.
- Hyper-V implementation now enters through `kernel/dev/hyperv/module.c`, with
  owned fragments under `kernel/dev/hyperv/`. Route VMBus/SynIC work to
  `hyperv_vmbus_core.c`, synthetic input/storage/network/video work to
  `hyperv_synth_devices.c`, vPCI work to `hyperv_vpci_config.c`, DXG
  object/shared-resource lifetime to the `hyperv_dxg_handle_manager.c` /
  `hyperv_dxg_allocations.c` / `hyperv_dxg_shared_objects.c` group, D3DKMT
  ioctl shaping to `hyperv_dxg_ioctls.c`, and DXG status/readiness exports to
  `hyperv_dxg_device.c` or `hyperv_dxg_status_device.c`.

### WSL And Linux Reference Split

- WSL2 `dxgkrnl` is the reference for Hyper-V DXG/D3DKMT object and wire
  behavior. In the Microsoft WSL2 Linux kernel it lives under
  `drivers/hv/dxgkrnl`, with UAPI in `include/uapi/misc/d3dkmthk.h`; do not
  look under `drivers/gpu/dxgkrnl`.
- Use WSL `dxgprocess`, `hmgrtable`, `dxgprocess_adapter`,
  `dxgsharedresource`, `dxgsharedsyncobject`, `dxgsyncfile`, and `dxgvmbus`
  code as the comparison anchors for `/dev/dxg` process lifetime, typed handle
  tables, shared-resource sealing, monitored-fence sync-file behavior, and
  D3DKMT packet layout.
- For `CREATEALLOCATION` and `OPENRESOURCE` unwind paths, keep cleanup packets
  on the same per-open `dxgprocess` host handle as the successful create/open
  packet. A global process handle in `DESTROYALLOCATION` cleanup is a WSL
  parity bug even if the helper usually succeeds on a single-process smoke.
- For `CREATEALLOCATION` and `OPENRESOURCE` late user-publication failures,
  keep a pure-C copyout-fault matrix in `dxgprobe`: force `-EFAULT` after the
  host succeeds, prove same-process `DESTROYALLOCATION`, prove no local handle
  was published by retrying destroy on the returned host handles, and prove
  existing-sysmem active page pins return to the pre-fault count.
  `OPENRESOURCE` should fault `open_alloc_info` before the final args copyout
  so cleanup uses the host-returned resource handle, not the still-zero user
  `req.resource` mirror. Also require parent rollback fields:
  `parent_same=1`, `parent_refs_balanced=1`, `parent_child_unlinked=1`, and
  `sealed_generation_coherent=1`.
- For WSL-like shared resources, treat the explicit resource metadata record
  and per-allocation records as the canonical seal/query/open lifetime model.
  The older flat fields can remain as compatibility mirrors only while
  validators require `shared_model_coherent=1` and `dxg_sharedresource_model`
  reports valid records that match the mirror.
- Preserve WSL-style shared-resource NT/global-share metadata through the
  parent object and each opened child. A display-bind pin may not succeed from
  a by-value clone or a child with stripped sharing state; use
  `dxg_display_bind_pin_diag` to compare typed fd kind, opened child, parent
  id, global share, sealed generation, and pinned refs before trusting the
  future sleepable provider.
- Current shared-resource parent work has a scaffold phase and a semantic
  phase. The scaffold may expose `dxg_sharedresource_parent`, parent id/ref/
  opened-child counters, and sealed allocation `num_pages`/`cached` metadata,
  but it does not close WSL parity while resource fds still deep-clone
  `hvdxg_tracked_resource` state. The semantic phase must introduce a real
  `dxgsharedresource`-style parent whose metadata and children outlive fd
  closes according to refs.
- The semantic parent phase is considered covered only when
  `shared_resource_parent_lifetime_matrix` and
  `shared_resource_sealed_alloc_metadata_matrix` are emitted by `dxgprobe` and
  PASS. Those rows prove fd refs, child refs, parent id stability, close-fd
  survival, and sealed allocation pages/cached/flags stability across
  share/query/open/close.
- For normal DXG object handles, keep the WSL `hmgrtable` shape visible:
  index-addressed lookup, destroyed-entry stale rejection, unique bump on free,
  free-count/head/tail diagnostics, and minimum-free expansion. Do not regress
  this back to unordered linear scans or silent best-effort tracking drops.
- For DXG teardown, keep the WSL order explicit. Device/process cleanup should
  stop the device, drop sync objects locally, handle allocations/resources,
  drop contexts and their HW queues, drop paging queues, then destroy the
  device and finally the process. Explicit destroy ioctls that WSL makes stale
  before host destroy should untrack local handles before sending the host
  packet. `/dev/dxg` must expose `d3dkmt_cleanup_wsl_order` with `valid:1`
  for the final-close validator.
- For WSL-style NT shared-object fds from `LX_DXSHAREOBJECTS`, require one
  object per call, preserve resource-vs-sync fd kind, set close-on-exec on the
  returned fd, and prove copyout-failure cleanup separately from normal close.
- For DXG sync-file parity, be precise: xv6 currently has a custom
  `anon_inode:sync_file` fd that follows the WSL DXG lifecycle shape, not a
  full Linux `sync_file`/`dma_fence`. Keep validators for create-copyout
  fd/event cleanup, open-copyout host sync-object destruction, temporary
  `WAITSYNCFILE` sync-object destruction, child-process open from the same fd,
  and `dxg_syncfile_lifetime` host-event/live-count balance.
- The top-level Hyper-V DXG validator must consume WSL lifetime rows, not just
  leave them as optional probe output: `dxg_process_mem_lifetime_matrix`,
  `dxgprocess_adapter_matrix`, `handle_lifetime_stale_matrix`,
  `shared_resource_parent_lifetime_matrix`,
  `shared_resource_sealed_alloc_metadata_matrix`, `sync_file_matrix`, and both
  `dxg_syncfile_*_unwind_matrix` rows.
- For native Wayland/D3D12 fence acquire, the chosen contract is WSL-style DXG
  sync-file acquire, not direct D3D12 fence fd import. `d3d12sharedsmoke`
  should emit `d3d12_fence_sharing_policy_matrix` and
  `d3d12_fence_sharing_validation_matrix` with
  `decision=dxg_syncfile_acquire`, same-adapter WSL trace provenance, direct
  D3D12 fence fd use disabled, and zero native-present/OpenGL-submit credit
  until the real display handoff exists.
- For runtime D3D12 Wayland resource-buffer admission, distinguish the shared
  fd's canonical creator-side resource handles from the compositor's per-open
  `OPENRESOURCEFROMNTHANDLE` handles. FB present-source registration should
  validate the compositor `/dev/dxg` owner table entry against the shared fd's
  global share and sealed metadata generation; it should not require the fd's
  stored creator handles to equal the compositor-opened resource/allocation.
  The focused runtime C gate is
  `d3d12sharedsmoke --runtime --allow-failclosed-present`: it must prove real
  `LX_DXCREATESYNCFILE` export, `LX_DXOPENSYNCOBJECTFROMSYNCFILE` import,
  same-LUID compositor resource/fence open,
  `d3d12_wayland_resource_buffer_admission_matrix`,
  per-open present-source register success, expected fail-closed
  `EOPNOTSUPP` at the missing `dxg-resource-scanout-bind` host ABI, drained
  callbacks/releases, and zero native-present/OpenGL-submit credit.
- For `dxgprocess` lifetime, keep reuse keyed by TGID like WSL. Do not reuse a
  retained host process handle across TGIDs; if a host workaround is ever
  necessary, expose it as non-parity diagnostics instead of sharing namespaces.
- For D3DKMT ioctls, keep the TGID ownership gate in the common `/dev/dxg`
  dispatch before per-open host-process binding, adapter alias creation, user
  copyout, or packet forwarding. Discovery/bind ioctls (`ENUMADAPTERS*`,
  `OPENADAPTERFROMLUID`, and `QUERYADAPTERINFO`) still bind in their own
  WSL-order paths, but inherited fds from a different TGID must fail before
  those paths can mint local aliases or query adapter data.
- For WSL packet-shape parity, keep the source comparison tight. WSL leaves
  `CREATEDEVICE.cdd_device` zeroed, sizes `MAKERESIDENT` as the base command
  plus allocation handles with no extra local tail dword, and forwards VGPU
  D3DKMT packets with the owning `dxgprocess` host handle. If a path cannot
  yet match WSL CPU-event signal or async-message semantics, make it an
  explicit active plan item with validator evidence instead of burying it in
  generic unsupported logging.
- For same-adapter WSL replay coverage, keep `dxgprobe --wsl-trace-replay`
  tied to the selected OPENADAPTER LUID and the current NVIDIA trace reference.
  The section is not covered unless `wsl_trace_replay_packet_matrix` rows prove
  each replayed D3DKMT packet shape and `wsl_trace_replay_signature` reports
  `status=PASS`.
- For shared-resource/shared-sync regression coverage, treat the focused core
  runner as the index: `shared_resource_seal_provenance_matrix`,
  `shared_mutation_rejection_matrix`, `shared_lifetime_record_matrix`,
  `resource_import_negative_matrix`, `opensync_layout_source_matrix`,
  `sync_import_negative_matrix`, `sync_file_matrix`, and the sync-file unwind
  rows must all stay green before editing native present or WebKit gates.
- For create-path publication faults, keep host cleanup before local handle
  publication WSL-shaped. `dxgprobe --create-publication-faults-validate`
  should fault the final result page for `CREATEDEVICE`,
  `CREATECONTEXTVIRTUAL`, and `CREATEHWQUEUE`, then require the same owning
  host process handle to destroy the host-created object and require stale
  local destroy retries to fail for the unpublished handles.
- For WSL `enqueue_cpu_event` signal paths, `SIGNALSYNCHRONIZATIONOBJECT` and
  `SIGNALSYNCHRONIZATIONOBJECTFROMGPU2` should allocate an eventfd-backed host
  event, send that host-event id in the VMBus `cpu_event_handle`, keep
  remove-after-signal ownership with the host-event table, and remove/fput on
  send failure. Validators should require `sync_signal_cpu_event_matrix`,
  `sync_gpu2_cpu_event_matrix`, and `dxg_synccpuevent_signal`, not only the
  absence of `-ENOTSUP`.
- For WSL `hdr.async_msg` parity, keep the command family exact:
  `SUBMITCOMMAND`, `SIGNALSYNCOBJECT`, `WAITFORSYNCOBJECTFROMGPU`, and
  `SUBMITCOMMANDTOHWQUEUE` can use `dxgvmb_send_async_msg()` when the host
  advertises async messages; `WAITFORSYNCOBJECTFROMCPU` remains synchronous.
  xv6 should expose both the send decision and the packet shape through
  `dxg_async_message_matrix` and `dxg_async_send_last`, with sync fallback
  reported explicitly when the host capability is absent.
- For broad packet-marshalling closure, prefer one aggregate pure-C matrix over
  loose status rows. `dxg_packet_shape_matrix` should prove command/result
  lengths, owner process handles, private blob order, first resource/sync/HWQ
  handles, async or sync-fallback send policy, and create-publication unwind
  counters before the packet-shape plan row is checked.
- For NT shared-object import coverage, validate both directions of fd kind
  separation: resource query/open must reject sync fds, and sync open must
  reject resource fds. Keep this as a pure-C `dxgprobe --import-negative`
  contract before using any shared handle as native-present evidence.
- For NT shared-object fd publication, prove the WSL-style copyout failure
  path separately from ordinary close: a bad `shared_handle` pointer must
  deallocate the just-installed fd, run last-fd close cleanup, drop the
  NT shared-object ref to zero, and still allow a subsequent valid share of the
  same resource or sync object.
- WSL `dxgkrnl` is not the DRM/KMS/Nouveau reference. Use Linux DRM, GEM, TTM,
  `dma_fence`, `dma_resv`, KMS atomic, PCI runtime, and Nouveau sources for
  `/dev/dri`, PRIME/dma-buf, scanout, and Nouveau compatibility work.
- Keep these tracks separate in plans and validators: WSL parity can close DXG
  transport/object gates, but it cannot by itself prove native display handoff,
  KMS scanout, Nouveau command submission, FPS, WebKit acceleration, or
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT`.

#### GPU Module Index

- `kernel/dev/fb/module.c`: unity root for the framebuffer/GPU module.
- `kernel/dev/fb/fb_common.c`: common includes shared by x86 and stubs.
- `kernel/dev/fb/fb_internal.c`: GPU constants, shared state, boot logo, and
  internal prototypes.
- `kernel/dev/fb/fb_scanout.c`: framebuffer scanout, blit, scanout map, flush,
  and display-completion accounting.
- `kernel/dev/fb/fb_bo_ttm_dmabuf.c`: BO/GEM objects, TTM placement,
  reservations, and dma-buf metadata/lifetime.
- `kernel/dev/fb/fb_dxg_present.c`: Hyper-V DXG present-source registration,
  commit, query, fail-closed native-present diagnostics, and the
  `FB_GPU_DXG_PRESENT_BIND_CONTRACT_QUERY` skeleton that ties native handoff to
  a registered source, source/resource generations, required metadata, selected
  GPU-P/DDA lane, and display-completion source.
  Verified sources pin their owning `/dev/dxg` fd plus typed
  `anon_inode:dxgresource` fd at registration time, then unpin on source
  unregister/owner close so future sleepable display-bind submissions cannot
  race fd close/reuse or process/resource lifetime.
  The selected lane is GPU-P/DDA `dxg-resource-scanout-bind`, with WSLg display
  channel unavailable, synthvid limited to GPA-dirty VRAM, no custom host tool,
  and `dxg_present_lane_selection_matrix` as the fbstat evidence row.
  `present_source_software_path_rejection_matrix` is the pure-C zero-credit
  proof that framebuffer blit, CPU map/readback, DRI software present,
  copy-export fallback, and callback-only/release-only paths cannot satisfy
  this native-present contract while the real display-bind lane is missing.
  `d3d12_shared_resource_fd_lifetime_matrix`,
  `d3d12_present_source_admission_matrix`,
  `d3d12_acquire_fence_lifetime_matrix`, and
  `d3d12_present_bind_contract_failclosed_matrix` are the current pure-C
  fail-closed D3D12 resource/fence/present-source validators. They prove fd
  lifetime, same-adapter admission, D3DKMT metadata, monitored-fence acquire
  metadata, stale/foreign source rejection, software-path rejection, cleanup
  balance, and zero native-present/OpenGL-submit credit; they do not prove
  native display completion.
  `d3d12_wayland_resource_buffer_admission_matrix` is compositor-side
  intermediate evidence that the Wayland D3D12 buffer path accepted a
  same-LUID resource/fence import. `d3d12_wayland_present_failclosed_identity_matrix`
  records the same client/resource/generation when GPU-copy proof exists but
  native display completion remains absent. Both rows are diagnostic and must
  be rejected by strict DXG/FPS/WebKit gates until nonzero present/completion,
  callbacks, releases, content progress, and backend OpenGL-submit all pass.
- `kernel/dev/fb/fb_fd_sync.c`: exported BO/fence/sync fd file operations,
  poll, close, and callback lifecycle.
- `kernel/dev/fb/fb_device_ioctl.c`: `/dev/fb0` and `/dev/gpu0` ownership,
  open/close, read/write, and `FB_GPU_*` ioctl dispatch.
- `kernel/dev/fb/fb_drm_core_kms.c`: DRM core helpers, legacy ioctls, KMS
  resources, properties, planes, CRTC, connector, and framebuffer metadata.
- `kernel/dev/fb/fb_kms_atomic.c`: KMS framebuffer lifecycle, leases,
  modeset, page-flip, vblank, and atomic commit/fence behavior.
  Atomic `OUT_FENCE_PTR` validation should use
  `atomic_out_fence_provenance_matrix out_fence_source=display_completion`
  with `out_fence_display_correlated=1`,
  `out_fence_software_scanout_correlated=0`, and zero native/OpenGL-submit
  credit. `IN_FENCE_FD` validation must still prove stale, duplicate, future,
  and nonblocking rejection plus balanced fd refs.
- `kernel/dev/fb/fb_syncobj_prime_virtgpu.c`: DRM syncobj/timeline,
  sync-file bridge, dumb BO, PRIME, and virtgpu compatibility ioctls.
- `kernel/dev/fb/fb_nouveau.c`: Nouveau PCI facade and Nouveau private DRM
  ioctl compatibility.
- `kernel/dev/fb/fb_drm_dispatch.c`: DRM ioctl switch, render-node file ops,
  and DRM mmap.
- `kernel/dev/fb/fb_init_panic.c`: GPU device registration, framebuffer init,
  firmware framebuffer setup, and panic screen renderer.
- `kernel/dev/fb/fb_non_x86.c`: non-x86 framebuffer/GPU stubs.

#### Hyper-V Module Index

- `kernel/dev/hyperv/module.c`: unity root for the Hyper-V module.
- `kernel/dev/hyperv/hyperv_common.c`: common includes shared by x86 and
  stubs.
- `kernel/dev/hyperv/hyperv_defs_state.c`: Hyper-V/VMBus/DXG constants, wire
  structs, and shared per-channel/device state.
- `kernel/dev/hyperv/hyperv_vpci_config.c`: Hyper-V vPCI config-window backend
  and bus-relations parsing.
- `kernel/dev/hyperv/hyperv_dxg_state.c`: shared `hvdxg` state struct and
  cross-file forward declarations.
- `kernel/dev/hyperv/hyperv_dxg_pci_version.c`: cmdline host LUID, vmbus version
  negotiation, and PCI guestcaps discovery.
- `kernel/dev/hyperv/hyperv_dxg_queryadapter_hwid.c`: QueryAdapter admission and
  adapter hardware-id resolution.
- `kernel/dev/hyperv/hyperv_dxg_diag.c`: WSL parity diagnostics, payload caches,
  d3d12 capture, and status text helpers.
- `kernel/dev/hyperv/hyperv_dxg_status_device.c`: `/dev/dxg` status read path,
  IO-space/MMIO helpers, and existing-sysmem mapping helpers.
- `kernel/dev/hyperv/hyperv_dxg_memory.c`: iospace mapping, sysmem pinning, and
  u32 table helpers.
- `kernel/dev/hyperv/hyperv_dxg_handle_manager.c`: DXG process/object/handle and
  object/sync/process tracking.
- `kernel/dev/hyperv/hyperv_dxg_allocations.c`: allocation/resource/sync
  tracking, queues, and resource linking.
- `kernel/dev/hyperv/hyperv_dxg_shared_objects.c`: NT shared resource/sync fds
  and shared-object operations.
- `kernel/dev/hyperv/hyperv_dxg_ioctl_queryadapter.c`: QueryAdapterInfo ioctl and
  its helpers.
- `kernel/dev/hyperv/hyperv_dxg_ioctls.c`: D3DKMT ioctl validation,
  ownership checks, packet shaping, forwarding, and completion handling.
- `kernel/dev/hyperv/hyperv_dxg_device.c`: DXG cdev/file operations and public
  transport/D3DKMT readiness exports.
- `kernel/dev/hyperv/hyperv_vmbus_core.c`: Hyper-V CPUID/MSR, SynIC, VMBus
  ring buffers, packets, events, completions, and DXG send/wait helpers.
- `kernel/dev/hyperv/hyperv_synth_devices.c`: synthetic HID/keyboard,
  StorVSC, NetVSC, SynthVid, and vPCI channel helpers.
- `kernel/dev/hyperv/hyperv_init_public.c`: Hyper-V channel opening, public
  init entry points, video status/dirty APIs, and startup sequencing.
- `kernel/dev/hyperv/hyperv_non_x86.c`: non-x86 Hyper-V stubs.

- Hyper-V DXG has a usable transport and D3DKMT readiness lane: `/dev/dxg`
  exposes global/vGPU transports, adapter enumeration/open works, and
  `fbstat`/`FB_GPU_BACKEND_QUERY` distinguish `DXG_TRANSPORT`, `D3DKMT`, and
  `OPENGL_SUBMIT`.
- Stable `dxgprobe` coverage includes adapter query, video memory query, device
  creation, paging queue creation, allocation create/destroy, residency/evict,
  GPUVA map/reserve/free, allocation priority/offer/reclaim/cache invalidate,
  CPU monitored-fence signal, shared resource NT fd query/open, shared sync NT
  fd open, owner isolation, leak-close cleanup, and unsupported ioctl handling.
- Hyper-V DXG allocation handling now includes existing-sysmem page pinning,
  PFN-list forwarding, cleanup-time unpinning, and late-failure unwind for host
  allocations when post-host-create local copyout/tracking fails.
- `/dev/dxg` per-open tracking is growable for devices, contexts, HW queues,
  paging queues, sync objects, allocations, resources, and GPUVA reservations.
  This is a baseline improvement, not the final WSL-style `dxgprocess` object
  graph.
- The general render substrate has `/dev/dri/renderD128`, libdrm/GBM discovery,
  PRIME-style BO fd export/import, render-fd ownership cleanup, pollable fence
  fd accounting, and no-leak validation through `gpubuftest`, `gbmtest`,
  `drmprimeprobe`, and `drmgpuprobe`.
- On GPU-P-only Hyper-V images, Nouveau must remain fail-closed unless a real
  BAR-backed DDA NVIDIA PCI function is accepted. `fbstat` should emit
  `nouveau_gpup_failclosed_matrix` with no fake BAR/DMA/IRQ/getparam/native
  present/OpenGL-submit credit. Accepted DDA PCI facts are still only
  diagnostics for native display until Linux-shaped display create, nonvirtual
  heads/connectors, hardware vblank IRQ, KMS `NOUVEAU_HW` page flip, and
  hardware flip completion all correlate in the same validation lineage.
- `nouveau_pci_runtime_contract_matrix` is the Linux-shaped PCI runtime
  diagnostic row. On GPU-P-only images it should pass with DMA/coherent masks
  not configured, BAR claims not attempted, no IRQ handler or delivery,
  runtime PM/remove-path deferred, and zero native-present/OpenGL-submit credit.
  On accepted DDA hardware it is still diagnostic until real MSI/legacy IRQ
  delivery, runtime PM, remove, and engine/native-present behavior are proven.
- `nouveau_pci_runtime_interface_matrix` keeps the same split at interface
  granularity. On GPU-P-only Hyper-V it should report resource tree and DMA
  mapping as `GPU_P_FAIL_CLOSED`, MSI/MSI-X not attempted, IRQ absent,
  runtime PM/remove/hot-remove deferred, native engine absent, and zero
  native-present/OpenGL-submit credit. A DDA/Nouveau device can only move this
  row forward after real BAR, DMA, IRQ, runtime-PM, engine, and present
  evidence exists.
- For Nouveau/PCI work on GPU-P-only Hyper-V, keep `accepts=0` and expose no
  fake BAR, DMA, IRQ, or native engine state. A real DDA path must claim BARs
  before `pci_iomap()`, report owner/unclaimed resource counters, arm a real
  IRQ handler before accepting the device, and count IRQ delivery only from
  handler execution.
- `dxg_resource_scanout_bind_host_abi_matrix` is the source-audited native
  present blocker row. It must say the selected lane is
  `gpup_dxg_scanout_bind`, no custom host tool is used, WSL dxgkrnl has no
  display-bind ioctl, synthvid is GPA-dirty-only, and D3DKMT shared-resource
  admission still grants zero display target, present id, native-present credit,
  or OpenGL-submit credit.
- `wsl_standard_alloc_surface_abi_matrix` proves only WSL VMBus private-data
  layout parity for `DDIGETSTANDARDALLOCATIONDRIVERDATA`: the shared-primary,
  shadow, staging, and GDI surface union arms are present. This is not a
  native display handoff and must keep `display_bind_ioctl=0` plus zero
  native-present/OpenGL-submit credit.
- `d3d12_present_resource_fd_typed_admission_matrix` is the FB-to-DXG
  admission guard before native present. It should prove the resource fd is a
  typed WSL-style `anon_inode:dxgresource`, sealed shared-resource records are
  visible, the fd metadata matches the D3DKMT handles/allocation count, the
  bind-contract resource generation comes from the sealed resource generation,
  stale source cleanup works, and native-present/OpenGL-submit credit stays
  zero.
- `d3d12_display_bind_pin_lifetime_matrix` is the WSL-style lifetime guard
  for the future sleepable display-bind provider. It should prove verified
  sources pin both the `/dev/dxg` owner fd and typed resource fd, carry nonzero
  resource/process generations, process refs, parent id/ref/opened-child
  counters, balance unpins after source cleanup, and keep native-present/
  OpenGL-submit credit at zero.
- `d3d12_present_syncfile_preopen_matrix` is the acquire-fence bridge guard
  before native present. It should prove a monitored fence can be exported as
  a WSL-style sync-file fd, reopened into a D3DKMT sync object, used for
  present-source wait metadata, and rejected for wrong fd kinds, while still
  granting zero native-present/OpenGL-submit credit.
- `d3d12_native_completion_zero_credit_matrix` and
  `d3d12_display_bind_absent_matrix` are the pre-native completion guards.
  They must show absent display bind/transport, zero present/completion ids,
  blocked or deferred callback/release ordering, required per-client
  generation matching, and zero native-present/OpenGL-submit credit.
- `d3d12_native_completion_lifetime_matrix` is the order/lifetime guard. It
  may pass preflight before any provider submit, but after a fail-closed
  provider submit it must show `provider_no_completion=1`; after a future
  successful provider submit it must require nonzero display-correlated
  present/completed ids, callback/release after completion, close-before-signal
  cancellation, cleanup balance, and no provider negative diagnostics.
- `d3d12_native_completion_consumer_escrow_matrix` is the downstream credit
  guard. While display bind is closed it must keep callback/release credit,
  final-handoff credit, FPS-visible credit, content-progress credit, and WebKit
  acceleration credit at zero, with `host_saw_display_bind_packet=0`, no
  completion demux, no transport-pending id, and no native-present/
  OpenGL-submit credit. A future positive path needs the provider-owned
  display completion first; consumers cannot create completion authority.
- `d3d12_display_bind_stale_source_zero_credit_matrix` is the stale-source
  guard. Owner close or explicit unregister must clear source/global
  display-bind ids, reject after-close queries, avoid late completion credit,
  and keep native-present/OpenGL/WebKit credit at zero.
- `d3d12_display_bind_stale_async_completion_contract_matrix` is the stricter
  real-sender future guard. It keeps completion-demux, transport-pending-id,
  owner-close-cancel, and late-completion-reject semantics visible while
  explicitly labelling today's evidence as no-sender fail-closed rather than a
  sampled post-send cancellation.
- `dxg_host_to_vm_presenthistory_completion_matrix` must treat present-history
  host-to-VM packets as telemetry even if they are observed. Do not require
  packet absence for the row to pass; require zero sender/completion
  contracts, zero display-bind completion successes, zero present/completed
  ids, and zero native-present/OpenGL-submit credit.
- `d3d12_dda_nouveau_separate_display_not_bind_matrix` is the DDA split guard.
  A separate DDA/Nouveau PCI display path is zero-credit for D3D12 native
  present because it is not the D3D12 resource scanout-bind path. Keep it in a
  native-display namespace unless a future source documents an explicit bridge
  from a D3D12 resource generation into the Nouveau display engine.
- `dda_nouveau_d3d12_bridge_disjoint_matrix` is the bridge-specific DDA split
  guard. It must keep DDA D3D12 import, DDA D3D12 scanout bind, D3D12 hardware
  flip completion, KMS-as-D3D12 lane credit, D3D12 display-bind ids,
  native-present credit, and OpenGL-submit credit at zero while DDA/Nouveau is
  only a separate PCI/KMS display diagnostic path.
- `foreign_prime_import_gap_matrix` is the DDA/Nouveau PRIME bridge guard.
  Local xv6 dma-buf/PRIME imports are not a D3D12 foreign-resource import or a
  Nouveau scanout-bind handoff; valid foreign fd rejects, zero D3D12 import
  credit, zero Nouveau scanout-bind import credit, and zero native/OpenGL credit
  must stay visible until a real Linux-shaped external import bridge exists.
- `public_present_api_not_guest_bind_matrix` is the public Windows/WSLg/RDP
  source-audit guard. ReactOS/Windows KMT present declarations, DirectX shared
  handles, and HWND-oriented sharing-contract APIs are not a guest VMBus or
  WSLg/RDP display-bind protocol; RDP-style frame/copy/dirty transport remains
  zero-credit for native D3D12 present until a source/resource-correlated
  scanout completion contract exists.
- `d3d12_display_bind_host_abi_discovery_matrix` is the bounded host-ABI
  source-audit gate. It must prove no custom host tool, no WSL display-bind
  ioctl, WSLg/FreeRDP absent, RDP copy/dirty-frame only, hv_sock display-bind
  service absent, GPU-P sender contract zero, completion-demux contract zero,
  DDA/Nouveau D3D12 import/scanout/hardware flip absent, provider
  fail-closed, transport/present/completed ids zero, and
  native-present/OpenGL/WebKit credit zero.
- `d3d12_negative_abi_manifest_matrix` is the canonical source-audited
  negative ABI manifest. It must keep WSL `d3dkmthk.h`/`dxgvmbus.c`
  display-bind absent, hv_sock display-bind service absent,
  present-history/redirected-flip/BLT/HWQUEUE enum candidates classified as
  telemetry or normal submit rather than scanout bind, synthvid classified as
  GPA dirty-rect display, DDA/Nouveau classified as a separate PCI display
  path, and host packet/completion/native-present credit at zero.
- `d3d12_display_bind_authority_chain_matrix` is the ordered authority guard.
  It must show host ABI, provider send, host packet, provider completion demux,
  display completion, resource generation, and consumer credit as one chain.
  Until a real sender exists, only the source/resource generation gates may be
  armed; host ABI, provider send, packet, demux, completion, and consumer
  credit must stay closed with zero WSL present-history, KMS, sync-file,
  DDA-native-display, native-present, OpenGL-submit, and WebKit credit.
- `nouveau_kms_acceptance_shape_matrix` and
  `nouveau_dda_display_positive_shape_matrix` keep DDA/Nouveau display work
  Linux-shaped without granting D3D12 credit. The KMS native-present gate must
  require display engine, mode_config, CRTCs/encoders/primary planes,
  outp/connector/head masks, NVIF head construction, non-virtual connector,
  HPD/DP IRQ events, per-head vblank IRQ source, atomic commit tail, hardware
  page-flip completion, linear scanout policy, and no unvalidated nonlinear
  modifiers before any DDA display credit is accepted.
- For sampled fail-closed provider submits,
  `d3d12_display_bind_provider_pending_publication_matrix` should show the
  no-host-ABI pending path resolved and refs released
  (`resolved_or_cancelled=1`, `refs_released=1`,
  `no_host_abi_cancelled=1`, `no_host_abi_refs_released=1`) while sender-owned
  publish-before-send, command/channel, completion demux, native-present, and
  OpenGL-submit fields remain zero.
- The matching `d3d12_display_bind_provider_no_send_preflight_matrix` should
  prove the same sample was ready to send but intentionally performed no send:
  `preflight_ready=1`, `send_attempts=0`,
  `send_blocked_no_host_abi>0`, `completion_demux_attempts=0`, and
  `completion_demux_blocked_no_contract>0`, with source/resource generation
  matches and zero host-saw packet/native-present/OpenGL-submit credit.
- `d3d12_display_bind_backend_boundary_matrix` is the canonical boundary row.
  It should mirror kernel `dxg_display_bind_*` stats and keep the current
  GPU-P-only path at `backend=gpup_dxg_scanout_bind`, transport absent,
  completion source required, present/completed ids zero, custom host tooling
  zero, and zero native-present/OpenGL-submit credit.
- `hyperv_opengl_submit_gate_matrix` is the backend flag invariant. On Hyper-V
  it must remain `backend_gate=closed` with `backend_opengl_submit=0` until
  native present, finite FPS, and the WebKit shared-surface contract are all
  proven from current-run evidence.
- For DDA/Nouveau `GETPARAM`, keep provenance split: PCI vendor/device,
  bus type, BAR/VRAM aperture, chipset, and VRAM base are DDA PCI facts;
  `HAS_BO_USAGE`, `HAS_PAGEFLIP`, `EXEC_PUSH_MAX`, `VRAM_USED`, and
  `HAS_VMA_TILEMODE` are local driver capabilities; unsupported engine/timer
  facts fail closed until sourced. `nouveau_getparam_ddafacts_matrix` must
  report zero synthetic hardware facts before this plan row is considered
  closed.
- Legacy Nouveau channel work is tracked separately from NVIF. The old
  `CHANNEL_ALLOC`/`GROBJ_ALLOC`/`NOTIFIEROBJ_ALLOC`/`GPUOBJ_FREE` ioctls should
  maintain per-open channel/object state, reject duplicates and unsupported
  classes, and report `nouveau_channel_object_matrix`. Publish channel/VM state
  only after successful ioctl copyout, so `-EFAULT` cannot leave active state
  behind. Mesa's newer NVIF object/subchannel path is still governed by the
  active NVIF plan row.
- NVIF support must not advertise made-up engine classes. Until the Nouveau
  class hierarchy is real, `DRM_NOUVEAU_NVIF` should parse v0 SCLASS/NEW/DEL
  and method/register/map/notify operations, return an empty SCLASS list, reject
  NEW and unsupported operations explicitly, and report
  `nouveau_nvif_failclosed_matrix`.
- Non-empty Nouveau submission remains fail-closed until a DDA command engine
  exists. Keep zero-op/fence-only `PUSHBUF`, `EXEC`, and `VM_BIND` separate
  from non-empty command buffers; `nouveau_submit_failclosed_matrix` should show
  non-empty pushbuf/exec/vm-bind rejects and zero native-present/OpenGL-submit
  credit.
- `nouveauabitest` is the Mesa/libdrm Nouveau smoke gate. DDA runs must reach
  libdrm winsys/device-info plus channel/BO/map/PRIME paths; GPU-P-only runs
  may pass only with `nouveau_mesa_smoke_gate_matrix` showing
  `synthetic_gpup_rejected=PASS` and no Mesa NVIF enablement.
- Wayland/compositor baseline includes standard `zwp_linux_dmabuf_v1` import for
  linear ARGB8888/XRGB8888/NV12, dmabuf feedback, explicit-sync release objects,
  acquire-fence waits with stall recovery, GPU BO present/direct scanout for
  framebuffer BOs, display completion accounting, and screenshot visual checks.
- Generic framebuffer/DRM/KMS diagnostics must stay distinct from
  D3D12/WebKit contract gates. Kernel ioctl trace output should use
  `fb-gpu-trace`; `fbstat` should emit `gpu_diagnostics_separation_matrix`
  with generic DRM/KMS/fb prefixes, DXG-present/WebKit policy as separate
  namespaces, and zero native-present/OpenGL-submit credit from generic
  scanout evidence alone.
- KVM/virtio-gpu/virgl is the current validated OpenGL-submit backend. It owns
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT` today; Hyper-V does not.
  `scripts/gpu-validate.sh` should prove that with `backend virgl`,
  `backend_opengl_submit 1`, `backend_opengl_submit_gate open`,
  `backend_virgl_opengl 1`, and
  `opengl_submit_backend_separation_matrix ... opengl_submit_credit=1`.
- The desktop 3D demo now launches through `mesademo`/`mesawlegl --demo` with a
  real 640x480 Wayland/EGL window, close/resize handling, and an RTC-based FPS
  overlay drawn inside the GL surface.
- WebKit acceleration is intentionally gated on `FB_GPU_BACKEND_F_OPENGL_SUBMIT`.
  Hyper-V render-node or D3DKMT presence alone must keep WebKit on the stable
  fallback path.
  `wlcomp_launcher` must compare the generated WebKit run id with both
  `d3d12_run_id` and `d3d12_present_identity_compositor_run_id` before it may
  select the D3D12 WebKit environment; `webkit_gpu_contract_matrix` is the
  launcher-side current-run gate row.
