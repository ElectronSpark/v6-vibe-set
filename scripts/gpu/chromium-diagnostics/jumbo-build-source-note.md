# Pinned jumbo / V8 cluster build audit

Status: **historical; browser build stopped and cleanup completed**. The user
requires the unchanged host-copied browser and a kernel fix. This note does
not authorize a Chromium build or configuration change; current work is the
[kernel workqueue investigation](../../../docs/chromium-kernel-workqueue-investigation-20260909.md).

This records a read-only source audit for Chromium `151.0.7922.34`, root commit
`782af9cb30a53f54487e5d2e44738645a8ec457c`. Its DEPS pins V8 to
`f479186c16abdb6fa05539fe957bb84deee830df`, matching the inspected V8 checkout.
No GN query, generation, build, flag change, or runtime test was performed.
The contemporaneous recommendation to retain the then-running build was
superseded by the user's stop/cleanup direction. This revision has a supported
V8-only cluster mode, disabled by default. It does not combine the inspected
Blink targets, and several expensive V8 files are explicitly excluded. The
source does not establish a net completion-time benefit from changing the
graph after substantial individual compilation has already occurred.

## Support and defaults

`build/config/jumbo.gni` is absent both on disk and from the pinned root commit
(`git cat-file -e HEAD:build/config/jumbo.gni` reports that the path does not
exist). The inspected `build/config/BUILDCONFIG.gn` library/source-set wrappers
and Blink core/platform templates forward source lists to ordinary targets;
they contain no source-grouping step. An old Chromium-wide `use_jumbo_build`
recipe is therefore not supported by these inspected paths.

V8 imports `gni/cluster_build.gni` at `v8/BUILD.gn:19`.
`v8/gni/v8.gni:68` declares `v8_enable_cluster_build = false`. The retained
final `receipts/args.gn` has no override for this flag or `cluster_size`.
This is a source-default and explicit-argument determination, not a fresh
evaluated-GN query.

`v8/gni/cluster_build.gni:7` sets `cluster_size = 25`, with size **5** for paths
containing `src/compiler/turboshaft` or `src/maglev` (`:18`). Only eligible
`.cc` sources are combined; exact exclusions and non-`.cc` files remain
separate (`:44`). The generator groups by source directory, sorts filenames,
and writes synthetic `.cc` files containing sequential `#include` directives
(`v8/tools/cluster_files.py:82`, `:111`). This is genuine unity compilation,
not a cache setting or a limit on compiler job count.

## Coverage of expensive families

All paths below are relative to the pinned Chromium source checkout.

| Family / target | Source result |
| --- | --- |
| Blink core | `third_party/blink/renderer/core/core.gni:54` uses a normal static library for this noncomponent build and forwards the original sources. V8 clustering does not cover it. |
| Blink platform | `third_party/blink/renderer/platform/platform.gni:15` uses a normal source set and forwards the original sources. V8 clustering does not cover it. |
| V8 Torque CSA initializers and definitions | Cluster targets at `v8/BUILD.gn:2602` and `:2666`; generated TSA `.cc` files, when enabled, are explicitly excluded at `:2639`. |
| V8 handwritten builtins initializers | Cluster target at `v8/BUILD.gn:3277`. |
| V8 compiler and compiler for mksnapshot | Cluster targets at `v8/BUILD.gn:5761` and `:5800`; shared exclusion list at `:5743`. Turboshaft sources otherwise use groups of five. |
| V8 base / Maglev / heap / objects | Cluster target at `v8/BUILD.gn:5860`; exclusion list at `:5872`. Maglev sources otherwise use groups of five. |
| cppgc | Cluster target at `v8/BUILD.gn:7750`, with `concurrent-marker.cc` excluded at `:7850`. |

The compiler exclusion list is `turbolev-graph-builder.cc`,
`wasm-turboshaft-compiler.cc`, `bytecode-analysis.cc`, `heap-refs.cc`, and
`wasm-load-elimination.cc` under `src/compiler` (the first two are in its
`turboshaft` subdirectory). The first is explicitly labeled slow.

The base exclusion list includes `maglev-graph-builder.cc`, `maglev-ir.cc`,
`snapshot/deserializer.cc`, `wasm/wasm-module.cc`, `heap/mark-compact.cc`,
`objects/intl-objects.cc`, `objects/js-collator.cc`,
`objects/js-display-names.cc`, `objects/js-list-format.cc`,
`objects/js-number-format.cc`, `objects/js-temporal-objects.cc`,
`runtime/runtime-test-wasm.cc`, and `sandbox/external-pointer-table.cc`.
The source explains these exclusions as template-instantiation order issues,
size, or specific name collisions. A long individual compile in one of these
files would remain individual after enabling cluster mode.

## Costs and diagnostic limits

- The template replaces eligible individual source entries with generated
  cluster entries (`v8/gni/cluster_build.gni:175`). Enabling it would require
  compiling those new objects, including constituents whose individual objects
  are already complete. Excluded and unrelated targets may remain reusable;
  this does not imply a clean rebuild of all Chromium. No remaining-work or
  break-even calculation was performed.
- Combining translation units changes name/macro visibility and template
  instantiation context. The explicit exclusions document actual compatibility
  constraints. The generator also suppresses `-Wheader-hygiene` within cluster
  files (`v8/tools/cluster_files.py:117`); it does not suppress all diagnostics.
  Larger units can change per-job memory and scheduling behavior; neither was
  measured here.
- Changed translation-unit boundaries can change optimization and executable
  layout even with ThinLTO disabled. An observational C++ patch does not make
  this separate build-configuration change performance-neutral. Any future
  cluster build needs its own identity, compile/runtime validation and
  same-build OFF/ON controls before bottleneck comparisons.
- The comment at `cluster_build.gni:9` estimates an additional 3–5% build-speed
  improvement when increasing an already clustered configuration to 50 files,
  with roughly 10% worse minimum recompilation time for one changed `.cc`.
  Those are upstream comment estimates, not measured gains for this host,
  the stopped build, or switching from unclustered mode.

V8 clustering did not address the inspected Blink translation units, and this
source audit supplied no measured completion-time benefit. The downloaded
source and generated build outputs have since been removed; the source
findings above are historical and no browser build remains authorized.
