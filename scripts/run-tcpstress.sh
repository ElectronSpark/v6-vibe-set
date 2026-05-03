#!/usr/bin/env bash
# scripts/run-tcpstress.sh
#
# Host↔guest TCP concurrency stress runner.  Starts the host TCP stress
# server, boots xv6-os headless, drives /bin/tcpstress over the serial
# console, and reports pass/fail.  Designed to exercise the same lwIP
# recvmmsg STREAM path (and SMP scheduler / completion / wait-queue paths)
# that WebKit hits during a real browse — without WebKit in the loop.
#
# Outputs a final line:
#   run-tcpstress: PASS | FAIL  parallel=N payload=B iters=I elapsed=Ts
#
# Env knobs (all optional):
#   TCPSTRESS_PARALLEL=8         concurrent client conns
#   TCPSTRESS_PAYLOAD=1048576    bytes per response
#   TCPSTRESS_ITERS=1            iterations per child
#   TCPSTRESS_HOST_PORT=5001     host listen port (forwarded by SLIRP)
#   TCPSTRESS_DELAY_MS=0         host artificial chunk pacing (slow-server sim)
#   TCPSTRESS_CHUNK=4096         host write() chunk size
#   TCPSTRESS_TIMEOUT=180        wall-clock seconds for the VM run
#   TCPSTRESS_BOOT_GRACE=12      seconds to wait after boot before driving sh
#   TCPSTRESS_LOG=/tmp/run-tcpstress.log
#   TCPSTRESS_KEEP=1             keep host server log on success
#   TCPSTRESS_NET_BACKEND=user|tap   user (default, slow ~30 Mbit/s SLIRP)
#                                    or tap (fast, requires `sudo bash
#                                    scripts/setup-tap.sh up` first AND a
#                                    DHCP server on the tap; see setup-tap.sh).
#   TCPSTRESS_TAP_HOST_IP=192.168.78.1  host IP on the tap interface,
#                                       passed to /bin/tcpstress as argv[1].
#   FSIMG, KERNEL, BUILD_DIR     forwarded to scripts/run-qemu.sh
#
# The host server only handles --max-conns connections then exits, so we
# never leave a stray Python listener behind even if QEMU dies unexpectedly.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

ARCH="${ARCH:-x86_64}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build-${ARCH}}"
FSIMG="${FSIMG:-${BUILD_DIR}/fs.img}"

PARALLEL="${TCPSTRESS_PARALLEL:-8}"
PAYLOAD="${TCPSTRESS_PAYLOAD:-1048576}"
ITERS="${TCPSTRESS_ITERS:-1}"
HOST_PORT="${TCPSTRESS_HOST_PORT:-5001}"
DELAY_MS="${TCPSTRESS_DELAY_MS:-0}"
CHUNK="${TCPSTRESS_CHUNK:-4096}"
TIMEOUT_SECS="${TCPSTRESS_TIMEOUT:-180}"
BOOT_GRACE="${TCPSTRESS_BOOT_GRACE:-12}"
LOG_PATH="${TCPSTRESS_LOG:-/tmp/run-tcpstress.log}"
HOST_LOG="${LOG_PATH%.log}.host.log"

# SLIRP guest gateway — fixed address that means "the host running QEMU".
GUEST_HOST_IP="10.0.2.2"
NET_BACKEND="${TCPSTRESS_NET_BACKEND:-user}"
if [[ "${NET_BACKEND}" == "tap" ]]; then
    GUEST_HOST_IP="${TCPSTRESS_TAP_HOST_IP:-192.168.78.1}"
fi

# Total connections the host server will see across all forks.
MAX_CONNS=$(( PARALLEL * ITERS + 4 ))

KERNEL_CANDIDATES=(
    "${KERNEL:-}"
    "${BUILD_DIR}/kernel/kernel.elf"
    "${BUILD_DIR}/kernel/build/kernel/kernel"
    "${BUILD_DIR}/kernel/kernel/kernel"
)
KERNEL_PATH=""
for c in "${KERNEL_CANDIDATES[@]}"; do
    [[ -n "$c" && -f "$c" ]] && { KERNEL_PATH="$c"; break; }
done
if [[ -z "${KERNEL_PATH}" || ! -f "${FSIMG}" ]]; then
    echo "run-tcpstress: kernel or rootfs missing (KERNEL_PATH='${KERNEL_PATH}' FSIMG='${FSIMG}')" >&2
    exit 1
fi

if ! command -v python3 >/dev/null; then
    echo "run-tcpstress: python3 required for host server" >&2
    exit 1
fi

# --- Free up a previous run if still around ----------------------------------
if pgrep -f tcpstress-host-server.py >/dev/null 2>&1; then
    pkill -f tcpstress-host-server.py 2>/dev/null || true
    sleep 1
fi

# --- Start host server -------------------------------------------------------
echo "run-tcpstress: net backend=${NET_BACKEND} guest_host_ip=${GUEST_HOST_IP}"
echo "run-tcpstress: starting host server on :${HOST_PORT} (max_conns=${MAX_CONNS}, chunk=${CHUNK}, delay_ms=${DELAY_MS})"
: >"${HOST_LOG}"
python3 "${SCRIPT_DIR}/tcpstress-host-server.py" \
    --bind 0.0.0.0 --port "${HOST_PORT}" \
    --chunk "${CHUNK}" --delay-ms "${DELAY_MS}" \
    --max-conns "${MAX_CONNS}" \
    >"${HOST_LOG}" 2>&1 &
HOST_PID=$!
trap 'kill -9 "${HOST_PID}" 2>/dev/null || true' EXIT

# Wait until the server is actually listening (or fail fast).
for _ in $(seq 1 50); do
    if ss -ltnH "sport = :${HOST_PORT}" 2>/dev/null | grep -q LISTEN; then
        break
    fi
    sleep 0.1
done
if ! ss -ltnH "sport = :${HOST_PORT}" 2>/dev/null | grep -q LISTEN; then
    echo "run-tcpstress: host server failed to listen, see ${HOST_LOG}"
    cat "${HOST_LOG}" >&2 || true
    exit 1
fi

# --- SLIRP needs to forward an inbound connection only if you connect into  --
# --- the guest.  Outbound from guest → 10.0.2.2:${HOST_PORT} works without  --
# --- any -netdev hostfwd; default SLIRP routing handles it.                 --

# --- Build the serial-console driver script.  Init drops us at /bin/sh,    --
# --- so we just type our test command, wait for "tcpstress: ok" or         --
# --- "tcpstress: FAIL", then poweroff via /bin/shutdown.                   --

DRIVE_SCRIPT=$(mktemp /tmp/tcpstress-driver.XXXXXX.sh)
cat >"${DRIVE_SCRIPT}" <<EOF
echo TCPSTRESS-START
/bin/_tcpstress ${GUEST_HOST_IP} ${HOST_PORT} ${PARALLEL} ${PAYLOAD} ${ITERS}
TS_RC=\$?
echo TCPSTRESS-EXIT-RC=\$TS_RC
/bin/_shutdown
EOF
trap 'rm -f "${DRIVE_SCRIPT}"; kill -9 "${HOST_PID}" 2>/dev/null || true' EXIT

# Concatenate: a boot-grace pause (silence on stdin while init/sh comes up),
# then the driver script.  Use `sleep` on the host to delay our writes to
# the QEMU serial input.  This works because run-qemu.sh uses -serial mon:stdio,
# which forwards our stdin straight into the guest's console.
INPUT_PIPE=$(mktemp -u /tmp/tcpstress-input.XXXXXX)
mkfifo "${INPUT_PIPE}"
trap 'rm -f "${DRIVE_SCRIPT}" "${INPUT_PIPE}"; kill -9 "${HOST_PID}" 2>/dev/null || true' EXIT

(
    sleep "${BOOT_GRACE}"
    # Send one line at a time with a small pause; the xv6 console driver
    # appears to discard input that arrives before the line-editor is
    # actually scheduled.
    printf 'echo TCPSTRESS-START\n'; sleep 1
    printf '/bin/tcpstress %s %d %d %d %d\n' \
        "${GUEST_HOST_IP}" "${HOST_PORT}" "${PARALLEL}" "${PAYLOAD}" "${ITERS}"
    # Give tcpstress a chance to actually run before queuing more input.
    sleep $(( TIMEOUT_SECS - BOOT_GRACE - 10 ))
    printf '/bin/shutdown\n'
    sleep 5
) >"${INPUT_PIPE}" &
WRITER_PID=$!

echo "run-tcpstress: booting xv6 (timeout=${TIMEOUT_SECS}s, log=${LOG_PATH})"
: >"${LOG_PATH}"

set +e
DISPLAY_MODE=nographic \
QEMU_NET=1 \
QEMU_NET_BACKEND="${NET_BACKEND}" \
QEMU_GPU=none \
QEMU_APPEND="root=/dev/disk0 netsurf=0 webkit=0 video=80x25" \
timeout "${TIMEOUT_SECS}" \
    bash "${SCRIPT_DIR}/run-qemu.sh" "${ARCH}" "${KERNEL_PATH}" "${FSIMG}" \
    <"${INPUT_PIPE}" \
    >"${LOG_PATH}" 2>&1
QEMU_RC=$?
set -e

kill "${WRITER_PID}" 2>/dev/null || true
wait "${WRITER_PID}" 2>/dev/null || true

# Even if QEMU was killed by timeout, the log may already contain the result.
GUEST_OK_LINE=$(grep -E '^tcpstress: ok' "${LOG_PATH}" | head -1 || true)
GUEST_FAIL_LINE=$(grep -E '^tcpstress: (FAIL|MISMATCH)' "${LOG_PATH}" | head -1 || true)

# Heartbeat info from kernel for triage.
HEARTBEATS=$(grep -E 'tcpip_thread:' "${LOG_PATH}" | tail -3 || true)

echo "----- guest tcpstress lines -----"
grep -E '^tcpstress:|TCPSTRESS-' "${LOG_PATH}" || echo "(none)"
if [[ -n "${HEARTBEATS}" ]]; then
    echo "----- recent tcpip_thread heartbeats -----"
    echo "${HEARTBEATS}"
fi
echo "----------------------------------"

if [[ -n "${GUEST_OK_LINE}" && -z "${GUEST_FAIL_LINE}" ]]; then
    echo "run-tcpstress: PASS  ${GUEST_OK_LINE#tcpstress: }"
    [[ "${TCPSTRESS_KEEP:-0}" != "1" ]] && rm -f "${HOST_LOG}"
    exit 0
fi

echo "run-tcpstress: FAIL  qemu_rc=${QEMU_RC}  guest_ok='${GUEST_OK_LINE}'  guest_fail='${GUEST_FAIL_LINE}'"
echo "  full log:    ${LOG_PATH}"
echo "  host server: ${HOST_LOG}"
exit 2
