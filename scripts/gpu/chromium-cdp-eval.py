#!/usr/bin/env python3
"""Evaluate one expression in a Chromium page through the local CDP socket.

This intentionally uses only the Python standard library so the same helper
can run on a minimal WSL host.  It accepts only an unencrypted loopback CDP
endpoint and emits one compact JSON object suitable for retained receipts.
"""

import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time
import urllib.parse
import urllib.error
import urllib.request


def fail(reason: str) -> None:
    print(json.dumps({"status": "FAIL", "reason": reason}, separators=(",", ":")))
    raise SystemExit(2)


def choose_target(port: int, url_substring: str, timeout: float) -> dict:
    deadline = time.monotonic() + timeout
    endpoint = f"http://127.0.0.1:{port}/json"
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(endpoint, timeout=2) as response:
                targets = json.load(response)
            matches = [target for target in targets
                       if target.get("type") == "page" and
                       url_substring in target.get("url", "") and
                       target.get("webSocketDebuggerUrl", "").startswith("ws://")]
            if len(matches) == 1:
                return matches[0]
        except (OSError, ValueError, urllib.error.URLError):
            pass
        time.sleep(0.25)
    fail("unique-target-not-found")


def read_exact(sock: socket.socket, count: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < count:
        chunk = sock.recv(count - len(chunks))
        if not chunk:
            raise EOFError("websocket closed")
        chunks.extend(chunk)
    return bytes(chunks)


def read_frame(sock: socket.socket) -> tuple[int, bytes]:
    first, second = read_exact(sock, 2)
    opcode = first & 0x0F
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", read_exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", read_exact(sock, 8))[0]
    mask = read_exact(sock, 4) if second & 0x80 else b""
    payload = bytearray(read_exact(sock, length))
    if mask:
        for index in range(length):
            payload[index] ^= mask[index % 4]
    return opcode, bytes(payload)


def send_frame(sock: socket.socket, opcode: int, payload: bytes) -> None:
    mask = os.urandom(4)
    length = len(payload)
    header = bytearray([0x80 | opcode])
    if length < 126:
        header.append(0x80 | length)
    elif length <= 0xFFFF:
        header.append(0x80 | 126)
        header.extend(struct.pack("!H", length))
    else:
        header.append(0x80 | 127)
        header.extend(struct.pack("!Q", length))
    header.extend(mask)
    header.extend(bytes(value ^ mask[index % 4]
                        for index, value in enumerate(payload)))
    sock.sendall(header)


def websocket_eval(ws_url: str, expression: str, timeout: float) -> dict:
    parsed = urllib.parse.urlparse(ws_url)
    if parsed.scheme != "ws" or parsed.hostname not in {"127.0.0.1", "localhost"}:
        fail("non-loopback-websocket")
    port = parsed.port or 80
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (f"GET {parsed.path or '/'} HTTP/1.1\r\n"
               f"Host: {parsed.hostname}:{port}\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
    with socket.create_connection((parsed.hostname, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(request.encode("ascii"))
        response = bytearray()
        while b"\r\n\r\n" not in response and len(response) < 16384:
            response.extend(sock.recv(4096))
        header, _, remainder = bytes(response).partition(b"\r\n\r\n")
        if remainder or not header.startswith(b"HTTP/1.1 101"):
            fail("websocket-handshake-invalid")
        expected = base64.b64encode(hashlib.sha1(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
        ).digest()).decode("ascii")
        if f"sec-websocket-accept: {expected}".lower() not in header.decode("latin1").lower():
            fail("websocket-accept-invalid")
        command = {
            "id": 1,
            "method": "Runtime.evaluate",
            "params": {
                "expression": expression,
                "awaitPromise": True,
                "returnByValue": True,
                "userGesture": True,
            },
        }
        send_frame(sock, 1, json.dumps(command, separators=(",", ":")).encode())
        while True:
            opcode, payload = read_frame(sock)
            if opcode == 8:
                fail("websocket-closed-before-response")
            if opcode == 9:
                send_frame(sock, 10, payload)
                continue
            if opcode != 1:
                continue
            message = json.loads(payload)
            if message.get("id") == 1:
                return message


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--url-contains", default="youtube.com/watch")
    parser.add_argument("--expression", required=True)
    parser.add_argument("--timeout", type=float, default=20.0)
    args = parser.parse_args()
    if not (1024 <= args.port <= 65535) or not (0.1 <= args.timeout <= 120):
        fail("argument-range-invalid")
    try:
        target = choose_target(args.port, args.url_contains, args.timeout)
        message = websocket_eval(target["webSocketDebuggerUrl"], args.expression,
                                 args.timeout)
    except (EOFError, OSError, ValueError, json.JSONDecodeError):
        fail("cdp-transport-error")
    if "error" in message:
        fail("cdp-command-error")
    result = message.get("result", {}).get("result", {})
    if result.get("subtype") == "error" or "exceptionDetails" in message.get("result", {}):
        fail("javascript-exception")
    print(json.dumps({
        "status": "PASS",
        "target_url": target.get("url", ""),
        "value": result.get("value"),
    }, separators=(",", ":"), sort_keys=True))


if __name__ == "__main__":
    main()
