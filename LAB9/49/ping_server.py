#!/usr/bin/env python3
from __future__ import annotations

import argparse
import socket
import struct
import sys


ICMP_ECHO_REPLY = 0
ICMP_ECHO_REQUEST = 8


def checksum(data: bytes) -> int:
    if len(data) % 2:
        data += b"\x00"
    total = 0
    for index in range(0, len(data), 2):
        total += (data[index] << 8) + data[index + 1]
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def parse_packet(packet: bytes) -> tuple[str | None, str | None, bytes] | None:
    if len(packet) >= 20 and (packet[0] >> 4) == 4:
        ihl = (packet[0] & 0x0F) * 4
        if ihl < 20 or len(packet) < ihl + 8:
            return None
        source_ip = socket.inet_ntoa(packet[12:16])
        dest_ip = socket.inet_ntoa(packet[16:20])
        return source_ip, dest_ip, packet[ihl:]
    if len(packet) >= 8:
        return None, None, packet
    return None


def build_echo_reply(request: bytes) -> bytes:
    icmp_type, code, _, identifier, sequence = struct.unpack("!BBHHH", request[:8])
    payload = request[8:]
    if icmp_type != ICMP_ECHO_REQUEST or code != 0:
        raise ValueError("not an echo request")
    header = struct.pack("!BBHHH", ICMP_ECHO_REPLY, 0, 0, identifier, sequence)
    packet_checksum = checksum(header + payload)
    return struct.pack("!BBHHH", ICMP_ECHO_REPLY, 0, packet_checksum, identifier, sequence) + payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Simple ICMP echo reply server")
    parser.add_argument("--listen-ip", default="", help="only reply to requests destined for this IPv4 address")
    args = parser.parse_args()

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_ICMP)
    except PermissionError:
        print("raw socket requires administrator privileges, please rerun with sudo", file=sys.stderr)
        return 1

    print("ICMP echo server is listening")
    if args.listen_ip:
        print(f"reply filter enabled: only packets for {args.listen_ip}")

    while True:
        packet, peer = sock.recvfrom(65535)
        parsed = parse_packet(packet)
        if parsed is None:
            continue
        source_ip, dest_ip, icmp_packet = parsed
        if len(icmp_packet) < 8:
            continue
        icmp_type, code, _, identifier, sequence = struct.unpack("!BBHHH", icmp_packet[:8])
        if icmp_type != ICMP_ECHO_REQUEST or code != 0:
            continue
        if args.listen_ip and dest_ip and dest_ip != args.listen_ip:
            continue

        reply = build_echo_reply(icmp_packet)
        target_ip = source_ip or peer[0]
        sock.sendto(reply, (target_ip, 0))
        destination_text = dest_ip if dest_ip else args.listen_ip or "<local>"
        print(
            f"reply to {target_ip}: id=0x{identifier:04x} seq={sequence} "
            f"request_for={destination_text} payload={len(icmp_packet) - 8}B"
        )


if __name__ == "__main__":
    raise SystemExit(main())
