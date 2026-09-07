# Chromium YouTube playback and watch-page audit — 2026-09-07

YouTube loaded the public Big Buck Bunny watch page, but playback did not advance
from **0:00 / 10:34**. The player displayed **“Audio renderer error. Please restart
your computer.”** The error returned after a keyboard reload. The captured audio
payload was entirely silent. Several watch-page controls worked through the QMP
virtual mouse, including mute, settings, playback speed, fullscreen and comment
sorting. This is a functional audit of the existing image, not a playback pass.

## Mode and provenance

- Receipt: [youtube-audit-20260907T173059Z][receipt], host UTC
  17:30:59–17:50:37; browser launched at 17:32:09. All action timestamps are host
  receipts; guest clocks and screenshots may differ slightly.
- Existing `build-x86_64/fs.img`, kernel SHA-256
  `af174a9fc9919fd9990942aff8e2b14a406a663ef6cd47d5b3a8320bb0472b21`;
  `AUTO_BUILD=0`, KVM, 6 CPUs, 8G, SDL OpenGL, corrected APT SDL modules,
  `virtio-vga-gl` at 1280×800, virtio tablet. No runtime source/image rebuild or fix.
  [Provenance][provenance] and [actual QEMU arguments][argv] preserve the configuration.
- This continuation used the existing opt-in `WAYLAND_CHROMIUM_MULTIPROCESS=1`
  mode established in the [OpenGL audit](chromium-opengl-audit-20260907.md), not the
  ordinary desktop launcher's GPU-disabled default. The serial shell imported the
  actual Plasma PID 88 environment before launching `/bin/wayland-chromium` with
  `--user-data-dir=/tmp/chromium-yt-audit` and the plain public watch URL.
  No parity extension, forced playback automation or `--use-gl`/`--use-angle` override
  was added. [Session environment][env] and [actual browser argv][browser] are retained.
- The URL was `https://www.youtube.com/watch?v=aqz-KE-bpKQ`; the page identified
  **Big Buck Bunny 60fps 4K - Official Blender Foundation Short Film**, by Blender.
  The title is content metadata, not evidence of the tested quality or frame rate.
- The temporary host wrapper changed only the serial transport to an owned Unix
  socket. [Wrapper arguments][wrapper] preserve before/after argv. Existing graphics
  and guest policy remained in place. `QEMU_WSL_SDL_FIT_WATCH_SECONDS=0` and one
  explicit host-window clamp were observational harness overrides.
- Host captures are 1356×897, with the guest region at approximately (38,59),
  1280×800. The clamped host origin was (564,123). A host assistant overlay is
  visible at the right; it is not guest UI. QMP screendump returned `no surface`,
  so the evidence uses actual SDL host-window captures.

## Graphical observations

The [action ledger][actions] contains 12 host mouse operations and 14 QMP virtual
mouse clicks, with 29 host PNG captures in the [hashed screenshot inventory][shots].
Keyboard Ctrl-R and URL entry used QMP keys. Screenshot labels describe intended
actions; the observations below determine which actions actually succeeded.

| Interaction | Observed result and evidence |
| --- | --- |
| Initial page and play attempt | [Poster][initial] at 0:00, followed by the [audio renderer error][error] after the host play attempt. No changing video frames or advancing playback time were established. |
| Reload/retry | Host reload click had no clear visible response. QMP Ctrl-R [restored the poster][reload]; the [same error returned][mute], still at 0:00. One page reload was used; no alternate video or service/flag workaround was applied. |
| Host mouse input boundary | Wheel scrolling and a generic Chromium context menu visibly responded. Host left-click attempts at settings, mute, description, comment sorting and reload did not establish the intended change. A title-bar double-click did not maximize the browser. These are input-path limitations, not established YouTube widget failures. |
| Mute | QMP virtual click changed the icon to a crossed speaker, exposed the volume slider and showed **Unmute M** in [capture 19][mute]. This establishes the muted UI state, not audible output or a successful muted playback retry. |
| Settings and quality | QMP clicks opened [Settings][settings] and the [quality list][quality]: 1080p60, 720p60, 480p and 360p were visible. After an intended 720p60 selection, the reopened [menu reported 360p][quality-after], so 720p60 selection was not confirmed. |
| Playback speed | The speed menu changed from [1.00×][speed-before] to [1.25×][speed-after] after a virtual mouse click. Actual playback at that speed remains untested. |
| Fullscreen | Virtual mouse clicks [entered player fullscreen][fullscreen] with the YouTube exit toast, then [returned to the watch page][fullscreen-exit]. The same error persisted throughout. |
| Description, comments and sidebar | Host wheel [revealed the description, 5,170 Comments and recommendations][scroll]. Description expansion was attempted through the uncertain host left-click path and was not established. |
| Comment sorting | A QMP click opened [Top / Newest][sort]; selecting Newest closed the menu and [changed the first visible comment][newest] from an older entry to one marked 3 days ago. No comment was posted. |
| Stats for nerds | QMP right-click opened the YouTube menu; selecting Stats produced [capture 29][stats]. The earlier host right-click had opened Chromium's generic page menu instead. |
| Remaining controls | Successful pause/resume, seeking, volume-level adjustment and theater mode were not established. No account, like, subscription, sharing or posting action was performed. |

At host 17:47:08, Stats for nerds reported viewport `640x360 / -`, current/optimal
resolution `0x0@30 / 1280x720@60`, codecs `avc1.4d401e (134) / opus (251)`,
network activity `0 KB`, buffer health `0.00 s` and media time `t:0.00`.
The frame field was `-`. These selected/reported representations do not prove
successful H.264/Opus decoding, AV1 support, 720p60 playback or presented frames.
The initial error and later post-reload captures both remained at 0:00.

## Graphics, audio and diagnostic boundaries

[Same-run chrome://gpu][gpu] reported Chrome 151.0.7922.34, OpenGL **Enabled**,
and hardware-accelerated Canvas, Compositing, Rasterization, WebGL and Video Decode.
These feature labels do not establish actual hardware video decoding in this failed
playback. The fresh [serial boot log][serial] records virgl capsets 1/2 ready,
the 3D scanout and `/dev/dri/renderD128` registration (lines 105–116).
Browser PID 214 and GPU process PID 249 were present in the [same-run process receipt][roles].
This audit did not repeat a WebGL draw or desktop OpenGL conformance test.

QEMU's actual audio backend was `wav` with `virtio-sound-pci,streams=1`.
The [WAV analysis][audio-analysis] found 4,459,716 file bytes, 4,459,672 PCM payload
bytes, stereo signed 16-bit samples at 44.1 kHz: 1,114,918 frames, or 25.2816 seconds.
**Every payload byte was zero.** There is no verified audible content. The WAV's
sample duration is not a timestamp mapping to a particular player action or the
full VM runtime. [audio.wav][audio] and its hash are retained.

The imported session used `PULSE_SERVER=unix:/dev/shm/xdg-runtime-root/pulse/native`
and its current Pulse cookie, while actual Chromium argv included
`--alsa-output-device=default`. Fresh `/kde-audio-status.log` reported PipeWire/Pulse
socket presence PASS and protocol probing SKIPPED. Socket presence and running
PipeWire processes do not prove a usable sink or successful stream start.
The [bounded serial readback][browser] returned no browser stdout content;
the requested fresh `CHROME_LOG_FILE=/tmp/youtube-audit.log` was absent and the
canonical launcher-log read returned no content in [receipt 18][roles].
The canonical log had been absent before launch. These limits do not identify
the failing audio layer or prove that Opus itself is unsupported.

## Cleanup and scope

The browser was closed graphically and synchronously waited: [chromium_exit=0][reap]
with a fresh serial marker and prompt. The owned controller exited 0 at 17:50:37;
[cleanup][cleanup] reports launcher exit 0, no remaining QEMU, removed private
overlay, and unchanged base image size/mtime. The worker and conductor independently
confirmed exact QEMU counts of zero after reap.

This run establishes watch-page loading and the specific working controls above.
It leaves actual YouTube playback, audible output, decode/performance parity and
host left-button delivery unresolved. No runtime fixes were attempted. The retained
controller also contains an unused local WebGL fixture/server inherited from the
prior harness; it was not exercised and contributes no result to this audit.

[receipt]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/
[provenance]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/provenance.json
[argv]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/qemu-cmdline.json
[env]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/03-current-session-environment.txt
[browser]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/17-fresh-browser-audio-evidence.txt
[wrapper]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/serial-wrapper-arguments.json
[actions]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/actions.jsonl
[shots]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/screenshots.json
[initial]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/05-youtube-initial-host.png
[error]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/06-play-request-host.png
[reload]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/16-keyboard-reload-fallback-host.png
[mute]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/19-qmp-mute-diagnostic-host.png
[settings]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/20-qmp-player-settings-host.png
[quality]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/21-quality-options-host.png
[quality-after]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/23-settings-after-quality-host.png
[speed-before]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/24-playback-speed-options-host.png
[speed-after]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/25-speed-125-selected-host.png
[fullscreen]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/26-fullscreen-host.png
[fullscreen-exit]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/27-exit-fullscreen-host.png
[scroll]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/11-description-comments-scroll-host.png
[sort]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/31-qmp-comment-sort-host.png
[newest]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/32-comment-newest-host.png
[stats]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/29-stats-for-nerds-host.png
[gpu]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/33-current-gpu-features-host.png
[serial]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/serial.log
[roles]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/18-browser-log-and-roles.txt
[audio-analysis]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/audio-analysis.json
[audio]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/audio.wav
[reap]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/35-browser-reap.txt
[cleanup]: ../build-x86_64/gui-progress-audit/youtube-audit-20260907T173059Z/cleanup.json
