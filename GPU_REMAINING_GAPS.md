# GPU Plan: Real NVIDIA GPU via Hyper-V DDA + Nouveau

Last updated: 2026-05-28

## Mission (Non-Negotiable Invariant)

This plan has exactly one goal, stated three ways. All three must be literally
true in the same validation run before any success is claimed:

1. **Aim for Nouveau.** The guest GPU driver is the in-tree Nouveau port
   (`kernel/kernel/dev/fb/fb_nouveau.c` and its PCI/DRM/KMS support). Mesa's
   Nouveau / NVK userspace is the rendering stack. We do **not** chase the
   GPU-P / DXG / D3DKMT paravirtual path for acceleration credit.
2. **Invoke Hyper-V.** The transport is a real Hyper-V virtual machine. The
   physical GPU reaches the guest over the Hyper-V virtual-PCI (vPCI / VMBus)
   bus that already exists in `kernel/kernel/dev/hyperv/` and `kernel/kernel/pci.c`.
3. **Genuinely invoke the host NVIDIA GPU.** The acceptance path is **Discrete
   Device Assignment (DDA)** — the physical NVIDIA PCIe GPU is dismounted from
   the Windows host and assigned to the xv6 VM. Nouveau then drives the *real
   silicon*: it reads the real chipset ID from MMIO, sizes real VRAM, loads
   real signed firmware, submits real commands to a real engine, and gets back
   real hardware fences. No emulation, no synthetic GETPARAM answers, no host
   proxy, no readback-as-present.

> Why this replaces the old plan: the previous tracker (now archived at
> `GPU_REMAINING_GAPS.dxg-failclosed-archive.md`) was blocked forever on a
> `dxg-resource-scanout-bind` host ABI that does not exist in WSL `dxgkrnl`
> (re-confirmed 2026-05-28 against `/home/es/reps/WSL2-Linux-Kernel` commit
> `427645e3db`: zero scanout/display-bind ioctls or VMBus senders). DDA is the
> only honest way to reach a *real* NVIDIA GPU under Hyper-V, so the whole plan
> is re-centered on it. The archived DXG fail-closed skeleton stays as-is and
> is no longer the acceleration path.

## How To Use This Plan (read first — written for a fresh agent)

- Work **top to bottom**. Each section depends on the one before it. Do not
  skip ahead; a later section cannot pass if an earlier one is faked.
- Each `[ ]` item lists: **what to build**, **which files**, **how to prove
  it**, and the **acceptance evidence** that lets you check the box.
- "Real-hardware evidence" always means a value that *could only come from the
  physical GPU* (chipset ID register, VRAM size probe, a completed hardware
  fence, a GL/Vulkan draw result). A counter that you set in software is **not**
  evidence.
- **Never** mark an item `[x]` from build-success alone. You need build +
  runtime evidence + a negative test (proving the fail-closed path still
  rejects fakes).
- If the real GPU is unavailable in your environment, you may implement and
  build the code, but you must leave the item `[ ]` and write
  `BLOCKED: no DDA hardware available` under it. Do not invent passing runs.

## Build And Run Commands

The configured kernel build directory is `build-codex-x86_64` (NOT
`/tmp/xv6-hyperv-build`, which does not exist on this checkout).

```sh
# Kernel only (fast inner loop):
cmake --build build-codex-x86_64 --target kernel -j"$(nproc)"

# Full world (kernel + user + ports + rootfs) — reuse existing toolchain:
mkdir -p build-x86_64/toolchain
cp -al build-toolchain-x86_64/* build-x86_64/toolchain/ 2>/dev/null || true
cmake -S . -B build-x86_64 -DXV6_ARCH=x86_64
cmake --build build-x86_64 --target world -j"$(nproc)"
```

Build artifacts:
- kernel ELF: `build-x86_64/kernel/build/kernel/kernel`
- rootfs:     `build-x86_64/fs.img`

The Hyper-V image used for DDA testing is `xv6-hyperv.vhdx` at the repo root.
Refresh it from the built kernel/rootfs with the existing image script in
`cmake/BuildImage.cmake` / `scripts/` before each hardware run.

## Reference Baselines (cite these, not WSL dxgkrnl)

- **Linux Nouveau** (`drivers/gpu/drm/nouveau` and `drm/nouveau/nvkm`): the
  authoritative model for PCI probe, MMIO register map, VRAM/instmem, FIFO
  channels, GPFIFO/pushbuf, firmware (GSP-RM / FECS / GPCCS), and KMS display.
  Use a current upstream Linux tree as the anchor.
- **Mesa Nouveau / NVK** (`src/nouveau`, `src/gallium/drivers/nouveau`): the
  userspace winsys, the `nouveau_ws_device` open path, and the GL/Vulkan
  command streams. NVK targets Turing (TU10x) and newer via GSP; classic
  Gallium Nouveau GL targets up to Pascal/Maxwell.
- **NVIDIA open-gpu-kernel-modules** and **envytools/nvgpu register DB**: the
  register definitions and firmware boot sequences for the real silicon.
- **Microsoft DDA docs** ("Plan for deploying devices using Discrete Device
  Assignment", `Dismount-VMHostAssignableDevice` / `Add-VMAssignableDevice`):
  the host-side passthrough procedure.
- The existing in-tree **Hyper-V vPCI** code (`HVPCI_*` messages in
  `kernel/kernel/dev/hyperv/hyperv_defs_state.c`) mirrors Linux `pci-hyperv.c`.

## Hardware And Host Prerequisites (do this before any guest code runs)

These are operational steps on the **Windows Hyper-V host**, not xv6 code, but
the guest path cannot work until they are done. Record the exact output of each
command in the validation log.

- [ ] **H1. Confirm the host can do DDA.**
  - IOMMU enabled in firmware (Intel VT-d or AMD-Vi) and SR-IOV/ACS such that
    the GPU sits in its own IOMMU group.
  - Hyper-V role installed; Windows Server 2019+ or Windows 11 Pro/Enterprise
    with a GPU that supports Function Level Reset (FLR) or a PCIe reset method
    Hyper-V accepts.
  - The GPU must **not** be the host's primary/boot display. Use the iGPU or a
    second GPU for the host console.
  - Honesty note: the old repo memory claim "DDA needs SR-IOV-class GPU" is
    wrong — that was RemoteFX vGPU. DDA is plain IOMMU PCIe passthrough and
    works with many consumer GeForce cards, *if* FLR/reset and isolation hold.
    If your specific card cannot be cleanly reset, record
    `BLOCKED: GPU not DDA-capable` and stop.
  - Evidence: `Get-VMHostAssignableDevice` lists nothing yet;
    `(Get-PnpDevice -PresentOnly).Where{$_.InstanceId -match 'PCI\\VEN_10DE'}`
    shows the NVIDIA device; record its `LocationPath` from
    `Get-PnpDeviceProperty -KeyName DEVPKEY_Device_LocationPaths`.

- [ ] **H2. Dismount the GPU from the host and assign it to the VM.**
  - Disable the host's NVIDIA driver for the device, then:
    `Dismount-VMHostAssignableDevice -Force -LocationPath "<path>"`.
  - `Add-VMAssignableDevice -LocationPath "<path>" -VMName "<xv6-vm>"`.
  - Give the VM enough MMIO window for the GPU BARs:
    `Set-VM -VMName "<xv6-vm>" -GuestControlledCacheTypes $true \
      -LowMemoryMappedIoSpace 3Gb -HighMemoryMappedIoSpace 33Gb`
    (raise HighMemoryMappedIoSpace to cover the card's large BAR1/resizable
    BAR; a 24 GB card needs >= 32 GB).
  - Evidence: `Get-VMAssignableDevice -VMName "<xv6-vm>"` lists the NVIDIA
    device; the VM starts without an MMIO-space error.

- [ ] **H3. Stage Nouveau firmware for the guest.**
  - Identify the chipset family (Maxwell GM20x / Pascal GP10x / Turing TU10x /
    Ampere GA10x / Ada AD10x). This decides the firmware and the userspace
    stack (classic Gallium GL vs NVK).
  - Copy the matching `linux-firmware` `nvidia/<chip>/` blobs (and for Turing+
    the GSP-RM firmware image) into the rootfs overlay under
    `rootfs-overlay/lib/firmware/nouveau/` so the guest can load them.
  - Evidence: the firmware files are present in `build-x86_64/fs.img` after a
    `world` build; list them from the running guest.

## Current Honest State (2026-05-28)

- Hyper-V vPCI/VMBus transport exists; `pci_note_nvidia_gpu` already records an
  NVIDIA PCIe candidate (vendor `0x10DE`), its BARs, and MSI/MSI-X caps.
- The Nouveau driver is a **scaffold**: `fb_nouveau.c` registers a
  `drm_core_driver` named "nouveau", has GETPARAM/channel/NVIF/submit entry
  points, and a large set of `nouveau_*` stats/diagnostic fields — but every
  hardware-facing answer is currently **fail-closed / synthetic-rejected**.
  No real chipset ID is read, no real VRAM is sized, no firmware is loaded, no
  real command reaches an engine, and no real KMS scanout happens.
- Linux-shaped PCI wrappers exist (`dma_set_mask_and_coherent`, `pci_enable_msi`,
  `pci_request_irq`, `pm_runtime_*`, claim-before-iomap) and Nouveau uses them
  in fail-closed mode.
- `FB_GPU_BACKEND_F_OPENGL_SUBMIT` is `0` on Hyper-V and must stay `0` until the
  real Nouveau-on-DDA OpenGL path proves itself.
- No DDA hardware is attached to the current development checkout (it is WSL).
  All hardware-evidence items below are therefore `[ ]` until run on a real
  Hyper-V + NVIDIA host.

## Honesty Gates (carry over, adapted to real hardware)

- Keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT == 0` on Hyper-V until: Nouveau opens the
  **real** assigned GPU, a **real** command submission completes with a **real**
  hardware fence, Mesa renders a frame on the GPU, and the 480p demo sustains
  > 60 FPS after warmup — all in one lineage.
- A PCI probe, a mapped BAR, an allocated channel handle, a synthetic GETPARAM,
  a render-node node, a dmabuf request, or a displayed FPS number is **not**
  real-GPU evidence by itself.
- Every hardware fact must be traceable to a register read or DMA result from
  the assigned device. Add kernel diagnostics that print the raw register
  offset and value the driver actually read.
- No synthetic/emulated GPU answers may earn credit. If the real device is
  absent, the path must fail closed, not fabricate values.
- Heavy GUI/FPS/WebKit validation runs after a whole section is code-complete,
  not after every edit. Use pure-C guest validators (`gpucorevalidate`,
  `drmiftest`, `nouveauabitest`, `dxgprobe`) for slice checks.

## Current Source Map

- NVIDIA PCIe candidate probe + BAR/MSI capture: `kernel/kernel/pci.c`
  (`pci_note_nvidia_gpu`, `nvidia_gpu_pci_devs`).
- Hyper-V vPCI/VMBus transport: `kernel/kernel/dev/hyperv/hyperv_defs_state.c`
  (`HVPCI_*`), `kernel/kernel/dev/hyperv/*.c`.
- Nouveau driver + DRM/KMS/PCI runtime: `kernel/kernel/dev/fb/fb_nouveau.c`,
  `kernel/kernel/dev/fb/fb_internal.c`, `kernel/kernel/dev/fb/fb_drm_*.c`.
- Linux-shaped PCI wrappers: `kernel/kernel/pci.c` and the `nouveau_pci_*`
  matrices in `fbstat` / `gpucorevalidate`.
- Stats/diagnostic ABI: `kernel/kernel/inc/dev/fb.h` (`nouveau_*` fields).
- Guest validators: `dxgprobe`, `drmiftest`, `gpucorevalidate`,
  `nouveauabitest`, `mesaglfeature`, `mesawlegl`, and the
  `scripts/hyperv-*-validate.sh` runners.

## Dependency Graph (new)

```
H1 host DDA capable
  -> H2 GPU assigned to VM
       -> 1 guest enumerates the REAL NVIDIA device on vPCI
            -> 2 Nouveau real-hardware bring-up (MMIO id, VRAM, firmware)
                 -> 3 real command submission (FIFO/GPFIFO + HW fence)
                      -> 4 real KMS display / scanout (or headless present)
                           -> 5 Mesa Nouveau/NVK OpenGL on the real GPU
                                -> 6 backend OpenGL-submit flag + 480p FPS
                                     -> 7 WebKit consumer on the same contract
```

Until item 1 produces a real chipset ID, every later validator stays
fail-closed and must reject synthetic/emulated evidence.

---

## Section 1. Enumerate the Real NVIDIA Device on Hyper-V vPCI

Goal: prove the assigned physical GPU appears in the guest over Hyper-V vPCI and
that xv6 captures its real identity and BARs.

- [ ] **1.1 Receive the assigned device through the Hyper-V vPCI bus.**
  - File: `kernel/kernel/dev/hyperv/hyperv_defs_state.c` and the vPCI handling
    in `kernel/kernel/dev/hyperv/*.c`; `kernel/kernel/pci.c`.
  - Implement/verify the `HVPCI_QUERY_BUS_RELATIONS` / `BUS_RELATIONS2` handling
    so the assigned NVIDIA function is reported to the guest PCI layer with its
    real BDF, vendor `0x10DE`, device id, and class `0x030000` (VGA) or
    `0x030200` (3D controller).
  - Prove `pci_note_nvidia_gpu` runs for the DDA device and records real BARs
    (BAR0 MMIO ~16 MB, BAR1 VRAM aperture, BAR3 if present), real IRQ
    line/pin, and real MSI/MSI-X capability offsets.
  - Acceptance evidence: boot log line `PCI: NVIDIA GPU candidate at B:D:F
    device=0x.... class=0x30000 ... bar0=0x..../0x....` with **nonzero,
    plausible** BAR sizes that match the physical card. A zero/absent BAR means
    the MMIO window (H2) is too small — fix and re-run.

- [ ] **1.1a Discover the high-MMIO window and assign assigned-device BARs.**
  - On Hyper-V the host does **not** pre-program BAR base addresses; the guest
    bus driver must allocate them from the bus MMIO window (the Linux
    `hv_pci_allocate_bridge_windows` + `hv_pci_assign_resources` step) and write
    them back through config space, or BAR0/BAR1 read back as zero and 1.1/1.3
    can never see nonzero, usable BARs.
  - File: `kernel/arch/x86_64/platform_x86.c` (ACPI), the vPCI handling in
    `kernel/kernel/dev/hyperv/*.c`.
  - CODE DONE 2026-05-28 (BLOCKED on hardware for the assigned-BAR evidence):
    - ACPI now discovers the high-MMIO aperture: `platform_info` gained
      `high_mmio[ACPI_HIGH_MMIO_MAX]` and `x86_acpi_scan_mmio_windows()` parses
      QWord Address Space Descriptors (large tag `0x8A`, memory type, base
      >= 4 GiB) from the DSDT/SSDT — these are the Hyper-V Gen2 VMBus `_CRS`
      producer windows (the aperture set by host `-HighMemoryMappedIoSpace`).
      Values are copied verbatim from the real ACPI descriptors; none invented.
      Boot log: `ACPI: high MMIO window base=0x.. size=0x.. (DDA BAR pool)`.
    - `hvpci` state gained a device-BAR window (`bar_window_base/size/next`,
      `bar_assign_count/fail`); `hvpci_bar_window_init()` selects the largest
      ACPI window clamped to the host offer MMIO budget, and **fails closed**
      (assignment disabled) when no window is advertised — never guesses a base.
    - `hvpci_assign_child_bars()` probes each child's memory BAR sizes through
      the config window, bump-allocates naturally aligned guest-physical
      addresses from the window, writes them back to the BAR registers, and
      enables Memory Space + Bus Master. Runs before PCI-core registration so
      the device is presented with usable resources. Boot log:
      `hyperv-pci: assigned slot=0x.. barN base=0x.. size=0x..`.
    - Kernel builds clean (`build-codex-x86_64`).
  - BLOCKED: no DDA hardware **assigned** yet — the dev box has a real Hyper-V
    host + NVIDIA RTX 4060, but DDA-dismounting the laptop's only GPU is
    destructive and not yet authorized. Leave `[ ]` until a boot on a VM with an
    assigned NVIDIA function shows `ACPI: high MMIO window ...` followed by
    `hyperv-pci: assigned slot=... bar0 ...` with nonzero BAR sizes that match
    the card. Do not invent passing runs.

- [ ] **1.2 Distinguish DDA passthrough from GPU-P and from absence.**
  - Add a stats field + matrix (extend `nouveau_pci_runtime_interface_matrix`
    or add `nouveau_dda_device_presence_matrix`) that reports:
    `dda_nvidia_present`, real `vendor_id`, `device_id`, `class_code`, BAR
    count and sizes, `transport=hyperv_vpci`, and `gpup_dxg_path=0` (this is the
    DDA lane, not DXG).
  - Fail-closed rule: if no `0x10DE` function is present on vPCI, the matrix
    must report `dda_nvidia_present=0` and every later section must reject.
  - Acceptance evidence: `gpucorevalidate` prints the matrix with the real
    device id and `dda_nvidia_present=1`; on a non-DDA image it prints
    `dda_nvidia_present=0 status=PASS_FAILCLOSED`.
  - CODE DONE 2026-05-28 (fail-closed side validated, accept side BLOCKED):
    - `struct fb_gpu_stats` gained `nouveau_dda_present`,
      `nouveau_dda_vendor_id`, `nouveau_dda_device_id`,
      `nouveau_dda_class_code`, `nouveau_dda_bar_count`
      (`kernel/kernel/inc/dev/fb.h`); all copied verbatim from the probed
      `pci_device_info`, never synthesized.
    - `gpu_nouveau_pci_probe()` populates them in the accept path and the
      remove path resets them to zero (`kernel/kernel/dev/fb/fb_nouveau.c`).
    - `gpucorevalidate` emits `nouveau_dda_device_presence_matrix` with
      `transport=hyperv_vpci gpup_dxg_path=0` and
      `status=PASS` when `dda_nvidia_present=1`, else `PASS_FAILCLOSED`.
    - `scripts/hyperv-gpu-core-validate.sh` asserts the fail-closed row
      (`dda_nvidia_present=0 ... status=PASS_FAILCLOSED`).
    - Kernel builds clean (`build-codex-x86_64`); validator C compiles clean.
  - BLOCKED: no DDA hardware available — the `dda_nvidia_present=1` accept row
    cannot be produced on this WSL/no-NVIDIA dev box. Leave `[ ]` until a real
    Hyper-V + assigned NVIDIA host runs the validator and shows the real
    device id with `dda_nvidia_present=1`.

- [ ] **1.3 Claim BARs and enable bus mastering on the real device.**
  - Use the existing claim-before-iomap path in `kernel/kernel/pci.c`. Claim
    BAR0/BAR1, `ioremap` BAR0 (registers), set the PCI command register
    `MEMORY` + `BUS_MASTER` bits, and program a usable DMA mask via
    `dma_set_mask_and_coherent()`.
  - Acceptance evidence: counters show `claim` and `iomap` succeeded for the
    NVIDIA BDF, bus-master bit reads back set, and no `unclaimed_iomap` events.

---

## Section 2. Nouveau Real-Hardware Bring-Up

Goal: replace synthetic Nouveau answers with values read from the real silicon.

- [ ] **2.1 Read the real chipset ID from MMIO `PMC_BOOT_0` (register 0x0).**
  - File: `kernel/kernel/dev/fb/fb_nouveau.c`.
  - After BAR0 is mapped, read the 32-bit value at offset `0x000000`
    (`NV_PMC_BOOT_0`). Decode chipset (e.g. bits identify GM20x/GP10x/TU10x/
    GA10x). This is the single most important "real GPU" proof.
  - Replace the synthetic chipset GETPARAM answer with this decoded value;
    leave it fail-closed (return error) if BAR0 is unmapped or the read returns
    `0xffffffff` (device not responding).
  - Acceptance evidence: boot log prints `nouveau: PMC_BOOT_0=0x........
    chipset=NV1xx family=...` with a value matching the physical card; the
    `nouveau_chipset_real` stat is nonzero; `nouveauabitest` reports the real
    chipset and `synthetic_gpup_rejected` no longer applies.

- [ ] **2.2 Size real VRAM and set up instance memory (instmem).**
  - Probe the framebuffer/VRAM size from the real registers (chip-family
    dependent: `PFB`/`PBFB` config or the GSP-reported FB size on Turing+).
    Map the BAR1 VRAM aperture for CPU access to a small window.
  - Acceptance evidence: `nouveau_vram_bytes` reports the real card size
    (e.g. 8/12/24 GiB), readable via `fbstat`; a CPU write/read round-trip to a
    scratch VRAM offset through BAR1 returns the written pattern (proves real
    VRAM access, not a synthetic number).

- [ ] **2.3 Load real signed firmware for the chipset.**
  - Maxwell GM20x+: load FECS/GPCCS ucode. Turing+ (TU10x and newer): load and
    boot the GSP-RM firmware (this is mandatory for NVK and for any modern
    card). Read blobs from `rootfs-overlay/lib/firmware/nouveau/` (staged in
    H3).
  - Implement the boot/handshake sequence per the Linux nvkm model for the
    detected family. Fail closed (no engine init) if firmware is missing or the
    handshake times out.
  - Acceptance evidence: log prints firmware name, size, and a successful
    boot/ack (`gsp boot ok` or `fecs/gpccs loaded`); `nouveau_fw_loaded=1`.
    Missing firmware reports `nouveau_fw_loaded=0 status=FAIL_CLOSED` and blocks
    Section 3.

- [ ] **2.4 Bring up the IRQ handler against the real device.**
  - Tighten the existing Nouveau IRQ path: read `NV_PMC_INTR_0` gated by
    `NV_PMC_INTR_EN_0`, ack real causes, and only count `delivery_claimed` when
    a real interrupt fires from the assigned device (MSI/MSI-X preferred,
    legacy INTx fallback).
  - Acceptance evidence: `nouveau_pci_irq_provenance_matrix` shows nonzero
    real interrupt deliveries with decoded causes during bring-up; spurious
    count stays low; on no-hardware images it stays zero/fail-closed.

---

## Section 3. Real Command Submission (FIFO + Hardware Fence)

Goal: push a real command to a real engine and observe a real completion fence.

- [ ] **3.1 Create a real FIFO channel with USERD/GPFIFO.**
  - Model on nvkm `fifo`/`chan`: allocate the channel instance block, USERD,
    GPFIFO ring in VRAM/instmem, and program the channel into the host FIFO.
    Wire this under the existing `nouveau_channel_object_matrix` path in
    `fb_nouveau.c`, replacing the no-op channel handle with a real channel.
  - Acceptance evidence: channel creation reads back a valid channel/runlist
    state from hardware registers; `nouveau_channel_real=1`. Fail closed if the
    runlist submit register does not acknowledge.

- [ ] **3.2 Submit a minimal real pushbuffer and signal a semaphore/fence.**
  - Build a tiny GPFIFO entry that writes a known value to a VRAM semaphore via
    the host/copy engine, then kick it. Poll the semaphore for the value.
  - This is the keystone real-hardware proof: a value that only appears if the
    GPU executed the pushbuffer.
  - Acceptance evidence: log prints `nouveau: submit fence target=0xAA55
    observed=0xAA55 (HW)`; a new `nouveau_real_submit_matrix` in
    `gpucorevalidate` requires the observed value to equal the target and the
    completion to be a hardware semaphore write (not a CPU store). On
    no-hardware images it must report `submit=fail_closed observed=0`.

- [ ] **3.3 Wire non-empty EXEC / VM_BIND / pushbuf to the real engine.**
  - Replace the current fail-closed rejection of non-empty GEM pushbuf / EXEC /
    VM_BIND (see `nouveau_submit_failclosed_matrix`) with real GPU address-space
    mapping (GPUVM/VMM page tables) and real engine submission for the
    Mesa Nouveau winsys.
  - Acceptance evidence: `nouveau_submit_real_matrix` shows real GPUVA maps and
    a completed engine submit with a hardware fence; the old fail-closed matrix
    still rejects malformed/foreign buffers.

---

## Section 4. Real Display / Scanout on the Assigned GPU

Goal: present rendered frames through real hardware, not CPU readback.

Pick the path that matches your setup and record which one you used:

- **Path A (monitor on the passed-through GPU):** drive the GPU's own display
  engine (nvkm `disp`, CRTC, EVO/NVDisplay channel, real vblank IRQ, hardware
  page-flip). Output goes to a physical monitor attached to the card.
- **Path B (headless render, present to the VM console):** render on the real
  GPU into a VRAM BO, export it as a dma-buf, and have the compositor import it.
  The final blit to the Hyper-V synthvid console is allowed **only** as the
  display transport; the *rendering* must be on the real GPU. A pure CPU
  readback-and-memcpy with no GPU rendering earns **zero** credit.

- [ ] **4.1 Bring up the chosen display/scanout path on real hardware.**
  - Files: `fb_nouveau.c`, `fb_drm_*.c`. For Path A, satisfy the Linux-shaped
    prerequisites already tracked by `nouveau_linux_display_readiness_matrix`:
    display-engine object, `mode_config`, CRTC/encoder/primary-plane, NVIF
    head/connector masks, HPD/DP IRQ events, per-head vblank IRQ, atomic commit
    tail, and **hardware** page-flip completion — but now backed by the real
    device instead of fail-closed.
  - Acceptance evidence: `nouveau_linux_display_readiness_matrix` flips from
    `PASS_FAILCLOSED` to `PASS` with `vblank_source_native_hw=1` and
    `page_flip_events_native_hw=1` (Path A), OR a `nouveau_headless_present_matrix`
    shows a real-GPU-rendered dma-buf reaching the compositor with the render
    proven by Section 3 fences (Path B).

- [ ] **4.2 Keep vblank/page-flip correlation honest.**
  - Real hardware vblank/page-flip counters must advance from device IRQs.
    Software/emulated completion stays zero-credit (reuse
    `kms_vblank_native_present_separation_matrix`).
  - Acceptance evidence: page-flip and vblank provenance counters are nonzero
    and sourced from real IRQs in the same run that renders frames.

---

## Section 5. Mesa Nouveau / NVK OpenGL on the Real GPU

Goal: a real GL (or Vulkan→GL via Zink) frame rendered by the assigned NVIDIA
GPU through Mesa Nouveau.

- [ ] **5.1 Select and build the right Mesa userspace for the chipset.**
  - Turing+ (TU10x and newer): NVK (Vulkan) + Zink for GL, or NVK directly.
  - Pascal/Maxwell and older: classic Gallium `nouveau` GL driver.
  - Build the matching Mesa port (`ports/mesa`) against the guest libdrm/Nouveau
    UAPI exposed by Sections 2–3.
  - Acceptance evidence: `world`/ports build succeeds; the guest exposes
    `/dev/dri/renderD128` backed by the real Nouveau device (not the dumb
    framebuffer fallback).

- [ ] **5.2 Pass `nouveauabitest` and `mesaglfeature` on the real device.**
  - `nouveauabitest` must open the real Nouveau device and reach
    winsys/device-info, channel, BO, map, and PRIME paths using **real**
    chipset/VRAM/engine facts. It must **not** pass on synthetic answers.
  - `mesaglfeature` must select the Nouveau renderer, create a real context, and
    render without device removal or software fallback.
  - Acceptance evidence: `nouveauabitest` reports the real chipset and a real
    BO round-trip; `mesaglfeature` passes naming the Nouveau hardware renderer.

- [ ] **5.3 Render a Mesa Wayland client frame through the real GPU.**
  - A Mesa EGL/Wayland client (`mesawlegl`) renders a frame on the real Nouveau
    GPU and presents it through the compositor via the Section 4 path (dma-buf
    import, no DRI software readback).
  - Acceptance evidence: `mesawlegl` frame reaches the compositor with the
    render backed by Section 3 hardware fences; no `llvmpipe`/software renderer
    string appears.

---

## Section 6. Backend OpenGL-Submit Flag + 480p FPS

Goal: only now may Hyper-V advertise OpenGL submit, and only with a finite,
source-correlated FPS proof.

- [ ] **6.1 Enable `FB_GPU_BACKEND_F_OPENGL_SUBMIT` on Hyper-V — DDA/Nouveau path.**
  - File: `kernel/kernel/dev/fb/fb_drm_core_kms.c` (`gpu_backend_fill`). Add a
    Nouveau-real branch that sets the flag **only** when: real chipset id (2.1),
    real submit fence (3.2), and real Mesa render (5.2/5.3) are all true in the
    current run. Keep the DXG branch unchanged (it never sets the flag).
  - Acceptance evidence: `fbstat` reports `backend nouveau` (or
    `hyperv-nouveau-dda`), `backend_opengl_submit 1`, with an
    `opengl_submit_backend_separation_matrix` showing `nouveau_real=1`,
    `dxg_transport` irrelevant, and the flag gated on real-GPU evidence.

- [ ] **6.2 Make the finite 480p 3D demo pass on real Nouveau frames.**
  - The 640x480 demo must be visible, closeable, resizable, and sustain
    > 60 FPS after warmup, with each presented frame backed by a real-GPU render
    (Section 3 fence + Section 4 present). Reuse the anti-inflation machinery in
    `scripts/hyperv-3d-fps-validate.sh` but point the native-completion source
    at the Nouveau hardware fence instead of the DXG display-bind id.
  - Acceptance evidence: `hyperv-3d-fps-validate.sh` passes with
    `render=640x480 render_div=1`, sustained content-frame FPS > 60 in both the
    sample and visual windows, and native completion sourced from Nouveau HW.
    The 40-FPS / inflated / frozen-window negatives still fail.

- [ ] **6.3 Re-check the KVM/virgl control backend is unchanged.**
  - `scripts/gpu-validate.sh` on KVM still reports `backend virgl`,
    `backend_opengl_submit 1`. The new Nouveau branch must not regress it.

---

## Section 7. WebKit Consumer on the Same Contract

Goal: WebKit acceleration consumes the exact real-Nouveau contract.

- [ ] **7.1 Route WebKitGTK through the real Nouveau render-node + present path.**
  - WebKit uses the same `/dev/dri/renderD128` (real Nouveau), the same dma-buf
    present path, and the same backend flag as Mesa clients. It stays gated off
    until Sections 5–6 pass.
  - Acceptance evidence: `hyperv-webkit-gpu-validate.sh` keeps `effective_accel=0`
    until real-Nouveau native present + finite FPS + backend flag are all true,
    then emits one enabled artifact tied to that lineage.

- [ ] **7.2 Animated WebKit fixture correlated with real-GPU frames.**
  - Correlate the animated fixture's content CRC/frame-hash progress with real
    Nouveau hardware-fenced present completions for the same client/resource.
  - Acceptance evidence: `webkit_animated_content_native_present_gate_matrix`
    opens only with real-GPU-backed content progress; title-only/chrome-only
    evidence stays zero-credit.

---

## Validation Rhythm

For each section: (1) implement the whole section or a bounded subsection,
(2) build-only check, (3) run the focused pure-C validator
(`gpucorevalidate` / `nouveauabitest` / `drmiftest`), (4) run heavy GUI/FPS/
WebKit validation only after the section is code-complete, (5) mark `[x]` only
when source, real-hardware runtime evidence, and the negative (fail-closed)
case all agree.

## Acceptance Gate (the whole plan is done only when ALL are true on real DDA hardware)

- The guest enumerates the real assigned NVIDIA device on Hyper-V vPCI and reads
  a real `PMC_BOOT_0` chipset id.
- Nouveau sizes real VRAM, loads real firmware, and completes a real command
  submission observed via a real hardware fence.
- A frame is rendered by the real NVIDIA GPU (Mesa Nouveau / NVK) and presented
  without CPU-readback-as-render.
- `fbstat` honestly reports `backend_opengl_submit 1` on Hyper-V, gated on the
  real-GPU evidence above.
- The 480p 3D demo is visible, closeable, resizable, and sustains > 60 FPS after
  warmup on real Nouveau frames.
- WebKit acceleration uses the same real-Nouveau contract and stays gated off
  when the real GPU is unavailable.
- Every fail-closed negative test still rejects synthetic/emulated/readback
  evidence with zero credit.
