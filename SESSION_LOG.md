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

## 2026-04-25 - Cursor and file manager stabilization

The cursor regression was traced away from Wayland protocol handling and into
the guest input path plus GUI process startup. The first recovery pass forced
PS/2 relative mode to prove the compositor could consume `/dev/mouse`; after
checking the original working tree, absolute vmmouse mode is again the primary
path and PS/2 remains the fallback:

- The kernel mouse driver probes and enables the VMware absolute pointer path
  first, logging `PS2 mouse: VMware absolute pointer enabled` when available.
- `ps2mouse_poll_thread()` drains the shared i8042 output buffer every 2 ms for
  PS/2 fallback, and `vmmouse_poll_thread()` drains absolute-pointer data when
  vmmouse is active.
- `scripts/run-qemu.sh` uses `-display gtk,grab-on-hover=on` so GTK forwards
  motion as soon as the host pointer enters the VM window.
- Temporary raw/event mouse diagnostics were removed after validation; the
  boot log now keeps only normal startup lines such as absolute-pointer or
  PS/2 fallback selection and `/dev/mouse registered`.

The GUI startup issue was a separate trap: dynamic `desktop`/`wlcomp` could
fail or stall before the compositor printed useful logs. `ports/wayland` now
rebuilds and installs static `desktop` and static patched `wlcomp` every time
the port target runs. `readelf -d` shows no `NEEDED` entries for either staged
binary.

Validation:

- `cmake --build build-x86_64 --target kernel` succeeds after the mouse-driver
  cleanup.
- `cmake --build build-x86_64/ports --target port-wayland` rebuilds static GUI
  binaries; only pre-existing unused drawing-helper warnings remain.
- `scripts/make-rootfs.sh build-x86_64/sysroot build-x86_64/fs.img 1536 ...`
  repacks the image.
- Booting with `QEMU_EXTRA='-monitor unix:/tmp/xv6-hmp.sock,server,nowait'
  bash scripts/launch-gui.sh` reaches `[desktop]`, `wlcomp: listening on
  wayland-0`, and `wlcomp: entering main loop`.
- HMP `mouse_move 180 110` changes the framebuffer hash and the screenshot
  shows the cursor in the moved position on the desktop.

The internal file manager crash path was also tightened:

- `fm_read_dir()` uses direct `opendir()`/`readdir()`/`stat()` instead of
  forking `/bin/ls` and parsing its output.
- Directory navigation no longer calls `snprintf(w->title, ..., w->fm_path)`
  with source and destination inside the same `iwin_t`; it formats through a
  local title buffer first.
- The `-Wrestrict` warning in `handle_iwin_click()` is gone from the rebuilt
  `wlcomp` output.

Current GUI caveats:

- NetSurf auto-launch and desktop launch points remain disabled while the
  browser/window-mapping path is debugged.
- HMP mouse injection is useful for movement verification but unreliable as a
  precise coordinate-click test once the PS/2 pointer has been clamped to an
  edge. Prefer visual/manual GTK testing for icon clicks.

## Outstanding / next session

## 2026-04-22 — CPython 3.12 + Flask + SQLite reachable from host

### GUI result

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

### GUI build notes

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

### GUI demo recipe

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

## 2026-04-24 - GUI boot and reproducible rootfs target

### NetSurf result

The xv6 GUI now boots stably in a QEMU GTK window on x86_64:

- Kernel comes up with 2 CPUs, 254 MiB RAM, framebuffer 1024x768x32,
  virtio-blk root disk, and e1000 networking.
- ext4 rootfs mounts and `/bin/init` launches `/bin/desktop` from
  `/etc/startup`.
- `desktop` starts `wlcomp`, the Wayland compositor, on `/dev/fb0` and
  listens on `wayland-0`.
- DHCP succeeds at `10.0.2.15`; tftpd, iperfd, sntpd, netbiosd, mdnsd,
  and gdbstub all start.
- Vim was verified inside the compositor terminal.
- NetSurf launch points are disabled in `wlcomp`; stray browser clicks no
  longer fork a guaranteed `127` child or trigger the earlier kernel panic.

### How

- `rootfs-overlay/etc/startup` starts `/bin/desktop` automatically.
- `ports/wayland/CMakeLists.txt` stages `desktop` and `wlcomp` from the
  reference sysroot, then rebuilds `wlcomp` from source with:
  - zero-extension for absolute mouse coordinates,
  - the NetSurf homepage pointed at bundled `welcome.html`,
  - the Flask probe loop skipped,
  - the NetSurf desktop icon and menu launch handlers made inert.
- `scripts/stage-wayland.sh` also stages `libffi.so*`, which `wlcomp`
  needs at runtime.
- `scripts/make-rootfs.sh` now accepts an optional musl libdir and stages
  `/lib/ld-musl-x86_64.so.1`, `/lib/libc.so`, and GCC runtime shared
  libraries (`libatomic`, `libgcc_s`, `libstdc++`) into the root image.
- `cmake/BuildImage.cmake` now makes the `rootfs` target produce
  `build-x86_64/fs.img` directly from the populated sysroot; default
  size is 1536 MiB for the GUI/Python/ports payload.
- `user/programs/mousetest/` was added as a small `/dev/mouse` event
  diagnostic and is auto-discovered by userland CMake.
- Temporary `init` exec tracing from bring-up was removed; startup keeps
  the normal `init: started ...` line.

### Validation

```bash
cmake -S . -B build-x86_64 -DXV6_ARCH=x86_64

PATH=$PWD/build-toolchain-x86_64/x86_64/phase2/bin:$PATH \
TOOLPREFIX=$PWD/build-toolchain-x86_64/x86_64/phase2/bin/x86_64-xv6-linux-musl- \
cmake -S user -B build-x86_64/user -DARCH=x86_64 -DOPT_LEVEL=2 \
    -DCMAKE_INSTALL_PREFIX=$PWD/build-x86_64/sysroot

PATH=$PWD/build-toolchain-x86_64/x86_64/phase2/bin:$PATH \
cmake --build build-x86_64/user -j16 --target install

bash scripts/make-rootfs.sh build-x86_64/sysroot build-x86_64/fs.img 1536 \
    build-x86_64/toolchain/x86_64/phase2/x86_64-xv6-linux-musl/lib
```

The user build configured with 61 programs and installed `_mousetest`.
The rootfs regenerated successfully:

```text
make-rootfs: wrote build-x86_64/fs.img (1536 MiB ext4, label=xv6root)
```

`debugfs` verified these paths in the regenerated image:
`/bin/desktop`, `/bin/wlcomp`, `/bin/mousetest`,
`/lib/ld-musl-x86_64.so.1`, and `/etc/startup`.

### Demo recipe

```bash
./scripts/launch-gui.sh
```

In the QEMU GTK window, right-click the desktop background for the menu.
Use Ctrl-Alt-G to release the mouse grab, and Ctrl-A X in the host
terminal to quit QEMU.

## 2026-04-24 - NetSurf GTK/Wayland runtime data

### Result

NetSurf now launches past the missing GTK/Wayland runtime data blockers:

- `ports/libxkbcommon/CMakeLists.txt` stages host xkeyboard-config data
  from `/usr/share/X11/xkb` to `build-x86_64/sysroot/share/X11/xkb`.
- `ports/gtk3/CMakeLists.txt` stages the host Adwaita cursor theme from
  `/usr/share/icons/Adwaita` to `build-x86_64/sysroot/share/icons/Adwaita`
  and installs a default theme alias at `share/icons/default/index.theme`,
  including real cursor files under `share/icons/default/cursors`.
- Both staging targets are data-only and can be run without rebuilding the
  full GTK/libxkbcommon ports.
- `fs.img` was regenerated and `debugfs` verified:
  `/share/X11/xkb/rules/evdev`, `/share/icons/Adwaita/cursors/default`,
  and `/share/icons/default/index.theme`.

### Runtime check

Manual launch recipe in the guest:

```sh
export XDG_RUNTIME_DIR=/tmp
export WAYLAND_DISPLAY=wayland-0
export GDK_BACKEND=wayland
export SSL_CERT_FILE=/share/netsurf/ca-bundle
/bin/netsurf file:///share/netsurf/welcome.html &
```

Previous failures were:

- `xkbcommon: Couldn't find file "rules/evdev"`
- `Gdk-WARNING **: Failed to load cursor theme default`, followed by the
  `display_wayland->cursor_theme_name` assertion.

After staging XKB and cursor data, the remaining cursor failure was traced
to Wayland cursor SHM setup: `posix_fallocate()` called musl syscall 990
(`SYS_fallocate`), which the kernel did not route. The kernel now defines
`SYS_fallocate` and routes it to `sys_fallocate()`, returning `-EOPNOTSUPP`
so Wayland falls back to the existing `ftruncate()` path.

Validation after rebuilding the kernel:

- `./scripts/launch-gui.sh` boots the x86_64 desktop.
- Manual NetSurf launch no longer prints `unknown syscall 990`.
- The old `Failed to load cursor theme default` warning and
  `display_wayland->cursor_theme_name` assertion no longer appear.
- QEMU framebuffer capture produced a valid 1024x768 desktop screenshot
  with the Wayland cursor rendered.

NetSurf still did not visibly map a browser window in the captured frame, so
keep the desktop NetSurf launch points inert until that separate visual/window
mapping issue is confirmed fixed.

## 2026-04-25 - NetSurf Wayland handshake debugging

### Current state

`desktop` now auto-launches NetSurf with a known-good Wayland environment:

```text
HOME=/
PATH=/bin:/usr/bin
XDG_RUNTIME_DIR=/tmp
WAYLAND_DISPLAY=wayland-0
GDK_BACKEND=wayland
WAYLAND_DEBUG=client
G_MESSAGES_DEBUG=all
```

This was necessary because the xv6 `/bin/sh` cannot compose the usual
`VAR=value command`, `;`, `&`, or redirection forms used in host shells. Earlier
manual NetSurf launch attempts that used shell-style environment setup did not
actually launch the browser with the intended environment.

### What the trace proves

With `WAYLAND_DEBUG=client`, NetSurf reliably opens a Wayland connection and
sends the first registry request:

```text
-> wl_display#1.get_registry(new id wl_registry#2)
-> wl_display#1.sync(new id wl_callback#3)
```

On some runs it progresses further and receives globals for `wl_compositor`,
`wl_shm`, `wl_seat`, `wl_output`, `xdg_wm_base`, and
`wl_data_device_manager`, plus `wl_shm.format`, `wl_output.geometry/mode/done`,
and occasionally `wl_callback#7.done(0)`. On other runs it stalls after only
the first two client messages. NetSurf still has not reached the point where it
binds `xdg_wm_base`, creates a surface, and maps a browser window.

### Build cache trap

`ports/wayland-src/src/src/wayland-client.c` contains a temporary xv6 workaround
in `wl_display_poll`: when libwayland-client asks for an infinite poll, the
poll is capped and the dispatch loop retries nonblocking `recvmsg()`. The
patched code is present in the NetSurf binary, but the sysroot archive
`build-x86_64/sysroot/lib/libwayland-client.a` can remain stale because the
CMake rule uses the installed archive itself as the stamp. To force a real
Wayland rebuild, delete the installed Wayland archives and the Meson build dir
before rebuilding:

```sh
rm -f build-x86_64/sysroot/lib/libwayland-client.a \
  build-x86_64/sysroot/lib/libwayland-server.a
rm -rf build-x86_64/ports/wayland-libs-build
cmake --build build-x86_64/ports --target port-wayland-libs port-netsurf port-wayland
```

### Kernel-side suspicion

The remaining nondeterminism points at the server-side event path. libwayland's
server event loop registers bare `EPOLLIN` file descriptors, expecting normal
Linux level-triggered epoll behavior. xv6's kqueue-backed epoll currently acts
closer to edge-triggered delivery: `kqueue_wait()` drains a knote from the ready
list and only reports it again after another `vfs_file_knote_notify()` call.
That can leave AF_UNIX data sitting readable while libwayland-server's next
`epoll_wait()` reports nothing.

A temporary `kqueue_wait()` recheck/requeue patch was added in
`kernel/kernel/kqueue/kqueue.c` to emulate level-triggered behavior by calling
the filter's `event()` callback after delivery and re-enqueuing still-active
read/write knotes. It builds, but the current boot still stalls before the
first registry globals are delivered, and the cursor stopped moving during the
test run. Treat this patch as suspect until the cursor/input regression is
explained.

### Next debugging step

Instrument the kqueue/epoll path directly instead of relying only on Wayland
client logs. The useful evidence is whether the Wayland server's fd knote is
being enqueued, delivered, rechecked, and re-enqueued while the AF_UNIX socket
is still readable. Keep the current NetSurf auto-launch in place while doing
that, because it provides the smallest reproducible workload for the stall.

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
- Decide whether the legacy `image` target should depend on `rootfs`, be
  repointed to ext4, or remain the old initrd/raw-image path until removal.
- Decide fate of `scripts/make-initrd.sh` and `scripts/make-image.sh`.
- Move host `mkfs.xv6fs` build out of the kernel tree.
