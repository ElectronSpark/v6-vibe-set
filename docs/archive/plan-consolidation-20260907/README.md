# Original plans archived on 2026-09-07

All active work is consolidated in [the work plan](../../active-work-plan.md).
These 15 documents are historical snapshots, not competing active plans.
Each snapshot preserves the original bytes and repository-relative directory
layout. [manifest.json](manifest.json) records original paths, SHA-256 hashes,
byte lengths and the owning repository HEAD at consolidation. No runtime
validation was performed by this archival operation.

Use the table to find the active workstream or recover any original checklist,
acceptance criterion, experiment, command or receipt. Relative links and
commands inside snapshots retain their original checkout context; they are
historical text and may name moved files, obsolete workflows or deleted build
artifacts. Follow the active plan and current AGENTS.md for new work.

| Original repository path | Unchanged snapshot | Active workstream |
| --- | --- | --- |
| `docs/active-work-plan.md` | [Original](docs/active-work-plan.md) | [desktop and youtube](../../active-work-plan.md#desktop-and-youtube) |
| `docs/alpine-virgl-desktop-handoff-plan.md` | [Original](docs/alpine-virgl-desktop-handoff-plan.md) | [drm gpu and browser coverage](../../active-work-plan.md#drm-gpu-and-browser-coverage) |
| `docs/linux-abi-compat-plan.md` | [Original](docs/linux-abi-compat-plan.md) | [linux abi and application compatibility](../../active-work-plan.md#linux-abi-and-application-compatibility) |
| `docs/linux-drm-abi-compat-plan.md` | [Original](docs/linux-drm-abi-compat-plan.md) | [drm gpu and browser coverage](../../active-work-plan.md#drm-gpu-and-browser-coverage) |
| `docs/linux-userland-abi-kernel-gap-plan.md` | [Original](docs/linux-userland-abi-kernel-gap-plan.md) | [linux abi and application compatibility](../../active-work-plan.md#linux-abi-and-application-compatibility) |
| `docs/linux-userland-upstream-depatch-plan.md` | [Original](docs/linux-userland-upstream-depatch-plan.md) | [upstream source cleanup and port migration](../../active-work-plan.md#upstream-source-cleanup-and-port-migration) |
| `docs/linux-vm-abi-compat-plan.md` | [Original](docs/linux-vm-abi-compat-plan.md) | [vm latency and lifetime](../../active-work-plan.md#vm-latency-and-lifetime) |
| `docs/linux-drm-abi-impl-steps.md` | [Original](docs/linux-drm-abi-impl-steps.md) | [drm gpu and browser coverage](../../active-work-plan.md#drm-gpu-and-browser-coverage) |
| `GPU_REMAINING_GAPS.md` | [Original](GPU_REMAINING_GAPS.md) | [drm gpu and browser coverage](../../active-work-plan.md#drm-gpu-and-browser-coverage) |
| `.github/skills/xv6-debug-gui-runtime/GPU_OPENGL_PLAN.md` | [Original](.github/skills/xv6-debug-gui-runtime/GPU_OPENGL_PLAN.md) | [drm gpu and browser coverage](../../active-work-plan.md#drm-gpu-and-browser-coverage) |
| `.github/skills/xv6-debug-gui-runtime/WEBKIT_TODO.md` | [Original](.github/skills/xv6-debug-gui-runtime/WEBKIT_TODO.md) | [drm gpu and browser coverage](../../active-work-plan.md#drm-gpu-and-browser-coverage) |
| `MIGRATION.md` | [Original](MIGRATION.md) | [upstream source cleanup and port migration](../../active-work-plan.md#upstream-source-cleanup-and-port-migration) |
| `ports/MIGRATION.md` | [Original](ports/MIGRATION.md) | [upstream source cleanup and port migration](../../active-work-plan.md#upstream-source-cleanup-and-port-migration) |
| `kernel/LWIP_PERF_PLAN.md` | [Original](kernel/LWIP_PERF_PLAN.md) | [network performance](../../active-work-plan.md#network-performance) |
| `kernel/kernel/vfs/VFS_DATA_IO_TODO.md` | [Original](kernel/kernel/vfs/VFS_DATA_IO_TODO.md) | [vfs data io](../../active-work-plan.md#vfs-data-io) |

## Scope and precedence

The snapshots cover the seven docs plans, DRM implementation companion,
GPU/WebKit plans, both migration roadmaps, kernel networking plan and kernel
VFS checklist. Upstream vendor files, generated ABI audits, reviewed inventory
TSVs, reference reports, reusable skills and older archives remain in place.
The kernel and ports snapshots originated in independent subrepositories;
the manifest preserves their respective source revisions.

The consolidated plan resolves stale musl, custom-compositor and launch-policy
queues, distinguishes historical passing receipts from later regressions,
and preserves unsupported/conditional hardware work without granting it
completion credit. It does not claim that archival review revalidated code.
