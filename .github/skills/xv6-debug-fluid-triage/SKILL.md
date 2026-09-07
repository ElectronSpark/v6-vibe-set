---
name: xv6-debug-fluid-triage
description: 'Use when: debugging xv6-os freezes, regressions, or evolving failures where runtime identity, partial observations, older workarounds and competing hypotheses must be separated before choosing a bounded next step.'
argument-hint: 'Describe the symptom, latest capture, and current uncertainty'
---
# xv6 Fluid Triage

Keep observations durable and hypotheses revisable. The current source and
[active plan](../../../docs/active-work-plan.md) supersede historical working
theories; neither proves what ran in a particular VM.

## Workflow

1. Write the requested outcome and scope: observation/audit, diagnosis, or an
   authorized implementation. A new failure during GUI exploration is a finding,
   not automatic authorization for fixes, rebuilds, reducers or altered launch
   policy. Continue independent work already within scope.
2. Bind evidence to UTC interval, source/submodule identity and dirty state,
   kernel/symbol/image hashes or receipt, actual QEMU arguments, application
   roles, and capture route. An existing image is not a clean-build receipt.
   Older evidence remains useful context but cannot validate later code.
3. Capture the failure before modifying its conditions. Preserve the ordinary
   launch path and the semantic action before/after, including unexpected UI
   outcomes. Reacquire coordinates after layout changes. Keep single-click,
   double-click and hover outcomes specific to each view/toolkit.
4. Label each claim:
   - **Observed:** directly supported by a retained capture, same-run log segment,
     process state or matched-source diagnostic.
   - **Inferred:** follows from identified evidence, with its assumptions stated.
   - **Hypothesis:** a proposed mechanism with a discriminating next observation.
   - **Deprecated:** an earlier theory contradicted or superseded by evidence.
5. Use a small hypothesis ledger: symptom, evidence for, evidence against, missing
   evidence and the smallest distinguishing check. Separate producer, waiter,
   consumer and visible output. For example, a mapped terminal does not prove its
   shell is ready; working editor input narrows but does not explain that failure.
6. Choose one next check within scope. An authorized live freeze capture follows
   [live GDB](../xv6-debug-live-gdb/SKILL.md), with the current process roles/PIDs.
   Do not assume the historical `wlcomp` selector exists. Source inspection can
   identify an owner without changing that owner or declaring a kernel cause.
7. If implementation is authorized, turn the supported hypothesis into a bounded
   fix and meaningful validation; bind the result to the new artifacts. Retest
   only what the change or remaining uncertainty requires. Otherwise record the
   follow-up in the active plan and finish the requested audit.

## Evidence boundaries

- Repeated execution samples establish sampled code progress, not completion,
  readiness, input delivery, or visible rendering. A framebuffer ioctl stack
  alone is not proof of acceleration or a healthy event loop. Idle waits can be
  correct; require a pending event and broken wake/consume transition to call a
  wait a deadlock.
- Inspect append/truncate behavior. A whole-file match in a guest log inherited
  from the base image is not fresh boot evidence. Keep a verified same-run byte
  boundary, producer identity or unambiguous start marker and the new segment.
- Window disappearance is not independently proven process exit. Match PID/TGID,
  process role, time and exit/assertion evidence before attribution. Duplicate
  event lines in two logs are one event. A nearby helper-thread kill or a
  software-rendering flag is not a proven cause of a browser disappearance.
- Successful input-helper return values, screenshot filenames, and one working
  hover path do not certify their intended semantic action. Preserve mixed hover
  evidence and failed guest-capture attempts instead of flattening them into a
  global pass/fail. Host-visible PNGs cannot replace a required independent
  guest/host pair or measured performance/audio evidence.
- Absence of matching fatal/stall markers is limited to inspected files and time.
  Simple UI success does not close long-stress, FPS, audio or media gates. Link
  one-off observations to audit documents rather than turning them into rules.

Apply root `AGENTS.md` throughout: guarded `/home/es/.local/bin/rg`, no banned
recursive options/generated-tree searches, explicit checked artifact files via
`scripts/audit/safe-rg-artifact.sh`, and the global search lock. Synchronously
wait on exact process/tool handles before new searches or heavy work. Keep serial
commands short with markers, handle first-character drop, and wait on the real
command plus a fresh prompt. Avoid `pgrep` self-matches. A VM lane requires a
single designated owner, exact zero-QEMU preflight, token/PID ownership, bounded
cleanup, synchronous reap and final exact zero; all other lanes are NO-BOOT.
