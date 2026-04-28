# WebKitGTK Override-To-Kernel Gap Map

This map covers every file under
`ports/webkit/overrides/webkitgtk-2.42.5`.  The goal is to keep WebKit source
overrides tied to a kernel/user ABI reproducer so each workaround can later be
retired deliberately.

Reproducer names refer to existing or newly-added xv6 user programs:

- `webkitabitest`: WebKit-shaped ABI coverage for AF_UNIX IPC, fd passing,
  memfd/shared mmap, VFS cache operations, waitpid cleanup, and random devices.
- `webkitnettest`: WebKit-shaped TCP coverage for nonblocking connect,
  `poll(POLLOUT)`, `getsockopt(SO_ERROR)`, short send/recv behavior, and
  multiple concurrent TCP connections.
- `cloexectest`: close-on-exec and fd inheritance coverage.
- `kqueuetest`: kqueue/readiness coverage.
- `mmaptest` / `mmapbigfile`: mmap, mprotect, and large mapping coverage.
- `syscalltest`: broad syscall coverage, including memfd, fsync/fdatasync, and
  System V IPC.
- `testsig`: signal delivery coverage.

## Policy Decisions

| Area | Decision | Retirement Condition |
| --- | --- | --- |
| `AF_UNIX SOCK_SEQPACKET` | Do not implement for the current WebKit bring-up. Keep WebKit on `SOCK_STREAM` and test stream behavior instead. | Revisit only if stream IPC remains fragile after `sendmsg`/`recvmsg`, `SCM_RIGHTS`, nonblocking, and poll semantics pass under stress. |
| WebKit sandboxing | xv6 intentionally runs WebKit without Linux namespaces/seccomp/bubblewrap for now. | Add a sandbox design and ABI tests before retiring no-sandbox overrides. |
| Accelerated compositing | Keep disabled; xv6 uses software Wayland SHM rendering. | Define EGL/GBM/DMABUF kernel ABI, fencing, mmap/ioctl, and lifetime tests before enabling. |
| JSC JIT | Keep disabled; interpreter-only JavaScript is the validation target. | Pass executable-memory and instruction-cache tests on every supported architecture. |
| Disk network cache | Keep disabled or guarded until VFS cache reproducers pass. | Pass nested create/write/read/rename/unlink/fsync/truncate/mmap cache tests. |

## Override Table

| Override file | Kind | xv6 primitive or policy | Minimal non-WebKit reproducer | Notes / retirement rule |
| --- | --- | --- | --- | --- |
| `Source/WTF/wtf/Assertions.cpp` | Debug-only | Crash/assertion observability | Runtime log inspection | Keep until kernel/user crash reporting is enough to identify abort sites without WebKit file logs. |
| `Source/WTF/wtf/glib/SocketConnection.cpp` | Kernel gap | `AF_UNIX`, nonblocking stream IPC, poll/HUP/error semantics | `webkitabitest`, `kqueuetest` | Retire after GLib socket connection tests pass without early HUP/ERR closure. |
| `Source/WebCore/PAL/pal/crypto/gcrypt/Initialization.h` | Runtime policy | Crypto initialization and entropy availability | `webkitabitest` random-device case, `syscalltest` getrandom coverage | Retire after `/dev/random`, `/dev/urandom`, and `getrandom()` are stable in rootfs and helper processes. |
| `Source/WebCore/loader/DocumentLoader.cpp` | UX/debug workaround | Navigation lifecycle under slow helper/process IPC | Validation ladder | Retire only after repeated local/HTTP/HTTPS navigation and close/reopen pass. |
| `Source/WebCore/loader/ResourceLoader.cpp` | Debug/network workaround | Network loading, cancellation, socket EOF/reset | Future HTTPS socket test, validation ladder | Retire after nonblocking connect/TLS and repeated navigation are stable. |
| `Source/WebCore/loader/appcache/ApplicationCacheHost.cpp` | Policy | Offline app cache disabled | VFS cache-shape tests | Retire only if appcache storage semantics are supported. |
| `Source/WebCore/loader/cache/CachedRawResource.cpp` | Debug/network workaround | Cache/resource loader lifecycle | VFS cache-shape tests, validation ladder | Retire after disk/memory cache reproducers pass. |
| `Source/WebCore/platform/network/soup/SoupNetworkSession.cpp` | Kernel/runtime gap | libsoup/GIO TLS, DNS, network cache, socket behavior | Future HTTPS socket test, `validate-webkit-runtime.sh` | Retire after OpenSSL/GIO fetch succeeds and cache decision is settled. |
| `Source/WebKit/NetworkProcess/EntryPoint/unix/NetworkProcessMain.cpp` | Debug-only | Helper process crash tracing | Runtime helper launch validation | Retire after helper launch/failure is visible through kernel logs or process accounting. |
| `Source/WebKit/NetworkProcess/NetworkConnectionToWebProcess.cpp` | Kernel gap | UI-to-NetworkProcess IPC, `SCM_RIGHTS`, nonblocking sendmsg | `webkitabitest` SCM_RIGHTS batch case | Retire after fd-batch passing and large startup IPC pass under stress. |
| `Source/WebKit/NetworkProcess/NetworkProcess.cpp` | Debug/kernel gap | NetworkProcess lifecycle, cache/session setup, IPC | `webkitabitest`, validation ladder | Retire after helper process remains alive through Google load/search/idle. |
| `Source/WebKit/NetworkProcess/NetworkResourceLoader.cpp` | Debug/network workaround | HTTP/TLS loader, EOF/reset/error handling | Future HTTPS socket test | Retire after HTTP and HTTPS validation pass without WebKit-specific loader guards. |
| `Source/WebKit/NetworkProcess/NetworkSession.cpp` | Runtime/VFS policy | Network disk cache and storage session behavior | `webkitabitest` VFS cache-shape case | Retire after cache directory reproducer passes and disk cache is re-enabled. |
| `Source/WebKit/NetworkProcess/soup/NetworkSessionSoup.cpp` | Runtime/network policy | libsoup session setup, DNS/TLS/cache | Future GIO/OpenSSL fetch test | Retire after GIO TLS module and resolver paths work from the rootfs. |
| `Source/WebKit/Platform/IPC/unix/ConnectionUnix.cpp` | Kernel gap | `AF_UNIX` IPC framing, `sendmsg`/`recvmsg`, `SCM_RIGHTS`, buffer sizing | `webkitabitest`, `kqueuetest` | Keep stream and buffer-size overrides until whole-message nonblocking behavior is proven. |
| `Source/WebKit/Platform/unix/SharedMemoryUnix.cpp` | Kernel gap | `memfd_create`, `ftruncate`, `MAP_SHARED`, fd passing | `webkitabitest`, `mmaptest`, `mmapbigfile` | Retire direct memfd workarounds after shared-memory lifecycle and sealing policy are settled. |
| `Source/WebKit/PlatformGTK.cmake` | Build/runtime policy | Disabled features, static/shared selection, introspection/docs/tests | Container build, `webkit-runtime-check` | Retire items one feature at a time, never wholesale. |
| `Source/WebKit/Shared/AuxiliaryProcess.cpp` | Runtime/process gap | Helper process initialization and IPC teardown | `webkitabitest` waitpid case, future spawn stress | Retire after repeated helper launch/exit cleanup has no leaked fds/processes. |
| `Source/WebKit/UIProcess/API/glib/WebKitNavigationClient.cpp` | Debug/UX workaround | Navigation errors and helper termination reporting | Validation ladder | Retire after user-visible navigation state matches helper/process state without extra logs. |
| `Source/WebKit/UIProcess/Launcher/glib/ProcessLauncherGLib.cpp` | Kernel/process gap | `fork`/exec or `posix_spawn`-like fd inheritance, child watch/waitpid | `cloexectest`, `webkitabitest` waitpid case, future spawn stress | Retire after GLib `GSubprocess` patterns pass without duplicate reaping. |
| `Source/WebKit/UIProcess/Network/NetworkProcessProxy.cpp` | Kernel/process/IPC gap | Network helper launch, sync IPC, timeout behavior | `webkitabitest`, validation ladder | Retire after UI-to-NetworkProcess request reaches fetch setup reliably. |
| `Source/WebKit/UIProcess/WebPageProxy.cpp` | Debug/UX workaround | Page lifecycle, responsiveness, process exit | Validation ladder | Retire after close/reopen, idle, and repeated navigation pass. |
| `Source/WebKit/UIProcess/gtk/HardwareAccelerationManager.cpp` | Policy | No EGL/GBM/DMABUF acceleration | None until acceleration ABI exists | Keep disabled for software-only SHM rendering. |
| `Source/WebKit/WebProcess/Network/NetworkProcessConnection.cpp` | Kernel IPC gap | WebProcess-to-UI sync IPC and fd passing | `webkitabitest` SCM_RIGHTS batch case | Retire after `GetNetworkProcessConnection` succeeds repeatedly under load. |
| `Source/WebKit/WebProcess/Network/WebLoaderStrategy.cpp` | Debug/network workaround | Resource loading IPC and network process dispatch | Validation ladder | Retire after Google search/navigation is stable. |
| `Source/WebKit/WebProcess/Network/WebResourceLoader.cpp` | Debug/network workaround | Resource loader error/cancel paths | Future HTTPS socket test | Retire after loader behavior is stable without extra guards/logs. |
| `Source/WebKit/WebProcess/WebCoreSupport/WebLocalFrameLoaderClient.cpp` | Debug/UX workaround | Frame loader lifecycle under slow IPC | Validation ladder | Retire after local/HTTP/HTTPS navigation and page-close cases pass. |
| `Source/WebKit/WebProcess/WebProcess.cpp` | Kernel/thread/process gap | Thread lifecycle, signal/default action, helper IPC | `testsig`, future pthread/thread stress, validation ladder | Retire after WebProcess workers exit cleanly with no zombie leader/lost UIProcess. |
| `Source/cmake/OptionsGTK.cmake` | Build/runtime policy | Feature flags: sandbox, JIT, acceleration, cache, docs/tests | Container build, validation ladder | Keep conservative feature set until each dependent xv6 primitive has tests. |
| `Tools/MiniBrowser/gtk/BrowserTab.c` | UX workaround | GTK/Wayland surface and tab UI behavior | Validation ladder | Retire after compositor geometry/input behavior is stable. |
| `Tools/MiniBrowser/gtk/BrowserWindow.c` | UX workaround | Window geometry, URL entry size, close/input routing | Validation ladder, compositor input tests | Retire after xdg geometry and input routing pass with unmodified MiniBrowser UI. |
| `Tools/MiniBrowser/gtk/main.c` | Runtime policy/UX | Environment, settings profile, JSC disablement, app lifecycle | Validation ladder, `validate-webkit-runtime.sh` | Retire settings/lifecycle workarounds one at a time after kernel/runtime tests pass. |

## Current Reproducer Coverage

| TODO area | Current coverage | Remaining gap |
| --- | --- | --- |
| Unix sockets and IPC | `webkitabitest` covers stream socketpair, nonblocking readiness, `SOCK_CLOEXEC`, and multi-fd `SCM_RIGHTS`. | Add long-run stress for large WebKit IPC payloads and writable readiness thresholds. |
| Process launch/fd inheritance | `cloexectest` and `webkitabitest` cover close-on-exec and waitpid. | Add a musl/GLib-like spawn helper test when musl-linked test programs are easy to run in CI. |
| Shared memory/mmap | `webkitabitest`, `mmaptest`, `mmapbigfile`, `syscalltest`. | Add memfd sealing tests if xv6 chooses to support `F_ADD_SEALS`. |
| VFS/cache semantics | `webkitabitest` cache-shape test plus `syscalltest` fsync/fdatasync. | Add mmap-backed truncate-after-mmap and advisory-lock coverage. |
| Networking | `webkitnettest` covers nonblocking connect, SO_ERROR, send/recv, and parallel TCP connection pressure. | Add a GIO/OpenSSL guest fetch once a musl-linked TLS smoke binary is available. |
| Signals/timers/threads | `testsig`, `clonetest`, `kqueuetest`, and `webkitabitest` timerfd coverage. | Add WebKit-like pthread count/futex stress once musl-linked test programs are easy to run in CI. |
| Executable memory/JSC | `webkitabitest`, `mmaptest`, and `mmapbigfile` cover mprotect basics and executable mapping policy. | Add architecture-specific instruction-cache coherency checks before enabling JIT. |
| Wayland/compositor | Kernel ABI audit is documented as framebuffer/input/ioctl plus kqueue readiness; compositor behavior remains userspace-owned. | Add automated compositor SHM buffer release stress. |
| Runtime reproducibility | `validate-webkit-runtime.sh` and `webkit-runtime-check`. | Add a true QEMU boot smoke once the image boot can be made deterministic in CI. |
