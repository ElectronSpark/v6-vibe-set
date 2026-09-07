# Virgl fence publication ordering — 2026-09-07

The Chromium stall investigation found an independent kernel ordering defect:
a prepared command could already look complete before it entered the GPU
queue. The fix assigns fence IDs when commands are published. Its connection
to the historical virgl stalls and the September animation-callback pause is
not established. See the [separate lifecycle baseline](chromium-gl-lifecycle-audit-20260907.md)
for the observed pause and the corrected serial marker diagnosis.

## Failure mechanism and implementation

`virtio_gpu_user_submit()` reserved an ID before allocating/copying the async
command and before acquiring `op_lock`. A second client could publish and
complete a newer ID during that interval or during an unlocked admission
retry. The global completion watermark then classified the older, unpublished
command as complete. The DRM execbuffer path uses that watermark when exporting
an initially signaled fence, so the ordering matters to buffer lifetime.

The matching [QEMU 9.0.2 virgl implementation](https://raw.githubusercontent.com/qemu/qemu/v9.0.2/hw/display/virtio-gpu-virgl.c)
also completes pending fenced commands whose IDs are no greater than its
renderer callback value. IDs therefore need to follow publication order on
this driver's single control-queue timeline.

Both synchronous and asynchronous publication now assign the ID under
`ctrlq.lock`, after admission and before updating the available ring. A
prepared command carries the `FENCE` flag with ID zero; an admission retry
does not allocate an ID. Async posting copies the published ID into
caller-owned `prep.fence_id` before the device can retire and clear the slot.
Synchronous callers read the published header while holding `op_lock`.
Cursor transfers, copies, browser submits and optional fenced scanout flushes
use this same allocator. Existing unfenced operations stay unfenced.

Synchronous completion checks the echoed fence flag and ID before advancing
the watermark. Duplicate helper-level fence accounting was removed. Resource
submit watermarks use a maximum so concurrent ioctl return order cannot
regress their last fence.

Async watchdog age now starts at publication, including the direct 3D-transfer
path that previously omitted its timestamp. Post counters and trace records
read a local snapshot, because the reaper may clear the live slot immediately
after publication. The five-second watchdog policy is unchanged.

## Deterministic regression

[The reducer](../scripts/image/virgl-fence-order-reducer.c) creates two named
contexts on one GPU file. With the default-off
`virtio_gpu_fence_order_probe=1` test gate, A pauses after preparation and
before `op_lock`; B waits for that point, submits a synchronous no-op and
completes; then A is released to publish. Every gate wait is limited to five
seconds, the helper has a 30-second deadline, and ordinary clients do not
enter the gate. The helper joins A, checks `A_fence > B_fence`, waits A's fence
and destroys both contexts, including after an ordering assertion failure.

Build the helper from the repository root:

```sh
cc -O2 -Wall -Wextra -Werror -pthread -idirafter kernel/kernel/inc \
  scripts/image/virgl-fence-order-reducer.c \
  -o /tmp/virgl-fence-order-reducer
```

Run once per boot with no arguments. Pair its result with the kernel's
`A-unpublished` log: the baseline must show an already-covered reserved ID,
and the fixed kernel must show ID zero while A remains unpublished. The
standalone inequality alone does not prove that the test gate ran. This
regression uses normal GPU no-ops and injects no device hang. Admission retry
handling was source-reviewed; a forced full-queue regression is separate work.

The [instrumented baseline build][baseline-build] uses kernel revision
`19ef57da` plus the test gate, without the publication fix. Its kernel SHA-256
is `c1758b60fe89169b271befe3ed0a83a1cf5db82265d6d4ba36b7c93e82aefc7e`.
The candidate source was saved, the baseline was built, and the candidate was
restored; source hashes, the exact baseline patch and restoration hashes are
retained in that receipt.

The [baseline VM][baseline-run] reproduced the defect:

| Observation | Value |
| --- | ---: |
| A's reserved fence while still unpublished | 169 |
| B's published and completed fence | 170 |
| Completion watermark while A was unpublished | 170 |
| `prematurely-complete` | 1 |
| Reducer result / exit status | `FAIL publication-order` / 1 |
| Context cleanup errors | 0 |

Kernel console output interleaved with the helper's stdout, splitting several
lines. The retained tail contains the values above, `reducer_rc=1`, the exact
completion token and a fresh shell prompt. The host parser nevertheless
required a standalone marker line and timed out. The test completed; the
after-test GPU counter command was not run. Owned cleanup reaped QEMU and
removed its overlay, and root independently checked exact QEMU count zero.
The next controller constructed its marker in separate shell pieces so that
the complete token could not appear in echoed shell input, and redirected
helper output to a guest file. This removed the marker-line requirement;
the candidate run exposed a separate overly strict prompt check below.

The [candidate build][candidate-build] completed with kernel SHA-256
`df79132d675029ff03386989574e8b1dbb9697adcd3236dc455b404ca147487b`.
The focused sparse invocation checked two compilation entries with zero
failures and zero errors. Its command, logs, kernel ELF and source hashes are
retained.

The [candidate probe VM][candidate-run] used the same reducer binary, SHA-256
`5d5dece92f172ed8459421053c16f1354194455adf4f2c99afbf9311d504ffa1`.
The shell reported **`test_rc=0`**. The kernel gate records:

| Observation | Fixed kernel |
| --- | ---: |
| A's fence while still unpublished | 0 |
| B's published and completed fence | 202 |
| `prematurely-complete` | 0 |
| A's subsequently published fence | 203 |
| Watermark immediately after A's publication | 202 |

The helper's zero exit covers both ordering assertions and context cleanup.
The pre-test counters showed zero GPU failures, timeouts and context failures,
329 async posts and retirements, and zero pending commands. After the test,
kernel output interleaved around the fresh `root:/#` prompt. An end-of-output
anchor in the prompt parser missed it even though the emitted completion
token and prompt were both present. The owned controller timed out and reaped
QEMU safely; root independently confirmed exact zero. The redirected result
file and post-test counters were not collected before overlay removal, so
they are not claimed as retained evidence. The correction accepts a fresh
prompt occurrence after the unique emitted token, including trailing kernel
output, while retaining missing-token, missing-prompt and echoed-token
negative checks.

This establishes the publication-order correction with a failing baseline
and passing candidate. It does not demonstrate real host-hang recovery.

## Graphical recovery boundary

The subsequent [GUI audit](chromium-gl-recovery-audit-20260907.md) runs the same
fixed kernel with the probe explicitly disabled. Mouse-triggered loss and
restoration work for both WebGL versions: each rebuilds its resources,
reaches generation 2 and passes pixel checks without unexpected context loss.

Sustained animation still fails. The first profile's soak fails after about twelve
seconds, a separate host-focus recovery attempt fails after about six seconds,
and a fresh second profile also fails after about six seconds. Timers and result
delivery continue; the first failure becomes visible on screen. Giving the
host window focus and restarting Chromium therefore do not establish
sustained animation recovery. The first browser exits cleanly and the new
profile passes initial drawing/readback before its animation failure.
The fixture ends each trial after its five-second progress bound is exceeded;
these failures do not measure the natural pause duration or autonomous recovery.

Final captured GPU counters show zero timeouts, failures and failed contexts,
3,106 posts and retirements, and zero pending work. This run does not establish
a virgl queue hang. The remaining investigation should correlate Chromium's
BeginFrame/Wayland frame callbacks and event delivery with the observed
animation pause, rather than treating the corrected fence-order defect as
its proven cause. See the GUI audit for control coverage, logs and cleanup.

To retain at most three generated kernel iterations, the oldest unused
pre-audio-fix kernel binary, ELF and symbol file were retired before this
candidate build. Their hashes remain in the [retention manifest][retention].
The audio-fixed rollback, private root filesystem and all audit logs remain.

## Remaining recovery work

The synchronous timeout path protects stale descriptor reuse, but subsequent
callers can still overwrite the shared command page before the stale-command
check. Caller-owned payload lifetime also needs review after a synchronous
timeout. This separate DMA-lifetime concern is not changed here and has not
been established as the trigger for the observed animation pause.

Intentional WebGL loss/restoration exercises browser resource rebuilding; it
does not prove recovery from a hung host renderer. Historical DESK-02 and
long-run Chromium recovery remain open until their own acceptance evidence
is collected.

[baseline-build]: ../build-x86_64/gui-progress-audit/virgl-fence-baseline-build-20260907T192302Z/
[baseline-run]: ../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T192431Z/
[candidate-build]: ../build-x86_64/gui-progress-audit/virgl-fence-candidate-build-20260907T193107Z/
[candidate-run]: ../build-x86_64/gui-progress-audit/chromium-gl-fence-20260907T193259Z/
[retention]: ../build-x86_64/gui-progress-audit/audio-passcred-baseline-20260907T182610Z/kernel-artifact-retention.json
