(function runXv6YouTubeMediaProbe(global) {
  "use strict";

  if (global.__xv6YouTubeMediaProbeStarted)
    return;
  global.__xv6YouTubeMediaProbeStarted = true;

  const lib = global.__xv6YouTubeMediaProbeLib;
  const nonceMatch = /^#xv6ytprobe=([0-9a-f]{32})$/.exec(global.location.hash);
  const nonce = nonceMatch ? nonceMatch[1] : "unavailable";
  const MAX_ROWS = 24;
  const SAMPLE_COUNT = 20;
  const SAMPLE_INTERVAL_MS = 1000;
  let emittedRows = 0;

  function emit(kind, fields) {
    if (emittedRows >= MAX_ROWS)
      return false;
    try {
      console.info(lib.formatRow(kind, { nonce, ...fields }));
      emittedRows++;
      return true;
    } catch (_) {
      if (emittedRows < MAX_ROWS) {
        console.info(`YT_MEDIA_PROBE_V1 kind=failure schema=1 nonce=${nonce} reason=row-format-error`);
        emittedRows++;
      }
      return false;
    }
  }

  function sleep(milliseconds) {
    return new Promise(resolve => global.setTimeout(resolve, milliseconds));
  }

  function qualityState() {
    const player = document.getElementById("movie_player");
    let selected = "unavailable";
    let available = "unavailable";
    let selectedApi = 0;
    let availableApi = 0;
    try {
      if (player && typeof player.getPlaybackQuality === "function") {
        selected = lib.boundedToken(player.getPlaybackQuality(), 32);
        selectedApi = 1;
      }
    } catch (_) {}
    try {
      if (player && typeof player.getAvailableQualityLevels === "function") {
        const levels = player.getAvailableQualityLevels();
        if (Array.isArray(levels)) {
          available = levels.slice(0, 16)
            .map(level => lib.boundedToken(level, 24))
            .join(",") || "empty";
          availableApi = 1;
        }
      }
    } catch (_) {}
    return { selected, available, selectedApi, availableApi };
  }

  function playbackQuality(video) {
    const result = { available: 0, total: -1, dropped: -1 };
    try {
      if (typeof video.getVideoPlaybackQuality === "function") {
        const quality = video.getVideoPlaybackQuality();
        const total = Number(quality.totalVideoFrames);
        const dropped = Number(quality.droppedVideoFrames);
        if (Number.isSafeInteger(total) && total >= 0 &&
            Number.isSafeInteger(dropped) && dropped >= 0) {
          result.available = 1;
          result.total = total;
          result.dropped = dropped;
        }
      }
    } catch (_) {}
    return result;
  }

  function webkitQuality(video) {
    const decoded = Number(video.webkitDecodedFrameCount);
    const dropped = Number(video.webkitDroppedFrameCount);
    if (Number.isSafeInteger(decoded) && decoded >= 0 &&
        Number.isSafeInteger(dropped) && dropped >= 0) {
      return { available: 1, decoded, dropped };
    }
    return { available: 0, decoded: -1, dropped: -1 };
  }

  function bufferedAhead(video) {
    try {
      const now = video.currentTime;
      for (let index = 0; index < video.buffered.length; index++) {
        if (video.buffered.start(index) <= now && now <= video.buffered.end(index))
          return Math.max(0, video.buffered.end(index) - now);
      }
    } catch (_) {}
    return 0;
  }

  async function waitForVideo() {
    for (let attempt = 0; attempt < 240; attempt++) {
      const video = document.querySelector("video");
      if (video && video.isConnected && video.videoWidth > 0 && video.videoHeight > 0)
        return video;
      await sleep(500);
    }
    return null;
  }

  async function run() {
    if (!lib) {
      console.info(`YT_MEDIA_PROBE_V1 kind=failure schema=1 nonce=${nonce} reason=library-missing`);
      return;
    }
    if (!nonceMatch) {
      emit("failure", { reason: "nonce-missing" });
      return;
    }

    const video = await waitForVideo();
    if (!video) {
      emit("failure", { reason: "video-timeout" });
      return;
    }

    const longTasks = { available: 0, count: 0, durationMs: 0 };
    let observer = null;
    try {
      observer = new PerformanceObserver(list => {
        for (const entry of list.getEntries()) {
          longTasks.count++;
          longTasks.durationMs += lib.finiteNumber(entry.duration, 0);
        }
      });
      observer.observe({ entryTypes: ["longtask"] });
      longTasks.available = 1;
    } catch (_) {}

    const rvfc = {
      available: typeof video.requestVideoFrameCallback === "function",
      callbacks: 0,
      scheduleFailures: 0,
      presentedFirst: -1,
      presentedLast: -1,
      presentedInvalidCallbacks: 0,
      presentedPairInvalid: 0,
      presentedRegressions: 0,
      presentedDuplicates: 0,
      priorPresented: -1,
      priorPresentedValid: false,
      mediaFirst: -1,
      mediaLast: -1,
      mediaInvalidCallbacks: 0,
      priorMedia: -1,
      priorMediaValid: false,
      deltasMs: [],
      active: true,
      callbackId: 0
    };

    function onVideoFrame(_now, metadata) {
      if (!rvfc.active)
        return;
      rvfc.callbacks++;

      const presented = Number(metadata.presentedFrames);
      const presentedValid = Number.isSafeInteger(presented) && presented >= 0;
      if (!presentedValid) rvfc.presentedInvalidCallbacks++;
      if (rvfc.callbacks === 1) {
        if (presentedValid) rvfc.presentedFirst = presented;
      } else if (!presentedValid || !rvfc.priorPresentedValid) {
        rvfc.presentedPairInvalid++;
      } else if (presented < rvfc.priorPresented) {
        rvfc.presentedRegressions++;
      } else if (presented === rvfc.priorPresented) {
        rvfc.presentedDuplicates++;
      }
      if (presentedValid) rvfc.presentedLast = presented;
      rvfc.priorPresented = presentedValid ? presented : -1;
      rvfc.priorPresentedValid = presentedValid;

      const media = Number(metadata.mediaTime);
      const mediaValid = Number.isFinite(media) && media >= 0;
      if (!mediaValid) rvfc.mediaInvalidCallbacks++;
      if (rvfc.callbacks === 1) {
        if (mediaValid) rvfc.mediaFirst = media;
      } else if (mediaValid && rvfc.priorMediaValid) {
        rvfc.deltasMs.push((media - rvfc.priorMedia) * 1000);
      } else {
        rvfc.deltasMs.push(Number.NaN);
      }
      if (mediaValid) rvfc.mediaLast = media;
      rvfc.priorMedia = mediaValid ? media : -1;
      rvfc.priorMediaValid = mediaValid;

      try {
        rvfc.callbackId = video.requestVideoFrameCallback(onVideoFrame);
      } catch (_) {
        rvfc.scheduleFailures++;
        rvfc.active = false;
      }
    }

    if (rvfc.available) {
      try {
        rvfc.callbackId = video.requestVideoFrameCallback(onVideoFrame);
      } catch (_) {
        rvfc.available = false;
        rvfc.scheduleFailures++;
      }
    }

    emit("start", {
      samples: SAMPLE_COUNT,
      interval_ms: SAMPLE_INTERVAL_MS,
      rvfc_available: rvfc.available ? 1 : 0,
      src_id: lib.sourceIdentity(video.currentSrc)
    });

    const widths = [];
    const heights = [];
    let firstVpq = null;
    let lastVpq = null;
    let firstWebkit = null;
    let lastWebkit = null;
    for (let sampleIndex = 1; sampleIndex <= SAMPLE_COUNT; sampleIndex++) {
      await sleep(SAMPLE_INTERVAL_MS);
      if (!video.isConnected) {
        emit("failure", { reason: `video-detached-${sampleIndex}` });
        rvfc.active = false;
        if (observer) observer.disconnect();
        return;
      }
      const quality = qualityState();
      const vpq = playbackQuality(video);
      const webkit = webkitQuality(video);
      if (!firstVpq) firstVpq = vpq;
      if (!firstWebkit) firstWebkit = webkit;
      lastVpq = vpq;
      lastWebkit = webkit;
      widths.push(video.videoWidth);
      heights.push(video.videoHeight);
      emit("sample", {
        index: sampleIndex,
        video_width: video.videoWidth,
        video_height: video.videoHeight,
        src_id: lib.sourceIdentity(video.currentSrc),
        current_time: lib.fixed(video.currentTime, 3, -1),
        playback_rate: lib.fixed(video.playbackRate, 3, -1),
        paused: video.paused ? 1 : 0,
        ended: video.ended ? 1 : 0,
        quality_selected: quality.selected,
        quality_available: quality.available,
        quality_selected_api: quality.selectedApi,
        quality_available_api: quality.availableApi,
        ready_state: video.readyState,
        network_state: video.networkState,
        buffered_ahead: lib.fixed(bufferedAhead(video), 3, -1),
        vpq_available: vpq.available,
        vpq_total: vpq.total,
        vpq_dropped: vpq.dropped,
        webkit_available: webkit.available,
        webkit_decoded: webkit.decoded,
        webkit_dropped: webkit.dropped,
        rvfc_available: rvfc.available ? 1 : 0,
        rvfc_callbacks: rvfc.callbacks,
        rvfc_presented: rvfc.presentedLast,
        rvfc_media_time: lib.fixed(rvfc.mediaLast, 6, -1),
        longtask_available: longTasks.available,
        longtask_count: longTasks.count,
        longtask_duration_ms: lib.fixed(longTasks.durationMs, 3, 0)
      });
    }

    rvfc.active = false;
    try {
      if (typeof video.cancelVideoFrameCallback === "function" && rvfc.callbackId)
        video.cancelVideoFrameCallback(rvfc.callbackId);
    } catch (_) {}
    if (observer) observer.disconnect();

    const cadence = lib.cadenceSummary(rvfc.deltasMs);
    const finalQuality = qualityState();
    emit("summary", {
      samples: SAMPLE_COUNT,
      width_min: Math.min(...widths),
      width_max: Math.max(...widths),
      height_min: Math.min(...heights),
      height_max: Math.max(...heights),
      src_id: lib.sourceIdentity(video.currentSrc),
      quality_selected: finalQuality.selected,
      quality_available: finalQuality.available,
      quality_selected_api: finalQuality.selectedApi,
      quality_available_api: finalQuality.availableApi,
      vpq_available: lastVpq ? lastVpq.available : 0,
      vpq_total_first: firstVpq ? firstVpq.total : -1,
      vpq_total_last: lastVpq ? lastVpq.total : -1,
      vpq_total_delta: firstVpq && lastVpq && firstVpq.total >= 0 && lastVpq.total >= 0
        ? lastVpq.total - firstVpq.total : -1,
      vpq_dropped_first: firstVpq ? firstVpq.dropped : -1,
      vpq_dropped_last: lastVpq ? lastVpq.dropped : -1,
      vpq_dropped_delta: firstVpq && lastVpq && firstVpq.dropped >= 0 && lastVpq.dropped >= 0
        ? lastVpq.dropped - firstVpq.dropped : -1,
      webkit_available: lastWebkit ? lastWebkit.available : 0,
      webkit_decoded_first: firstWebkit ? firstWebkit.decoded : -1,
      webkit_decoded_last: lastWebkit ? lastWebkit.decoded : -1,
      webkit_decoded_delta: firstWebkit && lastWebkit &&
        firstWebkit.decoded >= 0 && lastWebkit.decoded >= 0
        ? lastWebkit.decoded - firstWebkit.decoded : -1,
      webkit_dropped_first: firstWebkit ? firstWebkit.dropped : -1,
      webkit_dropped_last: lastWebkit ? lastWebkit.dropped : -1,
      webkit_dropped_delta: firstWebkit && lastWebkit &&
        firstWebkit.dropped >= 0 && lastWebkit.dropped >= 0
        ? lastWebkit.dropped - firstWebkit.dropped : -1,
      longtask_available: longTasks.available,
      longtask_count: longTasks.count,
      longtask_duration_ms: lib.fixed(longTasks.durationMs, 3, 0)
    });
    emit("rvfc_summary", {
      available: rvfc.available ? 1 : 0,
      callbacks: rvfc.callbacks,
      schedule_failures: rvfc.scheduleFailures,
      warmup_callbacks: 0,
      interval_slots: Math.max(0, rvfc.callbacks - 1),
      presented_first: rvfc.presentedFirst,
      presented_last: rvfc.presentedLast,
      presented_delta: rvfc.presentedFirst >= 0 && rvfc.presentedLast >= 0
        ? rvfc.presentedLast - rvfc.presentedFirst : -1,
      presented_invalid: rvfc.presentedInvalidCallbacks,
      presented_pair_invalid: rvfc.presentedPairInvalid,
      presented_regressions: rvfc.presentedRegressions,
      presented_duplicates: rvfc.presentedDuplicates,
      media_first: lib.fixed(rvfc.mediaFirst, 6, -1),
      media_last: lib.fixed(rvfc.mediaLast, 6, -1),
      media_delta: lib.fixed(rvfc.mediaFirst >= 0 && rvfc.mediaLast >= 0
        ? rvfc.mediaLast - rvfc.mediaFirst : -1, 6, -1),
      media_invalid: rvfc.mediaInvalidCallbacks,
      interval_count: cadence.intervalCount,
      interval_valid: cadence.validCount,
      interval_invalid: cadence.invalidCount,
      interval_positive: cadence.positiveCount,
      interval_regressions: cadence.regressionCount,
      interval_duplicates: cadence.duplicateCount,
      interval_sum_ms: lib.fixed(cadence.intervalSumMs, 3, 0),
      median_ms: lib.fixed(cadence.medianMs, 3, -1),
      near60_count: cadence.near60Count,
      hist: cadence.histogramText
    });
    emit("done", {
      status: "complete",
      samples: SAMPLE_COUNT,
      rows: emittedRows + 1
    });
  }

  run().catch(() => emit("failure", { reason: "uncaught-probe-error" }));
})(globalThis);
