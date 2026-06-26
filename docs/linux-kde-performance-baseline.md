# Linux KDE KVM/virgl Performance Baseline

Date: 2026-06-21

This records the Linux KDE Plasma control run and the current xv6 KDE result
under KVM with virgl. KDE/Qt/KWin/Plasma sources remain upstream-clean; xv6
changes are in staging, launch guardrails, probes, and rootfs construction.

## Harnesses

- Linux control: `scripts/gpu/linux-kde-virgl-baseline.sh`
- xv6 KDE smoke: `scripts/gpu/kde-plasma-desktop-smoke.expect`
- Linux artifact: `build-x86_64/linux-kde-virgl-baseline/20260621-123954`
- xv6 artifact: `build-x86_64/kde-plasma-desktop-smoke-history/20260626-061223-x11-glx-fps-timing-pass`

The Linux control boots Alpine 3.23.4 with QEMU 9.0.2, KVM, `virtio-vga-gl`,
`gtk,gl=on`, 8192 MiB RAM, and installs KDE/Plasma, KWin, Xwayland, Mesa, and
`glxgears` from upstream packages.

## Package Inventory

Dependency-ordered control inventory used for this baseline:

1. Seat/system services: `eudev`, `dbus`, `dbus-x11`, `elogind`, `polkit`,
   `polkit-elogind`, `polkit-kde-agent-1`, `udisks2`
2. Graphics/runtime: `libinput`, `hwdata`, `font-dejavu`, `xwayland`,
   `mesa-dri-gallium`, `mesa-egl`, `mesa-gbm`, `mesa-gl`, `mesa-gles`,
   `mesa-utils`, `mesa-demos`
3. KDE shell: `kwin`, `plasma-workspace`, `plasma-desktop`, `kscreen`,
   `kactivitymanagerd`, `kde-cli-tools`, `kded`, `plasma-integration`,
   `qqc2-desktop-style`

## Results

| Metric | Linux KVM+virgl | xv6 KVM+virgl |
| --- | ---: | ---: |
| Wayland socket after KWin start | 0.21 s | Xwayland visible by 7.22 s guest uptime |
| Plasma visible/running | 0.22 s | 8.17 s guest uptime |
| Xwayland visible | 0.43 s | 7.22 s guest uptime |
| Renderer | `virgl (D3D12 (Intel(R) UHD Graphics))` | `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))` |
| GL exposed to KWin | GL 4.1 compat via Linux userspace | OpenGL ES 3.1 via xv6 Mesa path |
| X11/GL smoke | `glxgears` 55.638 FPS | GLX context/draw/FPS pass with `kde_xwayland_glamor=auto` mapped to effective `-glamor es`; 31 frames in 5.362771 seconds, 5.781 FPS |
| KWin/desktop visual proof | QEMU screendump unavailable (`Error: no surface`) | default smoke preserves host screenshots/input diff; raw KWin readback warns `low-color-detail` in the current default artifact |
| Input proof | not captured | 347353 changed pixels; Konsole launched |

QEMU trace counts:

| Event | Linux | xv6 |
| --- | ---: | ---: |
| `virtio_gpu_cmd_ctx_submit` | 2427 | 207 |
| `virtio_gpu_cmd_set_scanout` | 384 | 53 |
| `virtio_gpu_cmd_res_flush` | 607 | 53 |
| `virtio_gpu_fence_ctrl` | 2431 | 207 |
| `virtio_gpu_fence_resp` | 2431 | 207 |
| `virtio_gpu_cmd_res_create_3d` | 54 | 275 |
| `virtio_gpu_cmd_res_xfer_toh_3d` | 0 | 2 |

## Current Fix

`/usr/bin/Xwayland` in the final rootfs now resolves to the xv6 wrapper at
`/bin/Xwayland`, preventing generated KDE overlays from bypassing the wrapper.
The wrapper keeps XKB data explicit, defaults `XV6_XWAYLAND_GLAMOR` to `off`
for the stable desktop smoke, and maps focused `XV6_XWAYLAND_GLAMOR=auto`
runs to effective `-glamor es`. Explicit `off`, `gl`, and `es` remain
available for focused tests.

This removes the noisy Xwayland GLAMOR startup failure from the default KDE
desktop path and restores the nonblack Plasma desktop/input proof. The earlier
unqualified automatic GLAMOR path reported:

```text
Supported GL version is not sufficient (required 21, found 0)
glGetString() returned NULL, your GL is broken
XWAYLAND: Disabling GLAMOR support
EGL setup failed, disabling glamor
```

For performance parity, the xv6 virgl/KDE path exposes enough OpenGL ES for
KWin compositing, but the wrapper must select the Xwayland ES GLAMOR path
explicitly before X11 GLX becomes usable.
The focused auto/effective-ES reducer passes:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260625-231025-x11-egl-auto-effective-es-pass/
Xwayland KDE wrapper: ... glamor=auto effective_glamor=es ... enable_glx=1
host-x11-egl-smoke: phase=gl_strings status=PASS api=glx vendor=Mesa renderer=virgl ... gl_version=4.2 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2
host-x11-egl-smoke: phase=draw status=PASS frame=1 mode=glx-probe color=0
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-egl session_probe=PASS
```

Latest FPS reducer outcome:

```sh
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1 kde_smoke_require_chromium=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-061223-x11-glx-fps-timing-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_timing status=PASS result_status=PASS frames=31 event_total_ms=2.962 draw_total_ms=2979.471 swap_total_ms=2140.386 final_xsync_ms=237.682 max_swap_ms=94.047 max_draw_ms=411.108 max_event_ms=1.112 avg_swap_ms=69.045
host-x11-egl-smoke: phase=glx_fps_result status=PASS mode=glx-fps reason=complete renderer="virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))" vendor="Mesa" gl_version="4.2 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2" direct_available=1 direct=1 frames=31 elapsed_seconds=5.362771 fps=5.781 target_seconds=5.000 max_frames=300
Xwayland KDE wrapper: ... glamor=auto effective_glamor=es ... enable_glx=1
chrome-drm-detail: virtgpu-context-first-submit-execbuffer ... proc=Xwayland.real ... capset=2 ... ret=0
chrome-drm-ioctl: exit ... proc=Xwayland.real ... cmd=DRM_IOCTL_SYNCOBJ_EVENTFD ... ret=-22
```

This verifies the harness improvement beyond startup: the session-launched
reducer now reaches GLX, draws, swaps, records timing, and exits cleanly. The
timing evidence makes event drain an unlikely explanation for the remaining
gap; the low frame rate is split across pre-swap GL work and
`glXSwapBuffers`. The next reducer should inspect Xwayland/DRI3 Present and
syncobj notification semantics, including the current `SYNCOBJ_EVENTFD`
`EINVAL`, before changing kernel behavior.

## Validation

Passing xv6 command:

```sh
timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Key passing lines:

```text
Xwayland KDE wrapper: EGL_PLATFORM=wayland GALLIUM_DRIVER=virgl MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu glamor=off
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... konsole=1 ... chromium=1 ... status=PASS
kde_kwin_screenshot_probe result=FAIL detail=low-color-detail
KDE_SMOKE_AGENT_DONE status=PASS
KDE-PLASMA-DESKTOP-SMOKE-DONE
```

Rejected Linux capture attempts before the baseline:

- `20260621-122604`: Alpine live root ran out of space during package install.
- `20260621-122854`: Expect/Tcl expanded guest shell variables while generating the script.
- `20260621-123132`: another host-side substitution issue in the guest script.
- `20260621-123625` and `20260621-123831`: partial metric capture before the completion marker was made reliable.

## Next Gap

To match the Linux baseline more closely, explain or reduce the
Xwayland/DRI3/Present timing gap. The current xv6 run proves direct GLX/virgl
startup and finite FPS measurement, but the QEMU submit/flush rate and measured
FPS remain well below Linux.
