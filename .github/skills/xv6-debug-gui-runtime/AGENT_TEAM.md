# xv6 GPU Agent Team

This file defines a working role map for long xv6 GPU/GUI sessions. These are
responsibilities, not permission to spawn subagents automatically. Only spawn
parallel agents when the user explicitly asks for agent delegation; otherwise
use this as a checklist for local work.

## Coordination Rules

- Keep one owner for the active blocker. Do not split a task if the next command
  depends on its result.
- Prefer concrete evidence over narration: inspect, edit, build, deploy, and
  validate before summarizing.
- Before changing code, check dirty state in the root repo and nested
  `kernel`, `user`, and `ports` repos. Never revert unrelated dirty work.
- Do not redeploy over a running freeze sample or long GPU trace until evidence
  has been collected.
- Keep Hyper-V capability claims honest. `DXG_TRANSPORT` and `D3DKMT` readiness
  do not imply `OPENGL_SUBMIT`.
- Use pure C guest validators where possible. The guest shell is primitive and
  long shell scripts are fragile.
- Every handoff must name the exact image deployed, command line, last passing
  probe, last failing probe, and next command to run. Do not leave only a broad
  goal such as "continue GPU work."
- When the user asks for visible progress, prefer a screenshot, finite FPS run,
  or focused GUI smoke before deeper protocol archaeology.

## Handoff Template

Record this before stopping a long GPU session:

- Deployed VHDX:
- Guest command line:
- Host GPU and selected adapter:
- Last build commands and result:
- Last deploy commands and result:
- Last passing guest probe:
- Last failing guest probe:
- Kernel/user-space trace files:
- Current blocker owner:
- Next exact command:

## Roles

### Integrator

- Owns the current blocker, applies patches, runs builds, deploys images, and
  updates `GPU_REMAINING_GAPS.md`.
- Keeps the critical path moving. If a long serial probe is running, use local
  inspection for non-overlapping prep work.
- Ensures final claims match the newest user request and the latest evidence.

### WSL Reference Scout

- Reads WSL2 Linux `dxgkrnl` sources and captures same-adapter live traces.
- Keeps Intel and NVIDIA evidence separate. Do not use an Intel WSL trace as
  proof for an NVIDIA Hyper-V failure.
- Records the exact ioctl sequence, private payload sizes, allocation order,
  return status, fence values, and cleanup behavior.

### DXG Kernel Worker

- Owns `kernel/kernel/dev/hyperv_input.c`,
  `kernel/kernel/inc/uabi/d3dkmthk.h`, and the relevant `/dev/dxg` diagnostics.
- Focuses on WSL-style object lifetime, handle ownership, private payload
  forwarding, VM-bus packet layout, memory pinning/cacheability, residency,
  GPUVA, sync objects, HW queues, and cleanup.
- When changing a packet before forwarding, adds or checks kernel-side evidence
  that shows what the host actually received. User-space `LD_PRELOAD` traces
  only show pre-kernel ioctl arguments.

### Mesa And Wayland Present Worker

- Owns Mesa/Wayland paths under `ports/mesa` and `ports/wayland`.
- Keeps native Wayland EGL, `mesawlegl`, `mesademo`, FPS overlays, resize/close
  behavior, and compositor import/present behavior consistent.
- Prioritizes a D3D12 shared-resource/fence present path over further demo-side
  tricks once the demo is proven to render through D3D12 but still presents via
  software/readback.

### Validation Runner

- Owns reproducible build/deploy/run commands and screenshots.
- Uses focused Hyper-V images with explicit command lines and 6 vCPUs for the
  current workflow.
- Distinguishes serial silence from guest failure. Long Mesa/DXG traces can
  emit nothing until the read window closes; poll the session, then verify VM
  state and guest freshness with short commands.
- Validates FPS with RTC-based in-surface overlay and finite post-warmup
  samples, not just title or stderr FPS.

### Documentation Reviewer

- Keeps `.github/skills`, `GPU_REMAINING_GAPS.md`, and validation scripts in
  sync with real evidence.
- Converts repeated mistakes into durable skill guidance.
- Removes stale or misleading claims, especially anything implying Hyper-V
  OpenGL is complete before the non-readback >60 FPS path is proven.

## Current Gap Ownership

- Multi-allocation `LX_DXMAKERESIDENT` host `EINVAL`: DXG Kernel Worker, with
  WSL Reference Scout providing same-adapter packet comparisons.
- D3D12 shared-resource/fence compositor present: Mesa And Wayland Present
  Worker, after the kernel can keep real D3D12 workloads alive.
- 480p resizable 3D demo above 60 FPS: Mesa And Wayland Present Worker and
  Validation Runner, measured with RTC-based in-surface FPS after warmup.
- WebKit GPU enablement: Integrator and Documentation Reviewer, gated on the
  same honest `OPENGL_SUBMIT` contract as the 3D demo.

## Current Lessons From May 2026 Hyper-V Work

- Real Mesa/NVIDIA D3D12 on xv6 now reaches successful HW-queue creation and
  real `SUBMITCOMMANDTOHWQUEUE` calls. The active blocker is not merely
  "context/submit unavailable."
- The current real blocker is the second-FBO residency transition:
  `MAKERESIDENT count=2 flags=0x1` returns host `EINVAL` and triggers
  `D3D12: Removing Device`.
- WSL/NVIDIA is the correct comparison for the RTX 4060 host path. WSL/Intel
  traces can explain general packet shape but must not be used to prove
  NVIDIA-specific ordering or private-data behavior.
- The visible 3D demo is real D3D12 rendering, but it still uses the
  software/readback presentation bridge and remains below 60 FPS. The next
  performance work is D3D12 shared-resource import/present, not more FPS-title
  or demo-loop tuning.
- WebKit acceleration must remain gated by `FB_GPU_BACKEND_F_OPENGL_SUBMIT`.
  A render node or DXG transport alone is not a durable accelerated WebKit
  surface contract.
