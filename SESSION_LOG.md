# Session log — bring-up of xv6-os umbrella (x86_64)

Session date: 2026-04-21
Working tree at session end: `/home/es/xv6-os/` (moved out of `/home/es/xv6fs-kdriver/xv6-os/`).

## Goal arc

1. **Phase 0 toolchain refactor** — rewrote `cmake/BuildToolchain.cmake` to drive
   the monolithic `toolchain/scripts/build_gcc_toolchain.sh` through two phases
   (Phase 1 static, Phase 2 dynamic-capable) instead of the obsolete per-stage
   script names. Committed as umbrella commit (later reset by repo init).
2. **x86_64 first** — user wanted to see GUI boot.
3. **Track umbrella under git** + clean-rebuild verification.

## Cross toolchain

- GCC 14.2.0, binutils 2.43, musl 1.2.5
- Triple: `x86_64-xv6-linux-musl`
- Built into: `/home/es/xv6-os/build-toolchain-x86_64/x86_64/phase{1,2}/bin/`
- Wall time: ~40 minutes on 16 cores
- Disk: ~18 GB

## Kernel build

- Standalone CMake, freestanding
- `ARCH=x86_64`, `PLATFORM=qemu`, `OPT_LEVEL=2`
- Multiboot entry `0x100000`, page offset `0xFFFF800000000000`
- Uses lwext4 + lwip
- Output: `build-x86_64/kernel/kernel/kernel` (~11 MB ELF)

### Loose end fixed
`kernel/scripts/` (gen_asm_offsets.py, gen_kernel_ld.py, gen_addrline.sh,
estimate_symbol_size.py, gen_ksymbols_placeholder.py, embed_ksymbols.py,
README_gen_asm_offsets.md) was missing on a fresh checkout — copied from the
upstream xv6-tmp tree and committed (kernel `cb209b6`).

## User build (62 → 60 programs)

Wired up in `user/CMakeLists.txt`:

- Auto-detect `KERNEL_DIR` (sibling)
- `-isystem $(gcc -print-file-name=include)` for freestanding stdarg.h etc.
  while keeping `-nostdinc`
- Include path: `-I${USER_LIB_DIR} -I${_user_parent} -I${KERNEL_DIR}
  -I${KERNEL_DIR}/kernel/inc`
- `add_compile_definitions(CONFIG_ARCH_X86_64=1)` for `struct stat` arch select
- Skip list: `pngtest` (needs libpng port), `timerdemo` (needs musl
  clock_gettime). Filter loop with `XV6_SKIP_PROGRAMS` cache var.
- Symlink `user/user.h` → `lib/user.h`
- Copied `user/lib/fsutil.h` from xv6-tmp.

Committed as user `558ad0c`.

### x86_64 user start() ABI fix (user `3b42d6b`)

Symptom: `init: starting sh` →
`pid 1 init: exception 13 (#GP General Protection) rip=0x16e0` — `movaps`
against a stack slot on entry to `fork()`.

Root cause: SysV AMD64 ABI requires RSP ≡ 8 (mod 16) on entry to a function so
that after the callee's prologue (`push %rbp`) the stack is 16-byte aligned for
SSE. The C wrapper in `ulib.c` did `push %rbp; call main`, leaving RSP
16-byte aligned at entry to `main()`, breaking compiler-emitted `movaps`.

Fix: naked-asm trampoline in `user/lib/ulib.c`:

```c
#if defined(CONFIG_ARCH_X86_64)
extern int main(int, char **);
__attribute__((naked, noreturn)) void start(int argc, char *argv[]) {
    __asm__ volatile(
        "xorl  %%ebp, %%ebp\n\t"
        "andq  $-16, %%rsp\n\t"   // align to 16
        "callq main\n\t"          // pushes 8 → main sees RSP ≡ 8 (mod 16)
        "movl  %%eax, %%edi\n\t"
        "callq exit\n\t"
        "ud2\n\t"
        ::: "memory");
}
#else
void start(int argc, char *argv[]) {
    extern int main(int,char**); main(argc,argv); exit(0);
}
#endif
```

## Boot model

Kernel reads root from **virtio-blk PCI**, not from a cpio initrd:

```
qemu-system-x86_64 -machine pc -cpu qemu64,+pcid -smp 2 -m 256M \
    -display gtk -serial stdio \
    -kernel build-x86_64/kernel/kernel/kernel \
    -drive file=build-x86_64/fs.img,if=none,format=raw,id=x0 \
    -device virtio-blk-pci,drive=x0 \
    -append root=/dev/disk0
```

`fs.img` is an ext4 image labeled `xv6root`, built by
`scripts/make-rootfs.sh` (committed). It stages the cross-installed
sysroot's `bin/_<name>` binaries (stripping the `_` prefix per xv6
convention) into a 64 MiB image via `mkfs.ext4 -d`.

Old `scripts/make-initrd.sh` and `scripts/make-image.sh` still target
the wrong (cpio) shape — left in place but unused.

## Boot result

Reaches `0:/#` shell with full network stack:

```
init: starting sh
0:/# lwip: DHCP lease acquired
     lwip: netif up — IP 10.0.2.15
     tftpd: server started on port 69 (root: /)
     iperfd: server started on port 5001
     sntpd: client started (polling pool.ntp.org)
     mdnsd: responder started (xv6.local)
     gdbstub: listening on port 2159
     sntpd: sync #1 — NTP <ts>.<frac>  RTC drift +<n> ms
```

Verified across a full from-scratch rebuild of `build-x86_64/`
(toolchain reused).

## Git layout at session end

Five independent git repositories:

| Path | Role | HEAD at session end |
|---|---|---|
| `/home/es/xv6fs-kdriver/` | unrelated Linux kdriver — has `/xv6-os/` in `.gitignore` | `2850f4a` |
| `/home/es/xv6-os/` | umbrella, pins sub-repos as 160000 gitlinks | `8b8a9d2` |
| `/home/es/xv6-os/toolchain/` | cross-toolchain build scripts | `a8ef295` |
| `/home/es/xv6-os/kernel/` | kernel sources + scripts/ | `f9e4dfb` |
| `/home/es/xv6-os/user/` | xv6-native userland | `3b42d6b` |
| `/home/es/xv6-os/ports/` | musl-linked third-party ports | `78e96cd` |

Umbrella commit history:
```
8b8a9d2 bump kernel: f9e4dfb (add .gitignore)
690d504 track sub-repositories as gitlinks
c781776 scripts: add make-rootfs.sh — ext4 root image builder
b86fcb0 initial commit: umbrella build system
```

To graduate gitlinks to true git submodules later: fill remote URLs into
`.gitmodules.template` and run `scripts/setup-submodules.sh`.

## Reproduce a clean build

```bash
cd /home/es/xv6-os
rm -rf build-x86_64
mkdir -p build-x86_64/{kernel,user,host,sysroot}

# Kernel
PATH=$PWD/build-toolchain-x86_64/x86_64/phase2/bin:$PATH \
TOOLPREFIX=$PWD/build-toolchain-x86_64/x86_64/phase2/bin/x86_64-xv6-linux-musl- \
cmake -S kernel -B build-x86_64/kernel -DARCH=x86_64 -DPLATFORM=qemu -DOPT_LEVEL=2
PATH=$PWD/build-toolchain-x86_64/x86_64/phase2/bin:$PATH \
cmake --build build-x86_64/kernel -j16

# User
PATH=$PWD/build-toolchain-x86_64/x86_64/phase2/bin:$PATH \
TOOLPREFIX=$PWD/build-toolchain-x86_64/x86_64/phase2/bin/x86_64-xv6-linux-musl- \
cmake -S user -B build-x86_64/user -DARCH=x86_64 -DOPT_LEVEL=2 \
    -DCMAKE_INSTALL_PREFIX=$PWD/build-x86_64/sysroot
PATH=$PWD/build-toolchain-x86_64/x86_64/phase2/bin:$PATH \
cmake --build build-x86_64/user -j16 --target install

# Image (host mkfs.ext4)
bash scripts/make-rootfs.sh build-x86_64/sysroot build-x86_64/fs.img 64

# Boot
bash scripts/run-qemu.sh x86_64 build-x86_64/kernel/kernel/kernel build-x86_64/fs.img
```

## Outstanding / next session

## 2026-04-22 — CPython 3.12 + Flask + SQLite reachable from host

### Result

The xv6 shell can now run `python /app.py` and serve a Flask app on
0.0.0.0:8080. With QEMU host-forwarding, `curl http://127.0.0.1:18080/`
from the host returns:

```
hello from xv6 / cpython 3.12.12+ / sqlite 3.52.1
```

`/api` returns a JSON document built from an in-memory SQLite database
through SQLAlchemy / `_sqlite3.so`.

The 17-import probe (`/test_flask.py`) passes for: `encodings, _socket,
select, _ssl, _hashlib, ctypes, sqlite3, _sqlite3, jinja2, markupsafe,
werkzeug, click, blinker, itsdangerous, flask, sqlalchemy,
flask_sqlalchemy`.

### How

Pragmatic stage-and-mirror, not a from-scratch port — the reference
xv6-tmp tree was built with the same `x86_64-xv6-linux-musl` triple, so
its prebuilt artefacts are ABI-compatible with the umbrella toolchain.

1. **`scripts/stage-cpython.sh`** (new) copies into `build-x86_64/sysroot`:
   - `lib/libpython3.12.so.1.0`, `libpython3.so`, `libgcc_s.so.1`,
     `libreadline.so.8.2`, `libncurses.so.6.4`, `libstdc++.so.6.0.33`,
     `libffi.so.7.1.0` (+ canonical SONAME symlinks).
   - `lib/python3.12/{lib-dynload,site-packages}` from the reference sysroot
     (Flask, SQLAlchemy, Werkzeug, Jinja2, Click, Blinker, ItsDangerous,
     MarkupSafe, NumPy, etc.) minus `*-312d-*.so`, `__pycache__`,
     `test/`, `idlelib/`, `turtledemo/`.
   - **stdlib `*.py` from `xv6-tmp/user/v6-cpython/Lib/`** — the reference
     sysroot ships only `lib-dynload`+`site-packages`, the pure-python
     stdlib lives in the cpython source tree. Override with `CPYTHON_LIB=…`.
   - `bin/python3.12` and `bin/python` symlink.
   - `share/terminfo/` so readline behaves at the prompt.
   - `lib/libc.so` and `lib/ld-musl-x86_64.so.1` from our phase-2 musl.

2. **`scripts/make-rootfs.sh`** extended:
   - Now also rsyncs `lib/`, `usr/`, `share/`, `etc/` from the sysroot
     into the staging tree, plus copies any top-level files (e.g.
     `/app.py`, `/test_flask.py`, `/diag.py`).
   - Image bumped to 256 MiB (`bash scripts/make-rootfs.sh
     build-x86_64/sysroot build-x86_64/fs.img 256`).

3. **`user/programs/sh/sh.c`** — `env_init()` now sets:
   - `PYTHONHOME=/`
   - `PYTHONPATH=/lib/python3.12:/lib/python3.12/site-packages`

   The reference build had used `/usr/local`; we install at `/`.

4. **`kernel/arch/x86_64/mm/vm.c`** — silenced the per-page
   `freewalk: LEAK va=… pte=… pa=…` printf inside `__freewalk()`.
   Each `python` exit produced ~2700 of those lines and completely
   masked userspace stdout. The summary
   `freewalk: WARNING: N leaked PTE(s) (pid X name)` from
   `freewalk()` is kept.

5. **`scripts/run-qemu.sh`** — added an explicit user-mode netdev with
   hostfwd so the guest is reachable from the host:
   ```
   -netdev user,id=n0,hostfwd=tcp::18080-:8080,hostfwd=tcp::15001-:5001
   -device e1000,netdev=n0
   ```
   Override the forwards via `HOSTFWD=…`.

### Demo recipe

```bash
# one-time (after toolchain + kernel + user are built):
bash scripts/stage-cpython.sh
bash scripts/make-rootfs.sh build-x86_64/sysroot build-x86_64/fs.img 256

# boot
DISPLAY_MODE=nographic \
  bash scripts/run-qemu.sh x86_64 \
       build-x86_64/kernel/kernel/kernel build-x86_64/fs.img

# inside xv6 shell:
0:/# python /app.py &

# from host:
curl http://127.0.0.1:18080/      # => hello from xv6 / cpython … / sqlite …
curl http://127.0.0.1:18080/api   # => [{"k":"a","v":1},...]
```

## Outstanding / next session

- **Proper `ports/cpython` recipe** — the staging shortcut depends on the
  external xv6-tmp tree. Convert to a real `ports/cpython/CMakeLists.txt`
  that configures CPython 3.12 against our sysroot (`--prefix=/`,
  `--with-system-ffi`, `--with-openssl=…`) and installs into
  `build-x86_64/sysroot/`.
- **`ports/openssl`, `ports/sqlite`, `ports/zlib` (already started),
  `ports/ncurses`, `ports/readline`, `ports/libffi`** — same pattern,
  needed once cpython is built from source.
- Run the cpython test suite under xv6 to find latent kernel bugs
  (the `freewalk: LEAK` warnings still fire — they are real PTE leaks
  in `exec()` cleanup paths).
- **RISC-V toolchain + boot** — not started.
- Wire `scripts/make-rootfs.sh` into `cmake/BuildImage.cmake` so the
  `image` target produces fs.img automatically.
- Decide fate of `scripts/make-initrd.sh` and `scripts/make-image.sh`.
- Move host `mkfs.xv6fs` build out of the kernel tree.
