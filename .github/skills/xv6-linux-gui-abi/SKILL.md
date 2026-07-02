---
name: xv6-linux-gui-abi
description: 'Use when: working on xv6-os Linux GUI ABI compatibility, X11/XWayland, imported host GUI programs, Chromium/WebKit GUI stress, AF_UNIX/SCM_RIGHTS/SCM_CREDENTIALS, D-Bus Unix sockets, procfs/fdtable/epoll/futex issues exposed by GUI apps, or desktop launcher ABI policy.'
---

# xv6 Linux GUI ABI

## Scope

Use this skill for Linux GUI compatibility work where a host-built or imported
Linux GUI program exposes an xv6 kernel/user ABI mismatch. X11, XWayland,
Wayland, WebKit, Chromium, GTK/Tk, D-Bus, AF_UNIX, procfs, fd tables, epoll,
poll, futex, clone, pidfd, and process lifecycle all belong here when the
symptom is a GUI program failing, hanging, not mapping, not rendering, or not
tearing down cleanly.

Do not use this skill as an app-porting playbook. Host GUI apps are probes.
The deliverable is Linux ABI compatibility or a reduced mismatch, not a custom
launcher, patched app, or one-off packaging workaround.

Current KDE/Chromium work skips Weston unless `docs/active-work-plan.md`
explicitly reopens that lane.

## Reducer-First Debugging

- Default to localizing mismatches with the smallest program that reproduces the
  suspected Linux ABI behavior.
- Write tiny host-built or guest-built reducers for one surface at a time:
  socket option, AF_UNIX control message, epoll readiness, procfs file, fd-table
  operation, X11 request, D-Bus handshake, futex wait, clone/pidfd behavior, or
  mmap/protection edge.
- Compare the reducer against Linux semantics and adapt xv6 to match that
  reducer before rerunning the large GUI app.
- Adapt the reducer as understanding improves: add only the next syscall, flag,
  event, or data shape needed to reproduce the mismatch.
- Keep reducers deterministic and evidence-rich. Print inputs, return values,
  errno, observed events, fd identities, cmsg lengths, and lifecycle markers.
- Use the full application as the reproducer only when the failing interaction
  is too complex to isolate cheaply, such as multi-process Mojo/X11/DRI3/D-Bus
  timing or a race that disappears outside the full process graph.
- When using the full app, still reduce the observation into phase evidence and
  extract the next small reducer from the trace whenever possible.

## Routing

- Use `.github/skills/xv6-os-debugging/SKILL.md` for repo-wide QEMU, image,
  rootfs, and submodule workflow.
- If the task references an active plan, read that plan for current state and
  evidence pointers. Keep transient run IDs, URLs, and current logs in the plan,
  not in this skill.
- Route narrow kernel work to the matching module skill:
  - AF_UNIX sockets and SCM: VFS/socket code plus event-wait skills.
  - epoll/poll/kqueue readiness: `xv6-kernel-event-wait`.
  - procfs, fd tables, and path/file behavior: VFS/filesystem skills.
  - futex, clone, pidfd, wait, signals, and process lifecycle: process,
    sleep/wakeup, and syscall skills.
  - compositor event loops and input/runtime observations:
    `xv6-debug-gui-runtime` or `xv6-wayland-kernel-bridge`.

## Build Discipline

- Default to kernel-only iteration for kernel-facing GUI ABI issues:
  `cmake --build build-x86_64 --target kernel -j2`.
- Do not rebuild user programs, ports, imported host payloads, rootfs overlays,
  or the toolchain unless a changed file or trace proves that layer is involved.
- Treat broad image/rootfs targets as expensive and potentially misleading.
  Before using one, name the non-kernel artifact that changed or the evidence
  that the current image is stale.
- If rootfs/sysroot/user/ports contents changed, refresh the image before
  booting and record the refresh method.
- Run the project’s mandatory GPU/video gate after GPU/DRM/desktop-visible
  changes. A local video gate passing is a regression guard, not proof that
  every external video workload is solved.

## Evidence Discipline

- Preserve durable logs for every runtime proof.
- Capture screenshots for visual claims: before launch, after launch, after
  input, and after exit when applicable.
- If screenshot capture fails, record the capture failure explicitly and do not
  count the visual proof as passing.
- Treat "desktop only", "icon exists", or "process is alive" as incomplete
  evidence for GUI support. A mapped window, input response, render/content
  change, and clean teardown prove different layers.
- When a large app fails, name the smallest ABI surface it implicates and either
  write a reducer for that surface or explicitly record why the full process is
  the simpler reproducer for this step.

## Dynamic Role Discovery

- Guest service PIDs, TGIDs, and TIDs are dynamic.
- Never identify Chromium, NetworkService, Xwayland, D-Bus, GPU, renderer,
  desktop/compositor services, or helpers by copied numeric IDs or launch
  order.
- First derive the role in the same boot from semantic evidence: argv,
  executable path, thread name, fd/socket graph, lifecycle, surface mapping, or
  SCM/credential traffic.
- Use numeric handles only as same-run coordinates after role discovery.
- Host trace numeric IDs are also disposable; compare semantic roles and ABI
  shapes, not numbers.

## GUI Stress App Workflow

- Treat Chromium/WebKit and other large host apps as stress probes.
- Separate failures into launch, process supervision, IPC, socket connection,
  request/response, render, input, and teardown phases.
- After identifying a failing phase, stop and build a focused reproducer unless
  the phase depends on the full app's process graph or timing.
- For host HTTP or local fixture tests, prove each phase separately:
  - fixture reachable from the host;
  - app argv or navigation carries the requested URL;
  - role-derived network component exists;
  - nonblocking connect completes or reports the Linux-shaped error;
  - request bytes leave the guest;
  - response bytes arrive;
  - the page renders or fails later.
- If bytes successfully leave and return but the page does not render, continue
  from browser/render IPC and compositor evidence rather than reopening DNS or
  launchers.
- Do not reopen DNS, static PID theories, desktop launchers, or packaging unless
  new evidence points at that layer.

## KDE Plasma Responsiveness Workflow

- Check `docs/active-work-plan.md` first for which bottleneck hypotheses
  are already closed by metrics before opening a new lane. Durable routing:
  scheduler wake-to-run latency is measurable behavior-free with
  `kde_wake_to_run_trace=<N>` (see `xv6-kernel-process-scheduler`);
  per-syscall fixed cost and TLB amplification are measurable with
  `syscalltlb` (see `xv6-kernel-traps-syscalls`); present-path frame caps
  are diagnosable by comparing flush rate to presented FPS (see
  `xv6-debug-gui-runtime`).
- Use Linux KVM+virgl as a phase reference for the same host and QEMU display
  shape. Keep visual capture validity separate from phase timing: a Linux run
  can be a useful PTY/wrapper/shell or cursor-policy control even when
  screenshot, hover, or tray hashes are unusable on the host.
- For WSLg/GTK/virgl interaction runs, default to a single visible QEMU/host
  cursor. Treat guest hardware cursor images as an opt-in debugging path, and
  compare QEMU trace counts before blaming cursor pixels. If Linux on the same
  display path records zero virtio cursor update/move commands, xv6 should not
  require guest cursor queue traffic for normal interaction.
- For Plasma launch responsiveness, split Konsole into explicit phases:
  fork/exec launch call, early process/thread/fd graph, first Wayland/DBus/
  eventfd/pipe/render-node graph, first `/dev/ptmx`, first `/dev/pts/*`,
  wrapper start, marker write, shell start, and visible window/render effects.
  A late PTY in the full app is not by itself a PTY kernel bug if live PTY
  reducers are fast.
- When a GUI wait looks futex-related, prove the futex shape before patching:
  capture wait enqueue, key, value, bitset, timeout parameters, wake key,
  wake return count, and wait-woken timing. A delayed wake with matching key
  and `ret=1` points to producer progress or wake-to-run latency, not futex
  key mismatch.
- Keep broad tracing out of timing runs unless the question requires it.
  Prefer role-scoped flags such as Konsole-only futex, wake-source, scheduler,
  fd graph, or activation probes. `WAYLAND_DEBUG`, broad IPC tracing, semantic
  D-Bus monitors, and high-frequency wait sampling can change the timing they
  are meant to measure.
- Before behavior-changing scheduler, futex, poll/kqueue, AF_UNIX, or PTY
  patches, gather a reducer or a behavior-free metric that identifies the
  delayed edge. Wake-to-run latency is already measured and bounded; the
  next open metric is producer-side progress between Konsole exec and the
  first `/dev/ptmx` open.
- Preserve GPU, network, and audio guards with responsiveness proofs:
  render-node/card-node presence, virgl submit/flush/fence activity, desktop
  process health, NetworkManager/SNI or connectivity proof, and PipeWire/Pulse
  sink/monitor or playback evidence as appropriate.

## AF_UNIX / SCM ABI

- Distinguish byte-stream reads from ancillary-message receives.
- Linux stream `read()`/`readv()` consumes bytes and discards ancillary data
  attached to consumed bytes.
- Linux `recvmsg()`/`recvmmsg()` must preserve and return ancillary data when
  the caller supplies the relevant control buffer.
- A broad stream SCM discard can break Wayland/X11 fd passing. Validate AF_UNIX
  edits with a focused X11/Wayland fd-passing proof before trusting them.
- Track these as first-class ABI surfaces:
  `SCM_RIGHTS`, `SCM_CREDENTIALS`, `SO_PASSCRED`, `SO_PEERCRED`,
  `MSG_CMSG_CLOEXEC`, control-buffer truncation, nonblocking connect,
  half-close, peer shutdown, packet boundaries, and epoll/poll wakeups.

## X11 / XWayland ABI

- Test X11 in layers: socket connect, XCB/Xlib event delivery, map/configure,
  input, selection/clipboard, MIT-SHM, DRI3/Present fd passing, and WM_DELETE
  teardown.
- Xwayland/DRI3 fd passing is an AF_UNIX and DRM lifetime stressor. Teardown
  failures may appear later as virgl, DRM, or compositor errors.
- For close/teardown bugs, preserve both guest logs and host/QEMU stderr where
  possible; late host graphics errors can be more informative than the client’s
  exit status.
- Keep decorations and launchers out of the kernel ABI path unless the failure
  reduces to a Linux ABI requirement. Prefer direct ELF desktop entries where
  possible.

## Plan And Skill Hygiene

- Keep active plans compact: current state, guardrails, open work, and evidence
  pointers.
- Keep reusable workflow lessons in skills.
- Do not put one-off URLs, run IDs, temporary ports, or current timeout results
  into skills; keep those in the plan or the run artifact.
- Use checklist rows in plans for work items and evidence, but preserve enough
  prompt context and guardrails for a fresh session.
