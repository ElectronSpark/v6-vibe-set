#!/usr/bin/env bash
# init-local-repos.sh — initialize each sub-repo as its own git repo
# (in-place). Useful before they're pushed to real remotes. Safe to
# re-run; skips dirs that already have .git.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for sub in toolchain kernel user ports; do
	d="${ROOT}/${sub}"
	if [[ ! -d "${d}" ]]; then
		echo "missing: ${d}" >&2
		continue
	fi
	if [[ -d "${d}/.git" ]]; then
		echo "[${sub}] already a git repo, skipping"
		continue
	fi
	git -C "${d}" init -q
	git -C "${d}" add -A
	git -C "${d}" -c user.email=xv6@local -c user.name=xv6 \
		commit -q -m "initial scaffold"
	echo "[${sub}] initialized"
done

echo
echo "Next steps:"
echo "  1. Push each sub-repo to a remote:"
echo "       cd toolchain && git remote add origin <url> && git push -u origin main"
echo "     (repeat for kernel, user, ports)"
echo "  2. Edit .gitmodules.template — replace REPLACE_ME with each remote URL."
echo "  3. From umbrella root: rm -rf {toolchain,kernel,user,ports}"
echo "                          ./scripts/setup-submodules.sh"
