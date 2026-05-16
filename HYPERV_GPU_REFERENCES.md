# Hyper-V GPU Reference Map

This file records upstream examples and docs that are directly relevant to the
xv6 Hyper-V GPU-PV/DXG work. Use these as implementation references, not as a
claim that Hyper-V OpenGL submit is complete.

## Architecture References

- Microsoft DirectX blog: DirectX on Linux/WSL
  - URL: https://devblogs.microsoft.com/directx/directx-heart-linux/
  - Why it matters: describes the intended stack: `/dev/dxg` exposes WDDM-like
    D3DKMT ioctls, `dxgkrnl` talks to the Windows host over VM bus, `libdxcore.so`
    hosts the flat D3DKMT API, and `libd3d12.so` is the Linux build of the D3D12
    runtime.
  - xv6 mapping: `kernel/kernel/dev/hyperv_input.c` is our dxgkrnl-like bridge;
    `ports/mesa` stages Mesa D3D12 plus the WSL `libdxcore.so`/`libd3d12.so`
    runtime for bring-up.

- Microsoft GPU paravirtualization driver docs
  - URL: https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-paravirtualization
  - Why it matters: documents the guest/host split: no guest KMD/VidMm/VidSch,
    D3D runtimes and KMT interfaces stay the same, guest dxgkrnl marshals thunk
    calls over VM bus, and VM bus messages are limited to 128 KiB.
  - xv6 mapping: keep `/dev/dxg` as a D3DKMT-compatible transport and do not
    advertise `FB_GPU_BACKEND_F_OPENGL_SUBMIT` until runtime-level D3D12/Mesa
    context and submit validation works.

## Kernel / DXG Transport References

- WSL2 Linux kernel dxgkrnl VM bus bridge
  - URL: https://github.com/microsoft/WSL2-Linux-Kernel/blob/linux-msft-wsl-6.6.y/drivers/hv/dxgkrnl/dxgvmbus.c
  - Relevant functions:
    - `dxgvmb_send_create_context`: builds `DXGK_VMBCOMMAND_CREATECONTEXTVIRTUAL`
      from device, node, engine affinity, flags, client hint, and optional
      private driver data.
    - `dxgvmb_send_create_allocation`: computes command/result sizes from global
      and per-allocation private data and forwards allocation creation.
    - `dxgvmb_send_submit_command`: forwards `D3DKMTSubmitCommand`, including
      history buffers and private driver data.
    - `dxgvmb_send_submit_command_hwqueue`: forwards HW queue submits with
      primaries and private data.
    - `dxgvmb_send_query_adapter_info`: forwards `D3DKMTQueryAdapterInfo` and
      adjusts adapter type flags for paravirtualized adapters.
  - xv6 mapping: compare this against the matching cases in
    `kernel/kernel/dev/hyperv_input.c`. Important divergence: upstream uses
    `DXG_MAX_VM_BUS_PACKET_SIZE = 128 * 1024`. `LX_DXQUERYADAPTERINFO` now
    follows that dynamic-buffer shape; remaining paths should be checked the
    same way as they become runtime blockers.

- WSL2 Linux kernel dxgkrnl ioctl entry points
  - URL: https://github.com/microsoft/WSL2-Linux-Kernel/blob/linux-msft-wsl-6.6.y/drivers/hv/dxgkrnl/ioctl.c
  - Relevant functions:
    - `dxgkio_query_adapter_info`
    - `dxgkio_create_context_virtual`
    - allocation creation path that calls `dxgvmb_send_create_allocation`
    - `dxgkio_submit_command`
    - `dxgkio_submit_command_to_hwqueue`
  - xv6 mapping: these show the expected validation/ownership lookup shape
    before forwarding to the host. Our per-open ownership tracking should keep
    mirroring this model where practical.

- WSL2 Linux D3DKMT UAPI header
  - URL: https://github.com/microsoft/WSL2-Linux-Kernel/blob/linux-msft-wsl-6.6.y/include/uapi/misc/d3dkmthk.h
  - Why it matters: source of Linux-side structure layout for D3DKMT ioctls.
  - xv6 mapping: `kernel/kernel/inc/uabi/d3dkmthk.h` should stay ABI-compatible
    with this where WSL exposes equivalent ioctls. It intentionally does not
    include every newer Windows `KMTQAITYPE`; use Microsoft DDI docs when WSL
    forwards a private type that the header does not name.

## QueryAdapterInfo / Registry References

- Microsoft `D3DDDI_QUERYREGISTRY_INFO` docs
  - URL: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_queryregistry_info
  - Why it matters: defines the private data layout used when a UMD queries
    registry-like adapter data through `QueryAdapterInfo`, including
    `OutputValueSize`, `Status`, and the buffer-overflow retry contract.
  - xv6 mapping: our current `KMTQAITYPE_QUERYREGISTRY` support in
    `hyperv_input.c` should follow this contract. The current D3D12 probe has
    shown `DXCoreAttributes` as a required adapter-key `REG_MULTI_SZ` query.

- Microsoft `KMTQUERYADAPTERINFOTYPE` / `D3DKMT_PHYSICAL_ADAPTER_COUNT` docs
  - URL: https://learn.microsoft.com/windows-hardware/drivers/ddi/d3dkmthk/ne-d3dkmthk-_kmtqueryadapterinfotype
  - URL: https://learn.microsoft.com/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_physical_adapter_count
  - Why it matters: `libdxcore.so` queries newer Windows adapter-info types
    that WSL's Linux UAPI header does not name. In the current probe sequence,
    type 30 is `KMTQAITYPE_PHYSICALADAPTERCOUNT`; returning host data directly
    produced a zero physical-adapter count even though D3DKMT enumeration
    exposed one vGPU adapter.
  - xv6 mapping: `hyperv_input.c` now shims physical-adapter count to one,
    matching the single GPU-PV adapter that `/dev/dxg` exposes to the guest.
    This is still not enough for DXCore to advertise a D3D12 adapter while the
    host partition reports `CurrentPartitionCompute : 0`.

- Host driver INF examples for `DXCoreAttributes`
  - Local examples:
    - `/mnt/c/Windows/INF/oem469.inf`
    - `/mnt/c/Windows/System32/DriverStore/FileRepository/nvmi.inf_amd64_9a9d1548c06ce277/nvmi.inf`
  - Relevant value: `DXCoreAttributes` is a `REG_MULTI_SZ` of adapter attribute
    GUIDs including D3D12 graphics/compute/media-style capabilities.
  - xv6 mapping: these values are useful for the bring-up shim, but a durable
    implementation should prefer forwarding host registry queries when possible
    or explicitly emulate only values required by `libdxcore.so`.

## Mesa / OpenGL-on-D3D12 References

- Mesa D3D12 driver docs
  - URL: https://docs.mesa3d.org/drivers/d3d12.html
  - Why it matters: Mesa's D3D12 Gallium driver emits D3D12 API calls and can
    provide desktop OpenGL 3.3 on top of D3D12-capable systems.
  - xv6 mapping: `ports/mesa/CMakeLists.txt` enables Gallium `d3d12`; the probe
    wrapper `ports/wayland/src/mesad3d12probe.c` forces `GALLIUM_DRIVER=d3d12`
    and `D3D12_DEBUG=verbose` for Hyper-V bring-up.

- Mesa D3D12 DXCore screen creation
  - URL: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/drivers/d3d12/d3d12_dxcore_screen.cpp
  - Relevant functions:
    - `get_dxcore_factory`: loads `libdxcore.so` and gets
      `DXCoreCreateAdapterFactory`.
    - `choose_dxcore_adapter`: creates a DXCore adapter list for the D3D12
      graphics attribute and optionally filters by
      `MESA_D3D12_DEFAULT_ADAPTER_NAME`.
    - `d3d12_init_dxcore_screen`: reads DXCore adapter properties, then calls
      `d3d12_init_screen`.
  - xv6 mapping: if `mesad3d12probe` still prints `screen=(nil)`, inspect which
    DXCore adapter-list/property query failed before `d3d12_init_screen`.

- Mesa D3D12 device and command queue creation
  - URL: https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/gallium/drivers/d3d12/d3d12_screen.cpp
  - Relevant functions:
    - `create_device`: calls `ID3D12DeviceFactory::CreateDevice` or
      `D3D12CreateDevice`.
    - `try_create_device_factory`: loads `D3D12GetInterface` and tries the
      D3D12 device factory path.
    - `d3d12_init_screen`: creates the D3D12 device and command queue.
  - xv6 mapping: once DXCore adapter enumeration works, failures here will map
    to D3DKMT device/context/HW queue/submit behavior visible through `/dev/dxg`.

- Microsoft DirectX-Headers
  - URL: https://github.com/microsoft/DirectX-Headers
  - Why it matters: open-source D3D12/DXCore headers used by Linux/WSL builds;
    includes WSL shims and GUID definitions for Mesa/libdxcore consumers.
  - xv6 mapping: useful for validating ABI and COM interface expectations when
    debugging Mesa D3D12 failures.

## Immediate Lessons For xv6

- Treat WSL `dxgkrnl` as the transport shape reference, especially message
  sizing, per-process/open handle ownership, and object lookup before forwarding.
- Treat Microsoft DDI docs as the authoritative source for newer Windows private
  query payloads such as `D3DDDI_QUERYREGISTRY_INFO`; WSL may just forward these
  without naming the query type in its public UAPI header.
- Treat Mesa `d3d12_dxcore_screen.cpp` as the consumer-side checklist:
  `libdxcore` load, adapter-list creation for D3D12 graphics, adapter property
  reads, D3D12 device creation, then command queue creation.
- Keep Hyper-V `backend_opengl_submit` false until the Mesa D3D12 path reaches
  real device/context/submit validation, not just `/dev/dxg` readiness.
