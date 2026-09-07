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

- Imported lwIP: `kernel/kernel/lwip/src/core/`, `kernel/kernel/lwip/src/api/`, `kernel/kernel/lwip/src/netif/`.
- xv6 port: `kernel/kernel/lwip_port/` (`sys_arch.c`, `lwip_glue.c`, `sys_socket.c`, `lwipopts.h`, `arch/`).
- Netdev bridge: `kernel/kernel/net.c`, `kernel/kernel/dev/netdev.c`.
- Daemons: `kernel/kernel/daemons/`.
- Device layer: `xv6-kernel-network-devices`.
- Performance status: [active network plan](../../../docs/active-work-plan.md#network-performance); old targets and blank result rows are not measured improvements.

## Workflow

1. Confirm the NIC/netdev layer hands packets to lwIP before debugging protocol code.
2. Check xv6 OS port primitives: time, mailbox/semaphore behavior, memory allocation, and thread context.
3. For socket bugs, follow file/VFS socket integration and poll readiness as well as lwIP state.
4. For DHCP/DNS, inspect timers and packet RX together.
5. For daemon issues, verify service startup, socket creation, and kernel thread scheduling.
6. For browser fetch failures, separate name resolution, connect, TLS, body transfer and application/IPC progress. Check the active application's resolver/TLS configuration and actual DHCP/resolv.conf state; `10.0.2.3` is the default QEMU SLIRP DNS address, not a TAP or arbitrary-host invariant. A still-readable TCP socket whose consumer sleeps needs socket/epoll evidence, not a browser workaround.

## Performance Changes

- Record NIC model and SLIRP/TAP backend with P1/P4/P8 throughput, CPU use and failures. Do not attribute a host-network bottleneck to lwIP from one run.
- For RX zero-copy, keep the mbuf alive until lwIP drops the final custom-pbuf reference, including receive queues; free it exactly once. Audit the current `net_rx` path and options before implementing an archived proposal.
- Change one measured lever at a time and retain DHCP/ping and concurrent TCP behavior. The earlier core-locking experiment did not improve the serial core ceiling; more vCPUs alone are not proof of stack scaling.

## Pitfalls

- lwIP uses kernel memory and synchronization through the xv6 port; allocator or sleep bugs can look like protocol bugs.
- Socket readiness crosses lwIP, VFS, and kqueue/epoll.
- Do not debug e1000 RX in lwIP until packet handoff is proven.
- A GUI browser error page can mean DNS/TLS/user-space config failed even when lwIP accepted `socket()` calls.
