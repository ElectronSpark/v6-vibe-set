#!/usr/bin/env tclsh

set here [file dirname [file normalize [info script]]]
source "$here/chromium-audio-localizer-parser.tcl"

proc static_fail {message} {
    puts stderr "AUDIO-LOCALIZER-STATIC-FAIL $message"
    exit 1
}

proc replace_once {text old new} {
    set first [string first $old $text]
    if {$first < 0 || [string first $old $text [expr {$first + 1}]] >= 0} {
        error "replace_once count is not one for $old"
    }
    return [string replace $text $first [expr {$first + [string length $old] - 1}] $new]
}

proc expect_class {name artifacts expected_valid expected_class} {
    set parsed [audio_localizer_classify $artifacts]
    if {[dict get $parsed valid] != $expected_valid ||
        [dict get $parsed class] ne $expected_class ||
        ![dict exists $parsed no_youtube_claim] ||
        [dict get $parsed no_youtube_claim] != 1} {
        static_fail "$name parsed=$parsed expected_valid=$expected_valid expected_class=$expected_class"
    }
}

set manifest [join {
    {AUDIO_LOCALIZER_STEP schema=1 name=arm_contract localizer=1 qemu_audio=none audio_disable=0 no_youtube_claim=1}
    {AUDIO_LOCALIZER_STEP schema=1 name=reducer_identity status=present path=/bin/chromium-pulse-stream-reducer}
    {AUDIO_LOCALIZER_STEP schema=1 name=trace_identity status=present path=/opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so}
    {AUDIO_LOCALIZER_STEP schema=1 name=primary_socket rc=0 path=/dev/shm/xdg-runtime-root/pulse/native}
    {AUDIO_LOCALIZER_STEP schema=1 name=fixture rc=0}
    {AUDIO_LOCALIZER_STEP schema=1 name=pw_play_primary rc=0 timeout_seconds=12}
    {AUDIO_LOCALIZER_STEP schema=1 name=paplay_primary rc=0 timeout_seconds=12}
    {AUDIO_LOCALIZER_STEP schema=1 name=reducer_primary rc=0 timeout_seconds=35}
    {AUDIO_LOCALIZER_STEP schema=1 name=server_selection selected_server=unix:/dev/shm/xdg-runtime-root/pulse/native selected_sink=xv6_null_output fallback_ready=0 paplay_primary_rc=0 reducer_primary_rc=0 paplay_fallback_rc=NA reducer_fallback_rc=NA}
    {AUDIO_LOCALIZER_STEP schema=1 name=chromium_start pid=700 selected_server=unix:/dev/shm/xdg-runtime-root/pulse/native selected_sink=xv6_null_output}
    {AUDIO_LOCALIZER_STEP schema=1 name=chromium_identity pid=700 rc=0 expected=chrome}
    {AUDIO_LOCALIZER_STEP schema=1 name=chromium_owned_cleanup pid=700 terminate_rc=0 wait_rc=143}
    {AUDIO_LOCALIZER_HELPER_RESULT schema=1 status=COMPLETE primary_socket_rc=0 fixture_rc=0 pw_play_primary_rc=0 paplay_primary_rc=0 reducer_primary_rc=0 fallback_ready=0 paplay_fallback_rc=NA reducer_fallback_rc=NA chromium_identity_rc=0 no_youtube_claim=1}
} "\n"]

set socket_evidence [join {
    {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xdg-runtime-root/pulse/native lstat_ret=0 errno=0 type=socket mode=777}
    {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xdg-runtime-root/pulse/native connect_ret=0 errno=0 verdict=PASS}
} "\n"]

set snapshots [join {
    {AUDIO_LOCALIZER_SNAPSHOT_BEGIN schema=1 tag=primary_pre server=unix:/dev/shm/xdg-runtime-root/pulse/native}
    {Server Name: PulseAudio (on PipeWire 0.3.65)}
    {Default Sink: xv6_null_output}
    {42 xv6_null_output PipeWire float32le 2ch 48000Hz RUNNING}
    {AUDIO_LOCALIZER_SNAPSHOT_END schema=1 tag=primary_pre server=unix:/dev/shm/xdg-runtime-root/pulse/native}
    {AUDIO_LOCALIZER_FINAL_PROCESS_BEGIN schema=1}
    {PID PPID STAT COMMAND}
    {1 0 S init}
    {AUDIO_LOCALIZER_FINAL_PROCESS_END schema=1}
} "\n"]

set reducer [join {
    {AUDIO_PULSE_REDUCER_V1 phase=start schema=1 duration_seconds=20}
    {AUDIO_PULSE_REDUCER_V1 phase=contract mainloop=pa_threaded_mainloop stream_constructor=pa_stream_new_with_proplist stream_name=Playback proplist=empty sample_format=PA_SAMPLE_FLOAT32LE sample_format_value=5 rate=48000 channels=2 channel_map=front-left,front-right device=null context_flags=PA_CONTEXT_NOAUTOSPAWN context_flags_value=0x1 frames_per_write=512 bytes_per_write=4096 write_path=pa_stream_begin_write,pa_stream_write}
    {AUDIO_PULSE_REDUCER_V1 phase=requested_attr maxlength=4294967295 minreq=2048 prebuf=4294967295 tlength=12288 fragsize=4294967295}
    {AUDIO_PULSE_REDUCER_V1 phase=stream_flags names=START_CORKED,INTERPOLATE_TIMING,NOT_MONOTONIC,AUTO_TIMING_UPDATE,ADJUST_LATENCY value=0x200f}
    {AUDIO_PULSE_REDUCER_V1 phase=context_state state=READY state_value=4 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=stream_state state=READY state_value=2 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=effective_attr observed=1 result=nonnull maxlength=4194304 minreq=4096 prebuf=8192 tlength=12288 fragsize=4294967295 errno=0 context_errno=0 required_for_health=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_return name=start_uncork pointer=nonnull errno=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation name=start_uncork result=nonnull operation_state=1 timed_out=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_callback name=start_uncork seen=1 success=1}
    {AUDIO_PULSE_REDUCER_V1 phase=write callback=1 requested_bytes=12288 begin_ret=0 begin_errno=0 begin_bytes=4096 pointer=nonnull chunk_bytes=4096 chunk_frames=512 write_index=0 ret=0 errno=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=lifecycle elapsed_ms=20025 required_ms=20000 callbacks=235 writes=237 frames=121344 requested_total=970752 request_min=4096 request_max=12288 short_callbacks=0 stream_state=READY context_state=READY}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_return name=stop_flush pointer=nonnull errno=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation name=stop_flush result=nonnull operation_state=1 timed_out=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_callback name=stop_flush seen=1 success=1}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_return name=stop_cork pointer=nonnull errno=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation name=stop_cork result=nonnull operation_state=1 timed_out=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_callback name=stop_cork seen=1 success=1}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_return name=reset_flush pointer=nonnull errno=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation name=reset_flush result=nonnull operation_state=1 timed_out=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=operation_callback name=reset_flush seen=1 success=1}
    {AUDIO_PULSE_REDUCER_V1 phase=close_clear_callback kind=write ret=void errno_preserved=1}
    {AUDIO_PULSE_REDUCER_V1 phase=close_clear_callback kind=state ret=void errno_preserved=1}
    {AUDIO_PULSE_REDUCER_V1 phase=stream_disconnect ret=0 errno=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=stream_unref ret=void}
    {AUDIO_PULSE_REDUCER_V1 phase=context_disconnect ret=void errno=0 context_errno=0}
    {AUDIO_PULSE_REDUCER_V1 phase=context_unref ret=void}
    {AUDIO_PULSE_REDUCER_V1 phase=threaded_mainloop_stop ret=void}
    {AUDIO_PULSE_REDUCER_V1 phase=threaded_mainloop_free ret=void}
    {AUDIO_PULSE_REDUCER_V1 phase=result verdict=PASS class=healthy reason=complete lifecycle_ms=20025 teardown_failures=0 teardown_first_failure=none callbacks=235 writes=237 frames=121344}
} "\n"]

set trace [join {
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=1 mono_ns=1000 event=provider provider_id=1 handle_id=1 path=/opt/host-gui/wayland-chromium/chrome-linux64/libpulse.so.0 soname=libpulse.so.0 build_id=5a42eddd12bc34f26064eb65a146075cbea16a8c errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=2 mono_ns=1010 event=context_new context_id=1 name=Chromium ret=nonnull errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=3 mono_ns=1020 event=context_notify context_id=1 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=4 mono_ns=1030 event=set_context_state_callback context_id=1 value=set errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=5 mono_ns=1040 event=context_connect context_id=1 server=default-null flags=0x1 ret=0 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=6 mono_ns=1050 event=context_state context_id=1 state=4 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=7 mono_ns=1060 event=context_errno context_id=1 value=0 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=8 mono_ns=1070 event=stream_new context_id=1 stream_id=1 variant=with_proplist name=Playback proplist=opaque format=5 rate=48000 channels=2 map_channels=2 map0=1 map1=2 ret=nonnull errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=9 mono_ns=1080 event=stream_notify stream_id=1 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=10 mono_ns=1090 event=set_state_callback stream_id=1 value=set errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=11 mono_ns=1100 event=request_callback stream_id=1 bytes=4096 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=12 mono_ns=1110 event=set_write_callback stream_id=1 value=set errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=13 mono_ns=1120 event=stream_connect context_id=1 stream_id=1 device=default-null flags=0x200f maxlength=4294967295 tlength=12288 prebuf=4294967295 minreq=2048 fragsize=4294967295 ret=0 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=14 mono_ns=1130 event=stream_state context_id=1 stream_id=1 state=2 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=15 mono_ns=1140 event=operation_return context_id=1 stream_id=1 operation_id=1 action=Start cork=0 ret=nonnull failure_class=none errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=16 mono_ns=1150 event=success_callback stream_id=1 action=Start success=1 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=17 mono_ns=1160 event=operation_state stream_id=1 operation_id=1 action=Start state=1 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=18 mono_ns=1170 event=operation_unref stream_id=1 operation_id=1 action=Start ret=void errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=19 mono_ns=1180 event=begin_write context_id=1 stream_id=1 ret=0 buffer=nonnull bytes=4096 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=20 mono_ns=1190 event=stream_write context_id=1 stream_id=1 bytes=4096 frames_f32le_stereo=512 free_callback=0 offset=0 seek=0 ret=0 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=21 mono_ns=1200 event=operation_return context_id=1 stream_id=1 operation_id=2 action=StopFlush cork=-1 ret=nonnull failure_class=none errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=22 mono_ns=1210 event=success_callback stream_id=1 action=StopFlush success=1 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=23 mono_ns=1220 event=operation_unref stream_id=1 operation_id=2 action=StopFlush ret=void errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=24 mono_ns=1230 event=operation_return context_id=1 stream_id=1 operation_id=3 action=StopCork cork=1 ret=nonnull failure_class=none errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=25 mono_ns=1240 event=success_callback stream_id=1 action=StopCork success=1 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=26 mono_ns=1250 event=operation_unref stream_id=1 operation_id=3 action=StopCork ret=void errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=27 mono_ns=1260 event=operation_return context_id=1 stream_id=1 operation_id=4 action=ResetFlush cork=-1 ret=nonnull failure_class=none errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=28 mono_ns=1270 event=success_callback stream_id=1 action=ResetFlush success=1 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=29 mono_ns=1280 event=operation_unref stream_id=1 operation_id=4 action=ResetFlush ret=void errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=30 mono_ns=1290 event=set_write_callback stream_id=1 value=clear errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=31 mono_ns=1300 event=set_state_callback stream_id=1 value=clear errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=32 mono_ns=1310 event=stream_disconnect context_id=1 stream_id=1 ret=0 errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=33 mono_ns=1320 event=stream_unref context_id=1 stream_id=1 ret=void errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=34 mono_ns=1330 event=set_context_state_callback context_id=1 value=clear errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=35 mono_ns=1340 event=context_disconnect context_id=1 ret=void errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=36 mono_ns=1350 event=context_unref context_id=1 ret=void errno_before=0 errno_after=0}
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=37 mono_ns=1360 event=summary binding_count=21 binding_mask=0x1fffff effective_attr_observed=0 effective_attr_reason=symbol_not_requested pointer_reuse=3 overflow=0 hot_seen=5 hot_suppressed=0 failure_rows=0 fork_resets=0 errno_before=0 errno_after=0}
} "\n"]

set launcher [join {
    {wayland-chromium-launcher: starting argc=2}
    {wayland-chromium-launcher: env LD_PRELOAD="/opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so"}
    {wayland-chromium-launcher: argv_0="/opt/host-gui/wayland-chromium/chrome-linux64/chrome"}
    {wayland-chromium-launcher: argv_1="--ozone-platform=wayland"}
    {wayland-chromium-launcher: argv_17="--enable-logging=stderr"}
    {wayland-chromium-launcher: argv_18="file:///share/chromium-audio-smoke.html"}
    {wayland-chromium-launcher: child_exec path=/opt/host-gui/wayland-chromium/chrome-linux64/chrome}
    {[700:700:INFO:CONSOLE(15)] "CHROMIUM-AUDIO-SMOKE playing state=running"}
    {[700:700:INFO:CONSOLE(22)] "CHROMIUM-AUDIO-SMOKE tick=21 currentTime=21.123 state=running"}
} "\n"]

set owner [join {
    {AUDIO_LOCALIZER_OWNER schema=1 phase=identity pid=700 pgid=700 starttime=12345 recorded_starttime=12345 expected=chrome exe=/opt/host-gui/wayland-chromium/chrome-linux64/chrome record=/audio-localizer/chromium.owner verdict=PASS errno=0}
    {AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_identity pid=700 pgid=700 starttime=12345 recorded_starttime=12345 expected=chrome exe=/opt/host-gui/wayland-chromium/chrome-linux64/chrome record=/audio-localizer/chromium.owner verdict=PASS errno=0}
    {AUDIO_LOCALIZER_OWNER schema=1 phase=signal signal=TERM pid=700 pgid=700 starttime=12345 ret=0 errno=0}
    {AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_result pid=700 pgid=700 starttime=12345 live=0 verdict=PASS}
} "\n"]
set owner_record {schema=1 pid=700 pgid=700 starttime=12345 expected=chrome}
set identity_sha [string repeat a 64]
set identity_build_id 5a42eddd12bc34f26064eb65a146075cbea16a8c
set provider_identity [join [list \
    "identity schema=1 name=reducer_provider guest=/opt/provider image=/tmp/provider image_sha=$identity_sha image_build_id=$identity_build_id expected_count=2 expected=/tmp/provider-build:$identity_sha:$identity_build_id,/tmp/provider-stage:$identity_sha:$identity_build_id status=PASS" \
    "identity schema=1 name=reducer_launcher guest=/bin/reducer image=/tmp/launcher image_sha=$identity_sha image_build_id=$identity_build_id expected_count=1 expected=/tmp/stage:$identity_sha:$identity_build_id status=PASS" \
    "identity schema=1 name=pulse_trace guest=/opt/trace image=/tmp/trace image_sha=$identity_sha image_build_id=$identity_build_id expected_count=2 expected=/tmp/trace-build:$identity_sha:$identity_build_id,/tmp/trace-stage:$identity_sha:$identity_build_id status=PASS" \
    "identity schema=1 name=pulse_provider guest=/opt/libpulse.so.0 image=/tmp/libpulse.so.0 image_sha=$identity_sha image_build_id=$identity_build_id expected_count=1 expected=/tmp/provider-lib:$identity_sha:$identity_build_id status=PASS" \
    "identity schema=1 name=audio_smoke guest=/share/smoke image=/tmp/smoke image_sha=$identity_sha image_build_id=not-applicable expected_count=1 expected=/tmp/source:$identity_sha:not-applicable status=PASS" \
    "identity schema=1 name=guest_helper guest=/helper image=/tmp/helper image_sha=$identity_sha image_build_id=not-applicable expected_count=1 expected=/tmp/source:$identity_sha:not-applicable status=PASS"] "\n"]

set base [dict create manifest $manifest snapshots $snapshots \
    primary_socket $socket_evidence reducer_primary $reducer \
    launcher $launcher trace $trace chromium_owner $owner \
    chromium_owner_record $owner_record \
    provider_identity $provider_identity \
    pipewire_quantum {        default.clock.min-quantum = 1024} \
    pulse_quantum {        pulse.min.quantum = 1024/48000} \
    evidence "$manifest\n$snapshots\n$socket_evidence\n$reducer\n$launcher\n$trace\n$owner"]

expect_class healthy $base 1 HEALTHY

set substrate $base
set substrate_manifest [replace_once $manifest \
    {name=pw_play_primary rc=0} {name=pw_play_primary rc=124}]
set substrate_manifest [replace_once $substrate_manifest \
    {pw_play_primary_rc=0 paplay_primary_rc=0} \
    {pw_play_primary_rc=124 paplay_primary_rc=0}]
dict set substrate manifest $substrate_manifest
dict set substrate evidence "$substrate_manifest\n$snapshots\n$launcher\n$trace"
expect_class substrate-timeout $substrate 1 SUBSTRATE

set server $base
set server_manifest [replace_once $manifest \
    {name=paplay_primary rc=0} {name=paplay_primary rc=1}]
set server_manifest [replace_once $server_manifest \
    {fallback_ready=0 paplay_primary_rc=0 reducer_primary_rc=0 paplay_fallback_rc=NA reducer_fallback_rc=NA} \
    {fallback_ready=1 paplay_primary_rc=1 reducer_primary_rc=0 paplay_fallback_rc=0 reducer_fallback_rc=0}]
set server_manifest [replace_once $server_manifest \
    {primary_socket_rc=0 fixture_rc=0 pw_play_primary_rc=0 paplay_primary_rc=0 reducer_primary_rc=0 fallback_ready=0 paplay_fallback_rc=NA reducer_fallback_rc=NA chromium_identity_rc=0} \
    {primary_socket_rc=0 fixture_rc=0 pw_play_primary_rc=0 paplay_primary_rc=1 reducer_primary_rc=0 fallback_ready=1 paplay_fallback_rc=0 reducer_fallback_rc=0 chromium_identity_rc=0}]
set server_manifest [replace_once $server_manifest \
    {AUDIO_LOCALIZER_HELPER_RESULT schema=1 status=COMPLETE} \
    {AUDIO_LOCALIZER_STEP schema=1 name=fallback_start pid=800 socket=/dev/shm/xv6-audio-localizer-pulse/native
AUDIO_LOCALIZER_STEP schema=1 name=fallback_ready identity_rc=0 socket_rc=0 pid=800
AUDIO_LOCALIZER_STEP schema=1 name=fallback_owned_cleanup pid=800 terminate_rc=0 wait_rc=143
AUDIO_LOCALIZER_HELPER_RESULT schema=1 status=COMPLETE}]
dict set server manifest $server_manifest
dict set server reducer_fallback $reducer
dict set server fallback_socket [join {
    {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xv6-audio-localizer-pulse/native lstat_ret=0 errno=0 type=socket mode=777}
    {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xv6-audio-localizer-pulse/native connect_ret=0 errno=0 verdict=PASS}
} "\n"]
dict set server fallback_owner [join {
    {AUDIO_LOCALIZER_OWNER schema=1 phase=identity pid=800 pgid=800 starttime=22345 recorded_starttime=22345 expected=pulseaudio exe=/usr/bin/pulseaudio record=/audio-localizer/fallback.owner verdict=PASS errno=0}
    {AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_identity pid=800 pgid=800 starttime=22345 recorded_starttime=22345 expected=pulseaudio exe=/usr/bin/pulseaudio record=/audio-localizer/fallback.owner verdict=PASS errno=0}
    {AUDIO_LOCALIZER_OWNER schema=1 phase=signal signal=TERM pid=800 pgid=800 starttime=22345 ret=0 errno=0}
    {AUDIO_LOCALIZER_OWNER schema=1 phase=terminate_result pid=800 pgid=800 starttime=22345 live=0 verdict=PASS}
} "\n"]
dict set server fallback_owner_record {schema=1 pid=800 pgid=800 starttime=22345 expected=pulseaudio}
dict set server evidence "$server_manifest\n$snapshots\n$launcher\n$trace"
expect_class server-fallback $server 1 SERVER

set fallback_owner_bad $server
dict unset fallback_owner_bad fallback_owner
expect_class fallback-owner-missing $fallback_owner_bad 0 INVALID

set negotiation $base
set negotiation_manifest [replace_once $manifest \
    {name=reducer_primary rc=0} {name=reducer_primary rc=124}]
set negotiation_manifest [replace_once $negotiation_manifest \
    {fallback_ready=0 paplay_primary_rc=0 reducer_primary_rc=0 paplay_fallback_rc=NA reducer_fallback_rc=NA} \
    {fallback_ready=0 paplay_primary_rc=0 reducer_primary_rc=124 paplay_fallback_rc=NA reducer_fallback_rc=NA}]
set negotiation_manifest [replace_once $negotiation_manifest \
    {pw_play_primary_rc=0 paplay_primary_rc=0 reducer_primary_rc=0 fallback_ready=0} \
    {pw_play_primary_rc=0 paplay_primary_rc=0 reducer_primary_rc=124 fallback_ready=0}]
dict set negotiation manifest $negotiation_manifest
set failed_reducer [replace_once $reducer \
    {phase=result verdict=PASS class=healthy reason=complete lifecycle_ms=20025 teardown_failures=0 teardown_first_failure=none} \
    {phase=result verdict=FAIL class=primary_playback reason=stream-terminal lifecycle_ms=20025 teardown_failures=0 teardown_first_failure=none}]
set failed_reducer [replace_once $failed_reducer \
    {stream_state=READY context_state=READY} \
    {stream_state=FAILED context_state=READY}]
dict set negotiation reducer_primary $failed_reducer
dict set negotiation evidence "$negotiation_manifest\n$snapshots\n$failed_reducer\n$launcher\n$trace"
expect_class negotiation-timeout $negotiation 1 NEGOTIATION

set teardown_reducer [replace_once $reducer \
    {phase=operation_return name=stop_flush pointer=nonnull errno=0 context_errno=0} \
    {phase=operation_return name=stop_flush pointer=null errno=32 context_errno=15}]
set teardown_reducer [replace_once $teardown_reducer \
    {phase=operation name=stop_flush result=nonnull operation_state=1 timed_out=0 context_errno=0} \
    {phase=operation name=stop_flush result=null context_errno=15 context_error=failed verdict=FAIL}]
set teardown_reducer [replace_once $teardown_reducer \
    {phase=operation_callback name=stop_flush seen=1 success=1} \
    {phase=operation_callback name=stop_flush seen=0 success=0}]
set teardown_reducer [replace_once $teardown_reducer \
    {phase=result verdict=PASS class=healthy reason=complete lifecycle_ms=20025 teardown_failures=0 teardown_first_failure=none} \
    {phase=result verdict=FAIL class=teardown reason=stop_flush lifecycle_ms=20025 teardown_failures=1 teardown_first_failure=stop_flush}]
set teardown_case $base
dict set teardown_case reducer_primary $teardown_reducer
dict set teardown_case evidence "$manifest\n$snapshots\n$teardown_reducer\n$launcher\n$trace"
expect_class reducer-teardown-separated $teardown_case 1 NEGOTIATION

regsub -line {^AUDIO_PULSE_REDUCER_V1 phase=effective_attr[^\n]*\n?} \
    $reducer {} reducer_effective_absent
set reducer_effective_case $base
dict set reducer_effective_case reducer_primary $reducer_effective_absent
dict set reducer_effective_case evidence "$manifest\n$snapshots\n$reducer_effective_absent\n$launcher\n$trace"
expect_class reducer-effective-absent $reducer_effective_case 1 HEALTHY

set reducer_2048 [replace_once $reducer \
    {begin_bytes=4096 pointer=nonnull chunk_bytes=4096 chunk_frames=512} \
    {begin_bytes=2048 pointer=nonnull chunk_bytes=2048 chunk_frames=256}]
set reducer_2048_case $base
dict set reducer_2048_case reducer_primary $reducer_2048
dict set reducer_2048_case evidence "$manifest\n$snapshots\n$reducer_2048\n$launcher\n$trace"
expect_class reducer-aligned-2048 $reducer_2048_case 1 HEALTHY

set reducer_unaligned [replace_once $reducer \
    {begin_bytes=4096 pointer=nonnull chunk_bytes=4096 chunk_frames=512} \
    {begin_bytes=1025 pointer=nonnull chunk_bytes=1025 chunk_frames=128}]
set reducer_unaligned_case $base
dict set reducer_unaligned_case reducer_primary $reducer_unaligned
dict set reducer_unaligned_case evidence "$manifest\n$snapshots\n$reducer_unaligned\n$launcher\n$trace"
expect_class reducer-unaligned-rejected $reducer_unaligned_case 0 INVALID

set reducer_reversed [string map [list stop_flush TEMP_REDUCER stop_cork stop_flush TEMP_REDUCER stop_cork] $reducer]
set reducer_reversed_case $base
dict set reducer_reversed_case reducer_primary $reducer_reversed
dict set reducer_reversed_case evidence "$manifest\n$snapshots\n$reducer_reversed\n$launcher\n$trace"
expect_class reducer-reversed-rejected $reducer_reversed_case 0 INVALID

set reducer_duplicate "$reducer\nAUDIO_PULSE_REDUCER_V1 phase=operation_return name=stop_flush pointer=nonnull errno=0 context_errno=0"
set reducer_duplicate_case $base
dict set reducer_duplicate_case reducer_primary $reducer_duplicate
dict set reducer_duplicate_case evidence "$manifest\n$snapshots\n$reducer_duplicate\n$launcher\n$trace"
expect_class reducer-duplicate-rejected $reducer_duplicate_case 0 INVALID

set reducer_drain "$reducer\nAUDIO_PULSE_REDUCER_V1 phase=operation_return name=drain pointer=nonnull errno=0 context_errno=0"
set reducer_drain_case $base
dict set reducer_drain_case reducer_primary $reducer_drain
dict set reducer_drain_case evidence "$manifest\n$snapshots\n$reducer_drain\n$launcher\n$trace"
expect_class reducer-nonchrome-drain-rejected $reducer_drain_case 0 INVALID

set chromium_short $base
set short_launcher [replace_once $launcher \
    {CHROMIUM-AUDIO-SMOKE tick=21 currentTime=21.123 state=running} \
    {CHROMIUM-AUDIO-SMOKE tick=8 currentTime=8.123 state=running}]
dict set chromium_short launcher $short_launcher
dict set chromium_short evidence "$manifest\n$snapshots\n$short_launcher\n$trace"
expect_class chromium-lifecycle $chromium_short 1 CHROMIUM_LIFECYCLE

set null_trace [replace_once $trace \
    {event=operation_return context_id=1 stream_id=1 operation_id=4 action=ResetFlush cork=-1 ret=nonnull failure_class=none} \
    {event=operation_return context_id=1 stream_id=1 operation_id=0 action=ResetFlush cork=-1 ret=null failure_class=teardown}]
set null_trace [string map [list \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=28 mono_ns=1270 event=success_callback stream_id=1 action=ResetFlush success=1 errno_before=0 errno_after=0
} {} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=29 mono_ns=1280 event=operation_unref stream_id=1 operation_id=4 action=ResetFlush ret=void errno_before=0 errno_after=0
} {} \
    {failure_rows=0} {failure_rows=1}] $null_trace]
set chromium_null $base
dict set chromium_null trace $null_trace
dict set chromium_null evidence "$manifest\n$snapshots\n$launcher\n$null_trace"
expect_class chromium-teardown-null $chromium_null 1 CHROMIUM_LIFECYCLE

set primary_null_trace [replace_once $trace \
    {event=operation_return context_id=1 stream_id=1 operation_id=1 action=Start cork=0 ret=nonnull failure_class=none} \
    {event=operation_return context_id=1 stream_id=1 operation_id=0 action=Start cork=0 ret=null failure_class=primary_playback}]
foreach primary_line [list \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=16 mono_ns=1150 event=success_callback stream_id=1 action=Start success=1 errno_before=0 errno_after=0
} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=17 mono_ns=1160 event=operation_state stream_id=1 operation_id=1 action=Start state=1 errno_before=0 errno_after=0
} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=18 mono_ns=1170 event=operation_unref stream_id=1 operation_id=1 action=Start ret=void errno_before=0 errno_after=0
} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=19 mono_ns=1180 event=begin_write context_id=1 stream_id=1 ret=0 buffer=nonnull bytes=4096 errno_before=0 errno_after=0
} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=20 mono_ns=1190 event=stream_write context_id=1 stream_id=1 bytes=4096 frames_f32le_stereo=512 free_callback=0 offset=0 seek=0 ret=0 errno_before=0 errno_after=0
}] {
    set primary_null_trace [string map [list $primary_line {}] $primary_null_trace]
}
set primary_null_trace [string map [list {failure_rows=0} {failure_rows=1}] $primary_null_trace]
set chromium_primary_null $base
dict set chromium_primary_null trace $primary_null_trace
dict set chromium_primary_null evidence "$manifest\n$snapshots\n$launcher\n$primary_null_trace"
expect_class chromium-primary-null $chromium_primary_null 1 CHROMIUM_LIFECYCLE

set missing $base
dict unset missing trace
expect_class missing-artifact $missing 0 INVALID

set malformed $base
dict set malformed manifest "$manifest\nAUDIO_LOCALIZER_HELPER_RESULT schema=1 status=COMPLETE"
expect_class duplicate-helper $malformed 0 INVALID

set quantum_missing $base
dict set quantum_missing pulse_quantum {#pulse.min.quantum = 1024/48000}
expect_class quantum-evidence-rejected $quantum_missing 0 INVALID

set audio_disabled $base
dict set audio_disabled launcher "$launcher\nwayland-chromium-launcher: argv_19=\"--disable-audio-output\""
dict set audio_disabled evidence "[dict get $audio_disabled launcher]\n$manifest"
expect_class audio-disable-rejected $audio_disabled 0 INVALID

set owner_bad $base
dict set owner_bad chromium_owner {AUDIO_LOCALIZER_OWNER schema=1 phase=identity pid=700 pgid=700 expected=chrome exe=/tmp/not-chrome verdict=FAIL errno=1}
dict set owner_bad evidence "$manifest\n$snapshots\n$launcher\n$trace"
expect_class owned-cleanup-rejected $owner_bad 0 INVALID

set owner_split $base
set owner_split_text [replace_once $owner \
    {phase=signal signal=TERM pid=700 pgid=700 starttime=12345} \
    {phase=signal signal=TERM pid=700 pgid=700 starttime=99999}]
dict set owner_split chromium_owner $owner_split_text
dict set owner_split evidence "$manifest\n$snapshots\n$launcher\n$trace\n$owner_split_text"
expect_class owner-starttime-split-rejected $owner_split 0 INVALID

set owner_reversed $base
set owner_reversed_text [join [lreverse [split $owner "\n"]] "\n"]
dict set owner_reversed chromium_owner $owner_reversed_text
dict set owner_reversed evidence "$manifest\n$snapshots\n$launcher\n$trace\n$owner_reversed_text"
expect_class owner-order-rejected $owner_reversed 0 INVALID

set helper_conflict $base
set helper_conflict_manifest [replace_once $manifest \
    {reducer_primary_rc=0 fallback_ready=0 paplay_fallback_rc=NA reducer_fallback_rc=NA chromium_identity_rc=0 no_youtube_claim=1} \
    {reducer_primary_rc=9 fallback_ready=0 paplay_fallback_rc=NA reducer_fallback_rc=NA chromium_identity_rc=0 no_youtube_claim=1}]
dict set helper_conflict manifest $helper_conflict_manifest
dict set helper_conflict evidence "$helper_conflict_manifest\n$snapshots\n$launcher\n$trace"
expect_class helper-rc-conflict-rejected $helper_conflict 0 INVALID

set identity_mismatch $base
set identity_mismatch_text [replace_once $provider_identity \
    "/tmp/provider-build:$identity_sha:$identity_build_id" \
    "/tmp/provider-build:[string repeat b 64]:$identity_build_id"]
dict set identity_mismatch provider_identity $identity_mismatch_text
expect_class identity-tuple-mismatch-rejected $identity_mismatch 0 INVALID

set residue $base
set residue_snapshots [replace_once $snapshots \
    {PID PPID STAT COMMAND} \
    {PID PPID STAT COMMAND
700 1 S chrome}]
dict set residue snapshots $residue_snapshots
dict set residue evidence "$manifest\n$residue_snapshots\n$launcher\n$trace"
expect_class final-process-residue $residue 0 INVALID

set split_trace [replace_once $trace \
    {pid=700 tid=701 seq=20 mono_ns=1190 event=stream_write} \
    {pid=701 tid=701 seq=20 mono_ns=1190 event=stream_write}]
set split_bad $base
dict set split_bad trace $split_trace
dict set split_bad evidence "$manifest\n$snapshots\n$launcher\n$split_trace"
expect_class trace-split-pid-rejected $split_bad 0 INVALID

set reversed_trace [string map [list StopFlush TEMP_ACTION StopCork StopFlush TEMP_ACTION StopCork] $trace]
set reversed_bad $base
dict set reversed_bad trace $reversed_trace
dict set reversed_bad evidence "$manifest\n$snapshots\n$launcher\n$reversed_trace"
expect_class trace-reversed-rejected $reversed_bad 0 INVALID

set duplicate_trace "$trace\nCHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=38 mono_ns=1370 event=provider provider_id=1 handle_id=1 path=/opt/host-gui/wayland-chromium/chrome-linux64/libpulse.so.0 soname=libpulse.so.0 build_id=5a42eddd12bc34f26064eb65a146075cbea16a8c errno_before=0 errno_after=0"
set duplicate_bad $base
dict set duplicate_bad trace $duplicate_trace
dict set duplicate_bad evidence "$manifest\n$snapshots\n$launcher\n$duplicate_trace"
expect_class trace-duplicate-rejected $duplicate_bad 0 INVALID

set provider_bad_trace [replace_once $trace \
    {build_id=5a42eddd12bc34f26064eb65a146075cbea16a8c} \
    {build_id=0000000000000000000000000000000000000000}]
set provider_bad $base
dict set provider_bad trace $provider_bad_trace
dict set provider_bad evidence "$manifest\n$snapshots\n$launcher\n$provider_bad_trace"
expect_class trace-provider-id-rejected $provider_bad 0 INVALID

set operation_id_bad_trace [replace_once $trace \
    {operation_id=2 action=StopFlush cork=-1} \
    {operation_id=1 action=StopFlush cork=-1}]
set operation_id_bad $base
dict set operation_id_bad trace $operation_id_bad_trace
dict set operation_id_bad evidence "$manifest\n$snapshots\n$launcher\n$operation_id_bad_trace"
expect_class trace-operation-id-rejected $operation_id_bad 0 INVALID

set callback_duplicate_trace [replace_once $trace \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=22 mono_ns=1210 event=success_callback stream_id=1 action=StopFlush success=1 errno_before=0 errno_after=0} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=22 mono_ns=1210 event=success_callback stream_id=1 action=StopFlush success=1 errno_before=0 errno_after=0
CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=22 mono_ns=1210 event=success_callback stream_id=1 action=StopFlush success=1 errno_before=0 errno_after=0}]
set callback_duplicate_bad $base
dict set callback_duplicate_bad trace $callback_duplicate_trace
dict set callback_duplicate_bad evidence "$manifest\n$snapshots\n$launcher\n$callback_duplicate_trace"
expect_class trace-callback-duplicate-rejected $callback_duplicate_bad 0 INVALID

set context_clear_missing_trace [string map [list \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=34 mono_ns=1330 event=set_context_state_callback context_id=1 value=clear errno_before=0 errno_after=0
} {}] $trace]
set context_clear_missing $base
dict set context_clear_missing trace $context_clear_missing_trace
dict set context_clear_missing evidence "$manifest\n$snapshots\n$launcher\n$context_clear_missing_trace"
expect_class trace-context-clear-missing-rejected $context_clear_missing 0 INVALID

set context_set_duplicate_trace [replace_once $trace \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=4 mono_ns=1030 event=set_context_state_callback context_id=1 value=set errno_before=0 errno_after=0} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=4 mono_ns=1030 event=set_context_state_callback context_id=1 value=set errno_before=0 errno_after=0
CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=4 mono_ns=1030 event=set_context_state_callback context_id=1 value=set errno_before=0 errno_after=0}]
set context_set_duplicate $base
dict set context_set_duplicate trace $context_set_duplicate_trace
dict set context_set_duplicate evidence "$manifest\n$snapshots\n$launcher\n$context_set_duplicate_trace"
expect_class trace-context-set-duplicate-rejected $context_set_duplicate 0 INVALID

set effective_trace [replace_once $trace \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=37 mono_ns=1360 event=summary binding_count=21 binding_mask=0x1fffff effective_attr_observed=0 effective_attr_reason=symbol_not_requested} \
    {CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=701 seq=37 mono_ns=1355 event=effective_attr context_id=1 stream_id=1 observed=1 ret=nonnull maxlength=4194304 tlength=12288 prebuf=8192 minreq=4096 fragsize=4294967295 errno_before=0 errno_after=0
CHROMIUM_PULSE_TRACE_V2 schema=2 pid=700 tid=700 seq=38 mono_ns=1360 event=summary binding_count=22 binding_mask=0x3fffff effective_attr_observed=1 effective_attr_reason=symbol_requested}]
set effective_case $base
dict set effective_case trace $effective_trace
dict set effective_case evidence "$manifest\n$snapshots\n$launcher\n$effective_trace"
expect_class trace-effective-observed $effective_case 1 HEALTHY

set trace_malformed $base
dict set trace_malformed trace "$trace\nnot-a-trace-row"
dict set trace_malformed evidence "$manifest\n$snapshots\n$launcher\n[dict get $trace_malformed trace]"
expect_class trace-malformed-rejected $trace_malformed 0 INVALID

set wrong_arm $base
set wrong_manifest [replace_once $manifest \
    {name=arm_contract localizer=1} {name=arm_contract localizer=0}]
dict set wrong_arm manifest $wrong_manifest
dict set wrong_arm evidence "$wrong_manifest\n$snapshots\n$launcher\n$trace"
expect_class arm-off-rejected $wrong_arm 0 INVALID

puts "AUDIO-LOCALIZER-STATIC-PASS classification=substrate,server,negotiation,chromium-lifecycle,healthy timeout=PASS malformed=REJECT missing=REJECT helper_conflict=REJECT owner_order=REJECT owner_starttime=REJECT reducer_reverse=REJECT reducer_duplicate=REJECT reducer_nonchrome=REJECT reducer_primary_teardown=SEPARATE reducer_effective_absent=PASS reducer_aligned_2048=PASS reducer_unaligned=REJECT trace_split=REJECT trace_reverse=REJECT trace_duplicate=REJECT trace_operation_id=REJECT trace_callback_cardinality=REJECT trace_context_set_clear=REJECT provider_id=REJECT identity_tuple=REJECT trace_effective_absent=PASS trace_effective_observed=PASS audio_disable=REJECT arm_off=REJECT no_youtube_claim=1"
