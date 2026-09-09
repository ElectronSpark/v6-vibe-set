#!/usr/bin/env python3
"""Tiny host collector checks; no guest, build, serial port, or VM."""
from contextlib import contextmanager
import ast
import ctypes
import hashlib
import http.client
import importlib.util
from pathlib import Path
import shlex
import socket
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch
import urllib.error
import urllib.parse
import urllib.request

spec = importlib.util.spec_from_file_location('workqueue_selftest',
                                             Path(__file__).with_name('workqueue-selftest.py'))
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


class CollectorTests(unittest.TestCase):
    def setUp(self):
        self.markers = [f'[workqueue-selftest] case={i} probes={4 if i == 4 else 1} '
                        'result=PASS phase=progress-before-release '
                        f'before_release={4 if i == 4 else 1} workers=2 idle=1 running=1 pending=0 drained=1'
                        for i in range(1, 5)]
        self.markers.append('[workqueue-selftest] END result=PASS cases=4 retained_queue=1')
        self.raw = ('other kernel output\n' + '\n'.join(self.markers) + '\n').encode()
        self.headers = {'X-Klog-Capacity': '16384',
                        'X-Klog-Sha256': hashlib.sha256(self.raw).hexdigest(),
                        'X-Klog-Helper-Sha256': hashlib.sha256(collector.KLOG_PROGRAM.encode()).hexdigest()}

    @contextmanager
    def server(self):
        server = collector.KernelRingServer('tiny-test-token')
        thread = threading.Thread(target=lambda: server.serve_forever(poll_interval=.01))
        thread.start()
        try:
            yield server, thread
        finally:
            server.stop(thread)
            self.assertFalse(thread.is_alive())

    def post(self, server, endpoint=None, headers=None):
        connection = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=2)
        try:
            connection.request('POST', endpoint or server.endpoint, body=self.raw,
                               headers=self.headers if headers is None else headers)
            response = connection.getresponse()
            return response.status, response.read(1024)
        finally:
            connection.close()

    def route_function(self, connector):
        now = [0.0]
        def sleep(seconds):
            now[0] += seconds
        namespace = {'socket': SimpleNamespace(create_connection=connector),
                     'time': SimpleNamespace(monotonic=lambda: now[0], sleep=sleep),
                     'urllib': SimpleNamespace(parse=urllib.parse)}
        function = next(node for node in ast.parse(collector.KLOG_PROGRAM).body
                        if isinstance(node, ast.FunctionDef) and node.name == 'wait_route')
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<route-check>', 'exec'), namespace)
        return namespace['wait_route'], now

    def test_no_route_then_ready_before_one_real_upload(self):
        calls = []
        def connect(address, timeout):
            calls.append((address, timeout))
            if len(calls) <= 2:
                raise OSError(101 if len(calls) == 1 else 113, 'route not ready')
            return socket.create_connection(address, timeout=timeout)
        wait_route, now = self.route_function(connect)
        with self.server() as (server, _):
            wait_route('http://127.0.0.1:' + str(server.server_port) + server.endpoint)
            self.assertEqual(len(calls), 3)
            self.assertEqual(now[0], .5)
            self.assertIsNone(server.raw)  # Readiness connections never POST.
            self.assertEqual(self.post(server), (200, b'OK\n'))

    def test_route_deadline_and_no_retries_for_other_errors(self):
        for error in (OSError(111, 'refused'), OSError(22, 'invalid'), TimeoutError('timed out')):
            connector = MagicMock(side_effect=error)
            wait_route, now = self.route_function(connector)
            with self.assertRaises(OSError): wait_route('http://127.0.0.1:9/unused')
            self.assertEqual(connector.call_count, 1)
            self.assertEqual(now[0], 0)
        connector = MagicMock(side_effect=OSError(113, 'no route'))
        wait_route, now = self.route_function(connector)
        with self.assertRaises(OSError): wait_route('http://127.0.0.1:9/unused')
        self.assertLessEqual(connector.call_count, 40)
        self.assertEqual(now[0], 10)

    def test_snapshot_is_read_once_and_post_errors_never_retry(self):
        calls = []
        def klogctl(action, buffer, size):
            calls.append(action)
            if action == 10:
                return 16384
            ctypes.memmove(buffer, self.raw, len(self.raw))
            return len(self.raw)
        with tempfile.TemporaryDirectory() as directory:
            helper = Path(directory) / 'helper.py'
            helper.write_text(collector.KLOG_PROGRAM)
            for number in (113, 111):
                calls.clear()
                error = urllib.error.URLError(OSError(number, 'upload failed'))
                with patch.object(ctypes, 'CDLL', return_value=SimpleNamespace(klogctl=klogctl)), \
                     patch.object(socket, 'create_connection', return_value=MagicMock()) as connect, \
                     patch.object(urllib.request, 'urlopen', side_effect=error) as post, \
                     patch.object(sys, 'argv', [str(helper), 'http://127.0.0.1:9/unused']):
                    with self.assertRaises(urllib.error.URLError):
                        exec(compile(collector.KLOG_PROGRAM, str(helper), 'exec'), {'__file__': str(helper)})
                    self.assertEqual(calls, [10, 3])
                    self.assertEqual(connect.call_count, 1)
                    self.assertEqual(post.call_count, 1)
                    self.assertEqual(post.call_args.args[0].data, self.raw)

    def test_complete_kernel_records_recover_strict_cases(self):
        with self.server() as (server, _):
            self.assertEqual(self.post(server), (200, b'OK\n'))
            self.assertEqual(server.raw, self.raw)
            self.assertEqual(server.proof['sha256'], hashlib.sha256(self.raw).hexdigest())
            markers = collector.record_markers(server.raw)
            self.assertTrue(collector.candidate_cases_complete(markers))

    def test_real_printf_timestamp_prefixes_and_unprefixed_records(self):
        raw = b'[0] early boot\n' + b''.join(
            ('[' + str(127000000 + index) + '] ' + line + '\n').encode()
            for index, line in enumerate(self.markers))
        self.assertEqual(collector.record_markers(raw), self.markers)
        self.assertTrue(collector.candidate_cases_complete(collector.record_markers(raw)))
        self.assertEqual(collector.record_markers(self.raw), self.markers)
        self.assertEqual(collector.record_markers(b'user-output ' + self.markers[0].encode() + b'\n'), [])

    def test_missing_interrupted_reordered_duplicate_cases_still_fail(self):
        variants = [self.markers[1:], [self.markers[0]] + self.markers,
                    [self.markers[1], self.markers[0], *self.markers[2:]],
                    [line.replace('before_release=1', 'before_release=0') for line in self.markers],
                    [line.replace('result=PASS phase', 'result=PAother outputSS phase') for line in self.markers]]
        for markers in variants:
            with self.subTest(markers=markers):
                self.assertFalse(collector.candidate_cases_complete(markers))

    def test_readback_integrity_and_size_gates(self):
        for field, value in [('X-Klog-Sha256', '0' * 64), ('X-Klog-Capacity', '0'),
                             ('Content-Length', str(collector.KLOG_LIMIT + 1)),
                             ('X-Klog-Helper-Sha256', '0' * 64), ('Transfer-Encoding', 'chunked')]:
            with self.subTest(field=field), self.server() as (server, _):
                headers = {**self.headers, field: value}
                self.assertEqual(self.post(server, headers=headers)[0], 400)
                self.assertIsNone(server.raw)

    def test_readback_requires_token_and_only_one_accepted_upload(self):
        with self.server() as (server, _):
            self.assertEqual(self.post(server, endpoint='/wrong-token')[0], 400)
            self.assertIsNone(server.raw)
            self.assertEqual(self.post(server)[0], 200)
            self.assertEqual(self.post(server)[0], 400)
            self.assertEqual(server.raw, self.raw)

    def test_shutdown_reaps_incomplete_owned_request(self):
        with self.server() as (server, thread):
            connection = socket.create_connection(('127.0.0.1', server.server_port), timeout=2)
            try:
                connection.sendall(('POST ' + server.endpoint + ' HTTP/1.1\r\nHost: localhost\r\n').encode())
                deadline = time.monotonic() + 1
                while server.active_connection is None and time.monotonic() < deadline:
                    time.sleep(.001)
                self.assertIsNotNone(server.active_connection)
                start = time.monotonic()
                server.stop(thread)
                self.assertLess(time.monotonic() - start, 2)
                self.assertFalse(thread.is_alive())
            finally:
                connection.close()

    def test_marker_echo_is_not_completion_and_drop_is_absorbed(self):
        marker = 'WQ_DONE_12_123456789012345'
        command = collector.marked_command('/usr/bin/python3 /tmp/wq-klog.py', marker)
        self.assertNotIn(marker, command)
        self.assertTrue(command[1:].startswith('m=WQ_DONE_'))
        self.assertIsNone(collector.command_complete(command.encode(), marker))
        self.assertIsNone(collector.command_complete((marker + ':0\r\n').encode(), marker))
        self.assertEqual(collector.command_complete((marker + ':0\r\n\x1b[?2004hroot:/# ').encode(), marker), 0)
        self.assertEqual(collector.command_complete((marker + ':7\r\nroot:/# ').encode(), marker), 7)

    def test_all_staging_commands_are_short_and_helper_is_valid_python(self):
        self.assertLessEqual(len(collector.KLOG_PROGRAM.encode()), 4096)
        compile(collector.KLOG_PROGRAM, '<guest-helper>', 'exec')
        for index, line in enumerate(collector.KLOG_PROGRAM.splitlines()):
            text = "printf '%s\\n' " + shlex.quote(line) + ' >> /tmp/wq-klog.py'
            command = collector.marked_command(text, 'WQ_DONE_' + str(index) + '_1234567890123456789')
            self.assertLessEqual(len(command), 250)
        with self.assertRaises(ValueError): collector.marked_command('x' * 250, 'WQ_DONE_1')


if __name__ == '__main__':
    unittest.main()
