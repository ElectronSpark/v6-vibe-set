---
name: xv6-kernel-lwip-networking
description: 'Use when: debugging xv6-os lwIP, TCP/IP, DHCP, DNS, sockets, sys_arch, lwip_glue, pbufs, socket syscalls, network daemons, or protocol behavior after NIC handoff.'
argument-hint: 'Describe the lwIP/socket/protocol symptom'
---

# xv6 Kernel lwIP Networking

## When to Use

- Packets reach the kernel but TCP, UDP, DHCP, DNS, or sockets misbehave.
- `socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`, or poll readiness is wrong.
- Network daemons or `/dev/netconf` state appear inconsistent after device handoff.

## Source Map

- Imported lwIP: `kernel/kernel/lwip/src/core`, `api`, `netif`.
- xv6 port: `kernel/kernel/lwip_port/sys_arch.c`, `lwip_glue.c`, `sys_socket.c`, `lwipopts.h`, `arch/`.
- Netdev bridge: `kernel/kernel/net.c`, `kernel/kernel/dev/netdev.c`.
- Daemons: `kernel/kernel/daemons/`.
- Device layer: `xv6-kernel-network-devices`.

## Workflow

1. Confirm the NIC/netdev layer hands packets to lwIP before debugging protocol code.
2. Check xv6 OS port primitives: time, mailbox/semaphore behavior, memory allocation, and thread context.
3. For socket bugs, follow file/VFS socket integration and poll readiness as well as lwIP state.
4. For DHCP/DNS, inspect timers and packet RX together.
5. For daemon issues, verify service startup, socket creation, and kernel thread scheduling.

## Pitfalls

- lwIP uses kernel memory and synchronization through the xv6 port; allocator or sleep bugs can look like protocol bugs.
- Socket readiness crosses lwIP, VFS, and kqueue/epoll.
- Do not debug e1000 RX in lwIP until packet handoff is proven.
