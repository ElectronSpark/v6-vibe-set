# Historical Hyper-V native-present contracts

Historical reference moved from the former GUI runtime entrypoint. Original entrypoint body lines 133–508.
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

- [Baseline context](#validated-gpu-baselines).
- [Native-present dependency tree](#native-present-dependency-tree).

For a narrow lookup, search only this file for `display_bind`, `evidence_seal`,
`source_authority`, `cancel`, or the exact validator matrix from the old receipt.

<!-- BEGIN PRESERVED HISTORICAL BODY -->
## Validated GPU Baselines

These items were retired from the GPU checklist (now archived at
`docs/archive/plan-consolidation-20260907/GPU_REMAINING_GAPS.md`) after the May 17, 2026
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
