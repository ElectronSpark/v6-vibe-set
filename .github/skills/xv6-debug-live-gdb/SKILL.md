---
name: xv6-debug-live-gdb
description: 'Use when: an authorized xv6-os live QEMU/GDB inspection needs matching kernel symbols, same-boot process/thread selection, saved syscall and wait state, or a bounded freeze capture.'
argument-hint: 'Describe the live VM/GDB state or paste the capture'
---
# xv6 Live GDB Debugging

Use `scripts/debug/attach-gdb.sh` and `scripts/debug/xv6.gdb`. Inspect their
current behavior before attaching. A read-only review does not authorize a VM
launch, debugger pause, kernel call, instrumentation patch, or rebuild.

## Establish ownership and symbols

1. Apply root `AGENTS.md`: guarded `/home/es/.local/bin/rg`, no banned recursive
   options/generated-tree content searches, checked explicit artifact files via
   `scripts/audit/safe-rg-artifact.sh`, and the global search lock. Synchronously
   wait on exact process/tool handles before new searches or heavy commands.
2. Identify the owned VM by exact executable, PID/start time, run token and
   command line. Use `scripts/launch/qemu-exact-inventory.sh`; do not use `pgrep`
   patterns that match the invoking shell. Discovering a VM does not authorize
   pausing or terminating an unrelated process.
3. For an authorized new VM, the conductor must verify no VM worker is active
   and exact QEMU count is zero before dispatch. Keep one VM owner and all other
   lanes NO-BOOT. Use `scripts/launch/launch-gui.sh` with `QEMU_GDB=1` and
   `AUTO_BUILD=0` for the existing-image GUI route; `QEMU_GDB_WAIT=1` deliberately
   starts stopped. Preserve the owned launcher handle through the entire session.
4. Record the runtime's kernel image and symbol artifact hashes, receipt and
   start time. An older VM remains evidence for its own code, not a rebuilt
   kernel. Do not attach new symbols to an old image or relaunch outside scope.
5. Point the debugger explicitly at the matching symbols and GDB port:

   ```sh
   KERNEL=/absolute/path/to/matching/kernel.elf GDB_PORT=1234 bash scripts/debug/attach-gdb.sh
   ```

   `attach-gdb.sh` does not automatically select `launch-gui.sh`'s latest receipt.
   Set `KERNEL` or `BUILD_DIR` explicitly when using a reproduction receipt. Its
   comments may show retired top-level paths; use the `scripts/debug/` paths.
   If the VM was deliberately started stopped, send `c` once and wait until the
   intended workload is actually running before interpreting a later freeze.

## Capture state without inventing the subject

1. Pause only the authorized VM. At a fresh GDB prompt, save diagnostics to the
   current run's log. Issue one command per prompt and wait for completion; do
   not paste concatenated helpers such as `xv6-freeze...` plus another command.
2. Start with debugger memory inspection: `info threads`, `thread apply all bt`,
   `xv6-cpus`, `xv6-threads`, `xv6-chan`, `xv6-input`, and `xv6-timers` as relevant.
   QEMU's GDB thread list describes target vCPUs/harts; it is not the xv6 process
   table. Saved xv6 thread/trapframe state identifies the application context.
3. Select the exact same-boot thread/PID from `xv6-threads`, then call
   `xv6-syscall <pid>` and `xv6-kqueue <pid>` for the relevant subject. The helper
   accepts a numeric PID or exact thread name, and defaults to the historical
   `wlcomp` name. KDE/browser processes need current role-specific selection;
   do not reuse a PID from another run or assume a truncated name is unique.
4. Inspect saved syscall/trapframe registers for user arguments rather than
   registers from an arbitrary selected CPU. Tie fd state, pending readiness,
   wait registration, wakeup and consumption to the same process/object lifetime.
5. Be explicit about helpers that execute target code. `xv6-procs`,
   `xv6-bt-blocked`, and `xv6-bt-pid` call kernel functions. `xv6-freeze` includes
   two of those calls and hardcodes `wlcomp`; it is not a purely read-only
   all-purpose KDE capture. Prefer the memory-reading sequence above. Use target
   calls only as a deliberate part of the authorized diagnostic with understood
   stop-state constraints; do not call arbitrary kernel functions in a suspect
   lock/scheduler state.
6. Resume with `c` when the capture is complete and continuation remains within
   the owned run. A stopped kernel explains an unresponsive desktop; do not
   report the pause itself as a GUI freeze. If a resumed sample is needed, bind
   both samples to the same workload and time interval.

## Interpretation and completion

Repeated but changing samples show sampled execution progress, not visible
content, completed I/O or a healthy renderer. An event wait can be correct;
`waiters=0` is not a general health verdict. Do not replace event waits with
sleeps or change application flags to manufacture a pass. Route supported
findings to [fluid triage](../xv6-debug-fluid-triage/SKILL.md) and the current
owner without expanding an observation task into implementation.

Keep serial commands short, use marker variables, handle the known first-character
drop after bracketed paste, and wait for the actual command and fresh prompt;
silence is not completion. End through the owned runner, synchronously reap the
launcher, perform bounded token/PID-verified cleanup, and retain a final exact
zero-QEMU result. Never leave the debug VM running or signal unrelated QEMU/GDB.
