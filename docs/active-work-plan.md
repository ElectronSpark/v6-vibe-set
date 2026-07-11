# Active xv6 Work Plan

Last updated: 2026-07-11. This is the sole live plan; superseded forensic and
self-FAIL chronology is retained in git history and recorded artifacts.

## Objective and acceptance

Close YouTube 720p60 to the comparable same-host Linux-VM result in two
separate acceptance modes:

1. Windowed watch page: N>=2 valid real KVM+virgl runs, first >=52 presented
   fps, then about 55-60 with low drops and stable pacing.
2. Actual player/document fullscreen: separate nonce-bound fullscreen proof,
   then N>=2 comparable runs with the same thresholds. A maximized window or
   windowed result cannot close fullscreen or the overall goal.

Only `yt-presentfps` and `PERF-VIDEO` with real KVM+virgl GL count. Reject
llvmpipe/software fallback. Never pool modes or credit N=1 timing results.
Every concrete job is delegated; no default changes without their battery.

## Binding host and VM discipline

- WSL crashed from overlapping recursive raw-image `rg --text` scans (about
  24 GiB and 6.7 GiB RSS), not QEMU. Use only
  `/home/es/.local/bin/rg`, source trees only; never recurse images/artifacts
  or use `-a`, `-u`, `--no-ignore`, or `--binary`. A named artifact needs the
  guarded artifact wrapper or bounded named-file tooling such as `debugfs`.
- Wait synchronously for every returned build, VM, and serial process. Serial
  silence is not completion: use short marker commands, account for the first
  bracketed-paste character drop, wait for the owned command, then reacquire a
  prompt before a verdict. Never use `pgrep`.
- The conductor authorizes at most one VM worker. Before any VM authorization,
  require no active VM worker and an exact `/proc/*/exe` scan showing zero
  `qemu-system-*`/`qemu-kvm`; repeat it after owned process-group cleanup and
  synchronous reap. Do not edit QEMU/launcher scripts for singleton control.
  Guest RAM is not capped; only concurrent VMs are.
- A gate is named-branch specific: compare live HEAD with
  `origin/<current-branch>` explicitly, never `@{upstream}`. Each consumed
  authorization requires a new gate. There is **no current VM authority**.

## Verified A1 and diagnostic state

### Frame supply

- Codec capacity is not the constraint: comparable local 1280x720@60 H.264
  and VP9 runs sustain about 52-54 presented fps. Multiprocess alone was null
  (28.295 control, 28.960 treatment) because YouTube selected 640x360.
- Default-OFF `YT_FORCE_HD720=1` is reviewed. Its diagnostic/treatment arm is
  multiprocess=1, audio-disable=1, media-probe=1, forced-hd720=1,
  EGL-forensics=0. The host parser alone classifies source evidence.
- The only valid forced-hd720 windowed measurement is
  `chromium-youtube-m7/20260710T174100Z-a1-hd720-t1-retry2-mp1-audio1`:
  44.29/44.63 fps, mean 44.46. It proves active 1280x720 source and is an N=1
  diagnostic, not a performance claim: 7.54 fps below the interim 52 gate.
  It recorded 812 callbacks; 1123 presents/19.950 s (56.29/s); VPQ 1148,
  drops 316 (27.53%); and async make-room stalls 6882/6780, max 401524 us.
  Remaining evidence points to VPQ/drop and host-retire pressure.

### Capture-completeness diagnostic

- The serial parser/PTY route and diagnostic-only terminal path at
  `3a65c797` have passed independent NO-BOOT review: diag1 remains before
  normal idle/render/media/frame work, retains nonce-bound raw terminal status,
  and cannot claim FPS or semantic success. Static parser/route/transport
  proofs pass. JS guest-runtime console-throw/block and explicit-off parity
  remain non-credit because they have not run in a guest.
- The sole previously gated diag1 attempt
  `chromium-youtube-m7/20260711T142308Z-pid1796106-mp1-audio1-media1-hd7201-capturediag1`
  is INVALID/NULL and did **not** boot QEMU. Its preflight correctly rejected
  stale image `probe.js`; `DIAG_BOOTSTRAP_FINISH` is no timer/rVFC/fbstat,
  GPU-role, FPS, or semantic evidence.
- The stale boundary is closed and independently reviewed. `rootfs-refresh`
  rebuilt base `fs.img` without user/ports work, then bounded host/base/fresh
  clone receipt proved all three exact bytes; the clone was removed:

  | asset | bytes | SHA-256 |
  | --- | ---: | --- |
  | `manifest.json` | 813 | `46dced0c2886a66f93b04da8646d427a3b0c1563a7de7fb052bd6af56d63ac22` |
  | `probe-lib.js` | 4336 | `2fde2692bb276d62b5a3e07a32e9bdf0e6ac70be0b3217bfbc8d8dd912ce137c` |
  | `probe.js` | 22351 | `a674159ccabb89576f7cab0021bba56cec3aca46876e71ecdc44da14cd36287d` |

  Scratch-only repair is prohibited; the driver must clone base and verify
  extension assets before staging only helpers. Exact diag1 `YT_STATIC_CHECK=1`
  passes. The refresh re-staged host-GUI (Chromium plus EGL/Pulse preloads);
  missing optional GTK/Wayland-EGL development probes and `HOST_IDLE_BINARY`
  remain warnings, not a user/ports rebuild or a failure.

### Kernel review

The kqueue `.poll` reentry fix has independent NO-BOOT review PASS at kernel
`ed808576` (top-level checkpoint `95f241af`): unlocked, pinned dispatch and
the guard/admission/lifetime/fairness suite pass. It is a kernel review closure,
not VM or YouTube performance authority.

## Fullscreen, audio, and residual gates

### Actual fullscreen

Fullscreen proof must establish actual player/document fullscreen, stable
transition/settle, active hd720 playback, `videoWidth/videoHeight=1280x720`,
output mode, viewport, scale, and composition state. The host parser rejects
drift, ambiguity, wrong dimensions, pause, and spoofed/out-of-order facts.
The 44.46 N=1 watch-page result is not a fullscreen baseline.

### A2/A4 real Chromium audio

`pacat` proves PipeWire-Pulse can run S16LE/48 kHz stereo streams and create a
nonempty QEMU WAV. Chromium's F32LE/48 kHz stereo/512-frame stream opens and
starts, then has its primary playback failure near 8 s; later
`pa_operation is nullptr` rows are teardown. The default-OFF handle-local
localizer/reducer (`051dfa1`) is reviewed, but effective 1024-frame quantum
negotiation is unproven. Kernel virtio-snd/OSS/ALSA is closed.

After A1 windowed N>=2 and exact QEMU zero, localize real audio with one
KVM+virgl boot: audio on, `QEMU_AUDIO=none`, null sink, then pw-play -> paplay
-> Chromium libpulse for >=20 s. Fix the proven userspace stream path; final
audio-on validation needs both windowed and actual fullscreen with no
audio-disable workaround, then `QEMU_AUDIO=virtio`/WAV confirmation.

### Residual present wall and defaults

After forced-hd720 N>=2 in each mode and real audio, compare matching windowed
and fullscreen Linux-VM/local baselines. Keep fullscreen scaling/composition
separate from frame supply, then investigate only VPQ/drop, async make-room,
host-retire, and present-throughput if either mode remains below about 55-60.
Do not reopen codec, DNS, launcher packaging, or the kwin 5.27 schedule wall
without new evidence.

No default promotion without same-session A/B, explicit-off control, KDE
active-sample plus Chromium launch-only, R5 watch knobs, and M4/M5/M8 within
noise. Queue kickoff prewarm + tooltip pair first, then the video pair
`virtio_gpu_async_present=1` + `virtio_gpu_present_clock_60hz=1` with both-mode
coverage. Keep `anon_free_poison`, `anon_fault_verify`, and
`vm_shootdown_cpumask_audit` armed. PCID remains off pending user-VM cpumask
completeness.

## Immediate authority chain

1. **Fresh conductor gate:** explicit same-name origin match, no active VM
   worker, exact zero-QEMU scan, and real KVM/virgl host prerequisites. It may
   authorize exactly one diag1 run only.
2. **Exactly one diag1 KVM+virgl run:** multiprocess=1, audio-disable=1,
   media-probe=1, forced-hd720=1, EGL=0, capture-diag=1. It is strictly
   no-FPS/no-semantic credit; own/reap QEMU and finish exact zero.
3. **Evidence-driven NO-BOOT branch if needed:** INVALID/INCOMPLETE diagnostics
   trigger bounded artifact/source forensics and adversarial review, never an
   arbitrary wait or retry. Return to a fresh gate only after that review.
4. **A1 windowed:** after diagnostic success/review and a new specific gate,
   run N>=2 valid forced-hd720 windowed trials; require >=52 before pursuing
   about 55-60.
5. **Fullscreen:** add/review actual-fullscreen evidence, then run distinct
   N>=2 fullscreen trials; never pool with windowed.
6. **A2 localization and userspace repair:** only after windowed A1 N>=2.
7. **Audio-on both-mode validation:** windowed and actual fullscreen, no
   audio-disable workaround.
8. **Residual present work:** compare each accepted mode to its Linux-VM/local
   baseline and address only measured remaining present pressure.
9. **Default batteries:** kickoff/tooltip first, then both-mode video pair.

## Closed lanes and checkpoint rules

Closed unless new focused evidence reopens them: codec capacity theory,
kernel audio base, loader/config M4 levers, single-lib stubs,
syscall/VFS-cost-as-M4, unlocked-wait, present-clock alone, and PCID/noflush
without prerequisites. Record PASS, FAIL, INVALID, NULL, and negative results
honestly. Adversarial NO-BOOT review precedes every kernel/image boot. Commit
verified checkpoints deepest-first and push this branch lineage explicitly;
preserve unrelated dirt.
