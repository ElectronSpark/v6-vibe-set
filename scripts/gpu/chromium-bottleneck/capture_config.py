"""Explicit diagnostic capture preflight. This module never launches a VM."""
import hashlib
import importlib.util
import json
import os
import re
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('chromium_capture_owner', HERE / 'owner-helper.py')
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)
PIN = HERE.parent / 'chromium-diagnostics' / 'upstream-sources.json'
PROTECTED_BASE = Path('/home/es/xv6-os/build-x86_64/gui-progress-audit/audio-passcred-baseline-20260907T182610Z/fs.img')
MAX_IMAGE_BYTES = 64 * 1024**3


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key: ' + key)
        result[key] = value
    return result


def regular_path(value):
    if not isinstance(value, str) or re.search(r'[\x00-\x1f\x7f]', value):
        raise ValueError('invalid artifact path')
    path = Path(value)
    if not path.is_absolute() or '..' in path.parts or str(path) != value:
        raise ValueError('artifact path must be canonical and absolute')
    owner.runtime_regular_file(path)
    return path


def read_document(path):
    path = regular_path(str(path))
    before = owner.runtime_regular_file(path)
    if before.st_size > owner.MAX_POLICY_BYTES:
        raise ValueError('JSON artifact exceeds cap')
    with path.open('rb') as stream:
        raw = stream.read(owner.MAX_POLICY_BYTES + 1)
    if len(raw) > owner.MAX_POLICY_BYTES or owner.file_stamp(before) != owner.file_stamp(owner.runtime_regular_file(path)):
        raise ValueError('JSON artifact changed or exceeds cap')
    document = json.loads(raw, object_pairs_hook=unique_object,
                          parse_constant=lambda value: (_ for _ in ()).throw(ValueError('nonfinite JSON')))
    if not isinstance(document, dict):
        raise ValueError('JSON document must be an object')
    return document, raw, hashlib.sha256(raw).hexdigest()


def hash_file(path, expected, cap, size=None):
    if not owner.sha256_value(expected):
        raise ValueError('invalid expected artifact SHA256')
    before = owner.runtime_regular_file(path)
    if before.st_size <= 0 or before.st_size > cap or (size is not None and before.st_size != size):
        raise ValueError('artifact size outside gate: ' + str(path))
    digest = hashlib.sha256()
    count = 0
    deadline = time.monotonic() + 600
    with path.open('rb') as stream:
        if owner.file_stamp(os.fstat(stream.fileno())) != owner.file_stamp(before):
            raise ValueError('artifact changed before hashing')
        while count < before.st_size:
            if time.monotonic() > deadline:
                raise ValueError('artifact hashing deadline')
            block = stream.read(min(1024 * 1024, before.st_size - count))
            if not block:
                raise ValueError('artifact shortened during hashing')
            digest.update(block)
            count += len(block)
    if owner.file_stamp(before) != owner.file_stamp(owner.runtime_regular_file(path)) or digest.hexdigest() != expected:
        raise ValueError('artifact hash/stability mismatch: ' + str(path))
    return {'path': str(path), 'bytes': count, 'sha256': expected,
            'stamp': owner.file_stamp(before), 'chunk_bytes': 1024 * 1024}


def require_schema(document, kind, value):
    if type(document.get('schema_version')) is not int or document['schema_version'] != 1 or document.get(kind) != value:
        raise ValueError('invalid diagnostic document schema/kind')


def load_diagnostic(path, token, protected_base=PROTECTED_BASE, pin_path=PIN, capture_purpose='comparison'):
    """Hash concrete staged inputs before launch and return a sealed guest policy."""
    selected_order = [trial for trial, _ in owner.capture_trials(capture_purpose)]
    config, config_raw, config_sha = read_document(Path(path).absolute())
    require_schema(config, 'arm', 'chromium-bottleneck-diagnostic')
    pin, pin_raw, pin_sha = read_document(pin_path)
    patch_name = pin.get('patch_file')
    if not isinstance(patch_name, str) or Path(patch_name).name != patch_name:
        raise ValueError('unsafe pinned patch name')
    patch = hash_file(pin_path.parent / patch_name, pin.get('patch_sha256'), 4 * 1024**2)
    commit = pin.get('chromium_commit')
    if not isinstance(commit, str) or not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('missing exact source commit pin')
    for key, expected in [('source_commit', commit), ('patch_sha256', patch['sha256'])]:
        if config.get(key) != expected:
            raise ValueError('capture config differs from pinned ' + key)
    manifest_path = regular_path(config.get('runtime_manifest_path'))
    manifest, manifest_raw, manifest_sha = read_document(manifest_path)
    if config.get('runtime_manifest_sha256') != manifest_sha:
        raise ValueError('runtime manifest SHA mismatch')
    require_schema(manifest, 'kind', 'chromium-bottleneck-diagnostic-runtime')
    if manifest.get('status') != 'staged':
        raise ValueError('runtime manifest is not a completed staging receipt')
    for key in ('source_commit', 'patch_sha256', 'rootfs_sha256'):
        if manifest.get(key) != config.get(key):
            raise ValueError('runtime manifest/config mismatch: ' + key)
    policy = {'schema_version': 1, 'mode': 'diagnostic', 'arm': config['arm'],
              'run_token': token, 'config_sha256': config_sha,
              'capture_purpose': capture_purpose, 'selected_order': selected_order,
              'runtime_manifest_sha256': manifest_sha}
    for key in ('source_commit', 'patch_sha256', 'rootfs_sha256', 'browser_path',
                'browser_sha256', 'browser_bytes', 'runtime_files'):
        policy[key] = manifest.get(key)
    policy['build_provenance'] = manifest.get('build_provenance', {})
    owner.validate_guest_policy(policy)
    encoded = owner.policy_bytes(policy)
    image = regular_path(config.get('rootfs_path'))
    protected = owner.runtime_regular_file(protected_base)
    image_stat = owner.runtime_regular_file(image)
    if (image_stat.st_dev, image_stat.st_ino) == (protected.st_dev, protected.st_ino):
        raise ValueError('diagnostic image must be separate from protected baseline')
    browser = regular_path(manifest.get('host_browser_path'))
    if browser.name != owner.BROWSER.name:
        raise ValueError('host runtime must contain fixed browser basename')
    with browser.open('rb') as stream:
        elf = stream.read(64)
    if len(elf) != 64 or elf[:6] != b'\x7fELF\x02\x01' or elf[18:20] != b'\x3e\x00' or not browser.stat().st_mode & 0o111:
        raise ValueError('host browser is not executable x86_64 ELF')
    host_files = []
    for row in policy['runtime_files']:
        suffix = Path(row['guest_path']).relative_to(owner.BROWSER.parent)
        runtime_path = browser.parent / suffix
        # Empty runtime data files are valid; hash_file otherwise rejects empty artifacts.
        if row['bytes'] == 0:
            st = owner.runtime_regular_file(runtime_path)
            if st.st_size or row['sha256'] != hashlib.sha256(b'').hexdigest():
                raise ValueError('empty runtime file mismatch')
            host_files.append({'path': str(runtime_path), 'bytes': 0, 'sha256': row['sha256'],
                               'stamp': owner.file_stamp(st), 'chunk_bytes': 1024 * 1024})
        else:
            host_files.append(hash_file(runtime_path, row['sha256'], owner.MAX_RUNTIME_FILE_BYTES, row['bytes']))
    image_receipt = hash_file(image, policy['rootfs_sha256'], MAX_IMAGE_BYTES)
    return {'image': image, 'policy': policy, 'policy_bytes': encoded,
            'policy_sha256': hashlib.sha256(encoded).hexdigest(),
            'config_raw': config_raw, 'manifest_raw': manifest_raw, 'pin_raw': pin_raw,
            'receipt': {'config_path': str(Path(path).absolute()), 'config_sha256': config_sha,
                        'runtime_manifest_path': str(manifest_path), 'runtime_manifest_sha256': manifest_sha,
                        'pin_path': str(pin_path), 'pin_sha256': pin_sha, 'patch': patch,
                        'image': image_receipt, 'host_runtime_files': host_files,
                        'protected_base': {'path': str(protected_base), 'stamp': owner.file_stamp(protected)},
                        'capture_identity': owner.capture_identity(policy),
                        'controls': owner.capture_controls(capture_purpose)}}


def capture_summary(trials, purpose):
    selected = [trial for trial, _ in owner.capture_trials(purpose)]
    complete = [row['trial'] for row in trials] == selected
    usable = complete and all(row['measurement_usable'] for row in trials)
    return {'capture_purpose': purpose, 'selected_order': selected,
            'completed_trials': len(trials), 'usable_trials': sum(row['measurement_usable'] for row in trials),
            'legacy_statuses': {row['trial']: row['legacy_status'] for row in trials},
            'accepted_trace_exports': sum(row.get('trace_export') == 'accepted' for row in trials),
            'valid_comparison': purpose == 'comparison' and usable,
            'performance_credit': 'none' if purpose == 'emission-smoke' else 'requires comparison analysis',
            'emission_smoke_capture_ready': purpose == 'emission-smoke' and usable and trials[0].get('trace_export') == 'accepted',
            'emission_validation': 'requires event coverage and loss analysis' if purpose == 'emission-smoke' else 'not this capture purpose'}


def check_prelaunch_stamps(loaded):
    """Catch changed inputs between streamed verification and QEMU launch."""
    receipt = loaded['receipt']
    for row in [receipt['image'], receipt['protected_base'], *receipt['host_runtime_files']]:
        if owner.file_stamp(owner.runtime_regular_file(Path(row['path']))) != row['stamp']:
            raise ValueError('diagnostic input changed after preflight: ' + row['path'])
