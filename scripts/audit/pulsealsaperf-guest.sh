#!/bin/bash

set +e

runtime=/dev/shm/xv6-pulse-alsa-perf
server=tcp:127.0.0.1:47141
owner=/pulse-alsa-perf.owner
server_log=/pulse-alsa-perf-server.log
reducer_log=/pulse-alsa-perf-reducer.log
secondary_log=/pulse-alsa-perf-secondary.log
secondary_owner=/pulse-alsa-perf-secondary.owner
before=/pulse-alsa-perf-before.txt
after=/pulse-alsa-perf-after.txt

field()
{
    name="$1"
    file="$2"
    value=0
    while read -r line; do
        for word in $line; do
            case "$word" in
                "$name="*) value=${word#*=} ;;
            esac
        done
    done < "$file"
    echo "$value"
}

rm -rf "$runtime"
rm -f "$owner" "$secondary_owner" "$server_log" "$reducer_log" \
    "$secondary_log" "$before" "$after"
mkdir -m 0700 -p "$runtime"
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=server-start"

PULSE_RUNTIME_PATH="$runtime" XDG_RUNTIME_DIR="$runtime" \
    /bin/chromium-pulse-stream-reducer --pgroup-leader pulseaudio \
    "$owner" -- /usr/bin/pulseaudio -n --daemonize=no --system=false \
    --use-pid-file=false --exit-idle-time=-1 --log-target=stderr \
    --log-level=debug \
    --load="module-native-protocol-tcp listen=127.0.0.1 port=47141 auth-anonymous=1" \
    --load="module-alsa-sink device=hw:0 sink_name=xv6_direct_alsa rate=48000 channels=2 tsched=0 fragments=4 fragment_size=4800" \
    --load="module-suspend-on-idle timeout=1" \
    > "$server_log" 2>&1 &
server_pid=$!
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=server-spawned pid=$server_pid"

ready=0
attempt=0
while [ "$attempt" -lt 20 ]; do
    attempt=$((attempt + 1))
    PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
        --run-bounded 2 pactl -- /usr/bin/pactl info \
        >/pulse-alsa-perf-pactl.log 2>&1
    if [ "$?" -eq 0 ]; then
        ready=1
        break
    fi
done
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=server-ready ready=$ready attempts=$attempt"

if [ "$ready" -ne 1 ]; then
    cat "$server_log"
    echo "PULSE_ALSA_PERF_RESULT schema=1 status=FAIL reason=server-not-ready attempts=$attempt"
    if [ -f "$owner" ]; then
        /bin/chromium-pulse-stream-reducer --pgroup-terminate "$owner"
    fi
    wait "$server_pid"
    exit 20
fi

# Chromium reaches this sink only after module-suspend-on-idle has closed the
# ALSA handle.  Wait for that observable state so this reducer covers the same
# reopen/resume path instead of racing the one-second idle timeout.
suspended=0
suspend_attempt=0
while [ "$suspend_attempt" -lt 40 ]; do
    suspend_attempt=$((suspend_attempt + 1))
    PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
        --run-bounded 2 pactl -- /usr/bin/pactl list short sinks \
        > /pulse-alsa-perf-sinks.log 2>&1
    if [ "$?" -eq 0 ]; then
        while read -r line; do
            for word in $line; do
                case "$word" in
                    SUSPENDED) suspended=1 ;;
                esac
            done
        done < /pulse-alsa-perf-sinks.log
    fi
    [ "$suspended" -eq 1 ] && break
done
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=server-suspended suspended=$suspended attempts=$suspend_attempt"

if [ "$suspended" -ne 1 ]; then
    cat "$server_log"
    echo "PULSE_ALSA_PERF_RESULT schema=1 status=FAIL reason=suspend-timeout attempts=$suspend_attempt"
    /bin/chromium-pulse-stream-reducer --pgroup-terminate "$owner"
    wait "$server_pid"
    exit 21
fi

cat /dev/sndstat > "$before"
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=reducer-start"
PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
    --duration-seconds=20 > "$reducer_log" 2>&1 &
reducer_pid=$!

running=0
running_attempt=0
while [ "$running_attempt" -lt 40 ]; do
    running_attempt=$((running_attempt + 1))
    PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
        --run-bounded 2 pactl -- /usr/bin/pactl list short sinks \
        > /pulse-alsa-perf-running.log 2>&1
    if [ "$?" -eq 0 ]; then
        while read -r line; do
            for word in $line; do
                case "$word" in
                    RUNNING) running=1 ;;
                esac
            done
        done < /pulse-alsa-perf-running.log
    fi
    [ "$running" -eq 1 ] && break
done
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=stream-running running=$running attempts=$running_attempt"

# Force a close/reopen with playback descriptors outstanding.  This is the
# reduced form of PulseAudio's Chromium xrun recovery path and catches an
# illegal EAGAIN at the virtio RELEASE -> PREPARE boundary.
PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
    --run-bounded 4 pactl -- /usr/bin/pactl suspend-sink xv6_direct_alsa 1 \
    > /pulse-alsa-perf-force-suspend.log 2>&1
force_suspend_rc=$?
forced_suspended=0
force_suspend_attempt=0
while [ "$force_suspend_attempt" -lt 40 ]; do
    force_suspend_attempt=$((force_suspend_attempt + 1))
    PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
        --run-bounded 2 pactl -- /usr/bin/pactl list short sinks \
        > /pulse-alsa-perf-force-suspend-state.log 2>&1
    if [ "$?" -eq 0 ]; then
        while read -r line; do
            for word in $line; do
                case "$word" in
                    SUSPENDED) forced_suspended=1 ;;
                esac
            done
        done < /pulse-alsa-perf-force-suspend-state.log
    fi
    [ "$forced_suspended" -eq 1 ] && break
done
PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
    --run-bounded 4 pactl -- /usr/bin/pactl suspend-sink xv6_direct_alsa 0 \
    > /pulse-alsa-perf-force-resume.log 2>&1
force_resume_rc=$?
resumed_running=0
resume_attempt=0
while [ "$resume_attempt" -lt 40 ]; do
    resume_attempt=$((resume_attempt + 1))
    PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
        --run-bounded 2 pactl -- /usr/bin/pactl list short sinks \
        > /pulse-alsa-perf-resume-state.log 2>&1
    if [ "$?" -eq 0 ]; then
        while read -r line; do
            for word in $line; do
                case "$word" in
                    RUNNING) resumed_running=1 ;;
                esac
            done
        done < /pulse-alsa-perf-resume-state.log
    fi
    [ "$resumed_running" -eq 1 ] && break
done
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=forced-recovery running=$running suspend_rc=$force_suspend_rc suspended=$forced_suspended resume_rc=$force_resume_rc resumed_running=$resumed_running"

# Chromium adds a second playback input while its original stream remains
# active.  Pulse rewinds the ALSA sink at that boundary, then immediately
# refills it after snd_pcm_avail().  Keep this overlap in the focused reducer:
# the first post-avail write must not see a transport-only EAGAIN.
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=secondary-start"
PULSE_SERVER="$server" /bin/chromium-pulse-stream-reducer \
    --pgroup-leader pacat "$secondary_owner" -- /usr/bin/pacat \
    --playback --raw --format=float32le --rate=44100 --channels=1 /dev/zero \
    > "$secondary_log" 2>&1 &
secondary_pid=$!

wait "$reducer_pid"
reducer_rc=$?
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=reducer-done reducer_rc=$reducer_rc"
/bin/chromium-pulse-stream-reducer --pgroup-identity "$secondary_owner" \
    > /pulse-alsa-perf-secondary-identity.log 2>&1
secondary_identity_rc=$?
/bin/chromium-pulse-stream-reducer --pgroup-terminate "$secondary_owner" \
    > /pulse-alsa-perf-secondary-terminate.log 2>&1
secondary_terminate_rc=$?
wait "$secondary_pid"
secondary_rc=$?
echo "PULSE_ALSA_PERF_STAGE schema=1 phase=secondary-done secondary_rc=$secondary_rc identity_rc=$secondary_identity_rc terminate_rc=$secondary_terminate_rc"
cat /dev/sndstat > "$after"
/bin/chromium-pulse-stream-reducer --pgroup-identity "$owner" \
    > /pulse-alsa-perf-server-identity.log 2>&1
server_identity_rc=$?

played_before=$(field played_bytes "$before")
played_after=$(field played_bytes "$after")
notify_before=$(field notify_ticks "$before")
notify_after=$(field notify_ticks "$after")
played_delta=$((played_after - played_before))
notify_delta=$((notify_after - notify_before))

cat "$server_log"
cat "$reducer_log"
cat "$secondary_log"
cat /pulse-alsa-perf-secondary-identity.log
cat /pulse-alsa-perf-secondary-terminate.log
cat /pulse-alsa-perf-force-suspend.log
cat /pulse-alsa-perf-force-resume.log
cat /pulse-alsa-perf-server-identity.log
cat "$before"
cat "$after"

terminate_rc=0
/bin/chromium-pulse-stream-reducer --pgroup-terminate "$owner"
terminate_rc=$?
wait "$server_pid"
wait_rc=$?

status=PASS
reason=complete
if [ "$reducer_rc" -ne 0 ]; then
    status=FAIL
    reason=reducer
elif [ "$secondary_identity_rc" -ne 0 ] || \
     [ "$secondary_terminate_rc" -ne 0 ]; then
    status=FAIL
    reason=secondary-reducer
elif [ "$running" -ne 1 ] || [ "$force_suspend_rc" -ne 0 ] || \
     [ "$forced_suspended" -ne 1 ] || [ "$force_resume_rc" -ne 0 ] || \
     [ "$resumed_running" -ne 1 ]; then
    status=FAIL
    reason=forced-recovery
elif [ "$server_identity_rc" -ne 0 ]; then
    status=FAIL
    reason=server-crash
elif [ "$played_delta" -lt 3600000 ]; then
    status=FAIL
    reason=hardware-progress
elif [ "$notify_delta" -lt 700 ]; then
    status=FAIL
    reason=notification-progress
elif [ "$terminate_rc" -ne 0 ]; then
    status=FAIL
    reason=server-cleanup
fi

echo "PULSE_ALSA_PERF_RESULT schema=1 status=$status reason=$reason ready=$ready attempts=$attempt suspended=$suspended suspend_attempts=$suspend_attempt running=$running running_attempts=$running_attempt force_suspend_rc=$force_suspend_rc forced_suspended=$forced_suspended force_suspend_attempts=$force_suspend_attempt force_resume_rc=$force_resume_rc resumed_running=$resumed_running resume_attempts=$resume_attempt reducer_rc=$reducer_rc secondary_rc=$secondary_rc secondary_identity_rc=$secondary_identity_rc secondary_terminate_rc=$secondary_terminate_rc server_identity_rc=$server_identity_rc played_bytes_before=$played_before played_bytes_after=$played_after played_bytes_delta=$played_delta notify_ticks_before=$notify_before notify_ticks_after=$notify_after notify_ticks_delta=$notify_delta terminate_rc=$terminate_rc wait_rc=$wait_rc"
[ "$status" = PASS ]
