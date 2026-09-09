# Linux KDE Plasma VM reference

Accepted reference date: 2026-07-14 UTC

This is the same-host Linux control for xv6 GUI responsiveness work. It is a
real KVM guest running Ubuntu 24.04.4, KDE Plasma on Wayland, and virgl backed
by the host NVIDIA D3D12 adapter. The accepted visible SDL/OpenGL result is:

`build-x86_64/linux-kde-reference/run-sdl-nvidia-20260714T022225Z`

The deeper behavioral reference is:

`build-x86_64/linux-kde-reference/run-behavior-20260714T030916Z`

An additional 2026-07-18 control replayed the real windowed YouTube workload
on the original APT `/usr/bin/qemu-system-x86_64` and its unmodified packaged
SDL/OpenGL modules (`QEMU_MODULE_DIR` explicitly unset):

`build-x86_64/youtube-parity/linux-windowed-20260718T021241Z-1151893-apt-system`

It passed at 61.735 presented fps with 1280x720 media, a 9.740% VPQ drop rate,
positive Pulse/hardware/WAV audio deltas, and visibly rendered KDE/YouTube host
captures. KDE-ready and YouTube-ready host pixel statistics were respectively
mean/stddev/colors `0.104454/0.27441/29717` and
`0.7799/0.347413/35394`. This proves that the all-black xv6 result is not an
inherent failure of the original APT frontend on WSLg.

The replay also observed APT SDL resizing its client again after a guest mode
transition: the one-shot native fit reached 1280x768, then KDE-ready capture
measured 1356x897. Linux remained fully visible and fast, but this confirms
that launch-time-only window sizing is insufficient. The repository launcher
now watches the early mode-set interval and re-applies native sizing only when
the client has drifted.

A fresh 2026-07-18 replay repeated that behavior on the same untouched APT
frontend and modules:

`build-x86_64/youtube-parity/linux-windowed-20260718T045117Z-1249241-apt-system`

It passed at 62.834 presented fps with 1280x720 media, a 7.862% VPQ drop rate,
1.097489 media/wall ratio, 17 valid display samples, and positive Pulse,
hardware, and WAV audio deltas. The renderer remained direct virgl on the
NVIDIA D3D12 adapter. The client again drifted from an exact 1280x768 fit to
1356x897 after KDE's mode transition and returned to 1280x768 after a second
native fit. KDE-ready and YouTube-ready client-capture statistics were
mean/stddev/colors `0.132584/0.318188/8154` and
`0.862901/0.247212/15520`. The generated host capture bitmaps were removed
after visual inspection; the numeric, renderer, geometry, media, audio, and
cleanup receipts remain.

The completed guest was consolidated into the reusable prepared control at
`build-x86_64/linux-kde-reference/linux-kde-behavior-20260714T024146Z.qcow2`.
`qemu-img check` reports no errors. The failed bootstrap layers were removed,
leaving only the two most recent successful Linux run receipts.

## Stock-APT behavioral refresh

Three clean 2026-07-18 behavior runs used that prepared image with the system
`/usr/bin/qemu-system-x86_64`, unmodified system SDL/OpenGL modules, six vCPUs,
8 GiB, KVM, and NVIDIA-backed virgl:

- `build-x86_64/linux-kde-behavior-live/behavior-20260718T151846Z-1468713`
- `build-x86_64/linux-kde-behavior-live/behavior-20260718T152257Z-1472243`
- `build-x86_64/linux-kde-behavior-live/behavior-20260718T152921Z-1476147`

All three report `LINUX_KDE_BEHAVIOR_DONE`, a responsive KWin D-Bus
roundtrip, a successful native `wayland-info` registry roundtrip on
`wayland-0`, direct virgl/NVIDIA rendering, 1280x768@60 at scale 1, a
non-black 1280x768 guest-side Spectacle capture, zero residual Konsole or
zombie processes, clean QEMU teardown, and zero exact QEMU processes after
the run.

| Observation | Run 1 | Run 2 | Run 3 | Current aggregate |
| --- | ---: | ---: | ---: | ---: |
| First Konsole readiness | 258 ms | 249 ms | 242 ms | 249.7 ms mean |
| Warm Konsole median | 206 ms | 206 ms | 212 ms | 206 ms pooled median |
| Warm Konsole range | 188-232 ms | 197-232 ms | 188-288 ms | 188-288 ms |
| Guest idle CPU busy | 0.240% | 0.314% | 0.259% | 0.271% mean |
| Idle KWin CPU, one-core basis | 0.000% | 0.100% | 0.000% | 0.033% mean |
| glmark guest CPU busy | 5.529% | 5.480% | 9.132% traced | 5.505% untraced mean |
| glmark KWin CPU, one-core basis | 58.225% | 57.703% | 60.879% traced | 57.964% untraced mean |
| glmark build FPS | 120 | 108 | 115 traced | 114.0 untraced mean |
| glmark texture FPS | 127 | 123 | 119 traced | 125.0 untraced mean |
| glmark shading FPS | 119 | 129 | 99 traced | 124.0 untraced mean |
| glmark buffer FPS | 47 | 46 | 30 traced | 46.5 untraced mean |
| glmark ideas FPS | 87 | 85 | 64 traced | 86.0 untraced mean |
| glmark score | 99 | 97 | 84 traced | 98.0 untraced mean |

The captured first/warm Konsole main threads spent 190.195-230.352 ms in
execution but only 0.441-1.226 ms waiting on the runqueue. Across all 15 warm
launches the min/median/mean/max is 188/206/213.5/288 ms. This reinforces the
earlier conclusion that Linux's interactive latency is execution and event
dispatch, not broad scheduler starvation.

With privileged wait-channel visibility, healthy KWin normally has its main,
D-Bus, libinput, and QML threads blocked in `poll`, and worker threads blocked
in `futex`. Blocking in `poll` is therefore not itself an xv6 defect. The
actionable xv6 difference is that its known-pending initial Wayland registry
exchange does not wake and complete, while all three Linux exchanges do.

The two untraced current stock-APT glmark scores average 98, lower than the
historical behavior score of 127. Both runs began and ended with no other QEMU,
so use 98 for current APT graphics comparisons and keep 127 as historical
evidence until frontend provenance is matched. The trace-complete run scored
84 and used 9.132% aggregate guest CPU after its instrumentation interval, so
it is deliberately excluded from the uninstrumented graphics mean. This does
not lower the media target: the same stock-APT control separately sustained
61.735 and 62.834 presented fps in real 1280x720 YouTube trials.

The third retained run stages the host's ABI-matched Ubuntu `strace 6.8` only
inside its disposable overlay and records `strace_status=PASS`. It traced
10,161 calls with 1,602 errors and 0.738341 seconds of accumulated syscall
time. `poll` and `futex` account for 77.48% of that time; `wait4` contributes
another 12.21%. `statx`, `openat`, `access`, and `readlink` together account
for only 2.93%, again making event waits more material than failed path probes.
The tracer and its downstream graphics sample are kept separate from the two
untraced performance samples rather than silently pooling instrumentation
overhead into the reference.

Both runs exited successfully. The behavioral run reports
`LINUX_KDE_BEHAVIOR_DONE`, an empty QEMU stderr, and zero remaining QEMU
processes owned by its harness. Its before/after inventories are retained so
unrelated external VMs can coexist without becoming cleanup targets.

## Contract

| Item | Accepted value |
| --- | --- |
| Host | WSL2 Linux 6.18.26.1, QEMU 9.0.2, KVM |
| Guest | Ubuntu 24.04.4 LTS, kernel 6.8.0-134 |
| Desktop | Plasma 5.27.12, KWin 5.27.11, Wayland |
| Terminal | Konsole 23.08.5 |
| VM resources | 6 vCPU, 8 GiB RAM, audio disabled |
| GPU | `virtio-vga-gl`; active renderer `virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))` |
| Host presentation | `MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA`, `-display sdl,gl=on` |
| Display | active physical 1280x768 at 60 Hz, scale 1 |
| Idle sample | 10 seconds |
| Konsole sample | one first launch plus five warm launches |
| Graphics sample | glmark2-wayland 2023.01, five selected scenes, 800x600 windowed |

The physical mode is the closest mode exposed by this QEMU display to xv6's
1280x800 contract. Do not silently treat the 32-pixel height difference as an
exact resolution match.

The Konsole timer begins immediately before `konsole --separate` and ends when
the shell inside the new terminal writes a monotonic-ready marker. This is a
shell-start readiness measurement, not merely process creation.

## Accepted measurements

| Metric | Result |
| --- | ---: |
| Konsole first launch | 257 ms |
| Konsole warm launches | 204, 212, 235, 207, 297 ms |
| Konsole warm min / median / mean / max | 204 / 212 / 231.0 / 297 ms |
| Guest idle CPU busy | 0.217% across 6 vCPU |
| KWin CPU, one-core basis | 0.000% |
| plasmashell CPU, one-core basis | 0.800% |
| glmark2 build | 151 FPS |
| glmark2 texture | 143 FPS |
| glmark2 shading | 138 FPS |
| glmark2 buffer | 49 FPS |
| glmark2 ideas | 93 FPS |
| glmark2 score | 113 |
| Captured framebuffer | 1280x768 RGBA PNG |

The archived current xv6 GUI evidence records warm `konsole_wait_ms` of
1184-1205 ms. Against this Linux median of 212 ms, xv6 is approximately
5.6 times slower for the terminal-ready interaction. This is a directionally
useful latency reference; it is not a claim that every userspace step in the
two probes is identical. glmark2 is Linux-only evidence until xv6 can execute
the same benchmark and scene set, so the score must not be used as a direct
xv6 pass/fail threshold.

## Black-window diagnosis and correction

The earlier GTK/OpenGL run rendered a valid in-guest Plasma framebuffer but
the host window appeared black. Its QEMU stderr was explicit:

```text
libEGL warning: egl: failed to create dri2 screen
qemu: GtkGLArea console lacks DMABUF support.
```

That run is rejected as the visible reference. Switching only the host
presentation frontend to SDL/OpenGL removed both errors. Pinning
`MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA` prevented SDL from silently choosing
the Intel adapter. The accepted QEMU stderr is empty, the guest renderer is
virgl/NVIDIA, and Spectacle independently captured a non-black 1280x768 Plasma
desktop. The rejected GTK glmark2 score of 333 is not pooled with the accepted
SDL result because the presentation paths are materially different.

KScreen occasionally aborts with allocator corruption after printing a valid
mode (`verify_rc=134`). The harness therefore records that return code and
accepts the mode only when the printed state independently contains the active
1280x768@60 marker. The accepted screenshot-size check is a separate hard
gate.

## Behavioral observation

The behavioral probe repeated the renderer and physical-mode gates, then
measured idle behavior, process readiness, scheduling, loader and file
activity, graphics load, memory pressure, and teardown. It used the same 6
vCPU, 8 GiB, KVM, virgl/NVIDIA, SDL/OpenGL, Wayland contract as the visible
reference. The accepted screenshot is a normal, non-black Plasma desktop at
1280x768.

| Observation | Accepted result |
| --- | ---: |
| Konsole first readiness | 231 ms |
| Konsole warm readiness samples | 232, 213, 210, 216, 226 ms |
| Konsole warm min / median / mean / max | 210 / 216 / 219.4 / 232 ms |
| First Konsole main-thread runtime / runqueue wait | 221.400 / 0.522 ms |
| Warm Konsole main-thread runtime / runqueue wait | 217.960 / 2.231 ms |
| Warm Konsole storage reads | 0 bytes |
| Guest idle CPU busy | 0.185% across 6 vCPU |
| Idle KWin / plasmashell CPU | 0.000% / 0.600% of one core |
| glmark2 interval | 50.450 s |
| glmark2 guest CPU busy | 5.603% across 6 vCPU |
| glmark2 KWin / plasmashell CPU | 55.818% / 1.328% of one core |
| glmark2 build / texture / shading / buffer / ideas | 156 / 177 / 155 / 52 / 103 FPS |
| glmark2 score | 127 |
| Konsole / zombies after teardown | 0 / 0 |

The six-launch observation interval recorded 20,212 context switches, 281
process creations, 126,847 total faults, and only 34 major faults. That
interval includes the probe's hold and inter-sample gaps, so these totals are
not per-launch costs. CPU pressure never reached `full`, and memory pressure
did not increase. During glmark2 the guest stayed approximately 94% aggregate
idle with no swap or sustained block-I/O activity, even while KWin consumed
about 56% of one core. Linux therefore behaves as an event-driven,
compositor-thread workload rather than a globally CPU- or memory-saturated
guest.

The first and warm Konsole main threads accumulated only 0.522 and 2.231 ms
of runqueue wait. Their readiness time was instead almost entirely execution
time. This is the important scheduler reference: a broad runnable-process
scheduler rewrite is not justified by Linux behavior. The xv6-specific
58-139 ms compositor damage-to-repaint gaps need stage-level localization in
the repaint/timer/present path.

The corrected syscall trace contains 14,065 calls and 2,151 errors. `poll`
and `futex` account for 85.39% of accumulated traced syscall time, consistent
with blocking event loops. `openat`, `statx`, `access`, and `readlink`
together account for about 2.70%, despite 1,217 `ENOENT` file-trace results.
The dynamic loader performs 68,206 final relocations; its initial relocation
phase is 71.1% of 6,451,692 startup cycles. Linux handles both large
relocation volume and failed path probes within the 216 ms warm result, so
raw lookup or relocation counts do not make VFS or loader work the leading
xv6 optimization. The trace's 1.047 s total is accumulated across traced
threads and a held workload, not launch wall time. Six
`/opt/linux-kde-behavior/lib` records belong to the environment-clearing
wrapper and are the known instrumentation floor.

These observations establish the comparison order for xv6:

1. Measure client commit -> damage -> repaint arm -> compositor runnable ->
   compositor run -> GPU submit -> host retire -> frame callback.
2. Reduce multi-vblank compositor/repaint and host-retire delays without
   introducing busy polling.
3. Revisit file faults, VFS, loader, or the generic scheduler only if a new
   same-session profile assigns material wall time to that stage.

The guest-side probe is `scripts/gpu/linux-kde-behavior-probe.sh`. It refuses
to run unless the guest hostname is exactly `linux-kde-reference`.

## Evidence hashes

```text
ffe6203da54deeb6db5d2a98a83f9ec8e55f149d3f7ba622e1abe5fa966ee3d6  noble-server-cloudimg-amd64.img
79e8cffb540dd4c98da318bfef37e72b9aaf6629b65ca4bfcb2f11afbd83f1c3  contract.txt
d67b905134b5016dbc92c5deb8a16d7b7e7e678824a8ad189f3669f47950c593  display-mode-post-settle.txt
68aeb88c7f661e222f5dfd9b71856ecff17e33d674ec29ef22edfe5089334e97  idle-cpu.txt
3fdb5343699074ac5187c670b912d76896afcf3b984bad448c1dac8688aa8658  konsole-launch.tsv
2f16568b760ff58b9709ce2770610979d0a150fb2db3bb21a4cd4b27ff52873b  konsole-summary.txt
024fdfbe2e1a58c3d4da5141efba3b35d0a2690d3873a1b9c9b69c32a27fbdef  glmark2.txt
e8263f8993d34fbc5293ebdb2b4a6b2218ed11081e3862d7dea85f0e7aae519f  plasma-reference.png
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  qemu.stderr.log
37e63a774cba62e5485ce23e9d7f5e25dd7f1d827903260ea0e1bdcfa0fd9856  renderer.txt
548608806c2cdb55973f2537fa46e0da25316f57af829348930f45d4ac175da2  system.txt
fb022266277d7d6314b73064238b5d8ad405ce82a06171d14faba777d0e11123  behavior/STATUS
486a1bee1f30d55ffcd76b550d3c6d4e78a70e7bd8dfd369d7b5a2581f68690a  behavior/contract.txt
b41bbb30056ea58b10b0a67f00ab32faeafd17ef6c4aa7b57e6f62ec004d2998  behavior/idle-summary.txt
b4d7b6b9063fdb3d6f5c5b760510191d45966409fe0bb45cba0ab52c3215b3d9  behavior/konsole-summary.txt
41a7db578cc6c4c1bb0b43577af4b7a30bd7060a34f40b9e82c546830934c9c0  behavior/konsole-strace-summary.txt
ad07df7af257c58a0d2d3e23a7d0c9b6a61fc9853eaf304b1376b9d03f7d0a2e  behavior/konsole-lddebug.txt
a641c578512be29171599ea6571fcab13c8e8e709ead4da37978299739dd18c1  behavior/glmark-load-summary.txt
65788c11de2cb72b06fe3c2fb80406601a4f9a8c98c9d882cc4538b9736bb495  behavior/teardown-summary.txt
b8f46ba3fe8a787a5f019b605a5dcc65b51663a713de34178ef8f4c24bb042f5  behavior/plasma-behavior.png
6f7d0205fcdb616e545a03d7ca2ed543e25838f73bca004e064f4ee43087ff5a  scripts/gpu/linux-kde-behavior-probe.sh
```

The cloud image came from Ubuntu's Noble amd64 cloud-image channel. The guest
uses Ubuntu's `glmark2-wayland` and `kde-spectacle` packages. Large VM and run
artifacts remain intentionally ignored under `build-x86_64`; this document is
the durable reference receipt, not a request to commit disk images.
