# Hyper-V/WSL GPU-PV D3D12 runtime search path. The Microsoft D3D12 runtime and
# the NVIDIA UMD live here; D3D12 clients (and Mesa dzn) need them on the path.
export LD_LIBRARY_PATH="/usr/lib/wsl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# When the Hyper-V GPU-P D3DKMT transport is present, make Mesa render on the
# host NVIDIA GPU by default so no caller has to export these by hand. Only
# rendering is GPU-backed; presentation is still a CPU framebuffer blit.
if [ -e /dev/dxg ]; then
    export GALLIUM_DRIVER=d3d12
    export MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA
    export LIBGL_ALWAYS_SOFTWARE=0
    export LIBGL_DRIVERS_PATH=/lib/dri
fi
