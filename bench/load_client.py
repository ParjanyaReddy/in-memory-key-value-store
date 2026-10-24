import argparse
import socket
import struct
import sys
import threading
import time
from typing import List, Tuple

def encode_frame(payload_str: str) -> bytes:
    payload_bytes = payload_str.encode("utf-8")
    return struct.pack("<I", len(payload_bytes)) + payload_bytes

def recv_frame(sock: socket.socket) -> str:
    header = bytearray()
    while len(header) < 4:
        chunk = sock.recv(4 - len(header))
        if not chunk:
            raise ConnectionResetError("Server closed connection")
        header.extend(chunk)
    payload_len = struct.unpack("<I", header)[0]
    payload = bytearray()
    while len(payload) < payload_len:
        chunk = sock.recv(payload_len - len(payload))
        if not chunk:
            raise ConnectionResetError("Server closed connection")
        payload.extend(chunk)
    return payload.decode("utf-8")

class ClientWorker(threading.Thread):
    def __init__(self, host: str, port: int, commands_per_client: int, pipeline_depth: int, command_type: str, worker_id: int):
        super().__init__()
        self.host = host
        self.port = port
        self.commands_per_client = commands_per_client
        self.pipeline_depth = pipeline_depth
        self.command_type = command_type
        self.worker_id = worker_id
        self.latencies_us: List[float] = []
        self.errors = 0

    def run(self):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.connect((self.host, self.port))
        except Exception:
            self.errors += self.commands_per_client
            return

        remaining = self.commands_per_client
        op_counter = 0

        try:
            while remaining > 0:
                batch_size = min(self.pipeline_depth, remaining)
                pipeline_bytes = bytearray()

                for i in range(batch_size):
                    key = f"key_{self.worker_id}_{op_counter + i}"
                    if self.command_type == "SET":
                        frame = encode_frame(f"SET {key} val_{op_counter + i}")
                    elif self.command_type == "GET":
                        frame = encode_frame(f"GET {key}")
                    else:  # MIXED 50/50
                        if (op_counter + i) % 2 == 0:
                            frame = encode_frame(f"SET {key} val_{op_counter + i}")
                        else:
                            frame = encode_frame(f"GET {key}")
                    pipeline_bytes.extend(frame)

                t0 = time.perf_counter()
                sock.sendall(pipeline_bytes)

                for _ in range(batch_size):
                    recv_frame(sock)

                t1 = time.perf_counter()
                elapsed_us = ((t1 - t0) * 1e6) / batch_size
                for _ in range(batch_size):
                    self.latencies_us.append(elapsed_us)

                remaining -= batch_size
                op_counter += batch_size

        except Exception:
            self.errors += remaining
        finally:
            try:
                sock.close()
            except Exception:
                pass

def run_benchmark(host="127.0.0.1", port=6379, total_requests=100000, num_clients=50, pipeline_depth=1, command_type="SET"):
    print("=" * 65)
    print(f"  BENCHMARK: {command_type} (Pipeline: {pipeline_depth}, Clients: {num_clients}, Total: {total_requests})")
    print("=" * 65)

    reqs_per_client = total_requests // num_clients
    workers: List[ClientWorker] = []

    for i in range(num_clients):
        w = ClientWorker(host, port, reqs_per_client, pipeline_depth, command_type, i)
        workers.append(w)

    start_wall = time.perf_counter()
    for w in workers:
        w.start()

    for w in workers:
        w.join()
    end_wall = time.perf_counter()

    total_time_s = end_wall - start_wall
    all_latencies = []
    total_errors = 0

    for w in workers:
        all_latencies.extend(w.latencies_us)
        total_errors += w.errors

    if not all_latencies:
        print("Benchmark failed: no successful responses received.")
        return

    all_latencies.sort()
    n = len(all_latencies)
    ops_per_sec = n / total_time_s if total_time_s > 0 else 0

    p50 = all_latencies[int(n * 0.50)]
    p90 = all_latencies[int(n * 0.90)]
    p99 = all_latencies[int(n * 0.99)]
    p999 = all_latencies[int(n * 0.999)]
    max_lat = all_latencies[-1]
    avg_lat = sum(all_latencies) / n

    print(f"\nThroughput:   {ops_per_sec:,.0f} requests/sec")
    print(f"Elapsed Time: {total_time_s:.2f} seconds")
    print(f"Total Ops:    {n:,} (Errors: {total_errors})")
    print(f"p50 Latency:  {p50:.1f} \u03bcs")
    print(f"p90 Latency:  {p90:.1f} \u03bcs")
    print(f"p99 Latency:  {p99:.1f} \u03bcs")
    print(f"p99.9:        {p999:.1f} \u03bcs")
    print(f"Max Latency:  {max_lat:.1f} \u03bcs ({max_lat/1000.0:.2f} ms)\n")

    return {
        "command": command_type,
        "pipeline": pipeline_depth,
        "clients": num_clients,
        "requests": n,
        "throughput_ops_sec": ops_per_sec,
        "total_time_s": total_time_s,
        "p50_us": p50,
        "p90_us": p90,
        "p99_us": p99,
        "p999_us": p999,
        "max_us": max_lat,
        "errors": total_errors,
    }

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Multi-client throughput benchmark")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--requests", type=int, default=100000)
    parser.add_argument("--clients", type=int, default=50)
    parser.add_argument("--pipeline", type=int, default=1)
    parser.add_argument("--command", choices=["SET", "GET", "MIXED"], default="SET")
    args = parser.parse_args()

    run_benchmark(args.host, args.port, args.requests, args.clients, args.pipeline, args.command)
