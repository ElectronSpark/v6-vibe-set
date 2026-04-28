#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <sysroot> [fs.img]" >&2
}

sysroot="${1:-}"
fsimg="${2:-}"

if [[ -z "${sysroot}" ]]; then
    usage
    exit 2
fi
if [[ ! -d "${sysroot}" ]]; then
    echo "webkit-runtime-check: sysroot not found: ${sysroot}" >&2
    exit 1
fi

required_sysroot=(
    "libexec/webkit2gtk-4.1/MiniBrowser"
    "libexec/webkit2gtk-4.1/WebKitNetworkProcess"
    "libexec/webkit2gtk-4.1/WebKitWebProcess"
    "libexec/webkit2gtk-4.1/jsc"
    "libexec/webkit2gtk-4.1/.webkit-stage.stamp"
    "lib/libwebkit2gtk-4.1.so"
    "lib/libjavascriptcoregtk-4.1.so"
    "lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
)

optional_sysroot=(
    "lib/gio/modules/libgioopenssl.so"
    "lib/pkgconfig/webkit2gtk-4.1.pc"
    "lib/pkgconfig/webkit2gtk-web-extension-4.1.pc"
    "lib/pkgconfig/javascriptcoregtk-4.1.pc"
)

missing=0
for rel in "${required_sysroot[@]}"; do
    if [[ ! -e "${sysroot}/${rel}" ]]; then
        echo "webkit-runtime-check: missing ${sysroot}/${rel}" >&2
        missing=1
    fi
done

if [[ ! -x "${sysroot}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
    echo "webkit-runtime-check: MiniBrowser is not executable" >&2
    missing=1
fi

for rel in "${optional_sysroot[@]}"; do
    if [[ ! -e "${sysroot}/${rel}" ]]; then
        echo "webkit-runtime-check: warning: optional runtime file absent: ${rel}" >&2
    fi
done

if ((missing)); then
    exit 1
fi

if [[ -f "${sysroot}/libexec/webkit2gtk-4.1/.webkit-stage-manifest" ]]; then
    if ! grep -q '^MiniBrowser$' "${sysroot}/libexec/webkit2gtk-4.1/.webkit-stage-manifest"; then
        echo "webkit-runtime-check: manifest does not list MiniBrowser" >&2
        exit 1
    fi
fi

if [[ -n "${fsimg}" ]]; then
    if [[ ! -f "${fsimg}" ]]; then
        echo "webkit-runtime-check: rootfs image not found: ${fsimg}" >&2
        exit 1
    fi
    if ! command -v debugfs >/dev/null 2>&1; then
        echo "webkit-runtime-check: debugfs not available; skipping fs.img inspection" >&2
        exit 0
    fi

    required_rootfs=(
        "/libexec/webkit2gtk-4.1/MiniBrowser"
        "/libexec/webkit2gtk-4.1/WebKitNetworkProcess"
        "/libexec/webkit2gtk-4.1/WebKitWebProcess"
        "/lib/libwebkit2gtk-4.1.so"
        "/lib/libjavascriptcoregtk-4.1.so"
        "/lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
        "/root/Desktop/webkit.desktop"
    )

    for path in "${required_rootfs[@]}"; do
        if ! debugfs -R "stat ${path}" "${fsimg}" >/dev/null 2>&1; then
            echo "webkit-runtime-check: fs.img missing ${path}" >&2
            missing=1
        fi
    done
fi

if ((missing)); then
    exit 1
fi

echo "webkit-runtime-check: ok"
