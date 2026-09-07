# Port and rootfs diagnostics

Use for an explicitly selected NetSurf, curl, OpenSSL, OpenSSH, or legacy
Wayland port issue. The normal GUI audit uses KDE/KWin and the GUI runtime
skill. Inspect the staged executable and its launch path before applying a
historical port assumption to an imported host program.

## NetSurf and TLS

- `ports/netsurf/CMakeLists.txt` enables `NETSURF_USE_CURL` and
  `NETSURF_USE_OPENSSL` and declares curl/OpenSSL build dependencies. Check the
  configured target graph if staging order is suspect.
- `ports/curl/CMakeLists.txt` selects the sysroot's static `libcrypto.a` and
  `libssl.a`. For that link path, absence of `libssl.so` from `readelf -d`
  does not prove TLS is missing. Inspect the actual link recipe and symbols
  such as `Curl_ssl_openssl` or `EVP_*` where symbols are available.
- Separate a fetch failure into mapped window, requested URL, DNS resolution,
  socket connect, bytes sent/received, TLS/certificate trust, and page render.
  Inspect the image's `/etc/resolv.conf`, CA bundle, and application resources.
  QEMU user networking normally supplies DNS at `10.0.2.3`; verify that this
  run actually uses that network backend.
- Imported browser launch flags can disable acceleration or change the process
  graph. Derive flags and roles from the running executable rather than from
  a different port's launcher or historical log.

## OpenSSL, OpenSSH, and image staging

- `ports/openssl/CMakeLists.txt` stages `/bin/openssl`, headers, and static libs.
  `ports/openssh/CMakeLists.txt` stages clients, `sshd`, and
  `/libexec/sshd-session`; verify the helper closure as well as the main binary.
- Use the already-staged OpenSSL sysroot when building this OpenSSH port. Keep
  unsupported integration switches aligned with its configure recipe. The
  recipe copies source into the build directory and touches that copied
  `configure` to avoid m4 timestamp regeneration; do not dirty the upstream
  source as a timestamp workaround.
- `scripts/image/make-rootfs.sh` stages the sysroot and overlays. Runtime
  account, resolver, certificate, SSH config/key, and writable-state paths must
  exist in the image, not merely in the host sysroot.
- Check ownership in the resulting ext4 image: `mke2fs -d` preserves source
  UID/GID. Host-generated SSH private keys require root ownership and `0600`;
  use the image builder's fixups. Check the staged `/etc/daemons` and init
  source before attributing a missing daemon to an exec failure.

## Legacy Wayland diagnostics

- For an older `desktop`/`wlcomp` image only, distinguish boot autostart,
  desktop shortcut activation, and manual launch. Its `netsurf=0` handling
  and `/tmp/app_log.txt` are not the current KDE startup contract.
- Match the actual executable's libc and syscall ABI. Current normal image
  construction expects host glibc; old xv6-musl syscall-number advice cannot be
  applied to imported Linux ELF programs.
- A mapped MiniBrowser window does not prove its network/web subprocesses,
  injected bundle, GIO TLS module, and shared libraries are present. Inspect
  that image's executable/library closure before rebuilding the kernel.
- Clean exit PCs near libc `_Exit` or clone teardown do not alone prove a
  fault. Correlate process role, exit status, signal, coredump, and kernel
  fault evidence from the same execution.
