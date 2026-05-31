#!/usr/bin/env bash
# Quick compile-check of the GPU-related kernel translation units using the
# exact flags from compile_commands.json. Compiles to /dev/null (syntax/type
# check only). Exits non-zero on the first failure.
set -u
ROOT=/home/es/xv6-os
CC_JSON="$ROOT/build-codex-x86_64/kernel/build/compile_commands.json"

python3 - "$CC_JSON" <<'PY'
import json, sys, subprocess, shlex, os
cc = json.load(open(sys.argv[1]))
seen = set()
targets = ('dev/fb/module.c', 'dev/hyperv/module.c', 'virtio_gpu.c')
rc = 0
for e in cc:
    f = e['file']
    if not any(f.endswith(t) for t in targets):
        continue
    if f in seen:
        continue
    seen.add(f)
    cmd = e['command']
    # redirect object output to /dev/null
    parts = shlex.split(cmd)
    out = []
    skip = False
    for i, p in enumerate(parts):
        if skip:
            skip = False
            continue
        if p == '-o':
            out += ['-o', '/dev/null']
            skip = True
            continue
        out.append(p)
    print('==> compiling', f.replace('/home/es/xv6-os/', ''), flush=True)
    r = subprocess.run(out, cwd=e['directory'])
    if r.returncode != 0:
        print('FAILED:', f)
        rc = 1
sys.exit(rc)
PY
status=$?
if [ $status -eq 0 ]; then
    echo "GPU-BUILD-OK"
else
    echo "GPU-BUILD-FAIL"
fi
exit $status
