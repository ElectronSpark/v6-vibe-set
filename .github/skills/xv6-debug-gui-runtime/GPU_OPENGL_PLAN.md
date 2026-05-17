# GPU And OpenGL Plan

Last updated: 2026-05-17

This companion plan is intentionally short. The source-audited checklist of
open work lives in `GPU_REMAINING_GAPS.md`; completed substrate facts live in
`SKILL.md` under "Validated GPU Baselines".

## Current Status

- KVM/virtio-gpu/virgl is the validated OpenGL-submit backend.
- Hyper-V GPU-PV has DXG transport and D3DKMT readiness, but not completed
  OpenGL submit/present support.
- Hyper-V must keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT` false until the D3D12
  render, shared-resource present, fence, and 480p >60 FPS demo path is proven.
- The 3D demo is a real native Wayland EGL window with an in-surface RTC FPS
  overlay. On Hyper-V it still presents through the DRI software/readback lane
  and remains below the >60 FPS target.
- WebKit acceleration is correctly gated by `FB_GPU_BACKEND_F_OPENGL_SUBMIT`;
  Hyper-V falls back safely until the same shared-surface contract works for
  native Mesa clients.

## Active Milestones

1. Finish WSL-style DXG object semantics.
   - Replace growable diagnostic trackers with typed `dxgprocess`/handle-table
     objects, krefs, handle namespaces, lock ordering, fork/exec behavior, and
     exact teardown/unwind ordering.
   - Seal resource/shared-resource metadata the way WSL does, including
     runtime/resource/allocation private data and multi-open lifetime.
   - Verify real UMD packet layouts against same-adapter WSL traces, especially
     residency, cacheability, fence, and cleanup side effects.

2. Fix the real Mesa D3D12 residency blocker.
   - Last observed blocker is the second-FBO multi-allocation
     `LX_DXMAKERESIDENT count=2 flags=0x1` host `EINVAL`.
   - Add kernel-side diagnostics for the actual forwarded packet before claiming
     packet ordering or field rewrites reached the host.

3. Build the Hyper-V D3D12 Wayland present path.
   - Add a protocol/import path for D3D12 NT shared-resource fds plus sync/fence
     metadata.
   - Teach the compositor to open/present/copy from those resources through
     DXG/D3D12 without CPU readback.
   - Keep framebuffer BO present/direct scanout as a separate baseline path; it
     does not make D3D12 render targets native compositor inputs.

4. Add validators for the missing contracts.
   - Pure-C D3D12 shared-resource export/open/fence/present validator.
   - WSL-trace replay validator that passes as an equivalence test, not only an
     opt-in repro for host rejections.
   - Finite desktop 3D performance validator that confirms visible nonblack
     output and fails below 60 FPS after warmup.

5. Enable WebKit only after the shared-surface path is real.
   - WebKit must use the same render-node/shared-resource/fence contract as
     native Mesa clients.
   - Do not treat render-node existence, D3DKMT readiness, or environment
     variables as proof of an accelerated WebKit surface contract.

## Acceptance Gate

Hyper-V OpenGL is complete only when:

- `mesaglfeature` passes on Hyper-V D3D12 without tracing or device removal.
- A Mesa Wayland client presents D3D12-rendered frames through a shared
  GPU-resource/fence path.
- The desktop-launched 480p 3D demo is visible, closeable, resizable, and
  sustains more than 60 FPS after warmup.
- `fbstat` can honestly report `backend_opengl_submit 1` on Hyper-V.
- WebKit acceleration uses that same validated contract and remains gated off
  when the contract is unavailable.
