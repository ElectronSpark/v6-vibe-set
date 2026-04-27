---
name: xv6-debug-fluid-triage
description: 'Use when: debugging xv6-os with incomplete evidence, evolving hypotheses, uncertain freeze causes, provisional diagnostics, contradictory captures, or fast-changing runtime symptoms that are not ready to become source-derived ground truth.'
argument-hint: 'Describe the symptom, latest capture, and current uncertainty'
---

# xv6 Fluid Debug Triage

## Fluidity Notice

This skill is intentionally provisional. It is not ground truth, may be incomplete, and can become outdated or deprecated without notice as new captures, source changes, or better subsystem skills replace it. Prefer current source code and validated subsystem skills whenever they conflict with this document.

## When to Use

- The symptom is real but the root cause is still moving.
- Captures disagree, are stale, or were taken from different kernels/images.
- You need a safe place to track hypotheses before promoting a finding into a module skill.
- A freeze, crash, or regression crosses several subsystems and no single skill owns it yet.

## Workflow

1. State what is known, what is suspected, and what is merely a working theory.
2. Record the exact runtime being observed: branch, commit, submodule SHAs, kernel timestamp, image timestamp, QEMU command, KVM setting, and whether GDB is attached.
3. Capture evidence before editing code. For freezes, prefer `xv6-freeze`, `xv6-syscall <name>`, `xv6-kqueue <name>`, and targeted subsystem dumps.
4. Separate stale evidence from fresh evidence. If QEMU was started before a rebuild, discard that capture for validating the new kernel.
5. Convert one hypothesis into one small test or patch, then retest with a fresh VM when the kernel or image changed.
6. Promote stable conclusions into the relevant source-derived skill only after the behavior is reproduced or explained by current source.

## Methodology

- Keep a hypothesis ledger: one line for evidence, one line for interpretation, one line for the next test.
- Prefer falsifiable questions over broad explanations. Example: ask whether `wlcomp` is blocked in a syscall before deciding the compositor is frozen.
- Change only one layer per experiment when possible: kernel wait path, generated compositor, QEMU launch flags, image/rootfs, or toolchain/build graph.
- Treat a successful workaround as a diagnostic result first. Only promote it to a fix after explaining why it works.
- Re-run the smallest capture that can disprove the current theory before widening the search.
- When a theory crosses subsystems, name the handoff explicitly: producer, readiness notification, waiter, timeout, scheduler, or user-space consumer.

## Common Problems

- **Stale runtime**: QEMU is still running an older kernel or rootfs after a rebuild.
- **Mixed captures**: evidence from KVM and non-KVM runs, or from different submodule commits, is combined as if it came from one run.
- **Symptom tunneling**: the first visible symptom, such as cursor freeze, is mistaken for the failing subsystem without checking the event path.
- **Overfitting to one stack**: a backtrace from one CPU is treated as the whole system state.
- **Hidden timeout behavior**: timed waits show unclear channels and can masquerade as ordinary sleeps.
- **Patch pile-up**: several plausible fixes are applied together, making validation impossible.

## Evidence Labels

- **Observed**: directly captured from current source/runtime.
- **Inferred**: strongly implied by source and captures, but not directly observed.
- **Hypothesis**: plausible explanation waiting for validation.
- **Deprecated**: a previous explanation contradicted by newer source or captures.

## Pitfalls

- Do not let a plausible theory harden into documentation without a matching capture or source path.
- Do not mix captures from old and new QEMU sessions after rebuilding.
- Do not patch multiple unrelated theories at once unless the user explicitly asks for a broad experiment.
