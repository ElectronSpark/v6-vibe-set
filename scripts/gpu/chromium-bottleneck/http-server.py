#!/usr/bin/env python3
"""Bounded HTTP transport for local-media A/B; importing this module starts nothing."""
import hashlib
import json
import re
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

TRIALS = (('off-1', False), ('on-1', True), ('on-2', True), ('off-2', False))
MAX_POST = 65536
MAX_LOG = 8 * 1024 * 1024


def single_range(value, size):
    if value is None:
        return (0, size - 1, False)
    match = re.fullmatch(r'bytes=(\d*)-(\d*)', value.strip())
    if not match or not any(match.groups()) or size <= 0:
        raise ValueError('unsupported or unsatisfiable byte range')
    first, last = match.groups()
    if not first:
        suffix = int(last)
        if suffix <= 0:
            raise ValueError('empty suffix')
        start, end = max(0, size - suffix), size - 1
    else:
        start = int(first)
        end = min(int(last), size - 1) if last else size - 1
        if start >= size or end < start:
            raise ValueError('unsatisfiable range')
    return (start, end, True)


class MediaServer(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 16

    def __init__(self, address, html, media, out, guest_helper=b'', guest_policy=b'', trials=TRIALS):
        self.html = Path(html)
        self.media = Path(media)
        self.out = Path(out)
        self.helper = guest_helper
        if len(guest_policy) > 1024 * 1024:
            raise ValueError('capture policy exceeds cap')
        self.guest_policy = guest_policy
        self.lock = threading.RLock()
        self.condition = threading.Condition(self.lock)
        if tuple(trials) not in (TRIALS, (('on-1', True),)):
            raise ValueError('unsupported capture order')
        self.controls = {name: {'launch': False, 'start': False, 'abort': False}
                         for name, _ in trials}
        self.records = []
        self.request_records = []
        self.closed = False
        super().__init__(address, MediaHandler)

    def record(self, kind, value):
        row = {'host_utc': datetime.now(timezone.utc).isoformat(),
               'host_monotonic_ns': time.monotonic_ns(), kind: value}
        with self.condition:
            self.records.append(row)
            with (self.out / 'media-events.jsonl').open('a') as stream:
                stream.write(json.dumps(row) + '\n')
            self.condition.notify_all()
        return row

    def set_control(self, trial, **values):
        if trial not in self.controls or set(values) - {'launch', 'start', 'abort'}:
            raise ValueError('invalid trial control')
        with self.condition:
            self.controls[trial].update(values)
            self.record('control', {'trial': trial, **self.controls[trial]})

    def wait_record(self, predicate, timeout):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for record in self.records:
                    if predicate(record):
                        return record
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError('media event deadline')
                self.condition.wait(min(remaining, 1))


class MediaHandler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, _format, *_args):
        pass

    def reply(self, code, data=b'', content_type='application/json', headers=None,
              head=False):
        self.send_response(code)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Content-Type', content_type)
        self.send_header('Cache-Control', 'no-store')
        for key, value in (headers or {}).items():
            self.send_header(key, str(value))
        self.end_headers()
        if not head:
            self.wfile.write(data)

    def do_HEAD(self):
        self.get(True)

    def do_GET(self):
        self.get(False)

    def get(self, head):
        parsed = urlsplit(self.path)
        if parsed.path == '/trial-control':
            trial = parse_qs(parsed.query).get('trial', [''])[0]
            with self.server.lock:
                control = self.server.controls.get(trial)
                data = None if control is None else json.dumps(control).encode()
            self.reply(404 if data is None else 200, data or b'{}', head=head)
            return
        if parsed.path == '/fb-pacing-snapshot':
            data=(self.server.out/'xv6-fb-pacing-snapshot').read_bytes()
            if len(data)>65536: raise RuntimeError('helper cap exceeded')
            self.reply(200,data,'application/octet-stream',head=head); return
        if parsed.path == '/owner.py':
            self.reply(200, self.server.helper, 'text/x-python', head=head)
            return
        if parsed.path == '/capture-policy.json':
            data = self.server.guest_policy
            self.reply(200 if data else 404, data or b'{}', head=head)
            return
        if parsed.path in ('/', '/media.html', '/diag.html'):
            self.reply(200, self.server.html.read_bytes(), 'text/html; charset=utf-8',
                       head=head)
            return
        if parsed.path not in ('/clip.mp4', '/media.mp4', '/perf-1280x800-60fps.mp4'):
            self.reply(404, b'{}', head=head)
            return
        stat = self.server.media.stat()
        try:
            start, end, partial = single_range(self.headers.get('Range'), stat.st_size)
        except ValueError:
            self.reply(416, b'', 'video/mp4',
                       {'Content-Range': f'bytes */{stat.st_size}',
                        'Accept-Ranges': 'bytes'}, head)
            return
        count = max(0, end - start + 1)
        code = 206 if partial else 200
        self.send_response(code)
        self.send_header('Content-Type', 'video/mp4')
        self.send_header('Content-Length', str(count))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Cache-Control', 'no-store')
        if partial:
            self.send_header('Content-Range', f'bytes {start}-{end}/{stat.st_size}')
        self.end_headers()
        sent = 0
        failed = None
        if not head:
            try:
                with self.server.media.open('rb') as stream:
                    stream.seek(start)
                    while sent < count:
                        block = stream.read(min(64 * 1024, count - sent))
                        if not block:
                            raise OSError('media file shortened during response')
                        self.wfile.write(block)
                        sent += len(block)
            except (BrokenPipeError, ConnectionResetError, OSError) as error:
                failed = str(error)
                self.close_connection = True
        row = {'method': self.command, 'path': parsed.path,
               'range': self.headers.get('Range'), 'status': code,
               'start': start, 'end': end, 'file_bytes': stat.st_size,
               'response_bytes': sent, 'client_disconnect_or_error': failed}
        self.server.record('media_request', row)

    def receive_log(self):
        parsed = urlsplit(self.path)
        trial = parse_qs(parsed.query).get('trial', [''])[0]
        try:
            size = int(self.headers.get('Content-Length', '-1'))
            offset = int(self.headers.get('X-Source-Offset', '-1'))
        except ValueError:
            size, offset = -1, -1
        target = self.server.out / (trial + '-chromium-stderr.log')
        if trial not in self.server.controls or not 0 <= size <= MAX_LOG or offset < 0:
            self.reply(400, b'{}'); self.close_connection = True; return
        if target.exists():
            self.reply(409, b'{}'); self.close_connection = True; return
        total = 0
        digest = hashlib.sha256()
        try:
            with target.open('xb') as stream:
                while total < size:
                    block = self.rfile.read(min(65536, size - total))
                    if not block:
                        raise OSError('short log upload')
                    stream.write(block); digest.update(block); total += len(block)
        except (OSError, TimeoutError) as error:
            self.server.record('log_upload_error', {'trial': trial, 'bytes': total, 'error': str(error)})
            self.reply(400, b'{}'); self.close_connection = True; return
        self.server.record('log_upload', {'trial': trial, 'bytes': total, 'source_offset': offset,
                                         'sha256': digest.hexdigest(), 'file': target.name})
        self.reply(200, json.dumps({'bytes': total, 'sha256': digest.hexdigest()}).encode())

    def receive_trace(self):
        parsed = urlsplit(self.path)
        trial = parse_qs(parsed.query).get('trial', [''])[0]
        try: size = int(self.headers.get('Content-Length', '-1'))
        except ValueError: size = -1
        phase = parse_qs(parsed.query).get('phase', [''])[0]
        diagnostic = parsed.path == '/trial-diag'
        rejected = parsed.path == '/trial-trace-rejected'
        allowed = trial in self.server.controls and (phase in ('before','after') if diagnostic else trial in ('on-1','on-2'))
        cap = 4*1024*1024 if diagnostic else 64*1024*1024
        if not allowed or not 0 < size <= cap:
            self.reply(400, b'{}'); self.close_connection = True; return
        target = self.server.out / (trial + '-' + phase + '-telemetry.json' if diagnostic else trial + '-rejected-chromium-trace.json.gz' if rejected else trial + '-chromium-trace.json')
        if target.exists():
            self.reply(409, b'{}'); self.close_connection = True; return
        count = 0
        digest = hashlib.sha256()
        self.connection.settimeout(25)
        try:
            with target.open('xb') as stream:
                while count < size:
                    block = self.rfile.read(min(65536, size-count))
                    if not block: raise OSError('short trace upload')
                    stream.write(block); digest.update(block); count += len(block)
        except (OSError, TimeoutError) as error:
            self.server.record('trace_upload_error', {'trial': trial, 'bytes': count, 'error': str(error)})
            self.reply(400, b'{}'); self.close_connection = True; return
        receipt = {'trial': trial, 'bytes': count, 'sha256': digest.hexdigest(), 'file': target.name}
        self.server.record('trace_upload', receipt)
        self.reply(200, json.dumps(receipt).encode())

    def do_POST(self):
        if urlsplit(self.path).path in ('/trial-trace', '/trial-trace-rejected', '/trial-diag'):
            self.receive_trace(); return
        if urlsplit(self.path).path == '/trial-log':
            self.receive_log(); return
        if urlsplit(self.path).path not in ('/media-result', '/trial-event'):
            self.reply(404, b'{}')
            self.close_connection = True
            return
        try:
            size = int(self.headers.get('Content-Length', '-1'))
        except ValueError:
            size = -1
        if not 0 < size <= MAX_POST:
            self.reply(413, b'{}')
            self.close_connection = True
            return
        try:
            body = self.rfile.read(size)
            if len(body) != size:
                raise ValueError('short body')
            payload = json.loads(body)
            if not isinstance(payload, dict):
                raise ValueError('object required')
            trial = payload.get('trial', payload.get('run'))
            if trial not in self.server.controls and trial != 'supervisor':
                raise ValueError('unknown trial')
        except (ValueError, OSError, TimeoutError):
            self.reply(400, b'{}')
            self.close_connection = True
            return
        self.server.record('payload', payload)
        self.reply(200, b'{"accepted":true}')
