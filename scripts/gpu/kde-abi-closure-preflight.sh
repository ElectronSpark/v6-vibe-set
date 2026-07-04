#!/usr/bin/env bash
# Offline KDE loader ABI preflight for an xv6 rootfs image.
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage: scripts/gpu/kde-abi-closure-preflight.sh [--keep-temp] [fs.img]

Extracts the KDE/Qt library runtime from fs.img into a temporary directory and
checks the KWin/Plasma/libinput/libudev dynamic loader closure without
modifying the image or booting a VM.
EOF
}

keep_temp=0
image="build-x86_64/fs.img"

while (($#)); do
    case "$1" in
        --keep-temp)
            keep_temp=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            usage
            exit 2
            ;;
        *)
            image="$1"
            shift
            ;;
    esac
done

if [[ ! -f "${image}" ]]; then
    echo "kde-abi-closure-preflight: image not found: ${image}" >&2
    exit 2
fi

for tool in debugfs readelf objdump ldd python3; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "kde-abi-closure-preflight: required tool not found: ${tool}" >&2
        exit 2
    fi
done

tmpdir="$(mktemp -d)"
root="${tmpdir}/root"

cleanup() {
    if [[ "${keep_temp}" == 1 ]]; then
        echo "kde-abi-closure-preflight: kept temp root ${root}" >&2
    else
        rm -rf "${tmpdir}"
    fi
}
trap cleanup EXIT

extract_one() {
    local src="$1"
    local dst="$2"

    mkdir -p "${dst}"
    echo "kde-abi-closure-preflight: extracting ${src}" >&2
    debugfs -R "rdump ${src} ${dst}" "${image}" >/dev/null
}

mkdir -p "${root}/usr/bin" "${root}/usr" "${root}/opt" "${root}"
extract_one /usr/bin/kwin_wayland "${root}/usr/bin"
extract_one /usr/bin/plasmashell "${root}/usr/bin"
extract_one /usr/lib "${root}/usr"
extract_one /lib "${root}"
extract_one /opt/xv6-kde-abi-libs "${root}/opt"

python3 - "${root}" <<'PY'
import os
import re
import subprocess
import sys
from collections import deque
from pathlib import Path

root = Path(sys.argv[1]).resolve()

ld_dirs = [
    "/opt/xv6-kde-abi-libs",
    "/usr/lib/x86_64-linux-gnu",
    "/lib/x86_64-linux-gnu",
    "/usr/lib",
    "/lib",
]
seed_paths = [
    "/usr/bin/kwin_wayland",
    "/usr/bin/plasmashell",
    "/usr/lib/x86_64-linux-gnu/libkwin.so.5",
    "/usr/lib/x86_64-linux-gnu/libKF5Solid.so.5",
]
seed_sonames = [
    "libinput.so.10",
    "libudev.so.1",
    "libevdev.so.2",
    "libmtdev.so.1",
    "libwacom.so.9",
]
expected_versions = [
    ("libinput_event_get_gesture_event", "LIBINPUT_0.20.0"),
    ("udev_device_get_udev", "LIBUDEV_183"),
    ("udev_enumerate_scan_subsystems", "LIBUDEV_183"),
]

errors = []
warnings = []

def rel(path):
    path = Path(path)
    try:
        return "/" + str(path.relative_to(root))
    except ValueError:
        return str(path)

def image_path(path):
    if path.startswith("/"):
        return root / path.lstrip("/")
    return root / path

def lexists(path):
    try:
        os.lstat(path)
        return True
    except FileNotFoundError:
        return False

def resolve_image_path(path):
    path = Path(path)
    seen = set()
    for _ in range(40):
        try:
            st = os.lstat(path)
        except FileNotFoundError:
            return path
        key = (st.st_dev, st.st_ino)
        if key in seen:
            errors.append(f"symlink loop while resolving {rel(path)}")
            return path
        seen.add(key)
        if not Path(path).is_symlink():
            return path
        target = os.readlink(path)
        if target.startswith("/"):
            path = root / target.lstrip("/")
        else:
            path = path.parent / target
    errors.append(f"too many symlink hops while resolving {rel(path)}")
    return path

def is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False

def run_tool(argv, *, env=None):
    return subprocess.run(
        argv,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
        check=False,
    )

def readelf_dynamic(path):
    result = run_tool(["readelf", "-d", str(path)])
    if result.returncode != 0:
        errors.append(f"readelf -d failed for {rel(path)}: {result.stdout.strip()}")
        return [], []
    needed = []
    rpaths = []
    for line in result.stdout.splitlines():
        m = re.search(r"\(NEEDED\).*Shared library: \[(.+)\]", line)
        if m:
            needed.append(m.group(1))
            continue
        m = re.search(r"\((?:RPATH|RUNPATH)\).*Library r(?:un)?path: \[(.+)\]", line)
        if m:
            rpaths.extend(p for p in m.group(1).split(":") if p)
    return needed, rpaths

def expand_rpaths(rpaths, requester):
    out = []
    origin = requester.parent
    for entry in rpaths:
        expanded = entry.replace("$ORIGIN", str(origin)).replace("${ORIGIN}", str(origin))
        if expanded.startswith(str(root)):
            out.append(Path(expanded))
        elif expanded.startswith("/"):
            out.append(image_path(expanded))
        else:
            out.append((origin / expanded).resolve())
    return out

def candidate_dirs(requester, rpaths):
    dirs = [image_path(d) for d in ld_dirs]
    dirs.extend(expand_rpaths(rpaths, requester))
    dirs.append(requester.parent)
    seen = set()
    out = []
    for d in dirs:
        key = str(d)
        if key not in seen:
            seen.add(key)
            out.append(d)
    return out

def find_library(name, requester, rpaths):
    for directory in candidate_dirs(requester, rpaths):
        candidate = directory / name
        if lexists(candidate):
            return resolve_image_path(candidate)
    return None

def require_file(path):
    host = image_path(path)
    if not lexists(host):
        errors.append(f"required image path missing: {path}")
        return None
    resolved = resolve_image_path(host)
    if not is_elf(resolved):
        errors.append(f"required image path is not an ELF object: {path}")
        return None
    return resolved

closure = {}
queue = deque()

for image_rel in seed_paths:
    resolved = require_file(image_rel)
    if resolved:
        queue.append(resolved)

opt_files = sorted(Path(root / "opt/xv6-kde-abi-libs").glob("*"))
if not opt_files:
    errors.append("required image path missing or empty: /opt/xv6-kde-abi-libs/*")
for path in opt_files:
    resolved = resolve_image_path(path)
    if is_elf(resolved):
        queue.append(resolved)

for soname in seed_sonames:
    found = find_library(soname, image_path("/usr/bin/kwin_wayland"), [])
    if not found:
        errors.append(f"required library missing from image-like search path: {soname}")
    elif not is_elf(found):
        errors.append(f"required library is not an ELF object: {soname} -> {rel(found)}")
    else:
        queue.append(found)

while queue:
    path = resolve_image_path(queue.popleft())
    if path in closure:
        continue
    if not is_elf(path):
        errors.append(f"closure member is not an ELF object: {rel(path)}")
        continue
    needed, rpaths = readelf_dynamic(path)
    closure[path] = needed
    for name in needed:
        found = find_library(name, path, rpaths)
        if not found:
            errors.append(f"missing DT_NEEDED dependency: requester={rel(path)} needed={name}")
            continue
        if not is_elf(found):
            errors.append(
                f"DT_NEEDED dependency is not an ELF object: requester={rel(path)} "
                f"needed={name} resolved={rel(found)}"
            )
            continue
        queue.append(found)

defs = {}
undefs = []

def parse_objdump(path):
    result = run_tool(["objdump", "-T", str(path)])
    if result.returncode != 0:
        errors.append(f"objdump -T failed for {rel(path)}: {result.stdout.strip()}")
        return
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 6 or fields[0].endswith(":"):
            continue
        if "*UND*" in fields:
            idx = fields.index("*UND*")
            if len(fields) <= idx + 2:
                continue
            version = fields[idx + 2]
            symbol_index = idx + 3
            if version.startswith("(") and version.endswith(")"):
                version = version[1:-1]
            elif version == "Base":
                continue
            else:
                continue
            if len(fields) <= symbol_index:
                continue
            undefs.append((fields[symbol_index], version, path))
            continue
        version = fields[-2]
        symbol = fields[-1]
        if version == "Base" or version.startswith(".") or version.startswith("("):
            continue
        defs.setdefault((symbol, version), []).append(path)

for path in sorted(closure):
    parse_objdump(path)

for symbol, version, requester in undefs:
    if (symbol, version) not in defs:
        errors.append(
            f"unresolved versioned symbol: requester={rel(requester)} "
            f"symbol={symbol}@{version}"
        )

for symbol, version in expected_versions:
    providers = defs.get((symbol, version), [])
    if not providers:
        errors.append(f"expected symbol version missing: {symbol}@{version}")
    else:
        provider_text = ",".join(sorted(rel(p) for p in providers))
        print(
            f"kde-abi-closure-preflight: expected-symbol PASS "
            f"{symbol}@{version} providers={provider_text}"
        )

for image_rel, version in [
    ("/opt/xv6-kde-abi-libs/libinput.so.10", "LIBINPUT_0.20.0"),
    ("/opt/xv6-kde-abi-libs/libudev.so.1", "LIBUDEV_183"),
]:
    path = require_file(image_rel)
    if not path:
        continue
    result = run_tool(["readelf", "-V", str(path)])
    if result.returncode != 0:
        errors.append(f"readelf -V failed for {image_rel}: {result.stdout.strip()}")
    elif version not in result.stdout:
        errors.append(f"readelf -V missing {version}: {image_rel}")
    else:
        print(f"kde-abi-closure-preflight: readelf-version PASS path={image_rel} version={version}")

ld_library_path = ":".join(str(image_path(d)) for d in ld_dirs)
ldd_targets = [
    image_path("/usr/bin/kwin_wayland"),
    image_path("/usr/bin/plasmashell"),
    image_path("/usr/lib/x86_64-linux-gnu/libkwin.so.5"),
    image_path("/usr/lib/x86_64-linux-gnu/libKF5Solid.so.5"),
    image_path("/opt/xv6-kde-abi-libs/libinput.so.10"),
]
env = os.environ.copy()
env["LD_LIBRARY_PATH"] = ld_library_path
env.pop("LD_PRELOAD", None)
for target in ldd_targets:
    if not is_elf(resolve_image_path(target)):
        continue
    result = run_tool(["ldd", "-r", str(target)], env=env)
    bad_lines = [
        line.strip()
        for line in result.stdout.splitlines()
        if "not found" in line or "undefined symbol:" in line
    ]
    if result.returncode != 0 and not bad_lines:
        warnings.append(f"ldd -r returned {result.returncode} for {rel(target)}")
    if bad_lines:
        for line in bad_lines:
            errors.append(f"ldd -r failure: target={rel(target)} line={line}")
    else:
        print(f"kde-abi-closure-preflight: ldd-r PASS target={rel(target)}")

if warnings:
    for warning in warnings:
        print(f"kde-abi-closure-preflight: WARN {warning}")

if errors:
    print("kde-abi-closure-preflight: result FAIL")
    for error in errors:
        print(f"kde-abi-closure-preflight: ERROR {error}")
    sys.exit(1)

print(
    "kde-abi-closure-preflight: result PASS "
    f"closure_objects={len(closure)} versioned_undefs={len(undefs)} "
    f"ld_library_path={ld_library_path}"
)
PY
