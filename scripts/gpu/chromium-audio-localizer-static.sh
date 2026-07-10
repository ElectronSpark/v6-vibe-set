#!/usr/bin/env bash
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
tmp="$(mktemp -d /tmp/xv6-audio-localizer-static.XXXXXX)"
owned_pid=0
owned_record="${tmp}/owner.record"
cd "${repo}"

cleanup() {
    if [[ "${owned_pid}" -gt 1 ]]; then
        "${tmp}/reducer" --pgroup-terminate "${owned_record}" \
            >>"${tmp}/owner.log" 2>&1 || true
        wait "${owned_pid}" 2>/dev/null || true
    fi
    rm -rf "${tmp}"
}
trap cleanup EXIT HUP INT TERM

expect "${repo}/scripts/gpu/chromium-audio-localizer-static.tcl"
bash -n "${repo}/scripts/gpu/chromium-audio-localizer-guest.sh"

cc -std=c11 -O2 -Wall -Wextra -Werror \
    -o "${tmp}/reducer" \
    "${repo}/scripts/image/chromium-pulse-stream-reducer.c" -ldl -pthread
cc -std=c11 -O2 -Wall -Wextra -Werror \
    -o "${tmp}/launcher" \
    "${repo}/scripts/image/chromium-pulse-stream-reducer-launcher.c"
cc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared \
    -o "${tmp}/trace.so" \
    "${repo}/scripts/image/chromium-pulse-trace-preload.c" -ldl -pthread
cc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared \
    -Wl,-soname,libpulse.so.0 \
    -Wl,--version-script="${repo}/scripts/image/chromium-pulse-trace-fake.map" \
    -o "${tmp}/libpulse.so.0" \
    "${repo}/scripts/image/chromium-pulse-trace-fake.c"
mkdir -p "${tmp}/provider-b"
cc -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared \
    -DFAKE_CONTEXT_STATE=104 \
    -Wl,-soname,libpulse.so.0 \
    -Wl,--version-script="${repo}/scripts/image/chromium-pulse-trace-fake.map" \
    -o "${tmp}/provider-b/libpulse.so.0" \
    "${repo}/scripts/image/chromium-pulse-trace-fake.c"
cc -std=c11 -O2 -Wall -Wextra -Werror -pthread \
    -o "${tmp}/trace-reducer" \
    "${repo}/scripts/image/chromium-pulse-trace-preload-reducer.c" -ldl

"${tmp}/reducer" --emit-fixtures "${tmp}/tone.wav" "${tmp}/tone.raw" \
    >"${tmp}/fixture.log"
[[ "$(stat -c '%s' "${tmp}/tone.wav")" == 576044 ]]
[[ "$(stat -c '%s' "${tmp}/tone.raw")" == 576000 ]]

timeout --signal=TERM --kill-after=2 15 \
    env LD_LIBRARY_PATH="${tmp}" XV6_PULSE_REDUCER_TEST_MODE=1 \
    XV6_PULSE_REDUCER_FAKE_REQUEST_BYTES=2048 \
    XV6_PULSE_REDUCER_FAKE_BEGIN_BYTES=2048 \
    "${tmp}/reducer" --duration-seconds=1 >"${tmp}/reducer-2048.log"
awk '
    /phase=write / && /requested_bytes=2048/ && /chunk_bytes=2048/ && /chunk_frames=256/ { short++ }
    /phase=operation_return name=start_uncork .*pointer=nonnull/ { start++ }
    /phase=operation_return name=stop_flush .*pointer=nonnull/ { stop_flush++ }
    /phase=operation_return name=stop_cork .*pointer=nonnull/ { stop_cork++ }
    /phase=operation_return name=reset_flush .*pointer=nonnull/ { reset_flush++ }
    /phase=result verdict=PASS class=healthy/ { pass++ }
    END { exit !(short >= 1 && start == 1 && stop_flush == 1 &&
                 stop_cork == 1 && reset_flush == 1 && pass == 1) }
' "${tmp}/reducer-2048.log"

timeout --signal=TERM --kill-after=2 15 \
    env LD_LIBRARY_PATH="${tmp}" XV6_PULSE_REDUCER_TEST_MODE=1 \
    XV6_PULSE_REDUCER_FAKE_REQUEST_BYTES=2048 \
    XV6_PULSE_REDUCER_FAKE_BEGIN_BYTES=1024 \
    "${tmp}/reducer" --duration-seconds=1 >"${tmp}/reducer-short.log"
awk '
    /phase=write / && /requested_bytes=2048/ && /chunk_bytes=1024/ && /chunk_frames=128/ { short++ }
    /phase=result verdict=PASS class=healthy/ { pass++ }
    END { exit !(short >= 2 && pass == 1) }
' "${tmp}/reducer-short.log"

set +e
timeout --signal=TERM --kill-after=2 15 \
    env LD_LIBRARY_PATH="${tmp}" XV6_PULSE_REDUCER_TEST_MODE=1 \
    XV6_PULSE_REDUCER_FAKE_BEGIN_BYTES=1025 \
    "${tmp}/reducer" --duration-seconds=1 >"${tmp}/reducer-unaligned.log"
unaligned_rc=$?
timeout --signal=TERM --kill-after=2 15 \
    env LD_LIBRARY_PATH="${tmp}" XV6_PULSE_REDUCER_TEST_MODE=1 \
    XV6_PULSE_TRACE_FAKE_BEGIN_FAIL=1 \
    "${tmp}/reducer" --duration-seconds=1 >"${tmp}/reducer-begin-fail.log"
begin_fail_rc=$?
timeout --signal=TERM --kill-after=2 12 \
    env LD_LIBRARY_PATH="${tmp}" XV6_PULSE_REDUCER_TEST_MODE=1 \
    XV6_PULSE_REDUCER_FAKE_LATE_CALLBACK=1 \
    "${tmp}/reducer" --duration-seconds=1 >"${tmp}/reducer-late-callback.log"
late_callback_rc=$?
set -e
[[ "${unaligned_rc}" == 21 && "${begin_fail_rc}" == 21 &&
   "${late_callback_rc}" == 21 ]]
awk '
    /phase=begin_write .*returned_bytes=1025 alignment=8 .*cancel_ret=0 .*verdict=FAIL/ { rejected++ }
    /phase=write .*chunk_bytes=1025/ { bad_write++ }
    END { exit !(rejected == 1 && bad_write == 0) }
' "${tmp}/reducer-unaligned.log"
awk '
    /phase=begin_write .*ret=-1 .*pointer=unchanged-poison .*verdict=FAIL/ { rejected++ }
    END { exit !(rejected == 1) }
' "${tmp}/reducer-begin-fail.log"
awk '
    /phase=operation_lifetime name=start_uncork action=defer_until_mainloop_stop operation_state=0 callback_seen=0/ { deferred++ }
    /phase=threaded_mainloop_stop/ { stop=NR }
    /phase=operation_deferred_release name=start_uncork callback_seen=1 success=1 mainloop_stopped=1/ {
        released++
        if (stop > 0 && NR > stop) ordered++
    }
    /phase=stream_unref .*deferred_safe=1/ { stream_safe++ }
    /phase=context_unref .*deferred_safe=1/ { context_safe++ }
    END { exit !(deferred == 1 && released == 1 && ordered == 1 &&
                 stream_safe == 1 && context_safe == 1) }
' "${tmp}/reducer-late-callback.log"
env AUDIO_REDUCER_LOG="${tmp}/reducer-late-callback.log" \
    AUDIO_REDUCER_PARSER="${repo}/scripts/gpu/chromium-audio-localizer-parser.tcl" \
    expect -c '
        source $env(AUDIO_REDUCER_PARSER)
        set parsed [audio_reducer_contract [audio_read_text $env(AUDIO_REDUCER_LOG)]]
        if {![dict get $parsed valid] ||
            [dict get $parsed deferred_operations] != 1 ||
            [dict get $parsed result_class] ne "primary_playback"} {
            puts stderr "late reducer parser rejected: $parsed"
            exit 1
        }
    '

set +e
"${tmp}/reducer" --duration-seconds=19 >"${tmp}/invalid-duration.log" 2>&1
invalid_duration_rc=$?
set -e
[[ "${invalid_duration_rc}" == 2 ]]

"${tmp}/reducer" --run-bounded 2 true -- /bin/true \
    >"${tmp}/bounded-pass.log"
set +e
"${tmp}/reducer" --run-bounded 1 sleep -- /bin/sleep 30 \
    >"${tmp}/bounded-timeout.log"
bounded_timeout_rc=$?
set -e
[[ "${bounded_timeout_rc}" == 124 ]]

"${tmp}/reducer" --pgroup-leader sleep "${owned_record}" -- /bin/sleep 30 \
    >"${tmp}/owner.log" 2>&1 &
owned_pid=$!
sleep 0.1
"${tmp}/reducer" --pgroup-identity "${owned_record}" \
    >>"${tmp}/owner.log" 2>&1
sed -E 's/starttime=[0-9]+/starttime=1/' "${owned_record}" \
    >"${tmp}/owner-wrong-starttime.record"
set +e
"${tmp}/reducer" --pgroup-terminate "${tmp}/owner-wrong-starttime.record" \
    >>"${tmp}/owner.log" 2>&1
wrong_starttime_rc=$?
set -e
[[ "${wrong_starttime_rc}" == 71 ]]
"${tmp}/reducer" --pgroup-identity "${owned_record}" \
    >>"${tmp}/owner.log" 2>&1
"${tmp}/reducer" --pgroup-terminate "${owned_record}" \
    >>"${tmp}/owner.log" 2>&1
set +e
wait "${owned_pid}"
owned_wait_rc=$?
set -e
owned_pid=0
[[ "${owned_wait_rc}" == 143 || "${owned_wait_rc}" == 137 ]]

timeout --signal=TERM --kill-after=2 30 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-baseline.stdout" 2>"${tmp}/trace-baseline.stderr"
timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-off.log" \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-off.stdout" 2>"${tmp}/trace-off.stderr"
[[ ! -e "${tmp}/trace-off.log" ]]
cmp "${tmp}/trace-baseline.stdout" "${tmp}/trace-off.stdout"
cmp "${tmp}/trace-baseline.stderr" "${tmp}/trace-off.stderr"

timeout --signal=TERM --kill-after=2 30 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    "${tmp}/provider-b/libpulse.so.0" \
    >"${tmp}/two-provider-baseline.stdout"
timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/two-provider.log" \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    "${tmp}/provider-b/libpulse.so.0" \
    >"${tmp}/two-provider.stdout"
cmp "${tmp}/two-provider-baseline.stdout" "${tmp}/two-provider.stdout"
awk '
    /event=provider / { providers++ }
    /event=summary .*binding_count=3 binding_mask=0x45/ { summary++ }
    END { exit !(providers == 2 && summary == 1) }
' "${tmp}/two-provider.log"

timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-on.log" \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-on.stdout" 2>"${tmp}/trace-on.stderr"
cmp "${tmp}/trace-baseline.stdout" "${tmp}/trace-on.stdout"
cmp "${tmp}/trace-baseline.stderr" "${tmp}/trace-on.stderr"

fake_build_id="$(readelf -n "${tmp}/libpulse.so.0" |
    awk '/Build ID:/ { print $3; exit }')"
[[ "${fake_build_id}" =~ ^[0-9a-f]{16,128}$ ]]
readelf -Ws "${tmp}/libpulse.so.0" |
    awk '$8 == "pa_context_new@@FAKE_1.0" { found=1 } END { exit !found }'
awk '
    BEGIN { provider=0; context=0; context_notify=0; context_set=0; context_clear=0; stream=0; connect=0; begin_count=0; write4096=0; callbacks=0; operations=0; close_count=0; summary=0; bad=0 }
    !/^CHROMIUM_PULSE_TRACE_V2 schema=2 pid=[0-9]+ tid=[0-9]+ seq=[0-9]+ mono_ns=[0-9]+ event=/ { bad++ }
    /event=provider .*soname=libpulse.so.0/ { provider++ }
    /event=context_new context_id=1 name=Chromium ret=nonnull/ { context++ }
    /event=context_notify context_id=1/ { context_notify++ }
    /event=set_context_state_callback context_id=1 value=set/ { context_set++ }
    /event=set_context_state_callback context_id=1 value=clear/ { context_clear++ }
    /event=stream_new context_id=1 stream_id=1 variant=with_proplist name=Playback .*format=5 rate=48000 channels=2 map_channels=2 map0=1 map1=2 ret=nonnull/ { stream++ }
    /event=stream_connect context_id=1 stream_id=1 device=default-null flags=0x200f maxlength=4294967295 tlength=12288 prebuf=4294967295 minreq=2048 fragsize=4294967295 ret=0/ { connect++ }
    /event=begin_write .*ret=0 buffer=nonnull bytes=[1-9][0-9]+/ { begin_count++ }
    /event=stream_write .*bytes=4096 frames_f32le_stereo=512 .*ret=0/ { write4096++ }
    /event=(stream_notify|request_callback|success_callback|free_callback)/ { callbacks++ }
    /event=operation_return .*action=(Start|StopFlush|StopCork|ResetFlush) .*ret=nonnull/ { operations++ }
    /event=context_unref context_id=1/ { close_count++ }
    /event=summary binding_count=21 binding_mask=0x1fffff effective_attr_observed=0 effective_attr_reason=symbol_not_requested .*overflow=0/ { summary++ }
    END {
        if (provider != 1 || context != 1 || context_notify != 1 ||
            context_set != 1 || context_clear != 1 || stream != 1 || connect != 1 ||
            begin_count < 1 || write4096 < 1 || callbacks < 7 || operations != 4 ||
            close_count != 1 || summary != 1 || bad != 0)
            exit 1
    }
' "${tmp}/trace-on.log"
awk -v expected="${fake_build_id}" '
    /event=provider/ {
        for (i=1; i<=NF; i++) if ($i == "build_id=" expected) found=1
    }
    END { exit !found }
' "${tmp}/trace-on.log"

timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-optional.log" \
    XV6_PULSE_TRACE_TEST_GET_ATTR=1 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-optional.stdout"
awk '
    /event=effective_attr .*observed=1 ret=nonnull/ { attr++ }
    /event=summary .*effective_attr_observed=1 effective_attr_reason=symbol_requested/ { summary++ }
    END { exit !(attr == 1 && summary == 1) }
' "${tmp}/trace-optional.log"

timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-poison.log" \
    XV6_PULSE_TRACE_FAKE_BEGIN_FAIL=1 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-poison.stdout"
awk '
    /event=begin_write .*ret=-1 buffer=unavailable bytes=unavailable/ { failed++ }
    /event=stream_write/ { writes++ }
    END { exit !(failed == 1 && writes == 0) }
' "${tmp}/trace-poison.log"

timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-hot.log" \
    XV6_PULSE_TRACE_TEST_HOT=1 XV6_PULSE_TRACE_FAKE_LATE_NULL=1 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-hot.stdout"
awk '
    /event=operation_return .*action=ResetFlush .*ret=null .*failure_class=teardown/ { late++ }
    /event=summary/ {
        seen=0; suppressed=0; failures=0
        for (i=1; i<=NF; i++) {
            if ($i ~ /^hot_seen=/) { split($i,a,"="); seen=a[2]+0 }
            if ($i ~ /^hot_suppressed=/) { split($i,a,"="); suppressed=a[2]+0 }
            if ($i ~ /^failure_rows=/) { split($i,a,"="); failures=a[2]+0 }
        }
        if (seen >= 100000 && suppressed > 0 && failures >= 1) summary++
    }
    END { exit !(late == 1 && summary == 1) }
' "${tmp}/trace-hot.log"

timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-threads.log" \
    XV6_PULSE_TRACE_TEST_THREADS=1 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-threads.stdout"
awk '
    /event=summary .*overflow=0/ { summary++ }
    END { exit !(summary == 1) }
' "${tmp}/trace-threads.log"

timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-fork.log" \
    XV6_PULSE_TRACE_TEST_FORK=1 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-fork.stdout"
awk '
    /event=context_new/ {
        pid=""; name=""
        for (i=1; i<=NF; i++) {
            if ($i ~ /^pid=/) { split($i,a,"="); pid=a[2] }
            if ($i ~ /^name=/) { split($i,a,"="); name=a[2] }
        }
        if (name == "ForkChild") child[pid]=1
        if (name == "Chromium") parent[pid]=1
    }
    END {
        c=0; p=0
        for (pid in child) c++
        for (pid in parent) p++
        exit !(c == 1 && p == 1)
    }
' "${tmp}/trace-fork.log"

timeout --signal=TERM --kill-after=2 30 \
    env LD_PRELOAD="${tmp}/trace.so" XV6_PULSE_TRACE=1 \
    XV6_PULSE_TRACE_LOG="${tmp}/trace-reuse.log" \
    XV6_PULSE_TRACE_TEST_REUSE=1 \
    "${tmp}/trace-reducer" "${tmp}/libpulse.so.0" \
    >"${tmp}/trace-reuse.stdout"
awk '
    /event=summary/ {
        for (i=1; i<=NF; i++) if ($i ~ /^pointer_reuse=/) {
            split($i,a,"="); if (a[2]+0 >= 7) pass=1
        }
    }
    END { exit !pass }
' "${tmp}/trace-reuse.log"

# Guard the live guest script, not the synthetic parser fixtures that
# intentionally contain rejected strings.
if /home/es/.local/bin/rg -n -S \
    '(^|[[:space:]])(pkill|pgrep|killall)([[:space:]]|$)|ls[[:space:]]+-l|grep[[:space:]]+(-|--)|--disable-audio-output' \
    scripts/gpu/chromium-audio-localizer-guest.sh; then
    echo "AUDIO-LOCALIZER-STATIC-FAIL forbidden-guest-command" >&2
    exit 1
fi

echo "AUDIO-LOCALIZER-NOBOOT-PASS c_werror=PASS bash_syntax=PASS classifier=PASS fixture=PASS lifecycle_min=20 timeout=PASS owned_identity=PASS owned_reap=PASS reducer_returned_2048=PASS reducer_returned_1024=PASS reducer_alignment_reject=PASS reducer_begin_poison=PASS reducer_late_callback_deferred=PASS trace_provider_slots=4 trace_two_provider_no_retarget=PASS trace_default_off_transcript=PASS trace_rtld_local_versioned=PASS trace_dlerror=PASS trace_untargeted=PASS trace_all_bindings=PASS trace_provider_build_id=PASS trace_context_notify_set_clear=PASS trace_callbacks=PASS trace_errno=PASS trace_poison=PASS trace_optional_attr=PASS trace_hot_100k_late_failure=PASS trace_threads_64=PASS trace_fork_lock=PASS trace_pointer_reuse=PASS guest_syntax=PASS no_audio_disable_flag=PASS"
