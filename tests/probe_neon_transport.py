#!/usr/bin/env python3
"""Observe ASE and stock RakNet 4 OPEN_CONNECTION_REQUEST_1 responses.

This bounded probe performs no login and sends no game/application packets.
It prints observations, not a verdict of MTA protocol compatibility. Run on
the Oracle VM as required by the repository's validation policy.
"""
import argparse
import datetime
import hashlib
import json
import socket


OFFLINE_MAGIC = bytes.fromhex("00ffff00fefefefefdfdfdfd12345678")
UDP_IPV4_HEADER_BYTES = 28


def exchange(ip, port, payload, timeout):
    result = {
        "port": port,
        "sent_bytes": len(payload),
        "sent_prefix_hex": payload[:18].hex(),
        "sent_sha256": hashlib.sha256(payload).hexdigest(),
    }
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.settimeout(timeout)
        try:
            udp.sendto(payload, (ip, port))
            response, source = udp.recvfrom(65535)
            result.update(
                outcome="response",
                source_ip=source[0],
                source_port=source[1],
                received_bytes=len(response),
                received_prefix_hex=response[:24].hex(),
            )
        except socket.timeout:
            result["outcome"] = "no_response_within_timeout"
        except OSError as error:
            result.update(outcome="socket_error", error=str(error))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="worldrpgbroficial.duckdns.org")
    parser.add_argument("--game-port", type=int, default=22003)
    parser.add_argument("--ase-port", type=int, default=22126)
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--mtus", type=int, nargs="+", default=[1492, 1200, 576])
    args = parser.parse_args()
    if not 0 < args.timeout <= 10:
        parser.error("timeout must be greater than 0 and at most 10 seconds")
    if not all(1 <= port <= 65535 for port in (args.game_port, args.ase_port)):
        parser.error("ports must be between 1 and 65535")
    if not all(576 <= mtu <= 1492 for mtu in args.mtus) or len(args.mtus) > 3:
        parser.error("use at most three MTUs between 576 and 1492")
    try:
        ip = socket.gethostbyname(args.host)
    except OSError as error:
        parser.exit(1, "Cannot resolve target: " + str(error) + "\n")
    report = {
        "recorded_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": args.host,
        "resolved_ipv4": ip,
        "timeout_seconds": args.timeout,
        "scope": "ASE and stock RakNet4 OPEN1 only; no MTA login or authentication",
        "probes": [],
    }
    ase = exchange(ip, args.ase_port, b"s", args.timeout)
    ase["kind"] = "ase_query"
    ase["eye1_header"] = ase.get("received_prefix_hex", "").startswith("45594531")
    report["probes"].append(ase)
    for protocol in (5, 6):
        for mtu in args.mtus:
            # Vendored RakPeer.cpp:5758-5761, MTUSize.h and
            # CCRakNetSlidingWindow.h. OPEN1 is ID5, magic16, protocol1,
            # zero-padded to the requested IPv4 MTU minus UDP/IP headers.
            header = b"\x05" + OFFLINE_MAGIC + bytes([protocol])
            payload = header.ljust(mtu - UDP_IPV4_HEADER_BYTES, b"\0")
            result = exchange(ip, args.game_port, payload, args.timeout)
            result.update(kind="raknet4_open1", protocol=protocol, ipv4_mtu=mtu)
            report["probes"].append(result)
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
