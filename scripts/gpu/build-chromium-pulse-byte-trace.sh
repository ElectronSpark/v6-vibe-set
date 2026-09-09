#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source_file="$root/scripts/image/chromium-pulse-byte-trace-preload.c"
out_dir="$root/build-x86_64/host-gui-runtime"
output="$out_dir/chromium-pulse-byte-trace-preload.so"
mkdir -p "$out_dir"
cc -std=c11 -O2 -fPIC -shared \
    -Wl,-z,defs -Wl,-z,now -Wl,-z,relro \
    -Wl,--version-script=/dev/stdin \
    -o "$output" "$source_file" -ldl -pthread <<'MAP'
{
  global: dlopen; dlsym; pa_stream_write;
  local: *;
};
MAP
printf 'PULSE_BYTE_TRACE_BUILD status=PASS output=%s\n' "$output"
