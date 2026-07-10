#!/usr/bin/env bash
# Small regression suite for the fail-closed ripgrep wrappers.
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd -P)"
readonly SAFE="$SCRIPT_DIR/safe-rg.sh"
readonly ARTIFACT="$SCRIPT_DIR/safe-rg-artifact.sh"
readonly LOCK_FILE="/tmp/xv6-os-safe-rg-${UID}.lock"

tmp=$(mktemp -d "$REPO_ROOT/.safe-rg-test.XXXXXX")
outside_tmp=$(mktemp -d)
lock_pid=
cleanup() {
    if [[ -n "$lock_pid" ]] && kill -0 "$lock_pid" 2>/dev/null; then
        kill "$lock_pid" 2>/dev/null || true
        wait "$lock_pid" 2>/dev/null || true
    fi
    rm -rf -- "$tmp"
    rm -rf -- "$outside_tmp"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'test-safe-rg: FAIL: %s\n' "$*" >&2
    exit 1
}

expect_reject() {
    local label=$1
    shift
    if "$@" >"$tmp/stdout" 2>"$tmp/stderr"; then
        fail "$label unexpectedly succeeded"
    fi
    grep -q 'rejected:' "$tmp/stderr" || fail "$label lacked rejection diagnostic"
}

mkdir -p "$tmp/src" "$tmp/build-x86_64" "$tmp/nested/build-x86_64" "$tmp/real-parent"
printf 'guardrail_needle source\n' >"$tmp/src/sample.c"
printf 'guardrail_needle source helper\n' >"$tmp/src/build-helper.sh"
printf 'guardrail_needle pattern\n' >"$tmp/src/patterns.txt"
printf 'guardrail_needle image payload\n' >"$tmp/build-x86_64/fs.img"
printf 'guardrail_needle bounded log\n' >"$tmp/build-x86_64/run.log"
printf 'guardrail_needle nested build payload\n' >"$tmp/nested/build-x86_64/run.log"
printf 'guardrail_needle raw payload\n' >"$tmp/disk.raw"
printf 'guardrail_needle symlink-parent source\n' >"$tmp/real-parent/parent.c"
ln -s "$tmp/src" "$tmp/src-link"
ln -s "$tmp/build-x86_64/run.log" "$tmp/log-link"
ln -s "$tmp/real-parent" "$tmp/symlink-parent"

blocked_extensions=(IMG VHD VHDX QCOW2 RAW ISO ZIP TAR GZ XZ ZST 7Z)
for extension in "${blocked_extensions[@]}"; do
    printf 'guardrail_needle blocked extension\n' >"$tmp/src/blocked.$extension"
done

"$SAFE" -n guardrail_needle "$tmp/src" >"$tmp/safe.out"
grep -q 'sample.c' "$tmp/safe.out" || fail "safe source search produced no match"
"$SAFE" -nH guardrail_needle "$tmp/src/build-helper.sh" >"$tmp/source-file.out"
grep -q 'build-helper.sh' "$tmp/source-file.out" || fail "ordinary build-named source file was rejected"

(
    cd "$tmp"
    "$SAFE" -l guardrail_needle >"$tmp/default.out"
)
grep -q 'src/sample.c' "$tmp/default.out" || fail "default source search produced no match"
if grep -q 'build-x86_64' "$tmp/default.out"; then
    fail "default search entered a generated build tree"
fi
if grep -q 'blocked extension' "$tmp/default.out"; then
    fail "default search entered a case-insensitive image/archive artifact"
fi

(
    cd "$tmp/src"
    "$SAFE" guardrail_needle >"$tmp/source-cwd.out"
    "$SAFE" -e guardrail_needle >"$tmp/source-cwd-regexp.out"
)
grep -q 'source' "$tmp/source-cwd.out" || fail "in-repo implicit source root failed"
grep -q 'source' "$tmp/source-cwd-regexp.out" || fail "in-repo -e implicit source root failed"

expect_reject implicit-etc bash -c 'cd /etc && exec "$1" guardrail_needle' _ "$SAFE"
expect_reject implicit-etc-regexp bash -c 'cd /etc && exec "$1" -e guardrail_needle' _ "$SAFE"
expect_reject implicit-etc-color bash -c 'cd /etc && exec "$1" --color never guardrail_needle' _ "$SAFE"
expect_reject implicit-etc-depth bash -c 'cd /etc && exec "$1" -d 1 guardrail_needle' _ "$SAFE"
expect_reject implicit-outside bash -c 'cd "$1" && exec "$2" guardrail_needle' \
    _ "$outside_tmp" "$SAFE"
ln -s "$tmp/src" "$tmp/source-cwd-link"
expect_reject implicit-symlink-cwd bash -c 'cd "$1" && exec "$2" guardrail_needle' \
    _ "$tmp/source-cwd-link" "$SAFE"

# Exact incident shape and flag variants must fail before /usr/bin/rg starts.
expect_reject exact-incident "$SAFE" -l -S --text guardrail_needle "$tmp/build-x86_64"
expect_reject short-text "$SAFE" -a guardrail_needle "$tmp/src"
expect_reject unrestricted "$SAFE" -uuu guardrail_needle "$tmp/src"
expect_reject clustered "$SAFE" -nua guardrail_needle "$tmp/src"
expect_reject no-ignore "$SAFE" --no-ignore-vcs guardrail_needle "$tmp/src"
expect_reject binary "$SAFE" --binary guardrail_needle "$tmp/src"
expect_reject long-file-separated "$SAFE" --file "$tmp/src/patterns.txt" "$tmp/src"
expect_reject long-file-equals "$SAFE" --file="$tmp/src/patterns.txt" "$tmp/src"
expect_reject short-file-cluster "$SAFE" -nf "$tmp/src/patterns.txt" "$tmp/src"
expect_reject threads-separated "$SAFE" --threads 2 guardrail_needle "$tmp/src"
expect_reject threads-equals "$SAFE" --threads=2 guardrail_needle "$tmp/src"
expect_reject filesize-separated "$SAFE" --max-filesize 1M guardrail_needle "$tmp/src"
expect_reject filesize-equals "$SAFE" --max-filesize=1M guardrail_needle "$tmp/src"
expect_reject ignore-file-separated "$SAFE" --ignore-file "$tmp/src/patterns.txt" guardrail_needle "$tmp/src"
expect_reject ignore-file-equals "$SAFE" --ignore-file="$tmp/src/patterns.txt" guardrail_needle "$tmp/src"
expect_reject pre-separated "$SAFE" --pre /bin/true guardrail_needle "$tmp/src"
expect_reject pre-equals "$SAFE" --pre=/bin/true guardrail_needle "$tmp/src"
expect_reject pre-glob-separated "$SAFE" --pre-glob '*.c' guardrail_needle "$tmp/src"
expect_reject pre-glob-equals "$SAFE" --pre-glob='*.c' guardrail_needle "$tmp/src"
expect_reject text-equals "$SAFE" --text=true guardrail_needle "$tmp/src"
expect_reject unrestricted-equals "$SAFE" --unrestricted=true guardrail_needle "$tmp/src"
expect_reject binary-equals "$SAFE" --binary=true guardrail_needle "$tmp/src"
expect_reject follow-equals "$SAFE" --follow=true guardrail_needle "$tmp/src"
expect_reject search-zip-equals "$SAFE" --search-zip=true guardrail_needle "$tmp/src"
expect_reject mmap-equals "$SAFE" --mmap=true guardrail_needle "$tmp/src"
expect_reject broad-root "$SAFE" guardrail_needle /
expect_reject outside-etc "$SAFE" guardrail_needle /etc
expect_reject build-tree "$SAFE" guardrail_needle "$tmp/build-x86_64"
expect_reject nested-build-tree "$SAFE" guardrail_needle "$tmp/nested/build-x86_64"
expect_reject raw-image "$SAFE" guardrail_needle "$tmp/disk.raw"
expect_reject recursive-symlink "$SAFE" guardrail_needle "$tmp/src-link"
expect_reject symlinked-parent "$SAFE" guardrail_needle "$tmp/symlink-parent/parent.c"
expect_reject parent-traversal "$SAFE" guardrail_needle "$tmp/src/../src/sample.c"

for extension in "${blocked_extensions[@]}"; do
    expect_reject "explicit-$extension" "$SAFE" guardrail_needle "$tmp/src/blocked.$extension"
done

"$ARTIFACT" guardrail_needle "$tmp/build-x86_64/run.log" >"$tmp/artifact.out"
grep -q 'bounded log' "$tmp/artifact.out" || fail "safe artifact search produced no match"
expect_reject artifact-image "$ARTIFACT" guardrail_needle "$tmp/build-x86_64/fs.img"
expect_reject artifact-raw "$ARTIFACT" guardrail_needle "$tmp/disk.raw"
expect_reject artifact-symlink "$ARTIFACT" guardrail_needle "$tmp/log-link"
expect_reject artifact-symlinked-parent "$ARTIFACT" guardrail_needle "$tmp/symlink-parent/parent.c"
expect_reject artifact-directory "$ARTIFACT" guardrail_needle "$tmp/src"
expect_reject artifact-outside-repo "$ARTIFACT" guardrail_needle /etc/hosts
expect_reject artifact-parent-traversal "$ARTIFACT" guardrail_needle "$tmp/src/../src/sample.c"

for extension in "${blocked_extensions[@]}"; do
    expect_reject "artifact-$extension" "$ARTIFACT" guardrail_needle "$tmp/src/blocked.$extension"
done

# Prove that both wrappers honor the same process-global serialization lock.
exec 8>"$LOCK_FILE"
flock -x 8
"$SAFE" guardrail_needle "$tmp/src" >"$tmp/locked.out" 8>&- &
lock_pid=$!
sleep 0.2
kill -0 "$lock_pid" 2>/dev/null || fail "search did not wait on the global lock"
flock -u 8
wait "$lock_pid" || fail "serialized search failed after lock release"
kill -0 "$lock_pid" 2>/dev/null && fail "serialized search process remained alive"
lock_pid=

if [[ -n "$(jobs -pr)" ]]; then
    fail "background process residue remains"
fi

printf 'test-safe-rg: PASS\n'
