---
name: xv6-debug-build-repro
description: 'Use when: debugging xv6-os fresh clones, Docker/container builds, copied prebuilt toolchains, CMake/Ninja stamp files, toolchain byproducts, submodule SHAs, clean build reproducibility, or build-vs-runtime mismatch.'
argument-hint: 'Describe the build environment and failing target'
---

# xv6 Build Repro Debugging

## Fluidity Notice

This skill is provisional. It records lessons from recent build and container experiments, not guaranteed project policy. It is not ground truth and can become deprecated without notice as CMake, Docker, or toolchain import paths are fixed.

## When to Use

- A fresh clone or container build fails differently from the working tree.
- A copied toolchain exists but CMake/Ninja still tries to rebuild GCC/binutils/musl.
- Submodule commits are local, dirty, or not pushed when the parent repo is committed.
- VS Code CMake Tools disagrees with the existing configured build tree.

## Workflow

1. Capture the exact build tree and generator before changing files:
   - source root, build root, generator, `XV6_ARCH`, `XV6_PARALLEL_JOBS`, and toolchain path.
2. For copied prebuilt toolchains, verify executable byproducts and CMake graph state together. A copied `toolchain/x86_64` directory alone may not satisfy target dependencies.
3. Inspect target stamp files and CMake complete files when trying to skip toolchain phases. Missing or mismatched stamps can cause `build_gcc_toolchain` to rerun.
4. Use dry runs when available:
   - `ninja -C <build> -n toolchain`
   - Check whether the dry run mentions toolchain build scripts.
5. For kernel-only validation in the existing workspace, prefer the known configured tree:
   - `cmake --build build-x86_64 --target kernel -j2`
6. If VS Code CMake Tools configure fails but the existing tree builds, record both facts; do not treat one as proof that the other path is broken.
7. To validate that all non-toolchain programs build in Docker without an outside repo, run the dev image and mount the local prebuilt toolchain read-only:
   - `docker run --rm --workdir /src/xv6-os -e XV6_SOURCE_DIR=/src/xv6-os -e XV6_BUILD_DIR=/src/xv6-os/build-x86_64-container-prebuilt -e XV6_ARCH=x86_64 -e XV6_PARALLEL_JOBS=2 -e XV6_PREBUILT_TOOLCHAIN_PREFIX=/opt/xv6-prebuilt-toolchain -v /home/es/xv6-os:/src/xv6-os -v /home/es/xv6-os/build-toolchain-x86_64:/opt/xv6-prebuilt-toolchain:ro xv6-os:dev-validate bash -lc 'cmake -S /src/xv6-os -B /src/xv6-os/build-x86_64-container-prebuilt -G Ninja -DXV6_ARCH=x86_64 -DXV6_PARALLEL_JOBS=2 -DXV6_PREBUILT_TOOLCHAIN_PREFIX=/opt/xv6-prebuilt-toolchain && cmake --build /src/xv6-os/build-x86_64-container-prebuilt --target toolchain -j2 && cmake --build /src/xv6-os/build-x86_64-container-prebuilt --target world -j2'`
   - After it finishes, verify `fs.img`, `kernel/kernel.elf`, `.ksymbols`, `.ksymbols_idx`, and expected staged programs such as `bin/wlcomp`, `bin/netsurf`, `bin/ssh`, and `bin/openssl`.
   - Confirm the build tree did not create `build-x86_64-container-prebuilt/toolchain`, proving the toolchain phase was skipped rather than rebuilt.

## Methodology

- Debug reproducibility as a dependency graph problem, not a file-copy problem. Check targets, byproducts, stamps, cache variables, and submodule SHAs together.
- Compare dirty working-tree builds against a clean clone by naming every intentional difference: copied toolchain, preseeded stamps, local submodule commit, Docker image, and generator.
- Use dry runs before expensive builds to see what the graph believes is missing.
- Keep host and container evidence separate. A path that exists on the host may be absent, mounted differently, or cached differently inside Docker.
- When skipping a build phase, prove both sides: the final executable byproducts exist and the build graph no longer schedules that phase.
- Commit and push submodules before treating a top-level commit as reproducible.

## Common Problems

- **Toolchain directory fallacy**: copied compiler binaries exist, but Ninja still rebuilds because stamps or expected byproducts are missing.
- **Cache mismatch**: CMake was configured before the copied toolchain or with a different build root.
- **Generator mismatch**: Makefile and Ninja build trees are compared without accounting for different target graphs.
- **Submodule invisibility**: the parent points at commits that have not been pushed from `kernel` or `ports`.
- **Partial Docker success**: the base image builds, but the image/rootfs target fails later in project-specific phases.
- **Build/runtime mismatch**: a successful build is tested against an older running QEMU session.
- **Stale rootfs image**: sysroot binaries can be newer than `/bin/*` inside `build-x86_64/fs.img`; verify with `debugfs` or dump the image binary and run `strings` before trusting a runtime test.
- **Optional WebKit runtime**: `ports/webkit` stages MiniBrowser/WebKit only from an explicit `XV6_WEBKIT_REF_SYSROOT` CMake/env setting, or from the repo-local `ports/webkit/sysroot` if populated. Fresh clones without a WebKit runtime skip staging and remove stale WebKit files; `scripts/make-rootfs.sh` must copy `libexec/` as well as `lib/` when a runtime is present, because MiniBrowser and the WebKit helper processes live under `/libexec/webkit2gtk-4.1`. When intentionally testing a reference runtime, the environment variable should override a stale cached CMake value.
- **No external WebKit dependency**: container builds should not rely on `/home/es/xv6/xv6-tmp` or any host-only WebKit sysroot. A cache value of `/src/xv6-os/ports/webkit/sysroot` is repo-local; if that directory is absent or lacks WebKitGTK, `port-webkit` should complete by skipping runtime staging.

## Submodule Commit Rule

- Commit changed submodules first, then commit the parent repo so the parent records real submodule SHAs.
- Push submodule branches before pushing the parent branch, otherwise a fresh clone of the parent can point at unavailable commits.
- For this workspace, common branches are `kernel` on `v6-kernel`, `ports` on `v6-port`, and top-level `/home/es/xv6-os` on `main`.

## Pitfalls

- A parent commit with unpublished submodule SHAs is not reproducible for other clones.
- A successful base Docker image build does not prove the full image target can build.
- Toolchain phase stamps are build-graph facts, not just cosmetic files.
