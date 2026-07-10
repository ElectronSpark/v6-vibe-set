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
assert(probeSource.includes("const FORCE_OBSERVE_ATTEMPTS = 40;"));
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
  const calls = [];
  let selected = options.initialQuality || "medium";
  let qualityReads = 0;
  let pendingSelection = null;
  let callback = null;
  let callbackId = 0;
  let presentedFrames = 100;
  let mediaTime = 1;
  let totalVideoFrames = 1000;
  const available = options.available || ["hd1080", "hd720", "large"];
  const video = {
    isConnected: options.videoMissing ? false : true,
    videoWidth: options.initialWidth || 640,
    videoHeight: options.initialHeight || 360,
    readyState: 4,
    networkState: 2,
    currentTime: 1,
    playbackRate: 1,
    paused: false,
    ended: false,
    buffered: {
      length: 1,
      start() { return 0; },
      end() { return video.currentTime + 30; }
    },
    getVideoPlaybackQuality() {
      return { totalVideoFrames, droppedVideoFrames: 0 };
    },
    get webkitDecodedFrameCount() { return totalVideoFrames; },
    get webkitDroppedFrameCount() { return 0; },
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
        : "#xv6ytprobe=0123456789abcdef0123456789abcdef&xv6ythd720=1"
    },
    console: { info(row) { rows.push(String(row)); } },
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
      Promise.resolve().then(() => {
        advance(milliseconds);
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
  assert(rows.some(row => row.includes(" kind=done ") || row.includes(" kind=failure ")),
    "probe did not terminate within the reducer bound");
  return { rows, calls, fields: rows.map(rowFields) };
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

  const normal = await runProbeScenario({ normalArm: true });
  assert.strictEqual(normal.rows.length, 24);
  assert.strictEqual(normal.fields[0].kind, "start");
  assert.strictEqual(normal.calls.length, 0);
  assert(!normal.rows.some(row => row.includes("kind=force_")));

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

  assert.strictEqual(typeof pass.classifyForce, "undefined");
  process.stdout.write(
    "YT-MEDIA-PROBE-JS-REDUCER-PASS redaction=PASS bounds=PASS " +
    "raw60=PASS raw30=PASS duplicate1080=COUNTED host_classifier_only=PASS " +
    "extension_id=edfilgocpdgbkehcgdillfgnnhclphol missing_api=PASS " +
    "force_main_world=PASS force_exact_order=PASS force_api_throw=RAW " +
    "force_noop=RAW force_delayed=BOUNDED normal_isolation=PASS\n");
})().catch(error => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
