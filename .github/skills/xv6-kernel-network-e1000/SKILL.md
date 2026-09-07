---
name: xv6-kernel-network-e1000
description: 'Debug xv6-os e1000 RX/TX, interrupt or timer hotspots and RX workqueue races when the active NIC or captured stack is e1000. Use the broader networking skills for virtio-net or protocol failures.'
argument-hint: 'Describe the NIC/network symptom or CPU stack'
---

# xv6 Kernel e1000

## When to Use

- CPU0 appears stuck in e1000 RX from timer or IRQ context.
- The active e1000 device drops traffic or its descriptor/completion path stalls.
- A freeze capture shows network RX work on the timer path.
- Check the actual NIC first; a DHCP or browser symptom alone does not select this driver.

## Key Findings

- Directly doing e1000 RX work in timer/IRQ context can dominate CPU0 and contribute to freezes.
- The safer path is to schedule RX onto a kernel workqueue and keep timer/interrupt handlers short.
- `e1000_poll_rx()` may still be called from CPU0 timer tick, but it should only schedule work when RX is pending.
- A pending flag is needed so repeated interrupts/timer polls do not enqueue unbounded duplicate RX work.
- `/dev/netconf` is a useful xv6 driver diagnostic. Upstream applications may use Linux discovery and resolver interfaces; do not replace their contracts with the old custom desktop application's policy.

## Procedure

1. With matching kernel symbols and the [freeze workflow](../xv6-kernel-freeze-triage/SKILL.md), inspect CPU stacks:
   - If CPU0 is in `e1000_recv()` from timer/IRQ, inspect the RX deferral path.
   - Idle CPUs do not exclude a missed RX wakeup; correlate pending descriptors, worker state and waiting consumers.
2. Check the e1000 RX structure:
   - `e1000_init()` initializes the RX workqueue and work item.
   - `e1000_rx_pending()` cheaply checks hardware state.
   - `e1000_schedule_rx()` uses an atomic pending flag before queueing work.
   - `e1000_rx_work_func()` drains RX with `e1000_recv()`, then clears the pending flag and rechecks for arrivals. Preserve the recheck/requeue and failed-queue reset paths so a packet arriving at the handoff cannot strand work.
3. Keep interrupt/timer handlers short:
   - `e1000_intr()` should schedule work, acknowledge interrupt state, and return.
   - `e1000_poll_rx()` should schedule work rather than drain packets directly.
4. Validate user-visible network state separately:
   - Check `/dev/netconf` for interface, address, gateway, DNS, or link state exported by the kernel/lwIP integration.
   - Do not infer kernel network failure solely from desktop app formatting.

## Relevant Files

- `kernel/kernel/e1000.c`
- `kernel/kernel/lwip_port/`
- `kernel/kernel/dev/netdev.c`
- `scripts/launch/run-qemu.sh`

## Pitfalls

- Do not do heavy RX draining from timer interrupt context.
- Do not enqueue repeated RX work without an atomic pending guard.
- Once driver handoff is proven, route socket/readiness failures to [lwIP networking](../xv6-kernel-lwip-networking/SKILL.md).
