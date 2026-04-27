---
name: xv6-kernel-network-devices
description: 'Use when: debugging xv6-os netdev, e1000, x1_emac, PHY drivers, RX/TX rings, NIC interrupts, packet handoff to lwIP, /dev/netconf, or network-driver freezes.'
argument-hint: 'Describe the NIC/netdev symptom or CPU stack'
---

# xv6 Kernel Network Devices

## When to Use

- Packet RX/TX, NIC interrupts, driver polling, or `/dev/netconf` state is wrong.
- CPU stacks show e1000 or netdev code in timer/IRQ paths.
- You need to distinguish device-driver failure from lwIP socket/protocol failure.

## Source Map

- Netdev core: `kernel/kernel/dev/netdev.c`, `kernel/kernel/inc/dev/net*.h`.
- QEMU NIC: `kernel/kernel/e1000.c`.
- Platform NICs: `kernel/kernel/dev/x1_emac.c`, `yt8531.c`.
- Bridge/state: `kernel/kernel/net.c`, `sysnet.c`, `kernel/kernel/inc/dev/netconf.h`.
- Focused e1000 debug: `xv6-kernel-network-e1000`.

## Workflow

1. Classify failure as device RX, device TX, interrupt/polling, netdev handoff, or lwIP protocol handling.
2. Keep hard IRQ and timer paths short; defer heavy RX work to workqueues when possible.
3. Check pending guards so repeated IRQ/timer polls do not enqueue unbounded work.
4. Validate packet ownership and buffer lifetime at the netdev/lwIP boundary.
5. Use `xv6-kernel-lwip-networking` once packets reach lwIP.

## Pitfalls

- xv6-tmp uses OrangePi EMAC/PHY paths that are not authoritative for x86_64 QEMU e1000.
- Heavy RX in timer/IRQ context can starve scheduler timers and GUI input.
- `/dev/netconf` display bugs are not always packet-path bugs.
