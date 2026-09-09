---
name: xv6-debug-build-repro
description: 'Use when: debugging xv6-os fresh clones, Docker/container builds, CMake/Ninja dependencies, nested submodule identity, clean build reproducibility, or build-versus-runtime mismatches. Covers the current x86_64 host-glibc/KDE image and distinguishing legacy cross-toolchain artifacts.'
argument-hint: 'Describe the build environment and failing target'
---
# xv6 Build and Reproduction Debugging

Establish which source produced the failing artifact before rebuilding. A build
log, current checkout and running image can describe different states. Use
[the active plan](../../../docs/active-work-plan.md) for current runtime policy.

## Workflow

1. Preserve existing work. Record source root, branch/commit, dirty state, nested
   submodule SHAs and dirty state, build directory, generator, `XV6_ARCH`, compiler,
   relevant cache values, container image identity and job limit. Do not reset,
   clean, commit, or publish merely to simplify a reproduction.
2. Apply root `AGENTS.md`: guarded `/home/es/.local/bin/rg`, no banned recursive
   options or generated-tree content searches, explicit eligible artifact files
   through `scripts/audit/safe-rg-artifact.sh`, and the global search lock. Wait
   synchronously for each exact build/tool handle before new searches or heavy
   commands. Inspect only the cache/log/stamp files needed for this failure.
3. Check current first-party owners: `CMakeLists.txt`, `cmake/`,
   `scripts/build/reproduce-in-container.sh`,
   `scripts/build/validate-reproduction.sh`, and
   `scripts/container/reproduce-workspace.sh`. Current x86_64 userland uses the
   host compiler/glibc and staged KDE/browser runtime; old custom-musl toolchain,
   `wlcomp`, and userland override recipes are not current build requirements.
4. Choose the smallest action within the user's scope. Read-only diagnosis does
   not require a build. For an authorized kernel-only change in an already
   configured matching tree, use `cmake --build build-x86_64 --target kernel -j2`.
   `rootfs-refresh` refreshes the image from the existing sysroot; it does not
   rebuild changed userland. The rootfs implementation is
   `scripts/image/make-rootfs.sh`. Inspect dependencies before choosing `image`.
5. For an explicitly requested clean full-workspace reproduction, the host
   entrypoint is `scripts/container/reproduce-workspace.sh`. It rebuilds the dev
   container, replaces the mutable build tree after ownership checks, stages the
   current image, validates it, and publishes an immutable local receipt under
   `build-reproductions/x86_64/`. This is a substantial rebuild, not a harmless
   diagnostic or a remote publish. Read its current cleanup scope before use.
6. Preserve at most the three newest completed generated iterations. Before a
   fourth, remove only the oldest confirmed-unused candidate through the owned
   workflow. Active disks, Docker data, deployed runtime images, source and the
   newest known-good rollback are never automatic cleanup candidates. Never
   bypass locks or host process-ownership checks from inside a container.
7. Retain the actual commands, exit status, source/dirty-state manifest and
   artifact hashes. Distinguish clean-clone reproducibility from reproducing a
   dirty bind-mounted workspace. A timestamp alone cannot bind symbols, kernel
   and image; launch only the matching receipt when runtime validation is within
   scope. An old VM can document old behavior but cannot validate a new kernel.

## Dependency and submodule pitfalls

- CMake/Ninja build edges depend on declared outputs/byproducts and stamp state.
  A copied executable alone does not satisfy a missing dependency. On a legacy
  branch with a custom toolchain target, inspect its declared byproducts and use
  `ninja -C <matching-build> -n toolchain` to inspect scheduling; do not assume
  that target exists in the current x86_64 build or forge stamps to hide failure.
- Check the current failing target before reacting to a stale VS Code task or
  cached error. Rootfs-only staging, kernel compilation and a full image build
  establish different things. Do not use successful staging as proof of ABI
  or graphical runtime behavior.
- A top-level submodule SHA may be unreachable from a fresh clone. Record that
  limitation and check the relevant remote/ref without publishing changes.
  When commits/pushes are already authorized, publish from the deepest changed
  submodule outward so parent pointers reference available commits; inspect
  actual branches/remotes rather than assuming historical branch names.
- Optional reference sysroots are explicit inputs. On matching legacy WebKit
  work, propagate `XV6_WEBKIT_REF_SYSROOT` consistently through environment,
  cache and nested configure; do not depend on a developer's absolute path.
  `ports/webkit/sysroot` is a staged runtime input when used, not an automatic
  cleanup candidate. Do not revive retired userland patches as a build shortcut.

For runtime work follow [GUI runtime](../xv6-debug-gui-runtime/SKILL.md).
The VM owner must verify exact zero QEMU before dispatch, use token/PID-owned
launch and bounded cleanup, synchronously reap, and verify exact zero afterward;
all other lanes are NO-BOOT. Avoid `pgrep` self-matches. If serial inspection is
needed, use short marker commands, handle first-character drop, and wait for the
actual command and fresh prompt. Build completion alone is not VM authorization.
