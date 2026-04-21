#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ipaddress
import socket
import struct
import sys
from dataclasses import dataclass


BOOTREQUEST = 1
BOOTREPLY = 2
DHCP_DISCOVER = 1
DHCP_OFFER = 2
DHCP_REQUEST = 3
DHCP_DECLINE = 4
DHCP_ACK = 5
DHCP_NAK = 6
DHCP_RELEASE = 7
DHCP_INFORM = 8
MAGIC_COOKIE = b"\x63\x82\x53\x63"


@dataclass
class DhcpMessage:
    op: int
    xid: int
    flags: int
    ciaddr: str
    yiaddr: str
    siaddr: str
    giaddr: str
    chaddr: bytes
    options: dict[int, list[bytes]]


def mac_to_text(mac: bytes) -> str:
    return ":".join(f"{octet:02x}" for octet in mac[:6])


def ip_to_bytes(ip_text: str) -> bytes:
    return socket.inet_aton(str(ipaddress.IPv4Address(ip_text)))


def parse_options(raw: bytes) -> dict[int, list[bytes]]:
    options: dict[int, list[bytes]] = {}
    index = 0
    while index < len(raw):
        option = raw[index]
        index += 1
        if option == 0:
            continue
        if option == 255:
            break
        if index >= len(raw):
            break
        length = raw[index]
        index += 1
        value = raw[index:index + length]
        index += length
        options.setdefault(option, []).append(value)
    return options


def parse_message(packet: bytes) -> DhcpMessage | None:
    if len(packet) < 240:
        return None
    fixed = packet[:236]
    cookie = packet[236:240]
    if cookie != MAGIC_COOKIE:
        return None
    (
        op,
        htype,
        hlen,
        hops,
        xid,
        secs,
        flags,
        ciaddr,
        yiaddr,
        siaddr,
        giaddr,
        chaddr,
        _,
        _,
    ) = struct.unpack("!BBBBIHH4s4s4s4s16s64s128s", fixed)
    if htype != 1 or hlen != 6:
        return None
    return DhcpMessage(
        op=op,
        xid=xid,
        flags=flags,
        ciaddr=socket.inet_ntoa(ciaddr),
        yiaddr=socket.inet_ntoa(yiaddr),
        siaddr=socket.inet_ntoa(siaddr),
        giaddr=socket.inet_ntoa(giaddr),
        chaddr=chaddr[:hlen],
        options=parse_options(packet[240:]),
    )


def first_option(message: DhcpMessage, code: int) -> bytes | None:
    values = message.options.get(code)
    return values[0] if values else None


def message_type_name(message_type: int) -> str:
    names = {
        DHCP_DISCOVER: "DISCOVER",
        DHCP_OFFER: "OFFER",
        DHCP_REQUEST: "REQUEST",
        DHCP_DECLINE: "DECLINE",
        DHCP_ACK: "ACK",
        DHCP_NAK: "NAK",
        DHCP_RELEASE: "RELEASE",
        DHCP_INFORM: "INFORM",
    }
    return names.get(message_type, f"TYPE-{message_type}")


def encode_options(options: list[tuple[int, bytes]]) -> bytes:
    body = bytearray(MAGIC_COOKIE)
    for code, value in options:
        body.append(code)
        body.append(len(value))
        body.extend(value)
    body.append(255)
    return bytes(body)


def build_reply(
    request: DhcpMessage,
    message_type: int,
    lease_ip: str,
    server_ip: str,
    subnet_mask: str,
    router: str,
    dns: str,
    lease_time: int,
) -> bytes:
    fixed = struct.pack(
        "!BBBBIHH4s4s4s4s16s64s128s",
        BOOTREPLY,
        1,
        6,
        0,
        request.xid,
        0,
        request.flags,
        b"\x00\x00\x00\x00",
        ip_to_bytes(lease_ip),
        ip_to_bytes(server_ip),
        b"\x00\x00\x00\x00",
        request.chaddr.ljust(16, b"\x00"),
        b"\x00" * 64,
        b"\x00" * 128,
    )
    options = [
        (53, bytes([message_type])),
        (54, ip_to_bytes(server_ip)),
        (51, struct.pack("!I", lease_time)),
        (1, ip_to_bytes(subnet_mask)),
        (3, ip_to_bytes(router)),
        (6, ip_to_bytes(dns)),
    ]
    parameter_request_list = first_option(request, 55)
    if parameter_request_list and 28 in parameter_request_list:
        network = ipaddress.IPv4Network(f"{lease_ip}/{subnet_mask}", strict=False)
        options.append((28, ip_to_bytes(str(network.broadcast_address))))
    return fixed + encode_options(options)


def planned_ip(mac_text: str, static_leases: dict[str, str], default_ip: str) -> str:
    return static_leases.get(mac_text, default_ip)


def parse_static_lease(item: str) -> tuple[str, str]:
    if "=" not in item:
        raise argparse.ArgumentTypeError("static lease must be in mac=ip form")
    mac_text, ip_text = item.split("=", 1)
    mac_text = mac_text.lower()
    parts = mac_text.split(":")
    if len(parts) != 6 or any(len(part) != 2 for part in parts):
        raise argparse.ArgumentTypeError("invalid MAC address")
    ipaddress.IPv4Address(ip_text)
    return mac_text, ip_text


def main() -> int:
    parser = argparse.ArgumentParser(description="Simple fixed-lease DHCP server")
    parser.add_argument("--bind", default="0.0.0.0", help="local bind address, default 0.0.0.0")
    parser.add_argument("--server-ip", required=True, help="server identifier IP address")
    parser.add_argument("--subnet-mask", default="255.255.255.0", help="subnet mask to announce")
    parser.add_argument("--router", required=True, help="default gateway to announce")
    parser.add_argument("--dns", required=True, help="DNS server to announce")
    parser.add_argument("--default-ip", default="192.168.1.2", help="fallback fixed IPv4 lease")
    parser.add_argument("--lease-seconds", type=int, default=3600, help="lease duration in seconds")
    parser.add_argument(
        "--static-lease",
        action="append",
        default=[],
        metavar="MAC=IP",
        help="static binding such as 00:11:22:33:44:55=192.168.1.20",
    )
    args = parser.parse_args()

    static_leases = dict(parse_static_lease(item) for item in args.static_lease)
    ipaddress.IPv4Address(args.server_ip)
    ipaddress.IPv4Network(f"0.0.0.0/{args.subnet_mask}")
    ipaddress.IPv4Address(args.router)
    ipaddress.IPv4Address(args.dns)
    ipaddress.IPv4Address(args.default_ip)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    try:
        sock.bind((args.bind, 67))
    except PermissionError:
        print("binding UDP 67 requires administrator privileges, please rerun with sudo", file=sys.stderr)
        return 1

    print(
        f"DHCP server listening on {args.bind}:67, server_id={args.server_ip}, "
        f"default_lease={args.default_ip}/{args.subnet_mask}"
    )
    if static_leases:
        print("static leases:")
        for mac_text, ip_text in sorted(static_leases.items()):
            print(f"  {mac_text} -> {ip_text}")

    while True:
        packet, peer = sock.recvfrom(4096)
        message = parse_message(packet)
        if message is None or message.op != BOOTREQUEST:
            continue
        raw_type = first_option(message, 53)
        if not raw_type:
            continue
        message_type = raw_type[0]
        mac_text = mac_to_text(message.chaddr)
        lease_ip = planned_ip(mac_text, static_leases, args.default_ip)
        requested_ip = first_option(message, 50)
        server_id = first_option(message, 54)

        print(f"recv {message_type_name(message_type)} from {mac_text} xid=0x{message.xid:08x} via {peer[0]}")

        if message_type == DHCP_DISCOVER:
            reply_type = DHCP_OFFER
        elif message_type == DHCP_REQUEST:
            requested_ip_text = socket.inet_ntoa(requested_ip) if requested_ip else None
            if requested_ip_text is None and message.ciaddr != "0.0.0.0":
                requested_ip_text = message.ciaddr
            if requested_ip_text and requested_ip_text != lease_ip:
                reply_type = DHCP_NAK
            elif server_id and socket.inet_ntoa(server_id) != args.server_ip:
                continue
            else:
                reply_type = DHCP_ACK
        elif message_type in {DHCP_RELEASE, DHCP_DECLINE, DHCP_INFORM}:
            continue
        else:
            continue

        if reply_type == DHCP_NAK:
            response = build_reply(
                request=message,
                message_type=reply_type,
                lease_ip="0.0.0.0",
                server_ip=args.server_ip,
                subnet_mask=args.subnet_mask,
                router=args.router,
                dns=args.dns,
                lease_time=args.lease_seconds,
            )
        else:
            response = build_reply(
                request=message,
                message_type=reply_type,
                lease_ip=lease_ip,
                server_ip=args.server_ip,
                subnet_mask=args.subnet_mask,
                router=args.router,
                dns=args.dns,
                lease_time=args.lease_seconds,
            )
        sock.sendto(response, ("255.255.255.255", 68))
        print(
            f"sent {message_type_name(reply_type)} to {mac_text} "
            f"yiaddr={lease_ip if reply_type != DHCP_NAK else '0.0.0.0'}"
        )


if __name__ == "__main__":
    raise SystemExit(main())
