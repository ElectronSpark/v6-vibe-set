# Linux DRM / GUI ABI Evidence Archive, 2026-06-27 To 2026-06-30

This archive preserves the detailed evidence that was previously embedded in
the archived `docs/archive/plan-consolidation-20260701/linux-drm-abi-compat-plan.md`.
The active plan in `docs/active-work-plan.md` should stay compact and current;
this file keeps the Linux VM controls, WSL2 host facts, Xwayland/GLX reducer
chronology, KDE performance notes, and Chromium stress-probe evidence for audit
and handoff.

### Linux VM Virgl / GBM Control

Plan-feasibility proof artifact:

```text
build-x86_64/linux-virgl-gbm-proof/20260627T145913Z/
LINUX-VM-VIRGL-GBM-FEASIBILITY: PASS
device_nodes=card0,renderD128
eglinfo_gbm_exit=0
eglinfo_gbm_renderer=virgl (D3D12 (Intel(R) UHD Graphics))
eglinfo_gbm_gles=OpenGL ES 3.0 Mesa 25.2.7
eglinfo_surfaceless_exit=0
eglinfo_surfaceless_renderer=virgl (D3D12 (Intel(R) UHD Graphics))
drm_dmesg_features=+virgl +edid -resource_blob -host_visible -context_init
virtio_gpu_trace_ctx_submit=12
virtio_gpu_trace_res_flush=65
```

Interpretation: a Linux KVM guest using QEMU `virtio-vga-gl` exposes
`/dev/dri/card0` and `/dev/dri/renderD128`, and Mesa can initialize both GBM
and surfaceless EGL against virgl. This validates the plan split: WSL host GBM
absence is host-specific, while the Linux VM control and the xv6 guest target
remain native DRM/GBM render-node environments.

Chromium-shape Linux control preflight:

```text
scripts/gpu/linux-virgl-gbm-chromium-shape.sh
```

Before running the Chromium-shaped Linux control, the probe status marker was
fixed so `LINUX_GBM_SHAPE_RESULT` reports `status=COMPLETE` with explicit
`pass=`/`fail=` context counts. This avoids treating "the reducer ran" as "all
Chromium-shaped pbuffer/window profiles passed". The verdict remains a harness
completion gate and records each `phase=chromium_config` and
`phase=context_attempt` line for the actual Linux-vs-xv6 comparison.

Chromium-shape Linux control proof:

```text
build-x86_64/linux-virgl-gbm-chromium-shape/20260627T193540Z/
LINUX-VM-GBM-CHROMIUM-SHAPE: PASS
expect_exit=0
shape_result=LINUX_GBM_SHAPE_RESULT status=COMPLETE attempts=10 pass=6 fail=4
config_summary=linux-gbm-shape: phase=config_summary status=PASS total=200 es2_renderable=200 es3_renderable=200 pbuffer=0 window=200 rgb888=50 exact_pbuffer_rgb=0 es2_pbuffer_rgb=0 es3_window_rgb=50
chromium_offscreen_pbuffer_es3 count=0 reason=no_config
chromium_offscreen_pbuffer_es2 count=0 reason=no_config
chromium_native_window_pbuffer_es3 count=0 reason=no_config
chromium_native_window_pbuffer_es2 count=0 reason=no_config
chromium_window_only_gbm_surface_es3 count=25 status=PASS
chromium_window_only_gbm_surface_es2 count=25 status=PASS
chromium_window_only_surfaceless_es3 count=25 status=PASS
chromium_window_only_surfaceless_es2 count=25 status=PASS
chromium_surfaceless_es3 count=25 status=PASS
chromium_surfaceless_es2 count=25 status=PASS
virtio_gpu_trace_ctx_submit=12
virtio_gpu_trace_res_flush=111
```

Interpretation: Linux KVM+virgl also exposes zero GBM pbuffer and
window+pbuffer configs for the Chromium-shaped RGB888+alpha profile, while
window-only and surfaceless ES3/ES2 contexts pass. The raw config counts are
not a kernel ABI verdict unless the Mesa identity also matches: this Linux
control used Alpine Mesa 25.2.7, while the xv6 GBM preprobe uses the repo Mesa
port (`26.2.0-devel`, `ports/mesa/src` at `5ca5a64d5546` in the current
build). Historical xv6 runs reported `40` Chromium-window matches and current
runs report `50`; those count changes track Mesa/runtime identity, while the
pass/fail profile shape remains stable. The control harness now records
`linux_mesa_packages`, `linux_gl_identity`, `xv6_mesa_reference`, and
`config_count_comparable`, which must be `yes` before raw count deltas can
drive a kernel fix. Do not treat Chromium's current GBM pbuffer failure as
evidence for an xv6 kernel DRM behavior patch. The next Chromium reducer should
keep upstream Chromium/Mesa clean and test whether an xv6-owned launch/runtime
policy can steer Chromium onto a surfaceless or window-capable path, then
measure playback/capture/execbuffer metrics there.

Same-Chromium Ubuntu Linux VM control:

```text
build-x86_64/linux-chromium-vm-control/20260628T151915Z-ubuntu-kwin-wayland-abs-retry/
status=PASS
virtio_gpu_cmd_ctx_submit 18
virtio_gpu_cmd_res_flush 924
virtio_gpu_cmd_set_scanout 3
```

This first Ubuntu Noble VM control used the repo-staged Chrome-for-Testing
binary (`150.0.7871.24`) and the local `perf-video.html` fixture. The guest had
`/dev/dri/card0` and `/dev/dri/renderD128`, and GBM/surfaceless EGL reported
`virgl (D3D12 (Intel(R) UHD Graphics))`. KWin's DRM backend failed in this
cloud-init/root session with:

```text
kwin_wayland_drm: failed to open drm device at "/dev/dri/card0"
kwin_wayland_drm: No suitable DRM devices have been found
```

With no durable Wayland compositor, both Chrome launches exited with the Linux
Wayland platform error `Failed to connect to Wayland display: Connection
refused (111)`. The passive preload still showed loader-safe activity and
render-node opens by the browser role, but this run is not a healthy KDE/DRM
desktop comparator.

Second-pass same-Chromium control:

```text
build-x86_64/linux-chromium-vm-control/20260628T152509Z-ubuntu-kwin-virtual-chromium/
status=PASS
virtio_gpu_cmd_ctx_create 5
virtio_gpu_cmd_ctx_submit 19
virtio_gpu_cmd_res_create_3d 7
virtio_gpu_cmd_res_flush 449
virtio_gpu_cmd_set_scanout 3
virtio_gpu_fence_ctrl 8
virtio_gpu_fence_resp 8
```

This reused the installed Ubuntu disk, captured the KWin DRM failure with
`strace`, then started `kwin_wayland --virtual --xwayland`. The virtual KWin
session reported:

```text
OpenGL renderer string: virgl (D3D12 (Intel(R) UHD Graphics))
OpenGL version string: OpenGL ES 3.0 Mesa 25.2.8-0ubuntu0.24.04.2
```

The same Chrome binary connected to Wayland and stayed alive until the harness
terminated it (`rc=143` for both no-preload and passive-preload runs). Sampled
roles were stable at `browser=6`, `zygote=2`, `gpu=0`, `renderer=0`. The
passive preload recorded `role=browser`, `use_gl=1`, `use_angle=1`,
`EGL_PLATFORM=gbm`, `OZONE_PLATFORM=wayland`, and successful
`/dev/dri/renderD128` opens with `DRM_IOCTL_VERSION` ioctls returning `0`.
Chrome stderr only showed Wayland protocol chatter in the captured tail, not
the earlier connection-refused error.

Interpretation: use the Alpine KDE baseline for healthy KWin DRM/KDE
comparison, and use the Ubuntu second-pass artifact as the same-glibc-Chromium
loader/process/render-node control. Do not infer an xv6 KWin DRM regression
from the Ubuntu cloud-init/root-seat failure. For the next xv6 Chromium-video
reducer, compare semantic roles and render-node activity: Linux same-Chromium
reaches browser+zygote with render-node opens but no sampled GPU-process role
under KWin virtual, while the xv6 trace-on run still needs a refreshed rootfs
and then focused role/`int3`/DRM evidence.

Additional same-Chromium Linux VM playback/control attempts:

```text
build-x86_64/linux-chromium-vm-control/20260628T154110Z-ubuntu-kwin-virtual-perf-video/
status=PASS
Google Chrome for Testing 150.0.7871.24
device_nodes=card0,renderD128
kwin_virtual_renderer=virgl (D3D12 (Intel(R) UHD Graphics))
kwin_virtual_gles=OpenGL ES 3.0 Mesa 25.2.8-0ubuntu0.24.04.2
sampled_roles=browser=6,gpu=0,renderer=0,zygote=2
perf_console_lines=0
virtio_gpu_cmd_ctx_submit 19
virtio_gpu_cmd_res_flush 353
virtio_gpu_cmd_set_scanout 3
virtio_gpu_fence_ctrl 8
virtio_gpu_fence_resp 8

build-x86_64/linux-chromium-vm-control/20260628T154449Z-ubuntu-kwin-virtual-cdp-perf/
status=PASS
cdp_probe=FAIL no DevTools page websocket; connection refused
sampled_roles=browser=5,gpu=0,renderer=0,zygote=2
kwin_virtual_xwayland=crash signal=11 address=0x8
virtio_gpu_cmd_ctx_submit 19
virtio_gpu_cmd_res_flush 238
virtio_gpu_cmd_set_scanout 3
virtio_gpu_fence_ctrl 8
virtio_gpu_fence_resp 8
```

Interpretation: these Ubuntu/KWin-virtual attempts are useful launch/process
and host-virgl controls, but they are not a clean video-FPS comparator. Chrome
stays alive under the same binary and fixture path, but the VM emits no
`PERF-VIDEO` console lines, DevTools never binds in the CDP attempt, and
KWin's virtual Xwayland crashes. Keep using the Alpine KDE baseline for healthy
desktop/GLX performance and the Ubuntu controls only for same-glibc Chromium
loader, role, and render-node shape.

Fresh same-Chromium Linux VM rerun after the xv6 GBM/sampler status fix:

```text
build-x86_64/linux-chromium-vm-control/20260628T155248Z-ubuntu-kwin-virtual-perf-video/
status=PASS
Google Chrome for Testing 150.0.7871.24
fixture=build-x86_64/linux-chromium-control/fixture/perf-video.html
fixture_asset=perf-1280x800-60fps.mp4 16069647 bytes
eglinfo_gbm_renderer=virgl (D3D12 (Intel(R) UHD Graphics))
eglinfo_surfaceless_renderer=virgl (D3D12 (Intel(R) UHD Graphics))
kwin_virtual_ready socket=1 alive=1
kwin_virtual_renderer=virgl (D3D12 (Intel(R) UHD Graphics))
sampled_roles=t2..t34 browser=6,gpu=0,renderer=0,zygote=2
chrome_exit=143 timeout-controlled
perf_console_lines=0
kwin_virtual_xwayland=crash signal=11 address=0x8
virtio_gpu_cmd_ctx_submit 19
virtio_gpu_cmd_res_flush 368
virtio_gpu_cmd_set_scanout 3
virtio_gpu_fence_ctrl 8
virtio_gpu_fence_resp 8
```

Interpretation: the Linux VM rerun proves the same Chromium binary can be
started inside a real Linux guest with virgl GBM/surfaceless acceleration and
KWin virtual compositing, but it again does not execute the perf-video page
script or expose stable GPU/renderer roles in this harness. Treat this as a
current Linux-guest launch/DRI/control proof, not as a playback-jitter oracle.
The xv6 Chromium-video run remains the only current lane with page-side video
metrics; compare Linux and xv6 here only on loader/process/DRI role shape unless
the Linux VM harness is extended to make DevTools or page console output work.

Visible same-Chromium Linux VM playback baselines:

```text
build-x86_64/linux-chromium-vm-control/20260628T163824Z-ubuntu-xorg-visible-http-perf-proof/
status=PASS media=PASS geometry=UNKNOWN screenshot=MISSING
Google Chrome for Testing 150.0.7871.24
Xorg visible socket=1 alive=1
GLX renderer=virgl (D3D12 (Intel(R) UHD Graphics))
GLX direct rendering=Yes
GLX Accelerated=yes
glxgears=2411 frames in 5.0 seconds = 482.165 FPS
process_counts=chrome_total:14,gpu:1,renderer:6,zygote:2
local_video=PASS measureFPS=31.933 measurePresented=479 measureDecoded=622 measureDropped=11 presented=717 decoded=960 dropped=22 rvfcFPS=44.577 rvfcAvgGapMs=22.465 rvfcMaxGapMs=183.3 playbackRate=0.994 currentTime=16
virtio_gpu_cmd_ctx_submit=8958
virtio_gpu_cmd_res_flush=4394
virtio_gpu_cmd_set_scanout=6
virtio_gpu_fence_ctrl=9042
virtio_gpu_fence_resp=9042
screenshot=missing
monitor_screendump_log=nc: /tmp/xv6-linux-chromium-vm-control/qemu-monitor-http-perf-4053222.sock: No such file or directory

build-x86_64/linux-chromium-vm-control/20260628T165123Z-ubuntu-xorg-visible-youtube-proof/
status=PASS media=PASS geometry=PASS screenshot=MISSING
geometry=1280x800
youtube_url=https://www.youtube.com/watch?v=dQw4w9WgXcQ&autoplay=1&mute=1&vq=hd720
Google Chrome for Testing 150.0.7871.24
Xorg geometry=current=1280x800 target=1280x800
GLX renderer=virgl (D3D12 (Intel(R) UHD Graphics))
GLX direct rendering=Yes
GLX Accelerated=yes
glxgears=1242 frames in 5.1 seconds = 242.327 FPS
process_counts=chrome_total:14,gpu:1,renderer:5,zygote:2
youtube=PASS samples=33 seconds=35.0 mediaProgress=29.668 decodedDelta=746 droppedDelta=9 decodedFPS=21.314 dropPct=1.206 width=854 height=480 paused=0 readyState=4
virtio_gpu_cmd_ctx_submit=7111
virtio_gpu_cmd_res_flush=3645
virtio_gpu_cmd_set_scanout=14
virtio_gpu_fence_ctrl=7205
virtio_gpu_fence_resp=7205
screenshot=missing
monitor_screendump_log=nc: /tmp/xv6-linux-chromium-vm-control/qemu-monitor-http-perf-4063814.sock: No such file or directory
```

Interpretation: the visible Linux/Xorg VM harness is now the Linux Chromium
playback baseline. Unlike the KWin-virtual controls, it opens a real host window,
runs Xorg at the requested 1280x800 geometry, exposes virgl/D3D12 GLX
acceleration, starts the same repo-staged Chrome-for-Testing binary, records
argument-derived GPU/renderer/zygote process counts plus total Chrome process
count, and records page-side media metrics. The local fixture baseline is
stronger than the YouTube baseline because it controls media resolution and
network. The YouTube run is still useful as a real-service stress baseline: on
this host it advanced 29.668 seconds of media during a 35-second sampler window,
decoded about 21.3 FPS, dropped 1.206 percent of decoded frames, and settled at
854x480 despite the `vq=hd720` hint. These runs are not durable visual proofs:
QEMU monitor screendump failed because the monitor socket was gone by the time
the capture command ran. Treat the serial/host logs as playback/performance
evidence only, and keep durable screenshot capture as an open Linux-baseline
harness gap.

Focused xv6 Chromium-video GBM reducer after the harness status fix:

```text
build-x86_64/kde-plasma-desktop-smoke/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fps-21.9
host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_status=status=DONE source=kde-app-launch-probe-log-attempted
sampler_result=kde_app_launch_probe chromium_sample_only=1 ... status=PASS
process_roles=browser:1,unknown:2,zygote:2,gpu:0,renderer:0
browser_render_fd_seen=1
chromium_int3_trap_count=0
chromium_fault_count=0
gpu_init_error_count=2
metrics=presented=337 presentedFPS=21.89 decoded=914 decodedFPS=59.38 dropped=610 dropPct=66.74 advanced=14.46 speed=0.939 wall=15.39
qemu_trace=ctx_submit:426,set_scanout:360,res_flush:360,fence_ctrl:426,fence_resp:426,res_create_3d:266
```

The remaining failure is no longer the fake guest-shell `$?` status problem:
GBM and sampler completion are completion-only markers, and post-evidence
requires explicit PASS result lines. Chromium decodes near 60 FPS and advances
near realtime, but only presents about 22 FPS with heavy drops. Chrome opens
the render node through the browser role; no GPU or renderer role is sampled,
and the run has no Chromium `int3` or page-fault signature. Next evidence
should distinguish browser-side GPU fallback, KWin/Wayland buffer import and
frame-callback pacing, and virtio scanout/present churn. Do not patch upstream
KDE/Qt/KWin/Xwayland/Mesa/Chromium from this result.

Fresh Chromium-video GBM reducer after the KDE/network SNI and `vm_copyout`
double-fault fixes:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T182600Z-chromium-video-gbm-int3/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-timeout
kde-chromium-video-post-evidence.log: status=FAIL reason=chrome-crash-regression
host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 ... status=PASS
launcher_policy: multiprocess=1 EGL_PLATFORM=gbm use_gl=egl-angle use_angle=opengles simdutf=(unset)
process_summary: browser_seen=1 gpu_seen=0 renderer_seen=0 zygote_seen=0 browser_render_fd_seen=0
perf_media_summary: perf_count=0 playing_count=0 tick_count=0 result_count=0
chromium_int3_summary: trap_count=1 pid=187 rip=0x4ad85a17 ip_file_off=0xad84a17 stack3_file_off=0x60d8b19 stack5_file_off=0xad84a36
chromium_int3_context: trap_roles=browser gpu_role_seen=0 trap_egl_platform=gbm trap_use_gl=egl-angle trap_use_angle=opengles trap_simdutf=missing
capture_present_count=4 capture_expected_count=12 capture_nonblack_count=3 capture_post_nonblack_changed_count=0
```

The harness label was `capture-timeout` because the capture loop never reached
`DONE`, but post-evidence shows the meaningful blocker is a browser-process
Chromium `int3`, not a fake shell status, GBM preprobe failure, sampler
failure, or page-side video metric failure. The durable GBM status file is a
completion marker, and the GBM/sampler PASS lines are explicit result lines.
The copied frames went black, then nonblack/static, then missing after the
browser trap.

Follow-up launch-only `about:blank` reducer:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T223300Z-chromium-aboutblank-gbm-int3/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
launch_only=1 chromium_video_url=about:blank chromium_extra_flags="--v=1 --enable-logging=stderr"
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
process_summary: browser_seen=1 zygote_seen=1 gpu_seen=0 renderer_seen=0 browser_render_fd_seen=1 launched_reparented=1
role_lifecycle_summary: browser_samples=60 zygote_samples=28 unknown_samples=29
chromium_int3_summary: trap_count=1 pid=199 rip=0x4ad85a17 ip_file_off=0xad84a17 stack3_file_off=0x60d8b19 stack5_file_off=0xad84a36
chromium_int3_context: trap_roles=browser gpu_role_seen=0 trap_egl_platform=gbm trap_use_gl=egl-angle trap_use_angle=opengles trap_cmd=...about:blank
qemu_trace_summary: ctx_submit=76 res_flush=23 set_scanout=23 fence_ctrl=76 fence_resp=76 res_create_3d=251
```

Verbose Chromium logging reached crash-consent setup, Widevine registration in
zygotes, policy/variation setup, allocator quarantine config, a D-Bus
`GetNameOwner` call for `org.freedesktop.login1`, and Wayland global binding
warnings before the same browser `int3`. The same `rip`/file-offset pair also
disassembles in the staged Chrome binary as a deliberate `int3; ud2` fatal
stub at ELF VMA `0xad85a17`; the stack caller at `0x60d9b10` immediately calls
that stub. No local `chrome.debug` sidecar is staged, so this is currently an
offset-level Chromium fatal path rather than a symbolized source CHECK.

Interpretation: the current Chromium blocker is generic browser startup after
early GBM/DRM/Wayland probing, not the local video fixture, YouTube, GBM config
shape, sampler status, SIMDUTF forcing, or upstream KDE/Qt/KWin/Xwayland/Mesa
source. The next reducer should avoid behavior changes and capture the missing
ABI edge immediately before the fatal stub, likely by adding xv6-owned syscall,
procfs, D-Bus/login1, Wayland, or DRM-role tracing around the browser process.
Do not spend the next cycle on video FPS until `about:blank` survives past this
browser `int3`.

Follow-up launch-only `about:blank` reducer with Chromium syscall-tail tracing:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T224000Z-chromium-aboutblank-syscall-tail-int3/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
launch_only=1 chromium_video_url=about:blank chromium_extra_flags="--v=1 --enable-logging=stderr"
QEMU_APPEND_EXTRA included chrome_syscall_tail_trace=1 chrome_lifecycle_trace=1
chrome_syscall_trace=1 chrome_syscall_enter_trace=1 and disabled DRM trace noise
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
process_summary: browser_seen=1 zygote_seen=1 gpu_seen=0 renderer_seen=0 browser_render_fd_seen=1
chromium_syscall_tail=available=1 tail_count=64 fd_count=2
chrome-syscall-tail-summary: reason=user-int3 pid=183 tgid=183 name=chrome recorded=64 first_seq=9699 next_seq=9763 depth=64
chrome-syscall-tail-families: clone=0 wait=0 futex=0 epoll=0 poll=0 open=26 fcntl=2 ioctl=0 mmap=5 sched=0 signal=0 ipc=7 other=24
last syscall before int3: mprotect addr=0x69400e02000 len=0x1000 prot=0x3 ret=-ENOMEM
previous mprotect: addr=0xead8de9c000 len=0x1000 prot=0x3 ret=0
chromium_int3_summary: trap_count=1 rip=0x4ad85a17 ip_file_off=0xad84a17 stack3_file_off=0x60d8b19 stack5_file_off=0xad84a36
```

This shifts the next reducer target from video playback to Linux `mprotect`
and VMA-shape semantics around the failed browser-process address. The failure
is immediately before Chromium's deliberate fatal `int3; ud2` site, while GBM
preprobe and browser render-node open both succeed. Next proof should first
enable the existing `vm_mprotect_trace=1 vm_mprotect_trace_ms=0` diagnostic or
add an xv6-owned failure-only VMA-neighborhood trace if the existing trace is
too shallow. Behavior changes to `mprotect` should wait for Linux comparison or
a focused reducer that proves whether the failed address is a hole, guard page,
unmerged split, fixed mapping, or protection-policy mismatch.

Follow-up launch-only `about:blank` reducer with trap-time VMA neighborhood:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T225853Z-aboutblank-tail-vma-mprotect-gap-int3/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
process_summary: browser_seen=1 zygote_seen=1 gpu_seen=0 renderer_seen=0 browser_render_fd_seen=1
chromium_syscall_tail=available=1 tail_count=64
last syscall before int3: mprotect addr=0xf400e02000 len=0x1000 prot=0x3 ret=-ENOMEM
chrome-syscall-tail-mprotect-fail: errno=12
left VMA: [0xf400de0000-0xf800000000) flags=0x8 private anon addr_in=1
hit VMA: none
right VMA: [0x3d035a0dc000-0x3d035a0dd000) flags=0x8 private anon addr_in=0
chromium_int3_summary: trap_count=1 rip=0x4ad85a17 ip_file_off=0xad84a17
```

Interpretation: the immediate Chromium crash was a VM lookup mismatch, not a
DRM/GBM failure. The previous non-NULL maple slot pointed at a still-valid
anonymous `PROT_NONE` VMA whose descriptor covered the requested page, but
`vm_find_area()` treated the page as a hole because `mtree_load()` found a NULL
slot. This matches the existing `vm.c` warning class around partial
`mtree_store_range` state: the VMA descriptor can remain live and continuous
while the maple slots contain a stale interior NULL.

Current kernel fix and proof:

```text
kernel/mm/vm.c: vm_find_area()
normal path: mtree_load(&vm->vm_mt, va)
fallback path: mt_prev(&vm->vm_mt, va, 0) only if vma_tree_entry_valid() and VMA_IN_RANGE(left, va)

build-x86_64/kde-plasma-desktop-smoke-history/20260628T230459Z-aboutblank-vm-find-area-fallback-gpu-utility-progress/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 ... status=PASS
process_summary: browser_seen=1 zygote_seen=1 gpu_seen=1 utility=1 renderer_seen=0 browser_render_fd_seen=1
process_policy_summary: browser and gpu-process carry use_gl=egl-angle use_angle=opengles EGL_PLATFORM=gbm
chromium_int3_summary: trap_count=0
chromium_syscall_tail=available=0
chromium_fault_summary: fault_count=0
```

The fallback is intentionally narrow: true gaps still return NULL, and invalid
or freed VMA pointers are rejected before use. This moved Chromium past the
old browser-process `mprotect`/`int3` gate and into GPU-process plus network
utility startup. The remaining failure is no longer the same crash signature.
The new log shows a helper `execve` failure for `/usr/bin/xdg-settings`
(`status=127`) and the GPU/utility roles appear only near the end of the
18-second sampler. Next reducer direction: keep upstream KDE/Qt/KWin/Xwayland/
Mesa/Chromium clean, stage or shim the missing `xdg-settings` helper if Linux
controls show Chromium expects it, and extend/adjust the launch-only role window
so the next failure is classified from GPU/renderer lifecycle, not from the
old browser `int3` bucket.

Classifier fix and current launch-only proof:

```text
scripts/gpu/kde-plasma-desktop-smoke.expect:
  tightened Chromium crash signatures so ordinary component status text such as
  "Cloud management controller initialization aborted as CBCM is not enabled"
  does not match chrome-crash-regression.

build-x86_64/kde-plasma-desktop-smoke-history/20260628T230926Z-aboutblank-launch-pass-gpu-init-respawn-after-classifier-fix/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_video=launch-log-only
chromium_video_post_evidence status=PASS reason=launch-only
launcher_crash_seen=0 launcher_loader_seen=0
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=63 ... status=PASS
process_summary: browser_seen=1 zygote_seen=1 gpu_seen=1 utility=1 renderer_seen=0 browser_render_fd_seen=1
process_policy_summary: gpu_use_gl_count=3 gpu_use_angle_count=3 use_gl=egl-angle use_angle=opengles EGL_PLATFORM=gbm
chromium_int3_summary: trap_count=0
chromium_fault_summary: fault_count=0
```

Outstanding evidence from the same run: the first GPU child still reports
`Requested GL implementation (gl=none,angle=none)` and exits GPU initialization,
then Chromium starts another GPU process with the intended `egl-angle/opengles`
policy. The run also preserves `/usr/bin/xdg-settings` lookup failures and
`org.freedesktop.portal.Documents` activation failure from `xdg-desktop-portal`.
Next work should compare Linux VM Chromium behavior for these helper/portal
lookups, then either stage a minimal `xdg-settings`/portal-data shim or add a
focused Chromium GPU-child argv/prefs reducer. Do not promote a kernel or DRM
behavior change from this proof alone.

Rootfs `xdg-settings` helper follow-up:

```text
scripts/image/xv6-xdg-settings.c
scripts/image/make-rootfs.sh stages it as /usr/bin/xdg-settings
cmake/BuildImage.cmake tracks the helper as a rootfs image source
debugfs fs.img: /usr/bin/xdg-settings is ELF, mode 0755
image helper check: default-web-browser chromium-browser.desktop -> exit 0
image helper check: default-web-browser firefox.desktop -> exit 1
```

The imported xdg-utils package remains upstream-clean; the final xv6 image
overlays only the Chromium-facing helper path with a native executable because
the kernel `execve` path handles ELF/PT_INTERP but not shell-script shebang
dispatch. A pre-alias VM proof preserved at
`20260628T232012Z-xdg-settings-elf-helper-exec-chromium-launch-pass` shows
Chromium now execs `/usr/bin/xdg-settings` as an ELF helper instead of returning
`ENOENT`/`ENOEXEC`, and the run reaches launch-only PASS with
`gpu_init_error_count=0`, `launcher_crash_seen=0`, `chromium_int3_summary:
trap_count=0`, and `chromium_fault_summary: fault_count=0`. That VM proof used
the first helper build, which exited 1 for Chromium's
`chromium-browser.desktop` alias; the rebuilt `fs.img` now accepts that alias.
Next VM proof should confirm the helper exits 0 in-guest and then move to the
remaining portal/Documents and full playback path.

Rootfs Documents portal shim follow-up:

```text
scripts/image/xv6-document-portal-shim.c
scripts/image/stage-kde-runtime.sh points org.freedesktop.portal.Documents at /bin/xv6-document-portal-shim
scripts/image/make-rootfs.sh stages the shim and rewrites the session service in the final image
cmake/BuildImage.cmake tracks the shim as a rootfs image source
```

The previous rootfs policy deliberately pointed
`org.freedesktop.portal.Documents.service` at `/bin/false` to fail-close the
FUSE document store. Chromium/GLib now probes that service during portal
startup, so the hard failure produced the observed sequence:
Documents activation exits status 1, `xdg-desktop-portal` fails to create the
Documents proxy, then GLib logs `g_close(fd:8/9) failed with EBADF`. The new
xv6-owned shim owns `org.freedesktop.portal.Documents`, exports
`/org/freedesktop/portal/documents`, answers `GetMountPoint`, and returns empty
document IDs for `AddFull` instead of claiming a working FUSE export path.
Next proof should rebuild the rootfs and verify the log contains
`xv6-document-portal-shim: acquired org.freedesktop.portal.Documents` and no
longer contains `Activated service 'org.freedesktop.portal.Documents' failed`
or the paired portal `g_close(... EBADF)` warnings.

KDE desktop color/readback follow-up on WSLg:

```text
build-x86_64/kde-plasma-desktop-smoke/
desktop-color-wakeup: failed in harness before after-click capture
kde-plasma-host-final.png: captured visible QEMU/WSLg window
visible QEMU crop saturation mean: 0.0818974
staged wallpaper saturation mean: 0.263902
fbstat sample-current before delayed init: nonzero=0 nonblack=0
fbstat sample-current after delayed init: nonzero=1024000 nonblack=0 center=0xff000000
kwin-screenshot reducer attempt: KWin crashed early with `pid 49 kwin_wayland: exception 1`
```

Interpretation: do not use forced X11 as the default WSLg workaround; the X11
GTK backend can create an unusably large/off-screen QEMU window on this host.
Keep the default launch on the native WSLg GTK/Wayland path and reserve
`QEMU_GTK_BACKEND=x11` for manual compatibility experiments only. The staged
wallpaper is color RGB data, while the visible QEMU window is low-saturation
grey and `/dev/fb0` readback stays black/alpha-black. That means the current
`fbstat` framebuffer path is not a valid virgl/KWin color oracle. The next
desktop-color reducer should use host-window screenshots or a real KWin/DRM
readback artifact, then compare before/after click saturation. Treat any fresh
KWin startup crash as a KWin lifetime reducer target before returning to
Chromium playback.

### WSL2 Host GPU Capability

Host audit artifact:

```text
build-x86_64/host-gpu-audit/20260627T141920Z/
```

Plan-feasibility proof artifact:

```text
build-x86_64/host-gpu-audit/20260627T145414Z-plan-feasibility-proof/
HOST-GPU-PLAN-FEASIBILITY: PASS
glx_default_renderer=D3D12 (Intel(R) UHD Graphics), accelerated=yes
glx_nvidia_renderer=D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU), accelerated=yes
egl_surfaceless_default_gles=D3D12 (Intel(R) UHD Graphics)
egl_surfaceless_nvidia_gles=D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)
egl_gbm_exit=1, egl_gbm_result="GBM platform: eglinfo: eglInitialize failed"
drm_class=version
kernel_source_live_mismatch=live:6.18.26.1-microsoft-standard-WSL2+ source:linux-msft-wsl-6.6.87.2
```

That proof captured the pre-fix source mismatch. The host source alignment was
fixed afterward:

```text
build-x86_64/host-gpu-audit/20260627T150818Z-wsl-source-alignment-fix/
WSL-SOURCE-ALIGNMENT-FIX: PASS
live_kernel=6.18.26.1-microsoft-standard-WSL2+
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
```

Fresh recheck after the Linux-VM feasibility proof:

```text
build-x86_64/host-gpu-audit/20260627T162253Z-wsl-source-alignment-recheck/
WSL-SOURCE-ALIGNMENT-RECHECK: PASS
live_kernel=6.18.26.1-microsoft-standard-WSL2+
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
build_tag=linux-msft-wsl-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
build_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
dev_dxg=present
dev_dri=missing
drm_class=version
```

Current source-alignment proof after the later mismatch audit:

```text
build-x86_64/host-gpu-audit/20260627T172140Z-wsl-source-alignment-current-proof/
WSL-SOURCE-ALIGNMENT-CURRENT-PROOF: PASS
live_kernel=6.18.26.1-microsoft-standard-WSL2+
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
source_head=f15bfa1288fbdc6924a73a6f58a21a02183fa523
source_status_count=0
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
dev_dxg=present
dev_dri=missing
drm_class=version
```

Live recheck before using host/source comparisons again:

```text
2026-06-28:
live_kernel=6.18.26.1-microsoft-standard-WSL2+
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
source_head=f15bfa1288fbdc6924a73a6f58a21a02183fa523
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
dev_dxg=present
dev_dri=missing
drm_class=version
kvm=present
```

Current fix proof:

```text
build-x86_64/host-gpu-audit/20260628T060119Z-wsl-source-mismatch-fix-current-proof/
WSL-SOURCE-MISMATCH-FIX-CURRENT-PROOF: PASS
live_kernel=6.18.26.1-microsoft-standard-WSL2+
expected_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
build_tag=linux-msft-wsl-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
dev_dxg=present
dev_dri=missing
drm_class=version
```

Live recheck before resuming Linux-VM validation:

```text
build-x86_64/host-gpu-audit/20260628T073725Z-wsl-source-mismatch-live-recheck/
WSL-SOURCE-MISMATCH-LIVE-RECHECK: PASS
live_kernel=6.18.26.1-microsoft-standard-WSL2+
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
dev_dxg=present
dev_dri=missing
```

Fresh fix recheck before continuing KDE/Chromium work:

```text
build-x86_64/host-gpu-audit/20260628T114411Z-wsl-source-mismatch-fix-recheck/
WSL-SOURCE-MISMATCH-FIX-RECHECK: PASS
live_kernel=6.18.26.1-microsoft-standard-WSL2+
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
source_head=f15bfa1288fbdc6924a73a6f58a21a02183fa523
source_status_count=0
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
dev_dxg=present
dev_dri=missing
drm_class=version
```

Current host facts:

- WSL 2.7.7.0, WSLg 1.0.73.2, live kernel
  `6.18.26.1-microsoft-standard-WSL2+`.
- `/dev/dxg` and `/dev/kvm` exist. `/dev/dri` is absent and
  `/sys/class/drm` only exposes the DRM core `version` node.
- OpenGL/EGL acceleration is available through Mesa D3D12 on `/dev/dxg`.
  The default GL/EGL adapter is Intel UHD Graphics; the NVIDIA RTX 4060 lane is
  selectable with `MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA`.
- Host Vulkan reports llvmpipe only in the current environment.
- Host `eglinfo -B -p gbm` fails at `eglInitialize`; this is expected on this
  WSL2 host because no native DRM/GBM render node is exposed.

Host kernel/source alignment:

- `/lib/modules/$(uname -r)/build` and `/lib/modules/$(uname -r)/source` now
  point at `/home/es/reps/WSL2-Linux-Kernel-6.18.26.1`, detached at the exact
  upstream `linux-msft-wsl-6.18.26.1` tag for the live
  `6.18.26.1-microsoft-standard-WSL2+` kernel.
- `/etc/wsl.conf` recreates both symlinks at WSL boot, and the previous
  `/etc/wsl.conf` was preserved as a timestamped
  `/etc/wsl.conf.codex-bak-*` backup before the update.
- The live `/proc/config.gz` was installed as the ignored generated
  `.config` in the matching source tree; the live and source config SHA-256
  values match.
- The live config keeps DRM core enabled
  (`CONFIG_DRM=y`, `CONFIG_DRM_KMS_HELPER=y`) and DXG enabled
  (`CONFIG_DXGKRNL=y`), while `CONFIG_DRM_VIRTIO_GPU` and
  `CONFIG_DRM_HYPERV` are not set.
- The WSL `dxgkrnl` source registers a misc device path for WSL GPU-P/D3D12
  (`/dev/dxg`) and binds Microsoft virtual render devices through PCI/VMBus.
  No inspected source or live sysfs evidence shows it creating DRM render nodes.

Plan impact:

- Do not treat host GBM/DRI failure as an xv6 regression. On this WSL2 host,
  the supported accelerated host path is DXG + Mesa D3D12, not `/dev/dri`.
- Keep the xv6 guest `/dev/dri/renderD128` KVM/virgl path as the KDE/KWin and
  Chromium GBM/DRM target.
- Pin and record the host adapter per run. Use the Intel D3D12 lane for
  historical baseline comparisons; use the NVIDIA D3D12 lane only as a separate
  optional performance lane. Do not compare FPS across those host lanes as the
  same baseline.
- Do not make host WSL DRI enablement a prerequisite for the KDE/Chromium GPU
  plan. If native host GBM proof is required, move that proof to native Linux or
  to a VM configuration that exposes a real DRM render node.

2026-06-27 launcher adapter mismatch fix: `scripts/launch/run-qemu.sh` no
longer maps `QEMU_WSL_D3D12_ADAPTER=auto` to NVIDIA when `nvidia-smi` is
present. `auto` now leaves Mesa/WSLg on its default adapter, matching the
Intel D3D12 baseline proofs above; NVIDIA remains available only through an
explicit `QEMU_WSL_D3D12_ADAPTER=NVIDIA` run.

2026-06-28 launcher WSL SDL default cleanup: the `run-qemu.sh` help text now
matches the code and documents `QEMU_WSL_SDL_VIDEODRIVER=x11`. Dry-run proof
shows WSL+D3D12+SDL still falls back to GTK by default to avoid the tested
black SDL GL window, `QEMU_WSL_D3D12_ADAPTER=auto` emits no
`MESA_D3D12_DEFAULT_ADAPTER_NAME`, and explicit
`QEMU_WSL_D3D12_ADAPTER=NVIDIA` still pins the NVIDIA lane.

Current launcher mismatch recheck before further KDE/Chromium validation:

```text
build-x86_64/host-gpu-audit/20260628T080123Z-wsl-kde-virgl-launcher-mismatch-recheck/
auto-default-adapter-dryrun.log:
  QEMU_GPU=virtio-vga-gl-primary resolves to WSL D3D12 virgl on GTK GLES
  without MESA_D3D12_DEFAULT_ADAPTER_NAME.
explicit-nvidia-adapter-dryrun.log:
  explicit QEMU_WSL_D3D12_ADAPTER=NVIDIA adds
  MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA.
sdl-guard-dryrun.log:
  QEMU_WSL_GL_DISPLAY=sdl is refused on the WSL D3D12 virgl path and the
  command falls back to -display gtk,gl=es.
```

Fresh current proof before resuming VM validation:

```text
build-x86_64/host-gpu-audit/20260628T111919Z-wsl-source-launcher-mismatch-current-proof/
WSL-SOURCE-LAUNCHER-MISMATCH-CURRENT-PROOF: PASS
live_kernel=6.18.26.1-microsoft-standard-WSL2+
module_build=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
module_source=/home/es/reps/WSL2-Linux-Kernel-6.18.26.1
source_tag=linux-msft-wsl-6.18.26.1
live_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
source_config_sha256=5ce8d4dbc8fbb7a58e532a3e818aca6f9bb444ca679c307ecfda2c2faef9684f
dev_dxg=present
dev_dri=missing
drm_class=version
auto_adapter_sets_mesa_default=no
nvidia_adapter_sets_mesa_default=yes
sdl_guard_falls_back_to_gtk=yes
```

Interpretation: the earlier host/source and launcher-selection mismatches are
closed in the live tree. A black KDE/Chromium result from this point is a
guest/compositor/present/readback failure to reduce, not evidence that the WSL
host is using the wrong kernel source, silently selecting the NVIDIA adapter in
`auto`, or launching the known-bad SDL GL display path.

Fresh mismatch-fix proof before resuming KDE/Chromium VM verification:

```text
build-x86_64/dependency-chain-audit/20260628T125535Z-mismatch-fix-current-proof/proof.txt
```

It shows `wayland-src/src` at `25da99a` with `HEAD...origin/main = 0 0`,
`HEAD...audit-original/wayland-src/main = 0 0`, no backup refs, and WSL
`/lib/modules/$(uname -r)/build` plus `source` both pointing at
`/home/es/reps/WSL2-Linux-Kernel-6.18.26.1` for live kernel
`6.18.26.1-microsoft-standard-WSL2+`. The parent `ports` repository still
shows the gitlink update as an uncommitted dependency metadata change, which is
expected until this work is committed; the imported source checkout itself is
fork-clean.

Chromium trace-preload audit fix before VM verification: `host-gui-runtime`
now depends on `scripts/image/chromium-egl-trace-preload.c`, the preload
directly compiles, an LD_PRELOAD ioctl smoke matches native Linux for
`FIONBIO` and a two-argument no-op ioctl, and
`cmake --build build-x86_64 --target host-gui-runtime -j2` staged
`/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so`
successfully. The preload no longer `fsync`s every trace line, preserves
`errno` around traced `open/openat`, and tracks simple `dup`/`dup2`/`dup3` and
`close_range` fd lifetime changes so IPC/render metrics are less likely to be
silently undercounted.

## Archived Gap Snapshot

This section is preserved as dated evidence. Current GUI direction lives in
`docs/active-work-plan.md`.

### 1. Xwayland GLAMOR / GLX Acceleration

Status: GLX context, draw, and FPS reducer evidence captured; performance
parity remains open.

The KDE default still guards Xwayland with `XV6_XWAYLAND_GLAMOR=off` for the
stable desktop smoke path. Focused acceleration runs now keep the requested
`kde_xwayland_glamor=auto` policy at the harness boundary while the xv6-owned
Xwayland wrapper maps that automatic policy to `-glamor es`, because the
unqualified Xwayland auto path chose no GLAMOR mode and disabled GLX before the
GL probe could create a context.

Historical no-Weston KDE X11/EGL failure evidence for unqualified auto:

```sh
KDE_SMOKE_REDUCER=x11-egl QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Failure artifact directory:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260625-230206-x11-egl-glamor-auto-glx-absent/
```

Result marker:

```text
KDE-PLASMA-DESKTOP-SMOKE-FAIL x11-egl-session-probe-FAIL exit_status=1
x11-egl-glx_query_extension-status-FAIL start=1 connect=1 gl=0 frame=0
```

Interpretation: KWin Wayland virgl reached OpenGL ES 3.1, but Xwayland's
unqualified auto path did not enable a usable GLX/GLAMOR mode. Xwayland logged
GL version 0/null and disabled GLAMOR before the direct X11 GLX probe could
select a visual; the host log also contained `glx_choose_visual missing`.

Current no-Weston KDE X11/GLX reducer evidence:

```sh
KDE_SMOKE_REDUCER=x11-egl QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1 kde_smoke_require_chromium=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Artifact directory:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260625-231025-x11-egl-auto-effective-es-pass/
```

Key evidence:

```text
Xwayland KDE wrapper: ... glamor=auto effective_glamor=es ... enable_glx=1
Xwayland KDE wrapper: final argv argc=17 glamor=es ... +extension_GLX=yes
host-x11-egl-smoke: phase=gl_strings status=PASS api=glx vendor=Mesa renderer=virgl ... gl_version=4.2 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2
host-x11-egl-smoke: phase=draw status=PASS frame=1 mode=glx-probe color=0
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-egl session_probe=PASS
```

Current no-Weston KDE X11/GLX FPS reducer outcome:

```sh
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1 kde_smoke_require_chromium=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Artifact directory:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-044514-x11-glx-fps-trace-disabled-confirmation-pass/
```

```text
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=85 elapsed_seconds=5.024329 fps=16.918
event_total_ms=8.345 draw_total_ms=3469.444 swap_total_ms=1528.249 final_xsync_ms=12.367 avg_swap_ms=17.979
QEMU trace: ctx_submit=387 set_scanout=100 res_flush=100 fence_ctrl/fence_resp=387/387 res_create_3d=53
```

Phase-detail artifact after probe refinement:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-045322-x11-glx-fps-phase-detail-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=93 elapsed_seconds=5.039396 fps=18.455
draw_total_ms=3539.314 swap_total_ms=1475.927 final_xsync_ms=13.364 avg_swap_ms=15.870
gl_issue_total_ms=3537.151 gl_error_total_ms=2.163 max_gl_issue_ms=130.051 max_gl_error_ms=0.396
QEMU trace: ctx_submit=414 set_scanout=106 res_flush=106 fence_ctrl/fence_resp=414/414 res_create_3d=54
```

Interpretation: the interactive serial-shell launch path was the reducer
blocker, not the GLX probe itself. The active FPS reducer now uses the
xv6-owned KDE session process to launch `/bin/host-x11-egl-smoke --glx-fps`,
which avoids KDE log noise corrupting typed shell commands. GLX starts, binds a
direct virgl context, records renderer strings, submits 3D work, and completes
the finite FPS loop. Trace-disabled confirmation shows diagnostic trace logging
was materially depressing FPS, but the best recorded xv6 result is still far
below the Linux GLX baseline of 55.638 FPS. The refined phase timing shows the
draw bucket is not `glGetError`; it is dominated by existing GL issue calls
(`glViewport`/`glClearColor`/`glClear`). The next reducer should stay
diagnostic, such as `finish-before-swap`, `swap-only`, or a Present-only pacing
burst, before making behavior changes.

GLX FPS variant harness update:

```text
scripts/image/host-x11-egl-smoke.c
scripts/image/kde-session.c
scripts/gpu/kde-plasma-desktop-smoke.expect
```

The xv6-owned GLX FPS reducer now accepts
`kde_x11_egl_glx_fps_variant=baseline|finish-before-swap|swap-only|oml-queue3-swap-only|oml-queue-depth-swap-only`.
Invalid variants fail before a misleading baseline PASS.
`finish-before-swap` resolves and validates `glFinish` through the GLX
proc-address path. The legacy OML queue variant,
`oml-queue3-swap-only`, resolves `GLX_OML_sync_control`, preserves the default
queue depth 3, requires three completed swap-buffer counters in the Expect
success path, and records OML issued/completed/pending timing fields without
changing the default no-variant log shape. The dynamic OML variant,
`oml-queue-depth-swap-only`, accepts
`kde_x11_egl_glx_fps_oml_queue_depth=N` for validated depths 1..8.

Build and staging checks after the harness update:

```text
git diff --check && git -C kernel diff --check
PKG_CONFIG_PATH=/tmp/xv6-host-devpkgs/root/usr/lib/x86_64-linux-gnu/pkgconfig PKG_CONFIG_SYSROOT_DIR=/tmp/xv6-host-devpkgs/root cmake --build build-x86_64 --target host-gui-runtime -j2
cmake --build build-x86_64 --target rootfs-refresh -j2
```

`host-gui-runtime` passed with the known optional probe staging warnings, and
`rootfs-refresh` passed with the known unrelated format-truncation warnings in
other probes. A Tcl-only completeness check could not run because `tclsh` was
not installed; an attempted `expect -n` fallback launched the smoke script, was
interrupted, and is not counted as verification evidence.

Successful `swap-only` variant artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-051923-x11-glx-fps-swap-only-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=105 elapsed_seconds=5.020442 fps=20.914 variant=swap-only
draw_total_ms=23.988 swap_total_ms=4969.928 final_xsync_ms=15.734 avg_swap_ms=47.333
gl_issue_total_ms=23.979 gl_error_total_ms=0.009 swap_only_skipped_draw_frames=104
QEMU trace: ctx_submit=467 set_scanout=119 res_flush=119 fence_ctrl/fence_resp=467/467 res_create_3d=53
```

Interpretation: skipping the per-frame draw work after frame 1 raised FPS only
from the best baseline 18.455 to 20.914, while nearly all elapsed time moved
into `glXSwapBuffers`. This argues against `glViewport`/`glClearColor`/
`glClear` issue overhead as the sole remaining limiter and makes GLX
swap/Present pacing the stronger next suspect.

`finish-before-swap` did not produce a timing result in this cycle. Three
attempts were preserved:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-051641-x11-glx-fps-finish-before-swap-kwin-startup-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-051815-x11-glx-fps-finish-before-swap-session-probe-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-052032-x11-glx-fps-finish-before-swap-spinlock-session-crash/
```

The last run reached the session probe and selected
`glx_fps_variant=finish-before-swap`, but failed before `glx_fps_result` with
`spin_lock reentry`. Treat these as KDE/session stability artifacts, not GLX
timing evidence.

Present-only pacing reducer update:

```text
scripts/image/host-x11-dri3-present-smoke.c
scripts/image/host-x11-dri3-present-smoke-launcher.c
scripts/image/kde-session.c
scripts/gpu/kde-plasma-desktop-smoke.expect
scripts/image/stage-host-gui-runtime.sh
```

The xv6-owned DRI3/Present probe now has a KDE session-launched
`--present-fps` mode and `KDE_SMOKE_REDUCER=x11-present-fps`. The host GUI
staging path links the probe against versioned `libxcb-dri3.so.0` and
`libxcb-present.so.0`, avoiding missing development pkg-config files while
keeping imported packages clean. The DRI3 launcher honors the session log
environment for durable `host-gui-host-x11-egl-smoke.log` capture and retains
the standalone DRI3 log fallback.

Successful no-Weston Present-only artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-055522-x11-present-fps-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=105 elapsed_seconds=5.003096 fps=20.987 present_only=1 gl_context=0
issued=105 completed=105 outstanding=0
present_request_total_ms=1.533 request_check_total_ms=1793.989 flush_total_ms=16.994
event_wait_total_ms=3182.260 completion_total_ms=5000.230 avg_completion_ms=47.621 max_completion_ms=334.250
first_msc=8589934671 last_msc=455266534495 msc_delta=446676599824
dri3_drm_name=virtio_gpu dri3_drm_version=0.1.0
```

Interpretation: the raw X11 Present-only loop under KDE/Xwayland reaches nearly
the same rate as the GLX `swap-only` result (20.987 FPS vs. 20.914 FPS), with
no GL context or draw work and zero outstanding presents. This strengthens the
case that the remaining acceleration gap is in Xwayland/Present pacing or the
xv6-facing wait/check path around Present completions rather than GL draw issue
overhead alone.

Unchecked Present request variant artifact:

```sh
KDE_SMOKE_REDUCER=x11-present-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_present_fps_variant=unchecked kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-061902-x11-present-fps-unchecked-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=107 elapsed_seconds=5.005733 fps=21.375 present_only=1 gl_context=0
issued=107 completed=107 outstanding=0
variant=unchecked request_check_enabled=0
complete_copy=107 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0 last_complete_mode=0
present_request_total_ms=1.676 request_check_total_ms=0.000 flush_total_ms=14.854
event_wait_total_ms=4979.203 completion_total_ms=5002.195 avg_completion_ms=46.749 max_completion_ms=311.131
dri3_drm_name=virtio_gpu dri3_drm_version=0.1.0
QEMU trace: ctx_submit=378 set_scanout=124 res_flush=124 fence_ctrl/fence_resp=378/378 res_create_3d=65
```

Interpretation: removing the per-frame `xcb_request_check()` eliminated the
1.79s request-check bucket from the baseline run, but did not materially raise
throughput: FPS moved only from 20.987 to 21.375 while nearly all elapsed time
moved into `event_wait_total_ms`. The reducer now points more strongly at
Present completion pacing or the X11 event-wait path than at checked-request
round trips. All 107 completions used Present complete mode copy, which is a
new useful split for the next pacing reducer.

Queue-depth Present request variant artifact:

```sh
KDE_SMOKE_REDUCER=x11-present-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_present_fps_variant=queue3-unchecked kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-064628-x11-present-fps-queue3-unchecked-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=211 elapsed_seconds=5.061021 fps=41.691 present_only=1 gl_context=0
issued=211 completed=211 outstanding=0
queue_depth=3 max_outstanding=3 drain_wait_total_ms=1.166 drain_waits=2
variant=queue3-unchecked request_check_enabled=0
complete_copy=211 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0 last_complete_mode=0
present_request_total_ms=5.092 request_check_total_ms=0.000 flush_total_ms=15.941
event_wait_total_ms=5019.538 completion_total_ms=15170.651 avg_completion_ms=71.899 max_completion_ms=299.129
dri3_drm_name=virtio_gpu dri3_drm_version=0.1.0
QEMU trace: ctx_submit=414 set_scanout=86 res_flush=86 fence_ctrl/fence_resp=414/414 res_create_3d=65
```

Interpretation: allowing up to three unchecked Present requests in flight
raised the Present-only loop from ~21 FPS to 41.691 FPS while still draining to
`outstanding=0`. This weakens the theory that kernel event-wait latency alone
is the limiter: `event_wait_total_ms` still spans the finite run, but more
Present completions are amortized across the same wait budget. The remaining
gap to the Linux GLX baseline now looks more like single-outstanding
swap/Present pacing or queue-depth policy than raw GL draw issue overhead.
All completions were still Present complete mode copy; no skip/flip mode was
observed.

Dynamic Present queue-depth harness update and depth-6 artifact:

```text
The xv6-owned Present FPS reducer now accepts
kde_x11_present_fps_variant=queue-unchecked with
kde_x11_present_fps_queue_depth=N for validated depths 1..8, while preserving
queue3-unchecked as the default depth-3 queue variant.
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-074258-x11-present-fps-queue-depth6-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=265 elapsed_seconds=5.064653 fps=52.323 present_only=1 gl_context=0
variant=queue-unchecked issued=265 completed=265 outstanding=0
queue_depth=6 max_outstanding=6
complete_copy=265 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0
present_request_total_ms=2.951 flush_total_ms=22.341 event_wait_total_ms=5013.991
completion_total_ms=30273.964
QEMU trace: ctx_submit=434 set_scanout=70 res_flush=70 fence_ctrl/fence_resp=434/434 res_create_3d=69
```

GLX OML queue-depth swap-only artifact after the stricter Expect success gate:

```sh
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue3-swap-only kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-071919-x11-glx-fps-oml-queue3-swap-only-strict-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=173 elapsed_seconds=5.023627 fps=34.437 variant=oml-queue3-swap-only
oml_available=1 oml_queue_depth=3 oml_sbc_issued=173 oml_sbc_completed=173 oml_max_pending_sbc=3
oml_issue_total_ms=2571.465 oml_wait_total_ms=2368.488 oml_drain_wait_total_ms=0.671 oml_gl_flush_before_swap=1
draw_total_ms=6.332 swap_total_ms=2571.465 final_xsync_ms=2.066 avg_swap_ms=14.864
QEMU trace: ctx_submit=548 set_scanout=92 res_flush=92 fence_ctrl/fence_resp=548/548 res_create_3d=54
```

Interpretation: queueing GLX swaps through `GLX_OML_sync_control` raises the
swap-only GLX loop from 20.914 FPS to 34.437 FPS, and the stricter Expect gate
now proves the final result line is the OML variant with `oml_available=1`,
queue depth 3, nonzero issued/completed SBC equality, and
`oml_max_pending_sbc=3`. This partially closes the GLX gap without patching
Mesa/Xwayland/KDE, but it still trails the Present queue3 reducer's 41.691 FPS
and the Linux KDE GLX baseline of 55.638 FPS. The remaining delta is now
reduced to GLX/Mesa/Xwayland swap pacing or throttling above the xv6 kernel's
raw Present queue-depth behavior, not to a missing GLX context or a single
kernel fd-passing/DRI3 failure.

Dynamic GLX OML queue-depth depth-6 artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-074136-x11-glx-fps-oml-queue-depth6-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=222 elapsed_seconds=5.066718 fps=43.815 variant=oml-queue-depth-swap-only
oml_available=1 oml_queue_depth=6 oml_sbc_issued=222 oml_sbc_completed=222 oml_max_pending_sbc=6
oml_issue_total_ms=4683.573 oml_wait_total_ms=275.204 oml_drain_wait_total_ms=62.137
draw_total_ms=14.298 swap_total_ms=4683.573
QEMU trace: ctx_submit=627 set_scanout=83 res_flush=83 fence_ctrl/fence_resp=627/627 res_create_3d=54
```

Interpretation: deeper queueing moves both reducers substantially. GLX depth 6
exceeds the prior Present queue3 result, 43.815 FPS versus 41.691 FPS, but
still trails Present depth 6 at 52.323 FPS and the Linux GLX baseline of
55.638 FPS. This points at queue-depth policy plus residual
GLX/Mesa/Xwayland throttling above raw Present, not an obvious kernel fence
starvation bug.

Depth-8 Present queue-depth artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-075019-x11-present-fps-queue-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-present-fps session_probe=PASS
frames=280 elapsed_seconds=5.092740 fps=54.980
variant=queue-unchecked issued=280 completed=280 outstanding=0
queue_depth=8 max_outstanding=8
complete_copy=280 complete_flip=0 complete_skip=0 complete_suboptimal_copy=0
present_request_total_ms=3.033 flush_total_ms=26.943 event_wait_total_ms=5040.579
completion_total_ms=40567.763
QEMU trace: ctx_submit=438 set_scanout=66 res_flush=66 fence_ctrl/fence_resp=438/438 res_create_3d=60
```

Depth-8 GLX OML queue-depth artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-075214-x11-glx-fps-oml-queue-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=226 elapsed_seconds=5.066070 fps=44.611
variant=oml-queue-depth-swap-only
oml_available=1 oml_queue_depth=8 oml_sbc_issued=226 oml_sbc_completed=226 oml_max_pending_sbc=8
oml_issue_total_ms=4820.616 oml_wait_total_ms=151.675 oml_drain_wait_total_ms=55.420 oml_gl_flush_before_swap=1
draw_total_ms=19.518 swap_total_ms=4820.616 final_xsync_ms=0.250 avg_swap_ms=21.330
QEMU trace: ctx_submit=630 set_scanout=80 res_flush=80 fence_ctrl/fence_resp=630/630 res_create_3d=53
```

Interpretation: Present depth 8 reaches 54.980 FPS, essentially matching the
Linux GLX control at 55.638 FPS with raw Present copy completions. GLX depth 8
only improves slightly over depth 6, 44.611 FPS versus 43.815 FPS, while OML
wait shrinks further and issue/swap MSC remains about 4.82s of the 5s run.
Queue depth explains raw Present pacing, but the residual GLX gap is now above
raw Present and likely in GLX/Mesa/Xwayland OML swap issue, flush, or
throttling. The next reducer should split OML `glFlush` versus `swap_msc`
issue timing, not start from a speculative kernel patch.

GLX OML flush/swap split reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue-depth-flush-swap-timing kde_x11_egl_glx_fps_oml_queue_depth=8 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect

build-x86_64/kde-plasma-desktop-smoke-history/20260626-081854-x11-glx-fps-oml-flush-swap-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=213 elapsed_seconds=5.085129 fps=41.887
variant=oml-queue-depth-flush-swap-timing
oml_available=1 oml_queue_depth=8 oml_sbc_issued=213 oml_sbc_completed=213 oml_max_pending_sbc=8
oml_issue_total_ms=4770.389 oml_wait_total_ms=202.580 oml_drain_wait_total_ms=65.749 oml_gl_flush_before_swap=1
oml_gl_flush_total_ms=7.406 oml_swap_msc_issue_total_ms=4762.982
draw_total_ms=19.464 swap_total_ms=4762.982 final_xsync_ms=3.898 avg_swap_ms=22.361
QEMU trace: ctx_submit=609 set_scanout=82 res_flush=82 fence_ctrl/fence_resp=609/609 res_create_3d=54 res_xfer_toh_3d=2
```

The new xv6-owned reducer variant exists in uncommitted code as
`oml-queue-depth-flush-swap-timing` and must be requested explicitly through
`QEMU_APPEND_EXTRA`; plain `KDE_SMOKE_REDUCER=x11-glx-fps` remains
backward-compatible after the audit adjustment. The residual GLX gap is not
explained by pre-swap `glFlush` cost: `glFlush` accounts for only 7.406 ms,
while `glXSwapBuffersMscOML` issue accounts for 4762.982 ms of the 4770.389 ms
OML issue bucket. This points at the GLX/Mesa/Xwayland swap/MSC
issue/throttling path above raw Present, not at a speculative kernel fence
starvation patch. Next evidence should compare the GLX/Xwayland/Mesa swap
request path or add a GLX non-OML/present-backed split, not alter kernel
behavior.

GLX OML issue-state timing reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue-depth-issue-state-timing kde_x11_egl_glx_fps_oml_queue_depth=8 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect

build-x86_64/kde-plasma-desktop-smoke-history/20260626-083821-x11-glx-fps-oml-issue-state-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=134 elapsed_seconds=5.067326 fps=26.444
variant=oml-queue-depth-issue-state-timing
oml_available=1 oml_queue_depth=8 oml_sbc_issued=134 oml_sbc_completed=134 oml_max_pending_sbc=8
oml_issue_total_ms=2184.040 oml_wait_total_ms=1.413 oml_drain_wait_total_ms=29.694 oml_gl_flush_before_swap=1
oml_gl_flush_total_ms=3.287 oml_swap_msc_issue_total_ms=2184.040
oml_get_sync_before_total_ms=485.399 oml_get_sync_after_total_ms=2283.982
oml_issue_state_samples=134 oml_post_issue_sbc_completed_count=33 oml_post_issue_sbc_lag_max=5
oml_post_issue_msc_delta_total=80 oml_post_issue_msc_delta_max=2
draw_total_ms=9.882 swap_total_ms=2184.040 final_xsync_ms=1.746 avg_swap_ms=16.299
QEMU trace: ctx_submit=518 set_scanout=116 res_flush=116 fence_ctrl/fence_resp=518/518 res_create_3d=52 res_xfer_toh_3d=2
```

This probe is intentionally intrusive: two extra `glXGetSyncValuesOML` calls
per issued swap lowered FPS to 26.444, so do not compare its FPS directly to
the earlier depth8 performance runs. It still proves the state sampling path:
134 samples for 134 issued SBCs, 33 post-issue samples already had
`sbc >= issued_sbc`, max post-issue lag was 5 SBC, and total MSC delta around
issue was 80 with max 2. `glXGetSyncValuesOML` itself is expensive here
(`before=485.399 ms`, `after=2283.982 ms`), so the next reducer should be less
intrusive, such as sparse state sampling or sampling only after every Nth
issue, before drawing kernel conclusions. Still no speculative kernel patch:
fences remain 1:1 and raw Present depth8 remains the performance control.

Sparse GLX OML issue-state timing reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=oml-queue-depth-issue-state-timing kde_x11_egl_glx_fps_oml_queue_depth=8 kde_x11_egl_glx_fps_oml_issue_state_sample_interval=16 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect

build-x86_64/kde-plasma-desktop-smoke-history/20260626-130930-x11-glx-fps-oml-issue-state-sparse16-depth8-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
frames=216 elapsed_seconds=5.076195 fps=42.552
variant=oml-queue-depth-issue-state-timing
oml_available=1 oml_queue_depth=8 oml_sbc_issued=216 oml_sbc_completed=216 oml_max_pending_sbc=8
oml_issue_total_ms=4091.230 oml_wait_total_ms=17.118 oml_drain_wait_total_ms=60.962 oml_gl_flush_before_swap=1
oml_gl_flush_total_ms=6.257 oml_swap_msc_issue_total_ms=4091.230
oml_get_sync_before_total_ms=602.750 oml_get_sync_after_total_ms=263.182
oml_issue_state_sample_interval=16 oml_issue_state_sampled_ratio=14/216
oml_issue_state_first_sample_frame=1 oml_issue_state_last_sample_frame=209
oml_issue_state_first_sample_sbc=1 oml_issue_state_last_sample_sbc=209
oml_issue_state_samples=14
oml_post_issue_sbc_completed_count=1 oml_post_issue_sbc_lag_max=4
oml_post_issue_msc_delta_total=5 oml_post_issue_msc_delta_max=1
QEMU trace: ctx_submit=630 set_scanout=90 res_flush=90 fence_ctrl/fence_resp=630/630 res_create_3d=54 res_xfer_toh_3d=2
```

The harness/probe now supports
`kde_x11_egl_glx_fps_oml_issue_state_sample_interval=N` /
`HOST_X11_EGL_GLX_FPS_OML_ISSUE_STATE_SAMPLE_INTERVAL` for
`oml-queue-depth-issue-state-timing`. The dense/default interval is 1, the
validated range is 1..64, and the sparse Expect gate validates the exact sample
count plus first/last sample cadence. This run used interval 16 and produced
14 samples, matching `ceil(216/16)=14`, from frame/SBC 1 through 209.

Interpretation: sparse issue-state sampling is materially less intrusive than
the dense timing probe and roughly in line with the previous flush/swap
issue-time profile, while still below the Linux GLX baseline of 55.638 FPS. It
reinforces that `glXSwapBuffersMscOML`/swap pacing remains the main residual
gap: sampled post-issue state mostly lagged, with only 1 sampled post-issue SBC
already complete. The next reducer/fix should continue from
GLX/Mesa/Xwayland swap/MSC pacing evidence, not fd passing/DRI3 or pre-swap
`glFlush`. Audit caveats: the status file records the reducer, `run.log` does
not literally include the env assignment, `cmdline` has
`kde_smoke_require_chromium=1` followed by the later `=0` override, no Weston
matches were present, PNGs are nonzero, and some sidecar logs are zero-byte.

Swap-interval-0 plain GLX swap-only reducer artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-095514-x11-glx-fps-swap-interval0-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0 ext_present=1 mesa_present=1 sgi_present=0
phase=glx_fps_result status=PASS frames=232 elapsed_seconds=5.046647 fps=45.971 swap_only_skipped_draw_frames=231 variant=swap-interval0-swap-only swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0 plain_swap_issue_total_ms=4965.974 plain_swap_avg_ms=21.405 plain_swap_max_ms=80.011
QEMU trace: ctx_submit=643 set_scanout=81 res_flush=81 fence_ctrl/fence_resp=643/643 res_create_3d=54 res_xfer_toh_3d=2
```

This is a diagnostic xv6-owned probe/harness change, not a KDE, Mesa, or
Xwayland source patch. Xwayland GLX accepted swap interval 0 through EXT, and
the MESA getter observed the interval move from 1 before the call to 0 after
it. Even with drawing skipped after the first frame and swap interval set to
0, ordinary `glXSwapBuffers` spent almost the entire run in swap
(`plain_swap_issue_total_ms=4965.974`) and produced 45.971 FPS. That is close
to, but still below, the Linux GLX baseline of 55.638 FPS and the raw Present
depth-8 result of 54.980 FPS. This narrows the remaining gap to plain
GLX/Xwayland swap path behavior above raw Present, not pre-swap draw work,
OML-only throttling, or missing DRI3/fd passing.

Swap-interval-0 plain GLX swap-only OML-state reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-swap-only kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-103840-x11-glx-fps-swap-interval0-oml-state-strict-pass/
Verification: git diff --check, focused reducer PASS with stricter after=0 Expect gate
Earlier same C/rootfs payload before the gate-only Expect fix: static diff checks, host-gui-runtime, rootfs-refresh, reducer PASS
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0
phase=glx_fps_result status=PASS frames=124 elapsed_seconds=5.006683 fps=24.767 variant=swap-interval0-oml-state-swap-only
swap_only_skipped_draw_frames=123
plain_oml_available=1 plain_oml_samples=124
plain_swap_issue_total_ms=2291.021 plain_swap_avg_ms=18.476 plain_swap_max_ms=86.867
plain_oml_get_sync_before_total_ms=275.566 plain_oml_get_sync_after_total_ms=1890.125
plain_oml_post_swap_sbc_delta_total=84 max=5
plain_oml_post_swap_msc_delta_total=58 max=2
plain_post_swap_xsync_total_ms=448.629 max=15.300
QEMU trace: ctx_submit=480 set_scanout=107 res_flush=107 fence_ctrl/fence_resp=480/480 res_create_3d=52 res_xfer_toh_3d=2
```

This variant is intentionally intrusive: it samples OML state before and after
every ordinary `glXSwapBuffers` and performs post-swap XSync, so its FPS should
not be compared directly to the prior non-sampling plain interval0 run. It
is now anchored by the final strict-pass artifact above, proving the durable
evidence path for ordinary swaps: swap interval 0 remained accepted with exact
after=0 verification, samples matched frames, post-swap OML state advanced
across the run, and virtgpu fences remained 1:1. The heavy after-swap
`glXGetSyncValuesOML` cost plus post-swap XSync cost explain much of the FPS
drop from the prior 45.971 FPS run. Continue with sparse/plain-swap state
sampling or GLX/Xwayland/Mesa swap-path attribution, not speculative
kernel/DRI3/fd-passing patches.

Sparse swap-interval-0 plain GLX swap-only OML-state reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-sampled-swap-only kde_x11_egl_glx_fps_oml_issue_state_sample_interval=16 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-110829-x11-glx-fps-swap-interval0-oml-state-sampled16-pass/
Verification: static diff checks across root/kernel/user/ports, host-gui-runtime, rootfs-refresh, focused no-Weston reducer PASS on first run with no retry
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0
phase=glx_fps_result status=PASS frames=210 elapsed_seconds=5.026245 fps=41.781 variant=swap-interval0-oml-state-sampled-swap-only
swap_only_skipped_draw_frames=209
plain_oml_available=1 plain_oml_samples=14 plain_oml_sample_interval=16 plain_oml_sampled_ratio=14/210 plain_oml_first_sample_frame=1 plain_oml_last_sample_frame=209
plain_swap_issue_total_ms=4224.962 plain_swap_avg_ms=20.119 plain_swap_max_ms=134.957
plain_oml_get_sync_before_total_ms=517.952 plain_oml_get_sync_after_total_ms=217.762
plain_oml_post_swap_sbc_delta_total=17 max=3
plain_oml_post_swap_msc_delta_total=7 max=1
plain_post_swap_xsync_total_ms=0.000 max=0.000
QEMU trace: ctx_submit=667 set_scanout=102 res_flush=102 fence_ctrl/fence_resp=667/667 res_create_3d=279 res_xfer_toh_3d=2
```

Sparse sampling avoids the dense sampler's per-frame post-swap XSync and
reduces observer cost: FPS returned to 41.781, much closer to the non-sampling
interval0 run at 45.971 than the dense sampler at 24.767, but still below
Linux GLX at 55.638 and raw Present depth8 at 54.980. The 14 samples match
`ceil(210/16)=14`, OML state advanced without regression, and virtgpu fences
remained 1:1. This keeps attribution above raw DRI3/Present/fd passing/fence
starvation; the remaining gap is ordinary GLX/Mesa/Xwayland swap pacing plus
sampled `glXGetSyncValuesOML` overhead. Next evidence should compare sparse
interval choices or GLX/Xwayland/Mesa swap-path attribution, not a speculative
kernel patch.

Sparse swap-interval-0 plain GLX swap-only post-swap-only OML-state reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-sampled-after-swap-only kde_x11_egl_glx_fps_oml_issue_state_sample_interval=16 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-113134-x11-glx-fps-swap-interval0-oml-state-sampled-after16-pass/
Verification: static diff checks across root/kernel/user/ports, host-gui-runtime, rootfs-refresh, focused no-Weston reducer PASS on first run with no retry; the history copy had its copied kde-plasma.fs.img removed only from the copy
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_swap_interval status=PASS requested=0 set_api=EXT before=1 after=0
phase=glx_fps_result status=PASS frames=241 elapsed_seconds=5.011420 fps=48.090 variant=swap-interval0-oml-state-sampled-after-swap-only
swap_only_skipped_draw_frames=240
plain_oml_available=1 plain_oml_samples=16 plain_oml_sample_interval=16 plain_oml_sampled_ratio=16/241 plain_oml_first_sample_frame=1 plain_oml_last_sample_frame=241
plain_swap_issue_total_ms=4393.292 plain_swap_avg_ms=18.229 plain_swap_max_ms=76.994
plain_oml_get_sync_before_total_ms=0.000 plain_oml_get_sync_after_total_ms=566.516
plain_oml_before_first_sbc=0 plain_oml_before_last_sbc=0 plain_oml_after_first_sbc=0 plain_oml_after_last_sbc=239
plain_oml_post_swap_sbc_delta_total=0 max=0 plain_oml_post_swap_msc_delta_total=0 max=0
plain_oml_after_sbc_delta_total=239 max=19 plain_oml_after_msc_delta_total=72 max=6
plain_post_swap_xsync_total_ms=0.000 max=0.000
QEMU trace: ctx_submit=653 set_scanout=79 res_flush=79 fence_ctrl/fence_resp=653/653 res_create_3d=43 res_xfer_toh_3d=2
```

This is xv6-owned harness/probe evidence, not a KDE/Mesa/Xwayland source patch.
It removes the pre-swap OML observer from the sparse interval16 sampler and
keeps post-swap XSync at zero. FPS rose to 48.090, above the earlier sparse
before+after sampler at 41.781 and slightly above the non-sampling interval0
plain swap-only run at 45.971, but still below the Linux GLX baseline of
55.638 and raw Present depth8 at 54.980. That attributes much of the earlier
sparse sampler penalty to pre-swap `glXGetSyncValuesOML`; the residual gap
remains ordinary GLX/Mesa/Xwayland swap path behavior above raw Present, not a
reason for speculative kernel/DRI3/fd-passing patches. The log does not emit
max values for OML get-sync before/after, nor first/last MSC fields for plain
after-swap samples.

Sparse swap-interval-0 plain GLX swap-only post-swap-only OML-state interval64 reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-oml-state-sampled-after-swap-only kde_x11_egl_glx_fps_oml_issue_state_sample_interval=64 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
Pre-reducer KWin startup crash: build-x86_64/kde-plasma-desktop-smoke-history/20260626-114317-x11-glx-fps-swap-interval0-oml-state-sampled-after64-fail/
Passing retry: build-x86_64/kde-plasma-desktop-smoke-history/20260626-114435-x11-glx-fps-swap-interval0-oml-state-sampled-after64-pass/
Verification: runtime-only; no source/docs edits before run, no build needed; static diff checks across top/kernel/user/ports passed; one pre-reducer KWin startup crash preserved; one retry PASS; no QEMU/smoke process remained afterward
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
probe_glx_fps_variant=swap-interval0-oml-state-sampled-after-swap-only
probe_glx_fps_oml_issue_state_sample_interval=64
frames=218 elapsed_seconds=5.046774 fps=43.196 variant=swap-interval0-oml-state-sampled-after-swap-only
swap_only_skipped_draw_frames=217
swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0
plain_swap_issue_total_ms=4718.298 plain_swap_avg_ms=21.644 plain_swap_max_ms=61.096
plain_oml_available=1 plain_oml_samples=4 plain_oml_sample_interval=64 plain_oml_sampled_ratio=4/218 plain_oml_first_sample_frame=1 plain_oml_last_sample_frame=193
plain_oml_get_sync_before_total_ms=0.000 plain_oml_get_sync_after_total_ms=209.927
plain_post_swap_xsync_total_ms=0.000 plain_post_swap_xsync_max_ms=0.000
plain_oml_after_first_sbc=1 plain_oml_after_last_sbc=192
plain_oml_after_sbc_delta_total=191 max=66 plain_oml_after_msc_delta_total=55 max=19
QEMU trace: ctx_submit=617 set_scanout=82 res_flush=82 fence_ctrl/fence_resp=617/617 res_create_3d=54 res_xfer_toh_3d=2
```

This was a runtime-only use of the committed xv6-owned reducer, not a
KDE/Mesa/Xwayland source patch. Interval64 lowered after-swap OML samples to
4 and kept pre-swap get-sync plus post-swap XSync at zero, but FPS fell to
43.196: below interval16 after-only at 48.090, non-sampling interval0 at
45.971, raw Present depth8 at 54.980, and Linux GLX at 55.638. The remaining
gap therefore did not disappear with lower OML observer frequency. Continue
with GLX/Xwayland/Mesa swap-path attribution above raw Present; do not infer a
kernel DRI3/fd-passing/fence issue from this run.

Swap-interval-0 plain GLX swap-only Present trace reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-131105-x11-glx-fps-present-trace-resolver-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
present_trace_result status=PASS pixmap_calls=229 register_special_xge_calls=1 present_special_registrations=1 wait_special_calls=133 poll_special_calls=783 complete_events=224 complete_copy=223 complete_flip=0 complete_suboptimal_copy=1 present_fallback_symbols=2 missing_symbols=0
frames=229 elapsed_seconds=5.070606 fps=45.162 variant=swap-interval0-swap-only
swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0
plain_swap_issue_total_ms=4998.262 plain_swap_avg_ms=21.826 plain_swap_max_ms=110.755
QEMU trace: ctx_submit=635 set_scanout=80 res_flush=80 fence_ctrl/fence_resp=635/635 res_create_3d=54 res_xfer_toh_3d=2
```

The trace preload is xv6-owned harness code and is injected child-only through
`HOST_X11_EGL_CHILD_LD_PRELOAD`; KDE, Qt, KWin, Xwayland, Mesa, and Chromium
sources remain upstream-clean. The first traced attempt proved that preloading
the launcher was wrong. A later trace attempt showed `RTLD_NEXT` did not expose
the Present entry points to the interposer, so the preload now falls back to
`dlopen("libxcb-present.so.0")` for `xcb_present_pixmap`,
`xcb_present_pixmap_checked`, and `xcb_present_id`. The passing artifact shows
the fallback resolved two symbols, no missing symbols, one Present special-event
registration, and hundreds of Present complete events. This closes the
diagnostic gap: the GLX swap path really reaches X11 Present and receives
completions under KDE/Xwayland. Remaining performance work should stay above
kernel fd passing, DRI3, and fence attribution unless a new reducer contradicts
this proof.

Swap-interval-0 plain GLX swap-only GLX-swap trace artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-133827-x11-glx-fps-glx-swap-trace-pass/
Verification: git diff --check, C syntax check for the preload, Expect
completeness check, independent audit PASS, host-gui-runtime, rootfs-refresh,
focused no-Weston reducer PASS on first VM run
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
present_trace_result status=PASS pixmap_calls=241 register_special_xge_calls=1 present_special_registrations=1 wait_special_calls=138 poll_special_calls=827 complete_events=237 complete_copy=236 complete_flip=0 complete_suboptimal_copy=1 present_fallback_symbols=2 missing_symbols=0
glx_swap_trace_result status=PASS glx_swap_buffers_calls=241 glx_swap_buffers_msc_oml_calls=0 glx_swap_total_ms=4970.392 glx_swap_max_ms=62.572 glx_swap_nested_pixmap_calls=241 glx_swap_nested_wait_special_calls=138 glx_swap_nested_poll_special_calls=821 glx_swap_nested_complete_events=237 glx_swap_accounted_present_ms=849.765 glx_swap_above_present_ms=4120.627 glx_swap_missing_symbols=0 glx_swap_recursion_skips=0
frames=241 elapsed_seconds=5.059753 fps=47.631 variant=swap-interval0-swap-only
swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0
plain_swap_issue_total_ms=4984.843 plain_swap_avg_ms=20.684 plain_swap_max_ms=62.591
QEMU trace: ctx_submit=668 set_scanout=84 res_flush=84 fence_ctrl/fence_resp=668/668 res_create_3d=54 res_xfer_toh_3d=2
```

This extends the child-only Present preload with passive GLX swap wrappers and
`glXGetProcAddress*` interposition. The independent audit caught and the final
patch fixed an availability hazard: the wrapper only returns local GLX swap
entry points after the real GLX loader or `RTLD_NEXT` proves the target exists.
The passing artifact shows all 241 ordinary `glXSwapBuffers` calls reached
Present, no GLX symbols were missing, and no recursion fallback was used. The
new split attributes about 849.765 ms of the 4970.392 ms GLX swap time to nested
Present calls and about 4120.627 ms above Present. That keeps the next fix
target in GLX/Mesa/Xwayland swap pacing above raw Present, not in kernel
DRI3/fd-passing/fence behavior.

Runtime `vblank_mode=1` plain GLX swap-only artifact:

```text
Temporary startup overlay:
LD_BIND_NOW=1 XV6_DESKTOP_DEFAULT=kde vblank_mode=1 /bin/xv6-desktop-session

KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-only kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-135133-x11-glx-fps-vblank-mode1-swap-only-pass/
Verification: direct make-rootfs with standard generated overlays plus the
temporary startup overlay, debugfs proof of `/etc/startup` before boot, focused
no-Weston reducer PASS, artifact preserved with `runtime-overlay/etc/startup`
and `startup-from-smoke-image`, then clean `rootfs-refresh` restored default
`/etc/startup`
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
run.log: ATTENTION: default value of option vblank_mode overridden by environment.
probe_glx_fps_variant=swap-only
phase=glx_fps_result status=PASS frames=221 elapsed_seconds=5.048027 fps=43.779 variant=swap-only
swap_interval_requested=0 swap_interval_set_api=none swap_interval_set_status=UNAVAILABLE swap_interval_before=-1 swap_interval_after=-1
plain_swap_issue_total_ms=4969.275 plain_swap_avg_ms=22.485 plain_swap_max_ms=160.755
QEMU trace: ctx_submit=618 set_scanout=80 res_flush=80 fence_ctrl/fence_resp=618/618 res_create_3d=54 res_xfer_toh_3d=2
```

This runtime-only test confirms that applying Mesa `vblank_mode=1` from process
start is not the missing acceleration lever. Mesa observed the environment, but
plain `swap-only` reached only 43.779 FPS: below the earlier explicit
`swap-interval0-swap-only` result at 47.631 FPS with GLX swap tracing, below
raw Present depth8 at 54.980 FPS, and below the Linux GLX baseline at 55.638
FPS. Do not make `vblank_mode=1` a persistent KDE default from this evidence.
Continue with GLX swap-path attribution, such as splitting generic XCB
wait/reply time inside `glXSwapBuffers`, before choosing a kernel/libc/sysroot
fix.

Swap-interval-0 plain GLX swap-only GLX-swap generic-XCB split artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
Initial trace-line truncation failure:
build-x86_64/kde-plasma-desktop-smoke-history/20260626-140957-x11-glx-fps-glx-swap-xcb-trace-counter-invalid/
Pre-probe KWin startup failure:
build-x86_64/kde-plasma-desktop-smoke-history/20260626-141657-x11-glx-fps-glx-swap-xcb-trace-kwin-startup-fail/
Passing retry:
build-x86_64/kde-plasma-desktop-smoke-history/20260626-141813-x11-glx-fps-glx-swap-xcb-trace-pass/
Verification: git diff --check, kernel diff check, C syntax check for the
preload, Expect completeness check, two independent audit PASS rounds,
host-gui-runtime, rootfs-refresh, focused no-Weston reducer PASS
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
present_trace_result status=PASS pixmap_calls=217 wait_special_calls=132 poll_special_calls=737 complete_events=213 complete_copy=212 complete_suboptimal_copy=1 present_fallback_symbols=2 missing_symbols=0
glx_swap_trace_result status=PASS glx_swap_buffers_calls=217 glx_swap_buffers_msc_oml_calls=0 glx_swap_total_ms=4972.181 glx_swap_max_ms=66.226 glx_swap_nested_pixmap_calls=217 glx_swap_nested_wait_special_calls=132 glx_swap_nested_poll_special_calls=731 glx_swap_nested_complete_events=213 glx_swap_nested_xcb_flush_calls=349 glx_swap_nested_xcb_flush_total_ms=26.802 glx_swap_nested_xcb_request_check_calls=2 glx_swap_nested_xcb_request_check_total_ms=20.519 glx_swap_nested_xcb_wait_reply_calls=0 glx_swap_nested_xcb_wait_reply_total_ms=0.000 glx_swap_nested_xcb_poll_reply_calls=0 glx_swap_nested_xcb_poll_reply_total_ms=0.000 glx_swap_nested_xcb_wait_event_calls=0 glx_swap_nested_xcb_wait_event_total_ms=0.000 glx_swap_nested_xcb_poll_event_calls=0 glx_swap_nested_xcb_poll_event_total_ms=0.000 glx_swap_accounted_present_ms=976.674 glx_swap_above_present_ms=3995.506 glx_swap_accounted_xcb_ms=47.320 glx_swap_above_xcb_ms=3948.186 glx_swap_missing_symbols=0 glx_swap_recursion_skips=0
frames=217 elapsed_seconds=5.050286 fps=42.968 variant=swap-interval0-swap-only
swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0
plain_swap_issue_total_ms=4977.956 plain_swap_avg_ms=22.940 plain_swap_max_ms=66.248
```

This extends the child-only GLX swap trace with generic XCB flush,
request-check, reply, and event attribution. The first run proved the added
line exceeded the old preload log buffer; the audited fix increased the
xv6-owned diagnostic logger buffer and preserved the failed artifact. The
passing retry shows generic XCB accounts for only 47.320 ms of the 4972.181 ms
GLX swap time, while nested Present accounts for 976.674 ms and about 3948.186
ms remains above both nested Present and generic XCB calls. Continue from
GLX/Mesa/Xwayland swap-path or loader/profile attribution above these nested
X11 calls; do not spend the next step on speculative kernel fd/fence/DRI3
changes from this evidence.

Swap-interval-0 plain GLX swap-only GLX-swap syscall split artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-143937-x11-glx-fps-glx-swap-syscall-trace-pass/
Verification: git diff --check, kernel diff check, C syntax check for the
preload, Expect completeness check, three independent audit passes including
ioctl varargs and pthread cleanup review, host-gui-runtime with `-pthread`,
rootfs-refresh, focused no-Weston reducer PASS
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
present_trace_result status=PASS pixmap_calls=234 wait_special_calls=100 poll_special_calls=837 complete_events=230 complete_copy=229 complete_suboptimal_copy=1 present_fallback_symbols=2 missing_symbols=0
glx_swap_trace_result status=PASS pid=119 glx_swap_syscall_trace=1 glx_swap_buffers_calls=234 glx_swap_total_ms=4897.093 glx_swap_accounted_present_ms=553.930 glx_swap_accounted_xcb_ms=90.374 glx_swap_above_xcb_ms=4252.790 glx_swap_syscall_total_ms=4563.773 glx_swap_syscall_nested_x11_ms=544.757 glx_swap_syscall_above_x11_ms=4019.017 glx_swap_syscall_ioctl_calls=236 glx_swap_syscall_ioctl_total_ms=3997.465 glx_swap_syscall_ioctl_max_ms=76.850 glx_swap_syscall_poll_ppoll_calls=571 glx_swap_syscall_poll_ppoll_total_ms=489.369 glx_swap_syscall_poll_ppoll_max_ms=41.541 glx_swap_syscall_read_recv_calls=711 glx_swap_syscall_read_recv_total_ms=62.853 glx_swap_syscall_write_send_calls=235 glx_swap_syscall_write_send_total_ms=14.086 glx_swap_missing_symbols=0 glx_swap_recursion_skips=0
frames=234 elapsed_seconds=5.045950 fps=46.374 variant=swap-interval0-swap-only
swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0
plain_swap_issue_total_ms=4911.277 plain_swap_avg_ms=20.988 plain_swap_max_ms=80.978
```

This extends the child-only trace with passive libc syscall-family attribution
inside `glXSwapBuffers` and keeps the wrappers disabled outside the GLX swap
depth. The audited implementation forwards `ioctl` conservatively as a
three-argument variadic call, balances Present special-event nesting with
pthread cleanup handlers, and builds the preload with `-pthread`. The passing
run shows the residual is kernel-facing: 4563.773 ms of the 4897.093 ms GLX
swap time is spent inside traced syscall wrappers, with 4019.017 ms above the
nested Present/XCB real calls. `ioctl` dominates that bucket at 3997.465 ms
over 236 calls; poll/ppoll contributes 489.369 ms, read/recv 62.853 ms, and
write/send 14.086 ms. The next reducer should identify the dominant DRM ioctl
request(s), fd roles, and wait semantics inside this GLX swap path before any
kernel behavior patch.

Swap-interval-0 plain GLX swap-only GLX-swap ioctl bucket artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-150229-x11-glx-fps-glx-swap-ioctl-trace-pass/
Verification: git diff --check, kernel diff check, C syntax check for the
preload, Expect completeness check, prior independent audit after the unsafe
argument-probing rollback, host-gui-runtime/rootfs-refresh from the same
diagnostic payload, focused no-Weston reducer PASS on first VM run, screenshots
and QEMU trace sidecars preserved.
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
present_trace_result status=PASS pixmap_calls=248 wait_special_calls=55 poll_special_calls=942 complete_events=247 complete_copy=246 complete_suboptimal_copy=1
glx_swap_trace_result status=PASS pid=122 glx_swap_buffers_calls=248 glx_swap_total_ms=4911.838 glx_swap_accounted_present_ms=435.933 glx_swap_accounted_xcb_ms=131.684 glx_swap_syscall_total_ms=4554.907 glx_swap_syscall_above_x11_ms=4053.719 glx_swap_syscall_ioctl_calls=250 glx_swap_syscall_ioctl_total_ms=4031.183 glx_swap_syscall_poll_ppoll_total_ms=452.705
glx_swap_ioctl_trace_result status=PASS ioctl_bucket_count=3 ioctl_bucket_drops=0 top_count=3 top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=248 top0_total_ms=4025.443 top0_max_ms=35.309 top0_ret_ok=248 top0_ret_fail=0 top0_shape=none top1_name=DRM_IOCTL_VIRTGPU_RESOURCE_CREATE top1_role=drm-render top1_calls=1 top1_total_ms=5.656 top2_name=DRM_IOCTL_PRIME_HANDLE_TO_FD top2_role=drm-render top2_calls=1 top2_total_ms=0.084
frames=248 elapsed_seconds=5.028198 fps=49.322 variant=swap-interval0-swap-only
swap_interval_requested=0 swap_interval_set_api=EXT swap_interval_set_status=PASS swap_interval_before=1 swap_interval_after=0
plain_swap_issue_total_ms=4921.914 plain_swap_avg_ms=19.846 plain_swap_max_ms=109.130
QEMU trace: ctx_submit=677 set_scanout=84 res_flush=84 fence_ctrl/fence_resp=677/677 res_create_3d=43 res_xfer_toh_3d=2
```

This reducer identifies the dominant kernel-facing request without changing
KDE, Qt, KWin, Xwayland, Mesa, or Chromium source. The residual GLX swap time
is not spread across DRI3 fd passing, generic XCB, or a mixed ioctl set:
`DRM_IOCTL_VIRTGPU_EXECBUFFER` on the render node accounts for 4025.443 ms of
4031.183 ms traced ioctl time and succeeds on every per-frame call. The
artifact records `top*_shape=none`; the current tracer reads ioctl arguments
through a fail-closed self `process_vm_readv` path, but this run does not log
the read errno. Request identity, fd role, call count, return status, and
timing are still durable. The next reducer/fix should target virtgpu execbuffer
wait or completion semantics in the GLX swap path, with kernel-side reducer
evidence before any behavior-changing patch.

No-code kernel DRM trace rerun for the same plain GLX swap path:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-152439-x11-glx-fps-glx-swap-kernel-drm-trace-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_result status=PASS frames=22 elapsed_seconds=5.455607 fps=4.033 variant=swap-interval0-swap-only
glx_swap_trace_result status=PASS glx_swap_buffers_calls=22 glx_swap_total_ms=4984.597 glx_swap_syscall_ioctl_calls=24 glx_swap_syscall_ioctl_total_ms=2972.696 glx_swap_syscall_poll_ppoll_total_ms=1889.895
glx_swap_ioctl_trace_result status=PASS ioctl_bucket_count=3 ioctl_bucket_drops=0 top_count=3 top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=22 top0_total_ms=2836.083 top0_ret_ok=22 top0_ret_fail=0
Kernel `execbuffer-time` aggregate for the GLX probe process (`pid=122`, owner `14:58`):
count=22 kernel_work_total_us=11710 submit_us=8993 trace_log_us=531539 max_work_us=2709 max_submit_us=2584 max_trace_log_us=77370
All traced execbuffers in the run:
count=68 kernel_work_total_us=71835 submit_us=26025 trace_log_us=1713469
QEMU trace: ctx_submit=110 set_scanout=26 res_flush=26 fence_ctrl/fence_resp=110/110 res_create_3d=43 res_xfer_toh_3d=2
```

This run used only existing kernel trace flags; no source changed. It is
intentionally not a performance comparison because per-call kernel `printf`
tracing is highly intrusive: FPS fell to 4.033 and kernel-side
`trace_log_us` alone reached 531.539 ms for the 22 GLX-probe execbuffers,
while all traced execbuffers spent 1.713469 seconds in trace logging. Still,
the existing phase split is useful: the measured GLX-probe execbuffer work
body was only 11.710 ms total, with 8.993 ms in `submit_us`, zero
`in_fence_us`, 1.073 ms in command copy, 0.042 ms in BO resolve, and 1.026 ms
in out-fence export. That contradicts a theory that the several seconds of
user-visible `ioctl()` time are simply spent inside the already-instrumented
execbuffer work body. The next diagnostic should therefore be a low-noise,
aggregate kernel trace around `virtio_gpu_user_submit()`,
`virtio_gpu_async_make_room()`, virtqueue notify/wait, and fence drain paths,
rather than more per-call console logging or a behavior-changing patch.

Low-noise virtgpu submit aggregate reducer artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0 virtio_gpu_submit_trace=1' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260626-153520-x11-glx-fps-glx-swap-virtgpu-submit-trace-pass/
Verification: git diff --check, kernel diff check, kernel build. Independent
audit PASS by Jason the 4th; no blocking findings. The diagnostic kernel patch
is gated by `virtio_gpu_submit_trace=1`, emitting compact
`virtio-gpu-submit-trace:` aggregate teardown summaries.
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_result status=PASS frames=240 elapsed_seconds=5.024100 fps=47.770 variant=swap-interval0-swap-only renderer="virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))" swap_interval_after=0 plain_swap_issue_total_ms=4963.477 plain_swap_avg_ms=20.681 plain_swap_max_ms=69.613
glx_swap_trace_result status=PASS glx_swap_buffers_calls=240 glx_swap_total_ms=4955.289 glx_swap_syscall_ioctl_total_ms=4282.132 glx_swap_syscall_poll_ppoll_total_ms=351.111
glx_swap_ioctl_trace_result status=PASS ioctl_bucket_count=3 ioctl_bucket_drops=0 top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=240 top0_total_ms=4272.070 top0_max_ms=44.490 top0_ret_ok=232 top0_ret_fail=8 top1_name=DRM_IOCTL_VIRTGPU_RESOURCE_CREATE top1_total_ms=9.945 top2_name=DRM_IOCTL_PRIME_HANDLE_TO_FD top2_total_ms=0.117
virtio-gpu-submit-trace: submit_calls=666 submit_us=6550226 lock_wait_us=2510799 attach_count=0 attach_us=0 async_prepare_us=36897 post_us=3988852 first_submit=5 failures=0 fence_calls=7 fence_us=18598 fence_drains=2 fence_drain_us=1 fence_failures=0 wait_used_calls=1016 wait_used_us=5050071 wait_used_max_us=231971 async_wait_progress_calls=617 async_wait_progress_us=4533879 make_room_calls=666 make_room_us=3660088 make_room_stalls=526 make_room_wait_us=3657717 make_room_max_wait_us=34369
QEMU trace: ctx_submit=667 set_scanout=87 res_flush=87 fence_ctrl/fence_resp=667/667 res_create_3d=45 res_xfer_toh_3d=2
```

This low-noise trace confirms that the earlier per-call kernel `printf` run
was perturbing FPS. The remaining swap cost is still host-visible execbuffer
ioctl time: 4272.070 ms of 4282.132 ms traced ioctl time is
`DRM_IOCTL_VIRTGPU_EXECBUFFER` on the render node. The kernel aggregate points
at async queue/make-room/wait-progress/post/op-lock time as the next evidence
surface, not at an immediate speculative behavior patch. Caveat: the kernel
summary is cumulative global state, so KWin, Xwayland, and probe clients are
mixed together; correlate it with the host-side GLX/ioctl bucket. The run also
logged Plasma-side `Cannot allocate memory` and a plasmashell breakpoint trap
after startup, but the `x11-glx-fps` session probe passed and artifacts were
preserved, so treat those as residual desktop stability signals rather than an
invalidation of the reducer evidence. Next step: narrow attribution per
owner/context or add deltas around async make-room/post and failed execbuffer
returns, without patching KDE, Qt, KWin, Xwayland, Mesa, or Chromium.

Owner-attributed virtgpu submit trace diagnostic evidence:

```text
Manual KDE GLAMOR auto VM inspection artifact:
build-x86_64/manual-vm-inspection/20260626-155455-kde-glamor-auto/
User-visible result: severe jitter, with YouTube playback stopping. Caveat:
no Chromium-specific stderr or durable role log was captured in this manual
inspection.

Diagnostic patch verification: independent audit PASS; kernel build PASS.

First trace-on reducer attempt:
build-x86_64/kde-plasma-desktop-smoke-history/20260626-161837-x11-glx-fps-owner-trace-kde-ready-crash-fail/
Result: failed at KWin readiness with no submits.

Trace-off control:
build-x86_64/kde-plasma-desktop-smoke-history/20260626-162000-x11-glx-fps-owner-trace-off-control-pass/
Result: PASS.

Repeat trace-on reducer:
build-x86_64/kde-plasma-desktop-smoke-history/20260626-162122-x11-glx-fps-owner-trace-pass/
Result: PASS.
Owner rows now include per-emission make_room, wait_progress, and wait_used
max fields.
```

This diagnostic preserves the no-behavior-change stance and improves
attribution over the previous global aggregate. The owner rows show no submit
failures and no fence failures, so the current signal is queue/backpressure
stalling rather than a failed fence path. The manual run includes
`QSGRenderThread` owner stalls around `wait_used` 297 ms and `make_room`
298 ms. The reducer global summary records `wait_used_max_us=329045`, KWin
owner max around 56.9 ms, and an Xwayland/probe owner row with about 1.06 s of
submit time, including about 595 ms of wait spread across roughly 29 ms
per-emission max intervals. Next step: add Chromium-specific durable
role/argv/stderr or local fixture capture and correlate browser/GPU/Xwayland
owner rows before behavior-changing kernel fixes.

2026-06-27 follow-up: the virtgpu submit trace now also records queue pressure
and async post-to-retire latency under the existing `virtio_gpu_submit_trace=1`
gate. Owner rows include `make_room_depth_max`, `make_room_count_max`,
`make_room_wait_count_max`, `post_count_max`, and retire latency/count splits
for submit/flush/transfer command types. The metric patch is diagnostic only;
`git -C kernel diff --check -- kernel/virtio_gpu.c` and
`cmake --build build-x86_64 --target kernel -j2` passed, and independent audit
found no blocking accounting, locking, or printf-format issues.

New no-Weston GLX swap-only evidence:

```text
Metric trace, default submit depth:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T081235Z-x11-glx-fps-submit-retire-trace-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_result status=PASS frames=92 elapsed_seconds=5.317829 fps=17.300 plain_swap_avg_ms=53.477 plain_swap_max_ms=244.784
glx_swap_ioctl_trace_result top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_calls=92 top0_total_ms=1754.257 top0_max_ms=107.054 top0_ret_ok=92 top0_ret_fail=0
virtio-gpu-submit-trace: submit_calls=273 submit_us=2859935 lock_wait_us=970112 post_us=1864010 make_room_stalls=130 make_room_wait_us=1660215 make_room_max_wait_us=93010 make_room_depth_max=1 make_room_count_max=1 make_room_wait_count_max=1 post_count_max=1 retire_submit_3d_us=10912066 retire_submit_3d_max_us=4592535

Host-trace control, default submit depth:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T081452Z-x11-glx-fps-host-trace-control-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_result status=PASS frames=221 elapsed_seconds=5.062059 fps=43.658 plain_swap_avg_ms=22.585 plain_swap_max_ms=183.459
glx_swap_ioctl_trace_result top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_calls=221 top0_total_ms=3891.542 top0_max_ms=67.463 top0_ret_ok=221 top0_ret_fail=0

Depth-2 host-trace experiment:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T081632Z-x11-glx-fps-submit-depth2-incomplete/
KDE-PLASMA-DESKTOP-SMOKE-FAIL x11-glx-fps-incomplete
host trace still reached frames=300 plain_swap_avg_ms=12.837 and
glx_swap_ioctl_trace_result top0_calls=300 top0_total_ms=2108.428 top0_max_ms=33.936.
Caveats: the GLX FPS result line was interleaved with trace output so the
Expect reducer failed the run, and this boot also logged a `pactl` #GP.

Depth-2 no-present-trace pass:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T081748Z-x11-glx-fps-submit-depth2-no-present-trace-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_result status=PASS frames=300 elapsed_seconds=3.529513 fps=84.998 plain_swap_avg_ms=11.619 plain_swap_max_ms=126.796

Depth-2 submit-trace validation attempt:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T082240Z-x11-glx-fps-submit-depth2-submit-trace-kwin-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
kde-exception-fatal-summary: pid=49 name=kwin_wayland ... rip=0x7fffff61171d
```

Interpretation: default SUBMIT_3D admission is effectively single-outstanding
under the KDE GLX path (`make_room_depth_max=1`, `make_room_count_max=1`), and
`virtio_gpu_async_submit_depth=2` is a strong reducer hint: it improves the
clean GLX swap-only pass from 43.658 FPS to 84.998 FPS. Do not flip the source
default yet. The depth-2 result still needs Chromium coverage, a stable
metric-backed run, and investigation of the trace/parser and startup-crash
interactions. For now, keep depth 2 as an opt-in `QEMU_APPEND_EXTRA` experiment
or, if it is later promoted, guard it narrowly behind `vgpu_async_pf=1` while
preserving explicit `virtio_gpu_async_depth` and
`virtio_gpu_async_submit_depth` overrides.

Stable depth-2 GLX sidecar Present trace artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T084319Z-x11-glx-fps-depth2-sidecar-present-trace-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_timing status=PASS result_status=PASS frames=159 avg_swap_ms=5.355
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=159 elapsed_seconds=5.014199 fps=31.710
host-x11-egl-smoke: phase=result status=PASS mode=glx-fps frame=159 use_glx=1
present_trace_result status=PASS pixmap_calls=159 complete_events=155 missing_symbols=0
glx_swap_trace_result status=PASS glx_swap_buffers_calls=159 glx_swap_total_ms=846.522 glx_swap_syscall_ioctl_total_ms=566.179
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_calls=159 top0_total_ms=566.179 top0_max_ms=23.601 top0_ret_ok=159 top0_ret_fail=0
```

The harness now writes the Present preload trace to
`host-gui-host-x11-present-trace.log` and appends that sidecar during parser
collection. The GLX result path also treats the `glx_fps_timing` PASS line as
a non-overriding completion fallback, and the host GLX smoke uses single-write
stderr logging for long proof lines. This is diagnostic-only xv6-owned
harness plumbing; it does not patch KDE, Qt, KWin, Xwayland, Mesa, or
Chromium. The sidecar run proves the prior depth-2/instrumented failure was
partly harness log interleaving, not absence of GLX/Present progress. It does
not make depth 2 safe to default, because Chromium stress still has to pass.

GLX swap ioctl argument-shape metric artifact:

```text
Initial safe-fallback calibration:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T101557Z-x11-glx-fps-ioctl-arg-read-metric-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=238 elapsed_seconds=5.042558 fps=47.198
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_calls=238 top0_total_ms=4094.607 top0_ret_ok=238 top0_ret_fail=0 top0_arg_read_attempts=238 top0_arg_read_ok=0 top0_arg_read_fail=238 top0_last_arg_errno=22 top0_shape=none

Argument-shape pass:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T101910Z-x11-glx-fps-ioctl-arg-shape-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=209 elapsed_seconds=5.042609 fps=41.447
glx_swap_trace_result status=PASS glx_swap_buffers_calls=209 glx_swap_total_ms=4967.994 glx_swap_syscall_ioctl_total_ms=3814.432
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=209 top0_total_ms=3804.187 top0_max_ms=71.885 top0_ret_ok=209 top0_ret_fail=0 top0_arg_read_attempts=209 top0_arg_read_ok=209 top0_arg_read_fail=0 top0_arg_read_fallback_ok=209 top0_shape=ve:f=0x2,sz=4104,bo=0,ffd=8

Kernel depth-probe attempt:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T103041Z-x11-glx-fps-submit-trace-startup-gp/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
cmdline included virtio_gpu_submit_trace=1 virtio_gpu_async_submit_depth=2
QEMU trace: ctx_submit=1 set_scanout=3 res_flush=3 fence_ctrl/fence_resp=1/1
No virtio-gpu-submit-depth-probe rows were emitted; KWin hit a startup #GP before the GLX reducer.

Default-depth kernel depth-probe pass:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T103758Z-x11-glx-fps-depth-probe-default-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=129 elapsed_seconds=5.060812 fps=25.490
plain_swap_avg_ms=38.519 plain_swap_max_ms=142.175
virtio-gpu-submit-trace: submit_calls=357 submit_us=3025345 lock_wait_us=937471 make_room_calls=357 make_room_stalls=199 make_room_wait_us=1882878 make_room_depth_max=1 make_room_count_max=1 make_room_wait_count_max=1 post_count_max=1 retire_submit_3d_us=10961250 retire_submit_3d_max_us=4826362 failures=0
QEMU trace: ctx_submit=358 set_scanout=43 res_flush=43 fence_ctrl/fence_resp=358/358
depth-probe examples: kwin_wayland drm_flags=0x2 nr_dwords=1141 resource_count=4; Xwayland.real drm_flags=0x0 nr_dwords=1028 resource_count=0 shape_submit_calls=13 shape_make_room_stalls=4 shape_make_room_wait_us=31772; ld-linux-x86-64/owner_tgid=58 nr_dwords=2840 shape_submit_calls=130 shape_make_room_stalls=97 shape_make_room_wait_us=1146757.

Hot-shape filter mismatch pass:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T122739Z-x11-glx-fps-hot-shape-filter-no-depth-probe-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=71 elapsed_seconds=5.189639 fps=13.681
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_calls=71 top0_total_ms=1704.514 top0_max_ms=159.902 top0_shape=ve:f=0x2,sz=4104,bo=0,ffd=8
No virtio-gpu-submit-depth-probe rows were emitted. The preload reports the out-fence fd after ioctl return, while the kernel trace shape is captured before out-fence export.

Request-time hot-shape depth-probe pass:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T123259Z-x11-glx-fps-hot-shape-depth-probe-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=86 elapsed_seconds=5.205788 fps=16.520
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=86 top0_total_ms=2086.910 top0_max_ms=294.448 top0_shape=ve:f=0x2,sz=4104,bo=0,ffd=9
virtio-gpu-submit-depth-probe aggregate: rows=8 shape_submit_calls=88 shape_make_room_calls=88 shape_make_room_stalls=71 shape_make_room_wait_us=1251135 shape_make_room_max_wait_us=293963 shape_make_room_depth_max=1 shape_make_room_count_max=1 shape_make_room_wait_count_max=1 shape_posted=88 shape_retired=88 shape_retire_us=1802132 shape_retire_max_us=108115 shape_failures=0 shape_mixed=0
QEMU trace: ctx_submit=320 set_scanout=55 res_flush=55 fence_ctrl/fence_resp=320/320 res_create_3d=264

Post-audit hot-shape metric fix and proof:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T224120Z-x11-glx-fps-hot-shape-kde-session-startup-retry-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
cmdline used virtio_gpu_submit_hot_shape_trace=1 with X11 Present trace enabled.
KWin stayed interruptible and `/dev/shm/xdg-runtime-root/wayland-0` was not
visible after compositor grace; QEMU trace only reached ctx_submit=1,
set_scanout=3, res_flush=3, fence_ctrl/fence_resp=1/1, and no
virtio-gpu-submit-depth-probe rows emitted.

Passing retry without the X11 Present preload:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T224243Z-x11-glx-fps-hot-shape-depth-probe-post-audit-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=209 elapsed_seconds=5.063299 fps=41.277 variant=swap-interval0-swap-only
plain_swap_avg_ms=23.833 plain_swap_max_ms=201.247
virtio-gpu-submit-depth-probe aggregate: rows=25 parseable_rows=25 shape_submit_calls=209 shape_make_room_calls=209 shape_make_room_stalls=176 shape_make_room_us=1761743 shape_make_room_wait_us=1760807 shape_make_room_max_wait_us=288605 shape_make_room_depth_max=1 shape_make_room_count_max=1 shape_make_room_wait_count_max=1 shape_posted=209 shape_post_count_max=1 shape_retired=209 shape_retire_us=727432 shape_retire_max_us=477594 shape_failures=0 shape_mixed=0 broad_submit_trace_rows=0 owner_trace_rows=0 fence_fd_values=-1

Audit fixes before the passing retry: hot-shape-only owner-drop accounting now
emits/advances the drops counter under either submit trace gate, and OUT-fence
fd values are normalized out of the request-time hot-shape identity unless the
execbuffer uses an input fence. The proof keeps `shape_mixed=0` and
`fence_fd=-1`, so fd export does not split identical GLX hot submit rows.

Hot-shape trace-gate proof without broad submit trace:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-hot-shape-trace-gate-pass/
HOT-SHAPE-TRACE-GATE: PASS
cmdline used virtio_gpu_submit_hot_shape_trace=1 without virtio_gpu_submit_trace=1
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=142 elapsed_seconds=5.070805 fps=28.003 variant=swap-interval0-swap-only
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=142 top0_total_ms=2586.101 top0_max_ms=93.074 top0_shape=ve:f=0x2,sz=4104,bo=0,ffd=9
virtio-gpu-submit-depth-probe aggregate: rows=72 parseable_rows=70 interleaved_rows=2 shape_submit_calls=141 shape_make_room_calls=141 shape_make_room_stalls=94 shape_posted=138 shape_retired=139 shape_make_room_depth_max=1 shape_post_count_max=1 shape_failures=0 shape_mixed=0 broad_submit_trace_rows=0 owner_trace_rows=0

Depth-2 hot-shape trace-gate comparison:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-depth2-hot-shape-trace-gate-pass/
DEPTH2-HOT-SHAPE-TRACE-GATE: PASS
cmdline used virtio_gpu_submit_hot_shape_trace=1 virtio_gpu_async_submit_depth=2 without virtio_gpu_submit_trace=1
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=157 elapsed_seconds=5.158046 fps=30.438 variant=swap-interval0-swap-only plain_swap_avg_ms=31.645 plain_swap_max_ms=141.988
glx_swap_trace_result status=PASS glx_swap_buffers_calls=157 glx_swap_total_ms=4911.631 glx_swap_syscall_ioctl_total_ms=992.334 glx_swap_syscall_poll_ppoll_total_ms=2662.551
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=157 top0_total_ms=977.426 top0_max_ms=63.826 top0_ret_ok=157 top0_ret_fail=0 top0_shape=ve:f=0x2,sz=4104,bo=0,ffd=8
virtio-gpu-submit-depth-probe aggregate: rows=79 parseable_rows=75 interleaved_rows=4 shape_submit_calls=154 shape_make_room_calls=151 shape_make_room_stalls=25 shape_posted=151 shape_retired=150 shape_make_room_depth_max=2 shape_make_room_count_max=2 shape_make_room_wait_count_max=2 shape_post_count_max=2 shape_failures=0 shape_mixed=0 broad_submit_trace_rows=0 owner_trace_rows=0
QEMU trace: ctx_submit=448 set_scanout=60 res_flush=60 fence_ctrl/fence_resp=448/448 res_create_3d=43

Depth-2 hot-shape submit-trace comparison attempt:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T123958Z-x11-glx-fps-depth2-hot-shape-startup-retry/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
cmdline included virtio_gpu_submit_trace=1 virtio_gpu_async_submit_depth=2 kde_x11_egl_glx_present_trace=1
Only the initial global submit trace emitted: submit_calls=0 wait_used_calls=28 wait_used_us=341160. KWin stayed interruptible and `/dev/shm/xdg-runtime-root/wayland-0` was not visible after the fixed 60s compositor grace. No GLX reducer, Present trace, or hot-shape depth rows ran.

Depth-2 lower-overhead Present/ioctl comparison:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T124210Z-x11-glx-fps-depth2-present-trace-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
host-x11-egl-smoke: phase=glx_fps_result status=PASS frames=300 elapsed_seconds=4.142910 fps=72.413 plain_swap_avg_ms=13.591 plain_swap_max_ms=47.007
present_trace_result status=PASS pixmap_calls=300 complete_events=299 complete_copy=298 complete_flip=0 complete_suboptimal_copy=1 missing_symbols=0
glx_swap_trace_result status=PASS glx_swap_buffers_calls=300 glx_swap_total_ms=4069.703 glx_swap_syscall_ioctl_total_ms=2523.064 glx_swap_syscall_poll_ppoll_total_ms=427.156
glx_swap_ioctl_trace_result status=PASS top0_name=DRM_IOCTL_VIRTGPU_EXECBUFFER top0_role=drm-render top0_calls=300 top0_total_ms=2508.609 top0_max_ms=38.114 top0_ret_ok=300 top0_ret_fail=0 top0_shape=ve:f=0x2,sz=4104,bo=0,ffd=9
QEMU trace: ctx_submit=874 set_scanout=128 res_flush=128 fence_ctrl/fence_resp=874/874 res_create_3d=53
This pass also logged a `pactl` #GP during audio startup, but KDE, Xwayland, and the GLX reducer continued to PASS.
```

The child-only Present/GLX preload now records ioctl argument-read attempts,
success/failure, fallback use, and last argument errno for the top ioctl
buckets. Independent audit found one blocker in the first version: a raw
same-process fallback after `process_vm_readv()` failure could turn a real
`EFAULT` ioctl into a child-side crash. The final diagnostic gates the fallback
on the real ioctl succeeding and the argument range being readable in
`/proc/self/maps`. It accepts xv6's current invalid-stub errno (`EINVAL`) as
the local `process_vm_readv` stub signal; the calibration run above proves why
`ENOSYS` alone was too narrow for this tree.

Interpretation: the remaining default-depth GLX swap cost is still dominated
by render-node `DRM_IOCTL_VIRTGPU_EXECBUFFER`, and the recovered hot-path shape
is stable enough to guide the next kernel-side metric: `flags=0x2`,
`size=4104`, and no BO handle array. The host-side preload reports the
post-return exported fd (`ffd=8` or `ffd=9` in the latest runs), while the
kernel diagnostic sees request-time `fence_fd=-1` before out-fence export; the
request-time filter intentionally keys on `flags/size/bo-count` instead. This
does not justify promoting submit depth 2 yet. It narrows the next reducer to
an audited default-depth versus candidate-depth comparison for this execbuffer
shape, followed by Chromium GBM stress before any default or behavior-changing
kernel patch.

The 20260627T103758Z default-depth pass proved the broad submit-depth probe was
stable but also showed the shape counters were contaminated by a mutable
latest-shape label. The 20260627T123259Z rerun fixes that diagnostic by
filtering to the request-time hot GLX execbuffer shape: the emitted rows now
show `shape_mixed=0`, `shape_failures=0`, effective single-outstanding
admission (`shape_make_room_depth_max=1`, `shape_make_room_count_max=1`,
`shape_make_room_wait_count_max=1`), and 71 make-room stalls across 88 hot
submits. The 20260627T-hot-shape-trace-gate-pass rerun then narrows that metric
behind `virtio_gpu_submit_hot_shape_trace=1`: it emitted only
`virtio-gpu-submit-depth-probe` rows for the hot request-time shape and no
`virtio-gpu-submit-trace:` or `virtio-gpu-submit-owner-trace:` rows. The two
incomplete depth-probe rows in that proof are console interleaving with Qt log
output, not counter failures; the aggregate still shows `shape_failures=0` and
`shape_mixed=0`. Independent audit found no blocker in the narrow gate but
confirmed that it is lower-noise, not zero-overhead: non-hot submits still
touch in-memory timing/accounting before the hot-shape owner gate. This gate is
therefore the preferred metric mode for the next default-depth versus
candidate-depth comparison because it avoids broad submit/owner printf rows and
owner-row allocation for non-hot shapes; it is not a trace-disabled control.

The 20260627T124210Z depth-2 lower-overhead pass proves the candidate depth
still substantially improves the GLX/DRM ioctl path in this tree
(`fps=72.413`, `top0_max_ms=38.114`), but the 20260627T123958Z run shows that
combining depth 2 with the broad kernel submit-trace metric can perturb KWin
startup before any GLX evidence is collected. Treat the 20260627T103041Z
depth-2 startup failure, 20260627T082240Z depth-2 submit-trace crash, and
20260627T123958Z depth-2 submit-trace startup retry as instability evidence,
not GLX performance evidence. The
20260627T-depth2-hot-shape-trace-gate-pass rerun proves the candidate depth can
survive the lower-noise hot-shape metric and reach `shape_make_room_depth_max=2`
with no broad/owner trace rows, no `shape_failures`, and no `shape_mixed`.
It also shows why the next proof must include a trace-disabled control and
Chromium GBM: under the Present/syscall preload, depth 2 reduced the hot
`DRM_IOCTL_VIRTGPU_EXECBUFFER` total from 2586.101 ms to 977.426 ms, but FPS
rose only from 28.003 to 30.438 because X11 Present wait/poll time dominated
the instrumented run. Depth 2 remains an opt-in reducer hint until Chromium
stress and trace-disabled controls are both green.

2026-06-27 GLX bottleneck summary artifact:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_x11_egl_glx_present_trace=1 kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260627T181448Z-x11-glx-fps-bottleneck-summary-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
kde-x11-glx-bottleneck-summary.log:
  frames=203 elapsed_seconds=5.023366 fps=40.411 variant=swap-interval0-swap-only
  glx_swap_total_ms=4966.256
  syscall_ms=4114.013 syscall_pct=82.8
  ioctl_ms=3945.208 ioctl_pct_of_swap=79.4
  top_ioctl name=DRM_IOCTL_VIRTGPU_EXECBUFFER role=drm-render calls=203 total_ms=3937.847 pct_of_ioctl=99.8 pct_of_swap=79.3 max_ms=149.283 ret_ok=203 ret_fail=0 shape=ve:f=0x2,sz=4104,bo=0,ffd=8
```

The smoke harness now writes `kde-x11-glx-bottleneck-summary.log`, a host-side
derived artifact from the already trusted `glx_fps_result`,
`glx_swap_trace_result`, and `glx_swap_ioctl_trace_result` lines. Independent
read-only audit found no blocker: the summary does not send guest commands,
does not use shell `$?`, tolerates missing or malformed numeric fields as
`n/a`, and is nonfatal if host-side summary writing fails. The fresh no-Weston
pass keeps the bottleneck on render-node `DRM_IOCTL_VIRTGPU_EXECBUFFER` for
the hot GLX swap shape.

Trace-disabled control:

```text
KDE_SMOKE_REDUCER=x11-glx-fps QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto kde_xwayland_enable_glx=1 kde_x11_egl_glx_fps_variant=swap-interval0-swap-only kde_smoke_require_chromium=0 chrome_drm_ioctl_trace=0 chrome_drm_fence_trace=0' timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
build-x86_64/kde-plasma-desktop-smoke-history/20260627T182318Z-x11-glx-fps-trace-disabled-control-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
kde-x11-glx-bottleneck-summary.log:
  frames=231 elapsed_seconds=5.045524 fps=45.783 variant=swap-interval0-swap-only
  plain_swap_avg_ms=21.475 plain_swap_max_ms=63.539
  glx_swap_total_ms=n/a syscall_ms=n/a ioctl_ms=n/a
  bottleneck=unclassified
```

Interpretation: without the Present/syscall preload, the current default-depth
GLX swap-only lane is slightly faster than the traced bottleneck run
(`45.783` FPS vs. `40.411` FPS) and has lower max swap time (`63.539 ms` vs.
`166.690 ms`). The trace-disabled control is now green; the next
behavior-changing kernel candidate still needs Chromium GBM regression proof
before any default change.

2026-06-28 current Intel-lane GLX metric/control pair:

```text
Hot-shape metric, default submit depth:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T025643Z-x11-glx-fps-hot-shape-current-default-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_result status=PASS frames=123 elapsed_seconds=5.081793 fps=24.204
plain_swap_avg_ms=40.688 plain_swap_max_ms=166.031
hot-shape aggregate, parseable rows only:
rows=67 parseable_rows=65 interleaved_rows=2
shape_submit_calls=117 shape_make_room_calls=117 shape_make_room_stalls=89
shape_make_room_wait_us=1016119 shape_make_room_max_wait_us=68034
shape_make_room_depth_max=1 shape_make_room_count_max=1
shape_make_room_wait_count_max=1 shape_post_count_max=1
shape_failures=0 shape_mixed=0
QEMU trace: ctx_submit=348 set_scanout=44 res_flush=44 fence_ctrl/fence_resp=348/348

Trace-disabled control:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T025820Z-x11-glx-fps-current-trace-disabled-control-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
phase=glx_fps_result status=PASS frames=231 elapsed_seconds=5.055552 fps=45.692
plain_swap_avg_ms=21.507 plain_swap_max_ms=110.834
QEMU trace: ctx_submit=635 set_scanout=78 res_flush=78 fence_ctrl/fence_resp=635/635
```

Interpretation: the narrow hot-shape serial metric still confirms the
single-outstanding admission pattern for the hot GLX execbuffer shape
(`shape_make_room_depth_max=1`, `shape_post_count_max=1`, no failures or mixed
shapes), but it is not performance-neutral: the measured FPS drops from the
trace-disabled control's `45.692` to `24.204`, and average swap time rises from
`21.507 ms` to `40.688 ms`. Treat this as diagnostic bottleneck evidence, not
as a performance proof. Do not promote `virtio_gpu_async_submit_depth=2` or any
behavior-changing kernel patch from this pair alone; the next metric step needs
a lower-perturbation counter/export path or a separately audited depth2/control
pair followed by Chromium-video regression proof.

Low-noise hot-shape stats proof and diagnostic gate fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T032846Z-x11-glx-fps-hot-shape-stats-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
cmdline used virtio_gpu_submit_hot_shape_stats=1 without hot-shape serial trace
phase=glx_fps_result status=PASS frames=238 elapsed_seconds=5.035623 fps=47.263
plain_swap_avg_ms=20.836 plain_swap_max_ms=61.858
serial marker recheck: no virtio-gpu-submit-depth-probe, owner-trace, or broad
submit-trace lines in active or archived run logs
fbstat hot-shape counters:
virtio_hot_shape_owners=2 virtio_hot_shape_owner_drops=0
virtio_hot_shape_submit_calls=238 virtio_hot_shape_make_room_calls=238
virtio_hot_shape_make_room_stalls=204
virtio_hot_shape_make_room_wait_us=2116554
virtio_hot_shape_make_room_max_wait_us=25001
virtio_hot_shape_make_room_depth_max=1
virtio_hot_shape_make_room_count_max=1
virtio_hot_shape_make_room_wait_count_max=1
virtio_hot_shape_posted=238 virtio_hot_shape_post_count_max=1
virtio_hot_shape_retired=238 virtio_hot_shape_retire_us=354484
virtio_hot_shape_retire_max_us=16980
virtio_hot_shape_failures=0 virtio_hot_shape_mixed=0
QEMU trace: ctx_submit=655 set_scanout=81 res_flush=81 fence_ctrl/fence_resp=655/655
```

Interpretation: the stats-only path is the low-perturbation GLX bottleneck
metric: FPS matches the trace-disabled control range while `fbstat` still proves
the single-outstanding hot-shape pattern. A follow-up audit found a source-level
contract mismatch in `virtio_gpu_submit_trace_emit()`: once the emit function
was entered, the `virtio-gpu-submit-depth-probe` printf was not directly gated
by `virtio_gpu_submit_hot_shape_trace=1`. The stats-only run stayed quiet
because emit returns before that point, and command-line key matching is exact,
but the kernel now explicitly gates the depth-probe printf with
`hot_shape_trace`. This preserves stats-only evidence as counters-only and keeps
serial depth-probe output owned by the hot-shape trace flag.

Depth-2 low-noise hot-shape stats candidate:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T033807Z-x11-glx-fps-depth2-hot-shape-stats-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
cmdline used virtio_gpu_async_submit_depth=2 virtio_gpu_submit_hot_shape_stats=1
phase=glx_fps_result status=PASS frames=300 elapsed_seconds=2.279533 fps=131.606
plain_swap_avg_ms=7.339 plain_swap_max_ms=64.228
serial marker recheck: no virtio-gpu-submit-depth-probe, owner-trace, broad
submit-trace, panic, crash, breakpoint trap, or smoke FAIL lines
fbstat hot-shape counters:
virtio_hot_shape_owners=3 virtio_hot_shape_owner_drops=0
virtio_hot_shape_submit_calls=323 virtio_hot_shape_make_room_calls=323
virtio_hot_shape_make_room_stalls=60
virtio_hot_shape_make_room_wait_us=80480
virtio_hot_shape_make_room_max_wait_us=10188
virtio_hot_shape_make_room_depth_max=2
virtio_hot_shape_make_room_count_max=2
virtio_hot_shape_make_room_wait_count_max=2
virtio_hot_shape_posted=323 virtio_hot_shape_post_count_max=2
virtio_hot_shape_retired=323 virtio_hot_shape_retire_us=871607
virtio_hot_shape_retire_max_us=23157
virtio_hot_shape_failures=0 virtio_hot_shape_mixed=0
QEMU trace: ctx_submit=813 set_scanout=109 res_flush=109 fence_ctrl/fence_resp=813/813
```

Interpretation: depth 2 is now proven with the low-noise counter path for the
KDE/Xwayland GLX swap-only reducer. The effective hot-shape depth reached
`2` (`make_room_depth_max`, `count_max`, `wait_count_max`, and
`post_count_max` all `2`), with no failures, mixed shapes, owner drops, crashes,
or serial trace leakage. Compared with the default-depth stats proof, GLX
throughput improved from `47.263` FPS to `131.606` FPS, average swap fell from
`20.836 ms` to `7.339 ms`, and hot-shape make-room stalls fell from `204/238`
to `60/323`. The run hit the probe's 300-frame cap before the 5s target, so a
longer GLX sample can refine the performance number, but the bottleneck
direction is clear. Do not promote depth 2 as a default until the Chromium-video
stress/regression probe passes under the same submit-depth policy.

Chromium-video GBM depth-2 stress artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T084551Z-chromium-video-gbm-depth2-chrome-crash-regression/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=6 capture_first_nonblack_index=6
capture_changed_count=1 capture_post_nonblack_changed_count=0
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 ... role_counts=browser:1,unknown:2,zygote:2
crash_match=[149377723958] pid 161 chrome: breakpoint trap rip=0x4ad85a17
```

Matching no-depth2 GBM trace control:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T084929Z-chromium-video-gbm-trace-control-perf-result-missing/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-missing
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=12 capture_first_nonblack_index=0
capture_changed_count=2 capture_post_nonblack_changed_count=2
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 ... role_counts=browser:1,unknown:2,zygote:2
status=FAIL reason=perf-result-missing
```

Interpretation: depth 2 remains an excellent GLX reducer hint, but it is not
yet a safe kernel behavior change. With the same Chromium GBM preprobe,
sampler, and DRM/fence trace flags, depth 2 introduces a Chromium breakpoint
trap and freezes after the first nonblack frame, while the no-depth2 control
avoids the crash and captures two post-nonblack visual changes but still never
gets the in-page perf result. Keep depth 2 opt-in only. The next Chromium
work should split browser/renderer/GPU role creation and the local video page's
completion signal, not promote queue depth or patch upstream Chromium/Mesa.

The first GLX depth-8 attempt failed before the reducer due to the known KWin
startup crash class and is preserved separately:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-075110-x11-glx-fps-oml-depth8-kwin-startup-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
```

Treat this as a pre-probe/session stability artifact, not GLX timing evidence.

Two non-passing attempts before the final queue-depth pass were preserved:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-063946-x11-present-fps-queue3-unchecked-kwin-startup-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-064057-x11-present-fps-queue3-unchecked-outstanding-fail/
```

The first failed before the reducer due to the known KWin startup crash class.
The second contained a valid host probe result with `outstanding=0`, but the
Expect parser misread `max_outstanding=3` as the primary outstanding field; the
parser now matches the adjacent `issued completed outstanding` fields.

Default KDE regression artifact after the Present FPS harness:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-055748-default-kde-smoke-pass-after-present-fps-harness/
KDE-PLASMA-DESKTOP-SMOKE-DONE
KDE_SMOKE_AGENT_DONE status=PASS
Xwayland KDE wrapper: ... glamor=off effective_glamor=off ... enable_glx=0
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... chromium=1 ... pipewire=1 ... status=PASS
```

The known raw KWin screenshot readback warning remained
`kde_kwin_screenshot_probe result=FAIL detail=low-color-detail`; the overall
default smoke still passed.

A same-command retry before the timing pass hit a known pre-probe KWin startup
crash signature and was preserved separately at:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-014657-x11-glx-fps-timing-kwin-crash/
```

Default KDE regression artifact after the session-probe change:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-053252-default-kde-smoke-pass-after-session-glx-fps/
KDE-PLASMA-DESKTOP-SMOKE-DONE
Xwayland KDE wrapper: ... glamor=off effective_glamor=off ... enable_glx=0
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... chromium=1 ... status=PASS
KDE_SMOKE_AGENT_DONE status=PASS
```

Default KDE regression artifact after the GLX FPS variant harness update:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-052229-default-kde-smoke-pass-after-glx-variant-harness/
KDE-PLASMA-DESKTOP-SMOKE-DONE
Xwayland KDE wrapper: ... glamor=off effective_glamor=off ... enable_glx=0
kde_app_launch_probe ... konsole=1 ... dolphin=1 ... chromium=1 ... status=PASS
kde_process_probe ... kwin=1 plasmashell=1 ... xwayland=1 ... chromium=1 ... status=PASS
kde_kwin_screenshot_probe result=FAIL detail=low-color-detail
KDE_SMOKE_AGENT_DONE status=PASS
```

Remaining success criteria:

- Improve GLX FPS toward the Linux baseline or explain the remaining delta with
  reduced evidence.
- QEMU trace shows Xwayland/client 3D submission comparable in shape and rate
  to the Linux baseline.
- Default KDE smoke stays green and remains nonblack/responsive.

### 2. KDE Performance Parity

Status: open after GLX/Present queue-depth proof; FPS is improved but still
below the Linux KDE GLX baseline.

The current xv6 KDE desktop is visually correct and responsive, and focused
Xwayland GLX context/draw proof now works with automatic policy mapped to
effective `-glamor es`. The deterministic KDE/X11 FPS reducer now records a
direct GLX/virgl result of 93 frames over 5.039396 seconds, or 18.455 FPS, in
the phase-detail pass. The trace-disabled confirmation pass recorded 85 frames
over 5.024329 seconds, or 16.918 FPS, confirming the older trace-heavy FPS
numbers were materially depressed by diagnostic logging. xv6 still does not
match the Linux KDE GLX baseline of 55.638 FPS.

The latest phase-detail pass records `draw_total_ms=3539.314`,
`swap_total_ms=1475.927`, `avg_swap_ms=15.870`,
`gl_issue_total_ms=3537.151`, and `gl_error_total_ms=2.163`, so the draw bucket
is not `glGetError`; it is dominated by the existing GL issue calls. The
`swap-only` variant then reduced draw time to 23.988 ms over the run but still
recorded only 20.914 FPS with `swap_total_ms=4969.928`, strengthening the case
that GLX swap/Present pacing rather than clear-call issue overhead dominates
the remaining FPS gap. The Present queue3 reducer reached 41.691 FPS with three
requests in flight, while the GLX OML queue3 swap-only reducer reached
34.437 FPS with `oml_sbc_issued=173`, `oml_sbc_completed=173`, and
`oml_max_pending_sbc=3`. Parameterized depth-6 queueing moved Present to
52.323 FPS and GLX OML swap-only to 43.815 FPS; GLX depth 6 now exceeds the
prior Present queue3 result but still trails Present depth 6 and the Linux GLX
baseline. Depth-8 queueing then moved raw Present to 54.980 FPS, essentially
matching the Linux GLX control at 55.638 FPS, while GLX OML swap-only only
rose to 44.611 FPS and spent about 4.82s in OML issue/swap MSC time. Queue
depth now explains raw Present pacing; the residual gap sits above raw Present
in GLX/Mesa/Xwayland OML swap issue or throttling. The flush/swap split
variant recorded only 7.406 ms in pre-swap `glFlush` but 4762.982 ms in
`glXSwapBuffersMscOML` issue at depth 8. The issue-state timing variant proved
the sampling path with 134 samples for 134 issued SBCs, 33 post-issue samples
already complete, max post-issue lag 5 SBC, and MSC delta 80 total / 2 max, but
its two extra `glXGetSyncValuesOML` calls per issue were intrusive enough to
drop the run to 26.444 FPS. Sparse issue-state sampling at interval 16 then
recorded 216 frames at 42.552 FPS with 14/216 samples, only 1 sampled
post-issue SBC already complete, max lag 4 SBC, and MSC delta 5 total / 1 max.
The later `swap-interval0-swap-only` plain GLX run accepted swap interval 0 via
EXT, observed the MESA getter move from 1 to 0, and reached 232 frames at
45.971 FPS while ordinary `glXSwapBuffers` still consumed 4965.974 ms of the
run. That keeps the evidence on GLX/Mesa/Xwayland swap-path pacing above raw
Present rather than fd passing/DRI3, pre-swap draw work, OML-only throttling,
or a speculative kernel patch. The GLX-swap ioctl bucket later isolated the
dominant host-visible cost to `DRM_IOCTL_VIRTGPU_EXECBUFFER` on the render
node, and the low-noise `virtio_gpu_submit_trace=1` run preserved 47.770 FPS
while recording cumulative async make-room, wait-progress, post, and op-lock
timing. Because those kernel counters are global and mix desktop clients with
the probe, the next step is per-owner/context or delta attribution around
virtgpu async queue/post and failed execbuffer returns, not a behavior-changing
kernel patch. The current virtgpu fence trace still shows submitted and
responded fences matching 1:1, so raw virtgpu fence starvation remains
unproven. The
remaining `DRM_IOCTL_SYNCOBJ_EVENTFD` probe returns Linux-shaped `ENOENT` for
`handle=0`.

Follow-up diagnostic artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-061409-x11-glx-fps-syncobj-eventfd-diag-pass/
```

Key diagnostic line:

```text
chrome-drm-detail: syncobj-eventfd owner=3:68 ret=-22 handle=0 flags=0x0 point=0 fd=-1 pad=0 fd_is_eventfd=-1 syncobj_exists=-1 state_has_fence=-1 reject_reason=invalid_args
```

Interpretation: the first Xwayland `DRM_IOCTL_SYNCOBJ_EVENTFD` call in this
run is an invalid/feature-probe-shaped request, not a real eventfd arm:
`handle=0` and `fd=-1`. The diagnostic patch intentionally preserves current
errno behavior.

Pre-fix focused reducer artifact:

```text
build-x86_64/drmabitest-syncobj-eventfd-validation-virgl-20260626-030504/
```

Command run inside a no-Weston virgl boot:

```sh
/bin/drmabitest --syncobj-eventfd-validation
```

Result summary:

```text
expect_status=0
rows=12
pass=6
fail=6
skip=0
```

Key rows:

```text
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-22 errno=22 linux_errno=2 status=FAIL
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-22 errno=22 linux_errno=2 status=FAIL
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_eventfd: handle=0 fd=5 ret=-2 errno=2 linux_errno=2 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=1 fd=-1 ret=-22 errno=22 linux_errno=9 status=FAIL
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_non_eventfd: handle=1 fd=6 ret=-22 errno=22 linux_errno=22 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_eventfd: handle=1 fd=5 ret=0 errno=0 linux_errno=0 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-22 errno=22 linux_errno=2 status=FAIL
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-22 errno=22 linux_errno=2 status=FAIL
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_eventfd: handle=0 fd=5 ret=-2 errno=2 linux_errno=2 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=2 fd=-1 ret=-22 errno=22 linux_errno=9 status=FAIL
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_non_eventfd: handle=2 fd=6 ret=-22 errno=22 linux_errno=22 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_eventfd: handle=2 fd=5 ret=0 errno=0 linux_errno=0 status=PASS
```

Interpretation: xv6 validated some eventfd arguments before matching Linux's
syncobj-handle ordering.

Post-fix focused reducer artifact:

```text
build-x86_64/drmabitest-syncobj-eventfd-validation-virgl-20260626-032843-kernel-order-pass/
```

Result summary:

```text
expect_status=0
rows=12
pass=12
fail=0
skip=0
```

Key rows:

```text
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-2 errno=2 linux_errno=2 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-2 errno=2 linux_errno=2 status=PASS
card0:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=1 fd=-1 ret=-9 errno=9 linux_errno=9 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd_minus1: handle=0 fd=-1 ret=-2 errno=2 linux_errno=2 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_invalid_handle_fd0: handle=0 fd=0 ret=-2 errno=2 linux_errno=2 status=PASS
renderD128:DRM_IOCTL_SYNCOBJ_EVENTFD.validation_valid_handle_fd_minus1: handle=2 fd=-1 ret=-9 errno=9 linux_errno=9 status=PASS
```

Interpretation: kernel-side `DRM_IOCTL_SYNCOBJ_EVENTFD` validation ordering now
matches the reducer: invalid syncobj handles return `ENOENT` before fd
validation, and a valid syncobj with `fd=-1` returns `EBADF`. The broader
queued eventfd waiter lifetime concern remains a separate follow-up that needs
its own reducer before behavior-changing cleanup.

Post-fix KDE X11/GLX FPS retry artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-033211-x11-glx-fps-after-syncobj-order-no-display-fail/
```

Result:

```text
KDE-PLASMA-DESKTOP-SMOKE-FAIL x11-glx-fps-session-probe-FAIL exit_status=1
host-x11-egl-smoke: phase=session_probe status=FAIL mode=session probe_mode=glx-fps exit_status=1 reason=no-display-candidate
chrome-drm-detail: syncobj-eventfd owner=3:66 ret=-2 handle=0 flags=0x0 point=0 fd=-1 pad=0 fd_is_eventfd=-1 syncobj_exists=0 state_has_fence=0 reject_reason=syncobj_missing
```

Interpretation: the KDE retry confirms the Xwayland invalid syncobj eventfd
probe now returns Linux-shaped `ENOENT`, but this run failed before GLX/FPS
measurement because the session probe could not open an authorized X display.
It should not be used as FPS evidence.

Session-probe auth diagnostic harness update:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260626-040250-x11-egl-auth-fix-glamor-auto-kwin-startup-crash/
build-x86_64/kde-plasma-desktop-smoke-history/20260626-040447-x11-egl-auth-diagnostics-glamor-off-glx-server-fail/
```

Results:

```text
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
pid 49 kwin_wayland: exception 13 (#GP General Protection) rip=0x7ffffcc2cd9d

host-x11-egl-smoke: diag xwayland_auth label=best pid=71 display=:0 auth_path=(unset) argv=/bin/Xwayland.real -glamor off ... :0 ...
host-x11-egl-smoke: phase=x11_preflight_candidate status=PASS display=:0 exit_status=0
host-x11-egl-smoke: phase=egl_initialize status=FALLBACK reason=eglInitialize egl_error=0x3001 error_name=EGL_NOT_INITIALIZED fallback=glx
host-x11-egl-smoke: diag glx_query_extension result=FAIL present=0 error_base=0 event_base=0
host-x11-egl-smoke: diag glx_client_strings result=PASS vendor=Mesa Project and SGI version=1.4
host-x11-egl-smoke: diag glx_server_strings result=FAIL vendor=(null) version=(null)
host-x11-egl-smoke: diag glx_fbconfigs result=FAIL count=0 ptr=(nil)
host-x11-egl-smoke: phase=session_probe status=FAIL mode=session probe_mode=glx-probe exit_status=1
```

Interpretation: the xv6-owned KDE session probe now records Xwayland auth
discovery, display-specific auth candidates, launch-time `XAUTHORITY`
selection, and GLX fallback phases in `host-gui-host-x11-egl-smoke.log`.
The glamor-auto run still failed before the probe because KWin crashed during
startup, so it is a startup artifact rather than GLX evidence. The glamor-off
control reached Xwayland and proves the current failing surface: no auth file is
present for `-glamor off`, X11 connect succeeds, EGL-on-X11 fails to initialize,
and GLX has client strings but no server extension/version/fbconfigs.

Success criteria:

- KDE smoke passes.
- X11 GL smoke FPS is recorded under xv6.
- QEMU trace counts and renderer logs are captured in a stable artifact.
- Weston/Chromium controls remain green.

### 3. Chromium As Regression / Stress Probe

Status: open only for current reproducible ABI gaps.

Do not reopen old Chromium issues unless a current run reproduces them. Current
Chromium work should focus on:

- Fullscreen YouTube: prove deterministic 720p fullscreen playback and cursor
  motion over a long enough capture window.
- VA/video decode: Mesa Gallium VA is staged, but hardware decode selection is
  not yet proven by Chromium diagnostics. Keep this as an open proof item.
- Normal launcher/minimal flags: continue only from current evidence that
  proves a Linux ABI surface, not from stale first-run/profile artifacts.
- X11 presentation timing should remain separate from native Wayland Chromium
  paths, especially when investigating `GetVSyncParametersIfAvailable()` or
  Xwayland/Present behavior.

Current local-video reducer evidence:

```text
Manual KDE/Chromium inspection artifact:
build-x86_64/manual-vm-history/manual-kde-youtube-inspection-20260626-165821/

Observed: YouTube playback still jittered and stopped. Serial inspection found
no live Chromium process and no /host-gui-wayland-chromium.log, while fbstat
showed virtio async make-room backpressure even with Chromium absent
max_wait_us=592264.

Failed serial-delivery artifacts while making the reducer durable:
build-x86_64/kde-plasma-desktop-smoke-history/20260626T220633Z-chromium-video-launch-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260626T220909Z-chromium-video-script-stage-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260626T221156Z-chromium-video-heredoc-unsupported/
build-x86_64/kde-plasma-desktop-smoke-history/20260626T221548Z-chromium-video-direct-launch-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260626T222146Z-chromium-video-split-launch-timeout/

Passing local fixture artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260626T222906Z-chromium-video-local-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_video=local-video samples=4 url=file:///share/webkit/perf-video.html?asset=perf-1280x800-60fps.mp4&ms=15000&hud=1

Opt-in multiprocess/GPU-enabled stress artifacts:
build-x86_64/kde-plasma-desktop-smoke-history/20260626T223426Z-chromium-video-multiprocess-kwin-crash-misclassified-pass/
  run.log: kwin_wayland exception 13 (#GP), all four frames black
  harness follow-up: success-path KWin crash signatures are now classified as
  KDE-PLASMA-DESKTOP-SMOKE-FAIL kwin-crash-regression.
build-x86_64/kde-plasma-desktop-smoke-history/20260626T223748Z-chromium-video-multiprocess-pass/
  KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video ... chromium_multiprocess=1
  launcher: multiprocess=1, no --disable-gpu, no --in-process-gpu, no --single-process
  frames: samples 00-02 black, sample 03 nonblack mean=0.630504 std=0.254788
```

Interpretation: the reducer now uses xv6-owned short probe launch metadata
(`--chromium-local-video`) instead of sending the long media URL through the
busy KDE serial console. The passing artifact preserves root-level Chromium
launcher logs, exact final argv URL proof, role-derived Chromium process
evidence, QEMU virtio-gpu trace counts, and four captured 1280x800 PPM frames.
The first three frame samples captured the black startup interval; sample 03 is
nonblack (`mean=0.630504`, `std=0.254788`). The next optimization target is
playback smoothness/readback timing rather than command delivery or missing
Chromium launch evidence. The opt-in `KDE_SMOKE_CHROMIUM_MULTIPROCESS=1` lane
now proves the same local fixture can run without Chromium's default
single-process/software fallback flags, but the KWin crash artifact shows that
GPU-enabled stress must remain a separate reducer until repeated runs and
additional timing counters explain the black interval and any compositor crash
surface.

2026-06-27 GBM stress follow-up:

```text
Depth-2 artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T084551Z-chromium-video-gbm-depth2-chrome-crash-regression/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression

No-depth2 trace control:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T084929Z-chromium-video-gbm-trace-control-perf-result-missing/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-missing
```

Both runs prove the fixed GBM/sampler status path: the GBM preprobe records an
explicit `host-egl-gbm-gl-smoke: phase=result status=PASS ...` line and the
status file is completion-only (`status=DONE source=...`), while the sampler
records `kde_app_launch_probe chromium_sample_only=1 ... status=PASS`. The
control avoids the depth-2 `chrome: breakpoint trap`, captures nonblack frames
from sample 0, and records two post-nonblack frame changes, but still misses
the local page perf result. Treat the remaining Chromium gap as role/process
creation plus page playback/result completion evidence, and keep
`virtio_gpu_async_submit_depth=2` opt-in until Chromium stress is green.

2026-06-27 Chromium startup-metric follow-up:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T090536Z-chromium-video-gbm-startup-metrics-capture-static/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
perf_console_count=4
perf_start=... "PERF-VIDEO start asset=perf-1280x800-60fps.mp4 rvfc=true runMs=15000 startupMs=8000"
gpu_init_error_count=3 gpu_config_error_count=2 gpu_exit_error_count=1 gpu_error_pids=372
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=7 capture_first_nonblack_index=5
capture_changed_count=1 capture_post_nonblack_changed_count=0
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 ... render_fd_seen=1 ...

Strict sampler-gate rerun:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T104530Z-chromium-video-gbm-strict-sampler-capture-static/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
perf_console_count=4
perf_start=... "PERF-VIDEO start asset=perf-1280x800-60fps.mp4 rvfc=true runMs=15000 startupMs=8000"
gpu_init_error_count=3 gpu_config_error_count=2 gpu_exit_error_count=1 gpu_error_pids=372
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=6 capture_first_nonblack_index=6
capture_changed_count=1 capture_post_nonblack_changed_count=0
frame_stats: samples 00-05 black; samples 06-11 nonblack mean=0.630504 std=0.254788 with prev_ae_delta=0 after sample 06
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 ... browser_render_fd_seen=1 ...
```

The xv6-owned local fixture now emits startup phase, media state, startup
timeout, media event, and page error logs; the smoke post-evidence parser now
summarizes those lines plus Chromium GPU/EGL initialization errors. Independent
audit found the metric parser no longer false-passes local-video when the page
never emits `PERF-VIDEO start`, and the focused GBM run confirms the remaining
preprobe marker no longer depends on xv6 `sh`'s fake `$?` (`KCVEGLDONE` plus a
completion-only status file). A follow-up read-only audit found one remaining
sampler-status false-pass risk: the post-evidence sampler gate could fall back
to `kde_chromium_process_evidence sampler end status=PASS` when the explicit
sampler log result was absent. The harness now requires the explicit
`kde_app_launch_probe chromium_sample_only=1 ... status=PASS` sampler-log line;
the 20260627T104530Z rerun proves that stricter gate while reproducing the same
capture-static failure. Rootfs refresh passed before the earlier startup-metric
run.

Interpretation: the current no-depth2 GBM failure is a real playback/progress
gap, not command delivery, fake shell status, missing Chromium launch, or GBM
preprobe failure. Chromium reaches the page and logs `before-src`, `after-src`,
`start`, and `before-play`; it never logs `playing`, `play-resolved`,
`startup`, `tick`, or `RESULT`. In the strict-gate boot, GPU PID 372 fails EGL
setup with `eglCreateContext ES 3.0 failed`, two `No suitable EGL configs`
messages, `CollectGraphicsInfo failed`, and GPU process exit. The GBM preprobe
itself passes only via configless/surfaceless fallback while Chromium-shaped
pbuffer/native-window pbuffer config counts are zero and window/surfaceless
counts are nonzero. Next reducer should continue with the xv6-owned EGL
config/context-shape probe that mirrors Chromium's offscreen/surface
requirements under the same rootfs, before changing kernel queueing or patching
Mesa/Chromium.

2026-06-27 Chromium EGL config-shape follow-up:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T091736Z-chromium-video-gbm-config-shape-chrome-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
pbuffer_es3_count=0 pbuffer_es2_count=0
native_window_pbuffer_es3_count=0 native_window_pbuffer_es2_count=0
window_es3_count=40 window_es2_count=40
surfaceless_es3_count=40 surfaceless_es2_count=40
capture_nonblack_count=12 capture_first_nonblack_index=0
capture_changed_count=0 capture_post_nonblack_changed_count=0
crash_match=[168635490458] pid 263 chrome: breakpoint trap rip=0x4ad85a17
QEMU trace: ctx_submit=79 set_scanout=24 res_flush=24 fence_ctrl/fence_resp=79/79 res_create_3d=247
```

The updated xv6-owned GBM probe now records Chromium-shaped `eglChooseConfig`
counts for ES3/ES2 RGB888+alpha pbuffer, window+pbuffer, window-only, and
`EGL_DONT_CARE` surface profiles. Build verification passed through
`host-gui-runtime`; `rootfs-refresh` passed before the focused VM run. Audit
found no parser or upstream-source issue; the broad GBM preprobe remains the
existing intentional fatal gate, while the new Chromium profile rows are
diagnostic by default.

Interpretation: the GBM/Mesa stack exposes usable ES3 window configs and
configless/surfaceless context creation, but no Chromium-shaped pbuffer or
window+pbuffer config at all. That directly explains Chromium's recurring
`No suitable EGL configs` / GPU info collection failure class without changing
Mesa or Chromium. The no-depth2 stress lane is also not stable: this run hit
the same Chromium breakpoint trap class previously seen in the depth-2
experiment, with all 12 captured frames nonblack but byte-identical. Do not
promote queue-depth behavior. The next reducer should stay xv6-owned and split
which Chromium EGL path is mandatory: either run a tiny program that attempts
Chromium's pbuffer/offscreen context sequence and records the failing attribute
set, or add a rootfs/sysroot/Mesa-data comparison against Linux to explain why
the virgl GBM display advertises window-only configs under xv6.

2026-06-27 Chromium EGL context-attempt follow-up:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T093219Z-chromium-video-gbm-attempts-kwin-startup-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
pid 49 kwin_wayland: exception 13 (#GP General Protection) rip=0x7ffffd25328b
kde-session: kwin exited before Wayland socket status=11
host_egl_gbm_log_present=0 host_egl_gbm_status_present=0
egl_gbm_chromium_attempts=1
status=FAIL reason=not-launched

build-x86_64/kde-plasma-desktop-smoke-history/20260627T093437Z-chromium-video-gbm-attempts-kwin-wayland-timeout/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
kde-session: /dev/shm/xdg-runtime-root/wayland-0 not visible after compositor grace
kde-session: KWin startup attempt=1 failed
host_egl_gbm_log_present=0 host_egl_gbm_status_present=0
egl_gbm_chromium_attempts=1
status=FAIL reason=not-launched
QEMU trace: ctx_submit=1 set_scanout=3 res_flush=3 fence_ctrl/fence_resp=1/1 res_create_3d=1
```

The xv6-owned GBM probe now has an opt-in context-attempt matrix
(`KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_ATTEMPTS=1`) that tries ES3/ES2 Chromium
profiles across pbuffer, window+pbuffer, window-only GBM surface, window-only
surfaceless, and surfaceless paths. It is diagnostic by default, and the
Chromium-video post-evidence records both compact per-profile results and raw
`egl_chromium_context_attempt` lines when the preprobe runs. Independent audit
confirmed the attempt matrix is opt-in and does not perturb the broad GBM smoke
unless explicitly requested.

Verification is still blocked at KDE startup, not at Chromium or the new GBM
probe: two focused retries with the attempt matrix enabled failed before the
preprobe ran, leaving no `host-egl-gbm-gl-smoke` log/status and no Chromium
launch. Treat these artifacts as KWin startup evidence only. The next step is
to recover a stable KDE startup lane or run the same xv6-owned GBM attempt
matrix in a smaller non-KDE harness before making behavior-changing DRM or
kernel patches.

2026-06-27 direct GBM context-attempt reducer:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T105715Z-host-egl-gbm-context-attempts-direct-pass/
HOST-EGL-GBM-GL-SMOKE-PROOF-PASS attempts=10 pass=6 fail=4
status=PASS attempts=10 pass=6 fail=4 source=host-egl-gbm-gl-smoke-log
host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
host-egl-gbm-gl-smoke: phase=egl_config_summary status=PASS api=gles3 total=200 renderable_match=200 es3_renderable=200 pbuffer=0 window=200 rgb888=160 exact_pbuffer_rgb=0 es2_pbuffer_rgb=0 es3_window_rgb=160
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-egl-gbm-gl-smoke-launcher: phase=child_exit status=PASS exit_status=0
```

The new no-KDE `desktop=0` proof lane stages a guest script, leaves `/etc/startup`
empty, and gates only on durable copied probe logs, not xv6 `sh` `$?` status.
Independent audit found no fake-status, Weston, or missing-row gate issue before
the VM run. A first retry exposed an xv6 shell portability issue with `2>&1`;
the harness now uses the launcher-owned `/tmp/host-gui-host-egl-gbm-gl-smoke.log`
and completion-only status instead.

The clean direct result confirms the Chromium-shaped GBM matrix outside KDE:
pbuffer and window+pbuffer profiles fail because `eglChooseConfig` returns zero
matching configs (`reason=no_config`, `egl_error=EGL_SUCCESS`), while window-only
GBM, window-only surfaceless, and pure surfaceless ES3/ES2 context creation all
pass. This matches the earlier Chromium-video symptom where the GPU process
reported no suitable EGL configs, but it removes KWin startup and Chromium launch
as dependencies for this specific fact. The next reducer should log Chromium's
actual EGL config attributes in the KDE/Chromium run, or compare the same GBM
config table against a Linux reference, before any kernel/DRM behavior change.

2026-06-27 Chromium-video GBM sampler/capture rerun:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T114255Z-chromium-video-gbm-sampler-startup-timeout/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-sampler-startup-timeout
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
chromium_video_post_evidence sampler_status=status=DONE source=kde-app-launch-probe-log
chromium_video_post_evidence sampler_result=kde_app_launch_probe chromium_sample_only=1 ... status=PASS
chromium_video_post_evidence capture_status=missing guest_path=/kde-chromium-video-capture.status

build-x86_64/kde-plasma-desktop-smoke-history/20260627T114923Z-chromium-video-gbm-pipewire-startup-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
pid 66 pipewire-pulse: fatal page fault

build-x86_64/kde-plasma-desktop-smoke-history/20260627T115119Z-chromium-video-gbm-capture-black/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-black
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
chromium_video_post_evidence launch_evidence_status=PASS
chromium_video_post_evidence sampler_status=status=DONE source=kde-app-launch-probe-log
chromium_video_post_evidence capture_status=status=DONE samples=12
chromium_video_post_evidence capture_present_count=12 capture_expected_count=12
chromium_video_post_evidence capture_nonblack_count=0 capture_changed_count=0
chromium_video_post_evidence process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 render_fd_seen=1 browser_render_fd_seen=1 ...
frame_stats: samples 00-11 present=1 bytes=3072016 mean=0 stddev=0 min=0,max=0 black
QEMU trace: ctx_submit=5 set_scanout=4 res_flush=4 fence_ctrl/fence_resp=5/5 res_create_3d=3
```

The focused rerun first exposed another serial-noise harness issue: the
background sampler's `KCVSSTART` echo can be interleaved with DRM/KDE serial
logs and missed by Expect, even though the durable sampler status/log later
show `DONE` and explicit sampler PASS. The harness no longer gates sampler
startup on that noisy marker; it starts the sampler, then polls the existing
completion-only status script after capture/result wait until `status=DONE`.
Independent read-only audit found this preserves the downstream explicit
sampler-log PASS requirement and removes the timing-lucky false-fail path. A
separate retry hit an early `pipewire-pulse` page fault before the reducer and
is startup noise only.

Interpretation: with the sampler/capture harness fixed, the no-depth2
Chromium-video GBM lane reaches a real failure: Chromium launches, owns a render
fd, the sampler passes, the GBM preprobe passes only through
configless/surfaceless fallback, and the capture script records 12 full-size
frames, but every frame is black. No renderer or GPU process role is observed
in the sampler evidence. This is now a Chromium process/rendering-progress gap,
not fake shell status, missing launch, missing sampler completion, GBM preprobe
failure, or frame-capture plumbing. The next reducer should enable the xv6-owned
Chromium EGL trace in this same lane, or add a role/child-process probe focused
on why renderer/GPU roles never become visible, before any kernel behavior
change.

2026-06-27 Chromium EGL trace instrumentation:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T110850Z-chromium-egl-trace-kwin-startup-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
pid 49 kwin_wayland: exception 13 (#GP General Protection) rip=0x7ffffd78f5e5
chromium_video_post_evidence chromium_egl_trace=1
chromium_video_post_evidence chromium_egl_trace_log_present=0
chromium_video_post_evidence chromium_egl_trace_summary=available=0
chromium_video_post_evidence host_egl_gbm_log_present=0
chromium_video_post_evidence status=FAIL reason=not-launched
QEMU trace: ctx_submit=1 set_scanout=3 res_flush=3 fence_ctrl/fence_resp=1/1 res_create_3d=1
```

Added an xv6-owned opt-in Chromium EGL trace preload staged under
`/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so`. The
Chromium launcher only enables it when `WAYLAND_CHROMIUM_EGL_TRACE=1`, and the
KDE smoke harness exposes that through `KDE_SMOKE_CHROMIUM_EGL_TRACE=1` for
Chromium-video runs. The preload records `eglChooseConfig`,
`eglCreateContext`, `eglCreatePbufferSurface`, and `eglCreateWindowSurface`
attributes/results to `/chromium-egl-trace.log` without consuming `eglGetError`.
Independent audit confirmed default runs are not preloaded, the staged path
matches the launcher, post-evidence is gated on the opt-in log, and a rootfs
refresh was required. `host-gui-runtime` and `rootfs-refresh` completed, and the
refreshed image contains both `/bin/wayland-chromium` and the new preload.

The first focused trace run did not reach Chromium: KWin crashed before the
GBM preprobe or Chromium launch, leaving only missing-artifact placeholders.
This run is KWin startup evidence, not Chromium EGL evidence. Next steps:
recover a stable KDE startup lane for the trace, or run a small direct
preload sanity check against an xv6-owned EGL probe to validate the hook while
continuing KWin startup reduction separately.

2026-06-27 Chromium-video GBM EGL trace rerun:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T115938Z-chromium-video-gbm-egl-trace-sync-timeout/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-sync-timeout
chromium_video_post_evidence chromium_egl_trace=1
chromium_video_post_evidence chromium_egl_trace_log_present=1
chromium_video_post_evidence chromium_egl_trace_summary=available=1 init=1 choose=0 choose_zero=0 create_context=0 create_pbuffer=0 create_window=0
chromium_video_post_evidence gpu_init_error_count=6 gpu_config_error_count=4 gpu_exit_error_count=2 gpu_error_pids=372,457
chromium_video_post_evidence gpu_error_first=... eglCreateContext ES 3.0 failed with error EGL_SUCCESS
chromium_video_post_evidence gpu_error_last=... Exiting GPU process due to errors during initialization
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status: status=DONE source=host-egl-gbm-gl-smoke-log
chromium_video_post_evidence egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
chromium_video_post_evidence capture_status=status=DONE samples=12
chromium_video_post_evidence capture_nonblack_count=11 capture_changed_count=1 capture_post_nonblack_changed_count=0
chromium_video_post_evidence process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 render_fd_seen=1 browser_render_fd_seen=1 ...
chromium_video_post_evidence perf_result=... PERF-VIDEO RESULT fail startup-timeout phase=play-promise ...
chromium_video_post_evidence status=FAIL reason=chromium-egl-trace-no-choose
QEMU trace: ctx_submit=401 set_scanout=330 res_flush=330 fence_ctrl/fence_resp=401/401 res_create_3d=278
```

This trace rerun reached Chromium and post-capture evidence, but the final sync
marker was serial-interleaved with desktop logs and the harness reported
`chromium-video-sync-timeout` after the actionable post-evidence had already
been written. The Chromium trace preload initialized in a late Chrome child
(`pid=522`) but did not observe `eglChooseConfig`, so post-evidence correctly
marks the trace as `chromium-egl-trace-no-choose`. In the same run, Chromium's
own log repeats `No suitable EGL configs found`, while the xv6-owned GBM
preprobe again proves ES3 works and Chromium-shaped pbuffer/window+pbuffer
configs are absent. Frame capture improved from all black to one nonblack static
image after the first sample, but playback still times out and does not advance.
The process sampler still sees no renderer or GPU role even though the browser
owns a render fd.

A verification rerun after switching the final sync marker to repeated async
tokens still ended in `chromium-video-sync-timeout`:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T120557Z-chromium-video-gbm-egl-trace-sync-timeout-rerun/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-sync-timeout
chromium_video_post_evidence chromium_egl_trace_summary=available=1 init=1 choose=0 choose_zero=0 create_context=0 create_pbuffer=0 create_window=0
chromium_video_post_evidence egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
chromium_video_post_evidence capture_status=status=DONE samples=12
chromium_video_post_evidence capture_nonblack_count=12 capture_changed_count=0 capture_post_nonblack_changed_count=0
chromium_video_post_evidence perf_result=... PERF-VIDEO RESULT fail startup-timeout phase=play-promise ...
chromium_video_post_evidence perf_playing=... time=6.015 ... presented=195 decoded=368 dropped=229 ...
chromium_video_post_evidence status=FAIL reason=chromium-egl-trace-no-choose
run.log: sync; for i in 1 2 3 4 5; do echo KCVSYNCDONE; done
QEMU trace: ctx_submit=361 set_scanout=307 res_flush=307 fence_ctrl/fence_resp=361/361 res_create_3d=233
```

The rerun proves the late final `sync` can fail to return within the harness
timeout after sampler/capture completion; it is not only marker interleaving.
The harness now records `chromium_video_post_evidence final_sync_status=...`
and lets a late final-sync timeout fall through to normal post-evidence
classification, while keeping sync crash and EOF fatal. That keeps the durable
Chromium reducer reason visible (`chromium-egl-trace-no-choose`) and records
the sync timeout as supporting evidence instead of masking the graphics result.
A first verification of that classification path exposed one more xv6 `sh`
portability bug in the marker command:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T121155Z-chromium-video-gbm-egl-trace-no-choose-final-sync-evidence/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chromium-egl-trace-no-choose
chromium_video_post_evidence final_sync_status=timeout
run.log: sync; for i in 1 2 3 4 5; do echo KCVSYNCDONE; done
run.log: exec for failed / exec do failed / exec done failed
```

The top-level classification now preserves the Chromium post-evidence reason,
but the repeated marker command must remain xv6-sh-compatible. The harness uses
plain repeated `echo KCVSYNCDONE` commands rather than a shell `for` loop.

2026-06-27 KWin startup ELF-tail reducer:

```text
build-x86_64/kf5coreaddons-elf-tail-probe/history-20260627T100414Z-pass-zero-tail-poweroff-hang/
KF5_ELF_TAIL_PROBE_DLOPEN:libKF5CoreAddons.so.5 handle=0x400864c0
KF5_ELF_TAIL_PROBE_ELF_LOAD:index=3 label=static_plugin_a vaddr=0xb5d68 filesz=0x32c8 memsz=0x36d0 tail=[0xb9030,0xb9438) offset=0xb9298 contains=1
KF5_ELF_TAIL_PROBE_ELF_LOAD:index=3 label=static_plugin_b vaddr=0xb5d68 filesz=0x32c8 memsz=0x36d0 tail=[0xb9030,0xb9438) offset=0xb92a0 contains=1
KF5_ELF_TAIL_PROBE_SLOT:static_plugin_a status=PASS addr=0x00007fffff4aa298 offset=0xb9298 zero=1 path_poison=0 bytes=00000000000000000000000000000000
KF5_ELF_TAIL_PROBE_SLOT:static_plugin_b status=PASS addr=0x00007fffff4aa2a0 offset=0xb92a0 zero=1 path_poison=0 bytes=00000000000000000000000000000000
KF5_ELF_TAIL_PROBE_PASS
```

The new xv6-owned `kf5coreaddons-elf-tail-probe` reproduces the suspected
KWin crash library in isolation under poison pressure, validates that both
static-plugin slots sit inside the writable `PT_LOAD` zero-fill tail, then
reads those slots after `dlopen("libKF5CoreAddons.so.5")`. Independent audit
rejected an earlier version that only failed on `/x86_64-` bytes and trusted
hard-coded offsets; the current probe now requires all-zero slot bytes and
validates the offsets against the loaded ELF layout before reporting PASS.

Interpretation: the bad `/x86_64-` pointer from the KWin #GP artifact does not
reproduce with a clean isolated `libKF5CoreAddons` load. The file-backed ELF
tail zero-fill path works for this reducer, including the final writable map
for inode 15646 at file offset `0xb8000`. Do not patch the ELF-tail mapping
path from this evidence alone. The next KWin startup reducer should widen from
the isolated library toward the Qt/KF static-plugin registration path and
plugin graph that KWin exercises during startup.

2026-06-27 KWin global singleton-slot reducer:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T112500Z-kwin-global-slot-proof-pass/
KWIN-GLOBAL-SLOT-PROOF-PASS
KWIN_GLOBAL_SLOT_PROBE_SYMBOL:workspace_self status=FOUND index=8057 value=0x5e1168 size=8 shndx=26
KWIN_GLOBAL_SLOT_PROBE_RELOC:workspace_self status=FOUND offset=0x5de070 type=6 addend=0
KWIN_GLOBAL_SLOT_PROBE_SLOT:workspace_self status=PASS ... slot_value=0x0000000000000000 slot_zero=1 slot_path_poison=0 ... got_matches=1
KWIN_GLOBAL_SLOT_PROBE_SLOT:wayland_server_self status=PASS ... slot_zero=1 ... got_matches=1
KWIN_GLOBAL_SLOT_PROBE_SLOT:cursors_self status=PASS ... slot_zero=1 ... got_matches=1
KWIN_GLOBAL_SLOT_PROBE_PASS
```

The latest KWin startup #GP at
`20260627T110850Z-chromium-egl-trace-kwin-startup-crash` mapped to
`QObjectPrivate::connectImpl()` from
`KWin::XdgActivationV1Integration::XdgActivationV1Integration()` during
`WaylandServer::init()`. Disassembly showed the faulting Qt sender pointer came
through libkwin's `KWin::Workspace::_self` GLOB_DAT path. The new xv6-owned
`kwin-global-slot-probe` validates, under `/x86_64-` poison pressure, that
`Workspace::_self`, `WaylandServer::s_self`, and `Cursors::s_self` are present
in libkwin `.dynsym`, have `R_X86_64_GLOB_DAT` relocations pointing to their
runtime slots, sit inside the final writable `PT_LOAD` zero-fill tail, and read
as all zero immediately after `dlopen("libkwin.so.5")`. The direct proof boots
with `desktop=0` and an empty startup file, and its guest status file is
completion-only; host analysis requires explicit probe PASS rows.

Interpretation: isolated libkwin load, BSS tail zero-fill, and GLOB_DAT
relocation for the exact `_self` slot implicated by the Qt `connectImpl` crash
are clean. Do not patch libkwin ELF-tail or generic BSS mapping from this
evidence. The next reducer should widen one step later into KWin's Qt/KF plugin
graph or startup constructors/lifetime path, where `Workspace::_self` is
supposed to become a real `QObject` before `XdgActivationV1Integration` connects
to it.

2026-06-27 KWin Qt/KF plugin-graph reducer:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T113957Z-kde-qt-plugin-graph-proof-pass/
KDE-QT-PLUGIN-GRAPH-PROOF-PASS
KDE_QT_PLUGIN_GRAPH_PROBE_DLOPEN:phase=qtcore ... status=PASS
KDE_QT_PLUGIN_GRAPH_PROBE_DLOPEN:phase=breeze_style ... status=PASS
KDE_QT_PLUGIN_GRAPH_PROBE_DLOPEN:phase=kwin_nightcolor ... status=PASS
KDE_QT_PLUGIN_GRAPH_PROBE_DLOPEN:phase=libkwin ... status=PASS
KDE_QT_PLUGIN_GRAPH_PROBE_KF5_TAIL:phase=libkwin label=static_plugin_a status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_KF5_TAIL:phase=libkwin label=static_plugin_b status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_KWIN_SELF:phase=libkwin label=workspace_self status=PASS ... addr_stable=1 value=0x0000000000000000 zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_KWIN_SELF:phase=libkwin label=wayland_server_self status=PASS ... addr_stable=1 value=0x0000000000000000 zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_KWIN_SELF:phase=libkwin label=cursors_self status=PASS ... addr_stable=1 value=0x0000000000000000 zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_RESULT status=PASS phases=35
KDE_QT_PLUGIN_GRAPH_PROBE_PASS
```

The xv6-owned `kde-dlopen-probe --kwin-plugin-graph` widens the KWin startup
reducer from isolated `libkwin` to the Qt/KF/Breeze/KWin plugin graph under
`/x86_64-` poison pressure, while keeping all handles open and requiring
explicit phase PASS rows. The direct proof boots with `desktop=0` and an empty
startup file; the guest status is completion-only, and host analysis rejects
any probe FAIL row or missing phase row. Independent audit found no source-level
blockers after `rootfs-refresh`; the refreshed image was checked for the graph
probe marker before running the VM proof.

Interpretation: plain `dlopen()` of the Qt/KF/Breeze/KWin plugin graph does not
reproduce the KWin startup #GP, and it keeps the KF5CoreAddons tail slots and
KWin `_self` singleton slots clean through `libkwin`. Do not patch generic
loader zero-fill, KF5 static-plugin tail handling, or KWin singleton relocation
from this evidence. The next KWin startup reducer needs to move past
dlopen-only graph loading into constructor/lifetime behavior around
`Workspace::_self`, `WaylandServer::init()`, and the
`XdgActivationV1Integration` `QObject::connect` path.

2026-06-27 Chromium-video GBM rerun exposed an earlier KWin/PCRE2 startup
fault before the Chromium reducer could run:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T125311Z-chromium-video-gbm-kwin-ready-timeout/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-timeout
chromium_video_post_evidence status=FAIL reason=not-launched
chromium_video_post_evidence final_sync_status=not-run
host_egl_gbm_status=missing guest_path=/host-gui-host-egl-gbm-gl-smoke.status
kde-chromium-sampler.status=missing guest_path=/kde-chromium-sampler.status
pid 49 kwin_wayland: exception 13 (#GP General Protection) rip=0x7fffff6116f9
exception-ip: file_off=0x1b6f9 ... /usr/lib/x86_64-linux-gnu/libpcre2-16.so.0
kde-exception-mem rdi/r12: "/usr/lib/x86_64-/usr/lib/x86_64-"
QEMU trace: ctx_submit=1 set_scanout=3 res_flush=3 fence_ctrl/fence_resp=1/1
```

`addr2line` maps `0x1b6f9` to `pcre2_dfa_match_16`, and disassembly shows the
faulting instruction dereferencing PCRE2's internal allocator/free-list node.
The bad register value is ASCII path poison where PCRE2's final writable
`PT_LOAD` zero-tail globals should hold zeroed allocator state. That makes this
run a KWin startup/ELF-data-lifetime reducer, not Chromium playback evidence.

The plugin-graph proof was widened to watch those exact PCRE2 writable-tail
slots (`0x8b250` allocator list and `0x8b260` mutex area) at every checkpoint:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T125804Z-kde-qt-plugin-graph-pcre2-tail-proof-pass/
KDE-QT-PLUGIN-GRAPH-PROOF-PASS
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=qtcore label=allocator_list status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=kf5coreaddons label=allocator_mutex status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=libkwin label=allocator_list status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=libkwin label=allocator_mutex status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_KF5_TAIL:phase=libkwin label=static_plugin_a status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_KWIN_SELF:phase=libkwin label=workspace_self status=PASS ... value=0x0000000000000000
KDE_QT_PLUGIN_GRAPH_PROBE_RESULT status=PASS phases=35
```

Interpretation: PCRE2 final writable-tail zero-fill is clean through the
dlopen-only Qt/KF/Breeze/KWin plugin graph under poison pressure, including the
two exact PCRE2 allocator slots implicated by the latest KWin #GP. Do not patch
generic ELF BSS/tail mapping from this evidence alone. The next reducer should
move one step later than dlopen-only graph loading into real KWin startup
constructor/activity paths that exercise `QRegularExpression`/PCRE2 during
`Workspace` and `WaylandServer` initialization.

The same reducer was extended one step further to exercise PCRE2's 16-bit DFA
matcher immediately after the plugin graph and `libkwin` load:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T131258Z-kde-qt-plugin-graph-pcre2-stress-proof-pass/
KDE-QT-PLUGIN-GRAPH-PCRE2-STRESS-PROOF-PASS
status=DONE source=kde-qt-plugin-graph-pcre2-stress-probe
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=pcre2-stress-before label=allocator_list status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=pcre2-stress-before label=allocator_mutex status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=pcre2-stress-after label=allocator_list status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=pcre2-stress-after label=allocator_mutex status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_STRESS:phase=post-libkwin status=PASS loops=64 matches=64 last_rc=1 total_us=4157 avg_us=64
KDE_QT_PLUGIN_GRAPH_PROBE_PASS
```

Interpretation: direct PCRE2 `pcre2_dfa_match_16` activity after the dlopen-only
Qt/KF/Breeze/KWin plugin graph also leaves the implicated allocator-list and
mutex slots zero and poison-free. That lowers the odds that the KWin #GP is
caused by a generic PCRE2 final-LOAD zero-fill or standalone PCRE2 DFA ABI bug.
The next reducer should instantiate the smallest real KWin startup path that
constructs the objects around `WaylandServer::init()`, `Workspace`, and
`XdgActivationV1Integration`, rather than widening generic loader patches.

2026-06-27 current-image rerun after the Chromium harness parser fix reproduced
the same split:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T211837Z-chromium-surfaceless-egl-angle-opengles-kwin-pcre2-gp-startup-crash-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
kwin_wayland: exception 13 (#GP General Protection) rip=0x7fffff60d6f9
exception-ip: file_off=0x1b6f9 ... /usr/lib/x86_64-linux-gnu/libpcre2-16.so.0
faulting instruction: pcre2_dfa_match_16+0x15d9, mov 0x20(%rax),%rdx
PCRE2 allocator_list slot=0x8b250, allocator_mutex slot=0x8b260
kde-exception-mem rdi/r12: "/usr/lib/x86_64-/usr/lib/x86_64-"
chromium_video_post_evidence status=FAIL reason=not-launched

build-x86_64/kde-qt-plugin-graph-pcre2-stress-proof/history-20260627T212036Z-pass-noimage/
KDE-QT-PLUGIN-GRAPH-PCRE2-STRESS-PROOF-PASS
KDE_QT_PLUGIN_GRAPH_PROBE_RESULT status=PASS phases=35
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=libkwin label=allocator_list status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=libkwin label=allocator_mutex status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=pcre2-stress-after label=allocator_list status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_TAIL:phase=pcre2-stress-after label=allocator_mutex status=PASS ... zero=1 path_poison=0
KDE_QT_PLUGIN_GRAPH_PROBE_PCRE2_STRESS:phase=post-libkwin status=PASS loops=64 matches=64 last_rc=1 total_us=4765 avg_us=74
```

Interpretation: the latest full KDE attempt again corrupts or observes PCRE2's
allocator-list global as path-poison only during real KWin startup, while the
current-image dlopen graph plus direct DFA stress keeps the same slots clean.
This is still reducer evidence against a broad PCRE2 source patch or generic
ELF zero-fill patch. The next smallest useful probe should run a controlled
KWin startup-adjacent path that creates the `QCoreApplication`/KWin objects
needed for `WaylandServer::init()` and `Workspace` enough to bracket the first
write to PCRE2 slot `0x8b250`, preferably with PCRE2-tail checks before and
after each KWin initialization phase.

The focused Chromium-video GBM reducer was retried after the GBM/sampler harness
status audit. It still did not reach Chromium; KWin aborted earlier during
startup with a C++ allocation failure:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T131718Z-chromium-video-gbm-kwin-bad-alloc-startup-fail/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
terminate called after throwing an instance of 'std::bad_alloc'
what():  std::bad_alloc
kde-session: kwin exited before Wayland socket status=6
VmPeak=136128 kB VmSize=136128 kB VmRSS=17104 kB VmData=2996 kB
chromium_video_post_evidence final_sync_status=not-run
chromium_video_post_evidence status=FAIL reason=not-launched
QEMU trace: ctx_submit=1 set_scanout=3 res_flush=3 fence_ctrl/fence_resp=1/1
```

Interpretation: this is another KWin startup blocker, not Chromium playback or
GBM/sampler evidence. The low RSS/VmData at abort makes a simple guest-memory
exhaustion explanation unlikely. The next reducer should trace the failing
allocation path from an xv6-owned KWin startup wrapper or preload, correlating
`operator new`/`malloc`/`mmap`/`brk` failures with the already identified KWin
startup object path, before any kernel memory-management behavior change.

The KWin startup allocation trace was added as an xv6-owned, opt-in preload
staged in the rootfs and enabled only by `kde_kwin_alloc_trace=1`. The wrapper
keeps KDE, Qt, KWin, Xwayland, Mesa, and Chromium upstream-clean, scopes the
preload to the KWin child, and records allocation/mmap/brk failure summaries
plus C++ throw summaries in `/kde-kwin-alloc-trace.log`. Independent audit
passed after the rootfs staging guard was tightened, and the smoke harness was
fixed so the GBM preprobe uses an opaque `KCVEGLDONE` progress marker instead
of fake xv6 `sh` `$?` status. Two teardown-only harness failures from the first
trace attempts are preserved separately:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T133256Z-chromium-video-gbm-kwin-alloc-trace-harness-teardown-fail/
build-x86_64/kde-plasma-desktop-smoke-history/20260627T133626Z-chromium-video-gbm-kwin-alloc-trace-finish-global-fail/
```

The corrected focused Chromium-video GBM reducer completed teardown and
returned to the real playback failure:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T133910Z-chromium-video-gbm-kwin-alloc-trace-capture-static-fail/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
kwin-alloc-trace: phase=summary status=DONE ... alloc_failures=0 mmap_failures=0 brk_failures=0 throw_calls=0 requested_bytes=103814 max_request=73728
chromium_video_post_evidence host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
chromium_video_post_evidence host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log
chromium_video_post_evidence sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
chromium_video_post_evidence capture_present_count=12 capture_expected_count=12
chromium_video_post_evidence capture_nonblack_count=11 capture_changed_count=1 capture_post_nonblack_changed_count=0
chromium_video_post_evidence gpu_init_error_count=6 gpu_config_error_count=4 gpu_exit_error_count=2 gpu_error_pids=374,452
chromium_video_post_evidence gpu_error_first=[374:374:0627/133802.496475:ERROR:ui/gl/gl_context_egl.cc:374] eglCreateContext ES 3.0 failed with error EGL_SUCCESS
chromium_video_post_evidence status=FAIL reason=capture-static
QEMU trace: ctx_submit=64 set_scanout=18 res_flush=18 fence_ctrl/fence_resp=64/63 res_create_3d=229 xfer_toh_3d=2
```

Frame stats show the first sample black, then 11 identical nonblack frames
(`prev_ae_delta=0` after frame 1). This proves the remaining Chromium-video GBM
gap is not fake shell status, missing sampler completion, missing GBM preprobe,
or a KWin allocation failure in this traced run. It is a Chromium GPU/EGL
initialization/config selection failure followed by static visible output. The
next reducer should stay xv6-owned and compare Chromium's actual
`eglChooseConfig`/`eglCreateContext` request path against the host GBM matrix,
or enable the existing Chromium EGL trace once final-sync handling is robust,
before any kernel DRM behavior change.

2026-06-27 KWin `GLStrictBinding` and GTK/GDK soname-shape fix:

```text
scripts/image/kde-session.c seeds:
GLPlatformInterface=egl
GLStrictBinding=false
OpenGLIsUnsafe=false

build-x86_64/fs.img:
/bin/kde-session contains GLStrictBinding=false
/lib/libgtk-3.so.0 -> libgtk-3.so.0.2409.32
/lib/libgdk-3.so.0 -> libgdk-3.so.0.2409.32
```

The failed surfaceless Chromium candidate before this fix did not reach
Chromium. KWin crashed in `KWin::GLPlatform::driver() const` from
`Options::setGlPreferBufferSwap('a')` while following the driver-default path;
the bad `this` pointer carried ASCII-ish `libQt5DBus` bytes. Seeding
`GLStrictBinding=false` in xv6-owned `kwinrc` data bypasses that fragile
follow-driver branch without patching KWin/Qt. During the rebuild, WebKit's
host GTK/GDK X11 compatibility staging exposed a separate sysroot mismatch:
it overwrote `libgtk-3.so.0` and `libgdk-3.so.0` as regular files after the
GTK port's runtime-link normalizer had run. The staging script now copies the
host GTK/GDK pair under their real versioned names and leaves `.so.0` as
symlinks, preserving WebKit's host ABI intent while keeping future GTK Meson
installs from failing before the normalizer can run.

Post-fix validation and Linux VM proof:

```text
build-x86_64/fs.img:
/lib/libgtk-3.so.0 Type: symlink -> libgtk-3.so.0.2409.32
/lib/libgdk-3.so.0 Type: symlink -> libgdk-3.so.0.2409.32
/bin/kde-dlopen-probe contains KDE_DLOPEN_PROBE_KWIN_HELP_QUIET

build-x86_64/kde-plasma-desktop-smoke-history/20260627T202505Z-chromium-surfaceless-chrome-int3-after-gtk-gdk-fix-noimage/
OpenGL renderer string: virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))
kde-session: KWin and plasmashell are running
KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS
KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=PASS
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
chromium_video_post_evidence capture_present_count=12 capture_expected_count=12
chromium_video_post_evidence capture_nonblack_count=12 capture_changed_count=0
chromium_video_post_evidence chromium_int3_summary=available=1 trap_count=1 pid=195 rip=0x4ad85a17 ip_file_off=0xad84a17
chromium_video_post_evidence status=FAIL reason=chrome-crash-regression
```

This proves the soname-shape mismatch and the KWin `GLStrictBinding` startup
hazard are no longer the blocker in this lane. The KWin loader IFUNC preflight
was also tightened so the probe can run in quiet mode and emit a compact PASS
line; the previous long KWin `--help` dump was vulnerable to busy serial-log
interleaving. Continue from Chromium role/lifecycle and the browser `int3`
site, not from GTK/GDK staging or KWin startup, for this surfaceless candidate.

2026-06-27 GBM preprobe durable-log fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-chromium-video-gbm-no-drm-trace-egl-status-incomplete-int3/
old generated /kcvegl.sh: /bin/host-egl-gbm-gl-smoke --api=gles3 >/host-gui-host-egl-gbm-gl-smoke.log 2>&1
run.log: missing file for redirection
chromium_video_post_evidence host_egl_gbm_status=status=running

build-x86_64/kde-plasma-desktop-smoke-history/20260627T-chromium-video-gbm-no-drm-trace-egl-result-missing-stdout-only/
generated /kcvegl.sh: /bin/host-egl-gbm-gl-smoke --api=gles3 >/host-gui-host-egl-gbm-gl-smoke.log
chromium_video_post_evidence host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log
chromium_video_post_evidence status=FAIL reason=egl-gbm-result-missing
```

The first form was invalid for xv6 `sh` because `2>&1` is parsed as a normal
redirection target, and the second form captured only stdout while the
xv6-owned GBM smoke writes its evidence to stderr. The harness now leaves
`HOST_EGL_GBM_SMOKE_STDIO` unset, lets the xv6-owned launcher capture stdout
and stderr into `/tmp/host-gui-host-egl-gbm-gl-smoke.log`, and copies that file
to `/host-gui-host-egl-gbm-gl-smoke.log` without relying on fake `$?`, `>>`, or
`2>&1`.

Runtime proof:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-chromium-video-gbm-no-drm-trace-harness-fixed-perf-fail/
generated /kcvegl.sh: /bin/host-egl-gbm-gl-smoke --api=gles3; cat /tmp/host-gui-host-egl-gbm-gl-smoke.log >/host-gui-host-egl-gbm-gl-smoke.log
chromium_video_post_evidence host_egl_gbm_result=host-egl-gbm-gl-smoke-launcher: phase=child_exit status=PASS exit_status=0
chromium_video_post_evidence host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
chromium_video_post_evidence host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log
chromium_video_post_evidence sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
chromium_video_post_evidence capture_present_count=12 capture_expected_count=12
chromium_video_post_evidence perf_console_count=41
chromium_video_post_evidence gpu_init_error_count=6 gpu_config_error_count=4 gpu_exit_error_count=2
chromium_video_post_evidence result=fail startup-timeout phase=play-promise startupMs=8000 ready=0 net=2 paused=0 seeking=0 time=0.000 duration=nan ... presented=0 decoded=0 dropped=0 err=0
chromium_video_post_evidence status=FAIL reason=perf-result-fail
QEMU trace: ctx_submit=393 set_scanout=325 res_flush=325 fence_ctrl/fence_resp=393/393 res_create_3d=278 xfer_toh_3d=2
```

Interpretation: the GBM/sampler harness mismatch is fixed. The focused
Chromium-video GBM no-DRM-trace lane now carries durable explicit GBM PASS
evidence and fails later on Chromium playback/GPU-process behavior. Continue
from Chromium EGL/config/context tracing or child-role evidence, not from shell
status or GBM preprobe plumbing.

2026-06-27 harness source-marker cleanup:

- The GBM preprobe completion marker is the opaque `KCVEGLDONE`; it no longer
  uses the fake xv6 `sh` `$?` value.
- The staged sampler and GBM preprobe scripts still write completion-only
  `status=DONE` files. They avoid unsupported xv6 `sh` `if/then/else/fi`
  syntax and record only that the sampler log or GBM log copy was attempted;
  the post-evidence phase classifies missing logs from the actual artifacts.
- The Chromium-video post-evidence verdict remains anchored to explicit probe
  lines: `host-egl-gbm-gl-smoke: phase=result status=PASS` for GBM and
  `kde_app_launch_probe chromium_sample_only=1 ... status=PASS` for the
  sampler. Missing logs or missing PASS lines still fail as
  `egl-gbm-log-missing`, `egl-gbm-result-missing`, `sampler-log-missing`, or
  `sampler-result-missing`.
- Static checks passed:
  `expect -c ... info complete`, `git diff --check --
  scripts/gpu/kde-plasma-desktop-smoke.expect`, and `git -C kernel diff
  --check`.

Follow-up proof before the shell-compatible cleanup:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T184301Z-chromium-video-gbm-shell-if-marker-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-marker-timeout
run.log: exec if failed; exec then failed; exec else failed; exec fi failed
host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log-missing
```

Interpretation: xv6 `sh` does not support `if/then/else/fi`, so the staged
GBM preprobe script could misclassify the copy path before post-evidence and
KDE log interleaving split the long interactive Chromium launch delimiter. The
harness now uses shell-compatible straight-line scripts and a short opaque
launch delimiter (`KCVBEGIN`), while retaining the older long delimiter as a
run-log fallback for historical artifacts.

Follow-up proof after that cleanup:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T184728Z-chromium-video-gbm-sampler-status-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-sampler-status-timeout
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
sampler_status=status=running
```

Interpretation: the run got past the GBM preprobe and the short Chromium launch
delimiter, then failed on the initial sampler-status polling marker after KDE
and DRM logs interleaved with `KCVSSTAT`. That first status ping is now
advisory; the later sampler completion wait and post-evidence gates still
require a real `status=DONE` file and the explicit
`kde_app_launch_probe chromium_sample_only=1 ... status=PASS` line.

Follow-up proof after making the initial sampler ping advisory:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T185101Z-chromium-video-gbm-fbstat-fail-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-fbstat
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 drm_fd_seen=1 render_fd_seen=1 browser_render_fd_seen=1
capture_present_count=7 capture_expected_count=12
capture_nonblack_count=7 capture_first_nonblack_index=0 capture_changed_count=1 capture_post_nonblack_changed_count=0
sampler_status=status=running
```

Interpretation: this run reached Chromium, opened `/dev/dri/renderD128`, and
captured seven present frames, but `fbstat` emitted a partial error line during
sample capture. The Chromium-video capture path now treats `fbstat:` output as
an advisory warning so the reducer can finish collecting sampler/capture
artifacts; post-evidence remains the hard verdict and still fails incomplete
capture, missing sampler completion, or missing sampler PASS.

Focused Chromium-video GBM proof after the capture warning cleanup:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T185610Z-chromium-video-gbm-perf-result-missing-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-missing
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_status=status=DONE samples=12
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=12 capture_first_nonblack_index=0
capture_changed_count=1 capture_post_nonblack_changed_count=1
perf_console_count=4
perf_start=... "PERF-VIDEO start asset=perf-1280x800-60fps.mp4 rvfc=true runMs=15000 startupMs=8000"
gpu_init_error_count=6 gpu_config_error_count=4 gpu_exit_error_count=2 gpu_error_pids=380,459
gpu_error_first=... eglCreateContext ES 3.0 failed with error EGL_SUCCESS
gpu_error_last=... Exiting GPU process due to errors during initialization
egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
pbuffer_es3_count=0 pbuffer_es2_count=0 native_window_pbuffer_es3_count=0 native_window_pbuffer_es2_count=0
window_es3_count=40 window_es2_count=40 surfaceless_es3_count=40 surfaceless_es2_count=40
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 drm_fd_seen=1 render_fd_seen=1 browser_render_fd_seen=1
QEMU trace: ctx_submit=75 set_scanout=22 res_flush=22 fence_ctrl/fence_resp=75/75 res_create_3d=261 xfer_toh_3d=2
```

Interpretation: the GBM/sampler/capture harness path is now proven through
post-evidence. Chromium reaches the local video page and emits `PERF-VIDEO
start`, but not a result. The remaining regression is not the shell marker,
GBM preprobe, sampler, or framebuffer capture path. The current numeric
bottleneck is Chromium's offscreen EGL config shape on xv6/virgl GBM: the
xv6-owned direct GBM probe finds zero ES2/ES3 pbuffer and window+pbuffer
configs while window-only and surfaceless profiles are available, and Chromium's
GPU processes fail `eglCreateContext`/`No suitable EGL configs` during graphics
info collection. Next proof should compare this exact config matrix against the
Linux KVM/virgl control or run the existing Chromium EGL trace with the stable
post-evidence path to capture Chromium's actual `eglChooseConfig` requests
before any behavior-changing DRM/kernel/sysroot change.

2026-06-27 preflight parser/source mismatch cleanup:

- The KWin loader IFUNC preflight now mirrors the QtQml startup preflight shape:
  it captures the explicit `KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS`
  marker from the live stream and falls back to the host smoke run log before
  reporting `kwin-loader-ifunc-missing-help`.
- This fixes the harness/source mismatch where durable probe evidence could
  show PASS while the serial Expect parser reported a missing result. The
  verdict still depends on the explicit PASS marker, not on xv6 `sh` `$?`.
- Static Tcl completeness plus top-level/kernel `git diff --check` passed after
  the cleanup.

2026-06-27 Chromium EGL trace dlsym/no-entrypoint follow-up:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-chromium-video-gbm-egl-trace-dlsym-no-entrypoint-int3/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chromium-egl-trace-no-entrypoint-call
chromium_video_post_evidence chromium_egl_trace=1
chromium_video_post_evidence chromium_egl_trace_log_present=1
chromium_video_post_evidence chromium_egl_trace_summary=available=1 init=1 getproc=0 getproc_traced=0 getproc_choose=0 getproc_create_context=0 getproc_create_pbuffer=0 getproc_create_window=0 dlsym=0 dlsym_traced=0 dlsym_choose=0 dlsym_create_context=0 dlsym_create_pbuffer=0 dlsym_create_window=0 choose=0 choose_zero=0 choose_pbuffer=0 choose_window=0 choose_window_pbuffer=0 create_context=0 create_pbuffer=0 create_window=0
chromium_video_post_evidence host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
chromium_video_post_evidence host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log
chromium_video_post_evidence egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config pbuffer_es3_count=0 pbuffer_es2_count=0 native_window_pbuffer_es3_count=0 native_window_pbuffer_es2_count=0 window_es3_count=40 window_es2_count=40 surfaceless_es3_count=40 surfaceless_es2_count=40
chromium_video_post_evidence sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
chromium_video_post_evidence capture_present_count=12 capture_expected_count=12
chromium_video_post_evidence capture_nonblack_count=3 capture_first_nonblack_index=9
chromium_video_post_evidence capture_changed_count=1 capture_post_nonblack_changed_count=0
chromium_video_post_evidence process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 render_fd_seen=1 browser_render_fd_seen=1 card_fd_seen=0 role_counts=browser:2,unknown:2,zygote:2
run.log: pid 152 chrome: breakpoint trap rip=0x4ad85a17
QEMU trace: ctx_submit=94 set_scanout=42 res_flush=42 fence_ctrl/fence_resp=94/94 res_create_3d=246 xfer_toh_3d=2
```

The EGL trace preload is now inherited and mapped by the sampled Chromium
browser process: process evidence records `WAYLAND_CHROMIUM_EGL_TRACE=1`,
`CHROMIUM_EGL_TRACE=1`, `CHROMIUM_EGL_TRACE_LOG=/chromium-egl-trace.log`, and
`LD_PRELOAD=/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so:...`,
with `/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so` mapped
in the browser. The trace log itself contains only the preload `init` row for a
Chrome child and no `eglGetProcAddress`, `dlsym`, `eglChooseConfig`, or
`eglCreate*` rows before the browser hits the known `int3` site. This narrows
the remaining evidence gap: the failing traced run did not reach a sampled
renderer/GPU EGL initialization path before Chromium trapped.

Interpretation: the current blocker is Chromium role/lifecycle and `int3`
provenance, not GBM preprobe delivery, fake shell status, sampler completion,
or missing trace preload inheritance. The next reducer should add xv6-owned
role/lifecycle evidence around Chromium child creation and exit, or identify
the `0x4ad85a17` browser trap site from the staged Chromium binary, before any
kernel or DRM behavior change.

Follow-up address analysis of the same artifact maps the browser trap to the
staged Chromium binary:

```text
build-x86_64/host-gui-runtime/wayland-chromium/chrome-linux64/chrome
Build ID: a6907ca9b7e2752718629b6c029d897014b580ca
runtime rip=0x4ad85a17, inferred PIE-relative vaddr=0xad85a17, file_offset=0xad84a17
file bytes @0xad84a07: 10 48 8d 45 f8 48 89 38 48 89 c7 e8 a1 dc 52 f8 cc 0f 0b cc ...
objdump @0xad85a00: call 0x32b36b8; int3; ud2
objdump @0x32b36b8: push rbp; mov rsp,rbp; pop rbp; ret
```

The sampled process evidence did not preserve Chrome executable map rows in
this older artifact, so the address mapping above is inferred from the trap
bytes and Chrome PIE layout rather than a same-run executable map line. The
sampler now treats `/chrome-linux64/chrome` and
`/chrome-linux64/chrome_crashpad_handler` maps as interesting so future
Chromium trap artifacts carry direct executable map/file-offset evidence.

Focused verification after adding the sampler map filter reproduced the same
no-entrypoint class and preserved a new artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-chromium-video-gbm-egl-trace-execmap-no-entrypoint-int3/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chromium-egl-trace-no-entrypoint-call
chromium_video_post_evidence chromium_egl_trace_summary=available=1 init=1 getproc=0 getproc_traced=0 getproc_choose=0 getproc_create_context=0 getproc_create_pbuffer=0 getproc_create_window=0 dlsym=0 dlsym_traced=0 dlsym_choose=0 dlsym_create_context=0 dlsym_create_pbuffer=0 dlsym_create_window=0 choose=0 choose_zero=0 choose_pbuffer=0 choose_window=0 choose_window_pbuffer=0 create_context=0 create_pbuffer=0 create_window=0
chromium_video_post_evidence host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
chromium_video_post_evidence sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
chromium_video_post_evidence capture_nonblack_count=9 capture_first_nonblack_index=3 capture_changed_count=1 capture_post_nonblack_changed_count=0
chromium_video_post_evidence process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 crashpad_seen=1 render_fd_seen=1 browser_render_fd_seen=1 ...
run.log int3-ip: addr=0x4ad85a17 map=[0x42f81000-0x50062000) pgoff=0x2f80000 file_off=0xad84a17 ino=4097 size=281758968 name=(unnamed)
run.log int3-stack[3]: addr=0x460d9b19 file_off=0x60d8b19
run.log int3-stack[5]: addr=0x4ad85a36 file_off=0xad84a36
QEMU trace: ctx_submit=75 set_scanout=23 res_flush=23 fence_ctrl/fence_resp=75/75 res_create_3d=243 xfer_toh_3d=2
```

This run confirms the browser owns `/dev/dri/renderD128` but no renderer/GPU
role is observed before the browser trap. The same-run kernel `int3-ip`
diagnostic now supplies the direct executable mapping and file offset. The
sampler map filter briefly matched Chromium `.pak` resource files because their
paths share the `chrome` prefix; the filter was tightened to require the exact
`/chrome-linux64/chrome` path if an executable map name is ever exposed.

2026-06-27 Chromium EGL trace durability follow-up:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-chromium-video-gbm-egl-trace-process-identity-capture-static/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
chromium_egl_trace_summary=available=1 init=6 process=0 choose=2 choose_zero=2 choose_pbuffer=2 create_context=0
gpu_init_error_count=6 gpu_config_error_count=4 gpu_exit_error_count=2 gpu_error_pids=371,452
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_present_count=12 capture_nonblack_count=3 capture_first_nonblack_index=9 capture_changed_count=1 capture_post_nonblack_changed_count=0
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 ... render_fd_seen=1 browser_render_fd_seen=1 ...
chromium_int3_summary=available=1 trap_count=0
```

The xv6-owned trace preload and smoke harness were changed to write per-PID
`/chromium-egl-trace.<pid>.log` files and concatenate them into the existing
host `chromium-egl-trace.log` artifact. That fixed the cross-process append
collision enough to prove the wrapper reaches Chromium GPU-error PIDs and
captures their `eglChooseConfig` requests.

2026-06-27 follow-up fix: the remaining trace mismatch was the guest append
path's last-writer behavior inside each per-PID trace file, not absence of
Chromium process tracing. A first split of the identity into short
`phase=process` plus optional `phase=argv` records was still insufficient in
the VM:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T191816Z-chromium-gbm-egl-trace-process-fix-perf-missing-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-missing
chromium_egl_trace_summary=available=1 init=7 process=0 choose=2 choose_zero=2 choose_pbuffer=2
```

The durable fix is to make the short `phase=process` record the final
constructor write and stop writing argv detail records. Host-side proof with
the staged preload shows the counted process line is 122 bytes and follows the
init line:

```text
build-x86_64/chromium-egl-trace-harness-proof/20260627T192021Z-process-last-constructor-record/
CHROMIUM-EGL-TRACE-HARNESS-PROOF: PASS
init_line_length=118
process_line_length=122
process_line=chromium_egl_trace phase=process pid=1397455 ppid=1397448 program=true role=unknown trace_env=1 log_env=1 cmdline_bytes=10
```

VM validation of that second-stage fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T192401Z-chromium-gbm-egl-trace-process-last-capture-static-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
chromium_egl_trace_summary=available=1 init=1 process=5 process_browser=1 process_renderer=0 process_gpu=1 process_zygote=2 choose=2 choose_zero=2 choose_pbuffer=2
first_process="chromium_egl_trace phase=process pid=211 ppid=1 program=chrome role=browser trace_env=1 log_env=1 cmdline_bytes=256"
last_process="chromium_egl_trace phase=process pid=522 ppid=211 program=exe role=gpu-process trace_env=1 log_env=1 cmdline_bytes=256"
gpu_error_pids=374,455
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_present_count=12 capture_nonblack_count=12 capture_changed_count=0
chromium_int3_summary=available=1 trap_count=0
```

Static validation passed:

```text
cc -fsyntax-only -Wall -Wextra -I/usr/include -I/usr/include/libdrm scripts/image/chromium-egl-trace-preload.c
expect -c '... info complete ... scripts/gpu/kde-plasma-desktop-smoke.expect ...'
git diff --check -- scripts/image/chromium-egl-trace-preload.c scripts/gpu/kde-plasma-desktop-smoke.expect docs/linux-drm-abi-compat-plan.md ...
```

`cmake --build build-x86_64 --target host-gui-runtime -j2` restaged
`chromium-egl-trace-preload.so`, and `rootfs-refresh` rebuilt
`build-x86_64/fs.img` with that runtime payload. The Chromium EGL trace harness
can now be used for process-role counts, with the caveat that later EGL
entrypoint records may still overwrite earlier constructor records for the same
PID. Use the process sampler as the primary role source when a role is missing
from the preload trace.

Verification of the compact process record was blocked by a separate early KWin
startup crash before Chromium launched:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T-chromium-video-gbm-egl-trace-compact-process-kwin-ready-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
/usr/bin/kwin_wayland: Relink `/usr/lib/x86_64-linux-gnu/libQt5Qml.so.5' with `/usr/lib/x86_64-linux-gnu/libc.so.6' for IFUNC symbol `memcpy'
kde-exception-fatal-summary: pid=49 name=kwin_wayland cr2=0x1c4 err=0x4 rip=0x7ffffc515752
exception-ip: file_off=0xb1752 path=/usr/lib/x86_64-linux-gnu/libc.so.6
kwinglutils-slot: file_off=0x26000 value starts `00 60 02 00 ...'
```

Interpretation: the current Chromium-video path is still a real
GPU/EGL/playback gap after GBM preprobe and sampler PASS, but the next VM run
should first recover a stable KDE/KWin startup lane or reduce the KWin
Qt/QML/libc IFUNC crash. Once KWin is stable, rerun the same Chromium EGL trace
lane to verify compact process-role lines and then compare Chromium's actual
GBM `eglChooseConfig`/context path against the xv6-owned direct GBM matrix. Do
not patch KDE, Qt, KWin, Xwayland, Mesa, or Chromium for this evidence.

Reducer update: the harness now covers the KWin initial-exec loader path, not
only late `dlopen()`. `scripts/gpu/kde-plasma-desktop-smoke.expect` runs
`/bin/kde-dlopen-probe --kwin-help-check` during preflight and requires real
KWin help text instead of trusting the guest shell's `$?`. The focused
`scripts/gpu/kwin-loader-ifunc-proof.expect` proof preserves
`build-x86_64/kwin-loader-ifunc-proof/kwin-loader-ifunc.log` and
`kwin-loader-ifunc.status`; PASS means `kwin_wayland` reached its own help path
without the QtQml/libc IFUNC startup crash.

Focused proof result:

```text
build-x86_64/kwin-loader-ifunc-proof/
status=PASS source=kwin-loader-ifunc
kwin-loader-ifunc.status: status=DONE source=kwin-loader-ifunc
kwin-loader-ifunc.log: Usage: kwin_wayland [options] [/path/to/application...]
run.log: no `Relink ... IFUNC`, `fatal page fault`, or `kde-exception-fatal-summary`
```

Stronger QtQml startup reducer:

```text
build-x86_64/kde-qtqml-ifunc-startup-proof/
status=PASS source=kde-qtqml-ifunc-startup-probe
kde-qtqml-ifunc-startup-probe.dynamic: NEEDED libQt5Qml.so.5
kde-qtqml-ifunc-startup-probe.status: status=DONE source=kde-qtqml-ifunc-startup-probe
kde-qtqml-ifunc-startup-probe.log:
  KDE_QTQML_IFUNC_STARTUP_PROBE_START pid=45
  KDE_QTQML_IFUNC_STARTUP_PROBE_MAP status=PASS ... /usr/lib/x86_64-linux-gnu/libQt5Qml.so.5
  KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=PASS qtqml=... memcpy=...
run.log/probe.log: no `Relink ... IFUNC`, `fatal page fault`, or `kde-exception-fatal-summary`
```

The image-staged `kde-qtqml-ifunc-startup-probe` is an xv6-owned binary linked
with forced `DT_NEEDED` on `libQt5Qml.so.5`; reaching its PASS line proves the
dynamic loader processed QtQml startup relocation, mapped QtQml, and resolved
`memcpy` without the earlier `_rtld_global_ro == NULL` IFUNC fault. The KDE
smoke preflight now runs this explicit check after the KWin loader check and
matches the probe's PASS markers directly rather than using the guest shell's
`$?`.

2026-06-27 IFUNC mismatch fix:

`scripts/image/xv6-ifunc-memcpy-shim.c` now stages
`/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so`, a libc-free xv6-owned preload
that exports non-IFUNC `memcpy@@GLIBC_2.14`, `memcpy@GLIBC_2.2.5`,
`floor@@GLIBC_2.2.5`, and `ceil@@GLIBC_2.2.5`. KDE-owned launchers prepend it
to `LD_PRELOAD`, ahead of the existing ABI shims, so QtQml and KWin avoid the
glibc IFUNC resolver paths that xv6 cannot yet safely execute during this
loader phase. This keeps KDE, Qt, KWin, Mesa, Xwayland, Chromium, and glibc
package sources upstream-clean.

The mismatch found in this lane was that source-level shim/probe changes were
not enough: the active image could still contain an older shim and the main KDE
smoke preflight did not force the same preload as the standalone proof.
`cmake/BuildImage.cmake` now lists the shim and QtQml startup probe as rootfs
and rootfs-refresh dependencies, and the main `kde-qtqml-ifunc-startup` guest
script exports the KDE ABI preload before running the probe.

Fresh proof after the shim and the smoke preflight log-validation fix:

```text
timeout 420 scripts/gpu/kwin-loader-ifunc-proof.expect
KWIN-LOADER-IFUNC-PROOF-PASS
kwin-loader-ifunc.log: KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS

timeout 420 scripts/gpu/kde-qtqml-ifunc-startup-proof.expect
KDE-QTQML-IFUNC-STARTUP-PROOF-PASS
kde-qtqml-ifunc-startup-probe.log:
  KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=memcpy lib=/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so ...
  KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=floor lib=/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so ...
  KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=ceil lib=/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so ...
  KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=PASS ... memcpy=... floor=... ceil=...
```

The focused Chromium-video GBM rerun after this fix no longer failed with the
QtQml/libc IFUNC crash; KWin had the shim mapped and the run log contained no
`Relink ... IFUNC`, `fatal page fault`, or `kde-exception-fatal-summary`.
It failed earlier at `KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry`
because `/dev/shm/xdg-runtime-root/wayland-0` was not published before the
compositor grace timeout:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T180453Z-chromium-video-ifunc-fixed-kwin-wayland-socket-timeout-noimage/
```

Interpretation for that artifact: treat the QtQml/glibc IFUNC mismatch as
fixed by xv6-owned rootfs/sysroot staging. Its remaining blocker was a KWin
startup/socket-publication stability issue, not the IFUNC crash and not
Chromium playback. Later launch-only evidence below advances past that KDE
startup point.

Follow-up Chromium-video GBM reducer after the QtQml proof:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T171527Z-chromium-video-gbm-static-after-qtqml-ifunc-proof/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_present_count=12 capture_expected_count=12 capture_nonblack_count=12 capture_changed_count=0
gpu_init_error_count=6 gpu_config_error_count=4 gpu_exit_error_count=2 gpu_error_pids=300,390
process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 render_fd_seen=1 browser_render_fd_seen=1 ...
qemu_trace: ctx_submit=113 set_scanout=59 res_flush=59 fence_ctrl/fence_resp=113/113 res_create_3d=205 xfer_toh_3d=2
execbuffer_time_count=5 total_us=50015 avg_us=10003.0 max_us=49092 ensure_us=48555 cmd_copy_us=358 submit_us=928 trace_log_us=300097
```

Interpretation from the saved logs:

- KDE/KWin startup is stable in this lane; no QtQml IFUNC crash recurred.
- Chromium is launched with `EGL_PLATFORM=gbm` and opens `/dev/dri/renderD128`.
- The direct GBM smoke passes, but its config matrix shows `pbuffer=0`,
  `window=200`, `surfaceless` profiles available, and zero Chromium offscreen
  pbuffer matches:
  `chromium_offscreen_pbuffer_es3 count=0`,
  `chromium_native_window_pbuffer_es3 count=0`,
  `chromium_window_only_es3 count=40`, `chromium_surfaceless_es3 count=40`.
- Chromium GPU processes fail before playback with
  `eglCreateContext ES 3.0 failed`, `No suitable EGL configs found`,
  `CollectGraphicsInfo failed`, and `Exiting GPU process due to errors during
  initialization`.
- The video page logs only `before-src`, `after-src`, `start`, and
  `before-play` with `currentSrc=(empty)`, `presented=0`, `decoded=0`.
  Captures are all nonblack but identical, so this is a GPU/EGL init and media
  startup blocker, not a black-screen capture failure.

Next reducer direction: keep upstream Chromium/Mesa clean and test an
xv6-owned Chromium launch/runtime-mode matrix that avoids the unavailable GBM
pbuffer path and attempts the Linux-proven window/surfaceless-capable paths.
Gate each candidate on explicit Chromium EGL trace rows, sampler PASS,
non-static capture/frame metrics, GPU-error counts, and virtgpu execbuffer
timing before changing kernel behavior. The Linux control proves the pbuffer
gap is a virgl GBM/Chromium mode-shape issue, not by itself an xv6 DRM ioctl
mismatch.

2026-06-27 first runtime-mode candidate:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T193923Z-chromium-surfaceless-kde-startup-crash-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
chromium_egl_platform=surfaceless
generated launch prefix: WAYLAND_CHROMIUM_MULTIPROCESS=1 EGL_PLATFORM='surfaceless'
chromium_video_post_evidence status=FAIL reason=not-launched
final_sync_status=not-run
chromium_egl_trace_summary=available=0
launch_evidence_status=missing
process_summary=browser_seen=0 renderer_seen=0 gpu_seen=0 render_fd_seen=0
kwin_wayland: exception 13 (#GP General Protection) rip=0x7ffffecfd884
exception-ip: map=[0x7ffffecfa000-0x7ffffed0d000) pgoff=0xa000 file_off=0xd884 ino=16832 size=157840
qemu trace: ctx_submit=1 set_scanout=3 res_flush=3 fence_ctrl/fence_resp=1/1 res_create_3d=1 xfer_toh_3d=2
```

Interpretation: this run is not evidence for or against Chromium
`EGL_PLATFORM=surfaceless`; KWin crashed before the GBM preprobe, Chromium
launch, sampler, or capture phases. Treat it as KDE startup stability evidence.
Before extending the Chromium runtime-mode matrix, reduce the recurrent KWin
startup #GP enough to make the KDE lane stable again, or run Chromium mode
candidates in a smaller non-KDE Wayland/DRM harness if that can still preserve
the relevant EGL/GBM/Chromium metrics.

2026-06-27 Chromium SIMDUTF dispatch mismatch closure:

```text
Reducer proof with explicit env:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T203834Z-chromium-surfaceless-simdutf-westmere-noimage/

Default-policy proof:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T204504Z-chromium-surfaceless-simdutf-default-noimage/
```

The staged Chrome ELF trap at file offset `0xad84a17` was an intentional
Chromium `int3`/`ud2` site reached through a SIMD dispatch path. A later
Chromium-video lane showed that forcing the launcher default to
`SIMDUTF_FORCE_IMPLEMENTATION=westmere` can itself reproduce that trap before
the GPU process appears. The launcher must not force a default implementation;
only the xv6-owned reducer override
`WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION` should set
`SIMDUTF_FORCE_IMPLEMENTATION`.

Default proof:

```text
wayland-chromium-launcher: env WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION="(unset)"
wayland-chromium-launcher: env SIMDUTF_FORCE_IMPLEMENTATION="westmere"
chromium_int3_summary=available=1 trap_count=0
launch_evidence_status=PASS
sampler_result=... status=PASS
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=12
capture_changed_count=0 capture_post_nonblack_changed_count=0
status=FAIL reason=capture-static
```

Interpretation: this older proof only showed that one short launch-only path
survived with `westmere`; it is not a durable default policy proof.
Continue from the next blocker: Chromium's GPU process still fails EGL
initialization with `eglCreateContext ES 3.0 failed`, `No suitable EGL configs
found`, `CollectGraphicsInfo failed`, and `Exiting GPU process due to errors
during initialization`. The GBM preprobe simultaneously proves window-only and
surfaceless ES2/ES3 contexts pass while Chromium-shaped pbuffer and
native-window+pbuffer configs are absent. The next reducer should compare
Chromium's actual offscreen config request against the Linux KVM/virgl control
or steer Chromium onto the proven window/surfaceless-capable path with
xv6-owned launcher/runtime policy.

2026-06-27 Chromium surfaceless runtime-policy A/B:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T205323Z-chromium-surfaceless-use-gl-egl-no-entrypoint-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chromium-egl-trace-no-entrypoint-call
chromium_extra_flags="--disable-gpu-early-init --use-gl=egl"
chromium_egl_trace_summary=available=1 process_gpu=3 choose=0 create_context=0 create_pbuffer=0 create_window=0
gpu_init_error_count=0 gpu_config_error_count=0 gpu_exit_error_count=2
GL error: Requested GL implementation (gl=egl-gles2,angle=none) not found in allowed implementations
perf_playing ... time=5.495 presented=121 decoded=338 dropped=222
capture_present_count=12 capture_nonblack_count=12 capture_changed_count=0 capture_post_nonblack_changed_count=0
virtio_gpu_cmd_ctx_submit=400 res_flush=318 fence_ctrl/fence_resp=400/399

build-x86_64/kde-plasma-desktop-smoke-history/20260627T205637Z-chromium-surfaceless-angle-opengles-no-entrypoint-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chromium-egl-trace-no-entrypoint-call
chromium_extra_flags="--disable-gpu-early-init --use-gl=angle --use-angle=opengles"
chromium_egl_trace_summary=available=1 process_gpu=3 choose=0 create_context=0 create_pbuffer=0 create_window=0
gpu_init_error_count=0 gpu_config_error_count=0 gpu_exit_error_count=2
GL error: Requested GL implementation (gl=none,angle=none) not found in allowed implementations
perf_playing ... time=2.510 presented=16 decoded=159 dropped=114
capture_present_count=12 capture_nonblack_count=12 capture_changed_count=0 capture_post_nonblack_changed_count=0
virtio_gpu_cmd_ctx_submit=328 res_flush=247 fence_ctrl/fence_resp=328/328

build-x86_64/kde-plasma-desktop-smoke-history/20260627T205846Z-chromium-surfaceless-egl-angle-opengles-kwin-startup-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
chromium_extra_flags="--disable-gpu-early-init --use-gl=egl-angle --use-angle=opengles"
kde-session: /dev/shm/xdg-runtime-root/wayland-0 not visible after compositor grace
chromium_video_post_evidence status=FAIL reason=not-launched
```

Interpretation: `--disable-gpu-early-init` plus invalid/unsupported Chromium GL
tokens avoids the earlier `No suitable EGL configs` log and lets the page
decode enough media to reach `playing`, but it does not fix presentation:
sampler/capture still sees 12 full-size nonblack frames with zero frame-to-frame
change. The process sampler still misses persistent renderer/GPU roles, while
the EGL trace preload sees transient GPU-process identities but no wrapped EGL
entrypoint calls. `--use-gl=angle` is not accepted by this Chromium build; the
allowed spelling appears to be `--use-gl=egl-angle`, but the first exact
`egl-angle/opengles` attempt was a KWin startup timeout and is not Chromium
evidence. Do not promote any runtime policy yet. The next useful reducer is an
exact `egl-angle/opengles` retry only after the KDE startup lane is stable, plus
a harness/source audit of why Chromium/ANGLE creates virtgpu contexts without
calling the current EGL trace entrypoints. Separately, the static visible
surface despite advancing `presented/decoded/dropped` counters points at the
Chromium-to-compositor/video-surface presentation path rather than media decode
or fake sampler/capture status.

2026-06-27 Chromium surfaceless `egl-angle/opengles` retry:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T210833Z-chromium-surfaceless-egl-angle-opengles-dlopen-harness-parser-error-noimage/
status_file=KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video
terminal_failure=harness Tcl parser bug in chromium_video_perf_media_summary
chromium_extra_flags="--disable-gpu-early-init --use-gl=egl-angle --use-angle=opengles"
host_egl_gbm_status="status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted"
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 renderer=virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))
sampler="kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS"
capture_status="status=DONE samples=12"
frame_stats=12 present full-size nonblack samples, every post-first sample changed
PERF-VIDEO RESULT fail startup-timeout at startupMs=8000, then play-resolved at wall=9.22
PERF-VIDEO final ended time=16.000 presented=462 decoded=960 dropped=611 err=0
chromium_egl_trace dlopen=/lib/gbm/dri_gbm.so and /lib/dri/virtio_gpu_drv_video.so
chromium_egl_trace process roles include transient gpu-process, but no wrapped EGL entrypoints
Chromium GL errors: two early gpu-process exits with requested (gl=none,angle=none)

build-x86_64/kde-plasma-desktop-smoke-history/20260627T211033Z-chromium-surfaceless-egl-angle-opengles-dlopen-prompt-sync-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL prompt-sync-timeout
chromium_video_post_evidence status=FAIL reason=not-launched
```

Interpretation: the exact Chromium spelling can get far enough to decode and
present video frames under KDE on the WSL2 Linux-VM virgl path, and the host
GBM/EGL probe proves the active renderer is D3D12-backed virgl. The first retry
is not a valid green reducer because the newly added Tcl media-summary regex
used double-quoted `[^...]` and crashed during post-evidence writing; that bug
was fixed by building the field regex with `format`. The second retry failed
before Chromium launch with a prompt synchronization timeout, so it is harness
timing evidence only. Keep the next validation focused on a clean rerun with the
parser fix and the same flags. If the run completes, classify from explicit
`PERF-VIDEO`, sampler, capture, and `dlopen`/entrypoint evidence rather than
promoting the runtime policy from this partial run.

2026-06-27 KWin PCRE2 allocator-slot trace:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T213349Z-chromium-surfaceless-egl-angle-opengles-kwin-pcre2-trace-clean-prompt-sync-timeout-noimage/
QEMU_APPEND_EXTRA includes kde_kwin_alloc_trace=1
status_file=KDE-PLASMA-DESKTOP-SMOKE-FAIL prompt-sync-timeout
visual_artifact=kde-plasma-host-final.png, QEMU/KDE desktop visible and not black
rootfs refreshed: build-x86_64/fs.img mtime=2026-06-27 17:31:10 -0400
staged preload: /opt/xv6-kde-abi-libs/kwin-alloc-trace-preload.so size=37056
kwin-alloc-trace pcre2_base=0x00007fffff5d9000 allocator_list=base+0x8b250 allocator_mutex=base+0x8b260
pcre2_slot_sample lines=12786 max_samples=14298 max_changes=10 max_poison_samples=0
path_poison lines=0
pcre2_match_enter=625 pcre2_compile_enter=5 pcre2_dfa_match_enter=0
cxa_throw/throw-summary=0
KDE role probe: kwin=1 plasmashell=1 xwayland=1 pipewire=1 status=PASS at uptime_s=20.24
virtio trace summary: ctx_submit=68 res_flush=21 fence_ctrl/fence_resp=68/68 res_create_3d=238
chromium_video_post_evidence status=FAIL reason=not-launched
```

Interpretation: the previous `pcre2_dfa_match_16` #GP with
`"/usr/lib/x86_64-"`-style poison in the allocator-list slot is not reproduced
when the xv6-owned KWin preload brackets real startup. The common Qt/PCRE2 path
does hit the new wrappers (`pcre2_match_16`, compile, match-data/context, and
code-free); DFA remains at zero calls, matching the import audit. Across more
than fourteen thousand PCRE2 slot samples, the allocator-list/mutex slots move
through valid pointer-looking states but never contain path poison, and KWin,
Plasma, Xwayland, and PipeWire all come up. This makes the current blocker a
harness prompt-sync/Chromium-launch sequencing issue, not a repeatable KWin
PCRE2 startup crash. Keep the trace shim for targeted retries, but do not
promote it as a runtime workaround. The next clean reducer should run without
the heavy PCRE2 wrapper unless the KWin crash returns; if it does return, use
the clean slot trace as the control and compare the poisoned run's first
`path_poison=1` line.

2026-06-27 Chromium-video harness audit and current bottleneck:

```text
Harness fixes:
- `sync_prompt` is marker-only. Bare `root:/#` prompts are counted as
  stale_prompts and no longer prove synchronization.
- Prompt-sync evidence is durable in `kde-prompt-sync.log`.
- Chromium-video final sync is a post-evidence requirement; `timeout` now
  fails as `final-sync-timeout` instead of allowing a false reducer success.
- The post-KWin role guard runs `/bin/kde-process-probe` directly and records
  the explicit result line in `kde-role-probe.log`; it no longer depends on a
  long redirected shell command plus `KDEROLEDONE`.

Proof artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T215729Z-chromium-surfaceless-egl-angle-opengles-video-perf-result-fail-noimage/

status_file=KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fail
prompt_sync=8/8 marker PASS, stale_prompts all 0
kde-role-probe: uptime_s=14.73 kwin=1 plasmashell=1 xwayland=1 pipewire=1 pipewire_pulse=1 status=PASS
kde-role-probe: uptime_s=17.78 kwin=1 plasmashell=1 xwayland=1 status=PASS
KWin loader IFUNC and QtQml IFUNC probes both PASS before Chromium launch
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3
GBM Chromium-shaped configs: pbuffer/native-window+pbuffer count=0; window/surfaceless ES2/ES3 count=40 and context attempts PASS
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_status=status=DONE samples=12
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=12 capture_changed_count=5 capture_post_nonblack_changed_count=5
Chromium process summary: browser_seen=1 zygote_seen=1 renderer_seen=0 gpu_seen=0 render_fd_seen=1 role_counts=browser:1,zygote:2
PERF-VIDEO RESULT fail startup-timeout phase=play-promise startupMs=8000 ready=1 presented=0 decoded=13 dropped=7
Later playback reached time=16.000 duration=16.000 presented=347 decoded=960 dropped=611 err=0
GPU process exits during early init: gpu_exit_error_count=2, no gpu_init/config error count
DRM execbuffer timings: count=3 total_us min=194 max=44472 avg=14961; submit_us min=94 max=148 avg=122; trace_log_us avg=58907
virtio trace summary: ctx_submit=484 set_scanout=384 res_flush=384 fence_ctrl/fence_resp=484/484 res_create_3d=280
visual proof: 12 full-size 1280x800 captures, 5 changed after the static first frames
```

Interpretation: the earlier `kwin-loader-ifunc-session-invalid` and
`prompt-sync-timeout` failures were harness evidence gaps, not the current KDE
session blocker. With marker-only prompt sync and direct role-probe evidence,
KWin, Plasma, Xwayland, GBM/EGL, Chromium launch, sampler, capture, and final
sync all complete. The current measured blocker is Chromium video startup and
GPU-process stability: playback starts too late for the 8 s startup gate and
then plays through with heavy frame dropping (`611/960` decoded dropped) and
only `347` presented frames over the 16 s asset. Since window/surfaceless
contexts pass and pbuffer shapes are absent on both xv6 and the Linux VM
control, do not patch Mesa/Chromium for pbuffer config shape. The next reducer
should quantify why Chromium's GPU-process role is transient/missed and why the
startup gate misses by roughly 1.8 s (`play-resolved wall=9.86`) despite later
video playback. Candidate xv6-owned probes: reduce the portal/D-Bus EBADF noise
seen in the same run, compare launch with portal-related environment or rootfs
service policy, and add timing around Chromium GPU-process execbuffer/open
lifetimes without the high `chrome_drm_ioctl_trace` logging overhead.

2026-06-27 low-overhead Chromium-video A/B:

```text
Proof artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T220311Z-chromium-surfaceless-egl-angle-opengles-video-low-overhead-perf-result-fail-noimage/

Run shape: same Chromium surfaceless egl-angle/opengles policy, GBM preprobe,
12 s warmup, 12 capture samples, and `kde_xwayland_glamor=auto`, but without
KDE alloc tracing, Chromium EGL trace preload, chrome DRM ioctl tracing, or
chrome DRM fence tracing.

status_file=KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fail
chromium_egl_trace=0
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=37 duration_ms=18000 interval_ms=500 status=PASS
capture_status=status=DONE samples=12
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=12 capture_changed_count=6 capture_post_nonblack_changed_count=6
Chromium process summary: browser_seen=1 zygote_seen=1 renderer_seen=0 gpu_seen=0 render_fd_seen=1 browser_render_fd_seen=1 role_counts=browser:1,unknown:2,zygote:2
GPU process exits during early init: gpu_exit_error_count=2, pids=331,414
PERF-VIDEO RESULT fail startup-timeout phase=play-promise startupMs=8000 ready=1 presented=0 decoded=15 dropped=13
play-resolved wall=10.58 ready=4 presented=1 decoded=38 dropped=26
Later playback reached time=16.000 duration=16.000 presented=325 decoded=960 dropped=580 err=0
virtio trace summary: ctx_submit=397 set_scanout=318 res_flush=318 fence_ctrl/fence_resp=397/397 res_create_3d=277
visual proof: 12 full-size 1280x800 captures, 6 changed after the static first frames
```

Interpretation: removing the high-overhead trace layers did not close the
startup gap. The low-overhead run still missed the 8 s startup gate and
resolved playback later than the traced control (`10.58 s` versus `9.86 s`),
while the steady-state drop ratio stayed in the same range (`580/960` decoded
dropped versus `611/960`). The next reducer should not be a kernel behavior
patch yet. First prove the Chromium GPU-child policy mismatch noted by the
artifact audit: the parent launch carries
`--disable-gpu-early-init --use-gl=egl-angle --use-angle=opengles`, but the
transient GPU children report a requested GL implementation of
`gl=none,angle=none` before exiting. Use an upstream-clean launcher/rootfs
policy reducer that varies only `--disable-gpu-early-init`, GL/ANGLE spelling,
`EGL_PLATFORM`, and optional xv6-owned trace preload, then compare GPU-child
cmdlines and Chromium `gl_factory` output before touching xv6 DRM, process, or
socket ABI behavior.

2026-06-27 GPU-child argv/env proof and harness scope fix:

```text
Proof artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T222257Z-chromium-egl-trace-gpu-child-argv-perf-playback-missing-noimage/

Harness fixes before proof:
- The Chromium-video GBM preprobe no longer uses the fake xv6 shell `$?`
  marker; it waits for the opaque `KCVEGLDONE` marker and explicit GBM log
  result lines.
- `finish` now declares the guest QtQml IFUNC log path before dumping it, so
  Chromium-video failures produce durable post-evidence instead of a Tcl scope
  exception.
- QtQml startup probe is staged as a guest script/log and passes before
  Chromium launch:
  `KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=PASS`.

Run shape: Chromium-video reducer with multiprocess Chromium, EGL trace
preload, `EGL_PLATFORM=surfaceless`, GBM preprobe, 18 s sampler with 3.5 s
100 ms burst, 6 capture samples, and
`--disable-gpu-early-init --use-gl=egl-angle --use-angle=opengles`.

status_file=KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-playback-missing
final_sync_status=done
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3
GBM renderer=virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))
QtQml IFUNC probe status=PASS
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=73 duration_ms=18000 interval_ms=500 burst_samples=36 burst_ms=3500 burst_interval_ms=100 status=PASS
capture_status=status=DONE samples=6
capture_present_count=6 capture_expected_count=6
capture_nonblack_count=6 capture_changed_count=0
process_summary=browser_seen=1 zygote_seen=1 renderer_seen=0 gpu_seen=1 gpu_first_sample=68 gpu_last_sample=72 render_fd_seen=1 browser_render_fd_seen=1 role_counts=browser:1,gpu-process:1,unknown:1,utility:1,zygote:3
GPU-child process evidence includes:
  --type=gpu-process --render-node-override=/dev/dri/renderD128
  --use-angle=opengles --use-gl=egl-angle
GPU-child env includes:
  EGL_PLATFORM=surfaceless
  LD_PRELOAD=/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so:...
Chromium launcher parent argv/env also include the same GL/ANGLE flags and
surfaceless EGL env.
Chromium GL errors:
  [283] Requested GL implementation (gl=none,angle=none) not found ...
  [378] Requested GL implementation (gl=none,angle=none) not found ...
gpu_exit_error_count=2 gpu_error_pids=283,378
perf_media_summary: playing_count=0 tick_count=0 result_count=0 last_time=0.000 presented=0 decoded=0 dropped=0
capture frame stats: 6 full-size nonblack frames, all identical
virtio trace summary: ctx_submit=105 set_scanout=42 res_flush=42 fence_ctrl/fence_resp=105/104 res_create_3d=130
```

Interpretation: the previous suspected launcher/rootfs propagation mismatch is
closed for this lane. Both the parent launch and a sampled GPU child carry
`--use-gl=egl-angle`, `--use-angle=opengles`, `EGL_PLATFORM=surfaceless`, and
the trace preload. The remaining mismatch is inside Chromium's GPU-process
initialization state: despite argv/env containing the supported spelling,
Chromium reports a requested GL implementation of `gl=none,angle=none` and
exits the GPU process. The direct GBM control still proves virgl GLES works on
this WSL2 host and in the guest render-node path, with window/surfaceless ES2
and ES3 contexts passing and pbuffer-shaped configs absent.

No-early-init/no-trace Chromium-video matrix point:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T223051Z-chromium-surfaceless-egl-angle-opengles-no-early-init-no-trace-perf-result-fail-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fail
chromium_extra_flags="--use-gl=egl-angle --use-angle=opengles"
chromium_egl_platform=surfaceless
chromium_egl_trace=0
PERF-VIDEO RESULT fail startup-timeout phase=play-promise startupMs=8000 ready=2 presented=0 decoded=19 dropped=12
play-resolved wall=8.67 ready=4 presented=1 decoded=28 dropped=17
event:playing time=2.662 presented=21 decoded=165 dropped=74
gpu_exit_error_count=2 gpu_error_pids=353,443
process_summary=browser_seen=1 zygote_seen=1 renderer_seen=0 gpu_seen=1 render_fd_seen=1 browser_render_fd_seen=1 role_counts=browser:1,gpu-process:1,unknown:2,utility:1,zygote:3
chromium_int3_summary trap_count=0
capture_present_count=6 capture_nonblack_count=5 capture_changed_count=1 capture_post_nonblack_changed_count=0
egl_chromium_context_ready=1 reason=window-or-surfaceless-pass
```

Interpretation: removing `--disable-gpu-early-init` and disabling the
Chromium EGL trace avoids the browser `int3` seen in the traced no-early-init
run and improves playback resolution versus the low-overhead early-init
control, but it does not close the startup/playback gap. GPU children still
exit during initialization and the sampled playback rate is only about
21 presented frames by media time 2.662 s while 74 of 165 decoded frames have
dropped. Continue with the launch/runtime-policy matrix before any DRM behavior
patch.

No-early-init/no-trace `egl-angle/opengl` matrix point:

```text
Non-answer preflight artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T224854Z-chromium-surfaceless-egl-angle-opengl-prechromium-qtqml-ifunc-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-qtqml-ifunc-startup-timeout

Chromium-result artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T225056Z-chromium-surfaceless-egl-angle-opengl-no-trace-perf-start-missing-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-start-missing
chromium_extra_flags="--use-gl=egl-angle --use-angle=opengl"
chromium_egl_platform=surfaceless
chromium_egl_trace=0
final_sync_status=done
perf_media_summary: perf_count=0 playing_count=0 tick_count=0 result_count=0
gpu_exit_error_count=1 gpu_error_pids=332
Chromium GL error:
  Requested GL implementation (gl=none,angle=none) not found in allowed
  implementations: [(gl=egl-angle,angle=opengl),(gl=egl-angle,angle=opengles),
  (gl=egl-angle,angle=vulkan)].
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3
egl_chromium_context_ready=1 reason=window-or-surfaceless-pass
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=73 duration_ms=18000 interval_ms=500 burst_samples=36 burst_ms=3500 burst_interval_ms=100 status=PASS
process_summary=browser_seen=1 zygote_seen=1 renderer_seen=0 gpu_seen=1 render_fd_seen=1 browser_render_fd_seen=1 role_counts=browser:1,gpu-process:1,unknown:2,utility:1,zygote:3
chromium_int3_summary trap_count=0
capture_present_count=6 capture_nonblack_count=6 capture_changed_count=0 capture_post_nonblack_changed_count=0
frame_stats: six 1280x800 nonblack captures, all identical, mean=0.630504
```

Interpretation: the first `opengl` attempt is only a KDE/QtQml startup
preflight timeout and does not answer the Chromium question. The retry reaches
Chromium and proves that switching `--use-angle=opengles` to `opengl` does not
fix the GPU preference collapse: the parent launch and sampled process evidence
carry `--use-gl=egl-angle --use-angle=opengl` with `EGL_PLATFORM=surfaceless`,
but Chromium still reports `gl=none,angle=none` inside GPU initialization and
exits the GPU process before any `PERF-VIDEO` event. The single GPU-process
exit is slightly different from the two exits in the matching `opengles` lane,
but the visible result is worse: no playback/perf start and static captures.
The next matrix dimension should be `EGL_PLATFORM` (`surfaceless` versus
Wayland/default) with no EGL trace preload and no `--disable-gpu-early-init`;
do not patch xv6 DRM or Chromium/Mesa for the ANGLE token itself.

Explicit-Wayland no-early-init/no-trace matrix point:

```text
Proof artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T225757Z-chromium-wayland-egl-angle-opengles-no-trace-chrome-crash-regression-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
history_image_pruned=PASS

chromium_egl_platform=wayland
chromium_extra_flags="--use-gl=egl-angle --use-angle=opengles"
chromium_egl_trace=0
final_sync_status=done
perf_media_summary: perf_count=0 playing_count=0 tick_count=0 result_count=0
gpu_exit_error_count=0
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3
egl_chromium_context_ready=1 reason=window-or-surfaceless-pass
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=73 duration_ms=18000 interval_ms=500 burst_samples=36 burst_ms=3500 burst_interval_ms=100 status=PASS
process_summary=browser_seen=1 zygote_seen=1 renderer_seen=0 gpu_seen=0 render_fd_seen=1 browser_render_fd_seen=1 role_counts=browser:1,unknown:2,zygote:2
chromium_int3_summary: trap_count=1 pid=154 rip=0x4ad85a17 ip_file_off=0xad84a17 stack3_file_off=0x60d8b19 stack5_file_off=0xad84a36
capture_present_count=6 capture_nonblack_count=6 capture_changed_count=1 capture_post_nonblack_changed_count=1
frame_stats: six 1280x800 nonblack captures; first five identical, final delta=44435
virtio trace summary: ctx_submit=76 set_scanout=19 res_flush=19 fence_ctrl/fence_resp=76/76 res_create_3d=223
```

Interpretation: explicit `EGL_PLATFORM=wayland` is not the missing Chromium
runtime policy. It removes the `gl=none,angle=none` GPU-init error seen in the
surfaceless lanes, but only because no GPU-process role is sampled before the
browser hits the known Chromium `int3` site at file offset `0xad84a17`. That is
the same staged Chrome intentional `int3`/`ud2` SIMD dispatch site previously
closed for the surfaceless lane by the default
`SIMDUTF_FORCE_IMPLEMENTATION=westmere`; this run proves the Wayland platform
path can still reach that trap even with the launcher logging
`SIMDUTF_FORCE_IMPLEMENTATION=westmere`. Treat explicit Wayland/default
`EGL_PLATFORM` as worse than surfaceless for the current KDE Chromium probe.
Do not spend the next lane on unset `EGL_PLATFORM`, because the launcher
default is also Wayland-shaped. Keep the current best Chromium policy on
surfaceless and move inward to a smaller xv6-owned reducer for the Chrome
CPU-dispatch/environment path or GPU preference construction before any DRM
behavior change.

Harness follow-up: `kde-chromium-video-post-evidence.log` now records
`gl_request_error_count`, `gl_request_none_count`, and first/last requested-GL
implementation error lines from `host-gui-wayland-chromium.log`. This makes the
`gl=none,angle=none` collapse a durable post-evidence metric instead of a
manual side grep. Static Tcl completeness and `git diff --check` passed after
the parser-only change; the next Chromium-video VM lane should verify the new
fields in a fresh artifact.

Launch-only validation non-answer:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T230919Z-chromium-launch-only-surfaceless-prechromium-kde-startup-retry-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
launch_only=1
chromium_egl_platform=surfaceless
chromium_extra_flags="--use-gl=egl-angle --use-angle=opengles"
final_sync_status=not-run
sampler_log_present=0
process_summary=browser_seen=0 gpu_seen=0 render_fd_seen=0
status=FAIL reason=not-launched
```

Interpretation: launch-only mode is wired far enough for post-evidence to
classify a no-Chromium run, but this artifact failed before Chromium because
KWin hit the intermittent startup-retry path. It does not answer the
`gl_request_*` metric question; rerun the same shortened surfaceless
launch-only lane and require a successful launch/sampler before drawing
Chromium conclusions.

Independent audit then found one launch-only final-status bug: a successful
post-evidence `reason=launch-only` still would have been converted to
`KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-launch-only` by the final
Chromium-video reducer gate. The gate now admits `launch-only` alongside
`perf-result-not-required` and `perf-result-pass`; launch-only remains
evidence-checked by post-evidence before reaching that final gate.

Launch-only rerun after the IFUNC/rootfs/preflight mismatch fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260627T232935Z-chromium-launch-only-surfaceless-ifunc-fixed-chrome-int3-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
kde-qtqml-ifunc-startup.log:
  KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=memcpy lib=/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so ...
  KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=floor lib=/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so ...
  KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=ceil lib=/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so ...
  KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=PASS ... memcpy=... floor=... ceil=...
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=130 duration_ms=7000 interval_ms=250 burst_samples=101 burst_ms=5000 burst_interval_ms=50 status=PASS
process_summary=browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1 render_fd_seen=1 browser_render_fd_seen=1 card_fd_seen=0
gl_request_error_count=0 gl_request_none_count=0
chromium_int3_summary=available=1 trap_count=1 pid=141 rip=0x4ad85a17 ip_file_off=0xad84a17 stack3_file_off=0x60d8b19 stack5_file_off=0xad84a36
status=FAIL reason=chrome-crash-regression
```

Interpretation: the original mismatch is fixed in the active image and the
main smoke preflight, not only in standalone host/source checks. KDE reaches
the Chromium launch-only sampler, Chromium opens the render node from the
browser role, and the next failure is the known Chrome `int3` path rather than
the QtQml/KWin IFUNC loader crash or a missing rootfs update.

Next reducer direction: do not patch xv6 DRM behavior yet. Keep upstream
Chromium/Mesa clean and build an xv6-owned launch/runtime-policy reducer that
tests the smallest matrix around Chromium GPU preference construction:
`--disable-gpu-early-init` on/off, `--use-gl=egl-angle` with `--use-angle`
`opengles` versus `opengl`, `EGL_PLATFORM=surfaceless` versus unset/wayland,
and the trace preload on/off. The proof target is a GPU child whose raw argv
still contains the chosen flags and whose Chromium log no longer collapses to
`gl=none,angle=none`. Separately, the per-pid Chromium trace concatenation can
be tightened because long `phase=process` rows were fragmented in
`chromium-egl-trace.log`; the sampler process evidence is currently the clean
argv/env source of truth.

2026-06-27 Chromium launch-only fault-evidence and KWin marker follow-up:

```text
Non-answer artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T235357Z-chromium-launch-only-video-url-kwin-loader-ifunc-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kwin-loader-ifunc-timeout
run.log: KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS ...
kde-kwin-loader-ifunc.log: KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS ...
kde-chromium-video-post-evidence.log: status=FAIL reason=not-launched
chromium_fault_summary=available=1 fault_count=0 ...
chromium_fault_context=available=1 fault_pid=missing fault_pos=-1 ...

Proof artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260627T235900Z-chromium-launch-only-video-url-fault-evidence-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_video=local-video samples=1 chromium_launch_only=1 chromium_multiprocess=1
Xwayland wrapper: glamor=auto effective_glamor=es enable_glx=1
KWin loader IFUNC: KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS
launcher_policy_summary=available=1 final_argc=30 use_gl="egl-angle" use_angle="opengles" target="file:///share/webkit/perf-video.html?asset=perf-1280x800-60fps.mp4&ms=15000&hud=1" egl_platform="surfaceless" simdutf="westmere" ld_preload_ifunc=1
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=130 duration_ms=7000 interval_ms=250 burst_samples=101 burst_ms=5000 burst_interval_ms=50 status=PASS
final_sync_status=done
process_summary=browser_seen=1 zygote_seen=1 gpu_seen=1 renderer_seen=0 render_fd_seen=1 browser_render_fd_seen=1 crashpad_seen=1 role_counts=browser:1,gpu-process:1,unknown:2,utility:1,zygote:3
process_policy_summary: browser/gpu/zygote/utility inherit SIMDUTF_FORCE_IMPLEMENTATION=westmere and EGL_PLATFORM=surfaceless; GPU child has --use-gl=egl-angle and --use-angle=opengles
chromium_int3_summary=available=1 trap_count=0
chromium_fault_summary=available=1 fault_count=0
chromium_fault_context=available=1 fault_pid=missing fault_pos=-1
perf_media_summary=available=1 perf_count=0 playing_count=0 tick_count=0 result_count=0
capture_present_count=0 capture_expected_count=1 (expected in launch-only)
virtio trace summary: ctx_submit=48 set_scanout=16 res_flush=16 fence_ctrl/fence_resp=48/48 res_create_3d=47
```

Harness fixes and audits:

- `kde-chromium-video-post-evidence.log` now records
  `chromium_fault_summary` and `chromium_fault_context` beside the existing
  `chromium_int3_*` fields. These fields parse kernel `fatal page fault`
  lines, registers, stack/memory snippets, sampler marker position, and
  role-derived process context without changing pass/fail decisions.
- `crashpad_seen` no longer counts the browser argv string
  `--disable-crashpad`; it requires a crashpad role or crashpad process
  `comm`. In the proof artifact it remains `1` because real
  `chrome_crashpad` helper processes are sampled.
- The KWin loader IFUNC harness now accepts a returned `root:/#` prompt or
  timeout only after the explicit
  `KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS` line has been observed. The
  non-answer artifact showed `DONE` could be lost in asynchronous DBus/Plasma
  console noise even though the probe result and dumped log both proved PASS.
  The failure paths for crash, EOF, missing PASS, and explicit probe failure
  remain intact.
- Independent read-only audits approved both the Chromium fault-evidence
  wiring and the KWin marker fix before the VM proof. Static Tcl completeness,
  `git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect`, and
  `git -C kernel diff --check` passed.

Interpretation: the launch-only surfaceless `egl-angle/opengles` policy is no
longer blocked by the previous IFUNC/rootfs mismatch, KWin marker false
timeout, browser `int3`, or browser SIGSEGV. With the low-overhead launch-only
lane, Chromium survives long enough to spawn zygotes, utility, crashpad, and a
GPU process, and the browser opens `/dev/dri/renderD128`. This is progress
toward the KDE/Xwayland GLAMOR/GLX acceleration target: the same run proves
Xwayland `effective_glamor=es` with GLX enabled and KWin on virgl. The next
non-launch-only reducer should keep this policy and measure playback with
captures/perf enabled, focusing on GPU-process stability, renderer absence, and
startup/playback timing rather than changing xv6 DRM behavior.

2026-06-28 non-launch-only retry exposed a pre-Chromium KWin/glibc TPP abort:

```text
KDE artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T001111Z-chromium-video-surfaceless-egl-angle-opengles-kwin-tpp-priority-assert-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-timeout
run.log: Fatal glibc error: tpp.c:83 (__pthread_tpp_change_priority): assertion failed: new_prio == -1 || (new_prio >= fifo_min_prio && new_prio <= fifo_max_prio)
kde-chromium-video-post-evidence.log: status=FAIL reason=not-launched
```

Reducer evidence:

```text
Host Linux pthread priority probe:
SCHED_FIFO min=1 max=99; PTHREAD_PRIO_PROTECT default ceiling=1;
pthread_mutexattr_setprioceiling(0)=EINVAL; lock returns EINVAL.

xv6 guest attribute reducer:
build-x86_64/sched-prio-reducer.JbYxht/
PY fifo 1 99
PY other 0 0
PY get0 0 1
PY get1 0 1
PY set0 22
PY set1 0
PY set99 0
PY set100 22
SCHED-PRIO-REDUCER: PASS

xv6 guest lock reducer:
build-x86_64/sched-prio-reducer-lock.Z426Ay/
PYLOCK mutex_init 0
PYLOCK lock_ret 22
SCHED-PRIO-LOCK-REDUCER: PASS
```

Interpretation: the non-launch-only Chromium lane did not reach Chromium; it is
KWin startup evidence. The obvious Linux scheduler/TPP ABI surfaces match the
host Linux control: FIFO/RR priority range is `1..99`, default/protect mutex
ceiling is `1`, ceiling `0` is rejected, and a default protected mutex lock
returns `EINVAL` instead of asserting. Do not patch `sched_get_priority_min`,
`sched_get_priority_max`, or the basic pthread priority-ceiling path from this
artifact. The next reducer should capture the KWin-specific
`__pthread_tpp_change_priority` input or narrow the memory/IFUNC path that can
produce an invalid `new_prio` only during real KWin startup.

Follow-up diagnostic fix:

```text
scripts/image/kwin-alloc-trace-preload.c
```

The xv6-owned, opt-in KWin preload now records glibc priority-protect mutex
state when booted with `kde_kwin_alloc_trace=1`. It logs protected mutex
init/lock/trylock/timedlock/unlock entries, decoded glibc priority ceiling,
FIFO min/max range, invalid-ceiling counts, and bad lock returns. It is
observation-only: it does not clamp ceilings, rewrite mutex state, retry calls,
or patch KDE, Qt, KWin, Xwayland, Mesa, Chromium, or glibc.

Host micro-proof:

```text
cc -Wall -Wextra -Werror -fPIC -shared scripts/image/kwin-alloc-trace-preload.c ... -ldl: PASS
valid PTHREAD_PRIO_PROTECT lock: lock=22, prio_ceiling=1, invalid_ceiling=0
poisoned glibc mutex lock: glibc abort reproduced, pre-abort trace flushed with prio_ceiling=0 invalid_ceiling=1
```

Focused VM proof with the diagnostic enabled:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T004213Z-chromium-video-surfaceless-egl-angle-opengles-kwin-tpp-trace-chrome-int3-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
kde-session: KWin and plasmashell are running
run.log: no Fatal glibc/tpp.c/__pthread_tpp_change_priority rows
kde-kwin-alloc-trace.log: invalid_ceiling_rows=0 pthread_enter_rows=0 path_poison_rows=0 alloc_fail_rows=0 phase_mmap_fail_rows=0 brk_fail_rows=0
chromium_video_post_evidence sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=147 ... status=PASS
chromium_video_post_evidence capture_present_count=12 capture_expected_count=12
chromium_video_post_evidence capture_nonblack_count=12 capture_changed_count=0
chromium_video_post_evidence process_summary=... browser_seen=1 renderer_seen=0 gpu_seen=0 ... browser_render_fd_seen=1 ...
chromium_video_post_evidence chromium_int3_summary=available=1 trap_count=1 pid=147 rip=0x4ad85a17 ip_file_off=0xad84a17 stack3_file_off=0x60d8b19 stack5_file_off=0xad84a36
chromium_video_post_evidence chromium_int3_context=... trap_roles=browser ... gpu_role_seen=0 trap_simdutf="westmere" trap_egl_platform="surfaceless" trap_use_gl="egl-angle" trap_use_angle="opengles"
virtio_gpu_cmd_ctx_submit=77 virtio_gpu_cmd_res_flush=24 virtio_gpu_cmd_res_create_3d=247
```

Interpretation: the TPP diagnostic is staged and catches the exact poisoned
ceiling class on Linux, but the focused xv6 VM lane did not reproduce the KWin
TPP abort. KWin reached virgl/Plasma/Xwayland readiness, and the next live
blocker in this lane is the Chromium browser `int3` before renderer/GPU-process
startup, not a proven scheduler priority-range mismatch. Keep the KWin
diagnostic available for the intermittent startup class, but move the next
behavior-changing work back to a reduced Chromium browser `int3` attribution.

2026-06-28 Chromium SIMDUTF launcher-policy fix:

```text
Code change:
scripts/image/wayland-chromium-launcher.c no longer sets
SIMDUTF_FORCE_IMPLEMENTATION=westmere by default. It still honors the
xv6-owned reducer override WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION.

Build:
cmake --build build-x86_64 --target rootfs-refresh -j2: PASS

KDE VM proof attempt:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T005428Z-chromium-video-simdutf-unset-kde-kwin-loader-pf-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
KWin failed before Chromium launch in the loader/IFUNC path:
kwin_wayland fatal page fault rip=0x7ffffc511ff2, fatal-ret=0x7001033c,
libQt5Qml memmove IFUNC relink warning present.

Headless xv6 guest proof:
QEMU_NET=0 DISPLAY_MODE=nographic QEMU_APPEND='root=/dev/disk0 desktop=0 ...'
/bin/wayland-chromium --version
SIMDUTF-PROOF-UNSET-PASS
wayland-chromium-launcher: env WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION="(unset)"
wayland-chromium-launcher: env SIMDUTF_FORCE_IMPLEMENTATION="(unset)"

Image proof:
debugfs dump /bin/wayland-chromium from build-x86_64/fs.img; strings show
WAYLAND_CHROMIUM_SIMDUTF_FORCE_IMPLEMENTATION and SIMDUTF_FORCE_IMPLEMENTATION
but no westmere string.
```

Interpretation: the earlier launcher default was an xv6-owned policy mismatch.
It is fixed in the staged image. The KDE validation lane is currently blocked
by a separate intermittent KWin loader/IFUNC page fault before Chromium starts;
do not count that KDE failure against the SIMDUTF policy fix.

2026-06-28 KWin/QtQml IFUNC `memmove` closure and Chromium handoff:

```text
Root mismatch:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T005428Z-chromium-video-simdutf-unset-kde-kwin-loader-pf-noimage/
showed libQt5Qml requiring non-IFUNC memmove@GLIBC_2.2.5 while the xv6
IFUNC override/proof only covered memcpy/floor/ceil. The visible symptom was
a KWin loader/startup page fault before Chromium launched.

Code changes, all xv6-owned:
scripts/image/xv6-ifunc-memcpy-shim.c now exports memmove@@GLIBC_2.2.5 and
uses integer-address overlap checks.
scripts/image/kde-qtqml-ifunc-startup-probe.c now resolves and verifies
memmove from libxv6-ifunc-memcpy.so and emits __KDE_QTQML_IFUNC_PASS__ only
after the detailed PASS rows are written.
scripts/gpu/kde-qtqml-ifunc-startup-proof.expect requires the memmove row and
the explicit PASS marker.
scripts/gpu/kde-plasma-desktop-smoke.expect runs KWin/QtQml IFUNC checks
after KDE readiness, validates the compact PASS marker instead of long
console-spliced rows, detaches Chromium reducer background jobs from stdin,
and hardens prompt sync with short repeated KSP markers plus a drain window.

Focused proof:
timeout 420 scripts/gpu/kde-qtqml-ifunc-startup-proof.expect: PASS
kde-qtqml-ifunc-startup-probe.log contains PASS rows for memcpy, memmove,
floor, ceil, QtQml map/result, and __KDE_QTQML_IFUNC_PASS__.
timeout 420 scripts/gpu/kwin-loader-ifunc-proof.expect: PASS
KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS.

Image proof:
build-x86_64/fs.img sha256=13f041b121d8d62c2161b37c2ec708330de43d8a03a9760dc13415cccd63bb46
dumped /opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so sha256=bfa3c569195ac058c5479952b632aa57a883e3d9c1bc18550c28a9ecdef28cc1
objdump exports GLIBC_2.2.5 memmove, GLIBC_2.14 memcpy,
(GLIBC_2.2.5) memcpy, GLIBC_2.2.5 floor, and GLIBC_2.2.5 ceil.

Current Chromium-video handoff proof:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T012707Z-chromium-video-memmove-fixed-chrome-int3-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
KWin/QtQml IFUNC preflight passed; no Relink IFUNC crash signature remained.
OpenGL renderer string: virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))
Xwayland effective_glamor=es +extension_GLX=yes; KWin and plasmashell running.
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=147 ... status=PASS
capture_present_count=12 capture_expected_count=12
capture_nonblack_count=12 capture_changed_count=1 capture_post_nonblack_changed_count=1
process_summary browser_seen=1 renderer_seen=0 gpu_seen=0 browser_render_fd_seen=1 render_fd_seen=1
chromium_int3_summary trap_count=1 pid=161 rip=0x4ad85a17 ip_file_off=0xad84a17
chromium_int3_context trap_roles=browser trap_egl_platform=surfaceless
trap_use_gl=egl-angle trap_use_angle=opengles trap_simdutf=missing
final_sync_status=done fault_count=0
virtio_gpu_cmd_ctx_submit=80 virtio_gpu_cmd_res_flush=25 virtio_gpu_cmd_res_create_3d=247
```

Interpretation: the KWin/Xwayland loader-side IFUNC mismatch is closed for the
current image. The active reducer now reaches accelerated KDE/Xwayland,
launches Chromium, samples/captures successfully, and reduces the remaining
Chromium gap to a browser-process `int3` before renderer/GPU-process startup.
The next behavior-changing work should target that Chromium browser `int3`
attribution, not KWin memmove/QtQml IFUNC or prompt-synchronization artifacts.

2026-06-28 Chromium-video surfaceless tail/reducer refresh:

```text
Harness fixes:
scripts/gpu/kde-plasma-desktop-smoke.expect now waits for KDE readiness before
the Chromium-video KWin/QtQml IFUNC preflight. Independent audit returned GO:
the readiness gate still fails early KWin crashes, and the IFUNC probes still
require explicit PASS evidence before Chromium launch.
The Chromium-video launch-begin and launch-start markers now emit five copies,
matching the existing prompt/final-sync marker style, because a single
KCVBEGIN was observed spliced by concurrent KDE output.

Focused proofs:
timeout 420 scripts/gpu/kwin-loader-ifunc-proof.expect: PASS
timeout 420 scripts/gpu/kde-qtqml-ifunc-startup-proof.expect: PASS
expect Tcl completeness: PASS
git diff --check for the smoke harness and syscall-tail kernel files: PASS

Preserved harness-failure artifacts:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T015211Z-chromium-video-gbm-tail-capture-black-noimage/
raw GBM Chromium policy: capture black, no playback, no syscall tail because no int3.
build-x86_64/kde-plasma-desktop-smoke-history/20260628T015519Z-chromium-video-surfaceless-tail-kwin-loader-ifunc-timeout-noimage/
old ordering ran KWin IFUNC check during KDE startup noise and timed out.
build-x86_64/kde-plasma-desktop-smoke-history/20260628T015940Z-chromium-video-surfaceless-tail-marker-timeout-noimage/
old single KCVBEGIN launch marker was spliced by concurrent KDE output.

Current Chromium evidence artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T020226Z-chromium-video-surfaceless-tail-perf-startup-timeout-static-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fail
launch policy: EGL_PLATFORM=surfaceless, --use-gl=egl-angle,
--use-angle=opengles, multiprocess=1, SIMDUTF unset.
KWin/QtQml IFUNC PASS logs are real, not placeholders.
GBM preprobe PASS and Chromium-shaped matrix unchanged:
pbuffer=0, native-window+pbuffer=0, window-only=40, surfaceless=40.
launch_evidence_status=PASS, sampler_result=PASS, final_sync_status=done.
process_summary: browser_seen=1, renderer_seen=0, gpu_seen=0,
browser_render_fd_seen=1, render_fd_seen=1, zygote_seen=1.
GPU child errors: two transient GPU processes requested gl=none/angle=none and
exited during initialization; later GPU-process warning appeared but sampler did
not classify a stable gpu role.
perf_media_summary: perf_count=25, playing_count=4, result_fail_count=1,
last_time=4.003, duration=16.000, presented=73, decoded=249, dropped=165.
perf_result: startup-timeout phase=play-promise at startupMs=8000, ready=1,
time=0, duration=16, presented=0, decoded=19, dropped=13.
capture: 12/12 frames present, all nonblack, changed_count=0.
virtio trace: ctx_submit=339, res_flush=270, set_scanout=270,
res_create_3d=272, res_xfer_toh_3d=2.
chromium_int3_summary trap_count=0; chromium_fault_summary fault_count=0;
chromium_syscall_tail=available=0 because no int3/fault dumped the tail.
```

Interpretation: the immediate Chromium stress gap has moved past launch and
past the prior browser `int3`. The current surfaceless/egl-angle/opengles lane
loads and starts the local video, eventually reaches `playing`, and produces
nonblack captures, but the page's 8s startup gate fires just before the play
promise resolves and the captured surface remains static. The next reducer
should quantify why the play promise is late and why KWin/Wayland capture is
static despite Chromium reporting presented/decoded frames: increase only the
page startup metric or add a page-side `rvfc`/present cadence metric first,
then compare with a Linux VM control before a behavior-changing kernel patch.
Do not chase syscall-tail output in this lane until a fresh int3/fault occurs.

2026-06-28 RVFC/post-timeout metric refresh:

```text
Audit and build gates:
Independent audit of the Chromium-video fixture/harness changes: GO.
The audit verified no guest xv6 shell `$?` status dependency, local-video URL
propagation through `--chromium-url`, explicit sampler/GBM PASS requirements,
and parser coverage for the new per-tick RVFC fields.
expect Tcl completeness: PASS
git diff --check for the smoke harness, perf-video fixture, plan, launcher, and
kernel diffs: PASS
cmake --build build-x86_64 --target rootfs-refresh -j2: PASS
debugfs confirmed `rvfcMetricFields`, `rvfcCount`, `postFailObserveMs`,
`startup-timeout`, `postfail-tick`, and `rvfc-summary` in `fs.img`.

Focused Chromium-video run:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T023416Z-chromium-video-rvfc-capture-incomplete-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-incomplete
command policy: EGL_PLATFORM=surfaceless, --use-gl=egl-angle,
--use-angle=opengles, multiprocess=1, GBM preprobe=1, startupMs=8000,
postFailObserveMs=12000, result wait=40s.

Key evidence:
launcher_policy_summary: final_argc=30 use_gl=egl-angle use_angle=opengles
target=file:///share/webkit/perf-video.html?asset=perf-1280x800-60fps.mp4&ms=15000&startupMs=8000&postFailObserveMs=12000&hud=1
host_egl_gbm_smoke_result: phase=result status=PASS api=gles3
GBM/Chromium shape: pbuffer=0, native-window+pbuffer=0, window=50,
surfaceless=50.
sampler_result: samples=63 duration_ms=18000 status=PASS
process_summary: browser_seen=1, renderer_seen=0, gpu_seen=0,
browser_render_fd_seen=1, render_fd_seen=1, zygote_seen=1.
process_policy_summary: browser EGL/use-gl/use-angle carried; no stable GPU
role carried the policy.
GPU child errors: two transient `gl=none,angle=none` GPU processes exited.
chromium_int3_summary trap_count=0; chromium_fault_summary fault_count=0.

RVFC/playback metrics:
perf_result: RESULT fail startup-timeout at startupMs=8000, ready=0,
time=0.000, decoded=0, dropped=0.
play promise later resolved at wall=10.22s, ready=4, time=0.357,
presented=1, decoded=28, dropped=17.
event:playing arrived at time=5.171 with presented=91, decoded=318,
dropped=213.
postfail summary: rvfc_count=228, rvfc_span_ms=10114.246,
rvfc_avg_gap_ms=44.556, rvfc_max_gap_ms=706.900, rvfc_last_media=9.550,
rvfc_media_span=9.250, rvfc_meta_presented=367.
final observed media state: time=9.598, duration=16.000, ready=4,
presented=228, decoded=584, dropped=389.

Capture metrics:
capture_status=DONE samples=12, but frame 06 was zero bytes, so
capture_present_count=11/12.
11/12 captures were nonblack, first_nonblack_index=0.
All present captures had identical ImageMagick mean/stddev and AE delta 0, so
capture_changed_count=0 and capture_post_nonblack_changed_count=0.
virtio trace: ctx_submit=363, res_flush=304, set_scanout=304,
fence_ctrl/fence_resp=363/363, res_create_3d=238, res_xfer_toh_3d=2.
```

Interpretation: the new page-side RVFC fields did their job. The run proves
that the 8s startup gate is too tight for this Chromium/KDE/virgl path under
the current policy: playback becomes ready and advances only after the
startup-timeout result has already been emitted. The more important bottleneck
is not launch, not the prior int3, and not GBM pbuffer shape; it is late
local-file video readiness plus high drop/jitter after recovery
(`rvfc_avg_gap_ms` about 44.6 ms, max about 706.9 ms, dropped 389/584 decoded
frames by 9.6s media time). The capture side also needs a harness-side retry or
classification improvement for transient `fbstat` zero-byte frames, but it
should not mask an explicit page `RESULT fail`.

Harness diagnosis follow-up: after this run, the post-evidence classifier was
adjusted so an explicit `PERF-VIDEO RESULT fail <reason>` wins over capture
completeness once launch, GBM, sampler, crash, and launch-only gates have
passed. Future runs should report this class as
`chromium-video-perf-result-startup-timeout` instead of
`chromium-video-capture-incomplete`. Independent audit of that ordering change
returned GO, and `git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect`
was clean. This is a harness diagnosis fix only; it does not change guest
runtime behavior.

2026-06-28 KWin loader IFUNC parser mismatch fix:

```text
Preserved artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T024653Z-chromium-video-startup12-kwin-loader-parser-miss-noimage/

Symptom:
run.log had the `KDE_DLOPEN_PROBE_KWIN_HELP_RESULT ... status=PASS` token
interleaved with unrelated D-Bus output, so the serial Expect matcher reported
`kwin-loader-ifunc-missing-help`.

Durable evidence:
kde-kwin-loader-ifunc.log:
KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS exit_status=0 signaled=0 signal=0 saw_usage=1 saw_title=1 saw_options=1 captured_bytes=4686
```

The smoke harness now dumps and parses the clean guest `/kli.log` artifact at
the KWin loader IFUNC decision point before consulting the noisy serial
`run.log`. The fallback still requires the explicit PASS result line and does
not depend on xv6 `sh` `$?`; missing or non-PASS guest artifacts continue to
fail as before. Static Tcl completeness and `git diff --check` for the smoke
harness and kernel diffs passed after the patch.

Verification run after the parser fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T025138Z-chromium-video-startup12-kwin-parser-fixed-perf-fps-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fps-27.8

KWin loader IFUNC: KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS ...
QtQml IFUNC: __KDE_QTQML_IFUNC_PASS__
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3
sampler_result: kde_app_launch_probe chromium_sample_only=1 ... status=PASS
capture_status=status=DONE samples=12; capture_present_count=12/12
chromium_int3_summary trap_count=0; chromium_fault_summary fault_count=0
```

The old `kwin-loader-ifunc-missing-help` mismatch is closed for this lane:
the harness reached Chromium-video post-evidence and classified from the page
result. With `startupMs=12000`, startup no longer failed; the page advanced to
the end and failed the performance criterion instead:

```text
perf_result: RESULT fail fps=27.8 speed=0.944 presentedFPS=27.8 decodedFPS=61.3 dropPct=66.56 advanced=14.32
perf_media_summary: last_rvfc_count=422 last_rvfc_avg_gap_ms=39.698 last_rvfc_max_gap_ms=1231.860 last_presented=440 last_decoded=960 last_dropped=645
process_summary: browser_seen=1 gpu_seen=1 renderer_seen=0 browser_render_fd_seen=1 render_fd_seen=1
process_policy_summary: browser and GPU process carried EGL_PLATFORM=surfaceless, --use-gl=egl-angle, --use-angle=opengles
```

Interpretation: the current Chromium-video surfaceless ANGLE lane is now past
the loader/startup mismatch and exposes the real playback gap: low presented
FPS, large RVFC gaps, and high frame drops. Capture remained static even though
all frames were present and nonblack, so the next behavior work should target
Chromium/GPU-process video pacing or the compositor/present path with a reducer
before changing kernel DRM behavior.

2026-06-28 Chromium-video depth-2 regression probe after GLX stats win:

```text
First attempt artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T034126Z-chromium-video-surfaceless-depth2-clear-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-clear-timeout
cmdline used virtio_gpu_async_submit_depth=2 virtio_gpu_submit_hot_shape_stats=1
launch policy requested EGL_PLATFORM=surfaceless, --use-gl=egl-angle,
--use-angle=opengles, multiprocess=1, startupMs=12000.
Chromium evidence: not launched; host_egl_gbm_log/status missing; sampler and
capture missing; process_summary browser_seen=0.
run.log showed the clear command/marker line interleaved with Plasma output.
```

Harness follow-up: the Chromium-video prelaunch clear phase now emits the opaque
`KCVCDONE` marker five times, uses `async_marker_re`, redirects the clear script
stdin from `/dev/null`, and allows 60 seconds. Independent audit returned GO:
the change does not depend on `$?`, preserves panic/fatal-fault/spinlock/crash,
EOF, and timeout detection, and is appropriate because the clear phase is
best-effort artifact cleanup before later explicit Chromium evidence checks.

Depth-2 Chromium retry artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T034613Z-chromium-video-surfaceless-depth2-capture-static-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
launcher_policy_summary: final_argc=30 use_gl=egl-angle use_angle=opengles
target=file:///share/webkit/perf-video.html?asset=perf-1280x800-60fps.mp4&ms=15000&startupMs=12000&postFailObserveMs=12000&hud=1
egl_platform=surfaceless ld_preload_ifunc=1
host_egl_gbm_smoke_result: phase=result status=PASS api=gles3 exit_status=0
GBM/Chromium shape: pbuffer=0, native-window+pbuffer=0, window=50,
surfaceless=50.
sampler_result: kde_app_launch_probe chromium_sample_only=1 samples=63
duration_ms=18000 interval_ms=500 burst_samples=26 status=PASS
capture_status=status=DONE samples=12; capture_present_count=12/12
capture_nonblack_count=12; capture_changed_count=0
frame stats: every PPM was 3072016 bytes, nonblack, mean=0.630577,
stddev=0.25475, and AE delta 0 after the first frame.
process_summary: browser_seen=1, renderer_seen=0, gpu_seen=0, zygote_seen=1,
crashpad_seen=1, render_fd_seen=1, browser_render_fd_seen=1.
process_policy_summary: browser carried EGL_PLATFORM=surfaceless,
--use-gl=egl-angle, --use-angle=opengles; no sampled GPU role carried policy.
gpu_init_error_count=0 gpu_config_error_count=0 gpu_exit_error_count=2
gl_request_error_count=2 gl_request_none_count=2
chromium_int3_summary trap_count=0; chromium_fault_summary fault_count=0.
perf_startup: wall=9.68 ready=1 presented=0 decoded=5 dropped=0
perf_play: wall=9.93 ready=2 time=0.223 presented=1 decoded=19 dropped=13
perf_playing: time=1.241 presented=10 decoded=81 dropped=50
perf_tick_last: wall=10.83 time=11.082 presented=315 decoded=671 dropped=420
rvfc_count=315 rvfc_span_ms=11337.400 rvfc_avg_gap_ms=36.106
rvfc_max_gap_ms=287.200 rvfc_last_media=11.033 rvfc_media_span=10.967
QEMU trace: ctx_submit=429 set_scanout=360 res_flush=360 fence_ctrl/fence_resp=429/429
```

Interpretation: depth 2 remains a strong KDE/Xwayland GLX reducer candidate,
but it is not enough to close Chromium. The Chromium stress lane no longer
crashes and no longer hits the 12s startup timeout, and RVFC jitter improved
relative to the prior startup-12 proof (`rvfc_avg_gap_ms` 36.106 vs 39.698,
`rvfc_max_gap_ms` 287.2 vs 1231.86). However, the host captures are still
static while Chromium reports advancing media, and this run did not sample a
stable GPU-process role carrying the EGL/ANGLE policy. Do not promote
`virtio_gpu_async_submit_depth=2` as a default yet. The next reducer should
focus on the mismatch between Chromium's page-side presented frames and the
unchanged Wayland/KWin/scanout captures, plus why GPU child initialization still
falls through transient `gl=none,angle=none` processes in the depth-2 lane.

2026-06-28 diagnostic scanout-read source fix:

```text
Patch: FB_GPU_SCANOUT_READ now prefers the active KMS framebuffer readback on
virtio-backed displays and falls back to the old virtio scanout read only when
no usable current KMS FB exists.

Proof artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T035557Z-chromium-video-surfaceless-depth2-kms-readback-perf-result-missing-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-missing
cmdline included virtio_gpu_async_submit_depth=2,
virtio_gpu_submit_hot_shape_stats=1, virtio_gpu_scanout_read_diag=1.

run.log scanout lineage:
12/12 visible FB_GPU_SCANOUT_READ diagnostics used
"FB: scanout-read used current KMS ..." with changing fb/resource identities:
fb=120/126/128/139/144/145, resource=14/4/5.

capture_status=status=DONE samples=12
capture_present_count=12/12; capture_nonblack_count=12
capture_changed_count=2; capture_post_nonblack_changed_count=2
frame hashes grouped as 0-2, 3-6, and 7-11 instead of one identical hash.
```

Interpretation: the stale guest capture mismatch from the prior artifact is
partly closed at the diagnostic readback layer. `fbstat ppm-current` no longer
samples one unchanging virtio scanout backing while a current KMS FB exists; it
follows active KMS framebuffer changes. This run did not prove Chromium video
content capture, because Chromium never reached a page result (`currentSrc` was
empty at `before-play`, GPU child init failed, and the final host screenshot was
the desktop). Treat the next Chromium failure as a launch/playback/GPU-process
policy problem, not as the old single-buffer screenshot-readback bug. A paired
QEMU `screendump` vs guest `fbstat ppm-current` harness probe is still the best
diagnostic if capture and host display disagree again.

2026-06-28 trap VMA path diagnostic mismatch fix:

```text
Patch:
- `print_fault_vma()` now keeps the existing `name=` field and also emits
  `path=` from `vma_debug_path()`. This fixes the diagnostic mismatch where a
  Chrome mapping with `file->opened_path` still printed `name=(unnamed)` only.
- Chromium-video post-evidence now preserves the new field as `ip_path=...`
  in `chromium_int3_summary`.

Static/build checks:
expect Tcl completeness: complete
git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect: PASS
git -C kernel diff --check -- arch/x86_64/irq/trap.c: PASS
cmake --build build-x86_64 --target kernel -j2: PASS
```

Proof artifacts:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T040816Z-chromium-video-launch-only-path-field-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1
post-evidence: chromium_int3_summary includes ip_path=missing with trap_count=0,
proving the harness summary field is present and backward-compatible when no
trap fires.

build-x86_64/kde-plasma-desktop-smoke-history/20260628T041050Z-chromium-video-surfaceless-path-field-perf-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-startup-timeout
startup_ms=12000
chromium_extra_flags="--use-gl=egl-angle --use-angle=opengles"
launcher_policy_summary: use_gl=egl-angle use_angle=opengles
EGL_PLATFORM=surfaceless argv_use_gl_count=1 argv_use_angle_count=1
host_egl_gbm_smoke_result: PASS api=gles3
sampler_result: PASS samples=63 duration_ms=18000
capture_present_count=4/4; capture_nonblack_count=4; capture_changed_count=0
chromium_int3_summary: trap_count=0 ip_path=missing
perf_media_summary: result_fail_count=1 last_time=3.250 ready=4
last_presented=46 last_decoded=202 last_dropped=134 currentSrc=local mp4
process_summary: browser and zygote seen; render fd seen; no renderer/GPU role
process_policy_summary: browser carried EGL_PLATFORM=surfaceless and
egl-angle/opengles policy; sampled GPU role absent
QEMU trace: ctx_submit=251 set_scanout=183 res_flush=183
fence_ctrl/fence_resp=251/251 res_create_3d=278 res_xfer_toh_3d=2
```

Interpretation: the diagnostic mismatch itself is fixed. A fresh `int3` will
now identify the VMA path directly in both `run.log` and post-evidence instead
of forcing inference from `file_off` and inode name. The two proof runs did not
reproduce the old Chrome browser `int3`; the full surfaceless lane advanced far
enough to decode/present frames internally, then failed the page result with a
static nonblack framebuffer capture. The immediate Chromium gap is therefore
back to the page-side presented-frame vs KWin/scanout/capture mismatch, plus
why no stable sampled GPU-process role appears with the EGL/ANGLE policy. Do
not add behavior-changing DRM/kernel patches from the old `name=(unnamed)`
evidence alone.

2026-06-28 KWin fault IP/return-path mismatch fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T042554Z-chromium-video-metrics-kwin-libkwin-ip-libqt-ret-crash-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
kde-exception-fatal-summary: pid=49 name=kwin_wayland cr2=0x8 err=0x4
exception-ip: file_off=0x1fe3db path=/usr/lib/x86_64-linux-gnu/libkwin.so.5
fatal-ret: file_off=0x312e16 path=/usr/lib/x86_64-linux-gnu/libQt5Core.so.5
```

Patch: `chromium_video_fault_summary` now records both the actual faulting
instruction VMA (`ip_addr`, `ip_file_off`, `ip_name`, `ip_path`) and the first
return-address VMA (`ret_addr`, `ret_file_off`, `ret_name`, `ret_path`). This
fixes the evidence mismatch where the post-evidence fault summary could point
debugging at the QtCore return site while the crashing instruction was in
KWin itself.

Static/probe checks:

```text
expect Tcl completeness: complete
git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect: PASS
git -C kernel diff --check: PASS
fault_path_parser_probe ip_path=/usr/lib/x86_64-linux-gnu/libkwin.so.5 ip_file_off=0x1fe3db ret_path=/usr/lib/x86_64-linux-gnu/libQt5Core.so.5 ret_file_off=0x312e16
```

Interpretation: the current blocker is pre-Chromium KDE session stability, not
Chromium playback. The next reducer should use the existing KWin/QtQml/IFUNC
startup probes and the new IP/return split to reduce why KWin reaches a null
call at `cr2=0x8`; avoid treating the QtCore return address alone as the root
cause.

2026-06-28 KWin low-null object-slot diagnostic and GLX datapoint:

```text
Offset attribution:
libkwin.so.5.27.11 0x1fe3db:
  KWin::EffectsHandlerImpl::checkInputWindowStacking()
  mov 0x8(%rax),%ecx
  prior instruction: mov 0x88(%rdi),%rax

libQt5Core.so.5.15.13 0x312e16:
  QObject::setProperty(char const*, QVariant const*) return site
```

Patch: `kde_dump_fatal_page_fault_summary()` now emits a diagnostic-only
`fatal-lowptr-object-slots` block for user faults below one page, dumping
`fatal-rdi+0x80`, `fatal-rdi+0x88`, `fatal-rdi+0x90`, and `fatal-rdi+0x98`.
For the observed KWin crash this directly measures whether the
`EffectsHandlerImpl` member loaded from `rdi+0x88` is null at the fault. This
does not change VM, scheduler, DRM, KWin, Qt, Xwayland, Mesa, or Chromium
behavior.

Static/build checks:

```text
git -C kernel diff --check -- arch/x86_64/irq/trap.c: PASS
git diff --check -- docs/linux-drm-abi-compat-plan.md scripts/gpu/kde-plasma-desktop-smoke.expect: PASS
cmake --build build-x86_64 --target kernel -j2: PASS
```

Focused proof before the full KDE retry:

```text
timeout 420 scripts/gpu/kwin-global-slot-proof.expect
KWIN-GLOBAL-SLOT-PROOF-PASS
```

Fresh KDE/Xwayland GLX reducer with Chromium out of scope:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T043234Z-x11-glx-fps-lowptr-diag-pass-fps20-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
renderer: virgl (D3D12 (Intel(R) UHD Graphics))
Xwayland: glamor=auto effective_glamor=es +extension_GLX=yes
glx_fps_result: frames=104 elapsed_seconds=5.046919 fps=20.607
glx_fps_timing: draw_total_ms=3592.962 swap_total_ms=1435.994
avg_swap_ms=13.808 max_swap_ms=30.369 max_draw_ms=130.502
QEMU trace: ctx_submit=476 set_scanout=127 res_flush=127
fence_ctrl/fence_resp=476/476 res_create_3d=56 res_xfer_toh_3d=2
```

Interpretation: the KWin low-null crash did not reproduce in the focused GLX
lane, so the new `fatal-rdi+0x88` metric remains armed for the next crash.
The old isolated global-slot hypothesis is still closed. The current GLX
baseline on the Intel WSL D3D12 lane is 20.607 FPS with probe draw time larger
than swap time and no Present/syscall trace attribution in this baseline
variant. The next performance iteration should run a traced GLX variant on the
same host adapter, then compare draw/swap/ioctl attribution before changing
kernel DRM behavior.

2026-06-28 depth-2 hot-shape stats-only GLX proof on the same Linux VM lane:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T043836Z-x11-glx-fps-depth2-hot-shape-stats-pass-fps110-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
renderer: virgl (D3D12 (Intel(R) UHD Graphics))
run shape: kde_xwayland_glamor=auto kde_xwayland_enable_glx=1
run shape: kde_x11_egl_glx_fps_variant=swap-interval0-swap-only
run shape: virtio_gpu_async_submit_depth=2 virtio_gpu_submit_hot_shape_stats=1
glx_fps_swap_interval: requested=0 before=1 after=0 status=PASS
glx_fps_result: frames=300 elapsed_seconds=2.722628 fps=110.188
glx_fps_timing: swap_total_ms=2641.555 avg_swap_ms=8.805 max_swap_ms=45.548
hot-shape: submit_calls=299 posted=299 retired=299
hot-shape: depth_max=2 count_max=2 wait_count_max=2 post_count_max=2
hot-shape: stalls=66 wait_us=313969 max_wait_us=17056
hot-shape: failures=0 mixed=0 owner_drops=0
QEMU trace: ctx_submit=769 set_scanout=77 res_flush=77
QEMU trace: fence_ctrl/fence_resp=769/769 res_create_3d=43 res_xfer_toh_3d=2
```

Follow-up depth-2 GLX proofs with the same swap-interval0 swap-only variant:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T063109Z-x11-glx-fps-depth2-hot-shape-current-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
renderer: virgl (D3D12 (Intel(R) UHD Graphics))
run shape: virtio_gpu_async_submit_depth=2 virtio_gpu_submit_hot_shape_stats=1
glx_fps_result: frames=300 elapsed_seconds=2.406241 fps=124.676
glx_fps_timing: swap_total_ms=2342.951 avg_swap_ms=7.810 max_swap_ms=60.713
hot-shape: owners=1 owner_drops=0 submit_calls=299 posted=299 retired=299
hot-shape: depth_max=2 count_max=2 wait_count_max=2 post_count_max=2
hot-shape: stalls=56 wait_us=87465 max_wait_us=10455
hot-shape: failures=0 mixed=0
QEMU trace: ctx_submit=785 set_scanout=83 res_flush=83
QEMU trace: fence_ctrl/fence_resp=785/785 res_create_3d=59 res_xfer_toh_3d=2
```

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T064726Z-x11-glx-fps-depth2-hot-shape-long-sample-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
renderer: virgl (D3D12 (Intel(R) UHD Graphics))
run shape: kde_x11_egl_glx_fps_target_ms=5000 kde_x11_egl_glx_fps_max_frames=10000
run shape: virtio_gpu_async_submit_depth=2 virtio_gpu_submit_hot_shape_stats=1
glx_fps_result: frames=671 elapsed_seconds=5.007277 fps=134.005
glx_fps_timing: swap_total_ms=4885.841 avg_swap_ms=7.281 max_swap_ms=184.012
hot-shape: owners=2 owner_drops=0 submit_calls=671 posted=671 retired=671
hot-shape: depth_max=2 count_max=2 wait_count_max=2 post_count_max=2
hot-shape: stalls=136 wait_us=215904 max_wait_us=14550
hot-shape: failures=0 mixed=0
QEMU trace: ctx_submit=1636 set_scanout=138 res_flush=138
QEMU trace: fence_ctrl/fence_resp=1636/1636 res_create_3d=53 res_xfer_toh_3d=2
```

The GLX target-duration and max-frame controls are xv6-owned diagnostic
harness knobs; their defaults remain unchanged. These artifacts strengthen the
same conclusion as the first depth-2 GLX proof but still do not promote depth 2
to default behavior. Chromium-video remains the stress gate, and the GBM Linux
control can only use raw config-count deltas after the harness reports
`config_count_comparable=yes` for a matching Mesa identity.

Apples-to-apples default-depth stats-only control:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T032846Z-x11-glx-fps-hot-shape-stats-pass-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=x11-glx-fps session_probe=PASS
variant=swap-interval0-swap-only
glx_fps_result: frames=238 elapsed_seconds=5.035623 fps=47.263
glx_fps_timing: swap_total_ms=4959.076 avg_swap_ms=20.836 max_swap_ms=61.858
hot-shape: submit_calls=238 posted=238 retired=238
hot-shape: depth_max=1 count_max=1 wait_count_max=1 post_count_max=1
hot-shape: stalls=204 wait_us=2116554 max_wait_us=25001
hot-shape: failures=0 mixed=0 owner_drops=0
QEMU trace: ctx_submit=655 set_scanout=81 res_flush=81
QEMU trace: fence_ctrl/fence_resp=655/655 res_create_3d=53 res_xfer_toh_3d=2
```

Interpretation: after fixing the host kernel-source mismatch and validating the
Linux VM lane, the low-perturbation stats-only evidence shows the submit-depth
candidate directly addresses the GLX swap-only hot shape on this host/adapter:
effective hot-shape depth reaches 2, FPS rises from 47.263 to 110.188, average
swap time drops from 20.836 ms to 8.805 ms, and no hot-shape failures, mixed
owners, or owner drops were recorded. The proof deliberately avoided the broad
submit trace hooks that previously depressed FPS. Keep depth 2 opt-in until the
Chromium-video stress lane passes with explicit sampler, capture, GPU/EGL, and
role evidence; do not make this a default kernel behavior from GLX alone.

2026-06-28 low-noise Chromium-video depth-2 retry preflight result:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T044811Z-chromium-video-depth2-qtqml-ifunc-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-qtqml-ifunc-startup-timeout
cmdline included virtio_gpu_async_submit_depth=2,
virtio_gpu_submit_hot_shape_stats=1, virtio_gpu_scanout_read_diag=1,
chrome_drm_ioctl_trace=0, and chrome_drm_fence_trace=0.
KWin loader IFUNC: PASS.
QtQml IFUNC guest log stayed at the staged placeholder.
Chromium evidence: not launched; GBM preprobe did not run; sampler/capture did
not run; post-evidence reason=not-launched.
```

The focused current-image QtQml IFUNC proof still passes:

```text
timeout 420 scripts/gpu/kde-qtqml-ifunc-startup-proof.expect
KDE-QTQML-IFUNC-STARTUP-PROOF-PASS
KDE_QTQML_IFUNC_STARTUP_PROBE_RESULT status=PASS
KDE_QTQML_IFUNC_STARTUP_PROBE_SYMBOL status=PASS name=memmove
lib=/opt/xv6-kde-abi-libs/libxv6-ifunc-memcpy.so
status=DONE source=kde-qtqml-ifunc-startup-probe
```

Harness-only follow-up: the full KDE smoke QtQml preflight now mirrors the
completion-status shape used by other probes. It stages `/kqi.status`, writes
`status=running` before launching `/bin/kde-qtqml-ifunc-startup-probe`, writes
`status=DONE source=kde-qtqml-ifunc-startup-probe` after the probe, echoes the
compact probe log into `run.log`, and dumps both `/kqi.log` and `/kqi.status`
on finish. Independent audit returned GO: the change preserves crash detection,
does not rely on xv6 sh `$?`, and does not change KDE, Qt, KWin, Xwayland,
Mesa, Chromium, or kernel behavior. The next Chromium-video VM retry should use
the same low-noise command and classify a repeat timeout from `/kqi.status`
rather than from a placeholder log alone.

That retry is no longer blocked on the QtQml preflight. It reached Chromium and
failed later as a browser-side `int3` crash:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T065315Z-chromium-video-depth2-gbm-chrome-int3-fail-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-chrome-crash-regression
run shape: chromium-video, multiprocess=1, EGL_PLATFORM=gbm,
run shape: virtio_gpu_async_submit_depth=2,
virtio_gpu_submit_hot_shape_stats=1, chrome_drm_ioctl_trace=1,
chrome_drm_fence_trace=1
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke phase=result status=PASS
host_egl_gbm_status: status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
window_es3_count=50 window_es2_count=50
surfaceless_es3_count=50 surfaceless_es2_count=50
sampler_result: kde_app_launch_probe chromium_sample_only=1 status=PASS
capture_log_summary: begin_count=12 end_count=12 fb_ppm_count=12 done_count=1
capture_frame_hash_summary: present_count=12 hash_count=12 unique_hash_count=2
process_summary: browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1
process_summary: drm_fd_seen=1 render_fd_seen=1 browser_render_fd_seen=1
process_policy_summary: EGL_PLATFORM=gbm, --use-gl=egl-angle,
--use-angle=opengles, no simdutf override
crash_match: pid 216 chrome: breakpoint trap rip=0x4ad85a17
chromium_int3_summary: trap_count=1 ip_file_off=0xad84a17
chromium_int3_context: trap_roles=browser gpu_role_seen=0
perf_media_summary: perf_count=0 playing_count=0 result_count=0
QEMU trace: ctx_submit=78 set_scanout=24 res_flush=24
QEMU trace: fence_ctrl/fence_resp=78/78 res_create_3d=247 res_xfer_toh_3d=2
```

Interpretation: the harness status-marker fixes held up under the full reducer:
GBM preprobe and sampler PASS lines were required and recorded independently of
shell exit status. The Chromium gap is now narrower than playback jitter: the
browser process traps before any renderer or GPU role appears and before the
fixture emits media ticks. Next work should reduce the browser `int3` at
`ip_file_off=0xad84a17` with xv6-owned probes or launch policy, then rerun the
same depth-2 Chromium-video gate. Do not treat the successful GLX depth-2
evidence as default-behavior approval until this browser-startup crash and the
later playback/capture metrics both pass.

Diagnostic retry with Chromium EGL trace and the GBM context-attempt matrix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T070105Z-chromium-video-depth2-gbm-egltrace-attempts-fps30-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fps-30.0
run shape: chromium-video, multiprocess=1, EGL_PLATFORM=gbm,
run shape: KDE_SMOKE_CHROMIUM_EGL_TRACE=1,
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_ATTEMPTS=1,
virtio_gpu_async_submit_depth=2, virtio_gpu_submit_hot_shape_stats=1,
chrome_drm_ioctl_trace=0, chrome_drm_fence_trace=0
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke phase=result status=PASS
host_egl_gbm_status: status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
egl_chromium_context_attempts:
  pbuffer_es3=FAIL/no_config pbuffer_es2=FAIL/no_config
  native_window_pbuffer_es3=FAIL/no_config
  native_window_pbuffer_es2=FAIL/no_config
  window_gbm_es3=PASS/ok window_gbm_es2=PASS/ok
  window_surfaceless_es3=PASS/ok window_surfaceless_es2=PASS/ok
  surfaceless_es3=PASS/ok surfaceless_es2=PASS/ok
egl_chromium_context_ready=1 reason=window-or-surfaceless-pass
sampler_result: kde_app_launch_probe chromium_sample_only=1 status=PASS
process_summary: browser_seen=1 gpu_seen=1 renderer_seen=0
process_summary: drm_fd_seen=1 render_fd_seen=1 browser_render_fd_seen=1
process_policy_summary: simdutf_count=0, gpu_use_gl_count=2,
gpu_use_angle_count=2
chromium_int3_summary: trap_count=0
chromium_fault_summary: fault_count=0
chromium_egl_trace_summary:
  init=8 process=7 process_browser=1 process_gpu=3 process_zygote=2
  process_gpu_env_egl_platform=3 process_gpu_env_ld_preload_trace=3
  dlopen=9 choose=0 create_context=0 create_pbuffer=0 create_window=0
  first_gpu_process includes --use-gl=egl-angle --use-angle=opengles
  last_gpu_process includes --use-gl=disabled --use-angle=opengles
gpu errors: gpu_exit_error_count=2 gpu_error_pids=453,578
gl_request_error_count=2 gl_request_none_count=2
perf_media_summary: playing_count=5 tick_count=28 result_fail_count=1
rvfc_summary: count=456 span_ms=15693.500 avg_gap_ms=34.491
metrics: presented=456 presentedFPS=29.97 decoded=949 decodedFPS=62.38
metrics: dropped=593 dropPct=62.49 advanced=15.07 speed=0.990 wall=15.21
capture_frame_hash_summary: present_count=12 hash_count=12 unique_hash_count=1
QEMU trace: ctx_submit=466 set_scanout=389 res_flush=389
QEMU trace: fence_ctrl/fence_resp=466/466 res_create_3d=267 res_xfer_toh_3d=2
```

Interpretation: the previous browser `int3` is not the only current blocker.
With EGL tracing and the direct GBM context-attempt reducer enabled, Chromium
reached media playback and produced a quantitative failure: decoded FPS stayed
near 60, presented FPS stayed near 30, and 62.49% of decoded frames were
dropped. The direct GBM matrix proves pbuffer and native-window+pbuffer configs
are absent, while window-only and surfaceless ES2/ES3 contexts work in the
same boot. Chromium's sampled GPU-process command line also flips from an
`egl-angle` GPU process to a later `--use-gl=disabled` GPU process, and the
EGL preload records process/env/dlopen rows but no intercepted
`eglChooseConfig` or `eglCreate*` entrypoints. Next bottleneck work should stay
numeric: compare a low-perturbation no-EGL-trace playback run against this
trace run, and/or enable scanout-read/virtgpu present diagnostics to decide
whether the 30 FPS ceiling is Chromium GPU fallback, compositor/scanout
presentation, or trace/preload perturbation. Do not default depth 2 from this
run; it proves progress and a measurable playback bottleneck, not success.

Low-perturbation comparison without Chromium EGL trace or context-attempts:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T070753Z-chromium-video-depth2-gbm-noegltrace-fps34-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fps-34.7
run shape: chromium-video, multiprocess=1, EGL_PLATFORM=gbm,
run shape: KDE_SMOKE_CHROMIUM_EGL_TRACE=0,
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_ATTEMPTS=0,
virtio_gpu_async_submit_depth=2, virtio_gpu_submit_hot_shape_stats=1,
chrome_drm_ioctl_trace=0, chrome_drm_fence_trace=0
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke phase=result status=PASS
host_egl_gbm_status: status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
egl_chromium_context_attempts: missing by design in this run
sampler_result: kde_app_launch_probe chromium_sample_only=1 status=PASS
process_summary: browser_seen=1 gpu_seen=1 renderer_seen=0
process_summary: drm_fd_seen=1 render_fd_seen=1 browser_render_fd_seen=1
process_policy_summary: simdutf_count=0, browser/gpu EGL_PLATFORM=gbm,
browser --use-gl=egl-angle --use-angle=opengles
gpu errors: gpu_exit_error_count=2
gl_request_error_count=2 gl_request_none_count=2
chromium_int3_summary: trap_count=0
chromium_fault_summary: fault_count=0
perf_media_summary: playing_count=6 tick_count=27 result_fail_count=1
rvfc_summary: count=527 span_ms=15784.740 avg_gap_ms=30.009
metrics: presented=527 presentedFPS=34.69 decoded=934 decodedFPS=61.48
metrics: dropped=582 dropPct=62.31 advanced=14.73 speed=0.969 wall=15.19
capture_frame_hash_summary: present_count=12 hash_count=12 unique_hash_count=1
QEMU trace: ctx_submit=473 set_scanout=404 res_flush=404
QEMU trace: fence_ctrl/fence_resp=473/473 res_create_3d=271 res_xfer_toh_3d=2
```

Comparison to the EGL-trace/context-attempt run: removing the EGL trace preload
and context-attempt matrix did not restore 60 FPS. Presented FPS improved only
from 29.97 to 34.69 while decoded FPS stayed near 60 and drop rate stayed near
62%. The browser `int3` did not recur, so the current dominant evidence is a
playback/presentation bottleneck, not instrumentation overhead alone. Next
work should add low-perturbation scanout/readback/present diagnostics or a
Linux KVM+virgl Chromium shape comparison before changing xv6 DRM behavior.

Scanout-read diagnostic comparison for the same GBM/no-EGL-trace lane:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T071517Z-chromium-video-depth2-gbm-scanoutdiag-fps21-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fps-21.6
run shape: chromium-video, multiprocess=1, EGL_PLATFORM=gbm,
run shape: KDE_SMOKE_CHROMIUM_EGL_TRACE=0,
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_ATTEMPTS=0,
virtio_gpu_async_submit_depth=2, virtio_gpu_submit_hot_shape_stats=1,
virtio_gpu_scanout_read_diag=1, chrome_drm_ioctl_trace=0,
chrome_drm_fence_trace=0
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke phase=result status=PASS
egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
sampler_result: kde_app_launch_probe chromium_sample_only=1 status=PASS
gpu errors: gpu_exit_error_count=2
gl_request_error_count=2 gl_request_none_count=2
process_summary: browser_seen=1 gpu_seen=0 renderer_seen=0
process_summary: drm_fd_seen=1 render_fd_seen=1 browser_render_fd_seen=1
process_policy_summary: browser EGL_PLATFORM=gbm, browser --use-gl=egl-angle
and --use-angle=opengles; no sampled GPU EGL/ANGLE policy
chromium_int3_summary: trap_count=0
chromium_fault_summary: fault_count=0
perf_media_summary: playing_count=11 tick_count=27 result_fail_count=1
rvfc_summary: count=324 span_ms=15746.048 avg_gap_ms=48.749 max_gap_ms=911.800
metrics: presented=324 presentedFPS=21.58 decoded=873 decodedFPS=58.16
metrics: dropped=588 dropPct=67.35 advanced=13.45 speed=0.896 wall=15.01
capture_frame_hash_summary: present_count=12 hash_count=12 unique_hash_count=2
capture frames: first 3 black, frames 3-11 one unchanged nonblack hash
scanout_read_summary: kms_count=12 distinct_fb_count=4 distinct_bo_count=3
distinct_resource_count=3 fbs=105,111,114,116 bos=1,2,11 resources=4,5,14
capture timing from existing /proc/uptime lines:
  paired_count=12 total_ms=18440.000 avg_ms=1536.667
  min_ms=1030.000 max_ms=2660.000 span_ms=22090.000
QEMU trace: ctx_submit=420 set_scanout=352 res_flush=352
QEMU trace: fence_ctrl/fence_resp=420/420 res_create_3d=266 res_xfer_toh_3d=2
```

Interpretation: `virtio_gpu_scanout_read_diag=1` answered the capture lineage
question but is too invasive for FPS comparison. The guest capture follows the
current KMS framebuffer and sees changing FB/BO/resource identities, so the
old stale-scanout readback bug is not the explanation for the static capture.
However, each 1280x800 `fbstat ppm-current` sample cost about 1.5 seconds on
average in this run, depressing Chromium from the prior 34.69 FPS to 21.58
FPS. Treat the FPS number as perturbed. The useful signal is that page-side
RVFC still reports hundreds of presented frames while KMS readback only moves
from black to one static nonblack image, and Chromium still records transient
`gl=none,angle=none` GPU init exits with no sampled renderer role. The next
discriminator should trace Chromium EGL/GPU role selection or Chrome DRM
ioctl shape with the lightest possible window; do not make a kernel DRM
behavior patch from the scanout diagnostic alone.

Low-readback Chrome DRM discriminator:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T072706Z-chromium-video-depth2-gbm-chromedrm-capture-static-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
run shape: chromium-video, multiprocess=1, EGL_PLATFORM=gbm,
KDE_SMOKE_CHROMIUM_EGL_TRACE=0,
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_PREPROBE=1,
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_ATTEMPTS=0,
KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=4,
virtio_gpu_async_submit_depth=2, virtio_gpu_submit_hot_shape_stats=1,
chrome_drm_ioctl_trace=1, chrome_drm_thread_dump=1,
chrome_drm_fence_trace=0, no virtio_gpu_scanout_read_diag
launcher_policy_summary: EGL_PLATFORM=gbm, use_gl=egl-angle,
use_angle=opengles, ld_preload_trace=0
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke phase=result status=PASS
host_egl_gbm_status: status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
egl_chromium_offscreen_ready=0 reason=no_es3_rgb_pbuffer_config
sampler_result: kde_app_launch_probe chromium_sample_only=1 status=PASS
gpu_exit_error_count=2, gl_request_error_count=2, gl_request_none_count=2
process_summary: browser_seen=1 gpu_seen=1 renderer_seen=0
process_summary: drm_fd_seen=1 render_fd_seen=1 browser_render_fd_seen=1
process_policy_summary: browser/gpu EGL_PLATFORM=gbm and
--use-gl=egl-angle --use-angle=opengles reached sampled roles
perf_media_summary: playing_count=5 tick_count=16 result_count=0
rvfc_summary: count=281 span_ms=9434.816 avg_gap_ms=33.696 max_gap_ms=576.700
last presented/decoded/dropped: 281/560/354
capture_timing_summary: paired_count=4 total_ms=6580.000 avg_ms=1645.000
min_ms=1160.000 max_ms=2620.000 span_ms=8180.000
capture_frame_hash_summary: present_count=4 hash_count=4 unique_hash_count=2
capture frames: frame 0 black; frames 1-3 one unchanged nonblack hash
scanout_read_summary: kms_count=0 distinct_fb_count=0 distinct_resource_count=0
QEMU trace: ctx_submit=387 set_scanout=320 res_flush=320
QEMU trace: fence_ctrl/fence_resp=387/387 res_create_3d=277 res_xfer_toh_3d=2
Chrome DRM detail: chrome tgid=550 created virgl ctx=8, resource=263,
and submitted first execbuffer ret=0 fence=82 signaled=81
```

Interpretation: this lower-overhead discriminator confirms that Chromium is
not merely failing before DRM submit. A Chrome role opens the render node,
creates a virgl context/resource, and successfully submits at least one
`DRM_IOCTL_VIRTGPU_EXECBUFFER`. The remaining reproduced gap is further down
the presentation chain: page-side RVFC advances hundreds of frames while
KMS-captured frames move from black to one unchanged nonblack image. The GPU
process now appears in the sampler, but no renderer role is sampled and the
same transient `gl=none,angle=none` GPU init exits remain. The next reducer
should distinguish Chromium GPU/renderer lifecycle from KWin/Wayland
presentation delivery, preferably by capturing role-derived Wayland surface or
commit/present evidence without adding per-frame scanout-read diagnostics.

Harness metric follow-up: Chromium-video post-evidence now records
`capture_timing_summary` from the existing `/proc/uptime` lines around each
`fbstat ppm-current`, so future scanout/readback probes quantify capture
pressure directly in the durable post-evidence log.

Follow-up retry after the QtQml status/global and `KCVBEGIN` async-marker
harness fixes:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T050905Z-chromium-video-depth2-qtqml-command-interleaved-noimage/
build-x86_64/kde-plasma-desktop-smoke-history/20260628T051224Z-chromium-video-depth2-qtqml-command-interleaved-prompt-drain-warning-noimage/
build-x86_64/kde-plasma-desktop-smoke-history/20260628T051404Z-chromium-video-depth2-prompt-sync-drain-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-qtqml-ifunc-startup-timeout
KWin loader IFUNC: PASS.
kde-qtqml-ifunc-startup.status: missing guest_path=/kqi.status
kde-qtqml-ifunc-startup.log: placeholder
run.log around KSP0002 showed `sh /kqis.sh` interleaved before the final
`KSP0002` echoes and `root:/#` prompt. The second artifact recorded
`prompt-sync-drain status=NO_PROMPT marker=KSP0002`, then reproduced the same
misclassified QtQml timeout after continuing. The final artifact verifies the
strict fix: it fails before launching `/kqis.sh` with
`KDE-PLASMA-DESKTOP-SMOKE-FAIL prompt-sync-drain-timeout` and records
`prompt-sync-drain status=NO_PROMPT marker=KSP0002 seconds=20`.
```

Interpretation: this artifact is a harness prompt-synchronization mismatch, not
a renewed QtQml/libc IFUNC failure. `sync_prompt` previously returned after the
first echoed marker and only drained briefly; under Plasma log load the next
probe command could be sent before the prompt following the marker command had
actually arrived. The harness now waits for the post-marker `root:/#` prompt
with the original sync timeout budget before returning. If the prompt still
does not arrive, it fails immediately as `prompt-sync-drain-timeout` instead of
sending the next probe command from an unproven shell state. The next low-noise
Chromium-video retry should first prove `/kqis.sh` begins by checking for
`__KDE_QTQML_IFUNC_BEGIN__`, `/kqi.status`, and the QtQml PASS rows before
interpreting any Chromium result.

2026-06-28 preflight runner shell/source mismatch closure:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T052746Z-chromium-video-depth2-preflight-runner-redirection-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-preflight-kwin-timeout
root cause: generated runner used `exec >/kspr.log 2>&1`, but xv6 sh rejected
that redirection shape before writing durable status.

build-x86_64/kde-plasma-desktop-smoke-history/20260628T053440Z-chromium-video-depth2-preflight-runner-for-argv-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-preflight-kwin-timeout
root cause: generated shell runner used a long generated `for`/control-flow
shape; xv6 sh hit its small-argv/control-flow limits and left
`/kspr.status` at `status=waiting`.
```

Fix: `kde-plasma-desktop-smoke.expect` now generates a small host-built C
runner, compiles it with `cc -x c`, stages it as `/kspr`, and launches it
directly from `/etc/startup`. The runner waits for KDE/Xwayland readiness with
`/bin/kde-process-probe --require-xwayland`, executes the KWin loader and
QtQml IFUNC scripts, and writes explicit durable PASS/FAIL status lines to
`/kspr.status`. It no longer relies on xv6 sh `if`, `for`, global redirection,
or `$?`.

First proof after the C runner fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T054403Z-chromium-video-depth2-preflight-c-runner-pass-prompt-drain-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL prompt-sync-drain-timeout
kde-preflight-runner.status: status=PASS source=kde-smoke-preflight-runner
kde-preflight-runner-ready.log: kde_process_probe ... kwin=1 plasmashell=1
  kactivitymanagerd=1 kded5=1 xwayland=1 ... status=PASS
kde-kwin-loader-ifunc.log: KDE_DLOPEN_PROBE_KWIN_HELP_RESULT status=PASS
kde-qtqml-ifunc-startup.status:
  status=DONE source=kde-qtqml-ifunc-startup-probe
kde-qtqml-ifunc-startup.log: map/result/memmove PASS and
  __KDE_QTQML_IFUNC_PASS__
host-gui-host-egl-gbm-gl-smoke.log:
  host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host-gui-host-egl-gbm-gl-smoke.status:
  status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
kde-prompt-sync.log:
  KSP0001..KSP0004 prompt-sync PASS, then
  prompt-sync-drain status=NO_PROMPT marker=KSP0004 seconds=20
chromium_video_post_evidence status=FAIL reason=not-launched
```

Interpretation: the guest shell/source mismatch is closed. KDE readiness,
KWin loader IFUNC, QtQml IFUNC, and GBM/surfaceless fallback proof all pass
with durable explicit logs. Chromium did not launch in this artifact because
the harness failed at the next serial prompt-drain boundary immediately after
the GBM preprobe marker `KCVEGLDONE`. Continue from Chromium-video command
sequencing/prompt resilience before changing kernel DRM behavior or upstream
KDE/Qt/KWin/Xwayland/Mesa/Chromium sources.

2026-06-28 Chromium-video GBM transition hardening and blocked retry:

Harness-only follow-up: after `KCVEGLDONE`, the Chromium-video GBM preprobe
path now verifies durable guest artifacts instead of immediately requiring a
fresh interactive prompt. The helper waits for
`/host-gui-host-egl-gbm-gl-smoke.status` to contain `status=DONE` and for
`/host-gui-host-egl-gbm-gl-smoke.log` to contain
`host-egl-gbm-gl-smoke: phase=result status=PASS`. Independent audit initially
returned NO-GO because the pre-marker crash regex was narrower than the
artifact waiter; that was fixed by matching `segmentation fault`,
`coredump: generating`, `kde-exception-fatal-summary`, and kernel user
exception lines in the pre-marker expect too. Static Tcl completeness and
`diff --check` passed.

The first VM retry after this transition fix did not exercise the GBM/Chromium
path:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T055433Z-chromium-video-depth2-kwin-qt5core-gp-before-gbm-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-timeout
preflight runner: status=waiting; last ready probe had kwin=0,
  plasmashell=0, xwayland=0, status=WAIT
KWin loader IFUNC: not reached (placeholder)
QtQml IFUNC: not reached (placeholder / missing /kqi.status)
GBM preprobe: not reached
Chromium launch/sampler/capture: not reached
QEMU trace: ctx_submit=1, set_scanout=3, res_flush=3,
  fence_ctrl/fence_resp=1/1, res_create_3d=1
KWin crash line:
  pid 51 kwin_wayland: exception 13 (#GP General Protection)
  rip=0x7ffffd78701b
exception-ip:
  /usr/lib/x86_64-linux-gnu/libQt5Core.so.5 file_off=0x30301b
```

Interpretation: this is KWin startup evidence, not Chromium evidence and not a
verdict on the GBM prompt-transition fix. The harness now also classifies
kernel-reported user exceptions (`pid ...: exception N`) as crashes in the KDE
session/preflight/Chromium waiters, so this failure class should stop
degrading into `kde-session-ready-timeout`. The next useful proof is either a
rerun that gets past KDE startup to exercise the GBM artifact transition, or a
focused reducer for the recurring KWin `#GP` in `libQt5Core.so.5` before
continuing Chromium-video measurements.

2026-06-28 capture-log durability and append semantics follow-up:

```text
build-x86_64/appendredir-proof/20260628T062440Z/
APPENDREDIR-PASS
test appendredir: OK
ALL TESTS PASSED
APPENDREDIR_RC=0
```

The guest mismatch behind the empty Chromium capture log was twofold:
`/kcvcap.sh` used unsupported xv6 shell `exec >file` redirection, and xv6
shell/kernel append semantics did not match Linux well enough to switch that
script to many `>>file` writes. The xv6-owned fix changes `sh >>` to open with
`O_APPEND`, updates VFS regular-file writes/writev to choose EOF for append
descriptors, and adds the `appendredir` usertest for direct `O_APPEND` plus
shell `>>`. The proof above booted a throwaway refreshed rootfs image and ran
that usertest inside the guest. This is sufficient for the single-writer
harness log path; full POSIX multi-writer atomic append can be hardened later
by moving EOF selection under the filesystem inode write lock.

The focused Chromium-video GBM short rerun then proved that the capture log is
durable:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T062659Z-chromium-video-short-capture-log-proof-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-start-missing
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=33 duration_ms=3000 interval_ms=500 burst_samples=26 burst_ms=2500 burst_interval_ms=100 status=PASS
capture_log_summary=available=1 begin_count=2 end_count=2 uptime_before_count=2 uptime_after_count=2 fb_ppm_count=2 wc_count=2 wc_min_bytes=3072016 wc_max_bytes=3072016 done_count=1
capture_status=status=DONE samples=2
capture_present_count=2 capture_expected_count=2
capture_nonblack_count=0 capture_changed_count=0
chromium_int3_summary=available=1 trap_count=0
```

Interpretation: GBM preprobe and sampler status are now completion-only and
backed by explicit PASS result lines, and the capture script records full-size
frame lines instead of an empty log. This short run intentionally used
`KDE_SMOKE_CHROMIUM_VIDEO_RESULT_WAIT_SECONDS=0`; the resulting
`perf-start-missing` and black two-frame capture are not a playback verdict.
They do prove that the previous evidence-path mismatch is closed. Continue
Chromium playback work from page/perf timing, renderer/GPU-role absence, and
KWin/scanout capture behavior, not from fake `$?` status or missing capture
logs.

2026-06-28 Chromium Wayland presentation-delivery metric:

The xv6-owned Chromium launcher now has an opt-in
`WAYLAND_CHROMIUM_WAYLAND_DEBUG=1` path, driven by
`KDE_SMOKE_CHROMIUM_WAYLAND_DEBUG=1`, that sets `WAYLAND_DEBUG=client` and
logs both the requested and effective values. The Chromium-video post-evidence
parser records Wayland client protocol counts for `wl_surface` attach, damage,
frame, commit, and frame-callback completion without changing the pass/fail
verdict. A subagent audit passed after the parser was tightened to correlate
`wl_surface.frame(new id wl_callback...)` with matching `wl_callback.done`.

Build proof before the VM lane:

```text
cmake --build build-x86_64 --target host-gui-runtime -j2
cmake --build build-x86_64 --target rootfs-refresh -j2
debugfs dump /bin/wayland-chromium: contains
  WAYLAND_CHROMIUM_WAYLAND_DEBUG
  WAYLAND_DEBUG
```

The first VM attempt after the refresh did not exercise Chromium:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T075110Z-chromium-wayland-debug-kwin-startup-retry-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry
kde-session: /dev/shm/xdg-runtime-root/wayland-0 not visible after compositor grace
preflight status=status=waiting source=kde-smoke-preflight-runner
last ready probe: kwin=0 plasmashell=0 xwayland=0 status=WAIT
qemu_trace: ctx_submit=1 set_scanout=3 res_flush=3 fence_ctrl/fence_resp=1/1
```

The clean retry reached Chromium and reproduced the static-capture failure:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T075357Z-chromium-wayland-debug-capture-static-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
launcher_policy_summary: use_gl="egl-angle" use_angle="opengles"
  egl_platform="gbm" wayland_debug_requested="1" wayland_debug="client"
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke: phase=result status=PASS
  api=gles3 exit_status=0
sampler_result: kde_app_launch_probe chromium_sample_only=1 samples=63
  duration_ms=18000 interval_ms=500 burst_samples=26 status=PASS
perf_media_summary: rvfc_count=154 rvfc_span_ms=7388.144
  rvfc_avg_gap_ms=48.289 rvfc_max_gap_ms=633.900 presented=154
  decoded=436 dropped=283
capture_timing_summary: paired_count=2 total_ms=4270.000 avg_ms=2135.000
  min_ms=1400.000 max_ms=2870.000 span_ms=4580.000
capture_frame_hash_summary: present_count=2 unique_hash_count=1
  first_hash=5789d2e8aa54833682b240ea485a2db7bb4ad79287fa07e2ddf99662cee54cdc
  last_hash=5789d2e8aa54833682b240ea485a2db7bb4ad79287fa07e2ddf99662cee54cdc
process_summary: browser_seen=1 gpu_seen=1 renderer_seen=0 render_fd_seen=1
  browser_render_fd_seen=1
role_lifecycle_summary: browser_samples=61 gpu_samples=2 gpu_first=61
  gpu_last=62 renderer_samples=0
qemu_trace: ctx_submit=314 set_scanout=245 res_flush=245
  fence_ctrl/fence_resp=314/314 res_create_3d=273 res_xfer_toh_3d=2
```

The first saved post-evidence line under-counted Wayland surface traffic
because libwayland prints object ids as `wl_surface#39` and `wl_callback#34`
rather than only `wl_surface@39`. The parser was corrected to accept both
formats. Replaying the corrected parser against the archived Chromium log gives:

```text
wayland_debug_summary=available=1 requested="1" wayland_debug="client"
  enabled=1 debug_line_count=1605 surface_line_count=761 surface_count=2
  attach_count=190 damage_count=190 damage_buffer_count=0 frame_count=188
  commit_count=193 frame_callback_count=1 frame_callback_done_count=187
  frame_callback_done_unique_count=1 callback_done_count=193 callback_count=6
```

Interpretation: Chromium is no longer only "not launching" or "not committing".
The browser created and committed its Wayland surface repeatedly, and KWin sent
frame callback completions, while the page's RVFC advanced and the KMS capture
remained a single nonblack hash. The next reducer should move downstream from
Chromium launch/Wayland commit into KWin composite, damage propagation,
scanout update, and the fbstat capture/readback path. The late GPU-process
lifecycle is still suspicious (`gpu_first=61`, `renderer_samples=0`, two early
`gl=none,angle=none` GPU exits), but the static KMS capture is no longer
explained by missing Chromium Wayland commits.

2026-06-28 Chromium scanout sample-current metric:

The Chromium-video capture harness now logs `fbstat sample-current` before and
after each `fbstat ppm-current` capture and fails post-evidence if those
explicit `fb_sample_current` lines are missing. The Wayland-debug summary now
also classifies `wl_pointer.set_cursor`, `xdg_wm_base.get_xdg_surface`, and
`xdg_surface.get_toplevel` so the reported primary surface prefers an
xdg-toplevel content surface over cursor-only surfaces. Independent audit
passed after these two guards were added.

Static and replay checks:

```text
expect info complete: complete
git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect docs/linux-drm-abi-compat-plan.md
git -C kernel diff --check
git -C user diff --check
git -C ports diff --check
archived Wayland replay:
  primary_surface=39 primary_surface_role=xdg_toplevel primary_is_cursor=0
  primary_is_xdg_toplevel=1 primary_commit_count=191 primary_damage_count=188
  primary_buffer_count=2
sample-current replay:
  status=PASS for complete before/after samples
  status=FAIL when the after-sample output is missing
```

The first VM lane did not reach Chromium and was archived separately:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T081606Z-chromium-scanout-sample-metric-kde-startup-crash-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
run.log: pid 76 wireplumber: exception 13
KWin had already reached virgl: renderer=virgl (D3D12 (Intel(R) UHD Graphics))
Xwayland.real had begun GLAMOR/virgl DRM ioctls before the service crash.
```

The clean retry reached Chromium and reproduced the expected static-capture
failure with the new scanout/readback metric:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T081821Z-chromium-scanout-sample-metric-capture-static-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-static
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke: phase=result status=PASS
  api=gles3 exit_status=0
sampler_result: kde_app_launch_probe chromium_sample_only=1 samples=63
  duration_ms=18000 interval_ms=500 burst_samples=26 status=PASS
wayland_debug_summary: primary_surface=28 primary_surface_role=xdg_toplevel
  primary_is_cursor=0 primary_is_xdg_toplevel=1 primary_attach_count=268
  primary_damage_count=268 primary_commit_count=271 primary_frame_done_count=267
  primary_damage_area_total=148777050 primary_buffer_count=2
perf_media_summary: last_rvfc_count=338 last_rvfc_span_ms=10968.552
  last_rvfc_avg_gap_ms=32.548 last_rvfc_max_gap_ms=591.300
  last_presented=338 last_decoded=668 last_dropped=431
capture_timing_summary: paired_count=4 total_ms=7070.000 avg_ms=1767.500
  min_ms=1460.000 max_ms=2250.000 span_ms=7990.000
capture_frame_hash_summary: present_count=4 unique_hash_count=1
  first_hash=5789d2e8aa54833682b240ea485a2db7bb4ad79287fa07e2ddf99662cee54cdc
scanout_sample_current_summary: status=PASS expected_count=8 sample_count=8
  before_count=4 after_count=4 unique_hash_count=1 first_hash=0x946b471d7e1cb4f5
  last_hash=0x946b471d7e1cb4f5 nonblack_count=8
  avg_rgb=163,162,157 center=0xfff1f8f9
scanout_read_summary: kms_count=12 distinct_fb_count=4 distinct_bo_count=3
  distinct_resource_count=3 fbs=111,114,115,116 bos=1,2,11 resources=4,5,14
qemu_trace: ctx_submit=396 set_scanout=327 res_flush=327
  fence_ctrl/fence_resp=396/396 res_create_3d=271 res_xfer_toh_3d=2
process_summary: browser_seen=1 gpu_seen=1 renderer_seen=0 render_fd_seen=1
  browser_render_fd_seen=1 gpu_first_sample=59 gpu_last_sample=62
```

Interpretation: Chromium is making real xdg-toplevel Wayland content commits
and damage, KWin is returning frame callbacks, and the page's RVFC/media
counters advance, but the scanout readback remains one nonblack hash even as
the KMS scanout-read path observes changing fb/bo/resource ids. The next
bottleneck is therefore downstream of Chromium Wayland delivery and upstream
of visible/readback content progress: KWin composite damage consumption,
Chromium buffer import/sample in KWin, or the virtio/KMS scanout-read source
for the currently displayed resource. Do not spend the next iteration on
Chromium launch, Wayland commit absence, fake `$?` status, or missing
`fbstat` capture plumbing unless new evidence contradicts this run.

2026-06-28 Chromium present/readback lineage diagnostic:

Patch:

- `fb_read_current_kms_framebuffer()` keeps `FB_GPU_SCANOUT_READ` behavior
  unchanged but enriches the existing `virtio_gpu_scanout_read_diag=1` line
  with KMS owner, transfer attempt/result, five-point sample hash, nonblack
  count, and center pixel.
- `fb_blit_from_bo_format()` can emit opt-in present-time five-point resource
  samples after successful virgl resource-scanout presents. This is behind the
  narrower `fb_present_sample_diag=1` / `virtio_gpu_present_sample_diag=1`
  flag, not generic `fb_present_perf`, because it performs tiny
  `TRANSFER_FROM_HOST_3D` readbacks for sampled points.
- Chromium-video post-evidence now summarizes both sides: `present_sample_*`
  from successful resource-scanout presents and `kms_sample_*` from
  `FB_GPU_SCANOUT_READ`.
- Diagnostic lanes can set `KDE_SMOKE_KDE_PREFLIGHT=0` to avoid the KWin/QtQml
  preflight startup injection and `KDE_SMOKE_CHROMIUM_VIDEO_POST_STRICT=0` to
  keep Chromium post-evidence artifact-only. Defaults preserve the current
  reducer gates.

Static/build/audit checks:

```text
expect info complete: complete
git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect docs/linux-drm-abi-compat-plan.md docs/linux-userland-upstream-depatch-plan.md
git -C kernel diff --check
git -C user diff --check
git -C ports diff --check
cmake --build build-x86_64 --target kernel -j2
Subagent audit initially NO-GO for broad diagnostic semantics; fixes added:
  present sampler uses explicit opt-in, preflight can be disabled, and
  Chromium post-evidence can be artifact-only for diagnostic runs.
```

Focused diagnostic proof:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T083936Z-chromium-present-readback-lineage-diagnostic/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video
post_strict=0 kde_preflight_enabled=0
cmdline included virtio_gpu_scanout_read_diag=1, fb_present_sample_diag=1
host_egl_gbm_smoke_result: host-egl-gbm-gl-smoke phase=result status=PASS
sampler_result: kde_app_launch_probe chromium_sample_only=1 status=PASS
wayland_debug_summary: primary_surface_role=xdg_toplevel primary_is_cursor=0
  primary_attach_count=227 primary_damage_count=227 primary_commit_count=230
perf_media_summary: last_rvfc_count=253 last_rvfc_span_ms=9126.200
  last_rvfc_avg_gap_ms=36.215 last_rvfc_max_gap_ms=267.336
  last_presented=253 last_decoded=549 last_dropped=342
process_summary: browser_seen=1 gpu_seen=1 renderer_seen=0 render_fd_seen=1
process_policy_summary: browser/gpu carried EGL_PLATFORM=gbm,
  --use-gl=egl-angle, --use-angle=opengles
gpu_exit_error_count=2 gl_request_none_count=2
capture_timing_summary: paired_count=4 total_ms=6930.000 avg_ms=1732.500
  min_ms=1420.000 max_ms=2370.000 span_ms=7780.000
capture_frame_hash_summary: present_count=4 unique_hash_count=1
  all four PPMs black (sha256=d4e96a65...)
scanout_sample_current_summary: sample_count=8 unique_hash_count=3
  changed_count=2 post_nonblack_changed_count=1
  first_hash=0x8d3b876f27f88383 last_hash=0x946b471d7e1cb4f5
scanout_read_summary: kms_count=11 distinct_fb_count=4
  distinct_bo_count=2 distinct_resource_count=2 transfer_count=11
  transfer_fail_count=0 kms_sample_unique_hash_count=3
  kms_sample_changed_count=2 present_sample_count=12
  present_sample_unique_hash_count=2 present_sample_changed_count=1
  present_sample_transfer_fail_count=0
  kms_sample_hashes=0x6aad230c149999a9,0x746535c4c59999a9,0xb945636f5d2a7707
  present_sample_hashes=0x6aad230c149999a9,0xb945636f5d2a7707
qemu_trace: ctx_submit=352 set_scanout=285 res_flush=285
  fence_ctrl/fence_resp=352/352 res_create_3d=265 res_xfer_toh_3d=2
```

Raw lineage interpretation: present-time resource samples and later
`FB_GPU_SCANOUT_READ` samples agree on the same content transitions, with zero
present-sample transfer failures and zero KMS readback transfer failures. The
resource/readback path is therefore not sampling a stale/wrong BO in this
diagnostic lane. The four PPM captures were black because the capture window
landed before the final nonblack transition: frame 03's `sample_current_before`
was alpha-black (`0x6c8dec05cdf88383`), `ppm-current` then wrote the black PPM,
and `sample_current_after` immediately observed the nonblack desktop-like hash
(`0x946b471d7e1cb4f5`). Treat the current bottleneck as Chromium/KWin
presentation content/lifecycle, not KMS scanout readback lineage. The next
reducer should focus on why no renderer role is sampled and why Chromium's
RVFC/presented counters advance while KWin receives/exports only static
desktop/black content.

2026-06-28 Chromium EGL/process-role diagnostic:

Artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T0847-chromium-egl-trace-startup-timeout/
```

Command shape: same Chromium-video GBM reducer as the present/readback lineage
lane, but with `KDE_SMOKE_CHROMIUM_EGL_TRACE=1` and artifact-only post
evidence.

Result:

```text
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-timeout
post_evidence status=FAIL reason=perf-result-startup-timeout
host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
chromium_egl_trace_summary: init=9 process=8 browser=1 renderer=0 gpu=4
  zygote=2 dlopen=9 egl entrypoint calls=0
process_summary: browser_seen=1 renderer_seen=0 gpu_seen=0 zygote_seen=1
role_lifecycle_summary: renderer_samples=0 gpu_samples=0 browser_samples=61
perf_media_summary: result_fail_count=1 last_presented=498 last_decoded=960
  last_dropped=631 last_rvfc_count=276 last_rvfc_avg_gap_ms=38.665
capture_timing_summary: paired_count=4 total_ms=7630.000 avg_ms=1907.500
capture_frame_hash_summary: unique_hash_count=2
capture_nonblack_count=2 capture_changed_count=1
scanout_read_summary: transfer_count=12 transfer_fail_count=0
  kms_sample_unique_hash_count=3 present_sample_unique_hash_count=3
qemu_trace: ctx_submit=474 set_scanout=401 res_flush=401
  fence_ctrl/fence_resp=474/474 res_create_3d=279 res_xfer_toh_3d=2
```

EGL trace interpretation:

- The Chromium launcher and browser process carried `--use-gl=egl-angle`,
  `--use-angle=opengles`, `EGL_PLATFORM=gbm`, and the EGL trace preload.
- The trace observed four GPU-process constructors. The early GPU children
  carried `--use-gl=egl-angle`, but Chrome still logged
  `Requested GL implementation (gl=none,angle=none)` and exited the GPU
  process during initialization. A later GPU child carried `--use-gl=disabled`,
  loaded VA/DRI video libraries, and the kernel observed render-node DRM
  ioctls plus a virgl context submit from that child.
- No traced process called `eglChooseConfig`, `eglCreateContext`,
  `eglCreatePbufferSurface`, `eglCreateWindowSurface`, `eglGetProcAddress`, or
  `dlsym` for those entry points. The current gap is therefore earlier than
  Mesa EGL entrypoint execution in Chromium's failing GPU-init path, even
  though independent GBM/EGL on the same guest image works.
- The run no longer produced an all-black capture: frames 2-3 were nonblack
  and stable. The media page still reported a startup-timeout result before
  later playing to completion, so the next performance bottleneck is Chromium
  startup/GPU-process lifecycle jitter, not scanout readback correctness.

Follow-up metric:

- Added xv6-owned role-classifier audit fields to
  `kde-app-launch-probe`: `role_source`, `type_arg`, `argv0`,
  `cmdline_bytes`, and `cmdline_truncated` on each Chromium process evidence
  line.
- `chromium_process_evidence_summary` now reports
  `renderer_candidate_seen`, `unknown_chrome_seen`,
  `cmdline_truncated_seen`, `role_source_counts`, and first suspicious
  examples. This distinguishes "no sampled renderer-shaped process" from
  "strict `--type=renderer` classification missed a Chromium child" in the
  next reducer lane.

2026-06-28 Chromium role-classifier proof:

Artifact:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T0903-chromium-role-metric-launch-only/
```

Command shape:

```sh
KDE_SMOKE_REDUCER=chromium-video
KDE_SMOKE_CHROMIUM_LAUNCH_ONLY=1
KDE_SMOKE_CHROMIUM_MULTIPROCESS=1
KDE_SMOKE_CHROMIUM_EGL_TRACE=1
KDE_SMOKE_CHROMIUM_EGL_PLATFORM=gbm
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_PREPROBE=0
timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Result:

```text
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1
chromium_video_post_evidence status=PASS reason=launch-only
sampler_result=... samples=63 duration_ms=18000 interval_ms=500
  burst_samples=26 burst_ms=2500 burst_interval_ms=100 status=PASS
launcher_policy_summary: use_gl="egl-angle" use_angle="opengles"
  egl_platform="gbm" egl_trace="1" ld_preload_trace=1
chromium_egl_trace_summary: init=6 process=5 browser=1 renderer=0
  gpu=1 zygote=2 dlopen=6 egl entrypoint calls=0
gpu_init_error_count=0 gpu_config_error_count=0 gpu_exit_error_count=0
process_summary: browser_seen=1 renderer_seen=0 renderer_candidate_seen=0
  gpu_seen=1 gpu_first_sample=59 gpu_last_sample=62
  zygote_seen=1 unknown_chrome_seen=0 cmdline_truncated_seen=0
  render_fd_seen=1 browser_render_fd_seen=1
  role_counts=browser:2,gpu-process:1,unknown:1,utility:1,zygote:3
  role_source_counts=root_chrome:63,type_arg:67,unknown:1
role_lifecycle_summary: renderer_samples=0 renderer_pids=0
  gpu_samples=4 gpu_pids=1 browser_samples=62
process_policy_summary: gpu_use_gl_count=4 gpu_use_angle_count=4
qemu_trace: ctx_submit=60 set_scanout=19 res_flush=19
  fence_ctrl/fence_resp=60/60 res_create_3d=213 res_xfer_toh_3d=2
```

Raw role-field proof from `kde-chromium-process-evidence.log`:

```text
64 role_source="root_chrome" type_arg="missing" argv0="chrome" cmdline_truncated=0
4  role_source="type_arg" type_arg="gpu-process" argv0="exe" cmdline_truncated=0
4  role_source="type_arg" type_arg="utility" argv0="exe" cmdline_truncated=0
59 role_source="type_arg" type_arg="zygote" argv0="chrome" cmdline_truncated=0
1  role_source="unknown" type_arg="missing" argv0="chrome_crashpad_handler" cmdline_truncated=0
```

Interpretation: the sampler metric is now VM-proven. In this launch-only lane,
the absence of renderer evidence is not a parser/classifier miss: no sampled
process had `type_arg="renderer"`, no Chromium-shaped unknown was present, and
no sampled command line was truncated. The GPU process still appears late
(samples 59-62), while the browser owns a render fd from sample 42. This keeps
the bottleneck on Chromium process/GPU startup and frame delivery, not on
unknown role parsing. Because this lane was intentionally launch-only, it does
not replace the video playback/jitter metrics from the full video lanes; it
only proves the role evidence layer.

Next metric target implementation: `kde-plasma-desktop-smoke.expect` now emits
`wayland_surface_delivery_matrix`, a derived xv6-owned row based on the existing
`KDE_SMOKE_CHROMIUM_WAYLAND_DEBUG=1` parser rather than a new Wayland
`LD_PRELOAD` interposer. It classifies whether RVFC/video counters advance
while Chromium emits `wl_surface.attach`/`damage`/`commit` and receives frame
callbacks. This separates Chromium-not-submitting from KWin-not-driving from
KMS/capture-static without patching Chromium, KWin, Xwayland, Mesa, or Qt, and
without forwarding Wayland's variadic client ABI.

Host-only extraction test against
`20260628T0847-chromium-egl-trace-startup-timeout` classified the existing full
debug artifact as:

```text
wayland_surface_delivery_matrix=available=1 enabled=1 status=PASS
  reason=surface-delivery-active primary_surface=28
  primary_surface_role=xdg_toplevel primary_attach_count=373
  primary_damage_count=373 primary_frame_count=373
  primary_frame_done_count=373 primary_commit_count=377
  primary_buffer_count=2 rvfc_count=276 presented=498 decoded=960 dropped=631
  capture_unique_hash_count=2 scanout_unique_hash_count=3
```

Interpretation: in that full debug lane, Chromium did submit and KWin did
acknowledge the primary xdg-toplevel surface while RVFC advanced. The next VM
lane should prove the new matrix row in fresh post-evidence, then continue
downstream into KWin composite/present timing and capture/static-window
correlation before any kernel behavior change.

2026-06-28 procfs VM lifetime mismatch fix:

The focused Chromium-video matrix retry first exposed a kernel panic before
Chromium launched:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T0913-kde-startup-procfs-rwsem-panic/
```

The backtrace reached `procfs_gen_pid_stat()` ->
`procfs_collect_vm_accounting()` -> `vm_rlock()` and crashed in the rwsem wait
queue. The mismatch was that procfs status/stat/statm readers copied `p->vm`
under RCU, dropped RCU, and then walked the VM without a lifetime reference.
The same publication hazard existed when exit/exec destroyed or replaced
`p->vm` before making the pointer safe for concurrent procfs readers.

Current kernel fix:

- `procfs_gen_status`, `procfs_gen_statm`, `procfs_gen_pid_stat`,
  `procfs_gen_maps`, `procfs_gen_smaps`, and `/proc/<pid>/mem` pin the target
  VM under the target thread lock before using it.
- `exit_release_vm`, `thread_destroy`, and `exec` stop publishing or replace
  `p->vm` under the thread lock, then drop the old VM after releasing the
  spinlock.
- `maps`/`smaps` now also drop the temporary VM ref on allocation failure.

Verification:

```text
git -C kernel diff --check -- kernel/vfs/procfs/inode.c kernel/proc/exit.c kernel/proc/thread.c kernel/exec.c
cmake --build build-x86_64 --target kernel -j2
```

Both passed. `kernel-sparse` was attempted and failed only on existing sparse
debt in Hyper-V/D3DKMT sources, not in the touched procfs/exec/exit/thread
files.

Post-fix VM proof:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T092415Z-procfs-vm-pin-kwin-wayland-timeout/
```

The previous procfs rwsem/list panic did not recur. The run reached a live
`kwin_wayland` process and successfully dumped `/proc/<pid>/status`,
`/proc/<pid>/statm`, and `/proc/<pid>/maps`. It then failed with
`KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-startup-retry` because
`/dev/shm/xdg-runtime-root/wayland-0` never appeared; Chromium was not launched
and `chromium_video_post_evidence` reports `status=FAIL reason=not-launched`.
Treat the next failure as a KWin Wayland-socket startup timeout, not the fixed
procfs VM lifetime panic and not Chromium playback evidence.

2026-06-28 Chromium Wayland-debug evidence mismatch fix:

The `20260628T093453Z-kwin-wait-metric-chromium-capture-timeout` artifact
exposed a harness classification mismatch: `host-gui-wayland-chromium.log`
recorded `WAYLAND_CHROMIUM_WAYLAND_DEBUG="1"` and `WAYLAND_DEBUG="client"`,
but `chromium_video_post_evidence` reported `wayland_debug_summary enabled=0`
and skipped the delivery matrix as `wayland-debug-disabled`. The parser was
using "saw at least one Wayland protocol line" as the definition of debug
being enabled.

Current harness fix:

- `chromium_wayland_debug_summary` now reports `requested_enabled` and
  `protocol_seen` separately.
- `enabled` is true when the launcher exported Wayland debug or protocol lines
  were seen.
- `wayland_surface_delivery_matrix` now reports
  `reason=wayland-debug-no-protocol-lines` when debug was requested/exported
  but no protocol stream appears, rather than skipping as disabled.

Host-only parser proof against the old artifact:

```text
SUMMARY available=1 requested="1" wayland_debug="client" enabled=1 requested_enabled=1 protocol_seen=0 debug_line_count=0
MATRIX available=1 enabled=1 requested_enabled=1 protocol_seen=0 status=FAIL reason=wayland-debug-no-protocol-lines primary_surface=missing
```

Interpretation: the old mismatch is fixed in the xv6-owned harness. The next
Chromium-video lane should treat absence of Wayland protocol lines as a real
capture/stream symptom, not as a disabled debug request.

2026-06-28 Chromium capture/sampler progress metric:

The same `20260628T093453Z-kwin-wait-metric-chromium-capture-timeout`
artifact then exposed an under-instrumented reducer timeout. The capture
script captured exactly one black frame and stopped after logging
`kde_chromium_video_capture frame=00 uptime_after`; it did not log the uptime
value or `frame=00 end`, and `/kde-chromium-video-capture.status` remained
`status=running samples=4`. The sampler status similarly stayed
`status=running` while the sampler log was empty.

Current harness metric:

- `kde-chromium-video-capture.status` is updated before each potentially
  blocking operation with `phase`, `frame`, `op`, `path`, and sample count.
  The covered operations include `/proc/uptime`, `fbstat sample-current`,
  `fbstat ppm-current`, `wc`, `sync`, and inter-frame `sleep`.
- `kde-chromium-sampler.status` is updated before sync and before the
  `kde-app-launch-probe --chromium-sample-only` call with sampler timing
  parameters.
- Status completion still uses explicit `status=DONE` markers and does not
  depend on xv6 shell `$?`.

Static and host-parser checks:

```text
expect completeness: complete
diff --check: pass
old-summary compatibility: MATRIX ... status=FAIL reason=wayland-debug-no-protocol-lines
```

Interpretation: the next Chromium-video VM lane should identify whether the
capture hang is in `cat /proc/uptime`, `fbstat`, `wc`, `sync`, sleep, or the
sampler probe itself, instead of leaving only a generic `status=running`.

Focused Chromium-video GBM reducer proof:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T094914Z-chromium-video-fps27-capture-fbstat-frame06/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-result-fps-27.0
```

Command shape:

```sh
KDE_SMOKE_REDUCER=chromium-video \
KDE_SMOKE_CHROMIUM_MULTIPROCESS=1 \
KDE_SMOKE_CHROMIUM_EGL_PLATFORM=gbm \
KDE_SMOKE_CHROMIUM_VIDEO_EGL_GBM_PREPROBE=1 \
KDE_SMOKE_CHROMIUM_VIDEO_WARMUP_SECONDS=12 \
KDE_SMOKE_CHROMIUM_VIDEO_SAMPLES=12 \
KDE_SMOKE_CHROMIUM_VIDEO_SAMPLE_INTERVAL_MS=1000 \
KDE_SMOKE_CHROMIUM_VIDEO_RESULT_WAIT_SECONDS=40 \
QEMU_APPEND_EXTRA='kde_xwayland_glamor=auto chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1' \
timeout 900 scripts/gpu/kde-plasma-desktop-smoke.expect
```

Performance and compositor evidence:

- KWin renderer: `virgl (D3D12 (Intel(R) UHD Graphics))`, OpenGL ES 3.0 Mesa
  `26.2.0-devel`.
- Xwayland wrapper: `glamor=auto effective_glamor=es enable_glx=0`,
  final argv `glamor=es`, `+extension_GLX=no`, `-extension_GLX=no`.
- GBM preprobe: `host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3`.
- Chromium EGL config shape: pbuffer profiles still have count `0`;
  window-only and surfaceless ES2/ES3 profiles have count `50`.
- Chromium media result: `PERF-VIDEO RESULT fail fps=27.0 speed=0.986
  presentedFPS=27.0 decodedFPS=61.0 dropPct=65.50 advanced=15.18`.
- RVFC summary: `rvfc_count=415`, average gap `38.544 ms`, max gap
  `364.900 ms`, `presented=429`, `decoded=960`, `dropped=634`.
- QEMU virtgpu trace: `ctx_submit=436`, `res_flush=371`,
  `set_scanout=371`, `fence_ctrl=436`, `fence_resp=436`,
  `res_create_3d=267`.

Capture and process evidence:

- Sampler completed:
  `kde_app_launch_probe chromium_sample_only=1 samples=63 ... status=PASS`.
- Capture progressed past the previous black-frame/no-progress failure:
  frames `0..5` are present and nonblack, but all six have identical SHA256
  and AE delta `0`.
- Scanout samples are nonblack but static:
  `nonblack_count=13`, `unique_hash_count=1`, `changed_count=0`,
  first/last average RGB `163,162,157`.
- The new capture status marker localized the capture abort/early-exit point
  to `status=running phase=frame frame=06 op=fbstat-sample-current-before`.
  The frame 06 `fbstat sample-current` line was written, but the script did
  not reach the next `op=fbstat-ppm-current` marker; the run log also printed
  `exec ec failed` before `KCVCAPDONE`.
- Multiprocess Chromium evidence improved over the single-process lane:
  browser, zygote, utility, and GPU process roles were sampled, but no renderer
  role or renderer candidate was sampled. The GPU process only appeared in the
  final three process samples.

New post-evidence metric:

`kde-plasma-desktop-smoke.expect` now emits
`drm_execbuffer_time_summary` from `chrome-drm-detail: execbuffer-time` lines.
Host-only parser proof against this artifact produced:

```text
available=1 count=1 chrome_count=1 total_us_sum=177 total_us_avg=177.000
submit_us_sum=101 submit_us_avg=101.000 trace_log_us_sum=47222
trace_log_us_avg=47222.000
```

Interpretation: this lane no longer points at launch failure, GBM context
absence, black-only scanout, or sampler incompletion. The active bottlenecks
are measurable playback throughput (`27 FPS`, `65.50%` dropped), static scanout
capture despite RVFC/media progress, late/short-lived GPU-process observation
with no sampled renderer role, and capture-script early exit around
`fbstat sample-current` frame 06. The DRM trace logging overhead is much larger
than the measured submit path for the sampled execbuffer, so future FPS
comparisons should include a low-trace lane after the current details are
preserved.

Chromium launcher preload-scope fix proof:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T100724Z-chromium-launch-only-ldpreload-sanitized-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1
```

Change: `scripts/image/wayland-chromium-launcher.c` now clears inherited
`LD_PRELOAD` before executing the imported Chromium binary, while still adding
the Chromium EGL trace preload only when `WAYLAND_CHROMIUM_EGL_TRACE=1` is
requested. The active image was rebuilt with the full CMake rootfs overlay set:
`host-gui`, `webkit-media`, `kde-runtime`, `gameboy-roms`, and
`gpup-umd-overlay`.

Proof lines:

- Image contains `/usr/bin/plasmashell`, `/bin/wayland-chromium`, and
  `/opt/host-gui/wayland-chromium/chrome-linux64/chrome`; dumped launcher
  strings include `LD_PRELOAD` and the Chromium EGL trace preload path, but no
  KDE preload library names.
- Xwayland wrapper: `glamor=auto effective_glamor=es enable_glx=1`,
  final argv `+extension_GLX=yes`.
- Chromium launcher log: `env LD_PRELOAD="(unset)"` and no
  `elf_machine_rela_relative` assertion.
- Launch probe:
  `kde_app_launch_probe chromium_only=1 chromium=1 chromium_exited=0 ... status=PASS`.
- Post-evidence: `launcher_policy_summary ... ld_preload_trace=0
  ld_preload_ifunc=0`, `launch_evidence_status=PASS`,
  `sampler_result=... status=PASS`, `status=PASS reason=launch-only`.
- Process roles: browser, zygote, utility, and GPU-process roles were sampled;
  render fd appeared at sampler sample 42.
- GBM preprobe still passed:
  `host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3`.

Interpretation: the earlier GLX-on launch failure was an xv6-owned runtime
environment mismatch, not an upstream Chromium/KDE/Mesa source issue. Chromium
must not inherit the KDE/Qt IFUNC/preload workaround. The remaining Chromium
gap after this fix is later than the loader: the launch-only proof still logs a
GPU process initialization exit with `gl=none,angle=none`, so the next
Chromium-video reducer should keep the sanitized launcher and isolate Chromium's
GPU-process GL selection/child argv/environment before returning to full
capture/perf.

Chromium GPU-process GL-selection mismatch proof:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T101615Z-chromium-launch-only-gpu-pid-context-kde-session-ready-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
run.log: pid 75 wireplumber: exception 13

build-x86_64/kde-plasma-desktop-smoke-history/20260628T101808Z-chromium-launch-only-gpu-pid-context-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1
```

Change: the Chromium-video post-evidence harness now writes
`gpu_error_pid_context`, correlating Chromium GL-init error PIDs with the
sampled `kde-chromium-process-evidence.log` argv/env/fd/map evidence. This is
host-side Expect instrumentation only; no upstream KDE/Qt/KWin/Xwayland/Mesa or
Chromium source was patched.

Fresh proof lines:

- KWin/Xwayland: KWin reports `virgl (D3D12 (Intel(R) UHD Graphics))`; Xwayland
  wrapper final argv has `glamor=es`, `+extension_GLX=yes`,
  `-extension_GLX=no`.
- GBM preprobe: `host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3`
  and completion status is `status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted`.
- Chromium launcher: `env LD_PRELOAD="(unset)"`; launcher policy summary has
  `use_gl="egl-angle"`, `use_angle="opengles"`, `egl_platform="gbm"`,
  `ld_preload_trace=0`, and `ld_preload_ifunc=0`.
- Sampler: `kde_app_launch_probe chromium_sample_only=1 samples=63 ... status=PASS`.
- Chrome GL error: pid `406` logs
  `Requested GL implementation (gl=none,angle=none) not found...` and exits the
  GPU process.
- New PID context: `gpu_error_pid_context ... first_error_pid=406
  first_role="gpu-process" first_phase=sample-059 last_phase=sample-062
  use_gl="egl-angle" use_angle="opengles" render_node="/dev/dri/renderD128"
  gpu_preferences_b64_len=152 cmdline_truncated=0 env_seen=1
  egl_platform="gbm" gallium="virgl" mesa_driver="virtio_gpu"
  ld_preload="absent" render_fd_seen=0 maps_libgbm=1 maps_libdrm=1`.
- Process policy summary: browser and GPU process both carry
  `--use-gl=egl-angle` and `--use-angle=opengles`; the sampled GPU process is
  present for samples 59 through 62.

Interpretation: the remaining mismatch is not host GBM availability, KWin
virgl, Xwayland GLX exposure, inherited KDE preload state, missing child argv
policy, or truncated `/proc/<pid>/cmdline`. The same GPU-process PID carries
the requested EGL/ANGLE policy and Mesa GBM environment while Chromium's
internal GL request has already collapsed to `none/none`. The next behavior
change should therefore target the xv6-owned Chromium launch/policy boundary:
decode/compare the `--gpu-preferences` payload against a Linux KVM+virgl
control and test whether the collapse is encoded there before making any
kernel DRM/ioctl change. The first decoded reducer payload is 152 base64
characters, not 152 bytes; it decodes to 112 bytes with FNV-1a32
`0xe9b76745`, so future evidence must compare the decoded byte payload rather
than the argument text length.

2026-06-28 gpu-preferences decoder proof attempt:

```text
Static/audit:
expect Tcl completeness: complete
git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect docs/linux-drm-abi-compat-plan.md: PASS
git -C kernel diff --check: PASS
decoder replay: b64_len=152 decoded_len=112 decoded_fnv32=0xe9b76745
independent audit: GO

VM artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T103209Z-chromium-launch-only-gpu-preferences-decode-kde-session-ready-crash/
KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
status_file: status_code=3 label=KDE-PLASMA-DESKTOP-SMOKE-FAIL kde-session-ready-crash
Chromium: not launched; gpu_error_pid_context=... gl_error_pids=none
KWin: renderer reached virgl (D3D12 (Intel(R) UHD Graphics))
KWin crash: cr2=0x8 err=0x4 rip=0x7ffffef653db
exception-ip: /usr/lib/x86_64-linux-gnu/libkwin.so.5 file_off=0x1fe3db
fatal-ret: /usr/lib/x86_64-linux-gnu/libQt5Core.so.5 file_off=0x312e16

Current-image focused check:
timeout 420 scripts/gpu/kwin-global-slot-proof.expect
KWIN-GLOBAL-SLOT-PROOF-PASS
workspace_self/wayland_server_self/cursors_self: status=PASS, slot_zero=1,
  got_matches=1, slot_path_poison=0 under /x86_64- poison pressure
```

Interpretation: the evidence decoder fix is ready, but this VM attempt did not
prove the new Chromium `gpu_preferences_decoded_len` fields because KWin
crashed before Chromium launch. The startup crash is the same
`libkwin.so.5`/`libQt5Core.so.5` low-null signature already captured by the
fault IP/return-path diagnostics. The old global-slot relocation hypothesis
remains closed on the current image. The next useful reducer is therefore a
focused KWin startup/lifetime reducer for the `EffectsHandlerImpl` low-null
member path, or a clean retry only after that startup lane is stable enough to
reach Chromium.

Harness evidence follow-up:

```text
2026-06-28 host replay against the same archive:
chromium_fault_summary:
  fault_count=1
  kde_exception_summary_seen=1
  pid=51 comm=kwin_wayland cr2=0x8 err=0x4
  ip_path=/usr/lib/x86_64-linux-gnu/libkwin.so.5 ip_file_off=0x1fe3db
  ret_path=/usr/lib/x86_64-linux-gnu/libQt5Core.so.5 ret_file_off=0x312e16
  lowptr_base=0x700390b8 lowptr_cr2=0x8 lowptr_slot_count=4
  lowptr_null_qword_count=2
  lowptr_rdi80_qword=0x70002820
  lowptr_rdi88_qword=0x0 lowptr_rdi88_is_null=1
  lowptr_rdi90_qword=0x70000000
  lowptr_rdi98_qword=0x0
chromium_fault_context:
  fault_pid=51 fault_pos=15178 process_evidence=0
  process_evidence_missing_guest_path=1
Synthetic mixed-format replay:
  summary+detailed duplicate for pid 7 counts once; separate plain pid 8
  keeps fault_count=2.
```

Patch: `chromium_video_fault_summary` now treats
`kde-exception-fatal-summary` as a first-class fault source, de-duplicates a
matching detailed `fatal page fault cr2=...` line, keeps a broad fallback for
plain `pid ... fatal page fault` lines, and exposes compact low-null slot
fields for `fatal-rdi+0x80`, `+0x88`, `+0x90`, and `+0x98`. The
`chromium_video_fault_context_summary` marker position now uses the earliest
of `fatal page fault` and `kde-exception-fatal-summary`, so the fault position
points at the rich KWin summary instead of the late process-death line. This is
host-side evidence parsing only; it does not change KDE, KWin, Qt, Xwayland,
Mesa, Chromium, or kernel behavior.

2026-06-28 KWin EffectsHandler lifetime metric attempt:

```text
Static/audit:
cc -O2 -Wall -Wextra -fPIC -shared \
  -o /tmp/kwin-alloc-trace-preload.so \
  scripts/image/kwin-alloc-trace-preload.c -ldl: PASS
exported symbol:
  _ZN4KWin18EffectsHandlerImpl24checkInputWindowStackingEv
independent audit: initial blind object-slot preload hook NO-GO; revised
  counter-only hook GO.
expect Tcl completeness: complete
git diff --check -- scripts/gpu/kde-plasma-desktop-smoke.expect
  scripts/image/kwin-alloc-trace-preload.c docs/linux-drm-abi-compat-plan.md:
  PASS
git -C kernel diff --check: PASS

Rootfs refresh:
cmake --build build-x86_64 --target rootfs-refresh -j2: PASS
/opt/xv6-kde-abi-libs/kwin-alloc-trace-preload.so staged size=51848

VM artifact:
build-x86_64/kde-plasma-desktop-smoke-history/20260628T110805Z-chromium-launch-only-kwin-effects-trace-pass/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1
cmdline included kde_kwin_alloc_trace=1
KWin: renderer=virgl (D3D12 (Intel(R) UHD Graphics)); no fatal fault
Xwayland: effective_glamor=es +extension_GLX=yes -extension_GLX=no
Chromium: launch_evidence_status=PASS; sampler_result status=PASS
Chromium process roles:
  browser_seen=1 gpu_seen=1 renderer_seen=0 zygote_seen=1
  render_fd_seen=1 render_fd_first_sample=42
GPU init errors: gpu_error_pid_context gl_error_pids=none gpu_exit_pids=none
Fault summary: fault_count=0 kde_exception_summary_seen=0
KWin alloc trace replay:
  begin_count=5 summary_count=1 effects_calls=0 effects_real_missing=0
  alloc_failures=0 mmap_failures=0 brk_failures=0
  pcre2_slot_samples=14670 pcre2_slot_changes=14
  pcre2_slot_poison_samples=0
  pthread_prio_protect_invalid_ceiling=0
  pthread_prio_protect_bad_lock_return=0
```

Interpretation: the safe counter-only preload did not preempt
`EffectsHandlerImpl::checkInputWindowStacking()` in this run
(`effects_calls=0`), which is consistent with libkwin internal calls not being
LD_PRELOAD-interposable. Keep the low-null `rdi+0x88` crash evidence in the
kernel fatal dump and post-evidence parser. The preload remains useful for KWin
allocator/PCRE2/mutex lifetime numbers and showed no allocator, mmap, PCRE2
poison, or prio-protect failures in the passing launch-only run. The run did
not exercise GBM preprobe or playback/capture timing because it was an
intentional KWin startup/Chromium launch-only lane.

Harness follow-up: `chromium_video_post_evidence` now emits
`kwin_alloc_trace_summary=...` so future lanes record KWin trace counts in the
durable post-evidence log instead of requiring manual grep of
`kde-kwin-alloc-trace.log`.

2026-06-28 full Chromium-video diagnostic after WSL/source and launcher
mismatch closure:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T112720Z-chromium-video-wayland-debug-capture-timeout-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-capture-timeout
```

Command shape: `KDE_SMOKE_REDUCER=chromium-video`, multiprocess Chromium,
`EGL_PLATFORM=gbm`, `--use-gl=egl-angle --use-angle=opengles`, GBM preprobe,
Wayland debug, `kde_xwayland_glamor=auto`, `kde_xwayland_enable_glx=1`,
`kde_kwin_alloc_trace=1`, `virtio_gpu_scanout_read_diag=1`,
`fb_present_sample_diag=1`, and Chrome DRM ioctl/fence tracing disabled.
Independent audit marked the lane GO as artifact-first diagnostics.

Evidence:

- KWin reached virgl and Xwayland ran with `effective_glamor=es` and
  `+extension_GLX=yes`.
- GBM preprobe passed:
  `host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3`.
- Chromium launched only far enough to sample browser and zygote roles:
  `browser_seen=1`, `zygote_seen=1`, `gpu_seen=0`, `renderer_seen=0`,
  `render_fd_seen=1`, browser fd `29 -> /dev/dri/renderD128`.
- Wayland debug was enabled (`WAYLAND_DEBUG=client`) but only 106 protocol
  lines were captured, with no primary surface attach/damage/commit and no
  PERF-VIDEO media/RVFC lines.
- Capture localized the timeout to the second PPM readback:
  `capture_status=status=running phase=frame frame=01
  op=fbstat-ppm-current path=/chromium-video-01.ppm`.
- The first capture frame completed as a full 1280x800 nonblack PPM
  (`3072016` bytes); frames 1..11 are partial 42-byte headers from timeout
  cleanup.
- `fbstat sample-current` succeeded immediately before the stuck
  `ppm-current`, and scanout-read diagnostics recorded five successful current
  KMS reads with stable nonblack hash `0xb945636f5d2a7707`.
- `scanout_read_summary`: `kms_count=5`, `distinct_fb_count=1`,
  `transfer_count=5`, `transfer_fail_count=0`,
  `kms_sample_unique_hash_count=1`, while present-sample diagnostics saw
  two hashes and one change before capture.
- KWin trace remained clean:
  `effects_calls=0`, `alloc_failures=0`, `mmap_failures=0`,
  `pcre2_slot_poison_samples=0`.

Interpretation: this run did not reach Chromium playback or GPU-process GL
selection. The current bottleneck is earlier: repeated diagnostic framebuffer
readback can block inside or immediately after `FB_GPU_SCANOUT_READ` while
KDE/Chromium startup is active. The existing success-only KMS readback log is
not enough to distinguish virtio readback, KMS readback, user copyout, and
userspace PPM write stages. The kernel now has gated `FB_GPU_SCANOUT_READ`
stage diagnostics behind `virtio_gpu_scanout_read_diag=1` /
`fb_scanout_read_ioctl_diag=1`, preserving the committed virtio-first /
KMS-override order while emitting `FB: scanout-read-ioctl[...]` and
`FB: kms-scanout-read[...]` begin/end timing rows. Next verification should
rerun a short capture-focused Chromium-video lane with
`fb_scanout_read_ioctl_diag=1` and inspect the last unmatched stage before
making a behavior-changing GPU/readback patch.

2026-06-28 short Chromium-video scanout-read diagnostic after the WSL/source
mismatch fix recheck:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T114305Z-chromium-video-short-scanout-read-diag-done-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video samples=2
post_evidence_status=FAIL reason=perf-start-missing
```

Command shape: multiprocess Chromium, `EGL_PLATFORM=gbm`,
`--use-gl=egl-angle --use-angle=opengles`, GBM preprobe, Wayland debug,
`kde_xwayland_glamor=auto`, `kde_xwayland_enable_glx=1`,
`kde_kwin_alloc_trace=1`, `virtio_gpu_scanout_read_diag=1`,
`fb_scanout_read_ioctl_diag=1`, and `fb_present_sample_diag=1`.

Evidence:

- Host GBM preprobe still passed:
  `host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3`.
- Capture completed: `capture_status=status=DONE samples=2`, both PPMs were
  full `3072016` byte 1280x800 frames, and `KCVCAPDONE` was reached.
- Both captured frames were solid black:
  `chromium-video-frame-stats.txt` reported `black_class=black`,
  `mean=0`, and identical hash
  `d4e96a65fd4f8e97bc1d762fc90cf2593bc2efb53a3125a72502fdae0f09395c`.
- Scanout sample-current also stayed black:
  `unique_hash_count=1`, `nonblack_count=0`, `center=0xff000000`.
- Stage diagnostics completed six `FB_GPU_SCANOUT_READ` ioctls with
  virtio, KMS, pixel copyout, and request copyout end markers; elapsed times
  ranged from about 262 ms to 457 ms, with no unmatched readback stage.
- Chromium still did not reach a renderer or GPU process:
  `browser_seen=1`, `zygote_seen=1`, `gpu_seen=0`, `renderer_seen=0`, with the
  browser holding `/dev/dri/renderD128`.
- Wayland debug was enabled, but there was no primary Chromium surface commit
  and no PERF-VIDEO media/RVFC output.

Interpretation update: the full-run capture timeout did not reproduce in the
short diagnostic lane, so do not patch readback behavior from the timeout
alone. The current black-screen evidence is upstream of scanout readback:
Chromium opens the render node but does not create renderer/GPU roles, does not
commit a primary Wayland surface, and never starts the page media probe. The
next reducer should stay on Chromium process/zygote startup and Wayland surface
delivery, with readback diagnostics available only as a guardrail.

2026-06-28 Wayland debug parser mismatch fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T115926Z-chromium-launch-only-egltrace-wayland-parser-mismatch-fix-done-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1
host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_status=status=DONE source=kde-app-launch-probe-log-attempted
sampler_result=kde_app_launch_probe chromium_sample_only=1 ... status=PASS
process_summary: browser_seen=1 zygote_seen=1 gpu_seen=0 renderer_seen=0 render_fd_seen=1
chromium_egl_trace_summary: process_browser=1 process_gpu=0 process_renderer=0 process_zygote=3 getproc=0 choose=0 create_context=0
```

The generated post-evidence from this run exposed a harness parser
contradiction: `surface_create_count=3` was parsed from Wayland protocol rows,
but `protocol_line_count=0`, `debug_line_count=0`, and `protocol_seen=0`.
The root cause was timestamp parsing. Chromium/Wayland debug timestamps are
formatted like `[ 174088.790]` with whitespace inside the brackets, while the
parser only accepted `[174088.790]`. The parser now accepts bracketed
timestamps with leading whitespace. Replay proof against the archived raw
`host-gui-wayland-chromium.log`:

```text
WAYLAND-PARSER-MISMATCH-FIX: PASS protocol_line_count=228 debug_line_count=202 surface_create_count=3 protocol_seen=1
```

Interpretation: do not carry forward the old
`reason=wayland-debug-no-protocol-lines` verdict from that launch-only
artifact. The corrected classification is "Wayland protocol exists, Chromium
creates three surfaces, but no surface has attach/damage/frame/commit delivery
ops"; that keeps the next reducer on Chromium process/GPU/renderer startup and
surface delivery rather than on missing Wayland logging.

2026-06-28 Chromium launch-path summary metrics:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T120615Z-chromium-launch-only-egltrace-launchpath-summary-done-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1
host_egl_gbm_status=status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=98 duration_ms=9000 interval_ms=250 burst_samples=61 burst_ms=3000 burst_interval_ms=50 status=PASS
```

Additive harness change: `chromium_egl_trace_summary` now includes
launch-path counters for trace-preload `open/openat/ioctl/execve/fork/vfork`
and `socketpair` rows, including failure counts, role-attributed browser/GPU/
zygote counts, render-node open counts, and ioctl timing sums/maxima. This is
xv6-owned post-evidence parsing only; no KDE/Qt/KWin/Xwayland/Mesa/Chromium
source changed.

2026-06-28 follow-up mismatch cleanup: the interrupted preload wait-hook
experiment was backed out, and the stale `chromium_egl_trace_summary`
`waitpid`/`wait4`/`waitid` counters were removed from the KDE smoke parser.
Child lifecycle evidence for the next reducer should come from the gated
kernel `chrome_lifecycle_trace=1` path, not from zero-valued preload fields.
The GBM preprobe command marker is the opaque `KCVEGLDONE`, and the GBM status
file remains completion-only; explicit `host-egl-gbm-gl-smoke: phase=result
status=PASS` in the durable log is still required for a preprobe pass.

Fresh proof lines:

```text
wayland_debug_summary: protocol_seen=1 protocol_line_count=228 debug_line_count=202
  surface_create_count=3 created_surface_without_delivery_count=3
  surface_delivery_op_count=0 attach_count=0 damage_count=0 frame_count=0 commit_count=0
wayland_surface_delivery_matrix: status=FAIL reason=no-primary-surface-commit
chromium_egl_trace_summary:
  process_browser=1 process_gpu=0 process_renderer=0 process_zygote=2
  open=3 open_fail=0 openat=0 openat_fail=0
  render_open=3 browser_render_open=3 gpu_render_open=0 zygote_render_open=0
  ioctl=8 ioctl_fail=0 browser_ioctl=8 gpu_ioctl=0 zygote_ioctl=0
  ioctl_elapsed_us_sum=1737 ioctl_elapsed_us_max=1348
  execve=0 execve_gpu=0 execve_renderer=0 execve_return=0 execve_return_fail=0
  fork=4 browser_fork=4 gpu_fork=0 zygote_fork=0
  socketpair=4 browser_socketpair=4 gpu_socketpair=0 zygote_socketpair=0
  getproc=0 dlsym=0 choose=0 create_context=0
process_summary:
  browser_seen=1 zygote_seen=1 gpu_seen=0 renderer_seen=0
  crashpad_seen=1 unknown_samples=1 render_fd_seen=1 browser_render_fd_seen=1
QEMU trace: ctx_submit=70 fence_ctrl=70 fence_resp=70 res_create_3d=233 res_flush=21 xfer_toh_3d=2
```

Interpretation: the current launch-only failure is no longer attributable to
host GBM, KWin virgl, Xwayland GLX, Wayland logging, render-node open, or the
first DRM ioctls: all pass in the browser process. It also no longer looks like
a sampled GPU-process GL-selection collapse in this lane; the trace and process
sampler agree that no GPU or renderer role reaches observable lifetime.
Chromium gets as far as browser-side GBM loader opens and zygote creation, then
stops before any EGL entrypoint, GPU-process exec/fork identity, renderer role,
or Wayland surface delivery operation. The next reducer should measure short
child lifecycle around the browser's post-zygote forks without perturbing child
post-fork state: add parent-side wait/exit-status or kernel-side process-exit
evidence for fork children, then decide whether the mismatch is process
lifecycle/wait, proc sampling blind spot, or a launch policy that prevents
Chromium from requesting GPU/renderer children.

2026-06-28 Chromium launch lifecycle trace reducer:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T123048Z-chromium-launch-lifecycle-trace-done-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1 chromium_multiprocess=1
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=98 duration_ms=9000 interval_ms=250 burst_samples=61 burst_ms=3000 burst_interval_ms=50 status=PASS
```

Trace/harness changes for this iteration were xv6-owned and diagnostic-only:
`chrome_lifecycle_trace=1` now reports clone/exec/exit/reap records with
`pid_seq`, role bits, exec paths, parent/waiter identity, and timestamps; the
KDE smoke post-evidence parser records `kernel_lifecycle_summary`. The audit
found and the follow-up fixed a clone-before-wakeup race, parent-vs-child
`esignal` logging, missing exec `pid_seq`/role parsing, and misleading
`clone_without_*` names. A parser follow-up after the run also fixed timestamp
prefixed `waitpid: reaping` lines; therefore the saved post-evidence in this
artifact shows `reap=0`, but the corrected raw run-log count is:

```text
corrected_kernel_lifecycle_counts clone=3 exec=5 exit=2 reap=2 waitpid=2
kernel_lifecycle_saved: clone=3 clone_process=3 clone_thread=0 clone_vfork=1 clone_vm=1 exec=5 exit=2
  process_clone_still_live_or_unobserved=1 process_clone_no_exec_seen=1
  exec_child_process=3 exec_zygote=2 exec_gpu=0 exec_renderer=0
  exit_child_process=2 exit_gpu=0 exit_renderer=0
```

Other key metrics:

```text
wayland_surface_delivery_matrix: status=FAIL reason=no-primary-surface-commit
  surface_create_count=3 created_surface_without_delivery_count=3 surface_delivery_op_count=0
chromium_egl_trace_summary:
  process_browser=1 process_gpu=0 process_renderer=0 process_zygote=2
  render_open=3 browser_render_open=3 ioctl=8 ioctl_fail=0
  ioctl_elapsed_us_sum=626 ioctl_elapsed_us_max=185
  fork=6 browser_fork=6 socketpair=6 browser_socketpair=6
  getproc=0 dlsym=0 choose=0 create_context=0 dlopen=6
process_summary:
  browser_seen=1 zygote_seen=1 gpu_seen=0 renderer_seen=0 crashpad_seen=1
  render_fd_seen=1 browser_render_fd_seen=1
QEMU trace:
  ctx_submit=47 fence_ctrl=47 fence_resp=47 res_create_3d=47 res_flush=16 xfer_toh_3d=2
```

Interpretation: lifecycle evidence closes the proc-sampling blind-spot theory
for the first failing phase. Chromium creates/reaps the crashpad monitor path
and execs two zygote children (`chrome_roles=0x42`), but no kernel exec,
process sampler, or EGL preload evidence shows `--type=gpu-process` or
`--type=renderer`. The next reducer should focus on the browser-to-zygote
launch policy/IPC edge after zygote readiness: why the browser never asks for
GPU/renderer children (or why that request is not visible as a clone/exec),
not on KWin virgl, host GBM, render-node open, or DRM ioctl failure.

2026-06-28 Chromium lifecycle role-trace mismatch fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T124218Z-chromium-launch-lifecycle-role-fix-done-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1 chromium_multiprocess=1
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
sampler_result=kde_app_launch_probe chromium_sample_only=1 samples=63 duration_ms=18000 interval_ms=500 burst_samples=26 burst_ms=2500 burst_interval_ms=100 status=PASS
post_evidence_status=PASS reason=launch-only
```

Fix: kernel Chromium lifecycle tracing now assigns an explicit browser role bit
(`TG_CHROME_TRACE_BROWSER`) to Chromium-shaped browser execs and uses a
role-aware `chrome_lifecycle_trace_match()` in clone/exec diagnostics. The
post-evidence parser now compares preload `fork` children against non-`vfork`
kernel clone children and accepts serial-interleaved clone continuation lines.

Proof from the fresh run:

```text
browser_exec: chrome_roles=0x80
gpu_exec: pid=406 --type=gpu-process chrome_roles=0xa
utility_exec: pid=409 --type=utility network.mojom.NetworkService chrome_roles=0x23
chromium_egl_trace_summary:
  process_browser=1 process_gpu=1 process_renderer=0 process_zygote=3
  render_open=4 browser_render_open=3 gpu_render_open=1
  ioctl=10 ioctl_fail=0 browser_ioctl=8 gpu_ioctl=2
  fork=7 browser_fork=6 zygote_fork=1 socketpair=11
process_summary:
  browser_seen=1 gpu_seen=1 zygote_seen=1 renderer_seen=0
  role_counts=browser:2,gpu-process:1,utility:1,zygote:3
kernel_lifecycle_saved_before_parser_replay:
  clone=7 exec=7 exit=4 reap=3 exec_gpu=1 exec_utility=1 exec_zygote=2
kernel_lifecycle_parser_replay_after_fix:
  clone=8 preload_fork_child=7 preload_fork_without_kernel_clone=0
  kernel_clone_without_preload_fork=0
```

Interpretation: the earlier lifecycle mismatch was a diagnostic attribution
gap, not proof that Chromium never reached a GPU child. After the role-trace
fix, the browser, GPU process, network utility, and zygote roles are visible in
kernel, process-sampler, and EGL-preload evidence. The next reducer should move
from process-lifecycle attribution to the GPU child GL/EGL startup edge:
the GPU process opens `/dev/dri/renderD128` and completes initial DRM ioctls,
but no EGL entrypoint trace (`eglChooseConfig`/`eglCreateContext`) appears and
the Wayland primary surface still never receives attach/damage/commit.

2026-06-28 process-group signal teardown mismatch fix:

The Chromium-video rerun at
`build-x86_64/kde-plasma-desktop-smoke-history/20260628T131232Z-chromium-gpu-egl-bootstrap-prechromium-kwin-futex-fail-noimage/`
showed KWin timing out before the Wayland socket, followed by
`kde-session: KWin startup attempt=1 failed` with no retry. The xv6 mismatch
was in kernel `kill(-pgid, SIGTERM)`: `pgroup_kill()` held `pid_lock` while
calling `tg_signal_send()`, which takes `pid_lock` internally. That could
deadlock KDE teardown in `kde-session` before the direct `kill(pid, SIGTERM)`
or retry path ran.

Fix: `pgroup_kill()` now snapshots process-group thread groups under
`pid_rlock()`, refs them, drops `pid_lock`, then delivers signals via
`tg_signal_send()` lock-free from the pgroup traversal. The proof reducer is
the xv6-owned `/bin/kde-pgroup-kill-probe`, staged from
`scripts/image/kde-pgroup-kill-probe.c`.

Proof:

```text
cmake --build build-x86_64 --target kernel -j2
cmake --build build-x86_64 --target rootfs-refresh -j2
build-x86_64/pgroup-kill-proof/20260628T-kill-pgid-python/serial-binary-final.log
kde_pgroup_kill_probe phase=after-kill ret=0 errno=0 Success
kde_pgroup_kill_probe status=PASS reaped=3 signaled=3
```

Interpretation: the KDE teardown/retry deadlock class is closed. If the next
KDE/Chromium lane fails before Chromium, treat it as a fresh KWin startup
failure rather than the fixed process-group signal mismatch.

2026-06-28 KDE kconf Python entrypoint mismatch fix:

The next Chromium-video GBM reducer reached KWin/virgl, Xwayland, the host
GBM preprobe, and Chromium browser launch, but failed with
`chromium-video-capture-crash` when KDE's kconf update path ran
`/usr/bin/python3 /usr/share/kconf_update/kwin-5.23-remove-cubeslide.py`.
The crash artifact is:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T134245Z-chromium-video-gbm-python-kconfupdate-crash-noimage/
pid 275 python3: fatal page fault cr2=0x18 err=0x4 rip=0x5728a7
```

Audit found an image-level interpreter split, not an upstream KDE change:
`/bin/python3.12` came from the xv6 sysroot, while `/usr/bin/python3.12`
came from the imported package tree and KDE shebangs used `/usr/bin/python3`.
`scripts/image/make-rootfs.sh` now normalizes `/usr/bin/python3` and
`/usr/bin/python3.12` to absolute symlinks back to `/bin/python3` and
`/bin/python3.12` after the package overlay is applied. A first attempt using
`../../bin/python3.12` reproduced an xv6 relative-symlink exec mismatch, so
the final rootfs uses absolute symlink targets matching other compat links.

Proof:

```text
cmake --build build-x86_64 --target rootfs-refresh -j2
debugfs build-x86_64/fs.img:
  /usr/bin/python3 -> /bin/python3
  /usr/bin/python3.12 -> /bin/python3.12
build-x86_64/python-kconf-proof/20260628T-python-kconf-path/run-staged.stdout.log
PYKCONF-STAGED: PASS usr_after=1 bin_after=1 delete_count=2
```

Interpretation: the rootfs no longer has competing Python executables on the
KDE kconf shebang path. This does not yet prove Chromium playback; rerun the
focused Chromium-video GBM reducer to see whether the previous kconf-update
fault is gone or whether a separate Python/kernel ABI bug remains.

2026-06-28 Chromium glibc loader/rootfs mismatch fix:

The next audit of the Chromium-video GBM failure showed that the current
launcher was no longer inheriting KDE preload state, but the image still had
four independent regular-file copies of `ld-linux-x86-64.so.2`:
`/lib`, `/lib64`, `/lib/x86_64-linux-gnu`, and
`/usr/lib/x86_64-linux-gnu`. Chromium's launch-time library search path also
preferred top-level `/lib` and `/usr/lib` before the multiarch package closure,
which could mix GLib/GObject families during child-loader startup.

Fix: `scripts/image/make-rootfs.sh` now canonicalizes the glibc loader after
package overlays are applied. `/lib/ld-linux-x86-64.so.2` remains the single
regular file and `/lib64`, `/lib/x86_64-linux-gnu`,
`/usr/lib/x86_64-linux-gnu`, and `/usr/lib64` loader entrypoints are relative
symlinks to it. `scripts/image/wayland-chromium-launcher.c` now puts
`/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu` before the top-level library
aliases in `LD_LIBRARY_PATH`, while still leaving the xv6 Mesa/DRM libraries
available from `/lib`.

Proof:

```text
cmake --build build-x86_64 --target rootfs-refresh -j2
debugfs build-x86_64/fs.img:
  /lib/ld-linux-x86-64.so.2 regular file
  /lib64/ld-linux-x86-64.so.2 -> ../lib/ld-linux-x86-64.so.2
  /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 -> ../ld-linux-x86-64.so.2
  /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 -> ../../../lib/ld-linux-x86-64.so.2
  /usr/lib64/ld-linux-x86-64.so.2 -> ../../lib/ld-linux-x86-64.so.2
build-x86_64/chromium-ldso-loader-path-proof/20260628T141917Z-quiet-fullpath-ld-linux-list/status.txt
status=PASS reason=ldso-verify-list-clean
postcheck=PASS no_ldso_assertions
```

The proof booted a throwaway nographic image with daemon/startup files blanked,
ran three full-path `/lib64/ld-linux-x86-64.so.2 --verify` checks against
Chromium, then ran `--list` against the same binary. The dependency list
resolved GLib/GObject/libc from `/usr/lib/x86_64-linux-gnu` and libgbm/libdrm
from `/lib`, with no `elf_machine_rela_relative` assertion. A direct
`chrome --version` reducer in the stripped-down boot timed out, so this closes
the loader-entrypoint/search-path mismatch only; rerun the focused
Chromium-video GBM reducer to prove whether Chromium's child GPU/network
processes are now past the old ld.so assertion.

2026-06-28 Chromium-video GBM rerun after loader/rootfs fix:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T143351Z-chromium-video-gbm-perf-start-missing-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-perf-start-missing
```

Harness/audit changes before the run:

- The smoke harness now separates Expect's per-operation `timeout` from the
  QEMU wrapper wall-clock timeout. Chromium-video defaults to
  `timeout --foreground 900`; other reducers retain the previous 360 second
  default unless `KDE_SMOKE_QEMU_TIMEOUT_SECONDS` or
  `KDE_SMOKE_TIMEOUT_SECONDS` is set.
- Independent read-only audit found no blocking Tcl scope, marker, or
  completion-status issue. The run log confirms the wrapper used
  `timeout --foreground 900`.

Evidence:

- KWin reached virgl on the Intel WSL D3D12 lane, and Xwayland ran with
  `glamor=auto effective_glamor=es enable_glx=0`.
- The command line included the intended
  `kde_xwayland_glamor=auto chrome_drm_ioctl_trace=1 chrome_drm_fence_trace=1`.
- Chromium launcher policy was correct after the loader fix:
  `EGL_PLATFORM=gbm`, `--use-gl=egl-angle`, `--use-angle=opengles`,
  `LD_PRELOAD=(unset)`, and `LD_LIBRARY_PATH` included
  `/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu`.
- No `elf_machine_rela_relative` or `Inconsistency detected` loader assertion
  appeared.
- GBM preprobe passed:
  `host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3`, with
  completion-only status
  `status=DONE source=host-egl-gbm-gl-smoke-log-copy-attempted`.
- Chromium-shaped GBM config matrix stayed Linux-control-like for this Mesa:
  pbuffer and native-window+pbuffer counts were `0`; window-only and
  surfaceless ES2/ES3 counts were `50`.
- Sampler passed:
  `kde_app_launch_probe chromium_sample_only=1 samples=63 duration_ms=18000
  interval_ms=500 burst_samples=26 ... status=PASS`.
- Capture produced 6 full nonblack 1280x800 PPMs before stopping around frame
  06. Frame hashes had `present_count=6`, `unique_hash_count=5`,
  `capture_changed_count=2`, and scanout sample-current had
  `nonblack_count=13`, `unique_hash_count=6`, `changed_count=5`.
- Chromium process evidence showed only browser, zygote, and crashpad/helper
  roles: `browser_seen=1`, `zygote_seen=1`, `renderer_seen=0`, `gpu_seen=0`,
  `render_fd_seen=1`, `browser_render_fd_seen=1`.
- No page/media metrics were emitted:
  `PERF-VIDEO` count `0`, `playing_count=0`, `tick_count=0`,
  `result_count=0`.
- `host-gui-wayland-chromium.log` contains a Chromium child crash:
  `Received signal ... SEGV_MAPERR 000000000030`, with `cr2=0x30` and
  `ip=0x458a5010`. Because the signal was emitted only in the launcher log and
  not as a kernel `fatal page fault`/`int3` line in `run.log`, the old harness
  classified the run as `perf-start-missing`.

Harness classifier follow-up: `chromium_video_post_evidence` now scans the
durable Chromium launcher log for child crash signatures such as
`Received signal`, `SEGV_MAPERR`, `SIGSEGV`, breakpoint traps, and common
fatal signal text. It records `launcher_crash_seen` and
`launcher_crash_match`, and classifies those artifacts as
`chrome-crash-regression` before later PERF-VIDEO absence checks. Static Tcl
completeness and `diff --check` passed; host replay against the preserved log
matches the exact `SEGV_MAPERR 000000000030` line.

Interpretation: the previous 360 second harness cutoff, GBM status-marker
fake-exit concern, loader-entrypoint mismatch, and GLib/libc library-search
mixing are closed for this run. The next reducer should target Chromium child
startup/renderer-GPU creation and the launcher-log low-address SEGV, ideally
with the existing EGL trace and Wayland debug paths enabled just long enough to
attribute which child or startup phase faults. Do not make a kernel DRM
behavior patch from this artifact; Chromium never reached page media
execution, no renderer/GPU process was sampled, and the captured nonblack
frames are KDE/Chromium-shell state rather than proven video playback.

2026-06-28 Chromium launch-only GBM/EGL-trace/Wayland-debug attribution run:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T144431Z-chromium-launch-only-gbm-egltrace-wayland-lifecycle-noimage/
KDE-PLASMA-DESKTOP-SMOKE-DONE reducer=chromium-video chromium_launch_only=1 chromium_multiprocess=1
```

Command shape: launch-only Chromium-video reducer, multiprocess Chromium,
`EGL_PLATFORM=gbm`, GBM preprobe enabled, `WAYLAND_DEBUG=client`, Chromium EGL
trace preload enabled, shortened 12s sampler, one nominal sample, and
`chrome_lifecycle_trace=1` with Chrome DRM ioctl/fence tracing disabled. A
read-only audit marked the shape GO because it avoids playback/capture pressure
while keeping launcher, sampler, lifecycle, Wayland, EGL-trace, and GBM
preprobe evidence.

Evidence:

- GBM preprobe still passed:
  `host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0`.
- The launcher policy was correct for the intended probe:
  `use_gl="egl-angle"`, `use_angle="opengles"`, `egl_platform="gbm"`,
  `wayland_debug="client"`, `egl_trace="1"`, and
  `LD_PRELOAD=/opt/host-gui/wayland-chromium/lib/chromium-egl-trace-preload.so`.
- The old low-address signal did not reproduce in this lighter lane:
  `launcher_crash_seen=0`.
- The run did reproduce a loader failure in the Chromium launcher log:
  four `Inconsistency detected by ld.so ... elf_machine_rela_relative`
  assertions. The previous post-evidence code missed this because it was not a
  signal-shaped crash and launch-only could still pass.
- Lifecycle numbers show Chromium reached child startup beyond the earlier
  no-GPU-process state: `clone=10`, `exec=7`, `exit=6`, `reap=4`,
  `exec_gpu=1`, `exec_utility=1`, `exec_zygote=1`, `exec_renderer=0`.
  The zygote, GPU, and utility children exited with status `127`; the GPU reap
  recorded `wait_status=32512`.
- Process samples saw the same roles late in the sampler window:
  browser samples `108`, zygote samples `45` across two pids, GPU samples `4`,
  utility samples `4`, renderer samples `0`.
- Wayland debug captured protocol setup but no surface delivery:
  `protocol_line_count=237`, `surface_create_count=3`,
  `created_surface_without_delivery_count=3`, `attach_count=0`,
  `damage_count=0`, `commit_count=0`, and
  `wayland_surface_delivery_matrix status=FAIL reason=no-primary-surface-commit`.
- The EGL trace summary shows browser-side GBM/render-node work only:
  `render_open=3`, `browser_render_open=3`, `ioctl=8`, `ioctl_fail=0`,
  `dlopen=6`, but `process_gpu=0`, `gpu_render_open=0`,
  `egl_initialize=0`, `choose=0`, `create_context=0`,
  `create_window=0`, and `create_pbuffer=0`.
- QEMU virtgpu activity remained modest: `ctx_submit=64`, `res_flush=19`,
  `set_scanout=19`, `res_create_3d=217`, `res_xfer_toh_3d=2`.

Harness classifier follow-up: `chromium_video_post_evidence` now scans the
durable Chromium launcher log for loader-fatal signatures
(`Inconsistency detected by ld.so`, `elf_machine_rela_relative`, and
`ld.so:.*Assertion`). It records `launcher_loader_seen` and
`launcher_loader_match`, and classifies the run as
`launcher-loader-regression` before launch-only can pass. Static Expect
completeness and `diff --check` passed, and an archived-log replay matched the
exact loader assertion. A read-only audit marked the classifier GO and confirmed
that a future same-shape artifact will fail as
`KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-launcher-loader-regression`.

Probe-perturbation finding: the injected xv6-owned
`chromium-egl-trace-preload.so` has a direct dynamic dependency on both
`libc.so.6` and `ld-linux-x86-64.so.2`; its version need includes
`ld-linux-x86-64.so.2 GLIBC_2.3`, and the undefined symbol set includes
`dlvsym@GLIBC_2.34`. Local compile probes with `-Wl,--as-needed`, with
`-Wl,-z,now`, and without `-ldl` still preserved the `ld-linux` dependency.
This makes the EGL-trace preload a plausible perturbation source for the
child-loader assertion. The next proof should either make the xv6-owned preload
less loader-hostile or rerun the same lifecycle/Wayland launch-only lane with
`KDE_SMOKE_CHROMIUM_EGL_TRACE=0` to separate Chromium's baseline child
`status=127` behavior from trace-preload-induced loader failure.

Follow-up proof and fix: rerunning the same launch-only GBM/Wayland-debug
lifecycle lane with `KDE_SMOKE_CHROMIUM_EGL_TRACE=0` produced
`20260628T145621Z-chromium-launch-no-egltrace-browser-int3-noimage`. The
launcher policy recorded `egl_trace="(unset)"`, `LD_PRELOAD="(unset)"`,
`chromium_egl_trace_log_present=0`, `launcher_loader_seen=0`, and
`launcher_loader_match=""`; the previous ld.so assertion did not reproduce.
The run reached the browser process and failed later as
`chromium-video-chrome-crash-regression` with a browser `int3` at
`rip=0x4ad85a17`, no GPU process, no renderer, and
`wayland_surface_delivery_matrix status=FAIL reason=no-primary-surface-commit`.
GBM remained healthy in the preprobe:
`host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3`; the status file
was completion-only. The sampler also passed:
`kde_app_launch_probe chromium_sample_only=1 samples=110 ... status=PASS`.

Mismatch fix: the xv6-owned Chromium trace preload was changed to be
loader-passive. It no longer interposes `dlsym`, `dlopen`, or direct EGL
entrypoints and no longer calls `dlvsym`; it keeps raw-syscall telemetry for
process identity, exec, render-node opens, DRM ioctls, socketpair/IPC, fd
dup/close, and `close_range`. A local rebuilt shared object with the staging
script flags now has only `NEEDED libc.so.6`; `readelf -Ws` shows no `dlvsym`,
`dlsym`, `dlopen`, exported EGL interposition symbols, or exported
`fork`/`vfork` hooks. The smoke harness now records both
`chromium_egl_trace_entrypoint_seen` and
`chromium_egl_trace_activity_seen`, and trace-on classification requires trace
activity rather than direct EGL entrypoint interception.

Current reducer target after this fix: the loader/preload mismatch is separated
from Chromium's baseline launch. The next KDE-safe reducer should focus on the
browser-process `int3` path before GPU/renderer startup, using the preserved
artifact above plus a fresh trace-on run only after the passive preload is
staged into the rootfs image.

2026-06-28 passive Chromium trace-preload verification:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260628T182413Z-chromium-launch-passive-egltrace-loader-regression-noimage/
KDE-PLASMA-DESKTOP-SMOKE-FAIL chromium-video-launcher-loader-regression
```

Command shape: launch-only Chromium-video reducer, multiprocess Chromium,
`EGL_PLATFORM=gbm`, GBM preprobe enabled, `WAYLAND_DEBUG=client`,
`KDE_SMOKE_CHROMIUM_EGL_TRACE=1`, and `chrome_lifecycle_trace=1`, with DRM
ioctl/fence tracing disabled and the passive `chromium-egl-trace-preload.so`
already staged in `build-x86_64/fs.img` at hash
`1105c24ef1455018d6673ce9bb05126ac58224110fb281b1a0b58ea77c85bd12`.

Evidence:

- The passive preload identity was correct before the run: `readelf -d` showed
  only `NEEDED libc.so.6`; symbol audit found no `dlvsym`, `dlsym`, `dlopen`,
  exported EGL interposition symbols, or `fork`/`vfork` hooks.
- KWin reached virgl on the Intel WSL D3D12 lane and Xwayland used the ES
  GLAMOR path with GLX disabled: `effective_glamor="es"`, `enable_glx="0"`.
- The GBM preprobe passed and recorded the Chromium-shaped EGL config matrix:
  pbuffer profiles still had count `0`; window-only and surfaceless ES2/ES3
  profiles had count `50`.
- Chromium launcher policy was as requested:
  `use_gl="egl-angle"`, `use_angle="opengles"`, `egl_platform="gbm"`,
  `wayland_debug="client"`, `egl_trace="1"`, and the preload in `LD_PRELOAD`.
- The loader regression reproduced despite removing EGL/dlsym/dlopen
  interposition:
  `Inconsistency detected by ld.so ... elf_machine_rela_relative`.
- Passive trace activity was present but not EGL-entrypoint activity:
  `chromium_egl_trace_activity_seen=1`,
  `chromium_egl_trace_entrypoint_seen=0`, `dlopen=0`, `dlsym=0`,
  `egl_initialize=0`, `choose=0`, `create_context=0`.
- Child lifecycle is now richer than the old no-GPU-process failure:
  `clone=13`, `exec=13`, `exec_gpu=4`, `exec_utility=4`, `exec_zygote=2`,
  `exec_renderer=0`; `exit_gpu=4`, `exit_utility=3`, `exit_zygote=2`, all
  failing children exited with status `127`.
- Process sampling saw browser, zygote, GPU, and utility roles but no renderer:
  `browser_seen=1`, `zygote_seen=1`, `gpu_seen=1`, `renderer_seen=0`.
- Wayland protocol connected and created surfaces, but no primary surface was
  delivered: `surface_create_count=4`, `attach_count=0`, `commit_count=0`,
  `wayland_surface_delivery_matrix status=FAIL reason=no-primary-surface-commit`.
- QEMU virtgpu activity stayed low for a launch-only lane:
  `ctx_submit=127`, `res_flush=77`, `set_scanout=77`, `res_create_3d=223`.

Interpretation: the previous direct EGL/dl interposition was not the only
loader perturbation. Merely injecting the xv6-owned trace preload into Chromium
children is enough to reproduce the dynamic-loader assertion on this guest.
Do not use LD_PRELOAD-based Chromium tracing as the next attribution tool for
child startup. The next reducer should compare the no-preload lifecycle path
against Linux and xv6 using kernel/process evidence only, then reduce the child
`status=127`/loader path at the ELF loader or `execve` ABI boundary without
patching Chromium, Mesa, Xwayland, KWin, Qt, or KDE.

2026-06-29 fullscreen/local-video follow-up evidence:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260629T004737Z-chromium-local-video-drm-stall-postprocess-fail/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T005952Z-chromium-local-video-capture-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T010652Z-chromium-local-video-scanoutdiag-prompt-sync-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T011245Z-chromium-local-video-scanoutdiag-chrome-int3-plasma-crash/
```

Evidence:

- Two low-overhead local-file video reducers reproduce the YouTube-like
  performance class without external network: `fps=4.2 speed=0.173
  dropPct=68.33` and `fps=3.6 speed=0.172 dropPct=65.94`.
- Frame capture in the first two runs shows normal motion through about frame
  4, then near-static scanout; the second run timed out during frame 6 capture.
- One earlier run showed an intermittent 85.355 s
  `DRM_IOCTL_VIRTGPU_GETPARAM` duration for `param=8`, which is
  `VIRTGPU_PARAM_EXPLICIT_DEBUG_NAME`, not cross-device. The same stall did
  not reproduce in the follow-up, so do not patch GETPARAM semantics from that
  single sample.
- With `virtio_gpu_scanout_read_diag=1` and
  `fb_scanout_read_ioctl_diag=1`, a quiet scanout diagnostic run reached
  capture and completed all 8 frames. Full-frame readback took roughly
  0.23-0.47 s per ioctl and did not hang.
- That same diagnostic run had no `PERF-VIDEO` lines because Chromium never
  reached playback. The browser hit a deliberate `int3` at
  `rip=0x4ad25d94` / file offset `0xad24d94`, and the launcher log recorded
  repeated `elf_machine_rela_relative` loader assertions even with
  `LD_PRELOAD` unset.
- Plasma also showed heap corruption during the same run:
  `free(): invalid pointer` followed later by `double free or corruption`.
- The diagnostic capture after the crash read a stable, invalid-looking
  desktop image: all 8 PPMs had the same SHA256, sample-current had one hash,
  and KMS readback reported the same fb/bo/resource with
  `sample_nonblack=3` and `sample_center=0xffffffffffffffff`.

Interpretation: there are at least two current failure classes. The earlier
local-video runs prove a real playback/presentation bottleneck when Chromium
survives long enough to play; the scanout diagnostic run proves the latest
crash path can fail earlier in dynamic-loader/child-startup or memory-lifetime
territory, leaving the desktop frozen before video metrics exist. The next
behavior-changing work should not be DRM GETPARAM or Vulkan. First reduce the
no-preload Chromium child loader assertion / browser `int3` and the concurrent
Plasma allocator corruption against Linux, then rerun the local-video
performance reducer after child startup is stable.

2026-06-29 KWin preload isolation:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260629T212516Z-media-codecs-launchonly-kwin-startup-gp/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T212645Z-media-codecs-launchonly-kwin-startup-gp-repeat/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T213038Z-media-codecs-launchonly-no-kwin-preload-browser-zygote-only/
```

Two launch-only Chromium media-codec probes failed before Chromium because
KWin crashed during startup while running with the KWin-only compatibility
`LD_PRELOAD` set to `libKF5Codecs.so.5:libpcre2-16.so.0`. The failures were
different instruction pointers, but both had string-shaped bogus pointers:
one resolved around QtCore `QMutex::unlock()`, and the repeat resolved to
`KWin::GLPlatform::driver() const` with `rdi=0x676e69776b62696c`
(`"libkwing"` bytes) and nearby memory containing repeated
`"libkwinglutils.s"`.

A counter-run with `kde_kwin_no_compat_ld_preload=1` reached a working KWin
Wayland session and logged virgl acceleration:

```text
kde-session: KWin inherited LD_PRELOAD disabled by default
OpenGL renderer string: virgl (D3D12 (Intel(R) UHD Graphics))
kde-session: KWin and plasmashell are running
```

That run then advanced to the Chromium probe and failed later with
`browser+zygote` only, no sampled renderer/GPU process, and console logging
stopping at:

```text
XV6-STEP before canPlayType 1 video/mp4; codecs="avc1.42E01E, mp4a.40.2"
```

Policy update: KWin now defaults to no `LD_PRELOAD`; the old compatibility
preload path is kept only as an explicit reducer knob
`kde_kwin_compat_ld_preload=1`, while full inheritance remains available with
`kde_kwin_inherit_ld_preload=1`. This removes a global startup corruption
hazard without patching upstream KWin/Qt/KDE/Mesa.

Follow-up verification found the preload policy is not the full root cause:
with default-disabled KWin preload, KWin still intermittently faulted in
`KWin::EffectsHandlerImpl::checkInputWindowStacking()` during
`Workspace::init()`. The `this` pointer was the ASCII bytes for
`"/x86_64-"`, while the caller was Qt signal delivery from
`VirtualDesktopManager::currentChanged()`. The current interpretation is
runtime object/signal target corruption, not a direct GPU command failure and
not the old KWin global-slot relocation bug: a fresh
`kwin-global-slot-proof.expect` run passed, with singleton slots and
`R_X86_64_GLOB_DAT` targets clean under `/x86_64-/usr/lib` poison pressure.

The KWin alloc-trace run did not reproduce the KWin #GP, but it showed PCRE2
allocator slots stayed free of poison (`pcre2_slot_poison_samples=0`) and no
allocation/mmap failures were recorded before the run timed out. That timeout
was a harness mismatch: preflight required Xwayland even though KDE Wayland and
Chromium Wayland can be ready while Xwayland is deferred until the first X11
client. The smoke preflight now waits for KWin/Plasma readiness only; Xwayland
must be required by X11/GLX-specific reducers, not by the generic
Chromium/Wayland preflight.

Success criteria:

- Current harness artifacts include host HTTP evidence when relevant,
  role-derived Chromium process evidence, screenshot/frame evidence, and a
  reduced ABI explanation for any failure.
- Local video gate stays green after any GPU/DRM-visible change.

2026-06-29 full-screen BO_PRESENT scanout-cache evidence:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260629T0540Z-submit-depth32-capture-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T0544Z-submit-depth4-capture-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T0547Z-default-depth-pactl-pagefault/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T0550Z-default-depth-pageflip-cache-capture-timeout/
build-x86_64/kde-plasma-desktop-smoke-history/20260629T0556Z-pageflip-readback-diag-capture-static/
```

Before the scanout-cache change, the focused Chromium-video GBM reducer proved
that the present-copy path was not active and the hot path was full-screen
virgl `BO_PRESENT` scanout churn:

```text
perf_result: fps=25.7 speed=0.953 presentedFPS=25.7 decodedFPS=61.2 dropPct=68.77
fbstat: virtio_present_copy_calls=0 virgl_bo_presents=349
fbstat: virtio_async_posted_submit_3d=420 virtio_async_posted_flush=0
fbstat: bo_present_virtio_ticks=17612962173 display_presents=349 kms_page_flips=348
qemu_trace: ctx_submit=421 set_scanout=352 res_flush=352 fence_ctrl=421
```

The current xv6-owned kernel change routes only exact full-screen, zero-offset
virgl `BO_PRESENT` scanouts through `virtio_gpu_page_flip_resource()`, with
fallback to `virtio_gpu_bind_resource_scanout()` and a destroy-time scanout
lifetime guard. KDE, Qt, KWin, Xwayland, Mesa, and Chromium source remain
upstream-clean.

The first same-command default-depth run after the change improved the hot
path materially:

```text
perf_result: fps=40.8 speed=0.993 presentedFPS=40.8 decodedFPS=64.0 dropPct=40.10
fbstat: virtio_async_posted_submit_3d=689 virtio_async_posted_flush=623
fbstat: virtio_async_make_room_submit_3d_stalls=33 virtio_async_make_room_flush_stalls=0
fbstat: virtio_present_copy_calls=0 virgl_bo_presents=623
fbstat: bo_present_virtio_ticks=7624126319 display_presents=623 kms_page_flips=622
qemu_trace: ctx_submit=690 set_scanout=6 res_flush=626 fence_ctrl=690
```

This closes the per-frame `SET_SCANOUT` regression class for full-screen
Chromium playback: `SET_SCANOUT` dropped from 352 to 6 and asynchronous
`RESOURCE_FLUSH` became the active display signal. The remaining gap is not
the present-copy drain path.

Negative depth evidence: overriding `virtio_gpu_async_submit_depth=32` and
`virtio_gpu_async_submit_depth=4` both produced capture-timeout artifacts that
stopped before the page reached playback. Do not globally raise submit depth
above the current conservative default from this evidence.

The latest readback diagnostic run did not hang and captured fbstat:

```text
status: chromium-video-capture-static
perf_media_summary: last_time=5.024 last_presented=147 last_decoded=305 last_dropped=121
capture_timing_summary: paired_count=3 avg_ms=2740.548 max_ms=3615.391
scanout_read_summary: kms_count=9 distinct_fb_count=2 distinct_bo_count=2 distinct_resource_count=2 transfer_count=9 transfer_fail_count=0
fbstat: virtio_async_posted=604 virtio_async_posted_submit_3d=333 virtio_async_posted_flush=268
fbstat: virtio_async_make_room_stalls=24 virtio_async_make_room_submit_3d_stalls=24 virtio_async_make_room_max_wait_us=439814
fbstat: virgl_bo_presents=268 bo_present_virtio_ticks=3089620501 kms_page_flips=267
qemu_trace: ctx_submit=360 set_scanout=6 res_flush=297 fence_ctrl=360
```

Interpretation: full-frame `FB_GPU_SCANOUT_READ` is a perturbing diagnostic
tool under video load because it drains async work and reads both the selected
virtio scanout and the current KMS fb, but it is not the root cause of the
multi-minute playback slowdown. The open playback gap is now Chromium
GPU-process/renderer lifecycle jitter plus async submit backpressure: repeated
GPU child exits still show `gl=none,angle=none`, the sampler can miss a live
GPU role entirely, and playback can enter `waiting` after a few seconds with
heavy frame drop. Next reducers should keep the full-screen page-flip cache,
avoid global submit-depth changes, and focus on Chromium child role creation,
GL preference propagation, and the `virtio_async_make_room_submit_3d_stalls`
shape.

2026-06-29 Chromium GL selector audit update:

Primary Chrome 150.0.7871.24 source check found that the command-line input
names differ from Chromium's diagnostic display names. `ui/gl/gl_switches.cc`
defines `kGLImplementationANGLEName` as `angle` and
`kANGLEImplementationOpenGLESName` as `gles`; `ui/gl/gl_implementation.cc`
prints those back as `(gl=egl-angle,angle=opengles)`. The xv6-owned Chromium
launcher and focused proof scripts were passing the printed names
`--use-gl=egl-angle --use-angle=opengles`, which causes Chromium's command-line
parser to produce `gl=none,angle=none` before any EGL/Mesa/render-node work.
The launcher, KDE smoke default extra flags, and Linux Chromium baseline proof
scripts now use the parser names `--use-gl=angle --use-angle=gles`.

Implication: rerun the focused Chromium-video GBM reducer before any kernel
presentation/backpressure change. If the GPU child now survives to EGL/DRM
activity, the previous `gl=none,angle=none` evidence was a launch-policy bug,
not a kernel ABI failure. If it still exits after EGL entry, continue with the
existing Mesa/DRM reducer path.

2026-06-29 corrected-selector reducer results:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260629T153606Z-chromium-gbm-correct-gl-selector-capture-timeout/
launcher_policy_summary: use_gl="angle" use_angle="gles" egl_platform="gbm"
gl_request_error_count=0 gl_request_none_count=0
host_egl_gbm_smoke_result=host-egl-gbm-gl-smoke: phase=result status=PASS api=gles3 exit_status=0
gpu_init_error_count=6 gpu_config_error_count=4 gpu_exit_error_count=2
gpu_error_first=eglCreateContext ES 3.0 failed with error EGL_SUCCESS
GPU log: No suitable EGL configs found; CreateOffscreenGLSurface failed; CollectGraphicsInfo failed
perf_media_summary: result_fail_count=1 last_presented=571 last_decoded=960 last_dropped=559

build-x86_64/kde-plasma-desktop-smoke-history/20260629T153933Z-chromium-surfaceless-correct-gl-selector-sampler-timeout/
launcher_policy_summary: use_gl="angle" use_angle="gles" egl_platform="surfaceless"
kernel_lifecycle_summary: exec_gpu=2 exit_gpu=10
process_policy_summary: gpu_use_gl_count=5 gpu_use_angle_count=5 last_use_gl="role=gpu-process value=angle" last_use_angle="role=gpu-process value=gles"
gpu_init_error_count=3 gpu_config_error_count=2 gpu_exit_error_count=1
GPU log: eglCreateContext ES 3.0 failed; No suitable EGL configs found; CreateOffscreenGLSurface failed
```

Interpretation: the global launcher mismatch is fixed and the old
`gl=none,angle=none` signature is gone. Chromium now reaches GPU-process and
render-node work, then fails GL info collection on the offscreen EGL surface
path. Primary Chromium source for 150.0.7871.24 shows Wayland Ozone chooses
`SurfacelessEGL` for zero-size offscreen surfaces only when
`GLDisplayEGL::IsEGLSurfacelessContextSupported()` is true; that boolean starts
from `EGL_KHR_surfaceless_context` and is then validated by creating a
surfaceless context and requiring the live GL context to advertise
`GL_OES_surfaceless_context`. Otherwise Ozone falls back to
`PbufferGLSurfaceEGL`, whose config query fails on the current virgl/GBM shape
because pbuffer and window+pbuffer config counts are zero.

The xv6-owned GBM smoke already proves the GBM display advertises
`EGL_KHR_surfaceless_context`, has configless support, and can create a
surfaceless ES3 context despite zero pbuffer configs. The next proof must
compare the exact Chromium gate: record whether that successful context also
has `GL_OES_surfaceless_context`, and then run a corrected-selector
Chromium launch with EGL tracing to determine whether Chrome initialized a
Wayland or GBM native display whose extension set differs from the standalone
GBM probe. Do not default to Vulkan or patch kernel backpressure from this
evidence; the current blocker is Chrome's EGL offscreen/surfaceless admission.

Harness note: the surfaceless run also proved the sampler status file can stay
at `status=running phase=probe-returned` even after the explicit sampler PASS
line is durable. The smoke harness now treats
`kde_app_launch_probe chromium_sample_only=1 ... status=PASS` plus fast-census
PASS, when enabled, as completion evidence while still logging the raw status
file for diagnosis.

2026-06-29 sparse Chromium address-space VM fix:

```text
before fix, xv6 16 GiB sparse MAP_NORESERVE reducer:
after-prot-none-noreserve VmSize=16780756 kB VmRSS=724 kB VmPTE=32774 kB
after-mprotect-all elapsed_ms=62 VmPTE=32774 kB

Linux control, same reducer shape:
16 GiB: VmPTE stayed ~44-52 kB, mprotect-all below 1 ms probe resolution
1 TiB: VmPTE stayed ~48-60 kB, mprotect-all below 1 ms probe resolution

after fix, xv6 1 TiB sparse MAP_NORESERVE reducer:
build-x86_64/kde-plasma-desktop-smoke-history/20260629T211524Z-sparse-mmap-mprotect-1tib-after-vm-fix/
after-prot-none-noreserve elapsed_ms=14 VmSize=1073745364 kB VmRSS=724 kB VmPTE=88 kB
after-touch-one-page elapsed_ms=0 VmPTE=100 kB
after-mprotect-all elapsed_ms=0 VmPTE=100 kB
status=PASS mode=sparse-mmap-mprotect gb=1024
```

Interpretation: the long Chromium-video run's huge sampled `VmPTE`
(`VmSize` around 1.5 TiB with `VmPTE` around 2.9 GiB) was a global procfs/VM
ABI mismatch, not real page-table memory. xv6 was reporting `VmPTE` as
virtual pages times `sizeof(pte_t)`, and `vm_mprotect()` walked every 4 KiB
page in large sparse ranges even when no page-table subtree existed. The VM
now exposes an architecture page-table helper that skips empty subtrees for
sparse range operations and reports `VmPTE` from actual allocated page-table
pages. This removes a multi-second VM-lock stall class for Chromium-sized
sparse reservations and makes process samplers stop interpreting sparse
address reservations as gigabytes of page-table pressure.

2026-06-29 global Chromium launcher policy update:

Two no-override launch-only Chromium-video reducers were run after refreshing
the rootfs and preserving compact history artifacts:

```text
build-x86_64/kde-plasma-desktop-smoke-history/20260629T2253-global-surface-eglangle-launchonly-pass/
launcher_policy_summary: use_gl="egl-angle" use_angle="opengles" egl_platform="surfaceless" argv_use_gl_count=1 argv_use_angle_count=1
gpu_init_error_count=0 gpu_config_error_count=0 gpu_exit_error_count=2
gl_request_error_count=2 gl_request_none_count=2
perf_media_summary: currentSrc selected, playing_count=0, result_fail_count=1
sampler_result: chromium_sample_only=1 status=PASS

build-x86_64/kde-plasma-desktop-smoke-history/20260629T2259-global-surfaceless-parsernames-plasma-crash/
launcher_policy_summary: use_gl="angle" use_angle="gles" egl_platform="surfaceless" argv_use_gl_count=1 argv_use_angle_count=1
gpu_init_error_count=0 gpu_exit_error_count=0 gl_request_error_count=0
process_policy_summary: browser carried angle/gles, gpu_use_gl_count=0
run.log: free(): invalid pointer; KCrash plasmashell; Wayland connection fatal Bad file descriptor
sampler_result: chromium_sample_only=1 status=PASS
```

Interpretation: `EGL_PLATFORM=gbm` plus parser-name ANGLE remains too risky
for the default path because it previously produced a huge EGL error loop and
OOM. `EGL_PLATFORM=surfaceless` is the safer global default. However, making
parser-name `--use-gl=angle --use-angle=gles` the global default on surfaceless
is also unsafe on the current KDE image: it crashed Plasma/Wayland before a
sampled GPU process or media console evidence appeared. The active rootfs was
refreshed after reverting the default launcher and KDE smoke auto-flags to the
KDE-stable containment policy `EGL_PLATFORM=surfaceless`,
`--use-gl=egl-angle --use-angle=opengles`; binary strings in
`build-x86_64/fs.img:/bin/wayland-chromium` confirm that policy.

This containment policy closes the GBM/OOM regression class but does not close
GPU acceleration or video playback: Chromium still logs `gl=none,angle=none`,
GPU children exit during initialization, and video can stall at `ready=0 net=2`.
Do not claim Chromium GPU acceleration is fixed from the containment pass.

Next Chromium-video proof should keep the stable default for normal KDE runs
and use explicit `WAYLAND_CHROMIUM_EXTRA_FLAGS="--use-gl=angle --use-angle=gles"`
only as a reducer. The next behavior work should reduce the parser-name
surfaceless Plasma crash (`free(): invalid pointer` / Wayland bad fd) before
promoting parser-name GPU activation to the desktop default. In parallel, use
the stable default to continue collecting video stalls as GPU-process restart,
EGL offscreen/surfaceless admission, or real RSS/BO growth evidence rather than
sparse `mprotect()`/`VmPTE`.

2026-06-29 Chromium loader/rela reducer narrowing:

```text
build-x86_64/chrome-rela-probe-proof/20260629T234311Z/
CHROME-RELA-PROOF-PASS chain_passes=8

build-x86_64/chrome-ldso-proof/20260629T234951Z/
status=PASS reason=ldso-clean verify_done=8 list_done=2
```

Two new xv6-owned nographic reducers booted copied images with KDE startup
replaced by a startup script. `chrome-rela-probe --chrome-chain` repeatedly
verified the imported Chrome binary and dependency chain through both `pread`
and `mmap`; the large Chrome table reported
`relacount=1056441` and all eight chain passes completed without a bad
`R_X86_64_RELATIVE` entry. A separate loader-boundary proof ran eight
`/lib64/ld-linux-x86-64.so.2 --verify` passes and two `--list` passes against
the same Chrome binary with the Chromium launcher library path, again with no
`elf_machine_rela_relative` assertion.

Interpretation: the current intermittent Chromium
`elf_machine_rela_relative` failure is not explained by deterministic bad
rootfs bytes, a generic quiet-boot file-backed `mmap` corruption, or a stable
Chrome loader/dependency mismatch. Keep the next attribution inside the
KDE/Chromium pressure path: child `execve`/file-fault timing, concurrent
pcache/readahead behavior, desktop service memory corruption, or Chromium
multi-process startup state. The next full KDE reducer should either run a
pre-launch `chrome-rela-probe` in the same desktop boot or enable targeted
`chrome_exec_phase_trace`, `vm_file_fault_trace`, and `chrome_mmap_trace`
without LD_PRELOAD-based Chromium tracing.

2026-06-30 follow-up: the surfaceless `egl-angle` Chromium-video reducer no
longer reproduced the RELA/loader assertion and did not hit a kernel trap, but
it still failed before playback. The sampler timed out with no renderer role,
while GPU processes exited after Chromium reported a requested GL implementation
of `gl=none,angle=none`; a later Chromium/Dawn warning said it could not get
`eglChooseConfig`. The run was preserved at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T001036Z-surfaceless-eglangle-gpu-gl-none-sampler-timeout`.
Next evidence should focus on EGL proc-address resolution in the Chromium GPU
process before changing kernel DRM or file-fault behavior.

2026-06-30 updated Chromium-video proof: a fair surfaceless
`--use-gl=egl-angle --use-angle=opengles` run with a 22s startup window and one
capture sample reached playback and preserved durable evidence at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T002654Z-eglangle-opengles-fair-fps29-drop48`.
The page completed, but failed the performance gate:
`fps=29.2 speed=0.968 presentedFPS=29.2 decodedFPS=63.5 dropPct=47.65`.
Sampler and fast-census evidence both passed, the scanout sample changed and was
nonblack, and no kernel fault or Chromium INT3 occurred. The remaining failure
is not page launch; it is GPU process selection/fallback plus frame pacing:
Chromium launched `egl-angle/opengles` GPU processes that exited with
`Requested GL implementation (gl=none,angle=none)`, then continued with a
`--use-gl=disabled` GPU process while the video decoded far ahead of presented
frames. A Linux-style `--use-gl=angle --use-angle=gles` A/B was archived at
`build-x86_64/kde-plasma-desktop-smoke-history/20260630T002200Z-angle-gles-wayland-vulkan-stall-no-perf-start`;
it removed the `gl=none` errors but entered Chromium's Wayland/Vulkan path and
stalled before the video start result, so it is not the current fix.
