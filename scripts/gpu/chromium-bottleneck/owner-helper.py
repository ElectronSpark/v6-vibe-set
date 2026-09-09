#!/usr/bin/env python3
"""Owned diagnostic launcher and bounded pre/post-playback inspection."""
import hashlib
import gzip
import json
import os
import re
import sys
import stat as stat_module
import time
import urllib.request
import http.client
import subprocess
from urllib.parse import urlsplit
from pathlib import Path

LOG = Path('/host-gui-wayland-chromium.log')
PREFIX = 'MEDIA_OWNER_JSON '
BROWSER = Path('/opt/host-gui/wayland-chromium/chrome-linux64/chrome')
BROWSER_SHA = '0b20b130e7edd9dd51873be867761295fe0cfad490c2b9a64f95bd3cfc08fa71'
TRACE_CATEGORIES = 'media,cc,viz,benchmark,mojom,mojom.flow,graphics.pipeline,disabled-by-default-mojom'
DIAGNOSTIC_CATEGORY = 'disabled-by-default-media.bottleneck'
POLICY_ENV = 'MEDIA_AUDIT_DIAGNOSTIC_POLICY_SHA256'
POLICY_FILE = Path('/tmp/media-audit-diagnostic-policy.json')
RUNTIME_PROOF = Path('/tmp/media-audit-runtime-verified.json')
MAX_POLICY_BYTES = 1024 * 1024
MAX_RUNTIME_FILES = 1024
MAX_RUNTIME_FILE_BYTES = 2 * 1024**3
MAX_RUNTIME_BYTES = 4 * 1024**3
ACTIVE_POLICY = None
COMPARISON_ORDER = ['off-1', 'on-1', 'on-2', 'off-2']
PARITY_ENV = ('WAYLAND_DEBUG', 'WAYLAND_CHROMIUM_MULTIPROCESS', 'WAYLAND_DISPLAY',
              'XDG_RUNTIME_DIR', 'XDG_SESSION_TYPE', 'DISPLAY', 'GALLIUM_DRIVER',
              'GALLIUM_HUD', 'LIBGL_ALWAYS_SOFTWARE', 'LIBGL_ALWAYS_INDIRECT',
              'LIBGL_DRI3_DISABLE', 'LIBGL_DEBUG', 'MESA_LOADER_DRIVER_OVERRIDE',
              'MESA_GL_VERSION_OVERRIDE', 'MESA_GLES_VERSION_OVERRIDE', 'MESA_DEBUG',
              'MESA_EXTENSION_OVERRIDE', 'MESA_NO_ERROR', 'DRI_PRIME', 'GBM_BACKEND',
              'EGL_PLATFORM', '__EGL_VENDOR_LIBRARY_FILENAMES', '__GLX_VENDOR_LIBRARY_NAME',
              'LD_LIBRARY_PATH', 'LD_PRELOAD', 'PULSE_SERVER', 'PULSE_CLIENTCONFIG',
              'PULSE_NO_SHM', 'ALSA_CONFIG_PATH', 'ALSA_PCM_CARD', 'ALSA_PCM_DEVICE',
              'OMP_NUM_THREADS', 'GOMP_CPU_AFFINITY')


def sha256_value(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def capture_trials(purpose):
    if purpose == 'comparison':
        return [(trial, trial.startswith('on-')) for trial in COMPARISON_ORDER]
    if purpose == 'emission-smoke':
        return [('on-1', True)]
    raise ValueError('unknown capture purpose')


def capture_controls(purpose):
    capture_trials(purpose)
    return ('single ON emission validation; no comparison/performance credit' if purpose == 'emission-smoke'
            else 'same-build OFF/ON/ON/OFF; diagnostic category enabled only for ON')


def runtime_files(records, browser_sha, browser_bytes):
    if not isinstance(records, list) or not 0 < len(records) <= MAX_RUNTIME_FILES:
        raise ValueError('runtime_files count outside gate')
    seen = set()
    total = 0
    normalized = []
    for row in records:
        if not isinstance(row, dict) or set(row) != {'guest_path', 'sha256', 'bytes'}:
            raise ValueError('invalid runtime file record')
        name, size, digest = row['guest_path'], row['bytes'], row['sha256']
        if (not isinstance(name, str) or len(name) > 1024 or
                not name.startswith(str(BROWSER.parent) + '/') or
                str(Path(name)) != name or '..' in Path(name).parts or
                re.search(r'[\x00-\x1f\x7f]', name) or name in seen):
            raise ValueError('unsafe or duplicate runtime path')
        if type(size) is not int or not 0 <= size <= MAX_RUNTIME_FILE_BYTES or not sha256_value(digest):
            raise ValueError('invalid runtime file size/hash')
        seen.add(name)
        total += size
        normalized.append(dict(row))
    if total > MAX_RUNTIME_BYTES:
        raise ValueError('runtime aggregate exceeds gate')
    browser = [row for row in normalized if row['guest_path'] == str(BROWSER)]
    if browser != [{'guest_path': str(BROWSER), 'sha256': browser_sha, 'bytes': browser_bytes}]:
        raise ValueError('runtime browser entry differs from top-level identity')
    return normalized


def validate_guest_policy(policy):
    if not isinstance(policy, dict) or type(policy.get('schema_version')) is not int or policy['schema_version'] != 1:
        raise ValueError('invalid diagnostic policy schema')
    if policy.get('arm') != 'chromium-bottleneck-diagnostic' or policy.get('mode') != 'diagnostic':
        raise ValueError('policy is not an explicit diagnostic arm')
    if policy.get('selected_order') != [trial for trial, _ in capture_trials(policy.get('capture_purpose'))]:
        raise ValueError('selected trial order differs from capture purpose')
    token = policy.get('run_token')
    if not isinstance(token, str) or not re.fullmatch(r'chromium-diagnostic-capture-[0-9]{8}T[0-9]{6}Z', token):
        raise ValueError('invalid diagnostic run token')
    if policy.get('browser_path') != str(BROWSER) or policy.get('browser_sha256') == BROWSER_SHA:
        raise ValueError('diagnostic policy must name the fixed path and a distinct binary')
    for field in ('browser_sha256', 'patch_sha256', 'rootfs_sha256', 'runtime_manifest_sha256', 'config_sha256'):
        if not sha256_value(policy.get(field)):
            raise ValueError('missing/invalid diagnostic identity: ' + field)
    commit = policy.get('source_commit')
    size = policy.get('browser_bytes')
    if not isinstance(commit, str) or not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('invalid source commit')
    if type(size) is not int or not 64 <= size <= MAX_RUNTIME_FILE_BYTES:
        raise ValueError('invalid diagnostic browser size')
    runtime_files(policy.get('runtime_files'), policy['browser_sha256'], size)
    if not isinstance(policy.get('build_provenance', {}), dict):
        raise ValueError('invalid build provenance')
    return policy


def policy_bytes(policy):
    data = json.dumps(policy, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()
    if len(data) > MAX_POLICY_BYTES:
        raise ValueError('diagnostic policy exceeds cap')
    return data


def capture_identity(policy):
    if policy is None:
        return {'mode': 'baseline', 'browser_sha256': BROWSER_SHA,
                'capture_purpose': 'comparison', 'selected_order': list(COMPARISON_ORDER)}
    return {key: policy[key] for key in ('mode', 'arm', 'run_token', 'browser_sha256',
            'source_commit', 'patch_sha256', 'rootfs_sha256', 'runtime_manifest_sha256', 'config_sha256',
            'capture_purpose', 'selected_order')}


def trace_flags(trial, policy=None):
    if trial not in ('off-1', 'on-1', 'on-2', 'off-2'):
        raise ValueError('unknown trial')
    if policy is not None and trial not in policy['selected_order']:
        raise ValueError('trial is outside sealed capture purpose/order')
    if not trial.startswith('on-'):
        return []
    categories = TRACE_CATEGORIES + (',' + DIAGNOSTIC_CATEGORY if policy is not None else '')
    return ['--enable-tracing=' + categories, '--trace-startup-format=json',
            '--trace-startup-file=/tmp/media-ab-' + trial + '-trace.json',
            '--trace-startup-record-mode=record-continuously',
            '--default-trace-buffer-size-limit-in-kb=32768']


def load_capture_policy():
    expected = os.environ.get(POLICY_ENV)
    if expected is None:
        return None  # A stale /tmp file must never opt the baseline into diagnostics.
    if not sha256_value(expected) or POLICY_FILE.is_symlink() or not POLICY_FILE.is_file():
        raise ValueError('explicit diagnostic policy missing or invalid')
    with POLICY_FILE.open('rb') as stream:
        data = stream.read(MAX_POLICY_BYTES + 1)
    if len(data) > MAX_POLICY_BYTES or hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('diagnostic policy hash/cap mismatch')
    return validate_guest_policy(json.loads(data))


def install_policy(expected):
    if not sha256_value(expected):
        raise ValueError('invalid requested policy hash')
    origin = Path('/tmp/media-audit-origin').read_text().strip()
    with urllib.request.urlopen(origin + '/capture-policy.json', timeout=10) as response:
        data = response.read(MAX_POLICY_BYTES + 1)
    if len(data) > MAX_POLICY_BYTES or hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('served diagnostic policy hash/cap mismatch')
    policy = validate_guest_policy(json.loads(data))
    with POLICY_FILE.open('xb') as stream:
        stream.write(data)
    return policy


def file_stamp(st):
    return [st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_mode,
            st.st_ctime_ns, st.st_nlink]


def runtime_regular_file(path):
    for component in [path, *path.parents]:
        if component.is_symlink():
            raise ValueError('symlink runtime path: ' + str(path))
    before = path.lstat()
    if not stat_module.S_ISREG(before.st_mode):
        raise ValueError('nonregular runtime file: ' + str(path))
    return before


def verify_runtime(policy):
    observed = []
    stamps = []
    deadline = time.monotonic() + 150
    for row in policy['runtime_files']:
        path = Path(row['guest_path'])
        before = runtime_regular_file(path)
        if before.st_size != row['bytes']:
            raise ValueError('runtime file size mismatch: ' + str(path))
        digest = hashlib.sha256()
        count = 0
        with path.open('rb') as stream:
            if file_stamp(os.fstat(stream.fileno())) != file_stamp(before):
                raise ValueError('runtime file changed before hash')
            while count < before.st_size:
                if time.monotonic() > deadline:
                    raise ValueError('runtime hashing deadline')
                block = stream.read(min(1024 * 1024, before.st_size - count))
                if not block:
                    raise ValueError('runtime file shortened')
                count += len(block)
                digest.update(block)
        after = runtime_regular_file(path)
        if file_stamp(before) != file_stamp(after) or digest.hexdigest() != row['sha256']:
            raise ValueError('runtime file identity/hash mismatch: ' + str(path))
        observed.append({'guest_path': str(path), 'sha256': digest.hexdigest(), 'bytes': count})
        stamps.append({'guest_path': str(path), 'stamp': file_stamp(after)})
    proof = {'policy_sha256': os.environ[POLICY_ENV], 'files': stamps,
             'runtime_files_sha256': hashlib.sha256(policy_bytes(observed)).hexdigest()}
    with RUNTIME_PROOF.open('x') as stream:
        json.dump(proof, stream)
    return {'runtime_files_count': len(observed), 'runtime_bytes': sum(row['bytes'] for row in observed),
            'runtime_files_sha256': proof['runtime_files_sha256'], 'all_runtime_files_matched': True}


def runtime_still_verified(policy):
    if RUNTIME_PROOF.is_symlink():
        raise ValueError('runtime verification proof is a symlink')
    with RUNTIME_PROOF.open('rb') as stream:
        raw = stream.read(MAX_POLICY_BYTES + 1)
    if len(raw) > MAX_POLICY_BYTES:
        raise ValueError('runtime verification proof exceeds cap')
    proof = json.loads(raw)
    if proof.get('policy_sha256') != os.environ[POLICY_ENV]:
        raise ValueError('runtime proof belongs to another capture policy')
    if [row.get('guest_path') for row in proof.get('files', [])] != [row['guest_path'] for row in policy['runtime_files']]:
        raise ValueError('runtime proof coverage mismatch')
    for row in proof['files']:
        if file_stamp(runtime_regular_file(Path(row['guest_path']))) != row['stamp']:
            raise ValueError('runtime changed after hash: ' + row['guest_path'])


def emit(row, timeout=10):
    row.setdefault('capture_identity', capture_identity(ACTIVE_POLICY))
    origin = Path('/tmp/media-audit-origin').read_text().strip()
    body = json.dumps({'trial':'supervisor','phase':'owner-result','owner':row}).encode()
    if len(body)>65536: raise RuntimeError('owner report exceeds cap')
    request = urllib.request.Request(origin+'/trial-event', data=body, method='POST',
                                     headers={'Content-Type':'application/json'})
    with urllib.request.urlopen(request,timeout=timeout) as response:
        if response.status != 200: raise RuntimeError('owner receipt upload rejected')
        response.read(4096)
    print('MEDIA_OWNER_ACK ' + str(row.get('phase')), flush=True)


def exit_read(path, cap):
    try:
        with path.open('rb') as stream:
            data = stream.read(cap + 1)
        return {'text': data[:cap].decode(errors='replace'),
                'truncated': len(data) > cap, 'cap_bytes': cap}
    except OSError as error:
        return {'error': str(error), 'errno': error.errno}


def exit_identity(pid, start, trial, proc_root=Path('/proc'), initial=False):
    if type(pid) is not int or pid <= 0 or not re.fullmatch(r'[0-9]{1,20}', start):
        raise ValueError('invalid exit-watch process identity')
    if trial not in COMPARISON_ORDER:
        raise ValueError('invalid exit-watch trial')
    process = proc_root / str(pid)
    try:
        with (process / 'stat').open('rb') as stream:
            raw = stream.read(4097)
    except FileNotFoundError:
        return {'identity': 'absent', 'pid': pid, 'start_ticks': start}
    except OSError as error:
        return {'identity': 'unreadable', 'error': str(error)}
    if len(raw) > 4096:
        return {'identity': 'unreadable', 'error': 'stat exceeds cap'}
    try:
        fields = raw.decode().rsplit(') ', 1)[1].split()
        observed, state = fields[19], fields[0]
    except (UnicodeError, IndexError):
        return {'identity': 'unreadable', 'error': 'invalid stat'}
    row = {'identity': 'owned' if observed == start else 'identity-mismatch',
           'pid': pid, 'start_ticks': start, 'observed_start_ticks': observed,
           'state': state, 'stat': raw.decode()}
    if initial and row['identity'] == 'owned':
        try:
            exe = os.readlink(process / 'exe')
        except OSError as error:
            row.update(identity='unreadable', error=str(error))
            return row
        cmd = exit_read(process / 'cmdline', 16384)
        argv = cmd.get('text', '').split('\0')
        profiles = [arg.split('=', 1)[1] for arg in argv
                    if arg.startswith('--user-data-dir=')]
        row.update(exe=exe, cmdline=cmd)
        if (exe != str(BROWSER) or cmd.get('truncated') or
                not profiles or profiles[-1] != '/tmp/media-ab-' + trial):
            row['identity'] = 'launch-mismatch'
    return row


def exit_trace_state(trial, directory=Path('/tmp')):
    if trial not in COMPARISON_ORDER:
        raise ValueError('invalid exit trace trial')
    path = directory / ('media-ab-' + trial + '-trace.json')
    try:
        st = path.lstat()
        return {'path': str(path), 'exists': True,
                'regular': stat_module.S_ISREG(st.st_mode), 'bytes': st.st_size,
                'inode': st.st_ino, 'mtime_ns': st.st_mtime_ns,
                'ctime_ns': st.st_ctime_ns, 'content_read': False}
    except FileNotFoundError:
        return {'path': str(path), 'exists': False, 'content_read': False}
    except OSError as error:
        return {'path': str(path), 'error': str(error), 'content_read': False}


def exit_stat_start(value):
    try:
        return value['text'].rsplit(') ', 1)[1].split()[19]
    except (KeyError, IndexError):
        return None


EXIT_FIELDS = (('comm', 128), ('stat', 1024), ('status', 2048),
               ('schedstat', 512), ('wchan', 256), ('syscall', 1024), ('stack', 2048))
EXIT_PROC_SCOPE = ('Sampled xv6 procfs, not a stopped coherent snapshot. stack has synthetic '
                  'state/context/trapframe rows, not unwound frames; wchan is a data channel '
                  'address or zero, not a symbol. syscall raw rax may be a return value. '
                  'stat state is abbreviated; retain status long state. R includes runnable '
                  'off-CPU tasks; these fields do not expose on_cpu/on_rq.')


def exit_related(proc_root=Path('/proc')):
    """Selected process leaders only, with explicit scan/count/time caps."""
    deadline = time.monotonic() + 3
    row = {'process_cap': 32, 'scan_cap': 512, 'entries_seen': 0,
           'truncated': False, 'processes': [],
           'scope': 'Chromium executable and Kthread leaders; not all tasks or queue attribution'}
    try:
        with os.scandir(proc_root) as entries:
            for entry in entries:
                if not entry.name.isdigit():
                    continue
                if (row['entries_seen'] >= 512 or len(row['processes']) >= 32 or
                        time.monotonic() >= deadline):
                    row['truncated'] = True
                    break
                row['entries_seen'] += 1
                process = Path(entry.path)
                before = exit_read(process / 'stat', 1024)
                status = exit_read(process / 'status', 2048)
                try:
                    exe = os.readlink(process / 'exe')
                except OSError as error:
                    exe = {'error': str(error)}
                kernel = any(line.split(':', 1)[0] == 'Kthread' and
                             line.split(':', 1)[1].strip() == '1'
                             for line in status.get('text', '').splitlines() if ':' in line)
                if not kernel and exe != str(BROWSER):
                    continue
                item = {'pid': int(entry.name), 'exe': exe,
                        'role': 'kernel-thread-unattributed' if kernel else 'chromium',
                        **{name: exit_read(process / name, cap) for name, cap in EXIT_FIELDS}}
                item['cmdline'] = exit_read(process / 'cmdline', 4096)
                after = exit_read(process / 'stat', 1024)
                item['identity_stable'] = (exit_stat_start(before) is not None and
                                           exit_stat_start(before) == exit_stat_start(after))
                row['processes'].append(item)
    except OSError as error:
        row['error'] = str(error)
    return row


def exit_snapshot(pid, start, trial, proc_root=Path('/proc'), task_cap=128, seconds=8):
    began = time.monotonic_ns()
    deadline = time.monotonic() + seconds
    process = proc_root / str(pid)
    row = {'identity_before': exit_identity(pid, start, trial, proc_root),
           'guest_monotonic_start_ns': began, 'tasks': [],
           'task_cap': task_cap, 'tasks_truncated': False, 'procfs_scope': EXIT_PROC_SCOPE}
    if row['identity_before']['identity'] != 'owned':
        return row
    row['process'] = {name: exit_read(process / name, cap)
                      for name, cap in EXIT_FIELDS}
    row['process']['cmdline'] = exit_read(process / 'cmdline', 16384)
    try:
        with os.scandir(process / 'task') as entries:
            for entry in entries:
                if not entry.name.isdigit():
                    continue
                if len(row['tasks']) >= task_cap or time.monotonic() >= deadline:
                    row['tasks_truncated'] = True
                    break
                task = Path(entry.path)
                before = exit_read(task / 'stat', 1024)
                item = {'tid': int(entry.name),
                        **{name: exit_read(task / name, cap)
                           for name, cap in EXIT_FIELDS}}
                after = exit_read(task / 'stat', 1024)
                item['identity_stable'] = (exit_stat_start(before) is not None and
                                           exit_stat_start(before) == exit_stat_start(after))
                row['tasks'].append(item)
    except OSError as error:
        row['tasks_error'] = str(error)
    row['identity_after'] = exit_identity(pid, start, trial, proc_root)
    row['guest_monotonic_end_ns'] = time.monotonic_ns()
    return row


def exit_watch(pid, start, trial):
    began = time.monotonic()
    deadline = began + 95
    own_start = (Path('/proc') / str(os.getpid()) / 'stat').read_text().rsplit(') ', 1)[1].split()[19]
    common = {'trial': trial, 'pid': pid, 'start_ticks': start,
              'watcher_pid': os.getpid(), 'watcher_start_ticks': own_start,
              'watch_started_guest_monotonic_ns': time.monotonic_ns(),
              'snapshot_after_seconds': 35, 'watch_budget_seconds': 95}
    def report(event, **values):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('exit watcher report deadline')
        emit({'phase': 'exit-watch', **common, 'event': event,
              'elapsed_seconds': time.monotonic() - began, **values},
             timeout=min(2, remaining))
    initial = exit_identity(pid, start, trial, initial=True)
    report('ready', process=initial, trace_file=exit_trace_state(trial))
    if initial['identity'] != 'owned':
        raise RuntimeError('exit watcher initial ownership failed')
    sampled = False
    while time.monotonic() < deadline - 2:
        current = exit_identity(pid, start, trial)
        if current['identity'] != 'owned':
            report('terminal', process=current, child_exit_code=None,
                   trace_file=exit_trace_state(trial),
                   process_disappearance_observed=current['identity'] == 'absent')
            return
        if not sampled and time.monotonic() - began >= 35:
            snapshot = exit_snapshot(pid, start, trial)
            tasks = snapshot.pop('tasks')
            related = exit_related()
            processes = related.pop('processes')
            report('snapshot', snapshot=snapshot, task_count=len(tasks),
                   related=related, related_count=len(processes), trace_file=exit_trace_state(trial))
            for offset in range(0, len(processes), 2):
                report('snapshot-related', process_offset=offset,
                       processes=processes[offset:offset + 2])
            for offset in range(0, len(tasks), 4):
                report('snapshot-tasks', task_offset=offset,
                       tasks=tasks[offset:offset + 4])
            sampled = True
        time.sleep(.25)
    report('terminal', process=exit_identity(pid, start, trial),
           trace_file=exit_trace_state(trial),
           watch_deadline_reached=True, child_exit_code=None,
           process_disappearance_observed=False)


def values(pid, name):
    return (Path('/proc') / str(pid) / name).read_bytes().split(b'\0')


def env_of(pid):
    return dict(x.decode(errors='replace').split('=', 1)
                for x in values(pid, 'environ') if b'=' in x)


def upload_trace_file(target, trial, origin, endpoint):
    size = target.stat().st_size
    if not 0 < size <= 64*1024*1024:
        raise RuntimeError('export file exceeds 64 MiB cap')
    destination = urlsplit(origin)
    connection = http.client.HTTPConnection(destination.hostname, destination.port, timeout=25)
    digest = hashlib.sha256()
    count = 0
    deadline = time.monotonic()+30
    try:
        connection.putrequest('POST', endpoint+'?trial='+trial)
        connection.putheader('Content-Length',str(size))
        connection.putheader('Content-Type','application/gzip' if endpoint.endswith('-rejected') else 'application/json')
        connection.endheaders()
        with target.open('rb') as stream:
            while count < size:
                if time.monotonic()>deadline: raise TimeoutError('trace upload deadline')
                block=stream.read(min(65536,size-count))
                if not block: raise OSError('export file shortened')
                connection.send(block);digest.update(block);count+=len(block)
        response=connection.getresponse()
        receipt=json.loads(response.read(4096))
        if response.status!=200 or receipt.get('bytes')!=count or receipt.get('sha256')!=digest.hexdigest():
            raise RuntimeError('trace upload receipt mismatch')
        return receipt
    finally:
        connection.close()


def preserve_rejected_trace(target, trial, origin, st):
    if not stat_module.S_ISREG(st.st_mode) or not 0 < st.st_size <= 128*1024*1024:
        return {'preserved':False,'reason':'outside regular source (0,128 MiB] preservation gate'}
    archive=target.with_suffix(target.suffix+'.gz')
    deadline=time.monotonic()+30
    digest=hashlib.sha256();count=0
    with target.open('rb') as source, archive.open('xb') as destination:
        with gzip.GzipFile(fileobj=destination,mode='wb',compresslevel=1,mtime=0) as output:
            while count<st.st_size:
                if time.monotonic()>deadline: raise TimeoutError('rejected trace compression deadline')
                block=source.read(min(65536,st.st_size-count))
                if not block: raise OSError('rejected trace shortened')
                count+=len(block);digest.update(block);output.write(block)
                if destination.tell()>64*1024*1024: raise RuntimeError('rejected archive exceeds 64 MiB')
        if destination.tell()>64*1024*1024: raise RuntimeError('rejected archive exceeds 64 MiB')
    after=target.lstat()
    if (st.st_ino,st.st_size,st.st_mtime_ns)!=(after.st_ino,after.st_size,after.st_mtime_ns):
        raise RuntimeError('rejected trace changed during preservation')
    receipt=upload_trace_file(archive,trial,origin,'/trial-trace-rejected')
    return {'preserved':True,'accepted_for_analysis':False,'source_bytes':count,
            'source_sha256':digest.hexdigest(),'archive_path':str(archive),'archive':receipt}


def main():
    global ACTIVE_POLICY
    mode = sys.argv[1]
    if mode == 'policy':
        ACTIVE_POLICY = install_policy(sys.argv[2])
        emit({'phase': 'capture-policy', 'sha256': sys.argv[2]})
        return
    ACTIVE_POLICY = load_capture_policy()
    if mode == 'exit-watch':
        pid, start, trial = int(sys.argv[2]), sys.argv[3], sys.argv[4]
        exit_watch(pid, start, trial)
    elif mode == 'exit-state':
        pid, start, trial = int(sys.argv[2]), sys.argv[3], sys.argv[4]
        snapshot = exit_snapshot(pid, start, trial, task_cap=8, seconds=2)
        tasks = snapshot.pop('tasks')
        emit({'phase': 'exit-state', 'trial': trial, 'pid': pid,
              'start_ticks': start, 'snapshot': snapshot, 'task_count': len(tasks),
              'trace_file': exit_trace_state(trial)}, timeout=2)
        for offset in range(0, len(tasks), 4):
            emit({'phase': 'exit-state-tasks', 'trial': trial, 'pid': pid,
                  'start_ticks': start, 'task_offset': offset,
                  'tasks': tasks[offset:offset + 4]}, timeout=2)
    elif mode == 'launch':
        trial, origin = sys.argv[2:4]
        if trial not in ('off-1', 'on-1', 'on-2', 'off-2'):
            raise ValueError('unknown trial')
        args = ['/bin/wayland-chromium', '--user-data-dir=/tmp/media-ab-' + trial]
        args += trace_flags(trial, ACTIVE_POLICY)
        if ACTIVE_POLICY is not None:
            runtime_still_verified(ACTIVE_POLICY)
        args.append(origin + '/?trial=' + trial)
        environment = os.environ.copy()
        environment.pop('WAYLAND_DEBUG', None)
        environment.pop(POLICY_ENV, None)
        own_stat=(Path('/proc')/str(os.getpid())/'stat').read_text()
        emit({'phase':'launch','trial':trial,'pid':os.getpid(),'start_ticks':own_stat.rsplit(') ',1)[1].split()[19],
              'requested_wrapper_argv':args,'url':args[-1]})
        os.execve(args[0], args, environment)
    elif mode == 'stage-fb':
        origin=sys.argv[2]
        with urllib.request.urlopen(origin+'/fb-pacing-snapshot',timeout=10) as response: data=response.read(65537)
        if len(data)>65536 or hashlib.sha256(data).hexdigest()!='96bdaf9c81eba3d742330e25e47953c78c856abc094eae872c0efc9b0cc05f59':
            raise RuntimeError('FB helper identity mismatch')
        target=Path('/tmp/xv6-fb-pacing-snapshot')
        target.write_bytes(data);target.chmod(0o755)
        emit({'phase':'stage-fb','sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)})
    elif mode == 'telemetry':
        trial, phase, origin = sys.argv[2:5]
        if trial not in ('off-1','on-1','on-2','off-2') or phase not in ('before','after'):
            raise ValueError('invalid telemetry identity')
        started = time.monotonic_ns()
        row = {'trial':trial, 'phase':phase, 'guest_monotonic_start_ns':started,
               'guest_wall_start_ns':time.time_ns(), 'processes':[], 'global':{}}
        def bounded_read(path, cap=16384):
            try:
                with path.open('rb') as stream: data=stream.read(cap+1)
                if len(data)>cap: return {'error':'exceeds cap','cap':cap}
                return data.decode(errors='replace')
            except OSError as e: return {'error':str(e)}
        for name in ('stat','uptime','loadavg','interrupts'):
            row['global'][name] = bounded_read(Path('/proc')/name, 65536)
        entries=[x for x in Path('/proc').iterdir() if x.name.isdigit()]
        if len(entries)>4096: raise RuntimeError('process inventory exceeds cap')
        row['selection']={'policy':'exe identity with argv role; Kthread status includes unnamed kernel workers',
                          'entries_seen':len(entries),'excluded':[],'errors':[]}
        def start_ticks(value):
            if not isinstance(value,str): return None
            try: return value.rsplit(') ',1)[1].split()[19]
            except (IndexError,ValueError): return None
        tasks_seen=0
        for entry in sorted(entries,key=lambda item:int(item.name)):
            cmd=bounded_read(entry/'cmdline',16384)
            argv=[arg for arg in cmd.split('\0') if arg] if isinstance(cmd,str) else []
            before=bounded_read(entry/'stat')
            status=bounded_read(entry/'status')
            try: exe=os.readlink(str(entry/'exe'))
            except OSError as error: exe={'error':str(error)}
            kernel_thread=isinstance(status,str) and any(
                line.split(':',1)[0]=='Kthread' and line.split(':',1)[1].strip()=='1'
                for line in status.splitlines() if ':' in line)
            executable_name=Path(exe).name if isinstance(exe,str) else ''
            argv_name=Path(argv[0]).name if argv else ''
            chromium=exe==str(BROWSER) or (not isinstance(exe,str) and argv_name in ('chrome','chromium'))
            kwin=executable_name=='kwin_wayland' or (not isinstance(exe,str) and argv_name=='kwin_wayland')
            role='kernel-thread-unattributed' if kernel_thread else 'kwin' if kwin else None
            if chromium:
                types=[arg.split('=',1)[1] for arg in argv if arg.startswith('--type=')]
                role='chromium-'+(types[-1] if types else 'browser')
            identity={'pid':int(entry.name),'exe':exe,'argv':argv,'start_ticks':start_ticks(before)}
            for name,value in [('cmdline',cmd),('stat',before),('status',status),('exe',exe)]:
                if isinstance(value,dict): row['selection']['errors'].append({'pid':int(entry.name),'field':name,**value})
            if role is None:
                row['selection']['excluded'].append({**identity,'reason':'outside Chromium, KWin and Kthread set'})
                continue
            process={**identity,'role':role,'status':status,'stat':before,'tasks':[],
                     'identity_basis':'exe' if isinstance(exe,str) else 'argv-fallback',
                     'kernel_worker_mapping':'queue identity unavailable in procfs' if kernel_thread else None}
            try:
                tasks=sorted((task for task in (entry/'task').iterdir() if task.name.isdigit()),key=lambda item:int(item.name))
            except OSError as error:
                process['tasks_error']=str(error);tasks=[]
            for task in tasks:
                tasks_seen+=1
                if tasks_seen>2048: raise RuntimeError('task inventory exceeds cap')
                task_stat=bounded_read(task/'stat')
                task_row={'tid':int(task.name),'start_ticks':start_ticks(task_stat),
                          'status':bounded_read(task/'status'),'stat':task_stat,
                          'schedstat':bounded_read(task/'schedstat')}
                task_after=bounded_read(task/'stat')
                task_row['identity_stable']=start_ticks(task_stat) is not None and start_ticks(task_stat)==start_ticks(task_after)
                process['tasks'].append(task_row)
            after=bounded_read(entry/'stat')
            try: exe_after=os.readlink(str(entry/'exe'))
            except OSError as error: exe_after={'error':str(error)}
            process['exe_after']=exe_after
            process['identity_stable']=start_ticks(before) is not None and start_ticks(before)==start_ticks(after) and exe==exe_after
            row['processes'].append(process)
        row['selection']['selected_roles']={}
        for process in row['processes']:
            role=process['role']
            row['selection']['selected_roles'][role]=row['selection']['selected_roles'].get(role,0)+1
        result=subprocess.run(['/tmp/xv6-fb-pacing-snapshot','--snapshot','59253eb271555b6e1e3e035280002d79a7fc82f3e236b288ca981a0e495ed104','f463248234ae9a83417b0557826a3f3c70a1c36fec447c2f8b6ed18ca0b40aff'],capture_output=True,text=True,timeout=10)
        row['fb_snapshot']={'exit':result.returncode,'stderr':result.stderr[:4096]}
        if result.returncode==0:
            row['fb_snapshot']['payload']=json.loads(result.stdout)
        else:
            row['fb_snapshot']['stdout']=result.stdout[:4096]
        row['guest_monotonic_end_ns']=time.monotonic_ns()
        body=json.dumps(row).encode()
        if not row['processes'] or len(body)>4*1024*1024: raise RuntimeError('telemetry empty or exceeds cap')
        target=Path('/tmp/media-ab-'+trial+'-'+phase+'-telemetry.json')
        target.write_bytes(body)
        request=urllib.request.Request(origin+'/trial-diag?trial='+trial+'&phase='+phase,
                                       data=body,method='POST',headers={'Content-Type':'application/json'})
        with urllib.request.urlopen(request,timeout=20) as response: receipt=json.load(response)
        if receipt.get('sha256')!=hashlib.sha256(body).hexdigest() or receipt.get('bytes')!=len(body):
            raise RuntimeError('telemetry upload mismatch')
        emit({'phase':'telemetry','trial':trial,'boundary':phase,'processes':len(row['processes']),
              'tasks':tasks_seen,'roles':row['selection']['selected_roles'],'bytes':len(body),'duration_ns':row['guest_monotonic_end_ns']-started,
              'upload':receipt})
    elif mode == 'trace-end':
        trial, origin = sys.argv[2:4]
        if trial not in ('on-1', 'on-2'):
            raise ValueError('unknown traced trial')
        target = Path('/tmp/media-ab-' + trial + '-trace.json')
        try: st = target.lstat()
        except OSError as error:
            emit({'phase':'trace-rejected','trial':trial,'path':str(target),'error_type':type(error).__name__,'error':str(error)})
            return
        if not stat_module.S_ISREG(st.st_mode) or not 0 < st.st_size <= 64*1024*1024:
            rejected={'phase':'trace-rejected','trial':trial,'path':str(target),'bytes':st.st_size,'mode':st.st_mode,
                      'regular':stat_module.S_ISREG(st.st_mode),'cap_bytes':64*1024*1024,
                      'error_type':'TraceAdmissionError','error':'trace empty, nonregular or exceeds 64 MiB'}
            try: rejected['preservation']=preserve_rejected_trace(target,trial,origin,st)
            except Exception as error:
                rejected['preservation']={'preserved':False,'error_type':type(error).__name__,'error':str(error)}
            emit(rejected)
            return
        receipt=upload_trace_file(target,trial,origin,'/trial-trace')
        emit({'phase':'trace-end','trial':trial,'path':str(target),**receipt,'upload':receipt})
    elif mode == 'binary':
        if ACTIVE_POLICY is not None:
            runtime = verify_runtime(ACTIVE_POLICY)
            emit({'phase': 'browser-binary', 'path': str(BROWSER),
                  'bytes': ACTIVE_POLICY['browser_bytes'], 'chunk_bytes': 1024 * 1024,
                  'sha256': ACTIVE_POLICY['browser_sha256'],
                  'expected_sha256': ACTIVE_POLICY['browser_sha256'],
                  'stable_during_hash': True, 'matched': True, **runtime})
            return
        before = BROWSER.stat()
        digest = hashlib.sha256()
        count = 0
        with BROWSER.open('rb') as stream:
            while True:
                block = stream.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
                count += len(block)
        after = BROWSER.stat()
        stable = (before.st_ino, before.st_size, before.st_mtime_ns) == (after.st_ino, after.st_size, after.st_mtime_ns)
        matched = stable and count == before.st_size and digest.hexdigest() == BROWSER_SHA
        emit({'phase': 'browser-binary', 'path': str(BROWSER), 'bytes': count,
              'chunk_bytes': 1024 * 1024, 'sha256': digest.hexdigest(),
              'expected_sha256': BROWSER_SHA, 'stable_during_hash': stable, 'matched': matched})
        if not matched:
            raise RuntimeError('protected-image browser binary identity mismatch')
    elif mode == 'session':
        deadline = time.monotonic() + 30
        checks = {}
        while time.monotonic() < deadline:
            matches = {'plasmashell': [], 'kwin_wayland': []}
            for entry in Path('/proc').iterdir():
                if not entry.name.isdigit():
                    continue
                try:
                    argv = [x.decode(errors='replace') for x in values(entry.name, 'cmdline') if x]
                    name = Path(argv[0]).name if argv else ''
                    if name in matches:
                        matches[name].append(int(entry.name))
                except (OSError, ValueError):
                    continue
            checks = {name + '_singleton': len(pids) == 1 for name, pids in matches.items()}
            environment = {}
            if checks['plasmashell_singleton']:
                try:
                    environment = env_of(matches['plasmashell'][0])
                except OSError:
                    pass
            runtime = environment.get('XDG_RUNTIME_DIR', '')
            display = environment.get('WAYLAND_DISPLAY', '')
            pulse = environment.get('PULSE_SERVER', '')
            checks.update(wayland_environment=bool(runtime and display),
                          virgl_environment=environment.get('GALLIUM_DRIVER') == 'virgl',
                          normal_pulse=pulse == 'unix:/dev/shm/xdg-runtime-root/pulse/native')
            for name, path in [('wayland_socket', str(Path(runtime) / display) if runtime and display else ''),
                               ('pulse_socket', pulse[5:] if pulse.startswith('unix:') else '')]:
                try:
                    checks[name] = bool(path) and stat_module.S_ISSOCK(Path(path).stat().st_mode)
                except OSError:
                    checks[name] = False
            if all(checks.values()):
                break
            time.sleep(.25)
        else:
            emit({'phase': 'session-not-ready', 'checks': checks})
            raise RuntimeError('KDE process/socket readiness deadline')
        if any('\n' in k or '\n' in v for k, v in environment.items()):
            raise RuntimeError('multiline session environment cannot be exported safely')
        Path('/tmp/media-ab-session.env').write_text(''.join(k + '=' + v + '\n' for k, v in environment.items()))
        emit({'phase': 'session', 'pid': matches['plasmashell'][0],
              'kwin_pid': matches['kwin_wayland'][0], 'checks': checks,
              'readiness_limit': 'live process and socket readiness; first mapped fixture/geometry confirms browser surface readiness',
              'environment': {k: environment.get(k) for k in
                              ('WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'GALLIUM_DRIVER', 'PULSE_SERVER')}})
    elif mode == 'cursor':
        trial = sys.argv[2]
        if Path('/tmp/media-ab-' + trial).exists():
            raise RuntimeError('fresh browser profile already exists')
        row = {'phase': 'cursor', 'trial': trial, 'profile_absent': True, 'path': str(LOG),
               'bytes': LOG.stat().st_size if LOG.exists() else 0}
        Path('/tmp/media-ab-' + trial + '-log-start.json').write_text(json.dumps(row))
        emit(row)
    elif mode == 'owner':
        pid, trial, expected = int(sys.argv[2]), sys.argv[3], sys.argv[4]
        argv = [x.decode(errors='replace') for x in values(pid, 'cmdline') if x]
        environment = env_of(pid)
        stat = (Path('/proc') / str(pid) / 'stat').read_text()
        profile = '/tmp/media-ab-' + trial
        profiles = [x.split('=', 1)[1] for x in argv if x.startswith('--user-data-dir=')]
        checks = {'chrome_exec': bool(argv) and argv[0] == str(BROWSER),
                  'profile_last_override': bool(profiles) and profiles[-1] == profile,
                  'multiprocess': environment.get('WAYLAND_CHROMIUM_MULTIPROCESS') == '1',
                  'trace': environment.get('WAYLAND_DEBUG', '') == ('client' if expected == 'on' else ''),
                  'wayland': environment.get('WAYLAND_DISPLAY') == 'wayland-0',
                  'virgl_policy': environment.get('GALLIUM_DRIVER') == 'virgl',
                  'normal_pulse': environment.get('PULSE_SERVER') == 'unix:/dev/shm/xdg-runtime-root/pulse/native'}
        if ACTIVE_POLICY is not None:
            runtime_still_verified(ACTIVE_POLICY)
            checks['diagnostic_exe'] = (Path('/proc') / str(pid) / 'exe').resolve(strict=True) == BROWSER
            checks['helper_policy_env_absent'] = POLICY_ENV not in environment
        row = {'phase': 'owner', 'trial': trial, 'pid': pid,
               'start_ticks': stat.rsplit(') ', 1)[1].split()[19],
               'argv': argv, 'expected_trace': expected,
               'checks': checks,
               'environment': {k: environment.get(k) for k in PARITY_ENV},
               'log_bytes': LOG.stat().st_size if LOG.exists() else 0}
        emit(row)
        if not all(checks.values()):
            raise RuntimeError('owner policy mismatch')
    elif mode == 'log-end':
        trial = sys.argv[2]
        begin = json.loads(Path('/tmp/media-ab-' + trial + '-log-start.json').read_text())
        end = LOG.stat().st_size if LOG.exists() else 0
        if end < begin['bytes']:
            raise RuntimeError('canonical log shrank')
        span = end - begin['bytes']
        # Bounded metadata/hash after the direct child exits, outside measurement.
        if span > 8 * 1024 * 1024:
            raise RuntimeError('trial log exceeds capture cap')
        with LOG.open('rb') as stream:
            stream.seek(begin['bytes'])
            body = stream.read(span)
        target = Path('/tmp/media-ab-' + trial + '-stderr.log')
        target.write_bytes(body)
        if len(sys.argv) > 3:
            request = urllib.request.Request(sys.argv[3] + '/trial-log?trial=' + trial,
                                             data=body, method='POST', headers={
                                                 'Content-Type': 'application/octet-stream',
                                                 'X-Source-Offset': str(begin['bytes'])})
            with urllib.request.urlopen(request, timeout=10) as response:
                receipt = json.load(response)
            if receipt.get('bytes') != len(body) or receipt.get('sha256') != hashlib.sha256(body).hexdigest():
                raise RuntimeError('log upload hash/length mismatch')
        emit({'phase': 'log-end', 'trial': trial, 'source': str(LOG),
              'source_offset': begin['bytes'], 'source_end': end, 'bytes': len(body),
              'sha256': hashlib.sha256(body).hexdigest(), 'saved': str(target)})
    else:
        raise ValueError('unknown inspection mode')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        emit({'phase': 'error', 'error': str(error)})
        raise SystemExit(1)
