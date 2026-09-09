# Frozen Chromium bottleneck capture

These diagnostic helpers restore the bounded controller, HTTP server, guest
inspection helper and serial QEMU wrapper retained in
`build-x86_64/gui-progress-audit/chromium-bottleneck-trace-20260908T020910Z/`.
The controller deliberately depends on that receipt's framebuffer helper and
its layout/validation files. It also requires the frozen wait-progress kernel,
audio-passcred rootfs and local 1280×800 60-fps clip named in `controller.py`.
It does not rebuild any artifact or alter the normal browser launcher policy.

Apply root `AGENTS.md` and the authoritative GUI runtime skill first. Only one
VM lane may be authorized. The controller repeats the exact zero-QEMU preflight
immediately before launching through `launch-gui.sh` / `run-owned-qemu.sh`.
Without `--run-vm`, it prints the preparation contract and exits without a VM.

An explicit diagnostic build uses `--diagnostic-config /absolute/capture.json`.
Without `--run-vm`, that option performs streamed input verification and exits
without launching. It may read the entire private image and staged runtime;
coordinate this resource-heavy preflight with other workers. The option does
not build, stage or modify either image. The kernel, fixture, media and kernel
append gates above still apply when launching.

The configuration is a JSON object with these required fields:

| Field | Required value |
| --- | --- |
| `schema_version` | Integer `1` |
| `arm` | `chromium-bottleneck-diagnostic` |
| `rootfs_path`, `rootfs_sha256` | Absolute private image path and its actual full-file SHA256 |
| `runtime_manifest_path`, `runtime_manifest_sha256` | Absolute completed runtime receipt path and its SHA256 |
| `source_commit`, `patch_sha256` | Exact current pins in `../chromium-diagnostics/upstream-sources.json` |

The completed runtime manifest requires `schema_version: 1`,
`kind: "chromium-bottleneck-diagnostic-runtime"`, `status: "staged"`, the same
`source_commit`, `patch_sha256` and `rootfs_sha256`, plus `browser_path`,
`browser_sha256`, `browser_bytes`, `host_browser_path` and `runtime_files`.
`browser_path` must be
`/opt/host-gui/wayland-chromium/chrome-linux64/chrome`; its diagnostic SHA must
differ from the frozen baseline. `host_browser_path` names the actual staged
x86_64 ELF executable. Each runtime record has exactly
`{guest_path, sha256, bytes}` and maps beneath `host_browser_path.parent` using
its suffix under the fixed guest runtime directory. The browser's record must
match the top-level browser identity. The optional `build_provenance` object
retains source, GN, toolchain and build receipt metadata. The original manifest
is also copied verbatim, preserving additional staging provenance.

Preflight rejects missing artifacts, incorrect hashes, symlink paths, traversal,
duplicate records and a private image alias/hardlink of the protected baseline.
All host runtime files and the entire image are hashed in 1-MiB chunks before
launch. Limits are 1 MiB per configuration/manifest/policy, 1,024 runtime files,
2 GiB per runtime file, 4 GiB total runtime bytes and 64 GiB for the image.
There is no placeholder configuration that bypasses these gates.

The controller serves a normalized policy, verifies its SHA in the guest and
activates it explicitly in the inspection helper. A stale guest policy file
cannot change baseline behavior. Guest preflight hashes every declared runtime
file before playback (150-second hash deadline); each launch rechecks those
files' identity, size, mode, modification/change times and link count. This proves the declared
runtime subtree; shared OS libraries outside it remain part of the image
identity. The helper removes its policy environment variable before invoking
the unchanged `/bin/wayland-chromium` wrapper.

Diagnostic receipts use `chromium-diagnostic-capture-...` and retain the config,
runtime manifest, source pin, normalized policy and host/guest verification.
All OFF/ON/ON/OFF trials use that same diagnostic build. Only ON appends
`disabled-by-default-media.bottleneck`; OFF does not enable browser tracing.
The default command retains the frozen image and browser SHA. These controls
measure enabled recording cost within the diagnostic build; producer code may
retain small bookkeeping costs while its category is disabled. Successful
capture configuration is not proof that required events were emitted or that
the capture is complete; validate runtime event coverage and loss separately.

Small-file, no-boot policy checks:

```sh
python3 scripts/gpu/chromium-bottleneck/test-capture-config.py
python3 scripts/gpu/chromium-bottleneck/test-capture-cleanup.py
```

For the initial emission check, add `--diagnostic-smoke` together with
`--diagnostic-config`. It selects only traced `on-1`, with the same unchanged
15-second fixture, visual review, ownership checks, silent playback and trace
export gates. The normalized guest policy and capture identity seal
`capture_purpose: "emission-smoke"` and `selected_order: ["on-1"]`. A normal
capture seals `capture_purpose: "comparison"` and all four trial names. A smoke
flag without a diagnostic configuration is rejected before artifact checks.

A smoke run writes `emission-smoke-status.json`; it cannot report a valid
comparison or performance credit. An accepted trace only makes the capture
ready for event-coverage/loss validation. The single ON observation has no
cross-arm argv reference. Its report retains actual trace flags, browser and
profile identity, same PID/start launch URL and fixture-ready proof, while
explicitly withholding complete-argv proof when procfs reaches its 32-argument
cap. The four-arm comparison retains its original cross-arm checks. The smoke
observation filename is `t1-on-1-completed-observation.json`.

```sh
env \
  GL_AUDIT_KERNEL_SHA256=59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104 \
  MEDIA_AUDIT_FIXTURE_SHA256=d5889aa784b8285ad7314a349db8817d71bf83c8792a53122f81ea74a7aef5bf \
  GL_AUDIT_APPEND_EXTRA=virtio_gpu_submit_trace=1 \
  python3 scripts/gpu/chromium-bottleneck/controller.py --run-vm
```

Keep the exact returned process handle and wait synchronously through final
cleanup. The controller prints its unique receipt directory and pauses before
each measurement for visual inspection of the named screenshot. Only after an
agent has actually inspected the mapped fixture may it create the printed
`*-mapped-visible.ok` regular file. These markers record agent visual review;
they do not request user permission. Retain any notification/occlusion or
capture limitation in the marker and final review. Guest QMP screendump can
report `no surface` on the GL path even when the host capture works.

The order is OFF/ON/ON/OFF with fresh browser profiles and the unchanged
15-second fixture. ON adds
`media,cc,viz,benchmark,mojom,mojom.flow,graphics.pipeline,disabled-by-default-mojom`
and a 32-MiB tracing ring. It retains the original bundled video-sink feature
path. No console command, QMP command or screenshot runs during playback.
Snapshots outside playback retain Chromium executable/argv/start identities,
all GPU-role TIDs, KWin and every `Kthread: 1` process, exclusions and read errors.
Inherited zygote argv does not identify renderer roles; reconcile with trace
metadata. Generic kernel workers cannot be assigned to a queue from procfs.

Accepted raw JSON exports are nonempty regular files no larger than 64 MiB,
transferred only after graceful browser exit. A rejected export preserves its
lstat metadata and continues the remaining controls. If the original is a
regular file no larger than 128 MiB, the helper also attempts a bounded gzip
preservation (30-second compression limit; 64-MiB archive/export limit), with
original and archive hashes. `*-rejected-chromium-trace.json.gz` remains rejected
evidence and must not silently enter accepted trace analysis. Missing/nonregular
files and failed preservation retain explicit errors. Independently validate
complete playback coverage and loss; a successfully exported ring can still
have overwritten earlier playback.

For an explicitly preserved oversized trace, `chromium-trace-shard.py` can
verify its original hash and produce ordered JSON parts plus a manifest without
removing events or fields. Every part is bounded, and reconstruction checks
order, hashes, event counts and full JSON structural equivalence. The original
export remains rejected and its archive stays untouched; analysis of the
verified derivative must be labeled separately.

```sh
python3 scripts/gpu/chromium-trace-shard.py RUN/on-1-rejected-chromium-trace.json.gz \
  --expected-raw-sha256 ORIGINAL_RAW_SHA256 --output-dir RUN/analysis/on-1-trace-shards
python3 scripts/gpu/chromium-frame-analysis.py --run-dir RUN --trial on-1 \
  --observation t2-on-1-completed-observation.json \
  --trace-manifest analysis/on-1-trace-shards/manifest.json --output-dir RUN/analysis
python3 scripts/gpu/chromium-mojo-boundary.py \
  RUN/analysis/on-1-trace-shards/manifest.json RUN/analysis/on-1-mojo-boundary.json \
  --trace-manifest --alignment RUN/analysis/on-1-trace-alignment.json
python3 scripts/gpu/chromium-frame-ledger.py \
  RUN/analysis/on-1-trace-shards/manifest.json --trace-manifest \
  --alignment RUN/analysis/on-1-trace-alignment.json \
  --output RUN/analysis/on-1-frame-ledger.json
```

Replace `RUN` and `ORIGINAL_RAW_SHA256` with the exact finalized receipt and
its recorded hash. Existing outputs are not overwritten. The frame analyzer's
input overrides are relative to `RUN`. Small raw JSON traces use its `--trace`
option or per-trial default filename; the Mojo analyzer also accepts positional
`TRACE OUTPUT` without `--trace-manifest`.

The optional frame ledger preserves every observed preparation, selection,
attempt, construction and named presentation scope in PID/PTS candidate buckets.
Repeated and unclosed scopes, events outside the window, ambiguous joins and
missing feedback remain explicit. Prepared-without-selection candidates are not
confirmed drops; the tool never assigns a drop-counter residual to a stage.
Counter producers and trace-loss/open-scope evidence are retained separately.
It uses a half-open measurement window and marks clock-boundary uncertainty.
Named presentation endpoints do not establish success or physical scanout.
Run its small offline regressions with
`python3 scripts/gpu/test-chromium-frame-ledger.py`.

Use an explicit regular JSON trace without `--trace-manifest` for a small raw
export. Outputs must be fresh paths; the loader preserves the existing limits
and refuses overwrite. An independently validated alignment is required;
PID/PTS candidates are not unique frame/stream/reset identities. The ledger
leaves existing frame-analysis semantics
unchanged. See the [producer map](../../../docs/chromium-bottleneck-field-map-20260908.md)
for required browser/kernel fields that the current configuration cannot emit.

Receipts retain source copies/hashes, frozen artifact identities, actual QEMU
and browser arguments, telemetry, screenshots, observations, exports, serial
completion markers, browser exits and cleanup. The owner requests QMP quit,
waits for the launcher, performs bounded identity-checked fallback cleanup if
needed, and records exact final inventory and overlay removal. Receipt creation
refuses existing directories and review markers. Teardown errors remain in
`cleanup.json`; launcher timeout/escalation cannot skip the final inventory.
The final verdict also fails for an unreaped/nonzero launcher, leftover overlay, changed
base/protected metadata, or an unknown/nonzero QEMU inventory. An HTTP server
whose thread never started is closed without waiting on `shutdown()`.
For a running server, the shutdown request and loop joins share a bounded
deadline; a blocked handler records a cleanup error and permits final inventory.
The cleanup tests use tiny ordinary child processes and a local HTTP listener,
with no VM; one test forces a TERM-resistant child through exact SIGKILL/reap
while checking that another child remains untouched. Independently
run `scripts/launch/qemu-exact-inventory.sh --require-zero` after the controller
exits. A final launcher exit does not erase failed trial/export evidence.
