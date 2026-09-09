(function reportXv6YouTubeDocumentStart() {
  "use strict";

  // Diagnostic only.  The semantic probe remains at document_idle; this
  // marker tells the harness whether Chromium committed a YouTube document
  // and injected a MAIN-world script before page load can stall.
  const url = encodeURIComponent(String(location.href)).slice(0, 384);
  console.info(
    `YT_DOCUMENT_STATE_V1 kind=document_start schema=1 ready_state=${document.readyState} url=${url}`);
})();
