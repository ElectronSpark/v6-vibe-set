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

process.stdout.write(
  "YT-MEDIA-PROBE-JS-REDUCER-PASS redaction=PASS bounds=PASS " +
  "raw60=PASS raw30=PASS duplicate1080=COUNTED host_classifier_only=PASS " +
  "extension_id=edfilgocpdgbkehcgdillfgnnhclphol missing_api=PASS\n");
