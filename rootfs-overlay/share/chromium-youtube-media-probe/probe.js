(function runXv6YouTubeMediaProbe(global) {
  "use strict";

  // Diagnostic-only entry evidence.  This intentionally precedes both the
  // per-document guard and URL/nonce parsing, so it must never claim a nonce
  // or carry page/URL data.  The host treats duplicates as invalid and never
  // uses this marker for render or FPS credit.
  console.info("YT_MEDIA_PROBE_STAGE_V1 kind=producer_boot schema=1");

  if (global.__xv6YouTubeMediaProbeStarted)
    return;
  global.__xv6YouTubeMediaProbeStarted = true;

  const lib = global.__xv6YouTubeMediaProbeLib;
  const nonceMatch = /^#xv6ytprobe=([0-9a-f]{32})(?:&xv6ythd720=(1))?(?:&xv6ytcapturediag=(1))?$/.exec(
    global.location.hash);
  const nonce = nonceMatch ? nonceMatch[1] : "unavailable";
  const forceHd720 = Boolean(nonceMatch && nonceMatch[2] === "1");
  const captureCompletenessDiagnostic = Boolean(nonceMatch && nonceMatch[3] === "1");
  const requestedDisplayMode = new URLSearchParams(global.location.search)
    .get("xv6ytmode");
  const MAX_ROWS = forceHd720 ? 30 : 24;
  const MAX_DIAGNOSTIC_ROWS = 64;
  const MAX_DIAGNOSTIC_ROW_BYTES = 320;
  const SAMPLE_COUNT = 20;
  const SAMPLE_INTERVAL_MS = 1000;
  const FORCE_READY_ATTEMPTS = 120;
  const FORCE_READY_INTERVAL_MS = 500;
  // A quality change can make YouTube replace its provisional <video> after
  // several seconds.  Keep this deadline long enough to observe the final
  // element, but admit only one stable identity with real media progress.
  const FORCE_OBSERVE_ATTEMPTS = 120;
  const FORCE_OBSERVE_INTERVAL_MS = 250;
  const FORCE_STABLE_OBSERVATIONS = 4;
  const POST_MODE_OBSERVE_ATTEMPTS = 120;
  const POST_MODE_OBSERVE_INTERVAL_MS = 250;
  const POST_MODE_STABLE_OBSERVATIONS = 16;
  const POST_MODE_TIMEOUT_MS = 30000;
  let emittedRows = 0;
  let producerReadyEmitted = false;

  function emitProducerReady() {
    if (producerReadyEmitted)
      return false;
    producerReadyEmitted = true;
    console.info(
      `YT_MEDIA_PROBE_STAGE_V1 kind=producer_ready schema=1 nonce=${nonce} url_arm=canonical force_hd720=${forceHd720 ? 1 : 0} capture_diag=${captureCompletenessDiagnostic ? 1 : 0}`);
    return true;
  }

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

  // This is intentionally a different wire marker from semantic media rows.
  // The host captures it through a separate nonce-bound transaction, so these
  // facts cannot alter semantic row count, source classification, or FPS
  // credit.  A console failure suppresses DONE: the host must report only an
  // incomplete/invalid diagnostic, never a fabricated complete observation.
  const diagnostic = {
    enabled: captureCompletenessDiagnostic,
    failed: false,
    rows: 0,
    arms: 0,
    fires: 0,
    rvfcCheckpoints: 0,
    currentSeq: 0,
    lastRvfcSeq: 0
  };

  // V2 is a separate diagnostic-only lifecycle.  Keep V1 emission intact so
  // its retained transport corpus stays byte-stable; the host selects V2
  // explicitly and never mixes either marker family into media semantics.
  const diagnosticV2 = {
    enabled: captureCompletenessDiagnostic,
    failed: false,
    rows: 0,
    arms: 0,
    fires: 0,
    rvfcCheckpoints: 0,
    currentSeq: 0,
    lastRvfcSeq: 0,
    started: false,
    postVideoHd720Ready: false,
    postVideoProof: null
  };

  function emitDiagnosticV2(kind, fields) {
    if (!diagnosticV2.enabled || diagnosticV2.failed ||
        diagnosticV2.rows >= MAX_DIAGNOSTIC_ROWS)
      return false;
    try {
      const pieces = [
        "YT_CAPTURE_COMPLETENESS_V2",
        `kind=${lib.boundedToken(kind, 24)}`,
        "schema=2",
        `nonce=${nonce}`
      ];
      for (const [key, rawValue] of Object.entries(fields)) {
        if (!/^[a-z][a-z0-9_]*$/.test(key))
          throw new Error("invalid diagnostic V2 key");
        pieces.push(`${key}=${lib.boundedToken(rawValue, 64)}`);
      }
      const row = pieces.join(" ");
      if (row.length > MAX_DIAGNOSTIC_ROW_BYTES)
        throw new Error("diagnostic V2 row exceeds bound");
      console.info(row);
      diagnosticV2.rows++;
      return true;
    } catch (_) {
      diagnosticV2.failed = true;
      return false;
    }
  }

  // `producer_start` is liveness only.  The host must not admit collection
  // from it: the only V2 admission is emitted below after the bounded HD720
  // selector has itself established active 1280x720 playback stability.
  function diagnosticV2ProducerStart() {
    if (!diagnosticV2.enabled || diagnosticV2.failed || diagnosticV2.started)
      return;
    diagnosticV2.started = emitDiagnosticV2("producer_start", {});
  }

  function diagnosticV2PostVideoHd720Ready(proof) {
    if (!diagnosticV2.enabled || diagnosticV2.failed ||
        diagnosticV2.postVideoHd720Ready || !proof ||
        proof.selected !== "hd720" || proof.videoWidth !== 1280 ||
        proof.videoHeight !== 720 || proof.stableCount < FORCE_STABLE_OBSERVATIONS ||
        !proof.selectorPrerequisites || !proof.rangeInvoked || proof.rangeThrew ||
        !proof.qualityInvoked || proof.qualityThrew || !proof.progressed)
      return false;
    diagnosticV2.postVideoHd720Ready = emitDiagnosticV2("post_video_hd720_ready", {
      selected: proof.selected,
      video_width: proof.videoWidth,
      video_height: proof.videoHeight,
      stable_count: proof.stableCount
    });
    return diagnosticV2.postVideoHd720Ready;
  }

  function emitDiagnostic(kind, fields) {
    if (!diagnostic.enabled || diagnostic.failed ||
        diagnostic.rows >= MAX_DIAGNOSTIC_ROWS)
      return false;
    try {
      const pieces = [
        "YT_CAPTURE_COMPLETENESS_V1",
        `kind=${lib.boundedToken(kind, 24)}`,
        "schema=1",
        `nonce=${nonce}`
      ];
      for (const [key, rawValue] of Object.entries(fields)) {
        if (!/^[a-z][a-z0-9_]*$/.test(key))
          throw new Error("invalid diagnostic key");
        pieces.push(`${key}=${lib.boundedToken(rawValue, 64)}`);
      }
      const row = pieces.join(" ");
      if (row.length > MAX_DIAGNOSTIC_ROW_BYTES)
        throw new Error("diagnostic row exceeds bound");
      console.info(row);
      diagnostic.rows++;
      return true;
    } catch (_) {
      diagnostic.failed = true;
      return false;
    }
  }

  function diagnosticTimerArm(seq, rvfc) {
    if (!diagnostic.enabled || diagnostic.failed)
      return;
    diagnostic.currentSeq = seq;
    if (emitDiagnostic("timer_arm", {
      seq,
      rvfc_available: rvfc.available ? 1 : 0,
      rvfc_callbacks: rvfc.callbacks
    })) {
      diagnostic.arms++;
    }
    diagnosticV2.currentSeq = seq;
    if (diagnosticV2.postVideoHd720Ready && emitDiagnosticV2("timer_arm", {
      seq,
      rvfc_available: rvfc.available ? 1 : 0,
      rvfc_callbacks: rvfc.callbacks
    })) {
      diagnosticV2.arms++;
    }
  }

  function diagnosticTimerFire(seq, rvfc) {
    if (!diagnostic.enabled || diagnostic.failed)
      return;
    if (emitDiagnostic("timer_fire", {
      seq,
      rvfc_available: rvfc.available ? 1 : 0,
      rvfc_callbacks: rvfc.callbacks
    })) {
      diagnostic.fires++;
    }
    if (diagnosticV2.postVideoHd720Ready && emitDiagnosticV2("timer_fire", {
      seq,
      rvfc_available: rvfc.available ? 1 : 0,
      rvfc_callbacks: rvfc.callbacks
    })) {
      diagnosticV2.fires++;
    }
  }

  function diagnosticRvfcCheckpoint(rvfc) {
    const seq = diagnostic.currentSeq;
    if (!diagnostic.enabled || diagnostic.failed || !seq ||
        diagnostic.lastRvfcSeq === seq)
      return;
    if (emitDiagnostic("rvfc_checkpoint", {
      seq,
      rvfc_callbacks: rvfc.callbacks
    })) {
      diagnostic.lastRvfcSeq = seq;
      diagnostic.rvfcCheckpoints++;
    }
    const v2Seq = diagnosticV2.currentSeq;
    if (diagnosticV2.postVideoHd720Ready && v2Seq && diagnosticV2.lastRvfcSeq !== v2Seq &&
        emitDiagnosticV2("rvfc_checkpoint", {
          seq: v2Seq,
          rvfc_callbacks: rvfc.callbacks
        })) {
      diagnosticV2.lastRvfcSeq = v2Seq;
      diagnosticV2.rvfcCheckpoints++;
    }
  }

  function diagnosticDone() {
    if (!diagnostic.enabled || diagnostic.failed)
      return;
    emitDiagnostic("done", {
      status: "complete",
      arms: diagnostic.arms,
      fires: diagnostic.fires,
      rvfc_checkpoints: diagnostic.rvfcCheckpoints,
      rows: diagnostic.rows + 1
    });
    if (diagnosticV2.postVideoHd720Ready) {
      emitDiagnosticV2("done", {
        status: "complete",
        arms: diagnosticV2.arms,
        fires: diagnosticV2.fires,
        rvfc_checkpoints: diagnosticV2.rvfcCheckpoints,
        rows: diagnosticV2.rows + 1
      });
    }
  }

  function sleep(milliseconds) {
    return new Promise(resolve => global.setTimeout(resolve, milliseconds));
  }

  function requestPlayback(video) {
    try {
      const pending = video.play();
      if (pending && typeof pending.catch === "function")
        pending.catch(() => {});
    } catch (_) {}
  }

  function emitCodecState() {
    const player = document.getElementById("movie_player");
    let statsApi = 0;
    let fmt = "unavailable";
    let afmt = "unavailable";
    try {
      if (player && typeof player.getVideoStats === "function") {
        const stats = player.getVideoStats();
        statsApi = 1;
        if (stats && typeof stats === "object") {
          fmt = lib.boundedToken(stats.fmt ?? "unavailable", 24);
          afmt = lib.boundedToken(stats.afmt ?? "unavailable", 24);
        }
      }
    } catch (_) {}
    console.info(
      `YT_CODEC_STATE_V1 schema=1 nonce=${nonce} stats_api=${statsApi} fmt=${fmt} afmt=${afmt}`);
  }

  function qualityState(playerOverride) {
    const player = playerOverride || document.getElementById("movie_player");
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

  function qualityIncludes(available, wanted) {
    return String(available).split(",").includes(wanted);
  }

  function returnType(value) {
    if (value === null)
      return "null";
    return lib.boundedToken(typeof value, 16);
  }

  /*
   * This routine emits bounded raw facts only.  In particular, neither its
   * loop exit nor any emitted value grants quality/cadence credit; the host
   * parser owns the exact arm, API, hd720, stability and progress verdicts.
   */
  async function exerciseHd720Selector() {
    let player = null;
    let video = null;
    let quality = {
      selected: "unavailable", available: "unavailable",
      selectedApi: 0, availableApi: 0
    };
    let readyAttempt = 0;
    let rangePresent = 0;
    let qualityPresent = 0;
    let selectorPrerequisites = false;
    let playerStateApi = 0;
    let playerState = -1;

    for (readyAttempt = 1; readyAttempt <= FORCE_READY_ATTEMPTS; readyAttempt++) {
      player = document.getElementById("movie_player");
      video = document.querySelector("video");
      quality = qualityState(player);
      rangePresent = player && typeof player.setPlaybackQualityRange === "function" ? 1 : 0;
      qualityPresent = player && typeof player.setPlaybackQuality === "function" ? 1 : 0;
      playerStateApi = player && typeof player.getPlayerState === "function" ? 1 : 0;
      playerState = -1;
      if (playerStateApi) {
        try {
          playerState = Number(player.getPlayerState());
          if (!Number.isSafeInteger(playerState))
            playerState = -1;
        } catch (_) {
          playerState = -1;
        }
      }
      selectorPrerequisites = Boolean(
        player && video && video.isConnected && video.videoWidth > 0 &&
        video.videoHeight > 0 && video.readyState >= 2 &&
        playerStateApi && playerState === 1 &&
        quality.selectedApi && quality.availableApi &&
        qualityIncludes(quality.available, "hd720") &&
        rangePresent && qualityPresent);
      if (selectorPrerequisites)
        break;
      if (readyAttempt < FORCE_READY_ATTEMPTS)
        await sleep(FORCE_READY_INTERVAL_MS);
    }
    if (readyAttempt > FORCE_READY_ATTEMPTS)
      readyAttempt = FORCE_READY_ATTEMPTS;

    emit("force_ready", {
      attempt: readyAttempt,
      player_present: player ? 1 : 0,
      player_state_api: playerStateApi,
      player_state: playerState,
      video_present: video && video.isConnected ? 1 : 0,
      ready_state: video ? video.readyState : -1,
      video_width: video ? video.videoWidth : 0,
      video_height: video ? video.videoHeight : 0,
      selected_api: quality.selectedApi,
      available_api: quality.availableApi,
      selected: quality.selected,
      available: quality.available,
      range_api: rangePresent,
      quality_api: qualityPresent
    });

    emit("force_attempt", {
      seq: 1,
      api: "setPlaybackQualityRange",
      arg_min: "hd720",
      arg_max: "hd720",
      present: rangePresent
    });
    let rangeInvoked = 0;
    let rangeThrew = 0;
    let rangeReturn;
    if (rangePresent && selectorPrerequisites) {
      rangeInvoked = 1;
      try {
        rangeReturn = player.setPlaybackQualityRange("hd720", "hd720");
      } catch (_) {
        rangeThrew = 1;
      }
    }
    emit("force_result", {
      seq: 1,
      api: "setPlaybackQualityRange",
      invoked: rangeInvoked,
      threw: rangeThrew,
      return_type: rangeInvoked && !rangeThrew ? returnType(rangeReturn) : "unavailable"
    });

    emit("force_attempt", {
      seq: 2,
      api: "setPlaybackQuality",
      arg_min: "hd720",
      arg_max: "hd720",
      present: qualityPresent
    });
    let qualityInvoked = 0;
    let qualityThrew = 0;
    let qualityReturn;
    if (qualityPresent && selectorPrerequisites) {
      qualityInvoked = 1;
      try {
        qualityReturn = player.setPlaybackQuality("hd720");
      } catch (_) {
        qualityThrew = 1;
      }
    }
    emit("force_result", {
      seq: 2,
      api: "setPlaybackQuality",
      invoked: qualityInvoked,
      threw: qualityThrew,
      return_type: qualityInvoked && !qualityThrew ? returnType(qualityReturn) : "unavailable"
    });

    let observeAttempt = 0;
    let stableCount = 0;
    let candidateVideo = null;
    let firstTime = -1;
    let lastTime = -1;
    for (observeAttempt = 1;
         observeAttempt <= FORCE_OBSERVE_ATTEMPTS;
         observeAttempt++) {
      player = document.getElementById("movie_player");
      video = document.querySelector("video");
      quality = qualityState(player);
      const observedTime = video ? lib.finiteNumber(video.currentTime, -1) : -1;
      const rawMatch = Boolean(
        video && video.isConnected && quality.selectedApi &&
        quality.availableApi && quality.selected === "hd720" &&
        qualityIncludes(quality.available, "hd720") &&
        video.videoWidth === 1280 && video.videoHeight === 720 &&
        video.readyState >= 2 && !video.paused && !video.ended);
      if (rawMatch) {
        if (video !== candidateVideo) {
          candidateVideo = video;
          firstTime = observedTime;
          stableCount = 1;
        } else {
          stableCount++;
        }
        lastTime = observedTime;
      } else {
        candidateVideo = null;
        firstTime = -1;
        lastTime = -1;
        stableCount = 0;
      }
      if (stableCount >= FORCE_STABLE_OBSERVATIONS &&
          firstTime >= 0 && lastTime - firstTime >= 0.5)
        break;
      if (observeAttempt < FORCE_OBSERVE_ATTEMPTS)
        await sleep(FORCE_OBSERVE_INTERVAL_MS);
    }
    if (observeAttempt > FORCE_OBSERVE_ATTEMPTS)
      observeAttempt = FORCE_OBSERVE_ATTEMPTS;
    emit("force_observation", {
      attempt: observeAttempt,
      stable_count: stableCount,
      selected_api: quality.selectedApi,
      available_api: quality.availableApi,
      selected: quality.selected,
      available: quality.available,
      video_width: video ? video.videoWidth : 0,
      video_height: video ? video.videoHeight : 0,
      ready_state: video ? video.readyState : -1,
      paused: video && video.paused ? 1 : 0,
      ended: video && video.ended ? 1 : 0,
      current_time_first: lib.fixed(firstTime, 3, -1),
      current_time_last: lib.fixed(lastTime, 3, -1)
    });

    diagnosticV2.postVideoProof = {
      selected: quality.selected,
      videoWidth: candidateVideo ? candidateVideo.videoWidth : 0,
      videoHeight: candidateVideo ? candidateVideo.videoHeight : 0,
      stableCount,
      selectorPrerequisites,
      rangeInvoked,
      rangeThrew,
      qualityInvoked,
      qualityThrew,
      progressed: firstTime >= 0 && lastTime - firstTime >= 0.5
    };
    return diagnosticV2.postVideoProof.selected === "hd720" &&
      diagnosticV2.postVideoProof.videoWidth === 1280 &&
      diagnosticV2.postVideoProof.videoHeight === 720 &&
      diagnosticV2.postVideoProof.stableCount >= FORCE_STABLE_OBSERVATIONS &&
      diagnosticV2.postVideoProof.progressed && candidateVideo &&
      candidateVideo.isConnected ? candidateVideo : null;
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

  function displayModeMatches(mode) {
    const player = document.getElementById("movie_player");
    const fullscreenElement = document.fullscreenElement;
    const documentFullscreen = Boolean(fullscreenElement);
    const playerFullscreen = Boolean(player && fullscreenElement &&
      (fullscreenElement === player || fullscreenElement.contains(player) ||
       player.contains(fullscreenElement)));
    if (mode === "fullscreen")
      return documentFullscreen && playerFullscreen;
    if (mode === "windowed")
      return !documentFullscreen && !playerFullscreen;
    return false;
  }

  function emitDisplayState(video) {
    const player = document.getElementById("movie_player");
    const fullscreenElement = document.fullscreenElement;
    const playerFullscreen = Boolean(player && fullscreenElement &&
      (fullscreenElement === player || fullscreenElement.contains(player) ||
       player.contains(fullscreenElement)));
    console.info(`YT_DISPLAY_STATE_V1 monotonic_ms=${Math.floor(performance.timeOrigin + performance.now())} mode=${requestedDisplayMode || "unspecified"} document_fullscreen=${fullscreenElement ? 1 : 0} player_fullscreen=${playerFullscreen ? 1 : 0} video_playing=${!video.paused && !video.ended ? 1 : 0} video_muted=${video.muted ? 1 : 0} video_volume=${video.volume.toFixed(3)} video_width=${video.videoWidth} video_height=${video.videoHeight}`);
  }

  function emitWallBoundary(kind) {
    console.info(`YT_MEDIA_WALL_V1 kind=${kind} schema=1 nonce=${nonce} monotonic_ms=${Math.floor(performance.timeOrigin + performance.now())}`);
  }

  /*
   * The host drives fullscreen through CDP with a real user-gesture context.
   * Do not let the semantic 20-second window start until the requested state
   * is established.  This keeps a slow host control round trip from turning a
   * nominal fullscreen trial into a mostly-windowed sample.  Absence of the
   * parameter preserves the historical probe contract.
   */
  async function waitForRequestedDisplayMode() {
    if (requestedDisplayMode === null)
      return true;
    if (requestedDisplayMode !== "windowed" &&
        requestedDisplayMode !== "fullscreen")
      return false;
    for (let attempt = 0; attempt < 120; attempt++) {
      if (displayModeMatches(requestedDisplayMode))
        return true;
      await sleep(500);
    }
    return false;
  }

  /*
   * Fullscreen and a forced representation change can each make YouTube swap
   * the media element after the corresponding API call has already returned.
   * Establish the semantic start boundary only after the requested display
   * mode owns one advancing element for four continuous seconds.  This gate
   * is identical on Linux and xv6; it does not alter or reset the subsequent
   * 20-second wall-clock observation.
   */
  async function settleVideoAfterDisplayMode(initialVideo) {
    let candidate = initialVideo;
    let stableCount = 0;
    let firstTime = -1;
    let lastTime = -1;
    let replacements = 0;
    const deadline = performance.now() + POST_MODE_TIMEOUT_MS;
    for (let attempt = 1; attempt <= POST_MODE_OBSERVE_ATTEMPTS; attempt++) {
      const observed = document.querySelector("video");
      if (observed && observed !== candidate) {
        candidate = observed;
        replacements++;
        stableCount = 0;
        firstTime = -1;
        lastTime = -1;
        try {
          candidate.muted = false;
          candidate.volume = 0.5;
        } catch (_) {}
        requestPlayback(candidate);
        console.info(
          `YT_MEDIA_VIDEO_SETTLE_REBIND_V1 schema=1 nonce=${nonce} attempt=${attempt} replacements=${replacements} current_time=${lib.fixed(candidate.currentTime, 3, -1)}`);
      }
      const quality = qualityState();
      const observedTime = candidate
        ? lib.finiteNumber(candidate.currentTime, -1) : -1;
      const valid = Boolean(
        candidate && candidate.isConnected && candidate.videoWidth > 0 &&
        candidate.videoHeight > 0 && candidate.readyState >= 2 &&
        !candidate.paused && !candidate.ended &&
        candidate.playbackRate >= 0.95 && candidate.playbackRate <= 1.05 &&
        (requestedDisplayMode === null ||
          displayModeMatches(requestedDisplayMode)) &&
        (!forceHd720 || (candidate.videoWidth === 1280 &&
          candidate.videoHeight === 720 && quality.selectedApi &&
          quality.availableApi && quality.selected === "hd720" &&
          qualityIncludes(quality.available, "hd720"))));
      if (valid) {
        if (stableCount === 0)
          firstTime = observedTime;
        stableCount++;
        lastTime = observedTime;
      } else {
        stableCount = 0;
        firstTime = -1;
        lastTime = -1;
      }
      if (stableCount >= POST_MODE_STABLE_OBSERVATIONS &&
          firstTime >= 0 && lastTime - firstTime >= 2.0) {
        return { video: candidate, stableCount, replacements,
          firstTime, lastTime, attempt };
      }
      const remainingMs = deadline - performance.now();
      if (attempt >= POST_MODE_OBSERVE_ATTEMPTS || remainingMs <= 0)
        break;
      await sleep(Math.min(POST_MODE_OBSERVE_INTERVAL_MS, remainingMs));
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

    // Library availability and the canonical nonce/arm URL parse are now
    // proven.  This remains diagnostic-only and precedes player discovery.
    emitProducerReady();

    // This is deliberately before video discovery but is liveness only.
    diagnosticV2ProducerStart();

    let video = forceHd720 ? await exerciseHd720Selector() : await waitForVideo();
    if (!video) {
      emit("failure", { reason: "video-timeout" });
      return;
    }
    // Keep the audio condition identical across the Linux reference and xv6.
    // Autoplay policy can otherwise leave a valid advancing video silently
    // muted, which would measure video-only throughput instead of A/V parity.
    try {
      video.muted = false;
      video.volume = 0.5;
    } catch (_) {}
    requestPlayback(video);
    if (forceHd720)
      diagnosticV2PostVideoHd720Ready(diagnosticV2.postVideoProof);
    if (!(await waitForRequestedDisplayMode())) {
      emit("failure", { reason: "display-mode-timeout" });
      return;
    }
    const settled = await settleVideoAfterDisplayMode(video);
    if (!settled) {
      emit("failure", { reason: "post-mode-video-timeout" });
      return;
    }
    video = settled.video;
    console.info(
      `YT_MEDIA_VIDEO_SETTLE_V1 schema=1 nonce=${nonce} attempt=${settled.attempt} stable_count=${settled.stableCount} replacements=${settled.replacements} current_time_first=${lib.fixed(settled.firstTime, 3, -1)} current_time_last=${lib.fixed(settled.lastTime, 3, -1)}`);
    const startSourceIdentity = lib.sourceIdentity(video.currentSrc);
    let videoGeneration = 0;
    let videoRebinds = 0;
    let lastSampleTime = lib.finiteNumber(video.currentTime, -1);
    // Diagnostic-only selected-format evidence. It is outside the semantic V1
    // row family, so absence or an unavailable internal API grants no credit.
    emitCodecState();

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
      callbackId: 0,
      generation: 0
    };

    /*
     * YouTube can replace the media element while recovering an output
     * stream.  rVFC counters belong to an element, so each attachment keeps
     * its raw counter origin private and maps its first callback onto the
     * last observed value.  That incomparable generation anchor is not a
     * semantic callback or interval and earns no presentation credit; only
     * later raw deltas from the replacement can advance the cumulative
     * result.  This preserves the summary invariant that every counted
     * interval is an actual within-element interval.
     */
    function attachRvfc(target) {
      if (typeof target.requestVideoFrameCallback !== "function")
        return false;
      const generation = ++rvfc.generation;
      let firstCallback = true;
      let presentedOffset = 0;
      let mediaOffset = 0;
      function onVideoFrame(_now, metadata) {
        if (!rvfc.active || generation !== rvfc.generation)
          return;
        const rawPresented = Number(metadata.presentedFrames);
        const rawPresentedValid = Number.isSafeInteger(rawPresented) &&
          rawPresented >= 0;
        const rawMedia = Number(metadata.mediaTime);
        const rawMediaValid = Number.isFinite(rawMedia) && rawMedia >= 0;
        const generationAnchor = firstCallback && generation > 1;
        if (firstCallback) {
          if (rawPresentedValid && rvfc.priorPresentedValid)
            presentedOffset = rvfc.priorPresented - rawPresented;
          if (rawMediaValid && rvfc.priorMediaValid)
            mediaOffset = rvfc.priorMedia - rawMedia;
          firstCallback = false;
        }
        const presented = rawPresentedValid
          ? rawPresented + presentedOffset : rawPresented;
        const presentedValid = Number.isSafeInteger(presented) && presented >= 0;
        const media = rawMediaValid ? rawMedia + mediaOffset : rawMedia;
        const mediaValid = Number.isFinite(media) && media >= 0;

        if (generationAnchor && rvfc.callbacks > 0 && rawPresentedValid &&
            rvfc.priorPresentedValid && rawMediaValid &&
            rvfc.priorMediaValid) {
          try {
            rvfc.callbackId = target.requestVideoFrameCallback(onVideoFrame);
          } catch (_) {
            rvfc.scheduleFailures++;
            rvfc.active = false;
          }
          return;
        }

        rvfc.callbacks++;
        diagnosticRvfcCheckpoint(rvfc);
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
          rvfc.callbackId = target.requestVideoFrameCallback(onVideoFrame);
        } catch (_) {
          rvfc.scheduleFailures++;
          rvfc.active = false;
        }
      }
      try {
        rvfc.callbackId = target.requestVideoFrameCallback(onVideoFrame);
        return true;
      } catch (_) {
        rvfc.scheduleFailures++;
        return false;
      }
    }

    if (rvfc.available && !attachRvfc(video))
      rvfc.available = false;

    emitWallBoundary("start");
    emit("start", {
      samples: SAMPLE_COUNT,
      interval_ms: SAMPLE_INTERVAL_MS,
      rvfc_available: rvfc.available ? 1 : 0,
      src_id: startSourceIdentity
    });

    const widths = [];
    const heights = [];
    let firstVpq = null;
    let lastVpq = null;
    let firstWebkit = null;
    let lastWebkit = null;
    const vpqNormalizer = {
      generation: -1, firstOffset: 0, secondOffset: 0, last: null
    };
    const webkitNormalizer = {
      generation: -1, firstOffset: 0, secondOffset: 0, last: null
    };
    function normalizeCounters(raw, state, firstKey, secondKey) {
      if (!raw.available)
        return raw;
      if (state.generation !== videoGeneration) {
        state.generation = videoGeneration;
        state.firstOffset = state.last ? state.last[firstKey] - raw[firstKey] : 0;
        state.secondOffset = state.last ? state.last[secondKey] - raw[secondKey] : 0;
      }
      const normalized = {
        ...raw,
        [firstKey]: raw[firstKey] + state.firstOffset,
        [secondKey]: raw[secondKey] + state.secondOffset
      };
      state.last = normalized;
      return normalized;
    }
    for (let sampleIndex = 1; sampleIndex <= SAMPLE_COUNT; sampleIndex++) {
      diagnosticTimerArm(sampleIndex, rvfc);
      await sleep(SAMPLE_INTERVAL_MS);
      diagnosticTimerFire(sampleIndex, rvfc);
      if (!video.isConnected) {
        const oldVideo = video;
        const replacement = document.querySelector("video");
        if (replacement && replacement !== oldVideo) {
          try {
            replacement.muted = false;
            replacement.volume = 0.5;
          } catch (_) {}
          requestPlayback(replacement);
        }
        const replacementTime = replacement
          ? lib.finiteNumber(replacement.currentTime, -1) : -1;
        const replacementQuality = qualityState();
        const replacementPresent = Boolean(replacement);
        const replacementDifferent = Boolean(replacement &&
          replacement !== oldVideo);
        const replacementConnected = Boolean(replacement &&
          replacement.isConnected);
        const replacementSourceMatch = Boolean(replacement &&
          lib.sourceIdentity(replacement.currentSrc) === startSourceIdentity);
        const replacementQualityOk = Boolean(!forceHd720 || (replacement &&
          replacement.videoWidth === 1280 && replacement.videoHeight === 720 &&
          replacementQuality.selectedApi && replacementQuality.availableApi &&
          replacementQuality.selected === "hd720" &&
          qualityIncludes(replacementQuality.available, "hd720")));
        const replacementDisplayOk = requestedDisplayMode === null ||
          displayModeMatches(requestedDisplayMode);
        const replacementTimeNonregress = replacementTime >= lastSampleTime;
        const replacementValid = Boolean(
          replacementPresent && replacementDifferent && replacementConnected &&
          replacementSourceMatch &&
          replacement.videoWidth > 0 && replacement.videoHeight > 0 &&
          replacementQualityOk &&
          replacement.readyState >= 2 && !replacement.paused &&
          !replacement.ended && replacement.playbackRate >= 0.95 &&
          replacement.playbackRate <= 1.05 &&
          replacementTimeNonregress && replacementDisplayOk &&
          videoRebinds < 4);
        if (!replacementValid) {
          console.info(
            `YT_MEDIA_VIDEO_REBIND_REJECT_V1 schema=1 nonce=${nonce} sample=${sampleIndex} present=${replacementPresent ? 1 : 0} different=${replacementDifferent ? 1 : 0} connected=${replacementConnected ? 1 : 0} source_match=${replacementSourceMatch ? 1 : 0} width=${replacement ? replacement.videoWidth : 0} height=${replacement ? replacement.videoHeight : 0} quality_ok=${replacementQualityOk ? 1 : 0} ready_state=${replacement ? replacement.readyState : -1} paused=${replacement && replacement.paused ? 1 : 0} ended=${replacement && replacement.ended ? 1 : 0} playback_rate=${lib.fixed(replacement ? replacement.playbackRate : -1, 3, -1)} replacement_time=${lib.fixed(replacementTime, 3, -1)} last_sample_time=${lib.fixed(lastSampleTime, 3, -1)} time_nonregress=${replacementTimeNonregress ? 1 : 0} display_ok=${replacementDisplayOk ? 1 : 0} rebind_count=${videoRebinds}`);
          emit("failure", { reason: `video-replacement-invalid-${sampleIndex}` });
          rvfc.active = false;
          if (observer) observer.disconnect();
          return;
        }
        try {
          if (typeof oldVideo.cancelVideoFrameCallback === "function" &&
              rvfc.callbackId)
            oldVideo.cancelVideoFrameCallback(rvfc.callbackId);
        } catch (_) {}
        video = replacement;
        videoGeneration++;
        videoRebinds++;
        rvfc.active = true;
        if (rvfc.available && !attachRvfc(video)) {
          emit("failure", { reason: `video-replacement-rvfc-${sampleIndex}` });
          rvfc.active = false;
          if (observer) observer.disconnect();
          return;
        }
        console.info(
          `YT_MEDIA_VIDEO_REBIND_V1 schema=1 nonce=${nonce} sample=${sampleIndex} generation=${videoGeneration} current_time=${lib.fixed(replacementTime, 3, -1)} source_id=${startSourceIdentity}`);
      }
      // YouTube can restore its persisted volume shortly after autoplay.
      // Keep every accepted Linux/xv6 sample on the declared unmuted 50%
      // A/V contract instead of silently measuring a different audio load.
      try {
        if (video.muted)
          video.muted = false;
        if (Math.abs(video.volume - 0.5) > 0.001)
          video.volume = 0.5;
      } catch (_) {}
      const quality = qualityState();
      const vpq = normalizeCounters(playbackQuality(video), vpqNormalizer,
        "total", "dropped");
      const webkit = normalizeCounters(webkitQuality(video), webkitNormalizer,
        "decoded", "dropped");
      if (!firstVpq) firstVpq = vpq;
      if (!firstWebkit) firstWebkit = webkit;
      lastVpq = vpq;
      lastWebkit = webkit;
      widths.push(video.videoWidth);
      heights.push(video.videoHeight);
      lastSampleTime = lib.finiteNumber(video.currentTime, lastSampleTime);
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
      emitDisplayState(video);
    }

    emitWallBoundary("end");

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
    diagnosticDone();
  }

  run().catch(() => emit("failure", { reason: "uncaught-probe-error" }));
})(globalThis);
