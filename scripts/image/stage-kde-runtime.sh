#!/usr/bin/env bash
# Download/extract upstream KDE/Qt package payloads into a generated rootfs overlay.
set -euo pipefail

OVERLAY="${1:?usage: $0 <overlay> <workdir>}"
WORKDIR="${2:?usage: $0 <overlay> <workdir>}"
ARCHIVES="${KDE_ARCHIVES_DIR:-${WORKDIR}/archives}"
INVENTORY="${WORKDIR}/kde-qt-package-inventory.tsv"
ORDER_FILE="${WORKDIR}/packages.apt-order.txt"
RESOLVED_FILE="${WORKDIR}/packages.resolved.txt"

SEEDS=(
    plasma-desktop
    plasma-workspace
    kwin-wayland
    qtwayland5
    kde-cli-tools
    kded5
    kactivitymanagerd
    kwayland-integration
    plasma-integration
    plasma-pa
    pipewire
    pipewire-pulse
    pipewire-audio-client-libraries
    pipewire-alsa
    wireplumber
    pulseaudio-utils
    alsa-utils
    bluez
    hspell
    breeze
    breeze-icon-theme
    dolphin
    konsole
    kate
    kwrite
    qterminal
    xterm
    libgpgmepp6t64
)

EXCLUDED_PACKAGES=(
    appstream
    appstream-doc
    apt-config-icons
    apt-config-icons-hidpi
    apt-config-icons-large
    apt-config-icons-large-hidpi
    flatpak
    fwupd
    kio-fuse
    kwin
    kwin-x11
    libpackagekit-glib2-18
    packagekit
    packagekit-tools
    plasma-discover
    plasma-discover-backend-snap
    plasma-discover-common
    plasma-discover-notifier
    snapd
)

note() {
    echo "stage-kde-runtime: $*" >&2
}

have_archive_for() {
    local pkg="$1"
    compgen -G "${ARCHIVES}/${pkg}_*.deb" >/dev/null
}

has_download_candidate() {
    local pkg="$1"
    local candidate

    candidate="$(apt-cache policy "${pkg}" 2>/dev/null |
        awk '/Candidate:/ {print $2; exit}')"
    [[ -n "${candidate}" && "${candidate}" != "(none)" ]]
}

is_excluded_package() {
    local pkg="$1"
    local excluded

    for excluded in "${EXCLUDED_PACKAGES[@]}"; do
        if [[ "${pkg}" == "${excluded}" ]]; then
            return 0
        fi
    done
    return 1
}

resolve_packages() {
    declare -A seen

    resolve_one() {
        local pkg="$1"
        local dep

        [[ -n "${pkg}" ]] || return 0
        [[ "${pkg}" != *:* ]] || pkg="${pkg%%:*}"
        if is_excluded_package "${pkg}"; then
            return 0
        fi
        [[ -z "${seen[${pkg}]:-}" ]] || return 0
        if ! has_download_candidate "${pkg}"; then
            return 0
        fi
        seen["${pkg}"]=1
        while IFS= read -r dep; do
            resolve_one "${dep}"
        done < <(
            apt-cache depends "${pkg}" |
                awk '
                    $1 ~ /^(PreDepends:|Depends:|Recommends:|\|Depends:|\|PreDepends:|\|Recommends:)$/ {
                        gsub(/[<>()]/, "", $2);
                        if ($2 != "") print $2;
                    }'
        )
        printf '%s\n' "${pkg}"
    }

    for seed in "${SEEDS[@]}"; do
        resolve_one "${seed}"
    done
}

resolve_incremental_packages() {
    declare -A seen
    local pkg

    if [[ -s "${ORDER_FILE}" ]]; then
        cp "${ORDER_FILE}" "${RESOLVED_FILE}"
        while IFS= read -r pkg; do
            [[ -n "${pkg}" ]] || continue
            seen["${pkg}"]=1
        done < "${ORDER_FILE}"
    else
        : > "${RESOLVED_FILE}"
    fi

    resolve_one() {
        local pkg="$1"
        local dep

        [[ -n "${pkg}" ]] || return 0
        [[ "${pkg}" != *:* ]] || pkg="${pkg%%:*}"
        if is_excluded_package "${pkg}"; then
            return 0
        fi
        [[ -z "${seen[${pkg}]:-}" ]] || return 0
        if ! has_download_candidate "${pkg}"; then
            return 0
        fi
        seen["${pkg}"]=1
        while IFS= read -r dep; do
            resolve_one "${dep}"
        done < <(
            apt-cache depends "${pkg}" |
                awk '
                    $1 ~ /^(PreDepends:|Depends:|Recommends:|\|Depends:|\|PreDepends:|\|Recommends:)$/ {
                        gsub(/[<>()]/, "", $2);
                        if ($2 != "") print $2;
                    }'
        )
        printf '%s\n' "${pkg}" >> "${RESOLVED_FILE}"
    }

    for pkg in "${SEEDS[@]}"; do
        resolve_one "${pkg}"
    done
}

download_missing_archives() {
    local pkg

    mkdir -p "${ARCHIVES}"
    if [[ -s "${ORDER_FILE}" ]]; then
        resolve_incremental_packages
    else
        resolve_packages > "${RESOLVED_FILE}"
    fi
    cp "${RESOLVED_FILE}" "${ORDER_FILE}"

    while IFS= read -r pkg; do
        [[ -n "${pkg}" ]] || continue
        if is_excluded_package "${pkg}"; then
            note "skipping excluded package ${pkg}"
            continue
        fi
        if ! has_download_candidate "${pkg}"; then
            note "skipping virtual package ${pkg}"
            continue
        fi
        if have_archive_for "${pkg}"; then
            continue
        fi
        note "downloading ${pkg}"
        (cd "${ARCHIVES}" && apt-get download "${pkg}")
    done < "${ORDER_FILE}"
}

copy_selected_tree() {
    local extracted="$1"

    mkdir -p "${OVERLAY}"
    for sub in usr lib lib64 opt; do
        if [[ -d "${extracted}/${sub}" || -L "${extracted}/${sub}" ]]; then
            rsync -aH "${extracted}/${sub}/" "${OVERLAY}/${sub}/"
        fi
    done

    mkdir -p "${OVERLAY}/etc"
    for sub in xdg fonts gtk-2.0 gtk-3.0 alternatives ssl dbus-1; do
        if [[ -d "${extracted}/etc/${sub}" || -L "${extracted}/etc/${sub}" ]]; then
            rsync -aH "${extracted}/etc/${sub}/" "${OVERLAY}/etc/${sub}/"
        fi
    done
}

fix_merged_usr_loader_links() {
    local usr_loader="${OVERLAY}/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"

    [[ -e "${usr_loader}" ]] || return 0

    mkdir -p "${OVERLAY}/lib/x86_64-linux-gnu" "${OVERLAY}/lib64" "${OVERLAY}/usr/lib64"
    rm -f \
        "${OVERLAY}/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
        "${OVERLAY}/lib64/ld-linux-x86-64.so.2" \
        "${OVERLAY}/usr/lib64/ld-linux-x86-64.so.2"
    cp -aL "${usr_loader}" "${OVERLAY}/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
    cp -aL "${usr_loader}" "${OVERLAY}/lib64/ld-linux-x86-64.so.2"
    ln -sfn "../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
        "${OVERLAY}/usr/lib64/ld-linux-x86-64.so.2"
}

prune_guest_graphics_runtime() {
    local dir
    local pattern

    for dir in \
        "${OVERLAY}/lib" \
        "${OVERLAY}/lib/x86_64-linux-gnu" \
        "${OVERLAY}/usr/lib" \
        "${OVERLAY}/usr/lib/x86_64-linux-gnu"; do
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

patch_xkb_for_xwayland() {
    local keycodes="${OVERLAY}/usr/share/X11/xkb/keycodes/evdev"
    local inet="${OVERLAY}/usr/share/X11/xkb/symbols/inet"

    # This image's Xwayland path uses xkbcomp, whose X11 keycode model is
    # capped at 255 and whose keysym table can lag newer xkeyboard-config
    # multimedia/nav symbols.  xv6 currently feeds ordinary keyboard/tablet
    # events, so prune only the generated high-code evdev entries that trigger
    # those startup warnings.
    if [[ -f "${keycodes}" ]]; then
        perl -0pi -e 's/^[ \t]*<I(?:25[6-9]|2[6-9][0-9]|[3-9][0-9][0-9])>[^\n]*\n//mg' \
            "${keycodes}"
    fi
    if [[ -f "${inet}" ]]; then
        perl -0pi -e 's/^[ \t]*key[ \t]+<I(?:25[6-9]|2[6-9][0-9]|[3-9][0-9][0-9])>[^\n]*\n//mg' \
            "${inet}"
    fi
}

compile_mime_database() {
    local mimedir="${OVERLAY}/usr/share/mime"

    [[ -d "${mimedir}/packages" ]] || return 0
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database "${mimedir}" >/dev/null
    else
        note "warning: update-mime-database unavailable; KDE MIME cache will be generated at runtime"
    fi
}

write_xv6_mime_package() {
    local package="${OVERLAY}/usr/share/mime/packages/xv6-kde-extra-mimetypes.xml"

    [[ -d "${OVERLAY}/usr/share/mime/packages" ]] || return 0
    cat > "${package}" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="x-scheme-handler/file">
    <comment>file URL handler</comment>
  </mime-type>
</mime-info>
EOF
}

write_kde_file_scheme_handler() {
    local appdir="${OVERLAY}/usr/share/applications"
    local desktop="${appdir}/xv6-file-scheme-handler.desktop"

    [[ -d "${appdir}" ]] || return 0
    cat > "${desktop}" <<'EOF'
[Desktop Entry]
Type=Application
Name=xv6 File URL Handler
NoDisplay=true
Exec=dolphin %u
MimeType=x-scheme-handler/file;
Categories=Qt;KDE;System;FileTools;FileManager;
EOF
}

write_minimal_application_mimeinfo_cache() {
    local appdir="${OVERLAY}/usr/share/applications"
    local cache="${appdir}/mimeinfo.cache"

    [[ -d "${appdir}" ]] || return 0

    cat > "${cache}" <<'EOF'
[MIME Cache]
inode/directory=org.kde.dolphin.desktop;
x-scheme-handler/file=xv6-file-scheme-handler.desktop;
EOF
}

generate_kservice_mime_types() {
    local outdir="${OVERLAY}/usr/share/kservicetypes5"
    local desktop
    local line
    local mime
    local major
    local minor
    local safe
    local extra

    [[ -d "${OVERLAY}/usr/share/mime" ]] || return 0
    mkdir -p "${outdir}"

    write_mime_service_type() {
        local mime="$1"
        local force="${2:-0}"
        local major="${mime%%/*}"
        local minor="${mime#*/}"
        local safe

        if [[ "${force}" != "1" &&
              ! -f "${OVERLAY}/usr/share/mime/${major}/${minor}.xml" ]]; then
            return 0
        fi
        safe="$(printf '%s' "${mime}" | tr -c 'A-Za-z0-9_' '_')"
        cat > "${outdir}/xv6-mimetype-${safe}.desktop" <<EOF
[Desktop Entry]
Type=ServiceType
X-KDE-ServiceType=${mime}
Name=${mime}
Comment=xv6 generated MIME service type for KDE sycoca staging
EOF
    }

    while IFS= read -r -d '' desktop; do
        while IFS= read -r line; do
            [[ "${line}" == MimeType=* ]] || continue
            line="${line#MimeType=}"
            IFS=';' read -r -a mimetypes <<< "${line}"
            for mime in "${mimetypes[@]}"; do
                [[ "${mime}" == */* ]] || continue
                write_mime_service_type "${mime}"
            done
        done < "${desktop}"
    done < <(
        find \
            "${OVERLAY}/usr/share/applications" \
            "${OVERLAY}/usr/share/kservices5" \
            -type f -name '*.desktop' -print0 2>/dev/null
    )

    for extra in \
        application/json \
        application/json5 \
        application/postscript \
        application/x-font-afm \
        application/yaml \
        x-scheme-handler/file; do
        write_mime_service_type "${extra}" 1
    done
}

disable_dead_ksplash_activation() {
    local service="${OVERLAY}/usr/share/dbus-1/services/org.kde.KSplash.service"

    # The minimal xv6 session disables splash screens.  The packaged service
    # waits for org.kde.KSplash while DBus is activating that same name, so a
    # stray optional Plasma ping turns into a 25s startup timeout.
    rm -f "${service}"
}

disable_dead_rtkit_activation() {
    local service="${OVERLAY}/usr/share/dbus-1/system-services/org.freedesktop.RealtimeKit1.service"

    # PipeWire can run without realtime scheduling on xv6.  Letting D-Bus
    # auto-spawn rtkit only produces a crash loop until scheduler priority
    # limits match Linux closely enough for rtkit-daemon.
    rm -f "${service}"
}

configure_bluez_activation() {
    local service="${OVERLAY}/usr/share/dbus-1/system-services/org.bluez.service"

    mkdir -p "$(dirname "${service}")"
    cat > "${service}" <<'EOF'
[D-BUS Service]
Name=org.bluez
Exec=/bin/xv6-bluez-shim
User=root
EOF
}

write_minimal_upower_config() {
    mkdir -p "${OVERLAY}/etc/UPower"
    cat > "${OVERLAY}/etc/UPower/UPower.conf" <<'EOF'
[UPower]
EnableWattsUpPro=false
NoPollBatteries=true
IgnoreLid=true
UsePercentageForPolicy=true
PercentageLow=20
PercentageCritical=5
PercentageAction=2
TimeLow=1200
TimeCritical=300
TimeAction=120
CriticalPowerAction=PowerOff
EOF
}

disable_conflicting_audio_autostart() {
    # KDE uses PipeWire + pipewire-pulse in this image.  The legacy PulseAudio
    # autostart races the compatibility server and can make plasma-pa briefly
    # connect to a context that disappears.
    rm -f "${OVERLAY}/etc/xdg/autostart/pulseaudio.desktop"
}

patch_optional_hardware_kde_defaults() {
    local file

    # The xv6 VM currently has no Bluetooth/rfkill or Thunderbolt/Bolt stack.
    # Keep those upstream plugins installed, but do not advertise them as
    # default tray/session entries until the matching device ABI exists.
    for file in \
        "${OVERLAY}/usr/share/kservices5/plasma-applet-org.kde.plasma.bluetooth.desktop" \
        "${OVERLAY}/usr/share/kservices5/plasma-applet-org.kde.kscreen.desktop"
    do
        if [[ -f "${file}" ]]; then
            perl -0pi -e 's/^X-KDE-PluginInfo-EnabledByDefault=true$/X-KDE-PluginInfo-EnabledByDefault=false/mg' \
                "${file}"
        fi
    done
}

strip_spa_json_module() {
    local file="$1"
    local module="$2"
    local tmp="${file}.tmp"

    [[ -f "${file}" ]] || return 0
    awk -v module="${module}" '
        function delta(s, a, b) {
            a = s
            b = gsub(/\{/, "", a)
            a = s
            return b - gsub(/\}/, "", a)
        }
        skipping == 0 && $0 ~ "\\{[[:space:]]*name[[:space:]]*=[[:space:]]*" module "([[:space:]}]|$)" {
            skipping = 1
            depth = delta($0)
            if (depth <= 0)
                skipping = 0
            next
        }
        skipping != 0 {
            depth += delta($0)
            if (depth <= 0)
                skipping = 0
            next
        }
        { print }
    ' "${file}" > "${tmp}"
    mv "${tmp}" "${file}"
}

patch_pipewire_runtime_config() {
    local file

    find "${OVERLAY}/usr/share/pipewire" \
         "${OVERLAY}/usr/share/wireplumber" \
         -type f \( -name '*.conf' -o -name '*.lua' \) -print0 2>/dev/null |
        while IFS= read -r -d '' file; do
            strip_spa_json_module "${file}" "libpipewire-module-rt"
            strip_spa_json_module "${file}" "libpipewire-module-jackdbus-detect"
        done

    file="${OVERLAY}/usr/share/pipewire/pipewire.conf"
    if [[ -f "${file}" ]]; then
        perl -0pi -e 's/module\.jackdbus-detect = true/module.jackdbus-detect = false/g' \
            "${file}"
        perl -0pi -e 's/#access\.socket = \{ pipewire-0 = "default", pipewire-0-manager = "unrestricted" \}/access.socket = { pipewire-0 = "unrestricted", pipewire-0-manager = "unrestricted" }/g' \
            "${file}"
        perl -0pi -e 's/^[ \t]*access\.legacy = true[ \t]*$/            #access.legacy = true/mg' \
            "${file}"
    fi

    file="${OVERLAY}/usr/share/pipewire/pipewire-pulse.conf"
    if [[ -f "${file}" ]]; then
        perl -0pi -e 's/"unix:native"/{ address = "unix:native" client.access = "unrestricted" }/g' \
            "${file}"
        perl -0pi -e 's/(args = "module-always-sink" flags = )\[ \]/${1}[ nofail ]/g' \
            "${file}"
    fi

    file="${OVERLAY}/usr/share/wireplumber/main.lua.d/50-default-access-config.lua"
    if [[ -f "${file}" ]]; then
        perl -0pi -e 's/\["enable-flatpak-portal"\] = true/\["enable-flatpak-portal"\] = false/g' \
            "${file}"
    fi

    file="${OVERLAY}/usr/share/wireplumber/main.lua.d/50-alsa-config.lua"
    if [[ -f "${file}" ]]; then
        perl -0pi -e 's/^alsa_monitor\.enabled = true$/alsa_monitor.enabled = false/mg' \
            "${file}"
        perl -0pi -e 's/\["alsa\.reserve"\] = true/\["alsa.reserve"\] = false/g' \
            "${file}"
    fi

    file="${OVERLAY}/usr/share/wireplumber/main.lua.d/50-v4l2-config.lua"
    if [[ -f "${file}" ]]; then
        perl -0pi -e 's/^v4l2_monitor\.enabled = true$/v4l2_monitor.enabled = false/mg' \
            "${file}"
    fi

    file="${OVERLAY}/usr/share/wireplumber/main.lua.d/50-libcamera-config.lua"
    if [[ -f "${file}" ]]; then
        perl -0pi -e 's/^libcamera_monitor\.enabled = true$/libcamera_monitor.enabled = false/mg' \
            "${file}"
    fi

    for file in \
        "${OVERLAY}/usr/share/wireplumber/bluetooth.lua.d/50-bluez-config.lua" \
        "${OVERLAY}/usr/share/wireplumber/bluetooth.lua.d/50-bluez-midi-config.lua"
    do
        if [[ -f "${file}" ]]; then
            perl -0pi -e 's/^bluez(_midi)?_monitor\.enabled = true$/bluez${1}_monitor.enabled = false/mg' \
                "${file}"
            perl -0pi -e 's/\["with-logind"\] = true/\["with-logind"\] = false/g' \
                "${file}"
        fi
    done
}

package_is_seed() {
    local pkg="$1"
    local seed

    for seed in "${SEEDS[@]}"; do
        if [[ "${pkg}" == "${seed}" ]]; then
            return 0
        fi
    done
    return 1
}

write_inventory_row() {
    local idx="$1"
    local deb="$2"
    local pkg version arch source source_pkg source_version depends predepends recommends seed archive

    pkg="$(dpkg-deb -f "${deb}" Package 2>/dev/null || basename "${deb}")"
    version="$(dpkg-deb -f "${deb}" Version 2>/dev/null || true)"
    arch="$(dpkg-deb -f "${deb}" Architecture 2>/dev/null || true)"
    source="$(dpkg-deb -f "${deb}" Source 2>/dev/null || true)"
    if [[ -n "${source}" ]]; then
        source_pkg="${source%% (*}"
        if [[ "${source}" == *"("*")"* ]]; then
            source_version="${source#*(}"
            source_version="${source_version%)}"
        else
            source_version="${version}"
        fi
    else
        source_pkg="${pkg}"
        source_version="${version}"
    fi
    depends="$(dpkg-deb -f "${deb}" Depends 2>/dev/null | tr '\t' ' ' || true)"
    predepends="$(dpkg-deb -f "${deb}" Pre-Depends 2>/dev/null | tr '\t' ' ' || true)"
    recommends="$(dpkg-deb -f "${deb}" Recommends 2>/dev/null | tr '\t' ' ' || true)"
    seed="no"
    if package_is_seed "${pkg}"; then
        seed="yes"
    fi
    archive="$(basename "${deb}")"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "${idx}" "${pkg}" "${version}" "${arch}" \
        "${predepends}" "${depends}" "${recommends}" \
        "${source_pkg}" "${source_version}" "${seed}" "${archive}" >> "${INVENTORY}"
}

write_qt_wayland_smoke_qml() {
    local qml="${OVERLAY}/opt/xv6-kde/qt-wayland-smoke.qml"

    mkdir -p "$(dirname "${qml}")"
    cat > "${qml}" <<'EOF'
import QtQuick 2.15

Rectangle {
    id: root
    width: 420
    height: 260
    color: active ? "#2f9e44" : "#1c7ed6"
    property bool active: false

    Text {
        anchors.centerIn: parent
        text: "Qt Wayland"
        color: "white"
        font.pixelSize: 34
    }

    Timer {
        interval: 900
        running: true
        repeat: true
        onTriggered: {
            root.active = !root.active
            console.log("XV6_QT_WAYLAND_SMOKE_FRAME", root.active ? 1 : 0)
        }
    }

    Component.onCompleted: console.log("XV6_QT_WAYLAND_SMOKE_READY")
}
EOF
}

ordered_archives() {
    local pkg deb

    if [[ -s "${ORDER_FILE}" ]]; then
        while IFS= read -r pkg; do
            [[ -n "${pkg}" ]] || continue
            is_excluded_package "${pkg}" && continue
            deb="$(find "${ARCHIVES}" -maxdepth 1 -type f -name "${pkg}_*.deb" | sort | head -n 1)"
            [[ -n "${deb}" ]] || continue
            printf '%s\n' "${deb}"
        done < "${ORDER_FILE}"
    fi
}

download_missing_archives

mapfile -t DEBS < <(ordered_archives)
if [[ "${#DEBS[@]}" -eq 0 ]]; then
    echo "stage-kde-runtime: error: no .deb archives in ${ARCHIVES}" >&2
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

rm -rf "${OVERLAY}"
mkdir -p "${OVERLAY}" "${WORKDIR}"
printf 'order\tpackage\tversion\tarch\tpredepends\tdepends\trecommends\tsource_package\tsource_version\tseed\tarchive\n' > "${INVENTORY}"

idx=0
for deb in "${DEBS[@]}"; do
    deb_tmp="${TMP}/deb-${idx}"

    idx=$((idx + 1))
    write_inventory_row "${idx}" "${deb}"
    rm -rf "${deb_tmp}"
    mkdir -p "${deb_tmp}"
    dpkg-deb -x "${deb}" "${deb_tmp}"
    copy_selected_tree "${deb_tmp}"
done

fix_merged_usr_loader_links
prune_guest_graphics_runtime
patch_xkb_for_xwayland
write_xv6_mime_package
write_kde_file_scheme_handler
compile_mime_database
generate_kservice_mime_types
write_minimal_application_mimeinfo_cache
disable_dead_ksplash_activation
disable_dead_rtkit_activation
configure_bluez_activation
write_minimal_upower_config
disable_conflicting_audio_autostart
patch_optional_hardware_kde_defaults
patch_pipewire_runtime_config

mkdir -p "${OVERLAY}/opt/xv6-kde"
cp "${INVENTORY}" "${OVERLAY}/opt/xv6-kde/kde-qt-package-inventory.tsv"
cp "${ORDER_FILE}" "${OVERLAY}/opt/xv6-kde/packages.apt-order.txt"
write_qt_wayland_smoke_qml
printf '%s\n' "source=ubuntu-noble-deb-archives" > "${OVERLAY}/opt/xv6-kde/README.txt"

note "staged ${#DEBS[@]} KDE/Qt package archives into ${OVERLAY}"
note "inventory=${INVENTORY}"
