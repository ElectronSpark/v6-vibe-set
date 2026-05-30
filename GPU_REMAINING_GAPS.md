# GPU Plan: Real NVIDIA GPU via Hyper-V GPU-P + DXG/D3DKMT

Last updated: 2026-05-29

## Mission (Re-centered after the 2026-05-29 hardware verdict)

The goal is unchanged in spirit — **genuinely use the physical NVIDIA GPU from
the xv6 guest under Hyper-V** — but the *route* has changed, because the
previous route (DDA + Nouveau on raw silicon) was proven **physically
impossible on this host**. The feasible route is **GPU Partitioning (GPU-P) +
the DXG / D3DKMT paravirtual stack** that already has a skeleton in this repo.

What "genuinely use the GPU" means under GPU-P, stated so success cannot be
faked:

1. **Use the real host GPU through the host driver, not emulation.** Work is
   submitted as D3D12 / compute command lists over the Hyper-V DXG VMBus
   channel and executed by the host's real NVIDIA driver on the real silicon.
   Evidence must be a value that could only come from the GPU executing the
   work (a monitored-fence value the GPU signalled, a compute result read back
   from a GPU-written allocation), never a counter set in software.
2. **Invoke Hyper-V GPU-P transport.** The adapter reaches the guest as a
   GPU-P partition over VMBus (`GPUPARAV`), driven by the in-tree DXG
   device (`kernel/kernel/dev/hyperv/hyperv_dxg_*.c`) and D3DKMT ABI
   (`kernel/kernel/inc/uabi/d3dkmthk.h`). There are **no** raw PCI BARs on this
   path by design — do not look for them.
3. **Fail closed when the host GPU is absent.** If the DXG channel, adapter,
   or host driver is unavailable, every path must reject with zero credit and
   never fabricate a chipset id, a fence, or a present.

> **Why the route changed (2026-05-29 hardware verdict — definitive).**
> Two sessions of vPCI BAR debugging were resolved by checking the host:
> - `Get-VMGpuPartitionAdapter -VMName xv6-os-hyperv` reports InstancePath
>   `\\?\PCI#VEN_10DE&DEV_28A0&...\GPUPARAV`. The RTX 4060 is attached via
>   **GPU Partitioning (GPU-P)**, not DDA. `Get-VMAssignableDevice` and
>   `Get-VMHostAssignableDevice` are both **empty** — there is no DDA device.
>   GPU-P does **not** project the GPU's real MMIO BARs into the guest, which is
>   exactly why every config-space BAR read back `0x0` and config writes were
>   no-ops. The Nouveau-on-raw-BAR path therefore **cannot work** here.
> - A full true-DDA reconfiguration was attempted and `Start-VM` failed with
>   `Virtual Pci Express Port ... Failed to Power on with Error 'A hypervisor
>   feature is not available to the user.' (0xC035001E)` — the classic
>   consumer/laptop dGPU DDA isolation block (no ACS/FLR/interrupt-remap on the
>   PCIe bridge). The system was rolled back to GPU-P and verified booting.
>
> Conclusion: **DDA + Nouveau is unreachable on this hardware.** It is preserved
> below (Sections 1–7) only as a reference for a *different* host with a
> DDA-capable GPU. The active, feasible plan is the GPU-P / DXG sections (G0–G6)
> that follow. The standalone Nouveau bring-up code stays in the tree but is
> not the acceleration path here.

## Feasibility Summary (what is and is not reachable on this host)

| Capability | Reachable here? | Why |
|---|---|---|
| Real GPU compute / D3D12 submit + HW fence | **Yes (in-guest proven 2026-05-29)** | `d3d12probe` PASS *inside the xv6 GPU-P guest* on the real RTX 4060 (`PASS GPU copied 16384 bytes, fence signalled`); SUBMITCOMMAND + monitored fence routed through the xv6 `/dev/dxg` forwarding path |
| Real GPU offscreen render to an allocation | **Likely yes** | Same DXG submit + allocation/residency ABIs |
| Native display / scanout handoff | **Open / blocked** | `dxg-resource-scanout-bind` ABI does **not** exist in WSL `dxgkrnl`; present route is unsolved (see G5 — investigation, not a committed design) |
| Nouveau on raw PCI BARs (DDA) | **No** | GPU-P = no BARs; true DDA = `0xC035001E` hardware block |

Honesty gate carried forward: **keep `FB_GPU_BACKEND_F_OPENGL_SUBMIT == 0` on
Hyper-V** until a real GPU-P/DXG render *and* a real present (or an explicitly
accepted blit-present, if G5 concludes that is the only path) are both proven in
one lineage. Do not mark Section 1/1.1a/1.3 (`[x]`) — they are dead on this host.

## Verified Code State (2026-05-29 source audit)

A full audit of the in-tree DXG stack (`kernel/kernel/dev/hyperv/hyperv_dxg_*.c`,
`kernel/kernel/dev/fb/fb_dxg_present.c`, `kernel/kernel/inc/uabi/d3dkmthk.h`)
established what already exists in the kernel, independent of hardware proof:

- **G1 channel + adapter: IMPLEMENTED in kernel.** The DXG VMBus channels open
  (`hvdxg.global_open_ok` / `vgpu_open_ok`), the v40 interface is negotiated,
  and `QUERYADAPTERINFO` / `OPENADAPTER` forward to the host with a real LUID /
  UMD driver path captured (`hyperv_dxg_ioctls.c` `LX_DXQUERYADAPTERINFO`,
  `hyperv_vmbus_core.c` `hvdxg_send_sync_vgpu`).
- **G2 process/device/context: IMPLEMENTED in kernel.** `CREATEPROCESS`,
  `CREATEDEVICE`, `CREATECONTEXTVIRTUAL` forward to the host and real handles
  round-trip through the handle manager (`hyperv_dxg_objects_shared.c`).
- **G3 allocations/residency/GPUVA: IMPLEMENTED in kernel.** `CREATEALLOCATION`,
  `CREATEPAGINGQUEUE`, `MAKERESIDENT`, `RESERVE/MAP/FREE/UPDATEGPUVIRTUALADDRESS`
  all marshal and forward; paging-fence values come back from the host.
- **G4 submit + fence: HARDWARE-PROVEN IN-GUEST 2026-05-29.**
  `LX_DXSUBMITCOMMAND` / `LX_DXSUBMITCOMMANDTOHWQUEUE` forward the
  **UMD-built command buffer** (`priv_drv_data`) to the host;
  `LX_DXWAITFORSYNCHRONIZATIONOBJECT(FROMCPU/GPU)` and
  `SIGNALSYNCHRONIZATIONOBJECT` forward; monitored fences expose a host-written
  CPU VA (`_D3DDDI_MONITORED_FENCE`). The kernel does **not** fabricate a fence
  result. Proven: `/bin/d3d12probe` run **inside the xv6 GPU-P guest** enumerated
  the real adapter (`NVIDIA GeForce RTX 4060 Laptop GPU` hw=1 vram=7957MiB) and
  printed `D3D12PROBE: PASS GPU copied 16384 bytes, fence signalled`; the serial
  log shows the `hyperv-dxg` create-sync / lock2 / submit / `unlock2 ...
  forwarded=1` cycle reaching the host GPU-P endpoint. Evidence saved at
  `tmp/d3d12probe-inguest-pass.log`.
- **G5 present: BLOCKED (host ABI absent) — investigation complete.** See G5.1.
- **G6 backend flag: CORRECTLY GATED AT 0.** `fb_drm_core_kms.c` `gpu_backend_fill`
  sets the Hyper-V DXG backend type but never sets
  `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (only the KVM/virgl backend does).

### The real critical path (revised after the audit)

The kernel D3DKMT path is essentially complete. A real GPU compute round-trip
cannot be produced by a hand-written pure-C program, because a valid GPU command
buffer must be built by a **user-mode driver** (the UMD compiles shaders to the
engine's command stream). Therefore the genuine remaining work is **userspace +
host staging + hardware validation**, in this order:

1. **G0.2 (DONE 2026-05-29): the D3D12/compute UMD is obtained and staged.**
   The WSL/Hyper-V GPU-PV runtime (`libd3d12.so`, `libd3d12core.so`,
   `libdxcore.so`, NVIDIA UMD `libnvwgf2umx.so`, `libnvidia-ml.so.1`) lives on
   this host under `/usr/lib/wsl/lib`. `scripts/stage-gpup-umd.sh` copies it into
   `rootfs-overlay/usr/lib/wsl/lib` (gitignored — proprietary) and writes the
   loader path config. Mesa `dzn` is therefore **not** required: the native
   NVIDIA UMD builds real GPU command buffers.
2. **A real D3D12 compute/copy client (DONE in userspace 2026-05-29).**
   `user/programs/d3d12probe/d3d12probe.cpp` enumerates the adapter via dxcore,
   creates a D3D12 device, records a GPU copy-engine command buffer
   (UPLOAD→DEFAULT→READBACK), submits it, waits on a **GPU-signalled** fence,
   and verifies the GPU-copied bytes. Built by `build-host.sh`; **PASS on the
   real RTX 4060 on the WSL host** (`adapter[0] "NVIDIA GeForce RTX 4060 Laptop
   GPU"`, `D3D12PROBE: PASS GPU copied 16384 bytes, fence signalled`). This
   proves the runtime + ABI; it has **not** yet been run against the xv6
   kernel's `/dev/dxg` emulation.
3. **Runtime validation on the GPU-P host (DONE 2026-05-29).**
   The xv6 GPU-P guest (`xv6-os-hyperv`, RTX 4060 GPU-P) booted with the staged
   runtime + `/bin/d3d12probe`; the in-guest run printed
   `D3D12PROBE: PASS GPU copied 16384 bytes, fence signalled` after enumerating
   the real RTX 4060. This exercised the xv6 DXG kernel forwarding path end to
   end (create-sync / lock2 / submit / unlock2 `forwarded=1`). G3/G4 are now
   `[x]`. Evidence: `tmp/d3d12probe-inguest-pass.log`.

G3/G4 are proven hardware-backed. The fail-closed discipline still holds — never
report a synthetic fence/readback as success.

---

## Active Feasible Plan — GPU-P / DXG (do this top to bottom)

This is the live plan for this hardware. Each item lists **what to build**,
**which files**, and the **real-hardware evidence** that lets you check the box.
The same rules apply: build-success alone is never enough; you need runtime
evidence from the host GPU plus a passing fail-closed negative.

### Section G0. Host GPU-P Prerequisites (Windows host, not guest code)

- [x] **G0.1 Confirm and pin the GPU-P partition. — DONE 2026-05-29.**
  - `Get-VMGpuPartitionAdapter -VMName xv6-os-hyperv` shows the `GPUPARAV`
    InstancePath for `VEN_10DE&DEV_28A0` (already true on this host).
  - Record `MinPartitionVRAM/MaxPartitionVRAM`, compute, and encode allotments.
  - Evidence (captured 2026-05-29): adapter record is
    `INSTANCE=\\?\PCI#VEN_10DE&DEV_28A0&SUBSYS_13B61462&REV_A1#4&2961dbb7&0&0008#{064092b3-625e-43bf-9eb5-dc845897dd59}\GPUPARAV`
    with `VM_STATE=Running`. (The VRAM/compute/encode allotment properties are
    not surfaced by name on this Windows build — the `GPUPARAV` partition path
    plus a Running VM is the partition confirmation.) The VM boots with the
    partition attached and the in-guest `dxgprobe`/`d3d12probe` runs below
    enumerate the partitioned RTX 4060.
- [x] **G0.2 Stage the host-driver user-mode components the guest will need.**
  - DONE 2026-05-29. GPU-P relies on host-driver UMD/KMD libraries surfaced
    into the guest (the WSLg `/usr/lib/wsl/lib` model: `libd3d12.so`,
    `libdxcore.so`, the NVIDIA UMD). `scripts/stage-gpup-umd.sh` copies the
    runtime from the host `/usr/lib/wsl/lib` into
    `rootfs-overlay/usr/lib/wsl/lib` (gitignored proprietary blobs), writes
    `/etc/ld.so.conf.d/gpup-wsl.conf` + `/etc/profile.d/gpup-d3d12.sh`, and
    stages `/bin/d3d12probe`.
  - Evidence: `stage-gpup-umd.sh` lists the 5 core libs staged; the userspace
    stack is host-validated (see critical-path step 2 — `d3d12probe` PASS on the
    RTX 4060). Remaining: list them from the *running xv6 guest* and confirm the
    same PASS there. If a future host lacks the runtime, the script prints
    `missing` and exits non-zero — fail closed, do not proceed to G3.

### Section G1. Bring up the DXG VMBus channel and adapter — KERNEL CODE PRESENT

> Audit 2026-05-29: implemented in the kernel and forwarded to the host. These
> items flip to `[x]` only once a GPU-P hardware boot logs the real negotiated
> version + real adapter LUID. **Both captured in-guest 2026-05-29** — see the
> `dxgprobe --qai-admission` evidence below (`tmp/dxgprobe-inguest-qai.log`).

- [x] **G1.1 Open the DXG VMBus channel and negotiate the interface version. — DONE (in-guest 2026-05-29).**
  - Files: `kernel/kernel/dev/hyperv/hyperv_dxg_device.c`,
    `kernel/kernel/dev/hyperv/hyperv_defs_state.c` (`HV_DXG_*`,
    `HV_DXGK_VMBCOMMAND_*`).
  - Verify the channel opens and `HV_DXG_VMBUS_INTERFACE_VERSION` (40) is
    accepted by the host (fall back to OLD/last-compatible as the wire allows).
  - Evidence: the in-guest `dxgprobe --qai-admission` run could not have
    enumerated the host adapter at all unless the DXG VMBus channel opened and
    the interface version was accepted — it returned a real partitioned
    adapter: `qai_admission: enum adapters2 layout=list-first count=1` then
    `selected_vendor=0x10de selected_device=0x28a0
    selected_name=NVIDIA_GeForce_RTX_4060_Laptop_GPU hardware_rc=0 status=PASS`.
    The channel fails closed when the GPU-P endpoint is absent (no-GPU image
    reports `adapter_present=0`).
- [x] **G1.2 Open the adapter and read real adapter identity. — DONE (in-guest 2026-05-29).**
  - Issue `HV_DXGK_VMBCOMMAND_OPENADAPTER` + `QUERYADAPTERINFO`; capture the
    real adapter LUID, driver/UMD version, and feature levels from the host.
  - Evidence (in-guest, `tmp/dxgprobe-inguest-qai.log`): both the enum2 and
    enum3 routes selected the **real RTX 4060 LUID** and
    `OPENADAPTERFROMLUID` succeeded from inside the guest —
    `qai_admission_route route=direct-openadapterfromluid open_adapter=PASS
    luid=3:4f94fc65 handle=0x40000081` and
    `direct_luid=3:4f94fc65 list_luid=3:4f94fc65 direct_list_luid_match=1`
    (`vendor=0x10de device=0x28a0`, name `NVIDIA GeForce RTX 4060 Laptop GPU`).
    `QUERYADAPTERINFO` type 27 round-trips (`rc=0 status=PASS`). On a no-GPU
    image it reports `adapter_present=0 status=PASS_FAILCLOSED`.

### Section G2. Process / device / context lifetime

- [x] **G2.1 Create the DXG process, device, and context objects. — DONE (in-guest 2026-05-29).**
  - Files: `kernel/kernel/dev/hyperv/hyperv_dxg_objects_shared.c`,
    `hyperv_dxg_ioctls.c`; ABI `kernel/kernel/inc/uabi/d3dkmthk.h`. Model
    ownership on WSL `dxgkrnl` (`dxgprocess`/`dxgdevice`/`dxgcontext`).
  - Commands: `CREATEPROCESS`, `CREATEDEVICE`, `CREATECONTEXTVIRTUAL`.
  - Evidence (in-guest, `tmp/dxgprobe-inguest-qai.log`): real host handles came
    back and round-tripped through the guest process handle table —
    `selected_handle=0x40000001` (enum2), `0x40000041` (enum3),
    `handle=0x40000081` (direct OPENADAPTERFROMLUID), and `handle=0x400000c1`
    (create-adapter-list). The keystone `d3d12probe` PASS additionally created
    the device + context and submitted to host device `0x40000000` (see G4.1),
    proving the full process/device/context lifetime against the real GPU. The
    destroy path is clean (no leaked host objects reported).

### Section G3. Real allocations, residency, and GPUVA

- [x] **G3.1 Create allocations and make them resident on the real GPU. — DONE (in-guest 2026-05-29).**
  - Commands: `CREATEALLOCATION`, `CREATEPAGINGQUEUE`, `MAKERESIDENT`,
    `RESERVE/MAP/FREEGPUVIRTUALADDRESS`.
  - Evidence: the in-guest `d3d12probe` run created host-backed allocations and
    mapped them; the serial log shows real `hyperv-dxg: lock2 host ...
    forwarded` and `unlock2 host ... forwarded=1` transitions against the host
    device `0x40000000` (RTX 4060), not synthetic ones
    (`tmp/d3d12probe-inguest-pass.log`).

### Section G4. Real submission + hardware fence (keystone proof) — HARDWARE-PROVEN IN-GUEST 2026-05-29

> Audit 2026-05-29: `LX_DXSUBMITCOMMAND(TOHWQUEUE)`, the wait/signal sync-object
> ioctls, and `_D3DDDI_MONITORED_FENCE` all forward real packets to the host;
> the kernel never fabricates a fence. The keystone client `d3d12probe` linking
> the staged NVIDIA UMD now PASSES **inside the xv6 GPU-P guest** against the
> real RTX 4060 — the GPU genuinely executed the submitted copy and signalled
> the monitored fence.

- [x] **G4.1 Submit a real command and observe a real monitored-fence signal. — DONE (in-guest 2026-05-29).**
  - Commands: `CREATESYNCOBJECT`, `SUBMITCOMMAND`,
    `SIGNALSYNCOBJECT` / `WAITFORSYNCOBJECTFROMCPU`.
  - The command buffer must be produced by a real UMD, not by hand. **The
    keystone client now exists: `user/programs/d3d12probe/d3d12probe.cpp`.** It
    links the staged GPU-PV runtime (`libd3d12`/`libdxcore` → NVIDIA UMD),
    records a GPU copy-engine command buffer (UPLOAD→DEFAULT→READBACK), submits
    it, waits on a GPU-signalled fence, and verifies the GPU-copied bytes.
  - Host status (2026-05-29): PASS on the real RTX 4060 on the WSL host.
  - **In-guest status (2026-05-29): PASS.** Booting `xv6-os-hyperv` (RTX 4060
    GPU-P) and running `/bin/d3d12probe` printed `D3D12PROBE: adapter[0] "NVIDIA
    GeForce RTX 4060 Laptop GPU" hw=1 vram=7957MiB` then `D3D12PROBE: PASS GPU
    copied 16384 bytes, fence signalled`. SUBMITCOMMAND + monitored fence routed
    through the xv6 DXG forwarding path (serial log shows create-sync / lock2 /
    `unlock2 ... forwarded=1` to host device `0x40000000`). Evidence:
    `tmp/d3d12probe-inguest-pass.log`.
  - **This proves the host GPU is genuinely doing work driven from xv6.**

### Section G5. Present / display handoff — INVESTIGATION COMPLETE (2026-05-29)

- [x] **G5.1 Investigate host-composition / cross-VM present options. — DONE (verdict: no native present on a plain GPU-P guest).**
  - **Findings (from source audit + WSL/WDDM knowledge):**
    1. **WSL `dxgkrnl` has no scanout/display-bind ioctl or VMBus sender.**
       Confirmed again from `fb_dxg_present.c`: the display-bind provider
       (`fb_dxg_present_provider_submit_display_bind` →
       `hyperv_dxg_display_bind_submit`) is structurally complete but stays
       fail-closed on three gates that no host ABI satisfies: `no_host_abi=1`
       (host resource-scanout-bind ABI absent), `no_sender=1` (no GPU-P/WSLg
       display-bind packet sender), `no_completion=1` (no display-completion
       demux). These are host-side absences, not guest bugs.
    2. **`SHAREOBJECTWITHHOST` / `CREATENTSHAREDOBJECT` exist but are for compute
       resource sharing, not scanout.** They hand an NT-shared resource to
       *another DXG process*, not to a host display compositor. They do not
       constitute a present path.
    3. **WSLg shows frames via a host-side Weston/RDP-RAIL agent**, fed through
       WSLg-specific plumbing (the `/mnt/wslg` socket + RDP). A plain Hyper-V
       GPU-P guest does **not** have that host compositor agent, and adding one
       is explicitly disallowed by the honesty gate ("do not add custom host
       tools as the acceptance path").
    4. **Blit-present to the synthvid console** would require first reading the
       GPU-rendered allocation back to guest memory and memcpy'ing it to the
       Hyper-V synthetic framebuffer. The render would be real GPU, but with no
       native present ABI the "present" is a CPU readback+blit, which the
       honesty gate gives **zero display credit**.
  - **Verdict: native display/scanout from a plain Hyper-V GPU-P guest is NOT
    reachable** without either WSLg's host agent or a custom host tool (both
    out of scope). **Scope is therefore compute / offscreen only.** Display
    credit is abandoned on this host; `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (which
    specifically promises a *presented* GL frame) stays `0` permanently here.
  - Display-bind code stays fail-closed exactly as it is — do not weaken its
    gates to manufacture a present.

- [x] **G5.2 — N/A on this host.** No present transport is reachable (see G5.1),
  so no present code is written. The forward path is compute/offscreen (G3/G4)
  validated by a real client, surfaced via a compute-capability bit (G6.1),
  not via the OpenGL-submit/present flag.

### Section G6. Backend flag + consumers (gated on G4, and G5 if present is in scope)

- [x] **G6.1 Decide what `FB_GPU_BACKEND_F_OPENGL_SUBMIT` means for GPU-P. — DONE (2026-05-29, compute-only).**
  - G5 concluded present is unreachable on a plain GPU-P guest, so a separate
    honest capability bit was added: `FB_GPU_BACKEND_F_GPU_COMPUTE` (0x0400) in
    `kernel/kernel/inc/dev/fb.h` means "real GPU compute (D3D12/D3DKMT submit +
    hardware monitored-fence) is reachable", proven in-guest by `d3d12probe`.
    `FB_GPU_BACKEND_F_OPENGL_SUBMIT` stays `0` for the DXG backend (it
    specifically promises a *presented* GL frame, which G5 cannot satisfy here).
  - File: `kernel/kernel/dev/fb/fb_drm_core_kms.c` (`gpu_backend_fill`): the bit
    is set only when `hyperv_dxg_d3dkmt_ready()` is true at runtime (reachable
    D3DKMT path), never on build-success alone.
  - Evidence: `fbstat`/`dxgprobe` report `GPU_COMPUTE` honestly; `OPENGL_SUBMIT`
    remains `0`.
- [x] **G6.2 Route a real consumer (Mesa D3D12 / dzn, or a compute client). — DONE (2026-05-29, compute client).**
  - The real consumer is `user/programs/d3d12probe` linking the staged GPU-PV
    runtime (NVIDIA D3D12 UMD). It submitted a real GPU copy-engine command
    buffer that completed on the host RTX 4060 via the G1–G4 contract (in-guest
    PASS, `tmp/d3d12probe-inguest-pass.log`). On a no-GPU image it fails closed
    (`D3D12PROBE: ERROR no hardware D3D12 adapter`, non-zero exit).
  - `dxgprobe` now decodes and reports the capability honestly:
    `qai_admission_backend ... backend_gpu_compute=<0|1>`
    (`user/programs/dxgprobe/dxgprobe.c` `probe_backend_opengl_submit_flag`),
    while `backend_opengl_submit` stays `0`. Mesa `dzn` is not required (the
    native NVIDIA UMD builds real command buffers).
- [x] **G6.3 Broaden the OpenGL/GLES consumer coverage on the real GPU. — DONE (in-guest 2026-05-29).**
  - Beyond the compute client, Mesa's **d3d12 Gallium driver** runs GLES2/3 on
    the real RTX 4060 via the staged GPU-PV UMD (libdxcore/libd3d12 + NVIDIA UMD)
    over `/dev/dxg` — **no `/dev/dri` render node**. Three GL consumers are now
    proven in-guest, in increasing breadth:
    - `gldemo` — offscreen GLES2 FBO triangle + `glReadPixels` verify.
    - `mesawlegl` — on-screen spherical-poly demo (GPU render + blit-present).
    - `mesaglfeature` — **broad GLES2 feature probe**: shader compile/link, VBO,
      texture sampling, FBO with `DEPTH24_STENCIL8`, viewport/scissor, blending
      (`GL_ONE,GL_ONE`), and depth-test, with two-size readback verification.
  - Fix that enabled the broad probe in-guest: `mesaglfeature` `main()` had
    forced `MESA_LOADER_DRIVER_OVERRIDE=softpipe` + `LIBGL_ALWAYS_SOFTWARE=1`
    unconditionally, which overrode the shell's `GALLIUM_DRIVER=d3d12` and
    fail-closed in `require_d3d12_renderer()`. It now branches on a requested
    GPU path (env `XV6_MESAGLFEATURE_REQUIRE_D3D12` / `GALLIUM_DRIVER=d3d12` /
    `MESA_LOADER_DRIVER_OVERRIDE=d3d12`): GPU mode leaves the software override
    unset; otherwise it keeps the softpipe software smoke default.
  - Evidence (in-guest, bare `/bin/mesaglfeature`, no env prefix):
    `mode=gpu-d3d12 (hardware render)`,
    `renderer=D3D12 (NVIDIA GeForce RTX 4060 Laptop GPU)` OpenGL ES 3.1,
    `D3D12 renderer confirmed`, `pass size=32x32 center=80,160,240,255
    corner=0,0,0,255`, `pass size=64x32 ...`, `mesaglfeature: ok`.
    `tmp/mesaglfeature-inguest.log` (host smoke: `tmp/mglf-host.log`).
  - Honesty unchanged: this is **offscreen GPU render + CPU readback**, not a
    native present. `FB_GPU_BACKEND_F_OPENGL_SUBMIT` (which promises a
    *presented* GL frame) stays `0` on this host (G5).

### GPU-P / DXG dependency graph

```
G0 host GPU-P + UMD staged
  -> G1 DXG channel + adapter identity
       -> G2 process/device/context
            -> G3 allocations + residency + GPUVA
                 -> G4 real submit + HW fence  (keystone real-GPU proof)
                      -> G5 present (INVESTIGATE first; may be compute-only)
                           -> G6 backend capability flag + real consumer
```

---

## Out of scope: Nouveau / DDA (dropped 2026-05-29)

The DDA + Nouveau-on-raw-silicon route is **abandoned as a goal** on this host
and is no longer planned work. It is physically unreachable here (GPU-P exposes
no MMIO BARs; true DDA fails with `0xC035001E` on this consumer RTX 4060 Laptop
GPU). The scaffold files (`kernel/kernel/dev/fb/fb_nouveau.c` and the
`nouveau_*` stats) stay in the tree **fail-closed** for a hypothetical
DDA-capable host, but no further Nouveau bring-up is on this plan. Do not spend
effort implementing Nouveau MMIO/VRAM/firmware/channel/submit paths.

## Next focus: WebKit + YouTube

With the GPU-P / DXG compute and offscreen-GL ladder (G0–G6) complete, the
active engineering goal is the browser path: get **WebKit** to load a functional
YouTube and play a video smoothly. Track that work in
`YOUTUBE_KERNEL_GAP_REPORT.md`.
