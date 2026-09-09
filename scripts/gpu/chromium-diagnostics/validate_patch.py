#!/usr/bin/env python3
"""Verify touched pinned sources and check/apply the observational patch.

This validates source application only. It neither builds Chromium nor starts
a browser/VM. A successful check is never compilation or runtime evidence.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


HERE = Path(__file__).resolve().parent
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_PATCH_BYTES = 256 * 1024


def regular_file(root, relative, *, must_exist=True, size_limit=MAX_SOURCE_BYTES):
    parts = Path(relative).parts
    if not parts or Path(relative).is_absolute() or any(p in (".", "..") for p in parts):
        raise ValueError(f"unsafe relative path: {relative}")
    target = root
    for part in parts:
        target = target / part
        if target.is_symlink():
            raise ValueError(f"symlink target/parent: {relative}")
    if must_exist:
        if not target.is_file():
            raise ValueError(f"missing/nonregular file: {relative}")
        if target.stat().st_size > size_limit:
            raise ValueError(f"file exceeds {size_limit} bytes: {relative}")
    elif target.exists():
        raise ValueError(f"new diagnostic file already exists: {relative}")
    return target


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(checkout, *args):
    return subprocess.run(
        ["git", "-C", str(checkout), *args],
        check=True, text=True, capture_output=True, timeout=60,
    )


def validate(checkout, *, apply=False, delta=False, raw_ref_delta=False):
    checkout = Path(checkout).resolve(strict=True)
    if not checkout.is_dir():
        raise ValueError("checkout must be a directory")
    # Avoid Git silently interpreting paths relative to an enclosing checkout.
    top = Path(git(checkout, "rev-parse", "--show-toplevel").stdout.strip()).resolve()
    if top != checkout:
        raise ValueError("checkout must be its Git worktree root")

    if delta and raw_ref_delta:
        raise ValueError("choose only one diagnostic delta")
    manifest_name = ("raw-ref-recorder.delta.json" if raw_ref_delta else
                     "surface-ack-completion.delta.json" if delta else "upstream-sources.json")
    manifest = json.loads((HERE / manifest_name).read_text())
    patch = regular_file(HERE, manifest["patch_file"], size_limit=MAX_PATCH_BYTES)
    if digest(patch) != manifest["patch_sha256"]:
        raise ValueError("patch SHA-256 differs from manifest")
    observed = []
    for row in manifest["sources"]:
        target = regular_file(checkout, row["path"])
        actual = digest(target)
        if actual != row["sha256"]:
            raise ValueError(f"pinned source SHA-256 mismatch: {row['path']}")
        observed.append({"path": row["path"], "sha256": actual})
    for relative in manifest["new_files"]:
        regular_file(checkout, relative, must_exist=False)

    git(checkout, "apply", "--check", "--whitespace=error-all", str(patch))
    try:
        commit = git(checkout, "rev-parse", "--verify", "HEAD").stdout.strip()
    except subprocess.CalledProcessError:
        commit = None  # An isolated retained-source tree need not have a commit.
    result = {
        "schema_version": 1,
        "checkout": str(checkout),
        "source_commit": commit,
        "pinned_tag": manifest["chromium_tag"],
        "patch_sha256": manifest["patch_sha256"],
        "verified_sources": observed,
        "patch_application_check": "passed",
        "applied": False,
        "compilation": "not performed",
        "runtime_validation": "not performed",
        "scope": "Only listed source hashes are verified; retain full checkout/build provenance separately.",
    }
    if delta or raw_ref_delta:
        result["from_full_patch_sha256"] = manifest["from_full_patch_sha256"]
        result["to_full_patch_sha256"] = manifest["to_full_patch_sha256"]
    if apply:
        git(checkout, "apply", "--whitespace=error-all", str(patch))
        for relative, expected in manifest["patched_sha256"].items():
            target = regular_file(checkout, relative)
            if digest(target) != expected:
                raise ValueError(f"applied output SHA-256 mismatch: {relative}")
        result["applied"] = True
        result["verified_outputs"] = manifest["patched_sha256"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("--apply", action="store_true", help="Apply after all source and patch checks pass")
    deltas = parser.add_mutually_exclusive_group()
    deltas.add_argument("--delta", action="store_true",
                        help="Check/apply the surface-completion delta over the exact prior diagnostic patch")
    deltas.add_argument("--raw-ref-delta", action="store_true",
                        help="Check/apply the raw_ref recorder fix over exact 088f instrumentation")
    parser.add_argument("--receipt", type=Path, help="Write a new JSON receipt; existing files are refused")
    args = parser.parse_args()
    if args.receipt and (args.receipt.exists() or args.receipt.is_symlink()):
        parser.error("receipt already exists")
    try:
        result = validate(args.checkout, apply=args.apply, delta=args.delta, raw_ref_delta=args.raw_ref_delta)
        output = json.dumps(result, indent=2) + "\n"
        if args.receipt:
            with args.receipt.open("x") as stream:
                stream.write(output)
        sys.stdout.write(output)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"diagnostic patch validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
