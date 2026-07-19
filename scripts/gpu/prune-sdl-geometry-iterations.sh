#!/usr/bin/env bash
# Retain only the newest completed SDL geometry evidence iterations.  Active or
# incompletely cleaned runs are never candidates.
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
probe_root="${repo_root}/build-x86_64/sdl-geometry-probe"
keep="${1:-3}"

if [[ ! "${keep}" =~ ^[1-9][0-9]*$ ]]; then
        echo "prune-sdl-geometry-iterations: invalid keep count: ${keep}" >&2
        exit 2
fi
if [[ ! -d "${probe_root}" ]]; then
        exit 0
fi
if [[ -L "${probe_root}" || "$(readlink -f -- "${probe_root}")" != "${probe_root}" ]]; then
        echo "prune-sdl-geometry-iterations: unsafe probe root: ${probe_root}" >&2
        exit 2
fi

completed_seen=0
while IFS= read -r -d '' entry; do
        dir="${entry#* }"
        [[ -d "${dir}" && ! -L "${dir}" ]] || continue
        # A retained overlay or missing completion receipt means the iteration
        # may still be active or its cleanup is unproven.
        [[ ! -e "${dir}/xv6-sdl.qcow2" ]] || continue
        completed=0
        if [[ -f "${dir}/completed-skip.txt" ]]; then
                completed=1
        elif [[ -f "${dir}/final-qemu-inventory.txt" ]]; then
                inventory="$(<"${dir}/final-qemu-inventory.txt")"
                if [[ "${inventory}" == *"exact_x86_qemu_count=0"* ]]; then
                        completed=1
                fi
        fi
        ((completed == 1)) || continue
        ((completed_seen += 1))
        if ((completed_seen > keep)); then
                rm -rf -- "${dir}"
                echo "prune-sdl-geometry-iterations: removed ${dir}"
        fi
done < <(
        find "${probe_root}" -mindepth 1 -maxdepth 1 -type d \
                -name 'live-*' -printf '%T@ %p\0' | sort -z -nr
)
