#!/usr/bin/env tclsh

set here [file dirname [file normalize [info script]]]
proc regex_quote {text} {
    regsub -all {[][(){}.^$*+?|\\]} $text {\\&} text
    return $text
}
source [file join $here chromium-youtube-media-probe-parser.tcl]

proc parity_fail {reason} {
    puts stderr "YOUTUBE_PARITY_RESULT status=FAIL reason=$reason"
    exit 2
}

proc parity_read {path} {
    if {![file exists $path] || ![file isfile $path]} {
        parity_fail "missing-file-[file tail $path]"
    }
    set fh [open $path r]
    set text [read $fh]
    close $fh
    return $text
}

proc parity_fields {line marker} {
    if {[string first "$marker " $line] != 0} { return {} }
    set fields [dict create]
    foreach word [lrange [split $line " "] 1 end] {
        if {![regexp {^([a-z][a-z0-9_]*)=([^ ]+)$} $word -> key value] ||
            [dict exists $fields $key]} {
            return {}
        }
        dict set fields $key $value
    }
    return $fields
}

proc parity_rows {text marker} {
    set rows {}
    foreach line [split $text "\n"] {
        set fields [parity_fields [string trim $line] $marker]
        if {[dict size $fields] > 0} { lappend rows $fields }
    }
    return $rows
}

proc parity_validate_wall {text expected_nonce} {
    set qnonce [regex_quote $expected_nonce]
    set extension_id edfilgocpdgbkehcgdillfgnnhclphol
    set qid [regex_quote $extension_id]
    set pattern [format {^\[([1-9][0-9]*):([1-9][0-9]*):([0-9]{4}/[0-9]{6}\.[0-9]{6}):INFO:CONSOLE:([0-9]+)\] "YT_MEDIA_WALL_V1 kind=(start|end) schema=1 nonce=%s monotonic_ms=([1-9][0-9]*)", source: chrome-extension://%s/probe\.js \(([0-9]+)\)$} $qnonce $qid]
    set rows {}
    foreach raw_line [split $text "\n"] {
        set line [string trimright $raw_line "\r"]
        if {[string first "YT_MEDIA_WALL_V1 " $line] < 0} { continue }
        if {![regexp $pattern $line -> pid tid stamp console_line kind mono source_line] ||
            $console_line ne $source_line} {
            parity_fail "wall-row-untrusted"
        }
        lappend rows [list $kind $mono]
    }
    if {[llength $rows] != 2 ||
        [lindex [lindex $rows 0] 0] ne "start" ||
        [lindex [lindex $rows 1] 0] ne "end"} {
        parity_fail "wall-row-order-or-count-[llength $rows]"
    }
    set first [lindex [lindex $rows 0] 1]
    set last [lindex [lindex $rows 1] 1]
    if {$last <= $first} { parity_fail "wall-time-nonpositive" }
    return [expr {double($last - $first) / 1000.0}]
}

proc parity_uint {value} {
    return [regexp {^(0|[1-9][0-9]*)$} $value]
}

proc parity_validate_display {text expected_mode} {
    set rows [parity_rows $text YT_DISPLAY_STATE_V1]
    if {[llength $rows] < 2} { parity_fail "display-row-count-[llength $rows]" }
    set prior -1
    foreach row $rows {
        foreach key {monotonic_ms mode document_fullscreen player_fullscreen
                     video_playing video_width video_height} {
            if {![dict exists $row $key]} { parity_fail "display-key-$key-missing" }
        }
        if {[dict get $row mode] ne $expected_mode ||
            ![parity_uint [dict get $row monotonic_ms]] ||
            [dict get $row monotonic_ms] <= $prior ||
            [dict get $row video_playing] ne "1" ||
            [dict get $row video_width] ne "1280" ||
            [dict get $row video_height] ne "720"} {
            parity_fail "display-row-invalid"
        }
        set expected [expr {$expected_mode eq "fullscreen" ? "1" : "0"}]
        if {[dict get $row document_fullscreen] ne $expected ||
            [dict get $row player_fullscreen] ne $expected} {
            parity_fail "display-state-not-$expected_mode"
        }
        set prior [dict get $row monotonic_ms]
    }
    return [llength $rows]
}

proc parity_validate_audio {text wav_before wav_after} {
    set rows [parity_rows $text YT_AUDIO_STATE_V1]
    if {[llength $rows] != 2} { parity_fail "audio-row-count-[llength $rows]" }
    set before [lindex $rows 0]
    set after [lindex $rows 1]
    foreach {row phase} [list $before before $after after] {
        foreach key {phase sink_null stream_count stream_bytes hw_bytes} {
            if {![dict exists $row $key]} { parity_fail "audio-key-$key-missing" }
        }
        if {[dict get $row phase] ne $phase || [dict get $row sink_null] ne "0"} {
            parity_fail "audio-$phase-invalid"
        }
        foreach key {stream_count stream_bytes hw_bytes} {
            if {![parity_uint [dict get $row $key]]} {
                parity_fail "audio-$phase-$key-invalid"
            }
        }
    }
    if {[dict get $after stream_count] < 1 ||
        [dict get $after stream_bytes] <= [dict get $before stream_bytes] ||
        [dict get $after hw_bytes] <= [dict get $before hw_bytes]} {
        parity_fail "audio-counters-did-not-advance"
    }
    if {![parity_uint $wav_before] || ![parity_uint $wav_after] ||
        $wav_after <= $wav_before || $wav_after - $wav_before <= 44} {
        parity_fail "wav-bytes-did-not-advance"
    }
    return [list \
        [expr {[dict get $after stream_bytes] - [dict get $before stream_bytes]}] \
        [expr {[dict get $after hw_bytes] - [dict get $before hw_bytes]}] \
        [expr {$wav_after - $wav_before}]]
}

proc parity_performance_reason {presented_fps media_wall_ratio drop_percent} {
    if {$presented_fps < 50.0 || $presented_fps > 75.0} {
        return [format "presented-fps-out-of-range=%.3f" $presented_fps]
    }
    if {$media_wall_ratio < 0.90 || $media_wall_ratio > 1.10} {
        return [format "media-wall-ratio-out-of-range=%.6f" $media_wall_ratio]
    }
    if {$drop_percent > 50.0} {
        return [format "vpq-drop-percent-too-high=%.3f" $drop_percent]
    }
    return pass
}

if {$argc == 1 && [lindex $argv 0] eq "--self-test"} {
    set nonce 0123456789abcdef0123456789abcdef
    set probe [synthetic_media_probe_evidence $nonce \
        [dict create force_hd720 1]]
    set parsed [parse_media_probe_evidence $probe $nonce \
        edfilgocpdgbkehcgdillfgnnhclphol 1]
    if {![lindex $parsed 0] || ![lindex $parsed 1]} {
        parity_fail "self-test-media-[lindex $parsed 2]"
    }
    set display "YT_DISPLAY_STATE_V1 monotonic_ms=100 mode=fullscreen document_fullscreen=1 player_fullscreen=1 video_playing=1 video_width=1280 video_height=720\nYT_DISPLAY_STATE_V1 monotonic_ms=200 mode=fullscreen document_fullscreen=1 player_fullscreen=1 video_playing=1 video_width=1280 video_height=720\n"
    if {[parity_validate_display $display fullscreen] != 2} {
        parity_fail "self-test-display"
    }
    set audio "YT_AUDIO_STATE_V1 phase=before sink_null=0 stream_count=1 stream_bytes=100 hw_bytes=80\nYT_AUDIO_STATE_V1 phase=after sink_null=0 stream_count=1 stream_bytes=8292 hw_bytes=8016\n"
    lassign [parity_validate_audio $audio 44 8240] stream hw wav
    if {$stream != 8192 || $hw != 7936 || $wav != 8196} {
        parity_fail "self-test-audio"
    }
    set wall "\[412:412:0710/120000.123456:INFO:CONSOLE:42\] \"YT_MEDIA_WALL_V1 kind=start schema=1 nonce=$nonce monotonic_ms=1000\", source: chrome-extension://edfilgocpdgbkehcgdillfgnnhclphol/probe.js (42)\n\[412:412:0710/120020.123456:INFO:CONSOLE:43\] \"YT_MEDIA_WALL_V1 kind=end schema=1 nonce=$nonce monotonic_ms=21000\", source: chrome-extension://edfilgocpdgbkehcgdillfgnnhclphol/probe.js (43)\n"
    if {[parity_validate_wall $wall $nonce] != 20.0} {
        parity_fail "self-test-wall"
    }
    foreach {presented ratio dropped expected} {
        60.0 1.0 1.0 pass
        49.9 1.0 1.0 presented-fps-out-of-range=49.900
        60.0 0.89 1.0 media-wall-ratio-out-of-range=0.890000
        60.0 1.0 50.1 vpq-drop-percent-too-high=50.100
    } {
        if {[parity_performance_reason $presented $ratio $dropped] ne
            $expected} {
            parity_fail "self-test-performance"
        }
    }
    puts "YOUTUBE_PARITY_SELF_TEST status=PASS"
    exit 0
}

if {$argc != 8} {
    puts stderr "usage: [file tail [info script]] <probe.log> <nonce> <windowed|fullscreen> <display.log> <audio.log> <wav-before> <wav-after> <linux|xv6>"
    exit 64
}
lassign $argv probe_path nonce mode display_path audio_path wav_before wav_after guest
if {![regexp {^[0-9a-f]{32}$} $nonce] ||
    $mode ni {windowed fullscreen} || $guest ni {linux xv6}} {
    parity_fail "argument-contract-invalid"
}

set probe_text [parity_read $probe_path]
set parsed [parse_media_probe_evidence $probe_text $nonce \
    edfilgocpdgbkehcgdillfgnnhclphol 1]
if {![lindex $parsed 0]} { parity_fail "media-[lindex $parsed 2]" }
if {![lindex $parsed 1]} { parity_fail "media-source-[lindex $parsed 2]" }
set summary [lindex $parsed 3]
set rvfc [lindex $parsed 4]
set display_samples [parity_validate_display [parity_read $display_path] $mode]
lassign [parity_validate_audio [parity_read $audio_path] $wav_before $wav_after] \
    stream_delta hw_delta wav_delta

set media_seconds [expr {double([dict get $rvfc media_delta])}]
set wall_seconds [parity_validate_wall $probe_text $nonce]
set presented_fps [expr {double([dict get $rvfc presented_delta]) / $wall_seconds}]
set media_wall_ratio [expr {$media_seconds / $wall_seconds}]
set vpq_total [dict get $summary vpq_total_delta]
set vpq_dropped [dict get $summary vpq_dropped_delta]
if {$vpq_total <= 0 || $vpq_dropped < 0 || $vpq_dropped > $vpq_total} {
    parity_fail "vpq-counters-invalid"
}
set drop_percent [expr {100.0 * $vpq_dropped / $vpq_total}]
set performance_reason [parity_performance_reason \
    $presented_fps $media_wall_ratio $drop_percent]
if {$performance_reason ne "pass"} { parity_fail $performance_reason }
puts [format \
    "YOUTUBE_PARITY_RESULT status=PASS guest=%s mode=%s presented_fps=%.3f media_seconds=%.6f wall_seconds=%.6f media_wall_ratio=%.6f vpq_total=%d vpq_dropped=%d vpq_drop_percent=%.3f display_samples=%d pulse_stream_bytes_delta=%d hw_played_bytes_delta=%d wav_bytes_delta=%d quality=%s width=1280 height=720" \
    $guest $mode $presented_fps $media_seconds $wall_seconds $media_wall_ratio \
    $vpq_total $vpq_dropped $drop_percent $display_samples $stream_delta \
    $hw_delta $wav_delta [dict get $summary quality_selected]]
