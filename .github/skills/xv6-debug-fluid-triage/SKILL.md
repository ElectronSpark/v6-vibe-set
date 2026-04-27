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

## Evidence Labels

- **Observed**: directly captured from current source/runtime.
- **Inferred**: strongly implied by source and captures, but not directly observed.
- **Hypothesis**: plausible explanation waiting for validation.
- **Deprecated**: a previous explanation contradicted by newer source or captures.

## Pitfalls

- Do not let a plausible theory harden into documentation without a matching capture or source path.
- Do not mix captures from old and new QEMU sessions after rebuilding.
- Do not patch multiple unrelated theories at once unless the user explicitly asks for a broad experiment.
