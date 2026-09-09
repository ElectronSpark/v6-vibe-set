#!/usr/bin/env bash
# Download/extract upstream KDE/Qt package payloads into a generated rootfs overlay.
set -euo pipefail

OVERLAY="${1:?usage: $0 <overlay> <workdir>}"
WORKDIR="${2:?usage: $0 <overlay> <workdir>}"
ARCHIVES="${KDE_ARCHIVES_DIR:-${WORKDIR}/archives}"
INVENTORY="${WORKDIR}/kde-qt-package-inventory.tsv"
ORDER_FILE="${KDE_PACKAGE_ORDER_FILE:-${WORKDIR}/packages.apt-order.txt}"
RESOLVED_FILE="${WORKDIR}/packages.resolved.txt"
ARCHIVE_SHA256_FILE="${KDE_ARCHIVE_SHA256_FILE:-}"
PACKAGE_LOCKED="${KDE_PACKAGE_LOCKED:-0}"
DOWNLOAD_ONLY="${KDE_DOWNLOAD_ONLY:-0}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
SERVER_BACKPORTS_KEY="${SCRIPT_DIR}/../locks/kde-noble/canonical-server-backports.asc"
SERVER_BACKPORTS_ROOT="${WORKDIR}/canonical-server-backports-apt"
SERVER_BACKPORTS_LIST="${SERVER_BACKPORTS_ROOT}/sources.list"
SERVER_BACKPORTS_LISTS="${SERVER_BACKPORTS_ROOT}/lists"
SERVER_BACKPORTS_CACHE="${SERVER_BACKPORTS_ROOT}/cache"
SERVER_BACKPORTS_READY=0

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
    qmlscene
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
    xwayland
    x11-xkb-utils
    xkb-data
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

download_locked_archives() {
    local expected_hash filename pkg encoded_version version

    prepare_server_backports() {
        [[ "${SERVER_BACKPORTS_READY}" == "0" ]] || return 0
        [[ -s "${SERVER_BACKPORTS_KEY}" ]] || {
            note "Canonical Server Team Backports signing key is missing"
            exit 1
        }

        mkdir -p \
            "${SERVER_BACKPORTS_LISTS}/partial" \
            "${SERVER_BACKPORTS_CACHE}/archives/partial"
        printf 'deb [signed-by=%s] https://ppa.launchpadcontent.net/canonical-server/server-backports/ubuntu noble main\n' \
            "${SERVER_BACKPORTS_KEY}" >"${SERVER_BACKPORTS_LIST}"
        apt-get \
            -o "Dir::Etc::sourcelist=${SERVER_BACKPORTS_LIST}" \
            -o 'Dir::Etc::sourceparts=-' \
            -o "Dir::State::lists=${SERVER_BACKPORTS_LISTS}" \
            -o "Dir::Cache=${SERVER_BACKPORTS_CACHE}" \
            -o "Dir::Cache::archives=${SERVER_BACKPORTS_CACHE}/archives" \
            update
        SERVER_BACKPORTS_READY=1
    }

    download_from_server_backports() {
        local package_version="$1"

        prepare_server_backports
        (
            cd "${ARCHIVES}"
            apt-get \
                -o "Dir::Etc::sourcelist=${SERVER_BACKPORTS_LIST}" \
                -o 'Dir::Etc::sourceparts=-' \
                -o "Dir::State::lists=${SERVER_BACKPORTS_LISTS}" \
                -o "Dir::Cache=${SERVER_BACKPORTS_CACHE}" \
                -o "Dir::Cache::archives=${SERVER_BACKPORTS_CACHE}/archives" \
                download "${package_version}"
        )
    }

    while read -r expected_hash filename; do
        [[ -n "${expected_hash}" && -n "${filename}" ]] || continue
        [[ "${filename}" != */* && "${filename}" == *.deb ]] || {
            note "invalid locked archive name: ${filename}"
            exit 1
        }
        [[ -f "${ARCHIVES}/${filename}" ]] && continue

        pkg="${filename%%_*}"
        encoded_version="${filename#*_}"
        encoded_version="${encoded_version%_*}"
        version="${encoded_version//%3a/:}"
        version="${version//%3A/:}"
        [[ -n "${pkg}" && -n "${version}" ]] || {
            note "cannot derive package/version from locked archive ${filename}"
            exit 1
        }

        note "downloading locked ${pkg}=${version}"
        if ! (cd "${ARCHIVES}" && apt-get download "${pkg}=${version}"); then
            note "locked version is absent from the primary archive; trying signed Canonical Server Team Backports"
            download_from_server_backports "${pkg}=${version}"
        fi
        [[ -f "${ARCHIVES}/${filename}" ]] || {
            note "locked download did not produce ${filename}"
            exit 1
        }
    done < "${ARCHIVE_SHA256_FILE}"
}

download_missing_archives() {
    local pkg

    mkdir -p "${ARCHIVES}"
    if [[ "${PACKAGE_LOCKED}" == "1" ]]; then
        [[ -s "${ORDER_FILE}" ]] || {
            note "locked package order is missing: ${ORDER_FILE}"
            exit 1
        }
        [[ -s "${ARCHIVE_SHA256_FILE}" ]] || {
            note "locked archive checksum file is missing: ${ARCHIVE_SHA256_FILE}"
            exit 1
        }
        download_locked_archives
        shopt -s nullglob
        locked_archives=("${ARCHIVES}"/*.deb)
        shopt -u nullglob
        lock_count="$(awk 'NF >= 2 { count++ } END { print count + 0 }' \
            "${ARCHIVE_SHA256_FILE}")"
        if (( ${#locked_archives[@]} != lock_count )); then
            note "locked archive count mismatch: actual=${#locked_archives[@]} expected=${lock_count}"
            exit 1
        fi
        (cd "${ARCHIVES}" && sha256sum -c "${ARCHIVE_SHA256_FILE}")
        cp "${ORDER_FILE}" "${RESOLVED_FILE}"
    elif [[ -s "${ORDER_FILE}" ]]; then
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
        if [[ "${PACKAGE_LOCKED}" == "1" ]]; then
            note "locked archive missing for ${pkg}; refusing repository substitution"
            exit 1
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

materialize_qtchooser_configs() {
    local conf_dir="${OVERLAY}/usr/share/qtchooser"
    local lib_dir="${OVERLAY}/usr/lib/x86_64-linux-gnu/qtchooser"
    local default_dir="${OVERLAY}/usr/lib/x86_64-linux-gnu/qt-default/qtchooser"
    local qt5_conf="${conf_dir}/qt5-x86_64-linux-gnu.conf"

    [[ -f "${qt5_conf}" ]] || return 0

    mkdir -p "${lib_dir}" "${default_dir}"
    rm -f "${lib_dir}/5.conf" "${lib_dir}/qt5.conf" "${default_dir}/default.conf"
    cp -a "${qt5_conf}" "${lib_dir}/5.conf"
    cp -a "${qt5_conf}" "${lib_dir}/qt5.conf"
    cp -a "${qt5_conf}" "${default_dir}/default.conf"
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
    local root keycodes inet

    # This image's Xwayland path uses xkbcomp, whose X11 keycode model is
    # capped at 255 and whose keysym table can lag newer xkeyboard-config
    # multimedia/nav symbols.  xv6 currently feeds ordinary keyboard/tablet
    # events, so prune only the generated high-code evdev entries that trigger
    # those startup warnings.
    for root in "${OVERLAY}/usr/share/X11" "${OVERLAY}/share/X11"; do
        keycodes="${root}/xkb/keycodes/evdev"
        inet="${root}/xkb/symbols/inet"
        if [[ -f "${keycodes}" ]]; then
            perl -0pi -e 's/^[ \t]*<I(?:25[6-9]|2[6-9][0-9]|[3-9][0-9][0-9])>[^\n]*\n//mg' \
                "${keycodes}"
        fi
        if [[ -f "${inet}" ]]; then
            perl -0pi -e 's/^[ \t]*key[ \t]+<I(?:25[6-9]|2[6-9][0-9]|[3-9][0-9][0-9])>[^\n]*\n//mg' \
                "${inet}"
        fi
    done
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

write_false_compat() {
    # Several optional D-Bus service files use /bin/false as the canonical
    # "installed but disabled" activation target.  The minimal image does not
    # otherwise stage coreutils false, so point it at xv6's tiny native helper
    # to make those activations fail quickly without an exec ENOENT.
    mkdir -p "${OVERLAY}/bin" "${OVERLAY}/usr/bin"
    ln -sf /bin/xv6-false "${OVERLAY}/bin/false"
    ln -sf /bin/xv6-false "${OVERLAY}/bin/fusermount3"
    ln -sf /bin/xv6-false "${OVERLAY}/usr/bin/false"
    ln -sf /bin/xv6-false "${OVERLAY}/usr/bin/fusermount3"
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

configure_modemmanager_activation() {
    local service="${OVERLAY}/usr/share/dbus-1/system-services/org.freedesktop.ModemManager1.service"

    # xv6 currently exposes no modem/radio kernel ABI.  Return a Linux-shaped
    # empty ModemManager object tree so KDE can complete optional enumeration
    # without a D-Bus activation failure.
    if [[ -f "${service}" ]]; then
        cat > "${service}" <<'EOF'
[D-BUS Service]
Name=org.freedesktop.ModemManager1
Exec=/bin/xv6-modemmanager-shim
User=root
EOF
    fi
}

configure_documents_portal_activation() {
    local service="${OVERLAY}/usr/share/dbus-1/services/org.freedesktop.portal.Documents.service"

    # xv6 does not expose the FUSE document-store filesystem yet, but Chromium
    # and GLib probe this session service during portal startup.  Activate an
    # xv6-owned no-export shim instead of /bin/false so the probe completes
    # without a D-Bus activation failure.
    if [[ -f "${service}" ]]; then
        cat > "${service}" <<'EOF'
[D-BUS Service]
Name=org.freedesktop.portal.Documents
Exec=/bin/xv6-document-portal-shim
EOF
    fi
}

disable_optional_tray_plasmoid() {
    local id="$1"
    local reason="$2"
    local plasmoid="${OVERLAY}/usr/share/plasma/plasmoids/${id}"
    local disabled="${OVERLAY}/usr/share/plasma/plasmoids/${id}.xv6-disabled"

    if [[ -d "${plasmoid}" ]]; then
        rm -rf "${disabled}"
        mv "${plasmoid}" "${disabled}"
        cat > "${disabled}/XV6_DISABLED.md" <<EOF
Disabled by xv6 rootfs staging.

The upstream package is kept source-clean, but this optional Plasma tray
plasmoid is not staged under its discoverable plugin id for this image.

Reason: ${reason}
EOF
    fi
}

disable_optional_tray_plasmoids() {
    # Plasma's system tray auto-discovers optional plasmoids and instantiates
    # their upstream QML in the hidden-items popup.  Several of those applets
    # target hardware/services xv6 intentionally shims or disables, and the
    # reduced tray startup path shows empty-dialog/layout churn before the
    # popup becomes visible.  Keep the package payloads under marker paths so
    # upstream sources remain clean, but hide unsupported optional tray applets
    # from discovery.  Volume stays staged, and the xv6 StatusNotifier item
    # provides the visible network indicator.
    disable_optional_tray_plasmoid \
        "org.kde.plasma.networkmanagement" \
        "xv6 uses /bin/xv6-network-status-sni for panel network state"
    disable_optional_tray_plasmoid \
        "org.kde.plasma.clipboard" \
        "klipper autostart is disabled in this image"
    disable_optional_tray_plasmoid \
        "org.kde.plasma.devicenotifier" \
        "removable-storage/udisks enumeration is shimmed empty"
    disable_optional_tray_plasmoid \
        "org.kde.plasma.vault" \
        "Plasma Vault backends are not part of the xv6 desktop target"
    disable_optional_tray_plasmoid \
        "org.kde.kscreen" \
        "display configuration is fixed by the xv6/QEMU video mode"
}

write_portal_defaults() {
    local dir="${OVERLAY}/usr/share/xdg-desktop-portal"
    local file

    # Make the portal implementation choice deterministic for the KDE image.
    # Without this, Chromium startup falls through the GTK fallback path first,
    # activating extra accessibility and portal helpers before KDE's portal.
    mkdir -p "${dir}"
    for file in portals.conf kde-portals.conf KDE-portals.conf; do
        cat > "${dir}/${file}" <<'EOF'
[preferred]
default=kde
org.freedesktop.impl.portal.Lockdown=none
org.freedesktop.impl.portal.FileChooser=kde
org.freedesktop.impl.portal.Settings=kde
org.freedesktop.impl.portal.ScreenCast=kde
org.freedesktop.impl.portal.Screenshot=kde
EOF
    done
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

gate_xsettingsd_without_x11() {
    local binary="${OVERLAY}/usr/bin/xsettingsd"
    local real="${OVERLAY}/usr/bin/xsettingsd.real"

    [[ -x "${binary}" ]] || return 0
    [[ -e "${real}" ]] || mv "${binary}" "${real}"
    cat > "${binary}" <<'EOF'
#!/bin/sh
display="${DISPLAY:-}"
case "${display}" in
    :[0-9]*)
        number="${display#:}"
        number="${number%%.*}"
        [ -S "/tmp/.X11-unix/X${number}" ] || exit 0
        ;;
    "")
        exit 0
        ;;
esac
exec /usr/bin/xsettingsd.real "$@"
EOF
    chmod 0755 "${binary}"
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
        if ! grep -q 'node.name[[:space:]]*=[[:space:]]*"alsa_output.xv6_virtio"' "${file}"; then
            perl -0pi -e 's/\n    # Use the metadata factory/\n    { factory = adapter\n        args = {\n            factory.name           = api.alsa.pcm.sink\n            node.name              = "alsa_output.xv6_virtio"\n            node.description       = "xv6 virtio PCM"\n            media.class            = "Audio\/Sink"\n            api.alsa.path          = "hw:0"\n            api.alsa.period-size   = 1200\n            api.alsa.headroom      = 0\n            api.alsa.disable-mmap  = true\n            api.alsa.disable-batch = true\n            audio.format           = "S16LE"\n            audio.rate             = 48000\n            audio.channels         = 2\n            audio.position         = "FL,FR"\n        }\n        flags = [ nofail ]\n    }\n\n    # Use the metadata factory/' \
                "${file}"
        fi
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
        perl -0pi -e 's/\["alsa\.midi"\] = true/\["alsa.midi"\] = false/g' \
            "${file}"
        perl -0pi -e 's/\["alsa\.midi\.monitoring"\] = true/\["alsa.midi.monitoring"\] = false/g' \
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

validate_xwayland_runtime() {
    local path
    local missing=0

    for path in \
        "${OVERLAY}/usr/bin/Xwayland" \
        "${OVERLAY}/usr/bin/xkbcomp" \
        "${OVERLAY}/usr/share/X11/xkb/keycodes/evdev"; do
        if [[ ! -f "${path}" ]]; then
            note "error: required Xwayland runtime payload missing: ${path#${OVERLAY}/}"
            missing=1
        fi
    done
    if [[ ! -x "${OVERLAY}/usr/bin/Xwayland" ]]; then
        note "error: Xwayland payload is not executable: usr/bin/Xwayland"
        missing=1
    fi
    if [[ ! -x "${OVERLAY}/usr/bin/xkbcomp" ]]; then
        note "error: xkbcomp payload is not executable: usr/bin/xkbcomp"
        missing=1
    fi
    if (( missing != 0 )); then
        return 1
    fi
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

mkdir -p "${WORKDIR}"
download_missing_archives

if [[ "${DOWNLOAD_ONLY}" == "1" ]]; then
    note "locked package cache is ready in ${ARCHIVES}"
    exit 0
fi

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
materialize_qtchooser_configs
prune_guest_graphics_runtime
patch_xkb_for_xwayland
write_xv6_mime_package
write_kde_file_scheme_handler
compile_mime_database
generate_kservice_mime_types
write_minimal_application_mimeinfo_cache
disable_dead_ksplash_activation
disable_dead_rtkit_activation
write_false_compat
configure_bluez_activation
configure_modemmanager_activation
configure_documents_portal_activation
disable_optional_tray_plasmoids
write_portal_defaults
write_minimal_upower_config
disable_conflicting_audio_autostart
gate_xsettingsd_without_x11
patch_optional_hardware_kde_defaults
patch_pipewire_runtime_config

mkdir -p "${OVERLAY}/opt/xv6-kde"
cp "${INVENTORY}" "${OVERLAY}/opt/xv6-kde/kde-qt-package-inventory.tsv"
cp "${ORDER_FILE}" "${OVERLAY}/opt/xv6-kde/packages.apt-order.txt"
write_qt_wayland_smoke_qml
printf '%s\n' "source=ubuntu-noble-deb-archives" > "${OVERLAY}/opt/xv6-kde/README.txt"
validate_xwayland_runtime

note "staged ${#DEBS[@]} KDE/Qt package archives into ${OVERLAY}"
note "inventory=${INVENTORY}"
