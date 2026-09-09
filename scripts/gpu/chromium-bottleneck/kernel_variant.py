"""Sealed kernel-only capture selection. No build, staging, or VM operations."""
from pathlib import Path
import re

import capture_config as config

MAX_KERNEL_BYTES = 256 * 1024**2
IDENTITY_FIELDS = ('variant', 'kernel_path', 'kernel_sha256', 'kernel_bytes',
                   'source_commit', 'source_patch_sha256')
RECEIPT_FIELDS = {'schema_version', 'kind', *IDENTITY_FIELDS,
                  'build_receipt_path', 'build_receipt_sha256'}


def load_receipt(path):
    path = config.regular_path(str(path))
    document, raw, sha = config.read_document(path)
    if set(document) != RECEIPT_FIELDS:
        raise ValueError('kernel variant receipt has missing or unsupported fields')
    if (type(document['schema_version']) is not int or document['schema_version'] != 1 or
            document['kind'] != 'chromium-kernel-bottleneck-variant'):
        raise ValueError('invalid kernel variant receipt schema/kind')
    if document['variant'] not in ('baseline', 'candidate'):
        raise ValueError('invalid kernel variant name')
    if (not isinstance(document['source_commit'], str) or
            re.fullmatch('[0-9a-f]{40}', document['source_commit']) is None):
        raise ValueError('invalid kernel source commit')
    for field in ('kernel_sha256', 'source_patch_sha256', 'build_receipt_sha256'):
        if not config.owner.sha256_value(document[field]):
            raise ValueError('invalid kernel identity hash: ' + field)
    size = document['kernel_bytes']
    if type(size) is not int or not 0 < size <= MAX_KERNEL_BYTES:
        raise ValueError('invalid kernel size')

    kernel = config.regular_path(document['kernel_path'])
    build_path = config.regular_path(document['build_receipt_path'])
    build, build_raw, build_sha = config.read_document(build_path)
    if build_sha != document['build_receipt_sha256']:
        raise ValueError('kernel build receipt SHA mismatch')
    if (type(build.get('schema_version')) is not int or build['schema_version'] != 1 or
            build.get('kind') != 'workqueue-kernel-build-receipt'):
        raise ValueError('invalid kernel build receipt schema/kind')
    stage = build.get('build_stage')
    if (build.get('build_passed') is not True or not isinstance(stage, dict) or
            type(stage.get('exit_code')) is not int or stage['exit_code'] != 0 or
            stage.get('remaining_group_members') != [] or stage.get('error') is not None):
        raise ValueError('kernel build receipt does not prove successful reaped completion')
    for field in IDENTITY_FIELDS:
        if type(build.get(field)) is not type(document[field]) or build[field] != document[field]:
            raise ValueError('kernel build receipt identity mismatch: ' + field)

    # Rehash the small JSON files to seal stamps to the exact bytes parsed.
    inputs = [config.hash_file(path, sha, config.owner.MAX_POLICY_BYTES, len(raw)),
              config.hash_file(build_path, build_sha, config.owner.MAX_POLICY_BYTES, len(build_raw)),
              config.hash_file(kernel, document['kernel_sha256'], MAX_KERNEL_BYTES, size)]
    identity = {field: document[field] for field in IDENTITY_FIELDS}
    identity.update(receipt_path=str(path), receipt_sha256=sha,
                    build_receipt_path=str(build_path), build_receipt_sha256=build_sha)
    return {'kernel': kernel, 'identity': identity, 'receipt_raw': raw,
            'build_receipt_raw': build_raw,
            'preflight': {'identity': identity, 'inputs': inputs}}


def check_environment(loaded, environ):
    for field, expected in [('GL_AUDIT_KERNEL', str(loaded['kernel'])),
                            ('GL_AUDIT_KERNEL_SHA256', loaded['identity']['kernel_sha256'])]:
        if environ.get(field) and environ[field] != expected:
            raise ValueError('kernel variant conflicts with ' + field)


def check_prelaunch(loaded):
    for row in loaded['preflight']['inputs']:
        current = config.owner.file_stamp(config.owner.runtime_regular_file(Path(row['path'])))
        if current != row['stamp']:
            raise ValueError('kernel variant input changed after preflight: ' + row['path'])
    return True
