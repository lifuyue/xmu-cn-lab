#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import select
import socket
import struct
import sys
import time


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


def build_echo_request(identifier: int, sequence: int, size: int) -> bytes:
    timestamp = struct.pack("!d", time.time())
    payload = timestamp + bytes((index % 251 for index in range(max(0, size - len(timestamp)))))
    header = struct.pack("!BBHHH", ICMP_ECHO_REQUEST, 0, 0, identifier, sequence)
    packet_checksum = checksum(header + payload)
    return struct.pack("!BBHHH", ICMP_ECHO_REQUEST, 0, packet_checksum, identifier, sequence) + payload


def parse_ip_icmp(packet: bytes) -> tuple[str | None, int, bytes, int | None]:
    if len(packet) >= 20 and (packet[0] >> 4) == 4:
        ihl = (packet[0] & 0x0F) * 4
        if ihl >= 20 and len(packet) >= ihl + 8:
            source_ip = socket.inet_ntoa(packet[12:16])
            ttl = packet[8]
            return source_ip, ihl, packet[ihl:], ttl
    return None, 0, packet, None


def receive_reply(sock: socket.socket, identifier: int, timeout: float) -> tuple[str, int, float, int, int | None] | None:
    deadline = time.time() + timeout
    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            return None
        readable, _, _ = select.select([sock], [], [], remaining)
        if not readable:
            return None
        packet, peer = sock.recvfrom(65535)
        source_ip, offset, icmp_packet, ttl = parse_ip_icmp(packet)
        if len(icmp_packet) < 8:
            continue
        icmp_type, code, _, reply_id, sequence = struct.unpack("!BBHHH", icmp_packet[:8])
        if icmp_type != ICMP_ECHO_REPLY or code != 0 or reply_id != identifier:
            continue
        payload = icmp_packet[8:]
        send_time = None
        if len(payload) >= 8:
            send_time = struct.unpack("!d", payload[:8])[0]
        rtt_ms = (time.time() - send_time) * 1000.0 if send_time is not None else -1.0
        packet_size = len(packet) - offset if offset else len(icmp_packet)
        return source_ip or peer[0], sequence, rtt_ms, packet_size, ttl


def main() -> int:
    parser = argparse.ArgumentParser(description="Simple ICMP ping client")
    parser.add_argument("host", help="target host name or IPv4 address")
    parser.add_argument("-c", "--count", type=int, default=4, help="number of requests to send")
    parser.add_argument("-i", "--interval", type=float, default=1.0, help="interval between requests in seconds")
    parser.add_argument("-W", "--timeout", type=float, default=1.0, help="reply timeout in seconds")
    parser.add_argument("-s", "--size", type=int, default=56, help="ICMP payload size in bytes")
    parser.add_argument("-t", "--ttl", type=int, default=64, help="IPv4 TTL")
    args = parser.parse_args()

    try:
        target_ip = socket.gethostbyname(args.host)
    except OSError as exc:
        print(f"failed to resolve {args.host}: {exc}", file=sys.stderr)
        return 1

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_ICMP)
    except PermissionError:
        print("raw socket requires administrator privileges, please rerun with sudo", file=sys.stderr)
        return 1

    sock.setsockopt(socket.IPPROTO_IP, socket.IP_TTL, args.ttl)
    identifier = os.getpid() & 0xFFFF
    transmitted = 0
    received = 0
    rtts: list[float] = []

    print(f"PING {args.host} ({target_ip}) {args.size} data bytes")
    for sequence in range(1, args.count + 1):
        packet = build_echo_request(identifier, sequence, args.size)
        start = time.time()
        sock.sendto(packet, (target_ip, 0))
        transmitted += 1

        reply = receive_reply(sock, identifier, args.timeout)
        if reply is None:
            print(f"Request timeout for icmp_seq {sequence}")
        else:
            source_ip, reply_seq, rtt_ms, packet_size, ttl = reply
            received += 1
            ttl_text = f" ttl={ttl}" if ttl is not None else ""
            if rtt_ms >= 0:
                rtts.append(rtt_ms)
                print(
                    f"{packet_size} bytes from {source_ip}: icmp_seq={reply_seq}"
                    f"{ttl_text} time={rtt_ms:.3f} ms"
                )
            else:
                print(f"{packet_size} bytes from {source_ip}: icmp_seq={reply_seq}{ttl_text}")

        elapsed = time.time() - start
        if sequence != args.count and elapsed < args.interval:
            time.sleep(args.interval - elapsed)

    loss = ((transmitted - received) / transmitted * 100.0) if transmitted else 0.0
    print(f"\n--- {args.host} ping statistics ---")
    print(f"{transmitted} packets transmitted, {received} packets received, {loss:.1f}% packet loss")
    if rtts:
        print(
            "round-trip min/avg/max = "
            f"{min(rtts):.3f}/{sum(rtts) / len(rtts):.3f}/{max(rtts):.3f} ms"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
