# Desktop YouTube Kernel Gap Report

Date: 2026-05-02

This report is a handoff for future agents continuing the desktop YouTube
compatibility work in this xv6 tree. The user preference is explicit: fix kernel
compatibility gaps first, and touch third-party ports only after kernel-side
behavior has been proven complete and correct.

## Current Goal

Make desktop YouTube load fully in WebKitGTK MiniBrowser inside the xv6 GUI VM.
A successful run should reach a fully loaded YouTube page, not a shortcut,
mobile fallback, or hardcoded final state.

Success markers seen in previous good runs:

- `NDT-HASH` and `NDT-READ-EOF` for the large `ytmainappweb` JavaScript file.
  The file is about 9.7 MB.
- `ytInitialData:true` in the MiniBrowser probe.
- `MB-LOAD event=finished progress=1.000`.
- No kernel `PANIC`, ASSERTION, `mailbox full`, false killed status, or VM
  freeze.

Common failing markers:

- The main page starts and commits, then stays at roughly progress `0.860` to
  `0.861`.
- The large `ytmainappweb` script reports HTTP 200 and `NDT-SEND-OK`, but never
  reports `NDT-HASH` or `NDT-READ-EOF`.
- MiniBrowser probe shows `ytInitialData:false`, empty body metrics, and no
  renderers or thumbnails.

## Repository State At Handoff

Relevant pushed commits before this report:

- Kernel submodule branch `v6-kernel`:
  - `3f8f4f5 Expand lwIP browser receive queues`
  - `f67c550 Preserve normal thread group exit status`
  - `cbb9eaf Fix recvmmsg zero-length stream results`
  - `ec9b0cd Stabilize browser network and GPU cleanup`
  - `1f6b8d8 Scale lwIP receive queues for browsers`
  - `9ea76d6 Drain TCP recvmmsg streams fully`
  - `b28d866 Use raw rq lock ownership checks`
- Top-level branch `main`:
  - `cdda43a Update lwIP browser receive queues`
  - Earlier parent pointer commits for the kernel changes above.

Known untracked artifact:

- `ports/webkit/sysroot/libexec/webkit2gtk-4.1/MiniBrowser.bak`

Do not commit that artifact unless the user explicitly asks.

## Build Commands

Build the kernel image:

```sh
cmake --build build-x86_64 --target kernel -j2
```

Run Sparse after kernel changes:

```sh
cmake --build build-x86_64 --target kernel-sparse -j2
```

Sparse currently emits existing context warnings in places, but the important
target result should end with no new failures/errors.

If changing ports or sysroot content, refresh an image explicitly:

```sh
scripts/make-rootfs.sh \
  build-x86_64/sysroot \
  /tmp/xv6-test.img \
  1536 \
  build-x86_64/toolchain/x86_64/phase2/x86_64-xv6-linux-musl/lib
```

If that libdir is absent, locate the musl loader with:

```sh
find build-x86_64/toolchain -path '*lib/ld-musl*'
```

## VM Launch Commands

Before launching, check for stale QEMU processes:

```sh
ps -eo pid,ppid,stat,comm,args | rg 'qemu-system|launch-gui|xv6-youtube|xv6-recvmmsg2' || true
```

Launch desktop YouTube in GTK/KVM:

```sh
env DISPLAY_MODE=gtk USE_KVM=1 QEMU_NET=1 \
  QEMU_GPU=virtio-gpu-gl QEMU_INPUT=virtio \
  QEMU_GTK_GRAB_ON_HOVER=on QEMU_GTK_SHOW_CURSOR=off \
  QEMU_APPEND='root=/dev/disk0 netsurf=0 webkit=1 webkit_accel=1 webkit_url=https://www.youtube.com/ video=1280x800' \
  FSIMG=/tmp/xv6-recvmmsg2.img \
  timeout 300 bash scripts/launch-gui.sh \
  > /tmp/xv6-youtube-test.log 2>&1
```

KVM ground truth on this host:

```sh
test -r /dev/kvm -a -w /dev/kvm && echo kvm-device-ok
rg -m1 'vmx|svm' /proc/cpuinfo
```

Previous checks showed `/dev/kvm` usable and `vmx` present. Treat VM freezes as
guest/kernel/browser behavior unless procfs proves KVM disappeared.

## Log Analysis Commands

Extract the important markers:

```sh
strings /tmp/xv6-youtube-test.log |
  rg 'NDT-HASH|NDT-READ-EOF|NDT-HDR|NDT-SEND-OK|ytmainappweb|ytInitialData|MB-LOAD|MB-PROBE|ASSERTION|PANIC|mailbox full'
```

Compare against known logs:

- Good:
  - `/tmp/xv6-youtube-procfs-fixed.log`
  - `/tmp/xv6-youtube-scmfix.log`
  - `/tmp/xv6-youtube-after-scm-payload.log`
- Bad or partial:
  - `/tmp/xv6-youtube-zero-msg-fix.log`
  - `/tmp/xv6-youtube-exit-status-fix.log`
  - `/tmp/xv6-youtube-recvd-batch.log`
  - `/tmp/xv6-youtube-bigger-window.log`
  - `/tmp/xv6-youtube-bigger-queues.log`
  - `/tmp/xv6-youtube-progress-trace.log`
  - `/tmp/xv6-youtube-epoll-trace.log`

The exact URLs and YouTube experiment hashes can vary between runs. Focus on
large-resource completion, `ytInitialData`, load completion, and kernel errors.

## What Has Already Been Fixed

### Browser Receive Queue Capacity

Committed and pushed:

- `MEMP_NUM_PBUF`: `2048 -> 4096`
- `MEMP_NUM_TCPIP_MSG_INPKT`: `2048 -> 4096`
- `PBUF_POOL_SIZE`: `8192 -> 16384`
- `SYS_MBOX_SIZE`: `512 -> 1024`

This is a real capacity improvement for browser workloads. It does not fully
fix YouTube by itself.

### TCP recvmmsg Zero-Length Handling

`cbb9eaf` fixed a kernel-side `recvmmsg` behavior where zero-length stream
results could be treated incorrectly. This was necessary for WebKit/GIO network
behavior, but current WebKit YouTube reads mostly go through `recvfrom()`.

### Thread Group Exit Status

`f67c550` fixed normal thread group exit status preservation. This removed a
misleading killed-status symptom around WebKit process teardown.

### GPU Cleanup And Stability

Earlier committed kernel changes improved browser/network/GPU stability. Do not
revert these while investigating YouTube unless a direct regression is proven.

## Current Evidence

### The Big YouTube JS Request Starts Correctly

Failing runs show:

- `NDT-CTOR`
- `NDT-CREATE`
- `NL-SCHED`
- `NL-START`
- `NDT-RESUME`
- `NDT-SEND-BEGIN`
- `NDT-SEND-QUEUED`
- `NDT-STARTING`
- `NDT-HDR status=200 len=9701889` or similar
- `NDT-SEND-OK`

This means the resource is requested and accepted by WebKit's network loader.
The failure is later: completion of the response body, delivery to WebCore, or
main-loop progress after partial body reads.

### The Kernel Receive Path Is recvfrom(), Not recvmmsg

A wide temporary socket trace showed WebKitNetworkProcess draining HTTPS through
`recvfrom()` on TCP sockets. The pattern is tiny TLS reads:

- 5-byte TLS record header reads
- about 1395-byte payload reads
- many nonblocking `EAGAIN` results between chunks

Do not focus only on `recvmsg()` or `recvmmsg()` for the current YouTube stall.
Those paths matter for completeness, but the observed hot path is `recvfrom()`
via `sock_tcp_recv_copyout()`.

### Kernel Still Has Data When Browser Stalls

The low-volume temporary trace in `/tmp/xv6-youtube-progress-trace.log` showed
the big socket crossing the first 256 KB threshold:

```text
[sock-progress] pid=61 fd=22 sk=0x... bytes=262280 reads=543 last=1157 req=1395 err=0 avail=60480 mbox=42 lastpbuf=1
```

Interpretation:

- WebKitNetworkProcess read at least 262 KB from a socket, likely the main page
  socket or the large JS socket depending fd reuse.
- At the trace point, the kernel still had receive data visible:
  - `recv_avail=60480`
  - receive mailbox count `42`
  - `lastpbuf=1`
- The browser nevertheless later sat at `progress=0.861`, `ytInitialData:false`,
  and the big JS never emitted EOF/hash.

This is strong evidence for a readiness/main-loop progress gap, not simply an
empty TCP receive buffer.

### Small Resources Complete

In the same failing runs, small YouTube scripts complete:

- `custom-elements-es5-adapter.js`
- `intersection-observer.min.js`
- `web-animations-next-lite.min.js`
- `webcomponents-sd.js`
- `generate_204`

So DNS, TCP connect, TLS handshake, basic HTTP body reads, and small WebKit
resource delivery are functional.

### Larger TCP Window Experiment Regressed

An experiment increasing:

- `TCP_WND` to `512 * TCP_MSS`
- `TCP_RCV_SCALE` to `4`

regressed badly: subresources barely loaded. It was reverted. Do not reapply
that as-is.

### Receive-Credit Batching Experiment Regressed

A temporary `netconn_tcp_recvd()` batching experiment regressed the stall
earlier, often on the 2.75 MB CSS. It was reverted. A better batching strategy
might still be possible, but naive batching is not the answer.

## Gaps Believed To Remain

### 1. epoll/kqueue Readiness Re-Notification For Still-Readable Sockets

Most likely kernel gap.

The socket can remain readable after a partial nonblocking TLS read. The kernel
currently calls `sock_notify_if_still_readable()` after successful TCP reads,
which should requeue `EVFILT_READ`. However, the browser still parks with data
available.

Areas to inspect:

- `kernel/lwip_port/sys_socket.c`
  - `sock_tcp_recv_copyout()`
  - `sock_notify_if_still_readable()`
  - `sock_has_rx_data()`
  - `sock_poll_ready()`
  - `sock_netconn_callback()`
- `kernel/kqueue/kqueue.c`
  - `vfs_file_knote_notify()`
  - `__knote_enqueue_core()`
  - `kqueue_rescan_registered_locked()`
  - `kqueue_wait()`
- `kernel/kqueue/epoll.c`
  - `sys_epoll_ctl()`
  - `sys_epoll_pwait()`
- `kernel/kqueue/kqueue_filters.c`
  - `knote_read_event()`

Questions to answer:

- Does WebKit register the stalled fd with `EPOLLET`, `EPOLLONESHOT`, or a mask
  that removes `EPOLLIN` after partial reads?
- If an `EV_CLEAR` knote is delivered while the socket remains readable, does it
  get requeued properly when no new lwIP callback occurs?
- Does `vfs_file_knote_notify()` suppress propagation when a knote is already
  queued, leaving outer kqueue/epoll waiters asleep?
- Does `kqueue_rescan_registered_locked()` skip a readable fd because the knote
  is disabled, detached, stale, or still marked queued?
- Does `epoll_pwait()` coalescing or the 20 ms rescan slice lose readiness when
  both read and write filters are registered?

Validation should prove both:

- The stalled fd is registered for read readiness at the time data is available.
- `epoll_pwait()` returns a readable event for that fd until `recvfrom()`
  drains all pending TLS data or returns a real terminal error.

### 2. recvfrom() Partial Read And MSG_PEEK Semantics

`sock_tcp_recv_copyout()` handles partial pbufs with `lastpbuf` and
`lastpbuf_off`. It then calls `netconn_tcp_recvd()` for copied bytes and notifies
if still readable.

Potential gaps:

- `MSG_PEEK` handling may leave `lastpbuf` in a state that interacts badly with
  readiness or recv window updates.
- `copied == 0` edge cases may leave a pbuf buffered without a matching
  readiness notification.
- A short read that ends at `len` can leave queued data in lwIP but not notify
  epoll strongly enough.
- `sock_tcp_wait_for_more()` sleeps up to 4 ms over partial reads; this can
  change the timing of GLib's TLS read loop. It is not yet proven wrong, but it
  is suspicious under many tiny TLS reads.

Validation:

- Add temporary per-socket counters keyed by `struct lwip_sock *` and fd:
  - total successful bytes
  - read call count
  - `EAGAIN` count
  - current `recv_avail`
  - `recvmbox.count`
  - `lastpbuf` presence
  - current epoll mask if available
- Log only every 256 KB or on terminal state to avoid slowing the VM.
- Remove trace before committing.

### 3. kqueue Propagation For Nested epoll/GLib Main Loops

GLib can monitor eventfds, pipes, sockets, timers, and internal wakeup fds
through epoll. The kernel epoll layer is implemented over kqueue, including
nested readiness propagation for kqueue fds.

Potential gaps:

- Outer kqueue propagation only happens for newly queued knotes in
  `__knote_enqueue_core()`. If the inner kqueue already has a queued knote,
  outer readiness may not be refreshed for another waiter.
- `kqueue_file_poll()` rescans registered knotes, but an already queued inner
  knote may not propagate to the outer layer when the outer layer sleeps.
- If WebKit/GLib uses an epoll fd monitored by another poll source, readiness
  can be present but not wake the top-level waiter.

Validation:

- Trace `epoll_create1`, `epoll_ctl`, and `epoll_pwait` for
  `WebKitNetworkProcess`.
- Trace kqueue fd poll results and propagation counts, not every event.
- Confirm whether the stalled network socket is monitored directly or through a
  nested GLib wakeup source.

### 4. TCP Window/Credit Behavior Under Sustained Tiny TLS Reads

The browser reads in small chunks. `netconn_tcp_recvd()` is called after each
copy. The naive batching experiment regressed, but that does not prove the
current credit path is perfect.

Potential gaps:

- Window update timing may be too chatty or too delayed for high-latency HTTPS
  body streams.
- `TCP_WND` and scaling values interact with QEMU user networking and YouTube's
  server behavior; larger is not automatically better.
- `recv_avail` can be positive while userspace is asleep, which may be readiness
  more than flow control.

Validation:

- Instrument `netconn_tcp_recvd()` call counts and total credited bytes per
  socket.
- Compare credit progress between a good log and a failing log.
- Avoid committing batching unless it improves both CSS and the 9.7 MB JS.

### 5. Process/Main-Loop Liveness Rather Than Network Transport

The WebKit NetworkProcess may stop scheduling reads because another kernel API
used by GLib is incomplete:

- `eventfd`
- `timerfd`
- futex
- pipes
- poll/ppoll/select
- signal masks around `epoll_pwait`
- thread wakeups

Evidence currently points to socket/epoll, but do not exclude GLib wake source
semantics.

Validation:

- Trace `epoll_pwait()` returns and fd masks. If it returns only eventfd/timerfd
  events and never the readable socket, inspect nested wake sources.
- If it returns the socket repeatedly but WebKit does not call `recvfrom()`, the
  gap may be userspace main-loop state or another kernel primitive.
- If it never returns despite `sock_poll_ready()` being true, the gap is
  kqueue/epoll delivery.

### 6. WebProcess/NetworkProcess IPC Backpressure

Small resources reaching `CACHEDSCRIPT-FINISH-YT` while the large JS does not
could also indicate IPC or shared memory pressure between WebKit processes.

Kernel areas to inspect:

- Unix domain socket readiness and backpressure:
  - `kernel/vfs/unix_socket.c`
- shm/mmap and shared memory:
  - `kernel/ipc/shm.c`
  - `kernel/mm/vm.c`
- pipe/eventfd readiness:
  - `kernel/vfs/pipe.c`
  - `kernel/vfs/eventfd.c`

Validation:

- Confirm the network loader is still reading the large response. If it is, but
  WebCore never receives completion, inspect IPC.
- If the network loader stops reading while socket data is available, fix
  socket/epoll first.

## Valuable Investigation Directions

### Direction A: Minimal epoll Mask Trace

Add a temporary trace in `kernel/kqueue/epoll.c`:

- Log only for `current->name` starting with `WebKitNetwork`.
- In `sys_epoll_ctl()`, print:
  - pid
  - epfd
  - op
  - fd
  - event mask
  - data
- In `sys_epoll_pwait()`, print only when:
  - returning an event for a socket fd of interest
  - returning zero after a timed wait while any registered socket is readable

Keep output low. Console logging can itself change timing.

Expected useful answer:

- Whether WebKit uses `EPOLLET` or `EPOLLONESHOT`.
- Whether the stalled fd remains registered for `EPOLLIN`.
- Whether `epoll_pwait()` returns the fd after `recv_avail > 0`.

### Direction B: Kernel-Internal Readiness Audit

Add a debug-only function that, for WebKitNetworkProcess, scans the current
epoll/kqueue registrations and checks each socket with `sock_poll_ready()`.

Useful state:

- fd
- registered filters
- knote flags/status
- socket readiness from `sock_poll_ready()`
- `recv_avail`
- `recvmbox.count`
- `lastpbuf`

Trigger this only on:

- `epoll_pwait()` timeout/zero return
- every N seconds while WebKit is loading YouTube
- a single chosen fd observed in `sock-progress`

Expected useful answer:

- Data exists, poll says readable, but kqueue has no queued event.
- Data exists, poll says not readable: bug in `sock_has_rx_data()`.
- No data exists: go back to TCP/lwIP flow-control analysis.

### Direction C: Requeue EV_CLEAR Read Knotes If Still Readable After Delivery

If WebKit uses `EPOLLET`, mapped to `EV_CLEAR`, current kqueue handling clears
event state after delivery. Linux edge-triggered epoll expects user code to
drain until `EAGAIN`; but if the kernel itself returns short reads or leaves
`lastpbuf`/mailbox data after a read, missing requeue can park userspace.

Do not blindly turn all edge-triggered epoll into level-triggered epoll. Instead
validate:

- Was the fd still readable immediately after the event was delivered?
- Did userspace receive an event and then read only part of the available data?
- Did the kernel generate another event after the read path noticed more data?

Possible complete fixes:

- Ensure socket read paths always call `vfs_file_knote_notify()` when data
  remains after a non-`MSG_PEEK` read.
- Ensure outer kqueue propagation happens even if an inner knote was already
  queued but the outer waiter needs a wake.
- Consider a Linux epoll compatibility rule: for socket read filters, if
  `EV_CLEAR` is set and the source remains readable after delivery, leave enough
  state for the next `epoll_pwait()` slice to see it.

### Direction D: Eliminate Ambiguous Short-Read Wait Behavior

`sock_tcp_wait_for_more()` can sleep after partial reads when userspace asked
for more bytes. TLS stacks often intentionally ask for exact header/body sizes.

Experiment carefully:

- Reduce or remove the 1 ms sleeps for WebKit TCP sockets only as a diagnostic.
- Validate with YouTube and small resource completion.
- If it helps, generalize the behavior based on nonblocking semantics rather
  than process name.

Risk:

- Returning too eagerly can increase epoll churn but should match nonblocking
  Linux socket behavior better than sleeping inside a read syscall after partial
  progress.

### Direction E: Compare Against lwIP sockets.c

The local kernel socket layer reimplements parts of lwIP's socket API. Compare:

- `kernel/lwip/src/api/sockets.c`
- `lwip_recvfrom()`
- `lwip_recv_tcp()`
- `lwip_selscan()` and select callback behavior
- `netconn_recv_data_tcp()`

Look for semantic differences around:

- nonblocking partial reads
- receive-window updates
- last pbuf handling
- `FIONREAD`
- event notification after partial consumption

Do not copy upstream blindly; adapt the semantics to this kernel's VFS and
kqueue architecture.

### Direction F: Build A Kernel-Side Repro Test

YouTube is high-value but noisy. Create a small user test if possible:

- Host serves one HTTPS or plain TCP response larger than 10 MB.
- Guest client uses epoll + nonblocking `recvfrom()` in the same pattern:
  - read 5 bytes
  - read payload chunks
  - wait in epoll
  - repeat until EOF
- Assert that epoll continues to wake while bytes remain.

This would isolate kernel socket/epoll behavior from WebKit complexity.

## Things Not To Do

- Do not hardcode YouTube URLs, user agents, or final DOM state as a success
  shortcut.
- Do not force mobile YouTube.
- Do not patch WebKit around kernel gaps unless kernel behavior has been proven
  Linux-compatible.
- Do not leave diagnostic traces enabled in committed kernel code.
- Do not launch multiple VMs without checking and killing stale QEMU processes.
- Do not interpret `sigaction(SIGSEGV)` log lines as real crashes. They often
  show handlers being installed, not signals being delivered.
- Do not overinterpret WebProcess teardown with `wait_status=0 killed=0`; that
  can be normal process replacement during navigation.

## Commit And Push Workflow

This repo uses nested submodules. Commit deepest changes first:

```sh
git -C kernel status --short --branch
git -C kernel add <files>
git -C kernel commit -m "<kernel message>"
git -C kernel push
```

Then update the top-level pointer:

```sh
git status --short --branch
git add kernel
git commit -m "Update <kernel change summary>"
git push
```

Do not stage unrelated untracked artifacts such as `ports/.../MiniBrowser.bak`.

## Final Validation Checklist

After a candidate fix:

1. Build kernel:

   ```sh
   cmake --build build-x86_64 --target kernel -j2
   ```

2. Run Sparse:

   ```sh
   cmake --build build-x86_64 --target kernel-sparse -j2
   ```

3. Launch one YouTube VM with KVM/GTK and capture logs.

4. Verify:

   ```sh
   strings /tmp/xv6-youtube-test.log |
     rg 'ytmainappweb|NDT-HASH|NDT-READ-EOF|ytInitialData|MB-LOAD|ASSERTION|PANIC|mailbox full'
   ```

5. Required success:

   - big `ytmainappweb` has `NDT-HASH` and `NDT-READ-EOF`
   - `ytInitialData:true`
   - `MB-LOAD event=finished progress=1.000`
   - no kernel panic/assertion/mailbox exhaustion
   - no VM freeze

6. Clean up:

   ```sh
   ps -eo pid,ppid,stat,comm,args | rg 'qemu-system|launch-gui|xv6-youtube|xv6-recvmmsg2' || true
   ```

## Current Best Hypothesis

The strongest current hypothesis is:

> Desktop YouTube stalls because WebKit's network process stops receiving
> read-readiness progress for a still-readable TCP socket after partial
> nonblocking TLS reads. The kernel has bytes available (`recv_avail`,
> mailbox entries, and/or `lastpbuf`), but the epoll/kqueue compatibility layer
> or nested readiness propagation does not reliably wake GLib/WebKit to finish
> draining the 9.7 MB JavaScript response.

Future agents should try to disprove this first. If disproven, use the same
evidence-driven approach to move outward: socket read semantics, nested epoll,
GLib wake primitives, then WebKit IPC.
