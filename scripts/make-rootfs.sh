#!/usr/bin/env bash
# make-rootfs.sh - build an ext4 root filesystem image from the staged sysroot.
# The kernel mounts this image as the root filesystem on virtio-blk
# (root=/dev/disk0).
#
# Usage:
#   make-rootfs.sh <sysroot_dir> <out_img> [size_mb]
#
# The sysroot is expected to contain host-glibc Linux userland under bin/,
# lib/, lib64/, libexec/, usr/, share/, and etc/.
#
# Set ROOTFS_OVERLAY=/path/to/overlay to use an alternate overlay directory for
# one-off diagnostic images.  The default is the repository's rootfs-overlay.
set -euo pipefail

SYSROOT="${1:?usage: $0 <sysroot_dir> <out_img> [size_mb]}"
OUT="${2:?usage: $0 <sysroot_dir> <out_img> [size_mb]}"
SIZE_MB="${3:-64}"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

if [[ ! -d "${SYSROOT}/bin" ]]; then
    echo "make-rootfs: ${SYSROOT}/bin not found - did you run 'cmake --build user --target install'?" >&2
    exit 1
fi

command -v mkfs.ext4 >/dev/null || { echo "make-rootfs: mkfs.ext4 not found (install e2fsprogs)" >&2; exit 1; }
command -v rsync    >/dev/null || { echo "make-rootfs: rsync not found"               >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "${STAGE}"' EXIT

mkdir -p "${STAGE}"/{bin,dev,proc,sys,tmp,etc,root,lib,libexec,usr,share,var}

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
OVERLAY="${ROOTFS_OVERLAY:-${SCRIPT_DIR}/../rootfs-overlay}"
if [[ -d "${OVERLAY}" ]]; then
    rsync -aH "${OVERLAY}/" "${STAGE}/"
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
sshd:x:74:74:Privilege-separated SSH:/var/empty:/bin/false
guest:x:1000:1000:Guest User:/home/guest:/bin/sh
nobody:x:65534:65534:Nobody:/nonexistent:/bin/false
EOF

cat > "${STAGE}/etc/group" <<'EOF'
root:x:0:root
wheel:x:10:root
sshd:x:74:
guest:x:1000:guest
nogroup:x:65534:
EOF

cat > "${STAGE}/etc/shadow" <<'EOF'
root::20517:0:99999:7:::
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

mkdir -p "${STAGE}/root/.ssh" "${STAGE}/root/Desktop" "${STAGE}/home/guest" "${STAGE}/var/empty" "${STAGE}/var/run" "${STAGE}/etc/ssh"
chmod 0700 "${STAGE}/root/.ssh"
chmod 0755 "${STAGE}/var/empty"

cat > "${STAGE}/root/Desktop/terminal.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Terminal
X-XV6-Builtin=terminal
IconChar=>
IconColor=0xFF3D6E9E
EOF

cat > "${STAGE}/root/Desktop/files.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Files
X-XV6-Builtin=files
IconChar=F
IconColor=0xFFA67C52
EOF

cat > "${STAGE}/root/Desktop/info.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Info
X-XV6-Builtin=sysinfo
IconChar=i
IconColor=0xFF3DA67C
EOF

cat > "${STAGE}/root/Desktop/calc.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Calc
X-XV6-Builtin=calc
IconChar=C
IconColor=0xFF7C3DA6
EOF

cat > "${STAGE}/root/Desktop/network.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Network
X-XV6-Builtin=network
IconChar=N
IconColor=0xFF3DA6A6
EOF

cat > "${STAGE}/root/Desktop/settings.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Settings
X-XV6-Builtin=settings
IconChar=S
IconColor=0xFF7B7B7B
EOF

cat > "${STAGE}/root/Desktop/monitor.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Monitor
X-XV6-Builtin=monitor
IconChar=M
IconColor=0xFFA63D7C
EOF

cat > "${STAGE}/root/Desktop/3ddemo.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=3D Demo
Exec=/bin/mesaglsmoke
Arg=--demo
IconChar=3
IconColor=0xFF6EA63D
EOF

cat > "${STAGE}/root/Desktop/editor.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Editor
X-XV6-Builtin=editor
IconChar=V
IconColor=0xFFA65C3D
EOF

cat > "${STAGE}/root/Desktop/browser.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Browser
Exec=/bin/netsurf
IconChar=W
IconColor=0xFF3D6E9E
EOF

is_webkit_placeholder() {
    local path="$1"
    [[ -f "${path}" ]] || return 1
    head -n 4 "${path}" 2>/dev/null |
        grep -q 'no host-glibc WebKitGTK runtime staged'
}

if [[ -x "${STAGE}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
cat > "${STAGE}/root/Desktop/webkit.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=WebKit
Exec=/libexec/webkit2gtk-4.1/MiniBrowser
Arg=https://www.google.com/
IconChar=K
IconColor=0xFF9B59B6
EOF
elif [[ -x "${STAGE}/bin/webkitgpusmoke" ]] &&
     ! is_webkit_placeholder "${STAGE}/bin/webkitgpusmoke"; then
cat > "${STAGE}/root/Desktop/webkit.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=WebKit
Exec=/bin/webkitgpusmoke
Arg=file:///share/webkit/gpu-smoke.html
IconChar=K
IconColor=0xFF9B59B6
EOF
else
    rm -f "${STAGE}/root/Desktop/webkit.desktop"
fi

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

        if [[ -e "${interp}" ]]; then
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
    done < <(find "${STAGE}/bin" "${STAGE}/libexec" "${STAGE}/lib" "${STAGE}/usr/lib" \
        -type f \( -perm -111 -o -name '*.so' -o -name '*.so.*' \) \
        -print0 2>/dev/null)
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
    if stage_has_sysroot_library "${src}" && [[ ! -e "${STAGE}${src}" ]]; then
        return 0
    fi
    mkdir -p "${STAGE}$(dirname "${src}")"
    cp -L "${src}" "${STAGE}${src}"
    chmod 0755 "${STAGE}${src}" 2>/dev/null || true
}

stage_ldd_dependencies() {
    local obj="$1"
    command -v ldd >/dev/null 2>&1 || return 0
    while IFS= read -r lib; do
        [[ -n "${lib}" && -e "${lib}" ]] || continue
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

stage_mesa_runtime() {
    local path
    shopt -s nullglob

    for path in \
        /usr/lib/x86_64-linux-gnu/libEGL_mesa.so* \
        /usr/lib/x86_64-linux-gnu/dri/swrast_dri.so \
        /usr/lib/x86_64-linux-gnu/dri/kms_swrast_dri.so; do
        stage_host_path "${path}"
        stage_ldd_dependencies "${path}"
    done

    for path in /usr/share/glvnd/egl_vendor.d/*.json; do
        stage_host_path "${path}"
    done

    shopt -u nullglob
}

stage_host_glibc
stage_mesa_runtime

rm -f "${OUT}"
truncate -s "${SIZE_MB}M" "${OUT}"
mkfs.ext4 -F -L xv6root -d "${STAGE}" "${OUT}" >/dev/null

if command -v debugfs >/dev/null 2>&1; then
    debugfs -w "${OUT}" <<'EOF' >/dev/null 2>&1 || true
set_inode_field /var uid 0
set_inode_field /var gid 0
set_inode_field /var/empty uid 0
set_inode_field /var/empty gid 0
set_inode_field /var/empty mode 040755
set_inode_field /root uid 0
set_inode_field /root gid 0
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

echo "make-rootfs: wrote ${OUT} (${SIZE_MB} MiB ext4, label=xv6root) from ${SYSROOT}"
