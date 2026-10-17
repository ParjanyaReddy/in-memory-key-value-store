#!/usr/bin/env python3
"""
Integration test for redis_clone_server Phase 1 (Echo Server).
Tests multi-client socket concurrency, packet fragmentation, and clean shutdown.
"""

import os
import socket
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

def test_echo_server():
    server_bin = find_server_binary()
    port = 16379
    server_proc = subprocess.Popen(
        [server_bin, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(0.3)

    try:
        # 1. Connect multiple concurrent clients
        clients = []
        num_clients = 5
        for i in range(num_clients):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(("127.0.0.1", port))
            clients.append(s)

        # 2. Transmit distinct payloads
        for i, s in enumerate(clients):
            msg = f"hello_from_client_{i}\n".encode()
            s.sendall(msg)

        # 3. Verify echoed responses
        for i, s in enumerate(clients):
            expected = f"hello_from_client_{i}\n".encode()
            data = s.recv(1024)
            assert data == expected, f"Client {i} expected {expected}, got {data}"

        # 4. Byte-by-byte fragmentation test
        frag_client = clients[0]
        frag_msg = b"fragmented_packet_stream\n"
        for byte in frag_msg:
            frag_client.sendall(bytes([byte]))
            time.sleep(0.005)

        received = b""
        while len(received) < len(frag_msg):
            chunk = frag_client.recv(1024)
            received += chunk

        assert received == frag_msg, f"Expected {frag_msg}, got {received}"

        # 5. Clean disconnects
        for s in clients:
            s.close()
        time.sleep(0.1)

        print("Phase 1 integration tests passed successfully!")

    finally:
        server_proc.terminate()
        server_proc.wait(timeout=2.0)

if __name__ == "__main__":
    test_echo_server()
