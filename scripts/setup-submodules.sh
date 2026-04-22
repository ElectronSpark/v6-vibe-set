#!/usr/bin/env bash
# setup-submodules.sh — convert the four bundled subdirs into proper
# git submodules. Run this AFTER you push each subdir as its own repo
# and edit .gitmodules.template with real URLs.
#
# Idempotent: skips already-tracked submodules.
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
