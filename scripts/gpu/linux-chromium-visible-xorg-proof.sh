#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BASE="${1:-${ROOT}/build-x86_64/linux-chromium-vm-control/${STAMP}-ubuntu-xorg-visible-http-perf-proof}"
BASE="$(realpath -m "$BASE")"
WORK="${LINUX_CHROMIUM_VM_WORK:-/tmp/xv6-linux-chromium-vm-control}"
WORK="$(realpath -m "$WORK")"
BACKING="${LINUX_CHROMIUM_BACKING:-${ROOT}/build-x86_64/linux-chromium-vm-control/20260628T151915Z-ubuntu-kwin-wayland-abs-retry/ubuntu-chromium-control.qcow2}"
XRES="${LINUX_CHROMIUM_XRES:-1280}"
YRES="${LINUX_CHROMIUM_YRES:-800}"
DISK="${BASE}/ubuntu-chromium-xorg-http-perf-proof.qcow2"
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
SCREENSHOT="${BASE}/chromium-xorg-perf-final.ppm"
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
instance-id: xv6-linux-chromium-xorg-http-perf-${STAMP}
local-hostname: chromium-xorg-http-perf
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
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=server-start"
      /root/chromium-http-perf-server.py >/tmp/chromium-http-server.stdout 2>&1 &
      server_pid=$!
      for i in $(seq 1 100); do
        python3 -c 'import urllib.request; urllib.request.urlopen("http://127.0.0.1:8000/perf.html", timeout=0.2).read(64)' >/dev/null 2>&1 && break
        sleep 0.1
      done
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=server-ready alive=$(kill -0 "$server_pid" 2>/dev/null && echo 1 || echo 0) pid=$server_pid"
      chrome=/mnt/xv6/build-x86_64/host-gui-runtime/wayland-chromium/chrome-linux64/chrome
      url="http://127.0.0.1:8000/perf.html?ms=15000&startupMs=6000"
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
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=chrome-launch url=$url"
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
        --autoplay-policy=no-user-gesture-required --use-gl=angle --use-angle=gles \
        --window-position=0,0 --window-size="${xres},${yres}" --start-fullscreen "$url" \
        >/tmp/chrome.stdout 2>/tmp/chrome.stderr &
      chrome_pid=$!
      result_seen=0
      for i in $(seq 1 45); do
        sleep 1
        echo "LINUX_CHROMIUM_HTTP_PROOF sample=t${i} chrome_processes=$(pgrep -fa '/chrome-linux64/chrome' | wc -l) gpu=$(pgrep -fa -- '--type=gpu-process' | wc -l) renderer=$(pgrep -fa -- '--type=renderer' | wc -l) zygote=$(pgrep -fa -- '--type=zygote' | wc -l) server_alive=$(kill -0 "$server_pid" 2>/dev/null && echo 1 || echo 0)"
        chrome_gpu_pref_summary
        if grep -q 'metric path=/result' /tmp/chromium-http-perf-server.log 2>/dev/null; then
          result_seen=1
          break
        fi
      done
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=screenshot-ready result_seen=$result_seen"
      sleep 3
      echo "LINUX_CHROMIUM_HTTP_PROOF phase=video-result-summary"
      python3 /root/chromium-http-result-summary.py || true
      if [ "$result_seen" = 1 ]; then
        echo "LINUX_CHROMIUM_HTTP_PROOF_DONE status=PASS"
      else
        echo "LINUX_CHROMIUM_HTTP_PROOF phase=chrome-stderr-diagnostics"
        grep -aE 'Exiting GPU process|gl_factory|vaInitialize|gpu-process|ERROR|WARNING|Requested GL implementation|DevTools' /tmp/chrome.stderr | tail -120 | sed 's/^/LINUX_CHROMIUM_HTTP_PROOF STDERR /' || true
        echo "LINUX_CHROMIUM_HTTP_PROOF phase=xorg-tail"
        tail -120 /tmp/Xorg.0.log | sed 's/^/LINUX_CHROMIUM_HTTP_PROOF XORG /' || true
        echo "LINUX_CHROMIUM_HTTP_PROOF_DONE status=FAIL reason=no-result"
      fi
      sleep 1
      kill "$chrome_pid" "$server_pid" "$openbox_pid" "$xorg_pid" 2>/dev/null || true
      sync
      poweroff -f
runcmd:
  - [ bash, /root/run-chromium-http-perf-proof.sh ]
CLOUD
sed -i "s/__XRES__/${XRES}/g; s/__YRES__/${YRES}/g" "$USER_DATA"

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
  -re "LINUX_CHROMIUM_HTTP_PROOF phase=screenshot-ready" {
    catch { exec sh -c "printf 'screendump $SCREENSHOT\\n' | nc -U '$MONITOR' > '$SCREENSHOT_LOG' 2>&1" }
    exp_continue
  }
  -re "LINUX_CHROMIUM_HTTP_PROOF_DONE status=PASS" { puts "LINUX_CHROMIUM_HTTP_PROOF_EXPECT seen-pass" }
  -re "LINUX_CHROMIUM_HTTP_PROOF_DONE status=FAIL reason=\\[^\\r\\n\\]*" { puts "LINUX_CHROMIUM_HTTP_PROOF_EXPECT seen-fail"; exit 6 }
  timeout { puts "LINUX_CHROMIUM_HTTP_PROOF_EXPECT timeout"; exit 4 }
  eof { puts "LINUX_CHROMIUM_HTTP_PROOF_EXPECT eof-before-done"; exit 5 }
}
expect { eof {} timeout { puts "LINUX_CHROMIUM_HTTP_PROOF_EXPECT poweroff-timeout" } }
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
  grep -a 'Google Chrome for Testing' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF phase=xorg-ready' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF phase=xorg-geometry' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF EGLINFO OpenGL ES profile renderer' "$SERIAL_LOG" | head -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF XRANDR ' "$SERIAL_LOG" | tail -8 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF GLX ' "$SERIAL_LOG" | tail -8 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF GLXGEARS ' "$SERIAL_LOG" | tail -8 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF VIDEO_RESULT ' "$SERIAL_LOG" | tail -1 || true
  grep -a 'LINUX_CHROMIUM_HTTP_PROOF sample=' "$SERIAL_LOG" | tail -5 || true
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
