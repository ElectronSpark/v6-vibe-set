# Hyper-V GPU contracts

Use only for Hyper-V DXG/D3DKMT or native display work. This is a conditional
reference, not the normal KDE/virgl launch recipe. Current source and
`docs/active-work-plan.md` determine which contracts remain open. Historical
`wlcomp` evidence does not validate the current KDE session. Root `AGENTS.md`
and the parent skill's ownership, wait, serial, and cleanup rules apply.

## Contents

- Transport, ownership, publication, and display admission
- Hyper-V GPU validation discipline

## Transport, ownership, publication, and display admission

- Treat Hyper-V GPU work as a transport, UMD, compositor-present, and validation problem. Do not mark `FB_GPU_BACKEND_F_OPENGL_SUBMIT` true just because `/dev/dri/renderD128`, DXG transport, D3DKMT readiness, or a real HW queue exists.
- The former large `kernel/kernel/dev/fb.c` and `kernel/kernel/dev/hyperv_input.c` files are
  split under subsystem roots:
  - `kernel/kernel/dev/fb/module.c` is the framebuffer/GPU root. Its fragments group
    scanout, BO/GEM/TTM/dmabuf, DXG present-source glue, exported fd/fence
    lifecycle, DRM/KMS, syncobj/PRIME/virtgpu, Nouveau, dispatch, init, and
    panic-screen code.
  - `kernel/kernel/dev/hyperv/module.c` is the Hyper-V root. Its fragments group
    common protocol state, vPCI config, DXG diagnostics/status, DXG object and
    shared-resource lifetime, D3DKMT ioctl forwarding, DXG device exports,
    VMBus core, synthetic devices, and public init/stub code.
  Keep new work in the narrow fragment that owns the behavior. Promote a
  fragment to a separately compiled `.c` file only after its shared state and
  static helper dependencies have explicit internal APIs.
- Known honest capability split:
  - Hyper-V may expose `FB_GPU_BACKEND_F_DXG_TRANSPORT` and `FB_GPU_BACKEND_F_D3DKMT`.
  - Hyper-V must keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT` false until Mesa D3D12 creates real render contexts, submits real UMD command buffers, presents without the software readback lane, and the 480p 3D demo sustains more than 60 FPS after warmup.
  - KVM/virgl is the current OpenGL-submit backend.
  - `scripts/gpu/gpu-validate.sh` is the positive KVM/virgl control validator:
    require `backend virgl`, `backend_opengl_submit 1`,
    `backend_opengl_submit_gate open`, `backend_virgl_opengl 1`, and
    `opengl_submit_backend_separation_matrix ... opengl_submit_credit=1`.
    This proves the control backend only; it does not grant Hyper-V credit.
- Select the configured Hyper-V build and deployed image explicitly; do not
  assume a historical `/tmp` build or VHDX still matches current source.
  Use `scripts/image/make-hyperv-image.sh` only for an authorized Hyper-V task,
  with explicit kernel, rootfs, output, and guest command line. Read its usage
  before deployment and preserve a known-good rollback.
- Wait synchronously on the actual serial command. Buffered output or an echoed
  command is not completion; check VM state and obtain a fresh prompt after a
  timeout. Keep commands short, use marker variables, and account for the
  bracketed-paste first-character drop. Collect freeze evidence before reset.
- Compare WSL and xv6 on the same adapter and UMD version. Preserve private
  payloads, kernel-side packet contents, host status, fences, and cleanup state.
  When using the repository DXG ioctl trace shim, `XV6_DXG_TRACE_HEAD_BYTES=4096`
  expands the otherwise short private-payload dump.
- WSL2 `dxgkrnl` lives under `drivers/hv/dxgkrnl` in Microsoft's WSL2 Linux
  kernel, with UAPI in `include/uapi/misc/d3dkmthk.h`. Use it as the DXG/D3DKMT
  process, handle-table, shared-resource, sync-file, and VMBus packet
  reference; do not treat it as the DRM/KMS/Nouveau display-stack reference.
- Match WSL's assign-before-expose ordering for host-opened resources and sync
  objects. After `OPENRESOURCEFROMNTHANDLE`,
  `OPENSYNCOBJECTFROMNTHANDLE2`, or `OPENSYNCOBJECTFROMSYNCFILE` succeeds on
  the host, commit the local dxgprocess object graph before copying handles to
  userspace; tracking or late-copyout failure must untrack local state and
  destroy the host-opened object through the owner-bound process handle.
- Apply the same WSL publication rule to host-created handles: device,
  context, allocation/resource, sync object, paging queue, and HW queue create
  paths must commit local object-table state before handle copyout. On late
  failure, untrack local state and destroy the host-created object through the
  owner-bound process handle where a D3DKMT destroy command exists.
- Treat monitored-fence mappings like WSL `dxgsyncobject_stop()` lifetime:
  track map size and owning VM, map kernel aliases as PFNMAP, and unmap both
  user and kernel fence aliases when the sync object is untracked or create/open
  publication unwinds. Do not leave a stale fence VA/KVA as evidence.
- Preserve the existing refcounted shared-resource parent: fd refs, host NT
  refs, sealed private-data/allocation metadata, and opened-resource children.
  Validate create/open/close unwind through `hvdxg_shared_parent_create`,
  `hvdxg_shared_parent_put`, and child attachment in
  `kernel/kernel/dev/hyperv/hyperv_dxg_allocations.c`. Per-open children must
  not deep-clone the parent as the authoritative lifetime object.
- Keep the reference tracks separate: WSL parity can close Hyper-V DXG object
  and wire-layout gates, while Linux DRM/GEM/TTM/KMS/Nouveau sources govern
  `/dev/dri`, PRIME/dma-buf, KMS atomic, PCI runtime, and Nouveau behavior.
- For PCI/Nouveau work, mirror Linux's layering without pretending GPU-P is
  DDA hardware. Accepted Nouveau requires a real BAR-backed NVIDIA display
  PCI function, PCI resource claim-before-iomap, bus mastering, requested and
  effective DMA mask diagnostics, streaming DMA map/unmap validation,
  explicit MSI/MSI-X fail-closed or programming state, legacy IRQ provenance,
  runtime-PM/remove diagnostics, and zero native-present/OpenGL-submit credit
  until a real native engine and display handoff exist. GPU-P-only Hyper-V
  must keep BAR/DMA/IRQ/map counters zero and report `GPU_P_FAIL_CLOSED`.
  Validate IRQ work with `nouveau_pci_irq_provenance_matrix`; on GPU-P-only
  boots, either a real DDA/Nouveau reject counter or `nouveau_pci_probes 0`
  is an honest fail-closed reason, but MSI/MSI-X, legacy IRQ, handler, cause,
  ack, and spurious counters must remain zero.
  Validate runtime-PM/remove work with `nouveau_pci_remove_pm_matrix`. Mirror
  Linux PCI's resume/barrier-before-remove shape: GPU-P-only boots must report
  `runtime_resume_before_remove=NOT_APPLICABLE` and zero remove/teardown
  counters, while accepted DDA hardware remains diagnostic until a real
  remove or hot-remove path proves BAR unmap, IRQ unregister, vector free,
  bus-master clear, device disable, and drvdata clear ordering.
  Accepted-DDA runtime suspend must save PCI command state and disable
  memory/I/O/bus-master decode; resume must restore that command state before
  the driver resume callback. Nouveau IRQ delivery can only be claimed after a
  real BAR0 interrupt cause is read and acked, currently via `NV_PMC_INTR_0`
  gated by `NV_PMC_INTR_EN_0`.
  Keep Linux-shaped PCI wrapper names available for future Nouveau port code:
  `dma_set_mask_and_coherent`, `pci_enable_msi`,
  `pci_enable_msix_range`, `pci_request_irq`, `pci_free_irq`,
  `pci_mmap_bar`, `pm_runtime_resume_and_get`, `pm_runtime_put`, and
  `pm_runtime_barrier`. These wrappers must remain diagnostic/fail-closed on
  GPU-P-only boots; they are not native-present or OpenGL-submit evidence.
- Remember what the trace layers mean:
  - `LD_PRELOAD` ioctl traces show the UMD's user-space ioctl arguments before the xv6 kernel rewrites or validates them.
  - `/dev/dxg` shows the kernel's recorded host-return state after forwarding.
  - If you transform a packet in the kernel, add or consult kernel-side diagnostics before claiming the host saw the transformed packet.
- Treat historical WSL replay payload sizes and May-era FBO/residency failures
  as prior evidence, not the current blocker. Use `docs/active-work-plan.md`
  and fresh same-adapter traces to choose the next probe. Do not sort or rewrite
  residency lists merely to hide a host error.
- Measure performance over a finite post-warmup interval, with an in-surface
  time source and actual display completion. Stderr or title FPS alone is
  insufficient.
- The source-level Hyper-V native-present skeleton is
  `FB_GPU_DXG_PRESENT_BIND_CONTRACT_QUERY`. It lives in
  `kernel/kernel/dev/fb/fb_dxg_present.c` and must remain fail-closed until a real
  GPU-P/DDA display lane exists. Treat its registered present source,
  source/resource generations, required metadata, selected bind lane, and
  display-completion source as the handoff contract; do not infer native
  present from loose D3DKMT handles or `/dev/dxg` readiness alone.
- Keep native-present admission split into explicit stages: source admission,
  GPU-side composite/copy, scanout-bind attempt, and display completion. Until
  a real GPU-P/DDA host display-bind transport exists, `dxg_scanout_bind_*`
  counters are allowed to show attempts, rejects, weak-evidence rejects, and
  source/resource generations only; successes, present IDs, completed IDs,
  native-present credit, and OpenGL-submit credit must stay zero.
- The selected scanout-bind provider boundary is Hyper-V-owned even while it
  is fail-closed: `fb_dxg_present.c` delegates to
  `hyperv_dxg_display_bind_submit_failclosed()`, which revalidates the pinned
  `/dev/dxg` + `anon_inode:dxgresource` metadata and reports explicit
  no-host-ABI/no-sender/no-completion diagnostics. Do not replace those zero
  ids with native-present credit until a real GPU-P/DDA sender and display
  completion source are documented and validated.
- Keep WSL present-history command IDs separate from native-present proof.
  `PRESENTHISTORYTOKEN`, redirected flip fence, and BLT enum values are known
  candidate command IDs, but without source-backed sender, packet, return, and
  completion contracts they remain rejected diagnostics only. Validators should
  keep `dxg_scanout_bind_candidate_command_matrix` and
  `dxg_scanout_bind_weak_evidence_matrix` green with zero native-present and
  OpenGL-submit credit.
- Treat visible FPS as app-loop evidence unless it is tied to native D3D12
  completion for the same run/resource/generation. A legacy `wlcomp` implementation should emit
  `d3d12_wayland_present_fps_provenance_matrix`, and `mesawlegl` should emit
  `mesawlegl_fps_present_credit_matrix`; on the fail-closed Hyper-V path these
  rows must report effective presented FPS as zero and ignore the displayed
  overlay FPS.
- For an explicitly restored legacy `wlcomp` image, `/tmp/wlcomp-d3d12-present`
  is its FPS/WebKit consumer contract; it is not a KDE session log:
  it should include `d3d12_wayland_present_fps_provenance_matrix` and scalar
  `d3d12_fps_provenance_*` keys, while
  `FB_GPU_DXG_PRESENT_SOURCE_COMMIT` only copies `present_id/completed` back
  on a real success path.
- In that legacy compositor, content progress lives on the D3D12 `wlcomp_buffer`
  lifetime. Its evidence writer should consume those per-buffer
  CRC/frame/hash fields only; title text, app-side counters, and source logs
  are liveness or client evidence, not visible/native content credit.
- The selected native-present handoff lane is GPU-P/DDA
  `dxg-resource-scanout-bind`, not WSLg display channel emulation and not a
  synthvid GPA-dirty bridge. `fbstat` should report
  `dxg_present_lane_selection_matrix` with WSLg disabled, synthvid limited to
  GPA dirty VRAM, `custom_host_tool=0`, and zero native-present/OpenGL-submit
  credit until the real host ABI and completion source exist.
- Review these Hyper-V GPU layers separately; current completion is in the active plan:
  - WSL-style typed per-open DXG object graph and teardown ordering.
  - Exact WDDM private payload and host return layout parity for real UMD sequences.
  - D3D12 shared-resource/fence export/import between a Mesa client and compositor.
  - A non-readback Wayland present path for Hyper-V.
  - A finite GUI performance validator that fails below 60 FPS after warmup.

## Hyper-V GPU Validation Discipline

- Keep validation hierarchical:
  - build the selected configured Hyper-V tree for implementation changes;
  - run focused pure-C guest sections for the implemented slice;
  - only then run heavier GUI/FPS/WebKit validation for a completed segment.
- For KMS/DRM format work, separate framebuffer metadata from scanout capability:
  - `ADDFB2`/`GETFB2` may accept metadata for formats that the primary scanout
    plane cannot present yet;
  - `GETPLANE` must advertise only formats that the primary plane can actually
    scan out;
  - do not add a format to `GETPLANE`/`IN_FORMATS` unless the present path
    handles it end-to-end; current accepted primary scanout formats are linear
    XRGB8888/ARGB8888 plus linear XBGR8888/ABGR8888 through explicit R/B
    conversion, while NV12 remains metadata-only and fail-closed for primary
    scanout;
  - `SETCRTC`, page flip, atomic commit, and atomic `TEST_ONLY` must reject an
    unsupported framebuffer before queuing events, taking in-fence refs,
    exporting or cleaning out-fences, mutating plane/current-FB state, or
    advancing display/DXG/native/OpenGL-submit credit.
  - focused validators should include `kms_primary_scanout_format_mod_matrix`,
    `kms_primary_scanout_actual_format_matrix`, and the NV12 fail-closed direct
    property row in `kms_present_completion_failclosed_matrix`.
- Treat zero-credit matrices as real contracts, not decorative logging:
  validators should prove `native_present_credit=0`, `opengl_submit_credit=0`,
  and no DXG-present/display deltas whenever a path is still software,
  synthetic, or fail-closed.
- For KMS vblank/page-flip work, keep event-source provenance separate from
  native-present proof:
  - pre-native KMS events may be display-completion-correlated only when
    validators report `kms_vblank_synthetic=0`,
    `kms_vblank_display_correlated=1`, and
    `display_completion_correlated=PASS`;
  - display-correlated KMS events still grant no native-present or
    OpenGL-submit credit until atomic OUT_FENCE/native display handoff is real;
  - software/immediate atomic OUT_FENCE provenance remains an open gate even
    after vblank/page-flip event timing is display-correlated.
- For sync_file/fence work, keep the hierarchy explicit:
  - live pending sync_file export/import is one layer;
  - callback lifecycle is a stricter layer and must prove poll arms, source
    signal fires, close-before-signal cancels, late fires stay zero, and live
    syncobj/fd state balances;
  - syncobj waits are stricter again when they prove per-state wait callbacks:
    a real sleeping wait arms a callback, signal/transfer fires it, finite
    timeout or interruption cancels it, late-fire count stays zero, and
    native/OpenGL-submit credit stays zero;
  - on xv6 custom fds, visible close-before-signal cancellation belongs in the
    `.last_fd_close` hook; `.release` can run later and should only be backup
    cleanup;
  - software fence-fd callbacks should prove the same add/fire/remove/late
    lifecycle on `fb_gpu_fence` objects before claiming broader dma-fence
    parity; the software fence fd uses VFS `early_release_on_close` so exact
    object lifetime can be validated when no hidden/concurrent references
    remain;
  - do not mark the broad Linux `dma_fence` gate complete until callback
    removal, timeline lifetime, poll wakeups, and reservation iteration are
    validated across GEM, PRIME, KMS, and syncobj.
