#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BASE="${1:-${ROOT}/build-x86_64/linux-chromium-vm-control/${STAMP}-ubuntu-xorg-visible-youtube-proof}"
BASE="$(realpath -m "$BASE")"
WORK="${LINUX_CHROMIUM_VM_WORK:-/tmp/xv6-linux-chromium-vm-control}"
WORK="$(realpath -m "$WORK")"
BACKING="${LINUX_CHROMIUM_BACKING:-${ROOT}/build-x86_64/linux-chromium-vm-control/20260628T151915Z-ubuntu-kwin-wayland-abs-retry/ubuntu-chromium-control.qcow2}"
XRES="${LINUX_CHROMIUM_XRES:-1280}"
YRES="${LINUX_CHROMIUM_YRES:-800}"
YOUTUBE_URL="${LINUX_CHROMIUM_YOUTUBE_URL:-https://www.youtube.com/watch?v=dQw4w9WgXcQ&autoplay=1&mute=1&vq=hd720}"
YOUTUBE_SECONDS="${LINUX_CHROMIUM_YOUTUBE_SECONDS:-35}"
CHROMIUM_USE_GL="${LINUX_CHROMIUM_USE_GL:-none}"
CHROMIUM_USE_ANGLE="${LINUX_CHROMIUM_USE_ANGLE:-none}"
YOUTUBE_URL_B64="$(printf '%s' "$YOUTUBE_URL" | base64 -w0)"
DISK="${BASE}/ubuntu-chromium-xorg-youtube-proof.qcow2"
SEED="${BASE}/seed.iso"
USER_DATA="${BASE}/user-data"
META_DATA="${BASE}/meta-data"
SERIAL_LOG="${BASE}/serial.log"
HOST_LOG="${BASE}/host.log"
MONITOR="${WORK}/qemu-monitor-http-perf-$$.sock"
TRACE_EVENTS="${BASE}/qemu-trace-events"
QEMU_TRACE="${BASE}/qemu-virtio-gpu.trace"
STATUS="${BASE}/status.txt"
SUMMARY="${BASE}/summary.txt"
SCREENSHOT="${BASE}/chromium-xorg-youtube-final.ppm"
SCREENSHOT_LOG="${BASE}/monitor-screendump.log"

mkdir -p "$BASE" "$WORK"

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing tool $1" >&2
    exit 1
  }
}

need qemu-system-x86_64
need qemu-img
need genisoimage
need expect
need nc

if [[ ! -e "$BACKING" ]]; then
  echo "missing backing image: $BACKING" >&2
  exit 1
fi
if [[ ! -e "${ROOT}/build-x86_64/linux-chromium-control/fixture/perf-1280x800-60fps.mp4" ]]; then
  echo "missing perf video fixture" >&2
  exit 1
fi

rm -f "$DISK" "$SEED" "$SERIAL_LOG" "$HOST_LOG" "$MONITOR" "$TRACE_EVENTS" \
  "$QEMU_TRACE" "$STATUS" "$SUMMARY" "$SCREENSHOT" "$SCREENSHOT_LOG" \
  "$BASE/qemu-img-create.log" "$BASE/qemu-trace-summary.txt" "$BASE/run.expect"

qemu-img create -f qcow2 -F qcow2 -b "$BACKING" "$DISK" > "$BASE/qemu-img-create.log"

cat > "$META_DATA" <<META
instance-id: xv6-linux-chromium-xorg-youtube-${STAMP}
local-hostname: chromium-xorg-youtube
META

cat > "$USER_DATA" <<'CLOUD'
#cloud-config
package_update: false
write_files:
  - path: /root/chromium-http-perf-server.py
    permissions: '0755'
    content: |
      #!/usr/bin/env python3
      import http.server, json, os, time, urllib.parse

      ROOT = "/mnt/xv6/build-x86_64/linux-chromium-control/fixture"
      VIDEO = os.path.join(ROOT, "perf-1280x800-60fps.mp4")
      LOG = "/tmp/chromium-http-perf-server.log"
      STATE = {"done": False, "last": None}

      HTML = r"""<!doctype html>
      <meta charset="utf-8">
      <title>linux-chromium-http-perf boot</title>
      <style>
        html, body { margin: 0; padding: 0; background: #000; overflow: hidden; }
        #v { position: fixed; inset: 0; width: 100vw; height: 100vh; object-fit: contain; background: #000; }
        #hud { position: fixed; left: 8px; bottom: 8px; color: #0f0; font: 14px/1.3 monospace; background: rgba(0,0,0,.55); padding: 6px 8px; white-space: pre; }
      </style>
      <video id="v" muted autoplay playsinline src="/perf-1280x800-60fps.mp4"></video>
      <pre id="hud">boot</pre>
      <script>
      (() => {
        const params = new URLSearchParams(location.search);
        const runMs = Math.max(1000, Math.min(60000, parseInt(params.get("ms") || "15000", 10)));
        const startupMs = Math.max(0, Math.min(60000, parseInt(params.get("startupMs") || "6000", 10)));
        const video = document.getElementById("v");
        const hud = document.getElementById("hud");
        let presented = 0, firstTs = 0, lastTs = 0, firstMedia = 0, lastMedia = 0;
        let previousTs = 0, gapSamples = 0, gapTotal = 0, maxGap = 0;
        let lastMetaPresented = -1;
        function quality() {
          return typeof video.getVideoPlaybackQuality === "function"
            ? video.getVideoPlaybackQuality() : null;
        }
        function payload(kind, extra) {
          const q = quality();
          const spanMs = firstTs && lastTs ? lastTs - firstTs : 0;
          const mediaSpan = presented ? lastMedia - firstMedia : 0;
          const out = {
            kind, extra: extra || "", href: location.href, title: document.title,
            ready: video.readyState, net: video.networkState, paused: video.paused ? 1 : 0,
            ended: video.ended ? 1 : 0, err: video.error ? video.error.code : 0,
            currentTime: Number(video.currentTime.toFixed(3)),
            duration: Number.isFinite(video.duration) ? Number(video.duration.toFixed(3)) : -1,
            presented, decoded: q ? q.totalVideoFrames : -1,
            dropped: q ? q.droppedVideoFrames : -1,
            rvfcSpanMs: Number(spanMs.toFixed(3)),
            rvfcFPS: spanMs > 0 ? Number((presented * 1000 / spanMs).toFixed(3)) : 0,
            rvfcAvgGapMs: gapSamples > 0 ? Number((gapTotal / gapSamples).toFixed(3)) : 0,
            rvfcMaxGapMs: Number(maxGap.toFixed(3)),
            mediaSpan: Number(mediaSpan.toFixed(3)),
            playbackRate: spanMs > 0 ? Number((mediaSpan / (spanMs / 1000)).toFixed(3)) : 0,
            metaPresentedFrames: lastMetaPresented,
            wallNowMs: Number(performance.now().toFixed(3))
          };
          return out;
        }
        function post(kind, extra) {
          const body = JSON.stringify(payload(kind, extra));
          fetch("/metric", {method: "POST", keepalive: true, body}).catch(() => {});
          const p = JSON.parse(body);
          document.title = "linux-chromium-http-perf:" + kind + ":presented=" + p.presented + ":decoded=" + p.decoded + ":fps=" + p.rvfcFPS;
          hud.textContent = kind + "\nmedia=" + p.currentTime + "s\npresented=" + p.presented + " decoded=" + p.decoded + " dropped=" + p.dropped + "\nfps=" + p.rvfcFPS + " maxGapMs=" + p.rvfcMaxGapMs;
        }
        function onFrame(now, meta) {
          presented++;
          const media = Number.isFinite(meta.mediaTime) ? meta.mediaTime : video.currentTime;
          if (!firstTs) { firstTs = now; firstMedia = media; }
          if (previousTs) {
            const gap = now - previousTs;
            gapSamples++;
            gapTotal += gap;
            if (gap > maxGap) maxGap = gap;
          }
          previousTs = now;
          lastTs = now;
          lastMedia = media;
          lastMetaPresented = Number.isFinite(meta.presentedFrames) ? meta.presentedFrames : -1;
          if (presented <= 5 || presented % 60 === 0 || (now - previousTs) > 250)
            post("rvfc", "seq=" + presented);
          video.requestVideoFrameCallback(onFrame);
        }
        ["loadstart", "loadedmetadata", "loadeddata", "canplay", "playing", "waiting", "stalled", "pause", "ended", "error"].forEach(ev => {
          video.addEventListener(ev, () => post("event:" + ev));
        });
        window.onerror = (m, s, l, c) => post("page-error", String(m) + "@" + l + ":" + c);
        post("start", "runMs=" + runMs + " startupMs=" + startupMs + " rvfc=" + (typeof video.requestVideoFrameCallback === "function" ? 1 : 0));
        if (typeof video.requestVideoFrameCallback === "function")
          video.requestVideoFrameCallback(onFrame);
        const startupTimer = setInterval(() => post("startup"), 1000);
        const playPromise = video.play();
        if (playPromise && typeof playPromise.catch === "function")
          playPromise.catch(e => post("play-rejected", e && e.name ? e.name : String(e)));
        setTimeout(() => {
          clearInterval(startupTimer);
          post("measure-begin");
          const beginPresented = presented;
          const beginDecoded = quality() ? quality().totalVideoFrames : -1;
          const beginDropped = quality() ? quality().droppedVideoFrames : -1;
          const t0 = performance.now();
          const tick = setInterval(() => post("tick"), 1000);
          setTimeout(() => {
            clearInterval(tick);
            const q = quality();
            const result = payload("result", "beginPresented=" + beginPresented + " beginDecoded=" + beginDecoded + " beginDropped=" + beginDropped + " measureWallMs=" + (performance.now() - t0).toFixed(3));
            result.measurePresented = presented - beginPresented;
            result.measureDecoded = q && beginDecoded >= 0 ? q.totalVideoFrames - beginDecoded : -1;
            result.measureDropped = q && beginDropped >= 0 ? q.droppedVideoFrames - beginDropped : -1;
            result.measureFPS = runMs > 0 ? Number((result.measurePresented * 1000 / runMs).toFixed(3)) : 0;
            fetch("/result", {method: "POST", keepalive: true, body: JSON.stringify(result)}).catch(() => {});
            document.title = "linux-chromium-http-perf:result:measureFPS=" + result.measureFPS + ":presented=" + result.presented + ":decoded=" + result.decoded;
            hud.textContent = "result\nmeasureFPS=" + result.measureFPS + "\npresented=" + result.presented + " decoded=" + result.decoded + " dropped=" + result.dropped;
          }, runMs);
        }, startupMs);
      })();
      </script>
      """

      def emit(line):
        with open(LOG, "a") as f:
          f.write(line + "\n")
          f.flush()
        print(line, flush=True)

      class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
          emit("LINUX_CHROMIUM_HTTP access client=%s request=%s" %
               (self.client_address[0], fmt % args))
        def do_GET(self):
          path = urllib.parse.urlparse(self.path).path
          if path in ("/", "/perf.html"):
            data = HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
          if path == "/perf-1280x800-60fps.mp4":
            try:
              st = os.stat(VIDEO)
              self.send_response(200)
              self.send_header("Content-Type", "video/mp4")
              self.send_header("Content-Length", str(st.st_size))
              self.end_headers()
              with open(VIDEO, "rb") as f:
                while True:
                  chunk = f.read(1024 * 1024)
                  if not chunk:
                    break
                  self.wfile.write(chunk)
              return
            except Exception as e:
              emit("LINUX_CHROMIUM_HTTP video_error=%r" % (e,))
          self.send_response(404)
          self.end_headers()
        def do_POST(self):
          n = int(self.headers.get("Content-Length", "0") or "0")
          body = self.rfile.read(n).decode("utf-8", "replace")
          path = urllib.parse.urlparse(self.path).path
          try:
            obj = json.loads(body)
          except Exception:
            obj = {"raw": body}
          emit("LINUX_CHROMIUM_HTTP metric path=%s data=%s" %
               (path, json.dumps(obj, sort_keys=True, separators=(",", ":"))))
          if path == "/result":
            STATE["done"] = True
            STATE["last"] = obj
          self.send_response(204)
          self.end_headers()

      if __name__ == "__main__":
        open(LOG, "w").close()
        emit("LINUX_CHROMIUM_HTTP_SERVER start video=%s exists=%d size=%d" %
             (VIDEO, 1 if os.path.exists(VIDEO) else 0, os.path.getsize(VIDEO) if os.path.exists(VIDEO) else -1))
        http.server.ThreadingHTTPServer(("127.0.0.1", 8000), Handler).serve_forever()
  - path: /root/chromium-http-result-summary.py
    permissions: '0755'
    content: |
      #!/usr/bin/env python3
      import json

      path = "/tmp/chromium-http-perf-server.log"
      result = None
      last_tick = None
      got_video = 0

      try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
          for line in f:
            if "GET /perf-1280x800-60fps.mp4" in line:
              got_video = 1
            if " data=" not in line:
              continue
            try:
              obj = json.loads(line.split(" data=", 1)[1])
            except Exception:
              continue
            if obj.get("kind") == "tick":
              last_tick = obj
            if "metric path=/result" in line:
              result = obj
      except FileNotFoundError:
        pass

      obj = result or last_tick or {}
      fields = {
        "status": "PASS" if result is not None else "MISSING_RESULT",
        "gotVideo": got_video,
        "measureFPS": obj.get("measureFPS", -1),
        "measurePresented": obj.get("measurePresented", -1),
        "measureDecoded": obj.get("measureDecoded", -1),
        "measureDropped": obj.get("measureDropped", -1),
        "presented": obj.get("presented", -1),
        "decoded": obj.get("decoded", -1),
        "dropped": obj.get("dropped", -1),
        "rvfcFPS": obj.get("rvfcFPS", -1),
        "rvfcAvgGapMs": obj.get("rvfcAvgGapMs", -1),
        "rvfcMaxGapMs": obj.get("rvfcMaxGapMs", -1),
        "playbackRate": obj.get("playbackRate", -1),
        "currentTime": obj.get("currentTime", -1),
      }
      print("LINUX_CHROMIUM_HTTP_PROOF VIDEO_RESULT " + " ".join(f"{k}={v}" for k, v in fields.items()), flush=True)
  - path: /root/chromium-youtube-cdp-sampler.py
    permissions: '0755'
    content: |
      #!/usr/bin/env python3
      import base64
      import hashlib
      import json
      import os
      import socket
      import struct
      import sys
      import time
      import urllib.request
      import urllib.parse

      PORT = int(os.environ.get("YOUTUBE_CDP_PORT", "9222"))
      SECONDS = float(os.environ.get("YOUTUBE_SAMPLE_SECONDS", "35"))

      JS = r"""
      (() => {
        const v = document.querySelector('video');
        if (!v) return {hasVideo:0, title:document.title, href:location.href};
        v.muted = true;
        const p = v.play();
        if (p && p.catch) p.catch(() => {});
        const q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : {};
        return {
          hasVideo:1, title:document.title, href:location.href,
          currentTime:Number((v.currentTime || 0).toFixed(3)),
          duration:Number.isFinite(v.duration) ? Number(v.duration.toFixed(3)) : -1,
          paused:v.paused ? 1 : 0,
          ended:v.ended ? 1 : 0,
          readyState:v.readyState,
          networkState:v.networkState,
          playbackRate:v.playbackRate,
          width:v.videoWidth || 0,
          height:v.videoHeight || 0,
          decoded:q.totalVideoFrames ?? -1,
          dropped:q.droppedVideoFrames ?? -1,
          corrupted:q.corruptedVideoFrames ?? -1
        };
      })()
      """

      def http_json(path):
        with urllib.request.urlopen(f"http://127.0.0.1:{PORT}{path}", timeout=1) as r:
          return json.loads(r.read().decode("utf-8", "replace"))

      def find_target(deadline):
        while time.time() < deadline:
          try:
            targets = http_json("/json/list")
            pages = [t for t in targets
                     if t.get("type") == "page"
                     and t.get("webSocketDebuggerUrl")
                     and (t.get("url") or "").startswith(("http://", "https://"))]
            for t in pages:
              if "youtube." in (t.get("url") or "") and "/watch" in (t.get("url") or ""):
                return t
            for t in pages:
              if "youtube." in (t.get("url") or "") or "youtube." in (t.get("title") or "").lower():
                return t
            if pages:
              return pages[0]
          except Exception:
            pass
          time.sleep(0.25)
        return None

      class WS:
        def __init__(self, url):
          u = urllib.parse.urlparse(url)
          self.sock = socket.create_connection((u.hostname, u.port or 80), timeout=3)
          key = base64.b64encode(os.urandom(16)).decode()
          resource = u.path + (("?" + u.query) if u.query else "")
          req = (
            f"GET {resource} HTTP/1.1\r\n"
            f"Host: {u.netloc}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
          )
          self.sock.sendall(req.encode())
          resp = b""
          while b"\r\n\r\n" not in resp:
            chunk = self.sock.recv(4096)
            if not chunk:
              raise RuntimeError("websocket handshake eof")
            resp += chunk
          if b" 101 " not in resp.split(b"\r\n", 1)[0]:
            raise RuntimeError("websocket handshake failed: " + resp[:120].decode("latin1", "replace"))
          self.next_id = 1

        def send(self, obj):
          data = json.dumps(obj, separators=(",", ":")).encode()
          header = bytearray([0x81])
          if len(data) < 126:
            header.append(0x80 | len(data))
          elif len(data) < 65536:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", len(data)))
          else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", len(data)))
          mask = os.urandom(4)
          header.extend(mask)
          masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
          self.sock.sendall(header + masked)

        def recv(self):
          h = self.sock.recv(2)
          if len(h) < 2:
            raise RuntimeError("websocket eof")
          opcode = h[0] & 0x0f
          n = h[1] & 0x7f
          if n == 126:
            n = struct.unpack("!H", self.sock.recv(2))[0]
          elif n == 127:
            n = struct.unpack("!Q", self.sock.recv(8))[0]
          if h[1] & 0x80:
            mask = self.sock.recv(4)
          else:
            mask = b""
          data = b""
          while len(data) < n:
            data += self.sock.recv(n - len(data))
          if mask:
            data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
          if opcode == 8:
            raise RuntimeError("websocket closed")
          if opcode != 1:
            return None
          return json.loads(data.decode("utf-8", "replace"))

        def call(self, method, params=None, timeout=3):
          mid = self.next_id
          self.next_id += 1
          self.send({"id": mid, "method": method, "params": params or {}})
          deadline = time.time() + timeout
          while time.time() < deadline:
            msg = self.recv()
            if msg and msg.get("id") == mid:
              return msg
          raise TimeoutError(method)

      def evaluate(ws):
        msg = ws.call("Runtime.evaluate", {
          "expression": JS,
          "returnByValue": True,
          "awaitPromise": False,
        })
        return (((msg.get("result") or {}).get("result") or {}).get("value") or {})

      def fmt(prefix, obj):
        keys = ["hasVideo", "currentTime", "duration", "paused", "readyState",
                "width", "height", "decoded", "dropped", "playbackRate"]
        vals = " ".join(f"{k}={obj.get(k, -1)}" for k in keys)
        title = str(obj.get("title", ""))[:80].replace("\n", " ")
        print(f"LINUX_CHROMIUM_YOUTUBE {prefix} {vals} title={json.dumps(title)}", flush=True)

      try:
        ws = None
        target = None
        connect_deadline = time.time() + 35
        last_error = ""
        while time.time() < connect_deadline and ws is None:
          target = find_target(time.time() + 2)
          if not target:
            last_error = "no-cdp-target"
            time.sleep(0.5)
            continue
          try:
            ws = WS(target["webSocketDebuggerUrl"])
          except RuntimeError as e:
            last_error = str(e).replace("\n", " ")[:160]
            if "No such target id" not in last_error and "500 Internal Server Error" not in last_error:
              raise
            time.sleep(0.5)
        if ws is None:
          print(f"LINUX_CHROMIUM_YOUTUBE_RESULT status=FAIL reason=no-cdp-websocket detail={last_error}", flush=True)
          sys.exit(2)
        print(
          "LINUX_CHROMIUM_YOUTUBE target "
          f"url={json.dumps(target.get('url', ''))} title={json.dumps(target.get('title', ''))}",
          flush=True)
        start = None
        end = None
        samples = 0
        deadline = time.time() + SECONDS
        while time.time() < deadline:
          obj = evaluate(ws)
          samples += 1
          if obj.get("hasVideo"):
            if start is None:
              start = dict(obj)
              fmt("sample=start", obj)
            end = dict(obj)
          if samples % 5 == 0:
            fmt(f"sample=t{samples}", obj)
          time.sleep(1)
        if not start or not end:
          print(f"LINUX_CHROMIUM_YOUTUBE_RESULT status=FAIL reason=no-video samples={samples}", flush=True)
          sys.exit(3)
        elapsed_media = float(end.get("currentTime", 0)) - float(start.get("currentTime", 0))
        decoded_delta = int(end.get("decoded", -1)) - int(start.get("decoded", -1))
        dropped_delta = int(end.get("dropped", -1)) - int(start.get("dropped", -1))
        fps = decoded_delta / SECONDS if decoded_delta >= 0 and SECONDS > 0 else -1
        drop_pct = (100.0 * dropped_delta / decoded_delta) if decoded_delta > 0 else -1
        status = "PASS" if elapsed_media >= max(5.0, SECONDS * 0.45) and decoded_delta > 0 else "FAIL"
        fmt("sample=end", end)
        print(
          "LINUX_CHROMIUM_YOUTUBE_RESULT "
          f"status={status} samples={samples} seconds={SECONDS:.1f} "
          f"mediaProgress={elapsed_media:.3f} decodedDelta={decoded_delta} "
          f"droppedDelta={dropped_delta} decodedFPS={fps:.3f} dropPct={drop_pct:.3f} "
          f"width={end.get('width', 0)} height={end.get('height', 0)} "
          f"paused={end.get('paused', -1)} readyState={end.get('readyState', -1)}",
          flush=True)
        sys.exit(0 if status == "PASS" else 4)
      except Exception as e:
        print(f"LINUX_CHROMIUM_YOUTUBE_RESULT status=FAIL reason=exception detail={type(e).__name__}:{e}", flush=True)
        sys.exit(5)
  - path: /root/run-chromium-http-perf-proof.sh
    permissions: '0755'
    content: |
      #!/bin/bash
      set -uo pipefail
      exec > >(tee -a /dev/console /root/linux-chromium-http-perf-proof.log) 2>&1
      xres=__XRES__
      yres=__YRES__
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=start backend=xorg-visible geometry=${xres}x${yres} date=$(date -Iseconds)"
      export DEBIAN_FRONTEND=noninteractive
      apt-get update || true
      apt-get install -y --no-install-recommends xserver-xorg-core xinit openbox x11-xserver-utils x11-utils xterm mesa-utils dbus-x11 || true
      mkdir -p /mnt/xv6
      mount -t 9p -o trans=virtio,version=9p2000.L,ro xv6repo /mnt/xv6
      modprobe virtio_gpu || true
      mkdir -p /run/user/0 /tmp/.X11-unix /tmp/chromium-http-profile
      chmod 700 /run/user/0
      export HOME=/root USER=root LOGNAME=root XDG_RUNTIME_DIR=/run/user/0
      export XDG_SESSION_TYPE=x11 XDG_CURRENT_DESKTOP=openbox XDG_SESSION_DESKTOP=openbox
      export GALLIUM_DRIVER=virgl LIBGL_ALWAYS_SOFTWARE=0
      export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/kde-session-bus
      dbus-daemon --system --fork || true
      dbus-daemon --session --fork --address=unix:path=/tmp/kde-session-bus --print-address=1 >/tmp/kde-session-bus.addr || true
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=device-nodes"
      ls -l /dev/dri || true
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=eglinfo"
      eglinfo -B 2>&1 | grep -E 'OpenGL ES profile renderer|OpenGL ES profile version|GBM platform|Surfaceless platform|eglinfo: eglInitialize failed' | sed 's/^/LINUX_CHROMIUM_HTTP_PROOF EGLINFO /' || true
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=xorg-start"
      XORG_BIN="$(command -v Xorg || printf /usr/lib/xorg/Xorg)"
      "$XORG_BIN" :0 -noreset -verbose 3 -logfile /tmp/Xorg.0.log vt1 >/tmp/Xorg.stdout 2>/tmp/Xorg.stderr &
      xorg_pid=$!
      for i in $(seq 1 300); do [ -S /tmp/.X11-unix/X0 ] && break; sleep 0.1; done
      export DISPLAY=:0
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=xorg-ready socket=$([ -S /tmp/.X11-unix/X0 ] && echo 1 || echo 0) alive=$(kill -0 "$xorg_pid" 2>/dev/null && echo 1 || echo 0) pid=$xorg_pid"
      output="$(xrandr --query 2>/dev/null | awk '/ connected/{print $1; exit}')"
      mode="${xres}x${yres}_60.00"
      if [ -n "$output" ]; then
        xrandr --newmode "$mode" 83.50 1280 1352 1480 1680 800 803 809 831 -hsync +vsync >/tmp/xrandr-set.log 2>&1 || true
        xrandr --addmode "$output" "$mode" >>/tmp/xrandr-set.log 2>&1 || true
        xrandr --output "$output" --mode "$mode" >>/tmp/xrandr-set.log 2>&1 || xrandr --output "$output" --mode "${xres}x${yres}" >>/tmp/xrandr-set.log 2>&1 || true
      else
        xrandr -s "${xres}x${yres}" >/tmp/xrandr-set.log 2>&1 || true
      fi
      xrandr --query >/tmp/xrandr-query.log 2>&1 || true
      awk '/ connected|\\*/{print "LINUX_CHROMIUM_HTTP_PROOF XRANDR " $0}' /tmp/xrandr-query.log || true
      current_geometry="$(xdpyinfo 2>/dev/null | awk '/dimensions:/{print $2; exit}')"
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=xorg-geometry current=${current_geometry:-unknown} target=${xres}x${yres}"
      xsetroot -solid '#102040' >/tmp/xsetroot.log 2>&1 || true
      openbox >/tmp/openbox.log 2>&1 &
      openbox_pid=$!
      glxinfo -B >/tmp/glxinfo-xorg.log 2>&1 || true
      cat /tmp/glxinfo-xorg.log | grep -E 'OpenGL renderer string|OpenGL version string|direct rendering|Accelerated|Device:' | sed 's/^/LINUX_CHROMIUM_HTTP_PROOF GLX /' || true
      timeout 8 glxgears -info -geometry 640x480+20+20 >/tmp/glxgears-xorg.log 2>&1 || true
      cat /tmp/glxgears-xorg.log | grep -E 'GL_RENDERER|frames in|FPS' | sed 's/^/LINUX_CHROMIUM_HTTP_PROOF GLXGEARS /' || true
      chrome=/mnt/xv6/build-x86_64/host-gui-runtime/wayland-chromium/chrome-linux64/chrome
      youtube_url_b64="__YOUTUBE_URL_B64__"
      url="$(printf '%s' "$youtube_url_b64" | base64 -d)"
      chromium_use_gl="__CHROMIUM_USE_GL__"
      chromium_use_angle="__CHROMIUM_USE_ANGLE__"
      chromium_gl_flags=()
      if [ -n "$chromium_use_gl" ] && [ "$chromium_use_gl" != none ]; then
        chromium_gl_flags+=(--use-gl="$chromium_use_gl")
      fi
      if [ -n "$chromium_use_angle" ] && [ "$chromium_use_angle" != none ]; then
        chromium_gl_flags+=(--use-angle="$chromium_use_angle")
      fi
      "$chrome" --version || true
      chrome_gpu_pref_summary() {
        for gp in $(pgrep -f -- '--type=gpu-process' || true); do
          cmd="$(tr '\0' ' ' <"/proc/$gp/cmdline" 2>/dev/null || true)"
          [ -n "$cmd" ] || continue
          use_gl=missing
          use_angle=missing
          pref=
          case " $cmd" in *" --use-gl="*) use_gl="${cmd#*--use-gl=}"; use_gl="${use_gl%% *}" ;; esac
          case " $cmd" in *" --use-angle="*) use_angle="${cmd#*--use-angle=}"; use_angle="${use_angle%% *}" ;; esac
          case " $cmd" in *" --gpu-preferences="*) pref="${cmd#*--gpu-preferences=}"; pref="${pref%% *}" ;; esac
          if [ -n "$pref" ]; then
            digest="$(python3 -c 'import base64,sys; p=sys.argv[1]; b=base64.b64decode(p+"="*((4-len(p)%4)%4)); h=2166136261; [globals().__setitem__("h", ((h ^ x) * 16777619) & 0xffffffff) for x in b]; print("decoded_len=%d fnv32=0x%08x" % (len(b), h))' "$pref" 2>/dev/null || printf 'decoded_len=0 fnv32=decode-error')"
          else
            digest="decoded_len=0 fnv32=missing"
          fi
          echo "LINUX_CHROMIUM_GPU_PREF pid=$gp use_gl=$use_gl use_angle=$use_angle b64_len=${#pref} $digest"
        done
      }
      echo "LINUX_CHROMIUM_YOUTUBE_PROOF phase=chrome-launch url=$url use_gl=$chromium_use_gl use_angle=$chromium_use_angle gl_flags=${chromium_gl_flags[*]:-none}"
      env OZONE_PLATFORM=x11 EGL_PLATFORM=x11 DISPLAY=:0 "$chrome" \
        --ozone-platform=x11 \
        --enable-features=UseOzonePlatform,AcceleratedVideoDecodeLinuxGL,VaapiIgnoreDriverChecks,VaapiOnNvidiaGPUs,VaapiVideoEncoder,CanvasOopRasterization \
        --ignore-gpu-blocklist --enable-gpu-rasterization \
        --no-sandbox --disable-setuid-sandbox --disable-seccomp-filter-sandbox --disable-gpu-sandbox \
        --disable-dev-shm-usage --disable-vulkan --disable-breakpad --disable-crashpad --disable-crash-reporter \
        --disable-quic --disable-component-update --disable-background-networking --disable-sync --no-proxy-server \
        --disable-renderer-accessibility --no-first-run --no-default-browser-check --password-store=basic \
        --disable-features=AccessibilityService,Crashpad,MediaRouter,OptimizationHints,CalculateNativeWinOcclusion,UseChromeOSDirectVideoDecoder,UseFreedesktopSecretPortal \
        --user-data-dir=/tmp/chromium-http-profile --enable-logging=stderr --alsa-output-device=default \
        --remote-debugging-address=127.0.0.1 --remote-debugging-port=9222 \
        --autoplay-policy=no-user-gesture-required "${chromium_gl_flags[@]}" \
        --window-position=0,0 --window-size="${xres},${yres}" --start-fullscreen "$url" \
        >/tmp/chrome.stdout 2>/tmp/chrome.stderr &
      chrome_pid=$!
      env YOUTUBE_SAMPLE_SECONDS=__YOUTUBE_SECONDS__ python3 /root/chromium-youtube-cdp-sampler.py >/tmp/youtube-cdp-sampler.log 2>&1 &
      sampler_pid=$!
      result_seen=0
      for i in $(seq 1 70); do
        sleep 1
        echo "LINUX_CHROMIUM_YOUTUBE_PROOF sample=t${i} chrome_processes=$(pgrep -fa '/chrome-linux64/chrome' | wc -l) gpu=$(pgrep -fa -- '--type=gpu-process' | wc -l) renderer=$(pgrep -fa -- '--type=renderer' | wc -l) zygote=$(pgrep -fa -- '--type=zygote' | wc -l) sampler_alive=$(kill -0 "$sampler_pid" 2>/dev/null && echo 1 || echo 0)"
        chrome_gpu_pref_summary
        if grep -q 'LINUX_CHROMIUM_YOUTUBE_RESULT status=PASS' /tmp/youtube-cdp-sampler.log 2>/dev/null; then
          result_seen=1
          break
        fi
      done
      wait "$sampler_pid" 2>/dev/null || true
      echo "LINUX_CHROMIUM_YOUTUBE_PROOF phase=screenshot-ready result_seen=$result_seen"
      sleep 3
      echo "LINUX_CHROMIUM_YOUTUBE_PROOF phase=youtube-result-summary"
      cat /tmp/youtube-cdp-sampler.log | sed 's/^/LINUX_CHROMIUM_YOUTUBE_PROOF SAMPLER /' || true
      if [ "$result_seen" = 1 ]; then
        echo "LINUX_CHROMIUM_YOUTUBE_PROOF_DONE status=PASS"
      else
        echo "LINUX_CHROMIUM_YOUTUBE_PROOF phase=chrome-stderr-diagnostics"
        grep -aE 'Exiting GPU process|gl_factory|vaInitialize|gpu-process|ERROR|WARNING|Requested GL implementation|DevTools' /tmp/chrome.stderr | tail -120 | sed 's/^/LINUX_CHROMIUM_YOUTUBE_PROOF STDERR /' || true
        echo "LINUX_CHROMIUM_YOUTUBE_PROOF phase=xorg-tail"
        tail -120 /tmp/Xorg.0.log | sed 's/^/LINUX_CHROMIUM_YOUTUBE_PROOF XORG /' || true
        echo "LINUX_CHROMIUM_YOUTUBE_PROOF_DONE status=FAIL reason=no-result"
      fi
      sleep 1
      kill "$chrome_pid" "$sampler_pid" "$openbox_pid" "$xorg_pid" 2>/dev/null || true
      sync
      poweroff -f
runcmd:
  - [ bash, /root/run-chromium-http-perf-proof.sh ]
CLOUD
sed -i \
  -e "s/__XRES__/${XRES}/g" \
  -e "s/__YRES__/${YRES}/g" \
  -e "s/__YOUTUBE_SECONDS__/${YOUTUBE_SECONDS}/g" \
  -e "s/__CHROMIUM_USE_GL__/${CHROMIUM_USE_GL}/g" \
  -e "s/__CHROMIUM_USE_ANGLE__/${CHROMIUM_USE_ANGLE}/g" \
  -e "s#__YOUTUBE_URL_B64__#${YOUTUBE_URL_B64}#g" \
  "$USER_DATA"

genisoimage -quiet -output "$SEED" -volid cidata -joliet -rock "$USER_DATA" "$META_DATA"
cat > "$TRACE_EVENTS" <<'TRACE'
virtio_gpu_features
virtio_gpu_cmd_get_display_info
virtio_gpu_cmd_get_edid
virtio_gpu_cmd_set_scanout
virtio_gpu_cmd_res_create_3d
virtio_gpu_cmd_res_flush
virtio_gpu_cmd_ctx_create
virtio_gpu_cmd_ctx_submit
virtio_gpu_fence_ctrl
virtio_gpu_fence_resp
TRACE

cat > "$BASE/run.expect" <<EXPECT
#!/usr/bin/expect -f
set timeout 900
match_max 12000000
log_file -a "$SERIAL_LOG"
spawn qemu-system-x86_64 \\
  -enable-kvm -cpu host -smp 4 -m 8192 \\
  -drive if=virtio,file=$DISK,format=qcow2 \\
  -drive if=virtio,media=cdrom,file=$SEED,format=raw \\
  -virtfs local,path=$ROOT,mount_tag=xv6repo,security_model=none,readonly=on \\
  -vga none -device virtio-vga-gl,xres=$XRES,yres=$YRES \\
  -display gtk,gl=on,show-cursor=off \\
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \\
  -serial stdio -monitor unix:$MONITOR,server,nowait -no-reboot \\
  -trace events=$TRACE_EVENTS,file=$QEMU_TRACE
expect {
  -re "LINUX_CHROMIUM_YOUTUBE_PROOF phase=screenshot-ready" {
    catch { exec sh -c "printf 'screendump $SCREENSHOT\\n' | nc -U '$MONITOR' > '$SCREENSHOT_LOG' 2>&1" }
    exp_continue
  }
  -re "LINUX_CHROMIUM_YOUTUBE_PROOF_DONE status=PASS" { puts "LINUX_CHROMIUM_YOUTUBE_PROOF_EXPECT seen-pass" }
  -re "LINUX_CHROMIUM_YOUTUBE_PROOF_DONE status=FAIL reason=\\[^\\r\\n\\]*" { puts "LINUX_CHROMIUM_YOUTUBE_PROOF_EXPECT seen-fail"; exit 6 }
  timeout { puts "LINUX_CHROMIUM_YOUTUBE_PROOF_EXPECT timeout"; exit 4 }
  eof { puts "LINUX_CHROMIUM_YOUTUBE_PROOF_EXPECT eof-before-done"; exit 5 }
}
expect { eof {} timeout { puts "LINUX_CHROMIUM_YOUTUBE_PROOF_EXPECT poweroff-timeout" } }
EXPECT
chmod +x "$BASE/run.expect"

if "$BASE/run.expect" > "$HOST_LOG" 2>&1; then
  run_status=PASS
  run_rc=0
else
  run_status=FAIL
  run_rc=$?
fi

screenshot_status=MISSING
if [[ -s "$SCREENSHOT" ]]; then
  screenshot_status=PASS
fi
geometry_status=UNKNOWN
if grep -a "LINUX_CHROMIUM_HTTP_PROOF phase=xorg-geometry current=${XRES}x${YRES} target=${XRES}x${YRES}" "$SERIAL_LOG" >/dev/null 2>&1; then
  geometry_status=PASS
elif grep -a 'LINUX_CHROMIUM_HTTP_PROOF phase=xorg-geometry' "$SERIAL_LOG" >/dev/null 2>&1; then
  geometry_status=FAIL
fi
if [[ "$run_status" == PASS ]]; then
  printf 'status=PASS media=PASS geometry=%s screenshot=%s\n' "$geometry_status" "$screenshot_status" > "$STATUS"
else
  printf 'status=FAIL rc=%s media=FAIL geometry=%s screenshot=%s\n' "$run_rc" "$geometry_status" "$screenshot_status" > "$STATUS"
fi

ctx_submit=$(grep -c 'virtio_gpu_cmd_ctx_submit' "$QEMU_TRACE" 2>/dev/null || true)
res_flush=$(grep -c 'virtio_gpu_cmd_res_flush' "$QEMU_TRACE" 2>/dev/null || true)
set_scanout=$(grep -c 'virtio_gpu_cmd_set_scanout' "$QEMU_TRACE" 2>/dev/null || true)
fence_ctrl=$(grep -c 'virtio_gpu_fence_ctrl' "$QEMU_TRACE" 2>/dev/null || true)
fence_resp=$(grep -c 'virtio_gpu_fence_resp' "$QEMU_TRACE" 2>/dev/null || true)
printf 'virtio_gpu_cmd_ctx_submit %s\nvirtio_gpu_cmd_res_flush %s\nvirtio_gpu_cmd_set_scanout %s\nvirtio_gpu_fence_ctrl %s\nvirtio_gpu_fence_resp %s\n' \
  "$ctx_submit" "$res_flush" "$set_scanout" "$fence_ctrl" "$fence_resp" > "$BASE/qemu-trace-summary.txt"

{
  cat "$STATUS"
  printf 'geometry=%sx%s\n' "$XRES" "$YRES"
  printf 'youtube_url=%s\n' "$YOUTUBE_URL"
  grep -a 'Google Chrome for Testing' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF phase=xorg-ready' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF phase=xorg-geometry' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF EGLINFO OpenGL ES profile renderer' "$SERIAL_LOG" | head -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF XRANDR ' "$SERIAL_LOG" | tail -8 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF GLX ' "$SERIAL_LOG" | tail -8 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF GLXGEARS ' "$SERIAL_LOG" | tail -8 || true
  grep -a 'LINUX_CHROMIUM_YOUTUBE_RESULT ' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_YOUTUBE_PROOF sample=' "$SERIAL_LOG" | tail -5 || true
  grep -a 'LINUX_CHROMIUM_GPU_PREF ' "$SERIAL_LOG" | tail -20 || true
  grep -aE 'LINUX_CHROMIUM_YOUTUBE_PROOF STDERR .*Exiting GPU process|LINUX_CHROMIUM_YOUTUBE_PROOF STDERR .*gl_factory|LINUX_CHROMIUM_YOUTUBE_PROOF STDERR .*Requested GL implementation' "$SERIAL_LOG" | tail -20 || true
  cat "$BASE/qemu-trace-summary.txt"
  if [[ -s "$SCREENSHOT" ]]; then
    printf 'screenshot=%s size=%s\n' "$SCREENSHOT" "$(stat -c %s "$SCREENSHOT")"
  else
    printf 'screenshot=missing\n'
  fi
  printf 'screenshot_status=%s\n' "$screenshot_status"
  if [[ -s "$SCREENSHOT_LOG" ]]; then
    printf 'monitor_screendump_log=%s\n' "$(tr '\n' ' ' < "$SCREENSHOT_LOG" | sed 's/[[:space:]]*$//')"
  fi
} > "$SUMMARY"

cat "$SUMMARY"
