#!/usr/bin/env python3
"""Private-image staging regressions using only temporary 8 MiB ext2 images."""
import copy
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('stage_runtime',pathlib.Path(__file__).with_name('stage_runtime.py'))
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


@unittest.skipUnless(shutil.which('debugfs') and shutil.which('mke2fs') and shutil.which('cp'),'ext2 tools required')
class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='chromium-stage-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        self.base = self.root/'base.img'
        with self.base.open('wb') as stream:
            stream.truncate(8*1024*1024)
        staging.run(['mke2fs','-q','-t','ext2','-F',str(self.base)],time.monotonic()+20)
        self.host = self.root/'runtime'
        self.host.mkdir(mode=0o750)
        (self.host/'locales').mkdir(mode=0o710)
        self.browser = self.host/'chrome'
        self.browser.write_bytes(b'diagnostic browser\n')
        self.browser.chmod(0o751)
        self.locale = self.host/'locales'/'en-US.pak'
        self.locale.write_bytes(b'new locale data\n')
        self.locale.chmod(0o640)
        old = self.root/'old.txt'
        old.write_bytes(b'frozen original\n')
        with self.base.open('r+b') as stream:
            image = staging.ExtImage(stream.fileno(),time.monotonic()+20)
            commands = []
            parent = ''
            for part in staging.ROOT.strip('/').split('/'):
                parent += '/'+part
                commands.append('mkdir '+parent)
            commands += ['mkdir '+staging.ROOT+'/old-assets',
                'write '+str(old)+' '+staging.ROOT+'/chrome',
                'write '+str(old)+' '+staging.ROOT+'/old-assets/stale.pak',
                'write '+str(old)+' /untouched.txt']
            image.commands(commands,True)
        pins = json.loads(pathlib.Path(__file__).with_name('upstream-sources.json').read_text())
        self.data = dict(schema_version=1,source_commit=pins['chromium_commit'],patch_sha256=pins['patch_sha256'],
            host_browser_path=str(self.browser),browser_sha256=sha(self.browser),browser_bytes=self.browser.stat().st_size,
            runtime_files=[dict(guest_path=staging.BROWSER,sha256=sha(self.browser),bytes=self.browser.stat().st_size),
                           dict(guest_path=staging.ROOT+'/locales/en-US.pak',sha256=sha(self.locale),bytes=self.locale.stat().st_size)],
            build_provenance={'gn_args':'is_debug=false', 'toolchain':{'revision':'test-only'}})
        self.input = self.root/'input.json'
        self.output = self.root/'private.img'
        self.manifest = self.root/'staged.json'
        self.save_input()

    def save_input(self):
        self.input.write_text(json.dumps(self.data))

    def stage(self):
        return staging.stage(self.base,self.input,self.output,self.manifest,timeout=30,base_sha256=sha(self.base))

    def dump(self,image_path,guest):
        with image_path.open('rb') as image_file, tempfile.TemporaryFile() as dumped:
            image = staging.ExtImage(image_file.fileno(),time.monotonic()+10)
            image.commands(['dump '+guest+' /proc/self/fd/'+str(dumped.fileno())],extra_fds=(dumped.fileno(),))
            dumped.seek(0)
            return dumped.read()

    def assert_no_outputs(self):
        self.assertFalse(self.output.exists())
        self.assertFalse(self.manifest.exists())

    def test_replaces_only_private_runtime_and_verifies_modes_hashes_provenance(self):
        before_sha, before_stat = sha(self.base), staging.identity(self.base.stat())
        result = self.stage()
        self.assertEqual(sha(self.base),before_sha)
        self.assertEqual(staging.identity(self.base.stat()),before_stat)
        self.assertNotEqual(self.base.stat().st_ino,self.output.stat().st_ino)
        self.assertEqual(self.output.stat().st_size,8*1024*1024)
        self.assertEqual(result['rootfs_sha256'],sha(self.output))
        self.assertEqual(self.dump(self.output,'/untouched.txt'),b'frozen original\n')
        self.assertEqual(self.dump(self.base,staging.BROWSER),b'frozen original\n')
        self.assertEqual(self.dump(self.output,staging.BROWSER),self.browser.read_bytes())
        with self.output.open('rb') as stream:
            tree = staging.ExtImage(stream.fileno(),time.monotonic()+10).tree()
        self.assertEqual(set(tree),{staging.ROOT,staging.BROWSER,staging.ROOT+'/locales',staging.ROOT+'/locales/en-US.pak'})
        self.assertEqual(tree[staging.BROWSER]['mode'],0o751)
        self.assertEqual(tree[staging.ROOT+'/locales']['mode'],0o710)
        self.assertEqual(tree[staging.ROOT+'/locales/en-US.pak']['mode'],0o640)
        self.assertEqual(result['runtime_files'],self.data['runtime_files'])
        self.assertEqual(result['build_provenance'],self.data['build_provenance'])
        self.assertEqual(result['kind'],'chromium-bottleneck-diagnostic-runtime')
        self.assertEqual(result['status'],'staged')
        self.assertEqual(json.loads(self.manifest.read_text()),result)

    def test_existing_destination_is_never_overwritten(self):
        self.output.write_bytes(b'do not overwrite')
        with self.assertRaisesRegex(ValueError,'already exists'):
            self.stage()
        self.assertEqual(self.output.read_bytes(),b'do not overwrite')
        self.assertFalse(self.manifest.exists())

    def test_symlink_output_parent_and_hardlink_base_are_refused(self):
        link = self.root/'linked-parent'
        link.symlink_to(self.host,target_is_directory=True)
        with self.assertRaisesRegex(ValueError,'symlink'):
            staging.stage(self.base,self.input,link/'private.img',self.manifest,30,base_sha256=sha(self.base))
        os.link(self.base,self.root/'base-alias.img')
        with self.assertRaisesRegex(ValueError,'single-link'):
            self.stage()
        self.assert_no_outputs()

    def test_symlink_and_hardlink_runtime_files_are_refused(self):
        self.locale.unlink()
        self.locale.symlink_to(self.browser)
        with self.assertRaisesRegex(ValueError,'symlink'):
            self.stage()
        self.locale.unlink()
        os.link(self.browser,self.locale)
        with self.assertRaisesRegex(ValueError,'single-link'):
            self.stage()
        self.assert_no_outputs()

    def test_unsafe_duplicate_paths_and_bad_hash_are_refused_before_clone(self):
        original = copy.deepcopy(self.data)
        for path in ('/etc/passwd',staging.ROOT+'/../escape',staging.ROOT+'/a\nrm /etc',staging.ROOT+'/a//b',staging.BROWSER):
            with self.subTest(path=path):
                self.data = copy.deepcopy(original)
                self.data['runtime_files'][1]['guest_path'] = path
                self.save_input()
                with self.assertRaises(ValueError):
                    self.stage()
                self.assert_no_outputs()
        self.data = copy.deepcopy(original)
        self.data['runtime_files'][1]['sha256'] = '0'*64
        self.save_input()
        with self.assertRaisesRegex(ValueError,'identity mismatch'):
            self.stage()
        self.assert_no_outputs()

    def test_wrong_pin_and_nonexecutable_browser_are_refused(self):
        self.data['source_commit'] = '0'*40
        self.save_input()
        with self.assertRaisesRegex(ValueError,'pin mismatch'):
            self.stage()
        self.data['source_commit'] = json.loads(pathlib.Path(__file__).with_name('upstream-sources.json').read_text())['chromium_commit']
        self.save_input()
        self.browser.chmod(0o640)
        with self.assertRaisesRegex(ValueError,'not executable'):
            self.stage()
        self.assert_no_outputs()

    def test_frozen_base_hash_mismatch_is_refused_before_clone(self):
        with self.assertRaisesRegex(ValueError,'frozen base SHA-256 mismatch'):
            staging.stage(self.base,self.input,self.output,self.manifest,30,base_sha256='0'*64)
        self.assert_no_outputs()

    def test_output_manifest_respects_capture_one_mib_cap(self):
        self.data['build_provenance']['large'] = 'x'*(600*1024)
        self.save_input()
        with self.assertRaisesRegex(ValueError,'output manifest cap'):
            self.stage()
        self.assert_no_outputs()

    def test_source_base_change_during_copy_removes_only_owned_outputs(self):
        original = staging.run
        def changed(argv,*args,**kwargs):
            result = original(argv,*args,**kwargs)
            if argv[0] == 'cp':
                with self.base.open('r+b') as stream:
                    stream.write(b'changed during copy')
            return result
        with mock.patch.object(staging,'run',changed):
            with self.assertRaisesRegex(ValueError,'source changed'):
                self.stage()
        self.assert_no_outputs()

    def test_runtime_source_change_during_write_is_refused(self):
        original = staging.ExtImage.commands
        def changed(image,commands,*args,**kwargs):
            result = original(image,commands,*args,**kwargs)
            if any(command.startswith('write ') and command.endswith(' '+staging.BROWSER) for command in commands):
                self.browser.write_bytes(b'changed source during staging\n')
            return result
        with mock.patch.object(staging.ExtImage,'commands',changed):
            with self.assertRaisesRegex(ValueError,'source changed'):
                self.stage()
        self.assert_no_outputs()

    def test_replaced_output_path_is_refused_and_foreign_file_preserved(self):
        original = staging.ExtImage.commands
        def replaced(image,commands,*args,**kwargs):
            result = original(image,commands,*args,**kwargs)
            if commands == ['stats']:
                self.output.unlink()
                self.output.write_bytes(b'foreign replacement')
            return result
        with mock.patch.object(staging.ExtImage,'commands',replaced):
            with self.assertRaisesRegex(ValueError,'owned output path changed'):
                self.stage()
        self.assertEqual(self.output.read_bytes(),b'foreign replacement')
        self.assertFalse(self.manifest.exists())

    def test_guest_symlink_is_refused_without_following_or_changing_base(self):
        with self.base.open('r+b') as stream:
            staging.ExtImage(stream.fileno(),time.monotonic()+10).commands(
                ['symlink '+staging.ROOT+'/escape /'],True)
        before = sha(self.base)
        with self.assertRaisesRegex(ValueError,'guest symlinks'):
            self.stage()
        self.assert_no_outputs()
        self.assertEqual(sha(self.base),before)

    def test_debugfs_zero_exit_without_writing_is_not_success(self):
        original = staging.ExtImage.commands
        def omitted(image,commands,*args,**kwargs):
            if any(command.startswith('write ') for command in commands):
                return ''
            return original(image,commands,*args,**kwargs)
        base_sha = sha(self.base)
        with mock.patch.object(staging.ExtImage,'commands',omitted):
            with self.assertRaisesRegex(ValueError,'membership differs'):
                self.stage()
        self.assert_no_outputs()
        self.assertEqual(sha(self.base),base_sha)

    def test_guest_readback_detects_same_size_corruption(self):
        wrong = self.root/'wrong.bin'
        wrong.write_bytes(b'X'*self.browser.stat().st_size)
        original = staging.ExtImage.commands
        def corrupted(image,commands,*args,**kwargs):
            result = original(image,commands,*args,**kwargs)
            if any(command.startswith('write ') and command.endswith(' '+staging.BROWSER) for command in commands):
                original(image,['rm '+staging.BROWSER,'write '+str(wrong)+' '+staging.BROWSER,
                                'sif '+staging.BROWSER+' mode 0100751'],True)
            return result
        with mock.patch.object(staging.ExtImage,'commands',corrupted):
            with self.assertRaisesRegex(ValueError,'content hash mismatch'):
                self.stage()
        self.assert_no_outputs()

    def test_debugfs_error_with_zero_exit_is_rejected(self):
        with self.base.open('rb') as stream:
            image = staging.ExtImage(stream.fileno(),time.monotonic()+10)
            with self.assertRaisesRegex(ValueError,'debugfs reported an error'):
                image.commands(['stat /does-not-exist'])

    def test_child_output_and_deadline_are_bounded_and_reaped(self):
        with self.assertRaisesRegex(ValueError,'output cap'):
            staging.run([sys.executable,'-c','print("x"*10000)'],time.monotonic()+5,output_limit=128)
        with self.assertRaisesRegex(ValueError,'deadline'):
            staging.run([sys.executable,'-c','import time; time.sleep(10)'],time.monotonic()+0.02)


if __name__ == '__main__':
    unittest.main()
