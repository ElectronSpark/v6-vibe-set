#!/usr/bin/env bash
# Import a host Linux GUI executable into the xv6 rootfs overlay.
#
# This is an offline staging helper for docs/active-work-plan.md (Linux ABI and application compatibility).
# It does not claim the imported app is supported; runtime proof still requires
# a Weston desktop launch, visible window, input, clean exit, logs, framebuffer
# proof, and the existing GPU/WebKit gates.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
OVERLAY="${ROOTFS_OVERLAY:-${REPO_ROOT}/rootfs-overlay}"

APP_ID=""
APP_NAME=""
DRY_RUN=0
REPLACE=0
INPUT=""
EXTRA_DATA=()

usage() {
    cat >&2 <<'EOF'
usage: import-host-gui.sh [options] <host-executable-or-desktop-file>

Options:
  --id ID          Guest import id, default derived from executable basename.
  --name NAME      Desktop entry name, default derived from .desktop Name/basename.
  --data PATH      Copy an additional host file/directory under app data/.
  --overlay DIR    Rootfs overlay to update, default rootfs-overlay.
  --replace        Replace an existing /opt/host-gui/ID import.
  --dry-run        Analyze and print the import plan without writing files.
  -h, --help       Show this help.
EOF
}

die() {
    echo "import-host-gui: $*" >&2
    exit 1
}

note() {
    echo "import-host-gui: $*" >&2
}

file_size() {
    wc -c < "$1" | tr -d '[:space:]'
}

file_sha256() {
    if command -v sha256sum >/dev/null; then
        sha256sum "$1" | awk '{ print $1 }'
    else
        printf 'unavailable'
    fi
}

manifest_add_file() {
    local kind="$1"
    local host_path="$2"
    local guest_path="$3"
    local local_path="$4"
    printf '%s\t%s\t%s\t%s\t%s\n' \
        "${kind}" "${host_path}" "${guest_path}" \
        "$(file_size "${local_path}")" "$(file_sha256 "${local_path}")" >> "${manifest}"
}

manifest_add_note() {
    local kind="$1"
    local host_path="$2"
    local guest_path="$3"
    printf '%s\t%s\t%s\t-\t-\n' "${kind}" "${host_path}" "${guest_path}" >> "${manifest}"
}

sanitize_id() {
    local raw="$1"
    raw="${raw##*/}"
    raw="${raw%.desktop}"
    raw="${raw%.*}"
    printf '%s\n' "${raw}" |
        tr '[:upper:]' '[:lower:]' |
        sed -E 's/[^a-z0-9._+-]+/-/g; s/^-+//; s/-+$//'
}

desktop_value() {
    local key="$1"
    local file="$2"
    awk -F= -v key="${key}" '
        $0 ~ /^\[/ { section = $0 }
        section == "[Desktop Entry]" && $1 == key {
            sub(/\r$/, "", $2)
            print $2
            exit
        }
    ' "${file}"
}

parse_exec_line() {
    local line="$1"
    python3 - "${line}" <<'PY'
import shlex
import sys

line = sys.argv[1]
out = []
for token in shlex.split(line):
    if token.startswith('%') and len(token) <= 2:
        continue
    token = ''.join(part for part in token.split('%') if part[:1] not in 'fFuUdDnNickvm')
    if token:
        out.append(token)
for token in out:
    sys.stdout.buffer.write(token.encode() + b'\0')
PY
}

shell_quote() {
    printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

c_string_literal() {
    python3 -c 'import json, sys; print(json.dumps(sys.argv[1]))' "$1"
}

is_graphics_runtime_lib() {
    local base
    base="$(basename "$1")"
    case "${base}" in
        libEGL.so*|libGL.so*|libGLX.so*|libGLdispatch.so*|\
        libGLES*.so*|libOpenGL.so*|libglapi.so*|\
        libgbm.so*|libdrm.so*|libdrm_*.so*|libwayland-*.so*|libweston-*.so*)
            return 0
            ;;
    esac
    return 1
}

add_nss_module_libs() {
    local lib="$1"
    local dir
    local module

    case "$(basename "${lib}")" in
        libnss3.so*|libnssutil3.so*|libsmime3.so*) ;;
        *) return 0 ;;
    esac

    dir="$(dirname "${lib}")"
    for module in libsoftokn3.so libfreeblpriv3.so libfreebl3.so; do
        if [[ -e "${dir}/${module}" ]]; then
            append_unique "${dir}/${module}" "${libs[@]}" &&
                libs+=("${dir}/${module}")
        fi
    done
}

resolve_executable() {
    local exe="$1"
    if [[ "${exe}" == */* ]]; then
        [[ -x "${exe}" ]] || die "executable not found or not executable: ${exe}"
        realpath "${exe}"
    else
        command -v -- "${exe}" || die "executable not found in PATH: ${exe}"
    fi
}

append_unique() {
    local value="$1"
    shift
    local existing
    for existing in "$@"; do
        [[ "${existing}" == "${value}" ]] && return 1
    done
    return 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --id)
            [[ $# -ge 2 ]] || die "--id needs a value"
            APP_ID="$2"
            shift 2
            ;;
        --name)
            [[ $# -ge 2 ]] || die "--name needs a value"
            APP_NAME="$2"
            shift 2
            ;;
        --data)
            [[ $# -ge 2 ]] || die "--data needs a value"
            EXTRA_DATA+=("$2")
            shift 2
            ;;
        --overlay)
            [[ $# -ge 2 ]] || die "--overlay needs a value"
            OVERLAY="$2"
            shift 2
            ;;
        --replace)
            REPLACE=1
            shift
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            die "unknown option: $1"
            ;;
        *)
            [[ -z "${INPUT}" ]] || die "only one input may be provided"
            INPUT="$1"
            shift
            ;;
    esac
done

[[ -n "${INPUT}" ]] || { usage; exit 2; }
[[ -d "${OVERLAY}" ]] || die "overlay not found: ${OVERLAY}"
command -v ldd >/dev/null || die "ldd is required"
command -v readelf >/dev/null || die "readelf is required"
command -v python3 >/dev/null || die "python3 is required for .desktop Exec parsing"

EXEC_ARGS=()
if [[ "${INPUT}" == *.desktop ]]; then
    [[ -f "${INPUT}" ]] || die ".desktop file not found: ${INPUT}"
    exec_line="$(desktop_value Exec "${INPUT}")"
    [[ -n "${exec_line}" ]] || die "missing Exec= in ${INPUT}"
    while IFS= read -r -d '' arg; do
        EXEC_ARGS+=("${arg}")
    done < <(parse_exec_line "${exec_line}")
    [[ ${#EXEC_ARGS[@]} -gt 0 ]] || die "Exec= did not contain an executable"
    [[ -n "${APP_NAME}" ]] || APP_NAME="$(desktop_value Name "${INPUT}")"
else
    EXEC_ARGS=("${INPUT}")
fi

HOST_EXE="$(resolve_executable "${EXEC_ARGS[0]}")"
EXEC_ARGS[0]="${HOST_EXE}"
[[ -n "${APP_ID}" ]] || APP_ID="$(sanitize_id "${HOST_EXE}")"
[[ -n "${APP_ID}" ]] || die "could not derive app id"
[[ "${APP_ID}" =~ ^[a-z0-9._+-]+$ ]] || die "invalid app id: ${APP_ID}"
[[ -n "${APP_NAME}" ]] || APP_NAME="$(basename "${HOST_EXE}")"

interp="$(
    readelf -l "${HOST_EXE}" |
        sed -nE 's/.*Requesting program interpreter: ([^]]+)\].*/\1/p' |
        head -1
)"
[[ -n "${interp}" ]] || die "${HOST_EXE} is not a dynamically linked ELF executable"
[[ -e "${interp}" ]] || die "ELF interpreter not found on host: ${interp}"

libs=()
skipped=()
while IFS= read -r line; do
    path=""
    if [[ "${line}" =~ '=> '[[:space:]]*(/[^[:space:]]+) ]]; then
        path="${BASH_REMATCH[1]}"
    elif [[ "${line}" =~ ^[[:space:]]*(/[^[:space:]]+) ]]; then
        path="${BASH_REMATCH[1]}"
    fi
    [[ -n "${path}" && -e "${path}" ]] || continue
    [[ "$(realpath "${path}")" == "$(realpath "${interp}")" ]] && continue
    if is_graphics_runtime_lib "${path}"; then
        append_unique "${path}" "${skipped[@]}" && skipped+=("${path}")
        continue
    fi
    if append_unique "${path}" "${libs[@]}"; then
        libs+=("${path}")
        add_nss_module_libs "${path}"
    fi
done < <(ldd "${HOST_EXE}")

app_root="${OVERLAY}/opt/host-gui/${APP_ID}"
guest_root="/opt/host-gui/${APP_ID}"
desktop_path="${OVERLAY}/root/desktop/imported-${APP_ID}"
launcher_src="${app_root}/launcher.c"
launcher_bin="${OVERLAY}/bin/host-${APP_ID}"
manifest="${app_root}/manifest.tsv"

note "input=${INPUT}"
note "id=${APP_ID} name=${APP_NAME}"
note "host_exe=${HOST_EXE}"
note "interpreter=${interp}"
note "copy_libs=${#libs[@]} skipped_guest_graphics_libs=${#skipped[@]}"
for lib in "${skipped[@]}"; do
    note "skip-guest-runtime $(basename "${lib}") from ${lib}"
done
if [[ "${DRY_RUN}" == "1" ]]; then
    note "dry-run: would stage ${app_root}"
    note "dry-run: would compile ELF launcher ${launcher_bin}"
    note "dry-run: would create desktop symlink ${desktop_path} -> ../../bin/host-${APP_ID}"
    exit 0
fi

if [[ -e "${app_root}" && "${REPLACE}" != "1" ]]; then
    die "${app_root} exists; pass --replace to overwrite"
fi
rm -rf "${app_root}"
mkdir -p "${app_root}/bin" "${app_root}/lib" "${app_root}/data" \
    "${OVERLAY}/bin" "${OVERLAY}/root/desktop"
printf 'kind\thost_path\tguest_path\tbytes\tsha256\n' > "${manifest}"

cp -aL "${HOST_EXE}" "${app_root}/bin/$(basename "${HOST_EXE}")"
manifest_add_file "executable" "${HOST_EXE}" "${guest_root}/bin/$(basename "${HOST_EXE}")" "${app_root}/bin/$(basename "${HOST_EXE}")"
cp -aL "${interp}" "${app_root}/lib/$(basename "${interp}")"
manifest_add_file "interpreter" "${interp}" "${guest_root}/lib/$(basename "${interp}")" "${app_root}/lib/$(basename "${interp}")"
for lib in "${libs[@]}"; do
    cp -aL "${lib}" "${app_root}/lib/$(basename "${lib}")"
    manifest_add_file "library" "${lib}" "${guest_root}/lib/$(basename "${lib}")" "${app_root}/lib/$(basename "${lib}")"
done
for lib in "${skipped[@]}"; do
    manifest_add_note "skipped-guest-runtime" "${lib}" "guest-provided"
done
for data in "${EXTRA_DATA[@]}"; do
    [[ -e "${data}" ]] || die "data path not found: ${data}"
    cp -aL "${data}" "${app_root}/data/"
    manifest_add_note "data" "${data}" "${guest_root}/data/$(basename "${data}")"
done
if [[ "${INPUT}" == *.desktop ]]; then
    cp -a "${INPUT}" "${app_root}/data/source.desktop"
    manifest_add_file "source-desktop" "${INPUT}" "${guest_root}/data/source.desktop" "${app_root}/data/source.desktop"
fi

default_args=("${EXEC_ARGS[@]:1}")
{
    cat <<EOF
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *guest_root = $(c_string_literal "${guest_root}");
static const char *app_id = $(c_string_literal "${APP_ID}");
static const char *interp_base = $(c_string_literal "$(basename "${interp}")");
static const char *exe_base = $(c_string_literal "$(basename "${HOST_EXE}")");
static const char *default_args[] = {
EOF
    for arg in "${default_args[@]}"; do
        printf '    %s,\n' "$(c_string_literal "${arg}")"
    done
    cat <<'EOF'
    NULL
};

static char *
join3(const char *a, const char *b, const char *c)
{
    size_t len = strlen(a) + strlen(b) + strlen(c) + 1;
    char *out = malloc(len);
    if (!out)
        return NULL;
    snprintf(out, len, "%s%s%s", a, b, c);
    return out;
}

int
main(int argc, char **argv)
{
    char log_path[256];
    char *interp;
    char *program;
    char *ld_path;
    char *env_ld_path;
    char **child_argv;
    size_t default_count = 0;
    size_t out = 0;
    int fd;

    setenv("XDG_RUNTIME_DIR", "/tmp/wayland-root", 0);
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
    setenv("GDK_BACKEND", "wayland", 0);
    setenv("QT_QPA_PLATFORM", "wayland", 0);
    setenv("SDL_VIDEODRIVER", "wayland", 0);
    setenv("XCURSOR_PATH", "/share/icons", 0);
    setenv("XCURSOR_THEME", "Adwaita", 0);
    setenv("XCURSOR_SIZE", "24", 0);
    setenv("SSL_CERT_FILE", "/etc/ssl/certs/ca-certificates.crt", 0);

    snprintf(log_path, sizeof(log_path), "/tmp/host-gui-%s.log", app_id);
    fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    if (chdir(guest_root) < 0) {
        fprintf(stderr, "host-gui-launcher: chdir %s failed: %s\n",
                guest_root, strerror(errno));
        return 127;
    }

    interp = join3(guest_root, "/lib/", interp_base);
    program = join3(guest_root, "/bin/", exe_base);
    ld_path = join3(guest_root, "/lib:/lib:/lib64:/usr/lib:/usr/lib64", "");
    env_ld_path = getenv("LD_LIBRARY_PATH")
        ? join3(ld_path, ":", getenv("LD_LIBRARY_PATH"))
        : strdup(ld_path);
    if (!interp || !program || !ld_path || !env_ld_path) {
        fprintf(stderr, "host-gui-launcher: out of memory\n");
        return 127;
    }
    setenv("LD_LIBRARY_PATH", env_ld_path, 1);

    while (default_args[default_count])
        default_count++;

    child_argv = calloc(4 + default_count + (size_t)argc, sizeof(char *));
    if (!child_argv) {
        fprintf(stderr, "host-gui-launcher: out of memory\n");
        return 127;
    }
    child_argv[out++] = interp;
    child_argv[out++] = "--library-path";
    child_argv[out++] = ld_path;
    child_argv[out++] = program;
    for (size_t i = 0; i < default_count; i++)
        child_argv[out++] = (char *)default_args[i];
    for (int i = 1; i < argc; i++)
        child_argv[out++] = argv[i];
    child_argv[out] = NULL;

    fprintf(stderr, "host-gui-launcher: exec %s via %s\n", program, interp);
    execv(interp, child_argv);
    fprintf(stderr, "host-gui-launcher: exec failed: %s\n", strerror(errno));
    return 127;
}
EOF
} > "${launcher_src}"

cc -O2 -Wall -o "${launcher_bin}" "${launcher_src}"
manifest_add_file "launcher-source" "generated" "${guest_root}/launcher.c" "${launcher_src}"
manifest_add_file "launcher" "generated" "/bin/host-${APP_ID}" "${launcher_bin}"

rm -f "${desktop_path}"
ln -s "../../bin/host-${APP_ID}" "${desktop_path}"
manifest_add_file "desktop-symlink" "generated" "/root/desktop/imported-${APP_ID}" "${desktop_path}"

note "staged=${app_root}"
note "desktop=${desktop_path}"
note "manifest=${manifest}"
note "log=/tmp/host-gui-${APP_ID}.log"
