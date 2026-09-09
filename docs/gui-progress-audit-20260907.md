# GUI progress audit — 2026-09-07

The later [mouse exploration audit](gui-mouse-audit-20260907.md) expands this
coverage with 74 mouse actions and records a reproducible Chromium New Tab exit.

The existing xv6 image booted into an interactive KDE desktop with virgl enabled.
Across one approximately 15-minute run, graphical menu/window operations,
Chromium HTTP/HTTPS browsing and input, and KWrite editing and same-session
save/reopen worked. Konsole did not produce a usable shell. Chromium's default
launcher selected software rendering, and Dolphin required an explicit
application choice to reopen a text file. This is a partial functional pass;
it does not establish current media performance or close the desktop plan.

The single active follow-up queue remains the
[consolidated work plan](active-work-plan.md). This document records observations,
evidence and their limits.

## Run and provenance

- UTC interval: **2026-09-07 15:19:50–15:34:43**; one VM, one session.
- Entry point: `scripts/launch/launch-gui.sh`, owned through the repository's
  QEMU runner. Token: `graphical-audit-20260907T151950Z`.
- Existing build artifacts, with `AUTO_BUILD=0`; no fresh build or source-to-image
  reproducibility claim. Kernel SHA-256:
  `af174a9fc9919fd9990942aff8e2b14a406a663ef6cd47d5b3a8320bb0472b21`.
  Base filesystem: `build-x86_64/fs.img`, 8,053,063,680 bytes;
  original size and modification time remained unchanged.
- KVM, host CPU, six vCPUs, 8 GiB RAM; stock APT QEMU 9.0.2 with the
  repository's corrected SDL/OpenGL modules. SDL GL frontend,
  `virtio-vga-gl`, 1280x800 guest mode, WSL D3D12 host GL route.
- Existing async presentation policy retained: submit depth 3, five-second
  stall watchdog and phase-stable 60 Hz presentation clock.
- A private qcow2 overlay isolated guest writes. The local browser fixture
  was served only during this run at `http://10.0.2.2:36423/`.
- Audit-specific display override: `QEMU_WSL_SDL_FIT_WATCH_SECONDS=0` used
  one-shot fitting. The host capture client measured 1356x897, containing
  the 1280x800 guest image at offset (38,59). This run does not validate the
  default continuing fit watcher or the exact host-client-size gate.

Full [launch provenance][provenance], [QEMU arguments][cmdline] and timestamped
[action log][actions] are retained with the receipt.

## Graphical observations

Application operations used the visible QEMU window, including foreground-checked
Windows mouse clicks and QEMU virtual keyboard/mouse events. No guest serial
commands were used for the application audit. Screenshots are host window
captures; keyboard success establishes guest input handling, not a physical
host-keyboard test.

| Operation | Observation | Evidence |
| --- | --- | --- |
| Boot and desktop | KDE wallpaper, panel and application icons appeared after startup. | [Settled desktop][desktop] |
| Launcher | Kickoff opened early and again near the end of the session. | [Initial menu][menu], [final menu][final-menu] |
| Window management | Konsole moved, resized and closed through graphical controls. | [Moved][moved], [resized][resized], [closed][terminal-closed] |
| Konsole shell | Window stayed blank except for its cursor; attempted `echo GUI_AUDIT_OK; uname -a` produced no visible echo or result. Focus attempts did not produce a prompt. | [Input attempt][terminal-input] |
| Chromium local page | Desktop icon launched Chrome for Testing 151.0.7922.34. Local HTML rendered, typed text appeared in the field and JavaScript echo, and a mouse click changed the counter from 0 to 1. | [Typed text][browser-input], [counter 1][browser-click], [HTTP requests][http] |
| Chromium HTTPS | Navigating to `https://example.com` displayed the Example Domain page without a visible certificate interstitial. | [HTTPS page][https] |
| Browser GPU status | `chrome://gpu` reported software-only canvas/compositing/rasterization/video and disabled OpenGL/WebGL. `chrome://version` showed `--disable-gpu --in-process-gpu --single-process --no-zygote`. | [GPU status][browser-gpu], [launch flags][browser-flags] |
| Native text input | KWrite accepted two lines: `Native KDE text input works.` and `Virgl GUI audit - 2026-09-07.` | [Editor contents][editor] |
| Save and reopen | Saved `/root/graphical-audit-20260907.txt`; Dolphin listed the 59-byte file. Opening it prompted for an application; choosing `kwrite` reopened the same text. This proves same-session behavior only. | [Saved document][saved], [file listing][files], [chooser][chooser], [reopened text][reopened] |
| End-of-session desktop | Closing applications restored the desktop, and Kickoff remained usable. No accumulated trails were apparent in the final captures. | [Final desktop][final-desktop], [final menu][final-menu] |

The first capture was a black startup surface; the subsequent settled desktop
and application captures provide the functional evidence. The blank rectangle
in the Konsole captures is its terminal content, not a whole-desktop blackout.

## Virgl evidence and boundaries

The fresh [boot log][runlog] records both virgl capsets ready, the virgl render
node at `/dev/dri/renderD128`, a 1280x800 3D scanout resource, asynchronous
page-flip presentation and activation of the 60 Hz presentation clock. The
actual QEMU arguments select `virtio-vga-gl` and `sdl,gl=on`. These support the
requested virgl-enabled VM and active graphics presentation path.

A graphical inspection of `/kde-session-plasma-child.log` showed
`GL_RENDERER: virgl (D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU))`
([capture][renderer]). However,
[session logging opens this file with `O_APPEND`](../scripts/image/kde-session.c#L738).
The viewed entry was not isolated from inherited base-image content, so it is
supporting historical evidence, **not a proven fresh renderer-string receipt**.

All 30 QMP `screendump` attempts returned `no surface` on this GL route.
The 30 successful host PNG captures and their hashes are recorded in
[the screenshot manifest][screenshots]. There is no independent guest-scanout
image pair for the visual contract. Neither the local page's CSS animation
nor the boot clock message is a measured frame-rate result.

## Findings and source ownership

1. **Default Chromium remains a software browser workload.** The observed
   flags match the default branch in
   [the Wayland Chromium launcher](../scripts/image/wayland-chromium-launcher.c#L127)
   and its [argument construction](../scripts/image/wayland-chromium-launcher.c#L211).
   Multiprocess mode requires `WAYLAND_CHROMIUM_MULTIPROCESS=1`; the ordinary
   desktop launch supplies no override. This is a source-policy explanation,
   not evidence of a virgl device failure. Track accelerated/plain launch
   coverage under **GUI-01**, separately from this working basic browser path.
2. **Konsole shell readiness is an open functional failure.** Its
   [configured profile](../scripts/image/kde-session.c#L415) launches `/bin/bash`.
   The successful browser and KWrite input narrow the symptom beyond a global
   keyboard failure. The existing
   [Konsole shell/PTY wrapper](../scripts/image/kde-konsole-shell-wrapper.c#L92)
   is the next diagnostic owner; child startup, terminal ownership and shell
   readiness were not instrumented in this run. No kernel cause is established.
3. **Text-file application association is incomplete.**
   [MIME cache staging](../scripts/image/stage-kde-runtime.sh#L455) and
   [session defaults](../scripts/image/kde-session.c#L361) establish directory
   and file-URL associations but no `text/plain` default. This is a configuration
   lead for Dolphin's chooser; successful explicit KWrite reopening supplies
   no evidence of a file-content failure. Track this and Konsole under **DESK-08**.
4. **The full visual evidence gate remains open.** Repeat with default fitting,
   independent guest/host capture and a fresh per-run renderer log segment
   before crediting the stronger display contract in **DESK-04**.

No kernel panic, fatal page-fault/general-protection-fault, quarantine or async
stall-timeout markers matched the retained boot/debug logs
([scan receipt][error-scan]). DHCP logged a two-second timeout and fallback;
the later HTTP and HTTPS operations worked. The absence of matching log markers
is limited to this run and does not close the historical stall or startup bugs.

## Untested work and cleanup

No YouTube, audio playback, hardware video decode, matched Linux control,
N>=2 performance samples, reboot persistence or long-duration stress was run.
`audio.wav` contains only its 44-byte header and earns no audio credit.
Existing performance and media work remains open.

After the graphical audit, QMP `quit` ended the owned VM. The launcher was
synchronously reaped with exit code 0; exact QEMU process checks returned zero,
the private overlay was removed, and the local fixture server stopped.
This was host-controlled termination, not a tested in-guest shutdown flow.
See [cleanup receipt][cleanup]. No runtime implementation was changed for
this audit.

[provenance]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/provenance.json
[cmdline]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/qemu-cmdline.json
[actions]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/actions.jsonl
[runlog]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/run.log
[http]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/http.log
[screenshots]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/screenshots.json
[error-scan]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/error-scan.txt
[cleanup]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/cleanup.json
[desktop]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/02-boot-settled-host.png
[menu]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/04-kickoff-open-host.png
[moved]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/09-konsole-moved-host.png
[resized]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/10-konsole-resized-host.png
[terminal-closed]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/11-konsole-close-host.png
[terminal-input]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/06-terminal-input-host.png
[browser-input]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/14-browser-keyboard-host.png
[browser-click]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/15-browser-click-host.png
[https]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/20-browser-https-host.png
[browser-gpu]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/16-chromium-gpu-host.png
[browser-flags]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/17-browser-launch-flags-host.png
[renderer]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/19-compositor-renderer-search-host.png
[editor]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/23-kwrite-input-host.png
[saved]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/25-file-saved-host.png
[files]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/26-dolphin-open-host.png
[chooser]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/27-file-reopened-host.png
[reopened]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/28-file-open-with-kwrite-host.png
[final-desktop]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/30-final-desktop-host.png
[final-menu]: ../build-x86_64/gui-progress-audit/graphical-audit-20260907T151950Z/31-final-kickoff-host.png
