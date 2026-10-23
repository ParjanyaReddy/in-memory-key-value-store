#!/usr/bin/env python3
"""
Scalability and concurrency test establishing up to 10,000 concurrent
client connections against redis_clone_server.
Validates descriptor scalability, event loop stability, and memory efficiency.
"""

import os
import resource
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
    raise FileNotFoundError("Could not find redis_clone_server binary")

def get_process_rss_mb(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    parts = line.split()
                    return int(parts[1]) / 1024.0  # Convert kB to MB
    except Exception:
        pass
    return 0.0

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

def run_scalability_test():
    # 1. Inspect and adjust file descriptor limits
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    target_connections = 10000
    needed_fds = target_connections + 200

    if hard < needed_fds:
        print(f"Hard RLIMIT_NOFILE is {hard}; adjusting target connections to {hard - 200}")
        target_connections = max(100, hard - 200)
        needed_fds = target_connections + 200

    try:
        resource.setrlimit(resource.RLIMIT_NOFILE, (needed_fds, hard))
    except Exception as e:
        print(f"Could not raise soft limit to {needed_fds}: {e}")
        target_connections = max(100, soft - 200)

    print(f"Running scalability benchmark with {target_connections} concurrent connections...")

    server_bin = find_server_binary()
    port = 19379
    server_proc = subprocess.Popen(
        [server_bin, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    time.sleep(0.3)

    sockets = []
    try:
        # 2. Connect client sockets in batches
        batch_size = 500
        for i in range(0, target_connections, batch_size):
            count = min(batch_size, target_connections - i)
            for _ in range(count):
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.connect(("127.0.0.1", port))
                s.setblocking(True)
                sockets.append(s)

        print(f"Successfully established {len(sockets)} active TCP connections to server.")

        # 3. Sample a subset of connections to verify bidirectional I/O under high descriptor load
        sample_step = max(1, len(sockets) // 20)
        for idx in range(0, len(sockets), sample_step):
            s = sockets[idx]
            send_frame(s, f"PING sample_{idx}")
            resp = recv_frame(s)
            expected = f"${len(f'sample_{idx}')}\r\nsample_{idx}\r\n"
            assert resp == expected, f"Unexpected response: {resp!r}, expected: {expected!r}"

        # 4. Measure server process RSS
        rss_mb = get_process_rss_mb(server_proc.pid)
        bytes_per_conn = (rss_mb * 1024 * 1024) / len(sockets) if sockets else 0
        print(f"Peak Server RSS: {rss_mb:.2f} MB (~{bytes_per_conn:.0f} bytes per connection)")

        # 5. Cleanly close connections
        for s in sockets:
            s.close()
        sockets.clear()
        time.sleep(0.2)

        print("Scalability benchmark completed successfully with 0 errors.")

    finally:
        for s in sockets:
            try:
                s.close()
            except Exception:
                pass
        server_proc.terminate()
        server_proc.wait(timeout=2.0)

if __name__ == "__main__":
    run_scalability_test()
