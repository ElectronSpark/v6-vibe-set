# Linux KDE Plasma VM reference

Accepted reference date: 2026-07-14 UTC

This is the same-host Linux control for xv6 GUI responsiveness work. It is a
real KVM guest running Ubuntu 24.04.4, KDE Plasma on Wayland, and virgl backed
by the host NVIDIA D3D12 adapter. The accepted visible SDL/OpenGL result is:

`build-x86_64/linux-kde-reference/run-sdl-nvidia-20260714T022225Z`

The deeper behavioral reference is:

`build-x86_64/linux-kde-reference/run-behavior-20260714T030916Z`

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
