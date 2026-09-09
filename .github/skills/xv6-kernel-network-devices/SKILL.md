---
name: xv6-kernel-network-devices
description: 'Debug xv6-os NIC and netdev paths: virtio-net, e1000, x1_emac/PHY, RX/TX rings, interrupts and packet ownership before lwIP handoff.'
argument-hint: 'Describe the NIC/netdev symptom or CPU stack'
---

# xv6 Kernel Network Devices

## When to Use

- Packet RX/TX, NIC interrupts, driver polling, or `/dev/netconf` state is wrong.
- CPU stacks show e1000 or netdev code in timer/IRQ paths.
- You need to distinguish device-driver failure from lwIP socket/protocol failure.

## Source Map

- Netdev core: `kernel/kernel/dev/netdev.c`, `kernel/kernel/inc/dev/net*.h`.
- QEMU NICs: `kernel/kernel/virtio_net.c`, `kernel/kernel/e1000.c`; identify the selected `QEMU_NET_MODEL` in `scripts/launch/run-qemu.sh` and the actual VM arguments.
- Platform NICs: `kernel/kernel/dev/x1_emac.c`, `kernel/kernel/dev/yt8531.c`.
- Bridge/state: `kernel/kernel/net.c`, `kernel/kernel/sysnet.c`, `kernel/kernel/inc/dev/netconf.h`.
- Focused e1000 debug: `xv6-kernel-network-e1000`.

## Workflow

1. Classify failure as device RX, device TX, interrupt/polling, netdev handoff, or lwIP protocol handling.
2. Keep hard IRQ and timer paths short; defer heavy RX work to workqueues when possible.
3. Check pending guards so repeated IRQ/timer polls do not enqueue unbounded work.
4. Validate packet ownership and buffer lifetime at the netdev/lwIP boundary.
5. Use `xv6-kernel-lwip-networking` once packets reach lwIP.

## Pitfalls

- EMAC/PHY notes and e1000 workqueue findings do not establish how an active virtio-net device behaves; follow the selected driver's queues and completion ownership.
- Heavy RX in timer/IRQ context can starve scheduler timers and GUI input.
- `/dev/netconf` display bugs are not always packet-path bugs.
