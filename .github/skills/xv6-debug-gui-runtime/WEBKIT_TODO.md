# WebKit TODO

This file is now the short, current checklist.  Kernel-side details and the
override-to-kernel mapping live in `WEBKIT_GAP_MAP.md`.

## Current Status

- [x] Do not introduce Yocto or any new external dependency for this repo.
- [x] Keep WebKit runtime staging optional and container-build friendly.
- [x] Add `webkit-runtime-check` for staged runtime/rootfs validation.
- [x] Add in-guest WebKit ABI and network probes:
  - `webkitabitest`: `16 passed, 1 skipped, 0 failed`.
  - `webkitnettest`: passes with the built-in loopback TCP server.
- [x] Fix kernel AF_UNIX/SCM_RIGHTS close and lifetime behavior needed by WebKit IPC.
- [x] Fix ext4-backed sparse `ftruncate` growth for cache/file semantics.
- [x] Narrow WebKit/MiniBrowser exit tracing so serial output stays usable.
- [x] Enlarge MiniBrowser/GTK UI text for the xv6 1024x768 desktop.
- [x] Boot desktop with `webkit=1` and complete a user-observed visual smoke pass.

## Build And Test Checkpoint

- [x] `cmake --build build-x86_64 --target kernel rootfs -j2`
- [x] `cmake --build build-x86_64 --target webkit-runtime-check -j2`
- [x] Guest `webkitabitest`
- [x] Guest `webkitnettest`
- [x] No Yocto or other new external dependency was added.

## Remaining Validation Ladder

- [ ] Launch MiniBrowser specifically to `about:blank`.
- [ ] Load local HTML in MiniBrowser.
- [ ] Load plain HTTP in MiniBrowser.
- [ ] Load HTTPS through GLib/GIO/OpenSSL.
- [ ] Load `https://www.google.com/`.
- [ ] Submit a Google search with JavaScript enabled.
- [ ] Navigate repeatedly for several minutes.
- [ ] Close and reopen MiniBrowser.
- [ ] Leave MiniBrowser idle long enough to catch delayed freezes.
- [ ] Repeat the validation after a fresh container build.

## Patch Retirement Rule

- [x] Do not remove a WebKit override until the corresponding kernel/ABI reproducer passes.
- [ ] Rebuild WebKit from clean source plus remaining overrides.
- [ ] Re-run the validation ladder.
- [ ] Retire only the overrides proven unnecessary by the rebuilt browser.
- [ ] Record the kernel/source commit and passing test for each retired override.

## Active Policy Gaps

- `AF_UNIX SOCK_SEQPACKET` is still an intentional skip; WebKit keeps the
  `SOCK_STREAM` IPC override for this pass.
- JSC stays interpreter-only until executable-memory and architecture coherency
  testing is expanded.
- Disk network cache remains guarded until the full MiniBrowser ladder passes.
- WebKit sandbox/namespace overrides stay because xv6 does not provide Linux
  namespace, seccomp, or bubblewrap primitives.
