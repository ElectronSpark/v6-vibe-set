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
fallback_runtime=/dev/shm/xv6-audio-localizer-pulse
fallback_socket="$fallback_runtime/native"
fallback_server="unix:$fallback_socket"
fallback_owner_record="$root/fallback.owner"
chromium_owner_record="$root/chromium.owner"
fallback_pid=0
chromium_pid=0
selected_server="$primary_server"
selected_sink=xv6_null_output

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

fallback_ready=0
paplay_fallback_rc=NA
reducer_fallback_rc=NA
if [ "$paplay_primary_rc" -ne 0 ]; then
    rm -rf "$fallback_runtime"
    mkdir -p "$fallback_runtime"
    chmod 0700 "$fallback_runtime"
    PULSE_RUNTIME_PATH="$fallback_runtime" XDG_RUNTIME_DIR="$fallback_runtime" \
        /bin/chromium-pulse-stream-reducer --pgroup-leader pulseaudio \
        "$fallback_owner_record" -- \
        /usr/bin/pulseaudio -n --daemonize=no --system=false \
        --use-pid-file=false --exit-idle-time=-1 --log-target=stderr \
        --load="module-native-protocol-unix socket=$fallback_socket auth-anonymous=1" \
        --load="module-null-sink sink_name=xv6_localizer_fallback rate=48000 channels=2" \
        > "$root/fallback-server.log" 2>&1 &
    fallback_pid=$!
    row "name=fallback_start pid=$fallback_pid socket=$fallback_socket log=$root/fallback-server.log"
    sleep 3
    /bin/chromium-pulse-stream-reducer \
        --pgroup-identity "$fallback_owner_record" \
        > "$root/fallback-owner.log" 2>&1
    fallback_identity_rc=$?
    /bin/chromium-pulse-stream-reducer --probe-socket "$fallback_socket" \
        > "$root/fallback-socket.log" 2>&1
    fallback_socket_rc=$?
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

row "name=server_selection selected_server=$selected_server selected_sink=$selected_sink fallback_ready=$fallback_ready paplay_primary_rc=$paplay_primary_rc reducer_primary_rc=$reducer_primary_rc paplay_fallback_rc=$paplay_fallback_rc reducer_fallback_rc=$reducer_fallback_rc"

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

final_row="AUDIO_LOCALIZER_HELPER_RESULT schema=1 status=COMPLETE primary_socket_rc=$primary_socket_rc fixture_rc=$fixture_rc pw_play_primary_rc=$pw_play_primary_rc paplay_primary_rc=$paplay_primary_rc reducer_primary_rc=$reducer_primary_rc fallback_ready=$fallback_ready paplay_fallback_rc=$paplay_fallback_rc reducer_fallback_rc=$reducer_fallback_rc chromium_identity_rc=$chromium_identity_rc no_youtube_claim=1"
echo "$final_row"
echo "$final_row" >> "$manifest"
sync
exit 0
