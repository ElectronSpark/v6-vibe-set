#!/usr/bin/env python3
"""Run the opt-in workqueue regression through the owned GUI launcher."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import secrets
import socket
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = Path(__file__).resolve().parents[2]
HELPERS = ROOT / 'scripts/gpu/chromium-bottleneck'
sys.path.insert(0, str(HELPERS))
import capture_cleanup
import capture_config

KLOG_LIMIT = 1024 * 1024
SERIAL_LIMIT = 16 * 1024 * 1024
KLOG_PROGRAM = '''import ctypes,hashlib,socket,sys,time,urllib.request,urllib.parse
c=ctypes.CDLL(None,use_errno=True)
f=c.klogctl
f.argtypes=[ctypes.c_int,ctypes.c_void_p,ctypes.c_int]
f.restype=ctypes.c_int
n=f(10,None,0)
if not 0<n<=1048576: raise RuntimeError(("capacity",n,ctypes.get_errno()))
b=ctypes.create_string_buffer(n)
r=f(3,b,n)
if not 0<r<=n: raise RuntimeError(("read",r,ctypes.get_errno()))
d=b.raw[:r]
h={"X-Klog-Capacity":str(n),"X-Klog-Sha256":hashlib.sha256(d).hexdigest()}
with open(__file__,"rb") as source:
 h["X-Klog-Helper-Sha256"]=hashlib.sha256(source.read(4097)).hexdigest()
def wait_route(url):
 p=urllib.parse.urlsplit(url)
 end=time.monotonic()+10
 while True:
  left=end-time.monotonic()
  if left<=0: raise TimeoutError("network readiness deadline")
  try:
   with socket.create_connection((p.hostname,p.port),timeout=min(.5,left)): return
  except OSError as error:
   if error.errno not in (101,113): raise
   left=end-time.monotonic()
   if left<=0: raise
   time.sleep(min(.25,left))
wait_route(sys.argv[1])
q=urllib.request.Request(sys.argv[1],data=d,headers=h,method="POST")
with urllib.request.urlopen(q,timeout=8) as response:
 if response.read(65)!=b"OK\\n": raise RuntimeError("upload acknowledgement")
'''


def clean_serial(data):
    return re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]', b'', data)


def fresh_prompt(data):
    return re.search(rb'root:[^\r\n]*[#\$] ', clean_serial(data)) is not None


def marked_command(text, marker):
    suffix = marker.removeprefix('WQ_DONE_')
    command = " m=WQ_DONE_; m=$m'" + suffix + "'; " + text + '; r=$?; echo; echo "$m:$r"\n'
    if marker in command or len(command) > 250:
        raise ValueError('unsafe or oversized serial command')
    return command


def command_complete(response, marker):
    # The literal completion marker is absent from the echoed command. Require
    # a complete status line and a new prompt after it, never serial silence.
    match = re.search(rb'(?:^|[\r\n])' + re.escape(marker.encode()) + rb':([0-9]+)[\r\n]',
                      clean_serial(response))
    if match is None or not fresh_prompt(clean_serial(response)[match.end():]):
        return None
    return int(match.group(1))


class KernelRingServer(HTTPServer):
    """One validated raw ring upload; the serial channel carries no payload."""
    def __init__(self, nonce):
        self.endpoint = '/kernel-ring/' + nonce
        self.raw = self.proof = None
        self.rejected = []
        self.active_connection = None
        super().__init__(('127.0.0.1', 0), KernelRingHandler)

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(3)
        self.active_connection = connection
        return connection, address

    def stop(self, thread):
        # Release this server's own in-progress body/header read before the
        # bounded serve_forever shutdown/join. Never touch other sockets.
        if self.active_connection is not None:
            try:
                self.active_connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        try:
            capture_cleanup.stop_server_loop(self, thread, timeout=8)
        finally:
            self.server_close()


class KernelRingHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        try:
            if self.path != self.server.endpoint or self.server.raw is not None:
                raise ValueError('wrong token or repeated upload')
            if self.headers.get_all('Transfer-Encoding'):
                raise ValueError('transfer encoding is not supported')
            def field(name):
                values = self.headers.get_all(name, [])
                if len(values) != 1:
                    raise ValueError('missing/duplicate ' + name)
                return values[0]
            size_text, capacity_text = field('Content-Length'), field('X-Klog-Capacity')
            if not re.fullmatch('[0-9]{1,7}', size_text) or not re.fullmatch('[0-9]{1,7}', capacity_text):
                raise ValueError('invalid kernel ring size header')
            size, capacity = int(size_text), int(capacity_text)
            if not 0 < size <= capacity <= KLOG_LIMIT:
                raise ValueError('kernel ring upload exceeds cap')
            sha, helper_sha = field('X-Klog-Sha256'), field('X-Klog-Helper-Sha256')
            if not re.fullmatch('[0-9a-f]{64}', sha) or helper_sha != hashlib.sha256(KLOG_PROGRAM.encode()).hexdigest():
                raise ValueError('invalid kernel ring or helper hash')
            raw = self.rfile.read(size)
            if len(raw) != size or hashlib.sha256(raw).hexdigest() != sha:
                raise ValueError('kernel ring upload hash/size mismatch')
            self.server.raw = raw
            self.server.proof = {'capacity': capacity, 'bytes': size, 'sha256': sha,
                                 'helper_sha256': helper_sha, 'transport': 'HTTP POST raw ring'}
            self.send_response(200)
            self.send_header('Content-Length', '3')
            self.end_headers()
            self.wfile.write(b'OK\n')
        except (OSError, ValueError) as error:
            if len(self.server.rejected) < 8:
                self.server.rejected.append(str(error))
            self.close_connection = True
            try:
                self.send_error(400, 'kernel ring upload rejected')
            except OSError:
                pass


def candidate_cases_complete(markers):
    cases = [line for line in markers if '] case=' in line]
    if len(cases) != 4:
        return False
    for index, line in enumerate(cases, 1):
        probes = 4 if index == 4 else 1
        if not (f'case={index} probes={probes} result=PASS phase=progress-before-release before_release={probes} ' in line
                and line.endswith(' drained=1')):
            return False
    return sum('END result=PASS cases=4' in line for line in markers) == 1


def record_markers(raw):
    # printf.c prefixes new lines with decimal r_time(): "[123] ". The
    # timestamp is part of the kernel ring too, not just the console stream.
    return [match.group(1).decode(errors='strict') for match in
            re.finditer(rb'(?m)^(?:\[[0-9]{1,20}\] )?(\[workqueue-selftest\][^\r\n]+)\r?$', raw)]


def run(argv, timeout=15):
    return subprocess.run(argv, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel-variant', type=Path, required=True)
    parser.add_argument('--run-vm', action='store_true')
    args = parser.parse_args()
    import kernel_variant
    selected = kernel_variant.load_receipt(args.kernel_variant)
    kernel_variant.check_environment(selected, os.environ)
    identity = selected['identity']
    expected = 'FAIL' if identity['variant'] == 'baseline' else 'PASS'
    if not args.run_vm:
        print(json.dumps({'mode': 'NO-BOOT', 'expected': expected,
                          'kernel_variant': identity}))
        return 0

    before_inventory = run(['bash', 'scripts/launch/qemu-exact-inventory.sh',
                            '--require-zero'])
    if before_inventory.returncode:
        raise RuntimeError(before_inventory.stdout)
    token = 'workqueue-selftest-' + identity['variant'] + '-' + datetime.now(
        timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out = capture_cleanup.create_receipt(ROOT / 'build-x86_64/gui-progress-audit' / token)
    qmp_path = Path('/tmp/' + token + '.qmp')
    serial_path = Path('/tmp/' + token + '.serial')
    pidfile = out / 'qemu.pid'
    overlay = out / 'session.qcow2'
    base = capture_config.PROTECTED_BASE
    base_stamp = capture_config.owner.file_stamp(base.stat())
    env = os.environ.copy()
    env.update(KERNEL=identity['kernel_path'], FSIMG=str(base), AUTO_BUILD='0',
               BUILD_DIR=str(ROOT / 'build-x86_64'), DISPLAY_MODE='sdl',
               QEMU_GPU='virtio-vga-gl-primary', QEMU_CPUS='6', QEMU_MEMORY='8G',
               QEMU_INPUT='virtio', QEMU_SDL_VARIANT='apt', QEMU_SDL_APT_MODULES='corrected',
               QEMU_ALLOW_WSL_SDL_GL='1', QEMU_RUN_TOKEN=token,
               QEMU_PIDFILE=str(pidfile), QEMU_OVERLAY=str(overlay),
               QEMU_NATURAL_ZERO_TIMEOUT='0', QEMU_DISPLAY_REPORT='1',
               QEMU_WSL_SDL_FIT_WATCH_SECONDS='0',
               QEMU_SDL_APT_BIN=str(HELPERS / 'serial-qemu-wrapper.py'),
               QEMU_OPENGL_SERIAL_PATH=str(serial_path),
               QEMU_OPENGL_WRAPPED_ARGS=str(out / 'serial-wrapper-arguments.json'),
               QEMU_EXTRA=f'-S -qmp unix:{qmp_path},server=on,wait=off',
               QEMU_APPEND='root=/dev/disk0 netsurf=0 webkit=0 workqueue_selftest=1')
    receipt = {'kind': 'workqueue-selftest-run', 'schema_version': 1,
               'token': token, 'kernel_variant': identity, 'expected': expected,
               'base_path': str(base), 'base_stamp_before': base_stamp,
               'launch': ['bash', 'scripts/launch/launch-gui.sh'],
               'append_requested': env['QEMU_APPEND'],
               'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               'preflight_inventory': before_inventory.stdout,
               'passed': False}
    (out / 'provenance.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'phase': 'launch', 'receipt': str(out), 'expected': expected}), flush=True)
    launcher = None
    qsock = qfile = serial = None
    owned_pid = owned_start = None
    sequence = 0

    def qmp(command):
        nonlocal sequence
        sequence += 1
        qsock.sendall((json.dumps({'execute': command, 'id': sequence}) + '\n').encode())
        while True:
            raw = qfile.readline(65536)
            if not raw:
                raise RuntimeError('QMP EOF')
            reply = json.loads(raw)
            if reply.get('id') == sequence:
                if 'error' in reply:
                    raise RuntimeError(str(reply['error']))
                return reply.get('return')

    def cleanup_qemu():
        nonlocal owned_pid, owned_start
        if owned_pid is None and pidfile.is_file() and not pidfile.is_symlink():
            candidate = int(pidfile.read_text().strip())
            candidate_stat = Path('/proc') / str(candidate) / 'stat'
            if candidate_stat.exists():
                stat = candidate_stat.read_text()
                owned_pid = candidate
                owned_start = stat.rsplit(') ', 1)[1].split()[19]
        if owned_pid is None or owned_start is None:
            return
        check = run(['bash', 'scripts/launch/cleanup-owned-qemu.sh', '--check-only',
                     str(owned_pid), owned_start, token])
        if check.returncode == 0:
            result = run(['bash', 'scripts/launch/cleanup-owned-qemu.sh',
                          str(owned_pid), owned_start, token], timeout=20)
            if result.returncode:
                raise RuntimeError(result.stdout)

    def read_kernel_ring(data, serial_log):
        # syslog action 3 snapshots without clearing. Kernel printf records are
        # serialized in klog before asynchronous console/user output can mix.
        # This fallback is only for missing console case records after END PASS.
        deadline = time.monotonic() + 120
        receipt['kernel_ring_readback'] = {'deadline_seconds': 120, 'commands': []}
        (out / 'kernel-ring-helper.py').write_text(KLOG_PROGRAM)

        def receive():
            if launcher.poll() is not None:
                raise RuntimeError('VM ended during kernel ring readback')
            try:
                chunk = serial.recv(65536)
            except socket.timeout:
                return
            if not chunk:
                raise RuntimeError('serial EOF during kernel ring readback')
            if len(data) + len(chunk) > SERIAL_LIMIT:
                raise RuntimeError('serial capture exceeds 16 MiB cap')
            data.extend(chunk)
            serial_log.write(chunk)
            serial_log.flush()

        prompt_deadline = min(deadline, time.monotonic() + 60)
        while not fresh_prompt(data[-16384:]):
            if time.monotonic() >= prompt_deadline:
                raise TimeoutError('kernel ring initial fresh-prompt deadline')
            receive()
        server = KernelRingServer(secrets.token_hex(16))
        thread = threading.Thread(target=server.serve_forever, name='workqueue-ring-http', daemon=True)
        url = 'http://10.0.2.2:' + str(server.server_port) + server.endpoint
        receipt['kernel_ring_readback']['http_endpoint'] = url
        try:
            thread.start()
            commands = [': > /tmp/wq-klog.py']
            commands += ["printf '%s\\n' " + shlex.quote(line) + ' >> /tmp/wq-klog.py'
                         for line in KLOG_PROGRAM.splitlines()]
            commands.append('/usr/bin/python3 /tmp/wq-klog.py ' + shlex.quote(url))
            for index, text in enumerate(commands):
                marker = 'WQ_DONE_' + str(index) + '_' + str(time.monotonic_ns())
                command = marked_command(text, marker)
                label = 'kernel-ring-command-' + str(index)
                (out / (label + '.sh')).write_text(command)
                row = {'command': label + '.sh', 'output': label + '.txt', 'marker': marker}
                receipt['kernel_ring_readback']['commands'].append(row)
                offset = len(data)
                # Final helper snapshots first, then allows <=10s for DHCP/route
                # readiness and one <=8s HTTP request. Other steps stay at15s.
                step_timeout = 25 if index == len(commands) - 1 else 15
                row['timeout_seconds'] = step_timeout
                step_deadline = min(deadline, time.monotonic() + step_timeout)
                try:
                    # Leading space absorbs the known first-character drop after
                    # the bracketed-paste prompt; a marker is assembled in-shell.
                    serial.sendall(command.encode())
                    while time.monotonic() < step_deadline:
                        status = command_complete(bytes(data[offset:]), marker)
                        if status is not None:
                            row['exit_code'] = status
                            if status != 0:
                                raise RuntimeError('kernel ring guest command failed: ' + label)
                            break
                        receive()
                    else:
                        raise TimeoutError('kernel ring command completion/fresh-prompt deadline: ' + label)
                finally:
                    response = bytes(data[offset:])
                    (out / (label + '.txt')).write_bytes(response)
                    row['output_bytes'] = len(response)
            if server.raw is None or server.proof is None:
                raise RuntimeError('no verified HTTP kernel ring upload')
            return server.raw
        finally:
            try:
                # Preserve an accepted upload even if its later serial status
                # marker was damaged; that run still fails the completion gate.
                if server.raw is not None and server.proof is not None:
                    (out / 'kernel-ring.bin').write_bytes(server.raw)
                    receipt['kernel_ring_readback'].update(server.proof)
            finally:
                receipt['kernel_ring_readback']['http_rejected'] = server.rejected
                try:
                    server.stop(thread)
                    receipt['kernel_ring_readback']['http_thread_reaped'] = not thread.is_alive()
                except BaseException as error:
                    receipt['kernel_ring_readback']['http_cleanup_error'] = str(error)
                    raise

    with (out / 'launcher.log').open('wb') as log, (out / 'serial.log').open('wb') as serial_log:
        try:
            kernel_variant.check_prelaunch(selected)
            launcher = subprocess.Popen(receipt['launch'], cwd=ROOT, env=env,
                                        stdout=log, stderr=subprocess.STDOUT,
                                        start_new_session=True)
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if pidfile.is_file() and qmp_path.exists() and serial_path.exists():
                    owned_pid = int(pidfile.read_text().strip())
                    try:
                        stat = (Path('/proc') / str(owned_pid) / 'stat').read_text()
                        owned_start = stat.rsplit(') ', 1)[1].split()[19]
                    except FileNotFoundError:
                        owned_pid = None
                        continue
                    check = run(['bash', 'scripts/launch/cleanup-owned-qemu.sh', '--check-only',
                                 str(owned_pid), owned_start, token])
                    if check.returncode == 0:
                        break
                if launcher.poll() is not None:
                    raise RuntimeError('launcher exited before ownership/connect')
                time.sleep(.05)
            else:
                raise TimeoutError('owned QEMU/socket deadline')
            receipt['owned_pid'] = owned_pid
            receipt['owned_start_ticks'] = owned_start
            receipt['qemu_cmdline'] = (Path('/proc') / str(owned_pid) / 'cmdline').read_bytes().decode().split('\0')[:-1]
            command = receipt['qemu_cmdline']
            if command.count('-kernel') != 1 or command[command.index('-kernel') + 1] != identity['kernel_path']:
                raise RuntimeError('actual QEMU kernel differs from receipt')
            if command.count('-append') != 1 or '-S' not in command:
                raise RuntimeError('missing actual QEMU append or paused start')
            append = command[command.index('-append') + 1].split()
            if [arg for arg in append if arg.startswith('workqueue_selftest=')] != ['workqueue_selftest=1']:
                raise RuntimeError('actual QEMU selftest argument mismatch')
            serial = socket.socket(socket.AF_UNIX)
            serial.settimeout(1)
            serial.connect(str(serial_path))
            qsock = socket.socket(socket.AF_UNIX)
            qsock.settimeout(5)
            qsock.connect(str(qmp_path))
            qfile = qsock.makefile('rb')
            receipt['qmp_greeting'] = json.loads(qfile.readline(65536))
            qmp('qmp_capabilities')
            qmp('cont')
            # -S ensures every serial byte is observable before the test starts.
            # No serial commands or silence-based completion verdicts are used.
            deadline = time.monotonic() + 180
            data = bytearray()
            terminal = None
            while time.monotonic() < deadline:
                try:
                    chunk = serial.recv(65536)
                except socket.timeout:
                    chunk = None
                if chunk:
                    serial_log.write(chunk)
                    serial_log.flush()
                    data.extend(chunk)
                    if len(data) > SERIAL_LIMIT:
                        raise RuntimeError('serial capture exceeds 16 MiB cap')
                    terminal = re.search(rb'\[workqueue-selftest\] END result=(PASS|FAIL)[^\r\n]*[\r\n]', data)
                    if terminal:
                        break
                elif chunk == b'':
                    raise RuntimeError('serial EOF before selftest verdict')
                if launcher.poll() is not None:
                    raise RuntimeError('VM exited before selftest verdict')
            if terminal is None:
                receipt['qmp_status_at_deadline'] = qmp('query-status')
                raise TimeoutError('180-second selftest completion deadline')
            receipt['markers'] = record_markers(data)
            receipt['console_markers'] = list(receipt['markers'])
            receipt['marker_source'] = 'serial-console'
            receipt['observed'] = terminal.group(1).decode()
            receipt['qmp_status_after_test'] = qmp('query-status')
            if expected == 'FAIL':
                correct = any('case=1 probes=1 result=FAIL phase=progress-before-release before_release=0 workers=2 idle=1 running=1 pending=1 drained=1' in line for line in receipt['markers'])
            else:
                correct = candidate_cases_complete(receipt['markers'])
                if receipt['observed'] == 'PASS' and not correct:
                    print(json.dumps({'phase': 'kernel-ring-readback', 'reason': 'incomplete console case records'}), flush=True)
                    ring = read_kernel_ring(data, serial_log)
                    receipt['markers'] = record_markers(ring)
                    receipt['marker_source'] = 'kernel-ring-readback'
                    correct = candidate_cases_complete(receipt['markers'])
            receipt['test_matches_expectation'] = receipt['observed'] == expected and correct
            print(json.dumps({'phase': 'selftest-result', 'markers': receipt['markers'],
                              'matches_expectation': receipt['test_matches_expectation']}), flush=True)
        except BaseException as error:
            receipt['error'] = {'type': type(error).__name__, 'message': str(error)}
            print(json.dumps({'phase': 'error', **receipt['error']}), flush=True)
        finally:
            cleanup_errors = []
            def cleanup_step(label, operation):
                try:
                    return operation()
                except BaseException as error:
                    cleanup_errors.append({'step': label, 'type': type(error).__name__, 'message': str(error)})
                    return None
            if launcher is not None:
                receipt['cleanup'] = cleanup_step('stop_launcher', lambda: capture_cleanup.stop_owned_launcher(
                    launcher, lambda: qmp('quit') if qsock is not None else cleanup_qemu(), cleanup_qemu))
                if receipt['cleanup']:
                    cleanup_errors.extend(receipt['cleanup']['errors'])
            for connection in (qfile, qsock, serial):
                if connection is not None:
                    cleanup_step('close_connection', connection.close)
            final = cleanup_step('final_inventory', lambda: run(['bash', 'scripts/launch/qemu-exact-inventory.sh', '--require-zero']))
            receipt['final_inventory'] = None if final is None else {'returncode': final.returncode, 'output': final.stdout}
            receipt['base_unchanged'] = cleanup_step('base_stamp', lambda: capture_config.owner.file_stamp(base.stat()) == base_stamp)
            receipt['overlay_removed'] = not os.path.lexists(overlay)
            cleanup_step('kernel_variant_stamps', lambda: kernel_variant.check_prelaunch(selected))
            for path in (qmp_path, serial_path):
                cleanup_step('remove_socket', lambda path=path: path.unlink() if path.exists() and path.is_socket() else None)
            receipt['cleanup_errors'] = cleanup_errors
            receipt['passed'] = bool(receipt.get('test_matches_expectation') and
                                     receipt.get('cleanup') and receipt['cleanup']['clean_exit'] and
                                     receipt['cleanup']['reaped'] and final is not None and
                                     final.returncode == 0 and receipt['base_unchanged'] and
                                     receipt['overlay_removed'] and not receipt.get('error') and not cleanup_errors)
            cleanup_step('result_write', lambda: (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n'))
            if cleanup_errors:
                receipt['passed'] = False
            print(json.dumps({'phase': 'finished', 'passed': receipt['passed'],
                              'result': str(out / 'result.json'),
                              'final_inventory': receipt['final_inventory'],
                              'cleanup_errors': cleanup_errors}), flush=True)
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
