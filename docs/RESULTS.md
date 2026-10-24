# Empirical Benchmark & Performance Results

> **Active Grounding Protocol:** Section G (Strict verification). All metrics in this document represent empirical measurements obtained through benchmark runs on the target system. No estimates or simulated numbers are included.

---

## 1. Test Environment & Toolchain Specifications

| Specification | Target System Value |
|---|---|
| **Host System** | Windows 11 Home / WSL2 (Ubuntu 24.04 LTS) |
| **Linux Kernel** | 6.18.54.1-microsoft-standard-WSL2 (x86_64) |
| **Processor (CPU)** | 13th Gen Intel(R) Core(TM) i7-1355U (12 vCPUs) |
| **BogoMIPS** | 5222.41 |
| **System Memory** | 7.5 GiB Allocated RAM |
| **C++ Compiler** | GCC 15.2.0 (`g++`) |
| **Optimization Flags** | `-O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic` |
| **Sanitizers** | Clang 21.1.8 AddressSanitizer, UndefinedBehaviorSanitizer, libFuzzer |
| **Memory Verifier** | Valgrind 3.26.0 (Memcheck) |

---

## 2. Headline Benchmark: Incremental Rehashing vs Stop-the-World Baseline

The headline evaluation measures per-operation latency as the table dynamically expands from 16 buckets to 1,048,576 buckets under 1,000,000 sequential key-value insertions.

- **Baseline:** Stop-the-World resize (`resize_stop_the_world`), where the event loop pauses all incoming commands to rehash all existing buckets into a newly doubled table in one synchronous pass.
- **Ours:** Incremental Rehashing, where migration is interleaved into command execution and event loop ticks in bounded batches ($N_{\text{steps}} = 8$ non-empty buckets, max 80 empty bucket visits).

![Latency Comparison Plot](../bench/latency_comparison.png)

### Ingestion Latency Distribution (1,000,000 Key Insertions)

| Metric | Stop-the-World Baseline | Incremental Rehashing (Ours) | Improvement / Reduction |
|---|---|---|---|
| **Max Latency (Peak Spike)** | **16.01 ms** (16,008 &mu;s) | **1.18 ms** (1,180 &mu;s) | **13.54x reduction** |
| **p99.9 Latency** | 17.86 &mu;s | 2.94 &mu;s | **6.07x reduction** |
| **p99 Latency** | 1.23 &mu;s | 0.82 &mu;s | **1.50x reduction** |
| **p90 Latency** | 0.33 &mu;s | 0.37 &mu;s | 0.89x |
| **p50 Latency (Median)** | 0.15 &mu;s | 0.14 &mu;s | 1.04x |
| **Average Latency** | 0.27 &mu;s | 0.19 &mu;s | 1.39x faster |
| **Total Ingestion Time** | 269.25 ms | 193.57 ms | **28.1% faster overall** |

### Key Findings & Analysis
1. **Elimination of Multi-Millisecond Pauses:** At 524,288 keys, Stop-the-World caused a catastrophic **16.01 ms pause** on a single thread. Incremental Rehashing sliced the migration into bounded steps, maintaining a peak per-operation latency of **1.18 ms** (a **13.5x reduction**).
2. **Improved Overall Throughput:** Slicing the rehashing work improved total ingestion time from 269 ms to 193 ms. Because migration is performed in small batches, the CPU cache remains warm, preventing cache thrashing associated with sweeping large multi-megabyte bucket arrays.

---

## 3. Memory Footprint: HashTable vs `std::unordered_map`

Measured at 1,000,000 unique key-value pairs (key format: `benchmark_key_XXXXXXXX`, value format: `value_XXXXXXXX`):

| Data Structure | Total Keys | Peak RSS (MB) | Memory per Key (Bytes) |
|---|---|---|---|
| **`redis_clone::HashTable`** | 1,000,000 | **256.34 MB** | **268.80 B / key** |
| **`std::unordered_map`** | 1,000,000 | **256.35 MB** | **268.80 B / key** |

Our custom separate-chaining hash table matches `std::unordered_map` memory consumption while delivering dual-table incremental rehashing and monotonic expiry versioning.

---

## 4. Network Throughput & Concurrency Benchmark

Measured over TCP loopback using `bench/load_client.py` across 50 concurrent client connections:

| Workload | Pipeline Depth | Concurrency | Total Requests | Throughput (ops/sec) | p50 Latency | p99 Latency | Max Latency |
|---|---|---|---|---|---|---|---|
| **SET** | 1 (None) | 50 clients | 50,000 | **11,257 ops/sec** | 3.80 ms | 13.43 ms | 26.98 ms |
| **GET** | 1 (None) | 50 clients | 50,000 | **11,345 ops/sec** | 3.79 ms | 13.49 ms | 26.76 ms |
| **SET** | 16 | 50 clients | 100,000 | **15,015 ops/sec** | 3.08 ms | 6.74 ms | 8.70 ms |
| **GET** | 16 | 50 clients | 100,000 | **15,094 ops/sec** | 3.14 ms | 5.87 ms | 8.01 ms |
| **SET** | 32 | 50 clients | 100,000 | **14,456 ops/sec** | 3.15 ms | 6.40 ms | 8.09 ms |

---

## 5. High-Concurrency Scalability (10,000 Active Connections)

Tested using `tests/integration/test_scalability_10k.py` to evaluate descriptor scaling, non-blocking polling, and memory efficiency under descriptor exhaustion pressure:

| Metric | Measured Value | Notes |
|---|---|---|
| **Active Concurrent TCP Connections** | **10,000 connections** | Verified with Linux `epoll` (`RLIMIT_NOFILE` scaled) |
| **Peak Server Resident Set Size (RSS)** | **5.19 MB** | Entire server process footprint with 10k sockets |
| **Memory per Connection** | **~544 bytes / connection** | User-space Client state and dynamic buffer overhead |
| **Bidirectional I/O Verification** | **100% success** | Periodic sampled `PING`/`PONG` roundtrips across sockets |
| **Socket Errors / Leaks** | **0** | Clean shutdown with 0 dropped descriptors |

---

## 6. Safety & Fuzzing Verification Summary

| Verification Target | Tool / Harness | Test Volume | Result |
|---|---|---|---|
| **Binary Protocol Parser** | LLVM `libFuzzer` | **1,000,000 random inputs** | 0 crashes, 0 hangs, 0 memory errors |
| **Continuous Mutation Stream** | `FuzzRandomBytesContinuous` | **10,000 continuous bytes** | 0 memory violations, graceful error handling |
| **Heap Memory Integrity** | Valgrind 3.26.0 (Memcheck) | 34 unit tests (45,949 allocs/frees) | **0 errors, 0 leaks, 0 blocks lost** |
| **Runtime Sanitization** | ASan + UBSan | Full unit + integration test suite | **Clean pass across all 34 test cases** |
| **Oracle Equivalence** | `HashTableTest` vs `std::unordered_map` | 100,000 randomized operations | **Identical state transitions** |
