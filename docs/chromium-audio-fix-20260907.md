# Chromium audio fix — 2026-09-07

The [YouTube audit](chromium-youtube-audit-20260907.md) stopped at 0:00 with
an audio renderer error. A kernel fix to ordinary Unix stream writes resolves
that observed failure: the same accelerated Chromium mode now plays the video
through 5:28, with working mouse controls and verified non-silent PCM output
through the normal KDE/PipeWire/virtio-sound path. Focused regressions and two
20-second Unix Pulse streams pass. Chromium and the VM both exit cleanly.

## Transport localization

The [diagnostic receipt][diagnostic] used the existing kernel
`af174a9fc9919fd9990942aff8e2b14a406a663ef6cd47d5b3a8320bb0472b21`,
the existing root filesystem, six vCPUs, 8 GiB, SDL/virgl, and a QEMU WAV
backend attached to virtio-sound. PipeWire and its Pulse server stayed alive,
and `pactl info` and sink enumeration succeeded. The default sink was
`alsa_output.xv6_virtio`; the Pulse server was PipeWire 1.0.5, protocol 35.
This boot did not establish a working KDE desktop.

The existing `chromium-pulse-stream-reducer` uses float32 stereo at 48 kHz,
512-frame writes and Chromium-like threaded-mainloop, cork, flush and reset
operations. Its native Unix-socket streams failed with Pulse error 11,
`Connection terminated`, at 623 ms and 1,487 ms. Disabling shared memory with
a supported `PULSE_CLIENTCONFIG` containing `enable-shm = no` and
`enable-memfd = no` still failed at 15,749 ms. The log confirmed private
transport, so disabling shared memory is not a sufficient fix.

For a controlled comparison, the same running server temporarily exposed
`module-native-protocol-tcp` on guest loopback `127.0.0.1:47139`. Two streams
completed 20 seconds (20,006 and 20,009 ms), including stop/reset/teardown,
through the same physical sink. The listener was then unloaded. No TCP
default, server restart, null sink or persistent configuration change was
used. Aggregate WAV analysis found nonzero PCM samples, with peak magnitude
2,621; the recording does not isolate individual test arms in time.

An earlier `PULSE_NO_SHM` environment experiment is not a valid shared-memory
control: [PulseAudio 16.1's client configuration parser][pulse-config] does
not parse that variable. The supported client configuration above supersedes
that experiment.

The VM was synchronously reaped, its temporary overlay removed, and the
worker and conductor independently confirmed exact zero QEMU processes.

## Implementation and regression

`kernel/kernel/vfs/unix_socket.c` previously published ordinary stream-write
bytes and woke readers before queuing `SO_PASSCRED` metadata. A receiver could
consume those bytes first, leaving a late credential entry behind the read
cursor. The receive path could then return zero for a positive-length read
even though the stream peer remained open. Short-write exits could also omit
credentials for already-published bytes.

The change publishes each stream segment and its credentials under
the sender lock, then wakes readers. Capacity checking and wait enrollment
use that same lock, and partial writes retain metadata for every committed
segment. The change is scoped to ordinary `SOCK_STREAM` writes.
Zero-length writes validate peer shutdown without queuing an empty credential
entry. Host Linux returns zero for a live peer and `EPIPE` for peer read-shutdown
or close; the implementation preserves those conditions.
[PipeWire 1.0.5's Pulse server][pipewire-client] uses ordinary nonblocking
`send` calls for descriptors and payloads, which reach this kernel write path.

`scripts/image/unix-passcred-stream-reducer.c` alternates `write`, `writev`
and nonblocking `send`, verifies data and credentials, and checks the drained
socket after the sender's syscall returns. A retained sender descriptor makes
zero-length receives during that phase conclusively premature. The test then
closes and reaps its child and verifies real EOF. Host Linux passed 4,096
rounds with 262,097 bytes and 4,096 credential deliveries. Compilation used
`cc -O2 -Wall -Wextra -Werror`.

The [original-kernel run][baseline] failed after 24 completed rounds: a receive
returned PID 0 and UID/GID 65534 instead of the live sender's credentials.
The test exited 1, synchronously reaped its child, and did not time out. Because
credential validation stopped the test first, this reducer run alone does not
establish premature EOF.

A separate Pulse run with `audio_unix_ipc_trace=3` captured client `recvmsg-scm`
with `bytes=0 ret_bytes=0 has_cred=1`, followed by credential emission with zero
payload. This matches the proposed failure mechanism, but its adjacent state
line was truncated. Tracing overflowed the guest console (415,897 dropped
bytes), the 45-second serial completion marker did not arrive, and the owned
controller cleaned up the VM. Do not count that traced run as a complete
20-second stream result. Exact zero QEMU was checked again before building.

The [focused kernel build][build] completed successfully using
`cmake --build build-x86_64 --target kernel -j2`. The new `xv6.bin` SHA-256 is
`b1ea5a2bde0f7372655eba012c1bdcb834303b5752bc212a48efa1b957557cc5`.
The build log retains the existing GNU-stack linker warnings. A scoped sparse
check of `unix_socket.c` completed with one file checked, zero failures and
zero errors. The original kernel, ELF and symbols are preserved in the
baseline receipt. The private rootfs differs only by the injected regression
executable, whose mode, size and dumped SHA-256 were verified; the original
base image was preserved.

The [patched-kernel run][verification], with verbose IPC tracing disabled,
passed 4,096 reducer rounds: 262,097 bytes, 5,445 credential deliveries,
4,096 drained-socket checks, real EOF after close, and child exit zero.
`webkitabitest chromium-ipc` passed all six checks, including its 4,096
SCM-passed SyncSocket exchanges; `webkitabitest stream-page` passed its
9,701,895-byte transfer. Both were run under the existing bounded process
wrapper and reaped successfully. Credential delivery counts can differ by
stream read segmentation; data, credential identity and EOF assertions are
the compatibility checks.

Two ordinary Unix-socket Pulse streams then passed 20 seconds (20,001 and
20,005 ms), with 1,522 and 1,511 writes respectively and clean operation and
teardown results. No TCP endpoint, shared-memory override or alternate client
configuration was used. The sink was drained before beginning browser audio
capture.

## Graphical YouTube verification

The same boot has a healthy KWin/Plasma session. Chromium used the actual
Plasma environment, the existing `WAYLAND_CHROMIUM_MULTIPROCESS=1` mode,
a fresh `/tmp/chromium-audio-fix` profile and the normal Unix Pulse endpoint.
The public Big Buck Bunny video (`aqz-KE-bpKQ`) opened on its poster. A QMP
mouse click on Play produced a cloud/tree video frame, and a later
[playing capture][playing] shows a different cave frame at **0:45 / 10:34**,
with the pause control and unmuted volume icon visible. The previous audio
renderer error is absent from these captures.

Mouse controls paused at 1:03, held the same frame for a further 23 seconds,
and resumed into a new scene. Mute and unmute visibly changed their icons,
tooltips and volume state. A paused timeline click advanced 1:56 to 2:36;
playback then resumed at 2:38. Player fullscreen showed enlarged moving video
and the YouTube fullscreen toast, and its exit control restored the watch page.

[Stats for nerds][stats] reported current `1280x720@30`, AV1
`av01.0.01M.08 (396)` with Opus `(251)`, 68.87 seconds of buffer and media
time 275.36 seconds. At the final pause the player reached **5:28 / 10:34**.
These observations establish functional AV1/Opus playback in this run.
The final counter was 960 dropped frames out of 11,543; this is not a matched
performance result or proof of hardware decoding, 60 fps or long-run recovery.
All player actions used the QMP virtual mouse; this does not resolve the
earlier host left-button delivery uncertainty.

## Captured media audio and cleanup

The [YouTube-only WAV][youtube-wav] contains **321.2001 seconds** of stereo
PCM16 at 44.1 kHz. The [analysis][youtube-analysis] counts 23,728,802 nonzero
samples out of 28,329,848, peak magnitude 32,766 and RMS 2,269.846
(-23.189 dBFS). This verifies non-silent media audio through the guest sound
stack and QEMU WAV backend; it is not a host-speaker listening test or an
audio/video synchronization measurement.

All tone tests finished before browser launch. Host file-size cursors bracketed
the YouTube interval, with the pre-play cursor at 9,187,328 bytes and post-exit
cursor at 66,023,424 bytes. Extraction excluded a further one-second guard
after the first cursor for QEMU stdio buffering, aligned the bounds to PCM
frames, and omitted the final unflushed tail. The exact source range is
`[9,363,728, 66,023,424)`. It includes playback, pause, mute, seek and associated
silence; its sample duration is not the wall-clock test duration. The reproducible
`audio-extract.py`, cursor ledger, original WAV and hashes are retained in the
verification receipt. Extracted WAV SHA-256:
`af93ed921892ff70170bd3d6e3f7ed71e1184a1a6003fbf32ebe1336dd45f239`.

After a graphical close, the serial shell synchronously waited for Chromium
and reported exit zero with a fresh prompt. Audio pending bytes were zero.
The owned VM controller then exited zero at 18:45:50 UTC, reaped QEMU, removed
its overlay and verified that its private base image's size/mtime were unchanged.
Worker and conductor independently confirmed exact zero QEMU processes.
The fixed kernel, original rollback kernel and private regression image remain
available; no test VM is left running.

The observed September audio failure is fixed. Historical dedicated-Pulse
stream-replacement failures, matched performance, dropped frames and other
desktop issues remain separate work in the [active plan](active-work-plan.md).

[diagnostic]: ../build-x86_64/gui-progress-audit/audio-diagnostic-20260907T180625Z/
[baseline]: ../build-x86_64/gui-progress-audit/audio-passcred-baseline-20260907T182610Z/
[build]: ../build-x86_64/gui-progress-audit/audio-passcred-build-20260907T183054Z/
[verification]: ../build-x86_64/gui-progress-audit/audio-passcred-verify-20260907T183220Z/
[playing]: ../build-x86_64/gui-progress-audit/audio-passcred-verify-20260907T183220Z/13-playing-time-host.png
[stats]: ../build-x86_64/gui-progress-audit/audio-passcred-verify-20260907T183220Z/26-stats-for-nerds-host.png
[youtube-wav]: ../build-x86_64/gui-progress-audit/audio-passcred-verify-20260907T183220Z/youtube-only.wav
[youtube-analysis]: ../build-x86_64/gui-progress-audit/audio-passcred-verify-20260907T183220Z/youtube-audio-analysis.json
[pulse-config]: https://raw.githubusercontent.com/pulseaudio/pulseaudio/v16.1/src/pulse/client-conf.c
[pipewire-client]: https://raw.githubusercontent.com/PipeWire/pipewire/1.0.5/src/modules/module-protocol-pulse/client.c
