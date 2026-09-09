# GPU And OpenGL Plan

Last updated: 2026-05-25

This companion plan is intentionally short. The source-audited checklist of
open work lives in `GPU_REMAINING_GAPS.md`; completed substrate facts live in
`SKILL.md` under "Validated GPU Baselines".

## Current Status

- KVM/virtio-gpu/virgl is the validated OpenGL-submit backend.
- Hyper-V GPU-PV has DXG transport, D3DKMT readiness, WSL-shaped object
  lifetime coverage, and a D3D12 shared-resource Wayland admission path, but
  not completed OpenGL submit/present support.
- Hyper-V must keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT` false until the D3D12
  render, shared-resource present, fence, and 480p >60 FPS demo path is proven.
- The 3D demo is a real native Wayland EGL window with an in-surface RTC FPS
  overlay. On Hyper-V visible/app-loop FPS is zero-credit unless it is tied to
  native D3D12 display completion for the same resource generation.
- WebKit acceleration is correctly gated by `FB_GPU_BACKEND_F_OPENGL_SUBMIT`;
  Hyper-V falls back safely until the same shared-surface contract works for
  native Mesa clients.

## Active Milestones

1. Keep WSL-style DXG object semantics green as regression coverage.
   - `dxgprocess`, typed handle-table, shared-resource, shared-sync, sync-file,
     packet-shape, and same-adapter trace replay checks live in the core GPU
     validator and should stay green before touching native present gates.

2. Keep the real Mesa D3D12 path source-correlated.
   - Same-adapter WSL replay and kernel host-saw diagnostics must remain the
     source of truth when packet fields are transformed or validated.

3. Build the Hyper-V D3D12 native present path.
   - The protocol/import path for D3D12 NT shared-resource fds plus DXG
     sync-file acquire exists and is fail-closed at the selected
     GPU-P/DDA `dxg-resource-scanout-bind` handoff.
   - The next real implementation must provide a non-custom GPU-P/DDA display
     bind and completion source with nonzero present id/completed counters for
     the same resource generation.
   - Keep framebuffer BO present/direct scanout as a separate baseline path; it
     does not make D3D12 render targets native compositor inputs.

4. Add validators for the missing contracts.
   - Native-completion validators must require present id/completed counters,
     callback/release ordering, close-before-signal cancellation, and cleanup
     balance after a real display completion.
   - The finite desktop 3D validator must confirm visible, closeable, resizable
     480p output and fail below 60 FPS after warmup.

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
