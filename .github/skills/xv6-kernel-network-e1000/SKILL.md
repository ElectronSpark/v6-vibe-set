---
name: xv6-kernel-network-e1000
description: 'Use when: debugging xv6-os e1000 NIC, lwIP, DHCP, timer interrupt hotspots, RX path freezes, network app display, /dev/netconf, or CPU0 stuck in e1000_poll_rx/e1000_recv.'
argument-hint: 'Describe the NIC/network symptom or CPU stack'
---

# xv6 Kernel Network And e1000

## When to Use

- CPU0 appears stuck in e1000 RX from timer or IRQ context.
- DHCP, lwIP, host forwarding, or the desktop Network app behaves oddly.
- A freeze capture shows network RX work on the timer path.
- You need to distinguish NIC/kernel issues from GUI display issues.

## Key Findings

- Directly doing e1000 RX work in timer/IRQ context can dominate CPU0 and contribute to freezes.
- The safer path is to schedule RX onto a kernel workqueue and keep timer/interrupt handlers short.
- `e1000_poll_rx()` may still be called from CPU0 timer tick, but it should only schedule work when RX is pending.
- A pending flag is needed so repeated interrupts/timer polls do not enqueue unbounded duplicate RX work.
- The desktop Network app should read xv6-specific `/dev/netconf`, not Linux `/proc/net/dev` or `/etc/resolv.conf` assumptions.

## Procedure

1. From `xv6-freeze`, inspect CPU stacks:
   - If CPU0 is in `e1000_recv()` from timer/IRQ, inspect the RX deferral path.
   - If all CPUs are idle, network is probably not the active freeze root.
2. Check the e1000 RX structure:
   - `e1000_init()` initializes the RX workqueue and work item.
   - `e1000_rx_pending()` cheaply checks hardware state.
   - `e1000_schedule_rx()` uses an atomic pending flag before queueing work.
   - `e1000_rx_work_func()` clears the pending flag and drains RX with `e1000_recv()`.
3. Keep interrupt/timer handlers short:
   - `e1000_intr()` should schedule work, acknowledge interrupt state, and return.
   - `e1000_poll_rx()` should schedule work rather than drain packets directly.
4. Validate user-visible network state separately:
   - Check `/dev/netconf` for interface, address, gateway, DNS, or link state exported by the kernel/lwIP integration.
   - Do not infer kernel network failure solely from desktop app formatting.

## Relevant Files

- `kernel/kernel/e1000.c`
- `kernel/kernel/lwip_port/`
- `ports/wayland/src/wlcomp.c`
- `scripts/run-qemu.sh`

## Pitfalls

- Do not do heavy RX draining from timer interrupt context.
- Do not enqueue repeated RX work without an atomic pending guard.
- Do not use Linux `/proc/net/dev` expectations for xv6 desktop network display.
