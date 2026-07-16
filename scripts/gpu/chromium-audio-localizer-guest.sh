#!/bin/bash
# Guest half of the default-off A2/A4 localization arm.  The host Expect
# driver stages this file only into its scratch image and invokes it once.

set +e

root=/audio-localizer
manifest="$root/manifest.log"
snapshots="$root/snapshots.log"
helper_log="$root/helper.log"
primary_socket=/dev/shm/xdg-runtime-root/pulse/native
primary_server="unix:$primary_socket"
pipewire_tcp_server="tcp:127.0.0.1:47139"
fallback_runtime=/dev/shm/xv6-audio-localizer-pulse
fallback_socket="$fallback_runtime/native"
fallback_server="tcp:127.0.0.1:47140"
fallback_owner_record="$root/fallback.owner"
chromium_owner_record="$root/chromium.owner"
fallback_pid=0
chromium_pid=0
selected_server="$primary_server"
selected_sink=xv6_null_output
unix_only=${AUDIO_LOCALIZER_UNIX_ONLY:-0}

mkdir -p "$root"
rm -f "$root"/*.log "$root"/*.raw "$root"/*.wav "$root"/*.owner
: > "$manifest"
: > "$snapshots"
exec > "$helper_log" 2>&1

row()
{
    line="AUDIO_LOCALIZER_STEP schema=1 $*"
    echo "$line"
    echo "$line" >> "$manifest"
}

bounded()
{
    name="$1"
    seconds="$2"
    expected="$3"
    logfile="$4"
    shift 4
    /bin/chromium-pulse-stream-reducer \
        --run-bounded "$seconds" "$expected" -- "$@" > "$logfile" 2>&1
    rc=$?
    row "name=$name rc=$rc timeout_seconds=$seconds expected=$expected log=$logfile"
    return "$rc"
}

snapshot()
{
    tag="$1"
    server="$2"
    echo "AUDIO_LOCALIZER_SNAPSHOT_BEGIN schema=1 tag=$tag server=$server" >> "$snapshots"
    echo "AUDIO_LOCALIZER_PROCESS_SNAPSHOT schema=1 tag=$tag" >> "$snapshots"
    ps >> "$snapshots" 2>&1
    PULSE_SERVER="$server" bounded "snapshot_${tag}_pactl_info" 8 pactl \
        "$root/snapshot-${tag}-pactl-info.log" /usr/bin/pactl info
    cat "$root/snapshot-${tag}-pactl-info.log" >> "$snapshots" 2>/dev/null
    PULSE_SERVER="$server" bounded "snapshot_${tag}_pactl_sinks_short" 8 pactl \
        "$root/snapshot-${tag}-pactl-sinks-short.log" /usr/bin/pactl list short sinks
    cat "$root/snapshot-${tag}-pactl-sinks-short.log" >> "$snapshots" 2>/dev/null
    PULSE_SERVER="$server" bounded "snapshot_${tag}_pactl_sinks" 8 pactl \
        "$root/snapshot-${tag}-pactl-sinks.log" /usr/bin/pactl list sinks
    cat "$root/snapshot-${tag}-pactl-sinks.log" >> "$snapshots" 2>/dev/null
    PULSE_SERVER="$server" bounded "snapshot_${tag}_pactl_sink_inputs" 8 pactl \
        "$root/snapshot-${tag}-pactl-sink-inputs.log" /usr/bin/pactl list short sink-inputs
    cat "$root/snapshot-${tag}-pactl-sink-inputs.log" >> "$snapshots" 2>/dev/null
    echo "AUDIO_LOCALIZER_SNAPSHOT_END schema=1 tag=$tag server=$server" >> "$snapshots"
}

cleanup_chromium()
{
    if [ "$chromium_pid" -gt 1 ]; then
        /bin/chromium-pulse-stream-reducer \
            --pgroup-terminate "$chromium_owner_record" >> "$root/chromium-owner.log" 2>&1
        terminate_rc=$?
        wait "$chromium_pid"
        wait_rc=$?
        row "name=chromium_owned_cleanup pid=$chromium_pid terminate_rc=$terminate_rc wait_rc=$wait_rc"
        chromium_pid=0
    fi
}

cleanup_fallback()
{
    if [ "$fallback_pid" -gt 1 ]; then
        /bin/chromium-pulse-stream-reducer \
            --pgroup-terminate "$fallback_owner_record" >> "$root/fallback-owner.log" 2>&1
        terminate_rc=$?
        wait "$fallback_pid"
        wait_rc=$?
        row "name=fallback_owned_cleanup pid=$fallback_pid terminate_rc=$terminate_rc wait_rc=$wait_rc"
        fallback_pid=0
    fi
}

cleanup_all()
{
    cleanup_chromium
    cleanup_fallback
}

trap cleanup_all EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

row "name=arm_contract localizer=1 qemu_audio=none audio_disable=0 no_youtube_claim=1"
if [ -x /bin/chromium-pulse-stream-reducer ]; then
    row "name=reducer_identity status=present path=/bin/chromium-pulse-stream-reducer"
else
    row "name=reducer_identity status=missing path=/bin/chromium-pulse-stream-reducer"
fi
if [ -f /opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so ]; then
    row "name=trace_identity status=present path=/opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so"
else
    row "name=trace_identity status=missing path=/opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so"
fi

/bin/grep 'default.clock.min-quantum' /usr/share/pipewire/pipewire.conf \
    > "$root/pipewire-quantum.log" 2>&1
row "name=pipewire_quantum_snapshot rc=$? log=$root/pipewire-quantum.log"
/bin/grep 'pulse.min.quantum' /usr/share/pipewire/pipewire-pulse.conf \
    > "$root/pipewire-pulse-quantum.log" 2>&1
row "name=pulse_quantum_snapshot rc=$? log=$root/pipewire-pulse-quantum.log"

/bin/chromium-pulse-stream-reducer --probe-socket "$primary_socket" \
    > "$root/primary-socket.log" 2>&1
primary_socket_rc=$?
row "name=primary_socket rc=$primary_socket_rc path=$primary_socket log=$root/primary-socket.log"
snapshot primary_pre "$primary_server"

/bin/chromium-pulse-stream-reducer --emit-fixtures \
    "$root/tone.wav" "$root/tone.s16le.raw" > "$root/fixture.log" 2>&1
fixture_rc=$?
row "name=fixture rc=$fixture_rc log=$root/fixture.log"

bounded pw_play_primary 12 pw-play "$root/pw-play-primary.log" \
    /usr/bin/pw-play --target xv6_null_output "$root/tone.wav"
pw_play_primary_rc=$?

PULSE_SERVER="$primary_server" bounded paplay_primary 12 paplay \
    "$root/paplay-primary.log" /usr/bin/paplay \
    --device=xv6_null_output --raw --format=s16le --rate=48000 --channels=2 \
    "$root/tone.s16le.raw"
paplay_primary_rc=$?

PULSE_SERVER="$primary_server" bounded reducer_primary 35 \
    chromium-pulse-stream-reducer "$root/reducer-primary.log" \
    /bin/chromium-pulse-stream-reducer --duration-seconds=20
reducer_primary_rc=$?
snapshot primary_post "$primary_server"

# AF_UNIX without Pulse shared-memory transport.  Local libpulse normally
# negotiates SHM/memfd and descriptor passing, whereas TCP cannot.  This arm
# separates the UNIX byte stream from its SCM_RIGHTS/memfd fast path.
paplay_primary_no_shm_rc=NA
reducer_primary_no_shm_rc=NA
PULSE_NO_SHM=1 PULSE_SERVER="$primary_server" bounded paplay_primary_no_shm 12 paplay \
    "$root/paplay-primary-no-shm.log" /usr/bin/paplay \
    --device=xv6_null_output --raw --format=s16le --rate=48000 --channels=2 \
    "$root/tone.s16le.raw"
paplay_primary_no_shm_rc=$?
PULSE_NO_SHM=1 PULSE_SERVER="$primary_server" bounded reducer_primary_no_shm 35 \
    chromium-pulse-stream-reducer "$root/reducer-primary-no-shm.log" \
    /bin/chromium-pulse-stream-reducer --duration-seconds=20
reducer_primary_no_shm_rc=$?
snapshot primary_no_shm_post "$primary_server"

# Short transport-localization mode.  The host harness enables this only for
# an explicitly requested diagnostic boot, after staging this helper into its
# scratch image.  Stop before the healthy TCP controls and Chromium so the
# serial trace contains only the failing AF_UNIX clients and their PipeWire
# peer; the normal arm and its classifier remain unchanged.
if [ "$unix_only" = 1 ]; then
    final_row="AUDIO_LOCALIZER_HELPER_RESULT schema=1 status=COMPLETE diagnostic_unix_only=1 primary_socket_rc=$primary_socket_rc fixture_rc=$fixture_rc pw_play_primary_rc=$pw_play_primary_rc paplay_primary_rc=$paplay_primary_rc reducer_primary_rc=$reducer_primary_rc paplay_primary_no_shm_rc=$paplay_primary_no_shm_rc reducer_primary_no_shm_rc=$reducer_primary_no_shm_rc no_youtube_claim=1"
    echo "$final_row"
    echo "$final_row" >> "$manifest"
    sync
    exit 0
fi

# Same-server, same-sink transport arm.  The gated session configuration gives
# this exact pipewire-pulse instance both its normal AF_UNIX listener and a
# loopback TCP listener.  A different result here therefore isolates socket
# family/transport behavior without changing PipeWire, format, or sink.
pipewire_tcp_ready=0
pipewire_tcp_probe_rc=1
pipewire_tcp_attempt=0
paplay_pipewire_tcp_rc=NA
reducer_pipewire_tcp_rc=NA
: > "$root/pipewire-tcp-socket.log"
while [ "$pipewire_tcp_attempt" -lt 50 ]; do
    pipewire_tcp_attempt=$((pipewire_tcp_attempt + 1))
    PULSE_SERVER="$pipewire_tcp_server" /usr/bin/pactl info \
        >> "$root/pipewire-tcp-socket.log" 2>&1
    pipewire_tcp_probe_rc=$?
    if [ "$pipewire_tcp_probe_rc" -eq 0 ]; then
        break
    fi
done
row "name=pipewire_tcp_probe rc=$pipewire_tcp_probe_rc attempts=$pipewire_tcp_attempt server=$pipewire_tcp_server log=$root/pipewire-tcp-socket.log"
if [ "$pipewire_tcp_probe_rc" -eq 0 ]; then
    pipewire_tcp_ready=1
    snapshot pipewire_tcp_pre "$pipewire_tcp_server"
    PULSE_SERVER="$pipewire_tcp_server" bounded paplay_pipewire_tcp 12 paplay \
        "$root/paplay-pipewire-tcp.log" /usr/bin/paplay \
        --device=xv6_null_output --raw --format=s16le --rate=48000 --channels=2 \
        "$root/tone.s16le.raw"
    paplay_pipewire_tcp_rc=$?
    PULSE_SERVER="$pipewire_tcp_server" bounded reducer_pipewire_tcp 35 \
        chromium-pulse-stream-reducer "$root/reducer-pipewire-tcp.log" \
        /bin/chromium-pulse-stream-reducer --duration-seconds=20
    reducer_pipewire_tcp_rc=$?
    snapshot pipewire_tcp_post "$pipewire_tcp_server"
fi

fallback_ready=0
paplay_fallback_rc=NA
reducer_fallback_rc=NA
# A successful short S16LE paplay does not prove the Chromium-shaped Pulse
# stream.  Enter the independent PulseAudio fallback when either probe fails;
# otherwise a format/latency-specific pipewire-pulse disconnect is incorrectly
# classified as a healthy primary server and the decisive comparison is never
# run.
if [ "$paplay_primary_rc" -ne 0 ] || [ "$reducer_primary_rc" -ne 0 ]; then
    rm -rf "$fallback_runtime"
    mkdir -p "$fallback_runtime"
    chmod 0700 "$fallback_runtime"
    PULSE_RUNTIME_PATH="$fallback_runtime" XDG_RUNTIME_DIR="$fallback_runtime" \
        /bin/chromium-pulse-stream-reducer --pgroup-leader pulseaudio \
        "$fallback_owner_record" -- \
        /usr/bin/pulseaudio -n --daemonize=no --system=false \
        --use-pid-file=false --exit-idle-time=-1 --log-target=stderr \
        --load="module-native-protocol-tcp listen=127.0.0.1 port=47140 auth-anonymous=1" \
        --load="module-null-sink sink_name=xv6_localizer_fallback rate=48000 channels=2" \
        > "$root/fallback-server.log" 2>&1 &
    fallback_pid=$!
    row "name=fallback_start pid=$fallback_pid socket=$fallback_socket log=$root/fallback-server.log"
    sleep 3
    /bin/chromium-pulse-stream-reducer \
        --pgroup-identity "$fallback_owner_record" \
        > "$root/fallback-owner.log" 2>&1
    fallback_identity_rc=$?
    fallback_socket_rc=1
    fallback_attempt=0
    : > "$root/fallback-socket.log"
    # Guest sleep can return early on this kernel.  Bound readiness by a fixed
    # number of short client attempts instead of assuming the nominal delay
    # elapsed before PulseAudio entered its main loop.
    while [ "$fallback_attempt" -lt 50 ]; do
        fallback_attempt=$((fallback_attempt + 1))
        PULSE_SERVER="$fallback_server" /usr/bin/pactl info \
            >> "$root/fallback-socket.log" 2>&1
        fallback_socket_rc=$?
        if [ "$fallback_socket_rc" -eq 0 ]; then
            break
        fi
    done
    row "name=fallback_probe rc=$fallback_socket_rc attempts=$fallback_attempt expected=pactl log=$root/fallback-socket.log"
    row "name=fallback_ready identity_rc=$fallback_identity_rc socket_rc=$fallback_socket_rc pid=$fallback_pid"
    if [ "$fallback_identity_rc" -eq 0 ] && [ "$fallback_socket_rc" -eq 0 ]; then
        fallback_ready=1
        PULSE_SERVER="$fallback_server" bounded fallback_set_default 8 pactl \
            "$root/fallback-set-default.log" /usr/bin/pactl set-default-sink xv6_localizer_fallback
        fallback_default_rc=$?
        snapshot fallback_pre "$fallback_server"
        PULSE_SERVER="$fallback_server" bounded paplay_fallback 12 paplay \
            "$root/paplay-fallback.log" /usr/bin/paplay \
            --device=xv6_localizer_fallback --raw --format=s16le --rate=48000 --channels=2 \
            "$root/tone.s16le.raw"
        paplay_fallback_rc=$?
        PULSE_SERVER="$fallback_server" bounded reducer_fallback 35 \
            chromium-pulse-stream-reducer "$root/reducer-fallback.log" \
            /bin/chromium-pulse-stream-reducer --duration-seconds=20
        reducer_fallback_rc=$?
        snapshot fallback_post "$fallback_server"
        selected_server="$fallback_server"
        selected_sink=xv6_localizer_fallback
    fi
fi

# Prefer the same PipeWire server's TCP listener for the Chromium smoke when
# its Chromium-shaped stream was healthy.  The standalone server remains an
# independent control and retains its own evidence rows.
if [ "$pipewire_tcp_ready" -eq 1 ] && \
   [ "$paplay_pipewire_tcp_rc" -eq 0 ] && \
   [ "$reducer_pipewire_tcp_rc" -eq 0 ]; then
    selected_server="$pipewire_tcp_server"
    selected_sink=xv6_null_output
fi

row "name=server_selection selected_server=$selected_server selected_sink=$selected_sink pipewire_tcp_ready=$pipewire_tcp_ready paplay_pipewire_tcp_rc=$paplay_pipewire_tcp_rc reducer_pipewire_tcp_rc=$reducer_pipewire_tcp_rc fallback_ready=$fallback_ready paplay_primary_rc=$paplay_primary_rc reducer_primary_rc=$reducer_primary_rc paplay_primary_no_shm_rc=$paplay_primary_no_shm_rc reducer_primary_no_shm_rc=$reducer_primary_no_shm_rc paplay_fallback_rc=$paplay_fallback_rc reducer_fallback_rc=$reducer_fallback_rc"

export XDG_RUNTIME_DIR=/dev/shm/xdg-runtime-root
export WAYLAND_DISPLAY=wayland-0
export WAYLAND_CHROMIUM_MULTIPROCESS=1
export WAYLAND_CHROMIUM_DISABLE_AUDIO_OUTPUT=0
export WAYLAND_CHROMIUM_EXTRA_FLAGS=--autoplay-policy=no-user-gesture-required
export XV6_PULSE_TRACE=1
export XV6_PULSE_TRACE_LOG=/chromium-pulse-trace.log
export LD_PRELOAD=/opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so
export PULSE_SERVER="$selected_server"
rm -f /chromium-pulse-trace.log /host-gui-wayland-chromium.log /chrome_debug.log

/bin/chromium-pulse-stream-reducer --pgroup-leader chrome \
    "$chromium_owner_record" -- \
    /bin/wayland-chromium file:///share/chromium-audio-smoke.html \
    > "$root/chromium-stdio.log" 2>&1 &
chromium_pid=$!
row "name=chromium_start pid=$chromium_pid selected_server=$selected_server selected_sink=$selected_sink"
sleep 3
/bin/chromium-pulse-stream-reducer --pgroup-identity "$chromium_owner_record" \
    > "$root/chromium-owner.log" 2>&1
chromium_identity_rc=$?
row "name=chromium_identity pid=$chromium_pid rc=$chromium_identity_rc expected=chrome"
snapshot chromium_live_3s "$selected_server"
sleep 22
snapshot chromium_live_25s "$selected_server"
cleanup_chromium

snapshot selected_final "$selected_server"
cleanup_fallback
echo "AUDIO_LOCALIZER_FINAL_PROCESS_BEGIN schema=1" >> "$snapshots"
ps >> "$snapshots" 2>&1
echo "AUDIO_LOCALIZER_FINAL_PROCESS_END schema=1" >> "$snapshots"
trap - EXIT HUP INT TERM

final_row="AUDIO_LOCALIZER_HELPER_RESULT schema=1 status=COMPLETE primary_socket_rc=$primary_socket_rc fixture_rc=$fixture_rc pw_play_primary_rc=$pw_play_primary_rc paplay_primary_rc=$paplay_primary_rc reducer_primary_rc=$reducer_primary_rc paplay_primary_no_shm_rc=$paplay_primary_no_shm_rc reducer_primary_no_shm_rc=$reducer_primary_no_shm_rc pipewire_tcp_ready=$pipewire_tcp_ready paplay_pipewire_tcp_rc=$paplay_pipewire_tcp_rc reducer_pipewire_tcp_rc=$reducer_pipewire_tcp_rc fallback_ready=$fallback_ready paplay_fallback_rc=$paplay_fallback_rc reducer_fallback_rc=$reducer_fallback_rc chromium_identity_rc=$chromium_identity_rc no_youtube_claim=1"
echo "$final_row"
echo "$final_row" >> "$manifest"
sync
exit 0
