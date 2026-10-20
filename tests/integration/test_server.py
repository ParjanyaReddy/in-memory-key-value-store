#!/usr/bin/env python3
"""
Integration test for redis_clone_server binary protocol.
Tests length-prefixed framing, PING/GET/SET/DEL commands, pipelining,
fragmented streams, and oversized frame rejection.
"""

import os
import socket
import struct
import subprocess
import sys
import time

def find_server_binary():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for candidate in ["./build/redis_clone_server", "./build-release/redis_clone_server"]:
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    raise FileNotFoundError("Could not find redis_clone_server in ./build or ./build-release")

def send_frame(sock, payload_str):
    payload_bytes = payload_str.encode("utf-8")
    header = struct.pack("<I", len(payload_bytes))
    sock.sendall(header + payload_bytes)

def recv_frame(sock):
    header = b""
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            return None
        header += chunk
    payload_len = struct.unpack("<I", header)[0]

    payload = b""
    while len(payload) < payload_len:
        chunk = sock.recv(payload_len - len(payload))
        if not chunk:
            break
        payload += chunk
    return payload.decode("utf-8")

def test_server():
    server_bin = find_server_binary()
    port = 17379
    server_proc = subprocess.Popen(
        [server_bin, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(0.3)

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", port))

        # 1. PING
        send_frame(s, "PING")
        assert recv_frame(s) == "+PONG\r\n"

        # 2. SET and GET
        send_frame(s, "SET user:101 Alice")
        assert recv_frame(s) == "+OK\r\n"

        send_frame(s, "GET user:101")
        assert recv_frame(s) == "$5\r\nAlice\r\n"

        # 3. DEL and nil GET
        send_frame(s, "DEL user:101")
        assert recv_frame(s) == ":1\r\n"

        send_frame(s, "GET user:101")
        assert recv_frame(s) == "$-1\r\n"

        # 4. Pipelining: 3 commands batched in a single TCP write
        batch = (
            struct.pack("<I", 4) + b"PING" +
            struct.pack("<I", 14) + b"SET counter 42" +
            struct.pack("<I", 11) + b"GET counter"
        )
        s.sendall(batch)

        assert recv_frame(s) == "+PONG\r\n"
        assert recv_frame(s) == "+OK\r\n"
        assert recv_frame(s) == "$2\r\n42\r\n"

        # 5. Packet fragmentation: single byte streaming
        frag_msg = struct.pack("<I", 11) + b"GET counter"
        for byte in frag_msg:
            s.sendall(bytes([byte]))
            time.sleep(0.002)

        assert recv_frame(s) == "$2\r\n42\r\n"
        s.close()

        # 6. Malicious oversized header rejection (> 1MB)
        s_bad = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s_bad.connect(("127.0.0.1", port))
        oversized_header = struct.pack("<I", 2 * 1024 * 1024)  # 2 MB
        s_bad.sendall(oversized_header)

        resp = recv_frame(s_bad)
        assert resp == "-ERR frame too large\r\n"
        # Connection must be closed by server
        data = s_bad.recv(1024)
        assert len(data) == 0  # Clean EOF
        s_bad.close()

        print("Phase 3 protocol integration tests passed successfully!")

    finally:
        server_proc.terminate()
        server_proc.wait(timeout=2.0)

if __name__ == "__main__":
    test_server()
