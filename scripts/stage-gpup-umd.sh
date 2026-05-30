#!/usr/bin/env bash
#
# stage-gpup-umd.sh - Stage the Hyper-V/WSL GPU-PV D3D12 user-mode runtime into
# the xv6 rootfs overlay so the guest can run real D3D12 (and compute) work on
# the host NVIDIA GPU over /dev/dxg.
#
# GPU-P does NOT expose raw PCI BARs to the guest; the only way to drive the
# real GPU is the D3DKMT/DXG paravirtual path (which the xv6 kernel implements
# under kernel/kernel/dev/hyperv/hyperv_dxg_*.c) plus the *host-provided*
# user-mode driver stack that builds GPU command buffers:
#
#   app -> libd3d12.so          (public D3D12 API shim)
#       -> libd3d12core.so      (Microsoft D3D12 runtime, "DirectX on Linux")
#       -> libdxcore.so         (adapter enumeration over /dev/dxg)
#       -> libnvwgf2umx.so      (NVIDIA D3D12 user-mode driver -> real GPU ISA)
#       -> /dev/dxg             (D3DKMT, emulated by the xv6 kernel)
#
# These libraries are proprietary NVIDIA/Microsoft binaries that ship with the
# WSL GPU-PV runtime (default: /usr/lib/wsl/lib). They are NOT redistributable
# and must NEVER be committed to the repository. This script copies them from
# the host at image-build time into a gitignored overlay path.
#
# Usage:
#   scripts/stage-gpup-umd.sh [--src DIR] [--dest DIR] [--with-extras]
#
#   --src DIR       Source dir for the GPU-PV runtime (default /usr/lib/wsl/lib)
#   --dest DIR      Overlay lib dir to populate
#                   (default: <repo>/rootfs-overlay/usr/lib/wsl/lib)
#   --with-extras   Also stage CUDA / NVML / codec libs (large, optional)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SRC="/usr/lib/wsl/lib"
DEST="${REPO_ROOT}/rootfs-overlay/usr/lib/wsl/lib"
WITH_EXTRAS=0
WITH_PROBE=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --src) SRC="$2"; shift 2 ;;
        --dest) DEST="$2"; shift 2 ;;
        --with-extras) WITH_EXTRAS=1; shift ;;
        --no-probe) WITH_PROBE=0; shift ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "stage-gpup-umd: unknown arg '$1'" >&2; exit 2 ;;
    esac
done

# Minimal set required to load a D3D12 device on the NVIDIA GPU-PV adapter.
CORE_LIBS=(
    libd3d12.so
    libd3d12core.so
    libdxcore.so
    libnvwgf2umx.so
    libnvidia-ml.so.1
)

# Optional: CUDA + DirectML kernels + codec. Large (>250 MiB total); only needed
# for CUDA/DirectML/NVENC workloads, not for a basic D3D12 compute/render proof.
EXTRA_LIBS=(
    libcuda.so
    libcuda.so.1
    libcuda.so.1.1
    libnvcuvid.so
    libnvcuvid.so.1
    libnvidia-encode.so
    libnvidia-encode.so.1
    libnvidia-opticalflow.so
    libnvidia-opticalflow.so.1
    libnvdxdlkernels.so
    libnvoptix.so.1
)

if [[ ! -d "${SRC}" ]]; then
    echo "stage-gpup-umd: source dir not found: ${SRC}" >&2
    echo "  This must run on a host with the WSL/Hyper-V GPU-PV runtime present." >&2
    echo "  Override with --src if the runtime lives elsewhere." >&2
    exit 1
fi

mkdir -p "${DEST}"

missing=0
stage_one() {
    local name="$1"
    local src="${SRC}/${name}"
    if [[ ! -e "${src}" ]]; then
        echo "stage-gpup-umd: WARNING missing ${src}" >&2
        missing=1
        return
    fi
    # -L: resolve symlinks so the image carries a real file, not a dangling link.
    cp -Lf "${src}" "${DEST}/${name}"
    chmod 0755 "${DEST}/${name}"
    printf '  staged %-28s %10d bytes\n' "${name}" "$(stat -c%s "${DEST}/${name}")"
}

echo "stage-gpup-umd: src=${SRC} dest=${DEST}"
echo "stage-gpup-umd: core D3D12 GPU-PV runtime:"
for lib in "${CORE_LIBS[@]}"; do
    stage_one "${lib}"
done

if [[ "${WITH_EXTRAS}" -eq 1 ]]; then
    echo "stage-gpup-umd: extras (CUDA/DirectML/codec):"
    for lib in "${EXTRA_LIBS[@]}"; do
        stage_one "${lib}"
    done
fi

# The Microsoft D3D12 runtime looks for its UMD next to libd3d12core.so / via
# the standard loader path. Drop an ld.so.conf.d entry and a profile fragment so
# the guest resolves /usr/lib/wsl/lib without callers needing to export paths.
CONF_DIR="${REPO_ROOT}/rootfs-overlay/etc/ld.so.conf.d"
mkdir -p "${CONF_DIR}"
cat > "${CONF_DIR}/gpup-wsl.conf" <<'EOF'
# Hyper-V/WSL GPU-PV D3D12 user-mode runtime (staged by stage-gpup-umd.sh).
/usr/lib/wsl/lib
EOF

PROFILE_DIR="${REPO_ROOT}/rootfs-overlay/etc/profile.d"
mkdir -p "${PROFILE_DIR}"
cat > "${PROFILE_DIR}/gpup-d3d12.sh" <<'EOF'
# Hyper-V/WSL GPU-PV D3D12 runtime search path. The Microsoft D3D12 runtime and
# the NVIDIA UMD live here; D3D12 clients (and Mesa dzn) need them on the path.
export LD_LIBRARY_PATH="/usr/lib/wsl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# When the Hyper-V GPU-P D3DKMT transport is present, make Mesa render on the
# host NVIDIA GPU by default so no caller has to export these by hand. Only
# rendering is GPU-backed; presentation is still a CPU framebuffer blit.
if [ -e /dev/dxg ]; then
    export GALLIUM_DRIVER=d3d12
    export MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA
    export LIBGL_ALWAYS_SOFTWARE=0
    export LIBGL_DRIVERS_PATH=/lib/dri
fi
EOF

echo "stage-gpup-umd: wrote ${CONF_DIR}/gpup-wsl.conf and ${PROFILE_DIR}/gpup-d3d12.sh"

# Note: /bin/d3d12probe is built and staged by scripts/build-linux-host-probes.sh
# as part of the normal image build (so it never goes stale relative to the
# source). The optional host sanity binary is built separately by
# user/programs/d3d12probe/build-host.sh.
if [[ "${WITH_PROBE}" -eq 1 ]]; then
    PROBE_BUILD="${REPO_ROOT}/user/programs/d3d12probe/build-host.sh"
    if [[ -x "${PROBE_BUILD}" ]]; then
        if GPUP_RUNTIME_DIR="${SRC}" "${PROBE_BUILD}" >/dev/null 2>&1; then
            echo "stage-gpup-umd: built host sanity binary user/programs/d3d12probe/d3d12probe-host"
        else
            echo "stage-gpup-umd: WARNING host sanity binary build failed (non-fatal)" >&2
        fi
    fi
fi

if [[ "${missing}" -ne 0 ]]; then
    echo "stage-gpup-umd: completed WITH MISSING libs (see warnings above)." >&2
    exit 1
fi
echo "stage-gpup-umd: done. Rebuild the rootfs image to include the runtime."
