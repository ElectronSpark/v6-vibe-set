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

## Submodule Commit Rule

- Commit changed submodules first, then commit the parent repo so the parent records real submodule SHAs.
- Push submodule branches before pushing the parent branch, otherwise a fresh clone of the parent can point at unavailable commits.
- For this workspace, common branches are `kernel` on `v6-kernel`, `ports` on `v6-port`, and top-level `/home/es/xv6-os` on `main`.

## Pitfalls

- A parent commit with unpublished submodule SHAs is not reproducible for other clones.
- A successful base Docker image build does not prove the full image target can build.
- Toolchain phase stamps are build-graph facts, not just cosmetic files.
