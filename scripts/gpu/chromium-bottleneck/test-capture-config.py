#!/usr/bin/env python3
"""Small-file diagnostic preflight tests. Never builds, stages an image, or boots."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import urllib.error
import urllib.request

import capture_config as config
owner = config.owner
TOKEN = 'chromium-diagnostic-capture-20260909T000000Z'


def digest(data):
    return hashlib.sha256(data).hexdigest()


class CaptureConfigTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.browser = self.root / 'runtime' / 'chrome'
        self.browser.parent.mkdir()
        elf = bytearray(80)
        elf[:6] = b'\x7fELF\x02\x01'
        elf[18:20] = b'\x3e\x00'
        self.browser.write_bytes(elf)
        self.browser.chmod(0o755)
        (self.browser.parent / 'resources.pak').write_bytes(b'fake resource')
        (self.browser.parent / 'empty').write_bytes(b'')
        self.base = self.root / 'protected.img'
        self.base.write_bytes(b'protected fixture')
        self.image = self.root / 'diagnostic.img'
        self.image.write_bytes(b'diagnostic fixture')
        self.pin = self.root / 'pin.json'
        (self.root / 'test.patch').write_bytes(b'synthetic pin only; no compile claim')
        self.commit = '7' * 40
        self.patch_sha = digest((self.root / 'test.patch').read_bytes())
        self.pin.write_text(json.dumps({'chromium_commit': self.commit,
            'patch_file': 'test.patch', 'patch_sha256': self.patch_sha}))
        rows = [{'guest_path': str(owner.BROWSER.parent / name),
                 'bytes': (self.browser.parent / name).stat().st_size,
                 'sha256': digest((self.browser.parent / name).read_bytes())}
                for name in ('chrome', 'resources.pak', 'empty')]
        self.manifest = {'schema_version': 1, 'kind': 'chromium-bottleneck-diagnostic-runtime',
            'status': 'staged', 'source_commit': self.commit, 'patch_sha256': self.patch_sha,
            'browser_path': str(owner.BROWSER), 'browser_sha256': digest(elf),
            'browser_bytes': len(elf), 'host_browser_path': str(self.browser),
            'rootfs_sha256': digest(self.image.read_bytes()), 'runtime_files': rows,
            'build_provenance': {'gn_args': ['is_debug=false'], 'receipt': 'synthetic unit fixture'}}
        self.manifest_path = self.root / 'runtime.json'
        self.configuration = {'schema_version': 1, 'arm': 'chromium-bottleneck-diagnostic',
            'rootfs_path': str(self.image), 'rootfs_sha256': self.manifest['rootfs_sha256'],
            'runtime_manifest_path': str(self.manifest_path),
            'source_commit': self.commit, 'patch_sha256': self.patch_sha}
        self.config_path = self.root / 'capture.json'
        self.write_documents()

    def write_documents(self):
        raw = json.dumps(self.manifest).encode()
        self.manifest_path.write_bytes(raw)
        self.configuration['runtime_manifest_sha256'] = digest(raw)
        self.config_path.write_text(json.dumps(self.configuration))

    def load(self):
        return config.load_diagnostic(self.config_path, TOKEN, self.base, self.pin)

    def test_concrete_receipt_and_provenance(self):
        loaded = self.load()
        self.assertEqual(loaded['policy']['runtime_files'], self.manifest['runtime_files'])
        self.assertEqual(loaded['policy']['build_provenance'], self.manifest['build_provenance'])
        self.assertEqual(loaded['receipt']['image']['sha256'], self.configuration['rootfs_sha256'])
        self.assertEqual(len(loaded['receipt']['host_runtime_files']), 3)
        self.assertEqual(loaded['policy_sha256'], digest(loaded['policy_bytes']))
        config.check_prelaunch_stamps(loaded)

    def test_missing_identity_and_unstaged_manifest(self):
        for field in ('source_commit', 'patch_sha256', 'browser_sha256', 'rootfs_sha256'):
            with self.subTest(field=field):
                saved = self.manifest.pop(field)
                self.write_documents()
                with self.assertRaises(ValueError): self.load()
                self.manifest[field] = saved
        self.manifest['status'] = 'planned'
        self.write_documents()
        with self.assertRaises(ValueError): self.load()

    def test_source_and_patch_must_match_pin(self):
        for field in ('source_commit', 'patch_sha256'):
            saved = self.configuration[field]
            self.configuration[field] = '0' * len(saved)
            self.write_documents()
            with self.assertRaises(ValueError): self.load()
            self.configuration[field] = saved
        self.write_documents()
        (self.root / 'test.patch').write_bytes(b'changed')
        with self.assertRaises(ValueError): self.load()

    def test_manifest_and_image_hashes_checked(self):
        self.manifest_path.write_text('{}')
        with self.assertRaises(ValueError): self.load()
        self.write_documents()
        self.image.write_bytes(b'changed fixture')
        with self.assertRaises(ValueError): self.load()

    def test_private_image_excludes_same_file_hardlink_and_symlink(self):
        self.configuration['rootfs_path'] = str(self.base)
        self.write_documents()
        with self.assertRaisesRegex(ValueError, 'separate'): self.load()
        alias = self.root / 'hardlink.img'
        os.link(self.base, alias)
        self.configuration['rootfs_path'] = str(alias)
        self.write_documents()
        with self.assertRaisesRegex(ValueError, 'separate'): self.load()
        alias.unlink()
        alias.symlink_to(self.image)
        with self.assertRaisesRegex(ValueError, 'symlink'): self.load()

    def test_runtime_asset_hash_is_not_just_declared(self):
        (self.browser.parent / 'resources.pak').write_bytes(b'bad resource!')
        with self.assertRaises(ValueError): self.load()

    def test_runtime_paths_and_caps(self):
        initial = copy.deepcopy(self.manifest['runtime_files'])
        for bad in (str(owner.BROWSER.parent / '../escape'), '/tmp/escape',
                    str(owner.BROWSER.parent) + '//asset', str(owner.BROWSER.parent) + '/a/./b'):
            self.manifest['runtime_files'] = copy.deepcopy(initial)
            self.manifest['runtime_files'][1]['guest_path'] = bad
            self.write_documents()
            with self.subTest(path=bad), self.assertRaises(ValueError): self.load()
        self.manifest['runtime_files'] = initial + [initial[0]]
        self.write_documents()
        with self.assertRaises(ValueError): self.load()
        self.manifest['runtime_files'] = copy.deepcopy(initial)
        self.manifest['runtime_files'][1]['bytes'] = owner.MAX_RUNTIME_FILE_BYTES + 1
        self.write_documents()
        with self.assertRaises(ValueError): self.load()

    def test_invalid_elf_or_nonexecutable(self):
        self.browser.chmod(0o644)
        with self.assertRaisesRegex(ValueError, 'ELF'): self.load()
        self.browser.chmod(0o755)
        self.browser.write_bytes(b'not an ELF' * 8)
        with self.assertRaisesRegex(ValueError, 'ELF'): self.load()

    def test_metadata_change_before_launch(self):
        loaded = self.load()
        self.image.write_bytes(b'changed')
        with self.assertRaises(ValueError): config.check_prelaunch_stamps(loaded)

    def test_same_size_rewrite_with_restored_mtime_is_refused(self):
        loaded = self.load()
        before = self.image.stat()
        self.image.write_bytes(b'X' * before.st_size)
        os.utime(self.image, ns=(before.st_atime_ns, before.st_mtime_ns))
        with self.assertRaises(ValueError): config.check_prelaunch_stamps(loaded)

    def test_new_hardlink_after_verification_is_refused(self):
        loaded = self.load()
        os.link(self.image, self.root / 'unexpected-alias.img')
        with self.assertRaises(ValueError): config.check_prelaunch_stamps(loaded)

    def test_baseline_ignores_stale_policy(self):
        policy_path = self.root / 'stale.json'
        policy_path.write_text('{}')
        with patch.dict(os.environ, {}, clear=True), patch.object(owner, 'POLICY_FILE', policy_path):
            self.assertIsNone(owner.load_capture_policy())
        self.assertEqual(owner.capture_identity(None), {'mode': 'baseline', 'browser_sha256': owner.BROWSER_SHA,
            'capture_purpose': 'comparison', 'selected_order': ['off-1', 'on-1', 'on-2', 'off-2']})

    def test_guest_activation_requires_exact_policy_hash(self):
        loaded = self.load()
        policy_path = self.root / 'policy.json'
        policy_path.write_bytes(loaded['policy_bytes'])
        with patch.object(owner, 'POLICY_FILE', policy_path), patch.dict(os.environ, {owner.POLICY_ENV: loaded['policy_sha256']}):
            self.assertEqual(owner.load_capture_policy(), loaded['policy'])
            policy_path.write_text('{}')
            with self.assertRaises(ValueError): owner.load_capture_policy()

    def test_guest_hashes_assets_and_rechecks_before_launch(self):
        policy = self.load()['policy']
        policy['browser_path'] = str(self.browser)
        for row in policy['runtime_files']:
            row['guest_path'] = str(self.browser.parent / Path(row['guest_path']).name)
        proof = self.root / 'proof.json'
        with patch.object(owner, 'BROWSER', self.browser), patch.object(owner, 'RUNTIME_PROOF', proof), patch.dict(os.environ, {owner.POLICY_ENV: 'a' * 64}):
            owner.validate_guest_policy(policy)
            receipt = owner.verify_runtime(policy)
            self.assertEqual(receipt['runtime_files_count'], 3)
            self.assertTrue(receipt['all_runtime_files_matched'])
            owner.runtime_still_verified(policy)
            (self.browser.parent / 'resources.pak').write_bytes(b'changed')
            with self.assertRaises(ValueError): owner.runtime_still_verified(policy)

    def test_on_only_category_and_baseline_flags_unchanged(self):
        policy = self.load()['policy']
        for trial in ('off-1', 'off-2'):
            self.assertEqual(owner.trace_flags(trial, policy), [])
            self.assertEqual(owner.trace_flags(trial), [])
        for trial in ('on-1', 'on-2'):
            baseline = owner.trace_flags(trial)
            diagnostic = owner.trace_flags(trial, policy)
            self.assertEqual(baseline[0], '--enable-tracing=media,cc,viz,benchmark,mojom,mojom.flow,graphics.pipeline,disabled-by-default-mojom')
            self.assertEqual(diagnostic[0], baseline[0] + ',' + owner.DIAGNOSTIC_CATEGORY)
            self.assertEqual(baseline[1:], diagnostic[1:])

    def test_duplicate_keys_and_boolean_schema_rejected(self):
        self.config_path.write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(ValueError, 'duplicate'): self.load()
        self.configuration['schema_version'] = True
        self.write_documents()
        with self.assertRaises(ValueError): self.load()

    def test_default_cli_remains_no_boot(self):
        result = subprocess.run([sys.executable, str(config.HERE / 'controller.py')],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['mode'], 'NO-BOOT preparation')

    def test_smoke_requires_explicit_diagnostic_config(self):
        result = subprocess.run([sys.executable, str(config.HERE / 'controller.py'), '--diagnostic-smoke'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--diagnostic-smoke requires --diagnostic-config', result.stderr)
        self.assertEqual(result.stdout, '')

    def test_smoke_purpose_and_order_are_sealed(self):
        comparison = self.load()
        smoke = config.load_diagnostic(self.config_path, TOKEN, self.base, self.pin,
                                       capture_purpose='emission-smoke')
        policy = smoke['policy']
        self.assertEqual(policy['capture_purpose'], 'emission-smoke')
        self.assertEqual(policy['selected_order'], ['on-1'])
        self.assertEqual(owner.capture_identity(policy)['selected_order'], ['on-1'])
        self.assertNotEqual(smoke['policy_sha256'], comparison['policy_sha256'])
        self.assertIn('no comparison/performance credit', smoke['receipt']['controls'])
        self.assertEqual(owner.trace_flags('on-1', policy), owner.trace_flags('on-1', comparison['policy']))
        with self.assertRaises(ValueError): owner.trace_flags('off-1', policy)
        with self.assertRaises(ValueError): owner.trace_flags('on-2', policy)
        policy['selected_order'] = owner.COMPARISON_ORDER
        with self.assertRaises(ValueError): owner.validate_guest_policy(policy)
        policy['selected_order'] = ['on-1']
        policy['capture_purpose'] = 'comparison'
        with self.assertRaises(ValueError): owner.validate_guest_policy(policy)

    def test_smoke_never_gets_comparison_or_performance_credit(self):
        row = {'trial': 'on-1', 'measurement_usable': True, 'legacy_status': 'pass', 'trace_export': 'accepted'}
        smoke = config.capture_summary([row], 'emission-smoke')
        self.assertFalse(smoke['valid_comparison'])
        self.assertEqual(smoke['performance_credit'], 'none')
        self.assertTrue(smoke['emission_smoke_capture_ready'])
        self.assertIn('requires event coverage', smoke['emission_validation'])
        self.assertFalse(config.capture_summary([], 'emission-smoke')['emission_smoke_capture_ready'])
        row['trace_export'] = 'rejected'
        self.assertFalse(config.capture_summary([row], 'emission-smoke')['emission_smoke_capture_ready'])
        rows = [dict(row, trial=trial) for trial, _ in owner.capture_trials('comparison')]
        self.assertTrue(config.capture_summary(rows, 'comparison')['valid_comparison'])
        self.assertFalse(config.capture_summary(rows, 'emission-smoke')['valid_comparison'])

    def test_selected_http_controls_match_smoke(self):
        spec = importlib.util.spec_from_file_location('smoke_test_http', config.HERE / 'http-server.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        server = module.MediaServer(('127.0.0.1', 0), self.config_path, self.image, self.root,
                                    trials=owner.capture_trials('emission-smoke'))
        try:
            self.assertEqual(list(server.controls), ['on-1'])
            with self.assertRaises(ValueError): server.set_control('off-1', start=True)
        finally:
            server.server_close()

    def test_off_policy_smoke_trace_uploads_are_refused(self):
        spec = importlib.util.spec_from_file_location('smoke_upload_test_http', config.HERE / 'http-server.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        server = module.MediaServer(('127.0.0.1', 0), self.config_path, self.image, self.root,
                                    trials=owner.capture_trials('emission-smoke'))
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            for endpoint in ('/trial-trace', '/trial-trace-rejected'):
                url = 'http://127.0.0.1:' + str(server.server_port) + endpoint + '?trial=on-2'
                request = urllib.request.Request(url, data=b'{}', method='POST')
                with self.assertRaises(urllib.error.HTTPError) as error:
                    urllib.request.urlopen(request, timeout=2)
                self.assertEqual(error.exception.code, 400)
            self.assertFalse((self.root / 'on-2-chromium-trace.json').exists())
            self.assertFalse((self.root / 'on-2-rejected-chromium-trace.json.gz').exists())
        finally:
            server.shutdown()
            thread.join(timeout=2)
            server.server_close()
            self.assertFalse(thread.is_alive())

    def test_policy_serial_commands_fit_existing_bound(self):
        commands = ['/usr/bin/python3 /tmp/mo.py policy ' + 'a' * 64,
                    'export ' + owner.POLICY_ENV + '=' + 'a' * 64,
                    'unset ' + owner.POLICY_ENV]
        for command in commands:
            framed = " m=MEDIA_DONE_; m=$m'999999_9999999999'; " + command + '; echo; echo "$m"\n'
            self.assertLessEqual(len(framed), 250)

    def test_http_policy_route_is_explicit(self):
        spec = importlib.util.spec_from_file_location('capture_test_http', config.HERE / 'http-server.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for body in (b'', b'{"synthetic":true}'):
            server = module.MediaServer(('127.0.0.1', 0), self.config_path, self.image, self.root, b'helper', body)
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            try:
                url = 'http://127.0.0.1:' + str(server.server_port) + '/capture-policy.json'
                if body:
                    with urllib.request.urlopen(url, timeout=2) as response:
                        self.assertEqual(response.read(), body)
                else:
                    with self.assertRaises(urllib.error.HTTPError) as error:
                        urllib.request.urlopen(url, timeout=2)
                    self.assertEqual(error.exception.code, 404)
            finally:
                server.shutdown()
                thread.join(timeout=2)
                server.server_close()
                self.assertFalse(thread.is_alive())


if __name__ == '__main__':
    unittest.main()
