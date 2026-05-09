#!/usr/bin/env bash
# setup-submodules.sh - initialize the three nested submodules.
#
# Idempotent: safe to rerun after cloning or after submodule URL changes.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

if [[ ! -f .gitmodules ]]; then
	if [[ ! -f .gitmodules.template ]]; then
		echo "no .gitmodules and no .gitmodules.template" >&2
		exit 1
	fi
	if grep -q REPLACE_ME .gitmodules.template; then
		echo "edit .gitmodules.template and replace REPLACE_ME URLs first" >&2
		exit 1
	fi
	cp .gitmodules.template .gitmodules
fi

git submodule sync
git submodule update --init --recursive
echo "[submodules] up to date"
