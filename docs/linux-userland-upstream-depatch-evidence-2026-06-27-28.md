# Linux Userland Upstream De-Patching Evidence, 2026-06-27 To 2026-06-28

This archive preserves detailed source-hygiene proof that should not live in
the active upstream de-patching plan. The active plan remains the policy,
inventory, phase order, and validation checklist; this file records the
Wayland source/ref repair evidence.

## Wayland Source Drift Repair

`ports/wayland-src/src` had been pinned at local-only commit
`2f7f04d1e9fc85e4e29ce827abe4233e85d39367`, while the original fork
`https://github.com/ElectronSpark/v6-wayland.git` exposes `main` at
`25da99a7e5459bd9ad565d8a5d1e0e4857f4e5f7` and `v6-port` at
`3c5ad4f5716b0d536b06f908c6e7af465ae9d23f`.

The local `2f7f04d` tree was identical to fork `main` commit `25da99a`, so the
repair moved the gitlink to `25da99a` and set the `.gitmodules` branch to
`main`. This keeps the imported Wayland source upstream-clean while removing
the local commit-ahead mismatch against the original fork.

Post-repair evidence:

```text
git -C ports submodule status -- wayland-src/src
25da99a7e5459bd9ad565d8a5d1e0e4857f4e5f7 wayland-src/src (heads/main)
git -C ports/wayland-src/src diff --stat origin/main..HEAD
```

The diff against `origin/main` was empty.

## Ref Cleanup

The stale local branch names were aligned to the original fork refs so
branch-ahead checks do not report false drift. Local `main` now equals
`origin/main` at `25da99a`, and local `v6-port` now equals `origin/v6-port` at
`3c5ad4f`.

The previous local-only branch tips were moved out of the imported repo refs and
preserved in:

```text
build-x86_64/dependency-chain-audit/20260628T070836Z-wayland-src-backup-ref-retire/old-local-wayland-cleanup-refs.bundle
```

Branch-only proof shows both local branches at `0 0` ahead/behind and
`origin_branch_extra_commits=0`. A separate comparison against freedesktop
`upstream/main` still reports `6 0` because the fork snapshot predates the
latest upstream protocol-only commits. That is version freshness, while
fork-clean source proof uses `HEAD...origin/main` and
`v6-port...origin/v6-port`.

## Audit-Original Ref Repair

A later audit found `refs/remotes/audit-original/wayland-src/main` still pointed
at freedesktop `upstream/main` (`165504a`) rather than the original
ElectronSpark fork branch. That made
`HEAD...audit-original/wayland-src/main` report `0 6`.

The stale ref was realigned to `origin/main` (`25da99a`) with proof in:

```text
build-x86_64/dependency-chain-audit/20260628T082213Z-wayland-src-audit-original-ref-fix/
```

After the fix, `HEAD...audit-original/wayland-src/main` reports `0 0` and
`diff --stat HEAD..audit-original/wayland-src/main` is empty. Freedesktop
`upstream/main` remains available as the separate version-freshness ref.

## Later Rechecks

Current mismatch proof is preserved under:

```text
build-x86_64/dependency-chain-audit/20260628T125535Z-mismatch-fix-current-proof/
```

It confirms `wayland-src/src` still matches the original fork `main` and the
`audit-original` ref with no local ahead/behind drift. The parent `ports`
gitlink update remains a metadata change until committed; the imported source
checkout itself is clean.

Fresh recheck after the mismatch fix is preserved under:

```text
build-x86_64/dependency-chain-audit/20260628T143903Z-mismatch-fix-recheck/
```

It confirms `wayland-src/src` HEAD, `origin/main`, and
`audit-original/wayland-src/main` all resolve to `25da99a` with the same tree
hash and `0 0` ahead/behind counts. The only Wayland-related delta is the
parent metadata that changes the submodule branch from `v6-port` to `main` and
records the fork-clean gitlink.
