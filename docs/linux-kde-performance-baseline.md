# Linux KDE KVM/virgl Performance Baseline

Date: 2026-06-21

This records the Linux KDE Plasma control run and the current xv6 KDE result
under KVM with virgl. KDE/Qt/KWin/Plasma sources remain upstream-clean; xv6
changes are in staging, launch guardrails, probes, and rootfs construction.

## Harnesses

- Linux control: `scripts/gpu/linux-kde-virgl-baseline.sh`
- xv6 KDE smoke: `scripts/gpu/kde-plasma-desktop-smoke.expect`
- Linux artifact: `build-x86_64/linux-kde-virgl-baseline/20260621-123954`
- xv6 artifact: `build-x86_64/kde-plasma-desktop-smoke`

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
| X11/GL smoke | `glxgears` 55.638 FPS | Xwayland GLAMOR guarded off |
| KWin screenshot | QEMU screendump unavailable (`Error: no surface`) | nonblack=1024000, colorful=1022059, unique RGB 254/253/252 |
| Input proof | not captured | 347353 changed pixels; Konsole launched |

QEMU trace counts:

| Event | Linux | xv6 |
| --- | ---: | ---: |
| `virtio_gpu_cmd_ctx_submit` | 2427 | 110 |
| `virtio_gpu_cmd_set_scanout` | 384 | 40 |
| `virtio_gpu_cmd_res_flush` | 607 | 40 |
| `virtio_gpu_fence_ctrl` | 2431 | 110 |
| `virtio_gpu_fence_resp` | 2431 | 110 |
| `virtio_gpu_cmd_res_create_3d` | 54 | 353 |
| `virtio_gpu_cmd_res_xfer_toh_3d` | 0 | 2 |

## Current Fix

`/usr/bin/Xwayland` in the final rootfs now resolves to the xv6 wrapper at
`/bin/Xwayland`, preventing generated KDE overlays from bypassing the wrapper.
The wrapper keeps XKB data explicit and defaults `XV6_XWAYLAND_GLAMOR` to
`off`; `XV6_XWAYLAND_GLAMOR=auto`, `gl`, or `es` can be used for focused
acceleration tests.

This removes the noisy Xwayland GLAMOR startup failure from the default KDE
desktop path and restores the nonblack Plasma desktop/input proof. When tested
with automatic GLAMOR, Xwayland still reports:

```text
Supported GL version is not sufficient (required 21, found 0)
glGetString() returned NULL, your GL is broken
XWAYLAND: Disabling GLAMOR support
EGL setup failed, disabling glamor
```

That is the remaining performance gap versus Linux: the xv6 virgl/KDE path
currently exposes enough OpenGL ES for KWin compositing, but not the GL 2.1+
path Xwayland GLAMOR expects for accelerated X11 clients.

## Validation

Passing xv6 command:

```sh
timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Key passing lines:

```text
Xwayland KDE wrapper: EGL_PLATFORM=wayland GALLIUM_DRIVER=virgl MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu glamor=off
kde_kwin_screenshot_probe result=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... konsole=1 ... status=PASS
KDE-PLASMA-DESKTOP-SMOKE-DONE
```

Rejected Linux capture attempts before the baseline:

- `20260621-122604`: Alpine live root ran out of space during package install.
- `20260621-122854`: Expect/Tcl expanded guest shell variables while generating the script.
- `20260621-123132`: another host-side substitution issue in the guest script.
- `20260621-123625` and `20260621-123831`: partial metric capture before the completion marker was made reliable.

## Next Gap

To match the Linux baseline more closely, the next reducer should target
Xwayland GLAMOR/GLX under xv6 with `XV6_XWAYLAND_GLAMOR=auto` and compare why
`glGetString(GL_VERSION)` is null there while KWin can create an OpenGL ES 3.1
virgl context. Likely surfaces: Mesa GL versus GLES loader selection, EGL/GLX
context creation on Xwayland's Wayland platform, and the DRM render-node ioctl
set used by that path.
