#!/usr/bin/env python3
"""TCP and UDP echo server for the wifi_s3 test app.

The firmware connects to 10.0.2.2:5555, which QEMU's user networking (slirp)
maps to the host loopback. Every message received is sent back and printed
as "HOST_ECHO tcp <text>" or "HOST_ECHO udp <text>", so a runner can check
what reached the host. Stops after --timeout seconds.

Usage: host_echo.py [--port 5555] [--timeout 120]
"""

import argparse
import select
import socket
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--port', type=int, default=5555)
    parser.add_argument('--timeout', type=float, default=120.0)
    args = parser.parse_args()

    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp.bind(('127.0.0.1', args.port))
    tcp.listen(4)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(('127.0.0.1', args.port))
    print(f'HOST_ECHO listening on 127.0.0.1:{args.port}', flush=True)

    end = time.monotonic() + args.timeout
    while time.monotonic() < end:
        ready, _, _ = select.select([tcp, udp], [], [], 0.5)
        for sock in ready:
            if sock is udp:
                data, peer = udp.recvfrom(2048)
                udp.sendto(data, peer)
                print(f'HOST_ECHO udp {data.decode(errors="replace")}', flush=True)
            else:
                conn, _ = tcp.accept()
                with conn:
                    conn.settimeout(5)
                    data = conn.recv(2048)
                    if data:
                        conn.sendall(data)
                        print(f'HOST_ECHO tcp {data.decode(errors="replace")}', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
