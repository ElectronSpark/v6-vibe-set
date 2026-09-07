# Chromium GL lifecycle baseline — 2026-09-07

**The first animation soak failed after 33 seconds, but this run did not
establish a virgl watchdog timeout or context quarantine.** WebGL 1 and 2
passed initial drawing/readback and a later resize. Animation callbacks stopped
advancing while the page's timer and HTTP reporting continued. A separate host
controller marker bug ended the run early; the guest diagnostic command had
actually completed and returned a shell prompt.

Receipt: `build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z`.
The [actions][actions], [complete JSON results][json], and [compact result
chronology][summary] retain host UTC and page-relative times. Guest wall time
is not assumed identical to host time.

## Workload and provenance

The [bounded fixture][fixture] draws one triangle per API, changes its color,
samples center/corner pixels every two seconds, and limits each canvas to
640×384. Its ordinary soak does not inject context loss. Fixture SHA-256:
`4aeeb5b24d53a756dd5b6258f802dd8171ad64531b8a96bfe01c8c0c13846096`.

This boot preserved the working audio kernel, SHA-256
`b1ea5a2bde0f7372655eba012c1bdcb834303b5752bc212a48efa1b957557cc5`,
and the private filesystem prepared for the audio regression. Root revision
was `470d6066897216c453ac99a26e9dfe966f6310bd`, kernel revision
`19ef57da2dc0970472bccba49bcc3188f07b05e8`; the fixture was uncommitted.
See [source state][source] and [artifact provenance][provenance].

The [actual QEMU arguments][qemu] retain QEMU 9.0.2, KVM, six CPUs, 8 GiB,
SDL/virgl and virtio input. No GPU timeout or diagnostic trace setting changed.
The serial-only wrapper replaced `-serial mon:stdio` with an owned socket.
Chromium used existing `WAYLAND_CHROMIUM_MULTIPROCESS=1`, a fresh
`/tmp/gl-first` profile, and the current Plasma environment. The ordinary
desktop launcher's flags were not changed. Same-run roles were KWin PID 62,
Plasma PID 88, browser PID 222 and Chromium GPU process PID 257.

## Observed chronology

| Host UTC | Observation |
| --- | --- |
| 19:10:26–19:11:00 | Owned VM launched. First host capture was black during startup; later capture 07 shows the KDE desktop. Neither is browser-readiness proof. |
| 19:12:44 | Browser 222 launched with the local fixture URL. |
| 19:13:06 | Initial JSON: both APIs ready, correct green center/black corner, no GL error, fresh ANGLE/Mesa/virgl/D3D12 RTX 4060 renderer. [Capture 08][ready] shows both triangles. |
| 19:13:45–46 | QMP mouse started the 60-second soak; [capture 09][start] shows running progress. |
| 19:14:16 | JSON sequence 17: 930 soak draws and 13 periodic checks per API; last animation-frame gap 3,063 ms. Timer reporting still runs and page visibility remains `visible`. |
| 19:14:18 | Sequence 18: `FAIL`, `animation stopped progressing`, elapsed 33,023 ms, last-frame gap 5,067 ms. Neither API reports an error or unexpected context loss. |
| 19:14:20 | QMP resize is processed after the failure: both canvases become 640×384 and new draw/readback checks pass. [Capture 10][stale] still shows earlier `RUNNING`, 27 seconds and 922 draws; it does not show the JSON failure or completed resize. |
| 19:14:47–56 | Full before/after GPU counters and process roles collected; exact marker and fresh shell prompt returned. |
| 19:15:39–19:16:24 | Binary argv/environment read completes, but the host parser misses its completion marker. Controller reaches its 45-second deadline and performs owned VM cleanup. |

The reported draw counts are application submissions, not measured presented
FPS. The capture/JSON mismatch and missing animation callbacks localize an
observation boundary; they do not identify Chromium, KWin, scheduling, a fence,
or host virgl as the cause.
The fixture checks its five-second progress bound every 250 ms and cancels the
pending animation callback on failure. The 5,067 ms gap is a threshold-crossing
observation, not a measured GPU timeout or the natural duration of the pause;
this run does not establish whether callbacks would resume unaided.

## GPU counter boundary

The [complete before/after capture][counters] reports:

| Counter | Before browser | After failed soak |
| --- | ---: | ---: |
| `virtio_failures`, `virtio_timeouts` | 0, 0 | 0, 0 |
| `virtio_context_failed`, `virtio_context_failures` | 0, 0 | 0, 0 |
| Async posted / retired / pending | 264 / 264 / 0 | 6,192 / 6,192 / 0 |
| Posted `SUBMIT_3D` | 157 | 4,188 |
| Submit admission stalls | 0 | 73 |
| Maximum admission wait | 0 µs | 27,169 µs |
| Display presents / completions | 53 / 53 | 999 / 999 |
| Reported async depth | 32 | 32 |

Admission waits are distinct from posted-command watchdog failures. The
complete retained [serial][serial], [debugcon][debugcon] and [QEMU log][runlog] contain no matching virgl
timeout/quarantine/failure, panic/fatal-page-fault, or console dropped-output
marker through cleanup. This is limited to these logs and captured counters.

## Serial timeout correction and remaining work

The complete [escaped serial log][serial] ends with the final environment
entry, a NUL byte, `GL_DONE_31_15524`, and a fresh `root:/#` prompt. The
[controller][controller] required its marker to occupy a complete line.
Because `/proc/.../environ` has no final newline, the marker was appended to
the binary output and failed that test. This proves a parser false timeout;
it supplies no guest-hang or console-backpressure evidence. A [parser replay][marker]
confirms the missed standalone marker and retained prompt. The precise
guest completion time is not timestamped separately. Raw logs remain intact;
[escaped-log provenance][escaped] records hashes and the reversible byte
escaping used for guarded text searches.

The next controller should emit a newline before its exact completion marker,
retain the fresh-prompt check, and capture smaller selected GPU counter sets.
Further reproduction should correlate timer progress, animation callbacks,
Wayland/presentation events and per-role fence state before linking this
pause to an independently identified kernel ordering defect.

Intentional loss/restoration, fullscreen, second-profile soak, browser clean
close/reopen and the subsequent YouTube transitions were **not executed**.
Five host PNGs were retained; QMP guest captures returned `no surface`, and
host overlays remain visible. There is no paired scanout, stress-closure,
performance or new audio claim.

[Owned cleanup][cleanup] reaped the launcher with status 0, removed the overlay,
and left the protected image size/mtime unchanged. Worker and root separately
verified exact QEMU count zero. Browser clean-exit status was not collected.

[actions]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/actions.jsonl
[json]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/webgl-results.jsonl
[summary]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/webgl-result-summary.json
[fixture]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/webgl-fixture.html
[source]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/source-state.txt
[provenance]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/provenance.json
[qemu]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/qemu-cmdline.json
[ready]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/08-first-ready-host.png
[start]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/09-first-soak-start-host.png
[stale]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/10-first-soak-resize-host.png
[counters]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/11-stall-gpu-counters-roles.txt
[serial]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/serial.log.escaped.txt
[debugcon]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/debugcon.log.escaped.txt
[runlog]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/run.log
[marker]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/serial-marker-diagnosis.json
[controller]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/controller.py
[escaped]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/escaped-log-provenance.json
[cleanup]: ../build-x86_64/gui-progress-audit/chromium-gl-lifecycle-20260907T191026Z/cleanup.json
