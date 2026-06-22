#!/usr/bin/env python3
import argparse
import ctypes
import os
import re
import subprocess
import sys
import time


class Monitor:
    def __init__(self, index, name, x, y, w, h):
        self.index = index
        self.name = name
        self.x = x
        self.y = y
        self.w = w
        self.h = h

    def contains(self, x, y):
        return self.x <= x < self.x + self.w and self.y <= y < self.y + self.h


def read_monitors():
    try:
        out = subprocess.check_output(
            ["xrandr", "--listmonitors"], text=True, stderr=subprocess.DEVNULL
        )
    except Exception:
        return []

    monitors = []
    pattern = re.compile(
        r"^\s*(\d+):\s+\S+\s+(\d+)/\d+x(\d+)/\d+\+(-?\d+)\+(-?\d+)\s+(\S+)"
    )
    for line in out.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        index, w, h, x, y, name = match.groups()
        monitors.append(
            Monitor(int(index), name, int(x), int(y), int(w), int(h))
        )
    return monitors


class X11:
    XA_CARDINAL = 6
    ANY_PROPERTY_TYPE = 0

    def __init__(self):
        self.x11 = ctypes.CDLL("libX11.so.6")
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultRootWindow.restype = ctypes.c_ulong
        self.x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.x11.XInternAtom.restype = ctypes.c_ulong
        self.x11.XQueryTree.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
            ctypes.POINTER(ctypes.c_uint),
        ]
        self.x11.XQueryTree.restype = ctypes.c_int
        self.x11.XGetWindowProperty.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_ulong,
            ctypes.c_long,
            ctypes.c_long,
            ctypes.c_int,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
        ]
        self.x11.XGetWindowProperty.restype = ctypes.c_int
        self.x11.XFree.argtypes = [ctypes.c_void_p]
        self.x11.XFree.restype = ctypes.c_int
        self.x11.XMoveWindow.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_int,
            ctypes.c_int,
        ]
        self.x11.XMoveWindow.restype = ctypes.c_int
        self.x11.XMapRaised.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XMapRaised.restype = ctypes.c_int
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.x11.XFlush.restype = ctypes.c_int
        self.x11.XQueryPointer.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_uint),
        ]
        self.x11.XQueryPointer.restype = ctypes.c_int

        self.display = self.x11.XOpenDisplay(os.environ.get("DISPLAY", "").encode() or None)
        if not self.display:
            raise RuntimeError("cannot open X display")
        self.root = self.x11.XDefaultRootWindow(self.display)
        self.atoms = {}

    def atom(self, name):
        if name not in self.atoms:
            self.atoms[name] = self.x11.XInternAtom(self.display, name.encode(), 1)
        return self.atoms[name]

    def pointer_xy(self):
        root_return = ctypes.c_ulong()
        child_return = ctypes.c_ulong()
        root_x = ctypes.c_int()
        root_y = ctypes.c_int()
        win_x = ctypes.c_int()
        win_y = ctypes.c_int()
        mask = ctypes.c_uint()
        ok = self.x11.XQueryPointer(
            self.display,
            self.root,
            ctypes.byref(root_return),
            ctypes.byref(child_return),
            ctypes.byref(root_x),
            ctypes.byref(root_y),
            ctypes.byref(win_x),
            ctypes.byref(win_y),
            ctypes.byref(mask),
        )
        if ok == 0:
            return None
        return root_x.value, root_y.value

    def get_property_bytes(self, window, atom_name, property_type=ANY_PROPERTY_TYPE):
        atom = self.atom(atom_name)
        if atom == 0:
            return None
        actual_type = ctypes.c_ulong()
        actual_format = ctypes.c_int()
        nitems = ctypes.c_ulong()
        bytes_after = ctypes.c_ulong()
        data = ctypes.POINTER(ctypes.c_ubyte)()
        status = self.x11.XGetWindowProperty(
            self.display,
            window,
            atom,
            0,
            4096,
            0,
            property_type,
            ctypes.byref(actual_type),
            ctypes.byref(actual_format),
            ctypes.byref(nitems),
            ctypes.byref(bytes_after),
            ctypes.byref(data),
        )
        if status != 0 or not data:
            return None
        try:
            if actual_format.value == 8:
                size = nitems.value
            elif actual_format.value == 32:
                size = nitems.value * ctypes.sizeof(ctypes.c_ulong)
            else:
                size = nitems.value * max(1, actual_format.value // 8)
            return ctypes.string_at(data, size)
        finally:
            self.x11.XFree(data)

    def get_string(self, window, atom_name):
        data = self.get_property_bytes(window, atom_name)
        if not data:
            return ""
        return data.split(b"\0", 1)[0].decode("utf-8", "replace")

    def get_pid(self, window):
        data = self.get_property_bytes(window, "_NET_WM_PID", self.XA_CARDINAL)
        if not data:
            return None
        if len(data) >= ctypes.sizeof(ctypes.c_ulong):
            return ctypes.c_ulong.from_buffer_copy(data[: ctypes.sizeof(ctypes.c_ulong)]).value
        if len(data) >= 4:
            return int.from_bytes(data[:4], sys.byteorder)
        return None

    def children(self, window):
        root = ctypes.c_ulong()
        parent = ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        nchildren = ctypes.c_uint()
        ok = self.x11.XQueryTree(
            self.display,
            window,
            ctypes.byref(root),
            ctypes.byref(parent),
            ctypes.byref(children),
            ctypes.byref(nchildren),
        )
        if ok == 0 or not children:
            return []
        try:
            return [children[i] for i in range(nchildren.value)]
        finally:
            self.x11.XFree(children)

    def find_window(self, title, pid):
        stack = [self.root]
        title = title or ""
        while stack:
            window = stack.pop()
            if window != self.root:
                wm_name = self.get_string(window, "WM_NAME")
                net_name = self.get_string(window, "_NET_WM_NAME")
                window_pid = self.get_pid(window)
                title_match = title and (title in wm_name or title in net_name)
                pid_match = pid is not None and window_pid == pid
                if title_match or pid_match:
                    return window
            stack.extend(reversed(self.children(window)))
        return None

    def move_window(self, window, x, y):
        self.x11.XMoveWindow(self.display, window, x, y)
        self.x11.XMapRaised(self.display, window)
        self.x11.XFlush(self.display)


def choose_monitor(monitors, x11, selector):
    if not monitors:
        return Monitor(0, "root", 0, 0, 0, 0)
    if selector in ("", "pointer", "current"):
        xy = x11.pointer_xy()
        if xy:
            for monitor in monitors:
                if monitor.contains(*xy):
                    return monitor
    for monitor in monitors:
        if selector == monitor.name or selector == str(monitor.index):
            return monitor
    return monitors[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", default="xv6-os QEMU")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--monitor", default="pointer")
    parser.add_argument("--x-offset", type=int, default=0)
    parser.add_argument("--y-offset", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()

    x11 = X11()
    monitor = choose_monitor(read_monitors(), x11, args.monitor)
    deadline = time.monotonic() + args.timeout
    window = None
    while time.monotonic() < deadline:
        window = x11.find_window(args.title, args.pid)
        if window is not None:
            break
        time.sleep(0.1)
    if window is None:
        print(
            f"place-qemu-window: no QEMU window found title={args.title!r} pid={args.pid}",
            file=sys.stderr,
        )
        return 1

    x = monitor.x + args.x_offset
    y = monitor.y + args.y_offset
    x11.move_window(window, x, y)
    print(
        f"place-qemu-window: moved title={args.title!r} pid={args.pid} "
        f"monitor={monitor.name} geometry={monitor.w}x{monitor.h}+{monitor.x}+{monitor.y} "
        f"target={x},{y}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
