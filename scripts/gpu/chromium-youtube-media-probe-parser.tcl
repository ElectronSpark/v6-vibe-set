# Strict host-side parser and reducer fixtures for the default-off YouTube
# media probe. JavaScript emits raw facts only; all semantic credit lives here.

proc media_probe_byte_length {text} {
    return [string length [encoding convertto utf-8 $text]]
}

proc media_probe_is_uint {value} {
    return [regexp {^(0|[1-9][0-9]*)$} $value]
}

proc media_probe_is_sint {value} {
    return [regexp {^-?(0|[1-9][0-9]*)$} $value]
}

proc media_probe_is_decimal {value} {
    if {![regexp {^-?(0|[1-9][0-9]*)(\.[0-9]+)?$} $value]} { return 0 }
    if {[regexp {^-0(\.0+)?$} $value]} { return 0 }
    return 1
}

proc media_probe_in_range {value low high} {
    return [expr {[media_probe_is_decimal $value] &&
                  double($value) >= double($low) &&
                  double($value) <= double($high)}]
}

proc media_probe_expected_keys {kind} {
    switch -- $kind {
        force_ready {
            return {
                kind schema nonce attempt player_present video_present
                player_state_api player_state ready_state video_width
                video_height selected_api available_api selected available
                range_api quality_api
            }
        }
        force_attempt {
            return {
                kind schema nonce seq api arg_min arg_max present
            }
        }
        force_result {
            return {
                kind schema nonce seq api invoked threw return_type
            }
        }
        force_observation {
            return {
                kind schema nonce attempt stable_count selected_api
                available_api selected available video_width video_height
                ready_state paused ended current_time_first current_time_last
            }
        }
        start {
            return {kind schema nonce samples interval_ms rvfc_available src_id}
        }
        sample {
            return {
                kind schema nonce index video_width video_height src_id
                current_time playback_rate paused ended quality_selected
                quality_available quality_selected_api quality_available_api
                ready_state network_state buffered_ahead vpq_available vpq_total
                vpq_dropped webkit_available webkit_decoded webkit_dropped
                rvfc_available rvfc_callbacks rvfc_presented rvfc_media_time
                longtask_available longtask_count longtask_duration_ms
            }
        }
        summary {
            return {
                kind schema nonce samples width_min width_max height_min
                height_max src_id quality_selected quality_available
                quality_selected_api quality_available_api vpq_available
                vpq_total_first vpq_total_last vpq_total_delta
                vpq_dropped_first vpq_dropped_last vpq_dropped_delta
                webkit_available webkit_decoded_first webkit_decoded_last
                webkit_decoded_delta webkit_dropped_first webkit_dropped_last
                webkit_dropped_delta longtask_available longtask_count
                longtask_duration_ms
            }
        }
        rvfc_summary {
            return {
                kind schema nonce available callbacks schedule_failures
                warmup_callbacks interval_slots presented_first presented_last
                presented_delta presented_invalid presented_pair_invalid
                presented_regressions presented_duplicates media_first media_last
                media_delta media_invalid interval_count interval_valid
                interval_invalid interval_positive interval_regressions
                interval_duplicates interval_sum_ms median_ms near60_count hist
            }
        }
        done { return {kind schema nonce status samples rows} }
        failure { return {kind schema nonce reason} }
        default { return {} }
    }
}

proc media_probe_exact_keys {fields kind} {
    set expected [lsort [media_probe_expected_keys $kind]]
    return [expr {$expected ne {} && [lsort [dict keys $fields]] eq $expected}]
}

proc media_probe_row_fields {row expected_nonce} {
    set fields [dict create]
    if {[media_probe_byte_length $row] > 1000} {
        return [list 0 "row-byte-cap-exceeded" $fields]
    }
    set words [split $row " "]
    if {[lindex $words 0] ne "YT_MEDIA_PROBE_V1"} {
        return [list 0 "marker-missing" $fields]
    }
    foreach word [lrange $words 1 end] {
        if {![regexp {^([a-z][a-z0-9_]*)=([^ ]+)$} $word -> key value]} {
            return [list 0 "invalid-field-$word" $fields]
        }
        if {[dict exists $fields $key]} {
            return [list 0 "duplicate-field-$key" $fields]
        }
        dict set fields $key $value
    }
    if {![dict exists $fields schema] || [dict get $fields schema] ne "1" ||
        ![dict exists $fields kind]} {
        return [list 0 "schema-or-kind-missing" $fields]
    }
    set kind [dict get $fields kind]
    if {![media_probe_exact_keys $fields $kind]} {
        return [list 0 "exact-key-set-mismatch-$kind" $fields]
    }
    if {![dict exists $fields nonce] ||
        ![regexp {^[0-9a-f]{32}$} [dict get $fields nonce]] ||
        [dict get $fields nonce] ne $expected_nonce} {
        return [list 0 "nonce-mismatch" $fields]
    }
    return [list 1 "pass" $fields]
}

proc media_probe_enveloped_rows {text expected_nonce extension_id {row_cap 24}} {
    if {$row_cap ni {24 30}} {
        return [list 0 "invalid-row-cap-$row_cap" {}]
    }
    set rows {}
    set total_bytes 0
    set qid [regex_quote $extension_id]
    set pattern [format {^\[([1-9][0-9]*):([1-9][0-9]*):([0-9]{4}/[0-9]{6}\.[0-9]{6}):INFO:CONSOLE:([0-9]+)\] "(YT_MEDIA_PROBE_V1 [^"]+)", source: chrome-extension://%s/probe\.js \(([0-9]+)\)$} $qid]
    set marker_lines 0
    foreach raw_line [split $text "\n"] {
        set line [string trimright $raw_line "\r"]
        if {[string first "YT_MEDIA_PROBE_V1 " $line] < 0} { continue }
        incr marker_lines
        if {![regexp $pattern $line -> pid tid stamp console_line payload source_line]} {
            return [list 0 "untrusted-marker-envelope" {}]
        }
        if {$console_line ne $source_line} {
            return [list 0 "console-source-line-mismatch" {}]
        }
        set parsed [media_probe_row_fields $payload $expected_nonce]
        if {![lindex $parsed 0]} {
            return [list 0 "row-parse-[lindex $parsed 1]" {}]
        }
        incr total_bytes [media_probe_byte_length $payload]
        lappend rows [list $payload [lindex $parsed 2]]
    }
    if {$marker_lines == 0} {
        return [list 0 "injection-no-marker-rows" {}]
    }
    if {$marker_lines > $row_cap} {
        return [list 0 "row-cap-exceeded-$marker_lines" {}]
    }
    if {$total_bytes > $row_cap * 1000} {
        return [list 0 "total-byte-cap-exceeded-$total_bytes" {}]
    }
    return [list 1 "pass" $rows]
}

proc media_probe_source_identity_valid {identity} {
    if {[media_probe_byte_length $identity] < 1 ||
        [media_probe_byte_length $identity] > 160} {
        return 0
    }
    if {![regexp {^[A-Za-z0-9._~:/<>,+%-]+$} $identity]} { return 0 }
    if {[regexp -nocase {[?&=]|token|signature|expire|sig[_-]} $identity]} {
        return 0
    }
    return 1
}

proc media_probe_quality_token_valid {quality} {
    return [expr {$quality in {
        unavailable empty auto tiny small medium large hd720 hd1080 hd1440
        hd2160 highres
    }}]
}

proc media_probe_quality_available_valid {value api_available} {
    if {!$api_available} { return [expr {$value eq "unavailable"}] }
    if {$value eq "empty"} { return 1 }
    if {[media_probe_byte_length $value] > 192} { return 0 }
    set seen {}
    set levels [split $value ,]
    if {[llength $levels] < 1 || [llength $levels] > 16} { return 0 }
    foreach level $levels {
        if {![media_probe_quality_token_valid $level] ||
            $level in {unavailable empty} || $level in $seen} {
            return 0
        }
        lappend seen $level
    }
    return 1
}

proc media_probe_histogram {text} {
    set expected {
        regress duplicate lt8 b8_12 b12_14 b14_15 b15_18p5 b18p5_20
        b20_28 b28_40 b40_80 ge80
    }
    set values [dict create]
    foreach item [split $text ,] {
        if {![regexp {^([a-z0-9_]+):([0-9]+)$} $item -> key value] ||
            $key ni $expected || [dict exists $values $key] ||
            ![media_probe_is_uint $value]} {
            return [list 0 "invalid-histogram-item-$item" $values]
        }
        dict set values $key $value
    }
    if {[lsort [dict keys $values]] ne [lsort $expected]} {
        return [list 0 "histogram-key-set-mismatch" $values]
    }
    return [list 1 "pass" $values]
}

proc media_probe_histogram_rank_bin {hist rank} {
    # key, lower bound, lower inclusive, upper bound, upper inclusive.
    set bins {
        {lt8 0 0 8 0}
        {b8_12 8 1 12 0}
        {b12_14 12 1 14 0}
        {b14_15 14 1 15 0}
        {b15_18p5 15 1 18.5 1}
        {b18p5_20 18.5 0 20 0}
        {b20_28 20 1 28 0}
        {b28_40 28 1 40 0}
        {b40_80 40 1 80 0}
        {ge80 80 1 {} 0}
    }
    set cumulative 0
    foreach bin $bins {
        incr cumulative [dict get $hist [lindex $bin 0]]
        if {$rank <= $cumulative} { return $bin }
    }
    return {}
}

proc media_probe_histogram_median_valid {hist positive_count median} {
    if {$positive_count == 0} {
        return [expr {double($median) == -1.0}]
    }
    if {double($median) <= 0.0} { return 0 }
    set lower_rank [expr {($positive_count + 1) / 2}]
    set upper_rank [expr {($positive_count + 2) / 2}]
    set lower_bin [media_probe_histogram_rank_bin $hist $lower_rank]
    set upper_bin [media_probe_histogram_rank_bin $hist $upper_rank]
    if {$lower_bin eq {} || $upper_bin eq {}} { return 0 }

    set feasible_low [expr {(double([lindex $lower_bin 1]) +
        double([lindex $upper_bin 1])) / 2.0}]
    set low_inclusive [expr {[lindex $lower_bin 2] && [lindex $upper_bin 2]}]
    set value [expr {double($median)}]
    # median_ms is serialized with three fractional digits. Treat it as the
    # exact half-ULP interval that could have produced that token; do not move
    # counts between histogram bins.
    set token_low [expr {$value - 0.0005}]
    set token_high [expr {$value + 0.0005}]
    if {$token_high < $feasible_low ||
        (!$low_inclusive && $token_high == $feasible_low)} {
        return 0
    }

    set lower_high [lindex $lower_bin 3]
    set upper_high [lindex $upper_bin 3]
    if {$lower_high ne {} && $upper_high ne {}} {
        set feasible_high [expr {(double($lower_high) + double($upper_high)) / 2.0}]
        set high_inclusive [expr {
            [lindex $lower_bin 4] && [lindex $upper_bin 4]}]
        if {$token_low > $feasible_high ||
            (!$high_inclusive && $token_low == $feasible_high)} {
            return 0
        }
    }
    return 1
}

proc media_probe_histogram_sum_valid {hist interval_sum_ms} {
    # interval_sum_ms is also serialized to three fractional digits. For
    # regression-free evidence, its half-ULP interval must intersect the sum
    # range implied by the positive histogram. Duplicate intervals add zero.
    if {[dict get $hist regress] > 0} { return 1 }
    set bins {
        {lt8 0 0 8 0}
        {b8_12 8 1 12 0}
        {b12_14 12 1 14 0}
        {b14_15 14 1 15 0}
        {b15_18p5 15 1 18.5 1}
        {b18p5_20 18.5 0 20 0}
        {b20_28 20 1 28 0}
        {b28_40 28 1 40 0}
        {b40_80 40 1 80 0}
        {ge80 80 1 {} 0}
    }
    set feasible_low 0.0
    set feasible_high 0.0
    set low_inclusive 1
    set high_inclusive 1
    set high_unbounded 0
    foreach bin $bins {
        set count [dict get $hist [lindex $bin 0]]
        if {$count == 0} { continue }
        set feasible_low [expr {$feasible_low +
            $count * double([lindex $bin 1])}]
        if {![lindex $bin 2]} { set low_inclusive 0 }
        if {[lindex $bin 3] eq {}} {
            set high_unbounded 1
        } elseif {!$high_unbounded} {
            set feasible_high [expr {$feasible_high +
                $count * double([lindex $bin 3])}]
            if {![lindex $bin 4]} { set high_inclusive 0 }
        }
    }
    set value [expr {double($interval_sum_ms)}]
    set token_low [expr {$value - 0.0005}]
    set token_high [expr {$value + 0.0005}]
    if {$token_high < $feasible_low ||
        (!$low_inclusive && $token_high == $feasible_low)} {
        return 0
    }
    if {!$high_unbounded &&
        ($token_low > $feasible_high ||
         (!$high_inclusive && $token_low == $feasible_high))} {
        return 0
    }
    return 1
}

# rVFC delivery to JavaScript may be coalesced while the main world is busy:
# metadata.presentedFrames and metadata.mediaTime then advance by more than one
# even though only one callback is delivered.  Bound those jumps against media
# time, not against an arbitrary multiple of delivered callback slots.  75 fps
# is a deliberately conservative ceiling for a source whose separate cadence
# gate requires approximately 60 fps; the small fixed slack covers independent
# sample reads at either edge of a one-second observation window.
proc media_probe_frame_window_cap {media_delta {slack 8}} {
    if {![media_probe_is_decimal $media_delta] ||
        double($media_delta) < 0.0 || ![media_probe_is_uint $slack]} {
        return -1
    }
    return [expr {wide(ceil(75.0 * double($media_delta))) + $slack}]
}

proc media_probe_sample_rvfc_consistency {samples rvfc} {
    set max_safe 9007199254740991
    foreach key {
        callbacks schedule_failures warmup_callbacks interval_slots
        presented_invalid presented_pair_invalid presented_regressions
        presented_duplicates media_invalid interval_count interval_valid
        interval_invalid interval_positive interval_regressions
        interval_duplicates near60_count
    } {
        if {[dict get $rvfc $key] > $max_safe} {
            return [list 0 "rvfc-range-$key"]
        }
    }
    foreach key {presented_first presented_last presented_delta} {
        set value [dict get $rvfc $key]
        if {$value < -1 || $value > $max_safe} {
            return [list 0 "rvfc-range-$key"]
        }
    }
    set callbacks [dict get $rvfc callbacks]
    set slots [dict get $rvfc interval_slots]
    set causal_clean [expr {
        [dict get $rvfc presented_invalid] == 0 &&
        [dict get $rvfc presented_pair_invalid] == 0 &&
        [dict get $rvfc presented_regressions] == 0 &&
        [dict get $rvfc presented_duplicates] == 0 &&
        [dict get $rvfc media_invalid] == 0 &&
        [dict get $rvfc interval_invalid] == 0 &&
        [dict get $rvfc interval_regressions] == 0 &&
        [dict get $rvfc interval_duplicates] == 0}]
    if {[dict get $rvfc presented_invalid] > $callbacks ||
        [dict get $rvfc media_invalid] > $callbacks ||
        [dict get $rvfc presented_pair_invalid] +
            [dict get $rvfc presented_regressions] +
            [dict get $rvfc presented_duplicates] > $slots} {
        return [list 0 "rvfc-counter-range"]
    }

    set observed_presented_invalid_min 0
    set observed_presented_pair_invalid_min 0
    set observed_presented_regression 0
    set observed_presented_nonincrease 0
    set observed_media_invalid_min 0
    set observed_interval_invalid_min 0
    set observed_media_regression 0
    set observed_media_nonincrease 0
    set retained_media_flat_callbacks 0
    set retained_media_flat_exits 0
    set retained_media_flat_pending 0
    set retained_media_flat_exit_regression 0
    set prior {}
    foreach sample $samples {
        set sample_index [dict get $sample index]
        set sample_callbacks [dict get $sample rvfc_callbacks]
        set presented [dict get $sample rvfc_presented]
        set media [expr {double([dict get $sample rvfc_media_time])}]
        if {$sample_callbacks > $max_safe || $presented > $max_safe} {
            return [list 0 "sample-$sample_index-rvfc-range"]
        }
        if {$sample_callbacks == 0} {
            if {$presented != -1 || $media != -1.0} {
                return [list 0 "sample-$sample_index-rvfc-zero-callback-shape"]
            }
        } else {
            if {$presented == -1} {
                set observed_presented_invalid_min [expr {max(
                    $observed_presented_invalid_min, $sample_callbacks)}]
                set observed_presented_pair_invalid_min [expr {max(
                    $observed_presented_pair_invalid_min,
                    $sample_callbacks - 1)}]
            }
            if {$media == -1.0} {
                set observed_media_invalid_min [expr {max(
                    $observed_media_invalid_min, $sample_callbacks)}]
                set observed_interval_invalid_min [expr {max(
                    $observed_interval_invalid_min, $sample_callbacks - 1)}]
            }
        }

        if {$prior ne {}} {
            set prior_callbacks [dict get $prior rvfc_callbacks]
            set prior_presented [dict get $prior rvfc_presented]
            set prior_media [expr {double([dict get $prior rvfc_media_time])}]
            set callback_delta [expr {$sample_callbacks - $prior_callbacks}]
            if {$callback_delta == 0} {
                if {$presented != $prior_presented || $media != $prior_media} {
                    return [list 0 "sample-$sample_index-rvfc-change-without-callback"]
                }
            } else {
                if {$prior_presented >= 0 && $presented == -1} {
                    return [list 0 "sample-$sample_index-presented-last-valid-lost"]
                }
                if {$prior_media >= 0.0 && $media == -1.0} {
                    return [list 0 "sample-$sample_index-media-last-valid-lost"]
                }
                if {$prior_presented == -1 && $presented >= 0 &&
                    $prior_callbacks > 0} {
                    set observed_presented_pair_invalid_min [expr {max(
                        $observed_presented_pair_invalid_min,
                        $prior_callbacks)}]
                } elseif {$prior_presented >= 0 && $presented >= 0} {
                    set presented_step [expr {$presented - $prior_presented}]
                    if {$presented_step < 0} {
                        set observed_presented_regression 1
                    } elseif {$presented_step < $callback_delta} {
                        set observed_presented_nonincrease 1
                    }
                }
                if {$prior_media >= 0.0 && $media >= 0.0} {
                    if {$media == $prior_media} {
                        incr retained_media_flat_callbacks $callback_delta
                        set retained_media_flat_pending 1
                    } else {
                        if {$retained_media_flat_pending} {
                            incr retained_media_flat_exits
                            set retained_media_flat_pending 0
                            if {$media < $prior_media} {
                                set retained_media_flat_exit_regression 1
                            }
                        } elseif {$media < $prior_media} {
                            set observed_media_regression 1
                        }
                    }
                } elseif {$prior_media == -1.0 && $media >= 0.0 &&
                    $prior_callbacks > 0} {
                    set observed_interval_invalid_min [expr {max(
                        $observed_interval_invalid_min, $prior_callbacks)}]
                }

                # Reconcile each independently sampled window only when both
                # endpoints are valid.  Callback coalescing is allowed, but a
                # presentedFrames or VPQ jump that cannot fit in elapsed media
                # time is not.  When VPQ exists, presented progress must lie
                # between total-minus-dropped and total, modulo edge skew.
                if {$causal_clean &&
                    $prior_presented >= 0 && $presented >= 0 &&
                    $prior_media >= 0.0 && $media >= $prior_media} {
                    set media_step [expr {$media - $prior_media}]
                    set presented_step [expr {$presented - $prior_presented}]
                    set frame_cap [media_probe_frame_window_cap $media_step]
                    if {$presented_step >= 0 && $presented_step > $frame_cap} {
                        return [list 0 "sample-$sample_index-presented-media-jump"]
                    }
                    if {[dict get $sample vpq_available]} {
                        set total_step [expr {[dict get $sample vpq_total] -
                            [dict get $prior vpq_total]}]
                        set dropped_step [expr {[dict get $sample vpq_dropped] -
                            [dict get $prior vpq_dropped]}]
                        if {$total_step < 0 || $dropped_step < 0} {
                            return [list 0 "sample-$sample_index-vpq-regression"]
                        }
                        if {$total_step > $frame_cap ||
                            $dropped_step > $total_step + 8} {
                            return [list 0 "sample-$sample_index-vpq-media-contradiction"]
                        }
                        if {$presented_step > $total_step + 8 ||
                            $presented_step + $dropped_step + 8 < $total_step} {
                            return [list 0 "sample-$sample_index-presented-vpq-contradiction"]
                        }
                    }
                }
            }
        }
        set prior $sample
    }

    set first_presented [dict get $rvfc presented_first]
    set first_media [expr {double([dict get $rvfc media_first])}]
    foreach sample $samples {
        set sample_callbacks [dict get $sample rvfc_callbacks]
        if {$sample_callbacks == 0} { continue }
        set presented [dict get $sample rvfc_presented]
        set media [expr {double([dict get $sample rvfc_media_time])}]
        set sample_slots [expr {$sample_callbacks - 1}]
        if {$first_presented >= 0} {
            if {$presented < 0} {
                return [list 0 "sample-rvfc-presented-first-shape"]
            }
            set delta [expr {$presented - $first_presented}]
            if {$delta < 0} {
                set observed_presented_regression 1
            } elseif {$delta < $sample_slots} {
                set observed_presented_nonincrease 1
            }
            if {$causal_clean &&
                $first_media >= 0.0 && $media >= $first_media &&
                $delta > [media_probe_frame_window_cap \
                    [expr {$media - $first_media}]]} {
                return [list 0 "sample-rvfc-presented-first-media-jump"]
            }
        }
        if {$first_media >= 0.0} {
            if {$media < 0.0} {
                return [list 0 "sample-rvfc-media-first-shape"]
            } elseif {$media < $first_media} {
                set observed_media_regression 1
            } elseif {$sample_slots > 0 && $media == $first_media} {
                set observed_media_nonincrease 1
            }
        }
    }

    if {$retained_media_flat_callbacks > 0} {
        set flat_media_invalid_min [expr {$observed_media_invalid_min +
            $retained_media_flat_callbacks}]
        set flat_interval_invalid_min [expr {$observed_interval_invalid_min +
            $retained_media_flat_callbacks + $retained_media_flat_exits}]
        if {[dict get $rvfc media_invalid] >= $flat_media_invalid_min &&
            [dict get $rvfc interval_invalid] >= $flat_interval_invalid_min} {
            set observed_media_invalid_min $flat_media_invalid_min
            set observed_interval_invalid_min $flat_interval_invalid_min
        } else {
            set observed_media_nonincrease 1
            if {$retained_media_flat_exit_regression} {
                set observed_media_regression 1
            }
        }
    }

    if {[dict get $rvfc presented_invalid] <
        $observed_presented_invalid_min} {
        return [list 0 "sample-rvfc-presented-invalid-unreconciled"]
    }
    if {[dict get $rvfc presented_pair_invalid] <
        $observed_presented_pair_invalid_min} {
        return [list 0 "sample-rvfc-presented-pair-invalid-unreconciled"]
    }
    if {$observed_presented_regression &&
        [dict get $rvfc presented_regressions] == 0} {
        return [list 0 "sample-rvfc-presented-regression-unreconciled"]
    }
    if {$observed_presented_nonincrease &&
        [dict get $rvfc presented_duplicates] == 0 &&
        [dict get $rvfc presented_regressions] == 0 &&
        [dict get $rvfc presented_pair_invalid] == 0} {
        return [list 0 "sample-rvfc-presented-nonincreasing-unreconciled"]
    }
    if {[dict get $rvfc media_invalid] < $observed_media_invalid_min} {
        return [list 0 "sample-rvfc-media-invalid-unreconciled"]
    }
    if {[dict get $rvfc interval_invalid] < $observed_interval_invalid_min} {
        return [list 0 "sample-rvfc-interval-invalid-unreconciled"]
    }
    if {$observed_media_regression &&
        [dict get $rvfc interval_regressions] == 0} {
        return [list 0 "sample-rvfc-media-regression-unreconciled"]
    }
    if {$observed_media_nonincrease &&
        [dict get $rvfc interval_regressions] == 0 &&
        [dict get $rvfc interval_duplicates] <
            $retained_media_flat_callbacks} {
        return [list 0 "sample-rvfc-media-nonincreasing-unreconciled"]
    }


    # The full rVFC window includes callbacks before sample 1, so it can only
    # be reconciled directly to its own media-time window.  The sample-1 to
    # sample-20 subwindow has matching VPQ endpoints and therefore receives a
    # stronger total/dropped-counter cross-check.
    if {$causal_clean &&
        [dict get $rvfc presented_first] >= 0 &&
        [dict get $rvfc presented_delta] >= 0 &&
        double([dict get $rvfc media_delta]) >= 0.0 &&
        [dict get $rvfc presented_delta] >
            [media_probe_frame_window_cap [dict get $rvfc media_delta]]} {
        return [list 0 "rvfc-presented-media-window-contradiction"]
    }
    set first_sample [lindex $samples 0]
    set last_sample [lindex $samples end]
    if {$causal_clean &&
        [dict get $first_sample rvfc_presented] >= 0 &&
        [dict get $last_sample rvfc_presented] >= 0 &&
        double([dict get $first_sample rvfc_media_time]) >= 0.0 &&
        double([dict get $last_sample rvfc_media_time]) >=
            double([dict get $first_sample rvfc_media_time])} {
        set sample_presented_delta [expr {
            [dict get $last_sample rvfc_presented] -
            [dict get $first_sample rvfc_presented]}]
        set sample_media_delta [expr {
            double([dict get $last_sample rvfc_media_time]) -
            double([dict get $first_sample rvfc_media_time])}]
        if {$sample_presented_delta < 0 ||
            $sample_presented_delta >
                [media_probe_frame_window_cap $sample_media_delta 16]} {
            return [list 0 "sample-window-presented-media-contradiction"]
        }
        if {[dict get $first_sample vpq_available]} {
            set total_delta [expr {[dict get $last_sample vpq_total] -
                [dict get $first_sample vpq_total]}]
            set dropped_delta [expr {[dict get $last_sample vpq_dropped] -
                [dict get $first_sample vpq_dropped]}]
            if {$total_delta < 0 || $dropped_delta < 0 ||
                $total_delta >
                    [media_probe_frame_window_cap $sample_media_delta 16] ||
                $dropped_delta > $total_delta + 16 ||
                $sample_presented_delta > $total_delta + 16 ||
                $sample_presented_delta + $dropped_delta + 16 < $total_delta} {
                return [list 0 "sample-window-presented-vpq-contradiction"]
            }
        }
    }
    return [list 1 "pass"]
}

proc media_probe_counter_shape {available first last delta} {
    if {!$available} {
        return [expr {$first eq "-1" && $last eq "-1" && $delta eq "-1"}]
    }
    if {![media_probe_is_uint $first] || ![media_probe_is_uint $last] ||
        ![media_probe_is_uint $delta]} {
        return 0
    }
    return [expr {$last >= $first && $delta == ($last - $first)}]
}

proc media_probe_quality_contains {available quality} {
    return [expr {$available ne "unavailable" && $available ne "empty" &&
                  $quality in [split $available ,]}]
}

proc media_probe_validate_force_ready_row {ready} {
    foreach key {
        attempt player_present video_present player_state_api selected_api
        available_api range_api quality_api
    } {
        if {![media_probe_is_uint [dict get $ready $key]]} {
            return [list 0 "force-ready-grammar-$key"]
        }
    }
    foreach key {
        player_present video_present player_state_api selected_api
        available_api range_api quality_api
    } {
        if {[dict get $ready $key] ni {0 1}} {
            return [list 0 "force-ready-boolean-$key"]
        }
    }
    foreach key {player_state ready_state video_width video_height} {
        if {![media_probe_is_sint [dict get $ready $key]]} {
            return [list 0 "force-ready-grammar-$key"]
        }
    }
    if {[dict get $ready attempt] < 1 || [dict get $ready attempt] > 120 ||
        [dict get $ready player_present] ne "1" ||
        [dict get $ready video_present] ne "1" ||
        [dict get $ready player_state_api] ne "1" ||
        [dict get $ready player_state] ne "1" ||
        [dict get $ready ready_state] < 2 || [dict get $ready ready_state] > 4 ||
        [dict get $ready video_width] < 1 || [dict get $ready video_width] > 8192 ||
        [dict get $ready video_height] < 1 || [dict get $ready video_height] > 8192 ||
        [dict get $ready selected_api] ne "1" ||
        [dict get $ready available_api] ne "1" ||
        [dict get $ready range_api] ne "1" ||
        [dict get $ready quality_api] ne "1" ||
        ![media_probe_quality_token_valid [dict get $ready selected]] ||
        [dict get $ready selected] in {unavailable empty} ||
        ![media_probe_quality_available_valid [dict get $ready available] 1] ||
        ![media_probe_quality_contains [dict get $ready available] hd720]} {
        return [list 0 "force-ready-prerequisites"]
    }

    return [list 1 "pass"]
}

proc media_probe_validate_force_attempt_row {attempt seq api} {
    if {[dict get $attempt seq] ne $seq ||
        [dict get $attempt api] ne $api ||
        [dict get $attempt arg_min] ne "hd720" ||
        [dict get $attempt arg_max] ne "hd720" ||
        [dict get $attempt present] ne "1"} {
        return [list 0 "force-attempt-$seq-invalid"]
    }
    return [list 1 "pass"]
}

proc media_probe_validate_force_result_row {result seq api} {
    if {[dict get $result seq] ne $seq ||
        [dict get $result api] ne $api ||
        [dict get $result invoked] ne "1" ||
        [dict get $result threw] ne "0" ||
        [dict get $result return_type] ni {
            undefined boolean number string object function symbol bigint null
        }} {
        return [list 0 "force-result-$seq-invalid"]
    }
    return [list 1 "pass"]
}

proc media_probe_validate_force_observation_row {observation} {
    foreach key {attempt stable_count video_width video_height ready_state} {
        if {![media_probe_is_uint [dict get $observation $key]]} {
            return [list 0 "force-observation-grammar-$key"]
        }
    }
    foreach key {selected_api available_api paused ended} {
        if {[dict get $observation $key] ni {0 1}} {
            return [list 0 "force-observation-boolean-$key"]
        }
    }
    foreach key {current_time_first current_time_last} {
        if {![media_probe_in_range [dict get $observation $key] 0 100000000]} {
            return [list 0 "force-observation-range-$key"]
        }
    }
    if {[dict get $observation attempt] < 1 ||
        [dict get $observation attempt] > 40 ||
        [dict get $observation stable_count] < 4 ||
        [dict get $observation stable_count] > [dict get $observation attempt] ||
        [dict get $observation selected_api] ne "1" ||
        [dict get $observation available_api] ne "1" ||
        [dict get $observation selected] ne "hd720" ||
        ![media_probe_quality_available_valid [dict get $observation available] 1] ||
        ![media_probe_quality_contains [dict get $observation available] hd720] ||
        [dict get $observation video_width] ne "1280" ||
        [dict get $observation video_height] ne "720" ||
        [dict get $observation ready_state] < 2 ||
        [dict get $observation ready_state] > 4 ||
        [dict get $observation paused] ne "0" ||
        [dict get $observation ended] ne "0" ||
        double([dict get $observation current_time_last]) -
            double([dict get $observation current_time_first]) < 0.5} {
        return [list 0 "force-observation-not-stable"]
    }
    return [list 1 "pass"]
}

proc media_probe_validate_force_rows {rows} {
    set validations [list \
        [media_probe_validate_force_ready_row \
            [lindex [lindex $rows 0] 1]] \
        [media_probe_validate_force_attempt_row \
            [lindex [lindex $rows 1] 1] 1 setPlaybackQualityRange] \
        [media_probe_validate_force_result_row \
            [lindex [lindex $rows 2] 1] 1 setPlaybackQualityRange] \
        [media_probe_validate_force_attempt_row \
            [lindex [lindex $rows 3] 1] 2 setPlaybackQuality] \
        [media_probe_validate_force_result_row \
            [lindex [lindex $rows 4] 1] 2 setPlaybackQuality] \
        [media_probe_validate_force_observation_row \
            [lindex [lindex $rows 5] 1]]]
    foreach validation $validations {
        if {![lindex $validation 0]} { return $validation }
    }
    return [list 1 "pass"]
}

proc media_probe_validate_start_row {start} {
    if {[dict get $start samples] ne "20" ||
        [dict get $start interval_ms] ne "1000" ||
        [dict get $start rvfc_available] ni {0 1} ||
        ![media_probe_source_identity_valid [dict get $start src_id]]} {
        return [list 0 "invalid-start-row"]
    }
    return [list 1 "pass"]
}

# Shared row-local and prefix-monotonic sample validator.  The complete parser
# consumes the returned playing bit for its source verdict; the partial-prefix
# gate sets require_force_source=1 so a forced-hd720 prefix can spend a wait
# only when every observed sample is already 1280x720 and actively playing.
proc media_probe_validate_sample_row {
    sample index start first_sample prior_sample force_hd720
    {require_force_source 0}
} {
    if {![media_probe_is_uint [dict get $sample index]] ||
        [dict get $sample index] != $index} {
        return [list 0 "sample-index-$index-invalid" 0]
    }
    foreach key {video_width video_height} {
        if {![media_probe_is_uint [dict get $sample $key]] ||
            [dict get $sample $key] < 1 || [dict get $sample $key] > 8192} {
            return [list 0 "sample-$index-range-$key" 0]
        }
    }
    if {![media_probe_source_identity_valid [dict get $sample src_id]] ||
        [dict get $sample src_id] ne [dict get $start src_id]} {
        return [list 0 "sample-$index-source-mismatch" 0]
    }
    foreach {key low high} {
        current_time 0 100000000
        playback_rate 0 16
        buffered_ahead 0 86400
        longtask_duration_ms 0 1000000000
        rvfc_media_time -1 100000000
    } {
        if {![media_probe_in_range [dict get $sample $key] $low $high]} {
            return [list 0 "sample-$index-range-$key" 0]
        }
    }
    foreach key {
        paused ended quality_selected_api quality_available_api
        vpq_available webkit_available rvfc_available longtask_available
    } {
        if {[dict get $sample $key] ni {0 1}} {
            return [list 0 "sample-$index-boolean-$key" 0]
        }
    }
    if {![media_probe_is_uint [dict get $sample ready_state]] ||
        [dict get $sample ready_state] > 4 ||
        ![media_probe_is_uint [dict get $sample network_state]] ||
        [dict get $sample network_state] > 3} {
        return [list 0 "sample-$index-media-state-range" 0]
    }
    set selected_api [dict get $sample quality_selected_api]
    set available_api [dict get $sample quality_available_api]
    set selected [dict get $sample quality_selected]
    set available [dict get $sample quality_available]
    if {![media_probe_quality_token_valid $selected] ||
        (!$selected_api && $selected ne "unavailable") ||
        ($selected_api && $selected in {unavailable empty}) ||
        ![media_probe_quality_available_valid $available $available_api]} {
        return [list 0 "sample-$index-quality-shape" 0]
    }
    if {$force_hd720 &&
        ($selected_api ne "1" || $available_api ne "1" ||
         $selected ne "hd720" ||
         ![media_probe_quality_contains $available hd720])} {
        return [list 0 "force-sample-$index-quality-not-hd720" 0]
    }
    foreach family {vpq webkit} {
        set api [dict get $sample ${family}_available]
        set first_key [expr {$family eq "vpq" ? "vpq_total" : "webkit_decoded"}]
        set second_key [expr {$family eq "vpq" ? "vpq_dropped" : "webkit_dropped"}]
        set first [dict get $sample $first_key]
        set second [dict get $sample $second_key]
        if {!$api} {
            if {$first ne "-1" || $second ne "-1"} {
                return [list 0 "sample-$index-$family-unavailable-shape" 0]
            }
        } elseif {![media_probe_is_uint $first] ||
                  ![media_probe_is_uint $second] || $second > $first} {
            return [list 0 "sample-$index-$family-counter-shape" 0]
        }
    }
    if {![media_probe_is_uint [dict get $sample rvfc_callbacks]] ||
        ([dict get $sample rvfc_presented] ne "-1" &&
         ![media_probe_is_uint [dict get $sample rvfc_presented]]) ||
        ![media_probe_is_uint [dict get $sample longtask_count]]} {
        return [list 0 "sample-$index-counter-grammar" 0]
    }
    if {[dict get $sample rvfc_available] ne [dict get $start rvfc_available]} {
        return [list 0 "sample-$index-rvfc-availability-mismatch" 0]
    }
    if {![dict get $sample rvfc_available] &&
        ([dict get $sample rvfc_callbacks] ne "0" ||
         [dict get $sample rvfc_presented] ne "-1" ||
         double([dict get $sample rvfc_media_time]) != -1.0)} {
        return [list 0 "sample-$index-rvfc-unavailable-shape" 0]
    }
    if {![dict get $sample longtask_available] &&
        ([dict get $sample longtask_count] ne "0" ||
         double([dict get $sample longtask_duration_ms]) != 0.0)} {
        return [list 0 "sample-$index-longtask-unavailable-shape" 0]
    }
    if {$index > 1} {
        if {double([dict get $sample current_time]) <
            double([dict get $prior_sample current_time])} {
            return [list 0 "sample-$index-current-time-regression" 0]
        }
        foreach key {rvfc_callbacks longtask_count} {
            if {[dict get $sample $key] < [dict get $prior_sample $key]} {
                return [list 0 "sample-$index-counter-regression-$key" 0]
            }
        }
        if {double([dict get $sample longtask_duration_ms]) <
            double([dict get $prior_sample longtask_duration_ms])} {
            return [list 0 "sample-$index-longtask-duration-regression" 0]
        }
        foreach family {vpq webkit} {
            if {[dict get $sample ${family}_available] ne
                [dict get $first_sample ${family}_available]} {
                return [list 0 "sample-$index-$family-availability-change" 0]
            }
        }
        if {[dict get $sample vpq_available]} {
            foreach key {vpq_total vpq_dropped} {
                if {[dict get $sample $key] < [dict get $prior_sample $key]} {
                    return [list 0 "sample-$index-counter-regression-$key" 0]
                }
            }
        }
        if {[dict get $sample webkit_available]} {
            foreach key {webkit_decoded webkit_dropped} {
                if {[dict get $sample $key] < [dict get $prior_sample $key]} {
                    return [list 0 "sample-$index-counter-regression-$key" 0]
                }
            }
        }
        foreach key {
            src_id quality_selected quality_available quality_selected_api
            quality_available_api vpq_available webkit_available
            rvfc_available longtask_available
        } {
            if {[dict get $sample $key] ne [dict get $first_sample $key]} {
                return [list 0 "sample-$index-consistency-$key" 0]
            }
        }
    }
    set playing [expr {
        [dict get $sample paused] eq "0" &&
        [dict get $sample ended] eq "0" &&
        double([dict get $sample playback_rate]) >= 0.95 &&
        double([dict get $sample playback_rate]) <= 1.05}]
    if {$require_force_source && $force_hd720 &&
        ([dict get $sample video_width] ne "1280" ||
         [dict get $sample video_height] ne "720")} {
        return [list 0 "force-sample-$index-dimensions-not-1280x720" 0]
    }
    if {$require_force_source &&
        ([dict get $sample ready_state] < 2 ||
         ([dict get $sample rvfc_available] &&
          ([dict get $sample rvfc_callbacks] < 1 ||
           [dict get $sample rvfc_presented] < 0 ||
           double([dict get $sample rvfc_media_time]) < 0.0)))} {
        return [list 0 "force-sample-$index-source-state-not-ready" 0]
    }
    if {$require_force_source && $index > 1 &&
        [dict get $sample rvfc_available] &&
        ([dict get $sample rvfc_presented] <
            [dict get $prior_sample rvfc_presented] ||
         double([dict get $sample rvfc_media_time]) <
            double([dict get $prior_sample rvfc_media_time]))} {
        return [list 0 "force-sample-$index-rvfc-regression" 0]
    }
    if {$require_force_source && !$playing} {
        return [list 0 "force-sample-$index-playback-not-active" 0]
    }
    return [list 1 "pass" $playing]
}

proc media_probe_validate_summary_row {summary samples} {
    if {[llength $samples] != 20} {
        return [list 0 "summary-sample-count-[llength $samples]"]
    }
    set first_sample [lindex $samples 0]
    set last_sample [lindex $samples end]
    set widths {}
    set heights {}
    foreach sample $samples {
        lappend widths [dict get $sample video_width]
        lappend heights [dict get $sample video_height]
    }
    foreach key {samples width_min width_max height_min height_max longtask_count} {
        if {![media_probe_is_uint [dict get $summary $key]]} {
            return [list 0 "summary-grammar-$key"]
        }
    }
    if {[dict get $summary samples] ne "20" ||
        [dict get $summary width_min] != [tcl::mathfunc::min {*}$widths] ||
        [dict get $summary width_max] != [tcl::mathfunc::max {*}$widths] ||
        [dict get $summary height_min] != [tcl::mathfunc::min {*}$heights] ||
        [dict get $summary height_max] != [tcl::mathfunc::max {*}$heights]} {
        return [list 0 "summary-dimension-mismatch"]
    }
    foreach key {
        src_id quality_selected quality_available quality_selected_api
        quality_available_api vpq_available webkit_available longtask_available
    } {
        if {[dict get $summary $key] ne [dict get $last_sample $key]} {
            return [list 0 "summary-consistency-$key"]
        }
    }
    if {![media_probe_in_range [dict get $summary longtask_duration_ms] 0 1000000000] ||
        [dict get $summary longtask_count] ne [dict get $last_sample longtask_count] ||
        double([dict get $summary longtask_duration_ms]) !=
        double([dict get $last_sample longtask_duration_ms])} {
        return [list 0 "summary-longtask-mismatch"]
    }
    foreach family {vpq_total vpq_dropped webkit_decoded webkit_dropped} {
        set first [dict get $first_sample $family]
        set last [dict get $last_sample $family]
        if {[dict get $summary ${family}_first] ne $first ||
            [dict get $summary ${family}_last] ne $last} {
            return [list 0 "summary-$family-first-last-mismatch"]
        }
        set available [expr {[string match vpq_* $family] ?
            [dict get $summary vpq_available] :
            [dict get $summary webkit_available]}]
        if {![media_probe_counter_shape $available $first $last \
            [dict get $summary ${family}_delta]]} {
            return [list 0 "summary-$family-delta-mismatch"]
        }
    }
    return [list 1 "pass"]
}

proc media_probe_validate_rvfc_summary_row {rvfc start samples} {
    if {[llength $samples] != 20} {
        return [list 0 "rvfc-sample-count-[llength $samples]"]
    }
    set last_sample [lindex $samples end]
    foreach key {
        available callbacks schedule_failures warmup_callbacks interval_slots
        presented_first presented_last presented_delta presented_invalid
        presented_pair_invalid presented_regressions presented_duplicates
        media_invalid interval_count interval_valid interval_invalid
        interval_positive interval_regressions interval_duplicates near60_count
    } {
        if {$key in {presented_first presented_last presented_delta}} {
            if {![media_probe_is_sint [dict get $rvfc $key]]} {
                return [list 0 "rvfc-grammar-$key"]
            }
        } elseif {![media_probe_is_uint [dict get $rvfc $key]]} {
            return [list 0 "rvfc-grammar-$key"]
        }
    }
    foreach key {media_first media_last media_delta interval_sum_ms median_ms} {
        if {![media_probe_is_decimal [dict get $rvfc $key]]} {
            return [list 0 "rvfc-grammar-$key"]
        }
    }
    if {[dict get $rvfc available] ni {0 1} ||
        [dict get $rvfc available] ne [dict get $start rvfc_available] ||
        [dict get $rvfc available] ne [dict get $last_sample rvfc_available] ||
        [dict get $rvfc callbacks] ne [dict get $last_sample rvfc_callbacks] ||
        [dict get $rvfc warmup_callbacks] ne "0"} {
        return [list 0 "rvfc-start-sample-final-mismatch"]
    }
    if {[dict get $rvfc interval_slots] !=
        [expr {max(0, [dict get $rvfc callbacks] - 1)}] ||
        [dict get $rvfc interval_count] != [dict get $rvfc interval_slots] ||
        [dict get $rvfc interval_valid] + [dict get $rvfc interval_invalid] !=
        [dict get $rvfc interval_count]} {
        return [list 0 "rvfc-callback-interval-reconciliation"]
    }
    set hist_result [media_probe_histogram [dict get $rvfc hist]]
    if {![lindex $hist_result 0]} { return [list 0 [lindex $hist_result 1]] }
    set hist [lindex $hist_result 2]
    set hist_total 0
    set positive_total 0
    foreach key [dict keys $hist] {
        incr hist_total [dict get $hist $key]
        if {$key ni {regress duplicate}} { incr positive_total [dict get $hist $key] }
    }
    if {$hist_total != [dict get $rvfc interval_valid] ||
        $positive_total != [dict get $rvfc interval_positive] ||
        [dict get $hist regress] != [dict get $rvfc interval_regressions] ||
        [dict get $hist duplicate] != [dict get $rvfc interval_duplicates] ||
        [dict get $hist b15_18p5] != [dict get $rvfc near60_count]} {
        return [list 0 "rvfc-histogram-reconciliation"]
    }
    if {![media_probe_histogram_median_valid $hist $positive_total \
            [dict get $rvfc median_ms]]} {
        return [list 0 "rvfc-histogram-median-impossible"]
    }
    if {![media_probe_histogram_sum_valid $hist \
            [dict get $rvfc interval_sum_ms]]} {
        return [list 0 "rvfc-histogram-sum-impossible"]
    }
    if {[dict get $rvfc presented_last] ne [dict get $last_sample rvfc_presented] ||
        double([dict get $rvfc media_last]) !=
        double([dict get $last_sample rvfc_media_time])} {
        return [list 0 "rvfc-last-sample-mismatch"]
    }
    if {[dict get $rvfc presented_first] >= 0 &&
        [dict get $rvfc presented_last] >= 0} {
        if {[dict get $rvfc presented_delta] !=
            [dict get $rvfc presented_last] - [dict get $rvfc presented_first]} {
            return [list 0 "rvfc-presented-delta-mismatch"]
        }
    } elseif {[dict get $rvfc presented_delta] ne "-1"} {
        return [list 0 "rvfc-presented-unavailable-delta-shape"]
    }
    if {double([dict get $rvfc media_first]) >= 0 &&
        double([dict get $rvfc media_last]) >= 0} {
        if {abs(double([dict get $rvfc media_delta]) -
                (double([dict get $rvfc media_last]) -
                 double([dict get $rvfc media_first]))) > 0.000002} {
            return [list 0 "rvfc-media-delta-mismatch"]
        }
    } elseif {double([dict get $rvfc media_delta]) != -1.0} {
        return [list 0 "rvfc-media-unavailable-delta-shape"]
    }
    if {![dict get $rvfc available]} {
        foreach {key exact} {
            callbacks 0 warmup_callbacks 0 interval_slots 0
            presented_first -1 presented_last -1 presented_delta -1
            presented_invalid 0 presented_pair_invalid 0
            presented_regressions 0 presented_duplicates 0 media_first -1.000000
            media_last -1.000000 media_delta -1.000000 media_invalid 0
            interval_count 0 interval_valid 0 interval_invalid 0
            interval_positive 0 interval_regressions 0 interval_duplicates 0
            interval_sum_ms 0.000 median_ms -1.000 near60_count 0
        } {
            if {double([dict get $rvfc $key]) != double($exact)} {
                return [list 0 "rvfc-unavailable-shape-$key"]
            }
        }
    }
    if {[dict get $rvfc interval_invalid] == 0 &&
        double([dict get $rvfc media_delta]) >= 0 &&
        abs(double([dict get $rvfc interval_sum_ms]) / 1000.0 -
            double([dict get $rvfc media_delta])) > 0.003} {
        return [list 0 "rvfc-interval-sum-media-delta-mismatch"]
    }
    set sample_rvfc [media_probe_sample_rvfc_consistency $samples $rvfc]
    if {![lindex $sample_rvfc 0]} { return $sample_rvfc }
    return [list 1 "pass"]
}

# A short capture may earn one delayed host recapture only when it is an exact
# positional prefix of the selected arm.  This is intentionally stricter than
# a row-count check: exact row schemas/nonces are already enforced by
# media_probe_enveloped_rows(), while this layer rejects duplicate rows,
# reordered kinds, wrong force API sequencing, and repeated/skipped sample
# indices before the host spends its wait budget.
proc media_probe_validate_partial_prefix {text expected_nonce extension_id force_hd720} {
    if {$force_hd720 ni {0 1}} {
        return [list 0 "prefix-force-arm-invalid"]
    }
    set expected_rows [expr {$force_hd720 ? 30 : 24}]
    if {[string trim $text] eq ""} {
        return [list 1 "prefix-empty"]
    }
    set envelope [media_probe_enveloped_rows $text $expected_nonce \
        $extension_id $expected_rows]
    if {![lindex $envelope 0]} {
        return [list 0 "prefix-[lindex $envelope 1]"]
    }
    set rows [lindex $envelope 2]
    if {[llength $rows] < 1 || [llength $rows] >= $expected_rows} {
        return [list 0 "prefix-row-count-[llength $rows]-invalid"]
    }
    set expected_order [concat \
        [expr {$force_hd720 ? {
            force_ready force_attempt force_result force_attempt force_result
            force_observation
        } : {}}] \
        start [lrepeat 20 sample] summary rvfc_summary done]
    set seen [dict create]
    set force_rows [expr {$force_hd720 ? 6 : 0}]
    set start_position $force_rows
    set prefix_start {}
    set first_sample {}
    set prior_sample {}
    set prefix_samples {}
    for {set position 0} {$position < [llength $rows]} {incr position} {
        set item [lindex $rows $position]
        set payload [lindex $item 0]
        set fields [lindex $item 1]
        if {[dict exists $seen $payload]} {
            return [list 0 "prefix-duplicate-row-position-$position"]
        }
        dict set seen $payload 1
        set kind [dict get $fields kind]
        set expected_kind [lindex $expected_order $position]
        if {$kind ne $expected_kind} {
            return [list 0 \
                "prefix-order-position-$position-kind-$kind-expected-$expected_kind"]
        }
        if {$force_hd720} {
            switch -- $position {
                0 {
                    set semantic [media_probe_validate_force_ready_row $fields]
                }
                1 {
                    set semantic [media_probe_validate_force_attempt_row \
                        $fields 1 setPlaybackQualityRange]
                }
                2 {
                    set semantic [media_probe_validate_force_result_row \
                        $fields 1 setPlaybackQualityRange]
                }
                3 {
                    set semantic [media_probe_validate_force_attempt_row \
                        $fields 2 setPlaybackQuality]
                }
                4 {
                    set semantic [media_probe_validate_force_result_row \
                        $fields 2 setPlaybackQuality]
                }
                5 {
                    set semantic [media_probe_validate_force_observation_row \
                        $fields]
                }
            }
            if {$position <= 5 && ![lindex $semantic 0]} {
                return [list 0 "prefix-[lindex $semantic 1]"]
            }
        }
        if {$position == $start_position} {
            set start_validation [media_probe_validate_start_row $fields]
            if {![lindex $start_validation 0]} {
                return [list 0 "prefix-[lindex $start_validation 1]"]
            }
            set prefix_start $fields
        }
        set sample_index [expr {$position - $start_position}]
        if {$sample_index >= 1 && $sample_index <= 20} {
            set sample_validation [media_probe_validate_sample_row $fields \
                $sample_index $prefix_start $first_sample $prior_sample \
                $force_hd720 1]
            if {![lindex $sample_validation 0]} {
                return [list 0 "prefix-[lindex $sample_validation 1]"]
            }
            if {$sample_index == 1} {
                set first_sample $fields
                if {$force_hd720} {
                    set observation [lindex [lindex $rows 5] 1]
                    if {double([dict get $fields current_time]) <
                        double([dict get $observation current_time_last])} {
                        return [list 0 \
                            "prefix-force-first-sample-time-regression"]
                    }
                }
            }
            set prior_sample $fields
            lappend prefix_samples $fields
        }
        if {$sample_index == 21} {
            set summary_validation [media_probe_validate_summary_row \
                $fields $prefix_samples]
            if {![lindex $summary_validation 0]} {
                return [list 0 "prefix-[lindex $summary_validation 1]"]
            }
        }
        if {$sample_index == 22} {
            set rvfc_validation [media_probe_validate_rvfc_summary_row \
                $fields $prefix_start $prefix_samples]
            if {![lindex $rvfc_validation 0]} {
                return [list 0 "prefix-[lindex $rvfc_validation 1]"]
            }
        }
        if {$sample_index > 22} {
            return [list 0 "prefix-done-row-requires-complete"]
        }
    }
    return [list 1 "prefix-rows-[llength $rows]-ordered-unique"]
}

proc parse_media_probe_evidence {text expected_nonce extension_id {force_hd720 0}} {
    if {$force_hd720 ni {0 1}} {
        return [list 0 0 "force-arm-must-be-0-or-1"]
    }
    set force_rows [expr {$force_hd720 ? 6 : 0}]
    set expected_rows [expr {24 + $force_rows}]
    set envelope [media_probe_enveloped_rows $text $expected_nonce $extension_id \
        $expected_rows]
    if {![lindex $envelope 0]} { return [list 0 0 [lindex $envelope 1]] }
    set rows [lindex $envelope 2]
    foreach item $rows {
        set fields [lindex $item 1]
        if {[dict get $fields kind] eq "failure"} {
            return [list 0 0 "injection-failure-[dict get $fields reason]"]
        }
    }
    if {[llength $rows] != $expected_rows} {
        return [list 0 0 "row-count-[llength $rows]-expected-$expected_rows"]
    }
    set expected_order [concat \
        [expr {$force_hd720 ? {
            force_ready force_attempt force_result force_attempt force_result
            force_observation
        } : {}}] \
        start [lrepeat 20 sample] summary rvfc_summary done]
    for {set position 0} {$position < $expected_rows} {incr position} {
        set kind [dict get [lindex [lindex $rows $position] 1] kind]
        if {$kind ne [lindex $expected_order $position]} {
            return [list 0 0 "row-order-position-$position-kind-$kind"]
        }
    }

    if {$force_hd720} {
        set force_validation [media_probe_validate_force_rows $rows]
        if {![lindex $force_validation 0]} {
            return [list 0 0 [lindex $force_validation 1]]
        }
    }

    set start [lindex [lindex $rows $force_rows] 1]
    set start_validation [media_probe_validate_start_row $start]
    if {![lindex $start_validation 0]} {
        return [list 0 0 [lindex $start_validation 1]]
    }

    set samples {}
    set widths {}
    set heights {}
    set current_times {}
    set all_playing 1
    set first_sample {}
    set prior_sample {}
    for {set index 1} {$index <= 20} {incr index} {
        set sample [lindex [lindex $rows [expr {$force_rows + $index}]] 1]
        set sample_validation [media_probe_validate_sample_row $sample $index \
            $start $first_sample $prior_sample $force_hd720]
        if {![lindex $sample_validation 0]} {
            return [list 0 0 [lindex $sample_validation 1]]
        }
        if {![lindex $sample_validation 2]} {
            set all_playing 0
        }
        if {$index == 1} { set first_sample $sample }
        lappend samples $sample
        lappend widths [dict get $sample video_width]
        lappend heights [dict get $sample video_height]
        lappend current_times [dict get $sample current_time]
        set prior_sample $sample
    }
    set last_sample [lindex $samples end]
    if {$force_hd720} {
        set force_observation [lindex [lindex $rows 5] 1]
        if {double([dict get $first_sample current_time]) <
            double([dict get $force_observation current_time_last])} {
            return [list 0 0 "force-first-sample-time-regression"]
        }
    }

    set summary [lindex [lindex $rows [expr {$force_rows + 21}]] 1]
    set summary_validation [media_probe_validate_summary_row $summary $samples]
    if {![lindex $summary_validation 0]} {
        return [list 0 0 [lindex $summary_validation 1]]
    }

    set rvfc [lindex [lindex $rows [expr {$force_rows + 22}]] 1]
    set rvfc_validation [media_probe_validate_rvfc_summary_row \
        $rvfc $start $samples]
    if {![lindex $rvfc_validation 0]} {
        return [list 0 0 [lindex $rvfc_validation 1]]
    }

    set done [lindex [lindex $rows [expr {$force_rows + 23}]] 1]
    if {[dict get $done status] ne "complete" ||
        [dict get $done samples] ne "20" ||
        [dict get $done rows] ne $expected_rows} {
        return [list 0 0 "done-row-invalid"]
    }

    set dimensions [expr {
        [dict get $summary width_min] == 1280 &&
        [dict get $summary width_max] == 1280 &&
        [dict get $summary height_min] == 720 &&
        [dict get $summary height_max] == 720}]
    set time_progress [expr {
        double([lindex $current_times end]) - double([lindex $current_times 0])}]
    set interval_count [dict get $rvfc interval_count]
    set near_ratio [expr {$interval_count > 0 ?
        double([dict get $rvfc near60_count]) / $interval_count : 0.0}]
    set cadence_clean [expr {
        [dict get $rvfc schedule_failures] == 0 &&
        [dict get $rvfc interval_invalid] == 0 &&
        [dict get $rvfc interval_regressions] == 0 &&
        [dict get $rvfc interval_duplicates] == 0 &&
        [dict get $rvfc media_invalid] == 0}]
    set cadence_60 [expr {
        $interval_count >= 120 &&
        [dict get $rvfc interval_positive] == $interval_count &&
        double([dict get $rvfc median_ms]) >= 15.0 &&
        double([dict get $rvfc median_ms]) <= 18.5 && $near_ratio >= 0.60}]
    set presented_plausible [expr {
        [dict get $rvfc presented_invalid] == 0 &&
        [dict get $rvfc presented_pair_invalid] == 0 &&
        [dict get $rvfc presented_regressions] == 0 &&
        [dict get $rvfc presented_duplicates] == 0 &&
        [dict get $rvfc presented_first] >= 0 &&
        [dict get $rvfc presented_delta] >= [dict get $rvfc interval_slots]}]
    set media_sufficient [expr {
        double([dict get $rvfc media_first]) >= 0 &&
        double([dict get $rvfc media_delta]) >= 15.0}]

    if {!$dimensions} {
        set reason "dimensions-not-1280x720"
    } elseif {!$all_playing || $time_progress < 15.0} {
        set reason "playback-not-active"
    } elseif {![dict get $rvfc available]} {
        set reason "rvfc-unavailable"
    } elseif {[dict get $rvfc interval_regressions] > 0 ||
              [dict get $rvfc interval_duplicates] > 0} {
        set reason "cadence-nonpositive"
    } elseif {!$cadence_clean} {
        set reason "cadence-invalid"
    } elseif {!$cadence_60} {
        set reason "cadence-not-16p7ms"
    } elseif {!$presented_plausible} {
        set reason "presented-frames-insufficient"
    } elseif {!$media_sufficient} {
        set reason "media-delta-insufficient"
    } else {
        set reason "pass"
    }
    set source_proven [expr {$reason eq "pass"}]
    set detail "reason=$reason intervals=$interval_count median_ms=[dict get $rvfc median_ms] near60_ratio=[format %.4f $near_ratio] time_progress=[format %.3f $time_progress]"
    if {$force_hd720} {
        set force_ready [lindex [lindex $rows 0] 1]
        set force_observation [lindex [lindex $rows 5] 1]
        append detail " force_hd720=1 force_ready_attempt=[dict get $force_ready attempt] force_observe_attempt=[dict get $force_observation attempt] force_stable_count=[dict get $force_observation stable_count]"
    }
    return [list 1 $source_proven $detail $summary $rvfc]
}

proc media_probe_console_line {payload {line_number 321} {extension_id edfilgocpdgbkehcgdillfgnnhclphol}} {
    return "\[412:412:0710/120000.123456:INFO:CONSOLE:$line_number\] \"$payload\", source: chrome-extension://$extension_id/probe.js ($line_number)"
}

proc synthetic_media_probe_evidence {nonce {options {}}} {
    set defaults [dict create \
        width 1280 height 720 optional_apis 1 paused 0 ended 0 \
        playback_rate 1.000 time_step 1.000 rvfc_available 1 \
        force_hd720 0 force_ready_attempt 3 force_ready_selected medium \
        force_player_state_api 1 force_player_state 1 \
        force_available hd1080,hd720,large force_range_present 1 \
        force_range_invoked 1 force_range_threw 0 force_quality_present 1 \
        force_quality_invoked 1 force_quality_threw 0 \
        force_observe_attempt 5 force_stable_count 4 \
        force_observe_selected hd720 force_observe_width 1280 \
        force_observe_height 720 force_time_first 0.000 force_time_last 0.750 \
        sample_quality hd720 sample_available hd1080,hd720,large \
        vpq_total_base 1000 vpq_total_step 60 \
        vpq_dropped_base 10 vpq_dropped_step 1 \
        interval_positive 1200 interval_near60 1200 interval_other30 0 \
        interval_b14_15 0 interval_b18p5_20 0 \
        interval_regressions 0 interval_duplicates 0 interval_invalid 0 \
        interval_sum_ms 20000.400 median_ms 16.667 media_delta 20.000400 \
        schedule_failures 0 media_invalid 0 presented_invalid 0 \
        presented_pair_invalid 0 presented_regressions 0 \
        presented_duplicates 0 presented_delta auto callbacks_override auto]
    set opt [dict merge $defaults $options]
    set src {blob:https://www.youtube.com/<redacted>}
    set payloads {}
    if {[dict get $opt force_hd720]} {
        lappend payloads "YT_MEDIA_PROBE_V1 kind=force_ready schema=1 nonce=$nonce attempt=[dict get $opt force_ready_attempt] player_present=1 video_present=1 player_state_api=[dict get $opt force_player_state_api] player_state=[dict get $opt force_player_state] ready_state=4 video_width=640 video_height=360 selected_api=1 available_api=1 selected=[dict get $opt force_ready_selected] available=[dict get $opt force_available] range_api=[dict get $opt force_range_present] quality_api=[dict get $opt force_quality_present]"
        lappend payloads "YT_MEDIA_PROBE_V1 kind=force_attempt schema=1 nonce=$nonce seq=1 api=setPlaybackQualityRange arg_min=hd720 arg_max=hd720 present=[dict get $opt force_range_present]"
        lappend payloads "YT_MEDIA_PROBE_V1 kind=force_result schema=1 nonce=$nonce seq=1 api=setPlaybackQualityRange invoked=[dict get $opt force_range_invoked] threw=[dict get $opt force_range_threw] return_type=[expr {[dict get $opt force_range_invoked] && ![dict get $opt force_range_threw] ? {undefined} : {unavailable}}]"
        lappend payloads "YT_MEDIA_PROBE_V1 kind=force_attempt schema=1 nonce=$nonce seq=2 api=setPlaybackQuality arg_min=hd720 arg_max=hd720 present=[dict get $opt force_quality_present]"
        lappend payloads "YT_MEDIA_PROBE_V1 kind=force_result schema=1 nonce=$nonce seq=2 api=setPlaybackQuality invoked=[dict get $opt force_quality_invoked] threw=[dict get $opt force_quality_threw] return_type=[expr {[dict get $opt force_quality_invoked] && ![dict get $opt force_quality_threw] ? {undefined} : {unavailable}}]"
        lappend payloads "YT_MEDIA_PROBE_V1 kind=force_observation schema=1 nonce=$nonce attempt=[dict get $opt force_observe_attempt] stable_count=[dict get $opt force_stable_count] selected_api=1 available_api=1 selected=[dict get $opt force_observe_selected] available=[dict get $opt force_available] video_width=[dict get $opt force_observe_width] video_height=[dict get $opt force_observe_height] ready_state=4 paused=0 ended=0 current_time_first=[dict get $opt force_time_first] current_time_last=[dict get $opt force_time_last]"
    }
    lappend payloads "YT_MEDIA_PROBE_V1 kind=start schema=1 nonce=$nonce samples=20 interval_ms=1000 rvfc_available=[dict get $opt rvfc_available] src_id=$src"
    set positive [dict get $opt interval_positive]
    set valid [expr {$positive + [dict get $opt interval_regressions] +
        [dict get $opt interval_duplicates]}]
    set intervals [expr {$valid + [dict get $opt interval_invalid]}]
    set callbacks [expr {[dict get $opt rvfc_available] ? $intervals + 1 : 0}]
    if {[dict get $opt callbacks_override] ne "auto"} {
        set callbacks [dict get $opt callbacks_override]
    }
    set presented_delta [dict get $opt presented_delta]
    if {$presented_delta eq "auto"} { set presented_delta $intervals }
    set presented_first [expr {$callbacks ? 100 : -1}]
    set presented_last [expr {$callbacks ?
        $presented_first + $presented_delta : -1}]
    set media_first [expr {$callbacks ? 1.0 : -1.0}]
    set media_delta [expr {$callbacks ?
        double([dict get $opt media_delta]) : -1.0}]
    set media_last [expr {$callbacks ?
        $media_first + $media_delta : -1.0}]
    foreach key {vpq_total_base vpq_total_step vpq_dropped_base vpq_dropped_step} {
        if {![media_probe_is_uint [dict get $opt $key]]} {
            error "invalid synthetic media-probe counter option: $key"
        }
    }
    set vpq_first -1
    set vpq_last -1
    set vpq_dropped_first -1
    set vpq_dropped_last -1
    for {set index 1} {$index <= 20} {incr index} {
        set optional [dict get $opt optional_apis]
        if {$optional} {
            set vpq_total [expr {[dict get $opt vpq_total_base] +
                ($index * [dict get $opt vpq_total_step])}]
            set vpq_dropped [expr {[dict get $opt vpq_dropped_base] +
                ($index * [dict get $opt vpq_dropped_step])}]
            set webkit_decoded $vpq_total
            set webkit_dropped $vpq_dropped
            if {$index == 1} {
                set vpq_first $vpq_total
                set vpq_dropped_first $vpq_dropped
            }
            set vpq_last $vpq_total
            set vpq_dropped_last $vpq_dropped
        } else {
            set vpq_total -1; set vpq_dropped -1
            set webkit_decoded -1; set webkit_dropped -1
        }
        set sample_callbacks [expr {$callbacks ? int(floor(double($callbacks) * $index / 20.0)) : 0}]
        if {$index == 20} { set sample_callbacks $callbacks }
        if {$sample_callbacks == 0} {
            set sample_presented -1
            set sample_media -1.0
        } elseif {$intervals == 0} {
            set sample_presented $presented_first
            set sample_media $media_first
        } else {
            set observed_slots [expr {$sample_callbacks - 1}]
            set sample_presented [expr {$presented_first + int(floor(
                double($presented_delta) * $observed_slots / $intervals))}]
            set sample_media [expr {$media_first +
                ($media_delta * $observed_slots / $intervals)}]
        }
        if {$index == 20} {
            set sample_presented $presented_last
            set sample_media $media_last
        }
        set current_time [expr {1.0 + (($index - 1) * double([dict get $opt time_step]))}]
        lappend payloads "YT_MEDIA_PROBE_V1 kind=sample schema=1 nonce=$nonce index=$index video_width=[dict get $opt width] video_height=[dict get $opt height] src_id=$src current_time=[format %.3f $current_time] playback_rate=[dict get $opt playback_rate] paused=[dict get $opt paused] ended=[dict get $opt ended] quality_selected=[dict get $opt sample_quality] quality_available=[dict get $opt sample_available] quality_selected_api=1 quality_available_api=1 ready_state=4 network_state=2 buffered_ahead=12.500 vpq_available=$optional vpq_total=$vpq_total vpq_dropped=$vpq_dropped webkit_available=$optional webkit_decoded=$webkit_decoded webkit_dropped=$webkit_dropped rvfc_available=[dict get $opt rvfc_available] rvfc_callbacks=$sample_callbacks rvfc_presented=$sample_presented rvfc_media_time=[format %.6f $sample_media] longtask_available=1 longtask_count=[expr {$index / 5}] longtask_duration_ms=[format %.3f [expr {$index * 6.0}]]"
    }
    if {[dict get $opt optional_apis]} {
        set vpq_delta [expr {$vpq_last - $vpq_first}]
        set vpq_dropped_delta [expr {$vpq_dropped_last - $vpq_dropped_first}]
        set api_summary "vpq_available=1 vpq_total_first=$vpq_first vpq_total_last=$vpq_last vpq_total_delta=$vpq_delta vpq_dropped_first=$vpq_dropped_first vpq_dropped_last=$vpq_dropped_last vpq_dropped_delta=$vpq_dropped_delta webkit_available=1 webkit_decoded_first=$vpq_first webkit_decoded_last=$vpq_last webkit_decoded_delta=$vpq_delta webkit_dropped_first=$vpq_dropped_first webkit_dropped_last=$vpq_dropped_last webkit_dropped_delta=$vpq_dropped_delta"
    } else {
        set api_summary {vpq_available=0 vpq_total_first=-1 vpq_total_last=-1 vpq_total_delta=-1 vpq_dropped_first=-1 vpq_dropped_last=-1 vpq_dropped_delta=-1 webkit_available=0 webkit_decoded_first=-1 webkit_decoded_last=-1 webkit_decoded_delta=-1 webkit_dropped_first=-1 webkit_dropped_last=-1 webkit_dropped_delta=-1}
    }
    lappend payloads "YT_MEDIA_PROBE_V1 kind=summary schema=1 nonce=$nonce samples=20 width_min=[dict get $opt width] width_max=[dict get $opt width] height_min=[dict get $opt height] height_max=[dict get $opt height] src_id=$src quality_selected=[dict get $opt sample_quality] quality_available=[dict get $opt sample_available] quality_selected_api=1 quality_available_api=1 $api_summary longtask_available=1 longtask_count=4 longtask_duration_ms=120.000"
    set hist "regress:[dict get $opt interval_regressions],duplicate:[dict get $opt interval_duplicates],lt8:0,b8_12:0,b12_14:0,b14_15:[dict get $opt interval_b14_15],b15_18p5:[dict get $opt interval_near60],b18p5_20:[dict get $opt interval_b18p5_20],b20_28:0,b28_40:[dict get $opt interval_other30],b40_80:0,ge80:0"
    lappend payloads "YT_MEDIA_PROBE_V1 kind=rvfc_summary schema=1 nonce=$nonce available=[dict get $opt rvfc_available] callbacks=$callbacks schedule_failures=[dict get $opt schedule_failures] warmup_callbacks=0 interval_slots=$intervals presented_first=$presented_first presented_last=$presented_last presented_delta=[expr {$callbacks ? $presented_delta : -1}] presented_invalid=[dict get $opt presented_invalid] presented_pair_invalid=[dict get $opt presented_pair_invalid] presented_regressions=[dict get $opt presented_regressions] presented_duplicates=[dict get $opt presented_duplicates] media_first=[format %.6f $media_first] media_last=[format %.6f $media_last] media_delta=[format %.6f $media_delta] media_invalid=[dict get $opt media_invalid] interval_count=$intervals interval_valid=$valid interval_invalid=[dict get $opt interval_invalid] interval_positive=$positive interval_regressions=[dict get $opt interval_regressions] interval_duplicates=[dict get $opt interval_duplicates] interval_sum_ms=[dict get $opt interval_sum_ms] median_ms=[dict get $opt median_ms] near60_count=[dict get $opt interval_near60] hist=$hist"
    set row_count [expr {[dict get $opt force_hd720] ? 30 : 24}]
    lappend payloads "YT_MEDIA_PROBE_V1 kind=done schema=1 nonce=$nonce status=complete samples=20 rows=$row_count"
    set lines {}
    set line_number 100
    foreach payload $payloads {
        lappend lines [media_probe_console_line $payload $line_number]
        incr line_number
    }
    return [join $lines "\n"]
}

proc media_probe_mutate_once {text from to} {
    set first [string first $from $text]
    if {$first < 0 || [string first $from $text [expr {$first + 1}]] >= 0} {
        error "mutation source must occur exactly once: $from"
    }
    return [string replace $text $first [expr {$first + [string length $from] - 1}] $to]
}

proc media_probe_mutate_first {text from to} {
    set first [string first $from $text]
    if {$first < 0} { error "mutation source missing: $from" }
    return [string replace $text $first [expr {$first + [string length $from] - 1}] $to]
}

proc media_probe_capture_payload {text expected_nonce} {
    if {![regexp {^[0-9a-f]{32}$} $expected_nonce]} {
        return [list 0 "capture-nonce-invalid" ""]
    }
    set begin "YT_MEDIA_PROBE_CAPTURE_BEGIN nonce=$expected_nonce status=oneshot polls=0 byte_cap=49152"
    set end "YT_MEDIA_PROBE_CAPTURE_END nonce=$expected_nonce status=oneshot polls=0 byte_cap=49152"
    set lines [split $text "\n"]
    set begin_indices {}
    set end_indices {}
    set line_index -1
    foreach raw_line $lines {
        incr line_index
        set line [string trimright $raw_line "\r"]
        if {[string first {YT_MEDIA_PROBE_CAPTURE_BEGIN } $line] >= 0} {
            if {$line ne $begin} {
                return [list 0 "capture-begin-shape-invalid" ""]
            }
            lappend begin_indices $line_index
        }
        if {[string first {YT_MEDIA_PROBE_CAPTURE_END } $line] >= 0} {
            if {$line ne $end} {
                return [list 0 "capture-end-shape-invalid" ""]
            }
            lappend end_indices $line_index
        }
    }
    if {[llength $begin_indices] != 1 || [llength $end_indices] != 1 ||
        [lindex $end_indices 0] <= [lindex $begin_indices 0]} {
        return [list 0 "capture-markers-invalid" ""]
    }
    set payload_lines {}
    for {set index [expr {[lindex $begin_indices 0] + 1}]} \
        {$index < [lindex $end_indices 0]} {incr index} {
        lappend payload_lines [string trimright [lindex $lines $index] "\r"]
    }
    return [list 1 "pass" [string trim [join $payload_lines "\n"]]]
}
