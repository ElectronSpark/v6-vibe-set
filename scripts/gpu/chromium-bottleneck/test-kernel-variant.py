#!/usr/bin/env python3
"""Tiny kernel-receipt fixtures only; never builds or launches a VM."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import kernel_variant as variant


def sha(data):
    return hashlib.sha256(data).hexdigest()


class KernelVariantTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.kernel = self.root / 'xv6.bin'
        self.kernel.write_bytes(b'tiny test kernel')
        self.build_path = self.root / 'build-receipt.json'
        self.path = self.root / 'variant.json'
        self.build = {'schema_version': 1, 'kind': 'workqueue-kernel-build-receipt',
                      'variant': 'candidate', 'kernel_path': str(self.kernel),
                      'kernel_sha256': sha(self.kernel.read_bytes()),
                      'kernel_bytes': self.kernel.stat().st_size,
                      'source_commit': 'a' * 40, 'source_patch_sha256': sha(b'patch'),
                      'build_passed': True,
                      'build_stage': {'command': ['tiny fixture only'], 'exit_code': 0,
                                      'remaining_group_members': [], 'error': None}}
        self.document = {'schema_version': 1, 'kind': 'chromium-kernel-bottleneck-variant',
                         **{key: self.build[key] for key in variant.IDENTITY_FIELDS},
                         'build_receipt_path': str(self.build_path)}
        self.write()

    def write(self):
        self.build_path.write_text(json.dumps(self.build))
        self.document['build_receipt_sha256'] = sha(self.build_path.read_bytes())
        self.path.write_text(json.dumps(self.document))

    def load(self):
        return variant.load_receipt(self.path)

    def controller(self, *args):
        env = os.environ.copy()
        for key in ('GL_AUDIT_KERNEL', 'GL_AUDIT_KERNEL_SHA256'):
            env.pop(key, None)
        return subprocess.run([sys.executable, str(Path(variant.__file__).with_name('controller.py')),
                               *args], capture_output=True, text=True, env=env, timeout=10)

    def test_verified_identity_and_retained_bytes(self):
        loaded = self.load()
        self.assertEqual(loaded['kernel'], self.kernel)
        self.assertEqual(loaded['identity']['receipt_sha256'], sha(self.path.read_bytes()))
        self.assertEqual(loaded['build_receipt_raw'], self.build_path.read_bytes())
        self.assertEqual(loaded['receipt_raw'], self.path.read_bytes())
        self.assertEqual(len(loaded['preflight']['inputs']), 3)
        variant.check_prelaunch(loaded)

    def test_baseline_and_candidate(self):
        for name in ('baseline', 'candidate'):
            self.build['variant'] = self.document['variant'] = name
            self.write()
            self.assertEqual(self.load()['identity']['variant'], name)

    def test_missing_and_unsupported_override_fields(self):
        for field in ('browser_path', 'rootfs_path', 'args', 'kernel_extra'):
            with self.subTest(field=field):
                original = copy.deepcopy(self.document)
                self.document[field] = '/some/override'
                self.write()
                with self.assertRaises(ValueError): self.load()
                self.document = original
        del self.document['kernel_bytes']
        self.write()
        with self.assertRaises(ValueError): self.load()

    def test_each_build_identity_must_match(self):
        for field in variant.IDENTITY_FIELDS:
            with self.subTest(field=field):
                old = self.build[field]
                self.build[field] = old + 1 if isinstance(old, int) else old + 'x'
                self.write()
                with self.assertRaisesRegex(ValueError, 'identity mismatch'): self.load()
                self.build[field] = old

    def test_invalid_schemas_hashes_and_types(self):
        invalid = [('schema_version', True), ('kind', 'other'), ('variant', 'arbitrary'),
                   ('source_commit', 'HEAD'), ('source_patch_sha256', 'todo'),
                   ('kernel_sha256', 'x' * 64), ('kernel_bytes', True), ('kernel_bytes', 0)]
        for field, value in invalid:
            with self.subTest(field=field, value=value):
                old = self.document[field]
                self.document[field] = value
                self.write()
                with self.assertRaises(ValueError): self.load()
                self.document[field] = old
        self.build['kind'] = 'unfinished-build'
        self.write()
        with self.assertRaises(ValueError): self.load()

    def test_build_receipt_hash_mismatch(self):
        self.build_path.write_text(self.build_path.read_text() + ' ')
        with self.assertRaisesRegex(ValueError, 'SHA mismatch'): self.load()

    def test_build_must_complete_successfully_with_no_remaining_children(self):
        original = copy.deepcopy(self.build)
        for value in (False, 1, None):
            self.build = copy.deepcopy(original)
            self.build['build_passed'] = value
            self.write()
            with self.assertRaisesRegex(ValueError, 'successful reaped completion'): self.load()
        for field, value in [('exit_code', 7), ('exit_code', True), ('exit_code', None),
                             ('remaining_group_members', [123]), ('remaining_group_members', None),
                             ('error', 'build timeout')]:
            self.build = copy.deepcopy(original)
            self.build['build_stage'][field] = value
            self.write()
            with self.assertRaisesRegex(ValueError, 'successful reaped completion'): self.load()
        self.build = copy.deepcopy(original)
        del self.build['build_stage']
        self.write()
        with self.assertRaisesRegex(ValueError, 'successful reaped completion'): self.load()

    def test_kernel_hash_and_size_mismatch(self):
        self.kernel.write_bytes(b'wrong tiny data')
        with self.assertRaises(ValueError): self.load()

    def test_caps_before_large_reads(self):
        with patch.object(variant, 'MAX_KERNEL_BYTES', 4):
            with self.assertRaisesRegex(ValueError, 'size'): self.load()
        with patch.object(variant.config.owner, 'MAX_POLICY_BYTES', 16):
            with self.assertRaisesRegex(ValueError, 'cap'): self.load()

    def test_absolute_canonical_and_regular_paths(self):
        with self.assertRaises(ValueError): variant.load_receipt(Path('relative.json'))
        with self.assertRaises(ValueError): variant.load_receipt(str(self.root) + '/../variant.json')
        with self.assertRaises(ValueError): variant.load_receipt(self.root)

    def test_symlink_file_and_parent(self):
        link = self.root / 'link.json'
        link.symlink_to(self.path)
        with self.assertRaises(ValueError): variant.load_receipt(link)
        link.unlink()
        link.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError): variant.load_receipt(link / self.path.name)

    def test_duplicate_json_keys(self):
        self.path.write_text(self.path.read_text()[:-1] + ',"variant":"baseline"}')
        with self.assertRaisesRegex(ValueError, 'duplicate'): self.load()

    def test_changed_inputs_fail_prelaunch(self):
        for path in (self.kernel, self.path, self.build_path):
            with self.subTest(path=path):
                loaded = self.load()
                original = path.read_bytes()
                before = path.stat()
                path.write_bytes(bytes([original[0] ^ 1]) + original[1:])
                os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
                with self.assertRaisesRegex(ValueError, 'changed after preflight'):
                    variant.check_prelaunch(loaded)
                path.write_bytes(original)

    def test_environment_cannot_override_variant(self):
        loaded = self.load()
        variant.check_environment(loaded, {})
        variant.check_environment(loaded, {'GL_AUDIT_KERNEL': str(self.kernel),
                                          'GL_AUDIT_KERNEL_SHA256': self.document['kernel_sha256']})
        for key in ('GL_AUDIT_KERNEL', 'GL_AUDIT_KERNEL_SHA256'):
            with self.assertRaises(ValueError): variant.check_environment(loaded, {key: 'wrong'})

    def test_controller_no_boot_preserves_browser_and_controls(self):
        result = self.controller('--kernel-variant', str(self.path))
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertIn('NO-BOOT validated kernel variant', report['mode'])
        self.assertEqual(report['capture_identity'], variant.config.owner.capture_identity(None))
        self.assertEqual(report['preflight']['identity']['variant'], 'candidate')
        self.assertEqual(report['controls'], variant.config.owner.capture_controls('comparison'))

    def test_controller_flag_exclusions(self):
        for flags in [('--diagnostic-config', str(self.path)), ('--diagnostic-smoke',)]:
            result = self.controller('--kernel-variant', str(self.path), *flags)
            self.assertEqual(result.returncode, 2)
            self.assertIn('--kernel-variant', result.stderr)

    def test_default_preparation_stays_frozen(self):
        result = self.controller()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['mode'], 'NO-BOOT preparation')
        self.assertEqual(report['order'], ['off-1', 'on-1', 'on-2', 'off-2'])


if __name__ == '__main__':
    unittest.main()
