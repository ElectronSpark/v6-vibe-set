# Linux DRM / GUI ABI Compatibility Plan

Last updated: 2026-06-18.

This document is the handoff prompt and active checklist for the Linux GUI ABI
effort. Keep it compact enough for a fresh session to read, but do not strip
the guardrails: the guardrails are part of the work.

Older long-form evidence trails live in git history and the ABI audit docs.

## Prompt For The Next Session

Work in `/home/es/xv6-os`. Continue the Linux GUI ABI and X11 kernel-first
effort. Read this file first, then read `.github/skills/xv6-os-debugging/SKILL.md`.

Core Linux DRM/GPU ABI convergence is complete for the current x86_64 KVM/virgl
target. Stock Mesa, GBM, libdrm, EGL, and upstream Weston drive the kernel
through standard Linux-facing DRM and virtio-gpu UAPI.

The active focus is Linux GUI ABI compatibility, especially kernel-visible
X11/XWayland, AF_UNIX/SCM, procfs, epoll/poll, futex, clone/pidfd, fd-table,
socket, D-Bus, and process-supervision semantics. Imported GUI programs are
probes, not deliverables. Chromium is a stress probe: when it fails, reduce the
failure to a smaller Linux ABI mismatch.

If the environment is full-access with approval policy `never`, do not ask for
permission. Use the available access directly. Do not push without asking.

## Guardrails

- [ ] Kernel first. Do not rebuild or modify user programs, ports, imported host
      payloads, desktop scripts, or rootfs overlays unless a changed file or
      trace proves that layer is involved.
- [ ] Avoid `world` and toolchain rebuilds.
- [ ] For kernel-only changes, use `cmake --build build-x86_64 --target kernel -j2`.
- [ ] Treat the broad CMake `image` target as a build-graph footgun because it
      currently walks user/ports staging even for kernel-only work.
- [ ] Add or use a narrower kernel-image/rootfs refresh path before long GUI ABI
      loops.
- [ ] Narrow rootfs refresh path available:
      `cmake --build build-x86_64 --target rootfs-refresh -j2`, or
      `scripts/container/enter-container.sh xv6-rootfs-refresh`; it reuses the
      staged sysroot and overlay without rebuilding user or ports.
- [ ] Narrow Weston runtime refresh path available after Weston source/runtime
      edits:
      `cmake --build build-x86_64/ports --target port-weston-runtime-refresh -j2`,
      followed by the rootfs refresh target above. This reuses the configured
      Weston Meson build and avoids the aggregate `port-weston` dependency
      graph walking Mesa/Cairo when only Weston needs restaging.
- [ ] If rootfs/sysroot/user/ports contents actually changed, refresh the image
      before booting.
- [ ] Prefer the narrow rootfs script over broad rebuilds when rootfs refresh is
      enough.
- [ ] Run the mandatory video gate after GPU/DRM/desktop-visible changes:
      `REPO_ROOT=/home/es/xv6-os timeout 320 expect scripts/gpu/perf-video-gate.expect`.
- [ ] Treat every guest service PID/TGID/TID as dynamic.
- [ ] Never identify Chromium, NetworkService, Xwayland, Weston, D-Bus, GPU,
      renderer, desktop, or helpers by copied numeric IDs.
- [ ] First derive guest service roles from same-boot argv, executable path,
      thread name, fd/socket graph, lifecycle, surface, or SCM/credential
      evidence.
- [ ] Use numeric process handles only as same-run coordinates after role
      discovery.
- [ ] Every runtime proof needs durable evidence: logs plus screenshots when the
      proof is visual.
- [ ] If screenshot capture fails, record the failure explicitly.
- [ ] Do not reopen DNS, static service PID theories, desktop launchers, or
      host-program packaging unless a new minimal Linux ABI mismatch points
      there.
- [ ] Desktop entries should remain symlinks to ELF binaries where possible.
- [ ] Avoid adding launcher scripts unless Linux ABI support cannot reasonably
      make the ELF launch directly.
- [ ] The native Settings panel is allowed to mutate guest runtime state. It
      currently applies resolution presets through `FBIOPUT_VSCREENINFO` and
      network presets through Linux-shaped interface/route ioctls plus
      `/etc/resolv.conf`. Resolution changes must be confirmed from the panel
      or they automatically roll back to the previous mode; use the narrow
      `port-wayland-settings-install` target after Settings-only edits.

## Current Baseline

- [ ] DRM nodes `/dev/dri/card0` and `/dev/dri/renderD128` are present and usable.
- [ ] `drmabitest`, Mesa virgl, GBM/EGL, KMS, PRIME/dma-buf, syncobj/sync_file,
      cursor, vblank, and KMS atomic paths have passed in prior current-image
      validation.
- [ ] Host-visible blob resources remain fail-closed because this host refuses
      mappable host-visible blobs.
- [ ] `RESOURCE_BLOB` guest blobs are supported.
- [ ] Weston is the compositor; do not bring back the old compositor path.
- [ ] Latest mandatory video gate evidence is in
      `build-x86_64/perf-video-gate/run.log`.
- [ ] Latest gate result:
      `xv6-perf-video:RESULT pass fps=59.2 speed=1.004 presentedFPS=0.0 decodedFPS=59.2 dropPct=0.00 advanced=15.32`.
- [ ] Latest gate frame evidence:
      `build-x86_64/perf-video-gate/perf-video-frame.ppm`.
- [ ] Latest gate PNG evidence:
      `build-x86_64/perf-video-gate/perf-video-frame.png`.
- [ ] Keep fullscreen YouTube performance open; the local video gate being green
      does not prove YouTube is solved. Current durable YouTube evidence is
      watch-page playback, not deterministic fullscreen playback.
- [ ] Latest YouTube watch-page cursor-motion probe:
      `chromium-youtube-wayland-quicoff-long` requested `vq=hd720` on the real
      YouTube watch page, ran native Wayland Chromium with `--disable-quic`,
      and captured 8 steady plus 8 cursor-motion full-frame samples. All
      adjacent pairs advanced (`steady` about 177k-181k changed pixels;
      `cursor` about 174k-219k). Frame evidence lives under
      `build-x86_64/chromium-youtube-smoothness/chromium-youtube-wayland-quicoff-long-frames/`.
      `chromium-youtube-fullscreen-250ms-20260618a` passed 30 steady samples
      and 30 cursor-motion samples at 250 ms requested cadence with zero
      inactive adjacent crop pairs. Evidence lives under
      `build-x86_64/chromium-youtube-smoothness/`.
- [ ] Mesa Gallium VA is now staged for the KVM/virgl path:
      `ports/mesa/CMakeLists.txt` enables `-Dgallium-va=enabled`,
      `-Dvideo-codecs=all_free`, forces the libva fallback subproject, and
      cleans stale sysroot `libva*.so` regular files before install so libva
      soname symlinks can be created. The sysroot has `include/va/va.h`,
      `lib/pkgconfig/libva.pc`, `lib/libva.so -> libva.so.2`, and
      `lib/dri/virtio_gpu_drv_video.so -> ../libgallium-26.2.0-devel.so`.
- [ ] Chromium's launcher now defaults `LIBVA_DRIVERS_PATH=/lib/dri`,
      `LIBVA_DRIVER_NAME=virtio_gpu`, `--ignore-gpu-blocklist`,
      `--enable-gpu-rasterization`, and
      `--enable-features=UseOzonePlatform,VaapiVideoDecodeLinuxGL,VaapiVideoEncoder,CanvasOopRasterization`.
      The old `vaInitialize failed` line no longer appeared in the latest
      YouTube artifacts, but verbose Chromium `--vmodule=*vaapi*,*video*`
      diagnostics also did not prove a VA decoder was selected. Keep hardware
      video decode as an open proof item rather than a solved claim.
- [ ] Latest real YouTube evidence after the VA staging:
      `chromium-youtube-va-fullscreen` entered YouTube fullscreen on
      `https://www.youtube.com/watch?v=dQw4w9WgXcQ&vq=hd720&autoplay=1&mute=1`,
      captured steady and cursor-motion frame series, and adjacent whole-frame
      deltas were large in both phases. Evidence:
      `build-x86_64/chromium-youtube-smoothness/chromium-youtube-va-fullscreen.run.log`
      and `build-x86_64/chromium-youtube-smoothness/chromium-youtube-va-fullscreen-frames/`.
      A shorter default-launcher check reached the YouTube page, but captured
      too early/paused and is not a smoothness baseline.
- [ ] Chromium native-Wayland menu popup placement was fixed in Weston xdg-shell
      constraint handling. The reduced proof is
      `scripts/gpu/chromium-menu-proof.expect`. Before the fix, Chromium asked
      for a `436x710` menu and Weston configured `popup=582,-80 436x126`,
      leaving the visible top-strip menu. After the fix, the same proof
      configured `popup=582,80 436x640` and captured a full menu in
      `build-x86_64/chromium-menu-proof/chromium-menu-flip-fix.png`.

Latest audio bring-up:

- [ ] QEMU GUI launches now default `QEMU_AUDIO_BACKEND=auto` for virtio-sound:
      interactive GTK/SDL paths pick an available host backend and nographic
      stays silent. Explicit `QEMU_AUDIO_BACKEND=wav,path=...` remains useful
      for deterministic smoke evidence.
- [ ] The kernel virtio-sound playback path is interrupt-capable on a shared
      PCI IRQ line. The IRQ core now supports chained handlers for shared
      legacy INTx lines, and the virtio-sound interrupt handler ignores shared
      IRQs when its ISR status is clear.
- [ ] xv6 exposes OSS `/dev/dsp` plus a minimal Linux ALSA hardware surface:
      `/dev/snd/controlC0` and playback `/dev/snd/pcmC0D0p`. The current GUI
      image still routes ALSA's `default` PCM through libasound's file plugin
      to `/dev/dsp` for broad compatibility; Chromium's launcher sets
      `ALSA_CONFIG_PATH` and defaults `--alsa-output-device=default`.
- [ ] Verification: direct `/dev/dsp` WAV proof produced nonzero samples;
      an in-guest libasound reducer opened `default`, configured S16_LE
      stereo 48 kHz, wrote frames, and produced nonzero WAV samples; Chromium
      WebAudio smoke opened `/usr/share/alsa/alsa.conf` and `/dev/dsp` on its
      `AudioThread`, and QEMU captured
      `/tmp/xv6-chromium-audio-alsa-default.wav` with size `11474040` and
      `11403444` nonzero bytes after the header.
- [ ] Latest ALSA hardware proof: a raw ioctl reducer opened
      `/dev/snd/controlC0` and `/dev/snd/pcmC0D0p`, queried card/PCM info,
      configured S16_LE stereo 48 kHz, wrote `4800` frames with
      `SNDRV_PCM_IOCTL_WRITEI_FRAMES`, drained, and produced
      `/tmp/xv6-alsa-hwprobe.wav` with nonzero samples. A libasound `hw:0,0`
      reducer opened the hardware PCM, completed `snd_pcm_set_params()`, wrote
      `4800` frames, drained, and produced `/tmp/xv6-alsa-lib-hwprobe.wav`
      with nonzero samples against the refreshed rootfs image.
- [ ] Remaining audio ABI gaps: the ALSA surface is intentionally minimal
      playback-only hardware enumeration, not a complete ALSA implementation.
      Timer, capture, mmap, async notification, mixer controls, and richer
      channel-map/status behavior remain future compatibility work if a Linux
      GUI/audio program proves it needs them.

Latest Chromium file-lock crash:

- [ ] Chromium could panic the kernel while opening with
      `spin_lock reentry on 'vfs_inode_flock'`. The reduced kernel bug was in
      the blocking `F_SETLKW`/`flock()` wait path: `tq_wait_in_state()` drops
      and then reacquires the passed spinlock before returning, but
      `vfs_file_lock_ctl()` retried by jumping to a loop top that locked the
      same inode lock again.
- [ ] `kernel/kernel/vfs/file_lock.c` now locks before the retry loop and
      treats a successful wake as returning with `inode->file_lock` already
      held. Nonblocking `F_SETLK`, `F_GETLK`, unlock, and interrupted wait
      exits keep their explicit unlock/return paths.
- [ ] Verification: `git diff --check`, `git -C kernel diff --check`, and
      `git -C ports diff --check` passed. `cmake --build build-x86_64
      --target kernel -j2` passed. Mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=59.0 speed=1.003 presentedFPS=0.0
      decodedFPS=59.0 dropPct=0.00 advanced=15.36`.
- [ ] Verification: focused Chromium X11 multiprocess `about:blank` run passed
      after the fix with guest framebuffer evidence:
      `CHROMIUM-SUPERVISOR-LOW-NOISE-DONE` in
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-flock-reentry-fix-guestfb-20260618.run.log`.
- [ ] Note: `kernel-sparse` still fails on pre-existing Hyper-V sparse parse
      errors and broad context warnings; no new `file_lock.c` sparse complaint
      appeared in that run.

## Recently Touched Kernel Area

Current active kernel patch area:

- [ ] `kernel/arch/x86_64/irq/syscall.c`
- [ ] `kernel/kernel/gdbstub/gdbstub.c`
- [ ] `kernel/kernel/proc/sys_misc.c`
- [ ] `kernel/kernel/proc/signal.c`
- [ ] `kernel/kernel/virtio_gpu.c`
- [ ] `kernel/kernel/virtio_gpu_scanout.c`
- [ ] `kernel/kernel/pci.c`
- [ ] `kernel/kernel/inc/dev/pci.h`
- [ ] `kernel/kernel/dev/fb/fb_device_ioctl.c`
- [ ] `kernel/kernel/dev/fb/fb_drm_kms_atomic_props.c`
- [ ] `kernel/kernel/dev/fb/fb_drm_kms_properties.c`
- [ ] `kernel/kernel/dev/fb/dma_fence.c`
- [ ] `kernel/kernel/dev/fb/fb_drm_dispatch.c`
- [ ] `kernel/kernel/dev/fb/fb_fd_sync.c`
- [ ] `kernel/kernel/inc/dev/dma_fence.h`
- [ ] `kernel/kernel/inc/vfs/unix_socket.h`
- [ ] `kernel/kernel/vfs/unix_socket.c`
- [ ] `kernel/kernel/lwip_port/sys_arch.c`
- [ ] `kernel/kernel/lwip_port/sys_socket.c`
- [ ] `kernel/kernel/dev/ossaudio.c`
- [ ] `kernel/kernel/inc/trap.h`
- [ ] `kernel/kernel/irq/irq.c`
- [ ] `kernel/kernel/virtio_snd.c`
- [ ] `kernel/kernel/vfs/file_lock.c`
- [ ] `scripts/launch/run-qemu.sh`
- [ ] `scripts/image/wayland-chromium-launcher.c`
- [ ] `rootfs-overlay/usr/share/alsa/alsa.conf`
- [ ] `rootfs-overlay/share/chromium-audio-smoke.html`

Latest cursor artifact mitigation:

- [ ] Intermittent hardware-cursor black-box reports are treated as a
      virtio-gpu cursor image lifetime issue or KMS cursor-plane reupload
      issue, not Chromium/userland.
- [ ] Cursor image uploads now rotate across 16 64x64 virtio-gpu
      cursor resources instead of rewriting the resource currently bound to the
      host cursor plane.
- [ ] KMS cursor plane moves with the same cursor FB avoid re-uploading the
      image and issue only a cursor move.
- [ ] Chromium can still expose cursor-shape changes that reuse the same KMS
      cursor `fb_id`. Atomic cursor commits now remember whether `FB_ID` was
      present in the commit: same-`fb_id` `FB_ID` commits force a fresh
      hardware cursor image upload, while move-only commits still issue only
      `MOVE_CURSOR`.
- [ ] Follow-up after a user report that the cursor can still occasionally
      become a black box: a checksum-based "same fb_id, changed pixels"
      reupload experiment regressed pointer/click proof and was backed out. Do
      not treat the checksum attempt as the fix; continue from atomic `FB_ID`
      reupload semantics, cursor FB reuse, plane update ordering, resource
      lifetime, and host cursor-image caching.
- [ ] A focused Chromium/X11 `fbstat` run showed the guest cursor BO is not an
      opaque black square: the latest cursor upload was 64x64 with
      `alpha_zero=3842`, `alpha_opaque=91`, and
      `kms_cursor_upload_failures=0`. The remaining black box is therefore in
      the virtio/QEMU cursor-plane composition path. A later software-cursor
      default hid the pointer inside Chromium, so `/bin/weston-session` now
      defaults back to `XV6_WESTON_SOFTWARE_CURSOR=0`; use
      `weston_software_cursor=1` only for explicit fallback experiments.
- [ ] Temporary scanout-read stage logs from the refresh/readback diagnostic were
      removed after they did not fire on the Chrome unresponsive path. Keep
      warning/error logs and opt-in traces; do not restore normal-path
      scanout-read noise without a focused reason.
- [ ] Verification: `git diff --check` passed for touched plan/GPU files.
- [ ] Verification: `cmake --build build-x86_64 --target kernel -j2` passed.
- [ ] Verification: mandatory video gate passed with the latest result
      above.
- [ ] Verification: software-cursor mitigation rebuilt with
      `port-weston-runtime-refresh`, `port-wayland`, and `rootfs-refresh`;
      mandatory video gate passed:
      `xv6-perf-video:RESULT pass fps=54.6 speed=1.003 presentedFPS=0.0
      decodedFPS=54.6 dropPct=0.12 advanced=15.44`.
- [ ] Follow-up after the user reported host-cursor lag over Chromium: default
      session was restored to `XV6_WESTON_SOFTWARE_CURSOR=0`; rebuild,
      rootfs/ISO refresh, and GUI gates passed for the hardware-cursor default:
      `xv6-perf-video:RESULT pass fps=58.8 speed=1.006 presentedFPS=0.0
      decodedFPS=58.8 dropPct=0.00 advanced=15.35` and
      `HOSTIDLE-X11-PASS launch_changed_pixels=565967
      input_changed_pixels=3755 exit_changed_pixels=565885`.
- [ ] Follow-up after the black-box report persisted: cursor image uploads now
      use a fenced `TRANSFER_TO_HOST_2D` before posting `UPDATE_CURSOR` to the
      separate virtio cursor queue. This matches the virtio-gpu ordering rule
      that cursor resources must be transferred and fenced before cursor queue
      updates can consume them.
- [ ] Cursor command buffer reuse no longer assumes in-order virtqueue
      completion. The cursor queue now reclaims command slots by used-ring
      descriptor id and only reuses slots that QEMU has actually returned,
      preventing high-rate Chromium cursor traffic from overwriting an
      in-flight cursor command.
- [ ] Exported package refreshed after the cursor-queue fix:
      `/home/es/xv6-wayland-chromium-qemu/` still contains only
      `launch-qemu.sh` and `xv6-wayland-chromium.iso`.
- [ ] Verification after the cursor-queue fix:
      `cmake --build build-x86_64 --target kernel -j2` passed;
      `git diff --check` and nested kernel/ports diff checks passed;
      mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=59.5 speed=1.002 presentedFPS=0.0
      decodedFPS=59.5 dropPct=0.00 advanced=15.29`; X11 proof passed with
      `HOSTIDLE-X11-PASS launch_changed_pixels=565967
      input_changed_pixels=299 exit_changed_pixels=565885`.
- [ ] Verification: `timeout 240 expect scripts/gpu/host-idle-x11-proof.expect`
      passed with pointer movement, key input, screenshots, and clean teardown:
      `HOSTIDLE-X11-PASS launch_changed_pixels=588096
      input_changed_pixels=613 exit_changed_pixels=588077`.
- [ ] Follow-up after the software cursor default hid the pointer in Chromium:
      `/bin/weston-session` was restored to
      `XV6_WESTON_SOFTWARE_CURSOR=0`. Keep `weston_software_cursor=1` as a
      diagnostic boot knob only; the black-box cursor issue must be fixed in
      the hardware cursor path rather than by defaulting Chromium users to the
      software fallback. The exported ISO was refreshed again, and the
      mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=57.4 speed=1.003 presentedFPS=0.0
      decodedFPS=57.4 dropPct=0.00 advanced=15.36`.
- [ ] Follow-up kernel cursor-init fix: the virtio cursor queue now starts
      hidden until a valid cursor resource has been uploaded, and a show/move
      request before the first image upload records the position without
      sending `MOVE_CURSOR` for an unbound resource. This prevents an early
      Weston/Chromium cursor move from asking QEMU to display an uninitialized
      hardware cursor. Verification: `cmake --build build-x86_64 --target
      kernel -j2` passed; the exported ISO was refreshed; mandatory video gate
      passed with `xv6-perf-video:RESULT pass fps=57.8 speed=1.003
      presentedFPS=0.0 decodedFPS=57.8 dropPct=0.00 advanced=15.28`.
      Focused X11 proof also passed:
      `HOSTIDLE-X11-PASS launch_changed_pixels=1.02016e+06
      input_changed_pixels=3955 exit_changed_pixels=588096`.
      Chromium/X11 low-noise proof completed on `about:blank` with a mapped
      Chrome screenshot:
      `CHROMIUM-SUPERVISOR-LOW-NOISE-DONE`, artifact prefix
      `cursor-init-chromium-x11`; Weston reported `cursor planes: yes`.
- [ ] Follow-up after the black box persisted in the interactive
      `./scripts/launch/launch-gui.sh` path: do not use software cursor as the
      fix. The `launch-gui.sh` software-cursor fallback experiment was removed
      at user request; continue fixing the virtio/KMS hardware cursor plane.
- [ ] Follow-up after the user requested no software cursor at all: the GTK
      launch path now keeps `weston_software_cursor` unset and enables
      `virtio_gpu_cursor_rgba_compat=1` instead. The kernel leaves the guest
      cursor BO format unchanged, but translates the uploaded virtio cursor
      resource to the unpremultiplied RGBA byte order QEMU GTK expects before
      posting `UPDATE_CURSOR`.
- [ ] Verification for the no-software-cursor GTK compat path:
      `AUTO_BUILD=0 QEMU_DRY_RUN=1 ./scripts/launch/launch-gui.sh` showed
      `show-cursor=off`, `virtio_gpu_cursor_rgba_compat=1`,
      `root=/dev/disk0`, and `video=1280x800` with no
      `weston_software_cursor`; `git diff --check` passed for the touched
      plan/launcher/kernel files; `cmake --build build-x86_64 --target kernel
      -j2` passed; mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=58.8 speed=1.004 presentedFPS=0.0
      decodedFPS=58.8 dropPct=0.00 advanced=15.42`.
- [ ] X11/Chromium verification for the same no-software-cursor GTK path:
      `timeout 240 expect scripts/gpu/host-idle-x11-proof.expect` passed with
      `HOSTIDLE-X11-PASS launch_changed_pixels=565885
      input_changed_pixels=299 exit_changed_pixels=565967`. Chromium low-noise
      with QEMU monitor `screendump` reached Chromium/Weston evidence but could
      not extract a PPM because QEMU returned `Error: no surface`. Re-running
      with `CHROMIUM_GUEST_FBSTAT=1` passed:
      `CHROMIUM-SUPERVISOR-LOW-NOISE-DONE`, artifact prefix
      `no-software-cursor-gtk-compat-guestfb`; the PNG shows Chromium mapped at
      `about:blank` and Weston reported `cursor planes: yes`.
- [ ] Follow-up after the black-box cursor still persisted: match Linux's
      virtualized cursor ABI more closely without enabling software cursors.
      The DRM core now accepts `DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT`; the KMS
      cursor plane exposes `HOTSPOT_X` and `HOTSPOT_Y` only to clients that opt
      into that cap; atomic cursor commits preserve and apply the hotspot when
      uploading the hardware cursor image. Weston now opts into the cap before
      plane discovery and sends the normal pointer sprite hotspot with cursor
      atomic commits. The rootfs image was refreshed after rebuilding the
      Weston DRM backend.
- [ ] Verification for the hotspot-aware hardware cursor path: no
      `weston_software_cursor` launch flag, `show-cursor=off`, and
      `virtio_gpu_cursor_rgba_compat=1`; kernel build passed; direct Weston
      Ninja rebuild compiled `kms.c` and `state-propose.c`; rootfs refresh
      wrote `build-x86_64/fs.img`; mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=56.5 speed=1.004 presentedFPS=0.0
      decodedFPS=56.5 dropPct=0.00 advanced=15.46`; X11 proof passed with
      `HOSTIDLE-X11-PASS launch_changed_pixels=565885
      input_changed_pixels=3755 exit_changed_pixels=565885`.
- [ ] Follow-up after the black-box cursor still persisted with Chromium:
      default GTK launches no longer send guest cursor images through QEMU
      GTK's cursor pixbuf path. `launch-gui.sh` now keeps the host pointer
      visible with `show-cursor=on`, keeps Weston software cursors disabled,
      and adds `virtio_gpu_host_cursor_only=1` for GTK/virgl by default. The
      kernel accepts cursor uploads and moves in this mode only as state
      updates and does not post `UPDATE_CURSOR` or `MOVE_CURSOR` commands to
      the virtio cursor queue. This is a host-frontend mitigation, not a
      Weston software-cursor fallback; the old guest hardware cursor upload
      path remains available with `virtio_gpu_host_cursor_only=0`.
- [ ] Verification for the GTK host-pointer cursor mitigation:
      `AUTO_BUILD=0 QEMU_DRY_RUN=1 ./scripts/launch/launch-gui.sh` showed
      `show-cursor=on`, `virtio_gpu_host_cursor_only=1`,
      `virtio_gpu_cursor_rgba_compat=1`, `root=/dev/disk0`, and `video=1280x800`
      with no `weston_software_cursor`; `git diff --check` passed for the
      touched launcher/kernel files; `cmake --build build-x86_64 --target
      kernel -j2` passed; mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=58.0 speed=1.003 presentedFPS=0.0
      decodedFPS=58.0 dropPct=0.00 advanced=15.31`; X11 proof passed with
      `HOSTIDLE-X11-PASS launch_changed_pixels=588179
      input_changed_pixels=3955 exit_changed_pixels=588096`.
- [ ] Follow-up after the black box disappeared but cursor shapes stopped
      changing: that is the expected tradeoff of
      `virtio_gpu_host_cursor_only=1`. The launcher now exposes
      `QEMU_GTK_CURSOR_MODE=host|guest`: `host` is the default black-box-free
      mode with a fixed host cursor shape, while `guest` restores guest
      hardware cursor image uploads and surface-specific cursor shapes for
      focused debugging of the QEMU/GTK cursor alpha/composition path.
- [ ] Verification for `QEMU_GTK_CURSOR_MODE`: default dry-run shows
      `show-cursor=on`, `virtio_gpu_host_cursor_only=1`, and
      `virtio_gpu_cursor_rgba_compat=1`; guest-mode dry-run shows
      `show-cursor=off`, `virtio_gpu_cursor_rgba_compat=1`, and no
      `virtio_gpu_host_cursor_only`; `git diff --check` passed for the
      launcher edit.
- [ ] Prior software-cursor diagnostic evidence:
      `cmake --build build-x86_64/ports --target
      port-wayland-session-install -j2` passed; direct rootfs refresh with
      `scripts/image/make-rootfs.sh build-x86_64/sysroot
      build-x86_64/fs.img 3456` passed; mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=59.1 speed=1.003 presentedFPS=0.0
      decodedFPS=59.1 dropPct=0.00 advanced=15.41`; Chromium/X11 boot proved
      Weston logged `xv6: using software cursor rendering` and
      `cursor planes: no`. The exported package was refreshed and
      `/home/es/xv6-wayland-chromium-qemu/` still contains only
      `launch-qemu.sh` and `xv6-wayland-chromium.iso`.

Latest AF_UNIX change:

- [ ] Plain stream `read()` and `readv()` now discard ready SCM entries whose
      carrying bytes were consumed, matching Linux behavior.
- [ ] `recvmsg()` and `recvmmsg()` use `unix_sock_read_preserve_scm()` so stream
      ancillary data can still be returned.
- [ ] Goal: preserve Chromium/Mojo/X11 fd passing while preventing stale stream
      SCM state from surviving plain reads.
- [ ] AF_UNIX stream poll now ignores `SO_RCVLOWAT` for byte readiness, matching
      a Linux host reducer where one queued byte reports readable even when the
      socket low-water mark is larger. The configured low-water mark remains
      visible through `getsockopt()`.

Latest Chromium kernel trace hygiene:

- [ ] The Chromium low-noise harness now leaves `chrome_lifecycle_trace` off by
      default; set `CHROMIUM_LIFECYCLE_TRACE=1` when a run needs process-level
      role evidence such as `exec`, `exec-argv`, non-thread `clone`, and
      `exit`.
- [ ] Chromium surface/compositor trace rows are also opt-in in the low-noise
      supervisor; set `CHROMIUM_SURFACE_TRACE=1` only when mapped-surface
      lifecycle evidence is needed.
- [ ] The low-noise supervisor now preserves an artifact-specific
      `<prefix>.run.log` alongside the rolling `run.log`, so later probes do
      not erase the evidence for a successful or failed prefix.
- [ ] Asset fd/VMA lifecycle dumps now require explicit `chrome_fd_trace=1`.
- [ ] Thread-clone lifecycle chatter now requires explicit
      `chrome_thread_lifecycle_trace=1`.
- [ ] Duplicate successful `execve`/`execveat` result rows now require explicit
      `chrome_exec_syscall_trace=1`; failed exec attempts remain logged when
      lifecycle tracing is active.
- [ ] Routine DRM node open rows (`DRM: open node=...`) are now hidden on the
      normal path and require `chrome_drm_ioctl_trace=1`; denied or unknown DRM
      ioctl rows remain unconditional.
- [ ] Additional success-only kernel bring-up breadcrumbs were removed from the
      normal boot path: `dma_fence` selftest success, virtio-gpu 3D smoke
      success, and virtio-gpu cursor queue init success. Keep warnings/errors
      and opt-in traces; `xv6-mesa:` rows are userspace library logs, not
      kernel logs.
- [ ] lwIP mailbox-full diagnostics are hidden on the normal path and require
      `lwip_mbox_full_trace=1`. When enabled, they print the mailbox's actual
      configured `max` as well as the fixed ring capacity, so small UDP
      recv-mailbox saturation is not confused with the global TCPIP mailbox.
- [ ] Chromium INET socket trace rows now require explicit
      `chrome_socket_trace=1` and are no longer tied to broad
      `chrome_fd_trace=1`. AF_UNIX/SCM `sendmsg()`/`recvmsg()` rows require
      `chrome_unix_ipc_trace=1`, so TCP connect/SO_ERROR/TLS-byte evidence can
      be collected without drowning the interactive harness in Unix IPC logs.
- [ ] Opt-in Chromium syscall tracing now decodes `clone3` argument details,
      including unsupported and unknown flag masks, so process-supervision
      deltas can be diagnosed without broad syscall-log guesswork.
- [ ] Chrome poll-summary tracing suppresses zero-ready, infinite-timeout,
      single-fd, non-AF_UNIX wait-loop rows by default. The noisy
      `chromium-crashpad-postfetch-ipc-20260615a` probe otherwise spent its log
      budget on `inotify_reader` idle polling and timed out before the initial
      framebuffer capture, so do not treat that run as browser/render evidence.
- [ ] Follow-up `chromium-crashpad-postfetch-ipc-quiet-20260615a` confirmed the
      idle `inotify_reader` wait-loop spam is gone, but still timed out at
      `capture-blank-timeout` with an empty host HTTP log. That run remains an
      early Crashpad/tracing timing probe, not post-fetch render evidence.

Latest live Chromium/surface follow-up:

- [ ] User-observed QEMU checker/gradient screenshot is consistent with a
      missing or disappeared guest scanout/client surface, not with rendered
      Chromium page content. Same-day evidence:
      `chromium-normalroot-humanbutton-monitor-refresh-20260615c` fetched
      `GET /human-button.html` and `/favicon.ico`, but QEMU monitor
      `screendump` returned `Error: no surface` and the guest framebuffer
      capture timed out. Desktop health remains separately covered by the
      X11 idle proof.
- [ ] Direct-URL multiprocess wrapper probe
      `chromium-wrapper-multiprocess-humanbutton-lifecycle-20260615a` launched
      `WAYLAND_CHROMIUM_MULTIPROCESS=1 /bin/wayland-chromium
      http://10.0.2.2:28213/human-button.html` with lifecycle/poll tracing,
      reached browser exec plus Crashpad and zygote execs, but produced no
      NetworkService, renderer, fixture `GET`, or visual proof before the
      harness ended. Host HTTP log was empty.
- [ ] Follow-up
      `chromium-wrapper-multiprocess-humanbutton-guestlog-20260615b` used the
      same short direct-URL launch and attempted a later guest-side log/`ps`
      liveness command. The guest shell did not answer after zygote startup
      and the host HTTP log remained empty. Treat this as an accelerated
      multiprocess startup/liveness failure before page fetch, distinct from
      the earlier single-process post-fetch/no-surface symptom.
- [ ] Latest user-observed gradient repeat did not correspond to a live QEMU
      process from the current Codex workspace when checked with `pgrep`; avoid
      stacking a second QEMU before collecting same-run evidence. The prior
      desktop-only control still passed, so treat the gradient as an
      intermittent startup/surface symptom until a same-run log proves Weston,
      scanout, or Chromium ownership.
- [ ] Same-run controlled repeat
      `chromium-gradient-repeat-20260615a` loaded
      `GET /input-smoke.html`, accepted page key input, and the guest
      framebuffer screenshot
      `build-x86_64/chromium-normal-desktop-proof/chromium-gradient-repeat-20260615a-after-page-key.png`
      shows Chrome rendered `typed:a`. In the same run, QEMU monitor
      `screendump` returned `Error: no surface`, so the visible gradient can
      be a host/QEMU presentation-surface loss even while the guest framebuffer
      and Chromium content are healthy. Continue from GTK/GL display backend,
      virtio-gpu scanout binding/flush, and QEMU monitor/display-surface
      evidence before reopening Chromium network/render ABI paths.
- [ ] Follow-up virgl desktop validation artifacts from the same investigation
      show the kernel/QEMU protocol side is still active despite monitor
      `Error: no surface`: `display_presents=463`, `display_completions=463`,
      `virtio_failures=0`, `virgl_bo_presents=463`, guest screenshot matrix
      `status=PASS`, and QEMU trace rows alternating 1280x800 `SET_SCANOUT`
      and `RESOURCE_FLUSH`. The validator exited nonzero only because its
      `renderer=virgl` log matcher is stale for the current
      `backend virgl ... renderer OpenGL via virtio-gpu virgl` output. Clean
      that harness separately; treat the user-visible gradient as a host
      GTK/GL presentation problem or direct-primary display-backend issue unless
      a same-run guest framebuffer capture also goes blank.
- [ ] Display-backend A/B for the repeated user-visible gradient:
      `chromium-gradient-sdl-repeat-20260615a` forced SDL GL and reproduced the
      launcher's known WSLg/D3D12 SDL weakness: the visible QEMU window stayed
      on the checker/gradient, Weston timed out waiting for
      `/tmp/wayland-0.lock`, no fixture request was made, and no guest
      framebuffer proof was produced. Do not use SDL GL as the workaround on
      this host.
- [ ] GTK GL mode A/B:
      `chromium-gradient-gtkglon-repeat-20260615a` forced `QEMU_GTK_GL=on`
      instead of the WSL default `gtk,gl=es`. It completed
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; host HTTP saw
      `GET /input-smoke.html` and `/favicon.ico`; the final guest framebuffer
      screenshot
      `build-x86_64/chromium-normal-desktop-proof/chromium-gradient-gtkglon-repeat-20260615a-after-page-key.png`
      shows Chromium rendered `typed:a` with the page input updated. QEMU
      monitor `screendump` still returned `Error: no surface`, and QEMU logged
      `GtkGLArea console lacks DMABUF support`. Treat the gradient as host QEMU
      GTK/GL presentation/surface failure while the guest scanout is healthy.
      Next display A/B should compare the direct-primary
      `virtio-vga-gl-primary` path with `virtio-gpu-gl-primary`/Bochs-visible
      fallback before changing kernel GPU paths.
- [ ] Later 2026-06-16 gradient after Settings/desktop-entry work was a
      separate Weston runtime staging failure, not Chromium render evidence:
      `/bin/weston-session` started, then Weston failed to load
      `libexec_weston.so.0` because the private library was installed under
      `/lib/weston` while `/bin/weston` carried a host-build absolute rpath.
      Fixed by giving Weston a guest-relative runpath
      `$ORIGIN/../lib/weston:$ORIGIN/../lib` and declaring
      `lib/weston/libexec_weston.so*` as Weston port outputs. Verification:
      `host-idle-x11-proof.expect` passed with
      `HOSTIDLE-X11-PASS launch_changed_pixels=565947
      input_changed_pixels=284 exit_changed_pixels=565864`.
- [ ] Bochs-visible/virtio-render fallback A/B:
      `chromium-gradient-bochs-visible-20260615a` proved QEMU monitor
      `screendump` works again with `virtio-gpu-gl-primary`, but xv6 still
      overwrote `/dev/fb0` with the secondary virtio-gpu scanout, leaving the
      host-visible Bochs surface stuck on the boot splash while the guest
      virtio framebuffer showed Chromium. Kernel follow-up now records the
      virtio-gpu PCI class code and keeps non-VGA display controllers
      (`class=0x038000`) render-only by default; `virtio_gpu_force_scanout=1`
      can override and `virtio_gpu_render_only=1` can force render-only.
- [ ] The same fallback also needed `FB_GPU_SCANOUT_READ` to support
      non-virtio `/dev/fb0` readback by copying from the current framebuffer.
      Post-fix proof
      `chromium-gradient-bochs-visible-renderonly-20260615b` completed
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; host HTTP saw
      `GET /input-smoke.html`; guest capture and QEMU monitor capture both
      show Chromium rendered `typed:a` with page input `a`:
      `build-x86_64/chromium-normal-desktop-proof/chromium-gradient-bochs-visible-renderonly-20260615b-after-page-key.png`
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-gradient-bochs-visible-renderonly-20260615b-after-page-key-monitor.png`.
- [ ] `scripts/launch/run-qemu.sh` auto GPU selection now prefers the
      Bochs-visible `virtio-gpu-gl-primary` fallback on WSLg/D3D12. Explicit
      callers that request `virtio-vga-gl-primary` still exercise the direct
      primary path; the mandatory video gate did so and passed after the
      kernel changes.
- [ ] New opt-in kernel diagnostic: `chrome_exec_phase_trace=1` prints compact
      `chrome-exec-phase` rows for Chromium-like `exec()` calls only. It is
      disabled by default and is intended to replace broader syscall/thread
      trace when investigating early Chrome startup.
- [ ] Launch-only diagnostic
      `chromium-exec-phase-launch-20260615a` showed one real Chrome `execve`
      sample taking about 11.2s; the only large grouped phase was between
      `kqueue-notify` and `vm-switch`. A split repeat,
      `chromium-exec-phase-vmput-20260615a`, did not reproduce that wall:
      real Chrome exec completed in about 0.48s and `old-vm-put` was about
      20ms. Continue treating the startup stall as intermittent lock/scheduler
      or VM-teardown contention, not as deterministic ELF/path loading cost.
- [ ] Next step: compare against the known
      `WAYLAND_CHROMIUM_EXTRA_FLAGS=--disable-gpu` multiprocess control and
      continue from GPU process/DRM readiness, zygote child launch, futex/epoll
      wakeups, and browser/renderer IPC. Do not reopen DNS, static PIDs,
      desktop startup, or host fixture setup for this direct-URL no-fetch case.

Latest Chromium input-latency diagnosis:

- [ ] User-observed Chromium input latency (clicks, omnibox typing, new tabs
      taking seconds or more) pointed at delayed input readiness rather than a
      dead desktop: `/dev/kbd` and `/dev/mouse` events woke sleep-channel
      readers but did not notify epoll/kqueue watchers registered on a
      different open file description for the same character device.
- [ ] Kernel fix direction now adds character-device knote lists, attaches
      cdev-backed EVFILT_READ/WRITE filters to shared device readiness, and
      notifies `/dev/kbd` and `/dev/mouse` epoll consumers when input enters
      the device ring. This keeps file-local notifications for sockets, pipes,
      eventfd, timerfd, and similar per-file sources.
- [ ] Validation:
      `cmake --build build-x86_64 --target kernel -j2` passed;
      `host-x11-abi-smoke-proof` passed with Xwayland focus, key presses,
      redraw/input_ready, and clean WM_DELETE teardown;
      `chromium-noarg-input-step-20260615a` passed through Chromium launch,
      injected address-bar input, later `xv6` typing visible in the captured
      omnibox frame, and framebuffer capture.
- [ ] Remaining caveat: the Chromium input-step host HTTP log was empty and
      the final screenshot showed `xv6` in the omnibox, so this is input
      responsiveness evidence, not HTTP fetch/render proof. Continue page-load
      and refresh behavior from browser/render IPC if the URL still requires a
      manual refresh.

Latest plain Wayland Chromium unresponsive diagnosis:

- [ ] A user-observed plain `wayland-chromium` freeze with a healthy desktop
      reduced to procfs maps generation holding a target VM read lock while
      Chromium VM writers queued behind it.
- [ ] Pre-fix evidence from `chromium-longidle-vmlock-ra-20260615a` and
      `chromium-longidle-procfs-progress-20260615a`: the Chromium
      `MemoryInfra` thread held the Chrome VM read lock from
      `procfs_gen_maps`; `^V` diagnostics showed `READERS=1`, `HOLDER=-1`,
      writer queue growth up to `WQ=2`, and scan progress stuck at
      `buf=65536 pos=65496 iters=1149` while formatting a maps entry.
- [ ] Root cause: `/proc/<pid>/maps` and `/proc/<pid>/smaps` generated the
      whole file under `vm_rlock()`, formatted a VMA line when only a tiny
      tail of the current buffer remained, and used the minimal kernel
      `snprintf("%s")` path on `opened_path`. The kernel `vsnprintf` computes
      full string length before precision/truncation, so a nearly-full maps
      buffer could stall inside formatting while holding the VM read lock.
- [ ] Fix direction now in `kernel/kernel/vfs/procfs/inode.c`: bound VM scans
      to user VMAs below `UVMTOP`, reserve enough room for a full maps/smaps
      entry before formatting, and avoid `%s` for the opened path by appending
      a bounded `VFS_USER_PATH_MAX` path manually.
- [ ] Post-fix evidence from
      `chromium-longidle-procfs-formatfix-20260615a`: after the page was
      entered, +35s, +78s, and +121s shell liveness probes all answered; the
      +78s and +121s VM-lock dumps had only the header and no stuck VM reader;
      the final process dump still showed Chromium/MemoryInfra/NetworkService
      alive and sleeping or runnable instead of wedged in `procfs_gen_maps`.
      The host HTTP fixture log for this particular harness was empty, so this
      is liveness/lock evidence rather than page-fetch proof.
- [ ] Temporary VM-lock/procfs-scan telemetry used for the diagnosis was
      removed after isolating the cause; keep the production fix narrow unless
      another freeze requires reintroducing opt-in diagnostics.

Latest clone3/signal ABI change:

- [ ] `clone3(CLONE_CLEAR_SIGHAND | CLONE_VFORK | SIGCHLD)` is now accepted
      instead of rejected with `-EINVAL`, matching the Linux ABI requirement
      that `CLONE_CLEAR_SIGHAND` reset caught signal dispositions in the child.
- [ ] `CLONE_CLEAR_SIGHAND | CLONE_SIGHAND` remains rejected as invalid, and
      namespace/cgroup/set-tid clone3 extensions remain known unsupported
      surfaces unless a reducer requires them.
- [ ] Pre-fix diagnostic evidence:
      `chromium-crashpad-child-clone3-20260615a` showed Chrome helper and
      Crashpad self-monitor `clone3` calls with flags `0x100004100` rejected as
      `unsupported=0x100000000`.
- [ ] Post-fix diagnostic evidence:
      `chromium-crashpad-clear-sighand-20260615a` showed the same flag pattern
      decoded as `unsupported=0x0 unknown=0x0` and returning child PIDs.
- [ ] Low-noise Crashpad-enabled follow-up
      `chromium-crashpad-clear-sighand-lowtrace-20260615a` accepted input and
      fetched `GET /key.html`, but did not produce render-title/page-input
      proof before framebuffer capture failed with QEMU monitor
      `Error: no surface`. Treat this as post-fetch browser/render IPC or
      event-loop evidence, not DNS, desktop startup, or packaging evidence.
- [ ] Linux host reducer accepted
      `clone3(CLONE_PIDFD | CLONE_THREAD | CLONE_VM | CLONE_FS |
      CLONE_FILES | CLONE_SIGHAND | CLONE_SYSVSEM | CLONE_SETTLS |
      CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID)`.
      xv6 now permits `CLONE_PIDFD | CLONE_THREAD` instead of rejecting it
      with `-EINVAL`; `CLONE_DETACHED` and missing `pidfd` pointer remain
      rejected.
- [ ] Thread pidfds are notified on non-leader `CLONE_THREAD` exit after the
      target is removed from the process table, so level-triggered
      poll/epoll rechecks see the specific thread pidfd as readable/exited.
- [ ] Linux wait-family clone-child filtering is now implemented for
      `wait()`, `waitpid()`, and `waitid()`: plain waits match `SIGCHLD`
      children, `__WCLONE` matches no-signal/non-`SIGCHLD` clone children, and
      `__WALL` or ptrace waits match both classes.
- [ ] `exit_group(status)` no longer fabricates `SIGKILL` as the public death
      reason for sibling threads during a normal group exit. A Chromium page
      child that previously reaped as `wait_status=9 killed_signo=9` now reaps
      as `wait_status=0 killed_signo=0` in
      `chromium-crashpad-exitgroup-status-20260615a`.

Latest Chromium Crashpad-enabled no-refresh state:

- [ ] ELF LOAD boundary pages now preserve file identity in VMAs while keeping
      the eager zero-filled page contents, so `/proc/<pid>/maps` reports the
      final partially file-backed loader/module page as file-backed like Linux.
      This removed Crashpad's `no module mappings 0x70036000` complaint.
- [ ] Verification: `git -C kernel diff --check -- kernel/exec.c
      kernel/proc/exit.c kernel/proc/thread_group.c` passed.
- [ ] Verification: `cmake --build build-x86_64 --target kernel -j2` passed
      after the wait-family and `exit_group()` fixes.
- [ ] Focused Crashpad-enabled run
      `chromium-crashpad-exitgroup-status-20260615a` still failed at
      `after-enter-liveness-timeout`.
- [ ] Positive evidence from that run: host HTTP saw `GET /key.html` and
      `GET /favicon.ico`; Crashpad maps/procfs parser errors were absent; the
      first page child reaped with normal status 0 instead of fake SIGKILL.
- [ ] Remaining failure: after the fetched navigation, Chromium still
      terminates the first page child and the guest shell stops responding
      before the after-enter framebuffer capture. Continue from post-fetch
      renderer/Mojo/zygote IPC, fd passing, futex/epoll readiness, and
      signal/process-supervision state. Do not reopen DNS, launcher, or
      packaging theories.

Latest TCP socket readiness change:

- [ ] Stream socket poll now reports readable/HUP/RDHUP readiness after
      orderly peer EOF (`rx_eof`).
- [ ] lwIP zero-length stream receive callbacks now set `rx_eof` before
      waking waiters, so level-triggered poll/epoll rechecks can observe FIN.

Latest Chromium procfs status ABI change:

- [ ] Crashpad's Linux process-info parser requires a `Groups:\t` line in
      `/proc/<pid>/status`; for empty supplementary groups Linux still leaves a
      trailing space after the tab.
- [ ] `procfs_gen_status()` now emits `Groups:\t ` for empty groups and
      `Groups:\t<gid> ... ` for non-empty groups instead of bare `Groups:`.
- [ ] Pre-fix evidence in
      `chromium-unresponsive-slowtrace-20260615a.run.log` showed
      `third_party/crashpad/crashpad/util/posix/process_info_linux.cc:159
      format error: missing fields` immediately before Crashpad `tgkill` noise
      and `GPU process exited unexpectedly: exit_code=9`.
- [ ] Post-fix evidence:
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-procfs-groups-fix-low-no-fbstat-20260615a.run.log`
      reached `human-button:PASS`, mapped Chromium content, and had no
      `format error: missing fields`, `GPU process exited`, child
      `Terminating current process after 15 seconds with no connection`,
      `Network service crashed`, Crashpad `tgkill: No such process`, or GPU
      reinitialization rows.
- [ ] Caveat: the low-noise harness still ended with `FAIL extract-ppm`, so this
      is strong guest-side render/process evidence but not screenshot proof.

Verification already run:

- [ ] `git diff --check` passed for the touched plan/kernel files.
- [ ] `cmake --build build-x86_64 --target kernel -j2` passed.
- [ ] `cmake --build build-x86_64 --target image -j2` passed before the last
      boot, but unnecessarily rebuilt/staged user and port artifacts.
- [ ] `cmake --build build-x86_64 --target rootfs-refresh -j2` passed and
      refreshed `build-x86_64/fs.img` from the existing staged sysroot without
      walking user or ports.
- [ ] Mandatory video gate passed with the latest result above.

## Desktop Regression Note

A too-broad experimental AF_UNIX stream `SCM_RIGHTS` discard broke Wayland/X11
fd passing and produced a blank desktop/background-only QEMU frame. The current
fix is the split described above: plain read discards consumed stream SCM, while
`recvmsg()` preserves and returns SCM. Future AF_UNIX edits must be checked
against X11/Wayland fd-passing smoke evidence before being trusted.

Known recovery evidence:

- [ ] `build-x86_64/host-x11-abi-smoke-proof/run.log`
- [ ] `build-x86_64/host-x11-abi-smoke-proof/host-x11-abi-smoke-before.png`
- [ ] `build-x86_64/host-x11-abi-smoke-proof/host-x11-abi-smoke-input.png`
- [ ] `build-x86_64/host-x11-abi-smoke-proof/host-x11-abi-smoke-exit.png`
- [ ] Latest focused X11 smoke after the narrow refresh reached
      `HOSTX11ABI-SMOKE-PASS` with launch/input/exit pixel deltas; durable
      verifier rows are in
      `build-x86_64/host-gui-proof-verify/host-x11-abi-smoke-proof-summary.tsv`.
- [ ] Latest focused X11 smoke after the TCP EOF readiness change also reached
      `HOSTX11ABI-SMOKE-PASS`:
      `launch_changed_pixels=254389 input_changed_pixels=191080
      exit_changed_pixels=254389`.

## Chromium / Host HTTP State

Chromium now has clean initial-navigation render proof for the local fixture on
both the X11 probe path and the launcher's default Wayland backend. Keep the
older "refresh once" failures as regression context, but the latest current
kernel/image no longer reproduces that failure in the focused fixture runs
below.

Latest Chrome unresponsive follow-up:

- [ ] Execbuffer out-fence fds are back to Linux-shaped sync_file fds with
      live virtio fence status. This removes the `sync_(file|fence)_info
      returned null` evidence from the focused run and avoids the previous
      virgl-fence poll path that could take the GPU op mutex while poll/kqueue
      held a spinlock.
- [ ] Verification: `git -C kernel diff --check`, top-level `git diff --check`,
      `cmake --build build-x86_64 --target kernel -j2`, and the mandatory
      video gate passed. Latest gate result:
      `xv6-perf-video:RESULT pass fps=51.3 speed=1.002 presentedFPS=0.0
      decodedFPS=51.3 dropPct=0.13 advanced=15.30`.
- [ ] Focused negative run
      `chromium-pruned-multiprocess-syncsubmit-refresh-20260615a` reached
      same-boot `GET /key.html`, page console evidence, and GPU-role
      `DRM_IOCTL_VIRTGPU_EXECBUFFER` samples returning `-EAGAIN` from a
      blocking input-fence path before Chromium's GPU watchdog killed and
      restarted the GPU process.
- [ ] Blocking virtio-fence waits now loop with sleep, signal, and timeout
      bounds for execbuffer input fences and `DRM_IOCTL_VIRTGPU_WAIT`, instead
      of leaking `-EAGAIN` to userspace. Chromium execbuffers are currently
      forced through synchronous submit to avoid presenting one long async
      virgl drain to the Chrome GPU watchdog while the kernel serializes
      virtio-gpu operations.
- [ ] Focused proof run
      `chromium-pruned-multiprocess-fencewait-refresh-20260615a` completed
      with `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`. The host fixture saw
      `GET /key.html` HTTP 200, `/favicon.ico` 404, and refresh
      `GET /key.html` HTTP 304. Preserved screenshots include
      `build-x86_64/chromium-normal-desktop-proof/chromium-pruned-multiprocess-fencewait-refresh-20260615a-after-page-key.png`,
      which shows the rendered Chrome window with tab title `keyed:PASS`.
- [ ] Remaining negative evidence: the same proof run still logged GPU process
      exits with `exit_code=9` and reinitialization. The minimal page can now
      render and process keyboard input, but GPU restart stability remains open.
      Continue from virgl/execbuffer/fence recovery and Chrome GPU watchdog
      evidence, not DNS, desktop launchers, host fixture setup, or the already
      closed sync_file ioctl surface.
- [ ] New focused follow-up for the user-visible "Chrome window unresponsive,
      desktop healthy" report found same-run thread evidence around the manual
      shell launch path. An initial no-extra-trace repeat with prefix
      `chromium-unresponsive-fence-status-20260615a` failed at
      `capture-blank-timeout` before URL entry because the guest shell stopped
      answering after Chrome opened; no host HTTP request was made.
- [ ] Kernel-side fence fix in this follow-up: when
      `fb_syncobj_file_status()` sees a timeline syncobj state become ready, it
      now promotes that readiness to the exported sync-file fence by signaling
      the `dma_fence`, setting `snapshot_signaled`, and preserving the
      reservation-fence snapshot when available. `DRM_IOCTL_VIRTGPU_EXECBUFFER`
      now re-checks sync-file status before parking on the generic sync-file
      fence wait, and treats only positive status as readiness.
- [ ] Post-fix diagnostic repeat with prefix
      `chromium-unresponsive-thread-dump-20260615a` enabled opt-in
      `chrome_thread_dump` and `chrome_drm_thread_dump`. It completed with
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; the host fixture saw
      `GET /key.html` HTTP 200 and `/favicon.ico` 404, and screenshot
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-thread-dump-20260615a-after-page-key.png`
      shows the page content `keyed:PASS`.
- [ ] Same-run thread evidence from that diagnostic repeat shows dynamic role
      derivation by executable and thread names: real Chrome exec path
      `/opt/host-gui/wayland-chromium/chrome-linux64/chrome`, in-process GPU
      role `Chrome_InProcGp`, renderer role `Chrome_InProcRe`, IO roles
      `Chrome_IOThread` and `Chrome_ChildIOT`, and `NetworkService`. The run had
      transient uninterruptible `D` samples in Chrome IPC/GPU threads, but no
      `GPU process exited unexpectedly`, no `wait_status=9` GPU watchdog kill,
      and no stale DNS/fixture evidence.
- [ ] Verification after this fence-status follow-up:
      `git -C kernel diff --check -- kernel/dev/fb/fb_fd_sync.c
      kernel/dev/fb/fb_drm_dispatch.c` passed;
      `cmake --build build-x86_64 --target kernel -j2` passed; mandatory video
      gate passed with the latest `fps=55.8` result above.
- [ ] Remaining caveat: the manual sticky Chrome-window symptom is
      timing-sensitive. If it recurs, continue from the transient `D` samples in
      browser/render/GPU IPC and wait paths, especially the post-render
      `Chrome_InProcGp`, `Chrome_ChildIOT`, and main browser-thread samples.
      Do not reopen DNS, static PIDs, desktop launchers, or host fixture setup
      without new same-run evidence.
- [ ] Latest strict manual input control:
      `chromium-manual-input-logquiet-fixed-20260615a` launched Chromium from
      the shell, typed `http://10.0.2.2:28213/input-smoke.html`, fetched it
      from the host fixture, stayed shell-responsive after navigation, injected
      page key `a`, and captured a screenshot with tab/title `typed:a` and the
      `a` visible in the page input. The run did not reproduce the user's
      unresponsive Chrome window; keep the manual symptom open as
      timing/workflow-specific rather than solved.
- [ ] The preceding same probe briefly exposed lwIP `sys_mbox_trypost()`
      saturation from a mailbox with `max=64`; the diagnostic is now opt-in via
      `lwip_mbox_full_trace=1` and no longer serial-spams default Chrome runs.
- [ ] Corrected no-extra-disables runtime wrapper proof:
      `build-x86_64/chromium-crashpad-normalpath-proof/crashpad-enabled-fixed-src.fs.img`
      replaces `/bin/wayland-chromium` with the Crashpad-enabled wrapper and no
      longer carries `--disable-dev-shm-usage`, `--disable-vulkan`, the extra
      sandbox-disable flags, or crashpad-disabling flags. With
      `WAYLAND_CHROMIUM_MULTIPROCESS=1`, the normal-path probe
      `chromium-crashpad-enabled-fixed-humanbutton-20260615a` fetched
      `GET /human-button.html` and reached
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`, but screenshots still showed a
      stale `about:blank` window/omnibox after Enter instead of committed page
      content.
- [ ] Same corrected wrapper with explicit refresh requested by the harness,
      `chromium-crashpad-enabled-fixed-refresh-20260615a`, fetched
      `GET /human-button.html` and `/favicon.ico` but failed before refresh at
      `after-enter-liveness-timeout`. This matches the user-visible report that
      the Chrome window becomes unresponsive while the desktop remains healthy.
- [ ] Targeted repeat
      `chromium-crashpad-enabled-fixed-thread-dump-20260615a` reproduced the
      same post-fetch liveness timeout with `chrome_thread_dump`,
      `chrome_drm_thread_dump`, and slow-syscall tracing enabled. Same-boot
      role evidence showed Chrome browser/UI, `Chrome_IOThread`,
      `Chrome_ChildIOT`, `NetworkService`, compositor, and GPU/watchdog roles;
      long waits were dominated by futex/epoll, the log included
      `Network service crashed or was terminated, restarting service`, and a
      later SIGTERM killed a Chrome child group. Continue from post-fetch
      process/IPC readiness, futex/epoll wakeups, and service lifecycle, not
      from desktop health, DNS, static PIDs, or launcher packaging.
- [ ] Accelerated normal-path A/B isolated another user-visible "Chrome
      unresponsive, desktop healthy" mode to kernel GPU reservation sync-file
      readiness, not DNS, desktop health, input injection, or host fixture
      setup. The disable-gpu control
      `chromium-normalpath-disablegpu-probe-20260615a` fetched and rendered
      `input-smoke.html` with title/input `typed:a`, while the accelerated
      control stayed stale at `about:blank`.
- [ ] Focused accelerated trace
      `chromium-normalpath-accel-fencetrace-20260615a` fetched
      `GET /input-smoke.html` and `/favicon.ico`, but stayed stale and timed
      out. Capped opt-in `chrome_drm_fence_trace=1` showed repeated GPU-role
      execbuffer input-fence waits on a dma-buf reservation sync-file:
      `kind=sync-file-resv ret=-11 target=3 signaled=0 reservation=3
      point=3 snapshot=0`. Treat numeric PIDs from that log as same-run
      coordinates only.
- [ ] Kernel fix: local TTM/dma-buf reservation bookkeeping fences now update
      `last_fence` and `signaled_fence` immediately when reserve/unreserve,
      release, PRIME/dma-buf attach, or local shared-reservation attach creates
      a synthetic fence marker. Imported sync-files still track issued fences
      separately and only advance `signaled_fence` when their source is ready.
      Negative sync-file status is no longer treated as readiness.
- [ ] Post-fix accelerated proof
      `chromium-normalpath-accel-sharedresvfix-20260615a` completed with
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; the host fixture saw
      `GET /input-smoke.html` HTTP 200, the passing run had no
      `chrome-drm-fence-wait` rows, and
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-accel-sharedresvfix-20260615a-after-page-key.png`
      shows Chrome rendered with tab title `typed:a` and the typed `a` visible
      in the page input.
- [ ] Verification after the reservation-fence fix:
      top-level `git diff --check`, `git -C kernel diff --check`, and
      `cmake --build build-x86_64 --target kernel -j2` passed for the touched
      GPU/DRM files. Mandatory video gate passed with the latest result above.
- [ ] Latest gdbstub/SIGTRAP follow-up for a mapped but apparently
      unresponsive Chrome window: unattached or wrong-process user
      `INT3`/`EBREAK` traps now return to the normal signal path by default
      instead of parking arbitrary applications in the in-kernel GDB stub.
      The legacy auto-wait behavior is opt-in with `gdbstub_wait_trap=1`.
      Pre-fix no-extra-flag diagnostic
      `chromium-nonroot-noflags-key-20260615a` parked Chrome with
      `gdbstub: pid ... (chrome) waiting for debugger on port 2159`.
- [ ] Post-fix manual-input diagnosis: an initial normal-launcher run
      `chromium-click-smoke-gdbtrap-20260615a` looked unresponsive because the
      harness typed before the Chrome window had mapped; the `blank` screenshot
      was still desktop-only, and the later window showed stale `about:blank`.
      Treat this as launch/surface timing, not a network or keyboard ABI
      failure.
- [ ] Longer-wait normal-launcher proof
      `chromium-click-smoke-gdbtrap-longwait-refresh-20260615a` accepted the
      URL bar focus click and keyboard navigation. The host HTTP log recorded
      `GET /click-smoke.html`, but the current fixture directory no longer has
      that file, so the rendered result was a valid HTTP 404 page.
- [ ] Current-fixture proof
      `chromium-human-button-gdbtrap-longwait-refresh-20260615a` fetched
      `GET /human-button.html` with HTTP 200, refreshed with HTTP 304, and
      captured
      `build-x86_64/chromium-normal-desktop-proof/chromium-human-button-gdbtrap-longwait-refresh-20260615a-after-enter.png`
      showing tab/title `human-button:PASS` and the rendered button. The
      after-refresh framebuffer capture timed out, so count the pre-refresh
      screenshot plus HTTP refresh log as the proof and keep refresh-capture
      robustness open.

Latest clean default-backend run:

- [ ] Harness: `scripts/gpu/wayland-chromium-supervisor-low-noise.expect`
- [ ] Quiet-default check after log cleanup: prefix
      `chromium-quiet-default-20260615`; `CHROMIUM_LIFECYCLE_TRACE=0`,
      `CHROMIUM_QEMU_NET=1`, URL
      `http://10.0.2.2:28213/human-button.html`.
- [ ] Result: the harness reached `Chromium evidence end reason=timer-3`, the
      host fixture saw `GET /human-button.html` and `/favicon.ico`, and Weston
      reached mapped content with title
      `human-button:PASS - Google Chrome for Testing`.
- [ ] Screenshot extraction failed for that quiet-default check with
      `CHROMIUM-SUPERVISOR-LOW-NOISE-FAIL extract-ppm`; preserve it as
      render-path log evidence only, not visual proof.
- [ ] Fresh surface-trace follow-up after the user reported an unresponsive
      Chrome window while the desktop stayed healthy:
      `chromium-supervisor-humanbutton-surfacequiet-20260615a` fetched
      `GET /human-button.html` but the final guest framebuffer fallback showed
      only the desktop wallpaper. QEMU monitor `screendump` reported
      `Error: no surface`, but the same monitor error also appears in
      successful framebuffer-fallback runs, so it is a capture-path artifact
      here rather than proof that Chromium failed to render.
- [ ] The traced repeat
      `chromium-supervisor-humanbutton-surfacetrace-20260615a` fetched
      `GET /human-button.html`, changed the Weston title to
      `human-button:PASS - Google Chrome for Testing`, acknowledged configure,
      committed content, mapped a `1056x682` Chromium surface, and preserved
      framebuffer proof at
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-supervisor-humanbutton-surfacetrace-20260615a.png`.
      The rolling log was preserved at
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-supervisor-humanbutton-surfacetrace-20260615a.run.log`.
- [ ] Interpretation: the current minimal human-button path is intermittent,
      not categorically broken. Initial fetch, title update, configure,
      mapping, and content commits can all succeed on the current image. If the
      user's Chrome window remains unresponsive, continue from the exact page
      or workflow and sample post-fetch surface/configure/event-loop state,
      not DNS, static PIDs, desktop health, or fixture setup.
- [ ] Kernel trace hygiene validation: the quiet-default log contains no
      `chrome-lifecycle`, `chrome-syscall`, `chrome-unix-ipc`,
      `chrome-epoll`, or `chrome-thread-dump` rows unless opt-in flags are set.
- [ ] Prefix:
      `CHROMIUM_ARTIFACT_PREFIX=chromium-default-backend-initial-20260615`
- [ ] Backend: launcher default (`WAYLAND_CHROMIUM_BACKEND` unset), multiprocess
      Chromium, QEMU user networking.
- [ ] Fixture URL: `http://10.0.2.2:28213/human-button.html`
- [ ] Result: `CHROMIUM-SUPERVISOR-LOW-NOISE-DONE`
- [ ] Preserved log:
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-default-backend-initial-20260615.run.log`
- [ ] Screenshot:
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-default-backend-initial-20260615.png`
- [ ] Evidence: host fixture saw `GET /human-button.html` and
      `GET /favicon.ico`; Weston/xdg surface trace reached mapped content with
      title `human-button:PASS - Google Chrome for Testing`; screenshot shows
      the fixture page and "I am a human" button.
- [ ] Trace knobs: no extra `CHROMIUM_EXTRA_APPEND`; current harness default
      leaves kernel Chrome lifecycle tracing off unless explicitly requested.

Latest clean X11 repeat runs:

- [ ] Harness: `scripts/gpu/wayland-chromium-supervisor-low-noise.expect`
- [ ] Backend: X11, multiprocess Chromium, QEMU user networking.
- [ ] Fixture URL: `http://10.0.2.2:28213/human-button.html`
- [ ] Prefix `chromium-x11-clean-initial-20260615`: result
      `CHROMIUM-SUPERVISOR-LOW-NOISE-DONE`; preserved log and screenshot:
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-x11-clean-initial-20260615.run.log`,
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-x11-clean-initial-20260615.png`.
- [ ] Prefix `chromium-x11-clean-initial-repeat-20260615`: result
      `CHROMIUM-SUPERVISOR-LOW-NOISE-DONE`; preserved log and screenshot:
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-x11-clean-initial-repeat-20260615.run.log`,
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-x11-clean-initial-repeat-20260615.png`.
- [ ] Evidence for both X11 runs: host fixture saw `GET /human-button.html` and
      `GET /favicon.ico`; Weston surface trace reached mapped content with
      title `human-button:PASS - Google Chrome for Testing`; screenshots show
      the rendered fixture page and button.
- [ ] Trace knobs: no extra `CHROMIUM_EXTRA_APPEND`; current harness default
      leaves kernel Chrome lifecycle tracing off unless explicitly requested.

Positive evidence:

- [ ] Earlier `chromium-x11-stream-scm-plainread` run: the host HTTP fixture
      saw a guest `GET /human-button.html`.
- [ ] Same-boot role evidence identified the NetworkService role from
      argv/thread/fd traces before numeric IDs were interpreted.
- [ ] Trace shows target URL in Chromium argv.
- [ ] Earlier trace shows nonblocking TCP connect to `10.0.2.2:28213`,
      `getsockopt(SO_ERROR)=0`, `tcp-send` carrying `GET /human-button...`,
      and `rcvplus`/`rcvminus` callbacks for the HTTP response.
- [ ] Latest lighter IPC trace shows AF_UNIX `SCM_RIGHTS` traffic between the
      browser, GPU, NetworkService, and child IO threads, including render-node
      and temporary-file fd passing.
- [ ] Latest lighter IPC trace shows the NetworkService role by same-boot argv
      (`--utility-sub-type=network.mojom.NetworkService`), then a nonblocking
      connect to `10.0.2.2:28213` that reaches send readiness:
      `EINPROGRESS`, lwIP `sendplus`, `SO_ERROR=0`, and writable poll revents.
- [ ] Later Weston surface evidence shows Chromium reached a mapped/content
      surface (`mapped=1`, `content=1`, title still `Untitled - Google Chrome
      for Testing`), so the latest timeout is beyond fetch and initial surface
      mapping.
- [ ] Post-TCP-EOF non-traced run with prefix `chromium-x11-tcp-eof-poll`
      exited cleanly and captured
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-x11-tcp-eof-poll.png`;
      the screenshot shows Chromium open with the fixture URL in the omnibox.
- [ ] Focused diagnostic run with prefix `chromium-x11-child-epoll-mojo`
      completed with `CHROMIUM-SUPERVISOR-LOW-NOISE-DONE`, host fixture
      `GET /human-button.html`, later `GET /favicon.ico`, Weston title
      evidence `human-button:PASS - Google Chrome for Testing`, and screenshot
      artifact
      `build-x86_64/wayland-chromium-supervisor-low-noise/chromium-x11-child-epoll-mojo.png`.
- [ ] The earlier diagnostic run's successful fetch was performed by a later
      NetworkService instance after earlier utility children logged Chromium's
      15-second "no connection" watchdog and the browser restarted the network
      service.
- [ ] Current clean default-backend and X11 repeat runs succeeded without an
      extra traced/restarted NetworkService requirement visible in the evidence
      trail.

Latest D-Bus-enabled auto-launch isolation:

- [ ] Harness shape: real `/etc/startup` left intact, so both
      `/bin/dbus-daemon-host` services and Weston started normally.
- [ ] Chromium was still auto-launched by the desktop evidence path using
      `host_chromium=1`; this is a diagnostic contrast, not the final normal
      user workflow proof.
- [ ] Prefix: `chromium-dbus-enabled-20260615b`.
- [ ] Result: `CHROMIUM-DBUS-ENABLED-DONE`.
- [ ] Preserved log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-dbus-enabled-20260615b.run.log`.
- [ ] Screenshot:
      `build-x86_64/chromium-normal-desktop-proof/chromium-dbus-enabled-20260615b.png`.
- [ ] Evidence: host fixture saw `GET /human-button.html` and
      `GET /favicon.ico`; Weston trace reached title
      `human-button:PASS - Google Chrome for Testing`, mapped content, and a
      captured 1280x800 PNG showing the fixture page and button.
- [ ] Interpretation: the normal manual-launch freeze should not be reduced to
      "D-Bus services are present" by itself. The next delta is ordinary
      shell/desktop launch activation versus the desktop auto-launch evidence
      path, plus the browser/render IPC state when the manually launched Chrome
      window becomes unresponsive while the desktop remains healthy.

Latest manual shell launch isolation:

- [ ] Harness shape: copied image with temporary no-D-Bus `/etc/startup`, real
      Weston desktop, Chromium launched later from the guest shell with
      `/bin/wayland-chromium http://10.0.2.2:28213/human-button.html`.
- [ ] Prefix: `chromium-manual-nodbus-20260615e`.
- [ ] Preserved serial log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-manual-nodbus-20260615e.debugcon.log`.
- [ ] Host fixture saw same-boot `GET /human-button.html` and later
      `GET /favicon.ico`.
- [ ] Same-boot role evidence shows `/bin/wayland-chromium` execing the real
      Chromium binary, then live Chromium and crashpad processes in the guest
      `ps` snapshot.
- [ ] Weston surface trace reached title
      `human-button:PASS - Google Chrome for Testing`, `ack-configure`,
      content commits, and `mapped=1`.
- [ ] Screenshot extraction remains missing for this run: the attempted guest
      `fbstat ppm-current` capture did not persist into the copied image, so
      this is render-path serial evidence only, not a visual proof.
- [ ] Interpretation: manual shell launch by itself is not enough to reproduce
      the unresponsive-window failure when D-Bus is removed. Continue from the
      remaining deltas: normal startup with D-Bus helper faults, post-render
      input/event responsiveness, and browser/render IPC if a rendered window
      later stops responding while the desktop stays healthy.

Latest manual post-render input probe:

- [ ] Harness shape: copied image with temporary no-D-Bus `/etc/startup`,
      manual shell launch of Chromium against a host-served `/click.html`
      fixture whose title changes from `click:WAIT` to `clicked:PASS` on
      button click.
- [ ] Prefix: `chromium-manual-nodbus-click-20260615a`.
- [ ] Preserved serial log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-manual-nodbus-click-20260615a.debugcon.log`.
- [ ] Host fixture saw same-boot `GET /click.html` and `/favicon.ico`.
- [ ] Weston trace reached mapped Chromium content and title
      `click:WAIT - Google Chrome for Testing`.
- [ ] Guest `mouseinject` delivered absolute pointer button down/up events at
      the intended approximate button location, but Weston never reported
      `clicked:PASS`.
- [ ] Screenshot extraction again remains missing because the guest `fbstat`
      capture did not persist into the copied image; verify the click target
      with either a reliable visual capture or keyboard `tab`/`enter` before
      reducing this to a kernel input/Wayland event ABI bug.
- [ ] Follow-up keyboard activation control: prefix
      `chromium-manual-nodbus-key-20260615a`, preserved serial log
      `build-x86_64/chromium-normal-desktop-proof/chromium-manual-nodbus-key-20260615a.debugcon.log`.
- [ ] The host fixture saw `GET /key.html`, Weston reached mapped content with
      title `key:WAIT - Google Chrome for Testing`, guest `keyinject key enter`
      succeeded, and Weston then reported
      `keyed:PASS - Google Chrome for Testing`.
- [ ] Interpretation: the earlier mouse-only click miss is not enough evidence
      for a broad Chromium input freeze. Keyboard input reaches the rendered
      Chromium page in the no-D-Bus manual path. Continue from normal-startup
      D-Bus helper faults and any separately reproduced post-render pointer
      coordinate/focus issue.

Latest normal D-Bus manual-launch negative:

- [ ] Harness shape: real `/etc/startup` left intact, so both
      `/bin/dbus-daemon-host` services and Weston started normally. Chromium
      was launched later from the guest shell with
      `/bin/wayland-chromium http://10.0.2.2:28213/key.html`.
- [ ] Prefix: `chromium-manual-dbus-key-20260615d`.
- [ ] Preserved serial log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-manual-dbus-key-20260615d.run.log`.
- [ ] Preserved host HTTP log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-manual-dbus-key-20260615d.host-http.log`.
- [ ] Result: `CHROME-MANUAL-DBUS-KEY-FAIL no-wait-title`.
- [ ] Same-boot role evidence: `ps -l` showed the live Chromium process as
      `/opt/host-gui/wayland-chromium/chrome-linux64/chrome` after the manual
      shell launch, with both D-Bus daemons and Weston still alive.
- [ ] Host fixture evidence: the host server saw same-boot `GET /key.html` and
      later `/favicon.ico`, so the failure is after URL launch and HTTP fetch.
- [ ] Negative render/input evidence: Weston never reported the fixture title
      `key:WAIT - Google Chrome for Testing`, so the run did not reach the
      rendered/input-ready state that the no-D-Bus keyboard control reached.
- [ ] Screenshot extraction failed because QEMU monitor `screendump` returned
      `Error: no surface`; preserve this as serial and host-HTTP evidence only.
- [ ] Interpretation: the user's "Chrome window is unresponsive while the
      desktop is healthy" symptom now has a normal-startup reproducer that
      reaches fetch but not the Chromium surface-title/render-ready state.
      Continue from D-Bus AF_UNIX behavior, browser/render IPC, Mojo readiness,
      epoll/poll revents, futexes, and fd passing after the HTTP response.

Latest live normal D-Bus manual key-page control:

- [ ] Harness shape: live PTY-driven QEMU boot with real `/etc/startup`, both
      `/bin/dbus-daemon-host` services, Weston, and manual shell launch of
      `/bin/wayland-chromium http://10.0.2.2:28213/key.html`.
- [ ] Prefix: `chromium-manual-dbus-live-20260615a`.
- [ ] Preserved debug console:
      `build-x86_64/chromium-normal-desktop-proof/chromium-manual-dbus-live-20260615a.debugcon.log`.
- [ ] Preserved host HTTP log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-manual-dbus-live-20260615a.host-http.log`.
- [ ] Host fixture evidence: same-boot `GET /key.html` returned 200 and
      `/favicon.ico` returned 404.
- [ ] Same-boot role evidence: D-Bus system/session services and Weston were
      alive before launch; Chromium was identified from its executable path;
      NetworkService was identified from thread/trace role before using numeric
      handles.
- [ ] Render evidence: Weston reached title
      `key:WAIT - Google Chrome for Testing`, committed content, and mapped the
      Chromium surface.
- [ ] Input evidence: guest `keyinject key enter` changed the title to
      `keyed:PASS - Google Chrome for Testing`.
- [ ] IPC evidence: AF_UNIX/SCM, D-Bus/GIO, NetworkService, compositor, and
      epoll traces were active; observed `SCM_RIGHTS` sends and receives
      completed without `sendmsg-cmsg-error`, `recvmsg-emit-error`, nonzero
      socket errors, panic, fatal fault, or Chromium watchdog termination.
- [ ] Screenshot evidence was not captured in this live run; treat it as
      serial/host-HTTP/render-title/input evidence only.
- [ ] Interpretation: the current kernel/image does not reproduce a total
      manual normal-D-Bus freeze on the minimal key-page fixture. If the Chrome
      window is still unresponsive in the user's desktop workflow, reproduce the
      exact heavier workflow or page and continue from post-render event loop,
      pointer/focus, compositor/Wayland dispatch, and external-page/background
      network behavior rather than reopening D-Bus presence or initial fetch.

Latest normal no-user-URL typed-navigation probe:

- [ ] Runtime-only harness:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-input-step.expect`.
- [ ] Prefix: `chromium-noarg-input-step-20260615a`.
- [ ] Normal `/etc/startup` was left intact with both D-Bus services and Weston.
      Chromium was launched from the guest shell as `/bin/wayland-chromium`
      with no user-supplied URL; the current launcher appends `about:blank`
      internally for `argc == 1`.
- [ ] Preserved run log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-input-step-20260615a.run.log`.
- [ ] Preserved host HTTP log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-input-step-20260615a.host-http.log`.
- [ ] Preserved framebuffer capture:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-input-step-20260615a.png`.
- [ ] Guest-side evidence: pre-Chromium `keyinject key enter` completed, then
      after Chromium launch `keyinject chord ctrl+l`,
      `keyinject text http://10.0.2.2:28213/input-smoke.html`,
      `keyinject key enter`, and `keyinject text xv6` all completed.
- [ ] Negative browser evidence: the host HTTP fixture saw no request at all.
      The captured framebuffer shows Chromium mapped on `about:blank`, with
      `xv6` in the omnibox/search UI after the later page-text probe.
- [ ] Interpretation: this was a useful negative probe, but the following
      phase-controlled run supersedes it. Treat this as focus/timing evidence,
      not as proof of a typed-navigation ABI failure, unless a same-run capture
      proves the Chrome window was mapped before URL input.

Latest normal no-user-URL typed-navigation and page-input control:

- [ ] Runtime-only harness:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-url-enter-phases.expect`.
- [ ] Fresh current-image unresponsive check: prefix
      `chromium-unresponsive-check-20260615a`, normal `/etc/startup` with both
      D-Bus services and Weston, `/bin/wayland-chromium` launched with no
      user-supplied URL, then keyboard navigation to
      `http://10.0.2.2:28213/key.html` and page-level Enter.
- [ ] Fresh evidence:
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-check-20260615a.run.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-check-20260615a.host-http.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-check-20260615a-after-enter.png`,
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-check-20260615a-after-page-key.png`.
- [ ] Fresh result: the host fixture saw `GET /key.html`; the after-enter
      screenshot shows the rendered page and tab title `key:WAIT`; after the
      page-level Enter, the screenshot shows title `keyed:PASS`. This does not
      reproduce a total Chromium input freeze on the minimal normal-startup
      path.
- [ ] User-observed unresponsive-window follow-up:
      `chromium-user-unresponsive-key200-20260615a` used the same current-image
      normal-startup/no-user-URL path with a real `key.html` fixture. It fetched
      `GET /key.html` with HTTP 200 and captured the rendered `key:WAIT` page,
      then accepted guest `keyinject key enter` but timed out on the next guest
      `fbstat` capture. This narrows that failure to a post-render/post-input
      liveness or framebuffer-capture boundary, not initial fetch or mapping.
- [ ] Immediate repeat with a runtime-only monitor-before-guest-capture
      checkpoint, prefix
      `chromium-user-unresponsive-monitor-afterkey-20260615b`, fetched
      `GET /key.html` with HTTP 200 and completed. The after-page-key screenshot
      shows `keyed:PASS`, proving the page can process the same Enter and that
      the current normal path is intermittent rather than deterministically
      frozen on the minimal fixture.
- [ ] Fresh pointer control: prefix `chromium-pointer-click-check-20260615c`
      used a temporary runtime-only click fixture under `build-x86_64`,
      navigated with no user-supplied launcher URL, then clicked the rendered
      page button with absolute tablet coordinates scaled to the 0..65535
      input range.
- [ ] Pointer evidence:
      `build-x86_64/chromium-normal-desktop-proof/chromium-pointer-click-check-20260615c.run.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-pointer-click-check-20260615c.host-http.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-pointer-click-check-20260615c-after-enter.png`,
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-pointer-click-check-20260615c-after-page-click.png`.
- [ ] Pointer result: the host fixture saw `GET /click.html`; after the
      correctly scaled page click, the screenshot shows tab title
      `clicked:PASS`. Earlier failed click attempts in this series were
      coordinate-calibration misses, not evidence of a Chromium pointer freeze.
- [ ] Latest focused real-click control after kernel log cleanup:
      `chromium-click-smoke-logcleanup-20260615a` used a temporary
      runtime-only `click-smoke.html` fixture under `build-x86_64`, navigated
      with no user-supplied launcher URL, clicked the rendered page button with
      corrected absolute tablet coordinates, and completed
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`.
- [ ] Latest click artifacts:
      `build-x86_64/chromium-normal-desktop-proof/chromium-click-smoke-logcleanup-20260615a.run.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-click-smoke-logcleanup-20260615a.host-http.log`,
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-click-smoke-logcleanup-20260615a-after-page-click.png`.
- [ ] Latest click result: host HTTP saw `GET /click-smoke.html`; the final
      screenshot shows tab/title `click:PASS` and button text `clicked`. This
      proves the current minimal normal path can process a real content click;
      the user-visible unresponsive-window report remains open for the exact
      heavier page/workflow or intermittent focus/cursor path.
- [ ] Fresh unresponsive follow-up after the user's desktop-healthy report:
      prefix `chromium-unresponsive-fresh-20260615a` used the same normal
      startup/no-user-URL path but typed too early or without stable focus; the
      URL was mangled into a Google search and the run timed out on the next
      key injection. Treat this as a focus/timing artifact, not as post-render
      browser evidence.
- [ ] Clean repeat with a longer launch settle and the real `key.html` fixture:
      prefix `chromium-unresponsive-keyfixture-longwait-20260615a`; normal
      `/etc/startup`, D-Bus services, Weston, no launcher URL, keyboard
      navigation to `http://10.0.2.2:28213/key.html`, liveness check, and
      page-level Enter.
- [ ] Fresh clean-repeat artifacts:
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-keyfixture-longwait-20260615a.run.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-keyfixture-longwait-20260615a.host-http.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-keyfixture-longwait-20260615a-after-enter.png`,
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-unresponsive-keyfixture-longwait-20260615a-after-page-key.png`.
- [ ] Fresh clean-repeat result: host fixture saw `GET /key.html` HTTP 200,
      the guest shell answered `AFTER_ENTER_ALIVE`, framebuffer captures
      completed, and the after-page-key screenshot shows title `keyed:PASS`.
      This keeps the minimal normal-start page-input path green. If the user's
      Chrome window remains unresponsive, continue with the exact heavier page
      or workflow rather than reducing it to D-Bus presence, initial fetch, or
      the minimal fixture path.
- [ ] Fresh desktop-healthy unresponsive check after the user report:
      `chromium-user-unresponsive-repro-20260615a` reproduced an early
      shell/fbstat liveness timeout after Chrome launch when bounded Unix IPC,
      epoll, poll-summary, and slow-syscall tracing were enabled. It did not
      reach URL entry and the host HTTP log was empty; treat it as a
      trace/timing-sensitive startup liveness sample, not page fetch/render
      evidence.
- [ ] Clean A/B repeat with the same long launch settle and real `key.html`
      fixture, `chromium-user-unresponsive-keyfixture-clean-20260615a`,
      completed `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`. Host HTTP saw
      `GET /key.html` HTTP 200, the guest shell answered `AFTER_ENTER_ALIVE`,
      and the after-page-key screenshot shows `keyed:PASS`.
- [ ] Interpretation: the current minimal normal-start/no-user-URL fixture path
      still renders and accepts page-level keyboard input. The user-visible
      unresponsive Chrome window remains open for the exact heavier page,
      Crashpad-enabled/normal-path timing, refresh-once first-load path, or
      post-render workflow that reproduces the symptom without broad tracing.
- [ ] Refresh-once follow-up for the user's "Chrome window unresponsive,
      desktop healthy" report: low-noise prefix
      `chromium-user-unresponsive-refresh-key-20260615b` loaded the real
      `key.html` fixture, captured rendered `key:WAIT`, proved guest shell
      liveness with `AFTER_ENTER_ALIVE`, then sent guest `keyinject chord
      ctrl+r`. The host fixture saw the refresh `GET /key.html` HTTP 304, but
      the next guest `fbstat` capture timed out at
      `capture-after-refresh-timeout`. Treat this as post-refresh guest
      capture/liveness or GPU/fb wait evidence after network response, not DNS,
      initial render, or desktop startup.
- [ ] Trace-heavy repeat
      `chromium-user-unresponsive-refresh-threadfence-20260615a` perturbed URL
      entry and timed out on the Ctrl+R acknowledgement while serial output was
      interleaved with slow futex/epoll trace rows. It showed long waits in
      Chrome compositor, child IO, service worker, audio, and thread-pool roles,
      plus early transient `D` samples around render-node open, but had an empty
      host HTTP log. Use it only as timing/trace-interference context.
- [ ] Monitor-refresh split
      `chromium-user-unresponsive-monitor-refresh-key-20260615a` used the same
      page and normal startup path, sent refresh through the QEMU monitor, saw
      host `GET /key.html` HTTP 304, then guest `keyinject key enter` and
      guest framebuffer capture completed. The final screenshot
      `build-x86_64/chromium-normal-desktop-proof/chromium-user-unresponsive-monitor-refresh-key-20260615a-after-page-key.png`
      shows tab title `keyed:PASS`. QEMU monitor screendump still reported
      `Error: no surface`, so do not rely on monitor screenshots for this gate.
- [ ] Interpretation: the current minimal fixture does not prove a persistent
      browser event-loop freeze after refresh; page input can still work after
      a monitor-triggered refresh. The remaining reproduced negative is the
      guest-triggered refresh followed by immediate guest `fbstat` timeout.
      Continue from guest input/capture ordering, framebuffer readback waits,
      virtio-gpu/fence wait paths, and post-refresh Chrome compositor/IO waits.
- [ ] Follow-up control
      `chromium-user-unresponsive-pagekey-no-refresh-20260615a` loaded the same
      real `key.html` fixture without a refresh, sent `keyinject key enter`
      directly to the rendered page, completed the guest framebuffer capture,
      and the final screenshot shows tab title `keyed:PASS`.
- [ ] Follow-up negative
      `chromium-user-unresponsive-refresh-cached-scanout-20260615a` used the
      same page with a temporary scanout-read diagnostic experiment; the page
      rendered `key:WAIT`, but guest Ctrl+R did not produce a refresh request in
      the host HTTP log before `capture-after-refresh-timeout`. The temporary
      scanout-read experiment was removed because it did not address this path.
- [ ] Updated interpretation: minimal content keyboard input works after the
      first render, so do not treat the fixture as a blanket renderer/input
      freeze. Continue from the guest-injected browser accelerator path
      (`Ctrl+R`) and focused-window/event routing across virtio-input, Weston,
      XWayland/X11, and Chromium browser-process dispatch; keep framebuffer
      readback waits as secondary evidence only when the refresh request is
      already observed.
- [ ] Kernel `/dev/kbd` synthetic-scancode compatibility gap closed for
      guest-injected accelerator chords. `kernel/kernel/dev/ps2kbd.c` now sends
      scancode-bearing synthetic writes through the same PS/2 byte translation
      path as hardware input, preserving the legacy direct event path only for
      keycode-only writes. This keeps modifier/key-down/keycode state aligned
      for `keyinject chord ctrl+r` and similar probes.
- [ ] Refresh-once proof after the `/dev/kbd` fix:
      `chromium-refresh-kbdwrite-normalize-20260615a` completed
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`. Host HTTP saw initial
      `GET /key.html` HTTP 200, `/favicon.ico` HTTP 404, then the guest
      Ctrl+R refresh `GET /key.html` HTTP 304. The final page-key screenshot
      `build-x86_64/chromium-normal-desktop-proof/chromium-refresh-kbdwrite-normalize-20260615a-after-page-key.png`
      shows tab title `keyed:PASS`.
- [ ] Regression gates after the `/dev/kbd` fix: narrow kernel rebuild passed,
      `git -C kernel diff --check -- kernel/dev/ps2kbd.c` passed, X11 host-idle
      input proof passed with
      `HOSTIDLE-X11-PASS launch_changed_pixels=565869 input_changed_pixels=3755 exit_changed_pixels=565869`,
      and the mandatory video gate passed with
      `xv6-perf-video:RESULT pass fps=55.5 speed=1.001 presentedFPS=0.0 decodedFPS=55.5 dropPct=0.00 advanced=15.32`.
- [ ] Updated interpretation: the guest-triggered refresh failure was at least
      partly a kernel input ABI mismatch in synthetic `/dev/kbd` scancode
      handling, not a network, DNS, desktop-startup, or blanket renderer
      freeze. Continue the user's unresponsive-Chrome report from remaining
      normal-path/Crashpad timing, heavier-page behavior, post-refresh
      compositor/IPC readiness, and any still-reproducible focused-window
      event routing gaps.
- [ ] Kernel `/dev/shm` compatibility gap closed for the Chromium
      `--disable-dev-shm-usage` workaround probe. `kernel/kernel/vfs/fs.c`
      now creates `/dev/shm` with Linux-style `01777` mode after the real root
      is mounted and mounts `tmpfs` there; procfs mount listings now include
      `/dev` and `/dev/shm`.
- [ ] `/dev/shm` proof artifacts:
      `build-x86_64/devshm-proof/devshm-proof-output.run.log` shows the boot
      line `tmpfs: mounted at /dev/shm` and a guest write/read of
      `/dev/shm/probe`; `build-x86_64/devshm-proof/devshm-proof-procfs.run.log`
      shows `/proc/mounts` reporting
      `tmpfs /dev/shm tmpfs rw,nosuid,nodev 0 0`.
- [ ] Diagnostic launcher proof without `--disable-dev-shm-usage`: a
      runtime-only launcher binary was built under
      `build-x86_64/chromium-devshm-flag-proof/`, verified not to contain the
      flag string, and injected into a copied fs image only.
- [ ] Diagnostic Chromium evidence:
      prefix `chromium-no-disable-dev-shm-key-20260615c`; run log
      `build-x86_64/chromium-normal-desktop-proof/chromium-no-disable-dev-shm-key-20260615c.run.log`;
      host HTTP log
      `build-x86_64/chromium-normal-desktop-proof/chromium-no-disable-dev-shm-key-20260615c.host-http.log`;
      screenshots
      `build-x86_64/chromium-normal-desktop-proof/chromium-no-disable-dev-shm-key-20260615c-after-enter.png`
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-no-disable-dev-shm-key-20260615c-after-page-key.png`.
- [ ] Diagnostic result: Chromium fetched `GET /key.html` with HTTP 200 and
      the after-page-key screenshot shows tab title `keyed:PASS`, proving this
      kernel ABI fix is sufficient to remove that one launcher workaround from
      the Chromium stress probe. Do not convert this into a launcher/rootfs
      edit unless the active task explicitly moves from kernel ABI closure to
      pruning probe flags.
- [ ] Sandbox flag diagnostic: removing `--no-sandbox` alone is not a useful
      kernel reducer while the launcher still disables the zygote/single-process
      cluster. Runtime-only serial-log probe
      `chromium-no-nosandbox-serial-20260615a` failed before mapping Chromium
      with Chromium's own message:
      `Zygote cannot be disabled if sandbox is enabled`.
- [ ] Sandbox+zygote diagnostic: removing `--no-sandbox` together with the
      single-process/no-zygote cluster reaches Chromium's root guard, not a
      syscall failure. Runtime-only serial-log probe
      `chromium-sandbox-zygote-serial-20260615a` logs:
      `Running as root without --no-sandbox is not supported`. Treat this as
      an execution-credential/desktop-user boundary before spending kernel time
      on seccomp/userns sandbox ABI; do not claim a kernel sandbox gap from
      this evidence alone.
- [ ] Redundant sandbox-disable flags under `--no-sandbox`: runtime-only probe
      `chromium-no-extra-sandbox-disables-20260615a` removed
      `--disable-setuid-sandbox`, `--disable-seccomp-filter-sandbox`,
      `--disable-gpu-sandbox`, and `--disable-dev-shm-usage` while keeping
      `--no-sandbox`. Chromium fetched `GET /key.html` with HTTP 200 and the
      after-page-key screenshot shows `keyed:PASS`. This proves those extra
      sandbox-disable flags are not current kernel blockers on the root
      no-sandbox path.
- [ ] Vulkan diagnostic: runtime-only probe
      `chromium-no-vulkan-disable-20260615a` additionally removed
      `--disable-vulkan`. Chromium fetched `GET /key.html` with HTTP 200 and
      the after-page-key screenshot shows `keyed:PASS`; Vulkan probing falls
      back cleanly on the current KVM/virgl image.
- [ ] Crashpad diagnostic: runtime-only serial probe
      `chromium-crashpad-enabled-serial-20260615a` removed the explicit
      crash-disabling switches and removed `Crashpad` from
      `--disable-features`. It mapped Chromium but left the browser on
      `about:blank` after typed navigation; the host fixture saw no
      `GET /key.html`.
- [ ] Crashpad feature split: runtime-only probe
      `chromium-crashpad-feature-enabled-serial-20260615a` kept
      `--disable-breakpad`, `--disable-crashpad`, and
      `--disable-crash-reporter`, but removed only `Crashpad` from
      `--disable-features`. It timed out before the initial framebuffer
      capture and made no host HTTP request.
- [ ] Crashpad lifecycle split: rerunning that same image with only
      `chrome_lifecycle_trace=1` completed successfully:
      `chromium-crashpad-feature-lifecycle-20260615a` fetched `GET /key.html`
      with HTTP 200 and the after-page-key screenshot shows `keyed:PASS`.
      Same-boot role evidence shows the browser spawning
      `chrome_crashpad_handler --monitor-self`, that handler spawning a second
      `chrome_crashpad_handler --no-periodic-tasks`, and both intermediate
      helper parents exiting cleanly. Treat this as timing-sensitive early
      helper/process-supervision evidence, not a deterministic compositor,
      DNS, or HTTP failure.
- [ ] Crashpad + `CLONE_CLEAR_SIGHAND` follow-up: runtime-only image
      `build-x86_64/chromium-crashpad-flag-proof/crashpad-enabled-serial-src.fs.img`
      with prefix `chromium-crashpad-after-enter-guestfb-20260615a` fetched
      `GET /key.html`, requested `/favicon.ico`, logged the page console
      message, and produced guest-framebuffer visual proof at
      `build-x86_64/chromium-normal-desktop-proof/chromium-crashpad-after-enter-guestfb-20260615a-after-enter.png`
      showing title `key:WAIT`. The earlier QEMU monitor `Error: no surface`
      path is a host monitor-capture artifact for this phase, not proof that
      Chromium failed to render.
- [ ] Crashpad-enabled repeat negative:
      `chromium-crashpad-pagekey-guestfb-20260615a` and
      `chromium-crashpad-browserdump-liveness-20260615a` both accepted focus,
      URL text, and navigation Enter, and the host fixture saw
      `GET /key.html`, but no favicon/page-console evidence followed and the
      guest shell did not answer the next framebuffer/liveness command before
      timeout. This matches the user's "Chrome window is unresponsive while
      desktop is healthy" shape on the Crashpad-enabled diagnostic path.
- [ ] Crashpad-enabled first-load/follow-up split:
      `chromium-crashpad-refresh-once-guestfb-20260615a` quietly reproduced
      the post-navigation wedge after a successful `GET /key.html`, favicon
      404, and page console message: the next guest `fbstat` capture timed out.
      `chromium-crashpad-refresh-once-skip-afterenter-20260615a` skipped that
      capture and then timed out trying to run guest-shell
      `keyinject chord ctrl+r`, so serial-shell injection is also part of the
      blocked surface after the first load.
- [ ] Crashpad-enabled host-monitor refresh distinction:
      `chromium-crashpad-monitor-refresh-20260615a` used QEMU monitor
      `sendkey ctrl-r` after the same successful first fetch/console point and
      completed the harness, but the host fixture did not see a second
      `GET /key.html` and QEMU monitor `screendump` returned `Error: no
      surface`. Treat this as evidence that host-side key injection is not
      enough proof of reload/render; continue from the post-fetch input/event
      and framebuffer/scanout boundary.
- [ ] Crashpad-enabled thread-pidfd follow-up:
      `chromium-crashpad-thread-pidfd-kernel-20260615a` booted the patched
      kernel with Crashpad enabled and lifecycle/syscall tracing. It completed
      the harness and kept the guest shell responsive, but the host HTTP log
      was empty and screenshots showed the harness typed before Chrome was
      mapped, then later captured `about:blank`; treat it as a startup/focus
      timing artifact, not page-load proof.
- [ ] Crashpad-enabled refresh-once proof after the thread-pidfd kernel fix:
      `chromium-crashpad-thread-pidfd-refresh-20260615a` used the same
      Crashpad-enabled source image, a longer launch wait, and an explicit
      `ctrl+r` after first navigation. The host fixture saw `GET /key.html`
      HTTP 200, `/favicon.ico` 404, then refreshed `GET /key.html` HTTP 304.
      The after-refresh screenshot shows rendered `key:WAIT`, and the
      after-page-key screenshot shows tab title `keyed:PASS`.
- [ ] Interpretation: the user's "refresh once to load the page" observation is
      real on the Crashpad-enabled diagnostic path. With enough startup wait
      and the refresh, Chromium processes page keyboard input after the
      thread-pidfd kernel fix. Continue from why the first navigation can wedge
      or lose readiness before/around NetworkService/Mojo/page activation,
      rather than from DNS, static PIDs, desktop health, or rootfs packaging.
- [ ] Browser-only thread dump evidence from
      `chromium-crashpad-browserdump-liveness-20260615a` identified the
      same-boot Chromium role by executable path and thread names. Before
      typed navigation, the browser had live UI/IO, NetworkService,
      VizCompositor, in-process GPU, compositor, and renderer/service-worker
      threads, mostly parked in Linux-shaped `poll`, `epoll_pwait`, or futex
      waits with some transient runnable/D-state VM/file activity. The sample
      window ended before the post-GET liveness timeout, so continue with a
      longer post-navigation sample rather than treating the existing dump as
      the final blocked-thread proof.
- [ ] Heavy diagnostic caution:
      `chromium-crashpad-postget-longdump-20260615a` failed at `ctrl-l-timeout`
      because the opt-in thread-dump firehose interleaved through the shell
      output and confused the expect matcher. Do not use broad thread-dump
      logging for the next refresh/input reducer; prefer a lower-volume
      targeted trace or a reducer for the implicated event/console/fb path.
- [ ] Current-default control after the `CLONE_CLEAR_SIGHAND` kernel change:
      prefix `chromium-current-default-after-clear-sighand-20260615a`, normal
      `/etc/startup` with D-Bus and Weston, `/bin/wayland-chromium` launched
      without a user URL, then Ctrl-L navigation to
      `http://10.0.2.2:28213/key.html` and page-level Enter.
- [ ] Current-default result: `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; the host
      fixture saw `GET /key.html` and `/favicon.ico`; framebuffer captures
      were preserved for blank, typed, after-enter, and after-page-key phases.
      This keeps the minimal normal launcher path green after the clone3/signal
      ABI patch.
- [ ] Prefix: `chromium-noarg-key-page-monitor-20260615a`.
- [ ] Normal `/etc/startup` was left intact with both D-Bus services and
      Weston. Chromium was launched from the guest shell as
      `/bin/wayland-chromium` with no user-supplied URL, then the harness
      focused the omnibox, typed `http://10.0.2.2:28213/key.html`, pressed
      Enter, waited for render, and pressed Enter again inside the page.
- [ ] Preserved run log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-key-page-monitor-20260615a.run.log`.
- [ ] Preserved host HTTP log:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-key-page-monitor-20260615a.host-http.log`.
- [ ] Preserved framebuffer captures:
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-key-page-monitor-20260615a-blank.png`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-key-page-monitor-20260615a-typed.png`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-key-page-monitor-20260615a-after-enter.png`,
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-noarg-key-page-monitor-20260615a-after-page-key.png`.
- [ ] Evidence: the host fixture saw same-boot `GET /key.html` and
      `/favicon.ico`; the after-enter screenshot shows visible rendered page
      content and title `key:WAIT`; after page-level Enter, the screenshot shows
      title `keyed:PASS`.
- [ ] Interpretation: no-user-URL typed navigation and renderer keyboard input
      work on the minimal key-page fixture when the harness waits until the
      Chrome window is actually mapped and focused. Earlier no-request or
      about:blank captures are now treated as focus/timing artifacts unless a
      same-run screenshot proves the Chrome window was mapped before input.
      If the user's desktop Chrome window remains unresponsive, reproduce that
      exact heavier workflow/page and continue from post-render event loop,
      pointer/focus, compositor dispatch, or external-page background network
      behavior, not DNS or initial typed-navigation ABI.
- [ ] Explicit sync-file ABI follow-up:
      `chromium-pruned-multiprocess-syncfile-wait-20260615a` used the
      Crashpad-enabled source image, pruned multiprocess Chromium, normal
      D-Bus/Weston startup, and the no-user-URL phase harness.
- [ ] Kernel changes under test: dma-buf/sync-file ioctl support from the prior
      patch is preserved, and DRM execbuffer input sync-file waits now use an
      uninterruptible dma-fence wait so Crashpad `SIGCONT` delivery does not
      surface as `DRM: execbuffer input fence wait failed ret=-4`.
- [ ] Result: `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; host HTTP saw
      `GET /key.html` and `/favicon.ico`, and the old
      `DMA_BUF_IOCTL_IMPORT_SYNC_FILE`, null sync-file-info, missing timestamp,
      `execbuffer input fence wait failed`, and `GPU process exited
      unexpectedly` rows were absent.
- [ ] Remaining negative evidence: the screenshots still show a live
      `about:blank` Chromium window, including after URL text and Enter; the
      URL did not appear in the omnibox and the page did not render. Same-run
      lifecycle evidence shows Crashpad logging `ptrace: Operation not
      permitted`, sending `SIGCONT` to the GPU process threads, and Chromium
      renderer/GPU process groups exiting before the harness input. Continue
      from Linux `ptrace`/Crashpad process-supervision semantics and the
      resulting browser/renderer/GPU recovery path, not from DNS, desktop
      health, or the already-fixed explicit-sync ioctl surface.
- [ ] Crashpad ptrace/procfs follow-up: `PTRACE_ATTACH`,
      `PTRACE_GETREGSET NT_PRSTATUS`, `PTRACE_GETREGSET NT_FPREGSET`, and
      detach now cover the Crashpad GPU-process inspection path without the
      old `EPERM`, `EINVAL`, or `detach_child: parent has no children` panic.
      Procfs maps/smaps now emit Linux-shaped map lines with a delimiter after
      the inode field and file-backed `dev:inode` values from the same inode
      `getattr()` path used by stat/fstat.
- [ ] Post-fix Crashpad probes:
      `chromium-crashpad-map-devino-fix-20260615a` and
      `chromium-crashpad-quiet-afterenter-20260615a` both fetched
      `GET /key.html` with HTTP 200 and no longer logged Crashpad
      `memory_map.cc` format errors, `process_reader_linux.cc` no-module rows,
      ptrace `Invalid argument`, or the ptrace detach assertion. They still
      timed out after first navigation when the guest shell attempted the next
      liveness/framebuffer step, so the remaining issue is beyond maps parsing
      and basic ptrace.
- [ ] Log hygiene: Chromium signal-send tracing is now opt-in via
      `chrome_signal_trace=1`; expensive user backtraces require
      `chrome_signal_backtrace=1` or broader syscall tracing. Default Chromium
      runs no longer print the repeated `signal: kill ... ThreadPoolSingl`
      backtraces.
- [ ] Refresh repro nuance after these fixes:
      `chromium-crashpad-monitor-refresh-20260615a` used QEMU monitor
      `sendkey ctrl-r` after first navigation and completed the harness, but
      the fixture saw only the initial `GET /key.html` and `/favicon.ico`, and
      monitor `screendump` still returned `Error: no surface`. Treat it as
      evidence that host-side refresh injection can keep the harness moving,
      not as visual proof that the page rendered after refresh.

Latest Chromium unresponsive-window A/B:

- [ ] Stale per-run filesystem images under
      `build-x86_64/chromium-normal-desktop-proof/` and the low-noise
      supervisor temp image were removed; run logs/screenshots and source
      images were preserved.
- [ ] Earlier Crashpad-enabled, multiprocess source-image runs without the
      default crashpad/dev-shm/vulkan disables reproduced a Chrome-specific
      unresponsive window on the accelerated GPU path. The latest minimal
      `input-smoke.html` accelerated controls below supersede that as a
      categorical failure, but keep heavier Crashpad/normal-path timing open.
- [ ] `chromium-normalpath-nogpuquiet-control-20260615a` used the same quiet
      trace budget as the successful split, launched
      `WAYLAND_CHROMIUM_MULTIPROCESS=1 /bin/wayland-chromium`, mapped a normal
      `about:blank` Chrome window, then failed at `ctrl-l-timeout` before any
      fixture HTTP request. Evidence:
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-nogpuquiet-control-20260615a.run.log`,
      blank frame
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-nogpuquiet-control-20260615a-blank.png`,
      empty host HTTP log
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-nogpuquiet-control-20260615a.host-http.log`.
- [ ] The A/B positive control
      `chromium-normalpath-disablegpu-probe-20260615a` used the same source
      image and trace budget but added only
      `WAYLAND_CHROMIUM_EXTRA_FLAGS=--disable-gpu`; it completed
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`, fetched
      `GET /input-smoke.html` and `/favicon.ico`, rendered the page, and
      accepted page key input with title `typed:a`. Evidence:
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-disablegpu-probe-20260615a.run.log`,
      visual proof
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-disablegpu-probe-20260615a-after-page-key.png`,
      host HTTP log
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-disablegpu-probe-20260615a.host-http.log`.
- [ ] Earlier heavier trace
      `chromium-normalpath-current-slowtrace-20260615b` reached
      `GET /input-smoke.html` on the accelerated GPU path but then timed out at
      after-enter liveness; its typed screenshot still showed visually stale
      `about:blank`. Treat the heavy run as supporting evidence only because
      its epoll/thread dump volume can perturb the shell.
- [ ] Current-kernel accelerated A/B before the PRIME fd-flag patch:
      `chromium-normalpath-accel-current-20260615a` completed the phase
      harness, and the host fixture saw `GET /input-smoke.html`, but the final
      framebuffer still showed stale `about:blank` with no favicon request,
      page console row, or old GPU/NetworkService/sync-file failure rows.
- [ ] Focused DRM trace
      `chromium-normalpath-accel-current-fencetrace-20260615a` was diagnostic
      only because trace volume perturbed capture. Its DRM ioctl rows all
      returned success, including PRIME export/import, RESOURCE_INFO,
      EXECBUFFER, and WAIT; no explicit ioctl error explained the stale
      accelerated window.
- [ ] Same-source `--disable-gpu` control
      `chromium-normalpath-disablegpu-current-nolive-20260615a` passed:
      `GET /input-smoke.html`, `/favicon.ico`, page console, and final visual
      title `typed:a`. This keeps the failure bounded to the accelerated
      GPU/presentation path, not DNS, keyboard injection, Crashpad source
      image, or basic renderer input.
- [ ] DRM PRIME Linux ABI gap under test:
      `DRM_IOCTL_PRIME_HANDLE_TO_FD` now accepts only `DRM_CLOEXEC |
      DRM_RDWR`, applies `O_RDWR` to the exported dma-buf file status when
      requested, and installs `FD_CLOEXEC` under the fdtable lock when
      requested. `DRM_IOCTL_PRIME_FD_TO_HANDLE` now rejects nonzero unused
      flags like Linux. The first run of this patch caught a local
      fdtable-lock assertion before the lock fix; preserve
      `chromium-normalpath-accel-primeflags-20260615a.run.log` as evidence of
      that patch-internal bug, not as Chromium evidence.
- [ ] Post-lock PRIME flag probe
      `chromium-normalpath-accel-primeflags-lock-20260615a` fetched
      `GET /input-smoke.html` and emitted the page console row, with no
      old GPU/NetworkService/sync-file failure rows, but then timed out in the
      immediate after-enter framebuffer capture. QEMU monitor `screendump`
      also reported `Error: no surface`. Treat this as progress past a PRIME
      fd flag mismatch plus remaining accelerated scanout/readback/present
      blocking, not as a completed Chrome responsiveness proof.
- [ ] Skip-after-enter-capture probe
      `chromium-normalpath-accel-primeflags-skipcapture-key-20260615a`
      completed the harness and later framebuffer capture, but did not reach
      the host fixture and the final frame remained `about:blank`; do not count
      it as page-input proof. It only shows the shell/key injection path can
      remain responsive when the immediate post-load capture is removed from a
      run that never actually navigated.
- [ ] Cursor-rotation/log-cleanup accelerated control
      `chromium-normalpath-accel-cursor16-wait35-20260615a` used the
      Crashpad-enabled corrected source image, GPU enabled, and
      `WAYLAND_CHROMIUM_MULTIPROCESS=1`. It completed
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; host HTTP saw
      `GET /input-smoke.html` and `/favicon.ico`; the page console row appeared;
      and the final screenshot
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-accel-cursor16-wait35-20260615a-after-page-key.png`
      shows tab title `typed:a` with `a` visible in the page input. No old
      NetworkService/no-connection/GPU/sync-file/execbuffer/capture failures
      were present in the focused grep.
- [ ] Shorter-wait sibling
      `chromium-normalpath-accel-cursor16-20260615a` fetched and rendered the
      same page but raced page activation before the final key, so treat it as
      timing evidence, not an input failure.
- [ ] The immediate default-launch repeat
      `chromium-normalpath-defaultlaunch-cursor16-wait35-20260615a` timed out
      before Chromium launched while waiting for the desktop-ready marker. Its
      host HTTP log was empty and the log stopped around Weston/Mesa desktop
      startup, so preserve it as an inconclusive desktop-start sample, not as
      Chromium evidence.
- [ ] Clean default-launch rerun
      `chromium-normalpath-defaultlaunch-rerun-20260615b` launched
      `/bin/wayland-chromium` with no extra environment, completed
      `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`, fetched `GET /input-smoke.html`
      and `/favicon.ico`, captured every framebuffer phase, and the final
      screenshot
      `build-x86_64/chromium-normal-desktop-proof/chromium-normalpath-defaultlaunch-rerun-20260615b-after-page-key.png`
      shows title `typed:a` and input `a`. This validates the current default
      launcher path for the minimal fixture, while still leaving the baked-in
      launcher workaround flags as the remaining "no special settings" gap.
- [ ] Runtime-only multiprocess/GPU-default wrapper proof
      `chromium-multiprocess-default-noenv-tolerant-20260615a` replaced
      `/bin/wayland-chromium` only in a copied fs image with a diagnostic
      launcher that defaults to multiprocess Chromium, keeps GPU enabled,
      leaves Crashpad enabled, omits the extra sandbox-disable/dev-shm/Vulkan
      workaround flags, and requires no external Chromium environment. It
      completed `CHROMIUM-NOARG-URL-ENTER-PHASES-DONE`; host HTTP saw
      `GET /input-smoke.html` and `/favicon.ico`; page console evidence
      appeared; and
      `build-x86_64/chromium-normal-desktop-proof/chromium-multiprocess-default-noenv-tolerant-20260615a-after-page-key.png`
      shows title `typed:a` and input `a`. This is evidence that the current
      kernel handles the minimal multiprocess/GPU/Crashpad-enabled path, but it
      is not a durable rootfs/user-program edit and it still carries
      `--no-sandbox`.
- [ ] Sandbox feature work is skipped by user request because it involves
      namespace/container-style kernel isolation. Runtime-only non-root
      diagnostic `chromium-nonroot-sandbox-lowtrace-20260615a` proved the
      boundary: after dropping to uid/euid 1000 and removing `--no-sandbox`,
      Chromium aborted with `No usable sandbox!`; the host HTTP log stayed
      empty and screenshots showed only the desktop after the browser exited.
      Do not chase user namespaces, pid/net/mount namespaces, seccomp filter
      engines, or SUID `chrome-sandbox` as part of the active goal unless the
      user reopens sandbox explicitly.
- [ ] Runtime-only minimal-flag wrapper proof
      `chromium-minflags-local-20260615a` replaced `/bin/wayland-chromium`
      only in a copied fs image with a launcher whose Chromium argv contains
      only the Wayland Ozone selection and the user-approved `--no-sandbox`
      exception. It omits the previous disable-gpu, in-process-gpu,
      single-process, no-zygote, background-networking, sync,
      renderer-accessibility, first-run, default-browser-check, feature-disable,
      profile-dir, dev-shm, Vulkan, and Crashpad workarounds. Chromium started
      multiprocess/GPU/Crashpad-capable enough to render the normal first-run
      "Sign in to Chromium" UI; no kernel fatal, root guard, or sandbox fatal
      occurred. The host HTTP log stayed empty because first-run UI captured
      the harness input before URL navigation.
- [ ] Follow-up first-run dismissal probes
      `chromium-minflags-first-run-local-20260615a` and
      `chromium-minflags-first-run-clickfix-20260615a` kept the same
      minimal-flag runtime launcher and tried to dismiss first-run UI through
      guest mouse injection. The clicks did not activate "Stay signed out", so
      no fixture HTTP request was made. Treat this as a first-run/browser-UI
      automation or pointer/focus issue, not a DNS, HTTP, renderer, GPU, or
      kernel-sandbox failure. A keyboard-dismiss repeat
      `chromium-minflags-first-run-keyboard-20260615a` timed out during
      desktop startup and is inconclusive.
- [ ] Updated first-run/profile diagnosis:
      `chromium-minflags-first-run-multiclick-20260615a` used a runtime-only
      multi-click harness and showed why the earlier click evidence was
      misleading: with no sane `HOME`, Chromium rendered repeated
      `Profile error occurred` dialogs, created no `/root/.config` profile,
      and made no fixture HTTP request. Runtime diagnostic
      `chromium-minflags-home-root-first-run-20260615a` set only
      `HOME=/root` in the shell environment; Chromium then created the normal
      `/root/.config/google-chrome-for-testing` profile tree and the profile
      modals disappeared. Treat the profile-error modals as launcher/session
      environment evidence, not a kernel ABI failure.
- [ ] Corrected first-run coordinate/timing follow-up:
      `chromium-minflags-home-root-first-run-settle-20260615a` used the
      virtio-tablet 0..32767 coordinate space for the "Stay signed out" button
      and a longer post-dismiss settle. It dismissed first-run, focused the URL
      bar, typed `http://10.0.2.2:28213/input-smoke.html`, and the host fixture
      saw `GET /input-smoke.html` HTTP 200. The run then failed at
      `capture-after-enter-timeout`: QEMU monitor screenshot also reported
      `Error: no surface`, and the guest shell did not answer the `fbstat`
      capture. This narrows the remaining minimal-flag evidence gap to
      post-fetch render/capture or shell liveness after navigation, not DNS,
      first-run coordinates, profile creation, or network connect.
- [ ] `FB_GPU_SCANOUT_READ` timeout check:
      `chromium-minflags-scanoutread-diag-20260615a` enabled the existing
      scanout-read diagnostic cmdline and proved four guest framebuffer reads
      completed in roughly 70-102 ms. The apparent `capture-after-page-key`
      timeout was an expect artifact: Chromium stderr interleaved into the
      `fb_ppm_current` line even though the guest PPM was written and manually
      extracted. The final frame showed Chromium's first-run UI, so do not
      treat this sample as page-render evidence; use it only to rule out a
      kernel scanout-read deadlock for that timeout.
- [ ] Upstream NSS root module staging:
      repeated Chromium logs showed
      `Root Certs, loaded==false: libnssckbi.so: cannot open shared object file`.
      The imported host Chromium payload already carried NSS libraries but was
      missing the upstream root module. Staged host
      `/usr/lib/x86_64-linux-gnu/libnssckbi.so` as
      `rootfs-overlay/opt/host-gui/wayland-chromium/lib/libnssckbi.so` and
      refreshed a narrow proof image
      `build-x86_64/chromium-normal-desktop-proof/nssckbi-proof.fs.img`.
      `chromium-nssckbi-default-key-20260615a` fetched the fixture path and no
      longer emitted the old `libnssckbi.so` / `Root Certs` error. The fixture
      path returned HTTP 404 because the default server directory did not carry
      `key.html`, and the run still timed out at post-enter capture, so this is
      NSS-library closure only, not a full HTTPS/webpage proof.
- [ ] HTTPS socket evidence with staged NSS:
      `chromium-sockettrace-https-example-skipearly-20260615a` used typed
      navigation to `https://example.com/` with early framebuffer captures
      skipped. Same-boot evidence after the typed URL shows `NetworkService`
      DNS connects to `10.0.2.3`, nonblocking TCP connect to
      `142.250.69.142:443`, `getsockopt(SO_ERROR)=0`, a 1774-byte TLS
      ClientHello send, multiple `rcvplus` response callbacks, and follow-up
      encrypted writes. No `libnssckbi.so` / `Root Certs` / TLS error rows
      appeared. The run still failed the expect `enter` matcher because Unix
      IPC trace rows interleaved with keyinject output before the trace split
      above; treat it as network/TLS-byte evidence only, not visual render
      proof. A quieter repeat
      `chromium-inettrace-https-example-skipearly-20260615a` stalled before
      the desktop-ready marker and was killed; do not use it as Chromium
      navigation evidence.
- [ ] Broad trace repeat
      `chromium-minflags-home-root-postfetch-trace-20260615a` enabled
      thread/DRM/epoll/Unix/slow-syscall tracing but timed out at
      `capture-blank-timeout` before URL entry because the opt-in trace stream
      again confused the interactive expect matcher. Preserve its same-boot
      startup evidence only: Chromium's wrapper-to-Chrome `execve` took about
      17.5 seconds with transient `D` samples, then zygote/Crashpad process
      creation proceeded. Do not count that run as post-fetch render evidence.
- [ ] Current narrowed lead: do not reopen DNS, desktop health, or simple
      typed-navigation/input on the minimal fixture. Continue from removing the
      remaining baked-in launcher workarounds through kernel ABI closure,
      while skipping sandbox by request. The kernel now handles the local
      fixture with all old multiprocess/GPU/Crashpad workaround flags removed.
      The remaining non-sandbox automatic-launch gaps are: a sane session home
      for the default profile path; broader HTTPS validation now that
      `libnssckbi.so` is staged; and the post-fetch render/capture or
      shell-liveness timeout after minimal-flag first-run dismissal. If the
      user-visible unresponsive window recurs, derive roles from same-run
      argv/thread/fd/surface evidence and inspect Wayland linux-dmabuf/DRM fd
      passing, sync-file/dma-buf fence readiness, GPU process lifecycle,
      compositor IPC, `FB_GPU_SCANOUT_READ` / `TRANSFER_FROM_HOST_3D` waits,
      browser UI event-loop state, and pointer/focus delivery to Chromium
      browser UI.

Historical negative evidence:

- [ ] Clean `chromium-x11-clean-refresh-signal` run did not hit the host
      fixture server and did not produce `human-button:PASS`.
- [ ] Clean screenshot shows the requested URL selected in the omnibox, tab and
      window title still `Untitled`, and a blank content area.
- [ ] Clean log repeats Chromium `child_thread_impl.cc:903` "Terminating
      current process after 15 seconds with no connection" and browser
      `Network service crashed or was terminated, restarting service` evidence.
- [ ] This supports the manual observation that one refresh can be required:
      the initial navigation is visible to the browser UI, but the first
      NetworkService/Mojo readiness path can fail before the HTTP request is
      issued.
- [ ] Latest lighter IPC trace reached the fixture TCP connect but did not send
      the HTTP GET before timeout.
- [ ] Guest evidence logging began at `Chromium evidence begin reason=timer-3`
      but never reached the expected evidence-end marker before QEMU timeout.
- [ ] Chromium also performed external HTTPS/background traffic afterward.
- [ ] The trace also shows `/bin/xdg-settings` and `/usr/bin/xdg-settings`
      missing with child exit status 127. Treat this as a userland helper
      absence, not a kernel ABI lead, unless a later trace ties it directly to
      navigation/render failure.

Next Chromium work should validate the same clean path from the user's normal
desktop/launcher workflow and then broaden from the local fixture to ordinary
pages. The latest live manual normal D-Bus key-page control reaches fetch,
mapped content, and keyboard-driven title change; if the user-visible Chrome
window still feels unresponsive, continue from the exact post-render workflow,
pointer/focus, compositor/Wayland dispatch, browser/render IPC state, D-Bus
AF_UNIX behavior, Mojo readiness, epoll/poll revents, AF_UNIX fd passing,
procfs/fdtable, futex, and X11/Xwayland IPC. If the old
"refresh once" symptom reappears before the first HTTP request, continue from
the first NetworkService/Mojo readiness failure and initial navigation
scheduling. Do not reopen DNS, static PIDs, D-Bus presence alone, desktop
launchers, or host-program packaging unless a new focused trace proves that
layer is involved.

## Host Chromium References

Use these host logs as semantic ABI references only. Numeric PIDs/TIDs inside
them are disposable coordinates.

- [ ] `/home/es/host-chromium-launch.log`
- [ ] `/home/es/host-chromium-launch.1.log`
- [ ] `/home/es/host-chromium-launch.2.log`
- [ ] Treat `/home/es/host-chromium-launch.2.log` as the heavier reference
      because it opens Chromium first and then opens YouTube.
- [ ] Host trace double-check: `.2` is not a full child-process trace; it has
      browser-process launch/X11/D-Bus/SCM evidence but no NetworkService,
      fixture URL, or `GET /human-button` evidence. Do not use it as the
      oracle for Chromium child Mojo/NetworkService timeout behavior.
- [ ] Compare host reference ABI shape: AF_UNIX `SOCK_SEQPACKET`.
- [ ] Compare host reference ABI shape: `SO_PASSCRED`.
- [ ] Compare host reference ABI shape: `SCM_CREDENTIALS`.
- [ ] Compare host reference ABI shape: `SCM_RIGHTS`.
- [ ] Compare host reference ABI shape: abstract then pathname X11 socket
      probing.
- [ ] Compare host reference ABI shape: D-Bus Unix-fd negotiation.
- [ ] Compare host reference ABI shape: procfs pseudo-file behavior.
- [ ] Compare host reference ABI shape: epoll/poll wakeups.
- [ ] Compare host reference ABI shape: large X11/DRI traffic.

## X11 / Host GUI Proofs

Keep these as focused ABI probes, not product goals:

- [ ] X11 ABI smoke proof directory: `build-x86_64/host-x11-abi-smoke-proof/`.
- [ ] X11 DRI3/Present proof directory:
      `build-x86_64/host-x11-dri3-present-smoke-proof/`.
- [ ] X11 IDLE + DRI3 teardown proof directory:
      `build-x86_64/x11-idle-dri3-teardown-proof/`.
- [ ] X11 SHM proof directory: `build-x86_64/host-x11-shm-smoke-proof/`.
- [ ] IDLE/Tk X11 proof directory: `build-x86_64/host-idle-x11-proof/`.
- [ ] Host Python REPL proof directory: `build-x86_64/host-python-repl-proof/`.
- [ ] Host GTK smoke proof directory: `build-x86_64/host-gtk-smoke-proof/`.
- [ ] Host WLEGL smoke proof directory: `build-x86_64/host-wlegl-smoke-proof/`.

## Active Open Work Queue

- [ ] Latest minimal-flags first-run/input-smoke evidence:
      `chromium-minflags-first-run-freeze-screendump-20260615a` used the
      fresh NSS source image, `HOME=/root`, `--no-sandbox`, and corrected
      `/dev/mouse` absolute coordinates scaled to 0..65535. The host fixture
      saw `GET /input-smoke.html` HTTP 200 plus `/favicon.ico` 404, and
      `build-x86_64/chromium-normal-desktop-proof/chromium-minflags-first-run-freeze-screendump-20260615a-after-page-key.png`
      shows the rendered page with title `typed:a` and the typed `a` visible.
      Earlier first-run failures using coordinates around `13900,26130` or
      `23100,22200` were harness-coordinate mistakes, not proof of broken
      Chromium input; those coordinates land on the wrong screen area because
      the guest `/dev/mouse` ABI reports absolute positions as 0..65535.
- [ ] Remaining negative evidence from the same focused area: first-run
      dismissal remains timing-sensitive. The `...-wait-20260615a` and
      `...input-smoke-65535-20260615a` repeats failed at
      `capture-after-first-run-timeout` with no echoed `fbstat` after the
      wait, while `chromium-minflags-first-run-liveness-20260615a` stayed
      alive when explicit shell liveness commands bracketed the wait. Continue
      from scheduler/serial-shell liveness, Chromium profile/cache file
      creation, and post-first-run browser IPC/readiness; do not reopen DNS,
      static PIDs, desktop launchers, or the kernel input path without new
      evidence.
- [ ] New profile-store clue: successful and failed first-run repeats sometimes
      log Chromium profile/cache write errors such as
      `/root/.config/google-chrome-for-testing/Default/GCM Store/000001.dbtmp:
      Unable to create writable file`. Reduce this as a filesystem/VFS ABI
      check before treating the intermittent post-first-run timeout as a GPU or
      network problem.
- [ ] Keep the AF_UNIX stream SCM split and validate it with focused X11/Wayland
      fd-passing proof after further IPC edits.
- [ ] Continue Chromium from the user's manual normal-launch unresponsive
      Chrome window if it reproduces outside the minimal key-page control; the
      desktop itself remains healthy.
- [ ] Fresh plain-launch delayed-freeze repro after the user clarified they
      opened plain `/bin/wayland-chromium` before seeing the freeze:
      `chromium-longidle-input-20260615a` launched plain Chromium with no URL
      argument, typed `http://10.0.2.2:28213/input-smoke.html`, and the host
      fixture saw `GET /input-smoke.html` HTTP 200 plus `/favicon.ico` 404.
      After a 120-second post-navigation idle, the next guest-shell liveness
      probe timed out at `after-enter-liveness-timeout`; no QEMU process was
      left running. Artifacts:
      `build-x86_64/chromium-normal-desktop-proof/chromium-longidle-input-20260615a.run.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-longidle-input-20260615a.host-http.log`,
      `build-x86_64/chromium-normal-desktop-proof/chromium-longidle-input-20260615a-blank.png`,
      and
      `build-x86_64/chromium-normal-desktop-proof/chromium-longidle-input-20260615a-typed.png`.
      A 60-second sibling,
      `chromium-longidle60-input-20260615a`, kept guest-shell liveness and
      accepted later key injection, but made no host HTTP request and remained
      visually on `about:blank`, so treat it only as a liveness/focus control,
      not a render control.
- [ ] Delayed-freeze diagnosis narrowed on 2026-06-15:
      `chromium-longidle-diagnose-20260615b` and `...20260615c` reproduced
      the post-navigation shell timeout after a host-served
      `GET /input-smoke.html` HTTP 200. Raw serial `^P` process dumps showed
      the Chromium browser TGID alive while the browser main thread transitioned
      to uninterruptible `D`; the desktop/Weston and lwIP threads remained
      present, so this is not a whole-guest CPU death, DNS failure, or initial
      desktop launch issue.
- [ ] Follow-up with temporary serial debug chords
      `chromium-longidle-diagnose-20260615d` added `^B` blocked backtraces and
      `^O` channel-queue dump. The channel queue was empty at the failure
      point, while the browser main thread backtrace was:
      `rwsem_acquire_write -> vm_wlock -> vm_madvise -> sys_madvise`.
      Most other Chromium threads were ordinary futex/epoll sleepers. Continue
      from VM address-space `rw_lock` ownership/starvation/leak around
      `madvise()`, `mmap()` eager validation, `munmap()`, page-fault/copyin/
      copyout read locks, and rwsem reader/writer wake semantics before
      revisiting browser/render IPC.
- [ ] Temporary diagnostic helpers now exist in the kernel console path:
      serial `^P` dumps process state, `^B` dumps blocked-thread backtraces,
      and `^O` dumps channel queues. They are quiet unless explicitly injected
      and were built with `cmake --build build-x86_64 --target kernel -j2`.
- [ ] Investigate browser/render IPC.
- [ ] Investigate Mojo readiness.
- [ ] Investigate epoll/poll revents.
- [ ] Investigate fd passing.
- [ ] Investigate procfs and fdtable surfaces.
- [ ] Investigate futex behavior only after excluding VM rwsem starvation/leak:
      the current backtrace shows many normal futex sleepers, but the
      unresponsive browser main thread is blocked before entering madvise
      memory work because it cannot acquire the VM write lock.
- [ ] Current follow-up for slow Chromium input/new-tab response: user VM
      address spaces now initialize their VM rwsem with writer priority so
      queued `madvise()`/`mmap()`/`munmap()` writers cannot be starved by a
      steady stream of short VM readers. `cmake --build build-x86_64 --target
      kernel -j2` passed. Focused serial/monitor validation
      `chromium-newtab-serial-wprio-20260616a` launched plain
      `/bin/wayland-chromium`, injected Ctrl+T through the QEMU monitor,
      returned both `PRE_NEWTAB_ALIVE` and `POST_NEWTAB_ALIVE`, had empty
      channel queues, and did not show the old
      `vm_wlock -> vm_madvise -> sys_madvise` writer-starvation stack. The
      remaining sampled new-tab stalls are read-side VM acquisitions
      (`vm_copyin`/rseq-adjacent user-memory copy paths) waiting behind VM
      writers during tab creation, so continue with VM rwsem fairness/hold-time
      instrumentation before broad futex/IPC changes.
- [ ] Investigate D-Bus AF_UNIX behavior.
- [ ] Investigate X11/Xwayland IPC.
- [ ] Reduce the minimal-flag post-fetch `fbstat`/shell-liveness timeout after
      the host has already served `GET /input-smoke.html`; avoid broad
      thread-dump tracing in the interactive harness because it can invalidate
      expect timing before URL entry.
- [ ] Decide how to handle session `HOME` for direct Chromium/profile startup
      without reintroducing Chromium command-line workarounds; current evidence
      points to launcher/session environment, not kernel ABI.
- [ ] Validate ordinary HTTPS pages with the staged upstream
      `libnssckbi.so`; the old missing-root-module error is closed and
      typed-navigation socket/TLS bytes now have same-run evidence, but local
      HTTP rendering remains the stronger visual kernel ABI control until an
      ordinary HTTPS visual/page-title proof survives the capture path.
- [ ] Use the narrower `rootfs-refresh` path for GUI ABI loops when only the
      rootfs image copy needs refreshing.
- [ ] Current YouTube 720p/cursor-motion evidence: native Wayland Chromium is
      the better current path than X11. `chromium-youtube-wayland-quicoff-long`
      reached a live YouTube watch page with video advancing through both
      steady and cursor-motion capture windows, using the default
      Wayland backend plus `--disable-quic`; adjacent full-frame deltas stayed
      nonzero in every sampled pair (`steady` about 177k-181k changed pixels,
      `cursor` about 174k-219k). The prior X11 diagnostic still showed
      Chromium GPU-process `GetVSyncParametersIfAvailable()` failures, so keep
      X11 presentation timing separate from the ordinary desktop Chromium path.
- [ ] Launcher optimization staged for the ordinary desktop Chromium path:
      `/bin/wayland-chromium` defaults to native Wayland unless explicitly
      overridden, sets `LIBVA_DRIVERS_PATH=/lib/dri` and
      `LIBVA_DRIVER_NAME=virtio_gpu`, enables Chromium's
      `AcceleratedVideoDecodeLinuxGL`/VAAPI ignore-driver-check features, and
      disables QUIC to avoid YouTube taking the UDP/QUIC path through QEMU user
      networking/lwIP while video is being profiled.
- [ ] Fullscreen YouTube validation remains open: the current automated probe
      can show watch-page video progress under cursor motion, but the
      fullscreen trigger is not deterministic yet (`keyinject` delivered `f`
      and direct control clicks landed, but the captured frame remained the
      normal watch page or hit a transient player/page state). Keep this as a
      harness gap, not proof that fullscreen playback is solved.
- [ ] Commit finished closed work in logical chunks when requested.
- [ ] Commit inner submodules first, then super repo pointer bumps.
- [ ] Ask before pushing.

## Closed Historical Milestones

- [ ] DRM ABI phases 0-6 are closed.
- [ ] `drmabitest` is closed for this target.
- [ ] Mesa/GBM/libdrm convergence is closed for this target.
- [ ] KMS is closed for this target.
- [ ] PRIME/dma-buf is closed for this target.
- [ ] syncobj/sync_file is closed for this target.
- [ ] vblank is closed for this target.
- [ ] cursor is closed for this target.
- [ ] virtio-gpu UAPI convergence is closed for this target.
- [ ] OOM RSS attribution is closed.
- [ ] Typed-URL harness and `/dev/kbd` write path are closed.
- [ ] `kcmp` is closed.
- [ ] Old housekeeping queue items are closed.
- [ ] Local C-client unified titlebars are closed.
- [ ] NetSurf is skipped.
- [ ] Host GUI representative proofs are closed for the current imported-app
      smoke set.
- [ ] Reopen closed host GUI proof work only for a concrete Linux ABI mismatch.

## Out Of Scope

- [ ] Chromium sandbox/namespace/container feature work is skipped by current
      user request.
- [ ] Hyper-V DXG/GPU-P native-present work is separate.
- [ ] Nouveau/DDA real-hardware support is separate.
- [ ] RISC-V graphics is separate.
- [ ] Full GNOME/Plasma-style desktop services are separate unless reduced to a
      concrete kernel/user ABI surface.
