#!/usr/bin/env bash
# make-rootfs.sh - build an ext4 root filesystem image from the staged sysroot.
# The kernel mounts this image as the root filesystem on virtio-blk
# (root=/dev/disk0).
#
# Usage:
#   make-rootfs.sh <sysroot_dir> <out_img> [size_mb|auto]
#
# The sysroot is expected to contain host-glibc Linux userland under bin/,
# lib/, lib64/, libexec/, usr/, share/, and etc/.
#
# Set ROOTFS_OVERLAY=/path/to/overlay to use an alternate overlay directory for
# one-off diagnostic images.  The default is the repository's rootfs-overlay.
# Set ROOTFS_EXTRA_OVERLAYS to a colon-separated list of generated overlays
# that should be layered after ROOTFS_OVERLAY.
set -euo pipefail

SYSROOT="${1:?usage: $0 <sysroot_dir> <out_img> [size_mb]}"
OUT="${2:?usage: $0 <sysroot_dir> <out_img> [size_mb]}"
SIZE_MB="${3:-64}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

if [[ ! -d "${SYSROOT}/bin" ]]; then
    echo "make-rootfs: ${SYSROOT}/bin not found - did you run 'cmake --build user --target install'?" >&2
    exit 1
fi

command -v mkfs.ext4 >/dev/null || { echo "make-rootfs: mkfs.ext4 not found (install e2fsprogs)" >&2; exit 1; }
command -v e2fsck   >/dev/null || { echo "make-rootfs: e2fsck not found (install e2fsprogs)" >&2; exit 1; }
command -v rsync    >/dev/null || { echo "make-rootfs: rsync not found"               >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

mkdir -p "${STAGE}"/{bin,dev,proc,sys,tmp,etc,root,lib,libexec,usr,share,var}
chmod 01777 "${STAGE}/tmp"

# 1. xv6-style user binaries: bin/_<name> -> /bin/<name>
shopt -s nullglob
for f in "${SYSROOT}/bin"/_*; do
    base="$(basename "$f")"
    cp "$f" "${STAGE}/bin/${base#_}"
done
# 2. Any other files in bin/ (no _ prefix) - copy verbatim, e.g. python3.12
for f in "${SYSROOT}/bin"/*; do
    base="$(basename "$f")"
    [[ "${base}" == _* ]] && continue
    cp -a "$f" "${STAGE}/bin/${base}"
done
shopt -u nullglob

# 3. Mirror dynamic-linker tree subdirs verbatim if present, preserving
#    symlinks/perms (these hold the host loader, libpython, stdlib, etc.).
for sub in lib libexec usr share etc; do
    if [[ -d "${SYSROOT}/${sub}" ]]; then
        rsync -aH "${SYSROOT}/${sub}/" "${STAGE}/${sub}/"
    fi
done

# 4. Copy top-level regular files in sysroot/ (e.g. diag.py, test_flask.py)
shopt -s nullglob
for f in "${SYSROOT}"/*; do
    [[ -f "$f" ]] || continue
    cp -a "$f" "${STAGE}/$(basename "$f")"
done
shopt -u nullglob

# 5. Apply the rootfs overlay (e.g. /etc/startup) on top of everything staged
#    so far. Path is resolved relative to this script so the default works
#    regardless of the caller's cwd.
OVERLAY="${ROOTFS_OVERLAY:-${REPO_ROOT}/rootfs-overlay}"
if [[ -d "${OVERLAY}" ]]; then
    rsync -aH "${OVERLAY}/" "${STAGE}/"
fi
if [[ -n "${ROOTFS_EXTRA_OVERLAYS:-}" ]]; then
    IFS=: read -r -a extra_overlays <<< "${ROOTFS_EXTRA_OVERLAYS}"
    for extra_overlay in "${extra_overlays[@]}"; do
        [[ -n "${extra_overlay}" ]] || continue
        if [[ -d "${extra_overlay}" ]]; then
            rsync -aH "${extra_overlay}/" "${STAGE}/"
        else
            echo "make-rootfs: warning: extra overlay not found: ${extra_overlay}" >&2
        fi
    done
fi

# Linux desktop daemons expect the conventional libmount runtime state.
# xv6 synthesizes /proc mounts in-kernel, but GLib/libmount also probes
# /run/mount and /etc/mtab while constructing Unix mount monitors.
mkdir -p "${STAGE}/run/mount"
ln -sfn /proc/self/mounts "${STAGE}/etc/mtab"
ln -sfn /usr/share/zoneinfo/Etc/UTC "${STAGE}/etc/localtime"

prune_overlay_graphics_runtime() {
    local dir
    local pattern

    for dir in \
        "${STAGE}/lib/x86_64-linux-gnu" \
        "${STAGE}/usr/lib" \
        "${STAGE}/usr/lib/x86_64-linux-gnu"; do
        [[ -d "${dir}" ]] || continue
        for pattern in \
            libEGL.so* \
            libGL.so* \
            libGLX.so* \
            libGLdispatch.so* \
            libGLES*.so* \
            libOpenGL.so* \
            libglapi.so* \
            libgbm.so* \
            libdrm.so* \
            libdrm_*.so* \
            libwayland-*.so* \
            libweston-*.so*; do
            find "${dir}" -maxdepth 1 \( -type f -o -type l \) -name "${pattern}" -delete
        done
    done
}

prune_overlay_graphics_runtime

stage_wayland_chromium_launcher() {
    local src="${REPO_ROOT}/scripts/image/wayland-chromium-launcher.c"
    local chrome="${STAGE}/opt/host-gui/wayland-chromium/chrome-linux64/chrome"
    local out="${STAGE}/bin/wayland-chromium"
    local cc_bin="${CC:-cc}"

    [[ -f "${src}" && -x "${chrome}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        if [[ -x "${out}" ]]; then
            echo "make-rootfs: warning: ${cc_bin} not found; keeping existing Chromium launcher" >&2
            return 0
        fi
        echo "make-rootfs: ${cc_bin} not found; cannot build Chromium launcher" >&2
        exit 1
    fi

    mkdir -p "${STAGE}/bin"
    "${cc_bin}" -O2 -Wall -Wextra -o "${out}" "${src}"
}

stage_wayland_chromium_desktop_entry() {
    local desktop="${STAGE}/usr/share/applications/xv6-wayland-chromium.desktop"

    [[ -x "${STAGE}/bin/wayland-chromium" ]] || return 0
    mkdir -p "$(dirname "${desktop}")"
    cat > "${desktop}" <<'EOF'
[Desktop Entry]
Type=Application
Name=Chromium
Exec=/bin/wayland-chromium
Icon=chromium
Categories=Network;WebBrowser;
Terminal=false
EOF
}

stage_plain_image_program() {
    local src="$1"
    local out="$2"
    local cc_bin="${CC:-cc}"

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        if [[ -x "${out}" ]]; then
            echo "make-rootfs: warning: ${cc_bin} not found; keeping existing $(basename "${out}")" >&2
            return 0
        fi
        echo "make-rootfs: ${cc_bin} not found; cannot build $(basename "${out}")" >&2
        exit 1
    fi

    mkdir -p "$(dirname "${out}")"
    "${cc_bin}" -O2 -Wall -Wextra -o "${out}" "${src}"
}

stage_login1_shim() {
    local src="${REPO_ROOT}/scripts/image/xv6-login1-shim.c"
    local out="${STAGE}/bin/xv6-login1-shim"
    local cc_bin="${CC:-cc}"
    local pcdir="${SYSROOT}/lib/pkgconfig"
    local cflags libs

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        echo "make-rootfs: ${cc_bin} not found; cannot build xv6-login1-shim" >&2
        exit 1
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "make-rootfs: pkg-config not found; cannot build xv6-login1-shim" >&2
        exit 1
    fi

    cflags="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --cflags gio-2.0 gio-unix-2.0
    )"
    libs="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --libs gio-2.0 gio-unix-2.0
    )"

    mkdir -p "${STAGE}/bin"
    # shellcheck disable=SC2086
    "${cc_bin}" -O2 -Wall -Wextra -Wl,-rpath,/lib -Wl,-rpath,/usr/lib \
        -o "${out}" ${cflags} "${src}" ${libs} -ldl
}

stage_bluez_shim() {
    local src="${REPO_ROOT}/scripts/image/xv6-bluez-shim.c"
    local out="${STAGE}/bin/xv6-bluez-shim"
    local cc_bin="${CC:-cc}"
    local pcdir="${SYSROOT}/lib/pkgconfig"
    local cflags libs

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        echo "make-rootfs: ${cc_bin} not found; cannot build xv6-bluez-shim" >&2
        exit 1
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "make-rootfs: pkg-config not found; cannot build xv6-bluez-shim" >&2
        exit 1
    fi

    cflags="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --cflags gio-2.0 gio-unix-2.0
    )"
    libs="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --libs gio-2.0 gio-unix-2.0
    )"

    mkdir -p "${STAGE}/bin"
    # shellcheck disable=SC2086
    "${cc_bin}" -O2 -Wall -Wextra -Wl,-rpath,/lib -Wl,-rpath,/usr/lib \
        -o "${out}" ${cflags} "${src}" ${libs}
}

stage_kde_libinput_probe() {
    local src="${REPO_ROOT}/scripts/image/kde-libinput-probe.c"
    local out="${STAGE}/bin/kde-libinput-probe"
    local cc_bin="${CC:-cc}"

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        echo "make-rootfs: ${cc_bin} not found; cannot build kde-libinput-probe" >&2
        exit 1
    fi

    mkdir -p "${STAGE}/bin"
    "${cc_bin}" -O2 -Wall -Wextra \
        -I"${SYSROOT}/include" -L"${SYSROOT}/lib" \
        -Wl,-rpath,/usr/lib/x86_64-linux-gnu -Wl,-rpath,/lib \
        -o "${out}" "${src}" -linput -ludev -pthread
}

stage_kde_proc_mountinfo_probe() {
    local src="${REPO_ROOT}/scripts/image/kde-proc-mountinfo-probe.c"
    local out="${STAGE}/bin/kde-proc-mountinfo-probe"
    local cc_bin="${CC:-cc}"
    local pcdir="${SYSROOT}/lib/pkgconfig"
    local cflags libs

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        echo "make-rootfs: ${cc_bin} not found; cannot build kde-proc-mountinfo-probe" >&2
        exit 1
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "make-rootfs: pkg-config not found; cannot build kde-proc-mountinfo-probe" >&2
        exit 1
    fi

    cflags="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --cflags gio-2.0 gio-unix-2.0
    )"
    libs="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --libs gio-2.0 gio-unix-2.0
    )"

    mkdir -p "${STAGE}/bin"
    # shellcheck disable=SC2086
    "${cc_bin}" -O2 -Wall -Wextra -Wl,-rpath,/lib -Wl,-rpath,/usr/lib \
        -o "${out}" ${cflags} "${src}" ${libs} -ldl
}

stage_kde_kwin_screenshot_probe() {
    local src="${REPO_ROOT}/scripts/image/kde-kwin-screenshot-probe.c"
    local out="${STAGE}/bin/kde-kwin-screenshot-probe"
    local desktop="${STAGE}/usr/share/applications/org.xv6.kde-kwin-screenshot-probe.desktop"
    local cc_bin="${CC:-cc}"
    local pcdir="${SYSROOT}/lib/pkgconfig"
    local cflags libs

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        echo "make-rootfs: ${cc_bin} not found; cannot build kde-kwin-screenshot-probe" >&2
        exit 1
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "make-rootfs: pkg-config not found; cannot build kde-kwin-screenshot-probe" >&2
        exit 1
    fi

    cflags="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --cflags gio-2.0 gio-unix-2.0
    )"
    libs="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --libs gio-2.0 gio-unix-2.0
    )"

    mkdir -p "${STAGE}/bin" "$(dirname "${desktop}")"
    # shellcheck disable=SC2086
    "${cc_bin}" -O2 -Wall -Wextra -Wl,-rpath,/lib -Wl,-rpath,/usr/lib \
        -o "${out}" ${cflags} "${src}" ${libs}
    cat > "${desktop}" <<'EOF'
[Desktop Entry]
Type=Application
Name=xv6 KDE KWin Screenshot Probe
Exec=/bin/kde-kwin-screenshot-probe
NoDisplay=true
X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
EOF
}

stage_kde_drm_probe() {
    local src="${REPO_ROOT}/scripts/image/kde-drm-probe.c"
    local out="${STAGE}/bin/kde-drm-probe"
    local cc_bin="${CC:-cc}"

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        echo "make-rootfs: ${cc_bin} not found; cannot build kde-drm-probe" >&2
        exit 1
    fi

    mkdir -p "${STAGE}/bin"
    "${cc_bin}" -O2 -Wall -Wextra \
        -I"${SYSROOT}/include" -I"${SYSROOT}/include/libdrm" \
        -L"${SYSROOT}/lib" -Wl,-rpath,/lib \
        -o "${out}" "${src}" -ldrm
}

stage_kde_wayland_seat_probe() {
    local src="${REPO_ROOT}/scripts/image/kde-wayland-seat-probe.c"
    local out="${STAGE}/bin/kde-wayland-seat-probe"
    local cc_bin="${CC:-cc}"
    local pcdir="${SYSROOT}/lib/pkgconfig"
    local cflags libs

    [[ -f "${src}" ]] || return 0
    if ! command -v "${cc_bin}" >/dev/null 2>&1; then
        echo "make-rootfs: ${cc_bin} not found; cannot build kde-wayland-seat-probe" >&2
        exit 1
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
        echo "make-rootfs: pkg-config not found; cannot build kde-wayland-seat-probe" >&2
        exit 1
    fi

    cflags="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --cflags wayland-client
    )"
    libs="$(
        PKG_CONFIG_LIBDIR="${pcdir}" PKG_CONFIG_SYSROOT_DIR="${SYSROOT}" \
            pkg-config --libs wayland-client
    )"

    mkdir -p "${STAGE}/bin"
    # shellcheck disable=SC2086
    "${cc_bin}" -O2 -Wall -Wextra -Wl,-rpath,/lib \
        -o "${out}" ${cflags} "${src}" ${libs}
}

stage_kde_abi_overrides() {
    local dir="${STAGE}/opt/xv6-kde-abi-libs"

    mkdir -p "${dir}"
    ln -sfn /lib/libinput.so.10 "${dir}/libinput.so.10"
    ln -sfn /lib/libudev.so.1 "${dir}/libudev.so.1"
    ln -sfn /lib/libdrm.so.2 "${dir}/libdrm.so.2"
}

stage_kde_session_launchers() {
    if [[ -x "${STAGE}/bin/Xwayland" ]]; then
        mkdir -p "${STAGE}/usr/bin"
        rm -f "${STAGE}/usr/bin/Xwayland"
        ln -sfn ../../bin/Xwayland "${STAGE}/usr/bin/Xwayland"
    fi
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-session.c" \
        "${STAGE}/bin/kde-session"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/xv6-desktop-session.c" \
        "${STAGE}/bin/xv6-desktop-session"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/xv6-false.c" \
        "${STAGE}/bin/xv6-false"
    ln -sf /bin/xv6-false "${STAGE}/bin/false"
    mkdir -p "${STAGE}/usr/bin"
    ln -sf /bin/xv6-false "${STAGE}/usr/bin/false"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-plasma-session-child.c" \
        "${STAGE}/bin/kde-plasma-session-child"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-abi-probe.c" \
        "${STAGE}/bin/kde-abi-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-dlopen-probe.c" \
        "${STAGE}/bin/kde-dlopen-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-unix-socket-probe.c" \
        "${STAGE}/bin/kde-unix-socket-probe"
    stage_kde_wayland_seat_probe
    stage_kde_drm_probe
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-process-probe.c" \
        "${STAGE}/bin/kde-process-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-app-launch-probe.c" \
        "${STAGE}/bin/kde-app-launch-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-smoke-agent.c" \
        "${STAGE}/bin/kde-smoke-agent"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-konsole-shell-wrapper.c" \
        "${STAGE}/bin/kde-konsole-shell-wrapper"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-terminal-launcher.c" \
        "${STAGE}/bin/kde-terminal-launcher"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/qt-wayland-smoke-launcher.c" \
        "${STAGE}/bin/qt-wayland-smoke-launcher"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-config-atomic-probe.c" \
        "${STAGE}/bin/kde-config-atomic-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-pulse-cookie-probe.c" \
        "${STAGE}/bin/kde-pulse-cookie-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-proc-comm-probe.c" \
        "${STAGE}/bin/kde-proc-comm-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-pty-shell-probe.c" \
        "${STAGE}/bin/kde-pty-shell-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-pty-openpty-probe.c" \
        "${STAGE}/bin/kde-pty-openpty-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-pty-readiness-probe.c" \
        "${STAGE}/bin/kde-pty-readiness-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-kwriteconfig-probe.c" \
        "${STAGE}/bin/kde-kwriteconfig-probe"
    stage_plain_image_program "${REPO_ROOT}/scripts/image/kde-trash-stat-probe.c" \
        "${STAGE}/bin/kde-trash-stat-probe"
    stage_login1_shim
    stage_bluez_shim
    stage_kde_libinput_probe
    stage_kde_proc_mountinfo_probe
    stage_kde_kwin_screenshot_probe
    stage_kde_abi_overrides
}

# libxkbcommon looks for X11 Compose tables at runtime.  Stage the compact
# locale test data we already build with libxkbcommon so Weston clients do not
# warn and disable compose when the host image lacks full X11 locale files.
XKB_COMPOSE_SRC="${REPO_ROOT}/ports/libxkbcommon/src/test/data/locale"
if [[ ! -f "${STAGE}/share/X11/locale/compose.dir" &&
      -f "${XKB_COMPOSE_SRC}/compose.dir" ]]; then
    mkdir -p "${STAGE}/share/X11/locale"
    rsync -aH "${XKB_COMPOSE_SRC}/" "${STAGE}/share/X11/locale/"
fi
if [[ -d "${STAGE}/share/X11/locale" &&
      ! -e "${STAGE}/usr/share/X11/locale" ]]; then
    mkdir -p "${STAGE}/usr/share/X11"
    ln -sfn ../../../share/X11/locale "${STAGE}/usr/share/X11/locale"
fi

# 5b. Runtime configuration expected by network clients and OpenSSH.
cat > "${STAGE}/etc/hosts" <<'EOF'
127.0.0.1 localhost
EOF

cat > "${STAGE}/etc/resolv.conf" <<'EOF'
nameserver 10.0.2.3
EOF

cat > "${STAGE}/etc/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/sh
messagebus:x:101:101:System Message Bus:/nonexistent:/bin/false
polkitd:x:102:102:User for polkitd:/nonexistent:/bin/false
systemd-network:x:103:103:systemd Network Management:/nonexistent:/bin/false
avahi:x:104:104:Avahi mDNS daemon:/nonexistent:/bin/false
dnsmasq:x:105:105:dnsmasq daemon:/nonexistent:/bin/false
geoclue:x:106:106:Geoclue daemon:/nonexistent:/bin/false
whoopsie:x:107:107:Crash report daemon:/nonexistent:/bin/false
rtkit:x:108:108:RealtimeKit daemon:/nonexistent:/bin/false
systemd-resolve:x:109:109:systemd Resolver:/nonexistent:/bin/false
systemd-timesync:x:110:110:systemd Time Synchronization:/nonexistent:/bin/false
sshd:x:74:74:Privilege-separated SSH:/var/empty:/bin/false
guest:x:1000:1000:Guest User:/home/guest:/bin/sh
nobody:x:65534:65534:Nobody:/nonexistent:/bin/false
EOF

cat > "${STAGE}/etc/group" <<'EOF'
root:x:0:root
messagebus:x:101:
polkitd:x:102:
systemd-network:x:103:
avahi:x:104:
dnsmasq:x:105:
geoclue:x:106:
whoopsie:x:107:
rtkit:x:108:
systemd-resolve:x:109:
systemd-timesync:x:110:
netdev:x:111:
bluetooth:x:112:
wheel:x:10:root
sshd:x:74:
guest:x:1000:guest
nogroup:x:65534:
EOF

cat > "${STAGE}/etc/shadow" <<'EOF'
root::20517:0:99999:7:::
messagebus:!:20517:0:99999:7:::
polkitd:!:20517:0:99999:7:::
systemd-network:!:20517:0:99999:7:::
avahi:!:20517:0:99999:7:::
dnsmasq:!:20517:0:99999:7:::
geoclue:!:20517:0:99999:7:::
whoopsie:!:20517:0:99999:7:::
rtkit:!:20517:0:99999:7:::
systemd-resolve:!:20517:0:99999:7:::
systemd-timesync:!:20517:0:99999:7:::
sshd:!:20517:0:99999:7:::
guest:!:20517:0:99999:7:::
nobody:!:20517:0:99999:7:::
EOF
chmod 0600 "${STAGE}/etc/shadow"

cat > "${STAGE}/etc/shells" <<'EOF'
/bin/sh
EOF

# Keep OpenSSL/GIO TLS clients on the same trust roots as NetSurf. OpenSSL's
# built-in OPENSSLDIR is /etc/ssl, so stage the bundle there for programs that
# do not receive an explicit SSL_CERT_FILE.
mkdir -p "${STAGE}/etc/ssl/certs"
if [[ -f "${STAGE}/share/netsurf/ca-bundle" ]]; then
    cp -a "${STAGE}/share/netsurf/ca-bundle" "${STAGE}/etc/ssl/certs/ca-certificates.crt"
    ln -sf certs/ca-certificates.crt "${STAGE}/etc/ssl/cert.pem"
elif [[ -f /etc/ssl/certs/ca-certificates.crt ]]; then
    cp -a /etc/ssl/certs/ca-certificates.crt "${STAGE}/etc/ssl/certs/ca-certificates.crt"
    ln -sf certs/ca-certificates.crt "${STAGE}/etc/ssl/cert.pem"
fi

mkdir -p "${STAGE}/root/.ssh" "${STAGE}/root/desktop" "${STAGE}/home/guest" "${STAGE}/var/empty" "${STAGE}/var/run" "${STAGE}/etc/ssh"
rm -rf "${STAGE}/root/Desktop"
chmod 0700 "${STAGE}/root/.ssh"
chmod 0755 "${STAGE}/var/empty"

# Host-built GStreamer registry files store the absolute sysroot path used
# during staging.  Preserve that path as a symlink inside the guest so the
# prebuilt registry can be used read-only instead of rescanning plugins during
# WebKit startup.
if [[ -f "${STAGE}/share/gstreamer-1.0/registry.x86_64.bin" ]]; then
    mkdir -p "${STAGE}/tmp/xv6-hyperv-build"
    ln -sfn / "${STAGE}/tmp/xv6-hyperv-build/sysroot"
fi

stage_wayland_chromium_launcher
stage_wayland_chromium_desktop_entry
stage_kde_session_launchers

stage_kde_desktop_shortcut() {
    local name="$1"
    local source="$2"
    local out="${STAGE}/root/Desktop/${name}.desktop"

    [[ -f "${STAGE}${source}" ]] || return 0
    mkdir -p "${STAGE}/root/Desktop"
    cp -a "${STAGE}${source}" "${out}"
    if ! grep -q '^X-KDE-Trusted=' "${out}"; then
        printf '\nX-KDE-Trusted=true\n' >> "${out}"
    fi
    chmod 0755 "${out}"
}

rm -f "${STAGE}/root/Desktop/Terminal.desktop"
stage_kde_desktop_shortcut "Konsole" "/usr/share/applications/org.kde.konsole.desktop"
if [[ -x "${STAGE}/usr/bin/qterminal" || -x "${STAGE}/usr/bin/xterm" ]]; then
    mkdir -p "${STAGE}/usr/share/applications"
    cat > "${STAGE}/usr/share/applications/xv6-terminal.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Terminal
GenericName=Terminal Emulator
Comment=Open an interactive shell
Exec=/bin/kde-terminal-launcher
Icon=utilities-terminal
Terminal=false
Categories=System;TerminalEmulator;
StartupNotify=true
X-KDE-Trusted=true
EOF
    stage_kde_desktop_shortcut "Terminal" "/usr/share/applications/xv6-terminal.desktop"
fi
stage_kde_desktop_shortcut "Files" "/usr/share/applications/org.kde.dolphin.desktop"
if [[ -f "${STAGE}/usr/share/applications/org.kde.kwrite.desktop" ]]; then
    stage_kde_desktop_shortcut "Text Editor" "/usr/share/applications/org.kde.kwrite.desktop"
else
    stage_kde_desktop_shortcut "Text Editor" "/usr/share/applications/org.kde.kate.desktop"
fi
stage_kde_desktop_shortcut "Chromium" "/usr/share/applications/xv6-wayland-chromium.desktop"

find "${STAGE}/root/desktop" -maxdepth 1 -type f -name '*.desktop' -delete

write_desktop_link() {
    local name="$1"
    local target="$2"
    local path="${STAGE}/root/desktop/${name}"

    rm -f "${path}"
    ln -s "${target}" "${path}"
}

write_desktop_link_if_executable() {
    local name="$1"
    local target="$2"

    if [[ -x "${STAGE}${target}" ]]; then
        write_desktop_link "${name}" "${target}"
    else
        rm -f "${STAGE}/root/desktop/${name}"
    fi
}

prune_broken_desktop_links() {
    local path link target

    shopt -s nullglob
    for path in "${STAGE}/root/desktop"/*; do
        [[ -L "${path}" ]] || continue
        link="$(readlink "${path}")" || continue
        if [[ "${link}" = /* ]]; then
            target="${STAGE}${link}"
        else
            target="$(realpath -m "$(dirname "${path}")/${link}")"
        fi
        if [[ "${target}" != "${STAGE}/"* || ! -x "${target}" ]]; then
            echo "make-rootfs: removing broken desktop link $(basename "${path}") -> ${link}" >&2
            rm -f "${path}"
        fi
    done
    shopt -u nullglob
}

prune_probe_desktop_links() {
    local path

    shopt -s nullglob
    for path in "${STAGE}/root/desktop"/imported-host-*; do
        [[ -e "${path}" || -L "${path}" ]] || continue
        echo "make-rootfs: hiding probe desktop link $(basename "${path}")" >&2
        rm -f "${path}"
    done
    shopt -u nullglob
}

write_desktop_link_if_executable "Terminal" "/bin/weston-terminal"
write_desktop_link_if_executable "Files" "/bin/xv6-open-files-root"
write_desktop_link_if_executable "Proc Files" "/bin/xv6-open-files-proc"
write_desktop_link_if_executable "Python" "/bin/xv6-open-python"
write_desktop_link_if_executable "Settings" "/bin/xv6-settings"
write_desktop_link_if_executable "3D Demo" "/bin/mesademo"

if [[ -x "${STAGE}/bin/glmaze" ]]; then
    write_desktop_link "GL Maze" "/bin/glmaze"
fi

if [[ -x "${STAGE}/bin/glsmoke" ]]; then
    write_desktop_link "GL Smoke" "/bin/glsmoke"
fi

if [[ -x "${STAGE}/bin/mesaglsmoke" &&
      -x "${STAGE}/bin/xv6-open-gl-sphere" ]]; then
    write_desktop_link "GL Sphere" "/bin/xv6-open-gl-sphere"
fi

if [[ -x "${STAGE}/bin/mesawlegl" &&
      -x "${STAGE}/bin/xv6-open-egl-demo" ]]; then
    write_desktop_link "EGL Demo" "/bin/xv6-open-egl-demo"
fi

if [[ -x "${STAGE}/bin/peanutgb" &&
      -f "${STAGE}/root/roms/dmg-acid2.gb" &&
      -x "${STAGE}/bin/xv6-open-game-boy" ]]; then
    write_desktop_link "Game Boy" "/bin/xv6-open-game-boy"
fi

write_desktop_link_if_executable "Editor" "/bin/xv6-open-editor"
write_desktop_link_if_executable "Browser" "/bin/netsurf"
write_desktop_link_if_executable "Chromium" "/bin/wayland-chromium"

is_webkit_placeholder() {
    local path="$1"
    [[ -f "${path}" ]] || return 1
    head -n 4 "${path}" 2>/dev/null |
        grep -q 'no host-glibc WebKitGTK runtime staged'
}

if [[ -x "${STAGE}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
    write_desktop_link_if_executable "WebKit" "/bin/xv6-open-webkit"
elif [[ -x "${STAGE}/bin/webkitgpusmoke" ]] &&
     ! is_webkit_placeholder "${STAGE}/bin/webkitgpusmoke"; then
    write_desktop_link "WebKit" "/bin/webkitgpusmoke"
else
    rm -f "${STAGE}/root/desktop/WebKit"
fi

prune_probe_desktop_links
prune_broken_desktop_links

if command -v ssh-keygen >/dev/null 2>&1; then
    ssh-keygen -t ed25519 -f "${STAGE}/etc/ssh/ssh_host_ed25519_key" -N "" -q 2>/dev/null || true
    ssh-keygen -t rsa -b 2048 -f "${STAGE}/etc/ssh/ssh_host_rsa_key" -N "" -q 2>/dev/null || true
    ssh-keygen -t ecdsa -b 256 -f "${STAGE}/etc/ssh/ssh_host_ecdsa_key" -N "" -q 2>/dev/null || true
else
    echo "make-rootfs: warning: host ssh-keygen not found; SSH host keys were not generated" >&2
fi

cat > "${STAGE}/etc/ssh/sshd_config" <<'EOF'
Port 22
ListenAddress 0.0.0.0
HostKey /etc/ssh/ssh_host_ed25519_key
HostKey /etc/ssh/ssh_host_rsa_key
HostKey /etc/ssh/ssh_host_ecdsa_key
PermitRootLogin yes
PasswordAuthentication yes
PermitEmptyPasswords yes
PubkeyAuthentication yes
AuthorizedKeysFile .ssh/authorized_keys
SyslogFacility AUTH
LogLevel INFO
UseDNS no
PrintMotd no
Compression no
X11Forwarding no
GatewayPorts no
TCPKeepAlive yes
Subsystem sftp /bin/sftp
AcceptEnv LANG LC_*
EOF

cat > "${STAGE}/etc/ssh/ssh_config" <<'EOF'
Host *
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    LogLevel ERROR
EOF

if command -v readelf >/dev/null 2>&1 &&
   find "${STAGE}/bin" "${STAGE}/libexec" -type f -perm -111 -print0 2>/dev/null |
   xargs -0 -r readelf -l 2>/dev/null |
   grep -q 'Requesting program interpreter: /lib/ld-musl-'; then
    echo "make-rootfs: musl-linked executable found; rebuild it with host glibc" >&2
    exit 1
fi

stage_host_glibc() {
    command -v readelf >/dev/null 2>&1 || {
        echo "make-rootfs: warning: readelf not found; cannot stage host glibc" >&2
        return 0
    }
    command -v ldd >/dev/null 2>&1 || {
        echo "make-rootfs: warning: ldd not found; cannot stage host glibc" >&2
        return 0
    }

    local exe interp lib
    while IFS= read -r -d '' exe; do
        readelf -h "$exe" >/dev/null 2>&1 || continue
        interp="$(
            readelf -l "$exe" 2>/dev/null |
            sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p'
        )" || true
        if [[ -n "${interp}" && "${interp}" != "/lib64/ld-linux-x86-64.so.2" ]]; then
            continue
        fi

        if [[ -e "${STAGE}${interp}" ]]; then
            :
        elif [[ -e "${interp}" ]]; then
            mkdir -p "${STAGE}$(dirname "${interp}")"
            cp -L "${interp}" "${STAGE}${interp}"
            chmod 0755 "${STAGE}${interp}"
        elif [[ -n "${interp}" ]]; then
            echo "make-rootfs: warning: host loader missing: ${interp}" >&2
        fi

        while IFS= read -r lib; do
            [[ -n "${lib}" && -e "${lib}" ]] || continue
            [[ "${lib}" == "${STAGE}/"* ]] && continue
            stage_host_path "${lib}"
        done < <(
            LD_LIBRARY_PATH="${STAGE}/lib:${STAGE}/usr/lib:${STAGE}/lib/x86_64-linux-gnu:${STAGE}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
                ldd "$exe" 2>/dev/null |
            awk '
                /=> \// { print $3; next }
                /^[[:space:]]*\// { print $1; next }
            '
        )
    done < <(find "${STAGE}/bin" "${STAGE}/usr/bin" \
        "${STAGE}/libexec" "${STAGE}/usr/libexec" \
        -type f -perm -111 -print0 2>/dev/null)
}

stage_has_sysroot_library() {
    local src="$1"
    local base

    base="$(basename "${src}")"
    case "${base}" in
        *.so|*.so.*) ;;
        *) return 1 ;;
    esac

    for dir in "${STAGE}/lib" "${STAGE}/usr/lib" "${STAGE}/lib64"; do
        [[ -e "${dir}/${base}" ]] && return 0
    done
    return 1
}

stage_host_path() {
    local src="$1"
    [[ -e "${src}" ]] || return 0
    [[ "${src}" == "${STAGE}/"* ]] && return 0
    if stage_has_sysroot_library "${src}" && [[ ! -e "${STAGE}${src}" ]]; then
        return 0
    fi
    mkdir -p "${STAGE}$(dirname "${src}")"
    cp -L "${src}" "${STAGE}${src}"
    chmod 0755 "${STAGE}${src}" 2>/dev/null || true
}

stage_host_path_force() {
    local src="$1"
    [[ -e "${src}" ]] || return 0
    [[ "${src}" == "${STAGE}/"* ]] && return 0
    mkdir -p "${STAGE}$(dirname "${src}")"
    cp -L "${src}" "${STAGE}${src}"
    chmod 0755 "${STAGE}${src}" 2>/dev/null || true
}

stage_ldd_dependencies() {
    local obj="$1"
    command -v ldd >/dev/null 2>&1 || return 0
    while IFS= read -r lib; do
        [[ -n "${lib}" && -e "${lib}" ]] || continue
        [[ "${lib}" == "${STAGE}/"* ]] && continue
        stage_host_path "${lib}"
    done < <(
        LD_LIBRARY_PATH="${STAGE}/lib:${STAGE}/usr/lib:${STAGE}/lib/x86_64-linux-gnu:${STAGE}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
            ldd "${obj}" 2>/dev/null |
        awk '
            /=> \// { print $3; next }
            /^[[:space:]]*\// { print $1; next }
        '
    )
}

stage_ldd_dependencies_force() {
    local obj="$1"
    command -v ldd >/dev/null 2>&1 || return 0
    while IFS= read -r lib; do
        [[ -n "${lib}" && -e "${lib}" ]] || continue
        stage_host_path_force "${lib}"
    done < <(
        LD_LIBRARY_PATH="${STAGE}/lib:${STAGE}/usr/lib:${STAGE}/lib/x86_64-linux-gnu:${STAGE}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
            ldd "${obj}" 2>/dev/null |
        awk '
            /=> \// { print $3; next }
            /^[[:space:]]*\// { print $1; next }
        '
    )
}

stage_mesa_runtime() {
    local path
    shopt -s nullglob

    for path in \
        /usr/lib/x86_64-linux-gnu/libOpenGL.so* \
        /usr/lib/x86_64-linux-gnu/libEGL_mesa.so* \
        /usr/lib/x86_64-linux-gnu/dri/swrast_dri.so \
        /usr/lib/x86_64-linux-gnu/dri/kms_swrast_dri.so; do
        stage_host_path "${path}"
        stage_ldd_dependencies "${path}"
    done

    for path in /usr/share/glvnd/egl_vendor.d/*.json; do
        stage_host_path "${path}"
    done

    # Host Chromium's X11/GLX path asks GLVND for libGLX_mesa.so.0.  The
    # guest Mesa port currently provides EGL/Wayland and the GLVND frontend,
    # but not Mesa's GLX vendor module.  Stage the upstream host Mesa GLX
    # closure coherently instead of mixing libGLX_mesa with the guest Mesa
    # gallium DSO.
    for path in \
        /lib/x86_64-linux-gnu/libGLX_mesa.so* \
        /usr/lib/x86_64-linux-gnu/libGLX_mesa.so* \
        /lib/x86_64-linux-gnu/libGLX_indirect.so* \
        /usr/lib/x86_64-linux-gnu/libGLX_indirect.so* \
        /usr/lib/x86_64-linux-gnu/dri/virtio_gpu_dri.so \
        /usr/lib/x86_64-linux-gnu/dri/swrast_dri.so \
        /usr/lib/x86_64-linux-gnu/dri/kms_swrast_dri.so; do
        stage_host_path_force "${path}"
        stage_ldd_dependencies_force "${path}"
    done

    shopt -u nullglob
}

stage_host_glibc
stage_mesa_runtime

if [[ "${SIZE_MB}" == "auto" ]]; then
    stage_kib="$(du -sk "${STAGE}" | awk '{print $1}')"
    # ext4 -d needs room for metadata, directories, and future runtime writes.
    # Full Plasma images have enough first-run cache/config churn that a small
    # percentage cushion still leaves KDE without practical breathing room.
    SIZE_MB=$(( (stage_kib * 170 / 100 + 1048576 + 1023) / 1024 ))
    if (( SIZE_MB < 1024 )); then
        SIZE_MB=1024
    fi
    SIZE_MB=$(( ((SIZE_MB + 127) / 128) * 128 ))
fi

if ! [[ "${SIZE_MB}" =~ ^[0-9]+$ ]] || (( SIZE_MB <= 0 )); then
    echo "make-rootfs: invalid image size '${SIZE_MB}' (expected MiB or auto)" >&2
    exit 1
fi

rm -f "${OUT}"
truncate -s "${SIZE_MB}M" "${OUT}"
mkfs.ext4 -F -L xv6root \
    -O '^has_journal,^metadata_csum,^64bit,^ext_attr,^resize_inode' \
    -d "${STAGE}" "${OUT}" >/dev/null

if command -v debugfs >/dev/null 2>&1; then
    debugfs -w "${OUT}" <<'EOF' >/dev/null 2>&1 || true
set_inode_field /var uid 0
set_inode_field /var gid 0
set_inode_field /var/empty uid 0
set_inode_field /var/empty gid 0
set_inode_field /var/empty mode 040755
set_inode_field /root uid 0
set_inode_field /root gid 0
set_inode_field /root/Desktop uid 0
set_inode_field /root/Desktop gid 0
set_inode_field /root/Desktop/Chromium.desktop uid 0
set_inode_field /root/Desktop/Chromium.desktop gid 0
set_inode_field /root/Desktop/Files.desktop uid 0
set_inode_field /root/Desktop/Files.desktop gid 0
set_inode_field /root/Desktop/Konsole.desktop uid 0
set_inode_field /root/Desktop/Konsole.desktop gid 0
set_inode_field "/root/Desktop/Text Editor.desktop" uid 0
set_inode_field "/root/Desktop/Text Editor.desktop" gid 0
set_inode_field /root/.ssh uid 0
set_inode_field /root/.ssh gid 0
set_inode_field /root/.ssh mode 040700
set_inode_field /etc uid 0
set_inode_field /etc gid 0
set_inode_field /etc/passwd uid 0
set_inode_field /etc/passwd gid 0
set_inode_field /etc/group uid 0
set_inode_field /etc/group gid 0
set_inode_field /etc/shadow uid 0
set_inode_field /etc/shadow gid 0
set_inode_field /etc/shadow mode 0100600
set_inode_field /etc/ssh uid 0
set_inode_field /etc/ssh gid 0
set_inode_field /etc/ssh/ssh_config uid 0
set_inode_field /etc/ssh/ssh_config gid 0
set_inode_field /etc/ssh/sshd_config uid 0
set_inode_field /etc/ssh/sshd_config gid 0
set_inode_field /etc/ssh/ssh_host_ed25519_key uid 0
set_inode_field /etc/ssh/ssh_host_ed25519_key gid 0
set_inode_field /etc/ssh/ssh_host_ed25519_key mode 0100600
set_inode_field /etc/ssh/ssh_host_ed25519_key.pub uid 0
set_inode_field /etc/ssh/ssh_host_ed25519_key.pub gid 0
set_inode_field /etc/ssh/ssh_host_rsa_key uid 0
set_inode_field /etc/ssh/ssh_host_rsa_key gid 0
set_inode_field /etc/ssh/ssh_host_rsa_key mode 0100600
set_inode_field /etc/ssh/ssh_host_rsa_key.pub uid 0
set_inode_field /etc/ssh/ssh_host_rsa_key.pub gid 0
set_inode_field /etc/ssh/ssh_host_ecdsa_key uid 0
set_inode_field /etc/ssh/ssh_host_ecdsa_key gid 0
set_inode_field /etc/ssh/ssh_host_ecdsa_key mode 0100600
set_inode_field /etc/ssh/ssh_host_ecdsa_key.pub uid 0
set_inode_field /etc/ssh/ssh_host_ecdsa_key.pub gid 0
EOF
fi

e2fsck -fy "${OUT}" >/dev/null

echo "make-rootfs: wrote ${OUT} (${SIZE_MB} MiB ext4, label=xv6root) from ${SYSROOT}"
