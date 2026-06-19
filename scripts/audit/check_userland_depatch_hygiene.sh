#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${repo_root}"

python3 scripts/audit/userland_depatch_inventory.py \
    --check-reviewed-allowlist \
    --check-reviewed-source-refs \
    --check-reviewed-user-programs \
    --check-reviewed-phases \
    "$@"
