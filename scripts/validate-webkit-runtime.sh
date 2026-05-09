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
    "usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/WebKitNetworkProcess"
    "usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/WebKitWebProcess"
    "libexec/webkit2gtk-4.1/.webkit-stage.stamp"
    "lib/libwebkit2gtk-4.1.so"
    "lib/libjavascriptcoregtk-4.1.so"
    "lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
    "share/glib-2.0/schemas/gschemas.compiled"
)

optional_sysroot=(
    "lib/gio/modules/libgioopenssl.so"
    "lib/pkgconfig/webkit2gtk-4.1.pc"
    "lib/pkgconfig/webkit2gtk-web-extension-4.1.pc"
    "lib/pkgconfig/javascriptcoregtk-4.1.pc"
)

missing=0
for rel in "${required_sysroot[@]}"; do
    if [[ ! -e "${sysroot}/${rel}" && ! -L "${sysroot}/${rel}" ]]; then
        echo "webkit-runtime-check: missing ${sysroot}/${rel}" >&2
        missing=1
    fi
done

if [[ ! -x "${sysroot}/libexec/webkit2gtk-4.1/MiniBrowser" ]]; then
    echo "webkit-runtime-check: MiniBrowser is not executable" >&2
    missing=1
fi

for rel in "${optional_sysroot[@]}"; do
    if [[ ! -e "${sysroot}/${rel}" && ! -L "${sysroot}/${rel}" ]]; then
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

is_glibc_baseline_soname() {
    case "$1" in
        libc.so.6|libm.so.6|libdl.so.2|libpthread.so.0|librt.so.1|ld-linux-x86-64.so.2)
            return 0
            ;;
    esac
    return 1
}

sysroot_has_library_soname() {
    local soname="$1"

    [[ -e "${sysroot}/lib/${soname}" || -e "${sysroot}/usr/lib/${soname}" ]]
}

check_webkit_elf_closure() {
    command -v readelf >/dev/null 2>&1 || return 0

    local queue=()
    local elf
    local soname
    local dep
    local roots=(
        "${sysroot}/libexec/webkit2gtk-4.1/MiniBrowser"
        "${sysroot}/libexec/webkit2gtk-4.1/WebKitNetworkProcess"
        "${sysroot}/libexec/webkit2gtk-4.1/WebKitWebProcess"
        "${sysroot}/libexec/webkit2gtk-4.1/jsc"
        "${sysroot}/lib/libwebkit2gtk-4.1.so.0"
        "${sysroot}/lib/libjavascriptcoregtk-4.1.so.0"
        "${sysroot}/lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
    )
    declare -A seen_elf=()

    for elf in "${roots[@]}"; do
        [[ -e "${elf}" ]] || continue
        seen_elf["${elf}"]=1
        queue+=("${elf}")
    done

    while ((${#queue[@]})); do
        elf="${queue[0]}"
        queue=("${queue[@]:1}")

        while IFS= read -r soname; do
            is_glibc_baseline_soname "${soname}" && continue
            if ! sysroot_has_library_soname "${soname}"; then
                echo "webkit-runtime-check: ${elf#${sysroot}/} needs missing ${soname}" >&2
                missing=1
                continue
            fi

            dep=""
            if [[ -e "${sysroot}/lib/${soname}" ]]; then
                dep="${sysroot}/lib/${soname}"
            elif [[ -e "${sysroot}/usr/lib/${soname}" ]]; then
                dep="${sysroot}/usr/lib/${soname}"
            fi
            if [[ -n "${dep}" && -z "${seen_elf[${dep}]:-}" ]]; then
                seen_elf["${dep}"]=1
                queue+=("${dep}")
            fi
        done < <(readelf -dW "${elf}" 2>/dev/null |
            sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
    done
}

check_webkit_elf_closure

if ((missing)); then
    exit 1
fi

elf_exports_symbol() {
    local elf="$1"
    local symbol="$2"

    [[ -e "${elf}" ]] || return 1
    nm -D "${elf}" 2>/dev/null |
        awk '{ print $3 }' |
        grep -x "${symbol}" >/dev/null
}

elf_needs_soname() {
    local elf="$1"
    local soname="$2"

    [[ -e "${elf}" ]] || return 1
    readelf -dW "${elf}" 2>/dev/null |
        grep -q "Shared library: \\[${soname}\\]"
}

check_no_private_gobject_runtime() {
    command -v nm >/dev/null 2>&1 || return 0
    command -v readelf >/dev/null 2>&1 || return 0

    local elf
    local rel
    local bad=0
    local candidates=(
        "lib/libpango-1.0.so.0"
        "lib/libpangoft2-1.0.so.0"
        "lib/libpangocairo-1.0.so.0"
        "lib/libcairo-gobject.so.2"
    )

    for rel in "${candidates[@]}"; do
        elf="${sysroot}/${rel}"
        [[ -e "${elf}" ]] || continue
        if elf_exports_symbol "${elf}" "g_object_new" ||
           elf_exports_symbol "${elf}" "g_signal_emit" ||
           elf_exports_symbol "${elf}" "g_cclosure_marshal_VOID__VOID"; then
            if ! elf_needs_soname "${elf}" "libgobject-2.0.so.0" ||
               ! elf_needs_soname "${elf}" "libglib-2.0.so.0"; then
                echo "webkit-runtime-check: ${rel} embeds GLib/GObject runtime symbols instead of depending on libglib/libgobject DSOs" >&2
                bad=1
            fi
        fi
    done

    if ((bad)); then
        exit 1
    fi
}

check_no_private_gobject_runtime

if command -v nm >/dev/null 2>&1 &&
   [[ -e "${sysroot}/lib/libwebkit2gtk-4.1.so.0" &&
      -e "${sysroot}/lib/libgdk-3.so.0" ]]; then
    webkit_needs_gdk_x11=0
    gdk_exports_x11=0
    gdk_needs_cairo_xlib=0
    cairo_exports_xlib=0
    if nm -D "${sysroot}/lib/libwebkit2gtk-4.1.so.0" 2>/dev/null |
       awk '$1 == "U" { print $2 } $2 == "U" { print $3 }' |
       grep -x 'gdk_x11_cursor_get_xcursor' >/dev/null; then
        webkit_needs_gdk_x11=1
    fi
    if nm -D "${sysroot}/lib/libgdk-3.so.0" 2>/dev/null |
       awk '{ print $3 }' |
       grep -x 'gdk_x11_cursor_get_xcursor' >/dev/null; then
        gdk_exports_x11=1
    fi
    if ((webkit_needs_gdk_x11 && !gdk_exports_x11)); then
        echo "webkit-runtime-check: libwebkit2gtk needs GDK X11 symbols but staged libgdk-3.so.0 does not export them" >&2
        exit 1
    fi
    if nm -D "${sysroot}/lib/libgdk-3.so.0" 2>/dev/null |
       awk '$1 == "U" { print $2 } $2 == "U" { print $3 }' |
       grep -x 'cairo_xlib_surface_get_display' >/dev/null; then
        gdk_needs_cairo_xlib=1
    fi
    if [[ -e "${sysroot}/lib/libcairo.so.2" ]] &&
       nm -D "${sysroot}/lib/libcairo.so.2" 2>/dev/null |
       awk '{ print $3 }' |
       grep -x 'cairo_xlib_surface_get_display' >/dev/null; then
        cairo_exports_xlib=1
    fi
    if ((gdk_needs_cairo_xlib && !cairo_exports_xlib)); then
        echo "webkit-runtime-check: libgdk-3 needs Cairo Xlib symbols but staged libcairo.so.2 does not export them" >&2
        exit 1
    fi
fi

if command -v nm >/dev/null 2>&1 &&
   [[ -e "${sysroot}/lib/libgtk-3.so.0" ]]; then
    gtk_needs_harfbuzz_alloc=0
    harfbuzz_exports_alloc=0
    gtk_needs_pangocairo_private=0
    pangocairo_exports_private=0
    if nm -D "${sysroot}/lib/libgtk-3.so.0" 2>/dev/null |
       awk '$1 == "U" { print $2 } $2 == "U" { print $3 }' |
       grep -x 'hb_calloc' >/dev/null; then
        gtk_needs_harfbuzz_alloc=1
    fi
    if [[ -e "${sysroot}/lib/libharfbuzz.so.0" ]] &&
       nm -D "${sysroot}/lib/libharfbuzz.so.0" 2>/dev/null |
       awk '{ print $3 }' |
       grep -x 'hb_calloc' >/dev/null; then
        harfbuzz_exports_alloc=1
    fi
    if ((gtk_needs_harfbuzz_alloc && !harfbuzz_exports_alloc)); then
        echo "webkit-runtime-check: libgtk-3 needs HarfBuzz allocator symbols but staged libharfbuzz.so.0 does not export them" >&2
        exit 1
    fi
    if nm -D "${sysroot}/lib/libgtk-3.so.0" 2>/dev/null |
       awk '$1 == "U" { print $2 } $2 == "U" { print $3 }' |
       grep -x '_pango_cairo_font_get_hex_box_scaled_font' >/dev/null; then
        gtk_needs_pangocairo_private=1
    fi
    if [[ -e "${sysroot}/lib/libpangocairo-1.0.so.0" ]] &&
       nm -D "${sysroot}/lib/libpangocairo-1.0.so.0" 2>/dev/null |
       awk '{ print $3 }' |
       grep -x '_pango_cairo_font_get_hex_box_scaled_font' >/dev/null; then
        pangocairo_exports_private=1
    fi
    if ((gtk_needs_pangocairo_private && !pangocairo_exports_private)); then
        echo "webkit-runtime-check: libgtk-3 needs PangoCairo private symbols but staged libpangocairo-1.0.so.0 does not export them" >&2
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
        "/usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/WebKitNetworkProcess"
        "/usr/lib/x86_64-linux-gnu/webkit2gtk-4.1/WebKitWebProcess"
        "/lib/libwebkit2gtk-4.1.so"
        "/lib/libjavascriptcoregtk-4.1.so"
        "/lib/libharfbuzz.so.0"
        "/lib/libpango-1.0.so.0"
        "/lib/libpangocairo-1.0.so.0"
        "/lib/libcairo.so.2"
        "/lib/libcairo-gobject.so.2"
        "/lib/webkit2gtk-4.1/injected-bundle/libwebkit2gtkinjectedbundle.so"
        "/share/glib-2.0/schemas/gschemas.compiled"
        "/root/Desktop/webkit.desktop"
    )

    if command -v readelf >/dev/null 2>&1 &&
       [[ -e "${sysroot}/lib/libgdk-3.so.0" ]] &&
       readelf -dW "${sysroot}/lib/libgdk-3.so.0" 2>/dev/null |
       grep -q 'Shared library: \[libX11\.so\.6\]'; then
        required_rootfs+=(
            "/lib/libX11.so.6"
            "/lib/libXext.so.6"
            "/lib/libXrender.so.1"
            "/lib/libXi.so.6"
            "/lib/libXcursor.so.1"
            "/lib/libXdamage.so.1"
            "/lib/libXfixes.so.3"
            "/lib/libXcomposite.so.1"
            "/lib/libXrandr.so.2"
            "/lib/libXinerama.so.1"
            "/lib/libxcb.so.1"
            "/lib/libxcb-render.so.0"
            "/lib/libxcb-shm.so.0"
            "/lib/libXau.so.6"
            "/lib/libXdmcp.so.6"
            "/lib/libbsd.so.0"
            "/lib/libmd.so.0"
        )
    fi

    if [[ -e "${sysroot}/lib/libxkbcommon.so.0" ]]; then
        required_rootfs+=(
            "/share/X11/xkb/rules/evdev"
            "/share/X11/xkb/keycodes/evdev"
            "/share/X11/xkb/symbols/us"
        )
    fi

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
