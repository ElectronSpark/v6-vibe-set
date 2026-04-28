# WebKit TODO

This file is now the short, current checklist.  The retired WebKit source
override map lives in `WEBKIT_GAP_MAP.md`.

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
- [x] Headless desktop boot with `webkit=1` reached
  `wlcomp: client title: Google` and survived a short idle smoke run.
- [x] Remove the repo WebKitGTK source override files; active override count is
  `0`.

## Build And Test Checkpoint

- [x] `cmake --build build-x86_64 --target kernel rootfs -j2`
- [x] `cmake --build build-x86_64 --target webkit-runtime-check -j2`
- [x] Guest `webkitabitest`
- [x] Guest `webkitnettest`
- [x] Headless `webkit=1` MiniBrowser autostart reached the Google page title.
- [x] Fresh container rebuild after removing `xv6-os-dev`, `xv6-os-base:local`,
  and `build-x86_64`; `xv6-images` and `webkit-runtime-check` passed.
- [x] `ports/webkit/apply-xv6-overrides.sh` is a clean no-op when no source
  overrides are present.
- [x] No Yocto or other new external dependency was added.

## Remaining Validation Ladder

- [ ] Launch MiniBrowser specifically to `about:blank`.
- [ ] Load local HTML in MiniBrowser.
- [ ] Load plain HTTP in MiniBrowser.
- [x] Load HTTPS through GLib/GIO/OpenSSL.
- [x] Load `https://www.google.com/`.
- [ ] Submit a Google search with JavaScript enabled.
- [ ] Navigate repeatedly for several minutes.
- [ ] Close and reopen MiniBrowser.
- [ ] Leave MiniBrowser idle long enough to catch delayed freezes.
- [x] Repeat the validation after a fresh container build.

## Patch Retirement Rule

- [x] Do not remove a WebKit override until the corresponding kernel/ABI reproducer passes.
- [x] Retire the repo-carried WebKitGTK source override files.
- [ ] Rebuild WebKit from clean upstream source without repo overrides.
- [ ] Re-run the validation ladder.
- [ ] Record any new source fixes as a real in-tree port or explicit patch
  series if the clean rebuild exposes missing xv6 behavior.

## Active Policy Gaps

- `AF_UNIX SOCK_SEQPACKET` is still an intentional skip; the current staged
  runtime uses the already-validated stream IPC path.
- JSC stays interpreter-only until executable-memory and architecture coherency
  testing is expanded.
- Disk network cache remains guarded until the full MiniBrowser ladder passes.
- WebKit sandbox/namespace support remains a product/kernel policy gap because
  xv6 does not provide Linux namespace, seccomp, or bubblewrap primitives.
