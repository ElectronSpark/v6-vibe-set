## Validated GPU Baselines

These items were retired from `GPU_REMAINING_GAPS.md` after the May 17, 2026
source audit. Re-check current source and validation logs before changing them,
but do not treat them as open plan items by default.

### Native Present Dependency Tree

- Treat Hyper-V D3D12 acceleration as a dependency chain, not as independent
  green checks: shared-resource import, sync-file acquire, compositor GPU copy,
  DXG present-source commit, display completion, visible-content/FPS credit,
  backend OpenGL-submit, then WebKit acceleration.
- `dxg-resource-scanout-bind` or an equivalent GPU-P/DDA display-bind transport
  is the root missing piece. Until it returns a nonzero present id and display
  completion for the same resource generation, callbacks/releases may be
  drained only as fail-closed lifecycle cleanup, not as native-present credit.
- Keep the rejected native-present lanes explicit in validator output. WSL
  present-history guest-to-host command IDs and the host-to-VM
  `PROPAGATEPRESENTHISTORYTOKEN` completion enum are only candidates without
  sender/completion contracts. Synthvid GPA dirty rectangles, Linux Hyper-V DRM
  shadow blits, and a separate DDA/Nouveau PCI display path are also zero-credit
  until one of them proves a real D3D12 resource-to-display completion path.
- Keep the WSL present-history distinction kernel-owned: VMBus command enum
  IDs are not Linux ioctls, and the stats row must report no sender,
  resource-bind, or display-completion contract before any native-present
  credit can be considered. `/dev/dxg` exposes `dxg_host_to_vm_last`, including
  `PROPAGATEPRESENTHISTORYTOKEN` count, length, command id, and payload-head
  bytes; keep `dxg_host_to_vm_presenthistory_completion_matrix` zero-credit
  until a real sender and matching host-to-VM completion payload are proven.
  `dxg_presenthistory_telemetry_not_completion_matrix` should stay green with
  `linux_inband_handler=absent`; present-history telemetry is not a
  display-bind completion contract.
  `dxg_presenthistory_orphan_completion_rejection_matrix` is the stricter
  downstream guard: even if present-history packets are observed, they are
  zero-credit unless they match a provider pending record and completion demux.
  Today that row must report `provider_pending_match=0`,
  `completion_demux_registered=0`, zero display-bind ids, and zero
  native-present/OpenGL-submit credit.
- Keep the fail-closed proof scalarized: WSL ioctl namespace checked,
  display-bind ioctl absent, standard allocations classified as private driver
  data rather than scanout binding, synthvid limited to GPA dirty rectangles,
  and DDA/Nouveau PCI display split from D3D12 resource import, scanout bind,
  and hardware flip completion.
  Keep `wsl_dxg_ioctl_namespace_probe_matrix` beside the source-audited WSL
  namespace rows: wrong ioctl type, wrong size, wrong direction, first
  post-WSL command `0x4a`, and a high future command such as `0x7f` must all
  fail and leave Linux ioctl/resource-bind/completion contracts plus
  native-present/OpenGL-submit credit at zero.
  Provider pending publication evidence must preserve WSL-shaped provenance
  without overstating retained krefs: `dxgprocess_generation`,
  `process_adapter_generation`, `hmgr_index_unique_valid`,
  `device_object_ref_active`, `resource_object_ref_active`,
  `allocation_object_ref_active`, `shared_parent_snapshot_valid`,
  `opened_child_snapshot_valid`, `syncobject_object_ref_active`, and
  `owner_close_cancelled=0` while fail-closed.
  Sender/source classification is also explicit zero-credit evidence while the
  provider is fail-closed: require `host_saw_display_bind_packet=0`,
  `display_bind_transport_source=none`, and
  `wsl_presenthistory_completion_credit=0` in the source catalog, host-ABI
  discovery, query-fields, and provider-pending rows. Future positive credit
  needs a non-WSL/DDA transport source, `host_saw_display_bind_packet=1`,
  provider completion demux, and source/resource-correlated display
  completion; WSL present-history telemetry remains zero-credit.
  Keep the WSL HMGR/process-scope diagnostics beside provider-pending work:
  `dxg_hmgr_type_coverage_matrix`, `dxg_object_table_type_counts_matrix`,
  `dxg_hmgr_entry_lifecycle_matrix`, `dxg_hmgr_pending_validity_matrix`,
  `dxg_object_table_scope_matrix`, `dxg_process_identity_matrix`, and
  `dxg_process_adapter_device_counts_matrix` must name missing WSL HMGR types,
  process-scoped object tables, pid/tgid identity, absent xv6 `vpid`/`nspid`
  namespace support, and zero native-present/OpenGL-submit credit.
- When planning the remaining native-present work, keep the chunks ordered:
  host ABI discovery/proof, kernel scanout-bind path, compositor handoff,
  native completion/lifetime, then FPS/backend/WebKit credit. Do not split
  those into independent pass claims; each later chunk depends on nonzero
  display-correlated completion from the earlier source/resource generation.
- Keep the remaining GPU plan wired through
  `gpu_remaining_plan_dependency_skeleton_matrix`: this row ties the root
  display-bind gate, native completion validators, finite 480p gate, demo
  interaction, Hyper-V OpenGL-submit, KVM/virgl recheck, WebKit route/content,
  and final WebKit artifact together. It should stay green only while WSL has
  no Linux display-bind ioctl or in-band present-history completion handler,
  GPU-P sender/completion contracts are zero, DDA/Nouveau import/scanout-bind/
  hardware-flip completion are absent, and native-present/OpenGL/WebKit credit
  is zero. The pure-C row must also spell out the granular closed gates:
  `real_display_bind_sender=0`, `real_display_bind_completion=0`,
  `native_completion_validator_gate=closed`, `finite_480p_gate=closed`,
  `backend_opengl_submit_gate=closed`, `webkit_enabled_artifact_gate=closed`,
  and `DDA/Nouveau-separate-display-not-D3D12-bind`.
  `gpu_remaining_holistic_skeleton_matrix` is the same dependency graph in
  execution order. It names the active open plan count, ordered chunks, selected
  lane, completion authority, and all downstream gates so future edits cannot
  open FPS, backend, or WebKit credit before display-bind completion exists.
  Keep its source-audited proof rows nearby:
  `wsl_dxg_uapi_namespace_negative_matrix`,
  `wsl_dxg_adapter_display_caps_negative_matrix`, and
  `dda_nouveau_non_readback_display_proof_matrix`, plus
  `wsl_submit_present_fields_not_bind_matrix`,
  `wsl_submit_ntstatus_not_completion_matrix`,
  `wsl_stdalloc_and_alloc_flags_not_bind_matrix`,
  `wsl_trace_display_bind_negative_matrix`, and
  `provider_credit_gate_negative_matrix`,
  `host_display_bind_source_catalog_matrix`,
  `d3d12_display_bind_host_abi_discovery_matrix`,
  `d3d12_display_bind_authority_chain_matrix`,
  `d3d12_completion_source_authority_matrix`, and
  `native_present_completion_source_namespace_matrix`. WSL adapter display caps,
  submit/present metadata, written primaries, standard-allocation private data,
  allocation flags, trace-visible open-resource/sync-file evidence, provider
  invocation counters, WSLg-channel absence, synthvid GPA-dirty evidence, and
  present-history telemetry remain negative proof. Completion authority is
  narrower than progress telemetry: only the source-local display-bind provider
  may issue D3D12 present/completed ids, while KMS vblank/page-flip and Nouveau
  IRQ-cause counters stay in a separate native-display namespace. DDA/Nouveau
  needs real Linux-shaped display creation, non-virtual connectors, hardware
  vblank IRQs, KMS `NOUVEAU_HW` page flips, and hardware flip completions
  before it can receive `dda_native_display_credit`, and even that remains
  separate from `d3d12_native_present_credit`. A D3D12 display-bind result must
  use `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`; do not
  let `dda_nouveau_native_display` satisfy the D3D12 resource-to-display bind
  gate.
- WebKit and FPS validators should consume `/tmp/wlcomp-d3d12-present` as the
  current-run evidence source and reject title/chrome/cursor-only progress,
  stale logs, dmabuf/render-node-only evidence, and app-loop FPS numbers.
  Keep this file self-contained: it must include canonical `display_bind_*`
  fields, and fail-closed rows should use
  `display_bind_completion_source=missing` with zero ids. Demo-side
  `effective_presented_fps` should be derived from native completion deltas,
  not visible/app-loop FPS.
- Treat `/tmp/wlcomp-d3d12-present` as an atomically sealed compositor
  artifact. The producer should write a temp file, flush/fsync it, then rename
  it into place. Consumers must require
  `d3d12_evidence_seal_begin=1`, `d3d12_evidence_seal_end=1`,
  `d3d12_evidence_seal_complete=1`, matching begin/end generation, and seal
  run ids equal to both `d3d12_run_id` and
  `d3d12_present_identity_compositor_run_id`. Unsealed or partial files are
  stale/partial evidence and must not supply native-present, FPS, backend, or
  WebKit credit.
- Keep FPS evidence source-isolated. `hyperv-3d-fps-validate.sh` wraps sampled
  `/tmp/wlcomp-d3d12-present`, `/tmp/mesawlegl-fps`, `/tmp/wlcomp-fps`,
  `fbstat`, and `/proc/uptime` output in source markers; canonical
  `display_bind_*`, `present_id`, and `completed` native-present evidence
  belongs to the compositor-owned `/tmp/wlcomp-d3d12-present` block. Client
  logs such as `mesawlegl_fps_sample` may carry diagnostic-prefixed copies of
  the evidence they read, but they must not emit canonical display-bind ids
  that the FPS parser could count as compositor proof. Use
  `fps_artifact_source_isolation_negative_matrix` to prove forged app-side
  display-bind/native-present/backend/content/final-handoff scalars stay
  zero-credit.
- Treat `display_bind_*` evidence as the canonical bridge between the kernel
  present-source contract, `wlcomp`, FPS, and WebKit. The required keys are
  `display_bind_backend`, `display_bind_transport`,
  `display_bind_transport_source`, `host_saw_display_bind_packet`,
  `wsl_presenthistory_completion_credit`, `display_bind_present_id`,
  `display_bind_completed_id`, `display_bind_resource_generation`, and
  `display_bind_completion_source`/`completion_source`. Fail-closed Hyper-V
  evidence may name the selected backend/transport, but present/completed ids
  must stay zero and no consumer may grant native-present credit from that.
  Future-positive FPS/demo/WebKit evidence must use
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`, and
  `wsl_presenthistory_completion_credit=0`; WSL present-history telemetry is
  not display-bind source authority.
- Credit-bearing D3D12 evidence rows must also be provider-owned and
  line-scoped. Require `evidence_provider=wlcomp`,
  `evidence_path=/tmp/wlcomp-d3d12-present`, a nonzero evidence generation,
  and the current validation run id on the same display-bind dependency, FPS,
  content-progress, or WebKit-open record that carries the canonical
  backend/transport/source-authority/native-completion tuple. Do not assemble
  a passing tuple from separate scalar lines, older logs, or app-side
  diagnostic copies.
- Display-bind source cleanup can be observed in two phases. A same-process
  `dxgprobe` stale-source row may report
  `cleanup_state=deferred_until_process_exit` when stale queries reject and all
  present/completed ids remain zero; the parent C validator must still observe
  nonzero `release_clears` after the child exits before the section passes.
- Kernel native-present work should go through the source-local display-bind
  request/result provider boundary in `fb_dxg_present.c`. The provider may
  remain fail-closed, but it must preserve source/resource generation,
  completion source, status, block reason, and zero present/completed ids until
  a documented DDA/GPU-P sender replaces the stub. The accept path must require
  an exact `host_saw_packet == 1` plus provider-owned completion demux before
  any display-bind result can become native-present credit.
- Keep display-bind cancellation Hyper-V-owned as well as submission-owned.
  `hyperv_dxg_display_bind_cancel()` is the future async sender cancellation
  boundary for owner-close/unregister paths; while fail-closed it reports
  no host packet, zero WSL present-history credit, zero present/completed ids,
  and no native-present/OpenGL-submit credit.
- `scripts/hyperv-dxg-validate.sh` supports
  `DXG_VALIDATE_SEGMENT=pure-c-lifetime` for the WSL-style DXG lifetime,
  handle-table, shared-resource, sync-file, WDDM payload, and residency
  validators. Use this segment before Mesa-heavy runs when validating a full
  lifetime subsection; its default command line disables boot-time glsmoke so
  Mesa readback/device-removal probes cannot freeze before pure-C rows are
  collected. Full mode must still run before claiming
  Mesa/native-present/FPS/WebKit completion.
- The display-bind provider boundary must be sleepable-safe: snapshot under
  `fb_state.lock`, submit outside that lock, then revalidate the present source
  and resource generation before accepting or recording any provider result.
  The visible diagnostics are `provider_submits`, `lock_dropped_submits`,
  `revalidate_attempts`, `revalidate_successes`, and
  `revalidate_failures`.
- The Hyper-V display-bind provider may be the concrete owner even before the
  host ABI exists. Keep `hyperv_dxg_display_bind_submit_failclosed()` explicit
  about pinned dxg/resource metadata, no host ABI, no sender, and no display
  completion; this is a future sender slot, not native-present proof.
- The public provider boundary is `hyperv_dxg_display_bind_submit()`. Today it
  must route to the fail-closed implementation and expose provider-returned
  `pin_revalidated`, `no_host_abi`, `no_sender`, and `no_completion`
  diagnostics. Do not replace those with credit until a documented GPU-P/DDA
  sender and display completion source exists.
- The provider request itself must be complete before the fail-closed result is
  accepted as useful evidence. Require
  `d3d12_display_bind_request_metadata_matrix` with complete device, resource,
  allocation, dimensions, format/modifier, adapter LUID, source/resource
  generation, and sync/fence metadata; the same row must keep present ids,
  native-present credit, and OpenGL-submit credit at zero until the real
  GPU-P/DDA sender exists.
- Treat the display-bind request as a source-owned pending object before any
  sleepable host sender runs. Publish the pending id and source/resource
  generations before dropping `fb_state.lock`, resolve it on provider return,
  and cancel it on source unregister or owner close. Validate lifecycle and
  revalidation separately: `d3d12_display_bind_pending_lifetime_matrix` proves
  pending entries drain with zero credit, while
  `d3d12_display_bind_generation_revalidation_matrix` proves the live
  bind-contract source/resource generation and pinned resource generation
  survived the provider lock drop.
- Keep the future real-sender publication contract explicit even while the
  sender is absent. `d3d12_display_bind_provider_pending_publication_matrix`
  must be backed by provider-owned kernel stats, not hardcoded validator
  placeholders: publication attempts, publish-before-send, transport pending
  id, command id, transaction id, channel, completion-demux registration,
  resolve/cancel, and ref release. Until a documented GPU-P/DDA sender exists,
  publication attempts may advance, the sender-owned fields stay zero, and the
  row must be `PASS_FAILCLOSED` with
  `provider_no_host_abi=1`, `provider_no_sender=1`, and
  `provider_no_completion=1`.
- Also require the no-send preflight handoff row. A sampled fail-closed
  provider must emit `d3d12_display_bind_provider_no_send_preflight_matrix`
  with complete request metadata, `provider_pin_revalidated=1`,
  source/resource generation matches, `preflight_ready=1`, zero send and
  completion-demux attempts, nonzero no-ABI/no-contract blocked counters, no
  host-saw packet, no transport source, zero present/completed ids, and zero
  native-present/OpenGL-submit credit. This is the replacement point for a
  future documented GPU-P/DDA sender, not proof that one exists.
- The fail-closed provider pending row must use provider-owned lifetime
  fields, not validator inference: provider owner/source/resource generations
  must match the kernel pending owner/source/resource generations, while
  `no_host_abi_cancelled`, `no_host_abi_refs_released`,
  publish-before-send, transport pending id, command id, transaction id,
  channel, completion demux, and native-present/OpenGL-submit credit remain
  zero until a documented sender exists. Pair this with
  `d3d12_display_bind_stale_source_zero_credit_matrix`; stale/after-release
  rejects may advance, but any late present/completed ids must remain zero.
- Keep the provider pending publication object graph granular and WSL-shaped.
  `d3d12_display_bind_provider_pending_publication_matrix` should expose
  process namespace validity, per-object HMGR validity for
  device/resource/allocation, active object refs, shared-parent id/ref/child
  and global-share identity, opened-child snapshot validity, and monitored
  sync-object fence value/CPU-VA/kernel-VA/map-size fields. These are
  admission/lifetime proof only; they do not grant native-present,
  OpenGL-submit, or WebKit credit while the sender and completion contract are
  absent.
- Keep provider shared-parent retention and sync-fence aliasing separate from
  the broad publication row. `d3d12_display_bind_provider_shared_parent_retention_matrix`
  must name parent fd refs, host NT refs, child refs, global share, host NT
  handle, and opened-child parent/global/share-generation matches.
  `d3d12_display_bind_provider_sync_fence_alias_matrix` must distinguish CPU
  VA, kernel VA, real returned GPU VA, GPU-VA source, and KVA alias gaps. Both
  rows remain zero-credit until a documented GPU-P/DDA sender and display
  completion source exist.
- Keep WSL-style packet lifetime visible even while no sender exists:
  `d3d12_display_bind_provider_packet_lifetime_matrix` must report no listed
  packet, request id, transport pending id, command id, transaction id,
  `channel=0`, completion demux, host-saw packet, transport source, or packet
  completion/removal credit. A future sender must replace those zeroes with
  real request-list publication and cancel/remove evidence before completion
  can carry native-present credit.
- Keep the stale async completion contract separate from stale-source cleanup.
  `d3d12_display_bind_stale_async_completion_contract_matrix` must report
  `real_sender=0`, `sender_state=absent`, `async_completion_path=absent`,
  `stale_async_scope=no_sender_failclosed`, no completion demux, no transport
  pending id, no host-saw display-bind packet, no transport source, zero
  present/completed ids, and zero native-present/OpenGL/WebKit credit while
  the real sender is absent. It should keep future owner-close-cancel and
  late-completion-reject obligations visible as after-real-send requirements,
  deferred-until-sender, untested, and `late_completion_rejected=not_sampled`
  until a documented sender publishes a pending packet and owner-close or
  unregister cancels that packet before a late completion is rejected against
  the same source/resource generation.
- WSL 6.6.87 and 6.18 dxgkrnl source audits found shared-resource,
  present-history, and sync-file primitives, but no Linux UAPI display-bind
  ioctl and no exposed Linux display-completion handler for binding a D3D12
  resource to scanout. Keep
  `d3d12_display_bind_success_shape_matrix` and
  `d3d12_display_bind_query_fields_matrix` as the current query/success-shape
  guards: they must preserve source/resource generation, provider status/block
  reason, completion source, dirty metadata, host-ABI/sender/completion
  presence, no-host/no-sender/no-completion diagnostics, pin revalidation, and
  zero native-present/OpenGL-submit credit until a documented GPU-P/DDA sender
  replaces the fail-closed provider.
  The May 26, 2026 follow-up audit also found no WSL Linux UAPI ioctl, VMBus
  sender, or host-to-VM demux that binds a D3D12 resource/allocation/fence to
  scanout and returns source/resource-correlated present completion. Treat
  `VM_PKT_COMP` transaction replies and `PROPAGATEPRESENTHISTORYTOKEN` as
  zero-credit telemetry until a provider-owned sender/completion contract
  exists.
- A future provider-success result must pass the full source-local accept
  shape before `fb_dxg_present.c` may copy out native ids: provider status
  success, real transport, scanout-bind operation, host ABI present, sender
  present, display completion present, revalidated pins, no negative provider
  diagnostics, zero block reason, matching source/resource generations,
  nonzero present id, completed >= present id, and display completion source.
  The current fail-closed provider deliberately returns zero ids plus
  no-host/no-sender/no-completion, so validators should keep
  `d3d12_display_bind_id_shape_matrix`,
  `d3d12_provider_credit_gate_matrix`, and
  `wsl_standard_alloc_not_display_bind_matrix` green with zero native-present
  and OpenGL-submit credit.
- `FB_GPU_BACKEND_F_OPENGL_SUBMIT` remains false on Hyper-V until the native
  present dependency chain and the finite 480p FPS gate both pass.
- Treat the animated WebKit fixture as a liveness probe until the compositor
  supplies content-owned CRC/frame/hash evidence tied to the same
  native-present completion. `webkitgpusmoke` should require nonzero content
  CRC, frame counter, frame hash, `NATIVE_PRESENT_COMPLETE` content states,
  and visible/native content credits before `webkit_gpu_contract_matrix ok=1`;
  the Hyper-V validator may run the fixture, but must keep the WebKit
  acceleration gate closed when that native/content evidence is absent.
- D3D12 content-progress evidence should come from compositor-owned
  `wlcomp_buffer` sample fields. Reset them on each D3D12 shared-resource
  commit, tie them to display-bind present/completed/resource generation, and
  leave `source_owned=0` plus zero visible/native credit until a real
  non-readback native-present content sampler fills CRC/frame/hash values.
  The finite FPS and WebKit gates require both content CRC/frame counters and
  compositor-owned frame-hash progress; title/FPS overlay movement, fixture
  title liveness, or CRC-only samples are not enough.
  Effective FPS must also be clamped by compositor-owned content cadence:
  sample-window and visual-window content-frame FPS are part of the minimum
  alongside native/display completion FPS, so app-loop or displayed FPS cannot
  pass when visible content advances at one-digit cadence.
  Final credit must use visible compositor-owned fields only:
  `d3d12_visible_content_crc`, `d3d12_visible_content_frame`,
  `d3d12_visible_frame_hash`, and
  `d3d12_content_progress_source_owned=1`. Client/app hashes are diagnostic
  context and must not open FPS/WebKit gates.
- FPS/WebKit consumers must also require exact identity between content
  progress, DXG present ids, display-bind ids, completion ids, and resource
  generation. They must also require current-run content-progress identity
  fields and final-handoff present/completed/resource-generation equality
  before accepting FPS or WebKit acceleration evidence. Generic
  KMS/vblank/OUT_FENCE/display-wait progress is not native D3D12 completion
  and should be reported by an explicit zero-credit
  `d3d12_native_completion_not_kms_matrix`.
- `webkitgpusmoke` emits
  `webkit_inprocess_contract_gate_matrix` from the pure-C contract validator.
  Keep that row zero-credit on Hyper-V until `FB_GPU_BACKEND_F_OPENGL_SUBMIT`,
  nonzero display-bind/native-present ids, shared-resource/fence identity, and
  compositor-owned content identity all pass together; the shell WebKit
  validator remains responsible for the downstream finite-480p FPS artifact.
- KMS/DRM remains a generic software-present compatibility path until a real
  DDA/Nouveau display engine exists. `gpu_kms_present_fb()` must try the
  native-present discriminator first and record reject reasons, then fall back
  to `fb_blit_from_bo_format()` without granting `kms_present_nouveau_hw`,
  native-present credit, or OpenGL-submit credit. Accepted DDA/Nouveau PCI
  probes may publish BAR/DMA/IRQ diagnostics and a fail-closed display-create
  attempt, but not heads/connectors/vblank/flip-completion success until those
  are backed by real hardware programming.
  Generic display completion must not be printed as D3D12 native-present
  credit; use `generic_display_last_complete` for ordinary scanout progress
  and keep `d3d12_native_present_credit=0`. KMS page-flip/vblank diagnostics
  should distinguish software display completion from future native hardware,
  with native hardware fields zero on the current path.
  Keep GBM scanout support in lockstep with KMS primary-plane support: NV12 is
  metadata/import-render only until the primary plane can scan it out without
  software fallback.

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

### DRM/KMS Validation Baselines

- Treat KMS format support as a two-level contract:
  - framebuffer metadata support means `ADDFB2`/`GETFB2` can store and
    round-trip format, modifier, plane handle, pitch, and offset fields;
  - scanout support means the format is advertised by `GETPLANE` and all
    modeset/present paths can use it without software-only side effects.
- Until native scanout exists for a format, validators should prove unsupported
  `SETCRTC`, page flip, atomic commit, and atomic `TEST_ONLY` reject before:
  - queuing DRM events;
  - taking or waiting in-fence refs;
  - creating, exporting, cleaning, or placeholder-writing out-fences;
  - mutating current KMS framebuffer state;
  - advancing display, DXG-present, native-present, or OpenGL-submit credit.
- The current Hyper-V KMS NV12 baseline is metadata-only: NV12
  `ADDFB2`/`GETFB2` round-trips, primary `GETPLANE` advertises only
  XRGB8888/ARGB8888, `DRM_CAP_ADDFB2_MODIFIERS` advertises only the accepted
  linear metadata contract, and the NV12 scanout/present matrix is
  fail-closed.
- Primary-plane `IN_FORMATS` must match actual scanout, not framebuffer
  metadata breadth. The current valid blob is immutable and advertises only
  XRGB8888/ARGB8888 with `DRM_FORMAT_MOD_LINEAR`; validators require
  `kms_in_formats_blob_matrix ... nv12_scanout=0 nonlinear_modifiers=0
  native_present_credit=0 opengl_submit_credit=0 status=PASS`.
- KMS vblank/page-flip event-source provenance is its own layer:
  - display-correlated timing requires `kms_vblank_synthetic=0`,
    `kms_vblank_display_correlated=1`, and
    `display_completion_correlated=PASS`;
  - those events are still not native-present evidence unless the same run also
    proves real atomic OUT_FENCE/native display handoff completion;
  - Hyper-V must keep `backend_opengl_submit 0` while OUT_FENCE provenance is
    `software_scanout_commit`.
- Native-display readiness is kernel-owned state, not a user-space inference
  from generic KMS success. On GPU-P-only Hyper-V the required rows are
  `native_display_readiness_failclosed_matrix`,
  `nouveau_display_failclosed_matrix`, and
  `kms_present_discriminator_failclosed_matrix`, each with zero
  native-present/OpenGL-submit credit and `reject_reasons=0x7f` until a real
  DDA/Nouveau display object, heads/connectors, vblank source, and hardware
  flip completion exist.
  The Linux-shaped Nouveau display skeleton is more granular than those first
  counters: require `nouveau_linux_display_readiness_matrix` to show absent
  display-engine object, `mode_config`, CRTC/head, encoder/outp,
  primary-plane, outp/connector/head masks, `nvif_head`, HPD/DP IRQ event,
  per-head vblank event, atomic commit-tail, and page-flip completion source
  on GPU-P-only Hyper-V, with only the linear-required policy set.
- Linux-shaped KMS/Nouveau/TTM diagnostics are allowed to pass only as
  fail-closed mismatch rows until the matching native layer exists. Keep rows
  such as `nouveau_display_kms_registration_matrix`,
  `nouveau_kms_vblank_irq_source_matrix`,
  `kms_scanout_cpu_convert_separation_matrix`,
  `ttm_real_move_backend_matrix`, `nouveau_gem_mmap_backing_matrix`, and
  `nouveau_gpuvm_mapping_failclosed_matrix` zero-credit on GPU-P-only Hyper-V.
- For DRM sync_file validation, distinguish the layers:
  - pending export/import readiness proves live source tracking;
  - callback lifecycle proves poll-arm, signal-fire, close-cancel, and
    no-late-fire accounting;
  - syncobj wait callback lifecycle proves actual sleeping waits arm per-state
    callbacks, signal/transfer fires them, finite timeout cancels them, and
    `wait_callback_late_delta=0`;
  - pending syncobj timeline transfer must copy the source dependency before
    it is signaled, then wake destination waiters only after the source point
    signals; the index row is `syncobj_pending_transfer_matrix`;
  - fd-visible cancellation should happen from `.last_fd_close`; delayed
    `.release` accounting is not enough evidence for close-before-signal
    behavior;
  - exported software fence fds need their own add/fire/remove/late callback
    matrix before broad dma-fence language is justified; exact software
    fence-object lifetime uses `early_release_on_close` when the fd has no
    hidden or concurrent refs;
  - the broad dma-fence row is closed only by
    `drm_dma_fence_lifetime_contract_matrix`, which ties the single
    `fb_gpu_fence` backing object to GEM/PRIME/dma-buf, KMS OUT_FENCE,
    syncobj/sync-file, poll callback removal, and final release without
    native-present/OpenGL-submit credit;
  - the broad dma-resv row is closed only when `ttmtest` and
    `gpucorevalidate` require `ttm_dma_resv_ww_mutex_matrix`: a
    `ww_acquire_ctx`-shaped reservation context, ordered multi-object acquire,
    reversed-order retry/backoff, balanced releases, and zero
    native-accel/OpenGL-submit credit across the existing TTM, PRIME/dma-buf,
    KMS, syncobj, sync-file, eviction, and teardown attach points;
  - native KMS OUT_FENCE and display-correlated vblank/page-flip completion are
    separate gates; passing the vblank/page-flip source matrix does not close
    the OUT_FENCE gate.
  - `kms_vblank_native_present_separation_matrix` is the focused source-level
    guard: vblank/page-flip/display counters may advance while native D3D12
    present and OpenGL-submit credit remain zero.
- The finite 480p FPS validator runs the anti-inflation preflight by default
  before heavy Hyper-V sampling. The required negative is the observed around-40
  app-loop/demo FPS with single-digit visible cadence, stale run id, static
  content, or frozen-window evidence. `mesawlegl` must keep the on-screen
  overlay/title at `0.0` FPS until strict native-present credit exists; the
  untrusted draw-loop rate belongs in `app_loop_fps` diagnostics only.
- `fps_frozen_window_rejection_matrix` and
  `fps_sustained_post_warmup_progress_matrix` are the freeze-after-motion
  guards. The FPS validator must sample more than one visual window and reject
  runs where native/display/content/callback/release/present/completed counters
  or thumbnails move briefly and then stop, even if the overlay still prints a
  plausible FPS number.
- The FPS validator's preflight must leave durable rejection evidence, not only
  console text. `fps_forged_display_bind_negative_matrix` belongs in the log,
  outside-overlay CRC transitions must be computed before sustained-window
  checks, and `finite_fps_credit` must stay separate from
  `opengl_submit_credit`; the latter requires both finite native-present FPS
  and `backend_opengl_submit=1`.
- `mesawlegl` owns `/tmp/mesawlegl-fps` app telemetry. Treat its app-loop
  FPS as context-only unless each sample has matching validation run id,
  `process_id == d3d12_client_pid`, nonzero DXG present/completed counters,
  `d3d12_native_present_requirements_satisfied=1`, no readback, and current
  compositor evidence generation/time/resource/buffer-generation metadata.
  The displayed `visible_fps`/`overlay_fps` fields should remain `0.000`
  without that credit, while `app_loop_fps` may carry the raw draw-loop number.
  `mesawlegl_fps_context_only_matrix` and
  `fps_overlay_inflation_rejection_matrix` are rejection evidence, not pass
  evidence for the 60 FPS gate.
- Use the present/FPS provenance rows to keep the GUI gate honest:
  `wlcomp` emits `d3d12_wayland_present_fps_provenance_matrix`, and
  `mesawlegl` emits `mesawlegl_fps_present_credit_matrix`. On fail-closed
  Hyper-V these must show `effective_presented_fps=0.000`,
  `visible_fps_ignored=1`, `overlay_fps=0.000`, and zero
  native-present/OpenGL-submit credit even if `app_loop_fps` is higher.
- Treat `/tmp/wlcomp-d3d12-present` as the durable handoff file for those
  provenance rows. It should carry both the matrix row and
  `d3d12_fps_provenance_*` scalar keys so FPS/WebKit validators do not depend
  on stderr timing. It must also carry the evidence-seal keys described in the
  native-present dependency tree; FPS/WebKit parsers should reject canonical
  `display_bind_*` rows from that file until the seal generation and run id
  match the current validation run.
- Treat visible-content progress as zero-credit until native D3D12 display
  completion is proven for the same resource/generation. The compositor should
  emit `d3d12_wayland_content_progress_matrix` and
  `d3d12_content_progress_*` scalar keys in `/tmp/wlcomp-d3d12-present`; on
  fail-closed Hyper-V these must report `d3d12_content_progress_state=DEFERRED`,
  `d3d12_visible_content_progress=DEFERRED`,
  `d3d12_content_progress_current_run_valid=0`,
  `d3d12_content_progress_identity_complete=0`,
  `d3d12_content_progress_requires_native_present=1`,
  `d3d12_visible_content_requires_native_present_completion=1`,
  `d3d12_visible_content_credit_before_native_present=0`, and zero
  native-present/OpenGL-submit credit. The evidence file should also include
  explicit zero-valued content sample fields
  (`d3d12_present_content_crc`, `d3d12_visible_content_crc`,
  `d3d12_present_content_frame`, `d3d12_visible_content_frame`,
  `d3d12_present_frame_hash`, `d3d12_visible_frame_hash`) until real
  compositor-owned native-present content correlation exists. `mesawlegl`
  source-side `client_content_hash`/`client_content_frame` and
  `content_region=client-content-no-title-fps` are useful context, not native
  present proof by themselves.
- Passing FPS evidence must include the exact geometry contract:
  `window=640x480 render=640x480 render_div=1`. WebKit's GPU validator should
  reject prior FPS artifacts that lack that full-resolution token.
- The 480p demo lifecycle gate has two evidence producers. `mesawlegl` owns
  source/app telemetry and must only set `resizable_demo=1` after an actual
  resize count. `wlcomp` owns compositor lifecycle telemetry and appends
  `wayland_demo_visible_close_resize_matrix` to `/tmp/mesawlegl-fps` with
  validation run id, client pid, mapped/closeable/resize state, geometry, and
  zero native/OpenGL-submit credit. Do not let these lifecycle rows carry fake
  present ids, completed ids, or content-progress credit.
- The lightweight FPS selftest emits
  `fps_demo_interaction_selftest_matrix`. Keep stale run-id, missing-resize,
  source-only, callback-only, and forged display-bind cases rejected with zero
  native-present/OpenGL-submit credit until the final native-present/FPS run
  can prove the demo is visible, closeable, and resizable.
- Final FPS display-bind acceptance is canonical only:
  `display_bind_backend=gpup_dxg_scanout_bind` and
  `display_bind_transport=gpu-p-dxg-resource-scanout-bind`, with exact
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`,
  `wsl_presenthistory_completion_credit=0`, and
  `display_bind_completion_source=display` on the same evidence record as the
  nonzero present id, completed id, and resource generation. Fail-closed
  `/tmp/wlcomp-d3d12-present` rows should instead report
  `display_bind_transport_source=none`, `host_saw_display_bind_packet=0`,
  and `wsl_presenthistory_completion_credit=0`, and remain zero-credit.
  Legacy aliases such as `hyperv-dxg`, `gpu-p`, `dda`, `nouveau`,
  `dxg-resource-scanout-bind`, `host-display-channel`,
  `FB_GPU_DXG_PRESENT_COMPLETION_DISPLAY`, numeric enum values, and
  case-changed `DISPLAY` are diagnostic text only, not final pass tokens.
  FPS acceptance must require coherent sample-window and visual-window
  display-bind tuples before any FPS credit.
- Kernel display-bind success acceptance must be stricter than the fail-closed
  metadata rows. A future positive result must have provider publication,
  publish-before-send ordering, nonzero transport pending id, command id,
  transaction id, channel, completion demux registration,
  `host_saw_display_bind_packet=1`, and
  `wsl_presenthistory_completion_credit=0` before nonzero present/completed ids
  can count. `d3d12_display_bind_success_shape_matrix` should expose those
  fields for both `fbstat` and `dxgprobe`.
- WebKit's animated native-present fixture currently proves liveness only.
  Treat `/share/webkit/webkit-animated-content-native-present.html` title/frame
  progress as insufficient until `/tmp/wlcomp-d3d12-present` supplies matching
  nonzero content CRC/frame/hash, same run/client/resource generation, native
  display completion, and canonical final display-bind names
  (`gpup_dxg_scanout_bind` and `gpu-p-dxg-resource-scanout-bind`).
- Keep both WebKit launch paths on the same strict D3D12 evidence contract.
  `wlcomp_launcher` and `desktop` should parse `/tmp/wlcomp-d3d12-present` as
  whitespace/line-bounded `key=value` tokens, require the generated WebKit run
  id to match both `d3d12_run_id` and
  `d3d12_present_identity_compositor_run_id`, and reject acceleration unless
  compositor-owned content CRC/frame/hash progress matches the provider-owned
  display-bind present/completed ids, resource generation, and source-authority
  tuple. Policy artifacts should expose `d3d12_run_id_match`,
  `d3d12_content_progress`, `d3d12_evidence_seal`,
  `display_bind_transport_source`, `host_saw_display_bind_packet`, and
  `wsl_presenthistory_completion_credit` so shell validators can fail stale,
  unsealed, prefixed, WSL-telemetry, host-copy, or chrome/title-only evidence.
- `webkitgpusmoke --negative-selftests` is the pure-C consumer-negative
  validator for WebKit evidence. Keep
  `webkit_contract_parser_negative_matrix`,
  `webkit_lineage_equality_negative_matrix`, and
  `webkit_animated_content_fixture_negative_matrix` green before trusting a
  shell WebKit artifact. Also keep
  `webkit_animated_content_native_present_negative_matrix` green: even
  plausible compositor-owned content CRC/frame/hash plus source-authority
  fields remain zero-credit when native present, prior finite FPS,
  backend OpenGL-submit, or shared-surface contract evidence is missing.
  Prefixed/suffixed keys, malformed numerics, backend/transport aliases,
  completion-source aliases, stale D3D12/FPS/content lineage, backend-zero
  nonzero-id claims, title-only animated fixture progress, and forged content
  progress must all remain zero-credit.
- WebKit acceleration validation starts with the
  `webkit_evidence_rejection_matrix` policy preflight. Chrome/title/cursor-only,
  callback-only, release-only, render-node-only, dmabuf-only, env-only, and
  software-fallback evidence must all fail before any enabled WebKit artifact is
  considered.
- WebKit policy preflight should also reject plausible but incomplete
  acceleration-looking rows: nonzero display/native ids while
  `backend_opengl_submit=0`, stale D3D12 run ids, and stale FPS artifact run
  ids. Downstream gate rows should keep separate reason fields for backend-zero,
  display-bind completion-source rejection, native-id rejection, and lineage
  rejection so a future enabled artifact cannot hide which dependency opened.
- WebKit C consumers must parse policy and D3D12 evidence as whitespace/line
  bounded `key=value` tokens. Raw substring searches can accept prefixed keys,
  suffixed keys, malformed numeric values, or backend/transport aliases; keep
  `display_bind_backend=gpup_dxg_scanout_bind` and
  `display_bind_transport=gpu-p-dxg-resource-scanout-bind` as the canonical
  final tokens.
- WebKit shell consumers must also be line-scoped. Before emitting any open
  WebKit gate, require one provider-owned display-bind record carrying exact
  backend, transport,
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`,
  `wsl_presenthistory_completion_credit=0`,
  `display_bind_completion_source=display`, `completion_source=display`,
  nonzero present/completed/resource generation, `backend_opengl_submit=1`,
  and a matching native completion id. Do not fill missing native completion
  ids from display-bind ids, and do not count final-handoff-only host-display
  aliases or WSL present-history telemetry as GPU-P/DDA commit acceptance.
- Pure-C validator consumers should use the same discipline. `gpucorevalidate`
  output checks should match whitespace/line-bounded tokens while still
  allowing deliberate `field=` prefix probes, and `dxgprobe` should parse
  `/dev/dxg` host-to-VM status fields on one line with whole-token numeric
  values before using present-history telemetry rows.
- WebKit also requires
  `webkit_stale_display_bind_evidence_rejection_matrix` and
  `webkit_enabled_artifact_contract_matrix`. Stale/after-close display-bind
  evidence is always zero-credit. A future enabled artifact must prove
  current-run D3D12 display-bind completion, finite 480p FPS,
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT`, shared-resource/fence identity, and
  compositor-owned content CRC/frame/hash identity from the same lineage.
  The enabled artifact row must also echo and require the provider-owned
  source-authority tuple:
  `display_bind_transport_source=non_wsl_linux_dxgkrnl_extension`,
  `host_saw_display_bind_packet=1`, and
  `wsl_presenthistory_completion_credit=0`; WSL present-history telemetry and
  host-copy aliases remain zero-credit even if other display-bind ids are
  nonzero.
- For WSL `hmgrtable` parity, keep local adapter handles and normal DXG object
  handles distinct:
  - `hvdxg_process_state` should keep WSL-shaped process object refs separate
    from process memory refs; validators should look for
    `dxg_process_mem_lifetime_matrix` and the kernel
    `d3dkmt_process_lifetime=` status row before closing process-lifetime
    parity items;
  - local adapter handles use the per-process local handle namespace and should
    prove index/unique encoding plus the WSL minimum-free reuse delay
    (`min_free=128`) with `dxgprobe --handle-lifetime-validate`;
  - stale-handle validation should explicitly name and reject every locally
    tracked class in the matrix: device, context, HW queue, HW-queue progress
    fence sync, sync object, paging queue, paging-queue sync, resource,
    standalone allocation, and GPUVA reservation;
  - `dxgprocess_adapter` parity means `CREATEDEVICE` must resolve a
    process-local adapter handle, not the raw host adapter handle, and final
    close of that per-process adapter should tear down still-live child
    devices; require `dxgprocess_adapter_matrix` before checking that plan row;
  - normal object handles still need full free-list parity before the broad
    handle-table gate can close, even if tombstone diagnostics and stale
    rejection counters look healthy.
