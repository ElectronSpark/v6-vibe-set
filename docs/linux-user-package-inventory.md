# Linux User Package Inventory

Last updated: 2026-06-25.

This is the current dependency-oriented inventory for user packages and ports
that matter to GUI, KDE/Qt/Plasma, Chromium/WebKit, Wayland, X11, DRM, audio,
and network bring-up. It is an inventory only: it does not define new launch
flows, build steps, validation gates, or source changes.

Evidence used for this pass:

- repo-local guidance in `.github/skills/xv6-os-debugging/SKILL.md` and
  `.github/skills/xv6-linux-gui-abi/SKILL.md`
- current direction and guardrails in `docs/active-work-plan.md`
- archived package scope/order in
  `docs/archive/plan-consolidation-20260701/linux-userland-upstream-depatch-plan.md`
- current `ports/*/CMakeLists.txt`, non-source staging files, patch-slot
  directories, `user/programs`, rootfs overlay files, and generated KDE package
  inventory under `build-x86_64/kde-noble-plasma`

`docs/linux-kde-minimal-integration-plan.md` is not present in this checkout
and is not used as an authority here.

## Status Legend

| Status | Meaning |
| --- | --- |
| `upstream-clean-boundary` | Imported source or package payload should stay upstream-clean. This inventory found no active local wrapper patch unless the row says otherwise. |
| `empty-patch-slot` | A `ports/<pkg>/patches` directory exists but is empty; it is wrapper infrastructure, not an active source delta. |
| `xv6-local-command` | xv6-authored ordinary command, local diagnostic, benchmark, or test; not an imported upstream payload. |
| `xv6-owned-shim` | xv6-owned compatibility implementation with a Linux-facing ABI/API. |
| `xv6-local-probe` | xv6-authored command, launcher, reducer, or smoke helper. |
| `build-wrapper` | CMake/Meson/autoconf glue, host helper, staging script, or package metadata. |
| `runtime-overlay` | Generated/staged package payload or rootfs data; not committed imported source. |
| `placeholder` | Reserved package boundary with no active files in the current tree. |

The status column is intentionally conservative. It records current tree
evidence from wrappers and patch slots; it is not a substitute for a full diff
against every upstream tarball or submodule commit.

## Dependency Bands

High-level bring-up order is:

1. `base-build-headers-data`: shared wrappers, Linux/Khronos headers, hardware
   metadata, host generators, and low-level compression/XML/crypto/network
   libraries.
2. `image-text-gtk`: image/font/text libraries and GTK prerequisites.
3. `graphics-input-display`: libudev/libseat/libinput shims, libdrm, Mesa,
   Wayland, XKB, X11/Xwayland runtime, KMS/DRM tools, and Weston control.
4. `browser-app-runtime`: NetSurf, WebKit, CPython/Vim/demo payloads, local
   Wayland desktop tools, and runtime WebKit/Chromium probes.
5. `kde-qt-overlay`: generated Ubuntu Noble KDE/Qt/Plasma package overlay.
6. `rootfs-runtime-data`: DBus, ALSA, MIME, WebKit, Chromium, and GUI service
   data staged into the image.
7. `local-abi-probes`: xv6 user programs and host-imported probes that expose
   Linux ABI gaps without patching imported applications.

## Port Inventory

| Package | Band | Source directory/submodule | Build or staging file | Role/category | Status | Direct ordering evidence |
| --- | --- | --- | --- | --- | --- | --- |
| `cmake` | `base-build-headers-data` | `ports/cmake` | `ports/cmake/AddPort.cmake`, `ports/cmake/ExposePkgConfigLibs.cmake`, `ports/cmake/apply_patches.sh` | Shared port wrapper and patch infrastructure | `build-wrapper` | Used by most `ports/*/CMakeLists.txt` through `xv6_port(...)`. |
| `linux-uapi-headers` | `base-build-headers-data` | none observed | `ports/linux-uapi-headers/CMakeLists.txt` | Linux UAPI headers for userspace builds | `upstream-clean-boundary` | Consumed by GTK and DRM/graphics-facing builds. |
| `khronos-headers` | `base-build-headers-data` | `ports/khronos-headers/egl-registry`, `ports/khronos-headers/opengl-registry` | `ports/khronos-headers/CMakeLists.txt` | EGL/OpenGL/GLES headers | `upstream-clean-boundary` | `mesa` and `libepoxy` depend on this header payload. |
| `hwdata` | `base-build-headers-data` | `ports/hwdata/src` | `ports/hwdata/CMakeLists.txt` | PCI/device metadata | `upstream-clean-boundary` | Weston depends on `hwdata`. |
| `wayland-src` | `base-build-headers-data` | `ports/wayland-src/src` | shared source only | Shared upstream Wayland source for host and target Wayland packages | `upstream-clean-boundary` | Used by `wayland-host` and `wayland-libs` package boundaries. |
| `wayland-host` | `base-build-headers-data` | uses shared Wayland source | `ports/wayland-host/CMakeLists.txt` | Host-side Wayland scanner/helper build | `build-wrapper` | Needed before target Wayland and protocol consumers. |
| `glib-host` | `base-build-headers-data` | host helper boundary | `ports/glib-host/CMakeLists.txt` | Host-side GLib tools for Meson packages | `build-wrapper` | Required by `gdk-pixbuf`, `pango`, and `gtk3`. |
| `libpng-host` | `base-build-headers-data` | host helper boundary | `ports/libpng-host/CMakeLists.txt` | Host-side libpng helper build | `build-wrapper` | Host helper adjacent to target `libpng`. |
| `zlib` | `base-build-headers-data` | `ports/zlib/src` | `ports/zlib/CMakeLists.txt` | Compression base library | `upstream-clean-boundary` | Required by `libpng`, `freetype`, `glib`, `curl`, and `libxml2`. |
| `bzip2` | `base-build-headers-data` | `ports/bzip2/src` | `ports/bzip2/CMakeLists.txt` | Compression base library | `upstream-clean-boundary` | `cairo` uses `bzip2-symlink`; `freetype` depends on `bzip2`. |
| `xz` | `base-build-headers-data` | `ports/xz/src` | `ports/xz/CMakeLists.txt` | LZMA compression library | `upstream-clean-boundary` | `libxml2` depends on `xz`. |
| `libffi` | `base-build-headers-data` | `ports/libffi/src` | `ports/libffi/CMakeLists.txt` | FFI support library | `upstream-clean-boundary` | `glib`, `wayland-libs`, and `cpython` use it. |
| `libexpat` | `base-build-headers-data` | `ports/libexpat/src` | `ports/libexpat/CMakeLists.txt` | XML parser | `upstream-clean-boundary` | `fontconfig`, `wayland-libs`, and NetSurf SVG/DOM packages depend on it. |
| `pcre2` | `base-build-headers-data` | `ports/pcre2/src` | `ports/pcre2/CMakeLists.txt` | Regex library | `upstream-clean-boundary` | `glib` depends on `pcre2`. |
| `json-c` | `base-build-headers-data` | `ports/json-c/src` | `ports/json-c/CMakeLists.txt` | JSON library | `upstream-clean-boundary` | `drm_info` depends on `json-c`. |
| `sqlite` | `base-build-headers-data` | `ports/sqlite/src` | `ports/sqlite/CMakeLists.txt` | Database library | `upstream-clean-boundary` | `cpython` depends on `sqlite`. |
| `ncurses` | `base-build-headers-data` | `ports/ncurses/src` | `ports/ncurses/CMakeLists.txt` | Terminal UI library | `upstream-clean-boundary` | `readline` and `cpython` depend on `ncurses`. |
| `readline` | `base-build-headers-data` | `ports/readline/src` | `ports/readline/CMakeLists.txt` | Line editing library | `upstream-clean-boundary` | Depends on `ncurses`; used by interactive tools. |
| `openssl` | `base-build-headers-data` | `ports/openssl/src` | `ports/openssl/CMakeLists.txt` | TLS/crypto library and `/bin/openssl` | `upstream-clean-boundary` | Required before `curl`, `openssh`, `cpython`, and NetSurf TLS. |
| `curl` | `base-build-headers-data` | `ports/curl/src` | `ports/curl/CMakeLists.txt` | Network transfer library/program | `upstream-clean-boundary` | Depends on `zlib` and `openssl`; NetSurf uses curl. |
| `openssh` | `base-build-headers-data` | `ports/openssh/src` | `ports/openssh/CMakeLists.txt` | SSH client/server suite | `upstream-clean-boundary` | Depends on `port-openssl`; wrapper copies source to build dir and touches copied `configure`. |
| `libxml2` | `base-build-headers-data` | `ports/libxml2/src` | `ports/libxml2/CMakeLists.txt` | XML library | `upstream-clean-boundary` | Depends on `zlib` and `xz`; used by higher-level app stacks. |
| `libpng` | `image-text-gtk` | `ports/libpng/src` | `ports/libpng/CMakeLists.txt` | PNG library | `upstream-clean-boundary` | Depends on `zlib`; used by image/font/GTK stack. |
| `libjpeg-turbo` | `image-text-gtk` | `ports/libjpeg-turbo/src` | `ports/libjpeg-turbo/CMakeLists.txt` | JPEG library | `upstream-clean-boundary` | Used by `gdk-pixbuf` and image consumers. |
| `freetype` | `image-text-gtk` | `ports/freetype/src` | `ports/freetype/CMakeLists.txt` | Font rendering | `upstream-clean-boundary` | Depends on `zlib`, `bzip2`, and `libpng`. |
| `fontconfig` | `image-text-gtk` | `ports/fontconfig/src` | `ports/fontconfig/CMakeLists.txt` | Font discovery/configuration | `upstream-clean-boundary` | Depends on `freetype` and `libexpat`; used by Cairo/Pango/GTK/KDE payloads. |
| `pixman` | `image-text-gtk` | `ports/pixman/src` | `ports/pixman/CMakeLists.txt` | Pixel manipulation | `upstream-clean-boundary` | Required by Cairo and Weston. |
| `fribidi` | `image-text-gtk` | `ports/fribidi/src` | `ports/fribidi/CMakeLists.txt` | Bidirectional text | `upstream-clean-boundary` | Used by Pango/GTK. |
| `harfbuzz` | `image-text-gtk` | `ports/harfbuzz/src` | `ports/harfbuzz/CMakeLists.txt` | Text shaping | `upstream-clean-boundary` | Depends on `freetype`; used by Pango/GTK/WebKit. |
| `glib` | `image-text-gtk` | `ports/glib/src` | `ports/glib/CMakeLists.txt` | GLib runtime | `upstream-clean-boundary` | Depends on `pcre2`, `libffi`, and `zlib`; base for GTK and many GUI stacks. |
| `atk` | `image-text-gtk` | `ports/atk/src` | `ports/atk/CMakeLists.txt` | Accessibility toolkit | `upstream-clean-boundary` | Depends on `glib`; feeds GTK. |
| `cairo` | `image-text-gtk` | `ports/cairo/src` | `ports/cairo/CMakeLists.txt` | 2D drawing | `upstream-clean-boundary` | Depends on `pixman`, `freetype`, `fontconfig`, `libpng`, `zlib`, `glib`, and `bzip2-symlink`. |
| `gdk-pixbuf` | `image-text-gtk` | `ports/gdk-pixbuf/src` | `ports/gdk-pixbuf/CMakeLists.txt` | Image loading | `upstream-clean-boundary` | Depends on `glib`, `libpng`, `libjpeg-turbo`, and `glib-host`. |
| `pango` | `image-text-gtk` | `ports/pango/src` | `ports/pango/CMakeLists.txt` | Text layout | `upstream-clean-boundary` | Depends on `glib`, `harfbuzz`, `fontconfig`, `cairo`, `fribidi`, `freetype`, and `glib-host`. |
| `gtk3` | `image-text-gtk` | `ports/gtk3/src` | `ports/gtk3/CMakeLists.txt`, `ports/gtk3/patches` | GTK 3 toolkit | `empty-patch-slot` | Depends on GLib/GTK base plus Wayland, XKB, libepoxy, and Linux UAPI headers. |
| `libudev` | `graphics-input-display` | `ports/libudev/src` | `ports/libudev/CMakeLists.txt` | udev-compatible discovery shim | `xv6-owned-shim` | Base shim for `libdrm` and `libinput`. |
| `libseat` | `graphics-input-display` | `ports/libseat/src` | `ports/libseat/CMakeLists.txt` | seat/session compatibility shim | `xv6-owned-shim` | Used by Weston/control compositor paths. |
| `libevdev` | `graphics-input-display` | `ports/libevdev/src` | `ports/libevdev/CMakeLists.txt` | evdev compatibility shim | `xv6-owned-shim` | `libinput` and Weston consume this surface. |
| `libinput` | `graphics-input-display` | `ports/libinput/src` | `ports/libinput/CMakeLists.txt` | input stack compatibility shim | `xv6-owned-shim` | Depends on `libudev` and `libevdev`; required by Weston/KDE input probes. |
| `libdrm` | `graphics-input-display` | `ports/libdrm/src` | `ports/libdrm/CMakeLists.txt` | DRM userspace library | `upstream-clean-boundary` | Depends on `libudev`; feeds Mesa, drm_info, kmscube, Weston, and KDE/Qt runtime. |
| `drm_info` | `graphics-input-display` | `ports/drm_info/src` | `ports/drm_info/CMakeLists.txt`, `ports/drm_info/patches` | DRM inspection program | `empty-patch-slot` | Depends on `libdrm` and `json-c`. |
| `kmscube` | `graphics-input-display` | `ports/kmscube/src` | `ports/kmscube/CMakeLists.txt`, `ports/kmscube/patches` | KMS/GBM/EGL diagnostic program | `empty-patch-slot` | Depends on `libdrm` and `mesa`. |
| `libepoxy` | `graphics-input-display` | `ports/libepoxy/src` | `ports/libepoxy/CMakeLists.txt`, `ports/libepoxy/patches` | GL dispatch library | `empty-patch-slot` | Depends on `khronos-headers`; used by GTK and GL consumers. |
| `wayland-libs` | `graphics-input-display` | uses `ports/wayland-src/src` | `ports/wayland-libs/CMakeLists.txt` | Target Wayland libraries | `upstream-clean-boundary` | Depends on `libffi`, `libexpat`, and `wayland-host`. |
| `wayland-protocols` | `graphics-input-display` | `ports/wayland-protocols/src` | `ports/wayland-protocols/CMakeLists.txt` | Wayland protocol XML data | `upstream-clean-boundary` | Depends on `wayland-libs` and `wayland-host`. |
| `xkeyboard-config` | `graphics-input-display` | no `src` directory; staged data tree | `ports/xkeyboard-config/CMakeLists.txt` | XKB keyboard data | `upstream-clean-boundary` | Stages XKB data consumed by Xwayland/KDE/Wayland input paths. |
| `libxkbcommon` | `graphics-input-display` | `ports/libxkbcommon/src` | `ports/libxkbcommon/CMakeLists.txt` | XKB keyboard handling library | `upstream-clean-boundary` | Depends on `wayland-libs`, `wayland-host`, and `xkeyboard-config`. |
| `xv6-gbm` | `graphics-input-display` | `ports/xv6-gbm` | none present | Reserved GBM compatibility boundary | `placeholder` | Directory exists but currently contains no files. |
| `mesa` | `graphics-input-display` | `ports/mesa/src` | `ports/mesa/CMakeLists.txt`, `ports/mesa/gl.pc` | Mesa EGL/GLES/GL/GBM/DRI stack | `upstream-clean-boundary` | Depends on `khronos-headers`, `libdrm`, Wayland/GBM-adjacent runtime, and local staging aliases. |
| `weston` | `graphics-input-display` | `ports/weston/src` | `ports/weston/CMakeLists.txt`, `ports/weston/xv6-weston.ini`, `ports/weston/xwayland-glamor-wrapper.c` | Regression/control compositor and Xwayland runtime staging owner | `upstream-clean-boundary` plus wrapper config | Depends on Cairo, hwdata, libdrm, input/seat shims, Mesa, pixman, Wayland libs/protocols. |
| `netsurf-buildsystem` | `browser-app-runtime` | `ports/netsurf-buildsystem/src` | `ports/netsurf-buildsystem/CMakeLists.txt` | NetSurf build helper source | `build-wrapper` | Base dependency for all NetSurf libraries. |
| `netsurf-libwapcaplet` | `browser-app-runtime` | `ports/netsurf-libwapcaplet/src` | `ports/netsurf-libwapcaplet/CMakeLists.txt` | NetSurf interned-string support library | `upstream-clean-boundary` | Depends on `netsurf-buildsystem`. |
| `netsurf-libparserutils` | `browser-app-runtime` | `ports/netsurf-libparserutils/src` | `ports/netsurf-libparserutils/CMakeLists.txt` | NetSurf parser utility library | `upstream-clean-boundary` | Depends on `netsurf-buildsystem`. |
| `netsurf-libcss` | `browser-app-runtime` | `ports/netsurf-libcss/src` | `ports/netsurf-libcss/CMakeLists.txt` | CSS library | `upstream-clean-boundary` | Depends on buildsystem, wapcaplet, and parserutils. |
| `netsurf-libnsbmp` | `browser-app-runtime` | `ports/netsurf-libnsbmp/src` | `ports/netsurf-libnsbmp/CMakeLists.txt` | BMP image library | `upstream-clean-boundary` | Depends on `netsurf-buildsystem`. |
| `netsurf-libnsgif` | `browser-app-runtime` | `ports/netsurf-libnsgif/src` | `ports/netsurf-libnsgif/CMakeLists.txt` | GIF image library | `upstream-clean-boundary` | Depends on `netsurf-buildsystem`. |
| `netsurf-libnslog` | `browser-app-runtime` | `ports/netsurf-libnslog/src` | `ports/netsurf-libnslog/CMakeLists.txt` | NetSurf logging library | `upstream-clean-boundary` | Depends on `netsurf-buildsystem`. |
| `netsurf-libnspsl` | `browser-app-runtime` | `ports/netsurf-libnspsl/src` | `ports/netsurf-libnspsl/CMakeLists.txt` | Public suffix support | `upstream-clean-boundary` | Depends on `netsurf-buildsystem`. |
| `netsurf-libnsutils` | `browser-app-runtime` | `ports/netsurf-libnsutils/src` | `ports/netsurf-libnsutils/CMakeLists.txt` | NetSurf utility library | `upstream-clean-boundary` | Depends on `netsurf-buildsystem`. |
| `netsurf-libhubbub` | `browser-app-runtime` | `ports/netsurf-libhubbub/src` | `ports/netsurf-libhubbub/CMakeLists.txt` | HTML parser library | `upstream-clean-boundary` | Depends on buildsystem and parserutils. |
| `netsurf-libsvgtiny` | `browser-app-runtime` | `ports/netsurf-libsvgtiny/src` | `ports/netsurf-libsvgtiny/CMakeLists.txt` | SVG library | `upstream-clean-boundary` | Depends on buildsystem, `libexpat`, wapcaplet, parserutils, hubbub, and DOM. |
| `netsurf-nsgenbind` | `browser-app-runtime` | `ports/netsurf-nsgenbind/src` | `ports/netsurf-nsgenbind/CMakeLists.txt` | NetSurf binding generator | `upstream-clean-boundary` | Depends on NetSurf build chain. |
| `netsurf-libdom` | `browser-app-runtime` | `ports/netsurf-libdom/src` | `ports/netsurf-libdom/CMakeLists.txt` | DOM library | `upstream-clean-boundary` | Depends on buildsystem, nsgenbind, `libexpat`, wapcaplet, parserutils, hubbub, and CSS. |
| `netsurf` | `browser-app-runtime` | `ports/netsurf/src` | `ports/netsurf/CMakeLists.txt` | Browser/control workload | `upstream-clean-boundary` | Consumes NetSurf libraries plus curl/OpenSSL and GTK/Wayland support. |
| `cpython` | `browser-app-runtime` | `ports/cpython/src` | `ports/cpython/CMakeLists.txt`, `ports/cpython/write-setup-local.cmake` | Python runtime for desktop/support tools | `upstream-clean-boundary` | Depends on `openssl`, `sqlite`, `libffi`, `zlib`, `bzip2`, `xz`, `ncurses`, and `readline`. |
| `vim` | `browser-app-runtime` | `ports/vim/src` | `ports/vim/CMakeLists.txt`, `ports/vim/xv6-vim-launcher.c` | Editor package with xv6 launcher wrapper | imported source plus `xv6-owned-shim` launcher | Uses local launcher/support wrapper outside imported Vim source. |
| `peanut-gb` | `browser-app-runtime` | `ports/peanut-gb/src` | no package wrapper present | Demo dependency referenced by local Wayland tools | `upstream-clean-boundary` | Directory exists; not a KDE/Qt bring-up target. |
| `webkit` | `browser-app-runtime` | host/reference runtime, no committed `src` directory observed | `ports/webkit/CMakeLists.txt`, `ports/webkit/stage-webkit-runtime.sh`, `ports/webkit/apply-xv6-overrides.sh`, `ports/webkit/overrides/README.md` | WebKitGTK runtime/control workload staging | `runtime-overlay` | Stages from `XV6_WEBKIT_REF_SYSROOT`; depends on already staged GTK/Pango/Cairo/Harfbuzz libs. |
| `wayland` | `browser-app-runtime` | `ports/wayland/src` | `ports/wayland/CMakeLists.txt` | xv6-authored desktop tools, launchers, GL/DRM/Wayland probes, preload wrappers | `xv6-local-probe` | Consumes Wayland/Mesa/DRM/display runtime and local demo support. |

## KDE/Qt/Plasma Overlay

The KDE/Qt/Plasma payload is generated runtime content, not committed KDE/Qt
source. The current generated evidence lives under
`build-x86_64/kde-noble-plasma`:

| File | Evidence |
| --- | --- |
| `kde-qt-package-inventory.tsv` | Present, 1 header plus 1,355 package rows. |
| `packages.apt-order.txt` | Present, 1,355 package rows. |
| `packages.resolved.txt` | Present, 1,355 package rows. |
| `packages.txt` | Present, 963 package rows. |
| `empty-dpkg-status` | Present empty dpkg status seed file. |

`scripts/image/stage-kde-runtime.sh` is the staging authority for this overlay.
It resolves apt dependencies from seeds, extracts package trees, writes
`kde-qt-package-inventory.tsv`, and then applies runtime-overlay fixups such as
loader links, qtchooser configs, MIME/service data, deterministic portal
defaults, PipeWire/WirePlumber xv6 runtime config, optional hardware defaults,
and the Qt Wayland smoke QML. These are rootfs/runtime data adaptations, not
patches to imported KDE/Qt/Plasma source.

Seed packages from the current staging script:

```text
plasma-desktop plasma-workspace kwin-wayland qtwayland5 kde-cli-tools kded5
kactivitymanagerd kwayland-integration plasma-integration qmlscene plasma-pa
pipewire pipewire-pulse pipewire-audio-client-libraries pipewire-alsa
wireplumber pulseaudio-utils alsa-utils bluez hspell breeze breeze-icon-theme
dolphin konsole kate kwrite qterminal xterm libgpgmepp6t64
```

Important generated package anchors from the current TSV:

| Package | Order | Version | Source package | Role |
| --- | ---: | --- | --- | --- |
| `qtwayland5` | 275 | `5.15.13-1` | `qtwayland-opensource-src` | Qt Wayland platform support. |
| `kwayland-integration` | 317 | `4:5.27.11-0ubuntu3` | `kwayland-integration` | KDE/Wayland integration. |
| `kded5` | 333 | `5.115.0-0ubuntu5` | `kded` | KDE daemon framework. |
| `breeze` | 457 | `4:5.27.12-0ubuntu0.1` | `breeze` | KDE theme/style payload. |
| `kactivitymanagerd` | 478 | `5.27.11-0ubuntu3` | `kactivitymanagerd` | KDE activity service. |
| `kde-cli-tools` | 520 | `4:5.27.12-0ubuntu0.1` | `kde-cli-tools` | KDE command helpers. |
| `plasma-integration` | 627 | `5.27.11-0ubuntu3` | `plasma-integration` | Plasma/Qt integration. |
| `plasma-workspace` | 1034 | `4:5.27.12-0ubuntu0.1` | `plasma-workspace` | Plasma shell/session payload. |
| `pipewire` | 1247 | `1.0.5-1ubuntu3.2` | `pipewire` | GUI audio server path. |
| `plasma-desktop` | 1277 | `4:5.27.12-0ubuntu0.1` | `plasma-desktop` | Desktop shell seed. |
| `kwin-wayland` | 1284 | `4:5.27.11-0ubuntu3` | `kwin` | Wayland compositor/window manager. |
| `dolphin` | 1339 | `4:23.08.5-0ubuntu4` | `dolphin` | File manager/control app. |
| `konsole` | 1341 | `4:23.08.5-0ubuntu4` | `konsole` | Terminal/control app. |
| `kate` | 1343 | `4:23.08.5-0ubuntu3` | `kate` | Editor/control app. |
| `kwrite` | 1344 | `4:23.08.5-0ubuntu3` | `kate` | Editor/control app. |
| `xterm` | 1349 | `390-1ubuntu3` | `xterm` | X11 terminal/control app. |
| `qterminal` | 1354 | `1.4.0-0ubuntu5` | `qterminal` | Qt terminal/control app. |
| `qmlscene` | 1355 | `5.15.13+dfsg-1ubuntu0.1` | `qtdeclarative-opensource-src` | Qt/QML smoke surface. |

KDE/Qt/KWin/Plasma source must remain upstream-clean except for marker-only
xv6 metadata. If KDE fails, the owner is kernel ABI, libc/sysroot, rootfs data,
build/staging wrappers, or local shims rather than KDE/Qt source.

## X11, Xwayland, Chromium, And WebKit Runtime Boundaries

| Boundary | Source/staging file | Role | Status | Dependency note |
| --- | --- | --- | --- | --- |
| Xwayland runtime | `scripts/image/stage-xwayland-runtime.sh`, `scripts/image/xwayland-kde-wrapper.c` | Stages host/package Xwayland and installs the KDE wrapper at `/bin/Xwayland` and `/usr/bin/Xwayland`. | `runtime-overlay` plus `xv6-local-probe` wrapper | Required by KDE/KWin Xwayland and X11/GLX probes; final rootfs must keep `/usr/bin/Xwayland -> ../../bin/Xwayland` behavior described in the DRM plan. |
| Host GUI import helpers | `scripts/image/stage-host-gui-runtime.sh`, `scripts/image/import-host-gui.sh` | Stage host GUI payloads and launchers used as Linux ABI probes. | `runtime-overlay` | Feeds GTK/X11/Wayland/Chromium-style stress probes. |
| Chromium controls | `scripts/image/wayland-chromium-launcher.c`, `scripts/gpu/chromium-menu-proof.expect`, `scripts/gpu/chromium-youtube-smoothness.expect`, `scripts/gpu/wayland-chromium-supervisor-low-noise.expect`, `rootfs-overlay/share/chromium-audio-smoke.html` | Chromium is a regression/stress workload, not a committed `ports/chromium` source package in this tree. | `xv6-local-probe` plus `runtime-overlay` | Depends on KDE/Wayland/X11/DRM/audio/network ABI and host/staged browser payloads. |
| WebKit runtime | `ports/webkit/CMakeLists.txt`, `ports/webkit/stage-webkit-runtime.sh`, `scripts/container/docker-build-webkit.sh`, `scripts/gpu/validate-webkit-runtime.sh` | WebKitGTK MiniBrowser/runtime staging and validation. | `runtime-overlay` | Depends on GTK/Pango/Cairo/Harfbuzz and staged media/runtime data. |
| WebKit media probes | `scripts/image/stage-webkit-media.sh`, `rootfs-overlay/share/webkit/*.html` | Local HTML/network/media stress fixtures. | `runtime-overlay` | Exercises network fetch, MSE/audio/video, GPU, input, lifecycle, and scheduler behavior. |

## Rootfs Runtime Data

These files are package/runtime data boundaries. They can adapt the rootfs
environment, but they must not hide missing Linux behavior inside imported
source trees.

| Path | Band | Role | Status |
| --- | --- | --- | --- |
| `rootfs-overlay/etc/startup` | `rootfs-runtime-data` | Starts default KDE desktop session in the current overlay. | `runtime-overlay` |
| `rootfs-overlay/etc/daemons` | `rootfs-runtime-data` | Daemon startup list. | `runtime-overlay` |
| `rootfs-overlay/etc/gtk-3.0/settings.ini` | `rootfs-runtime-data` | GTK runtime settings. | `runtime-overlay` |
| `rootfs-overlay/usr/share/alsa/alsa.conf` | `rootfs-runtime-data` | ALSA default config for GUI audio path. | `runtime-overlay` |
| `rootfs-overlay/bin/start-dbus-system` | `rootfs-runtime-data` | DBus system startup helper. | `runtime-overlay` |
| `rootfs-overlay/usr/share/dbus-1/xv6-session.conf`, `rootfs-overlay/usr/share/dbus-1/xv6-system.conf` | `rootfs-runtime-data` | DBus session/system policy for desktop smoke. | `runtime-overlay` |
| `rootfs-overlay/etc/ld.so.conf.d/gpup-wsl.conf`, `rootfs-overlay/etc/profile.d/gpup-d3d12.sh` | `rootfs-runtime-data` | Hyper-V/WSL GPU-P runtime loader environment. | `runtime-overlay` |
| `rootfs-overlay/etc/udisks2/udisks2.conf` | `rootfs-runtime-data` | Desktop storage service config. | `runtime-overlay` |
| `rootfs-overlay/share/mime/*` | `rootfs-runtime-data` | Minimal MIME data used by desktop/browser fixtures. | `runtime-overlay` |
| `rootfs-overlay/share/webkit/*` | `rootfs-runtime-data` | WebKit network/media/browser fixtures. | `runtime-overlay` |
| `rootfs-overlay/share/chromium-audio-smoke.html` | `rootfs-runtime-data` | Chromium audio smoke fixture. | `runtime-overlay` |

## Local User Libraries And Programs

Local libraries are xv6-owned support surfaces:

```text
user/lib
user/lib/x86_64
```

Current local program coverage for every `user/programs` entry. These are
xv6-authored local programs, commands, tests, benchmarks, and probes; they are
not imported upstream package payloads. The GUI/KDE/Qt/Chromium/WebKit/Wayland/
X11/DRM/audio/network probes remain called out, while ordinary/local commands
stay in scope for the all-user-packages de-patching inventory.

| Group | Programs | Role/category | Status |
| --- | --- | --- | --- |
| Boot/session and system commands | `init sh mount umount ps top lsblk free sync shutdown reboot kill sleep` | Local boot/session control and inspection commands used by desktop bring-up and ordinary xv6 operation. | `xv6-local-command` |
| File, text, and shell utilities | `cat echo cp mv rm ln mkdir mknod find grep wc xargs dd losetup` | Local command-line utilities in the userland scope; not imported coreutils/findutils payloads. | `xv6-local-command` |
| Filesystem/device/local diagnostics | `mkfs_xv6fs dumpinode dumpchan dumppcache dumprq dh waitgdb wallclock kprofile` | xv6-owned diagnostics and support tools for local filesystem, device, timing, profiling, and debug workflows. | `xv6-local-command` |
| Filesystem/process/socket stress adjacent to GUI ABI | `clonetest cloexectest cowtest fdtabletest iovectest kqueuetest timerfdstress syscalltest linuxsyscallabitest testsig vforktest mmaptest mmapbigfile dnsstress tcpstress preempttest` | Reducers for process, fd-table, event, timer, VM, DNS, and socket behavior exposed by GUI apps. | `xv6-local-probe` |
| Audio probes | `alsapcmpoll devtest` | OSS/ALSA/audio readiness checks used by GUI smoke. | `xv6-local-probe` |
| Image/input desktop probes | `pngtest keyinject mouseinject mousetest timerdemo` | Image decode and input/event helpers. | `xv6-local-probe` |
| DRM/GPU probes | `fbstat gldemo virgltest gpubuftest gpucorevalidate drmabitest drmiftest drmprimeprobe nouveauabitest dxgprobe d3d12probe ttmtest` | DRM/KMS/GEM/virgl/DXG/D3D12/TTM diagnostic surface. | `xv6-local-probe` |
| WebKit/browser probes | `webkitabitest webkitnettest` | WebKit/network ABI checks. | `xv6-local-probe` |
| Regression/test support | `bigfile forktest usertests grind crashtest stressfs iobench blocksendwake dontwaitsend regpreservetest pingpong primes randtest symlinktest zombie` | Local stress, benchmark, and ABI regression tests. | `xv6-local-probe` |

## Patch And Marker Notes

- Active local patch-slot directories observed in the current port wrappers are
  empty: `ports/drm_info/patches`, `ports/gtk3/patches`,
  `ports/kmscube/patches`, and `ports/libepoxy/patches`.
- Upstream-internal patch/diff files under `ports/*/src` are part of imported
  payloads or upstream test/build fixtures unless a wrapper applies them as xv6
  patches.
- `ports/webkit/apply-xv6-overrides.sh` and `scripts/image/stage-kde-runtime.sh`
  are runtime/staging adaptation boundaries, not evidence that imported
  WebKit/KDE source is patched in this checkout.
- `ports/xv6-gbm` is present only as an empty placeholder boundary.
- No `ports/chromium` source package was found. Chromium relevance is through
  launchers, fixtures, harnesses, and staged/host runtime payloads.

## Current Tree Mismatches To Keep Visible

| Item | Current state | Inventory treatment |
| --- | --- | --- |
| `alsapcmpoll` | Present in `user/programs`; not listed in the archived upstream de-patch plan. | Included as a local audio ABI probe. |
| `regpreservetest` | Present in `user/programs`. | Included as a local ABI regression test. |
| `ports/vim/xv6-vim-launcher.c` | Present beside imported Vim source. | Listed as xv6-owned launcher/support wrapper, not an imported Vim source patch. |
| `ports/peanut-gb` | Present in current `ports` tree though not in the earlier CMake wrapper list. | Listed as a demo dependency for local Wayland tools, not a KDE target. |
| `ports/xv6-gbm` | Present but empty. | Kept as `placeholder` local-shim boundary. |
| `build-x86_64/kde-noble-plasma` | Generated package evidence present. | Treated as `runtime-overlay`; no regeneration was performed. |
