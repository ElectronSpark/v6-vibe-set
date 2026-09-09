#!/usr/bin/env python3
"""Real tiny child-process cleanup regressions; never launch or signal QEMU."""
import pathlib
import select
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import HTTPServer,BaseHTTPRequestHandler
import urllib.request

import capture_cleanup


class CleanupTests(unittest.TestCase):
    def test_started_server_blocked_handler_cannot_block_finalization(self):
        entered, release = threading.Event(), threading.Event()
        client_errors = []
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                entered.set()
                release.wait(timeout=3)
                self.send_response(204)
                self.end_headers()
            def log_message(self,*args):
                pass
        server = HTTPServer(('127.0.0.1',0),Handler)
        server_thread = threading.Thread(target=server.serve_forever,daemon=True)
        def request():
            try:
                with urllib.request.urlopen('http://127.0.0.1:'+str(server.server_port),timeout=3) as response:
                    response.read()
            except BaseException as error:
                client_errors.append(error)
        client = threading.Thread(target=request)
        shutdown_thread = None
        server_thread.start()
        client.start()
        try:
            self.assertTrue(entered.wait(timeout=2))
            started = time.monotonic()
            with self.assertRaisesRegex(TimeoutError,'HTTP shutdown deadline') as caught:
                capture_cleanup.stop_server_loop(server,server_thread,timeout=0.03)
            shutdown_thread = caught.exception.shutdown_thread
            self.assertLess(time.monotonic()-started,0.75)
            self.assertTrue(shutdown_thread.daemon)
            self.assertTrue(server_thread.is_alive())
            # Execution has returned to the conductor while the handler is
            # still blocked; final inventory/receipt operations can now run.
            self.assertFalse(release.is_set())
        finally:
            release.set()
            if shutdown_thread is None and server_thread.is_alive():server.shutdown()
            client.join(timeout=3)
            server_thread.join(timeout=3)
            if shutdown_thread is not None:shutdown_thread.join(timeout=3)
            server.server_close()
        self.assertFalse(client.is_alive())
        self.assertFalse(server_thread.is_alive())
        self.assertFalse(shutdown_thread.is_alive())
        self.assertEqual(client_errors,[])

    def test_http_shutdown_is_skipped_when_serve_thread_never_started(self):
        server = HTTPServer(('127.0.0.1',0),BaseHTTPRequestHandler)
        thread = threading.Thread(target=server.serve_forever)
        try:
            self.assertIsNone(thread.ident)
            capture_cleanup.stop_server_loop(server,thread)
            self.assertFalse(thread.is_alive())
        finally:
            server.server_close()

    def test_fresh_receipt_refuses_existing_review_marker_and_symlink(self):
        with tempfile.TemporaryDirectory(prefix='capture-receipt-test-') as temporary:
            root = pathlib.Path(temporary)
            receipt = capture_cleanup.create_receipt(root/'new-run')
            marker = receipt/'t1-on-1-mapped-visible.ok'
            marker.write_bytes(b'previous review')
            with self.assertRaises(FileExistsError):
                capture_cleanup.create_receipt(receipt)
            self.assertEqual(marker.read_bytes(),b'previous review')
            alias = root/'alias'
            alias.symlink_to(receipt,target_is_directory=True)
            with self.assertRaises(FileExistsError):
                capture_cleanup.create_receipt(alias)

    def test_terminate_resistant_child_is_reaped_despite_cleanup_errors(self):
        code = 'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); print("ready",flush=True); time.sleep(30)'
        target = subprocess.Popen([sys.executable,'-c',code],stdout=subprocess.PIPE)
        unrelated = subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'])
        calls = []
        def failed_quit():
            calls.append('qmp')
            raise RuntimeError('synthetic QMP failure')
        def failed_owned_cleanup():
            calls.append('owned')
            raise RuntimeError('synthetic ownership helper failure')
        try:
            ready,_,_ = select.select([target.stdout],[],[],2)
            self.assertTrue(ready,'tiny target failed to initialize')
            self.assertEqual(target.stdout.readline(),b'ready\n')
            result = capture_cleanup.stop_owned_launcher(target,failed_quit,failed_owned_cleanup,
                natural_timeout=0.02,cleanup_timeout=0.02,terminate_timeout=0.02,kill_timeout=2)
            self.assertTrue(result['reaped'],result)
            self.assertEqual(result['returncode'],-9)
            self.assertFalse(result['clean_exit'])
            self.assertEqual(target.wait(timeout=0),-9)
            self.assertIsNone(unrelated.poll(),'cleanup signaled a process it did not own')
            self.assertEqual(calls,['qmp','owned','owned'])
            self.assertIn('reap_after_terminate',[row['step'] for row in result['errors']])
            self.assertIn('owned_qemu_cleanup',[row['step'] for row in result['errors']])
            self.assertEqual(result['attempts'][-1],{'step':'reap_after_kill','status':'completed'})
        finally:
            for child in (target,unrelated):
                if child.poll() is None:child.kill()
                child.wait(timeout=2)
            target.stdout.close()

    def test_naturally_reaped_launcher_still_checks_owned_qemu(self):
        child = subprocess.Popen([sys.executable,'-c','pass'])
        calls = []
        try:
            result = capture_cleanup.stop_owned_launcher(child,lambda:calls.append('quit'),
                lambda:calls.append('owned'),natural_timeout=2)
            self.assertTrue(result['reaped'])
            self.assertEqual(result['returncode'],0)
            self.assertTrue(result['clean_exit'])
            self.assertIn('owned',calls)
            self.assertEqual(result['errors'],[])
        finally:
            if child.poll() is None:child.kill()
            child.wait(timeout=2)

    def test_naturally_reaped_nonzero_exit_is_not_clean(self):
        child = subprocess.Popen([sys.executable,'-c','raise SystemExit(7)'])
        try:
            result = capture_cleanup.stop_owned_launcher(child,lambda:None,lambda:None,natural_timeout=2)
            self.assertTrue(result['reaped'])
            self.assertEqual(result['returncode'],7)
            self.assertFalse(result['clean_exit'])
        finally:
            if child.poll() is None:child.kill()
            child.wait(timeout=2)


if __name__ == '__main__':
    unittest.main()
