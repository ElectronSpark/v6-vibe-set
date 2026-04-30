set pagination off
set confirm off
set print pretty on

python
import gdb

_STATE_NAMES = {
    0: "UNUSED",
    1: "USED",
    2: "INTERRUPTIBLE",
    3: "KILLABLE",
    4: "TIMER",
    5: "KILLABLE_TIMER",
    6: "UNINTERRUPTIBLE",
    7: "WAKENING",
    8: "RUNNING",
    9: "STOPPED",
    10: "EXITING",
    11: "ZOMBIE",
}

_SYSCALL_NAMES = {
    22: "read",
    23: "write",
    64: "poll",
    65: "kqueue",
    66: "kevent_register",
    67: "kevent_wait",
    945: "ppoll",
    981: "timerfd_gettime",
    982: "timerfd_settime",
    983: "timerfd_create",
    985: "eventfd2",
    986: "epoll_pwait",
    987: "epoll_ctl",
    988: "epoll_create1",
}

def _u64(value):
    return int(value.cast(gdb.lookup_type("unsigned long")))

def _symbol(addr):
    if addr == 0:
        return "-"
    try:
        text = gdb.execute(f"info symbol 0x{addr:x}", to_string=True).strip()
        if text and "No symbol" not in text:
            return text
    except gdb.error:
        pass
    return f"0x{addr:x}"

def _eval_int(expr, default=None):
    try:
        return int(gdb.parse_and_eval(expr))
    except gdb.error:
        return default

def _field(value, name, default=None):
    try:
        return value[name]
    except gdb.error:
        return default

def _offset(type_name, field_name):
    return int(gdb.parse_and_eval(f"(unsigned long)&(({type_name} *)0)->{field_name}"))

def _iter_threads():
    proc_table = gdb.parse_and_eval("proc_table")
    head = proc_table["procs_list"]
    node = head["next"]
    head_addr = int(head.address)
    offset = _offset("struct thread", "dmp_list_entry")
    thread_type = gdb.lookup_type("struct thread").pointer()
    while int(node) != head_addr:
        thread_addr = int(node) - offset
        yield gdb.Value(thread_addr).cast(thread_type).dereference()
        node = node["next"]

def _find_thread(selector):
    selector = selector.strip()
    if not selector:
        selector = "wlcomp"
    want_pid = None
    try:
        want_pid = int(selector, 0)
    except ValueError:
        pass

    for thread in _iter_threads():
        pid = int(thread["pid"])
        name = thread["name"].string(errors="ignore")
        if want_pid is not None and pid == want_pid:
            return thread
        if want_pid is None and name == selector:
            return thread
    return None

def _count_list(head):
    try:
        node = head["next"]
        head_addr = int(head.address)
        count = 0
        while int(node) != head_addr and count < 1024:
            count += 1
            node = node["next"]
        return count
    except gdb.error:
        return -1

def _dev_major(dev):
    return (dev >> 20) & 0xfff

def _dev_minor(dev):
    return dev & 0xfffff

def _file_label(file_value):
    try:
        inode_ref = file_value["inode"]
        inode = inode_ref["inode"]
        if int(inode) != 0:
            inode_d = inode.dereference()
            mode = int(inode_d["mode"])
            if mode & 0x2000:  # S_IFCHR
                dev = int(inode_d["cdev"])
                return f"cdev { _dev_major(dev) }:{ _dev_minor(dev) }"
            if mode & 0x1000:
                return "fifo"
            if mode & 0x4000:
                return "dir"
            if mode & 0x8000:
                return "file"
            return f"mode=0x{mode:x}"
        if int(file_value["ops"]) == int(gdb.parse_and_eval("&kqueue_file_ops")):
            return "kqueue"
        if int(file_value["private_data"]) != 0:
            return "custom"
    except gdb.error:
        pass
    return "unknown"

def _fd_for_file(thread, file_addr):
    try:
        fdtable = thread["fdtable"]
        if int(fdtable) == 0:
            return -1
        files = fdtable.dereference()["files"]
        for fd in range(512):
            if int(files[fd]) == file_addr:
                return fd
    except gdb.error:
        pass
    return -1

def _dump_knote(kn, owner_thread=None, prefix="    "):
    try:
        ident = int(kn["ident"])
        filt = int(kn["filter"])
        flags = int(kn["flags"])
        status = int(kn["status"])
        data = int(kn["data"])
        attached_file = int(kn["attached_file"])
        fd_hint = ""
        if owner_thread is not None and attached_file != 0:
            fd = _fd_for_file(owner_thread, attached_file)
            if fd >= 0:
                fd_hint = f" attached_fd={fd}"
        print(f"{prefix}kn ident={ident} filter={filt} flags=0x{flags:x} status=0x{status:x} data={data} file=0x{attached_file:016x}{fd_hint}")
    except gdb.error as exc:
        print(f"{prefix}knote unavailable ({exc})")

def _dump_kqueue(kq, owner_thread=None, prefix="  "):
    try:
        print(f"{prefix}kqueue=0x{int(kq.address):016x} nreg={int(kq['nregistered'])} nready={int(kq['nready'])} waiters={int(kq['waiters'])} closed={int(kq['closed'])} waitq_count={int(kq['waitq']['counter'])}")
        head = kq["registered"]
        node = head["next"]
        head_addr = int(head.address)
        offset = _offset("struct knote", "kq_entry")
        knote_type = gdb.lookup_type("struct knote").pointer()
        count = 0
        while int(node) != head_addr and count < 32:
            kn_addr = int(node) - offset
            kn = gdb.Value(kn_addr).cast(knote_type).dereference()
            _dump_knote(kn, owner_thread, prefix + "  ")
            node = node["next"]
            count += 1
        if count == 0:
            print(prefix + "  (no registered knotes)")
    except gdb.error as exc:
        print(f"{prefix}kqueue unavailable ({exc})")

class Xv6Threads(gdb.Command):
    "Dump live xv6 thread state from proc_table."

    def __init__(self):
        super(Xv6Threads, self).__init__("xv6-threads", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            proc_table = gdb.parse_and_eval("proc_table")
            head = proc_table["procs_list"]
            node = head["next"]
            offset = int(gdb.parse_and_eval("(unsigned long)&((struct thread *)0)->dmp_list_entry"))
            print("PID   TGID  CPU  STATE             ONRQ ONCPU CHAN               RA                 NAME")
            while int(node) != int(head.address):
                thread_addr = int(node) - offset
                thread = gdb.Value(thread_addr).cast(gdb.lookup_type("struct thread").pointer()).dereference()
                se = thread["sched_entity"]
                state = int(thread["state"])
                pid = int(thread["pid"])
                tgid = int(thread["tgid"])
                chan = int(thread["chan"])
                name = thread["name"].string(errors="ignore")
                if int(se) != 0:
                    se_d = se.dereference()
                    cpu = int(se_d["cpu_id"])
                    on_rq = int(se_d["on_rq"])
                    on_cpu = int(se_d["on_cpu"])
                    ra = int(se_d["context"]["ra"])
                else:
                    cpu = -1
                    on_rq = 0
                    on_cpu = 0
                    ra = 0
                print(f"{pid:<5} {tgid:<5} {cpu:<4} {_STATE_NAMES.get(state, state)!s:<17} {on_rq:<4} {on_cpu:<5} 0x{chan:016x} 0x{ra:016x} {name}")
                node = node["next"]
        except gdb.error as exc:
            print(f"xv6-threads failed: {exc}")

class Xv6Cpus(gdb.Command):
    "Dump xv6 per-CPU current and idle thread pointers."

    def __init__(self):
        super(Xv6Cpus, self).__init__("xv6-cpus", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            ncpu = int(gdb.parse_and_eval("platform.ncpu"))
        except gdb.error:
            ncpu = 8
        cpus = gdb.parse_and_eval("cpus")
        print("CPU  CURRENT             IDLE                FLAGS NOFF SPIN TOTAL BUSY UTIL")
        for cpu in range(ncpu):
            c = cpus[cpu]
            current = int(c["proc"])
            idle = int(c["idle_thread"])
            flags = int(c["flags"])
            noff = int(c["noff"])
            spin = int(c["spin_depth"])
            total = int(c["total_ticks"])
            busy = int(c["busy_ticks"])
            util = int(c["util_1s"])
            print(f"{cpu:<4} 0x{current:016x}  0x{idle:016x}  0x{flags:x} {noff:<4} {spin:<4} {total:<5} {busy:<5} {util}")

class Xv6Input(gdb.Command):
    "Dump PS/2/vmmouse input counters and ring positions."

    def __init__(self):
        super(Xv6Input, self).__init__("xv6-input", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        names = [
            "dbg_mouse_irqs",
            "dbg_mouse_bytes",
            "dbg_mouse_outofsync",
            "dbg_mouse_packets",
            "dbg_mouse_overflow",
            "dbg_mouse_ringpush",
            "dbg_mouse_reads",
            "dbg_mouse_reads_ok",
            "dbg_vmmouse_status_err",
            "dbg_vmmouse_partial",
            "dbg_vmmouse_abs_reqs",
            "dbg_vmmouse_fallbacks",
            "vmmouse_available",
        ]
        print("Mouse counters:")
        for name in names:
            value = _eval_int(name)
            if value is None:
                print(f"  {name}: unavailable")
            else:
                print(f"  {name}: {value}")

        for prefix, expr in (("mouse", "mouse_state"), ("kbd", "kbd_state")):
            try:
                state = gdb.parse_and_eval(expr)
                head = int(state["head"])
                tail = int(state["tail"])
                print(f"{prefix} ring: head={head} tail={tail}")
            except gdb.error as exc:
                print(f"{prefix} ring: unavailable ({exc})")

class Xv6Chan(gdb.Command):
    "Dump sleeping xv6 threads grouped by wait channel pointer."

    def __init__(self):
        super(Xv6Chan, self).__init__("xv6-chan", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            proc_table = gdb.parse_and_eval("proc_table")
            head = proc_table["procs_list"]
            node = head["next"]
            offset = int(gdb.parse_and_eval("(unsigned long)&((struct thread *)0)->dmp_list_entry"))
            print("PID   STATE             CHAN               SYMBOL / NAME")
            while int(node) != int(head.address):
                thread_addr = int(node) - offset
                thread = gdb.Value(thread_addr).cast(gdb.lookup_type("struct thread").pointer()).dereference()
                chan = int(thread["chan"])
                if chan != 0:
                    pid = int(thread["pid"])
                    state = int(thread["state"])
                    name = thread["name"].string(errors="ignore")
                    print(f"{pid:<5} {_STATE_NAMES.get(state, state)!s:<17} 0x{chan:016x} {_symbol(chan)} / {name}")
                node = node["next"]
        except gdb.error as exc:
            print(f"xv6-chan failed: {exc}")

class Xv6Timers(gdb.Command):
    "Dump scheduler timer state and the first few pending timers."

    def __init__(self):
        super(Xv6Timers, self).__init__("xv6-timers", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            sched_ms = _eval_int("__sched_timer_ms")
            jiffies = _eval_int("ticks")
            timer = gdb.parse_and_eval("__sched_timer")
            current_tick = int(timer["current_tick"])
            next_tick = int(timer["next_tick"])
            valid = int(timer["valid"])
            print("Scheduler timer:")
            print(f"  sched_ms={sched_ms} jiffies={jiffies} current_tick={current_tick} next_tick={next_tick} valid={valid}")
            if sched_ms is not None and next_tick != 0:
                print(f"  next_delta_ms={next_tick - sched_ms}")

            head = timer["list_head"]
            node = head["next"]
            head_addr = int(head.address)
            offset = int(gdb.parse_and_eval("(unsigned long)&((struct timer_node *)0)->list_entry"))
            timer_type = gdb.lookup_type("struct timer_node").pointer()
            count = 0
            print("  pending timers:")
            while int(node) != head_addr and count < 8:
                timer_addr = int(node) - offset
                tn = gdb.Value(timer_addr).cast(timer_type).dereference()
                expires = int(tn["expires"])
                retry = int(tn["retry"])
                retry_limit = int(tn["retry_limit"])
                data = int(tn["data"])
                callback = int(tn["callback"])
                print(f"    #{count} expires={expires} delta={expires - sched_ms if sched_ms is not None else '?'} retry={retry}/{retry_limit} data=0x{data:016x} callback={_symbol(callback)}")
                node = node["next"]
                count += 1
            if count == 0:
                print("    (none)")
        except gdb.error as exc:
            print(f"xv6-timers failed: {exc}")

class Xv6Kqueue(gdb.Command):
    "Dump fd, epoll/kqueue, and attached knote state for a pid or process name. Defaults to wlcomp."

    def __init__(self):
        super(Xv6Kqueue, self).__init__("xv6-kqueue", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            thread = _find_thread(arg)
            if thread is None:
                print(f"xv6-kqueue: no thread found for '{arg.strip() or 'wlcomp'}'")
                return

            pid = int(thread["pid"])
            name = thread["name"].string(errors="ignore")
            fdtable = thread["fdtable"]
            print(f"fd/kqueue state for {name} pid={pid} fdtable=0x{int(fdtable):016x}")
            if int(fdtable) == 0:
                return

            files = fdtable.dereference()["files"]
            kqueue_ops = int(gdb.parse_and_eval("&kqueue_file_ops"))
            knote_source_off = _offset("struct knote", "source_entry")
            knote_type = gdb.lookup_type("struct knote").pointer()

            for fd in range(512):
                file_ptr = files[fd]
                if int(file_ptr) == 0:
                    continue

                f = file_ptr.dereference()
                ops = int(f["ops"])
                private_data = int(f["private_data"])
                cdev = int(f["cdev"])
                ref_count = int(f["ref_count"])
                flags = int(f["f_flags"])
                knotes = _count_list(f["knote_list"])
                print(f"fd {fd:<3} file=0x{int(file_ptr):016x} ref={ref_count:<3} flags=0x{flags:x} ops=0x{ops:016x} priv=0x{private_data:016x} cdev=0x{cdev:016x} knotes={knotes:<3} {_file_label(f)}")

                if knotes > 0:
                    head = f["knote_list"]
                    node = head["next"]
                    head_addr = int(head.address)
                    count = 0
                    while int(node) != head_addr and count < 16:
                        kn_addr = int(node) - knote_source_off
                        kn = gdb.Value(kn_addr).cast(knote_type).dereference()
                        _dump_knote(kn, thread, "    ")
                        node = node["next"]
                        count += 1

                if ops == kqueue_ops and private_data != 0:
                    kq = gdb.Value(private_data).cast(gdb.lookup_type("struct kqueue").pointer()).dereference()
                    _dump_kqueue(kq, thread, "    ")
        except gdb.error as exc:
            print(f"xv6-kqueue failed: {exc}")

class Xv6Syscall(gdb.Command):
    "Dump the saved syscall trapframe for a pid or process name. Defaults to wlcomp."

    def __init__(self):
        super(Xv6Syscall, self).__init__("xv6-syscall", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            thread = _find_thread(arg)
            if thread is None:
                print(f"xv6-syscall: no thread found for '{arg.strip() or 'wlcomp'}'")
                return

            pid = int(thread["pid"])
            name = thread["name"].string(errors="ignore")
            tfp = thread["trapframe"]
            print(f"syscall trapframe for {name} pid={pid} trapframe=0x{int(tfp):016x}")
            if int(tfp) == 0:
                return
            tf = tfp.dereference()["trapframe"]
            num = int(tf["rax"])
            print(f"  nr={num} ({_SYSCALL_NAMES.get(num, 'unknown')}) rip=0x{int(tf['rip']):016x} rsp=0x{int(tf['rsp']):016x}")
            print("  x86_64 args: rdi=a0 rsi=a1 rdx=a2 r10=a3 r8=a4 r9=a5")
            print(f"  a0/rdi=0x{int(tf['rdi']):016x} ({int(tf['rdi'])})")
            print(f"  a1/rsi=0x{int(tf['rsi']):016x} ({int(tf['rsi'])})")
            print(f"  a2/rdx=0x{int(tf['rdx']):016x} ({int(tf['rdx'])})")
            print(f"  a3/r10=0x{int(tf['r10']):016x} ({int(tf['r10'])})")
            print(f"  a4/r8 =0x{int(tf['r8']):016x} ({int(tf['r8'])})")
            print(f"  a5/r9 =0x{int(tf['r9']):016x} ({int(tf['r9'])})")
            if num == 986:
                print(f"  epoll_pwait(epfd={int(tf['rdi'])}, events=0x{int(tf['rsi']):x}, maxevents={int(tf['rdx'])}, timeout={int(tf['r10'])}, sigmask=0x{int(tf['r8']):x})")
            elif num == 987:
                print(f"  epoll_ctl(epfd={int(tf['rdi'])}, op={int(tf['rsi'])}, fd={int(tf['rdx'])}, event=0x{int(tf['r10']):x})")
        except gdb.error as exc:
            print(f"xv6-syscall failed: {exc}")

Xv6Threads()
Xv6Cpus()
Xv6Input()
Xv6Chan()
Xv6Timers()
Xv6Kqueue()
Xv6Syscall()
end

define xv6-procs
  call procdump()
end
document xv6-procs
Print the kernel process table using procdump().
end

define xv6-bt-blocked
  call procdump_bt()
end
document xv6-bt-blocked
Print saved kernel backtraces for sleeping/uninterruptible xv6 threads.
end

define xv6-bt-pid
    call procdump_bt_pid($arg0)
end
document xv6-bt-pid
Print a saved kernel backtrace for one xv6 pid, e.g. xv6-bt-pid 45.
end

define xv6-freeze
    info threads
    thread apply all bt
    xv6-cpus
    xv6-threads
    xv6-chan
    xv6-input
    xv6-timers
    xv6-syscall wlcomp
    xv6-kqueue wlcomp
    xv6-procs
    xv6-bt-blocked
end
document xv6-freeze
Run freeze triage: QEMU CPU threads, host-visible kernel stacks, xv6 CPU state, xv6 threads, wait channels, input counters, timers, wlcomp epoll/kqueue state, process table, and blocked thread backtraces.
end
