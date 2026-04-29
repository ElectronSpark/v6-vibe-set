# WebKit TODO

This file is now the short, current checklist.  The retired WebKit source
override map lives in `WEBKIT_GAP_MAP.md`; GPU/OpenGL work lives in
`GPU_OPENGL_PLAN.md`.

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
- [x] Fresh host rebuild after override retirement passed `image` and
  `webkit-runtime-check`, then KVM WebKit validation reached Google Search,
  GitHub, YouTube, and xv6-public GitHub pages.
- [x] GPU gate fallback smoke: KVM/GTK `webkit=1` launched MiniBrowser with
  `accel=0`, kept the software/low-feature environment, and showed no fatal
  graphics/kernel fault during startup/idle.
- [x] GPU gate opt-in smoke: KVM/GTK `webkit=1 webkit_accel=1` launched
  MiniBrowser with `accel=1`, selected the virgl Mesa environment, and stayed
  visually clean during startup/idle.  This does not yet prove active WebKit GPU
  compositing; see `GPU_OPENGL_PLAN.md`.
- [x] Local WebKit GPU smoke page is staged in the rootfs and loads through a
  file URI.
- [x] WebKit API-level GPU smoke client added: `/bin/webkitgpusmoke` forces
  `enable-webgl=TRUE` and hardware acceleration policy `ALWAYS` before loading
  the local smoke page.
- [x] Clean upstream WebKit port rebuild passed with override count `0`; the
  dependency rebuild path now disables unsupported target helper executables in
  ATK, gdk-pixbuf, and Pango instead of carrying WebKit source overrides.
- [x] WebKit API close/reopen smoke passed with `webkit_reopen=2`: two
  independent `/bin/webkitgpusmoke` launches loaded the local smoke page,
  exited cleanly, and the desktop shut down without fatal page faults, stale
  helper processes, or virtio-gpu failures/timeouts.
- [x] Fontconfig warning bursts from unsupported `48-guessfamily.conf` and
  `49-sansserif.conf` snippets are no longer activated or staged in the xv6
  rootfs.
- [ ] WebKit WebGL is still unavailable in the current GTK/Wayland runtime even
  with `webkit_accel=1 webkit_api_smoke=1 webkit_gpu_smoke=1`; the current
  suspected blocker is WebKit's ANGLE platform-display binding, tracked in
  `GPU_OPENGL_PLAN.md`.

## Build And Test Checkpoint

- [x] `cmake --build build-x86_64 --target kernel rootfs -j2`
- [x] `cmake --build build-x86_64 --target webkit-runtime-check -j2`
- [x] Guest `webkitabitest`
- [x] Guest `webkitnettest`
- [x] Headless `webkit=1` MiniBrowser autostart reached the Google page title.
- [x] Fresh container rebuild after removing `xv6-os-dev`, `xv6-os-base:local`,
  and `build-x86_64`; `xv6-images` and `webkit-runtime-check` passed.
- [x] Fresh host rebuild after removing `build-x86_64`;
  `cmake --build build-x86_64 --target image webkit-runtime-check -j2`
  passed.
- [x] `ports/webkit/apply-xv6-overrides.sh` is a clean no-op when no source
  overrides are present.
- [x] No Yocto or other new external dependency was added.

## Remaining Validation Ladder

- [x] Launch MiniBrowser specifically to `about:blank`.
  KVM/headless validation with
  `webkit_url=about:blank webkit_timeout_ms=18000 video=1280x800` reached the
  `WebKitGTK MiniBrowser` Wayland title, then timed out and reaped helpers
  cleanly without fatal page faults.
- [x] Load local HTML in MiniBrowser.
- [x] Load plain HTTP in MiniBrowser.
  KVM/headless validation with `webkit_http_smoke=1 webkit_timeout_ms=22000`
  launched an in-guest loopback HTTP server, loaded
  `http://127.0.0.1:18080/`, observed Soup status `200`, and changed the page
  title to `xv6 plain HTTP smoke`.
- [x] Load HTTPS through GLib/GIO/OpenSSL.
- [x] Load `https://www.google.com/`.
- [x] Submit a Google search with JavaScript enabled.
- [x] Navigate repeatedly for several minutes.
- [x] Close and reopen the WebKitGTK runtime through the API smoke harness.
  MiniBrowser manual close/reopen remains part of the interactive ladder.
- [x] Leave MiniBrowser idle long enough to catch delayed freezes.
- [x] Repeat the validation after a fresh container build.
- [x] Repeat the validation after a fresh host build with KVM.

## Patch Retirement Rule

- [x] Do not remove a WebKit override until the corresponding kernel/ABI reproducer passes.
- [x] Retire the repo-carried WebKitGTK source override files.
- [x] Rebuild WebKit from clean upstream source without repo overrides.
- [x] Re-run the automated GPU/local-file/close-reopen validation ladder.
- [x] Record new source fixes as real in-tree port patches rather than WebKit
  source overrides: ATK/gdk-pixbuf/Pango target helper builds are now disabled
  at the port layer, and Fontconfig drops noisy unsupported config activations.

## Active Policy Gaps

- `AF_UNIX SOCK_SEQPACKET` is still an intentional skip; the current staged
  runtime uses the already-validated stream IPC path.
- JSC stays interpreter-only until executable-memory and architecture coherency
  testing is expanded.
- Disk network cache remains guarded until the full MiniBrowser ladder passes.
- WebKit sandbox/namespace support remains a product/kernel policy gap because
  xv6 does not provide Linux namespace, seccomp, or bubblewrap primitives.
