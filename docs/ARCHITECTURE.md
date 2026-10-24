# System Architecture Specification

## 1. Overview & High-Level Philosophy

`redis_clone` is an in-memory key-value server written in C++17 designed to demonstrate fundamental systems programming principles:
- **Single-Threaded Command Execution:** A single-threaded event loop processes all storage mutations, completely eliminating lock contention, mutex overhead, and race conditions in data structures.
- **Incremental Resource Allocation:** Resizing a large hash table in a single step halts the event loop, causing severe latency spikes (tail latency degradation). Incremental rehashing distributes table migration across subsequent commands.
- **Deterministic Memory Reclamation:** Every dynamic allocation has explicit ownership; data structures are verified leak-free via AddressSanitizer and Valgrind.
- **Binary Stream Framing:** Full separation of network transport from message semantics via length-prefixed protocol framing, handling arbitrary TCP fragmentation and pipelining.

---

## 2. Layered Architecture

```
+-----------------------------------------------------------------------+
|                            Client Layer                               |
|              (TCP Connections over Non-Blocking Sockets)              |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                          Server & I/O Layer                           |
|  - EventLoop: Multiplexes sockets via epoll (Linux) or poll fallback  |
|  - Client: Manages socket lifecycle, read/write buffers, timeouts     |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                         Protocol & Parser                             |
|  - FrameParser: 4-byte LE length prefix, <= 1MB validation            |
|  - Command: Serializes and deserializes GET, SET, DEL, EXPIRE         |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                           Storage Engine                              |
|  - Database: Orchestrates store, rehashing, and TTL subsystems        |
|  - HashTable: Separate-chaining table with incremental rehashing      |
|  - TTLHeap: Priority queue min-heap for active expiration             |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
|                           Utilities & Core                            |
|  - Hash: 64-bit FNV-1a hash function                                  |
|  - Time: Monotonic millisecond and microsecond clocks                 |
|  - Logging: Structured stdout logger                                  |
+-----------------------------------------------------------------------+
```

---

## 3. Component Deep Dive

### 3.1 Network Multiplexing
- Non-blocking sockets (`O_NONBLOCK`) prevent I/O operations from blocking the single thread.
- The event loop listens for read events (`EPOLLIN`). When a socket becomes readable, data is drained into a client-specific buffer until the system call returns `EAGAIN` or `EWOULDBLOCK`.
- Write interest (`EPOLLOUT`) is enabled only when a client's outgoing buffer contains unsent response bytes. Once fully drained, write interest is removed to avoid busy-looping.

### 3.2 Protocol & Framing
- TCP is a byte stream. Messages have no inherent boundaries.
- Our protocol prefixes each frame with a **4-byte little-endian unsigned integer** defining the payload length.
- An incoming frame is rejected immediately if length exceeds **1 MB (1,048,576 bytes)** before any buffer allocation occurs.
- The parser processes multiple pipelined frames in a single incoming buffer, advancing the read offset deterministically.

### 3.3 Storage Engine & Incremental Rehashing
- The store is an array of singly-linked list buckets with power-of-two capacity ($2^k$).
- When the load factor exceeds 1.0, a new table of size $2^{k+1}$ is allocated (`table_new`).
- The old table (`table_old`) is not migrated all at once. Instead, each subsequent command migrates a batch of buckets (e.g. 8 buckets), skipping empty buckets up to a bounded visit cap.
- Lookups check `table_new` first, then `table_old`. Inserts write to `table_new` and prune existing keys from `table_old`.

### 3.4 Expiration Subsystem
- Expiration timestamps use monotonic clock offsets.
- **Lazy deletion:** Keys accessed past their expiration deadline are purged immediately.
- **Active deletion:** An active binary min-heap tracks `(expire_at, key, version)`. During each event loop tick, expired keys are popped up to a hard iteration cap (e.g., max 100 entries).
- **Stale entry mitigation:** If a key is overwritten or deleted before its TTL expires, its monotonic version is incremented. When the stale heap node is eventually popped, the version mismatch causes it to be discarded safely in $O(1)$ time.
