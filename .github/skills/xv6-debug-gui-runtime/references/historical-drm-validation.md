# Historical DRM, KMS, FPS and WebKit validation contracts

Historical reference moved from the former GUI runtime entrypoint. Original entrypoint body lines 1033–1321.
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

- [DRM/KMS validation baselines](#drmkms-validation-baselines).

For a narrow lookup, search only this file for `IN_FORMATS`, `syncobj`,
`dma_fence`, `dma_resv`, `fps_`, `display_bind`, `webkit_`, or `hmgrtable`.

<!-- BEGIN PRESERVED HISTORICAL BODY -->
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
