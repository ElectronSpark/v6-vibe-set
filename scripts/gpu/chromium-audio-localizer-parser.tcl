# Host-authoritative parser/classifier for the default-off Chromium audio
# localizer.  Guest helpers preserve evidence; they never make the verdict.

proc audio_read_text {path} {
    if {![file exists $path] || ![file isfile $path]} { return "" }
    set fh [open $path r]
    set text [read $fh]
    close $fh
    return $text
}

proc audio_kv_line {line} {
    set result [dict create]
    foreach token [split [string trim $line] " "] {
        if {$token eq "" || ![regexp {^([^=]+)=(.*)$} $token -> key value]} {
            continue
        }
        dict set result $key $value
    }
    return $result
}

proc audio_lines_with_prefix {text prefix} {
    set result {}
    foreach line [split $text "\n"] {
        if {[string first $prefix $line] == 0} { lappend result $line }
    }
    return $result
}

proc audio_unique_phase {text marker phase} {
    set matches {}
    foreach line [audio_lines_with_prefix $text $marker] {
        set kv [audio_kv_line $line]
        if {[dict exists $kv phase] && [dict get $kv phase] eq $phase} {
            lappend matches [list $line $kv]
        }
    }
    if {[llength $matches] != 1} {
        return [list 0 "phase-$phase-count-[llength $matches]" {}]
    }
    return [list 1 [lindex [lindex $matches 0] 0] [lindex [lindex $matches 0] 1]]
}

proc audio_parse_manifest {text} {
    set steps [dict create]
    set errors {}
    set helper_rows 0
    set helper [dict create]

    foreach line [split $text "\n"] {
        if {[string first {AUDIO_LOCALIZER_STEP schema=1 } $line] == 0} {
            set kv [audio_kv_line $line]
            if {![dict exists $kv name]} {
                lappend errors "step-without-name"
                continue
            }
            set name [dict get $kv name]
            if {[dict exists $steps $name]} {
                lappend errors "duplicate-step-$name"
                continue
            }
            dict set steps $name $kv
        } elseif {[string first {AUDIO_LOCALIZER_HELPER_RESULT schema=1 } $line] == 0} {
            incr helper_rows
            set helper [audio_kv_line $line]
        }
    }
    if {$helper_rows != 1} { lappend errors "helper-result-count-$helper_rows" }
    if {$helper_rows == 1 &&
        (![dict exists $helper status] || [dict get $helper status] ne "COMPLETE")} {
        lappend errors "helper-not-complete"
    }
    foreach required {
        arm_contract reducer_identity trace_identity primary_socket fixture
        pw_play_primary paplay_primary reducer_primary server_selection
        chromium_start chromium_identity chromium_owned_cleanup
    } {
        if {![dict exists $steps $required]} {
            lappend errors "missing-step-$required"
        }
    }
    if {[dict exists $steps arm_contract]} {
        set arm [dict get $steps arm_contract]
        foreach {key expected} {
            localizer 1 qemu_audio none audio_disable 0 no_youtube_claim 1
        } {
            if {![dict exists $arm $key] || [dict get $arm $key] ne $expected} {
                lappend errors "arm-$key-not-$expected"
            }
        }
    }
    foreach identity {reducer_identity trace_identity} {
        if {[dict exists $steps $identity]} {
            set row [dict get $steps $identity]
            if {![dict exists $row status] || [dict get $row status] ne "present"} {
                lappend errors "$identity-not-present"
            }
        }
    }
    return [dict create valid [expr {[llength $errors] == 0}] \
        errors $errors steps $steps helper $helper]
}

proc audio_step_value {parsed name key {default __missing__}} {
    if {![dict exists $parsed steps $name $key]} { return $default }
    return [dict get $parsed steps $name $key]
}

proc audio_reconcile_helper {parsed} {
    set errors {}
    foreach {step helper_key} {
        primary_socket primary_socket_rc
        fixture fixture_rc
        pw_play_primary pw_play_primary_rc
        paplay_primary paplay_primary_rc
        reducer_primary reducer_primary_rc
        chromium_identity chromium_identity_rc
    } {
        set step_value [audio_step_value $parsed $step rc]
        set helper_value [expr {[dict exists $parsed helper $helper_key] ?
            [dict get $parsed helper $helper_key] : "__missing__"}]
        if {$step_value ne $helper_value} {
            lappend errors "helper-step-mismatch-$step-$step_value-$helper_value"
        }
    }
    foreach key {fallback_ready paplay_primary_rc reducer_primary_rc paplay_fallback_rc reducer_fallback_rc} {
        set selected [audio_step_value $parsed server_selection $key]
        set helper [expr {[dict exists $parsed helper $key] ?
            [dict get $parsed helper $key] : "__missing__"}]
        if {$selected ne $helper} {
            lappend errors "helper-selection-mismatch-$key-$selected-$helper"
        }
    }
    return $errors
}

proc audio_nonnegative_int {value} {
    return [expr {[string is integer -strict $value] && $value >= 0}]
}

proc audio_reducer_contract {text} {
    set errors {}
    set healthy 0
    set failure_reason unavailable
    set marker AUDIO_PULSE_REDUCER_V1
    set rows [audio_lines_with_prefix $text $marker]
    set phase_positions [dict create]
    set phase_rows [dict create]
    set operation_positions [dict create]
    set operation_rows [dict create]
    set operation_state_positions [dict create]
    set operation_state_rows [dict create]
    set operation_callback_positions [dict create]
    set operation_callback_rows [dict create]
    set write_exact 0
    set null_operations 0
    set index 0

    foreach line $rows {
        set kv [audio_kv_line $line]
        if {![dict exists $kv phase]} {
            lappend errors "row-without-phase-$index"
            incr index
            continue
        }
        set phase [dict get $kv phase]
        dict lappend phase_positions $phase $index
        dict lappend phase_rows $phase $kv
        if {$phase eq "operation_return"} {
            if {![dict exists $kv name]} {
                lappend errors "operation-without-name-$index"
            } else {
                set name [dict get $kv name]
                dict lappend operation_positions $name $index
                dict lappend operation_rows $name $kv
                if {$name in {drain flush_before_stop flush_after_stop}} {
                    lappend errors "non-chrome-operation-$name"
                }
            }
            if {[dict exists $kv pointer] && [dict get $kv pointer] eq "null"} {
                incr null_operations
            }
        } elseif {$phase in {operation operation_callback} &&
                  [dict exists $kv name]} {
            set name [dict get $kv name]
            if {$phase eq "operation"} {
                dict lappend operation_state_positions $name $index
                dict lappend operation_state_rows $name $kv
            } else {
                dict lappend operation_callback_positions $name $index
                dict lappend operation_callback_rows $name $kv
            }
        }
        if {$phase eq "write" &&
            [dict exists $kv begin_ret] && [dict get $kv begin_ret] eq "0" &&
            [dict exists $kv pointer] && [dict get $kv pointer] eq "nonnull" &&
            [dict exists $kv chunk_bytes] &&
            [audio_nonnegative_int [dict get $kv chunk_bytes]] &&
            [dict get $kv chunk_bytes] > 0 &&
            [dict get $kv chunk_bytes] <= 4096 &&
            [dict get $kv chunk_bytes] % 8 == 0 &&
            [dict exists $kv begin_bytes] &&
            [dict get $kv begin_bytes] eq [dict get $kv chunk_bytes] &&
            [dict exists $kv chunk_frames] &&
            [audio_nonnegative_int [dict get $kv chunk_frames]] &&
            [dict get $kv chunk_frames] * 8 == [dict get $kv chunk_bytes] &&
            [dict exists $kv ret] && [dict get $kv ret] eq "0"} {
            set write_exact 1
        }
        incr index
    }

    foreach phase {contract requested_attr stream_flags result} {
        set count [expr {[dict exists $phase_positions $phase] ?
            [llength [dict get $phase_positions $phase]] : 0}]
        if {$count != 1} { lappend errors "phase-$phase-count-$count" }
    }
    if {[llength $errors] > 0 &&
        (! [dict exists $phase_rows contract] ||
         ! [dict exists $phase_rows requested_attr] ||
         ! [dict exists $phase_rows stream_flags] ||
         ! [dict exists $phase_rows result])} {
        return [dict create valid 0 healthy 0 errors $errors reason malformed \
            null_operations $null_operations]
    }

    set contract [lindex [dict get $phase_rows contract] 0]
    set requested [lindex [dict get $phase_rows requested_attr] 0]
    set flags [lindex [dict get $phase_rows stream_flags] 0]
    set result [lindex [dict get $phase_rows result] 0]
    set result_class unavailable

    foreach {key expected} {
        mainloop pa_threaded_mainloop
        stream_constructor pa_stream_new_with_proplist
        stream_name Playback proplist empty
        sample_format PA_SAMPLE_FLOAT32LE sample_format_value 5 rate 48000
        channels 2 channel_map front-left,front-right device null
        context_flags PA_CONTEXT_NOAUTOSPAWN context_flags_value 0x1
        frames_per_write 512 bytes_per_write 4096
        write_path pa_stream_begin_write,pa_stream_write
    } {
        if {![dict exists $contract $key] ||
            [dict get $contract $key] ne $expected} {
            lappend errors "contract-$key"
        }
    }
    foreach {key expected} {
        maxlength 4294967295 minreq 2048 prebuf 4294967295
        tlength 12288 fragsize 4294967295
    } {
        if {![dict exists $requested $key] ||
            [dict get $requested $key] ne $expected} {
            lappend errors "requested-$key"
        }
    }
    if {![dict exists $flags names] ||
        [dict get $flags names] ne {START_CORKED,INTERPOLATE_TIMING,NOT_MONOTONIC,AUTO_TIMING_UPDATE,ADJUST_LATENCY} ||
        ![dict exists $flags value] || [dict get $flags value] ne "0x200f"} {
        lappend errors "stream-flags"
    }
    if {![dict exists $result verdict] || [dict get $result verdict] ni {PASS FAIL}} {
        lappend errors "result-verdict"
    } elseif {[dict get $result verdict] eq "PASS"} {
        set healthy 1
        set result_class healthy
        if {![dict exists $result class] || [dict get $result class] ne "healthy" ||
            ![dict exists $result teardown_failures] ||
            [dict get $result teardown_failures] ne "0"} {
            lappend errors "healthy-result-shape"
        }
    } elseif {[dict exists $result reason]} {
        set failure_reason [dict get $result reason]
        if {![dict exists $result class] ||
            [dict get $result class] ni {primary_playback teardown}} {
            lappend errors "failure-result-class"
        } else {
            set result_class [dict get $result class]
        }
    }
    if {[dict exists $result teardown_failures] &&
        ![audio_nonnegative_int [dict get $result teardown_failures]]} {
        lappend errors "result-teardown-failures"
    }

    # Effective attrs are optional observation.  If a row exists, it must be
    # unique and poison-safe; absence is not a health failure because Chrome
    # 150 does not resolve this symbol.
    set effective_count [expr {[dict exists $phase_positions effective_attr] ?
        [llength [dict get $phase_positions effective_attr]] : 0}]
    if {$effective_count > 1} {
        lappend errors "effective-count-$effective_count"
    } elseif {$effective_count == 1} {
        set effective [lindex [dict get $phase_rows effective_attr] 0]
        if {![dict exists $effective observed] ||
            [dict get $effective observed] ne "1" ||
            ![dict exists $effective result] ||
            [dict get $effective result] ni {null nonnull} ||
            ![dict exists $effective errno] ||
            ![string is integer -strict [dict get $effective errno]] ||
            ![dict exists $effective required_for_health] ||
            [dict get $effective required_for_health] ne "0"} {
            lappend errors "effective-shape"
        }
        if {[dict exists $effective result] &&
            [dict get $effective result] eq "nonnull"} {
            foreach key {maxlength minreq prebuf tlength fragsize} {
                if {![dict exists $effective $key] ||
                    ![audio_nonnegative_int [dict get $effective $key]]} {
                    lappend errors "effective-$key"
                }
            }
        }
    }

    set lifecycle_count [expr {[dict exists $phase_positions lifecycle] ?
        [llength [dict get $phase_positions lifecycle]] : 0}]
    set lifetime_count [expr {[dict exists $phase_positions operation_lifetime] ?
        [llength [dict get $phase_positions operation_lifetime]] : 0}]
    set release_count [expr {[dict exists $phase_positions operation_deferred_release] ?
        [llength [dict get $phase_positions operation_deferred_release]] : 0}]
    set full_chain [expr {$healthy || $lifecycle_count > 0}]
    if {$full_chain} {
        foreach name {start_uncork stop_flush stop_cork reset_flush} {
            set count [expr {[dict exists $operation_positions $name] ?
                [llength [dict get $operation_positions $name]] : 0}]
            if {$count != 1} { lappend errors "operation-$name-count-$count" }
            set state_count [expr {[dict exists $operation_state_positions $name] ?
                [llength [dict get $operation_state_positions $name]] : 0}]
            set callback_count [expr {[dict exists $operation_callback_positions $name] ?
                [llength [dict get $operation_callback_positions $name]] : 0}]
            if {$state_count != 1} {
                lappend errors "operation-state-$name-count-$state_count"
            }
            if {$callback_count != 1} {
                lappend errors "operation-callback-$name-count-$callback_count"
            }
            if {$count == 1 && $state_count == 1 && $callback_count == 1} {
                set returned [lindex [dict get $operation_rows $name] 0]
                set state [lindex [dict get $operation_state_rows $name] 0]
                set callback [lindex [dict get $operation_callback_rows $name] 0]
                set pointer [expr {[dict exists $returned pointer] ?
                    [dict get $returned pointer] : "missing"}]
                set operation_deferred 0
                if {[dict exists $phase_rows operation_lifetime]} {
                    foreach lifetime_row [dict get $phase_rows operation_lifetime] {
                        if {[dict exists $lifetime_row name] &&
                            [dict get $lifetime_row name] eq $name} {
                            set operation_deferred 1
                        }
                    }
                }
                if {$pointer eq "nonnull"} {
                    if {$operation_deferred} {
                        if {![dict exists $state result] ||
                            [dict get $state result] ne "nonnull" ||
                            ![dict exists $state operation_state] ||
                            [dict get $state operation_state] ne "0" ||
                            ![dict exists $state timed_out] ||
                            [dict get $state timed_out] ne "1" ||
                            ![dict exists $callback seen] ||
                            [dict get $callback seen] ne "0" ||
                            ![dict exists $callback success] ||
                            [dict get $callback success] ne "0"} {
                            lappend errors "operation-deferred-shape-$name"
                        }
                    } elseif {![dict exists $state result] ||
                        [dict get $state result] ne "nonnull" ||
                        ![dict exists $state operation_state] ||
                        [dict get $state operation_state] ne "1" ||
                        ![dict exists $state timed_out] ||
                        [dict get $state timed_out] ne "0" ||
                        ![dict exists $callback seen] ||
                        [dict get $callback seen] ne "1" ||
                        ![dict exists $callback success] ||
                        [dict get $callback success] ne "1"} {
                        lappend errors "operation-success-shape-$name"
                    }
                } elseif {$pointer eq "null"} {
                    if {![dict exists $state result] ||
                        [dict get $state result] ne "null" ||
                        ![dict exists $state verdict] ||
                        [dict get $state verdict] ne "FAIL" ||
                        ![dict exists $callback seen] ||
                        [dict get $callback seen] ne "0" ||
                        ![dict exists $callback success] ||
                        [dict get $callback success] ne "0"} {
                        lappend errors "operation-null-shape-$name"
                    }
                } else {
                    lappend errors "operation-pointer-$name"
                }
                set return_pos [lindex [dict get $operation_positions $name] 0]
                set state_pos [lindex [dict get $operation_state_positions $name] 0]
                set callback_pos [lindex [dict get $operation_callback_positions $name] 0]
                if {!($return_pos < $state_pos && $state_pos < $callback_pos)} {
                    lappend errors "operation-order-$name"
                }
            }
        }
        foreach phase {lifecycle close_clear_callback stream_disconnect stream_unref context_disconnect context_unref threaded_mainloop_stop threaded_mainloop_free} {
            set count [expr {[dict exists $phase_positions $phase] ?
                [llength [dict get $phase_positions $phase]] : 0}]
            if {$phase eq "close_clear_callback"} {
                if {$count != 2} { lappend errors "phase-$phase-count-$count" }
            } elseif {$count != 1} {
                lappend errors "phase-$phase-count-$count"
            }
        }
        if {$lifecycle_count != 1} {
            lappend errors "lifecycle-count-$lifecycle_count"
        } else {
            set l [lindex [dict get $phase_rows lifecycle] 0]
            foreach key {elapsed_ms callbacks writes frames requested_total request_min request_max short_callbacks} {
                if {![dict exists $l $key] ||
                    ![audio_nonnegative_int [dict get $l $key]]} {
                    lappend errors "lifecycle-$key"
                }
            }
            if {[dict exists $l elapsed_ms] && [dict get $l elapsed_ms] < 20000} {
                lappend errors "lifecycle-short"
            }
            foreach key {callbacks writes frames} {
                if {[dict exists $l $key] && [dict get $l $key] <= 0} {
                    lappend errors "lifecycle-zero-$key"
                }
            }
            if {$healthy &&
                (![dict exists $l stream_state] || [dict get $l stream_state] ne "READY" ||
                 ![dict exists $l context_state] || [dict get $l context_state] ne "READY")} {
                lappend errors "lifecycle-terminal-state"
            }
        }
        if {!$write_exact} { lappend errors "begin-write-aligned-write-missing" }

        set can_order 1
        foreach required_operation {start_uncork stop_flush stop_cork reset_flush} {
            if {![dict exists $operation_positions $required_operation]} {
                set can_order 0
            }
        }
        foreach required_phase {lifecycle stream_disconnect stream_unref context_disconnect context_unref threaded_mainloop_stop threaded_mainloop_free result} {
            if {![dict exists $phase_positions $required_phase]} {
                set can_order 0
            }
        }
        if {$can_order && $lifetime_count == 0} {
            set ordered [list \
                [lindex [dict get $operation_positions start_uncork] 0] \
                [lindex [dict get $phase_positions lifecycle] 0] \
                [lindex [dict get $operation_positions stop_flush] 0] \
                [lindex [dict get $operation_positions stop_cork] 0] \
                [lindex [dict get $operation_positions reset_flush] 0] \
                [lindex [dict get $phase_positions stream_disconnect] 0] \
                [lindex [dict get $phase_positions stream_unref] 0] \
                [lindex [dict get $phase_positions context_disconnect] 0] \
                [lindex [dict get $phase_positions context_unref] 0] \
                [lindex [dict get $phase_positions threaded_mainloop_stop] 0] \
                [lindex [dict get $phase_positions threaded_mainloop_free] 0] \
                [lindex [dict get $phase_positions result] 0]]
            set previous -1
            foreach position $ordered {
                if {$position <= $previous} { lappend errors "lifecycle-order"; break }
                set previous $position
            }
        }
        if {$healthy && $null_operations != 0} {
            lappend errors "healthy-null-operations-$null_operations"
        }
        if {!$healthy && $result_class eq "primary_playback"} {
            set primary_evidence 0
            if {[dict exists $operation_rows start_uncork]} {
                set start_row [lindex [dict get $operation_rows start_uncork] 0]
                if {[dict exists $start_row pointer] &&
                    [dict get $start_row pointer] eq "null"} {
                    set primary_evidence 1
                }
            }
            if {$lifecycle_count == 1} {
                set l [lindex [dict get $phase_rows lifecycle] 0]
                if {([dict exists $l stream_state] && [dict get $l stream_state] ne "READY") ||
                    ([dict exists $l context_state] && [dict get $l context_state] ne "READY")} {
                    set primary_evidence 1
                }
            }
            if {!$primary_evidence} {
                lappend errors "primary-failure-without-evidence"
            }
        }
        if {!$healthy && $result_class eq "teardown"} {
            set teardown_evidence 0
            foreach name {stop_flush stop_cork reset_flush} {
                if {[dict exists $operation_rows $name]} {
                    set teardown_row [lindex [dict get $operation_rows $name] 0]
                    if {[dict exists $teardown_row pointer] &&
                        [dict get $teardown_row pointer] eq "null"} {
                        set teardown_evidence 1
                    }
                }
                if {[dict exists $phase_rows operation_lifetime]} {
                    foreach lifetime_row [dict get $phase_rows operation_lifetime] {
                        if {[dict exists $lifetime_row name] &&
                            [dict get $lifetime_row name] eq $name} {
                            set teardown_evidence 1
                        }
                    }
                }
            }
            if {!$teardown_evidence ||
                ![dict exists $result teardown_failures] ||
                [dict get $result teardown_failures] < 1} {
                lappend errors "teardown-failure-without-evidence"
            }
        }
    }
    if {$lifetime_count != $release_count} {
        lappend errors "operation-deferred-count-$lifetime_count-$release_count"
    }
    if {$lifetime_count > 0 && $lifetime_count == $release_count} {
        set lifetime_names [dict create]
        foreach row [dict get $phase_rows operation_lifetime] {
            if {![dict exists $row name] ||
                [dict exists $lifetime_names [dict get $row name]] ||
                ![dict exists $row action] ||
                [dict get $row action] ne "defer_until_mainloop_stop" ||
                ![dict exists $row operation_state] ||
                [dict get $row operation_state] ne "0" ||
                ![dict exists $row callback_seen] ||
                [dict get $row callback_seen] ne "0"} {
                lappend errors "operation-deferred-lifetime-shape"
            } else {
                dict set lifetime_names [dict get $row name] 1
            }
        }
        set release_names [dict create]
        foreach row [dict get $phase_rows operation_deferred_release] {
            if {![dict exists $row name] ||
                [dict exists $release_names [dict get $row name]] ||
                ![dict exists $row callback_seen] ||
                [dict get $row callback_seen] ne "1" ||
                ![dict exists $row success] ||
                [dict get $row success] ne "1" ||
                ![dict exists $row mainloop_stopped] ||
                [dict get $row mainloop_stopped] ne "1"} {
                lappend errors "operation-deferred-release-shape"
            } else {
                dict set release_names [dict get $row name] 1
            }
        }
        if {[lsort [dict keys $lifetime_names]] ne
            [lsort [dict keys $release_names]]} {
            lappend errors "operation-deferred-name-mismatch"
        }
        foreach required_phase {threaded_mainloop_stop stream_unref context_unref threaded_mainloop_free result} {
            if {![dict exists $phase_positions $required_phase] ||
                [llength [dict get $phase_positions $required_phase]] != 1} {
                lappend errors "operation-deferred-phase-$required_phase"
            }
        }
        if {[dict exists $phase_positions threaded_mainloop_stop] &&
            [dict exists $phase_positions stream_unref] &&
            [dict exists $phase_positions context_unref] &&
            [dict exists $phase_positions threaded_mainloop_free] &&
            [dict exists $phase_positions result]} {
            set stop_pos [lindex [dict get $phase_positions threaded_mainloop_stop] 0]
            set stream_unref_pos [lindex [dict get $phase_positions stream_unref] 0]
            set context_unref_pos [lindex [dict get $phase_positions context_unref] 0]
            set free_pos [lindex [dict get $phase_positions threaded_mainloop_free] 0]
            set result_pos [lindex [dict get $phase_positions result] 0]
            foreach release_pos [dict get $phase_positions operation_deferred_release] {
                if {!($stop_pos < $release_pos && $release_pos < $stream_unref_pos)} {
                    lappend errors "operation-deferred-release-order"
                }
            }
            if {!($stream_unref_pos < $context_unref_pos &&
                  $context_unref_pos < $free_pos && $free_pos < $result_pos)} {
                lappend errors "operation-deferred-close-order"
            }
        }
        if {$full_chain} {
            set deferred_chain_ok 1
            foreach required_operation {start_uncork stop_flush stop_cork reset_flush} {
                if {![dict exists $operation_positions $required_operation]} {
                    set deferred_chain_ok 0
                }
            }
            foreach required_phase {lifecycle stream_disconnect context_disconnect threaded_mainloop_stop} {
                if {![dict exists $phase_positions $required_phase]} {
                    set deferred_chain_ok 0
                }
            }
            if {$deferred_chain_ok} {
                set deferred_order [list \
                    [lindex [dict get $operation_positions start_uncork] 0] \
                    [lindex [dict get $phase_positions lifecycle] 0] \
                    [lindex [dict get $operation_positions stop_flush] 0] \
                    [lindex [dict get $operation_positions stop_cork] 0] \
                    [lindex [dict get $operation_positions reset_flush] 0] \
                    [lindex [dict get $phase_positions stream_disconnect] 0] \
                    [lindex [dict get $phase_positions context_disconnect] 0] \
                    [lindex [dict get $phase_positions threaded_mainloop_stop] 0]]
                set previous -1
                foreach position $deferred_order {
                    if {$position <= $previous} {
                        lappend errors "operation-deferred-chain-order"
                        break
                    }
                    set previous $position
                }
            }
        }
    }
    return [dict create valid [expr {[llength $errors] == 0}] \
        healthy $healthy errors $errors reason $failure_reason \
        null_operations $null_operations effective_observed $effective_count \
        full_chain $full_chain result_class $result_class \
        deferred_operations $lifetime_count]
}

proc audio_trace_contract {text} {
    set errors {}
    set parsed_rows {}
    set event_rows [dict create]
    set last_seq [dict create]
    set last_time [dict create]
    set sequence_seen [dict create]
    set provider_pids [dict create]
    set rows 0

    foreach line [split [string trim $text] "\n"] {
        if {$line eq ""} { continue }
        if {[string first {CHROMIUM_PULSE_TRACE_V2 schema=2 } $line] != 0} {
            lappend errors "malformed-trace-line"
            continue
        }
        incr rows
        if {[string length $line] > 2048} { lappend errors "trace-row-too-long" }
        set kv [audio_kv_line $line]
        foreach key {schema pid tid seq mono_ns event errno_before errno_after} {
            if {![dict exists $kv $key]} { lappend errors "trace-missing-$key" }
        }
        if {![dict exists $kv schema] || [dict get $kv schema] ne "2"} {
            lappend errors "trace-schema"
        }
        set numeric_ok 1
        foreach key {pid tid seq mono_ns errno_before errno_after} {
            if {![dict exists $kv $key] ||
                ![string is integer -strict [dict get $kv $key]]} {
                set numeric_ok 0
                lappend errors "trace-invalid-$key"
            }
        }
        if {!$numeric_ok} { continue }
        if {[dict get $kv pid] <= 1 || [dict get $kv tid] <= 0 ||
            [dict get $kv seq] <= 0 || [dict get $kv mono_ns] <= 0} {
            lappend errors "trace-nonpositive-identity"
            continue
        }
        set pid [dict get $kv pid]
        set seq [dict get $kv seq]
        set mono [dict get $kv mono_ns]
        if {[dict exists $sequence_seen "$pid,$seq"]} {
            lappend errors "trace-duplicate-seq-$pid-$seq"
        }
        dict set sequence_seen "$pid,$seq" 1
        if {[dict exists $last_seq $pid] &&
            ($seq <= [dict get $last_seq $pid] ||
             $mono < [dict get $last_time $pid])} {
            lappend errors "trace-order-$pid-$seq"
        }
        dict set last_seq $pid $seq
        dict set last_time $pid $mono
        if {![dict exists $kv event] ||
            ![regexp {^[a-z][a-z0-9_]*$} [dict get $kv event]]} {
            lappend errors "trace-event-shape"
            continue
        }
        set event [dict get $kv event]
        if {$event eq "provider"} {
            dict set provider_pids $pid 1
        }
        dict lappend event_rows $event $kv
        lappend parsed_rows $kv
    }
    if {$rows == 0} { lappend errors "trace-empty" }
    set session_pid -1
    if {[dict size $provider_pids] != 1} {
        lappend errors "trace-provider-pid-count-[dict size $provider_pids]"
    } else {
        set session_pid [lindex [dict keys $provider_pids] 0]
    }
    foreach kv $parsed_rows {
        set event [dict get $kv event]
        if {[dict get $kv pid] != $session_pid &&
            $event in {provider context_new set_context_state_callback context_notify context_connect context_state context_errno stream_new set_state_callback set_write_callback stream_notify request_callback stream_connect stream_state begin_write stream_write operation_return operation_state operation_unref success_callback free_callback effective_attr stream_disconnect stream_unref context_disconnect context_unref}} {
            lappend errors "trace-split-pid-$event"
        }
    }

    set session_events [dict create]
    foreach kv $parsed_rows {
        if {[dict get $kv pid] == $session_pid} {
            dict lappend session_events [dict get $kv event] $kv
        }
    }
    foreach event {provider context_new context_connect stream_new stream_connect summary} {
        set count [expr {[dict exists $session_events $event] ?
            [llength [dict get $session_events $event]] : 0}]
        if {$count != 1} { lappend errors "trace-$event-count-$count" }
    }
    if {[dict exists $session_events provider] &&
        [llength [dict get $session_events provider]] == 1} {
        set provider [lindex [dict get $session_events provider] 0]
        foreach {key expected} {
            soname libpulse.so.0
            build_id 5a42eddd12bc34f26064eb65a146075cbea16a8c
        } {
            if {![dict exists $provider $key] ||
                [dict get $provider $key] ne $expected} {
                lappend errors "trace-provider-$key"
            }
        }
        if {![dict exists $provider path] ||
            ![string match */libpulse.so.0 [dict get $provider path]] ||
            ![dict exists $provider provider_id] ||
            ![audio_nonnegative_int [dict get $provider provider_id]] ||
            [dict get $provider provider_id] == 0 ||
            ![dict exists $provider handle_id] ||
            [dict get $provider handle_id] ne [dict get $provider provider_id]} {
            lappend errors "trace-provider-identity"
        }
    }

    set context_id -1
    set stream_id -1
    set position [dict create]
    foreach kv $parsed_rows {
        if {[dict get $kv pid] == $session_pid} {
            set event [dict get $kv event]
            if {![dict exists $position $event]} {
                dict set position $event [dict get $kv seq]
            }
        }
    }
    if {[dict exists $session_events context_new] &&
        [llength [dict get $session_events context_new]] == 1} {
        set row [lindex [dict get $session_events context_new] 0]
        if {![dict exists $row context_id] ||
            ![audio_nonnegative_int [dict get $row context_id]] ||
            [dict get $row context_id] == 0 ||
            ![dict exists $row name] || [dict get $row name] in {"" null} ||
            ![dict exists $row ret] || [dict get $row ret] ne "nonnull"} {
            lappend errors "trace-context-new-shape"
        } else {
            set context_id [dict get $row context_id]
        }
    }
    if {[dict exists $session_events stream_new] &&
        [llength [dict get $session_events stream_new]] == 1} {
        set row [lindex [dict get $session_events stream_new] 0]
        foreach {key expected} {
            variant with_proplist name Playback proplist opaque
            format 5 rate 48000 channels 2 map_channels 2 map0 1 map1 2
            ret nonnull
        } {
            if {![dict exists $row $key] || [dict get $row $key] ne $expected} {
                lappend errors "trace-stream-new-$key"
            }
        }
        if {![dict exists $row context_id] ||
            [dict get $row context_id] ne $context_id ||
            ![dict exists $row stream_id] ||
            ![audio_nonnegative_int [dict get $row stream_id]] ||
            [dict get $row stream_id] == 0} {
            lappend errors "trace-stream-new-identity"
        } else {
            set stream_id [dict get $row stream_id]
        }
    }

    foreach event {context_connect context_state context_errno stream_connect stream_state begin_write stream_write operation_return stream_disconnect stream_unref context_disconnect context_unref} {
        if {![dict exists $session_events $event]} { continue }
        foreach row [dict get $session_events $event] {
            if {[dict exists $row context_id] &&
                [dict get $row context_id] ne $context_id} {
                lappend errors "trace-split-context-$event"
            }
            if {[dict exists $row stream_id] &&
                [dict get $row stream_id] ne $stream_id} {
                lappend errors "trace-split-stream-$event"
            }
        }
    }
    if {[dict exists $session_events context_connect] &&
        [llength [dict get $session_events context_connect]] == 1} {
        set row [lindex [dict get $session_events context_connect] 0]
        foreach {key expected} {server default-null flags 0x1 ret 0} {
            if {![dict exists $row $key] || [dict get $row $key] ne $expected} {
                lappend errors "trace-context-connect-$key"
            }
        }
    }
    set context_ready 0
    if {[dict exists $session_events context_state]} {
        foreach row [dict get $session_events context_state] {
            if {[dict exists $row state] && [dict get $row state] eq "4" &&
                [dict exists $row context_id] &&
                [dict get $row context_id] eq $context_id} {
                set context_ready 1
            }
        }
    }
    if {!$context_ready} { lappend errors "trace-context-ready-missing" }
    foreach value {set clear} {
        set found 0
        set found_seq -1
        if {[dict exists $session_events set_context_state_callback]} {
            foreach row [dict get $session_events set_context_state_callback] {
                if {[dict exists $row context_id] &&
                    [dict get $row context_id] eq $context_id &&
                    [dict exists $row value] && [dict get $row value] eq $value} {
                    incr found
                    set found_seq [dict get $row seq]
                }
            }
        }
        if {$found != 1} {
            lappend errors "trace-context-callback-$value-count-$found"
        } elseif {$value eq "set" && [dict exists $position context_connect] &&
                  $found_seq > [dict get $position context_connect]} {
            lappend errors "trace-context-callback-set-order"
        } elseif {$value eq "clear" &&
                  (([dict exists $position context_disconnect] &&
                    $found_seq > [dict get $position context_disconnect]) ||
                   ([dict exists $position stream_unref] &&
                    $found_seq < [dict get $position stream_unref]))} {
            lappend errors "trace-context-callback-clear-order"
        }
    }
    set context_notify_count 0
    if {[dict exists $session_events context_notify]} {
        foreach row [dict get $session_events context_notify] {
            if {[dict exists $row context_id] &&
                [dict get $row context_id] eq $context_id} {
                incr context_notify_count
            }
        }
    }
    if {$context_notify_count < 1} {
        lappend errors "trace-context-notify-missing"
    }
    if {[dict exists $session_events stream_connect] &&
        [llength [dict get $session_events stream_connect]] == 1} {
        set row [lindex [dict get $session_events stream_connect] 0]
        foreach {key expected} {
            device default-null flags 0x200f maxlength 4294967295
            tlength 12288 prebuf 4294967295 minreq 2048 fragsize 4294967295
            ret 0
        } {
            if {![dict exists $row $key] || [dict get $row $key] ne $expected} {
                lappend errors "trace-stream-connect-$key"
            }
        }
    }
    set stream_ready 0
    set primary_failures 0
    set primary_failure_reason none
    if {[dict exists $session_events stream_state]} {
        foreach row [dict get $session_events stream_state] {
            if {[dict exists $row state] && [dict get $row state] eq "2" &&
                [dict exists $row stream_id] &&
                [dict get $row stream_id] eq $stream_id} {
                set stream_ready 1
            }
            if {[dict exists $row state] &&
                [dict get $row state] in {3 4} &&
                [dict exists $row stream_id] &&
                [dict get $row stream_id] eq $stream_id} {
                incr primary_failures
                set primary_failure_reason stream_terminal
            }
        }
    }
    if {!$stream_ready && $primary_failures == 0} {
        lappend errors "trace-stream-ready-missing"
    }

    foreach {event value} {set_state_callback set set_write_callback set} {
        set found 0
        if {[dict exists $session_events $event]} {
            foreach row [dict get $session_events $event] {
                if {[dict exists $row stream_id] &&
                    [dict get $row stream_id] eq $stream_id &&
                    [dict exists $row value] && [dict get $row value] eq $value} {
                    incr found
                }
            }
        }
        if {$found != 1} { lappend errors "trace-$event-$value-count-$found" }
    }
    foreach callback_event {stream_notify request_callback} {
        set found 0
        if {[dict exists $session_events $callback_event]} {
            foreach row [dict get $session_events $callback_event] {
                if {[dict exists $row stream_id] &&
                    [dict get $row stream_id] eq $stream_id} { set found 1 }
            }
        }
        if {!$found} { lappend errors "trace-$callback_event-missing" }
    }

    set actions [dict create]
    set teardown_failures 0
    if {[dict exists $session_events operation_return]} {
        foreach row [dict get $session_events operation_return] {
            if {![dict exists $row action]} {
                lappend errors "trace-operation-action-missing"
                continue
            }
            set action [dict get $row action]
            dict lappend actions $action $row
            if {![dict exists $row ret] || [dict get $row ret] ni {nonnull null} ||
                ![dict exists $row failure_class]} {
                lappend errors "trace-operation-shape-$action"
            } elseif {[dict get $row ret] eq "null"} {
                if {$action eq "Start" &&
                    [dict get $row failure_class] eq "primary_playback"} {
                    incr primary_failures
                    set primary_failure_reason start_operation
                } elseif {$action in {StopFlush StopCork ResetFlush} &&
                          [dict get $row failure_class] eq "teardown"} {
                    incr teardown_failures
                } else {
                    lappend errors "trace-operation-failure-class-$action"
                }
            } elseif {[dict get $row failure_class] ne "none"} {
                lappend errors "trace-operation-success-class-$action"
            }
        }
    }
    foreach action {Start StopFlush StopCork ResetFlush} {
        set count [expr {[dict exists $actions $action] ?
            [llength [dict get $actions $action]] : 0}]
        if {$primary_failures == 0 && $count != 1} {
            lappend errors "trace-action-$action-count-$count"
        }
        if {$primary_failures > 0 && $count > 1} {
            lappend errors "trace-action-$action-count-$count"
        }
    }
    if {$primary_failures > 1} {
        lappend errors "trace-primary-failure-count-$primary_failures"
    }

    set operation_ids [dict create]
    foreach action {Start StopFlush StopCork ResetFlush} {
        if {![dict exists $actions $action]} { continue }
        foreach operation_row [dict get $actions $action] {
            set operation_id [expr {[dict exists $operation_row operation_id] ?
                [dict get $operation_row operation_id] : -1}]
            set expected_cork [expr {$action eq "Start" ? "0" :
                                      $action eq "StopCork" ? "1" : "-1"}]
            if {![dict exists $operation_row cork] ||
                [dict get $operation_row cork] ne $expected_cork} {
                lappend errors "trace-operation-cork-$action"
            }
            if {[dict exists $operation_row ret] &&
                [dict get $operation_row ret] eq "null"} {
                if {$operation_id ne "0"} {
                    lappend errors "trace-null-operation-id-$action"
                }
                foreach follow_event {success_callback operation_state operation_unref} {
                    if {![dict exists $session_events $follow_event]} { continue }
                    foreach follow [dict get $session_events $follow_event] {
                        if {[dict exists $follow action] &&
                            [dict get $follow action] eq $action &&
                            [dict get $follow seq] > [dict get $operation_row seq]} {
                            lappend errors "trace-null-operation-followup-$action-$follow_event"
                        }
                    }
                }
                continue
            }
            if {![audio_nonnegative_int $operation_id] || $operation_id == 0} {
                lappend errors "trace-operation-id-$action"
                continue
            }
            if {[dict exists $operation_ids $operation_id]} {
                lappend errors "trace-operation-id-reused-$operation_id"
            }
            dict set operation_ids $operation_id 1
            foreach {follow_event wanted_count} {
                success_callback 1 operation_unref 1
            } {
                set matches 0
                set follow_seq -1
                if {[dict exists $session_events $follow_event]} {
                    foreach follow [dict get $session_events $follow_event] {
                        if {[dict exists $follow action] &&
                            [dict get $follow action] eq $action &&
                            [dict exists $follow stream_id] &&
                            [dict get $follow stream_id] eq $stream_id &&
                            ($follow_event ne "operation_unref" ||
                             ([dict exists $follow operation_id] &&
                              [dict get $follow operation_id] eq $operation_id))} {
                            incr matches
                            set follow_seq [dict get $follow seq]
                            if {$follow_event eq "success_callback" &&
                                (![dict exists $follow success] ||
                                 [dict get $follow success] ne "1")} {
                                lappend errors "trace-operation-callback-failed-$action"
                            }
                        }
                    }
                }
                if {$matches != $wanted_count ||
                    $follow_seq <= [dict get $operation_row seq]} {
                    lappend errors "trace-operation-$follow_event-$action"
                }
            }
        }
    }

    set write_4096 0
    if {[dict exists $session_events begin_write]} {
        foreach begin [dict get $session_events begin_write] {
            if {[dict exists $begin ret] && [dict get $begin ret] ne "0"} {
                if {![dict exists $begin buffer] ||
                    [dict get $begin buffer] ne "unavailable" ||
                    ![dict exists $begin bytes] ||
                    [dict get $begin bytes] ne "unavailable"} {
                    lappend errors "trace-begin-failure-output"
                }
                incr primary_failures
                set primary_failure_reason begin_write
            }
        }
    }
    if {[dict exists $session_events begin_write] &&
        [dict exists $session_events stream_write]} {
        foreach begin [dict get $session_events begin_write] {
            if {![dict exists $begin ret] || [dict get $begin ret] ne "0" ||
                ![dict exists $begin buffer] ||
                [dict get $begin buffer] ne "nonnull" ||
                ![dict exists $begin bytes] ||
                ![audio_nonnegative_int [dict get $begin bytes]] ||
                [dict get $begin bytes] < 4096} { continue }
            foreach write [dict get $session_events stream_write] {
                if {[dict get $write seq] > [dict get $begin seq] &&
                    [dict exists $write bytes] && [dict get $write bytes] eq "4096" &&
                    [dict exists $write frames_f32le_stereo] &&
                    [dict get $write frames_f32le_stereo] eq "512" &&
                    [dict exists $write ret] && [dict get $write ret] eq "0"} {
                    set write_4096 1
                }
            }
        }
    }
    if {$primary_failures == 0 && !$write_4096} {
        lappend errors "trace-begin-write-4096-missing"
    }
    if {$primary_failures > 1} {
        lappend errors "trace-primary-failure-final-count-$primary_failures"
    }

    if {$primary_failures == 0} {
        set ordered_events [list provider context_new context_connect stream_new \
            stream_connect Start begin_write stream_write StopFlush StopCork \
            ResetFlush stream_disconnect stream_unref context_disconnect \
            context_unref summary]
        set previous -1
        foreach item $ordered_events {
            if {$item in {Start StopFlush StopCork ResetFlush}} {
                set current [expr {[dict exists $actions $item] ?
                    [dict get [lindex [dict get $actions $item] 0] seq] : -1}]
            } else {
                set current [expr {[dict exists $position $item] ?
                    [dict get $position $item] : -1}]
            }
            if {$current < 0 || $current <= $previous} {
                lappend errors "trace-causal-order-$item"
                break
            }
            set previous $current
        }
    }

    foreach {event value} {set_write_callback clear set_state_callback clear} {
        set found 0
        if {[dict exists $session_events $event]} {
            foreach row [dict get $session_events $event] {
                if {[dict exists $row stream_id] &&
                    [dict get $row stream_id] eq $stream_id &&
                    [dict exists $row value] && [dict get $row value] eq $value &&
                    (![dict exists $position stream_disconnect] ||
                     [dict get $row seq] < [dict get $position stream_disconnect])} {
                    incr found
                }
            }
        }
        if {$found != 1} { lappend errors "trace-$event-$value-count-$found" }
    }

    foreach event {stream_disconnect stream_unref context_disconnect context_unref} {
        set count [expr {[dict exists $session_events $event] ?
            [llength [dict get $session_events $event]] : 0}]
        if {$count != 1} {
            lappend errors "trace-close-$event-count-$count"
            continue
        }
        set close_row [lindex [dict get $session_events $event] 0]
        if {$event eq "stream_disconnect" &&
            (![dict exists $close_row ret] || [dict get $close_row ret] ne "0")} {
            lappend errors "trace-close-stream-disconnect-ret"
        }
    }

    set effective_count [expr {[dict exists $session_events effective_attr] ?
        [llength [dict get $session_events effective_attr]] : 0}]
    if {$effective_count > 1} {
        lappend errors "trace-effective-count-$effective_count"
    } elseif {$effective_count == 1} {
        set effective [lindex [dict get $session_events effective_attr] 0]
        if {![dict exists $effective observed] ||
            [dict get $effective observed] ne "1" ||
            ![dict exists $effective ret] ||
            [dict get $effective ret] ni {nonnull null}} {
            lappend errors "trace-effective-shape"
        } elseif {[dict get $effective ret] eq "nonnull"} {
            foreach key {maxlength tlength prebuf minreq fragsize} {
                if {![dict exists $effective $key] ||
                    ![audio_nonnegative_int [dict get $effective $key]]} {
                    lappend errors "trace-effective-$key"
                }
            }
        } elseif {![dict exists $effective values] ||
                  [dict get $effective values] ne "unavailable"} {
            lappend errors "trace-effective-null-output"
        }
    }
    if {[dict exists $session_events summary] &&
        [llength [dict get $session_events summary]] == 1} {
        set summary [lindex [dict get $session_events summary] 0]
        foreach key {binding_count binding_mask effective_attr_observed effective_attr_reason pointer_reuse overflow hot_seen hot_suppressed failure_rows fork_resets} {
            if {![dict exists $summary $key]} { lappend errors "trace-summary-$key" }
        }
        if {[dict exists $summary binding_mask] &&
            (![regexp {^0x[0-9a-f]+$} [dict get $summary binding_mask]] ||
             [expr {[dict get $summary binding_mask] & 0x1fffff}] != 0x1fffff)} {
            lappend errors "trace-summary-binding-mask"
        }
        if {[dict exists $summary binding_count] &&
            (![audio_nonnegative_int [dict get $summary binding_count]] ||
             [dict get $summary binding_count] < 21)} {
            lappend errors "trace-summary-binding-count"
        }
        if {$effective_count == 0 &&
            (![dict exists $summary effective_attr_observed] ||
             [dict get $summary effective_attr_observed] ne "0" ||
             ![dict exists $summary effective_attr_reason] ||
             [dict get $summary effective_attr_reason] ne "symbol_not_requested")} {
            lappend errors "trace-effective-absent-summary"
        }
        if {$effective_count == 1 &&
            (![dict exists $summary effective_attr_observed] ||
             [dict get $summary effective_attr_observed] ne "1" ||
             ![dict exists $summary effective_attr_reason] ||
             [dict get $summary effective_attr_reason] ne "symbol_requested")} {
            lappend errors "trace-effective-observed-summary"
        }
        if {[dict exists $summary overflow] &&
            [dict get $summary overflow] ne "0"} {
            lappend errors "trace-overflow"
        }
        foreach row $parsed_rows {
            if {[dict get $row pid] == $session_pid &&
                [dict get $row seq] > [dict get $summary seq]} {
                lappend errors "trace-summary-not-terminal"
                break
            }
        }
    }
    return [dict create valid [expr {[llength $errors] == 0}] errors $errors \
        rows $rows session_pid $session_pid context_id $context_id \
        stream_id $stream_id primary_failures $primary_failures \
        primary_failure_reason $primary_failure_reason \
        teardown_failures $teardown_failures effective_observed $effective_count \
        write_4096 $write_4096 context_ready $context_ready \
        stream_ready $stream_ready]
}

proc audio_owner_contract {text record_text expected record_path} {
    set errors {}
    set record_lines [split [string trim $record_text] "\n"]
    if {[llength $record_lines] != 1} {
        return [dict create valid 0 errors [list "owner-record-lines-[llength $record_lines]"]]
    }
    set record [audio_kv_line [lindex $record_lines 0]]
    foreach {key wanted} [list schema 1 expected $expected] {
        if {![dict exists $record $key] || [dict get $record $key] ne $wanted} {
            lappend errors "owner-record-$key"
        }
    }
    foreach key {pid pgid starttime} {
        if {![dict exists $record $key] ||
            ![string is integer -strict [dict get $record $key]] ||
            [dict get $record $key] <= 1} {
            lappend errors "owner-record-$key"
        }
    }
    if {[dict exists $record pid] && [dict exists $record pgid] &&
        [dict get $record pid] ne [dict get $record pgid]} {
        lappend errors "owner-record-pid-pgid"
    }
    set phases [dict create]
    set phase_positions [dict create]
    set owner_index 0
    foreach line [split $text "\n"] {
        if {[string first {AUDIO_LOCALIZER_OWNER } $line] == 0 &&
            [string first {AUDIO_LOCALIZER_OWNER schema=1 } $line] != 0} {
            lappend errors "owner-malformed-schema"
            incr owner_index
            continue
        }
        if {[string first {AUDIO_LOCALIZER_OWNER schema=1 } $line] != 0} {
            incr owner_index
            continue
        }
        set kv [audio_kv_line $line]
        if {![dict exists $kv phase]} {
            lappend errors "owner-row-without-phase"
            continue
        }
        set phase [dict get $kv phase]
        if {[dict exists $phases $phase]} {
            lappend errors "owner-duplicate-$phase"
            continue
        }
        dict set phases $phase $kv
        dict set phase_positions $phase $owner_index
        incr owner_index
    }
    foreach phase {identity terminate_identity signal terminate_result} {
        if {![dict exists $phases $phase]} {
            lappend errors "owner-missing-$phase"
        }
    }
    if {[dict size $phase_positions] == 4} {
        set previous -1
        foreach phase {identity terminate_identity signal terminate_result} {
            set current [dict get $phase_positions $phase]
            if {$current <= $previous} {
                lappend errors "owner-phase-order-$phase"
                break
            }
            set previous $current
        }
    }
    if {[llength $errors] == 0} {
        set pid [dict get $record pid]
        set starttime [dict get $record starttime]
        foreach phase {identity terminate_identity} {
            set row [dict get $phases $phase]
            foreach {key wanted} [list pid $pid pgid $pid starttime $starttime \
                recorded_starttime $starttime expected $expected \
                record $record_path verdict PASS errno 0] {
                if {![dict exists $row $key] || [dict get $row $key] ne $wanted} {
                    lappend errors "owner-$phase-$key"
                }
            }
            if {![dict exists $row exe] ||
                ![string match "*/$expected" [dict get $row exe]]} {
                lappend errors "owner-$phase-exe"
            }
        }
        set signal [dict get $phases signal]
        foreach {key wanted} [list signal TERM pid $pid pgid $pid \
            starttime $starttime ret 0 errno 0] {
            if {![dict exists $signal $key] || [dict get $signal $key] ne $wanted} {
                lappend errors "owner-signal-$key"
            }
        }
        set terminal [dict get $phases terminate_result]
        foreach {key wanted} [list pid $pid pgid $pid starttime $starttime \
            live 0 verdict PASS] {
            if {![dict exists $terminal $key] || [dict get $terminal $key] ne $wanted} {
                lappend errors "owner-result-$key"
            }
        }
    }
    return [dict create valid [expr {[llength $errors] == 0}] errors $errors \
        pid [expr {[dict exists $record pid] ? [dict get $record pid] : -1}] \
        starttime [expr {[dict exists $record starttime] ? [dict get $record starttime] : -1}]]
}

proc audio_chromium_contract {launcher trace owner owner_record manifest} {
    set errors {}
    set max_time -1.0
    set trace_result [audio_trace_contract $trace]

    if {[regexp {wayland-chromium-launcher: argv_[0-9]+="--disable-audio-output"} $launcher]} {
        lappend errors "audio-disable-present"
    }
    foreach forbidden {--disable-gpu --in-process-gpu --single-process --no-zygote} {
        set escaped [string map {- {\-}} $forbidden]
        if {[regexp "wayland-chromium-launcher: argv_\\d+=\"$escaped\"" $launcher]} {
            lappend errors "forbidden-argv-$forbidden"
        }
    }
    if {![regexp {wayland-chromium-launcher: argv_[0-9]+="file:///share/chromium-audio-smoke.html"} $launcher]} {
        lappend errors "smoke-url-missing"
    }
    if {![regexp {wayland-chromium-launcher: env LD_PRELOAD="/opt/host-gui/wayland-chromium/lib/chromium-pulse-trace-preload.so"} $launcher]} {
        lappend errors "trace-preload-env-missing"
    }
    if {![regexp {CHROMIUM-AUDIO-SMOKE playing state=running} $launcher]} {
        lappend errors "smoke-playing-missing"
    }
    foreach line [split $launcher "\n"] {
        if {[regexp {CHROMIUM-AUDIO-SMOKE tick=[0-9]+ currentTime=([0-9]+\.[0-9]+) state=running} $line -> current] &&
            [string is double -strict $current] && $current > $max_time} {
            set max_time $current
        }
    }
    if {$max_time < 20.0} { lappend errors "smoke-lifecycle-short-$max_time" }
    if {[regexp -nocase {CHROMIUM-AUDIO-SMOKE error|Error during independent playback|OnError\(|AUDIO_RENDERER_ERROR|Out of memory|fatal page fault|PANIC|segmentation fault} $launcher]} {
        lappend errors "smoke-error-marker"
    }
    if {![dict get $trace_result valid]} {
        lappend errors "trace-[join [dict get $trace_result errors] :]"
    }
    if {[dict exists $trace_result primary_failures] &&
        [dict get $trace_result primary_failures] > 0} {
        lappend errors "trace-primary-playback-failure"
    }
    if {[dict exists $trace_result teardown_failures] &&
        [dict get $trace_result teardown_failures] > 0} {
        lappend errors "trace-teardown-failure-[dict get $trace_result teardown_failures]"
    }
    set owner_result [audio_owner_contract $owner $owner_record chrome \
        /audio-localizer/chromium.owner]
    if {![dict get $owner_result valid]} {
        lappend errors "chromium-owner-[join [dict get $owner_result errors] :]"
    }
    set parsed_manifest [audio_parse_manifest $manifest]
    if {[dict get $parsed_manifest valid]} {
        set identity_rc [audio_step_value $parsed_manifest chromium_identity rc]
        set terminate_rc [audio_step_value $parsed_manifest chromium_owned_cleanup terminate_rc]
        set wait_rc [audio_step_value $parsed_manifest chromium_owned_cleanup wait_rc]
        set start_pid [audio_step_value $parsed_manifest chromium_start pid]
        set identity_pid [audio_step_value $parsed_manifest chromium_identity pid]
        set cleanup_pid [audio_step_value $parsed_manifest chromium_owned_cleanup pid]
        set helper_identity [expr {[dict exists $parsed_manifest helper chromium_identity_rc] ?
            [dict get $parsed_manifest helper chromium_identity_rc] : "missing"}]
        if {$identity_rc ne "0" || $terminate_rc ne "0" ||
            $helper_identity ne "0" || $wait_rc ni {137 143} ||
            $start_pid ne $identity_pid || $start_pid ne $cleanup_pid ||
            ($start_pid ne [dict get $owner_result pid]) ||
            ([dict exists $trace_result session_pid] &&
             [dict get $trace_result session_pid] ne $start_pid)} {
            lappend errors "chromium-cleanup-rc"
        }
    }
    return [dict create valid [expr {[llength $errors] == 0}] errors $errors \
        max_time $max_time trace $trace_result owner $owner_result]
}

proc audio_semantic_primary {socket snapshots} {
    set errors {}

    if {![regexp {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xdg-runtime-root/pulse/native lstat_ret=0 errno=0 type=socket} $socket] ||
        ![regexp {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xdg-runtime-root/pulse/native connect_ret=0 errno=0 verdict=PASS} $socket]} {
        lappend errors "primary-socket"
    }
    if {![regexp -nocase {Server Name:[ \t]+PulseAudio \(on PipeWire} $snapshots]} {
        lappend errors "pipewire-pulse-server"
    }
    if {![regexp {Default Sink:[ \t]+xv6_null_output} $snapshots]} {
        lappend errors "default-sink"
    }
    if {![regexp {(^|[\r\n])[^\r\n]*xv6_null_output[^\r\n]*($|[\r\n])} $snapshots]} {
        lappend errors "sink-xv6-null-output"
    }
    return [dict create valid [expr {[llength $errors] == 0}] errors $errors]
}

proc audio_identity_contract {text} {
    set errors {}
    set rows [dict create]
    foreach line [split $text "\n"] {
        if {[string first {identity schema=1 } $line] != 0} { continue }
        set kv [audio_kv_line $line]
        if {![dict exists $kv name]} {
            lappend errors "identity-name-missing"
            continue
        }
        set name [dict get $kv name]
        if {[dict exists $rows $name]} {
            lappend errors "identity-duplicate-$name"
            continue
        }
        dict set rows $name $kv
    }
    foreach name {reducer_provider reducer_launcher pulse_trace pulse_provider audio_smoke guest_helper} {
        if {![dict exists $rows $name]} {
            lappend errors "identity-missing-$name"
            continue
        }
        set row [dict get $rows $name]
        if {![dict exists $row status] || [dict get $row status] ne "PASS" ||
            ![dict exists $row image_sha] ||
            ![regexp {^[0-9a-f]{64}$} [dict get $row image_sha]] ||
            ![dict exists $row expected_count] ||
            ![string is integer -strict [dict get $row expected_count]] ||
            [dict get $row expected_count] < 1 ||
            ![dict exists $row expected]} {
            lappend errors "identity-malformed-$name"
        }
        if {$name in {reducer_provider reducer_launcher pulse_trace pulse_provider}} {
            if {![dict exists $row image_build_id] ||
                ![regexp {^(?:[0-9a-f]{2}){8,64}$} [dict get $row image_build_id]]} {
                lappend errors "identity-build-id-$name"
            }
        } elseif {![dict exists $row image_build_id] ||
                  [dict get $row image_build_id] ne "not-applicable"} {
                lappend errors "identity-unexpected-build-id-$name"
        }
        if {$name eq "pulse_provider" &&
            [dict exists $row image_build_id] &&
            [dict get $row image_build_id] ne
                "5a42eddd12bc34f26064eb65a146075cbea16a8c"} {
            lappend errors "identity-provider-build-id"
        }
        if {[dict exists $row expected] &&
            [dict exists $row expected_count] &&
            [string is integer -strict [dict get $row expected_count]] &&
            [dict exists $row image_sha] &&
            [dict exists $row image_build_id]} {
            set expected_entries [split [dict get $row expected] ,]
            if {[llength $expected_entries] != [dict get $row expected_count]} {
                lappend errors "identity-expected-count-$name"
            }
            foreach entry $expected_entries {
                if {![regexp {^([^:]+):([0-9a-f]{64}):([^:]+)$} $entry -> \
                        expected_path expected_sha expected_build_id]} {
                    lappend errors "identity-expected-shape-$name"
                    continue
                }
                if {$expected_path eq "" ||
                    $expected_sha ne [dict get $row image_sha] ||
                    $expected_build_id ne [dict get $row image_build_id]} {
                    lappend errors "identity-expected-mismatch-$name"
                }
            }
        }
    }
    return [dict create valid [expr {[llength $errors] == 0}] errors $errors]
}

proc audio_localizer_classify {artifacts} {
    set errors {}
    foreach required {manifest snapshots primary_socket reducer_primary launcher trace chromium_owner chromium_owner_record provider_identity pipewire_quantum pulse_quantum evidence} {
        if {![dict exists $artifacts $required]} {
            lappend errors "artifact-key-$required"
        }
    }
    if {[llength $errors] > 0} {
        return [dict create valid 0 class INVALID errors $errors no_youtube_claim 1]
    }
    set manifest [dict get $artifacts manifest]
    set parsed [audio_parse_manifest $manifest]
    if {![dict get $parsed valid]} {
        lappend errors {*}[dict get $parsed errors]
    } else {
        lappend errors {*}[audio_reconcile_helper $parsed]
    }
    set semantic [audio_semantic_primary [dict get $artifacts primary_socket] \
        [dict get $artifacts snapshots]]
    set identity [audio_identity_contract [dict get $artifacts provider_identity]]
    if {![dict get $identity valid]} {
        lappend errors "identity-[join [dict get $identity errors] :]"
    }
    if {![regexp {(^|[\r\n])[ \t]*default\.clock\.min-quantum[ \t]*=[ \t]*[1-9][0-9]*([ \t]|$)} \
            [dict get $artifacts pipewire_quantum]]} {
        lappend errors "pipewire-min-quantum-evidence"
    }
    if {![regexp {(^|[\r\n])[ \t]*pulse\.min\.quantum[ \t]*=[ \t]*[1-9][0-9]*/48000([ \t]|$)} \
            [dict get $artifacts pulse_quantum]]} {
        lappend errors "pulse-min-quantum-evidence"
    }
    set snapshots [dict get $artifacts snapshots]
    if {![regexp {(?s)AUDIO_LOCALIZER_FINAL_PROCESS_BEGIN schema=1(.*?)AUDIO_LOCALIZER_FINAL_PROCESS_END schema=1} $snapshots -> final_processes]} {
        lappend errors "final-process-snapshot-missing"
    } elseif {[regexp -nocase {(^|[\r\n])[^\r\n]*(chrome|pulseaudio)[^\r\n]*($|[\r\n])} $final_processes]} {
        lappend errors "final-owned-process-residue"
    }
    set reducer_primary [audio_reducer_contract [dict get $artifacts reducer_primary]]
    if {![dict get $reducer_primary valid]} {
        lappend errors "primary-reducer-[join [dict get $reducer_primary errors] :]"
    }
    set chromium [audio_chromium_contract [dict get $artifacts launcher] \
        [dict get $artifacts trace] [dict get $artifacts chromium_owner] \
        [dict get $artifacts chromium_owner_record] $manifest]
    if {![dict get [dict get $chromium trace] valid]} {
        lappend errors "chromium-trace-causal-contract"
    }
    if {![dict get [dict get $chromium owner] valid]} {
        lappend errors "chromium-owner-causal-contract"
    }
    set evidence [dict get $artifacts evidence]
    if {[regexp -nocase {PANIC|panic:|fatal page fault|spin_lock reentry|kernel trap|KERNEL PAGE FAULT|coredump: generating|Out of memory|segmentation fault} $evidence]} {
        lappend errors "crash-or-oom-marker"
    }
    if {[regexp -nocase {(^|[^A-Za-z0-9_])(llvmpipe|softpipe|swrast|kms_swrast|drisw|SwiftShader)([^A-Za-z0-9_]|$)|LIBGL_ALWAYS_SOFTWARE=(1|true|yes|on)} $evidence]} {
        lappend errors "software-renderer-marker"
    }
    if {[regexp {argv_[0-9]+="--disable-audio-output"} $evidence]} {
        lappend errors "audio-disable-present-anywhere"
    }
    if {[llength $errors] > 0} {
        return [dict create valid 0 class INVALID errors $errors \
            semantic $semantic reducer_primary $reducer_primary chromium $chromium \
            no_youtube_claim 1]
    }

    set pw_rc [audio_step_value $parsed pw_play_primary rc]
    set paplay_rc [audio_step_value $parsed paplay_primary rc]
    set reducer_rc [audio_step_value $parsed reducer_primary rc]
    foreach {name value} [list pw_play_primary $pw_rc paplay_primary $paplay_rc reducer_primary $reducer_rc] {
        if {![audio_nonnegative_int $value]} {
            return [dict create valid 0 class INVALID errors [list "invalid-rc-$name-$value"] no_youtube_claim 1]
        }
    }
    if {![dict get $semantic valid] || $pw_rc != 0} {
        return [dict create valid 1 class SUBSTRATE \
            detail "semantic=[join [dict get $semantic errors] :] pw_play_rc=$pw_rc" \
            no_youtube_claim 1]
    }
    if {$paplay_rc != 0} {
        set fallback_ready [audio_step_value $parsed server_selection fallback_ready]
        set fallback_paplay [audio_step_value $parsed server_selection paplay_fallback_rc]
        set fallback_reducer [audio_step_value $parsed server_selection reducer_fallback_rc]
        set fallback_errors {}
        foreach step {fallback_start fallback_ready fallback_owned_cleanup} {
            if {![dict exists $parsed steps $step]} {
                lappend fallback_errors "missing-step-$step"
            }
        }
        if {![dict exists $artifacts fallback_owner] ||
            ![dict exists $artifacts fallback_owner_record]} {
            lappend fallback_errors "missing-fallback-owner-artifact"
        } else {
            set fallback_owner [dict get $artifacts fallback_owner]
            set fallback_owner_result [audio_owner_contract $fallback_owner \
                [dict get $artifacts fallback_owner_record] pulseaudio \
                /audio-localizer/fallback.owner]
            if {![dict get $fallback_owner_result valid]} {
                lappend fallback_errors \
                    "fallback-owner-[join [dict get $fallback_owner_result errors] :]"
            }
        }
        if {[dict exists $parsed steps fallback_owned_cleanup] &&
            ([audio_step_value $parsed fallback_owned_cleanup terminate_rc] ne "0" ||
             [audio_step_value $parsed fallback_owned_cleanup wait_rc] ni {137 143} ||
             [audio_step_value $parsed fallback_start pid] ne
                [audio_step_value $parsed fallback_ready pid] ||
             [audio_step_value $parsed fallback_start pid] ne
                [audio_step_value $parsed fallback_owned_cleanup pid])} {
            lappend fallback_errors "fallback-cleanup-rc"
        }
        if {[llength $fallback_errors] > 0} {
            return [dict create valid 0 class INVALID errors $fallback_errors no_youtube_claim 1]
        }
        if {$fallback_ready eq "1" && $fallback_paplay eq "0" &&
            $fallback_reducer eq "0" && [dict exists $artifacts reducer_fallback] &&
            [dict exists $artifacts fallback_socket]} {
            set fallback_contract [audio_reducer_contract [dict get $artifacts reducer_fallback]]
            set fallback_socket [dict get $artifacts fallback_socket]
            if {[dict get $fallback_contract valid] && [dict get $fallback_contract healthy] &&
                [regexp {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xv6-audio-localizer-pulse/native lstat_ret=0 errno=0 type=socket} $fallback_socket] &&
                [regexp {AUDIO_LOCALIZER_SOCKET schema=1 path=/dev/shm/xv6-audio-localizer-pulse/native connect_ret=0 errno=0 verdict=PASS} $fallback_socket]} {
                return [dict create valid 1 class SERVER \
                    detail "primary-paplay-failed fallback-paplay-and-reducer-pass" \
                    no_youtube_claim 1]
            }
        }
        return [dict create valid 1 class SUBSTRATE \
            detail "primary-paplay-failed fallback-not-proven" no_youtube_claim 1]
    }
    if {$reducer_rc != 0 || ![dict get $reducer_primary healthy]} {
        return [dict create valid 1 class NEGOTIATION \
            detail "paplay-pass chromium-shaped-reducer-fail reason=[dict get $reducer_primary reason]" \
            no_youtube_claim 1]
    }
    if {![dict get $chromium valid]} {
        return [dict create valid 1 class CHROMIUM_LIFECYCLE \
            detail [join [dict get $chromium errors] :] no_youtube_claim 1]
    }
    return [dict create valid 1 class HEALTHY \
        detail "substrate-server-reducer-chromium-20s-pass" no_youtube_claim 1]
}
