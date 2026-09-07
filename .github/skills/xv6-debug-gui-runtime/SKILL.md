---
name: xv6-debug-gui-runtime
description: 'Use when: operating or auditing the xv6 KDE desktop, SDL/virgl GUI, graphical mouse or keyboard behavior, window controls, terminal/editor/browser readiness, renderer provenance, or host/guest captures. Use Linux GUI ABI and Wayland bridge skills for targeted kernel/input/presentation diagnosis.'
argument-hint: 'Describe the GUI symptom and latest runtime observation'
---
# xv6 GUI Runtime Debugging

Use the current KDE desktop, normal application launchers, and the owned SDL/
virgl runner. Read [the active work plan](../../../docs/active-work-plan.md) for
current priorities and acceptance gates; keep run IDs, results, and open-work
status in audit documents and that plan rather than this skill.

## Scope and routing

- An instruction to launch or operate the GUI authorizes the requested audit.
  Record failures and bounded evidence within that scope. It does not by itself
  authorize source fixes, rebuilds, diagnostic reducers, changed browser flags,
  kernel instrumentation, or a different workload. Apply already-authorized
  implementation scope when present; do not seek duplicate permission.
- Use [build reproduction](../xv6-debug-build-repro/SKILL.md) for artifact identity
  or an authorized rebuild, [live GDB](../xv6-debug-live-gdb/SKILL.md) for an
  authorized freeze capture, and [fluid triage](../xv6-debug-fluid-triage/SKILL.md)
  to separate observations from hypotheses.
- Use [Linux GUI ABI](../xv6-linux-gui-abi/SKILL.md) for Linux syscall, DRM, PTY,
  signal, process, and IPC gaps; use [the Wayland bridge skill](../xv6-wayland-kernel-bridge/SKILL.md)
  for current session, input, compositor, and presentation boundaries. Verify
  actual source ownership before proposing a change.
- Retired `wlcomp`/`desktop` generation, old custom-musl GUI patches, and archived
  WebKit/Hyper-V recipes are not the current KDE startup workflow. Consult the
  historical references below only for a matching legacy investigation.

## Establish the runtime and ownership

1. Apply root `AGENTS.md`: guarded `/home/es/.local/bin/rg`, source-only bounded
   searches, the global one-large-search lock, and explicit checked artifact
   files via `scripts/audit/safe-rg-artifact.sh`. Never use banned recursive
   options or generated-tree recursion. Wait synchronously for the exact running
   tool/process handle before another search or heavy command.
2. Before dispatching any VM work, verify no VM worker is active and run
   `scripts/launch/qemu-exact-inventory.sh --require-zero`. It scans exact
   `/proc/*/exe` identities; do not use `pgrep` or signal an unrelated process.
   Authorize one VM owner; all other lanes must be explicitly NO-BOOT.
3. Record UTC start, source/submodule identity and dirty state, chosen kernel,
   symbol file and filesystem receipt, artifact hashes or recorded integrity
   manifest, actual QEMU executable/arguments, display/input policy, guest mode,
   and host geometry. An existing image does not prove a fresh source build.
4. For an existing-image GUI audit, the normal command is:

   ```sh
   AUTO_BUILD=0 bash scripts/launch/launch-gui.sh
   ```

   Inspect `scripts/launch/launch-gui.sh` for current defaults and any overrides.
   It prefers a complete reproduction receipt; `XV6_RECEIPT` selects an explicit
   receipt. Its current defaults are SDL, `virtio-vga-gl-primary`, corrected APT
   SDL modules, six vCPUs, 8 GiB, and `QEMU_INPUT=virtio`. Defaults are not evidence
   of effective arguments: retain the actual owned QEMU command line.
5. Keep `scripts/launch/run-owned-qemu.sh` as the ownership boundary: immutable
   base with a private overlay, token/PID/start-time-bound process group,
   synchronous reap, bounded cleanup, then an exact zero-QEMU check. Retain the
   launcher handle throughout the audit; never abandon it or leave QEMU running.
   Do not start another VM lane until cleanup and exact zero both pass.
6. Complete the audit through the requested graphical channel. If serial
   diagnostics are within scope, keep commands short, use marker variables,
   account for the first-character drop after bracketed paste, wait on the actual
   command, and obtain a fresh prompt. Serial silence is not a completion verdict.

## Operate and capture semantic changes

- Use the ordinary desktop/menu entry for the application. Record its actual
  launch flags and process role before comparing with a diagnostic launcher.
  A normal launcher and a forced accelerated/multiprocess launch are different
  workloads. Do not silently substitute one for the other.
- Use token-matched, foreground-checked host input and current window geometry.
  Inspect `scripts/gpu/capture-host-screen.sh`,
  `scripts/gpu/probe-owned-qemu-sdl-input.sh`, and
  `scripts/gpu/fit-owned-qemu-window.sh` before using their capture/input/fitting
  interfaces. A helper's successful return proves delivery was attempted, not
  that the intended widget changed.
- Capture the visible before state, send one intentional action, wait for the
  bounded UI response, and capture the after state. Label the semantic result:
  selection, activated application, changed view, scrolled content, moved window,
  resized client, mapped menu, changed field, or unchanged visible state.
- Reacquire the screenshot and coordinate mapping after movement, resizing,
  menus, toolbar additions, zoom, split views, tab-layout changes, or fitting.
  Distinguish host-client coordinates from guest-content coordinates and keep
  offsets/scaling with the action receipt. Never reuse a layout's coordinates
  merely because the target has the same name.
- Establish single-click versus double-click behavior for the current view.
  Desktop selection and a file picker's single-click activation can differ;
  double-clicking an already-activating folder can traverse two levels. Record
  the destination actually displayed. A screenshot filename is not an assertion.
- Exercise independent operations as requested: menus and dismissal; folder view
  modes/split panes/navigation; window move, resize, maximize, restore, minimize
  and taskbar restore; editor menus/input/save/reopen; browser controls and page
  input. Save only within the requested scope and retain whether storage was an
  ephemeral overlay, a single session, or actually tested across reboot.
- For hover, record target, pointer position, dwell, current selection and visible
  result. Preserve mixed outcomes across menus/toolkits. One working submenu does
  not validate all hover, and one unchanged capture does not isolate timing,
  input delivery, focus, or toolkit policy. A click fallback is separate evidence.

## Keep evidence claims at their observed layer

- Prove fresh logging: inspect whether each producer truncates or appends. The KDE
  session's `/kde-session-plasma-child.log` is opened with `O_APPEND` in
  `scripts/image/kde-session.c`; an old renderer line can survive in the base
  image. Establish a same-run boundary using a recorded pre-run byte offset,
  producer start/identity or unambiguous boot marker, then capture the new segment.
  Do not delete inherited evidence or call an unbounded whole-file match fresh.
- Separate configured virgl, fresh boot/device/scanout evidence, compositor GL
  renderer, and browser GPU status. A virgl device plus working desktop does not
  make every application accelerated. Match renderer/process/flags to the same
  run; a historical string supplies context only.
- Capture both host-visible output and an independent guest scanout image when
  the acceptance gate requires both. QMP `screendump` may return `no surface` on
  a GL route. Record that failure and retain host PNGs; do not invent a guest
  image, claim exact host/guest fitting, or infer a rendering failure from that
  capture limitation. Record host notifications/occlusion and fitting overrides.
- Keep timestamps, action logs, semantic annotations, image hashes and provenance
  together. Qualify keyboard evidence by injection route: virtual keyboard input
  does not establish physical host-keyboard behavior.
- A disappearing window proves visible disappearance. Confirm application/thread
  ownership and process status before reporting an exit, crash, or signal cause.
  An abnormal-shutdown prompt on relaunch supports that occurrence; do not apply
  it to another disappearance without its own evidence. Same-session retries
  with one profile are not independent fresh-boot samples.
- Correlate logs by run, timestamp, PID/TGID and role. A nearby killed helper
  thread or duplicate event in two logs does not establish a browser/kernel/GPU
  cause or two independent failures. Absence of matching panic/quarantine/stall
  markers is limited to the inspected files and time interval.
- Distinguish mapped terminal windows from a ready shell, a file-association
  chooser from lost data, and global input from application-specific readiness.
  Keep intended behavior, observed result and source-policy explanation separate.
- Simple UI success, animation, or a 60 Hz clock line earns no measured FPS,
  media, decode, audio, long-stress or performance-parity credit. A header-only
  WAV is not audio. Apply the active plan's sustained-content, paired-capture,
  matched-control and sample-count gates only with the required evidence.

## Close the audit and choose further reading

Retain the owned termination request, synchronous launcher result, exact QEMU
inventory, overlay cleanup and any temporary fixture cleanup. Host-controlled
termination is not an in-guest shutdown test. Report what changed visibly, what
failed, and which stronger claims remain untested; link receipts from the audit
and put follow-up work in the single active plan.

The reusable evidence method comes from the
[general GUI audit](../../../docs/gui-progress-audit-20260907.md) and
[mouse exploration audit](../../../docs/gui-mouse-audit-20260907.md). Read those
for concrete observations and current follow-ups, without copying run IDs or
one-off results into this entrypoint.

Read only the historical reference matching a legacy contract:

- [wlcomp and WebKit runtime notes](references/historical-wlcomp-webkit.md): old
  compositor generation, NetSurf/WebKit startup, GLib child watches and readiness
  pitfalls. Retired launch and workaround instructions are historical only.
- [Hyper-V native-present contracts](references/historical-hyperv-native-present.md):
  display-bind authority, same-run evidence seals, GPU-P/DDA separation and
  fail-closed native-present proof. These gates are distinct from virgl.
- [GPU/WSL/Hyper-V module map](references/historical-gpu-module-map.md): old module
  ownership, object lifetime, process handle tables, transport and Nouveau gaps;
  verify paths and state against current source before using the map.
- [DRM/KMS/FPS/WebKit validation contracts](references/historical-drm-validation.md):
  metadata versus scanout, callback/fence lifetime, anti-inflation and parser
  rejection invariants. Old pass/deferred status is not current acceptance.
