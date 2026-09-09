# Chromium OpenGL audit — 2026-09-07

**The VM's virgl graphics path works, and Chromium can render WebGL 1 and
WebGL 2 through it. The ordinary desktop launcher still disables Chromium's
OpenGL/WebGL path.** The existing multiprocess mode, tested in a separate
profile, rendered visible triangles and returned the expected pixels.

No kernel, launcher, rootfs or browser implementation was changed or rebuilt.
Follow-ups remain in [the active plan](active-work-plan.md). This check does not
close the earlier [GUI](gui-progress-audit-20260907.md) or
[mouse-audit](gui-mouse-audit-20260907.md) failures.

## Comparison

| Evidence | Normal desktop launch | Multiprocess diagnostic |
| --- | --- | --- |
| Browser | Chrome for Testing 151.0.7922.34 | Same version |
| `chrome://gpu` OpenGL | Disabled | Enabled |
| `chrome://gpu` WebGL | Disabled | Hardware accelerated |
| Canvas/compositing/rasterization | Software only | Reported hardware accelerated |
| GL implementation | `(gl=disabled,angle=none)` | `(gl=egl-angle,angle=opengl)` |
| GL vendor/renderer/version | All `Disabled` | ANGLE / Mesa / virgl / D3D12 / NVIDIA RTX 4060 Laptop GPU |
| WebGL 1 and 2 contexts | Both null, twice | Both created, twice |
| Shader draw and pixel readback | Unavailable | Both APIs passed initial and mouse-rerun tests |
| Browser close | Window closed graphically | Graphical close, then owned PID reaped with exit code 0 |

Normal evidence: [features][n-gpu], [implementation][n-gl],
[Problems Detected][n-problems], [version/flags][n-version],
[initial null contexts][n-webgl], [mouse rerun][n-rerun].
Diagnostic evidence: [features][m-gpu], [renderer][m-gl],
[Problems Detected][m-problems], [settled version/flags][m-version],
[green triangles][m-green], [mouse-triggered blue triangles][m-blue].

The diagnostic report gives `GL_VENDOR=Google Inc. (Mesa)` and renderer
`ANGLE (Mesa, virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)), OpenGL ES 3.1 Mesa 26.2.0-devel (git-7626b94ef9))`.
Chromium's `GL_VERSION` is **OpenGL ES 3.0**, ANGLE 2.1.28222, git hash
`6dab7c7e742b`; the underlying renderer's ES 3.1 string is a separate layer.
No desktop OpenGL 4.x or full-conformance claim is made.

## Actual drawing

The [local fixture][fixture] independently creates WebGL 1 and WebGL 2 contexts,
compiles shaders, draws a triangle, and reads its center and background corner.
Its SHA-256 is `f2f708e819044238572b556e9a43428decf66306470a7311c04a31613f6f52a0`.
In normal mode, both iterations returned null contexts with `GL_VENDOR` and
`GL_RENDERER` disabled and `BindToCurrentSequence failed` ([JSON][n-results]).

In diagnostic mode, both APIs passed twice: initial green center RGBA
`[51,204,102,255]`, mouse-rerun blue center `[51,102,230,255]`, and background
`[0,0,0,255]`. Draw/readback errors were zero and `context_lost=false`.
The [JSON results][m-results] preserve timestamps, renderer, attributes,
extensions and pixels. The APIs report WebGL 1 / ES 2.0 and WebGL 2 / ES 3.0.
Readback alone is not hardware proof; here it accompanies same-run renderer,
GPU feature, process-argument and fresh virgl boot evidence. These are two
iterations in one browser session, not independent fresh-boot samples.

## Modes and provenance

The normal browser was opened by double-clicking its desktop icon. Its command
line included `--disable-gpu`, `--in-process-gpu`, `--single-process` and
`--no-zygote`, matching the [launcher's default branch](../scripts/image/wayland-chromium-launcher.c#L210).
Problems Detected reports command-line-disabled GPU access and graphics features.
This explains the browser baseline without implying virgl is absent.

After that baseline, graphical command-launcher attempts did not yield a usable
launcher; stdin serial control failed its completion-marker deadline and the
first VM was cleaned up. This was an audit-control limitation, not an opt-in
browser failure. The second authorized boot used a temporary host wrapper
changing only `-serial mon:stdio` to an owned UNIX serial socket
([before/after arguments][wrapper-args]); repository launchers were unchanged.
Actual QEMU executable, machine/CPU/memory/display/device/append options matched.

Dedicated serial commands completed with markers and fresh prompts. KWin PID 62
and Plasma PID 88 were identified ([processes][process-before]); the
[actual Plasma environment][session-env] was imported with NUL-delimited Bash
`read`/`export`, retaining its Wayland, D-Bus, Mesa/virgl and library settings.
The browser-specific diagnostic invocation was:

```sh
export WAYLAND_CHROMIUM_MULTIPROCESS=1
export CHROME_LOG_FILE=/tmp/chromium-opengl-diag.log
/bin/wayland-chromium --user-data-dir=/tmp/chromium-gl-audit 'http://10.0.2.2:41203/?run=mp'
```

Stdout/stderr went to fresh `/tmp/gl-console.log`; the separate Chrome log path
and canonical launcher log were absent before launch. The
[fresh log/process receipt][m-logs] identifies browser PID 219 and GPU PID 254.
[PID-219 arguments][m-argv] confirm the four default restrictions were absent;
the last profile argument selects `/tmp/chromium-gl-audit`. No `--use-gl`,
`--use-angle` or additional graphics-disable flags were added. Existing flags,
including `--disable-vulkan` and the launcher's sandbox policy, remained.

Both runs used the same existing kernel and 8,053,063,680-byte base filesystem,
`AUTO_BUILD=0`, KVM, six vCPUs, 8 GiB, corrected APT SDL modules and
`virtio-vga-gl`. Kernel SHA-256:
`af174a9fc9919fd9990942aff8e2b14a406a663ef6cd47d5b3a8320bb0472b21`.
This is not a fresh source-to-image reproducibility result.

- Normal **16:55:41–17:02:27 UTC**, `opengl-audit-20260907T165541Z`:
  [provenance][n-provenance], [QEMU arguments][n-qemu], [actions][n-actions], [image hashes][n-images].
- Diagnostic **17:05:28–17:13:45 UTC**, `opengl-multiprocess-20260907T170528Z`:
  [provenance][m-provenance], [QEMU arguments][m-qemu], [actions][m-actions], [image hashes][m-images].

Fresh [normal][n-boot] and [diagnostic][m-boot] boot logs record virgl capsets,
3D scanout, `/dev/dri/renderD128`, page flips and the 60 Hz presentation clock.
No inherited append-only KDE renderer line is used as fresh browser proof.

## Limits and cleanup

The diagnostic Problems Detected report lists disabled video encode and driver
workarounds, including `exit_on_context_lost`. Fresh logs contain an
`eglCreateContext` / `EGL_BAD_ATTRIBUTE` warning during Dawn OpenGL-adapter
discovery. It did not prevent these ES/WebGL successes. Desktop-GL, WebGPU,
media/audio, performance and stress behavior remain untested; GPU-report labels
for video decode/WebGPU add no functional credit for those workloads.

There are 15 normal and 14 diagnostic host PNGs. All 34 QMP guest-capture attempts
returned `no surface`; five serial snapshots intentionally omitted host PNGs.
The one-shot-fit override `QEMU_WSL_SDL_FIT_WATCH_SECONDS=0` remained. Clients
measured 1356x897 with a 1280x800 guest region; the second host placement caused
bottom clipping/black pixels and host taskbar/assistant-overlay occlusion. Cited
triangles and GPU fields remain visible, but there is no independent guest/host
pair or exact-fit validation. Mouse actions used the foreground-checked host
pointer; typing used QMP keys. Early captures can precede settled content:
use diagnostic 10/11 for triangles and 16 for the version page.

Diagnostic graphical close was followed by `wait` reporting
[`chromium_exit=0`][browser-exit]. Both owned QEMU launchers were synchronously
reaped with exit code 0, overlays removed, fixture servers stopped and exact
inventories zero. Base-image size/mtime remained unchanged
([normal cleanup][n-cleanup], [diagnostic cleanup][m-cleanup]). These were
host-controlled VM terminations, not in-guest shutdown tests.

[n-gpu]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/04-normal-gpu-features-host.png
[n-gl]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/08-normal-gl-implementation-search-host.png
[n-problems]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/09-normal-problems-host.png
[n-version]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/10-normal-version-host.png
[n-webgl]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/11-normal-webgl-initial-host.png
[n-rerun]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/12-normal-webgl-mouse-rerun-host.png
[n-results]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/webgl-results.jsonl
[n-provenance]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/provenance.json
[n-qemu]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/qemu-cmdline.json
[n-actions]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/actions.jsonl
[n-images]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/screenshots.json
[n-boot]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/run.log
[n-cleanup]: ../build-x86_64/gui-progress-audit/opengl-audit-20260907T165541Z/cleanup.json
[m-gpu]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/12-opt-in-gpu-features-host.png
[m-gl]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/13-opt-in-gl-renderer-host.png
[m-problems]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/14-opt-in-problems-host.png
[m-version]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/16-opt-in-actual-command-host.png
[m-green]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/10-opt-in-webgl-green-host.png
[m-blue]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/11-opt-in-webgl-mouse-blue-host.png
[fixture]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/webgl-fixture.html
[m-results]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/webgl-results.jsonl
[wrapper-args]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/serial-wrapper-arguments.json
[process-before]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/02-session-processes.txt
[session-env]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/04-current-kde-environment-raw.txt
[m-logs]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/17-opt-in-processes-and-fresh-log.txt
[m-argv]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/browser-cmdline.json
[m-provenance]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/provenance.json
[m-qemu]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/qemu-cmdline.json
[m-actions]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/actions.jsonl
[m-images]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/screenshots.json
[m-boot]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/run.log
[browser-exit]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/19-browser-reaped.txt
[m-cleanup]: ../build-x86_64/gui-progress-audit/opengl-multiprocess-20260907T170528Z/cleanup.json
