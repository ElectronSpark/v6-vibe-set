# Linux User Package Inventory

Last updated: 2026-06-24.

This is the current-state inventory milestone for local user programs,
`ports/*`, and the KDE/Qt/Plasma runtime overlay. It follows the ordering in
`docs/linux-userland-upstream-depatch-plan.md` and was checked against the
current `user/programs` and `ports` directories. This file is inventory only:
it does not define new runtime instructions, launch flows, build steps, or
behavior changes.

`docs/linux-kde-minimal-integration-plan.md` is not present in this checkout
and is not used as an authority here.

## Upstream-Clean Boundary

Imported source should converge to marker-only xv6 metadata. Behavioral
compatibility must live below or beside imported source:

- kernel Linux ABI behavior
- libc/sysroot behavior and staged Linux headers
- rootfs runtime data
- package build wrappers and staging policy
- xv6-owned local shims with Linux-compatible ABI surfaces
- xv6-authored local probes and tests that expose ABI behavior

Imported applications and libraries must not carry xv6-only syscall paths,
struct substitutions, errno rewrites, GUI workarounds, feature-disabling
branches, scheduler assumptions, socket/fd/procfs shortcuts, or app-specific
polling/sleep fixes. KDE, Qt, KWin, Plasma, Chromium, WebKit, NetSurf, Mesa,
GTK, X11, and Wayland payloads are validation workloads or upstream payloads,
not places to hide missing kernel/libc/sysroot behavior. Weston is kept as a
regression/control package only.

## Current Tree Evidence

- `user/programs`: matches the de-patching plan plus `alsapcmpoll` and the
  present-but-untracked `regpreservetest`.
- `ports`: matches the de-patching plan package list.
- `ports/xv6-gbm`: directory exists but currently contains no files; keep it
  as a placeholder local-shim inventory item.
- `ports/drm_info/patches`, `ports/gtk3/patches`, `ports/kmscube/patches`, and
  `ports/libepoxy/patches`: empty local patch-slot directories are present and
  inventoried as build-wrapper patch slots, not active source modifications.
- `build-x86_64/kde-noble-plasma`: current generated package evidence is
  present; no regeneration was performed for this inventory.

## Kind Legend

| Kind | Meaning |
| --- | --- |
| `local-program` | xv6-authored command, test, benchmark, desktop tool, or ABI probe. |
| `imported-source` | Upstream source package that should become marker-only. |
| `local-shim` | xv6-owned compatibility library or support surface with Linux-facing ABI/API. |
| `build-wrapper` | Package metadata, cross-build helper, host helper, staging glue, or patch application infrastructure. |
| `data-or-headers` | Imported data or header package. |
| `runtime-overlay` | Generated/staged runtime payload, not committed imported source. |

## Local User Libraries

Kind: `local-shim`.

These are xv6-owned support libraries for local commands and ABI probes. They
may expose compatibility helpers to local programs, but they should not become
application-specific workarounds for imported programs.

```text
user/lib
user/lib/x86_64
```

## Local User Programs

All entries in this section are kind: `local-program`.

### Production Commands

```text
init sh cat echo sleep kill reboot shutdown sync free ps top lsblk mount umount
mknod mkdir cp mv rm ln find grep wc xargs dd losetup
```

### Filesystem And Local Diagnostics

```text
mkfs_xv6fs dumpinode dumpchan dumppcache dumprq dh wallclock keyinject
mouseinject mousetest waitgdb kprofile pingpong primes randtest symlinktest
```

### Stress Tests

```text
bigfile zombie forktest usertests grind crashtest stressfs iobench
blocksendwake dontwaitsend dnsstress tcpstress preempttest
```

### Linux ABI, Audio, And GUI Probes

```text
alsapcmpoll devtest timerdemo pngtest vforktest clonetest cloexectest cowtest
mmaptest mmapbigfile fdtabletest iovectest kqueuetest timerfdstress
syscalltest linuxsyscallabitest testsig fbstat gldemo virgltest gpubuftest
gpucorevalidate drmabitest drmiftest drmprimeprobe nouveauabitest dxgprobe
d3d12probe ttmtest webkitabitest webkitnettest
```

### Present But Untracked

Kind: `local-program`.

```text
regpreservetest
```

`user/programs/regpreservetest/regpreservetest.c` exists in the working tree,
but `git -C user status --short` reports `?? programs/regpreservetest/`.

## Ports: Build, Headers, Data, And Base Libraries

This group corresponds to de-patching Phase 1 plus host/build helpers required
before consumers.

| Item | Kind | Current role |
| --- | --- | --- |
| `cmake` | `build-wrapper` | Shared port CMake helpers, including patch application infrastructure. |
| `linux-uapi-headers` | `data-or-headers` | Linux UAPI headers staged for userland builds. |
| `khronos-headers` | `data-or-headers` | Khronos/EGL/OpenGL header payload. |
| `hwdata` | `data-or-headers` | Device metadata data package. |
| `wayland-host` | `build-wrapper` | Host-side Wayland helper build. |
| `glib-host` | `build-wrapper` | Host-side GLib helper build. |
| `libpng-host` | `build-wrapper` | Host-side libpng helper build. |
| `zlib` | `imported-source` | Low-level compression library. |
| `bzip2` | `imported-source` | Low-level compression library. |
| `xz` | `imported-source` | Low-level compression library. |
| `libffi` | `imported-source` | FFI support library. |
| `libexpat` | `imported-source` | XML parser library. |
| `pcre2` | `imported-source` | Regex library. |
| `json-c` | `imported-source` | JSON library. |
| `sqlite` | `imported-source` | Database library. |
| `ncurses` | `imported-source` | Terminal UI library. |
| `readline` | `imported-source` | Line editing library. |
| `openssl` | `imported-source` | TLS/crypto library and program. |
| `curl` | `imported-source` | Network transfer library and program. |
| `openssh` | `imported-source` | SSH program suite. |
| `libxml2` | `imported-source` | XML library. |

## Ports: Image, Font, Text, And GTK

This group corresponds to de-patching Phase 2.

| Item | Kind | Current role |
| --- | --- | --- |
| `libpng` | `imported-source` | PNG image library. |
| `libjpeg-turbo` | `imported-source` | JPEG image library. |
| `freetype` | `imported-source` | Font rendering library. |
| `fontconfig` | `imported-source` | Font discovery/configuration library. |
| `pixman` | `imported-source` | Pixel manipulation library. |
| `fribidi` | `imported-source` | Bidirectional text library. |
| `harfbuzz` | `imported-source` | Text shaping library. |
| `glib` | `imported-source` | GLib runtime library. |
| `atk` | `imported-source` | Accessibility toolkit library. |
| `cairo` | `imported-source` | 2D graphics library. |
| `gdk-pixbuf` | `imported-source` | Image loading library. |
| `pango` | `imported-source` | Text layout library. |
| `gtk3` | `imported-source` | GTK 3 toolkit. |
| `gtk3/patches` | `build-wrapper` | Empty local patch-slot directory; no active local patches. |

## Ports: Graphics, Input, And Desktop ABI

This group corresponds to de-patching Phase 3. Weston remains a
regression/control item only.

| Item | Kind | Current role |
| --- | --- | --- |
| `libudev` | `local-shim` | xv6-owned libudev-compatible surface. |
| `libseat` | `local-shim` | xv6-owned seat/session compatibility surface. |
| `libevdev` | `local-shim` | xv6-owned libevdev-compatible surface. |
| `libinput` | `local-shim` | xv6-owned libinput-compatible surface. |
| `libdrm` | `imported-source` | DRM userspace library. |
| `drm_info` | `imported-source` | DRM inspection program. |
| `drm_info/patches` | `build-wrapper` | Empty local patch-slot directory; no active local patches. |
| `kmscube` | `imported-source` | KMS/GBM/EGL diagnostic program. |
| `kmscube/patches` | `build-wrapper` | Empty local patch-slot directory; no active local patches. |
| `libepoxy` | `imported-source` | GL dispatch library. |
| `libepoxy/patches` | `build-wrapper` | Empty local patch-slot directory; no active local patches. |
| `wayland-libs` | `imported-source` | Target Wayland libraries from shared Wayland source. |
| `wayland-protocols` | `data-or-headers` | Wayland protocol XML data. |
| `xkeyboard-config` | `data-or-headers` | Keyboard layout/configuration data. |
| `libxkbcommon` | `imported-source` | XKB keyboard handling library. |
| `xv6-gbm` | `local-shim` | Placeholder local GBM compatibility boundary; directory currently has no files. |
| `mesa` | `imported-source` | Mesa graphics stack. |
| `weston` | `imported-source` | Regression/control compositor package. |

## Ports: Browser, Networked App, And Desktop Stack

This group corresponds to de-patching Phase 4.

| Item | Kind | Current role |
| --- | --- | --- |
| `netsurf-buildsystem` | `build-wrapper` | NetSurf build helper source used by NetSurf packages. |
| `netsurf-libwapcaplet` | `imported-source` | NetSurf support library. |
| `netsurf-libparserutils` | `imported-source` | NetSurf support library. |
| `netsurf-libcss` | `imported-source` | NetSurf support library. |
| `netsurf-libnsbmp` | `imported-source` | NetSurf support library. |
| `netsurf-libnsgif` | `imported-source` | NetSurf support library. |
| `netsurf-libnslog` | `imported-source` | NetSurf support library. |
| `netsurf-libnspsl` | `imported-source` | NetSurf support library. |
| `netsurf-libnsutils` | `imported-source` | NetSurf support library. |
| `netsurf-libhubbub` | `imported-source` | NetSurf support library. |
| `netsurf-libsvgtiny` | `imported-source` | NetSurf support library. |
| `netsurf-nsgenbind` | `imported-source` | NetSurf binding generator. |
| `netsurf-libdom` | `imported-source` | NetSurf DOM library. |
| `netsurf` | `imported-source` | Browser/control app. |
| `cpython` | `imported-source` | Python runtime. |
| `vim` | `imported-source` | Editor program. |
| `peanut-gb` | `imported-source` | Game Boy emulator dependency for a local demo. |
| `wayland-src` | `imported-source` | Shared upstream Wayland source for host/target Wayland packages. |
| `webkit` | `imported-source` | WebKitGTK browser engine/control workload. |
| `wayland` | `local-program` | xv6-authored desktop tools, launchers, GL/DRM/Wayland probes, and local preload-wrapper code. |

## Local Shims And Build Wrappers

These are the current non-upstream-clean boundaries that are expected to remain
xv6-owned or wrapper-owned rather than becoming imported application patches.

| Item | Kind | Boundary |
| --- | --- | --- |
| `ports/cmake` | `build-wrapper` | Shared `xv6_port` metadata, pkg-config exposure, and patch application helper. |
| `ports/wayland-host` | `build-wrapper` | Host generator/helper build. |
| `ports/glib-host` | `build-wrapper` | Host helper build. |
| `ports/libpng-host` | `build-wrapper` | Host helper build. |
| `ports/netsurf-buildsystem` | `build-wrapper` | NetSurf build system source/helper. |
| `ports/drm_info/patches` | `build-wrapper` | Empty local patch slot for the `drm_info` wrapper; no active local patches. |
| `ports/gtk3/patches` | `build-wrapper` | Empty local patch slot for the `gtk3` wrapper; no active local patches. |
| `ports/kmscube/patches` | `build-wrapper` | Empty local patch slot for the `kmscube` wrapper; no active local patches. |
| `ports/libepoxy/patches` | `build-wrapper` | Empty local patch slot for the `libepoxy` wrapper; no active local patches. |
| `ports/libudev` | `local-shim` | xv6-owned udev API/ABI stand-in. |
| `ports/libseat` | `local-shim` | xv6-owned seat API/ABI stand-in. |
| `ports/libevdev` | `local-shim` | xv6-owned evdev API/ABI stand-in. |
| `ports/libinput` | `local-shim` | xv6-owned input API/ABI stand-in. |
| `ports/xv6-gbm` | `local-shim` | Reserved GBM compatibility boundary; currently empty. |
| `ports/wayland` | `local-program` | Local desktop/probe programs and xv6 wrapper code, not upstream application source. |

## KDE/Qt/Plasma Runtime Overlay Evidence

Kind: `runtime-overlay`.

Current evidence files under `build-x86_64/kde-noble-plasma`:

| File | Current evidence |
| --- | --- |
| `packages.apt-order.txt` | Present, 1,355 package rows. |
| `packages.resolved.txt` | Present, 1,355 package rows. |
| `packages.txt` | Present, 963 package rows. |
| `kde-qt-package-inventory.tsv` | Present, 1 header plus 1,355 package rows. |
| `empty-dpkg-status` | Present empty dpkg status seed file. |

`kde-qt-package-inventory.tsv` columns:

```text
order package version arch predepends depends recommends source_package source_version seed archive
```

Notable anchors from the current TSV:

| Package | Order | Version | Source package |
| --- | ---: | --- | --- |
| `qtwayland5` | 275 | `5.15.13-1` | `qtwayland-opensource-src` |
| `kwayland-integration` | 317 | `4:5.27.11-0ubuntu3` | `kwayland-integration` |
| `kded5` | 333 | `5.115.0-0ubuntu5` | `kded` |
| `breeze` | 457 | `4:5.27.12-0ubuntu0.1` | `breeze` |
| `kactivitymanagerd` | 478 | `5.27.11-0ubuntu3` | `kactivitymanagerd` |
| `kde-cli-tools` | 520 | `4:5.27.12-0ubuntu0.1` | `kde-cli-tools` |
| `plasma-integration` | 627 | `5.27.11-0ubuntu3` | `plasma-integration` |
| `plasma-workspace` | 1034 | `4:5.27.12-0ubuntu0.1` | `plasma-workspace` |
| `pipewire` | 1247 | `1.0.5-1ubuntu3.2` | `pipewire` |
| `plasma-desktop` | 1277 | `4:5.27.12-0ubuntu0.1` | `plasma-desktop` |
| `kwin-wayland` | 1284 | `4:5.27.11-0ubuntu3` | `kwin` |
| `dolphin` | 1339 | `4:23.08.5-0ubuntu4` | `dolphin` |
| `konsole` | 1341 | `4:23.08.5-0ubuntu4` | `konsole` |
| `kate` | 1343 | `4:23.08.5-0ubuntu3` | `kate` |
| `kwrite` | 1344 | `4:23.08.5-0ubuntu3` | `kate` |
| `xterm` | 1349 | `390-1ubuntu3` | `xterm` |
| `qterminal` | 1354 | `1.4.0-0ubuntu5` | `qterminal` |

The overlay payload is generated/staged runtime content, not committed imported
KDE/Qt/Plasma source. The upstream-clean boundary still requires behavioral
compatibility to be fixed in kernel/libc/sysroot/rootfs data, build wrappers,
or local shims, not in KDE/Qt/Plasma source.

## Imported-Source Patch Risk Notes

Empty local patch-slot directories are build-wrapper inventory, not evidence of
active imported-source modifications:

```text
ports/drm_info/patches
ports/gtk3/patches
ports/kmscube/patches
ports/libepoxy/patches
```

Upstream-internal patch and diff files under `ports/*/src` are part of imported
source payloads or their upstream test/build fixtures. They should not be
counted as local xv6 de-patching work unless a wrapper applies them as xv6
patches or the checkout carries a local source delta against the imported
payload. Current examples include CPython, curl, FreeType, GLib, Mesa, ncurses,
vim spelling data, and Weston display-info test data patch/diff files under
their respective `src` trees.

## Plan Versus Current Tree Mismatches

| Item | Current state | Inventory treatment |
| --- | --- | --- |
| `alsapcmpoll` | Present in `user/programs`; not listed in `docs/linux-userland-upstream-depatch-plan.md`. | Added as a local Linux/audio ABI probe. |
| `regpreservetest` | Present in `user/programs` but untracked in the `user` submodule. | Listed as present-but-untracked local program. |
| `ports/xv6-gbm` | Listed by the plan and present as a directory, but currently empty. | Kept as placeholder `local-shim`. |
| `ports/drm_info/patches`, `ports/gtk3/patches`, `ports/kmscube/patches`, `ports/libepoxy/patches` | Present as empty directories. | Listed as build-wrapper patch slots, not active imported-source patches. |
| `docs/linux-kde-minimal-integration-plan.md` | Missing in this checkout. | Not used. |

No port package listed in the de-patching plan is missing from the current
`ports` directory, and no extra `ports/*` package directory was found.
