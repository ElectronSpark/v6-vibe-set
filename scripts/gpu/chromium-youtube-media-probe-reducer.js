#!/usr/bin/env node
"use strict";

const assert = require("assert");
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const root = path.resolve(__dirname, "../..");
const libraryPath = path.join(root,
  "rootfs-overlay/share/chromium-youtube-media-probe/probe-lib.js");
const manifestPath = path.join(root,
  "rootfs-overlay/share/chromium-youtube-media-probe/manifest.json");
const probePath = path.join(root,
  "rootfs-overlay/share/chromium-youtube-media-probe/probe.js");
const context = {
  URL,
  location: { href: "https://www.youtube.com/watch?v=fixture" }
};
context.globalThis = context;
vm.runInNewContext(fs.readFileSync(libraryPath, "utf8"), context,
  { filename: libraryPath });
const lib = context.__xv6YouTubeMediaProbeLib;

assert(lib, "probe library was not installed");
const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
const publicKey = Buffer.from(manifest.key, "base64");
const extensionId = [...crypto.createHash("sha256").update(publicKey).digest().subarray(0, 16)]
  .map(byte => String.fromCharCode(97 + (byte >>> 4), 97 + (byte & 15)))
  .join("");
assert.strictEqual(extensionId, "edfilgocpdgbkehcgdillfgnnhclphol");
const probeSource = fs.readFileSync(probePath, "utf8");
assert(!probeSource.includes("setInterval("), "probe must not install an endless timer");
assert(probeSource.includes("const FORCE_READY_ATTEMPTS = 120;"));
assert(probeSource.includes("const FORCE_OBSERVE_ATTEMPTS = 120;"));
assert(probeSource.includes("const POST_MODE_STABLE_OBSERVATIONS = 16;"));
assert(probeSource.includes("const POST_MODE_TIMEOUT_MS = 30000;"));
assert(probeSource.includes("player.setPlaybackQualityRange(\"hd720\", \"hd720\")"));
assert(probeSource.includes("player.setPlaybackQuality(\"hd720\")"));

const blobIdentity = lib.sourceIdentity(
  "blob:https://www.youtube.com/50fe132a-0f1e-4b88-99aa-secret");
assert.strictEqual(blobIdentity, "blob:https://www.youtube.com/<redacted>");
const networkIdentity = lib.sourceIdentity(
  "https://rr1---sn.example.googlevideo.com/videoplayback?expire=123&sig=secret-token");
assert.strictEqual(networkIdentity,
  "https://rr1---sn.example.googlevideo.com/<path-redacted>");
assert(!/[?&=]|secret|token|expire|sig=/i.test(networkIdentity),
  "source identity leaked a query or token");
assert(networkIdentity.length <= lib.MAX_SOURCE_BYTES,
  "source identity exceeded its bound");

const sixtyDeltas = Array.from({ length: 1200 }, (_, index) =>
  16.667 + ((index % 5) - 2) * 0.08);
const sixtyCadence = lib.cadenceSummary(sixtyDeltas);
assert.strictEqual(sixtyCadence.intervalCount, 1200);
assert.strictEqual(sixtyCadence.validCount, 1200);
assert.strictEqual(sixtyCadence.positiveCount, 1200);
assert.strictEqual(sixtyCadence.near60Count, 1200);
assert.strictEqual(sixtyCadence.regressionCount, 0);
assert.strictEqual(sixtyCadence.duplicateCount, 0);

const thirtyDeltas = Array.from({ length: 600 }, () => 33.333);
const thirtyCadence = lib.cadenceSummary(thirtyDeltas);
assert.strictEqual(thirtyCadence.intervalCount, 600);
assert.strictEqual(thirtyCadence.near60Count, 0);
assert.strictEqual(thirtyCadence.histogram.b28_40, 600);

const duplicateAdversary = lib.cadenceSummary([
  ...Array.from({ length: 1080 }, () => 0),
  ...Array.from({ length: 120 }, () => 16.667)
]);
assert.strictEqual(duplicateAdversary.intervalCount, 1200);
assert.strictEqual(duplicateAdversary.duplicateCount, 1080);
assert.strictEqual(duplicateAdversary.near60Count, 120);
assert.strictEqual(duplicateAdversary.medianMs, 16.667);
assert.strictEqual(typeof lib.classifySource, "undefined",
  "JavaScript must not own semantic source classification");

const missingOptionalApis = lib.formatRow("sample", {
  index: 1,
  src_id: "unavailable",
  vpq_available: 0,
  vpq_total: -1,
  vpq_dropped: -1,
  webkit_available: 0,
  webkit_decoded: -1,
  webkit_dropped: -1
});
assert(missingOptionalApis.includes("vpq_available=0"));
assert(missingOptionalApis.includes("webkit_available=0"));
assert(missingOptionalApis.length <= lib.MAX_ROW_BYTES);

const bounded = lib.formatRow("sample", {
  index: 1,
  src_id: "https://example.invalid/video?token=" + "x".repeat(2000),
  quality_available: "hd720,".repeat(200)
});
assert(bounded.length <= lib.MAX_ROW_BYTES, "formatted row exceeded its cap");
assert(!bounded.includes("?"), "formatted row retained a query delimiter");

function rowFields(row) {
  const fields = Object.create(null);
  for (const word of row.split(" ").slice(1)) {
    const split = word.indexOf("=");
    assert(split > 0, `malformed row word: ${word}`);
    const key = word.slice(0, split);
    assert.strictEqual(fields[key], undefined, `duplicate row key: ${key}`);
    fields[key] = word.slice(split + 1);
  }
  return fields;
}

async function runProbeScenario(options = {}) {
  const rows = [];
  const diagnosticRows = [];
  const diagnosticV2Rows = [];
  const rebindRows = [];
  const auxiliaryRows = [];
  const calls = [];
  let selected = options.initialQuality || "medium";
  let qualityReads = 0;
  let pendingSelection = null;
  let callback = null;
  let callbackId = 0;
  let presentedFrames = 100;
  let mediaTime = 1;
  let totalVideoFrames = 1000;
  let coalescedAdvanceDone = false;
  let sampleTimerArms = 0;
  let replacementDone = false;
  const available = options.available || ["hd1080", "hd720", "large"];
  function makeVideo(currentTime = 1, width = options.initialWidth || 640,
      height = options.initialHeight || 360) {
    const instance = {
      isConnected: !options.videoMissing,
      videoWidth: width,
      videoHeight: height,
      readyState: 4,
      networkState: 2,
      currentTime,
      currentSrc: "blob:https://www.youtube.com/reducer-fixture",
      playbackRate: 1,
      paused: false,
      ended: false,
      muted: false,
      volume: 0.5,
      buffered: {
        length: 1,
        start() { return 0; },
        end() { return instance.currentTime + 30; }
      },
      getVideoPlaybackQuality() {
        return { totalVideoFrames, droppedVideoFrames: 0 };
      },
      get webkitDecodedFrameCount() { return totalVideoFrames; },
      get webkitDroppedFrameCount() { return 0; },
      async play() { instance.paused = false; },
      requestVideoFrameCallback(next) {
        callback = next;
        callbackId++;
        return callbackId;
      },
      cancelVideoFrameCallback(id) {
        if (id === callbackId)
          callback = null;
      }
    };
    return instance;
  }
  let video = makeVideo();
  function maybeApplySelection() {
    qualityReads++;
    const delay = options.selectionDelayReads || 0;
    if (pendingSelection && qualityReads >= delay) {
      if (!options.noop) {
        selected = pendingSelection;
        if (!options.wrongDimensions) {
          video.videoWidth = 1280;
          video.videoHeight = 720;
        }
      }
      pendingSelection = null;
    }
  }
  const player = {
    getPlayerState() { return options.playerState === undefined ? 1 : options.playerState; },
    getPlaybackQuality() {
      maybeApplySelection();
      return selected;
    },
    getAvailableQualityLevels() { return available.slice(); }
  };
  if (!options.rangeMissing) {
    player.setPlaybackQualityRange = (minimum, maximum) => {
      calls.push(["setPlaybackQualityRange", minimum, maximum]);
      if (options.rangeThrow)
        throw new Error("synthetic range throw");
      pendingSelection = minimum;
      return undefined;
    };
  }
  if (!options.qualityMissing) {
    player.setPlaybackQuality = quality => {
      calls.push(["setPlaybackQuality", quality]);
      if (options.qualityThrow)
        throw new Error("synthetic quality throw");
      pendingSelection = quality;
      return undefined;
    };
  }

  function advance(milliseconds) {
    video.currentTime += milliseconds / 1000;
    if (!callback)
      return;
    const frameCount = Math.max(1, Math.round(milliseconds / 16.667));
    if (options.coalesceFirstAdvance && !coalescedAdvanceDone && frameCount >= 2) {
      coalescedAdvanceDone = true;
      // Model a MAIN-world long task: one callback is delivered before the
      // blocked interval, then the next delivery reports the latest frame.
      // The producer must preserve that presentedFrames/mediaTime jump as raw
      // evidence; only the Tcl host parser may decide whether it is causal.
      let current = callback;
      callback = null;
      presentedFrames++;
      totalVideoFrames++;
      mediaTime += 1 / 60;
      current(video.currentTime * 1000, { presentedFrames, mediaTime });
      assert(callback, "coalesced reducer callback was not rescheduled");
      current = callback;
      callback = null;
      presentedFrames += frameCount - 1;
      totalVideoFrames += frameCount - 1;
      mediaTime += (frameCount - 1) / 60;
      current(video.currentTime * 1000, { presentedFrames, mediaTime });
      return;
    }
    for (let index = 0; index < frameCount && callback; index++) {
      const current = callback;
      callback = null;
      presentedFrames++;
      totalVideoFrames++;
      mediaTime += 1 / 60;
      current(video.currentTime * 1000, { presentedFrames, mediaTime });
    }
  }

  const scenario = {
    URL,
    location: {
      href: "https://www.youtube.com/watch?v=fixture",
      hash: options.normalArm
        ? "#xv6ytprobe=0123456789abcdef0123456789abcdef"
        : "#xv6ytprobe=0123456789abcdef0123456789abcdef&xv6ythd720=1" +
          (options.captureDiagnostic ? "&xv6ytcapturediag=1" : "")
    },
    console: {
      info(row) {
        const text = String(row);
        if (text.startsWith("YT_MEDIA_VIDEO_REBIND_V1 ")) {
          rebindRows.push(text);
          return;
        }
        if (text.startsWith("YT_CODEC_STATE_V1 ") ||
            text.startsWith("YT_MEDIA_WALL_V1 ") ||
            text.startsWith("YT_DISPLAY_STATE_V1 ") ||
            text.startsWith("YT_MEDIA_VIDEO_SETTLE_V1 ") ||
            text.startsWith("YT_MEDIA_VIDEO_SETTLE_REBIND_V1 ") ||
            text.startsWith("YT_MEDIA_VIDEO_REBIND_REJECT_V1 ")) {
          auxiliaryRows.push(text);
          return;
        }
        if (text.startsWith("YT_CAPTURE_COMPLETENESS_V1 ")) {
          if (options.consoleThrowDiagnostic)
            throw new Error("synthetic diagnostic console throw");
          if (options.consoleBlockDiagnostic)
            return;
          diagnosticRows.push(text);
          return;
        }
        if (text.startsWith("YT_CAPTURE_COMPLETENESS_V2 ")) {
          if (options.consoleThrowDiagnostic)
            throw new Error("synthetic diagnostic console throw");
          if (options.consoleBlockDiagnostic)
            return;
          diagnosticV2Rows.push(text);
          return;
        }
        rows.push(text);
      }
    },
    document: {
      getElementById(id) {
        return id === "movie_player" && !options.playerMissing ? player : null;
      },
      querySelector(selector) {
        return selector === "video" && !options.videoMissing ? video : null;
      }
    },
    PerformanceObserver: class {
      observe() {}
      disconnect() {}
    },
    setTimeout(resolve, milliseconds) {
      if (milliseconds === 1000)
        sampleTimerArms++;
      if (options.freezeTimerAt === sampleTimerArms && milliseconds === 1000) {
        if (options.rvfcContinuesAfterFreeze) {
          for (let index = 0; index < 8; index++)
            advance(16.667);
        }
        return 1;
      }
      Promise.resolve().then(() => {
        advance(milliseconds);
        if (!replacementDone && options.replaceAtSample === sampleTimerArms &&
            milliseconds === 1000) {
          const oldVideo = video;
          oldVideo.isConnected = false;
          callback = null;
          replacementDone = true;
          totalVideoFrames = options.replacementFrameBase === undefined
            ? 10 : options.replacementFrameBase;
          presentedFrames = options.replacementPresentedBase === undefined
            ? 5 : options.replacementPresentedBase;
          mediaTime = options.replacementMediaBase === undefined
            ? 0.25 : options.replacementMediaBase;
          const replacementTimeOffset = options.replacementTimeOffset || 0;
          video = makeVideo(oldVideo.currentTime + replacementTimeOffset,
            1280, 720);
        }
        resolve();
      });
      return 1;
    }
  };
  scenario.globalThis = scenario;
  vm.runInNewContext(fs.readFileSync(libraryPath, "utf8"), scenario,
    { filename: libraryPath });
  vm.runInNewContext(fs.readFileSync(probePath, "utf8"), scenario,
    { filename: probePath });
  for (let turn = 0; turn < 300; turn++) {
    if (rows.some(row => row.includes(" kind=done ") || row.includes(" kind=failure ")))
      break;
    await new Promise(resolve => setImmediate(resolve));
  }
  const terminal = rows.some(row => row.includes(" kind=done ") ||
    row.includes(" kind=failure "));
  if (!options.allowIncomplete)
    assert(terminal, "probe did not terminate within the reducer bound");
  return { rows, diagnosticRows, diagnosticV2Rows, rebindRows, auxiliaryRows, calls,
    fields: rows.map(rowFields), terminal };
}

function forcePrelude(result) {
  return result.fields.slice(0, 6).map(fields => fields.kind);
}

(async () => {
  const pass = await runProbeScenario();
  assert.strictEqual(pass.rows.length, 30);
  assert.deepStrictEqual(forcePrelude(pass), [
    "force_ready", "force_attempt", "force_result", "force_attempt",
    "force_result", "force_observation"
  ]);
  assert.deepStrictEqual(pass.calls, [
    ["setPlaybackQualityRange", "hd720", "hd720"],
    ["setPlaybackQuality", "hd720"]
  ]);
  assert.strictEqual(pass.fields[2].invoked, "1");
  assert.strictEqual(pass.fields[2].threw, "0");
  assert.strictEqual(pass.fields[4].invoked, "1");
  assert.strictEqual(pass.fields[4].threw, "0");
  assert.strictEqual(pass.fields[5].selected, "hd720");
  assert.strictEqual(pass.fields[0].player_state_api, "1");
  assert.strictEqual(pass.fields[0].player_state, "1");
  assert.strictEqual(pass.fields[5].video_width, "1280");
  assert(Number(pass.fields[5].stable_count) >= 4);

  const rebound = await runProbeScenario({ replaceAtSample: 3 });
  assert.strictEqual(rebound.rows.length, 30,
    "a valid replacement changed the semantic row count");
  assert.strictEqual(rebound.rebindRows.length, 1);
  assert(rebound.rebindRows[0].includes(" sample=3 "));
  assert(rebound.rows.some(row => row.includes(" kind=done ")),
    "a valid replacement did not complete");
  const reboundRvfc = rebound.fields.find(fields =>
    fields.kind === "rvfc_summary");
  assert(reboundRvfc, "a valid replacement omitted its rVFC summary");
  assert.strictEqual(reboundRvfc.interval_duplicates, "0");
  assert.strictEqual(reboundRvfc.presented_duplicates, "0");
  const reboundSamples = rebound.fields.filter(fields => fields.kind === "sample");
  assert.strictEqual(reboundSamples.length, 20);
  for (let index = 1; index < reboundSamples.length; index++) {
    assert(Number(reboundSamples[index].current_time) >=
      Number(reboundSamples[index - 1].current_time));
    assert(Number(reboundSamples[index].vpq_total) >=
      Number(reboundSamples[index - 1].vpq_total));
    assert(Number(reboundSamples[index].rvfc_presented) >=
      Number(reboundSamples[index - 1].rvfc_presented));
  }

  const rewindRejected = await runProbeScenario({
    replaceAtSample: 3,
    replacementTimeOffset: -5
  });
  assert(rewindRejected.rows.some(row =>
    row.includes(" kind=failure ") &&
    row.includes(" reason=video-replacement-invalid-3")));
  const rewindRejectRow = rewindRejected.auxiliaryRows.find(row =>
    row.startsWith("YT_MEDIA_VIDEO_REBIND_REJECT_V1 "));
  assert(rewindRejectRow, "a rejected replacement omitted its invariant marker");
  assert(rewindRejectRow.includes(" time_nonregress=0 "));

  const normal = await runProbeScenario({ normalArm: true });
  assert.strictEqual(normal.rows.length, 24);
  assert.strictEqual(normal.fields[0].kind, "start");
  assert.strictEqual(normal.calls.length, 0);
  assert(!normal.rows.some(row => row.includes("kind=force_")));

  const diagnostic = await runProbeScenario({ captureDiagnostic: true });
  assert.strictEqual(diagnostic.rows.length, 30,
    "diagnostic arm changed the fixed semantic row count");
  assert.deepStrictEqual(diagnostic.rows, pass.rows,
    "diagnostic arm changed semantic row bytes");
  assert.strictEqual(diagnostic.diagnosticRows.length, 61);
  assert(diagnostic.diagnosticRows.every(row =>
    row.startsWith("YT_CAPTURE_COMPLETENESS_V1 ") && row.length <= 320));
  assert.strictEqual(diagnostic.diagnosticRows.filter(row =>
    row.includes(" kind=timer_arm ")).length, 20);
  assert.strictEqual(diagnostic.diagnosticRows.filter(row =>
    row.includes(" kind=timer_fire ")).length, 20);
  assert.strictEqual(diagnostic.diagnosticRows.filter(row =>
    row.includes(" kind=rvfc_checkpoint ")).length, 20);
  assert.strictEqual(diagnostic.diagnosticRows.filter(row =>
    row.includes(" kind=done ")).length, 1);
  assert.strictEqual(diagnostic.diagnosticV2Rows.length, 63);
  assert(diagnostic.diagnosticV2Rows.every(row =>
    row.startsWith("YT_CAPTURE_COMPLETENESS_V2 ") && row.length <= 320));
  assert(diagnostic.diagnosticV2Rows[0].includes(" kind=producer_start "));
  assert(diagnostic.diagnosticV2Rows[1].includes(" kind=post_video_hd720_ready "));
  assert.strictEqual(diagnostic.diagnosticV2Rows.filter(row =>
    row.includes(" kind=timer_arm ")).length, 20);
  assert.strictEqual(diagnostic.diagnosticV2Rows.filter(row =>
    row.includes(" kind=timer_fire ")).length, 20);

  const frozenContinued = await runProbeScenario({
    captureDiagnostic: true,
    freezeTimerAt: 4,
    rvfcContinuesAfterFreeze: true,
    allowIncomplete: true
  });
  assert.strictEqual(frozenContinued.terminal, false);
  assert(frozenContinued.diagnosticRows.some(row =>
    row.includes(" kind=timer_arm ") && row.includes(" seq=4 ")));
  assert(!frozenContinued.diagnosticRows.some(row =>
    row.includes(" kind=timer_fire ") && row.includes(" seq=4 ")));
  assert(frozenContinued.diagnosticRows.some(row =>
    row.includes(" kind=rvfc_checkpoint ") && row.includes(" seq=4 ")));
  assert(!frozenContinued.diagnosticRows.some(row => row.includes(" kind=done ")));
  assert(frozenContinued.diagnosticV2Rows.some(row =>
    row.includes(" kind=post_video_hd720_ready ")));

  const frozenStopped = await runProbeScenario({
    captureDiagnostic: true,
    freezeTimerAt: 4,
    allowIncomplete: true
  });
  assert.strictEqual(frozenStopped.terminal, false);
  assert(!frozenStopped.diagnosticRows.some(row =>
    row.includes(" kind=rvfc_checkpoint ") && row.includes(" seq=4 ")));

  const consoleThrow = await runProbeScenario({
    captureDiagnostic: true,
    consoleThrowDiagnostic: true
  });
  assert.strictEqual(consoleThrow.rows.length, 30);
  assert.strictEqual(consoleThrow.diagnosticRows.length, 0);
  assert.strictEqual(consoleThrow.diagnosticV2Rows.length, 0);
  const consoleBlock = await runProbeScenario({
    captureDiagnostic: true,
    consoleBlockDiagnostic: true
  });
  assert.strictEqual(consoleBlock.rows.length, 30);
  assert.strictEqual(consoleBlock.diagnosticRows.length, 0);
  assert.strictEqual(consoleBlock.diagnosticV2Rows.length, 0);

  const unavailable = await runProbeScenario({ available: ["hd1080", "large"] });
  assert.strictEqual(unavailable.calls.length, 0,
    "selector called an API before hd720 was advertised");
  assert.strictEqual(unavailable.fields[0].attempt, "120");
  assert.strictEqual(unavailable.fields[2].invoked, "0");

  const rangeThrow = await runProbeScenario({ rangeThrow: true });
  assert.strictEqual(rangeThrow.fields[2].threw, "1");
  assert.deepStrictEqual(rangeThrow.calls.map(call => call[0]),
    ["setPlaybackQualityRange", "setPlaybackQuality"]);

  const qualityThrow = await runProbeScenario({ qualityThrow: true });
  assert.strictEqual(qualityThrow.fields[4].threw, "1");

  const missingRange = await runProbeScenario({ rangeMissing: true });
  assert.strictEqual(missingRange.calls.length, 0);
  assert.strictEqual(missingRange.fields[1].present, "0");
  assert.strictEqual(missingRange.fields[2].invoked, "0");

  const missingQuality = await runProbeScenario({ qualityMissing: true });
  assert.strictEqual(missingQuality.calls.length, 0);
  assert.strictEqual(missingQuality.fields[3].present, "0");
  assert.strictEqual(missingQuality.fields[4].invoked, "0");

  const noop = await runProbeScenario({ noop: true });
  assert.strictEqual(noop.fields[5].selected, "medium");
  assert.strictEqual(noop.fields[5].stable_count, "0");

  const delayed = await runProbeScenario({ selectionDelayReads: 20 });
  assert.strictEqual(delayed.fields[5].selected, "hd720");
  assert(Number(delayed.fields[5].attempt) > 4);
  assert(Number(delayed.fields[5].attempt) <= 40);

  const wrongDimensions = await runProbeScenario({ wrongDimensions: true });
  assert.strictEqual(wrongDimensions.fields[5].video_width, "640");
  assert.strictEqual(wrongDimensions.fields[5].stable_count, "0");

  const coalesced = await runProbeScenario({ coalesceFirstAdvance: true });
  const coalescedFirstSample = coalesced.fields.find(fields =>
    fields.kind === "sample" && fields.index === "1");
  const coalescedSummary = coalesced.fields.find(fields =>
    fields.kind === "rvfc_summary");
  assert(coalescedFirstSample && coalescedSummary);
  assert.strictEqual(coalescedFirstSample.rvfc_callbacks, "2");
  assert.strictEqual(coalescedFirstSample.rvfc_presented, "160");
  assert.strictEqual(coalescedFirstSample.rvfc_media_time, "2.000000");
  assert.strictEqual(coalescedSummary.callbacks, "1142");
  assert.strictEqual(coalescedSummary.presented_first, "101");
  assert.strictEqual(coalescedSummary.presented_delta, "1199");
  assert.strictEqual(coalescedSummary.near60_count, "1140");
  assert(coalescedSummary.hist.includes("ge80:1"));

  assert.strictEqual(typeof pass.classifyForce, "undefined");
  process.stdout.write(
    "YT-MEDIA-PROBE-JS-REDUCER-PASS redaction=PASS bounds=PASS " +
    "raw60=PASS raw30=PASS duplicate1080=COUNTED host_classifier_only=PASS " +
    "extension_id=edfilgocpdgbkehcgdillfgnnhclphol missing_api=PASS " +
    "force_main_world=PASS force_exact_order=PASS force_api_throw=RAW " +
    "force_noop=RAW force_delayed=BOUNDED normal_isolation=PASS " +
    "coalesced_rvfc=RAW post_mode_settle=PASS video_rebind=CONSERVATIVE " +
    "capture_diag_off_parity=PASS " +
    "capture_diag_timer_freeze=RAW capture_diag_rvfc_split=RAW " +
    "capture_diag_console_throw_block=FAIL_CLOSED\n");
})().catch(error => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
