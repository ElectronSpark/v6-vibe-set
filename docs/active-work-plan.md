# Active xv6 Work Plan

Last updated: 2026-07-11. This is the sole live plan. Superseded V2/V3
self-failure chronology and disposable artifacts remain in git history.

## Objective and acceptance

Close YouTube 720p60 to the comparable Linux VM on this KVM+virgl host in two
separate, mandatory modes:

1. **Windowed watch page:** N>=2 valid real KVM+virgl runs, first >=52
   presented fps, then about 55-60 with low drops and stable pacing.
2. **Actual player/document fullscreen:** a separate nonce-bound proof of real
   player or document fullscreen, settled active 1280x720 playback, then N>=2
   valid runs with the same thresholds.

A maximized window is not fullscreen. Windowed and fullscreen samples never
pool, and either mode missing its own evidence leaves the overall goal open.
Only `yt-presentfps` plus `PERF-VIDEO` on real KVM+virgl GL count; software
fallback, llvmpipe, N=1 timing wins, and diagnostic-only output do not.

## Binding host and VM discipline

- WSL crashed from overlapping recursive raw-image `rg --text` scans (about
  24 GiB and 6.7 GiB RSS), not QEMU. Repository searches use only
  `/home/es/.local/bin/rg`, source trees only: never recursive images/artifacts
  or `-a`, `-u`, `--no-ignore`, or `--binary`. A necessary artifact operation
  names each regular file and uses the bounded artifact wrapper or `debugfs`.
- Every build, VM, and serial command is synchronously reaped. Serial silence
  is not completion: use short marker commands, allow the known first
  bracketed-paste character drop, wait for the owned command, then reacquire a
  prompt. Never use `pgrep`.
- The conductor authorizes at most one VM worker. Before a VM authorization,
  require no active VM worker and an exact `/proc/*/exe` count of zero for
  `qemu-system-*`/`qemu-kvm`; repeat after owned process-group cleanup and
  synchronous reap. Never edit QEMU/launcher scripts to enforce this. Guest
  RAM is not capped; concurrent VMs are.
- Every authorization is named-branch specific: compare HEAD with
  `origin/<current-branch>`, never `@{upstream}`. A consumed gate needs a new
  gate. Preserve unrelated dirt and never touch an external QEMU.

## Current authority and diagnostic state

There is **no current VM authority**. The only prior authorized V3 diag boot,
`chromium-youtube-m7/20260711T150252Z-pid1861734-mp1-audio1-media1-hd7201-capturediag1`,
was a real KVM+virgl boot but N=0 INVALID/NULL: virgl/OpenGL-submit and media
assets were valid, yet its terminal was INCOMPLETE `diagnostic-no-marker-rows`
and the outer validator returned code 8 on diagnostic-artifact disagreement.
It grants no timer/rVFC, semantic, or FPS credit. Its QEMU group was reaped.

The contemporaneous bounded artifact forensic located the observed absence
before serial parsing: the capture file was zero bytes and serial had only
BEGIN/END plus RC/FENCE. The helper's raw status 0 proves neither source-log
presence nor a marker match. Treat that run as invalid evidence, not a kernel,
codec, or presentation conclusion.

**Consumed V3 diag1 gate — INVALID/NULL (2026-07-11):** token
`V3-DIAG1-5d8521a-20260711T172315Z-Q0` was consumed by exactly one fresh run,
`20260711T172454Z-pid2002542-mp1-audio1-media1-hd7201-capturediag1`.
Prelaunch branch/origin and exact-QEMU gates passed; image preflight passed all
three current asset hashes. The owned run proved KVM launch and WSL D3D12 virgl
host selection, and the guest reported virgl capsets/render node, but it
returned driver code 8 before diagnostic launch send:
`INVALID command-launch-status-DIAG_DEADLINE_PRE_SEND`. All three terminal
receipts are the retained nonregular/zero empty state; fbstat is absent and
there are no producer, post-video, collect, timer, rVFC, FPS, audio, fullscreen,
or semantic facts. Serial and summary terminal tuples match, raw witness is
zero-byte, and `media_probe_image_assets=PASS` records the three exact hashes.
The owned QEMU group was synchronously reaped (`waited:2002846 exp4 0 0`) and
the final exact QEMU count was zero. Preserve named evidence in
`/tmp/xv6-v3-diag1.dvKyhE`, log `/tmp/xv6-v3-diag1-log.wzCwlG`, and retained
scratch image `/tmp/xv6-yt-20260711T172454Z-pid2002542-mp1-audio1-media1-hd7201-capturediag1.fs.img`.
This is INVALID/NULL and consumes the gate; no retry or VM authority exists.

### V3 source protocol: host-only review PASS

V3 demotes `producer_start` to liveness. Its sole admitting fact is the
nonce-bound `post_video_hd720_ready`, emitted only after `waitForVideo` and the
full selector proof: selected `hd720`, 1280x720 dimensions, stable observation,
selector prerequisites/range/quality calls, and observed progress. Missing or
failed selector evidence never admits collection.

Receipts are phase- and nonce-bound (`producer_start`, `post_video_hd720`,
`collect`), with source status, bounded bytes/digest, grep status, rows/bytes,
and ready digest. A readable raw-status-0/no-match before post-video readiness
is INCOMPLETE `producer-not-ready`; after authenticated readiness it is
INCOMPLETE `ready-but-no-markers`. Missing, unreadable, nonregular, malformed,
phase/digest-mismatched, bad-capture, or nonzero-command evidence is INVALID.

The exact sequential envelope is 210000 ms:
35000 launch + 70000 readiness + 35000 before-fbstat + 30000 collect + 35000
after-fbstat + one 5000 ms global reserve. Endpoints are 35/105/140/170/205 s.
Admission checks both stage endpoint and outer budget with the reserve exactly
once; the old 165 s maximum path is rejected.

The terminal is fixed-order and diagnostic-only: all three receipts, ready and
capture digests, fbstat shapes, tuple digest, serial/summary parity, and raw
witness must reparse. Nonce, phase, marker, payload, tuple, summary, or digest
drift has only `terminal-reparse-drift`. Diag0 preserves the exact OFF route
and cannot claim FPS or semantic success.

The bounded `YT_STATIC_CHECK=1` replay passed all V3 groups, and an independent
bounded replay/review re-ran source-order/HD720, receipt/raw0,
freeze/reorder/duplicate, terminal/raw drift, 165/210-edge, and poisoned-diag0
checks. Both explicitly report `js_guest_runtime=UNEXECUTED`. This is a
host-only source checkpoint: it is not guest-JS, image, diagnostic, semantic,
FPS, or VM credit.

### Base-image freshness: independent receipt review PASS

At source checkpoint `fa0547a`, the authorized target-only
`cmake --build build-x86_64 --target rootfs-refresh -j2` completed and rewrote
`build-x86_64/fs.img`; it did not build user or ports targets or run a VM.
Host-GUI staging ran for Xwayland, xkbcomp, and XKB data. The log retains
existing compiler-format, GLib schema deprecation, and font-cache warnings,
but ended with `make-rootfs: wrote ... fs.img` and no target failure.

The prior named-file receipt recorded that the overlay, refreshed base, and a
fresh disposable clone had exactly `manifest.json`, `probe-lib.js`, and
`probe.js` byte-for-byte; the independent review below rechecks overlay/base
directly. It recorded the clone as removed:

| asset | reported bytes / SHA-256 in overlay, base, and clone | status |
| --- | --- | --- |
| `manifest.json` | 813 / `46dced0c2886a66f93b04da8646d427a3b0c1563a7de7fb052bd6af56d63ac22` | match |
| `probe-lib.js` | 4336 / `2fde2692bb276d62b5a3e07a32e9bdf0e6ac70be0b3217bfbc8d8dd912ce137c` | match |
| `probe.js` | 26585 / `de6fa5dcc51dfd672a131f80bfc598c0dd98c532def3a164268f8b1f5af48fe8` | match |

Scratch-only injection remains prohibited. This closes freshness measurement,
not VM, diagnostic, semantic, or FPS credit. The generated base image remains
an authorized refresh artifact, not a staged source change.

**Independent asset-receipt review — PASS (2026-07-11):** at
`fa0547ac970eb32f31a9577ba415d2dda9aa585a`, equal to both live HEAD and
`origin/codex/host-linux-abi-shell-port-ff`, a fresh named-file-only receipt
found exactly those three regular files in both the overlay directory and
`/share/chromium-youtube-media-probe` in `build-x86_64/fs.img`; direct
`debugfs` extraction reproduced every listed byte count and SHA-256, including
`probe.js` 26585 / `de6fa5dcc51dfd672a131f80bfc598c0dd98c532def3a164268f8b1f5af48fe8`.
The bounded refresh log records only the target rootfs refresh and its final
`make-rootfs: wrote ... fs.img`. The disposable-clone pathname is not retained
in the named log or current plan, so it was not enumerated; this review makes
no broader `/tmp` absence claim. No VM gate or diagnostic ran.

## Immediate authority chain

1. **Source checkpoint — PASS:** reviewed V3 diagnostic sources and the compact
   plan are checkpointed at `fa0547a`; that is host-only source credit.
2. **Narrow rootfs refresh — PASS:** base extension assets were refreshed with
   no user/ports target, scratch substitution, or VM.
3. **Host/base/fresh-clone receipt — independent review PASS:** all three
   named assets match exactly. The historical disposable clone was not
   enumerated without a retained pathname; base freshness is independently
   accepted, while no broader `/tmp` absence claim is made.
4. **NEXT — NO-BOOT pre-send-deadline forensic and review:** localize the
   owned `DIAG_DEADLINE_PRE_SEND` path using retained named artifacts and
   source/reducer evidence only. No retry, new VM gate, performance, audio, or
   fullscreen work is authorized until that invalid terminal is understood and
   independently reviewed.
5. **A1 windowed:** only after valid diagnostic success/review and a new gate,
   run N>=2 forced-hd720 trials; clear >=52 before pursuing about 55-60.
6. **Actual fullscreen:** first prove real fullscreen and settled active HD720;
   then run distinct N>=2 trials. Never pool with windowed.
7. **A2/A4 localization and repair:** only after windowed A1 N>=2; then
   validate audio-on in both windowed and actual fullscreen modes.
8. **Residual present work and default batteries:** compare each accepted mode
   with matching Linux-VM/local baselines, then use the standard batteries.

No rootfs refresh, VM, performance, audio, fullscreen, or default work may
skip an earlier item.

## A1 frame-supply evidence

- Codec capacity is not the constraint: comparable local 1280x720@60 H.264 and
  VP9 runs sustain about 52-54 presented fps.
- Multiprocess alone was null (28.295 control, 28.960 treatment) because
  YouTube selected 640x360. `YT_FORCE_HD720=1` is reviewed default-OFF; its
  treatment is MP=1, audio-disable=1, media=1, forced-hd720=1, EGL=0.
- The sole valid forced-hd720 windowed measurement,
  `chromium-youtube-m7/20260710T174100Z-a1-hd720-t1-retry2-mp1-audio1`, is N=1:
  44.29/44.63 fps, mean 44.46. It proves active 1280x720 source but is 7.54 fps
  below the interim 52 gate. It recorded 812 callbacks; 1123 presents/19.950 s
  (56.29/s); VPQ 1148; 316 drops (27.53%); and async make-room stalls
  6882/6780, max 401524 us. The remaining lead is VPQ/drop and host-retire
  pressure, not codec capacity.

## Fullscreen, audio, and residual gates

### Actual fullscreen

Fullscreen acceptance requires nonce-bound actual player/document-fullscreen
state, stable transition/settle, active 1280x720 video, output mode, viewport,
scale, and composition state. The host parser rejects wrong dimensions, pause,
ambiguity, drift, and spoofed/out-of-order facts. The 44.46 windowed N=1 result
is not a fullscreen baseline or substitute.

### A2/A4 real Chromium audio

Kernel virtio-snd/OSS/ALSA is closed. `pacat` proves PipeWire-Pulse can create
S16LE/48 kHz stereo streams and a nonempty QEMU WAV. Chromium's
F32LE/48 kHz stereo/512-frame stream opens and starts, then has its primary
playback failure near 8 s; later `pa_operation is nullptr` rows are teardown.
The default-OFF handle-local localizer/reducer is reviewed, but effective
1024-frame quantum negotiation is unproven.

After A1 windowed N>=2, localize with one KVM+virgl boot: audio on,
`QEMU_AUDIO=none`, null sink, then pw-play -> paplay -> Chromium libpulse for
at least 20 s. Repair the proven userspace stream path. Final audio-on
validation is windowed plus actual fullscreen without the audio-disable flag,
then `QEMU_AUDIO=virtio`/WAV confirmation.

### Residual present wall and defaults

After forced-hd720 N>=2 in each mode and real audio, compare matching windowed
and fullscreen Linux-VM/local baselines. Investigate only measured VPQ/drop,
async make-room, host-retire, and present-throughput residuals if either mode
remains below about 55-60. Do not reopen codec, DNS, launcher packaging, or
the kwin 5.27 schedule wall without new evidence.

No default promotion without same-session A/B, explicit-off control, KDE
active-sample plus Chromium launch-only, R5 watch knobs, and M4/M5/M8 within
noise. Queue kickoff prewarm + tooltip first, then the video pair
`virtio_gpu_async_present=1` + `virtio_gpu_present_clock_60hz=1`, with
both-mode coverage. Keep `anon_free_poison`, `anon_fault_verify`, and
`vm_shootdown_cpumask_audit` armed. PCID remains off pending user-VM cpumask
completeness.

## Closed lanes and checkpoint rules

Closed unless focused evidence reopens them: codec-capacity theory, kernel
audio base, loader/config M4 levers, single-lib stubs, syscall/VFS-cost-as-M4,
unlocked-wait, present-clock alone, and PCID/noflush without prerequisites.
The kqueue `.poll` reentry fix independently passed at kernel `ed808576`
(top-level checkpoint `95f241af`); it is closed kernel review work, not VM or
YouTube credit.

Record PASS, FAIL, INVALID, NULL, and negative results honestly. Adversarial
NO-BOOT review precedes every kernel/image boot. Commit verified checkpoints
deepest-first and push the explicit branch lineage; preserve unrelated dirt.
