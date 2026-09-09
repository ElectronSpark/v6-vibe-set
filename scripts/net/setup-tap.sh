#!/usr/bin/env bash
# setup-tap.sh — create / bring up / tear down a host tap device for QEMU.
#
# Usage:
#   sudo bash scripts/setup-tap.sh up   [ifname=tap0] [host_cidr=192.168.78.1/24]
#   sudo bash scripts/setup-tap.sh down [ifname=tap0]
#   sudo bash scripts/setup-tap.sh dhcp [ifname=tap0] [range=192.168.78.10,192.168.78.50] [gateway=192.168.78.1]
#
# Notes:
#   * Requires CAP_NET_ADMIN on the host (run with sudo).
#   * The tap is owned by the invoking $SUDO_USER so QEMU can open it
#     without running as root.
#   * 'dhcp' starts dnsmasq in --no-daemon mode; ^C stops it.  This is what
#     the xv6 guest needs to acquire an address (it always uses DHCP).
set -euo pipefail

cmd="${1:-up}"
shift || true

IFNAME="${1:-tap0}"; shift || true

case "$cmd" in
        up)
                HOST_CIDR="${1:-192.168.78.1/24}"
                if ! ip link show "$IFNAME" >/dev/null 2>&1; then
                        ip tuntap add dev "$IFNAME" mode tap user "${SUDO_USER:-$USER}"
                fi
                ip addr flush dev "$IFNAME" || true
                ip addr add "$HOST_CIDR" dev "$IFNAME"
                ip link set "$IFNAME" up
                # Allow the guest to reach the rest of the host (and the
                # outside world if the host is forwarding).  These rules are
                # additive; we don't tear them down on 'down'.
                sysctl -wq net.ipv4.ip_forward=1 || true
                # Loopback to host services on 192.168.78.1 just works
                # because the tap interface is a real netif.
                echo "tap up: $IFNAME  host=$HOST_CIDR  owner=${SUDO_USER:-$USER}"
                ;;

        down)
                if ip link show "$IFNAME" >/dev/null 2>&1; then
                        ip link set "$IFNAME" down || true
                        ip tuntap del dev "$IFNAME" mode tap || true
                fi
                echo "tap down: $IFNAME"
                ;;

        dhcp)
                RANGE="${1:-192.168.78.10,192.168.78.50}"; shift || true
                GATEWAY="${1:-192.168.78.1}"; shift || true
                if ! command -v dnsmasq >/dev/null; then
                        echo "dnsmasq not installed; install it (apt install dnsmasq) or run a DHCP server some other way" >&2
                        exit 1
                fi
                exec dnsmasq --no-daemon --conf-file=/dev/null \
                        --interface="$IFNAME" --bind-interfaces \
                        --except-interface=lo \
                        --no-resolv --no-hosts \
                        --dhcp-range="${RANGE},255.255.255.0,1h" \
                        --dhcp-option=3,"$GATEWAY" \
                        --dhcp-option=6,"$GATEWAY" \
                        --log-dhcp \
                        --port=0
                ;;

        *)
                echo "usage: $0 {up|down|dhcp} [ifname] [...]" >&2
                exit 2
                ;;
esac
