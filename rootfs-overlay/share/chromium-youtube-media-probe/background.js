(function runXv6YouTubeTabDiagnostic() {
  "use strict";

  // Diagnostic only: this worker never creates, closes, reloads, or navigates
  // a tab.  It gives cold-start failures a bounded phase receipt even when a
  // YouTube document never exists and the semantic content script cannot run.
  const MAX_ROWS = 64;
  let rows = 0;

  function bounded(value, limit) {
    return encodeURIComponent(String(value ?? "unavailable")).slice(0, limit);
  }

  function emit(kind, tabId, status, url) {
    if (rows >= MAX_ROWS)
      return;
    console.info(
      `YT_TAB_STATE_V1 kind=${bounded(kind, 16)} schema=1 tab_id=${Number.isInteger(tabId) ? tabId : -1} status=${bounded(status, 16)} url=${bounded(url, 384)}`);
    rows++;
  }

  function emitNavigation(kind, details, status) {
    // Only the top-level document is relevant.  Subframe events are numerous
    // and would hide the single lifecycle transition this diagnostic needs.
    if (details.frameId !== 0)
      return;
    emit(`nav_${kind}`, details.tabId, status,
         details.url || details.error || "unavailable");
  }

  chrome.tabs.query({}, tabs => {
    if (chrome.runtime.lastError) {
      emit("query_error", -1, "error", chrome.runtime.lastError.message);
      return;
    }
    emit("worker_ready", -1, "ready", "extension://background");
    for (const tab of tabs)
      emit("snapshot", tab.id, tab.status, tab.url);
  });

  chrome.tabs.onCreated.addListener(tab =>
    emit("created", tab.id, tab.status, tab.url));
  chrome.tabs.onUpdated.addListener((tabId, change, tab) => {
    if (change.status || change.url)
      emit("updated", tabId, change.status || tab.status, change.url || tab.url);
  });
  chrome.tabs.onRemoved.addListener(tabId =>
    emit("removed", tabId, "removed", "unavailable"));

  chrome.webNavigation.onBeforeNavigate.addListener(details =>
    emitNavigation("before", details, "before"));
  chrome.webNavigation.onCommitted.addListener(details =>
    emitNavigation("committed", details, details.transitionType || "committed"));
  chrome.webNavigation.onDOMContentLoaded.addListener(details =>
    emitNavigation("dom", details, "dom"));
  chrome.webNavigation.onCompleted.addListener(details =>
    emitNavigation("complete", details, "complete"));
  chrome.webNavigation.onErrorOccurred.addListener(details =>
    emitNavigation("error", details, details.error || "error"));
})();
