# Architectural Walkthrough & Interview Defense Guide

> **Project:** `redis_clone` (High-Performance In-Memory Key-Value Store in C++17)  
> **Source Repository:** [github.com/ParjanyaReddy/in-memory-key-value-store](https://github.com/ParjanyaReddy/in-memory-key-value-store)

---

## 1. The 5-File Reading Roadmap

To understand the core engineering decisions and data structure mechanics of this repository within 15 minutes, inspect these five files in sequence:

```
  1. src/server/event_loop.h & .cpp  ──►  Single-threaded epoll I/O multiplexer & idle reaper
  2. src/store/hash_table.h & .cpp   ──►  Dual-table incremental rehashing engine
  3. src/store/ttl_heap.h & .cpp     ──►  Binary min-heap with monotonic versioning
  4. src/protocol/frame_parser.h     ──►  Streaming framing state machine (<= 1MB ceiling)
  5. src/store/database.h & .cpp     ──►  Coordination layer unifying storage, rehash & TTL
```

### File 1: `src/server/event_loop.h` & `src/server/event_loop.cpp`
- **What to look for:**
  - The single-threaded non-blocking reactor pattern. Notice the lack of mutexes or condition variables.
  - `EpollPoller` wrapping Linux `epoll` with a portable `PollPoller` fallback (`IPoller`).
  - Dynamic `EPOLLOUT` interest registration: client sockets are registered strictly for `EPOLLIN` by default. `EPOLLOUT` is enabled *only* when `client.has_pending_write()` is true, and immediately cleared once drained, preventing 100% CPU spinning.
  - `reap_idle_clients()`: iterates over clients, checks monotonic timestamps, and safely detaches expired descriptors into an auxiliary vector before disconnecting to avoid iterator invalidation.

### File 2: `src/store/hash_table.h` & `src/store/hash_table.cpp`
- **What to look for:**
  - **Separate chaining** with singly-linked `HashNode` lists and power-of-two capacity masks (`hash & (capacity - 1)`).
  - **Dual-table incremental rehashing:** `table_old_` and `table_new_`.
  - `rehash_step(max_buckets=8, max_empty=80)`: migrates up to 8 non-empty buckets and bounds empty bucket scans to 80 per step to avoid event loop latency spikes on sparse tables.
  - Zero-allocation pointer rewiring: during migration, existing `HashNode*` pointers are rewired directly into `table_new_` without copying or allocating heap memory.
  - Concurrent lookup and mutation: `get()` checks `table_new_` then `table_old_`; `set()` moves old entries to `table_new_` or inserts directly into `table_new_`.

### File 3: `src/store/ttl_heap.h` & `src/store/ttl_heap.cpp`
- **What to look for:**
  - Contiguous contiguous vector-backed binary min-heap ordered by expiration deadline (`expire_at_ms`).
  - **Monotonic Expiry Versioning:** Each `HeapEntry` stores `(expire_at_ms, key, expiry_version)`.
  - When keys are overwritten or deleted before expiring, the old heap entry is *not* searched ($O(N)$) or tracked via complex bidirectional index maps. Instead, `node->expiry_version` increments. When a stale entry eventually pops from the min-heap, it is validated in $O(1)$ and discarded if the versions mismatch.

### File 4: `src/protocol/frame_parser.h` & `src/protocol/frame_parser.cpp`
- **What to look for:**
  - Length-prefixed binary wire protocol: `[4-byte little-endian uint32_t payload_len] [payload_bytes]`.
  - Strict 1 MB pre-allocation ceiling (`kMaxFramePayloadSize = 1024 * 1024`): rejecting malicious frames before memory allocation.
  - Stream fragmentation handling: safely retains partial bytes across non-blocking socket reads, parsing multiple pipelined frames in a single call.

### File 5: `src/store/database.h` & `src/store/database.cpp`
- **What to look for:**
  - Unification of storage, rehash progression, and TTL lifecycle.
  - Dual-layer expiration:
    1. **Lazy Expiry:** checked on every access (`get`, `ttl`, `contains`).
    2. **Active Expiry:** `active_expire_step(max_keys=100)` invoked every event loop tick.

---

## 2. The 10-Minute Interview Defense Script

### Q1: Blocking vs. Non-blocking I/O?
> *"A blocking `read()` or `recv()` suspends the calling thread at the kernel level until bytes arrive in the socket receive buffer. In a multi-client server, a blocking model requires either one thread per connection (which runs into thread stack memory limits and OS context switching overhead) or a thread pool.  
> In contrast, our server sets all sockets to `O_NONBLOCK`. Non-blocking system calls return immediately with `EAGAIN` or `EWOULDBLOCK` if no data is ready. This allows a single thread to multiplex thousands of concurrent connections using an event poller (`epoll`), servicing sockets only when the kernel signals that bytes are actually ready to be read or written."*

### Q2: Why is a single-threaded server fast? What is the tradeoff?
> *"A single-threaded architecture is fast for three reasons:  
> 1. **Zero Lock Contention:** No mutexes, spinlocks, or read-write locks means zero CPU cycles wasted waiting on threads.  
> 2. **Zero Context Switching:** No thread preemption overhead or TLB cache thrashing.  
> 3. **CPU Cache Locality:** Data structures remain hot in L1/L2 caches. In-memory hash lookups take nanoseconds, so the bottleneck is almost always network I/O and syscalls, not CPU.  
>  
> **The Tradeoff:** If any single command takes 10 milliseconds (e.g., a stop-the-world rehash or an unbounded $O(N)$ scan), the entire event loop freezes and every other client experiences a 10 ms latency spike. Furthermore, a single process cannot utilize multiple CPU cores. In production, Redis scales by running multiple single-threaded processes partitioned across cores or cluster shards."*

### Q3: `poll` vs. `epoll`? Level-triggered vs. Edge-triggered?
> *"With `poll()`, the application must pass an array of all $N$ file descriptors to the kernel on every call, and the kernel performs an $O(N)$ scan to determine readiness. If you have 10,000 connections but only 10 are active, `poll` wastes CPU scanning 9,990 idle descriptors on every iteration.  
> `epoll` solves this with an in-kernel interest list (a red-black tree) and a ready list (a linked list). When a network packet arrives, the network driver invokes an OS callback that places the descriptor onto the ready list. `epoll_wait()` returns only the active file descriptors in $O(K)$ time, where $K$ is the number of active events.  
>  
> **Level vs. Edge Triggered:** We use **Level-Triggered** mode. In level-triggered mode, `epoll` continues notifying you as long as data remains in the buffer. In edge-triggered mode, `epoll` notifies you only on state transitions (empty -> non-empty). Edge-triggered requires draining the socket in a `while(true)` loop until `EAGAIN`, which risks starving other connections if one client streams data indefinitely."*

### Q4: Why separate chaining over open addressing?
> *"We chose separate chaining because:  
> 1. **Graceful Degradation:** When load factor exceeds 1.0, separate chaining simply lengthens list buckets from 1 to 2 nodes. Open addressing (linear probing) suffers catastrophic clustering as load factor approaches 0.8, degrading lookup from $O(1)$ to $O(N)$.  
> 2. **Clean Deletions:** Deleting a node in separate chaining is a straightforward pointer swap and `delete node`. Open addressing requires complex tombstone markers or backward-shift deletions to preserve probe chains.  
> 3. **Zero-Allocation Rehashing:** During incremental rehashing, we rewire existing `HashNode*` pointers between tables in $O(1)$ time without copying strings or reallocating memory.  
>  
> **The Tradeoff:** Singly-linked list nodes have an extra 8-byte `next` pointer overhead and pointer indirection causes cache misses compared to flat contiguous arrays."*

### Q5: Why incremental rehashing?
> *"In a naive hash table, when the load factor threshold is crossed, the table doubles its capacity and rehashes all elements in a single synchronous loop. When our table grew to 1,000,000 keys, this **Stop-the-World** resize took **16.01 milliseconds**. In a single-threaded server, a 16 ms pause blocks all concurrent requests.  
> Our **Incremental Rehashing** engine maintains two tables (`table_old_` and `table_new_`). Resize work is amortized across subsequent commands and event loop ticks in bounded steps ($N_{\text{steps}} = 8$ non-empty buckets, capped at 80 empty bucket visits). This reduced peak resize latency from **16.01 ms down to 1.18 ms** (a **13.5x reduction**), keeping per-operation latency bounded."*

### Q6: What if a malicious client sends a 4 GB length header?
> *"Without protection, reading a 4 GB length prefix would cause the server to execute `std::vector::resize(4ULL * 1024 * 1024 * 1024)`, triggering an immediate `std::bad_alloc` crash or triggering the Linux OOM killer.  
> We enforce a strict **1 MB pre-allocation ceiling** (`kMaxFramePayloadSize = 1024 * 1024`). As soon as the 4-byte header is unpacked, if `payload_len > 1MB`, the server immediately rejects the frame, writes a `-ERR frame too large` response, and terminates the socket connection without allocating a single byte."*

### Q7: How do you guarantee memory safety without leaks?
> *"We apply a strict dual memory verification strategy:  
> 1. **Compile-Time Instrumentation:** Built with LLVM AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) via `-fsanitize=address,undefined`. Every memory access, out-of-bounds index, or invalid integer shift is caught immediately.  
> 2. **Runtime Emulation:** Validated under Valgrind Memcheck. Across 34 unit tests executing 45,949 heap allocations and frees, Valgrind reported **0 errors and 0 bytes leaked**.  
> 3. **Coverage-Guided Fuzzing:** The framing parser was subjected to **1,000,000 random inputs** using LLVM `libFuzzer` with zero crashes or memory violations."*

### Q8: What features are missing compared to production Redis?
> *"Our project intentionally focuses on core systems engineering (event loop, network multiplexing, custom hash tables, incremental rehashing, binary min-heap TTL). The primary production features missing are:  
> 1. **Persistence:** No RDB snapshots or AOF (Append-Only File) logging. To add AOF, we would intercept write commands in the dispatcher, append the command buffer to an `aof_buf`, and periodically flush via `fdatasync()`.  
> 2. **Replication & Consensus:** No leader-follower replication stream or Raft/Sentinel failover.  
> 3. **Richer Data Types:** Real Redis supports Sorted Sets (SkipLists), Hashes (ZipLists), and Streams (Radix Trees). We implement string key-values."*

---

## 3. Verified Resume Bullets (Grounding Protocol: Section G)

- Built a single-threaded C++17 in-memory key-value server on an `epoll` event loop handling **10,000 concurrent active TCP connections** with a peak memory footprint of **5.19 MB (~544 bytes per connection)**.
- Implemented an amortized dual-table incremental rehashing engine with bounded empty-bucket visits, reducing **peak resize latency from 16.01 ms to 1.18 ms (13.5x reduction) at 1,000,000 keys** versus a stop-the-world baseline.
- Engineered a length-prefixed binary wire protocol parser with a 1 MB pre-allocation ceiling; validated against **1,000,000 fuzzed inputs using LLVM libFuzzer** with zero crashes and clean Valgrind runs (0 leaks across 45,949 allocations).
- Designed a TTL eviction subsystem using a binary min-heap with monotonic per-key versioning for $O(1)$ stale-entry elimination, paired with lazy eviction on access and bounded active tick cleanup (max 100 keys/tick).
- Achieved **15,094 ops/sec pipelined throughput** over loopback TCP with an average memory consumption of **268.80 bytes per key**, matching `std::unordered_map` efficiency.
