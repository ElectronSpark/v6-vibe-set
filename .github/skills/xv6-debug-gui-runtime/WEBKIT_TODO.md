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
- [x] Keep the WebKitGTK source overrides narrow and reproducible.  Active
  overrides now live under `ports/webkit/overrides/webkitgtk-2.42.5/` and are
  applied with `ports/webkit/apply-xv6-overrides.sh` for clean source rebuilds.
- [x] Fresh host rebuild after override retirement passed `image` and
  `webkit-runtime-check`, then KVM WebKit validation reached Google Search,
  GitHub, YouTube, and xv6-public GitHub pages.
- [x] GPU gate fallback smoke: KVM/GTK `webkit=1` launched MiniBrowser with
  `accel=0`, kept the software/low-feature environment, and showed no fatal
  graphics/kernel fault during startup/idle.
- [x] GPU gate opt-in smoke: KVM/GTK `webkit=1 webkit_accel=1` launched
  MiniBrowser with `accel=1`, initialized virtio-gpu/virgl, selected the virgl
  Mesa environment, and stayed visually clean during startup/idle.  This is the
  safe hybrid mode: GPU device and Mesa/virgl are enabled, while WebKit content
  presentation remains on the coordinated software drawing path until the GTK
  accelerated backing-store contract is implemented.
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
- [x] KVM/GTK `virtio-gpu-gl` validation with
  `webkit=1 webkit_accel=1 webkit_api_smoke=1 webkit_gpu_smoke=1
  webkit_timeout_ms=25000 video=1280x800` loaded and visibly painted the local
  WebKit render-smoke page, changed the page title to `xv6 WebKit GPU Smoke`,
  timed out cleanly, and showed no fatal page faults, coredumps, panics,
  `vma_alloc` warnings, or virtio-gpu failures.
- [x] Forced WebKit WebGL now reaches Mesa instead of failing as unavailable:
  KVM/GTK `virtio-gpu-gl` with
  `webkit=1 webkit_accel=1 webkit_api_smoke=1 webkit_webgl_smoke=1
  webkit_timeout_ms=0 video=1280x800` has reached
  `xv6 WebKit WebGL Spherical Poly: webgl ready` and then
  `xv6 WebKit WebGL Spherical Poly: webgl spherical poly`, proving a WebGL
  context and first rendered frame.
- [x] Fix the compositor-side WebKit launch regression where `wlcomp` treated a
  normal waitable launcher child as authority to kill the whole process group
  and destroy matching Wayland clients.  Normal reap now only frees the launcher
  tracking slot; explicit close/force-close remains responsible for client
  teardown.
- [x] KVM/GTK `virtio-gpu-gl` WebKit GPU-mode validation reaches real Google
  HTTPS paths with virgl capsets available and `webkit_accel=1`.  With
  `webkit_url=https://www.google.com/robots.txt`, the page reaches
  `load-finished`, `readyState=complete`, and visibly paints Google text.  With
  the default Google Search compatibility endpoint
  (`https://www.google.com/search?q=xv6&gbv=1`), MiniBrowser reaches the
  `Google Search` title, completes the first document, and then follows Google's
  expected anti-automation redirect.  This validates the safe hybrid GPU-mode
  baseline, not active WebKit accelerated backing-store presentation.
- [x] Remove `WEBKIT_XV6_SKIP_INITIAL_EMPTY_RENDER=1` from the accelerated
  MiniBrowser environment.  With the skip still set, the Google surface stayed
  transparent/dark; without it, WebKit paints its page surface.  The accepted
  GPU-mode launcher now keeps WebKitGTK accelerated compositing disabled because
  forced backing-store presentation is still blank/crash-prone.
- [x] Hot-patch the currently staged WebKit runtime for the
  `ConnectionUnix.cpp` full-length attachment-bearing `sendmsg()` result.  The
  source override remains in-tree for the next clean WebKit runtime rebuild.
- [ ] Forced WebKit WebGL is not stable yet.  The current blocker is a later
  WebKit/UI process crash after the first rendered frame.  Track the fix,
  repeated close/reopen validation, and the remaining accelerated-compositing
  contract in `GPU_OPENGL_PLAN.md`.
- [ ] Rebuild or restage the WebKitGTK runtime from source with the current
  `ConnectionUnix.cpp` override so the binary hot patch is no longer needed.
- [ ] Active WebKit accelerated compositing is still experimental.  The durable
  blocker is WebKitGTK's ANGLE/dmabuf platform-display and GTK accelerated
  backing-store contract, which xv6 must either implement or bridge with
  matching lifetime/fence semantics.  Forcing the path today can load the DOM
  but leaves the web content blank, and stale runtimes can crash in
  `webkitWebViewBaseDraw()` when the accelerated backing store is null.

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
- [x] `ports/webkit/apply-xv6-overrides.sh` can apply the repo-carried
  WebKitGTK 2.42.5 overrides to a clean source checkout.
- [x] No Yocto or other new external dependency was added.

## Remaining Validation Ladder

- [ ] Forced WebKit WebGL smoke can stay open, animate, close, and reap helpers
  without a fatal page fault or coredump.
- [ ] Forced WebKit WebGL smoke survives repeated close/reopen with stable
  `_fbstat` virgl/BO counters and no stale helper processes.
- [ ] Manual MiniBrowser browsing remains stable with the default software
  profile after the forced-WebGL changes.
  2026-04-30 KVM/GTK `webkit=1 video=1280x800` autostart reached
  `wlcomp: client title: WebKitGTK MiniBrowser`, `client app_id: MiniBrowser`,
  launched both WebKit helper processes, and stayed in the MiniBrowser manual
  main loop without a fatal kernel or graphics fault.  It did not reach the
  Google title in the short smoke window.
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
- [x] Launch Google with `webkit_accel=1` and virgl enabled.  The validated safe
  GPU-mode URLs are `https://www.google.com/robots.txt` for visible painted
  Google content and `https://www.google.com/search?q=xv6&gbv=1` for the
  default Google Search compatibility path.  The modern Google homepage and
  forced WebKit accelerated backing store remain separate compatibility/stress
  targets.
- [x] Navigate repeatedly for several minutes.
- [x] Close and reopen the WebKitGTK runtime through the API smoke harness.
  MiniBrowser manual close/reopen remains part of the interactive ladder.
- [x] Leave MiniBrowser idle long enough to catch delayed freezes.
- [x] Repeat the validation after a fresh container build.
- [x] Repeat the validation after a fresh host build with KVM.

## Patch Retirement Rule

- [x] Do not remove a WebKit override until the corresponding kernel/ABI reproducer passes.
- [x] Retire stale broad WebKitGTK source override files.
- [x] Rebuild WebKit from a clean upstream source tree plus the current narrow
  repo-carried overrides.
- [x] Re-run the automated GPU/local-file/close-reopen validation ladder.
- [x] Record new source fixes as durable repo state rather than an untracked
  external checkout: ATK/gdk-pixbuf/Pango target helper builds are disabled at
  the port layer, Fontconfig drops noisy unsupported config activations, and
  WebKit source fixes live under `ports/webkit/overrides/webkitgtk-2.42.5/`.

## Active Policy Gaps

- `AF_UNIX SOCK_SEQPACKET` is still an intentional skip; the current staged
  runtime uses the already-validated stream IPC path.
- JSC stays interpreter-only until executable-memory and architecture coherency
  testing is expanded.
- Disk network cache remains guarded until the full MiniBrowser ladder passes.
- WebKit sandbox/namespace support remains a product/kernel policy gap because
  xv6 does not provide Linux namespace, seccomp, or bubblewrap primitives.
