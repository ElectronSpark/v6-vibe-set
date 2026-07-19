#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE_SCRIPT="${SCRIPT_DIR}/stage-xwayland-runtime.sh"
WRAPPER_SOURCE="${SCRIPT_DIR}/xwayland-kde-wrapper.c"
FAKE_SOURCE="${SCRIPT_DIR}/xwayland-wrapper-fake.c"
CC_BIN="${CC:-cc}"
TMP="$(mktemp -d /tmp/xv6-xwayland-wrapper-test.XXXXXX)"
trap 'rm -rf "${TMP}"' EXIT

fail() {
    echo "test-xwayland-wrapper: FAIL: $*" >&2
    exit 1
}

require_contains() {
    local text="$1"
    local expected="$2"

    [[ "${text}" == *"${expected}"* ]] ||
        fail "missing expected text: ${expected}"
}

command -v "${CC_BIN}" >/dev/null 2>&1 ||
    fail "compiler not found: ${CC_BIN}"

RUNTIME="${TMP}/runtime"
SUBSTRING_SYSROOT="${TMP}/substring-sysroot"
EXACT_SYSROOT="${TMP}/exact-sysroot"
mkdir -p "${RUNTIME}/usr/bin" \
    "${RUNTIME}/usr/share/X11/xkb/keycodes"
"${CC_BIN}" -O2 -Wall -Wextra "${FAKE_SOURCE}" \
    -o "${RUNTIME}/usr/bin/Xwayland"
cp "${RUNTIME}/usr/bin/Xwayland" "${RUNTIME}/usr/bin/xkbcomp"
install -m 0644 /dev/null \
    "${RUNTIME}/usr/share/X11/xkb/keycodes/evdev"

substring_stage="$({
    XV6_FAKE_XWAYLAND_HELP_MODE=substring \
    XWAYLAND_RUNTIME_ROOT="${RUNTIME}" \
    XWAYLAND_ALLOW_HOST_FALLBACK=0 \
        "${STAGE_SCRIPT}" "${SUBSTRING_SYSROOT}" "${TMP}/unused-wrapper"
} 2>&1)"
require_contains "${substring_stage}" \
    "Xwayland -glamor option supported=0"

set +e
unsupported_log="$({
    XV6_XWAYLAND_GLAMOR=off \
        "${SUBSTRING_SYSROOT}/bin/Xwayland"
} 2>&1)"
unsupported_status=$?
set -e
[[ "${unsupported_status}" -eq 64 ]] ||
    fail "unsupported explicit glamor returned ${unsupported_status}, expected 64"
require_contains "${unsupported_log}" \
    "XV6_XWAYLAND_GLAMOR=off requires the -glamor option"
require_contains "${unsupported_log}" "refusing to start"

exact_stage="$({
    XV6_FAKE_XWAYLAND_HELP_MODE=exact \
    XWAYLAND_RUNTIME_ROOT="${RUNTIME}" \
    XWAYLAND_ALLOW_HOST_FALLBACK=0 \
        "${STAGE_SCRIPT}" "${EXACT_SYSROOT}" "${TMP}/unused-wrapper"
} 2>&1)"
require_contains "${exact_stage}" "Xwayland -glamor option supported=1"

FAKE_REAL="${TMP}/fake-Xwayland.real"
WRAPPER_TEST="${TMP}/Xwayland-wrapper"
cp "${RUNTIME}/usr/bin/Xwayland" "${FAKE_REAL}"
"${CC_BIN}" -O2 -Wall -Wextra \
    -DXV6_XWAYLAND_HAS_GLAMOR_OPTION=1 \
    "-DXV6_XWAYLAND_REAL_PATH=\"${FAKE_REAL}\"" \
    "${WRAPPER_SOURCE}" -o "${WRAPPER_TEST}"

default_log="$({
    env -u XV6_XWAYLAND_GLAMOR -u XV6_XWAYLAND_VERBOSE \
        "${WRAPPER_TEST}" :7 -displayfd 9
} 2>&1)"
require_contains "${default_log}" \
    "final argv argc=6 glamor=(absent) verbose=(absent)"
[[ "${default_log}" != *"fake-Xwayland: argv[1]=-glamor"* ]] ||
    fail "unset glamor unexpectedly forced a -glamor argument"
require_contains "${default_log}" "fake-Xwayland: argv[1]=-xkbdir"
require_contains "${default_log}" "fake-Xwayland: argv[3]=:7"

argv_log="$({
    XV6_XWAYLAND_GLAMOR=gl XV6_XWAYLAND_VERBOSE=2 \
        "${WRAPPER_TEST}" :7 -displayfd 9
} 2>&1)"
require_contains "${argv_log}" \
    "final argv argc=10 glamor=gl verbose=2"
require_contains "${argv_log}" "fake-Xwayland: argv[1]=-glamor"
require_contains "${argv_log}" "fake-Xwayland: argv[2]=gl"
require_contains "${argv_log}" "fake-Xwayland: argv[3]=-verbose"
require_contains "${argv_log}" "fake-Xwayland: argv[4]=2"
require_contains "${argv_log}" "fake-Xwayland: argv[5]=-xkbdir"
require_contains "${argv_log}" \
    "fake-Xwayland: argv[6]=/usr/share/X11/xkb"
require_contains "${argv_log}" "fake-Xwayland: argv[7]=:7"
require_contains "${argv_log}" "fake-Xwayland: argv[8]=-displayfd"
require_contains "${argv_log}" "fake-Xwayland: argv[9]=9"

echo "test-xwayland-wrapper: PASS"
