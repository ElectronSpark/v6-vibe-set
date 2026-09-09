"""Bounded cleanup of an already-owned launcher; never finds or launches VMs."""
import threading
import time


def create_receipt(path):
    """A prior receipt or review marker must never become part of a new run."""
    path.mkdir(parents=True,exist_ok=False)
    return path


def stop_server_loop(server, thread, timeout=2):
    # BaseServer.shutdown waits forever if serve_forever never started.
    if thread is None or thread.ident is None or not thread.is_alive():
        return
    deadline = time.monotonic()+timeout
    errors = []
    def shutdown():
        try:
            server.shutdown()
        except BaseException as error:
            errors.append(error)
    # shutdown() itself waits for serve_forever, so it must not run on the
    # conductor thread. A blocked handler cannot delay final QEMU inventory.
    request = threading.Thread(target=shutdown,name='chromium-capture-http-shutdown',daemon=True)
    request.start()
    request.join(timeout=max(0,deadline-time.monotonic()))
    if errors:
        raise RuntimeError('HTTP shutdown failed: '+str(errors[0])) from errors[0]
    thread.join(timeout=max(0,deadline-time.monotonic()))
    if request.is_alive() or thread.is_alive():
        error = TimeoutError('HTTP shutdown deadline: shutdown_helper_alive='+str(request.is_alive())+
                             ', server_loop_alive='+str(thread.is_alive()))
        # Preserve the exact helper handle for an owner that can subsequently
        # release a blocked handler; the controller records this as a failure.
        error.shutdown_thread = request
        raise error


def stop_owned_launcher(launcher, request_quit, cleanup_qemu,
                        natural_timeout=12, cleanup_timeout=10,
                        terminate_timeout=10, kill_timeout=5):
    """Reap this Popen only; QEMU actions require caller's exact owned check.

    Errors are retained so the caller can always perform its final exact
    inventory and write a failure receipt. Escalation never uses process names.
    """
    result = {'pid': launcher.pid, 'attempts': [], 'errors': [], 'reaped': False,
              'returncode': None, 'clean_exit': False}

    def attempt(label, operation):
        try:
            value = operation()
            result['attempts'].append({'step':label,'status':'completed'})
            return True, value
        except BaseException as error:
            result['attempts'].append({'step':label,'status':'failed'})
            result['errors'].append({'step':label,'type':type(error).__name__,'error':str(error)})
            return False, None

    def wait(label, timeout):
        success, code = attempt(label,lambda:launcher.wait(timeout=timeout))
        if success:
            result.update(reaped=True,returncode=code,clean_exit=code==0)
        return success

    if launcher.poll() is None:
        attempt('qmp_quit',request_quit)
    if wait('natural_reap',natural_timeout):
        # A launcher can end before a separately owned QEMU; validate and clean
        # that exact QEMU too. An already-exited QEMU is a caller-side no-op.
        attempt('owned_qemu_cleanup',cleanup_qemu)
        return result
    attempt('owned_qemu_cleanup',cleanup_qemu)
    if wait('reap_after_qemu_cleanup',cleanup_timeout):
        return result
    attempt('launcher_terminate',launcher.terminate)
    if wait('reap_after_terminate',terminate_timeout):
        return result
    attempt('owned_qemu_cleanup_retry',cleanup_qemu)
    attempt('launcher_kill',launcher.kill)
    wait('reap_after_kill',kill_timeout)
    return result
