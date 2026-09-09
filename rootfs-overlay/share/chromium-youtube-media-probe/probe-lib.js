(function installXv6YouTubeMediaProbeLibrary(global) {
  "use strict";

  const MARKER = "YT_MEDIA_PROBE_V1";
  const MAX_ROW_BYTES = 1000;
  const MAX_SOURCE_BYTES = 160;

  function finiteNumber(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? number : fallback;
  }

  function fixed(value, digits, fallback) {
    const number = finiteNumber(value, fallback);
    return number.toFixed(digits);
  }

  function boundedToken(value, limit) {
    const text = String(value === undefined || value === null ? "unavailable" : value)
      .replace(/[^A-Za-z0-9._~:/<>,+%-]/g, "_");
    return (text || "unavailable").slice(0, limit);
  }

  function sourceIdentity(source) {
    const raw = String(source || "");
    if (!raw)
      return "unavailable";

    let identity = "unavailable";
    try {
      if (raw.startsWith("blob:")) {
        const inner = new URL(raw.slice(5));
        identity = `blob:${inner.protocol}//${inner.host}/<redacted>`;
      } else if (raw.startsWith("data:")) {
        const comma = raw.indexOf(",");
        const mime = comma >= 0 ? raw.slice(5, comma) : raw.slice(5);
        identity = `data:${boundedToken(mime, 48)},<redacted>`;
      } else {
        const parsed = new URL(raw, global.location ? global.location.href : undefined);
        identity = `${parsed.protocol}//${parsed.host}/<path-redacted>`;
      }
    } catch (_) {
      const colon = raw.indexOf(":");
      const scheme = colon > 0 ? boundedToken(raw.slice(0, colon), 16) : "unknown";
      identity = `${scheme}:<redacted>`;
    }
    return boundedToken(identity, MAX_SOURCE_BYTES);
  }

  /* Raw cadence reduction only. The host owns all semantic classification. */
  function cadenceSummary(deltas) {
    const histogram = {
      regress: 0,
      duplicate: 0,
      lt8: 0,
      b8_12: 0,
      b12_14: 0,
      b14_15: 0,
      b15_18p5: 0,
      b18p5_20: 0,
      b20_28: 0,
      b28_40: 0,
      b40_80: 0,
      ge80: 0
    };
    const positive = [];
    let invalidCount = 0;
    let intervalSumMs = 0;

    for (const raw of deltas) {
      const delta = Number(raw);
      if (!Number.isFinite(delta)) {
        invalidCount++;
        continue;
      }
      intervalSumMs += delta;
      if (delta < 0) histogram.regress++;
      else if (delta === 0) histogram.duplicate++;
      else {
        positive.push(delta);
        if (delta < 8) histogram.lt8++;
        else if (delta < 12) histogram.b8_12++;
        else if (delta < 14) histogram.b12_14++;
        else if (delta < 15) histogram.b14_15++;
        else if (delta <= 18.5) histogram.b15_18p5++;
        else if (delta < 20) histogram.b18p5_20++;
        else if (delta < 28) histogram.b20_28++;
        else if (delta < 40) histogram.b28_40++;
        else if (delta < 80) histogram.b40_80++;
        else histogram.ge80++;
      }
    }

    positive.sort((a, b) => a - b);
    let median = -1;
    if (positive.length) {
      const middle = Math.floor(positive.length / 2);
      median = positive.length % 2
        ? positive[middle]
        : (positive[middle - 1] + positive[middle]) / 2;
    }
    const histogramText = Object.entries(histogram)
      .map(([name, count]) => `${name}:${count}`)
      .join(",");
    return {
      intervalCount: deltas.length,
      validCount: deltas.length - invalidCount,
      invalidCount,
      positiveCount: positive.length,
      regressionCount: histogram.regress,
      duplicateCount: histogram.duplicate,
      near60Count: histogram.b15_18p5,
      intervalSumMs,
      medianMs: median,
      histogram,
      histogramText
    };
  }

  function formatRow(kind, fields) {
    const pieces = [MARKER, `kind=${boundedToken(kind, 24)}`, "schema=1"];
    for (const [key, rawValue] of Object.entries(fields)) {
      if (!/^[a-z][a-z0-9_]*$/.test(key))
        throw new Error("invalid media-probe key");
      pieces.push(`${key}=${boundedToken(rawValue, 256)}`);
    }
    const row = pieces.join(" ");
    if (row.length > MAX_ROW_BYTES)
      throw new Error("media-probe row exceeds bound");
    return row;
  }

  global.__xv6YouTubeMediaProbeLib = Object.freeze({
    MARKER,
    MAX_ROW_BYTES,
    MAX_SOURCE_BYTES,
    boundedToken,
    cadenceSummary,
    finiteNumber,
    fixed,
    formatRow,
    sourceIdentity
  });
})(globalThis);
