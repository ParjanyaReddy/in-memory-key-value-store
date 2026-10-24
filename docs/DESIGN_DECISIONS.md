# Architecture & Design Decisions Log

This document records the architectural and engineering choices made across the implementation of `redis_clone`, including the problem context, chosen solution, alternatives considered, and the associated tradeoffs.

---

## ADR 001: Language Standard & Toolchain Selection

- **Status:** Accepted
- **Context:** We need a modern, high-performance systems language with deterministic memory management, zero-cost abstractions, standard library containers for testing, and broad tooling support (ASan, UBSan, Valgrind, libFuzzer).
- **Decision:** Adopt **C++17** compiled under GCC 15 / Clang 21 with `-Wall -Wextra -Wpedantic`.
- **Alternatives Considered:**
  - *C99 / C11:* Simpler language, but lacks RAII for resource management, requires manual macro templating for testing or oracle comparisons, and provides no standard vector/string abstractions for test harnesses.
  - *C++20 / C++23:* Features like Coroutines and Modules are still maturing in toolchain ecosystem stability across varied Linux distributions. C++17 has universal, mature compiler and toolchain support.
- **Tradeoffs:**
  - *Pros:* Structured RAII for socket and memory management, strong type safety, mature tooling across all CI platforms.
  - *Cons:* Slightly steeper learning curve than C, requires strict adherence to modern idioms to avoid accidental copies or leaks.

---

## ADR 002: Dependency Management for Unit Testing (GoogleTest via FetchContent)

- **Status:** Accepted
- **Context:** We need an automated testing framework for unit testing, oracle testing against standard library containers, and micro-verification.
- **Decision:** Pin GoogleTest (`v1.15.2`) using CMake's `FetchContent` module.
- **Alternatives Considered:**
  - *System package manager (`apt install libgtest-dev`):* Prone to version drift between developer machines and CI runners.
  - *Git submodules:* Requires manual submodule updates and multi-step git checkouts (`git submodule update --init`).
- **Tradeoffs:**
  - *Pros:* Completely self-contained, reproducible builds from a fresh `git clone`, identical behavior across WSL2 and GitHub Actions.
  - *Cons:* Initial build takes an extra 10–15 seconds to fetch and compile GoogleTest.

---

## ADR 003: Memory Safety Strategy (ASan/UBSan + Valgrind Dual Strategy)

- **Status:** Accepted
- **Context:** Systems code with custom memory management (linked list buckets for hash tables, binary heaps, dynamic ring/vector buffers) is vulnerable to buffer overflows, use-after-free, memory leaks, and undefined integer shifts.
- **Decision:** Implement dedicated CMake build targets for AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined`), paired with Valgrind leak runs in CI and local verification.
- **Alternatives Considered:**
  - *Valgrind only:* Too slow (~20-50x) for rapid local iterative test cycles; misses certain stack buffer overruns.
  - *ASan only:* Does not detect reads of uninitialized memory in uninstrumented precompiled binaries.
- **Tradeoffs:**
  - *Pros:* Complete diagnostic coverage across both compile-time instrumentation and runtime emulation.
  - *Cons:* Requires maintaining separate build configurations (Sanitizer vs Release).

---

## ADR 004: Event Loop Multiplexing Abstraction

- **Status:** Accepted
- **Context:** High-performance Linux network servers rely on `epoll`, but developers on macOS or non-Linux systems require portability.
- **Decision:** Introduce an abstract interface `IPoller` with a primary high-performance `EpollPoller` (Linux) and a portable `PollPoller` fallback.
- **Alternatives Considered:**
  - *Linux-only `epoll` directly in `EventLoop`:* Ties codebase strictly to Linux, breaking compilation on developer macOS laptops.
  - *Third-party event library (libuv / libevent):* Violates project ground rules forbidding third-party networking dependencies.
- **Tradeoffs:**
  - *Pros:* Clean separation of concerns; enables native high-performance Linux operation without sacrificing portability.
  - *Cons:* Minimal virtual function dispatch overhead per polling iteration.

---

## ADR 005: Client-Side Dynamic Vector Buffering

- **Status:** Accepted
- **Context:** TCP is a continuous byte stream. Partial reads and partial writes occur frequently under network congestion or when payload sizes vary from small status pings (4 bytes) to large bulk entries (up to 1 MB).
- **Decision:** Utilize `std::vector<uint8_t>` for per-client incoming and outgoing buffers with offset-tracked consumption and linear byte compaction.
- **Alternatives Considered:**
  - *Fixed-size circular ring buffers:* Ring buffers avoid dynamic reallocations, but require wrapping logic for contiguous memory spans and either reject frames exceeding the fixed ring capacity or require complex overflow chains.
  - *Linked list of packet chunks:* High allocation overhead per node and worse memory cache locality during parsing.
- **Tradeoffs:**
  - *Pros:* Contiguous memory layout for fast sequential parsing; straightforward buffer shrinking and clearing.
  - *Cons:* Vector reallocations during large frame bursts, mitigated by pre-sizing and clearing without deallocating capacity.

---

## ADR 006: Dynamic EPOLLOUT Interest Registration

- **Status:** Accepted
- **Context:** Sockets remain writable almost continuously unless kernel socket send buffers are saturated.
- **Decision:** Register client sockets strictly for `EPOLLIN` by default. Only enable `EPOLLOUT` when `has_pending_write()` is true, and immediately revoke `EPOLLOUT` once the write buffer is drained.
- **Alternatives Considered:**
  - *Always-on `EPOLLOUT`:* Keeps `EPOLLOUT` active at all times.
  - *Blocking writes on send failure:* Violates single-threaded non-blocking architecture, freezing the event loop.
- **Tradeoffs:**
  - *Pros:* Completely eliminates 100% CPU spinning / busy-waiting while keeping the event loop purely reactive.
  - *Cons:* Requires an extra `epoll_ctl(EPOLL_CTL_MOD)` call when switching between pending and fully-flushed states.

---

## ADR 007: Collision Resolution Strategy (Separate Chaining)

- **Status:** Accepted
- **Context:** An in-memory store requires high insertion, lookup, and deletion performance with predictable degradation under memory pressure.
- **Decision:** Implement **separate chaining** using singly-linked lists headed by an array of bucket pointers.
- **Alternatives Considered:**
  - *Open Addressing (Linear / Quadratic Probing):* Better cache locality, but deletions require complex tombstone mechanisms, and performance drops catastrophically once load factor exceeds ~0.7.
  - *Robin Hood Hashing:* Bounded probe sequences, but complex backward shift deletions and significant mutation overhead during inserts.
- **Tradeoffs:**
  - *Pros:* Simple $O(1)$ deletions without tombstones; graceful degradation even if load factor temporarily exceeds 1.0; simplified pointer migration during incremental rehashing.
  - *Cons:* Pointer overhead per node (8 bytes `next` pointer); non-contiguous heap allocations causing cache misses on long chains.

---

## ADR 008: Hash Function & Power-of-Two Indexing

- **Status:** Accepted
- **Context:** Bucket calculation `hash % capacity` occurs on every single operation. Integer division instructions take 10–40 CPU cycles.
- **Decision:** Implement 64-bit **FNV-1a** hashing paired with **power-of-two table capacities**, computing bucket indices via bitwise masking: `hash & (capacity - 1)`.
- **Alternatives Considered:**
  - *Prime number table capacities with modulo:* Slightly better protection against poor hash functions, but incurs an expensive division instruction on every lookup.
  - *Cryptographic hash (SHA-256):* Hundreds of cycles per call, excessive for in-memory key-value operations.
- **Tradeoffs:**
  - *Pros:* 1-cycle bitwise masking; uniform bit dispersion from 64-bit FNV-1a.
  - *Cons:* Table sizes double on each resize, temporarily requiring up to 2x memory during transitions.

---

## ADR 009: Dual-Table Incremental Rehashing with Bounded Scans

- **Status:** Accepted
- **Context:** Resizing a large hash table in a single stop-the-world pass takes $O(N)$ time, causing unacceptable millisecond-level latency spikes that stall the single-threaded event loop.
- **Decision:** Implement dual-table incremental rehashing (`table_old_` and `table_new_`). Trigger incremental rehash when load factor $> 1.0$. Migrate a small batch ($N = 8$) of non-empty buckets per command/tick, bounded by a maximum of $10 \times N = 80$ empty bucket visits to avoid stalling on sparse tables. Lookups check `table_new_` then `table_old_`; inserts go into `table_new_` while pruning duplicates from `table_old_`; deletions remove from both tables.
- **Alternatives Considered:**
  - *Stop-the-world resizing:* Simpler single table, but causes 10ms–100ms pauses on 1M+ keys. (Preserved as a baseline for benchmark comparison).
  - *Background thread resizing:* Requires mutexes or lock-free concurrency, introducing locking overhead, thread contention, and cache bouncing on every key access.
- **Tradeoffs:**
  - *Pros:* Eliminates latency spikes; bounds per-operation latency to sub-microsecond levels; preserves single-threaded lock-free architecture.
  - *Cons:* Lookups and deletions require checking two tables during active migration; temporary $1.5\times$ to $2\times$ memory footprint during table growth.

---

## ADR 010: Event Loop Tick Callback for Idle Maintenance

- **Status:** Accepted
- **Context:** Background maintenance tasks (incremental rehashing, active TTL eviction) must progress even when network socket activity is low or bursty.
- **Decision:** Provide a non-blocking `TickCallback` hook on `EventLoop::step()`, invoked once per loop iteration regardless of whether I/O events occurred or the poller timed out.
- **Alternatives Considered:**
  - *POSIX Timer Signals (SIGALRM):* Signal handlers are unsafe for complex data structure manipulations without extensive async-signal-safety precautions.
  - *TimerFD on Linux:* Portable fallback on macOS/BSD (`poll`) lacks `timerfd`, adding OS-specific complexity.
- **Tradeoffs:**
  - *Pros:* Simple, thread-safe, and portable across Linux (`epoll`) and fallback (`poll`); maintenance work is cleanly bounded and interleaved with network dispatch.
  - *Cons:* Slightly higher idle CPU consumption if poller timeout is set too low (configured to 100ms default).

---

## ADR 011: Per-Key Monotonic Versioning for Stale TTL Entries

- **Status:** Accepted
- **Context:** When a key with an active TTL is deleted or overwritten with a new TTL/no TTL, its prior entry in the binary min-heap becomes stale. Searching the min-heap to delete or update that entry requires an expensive $O(N)$ linear search or complex bidirectional index tracking.
- **Decision:** Maintain a 64-bit monotonic `expiry_version` counter in each `HashNode`. Whenever a key is mutated, its version increments. The TTL heap entry records `(expire_at_ms, key, version)`. When popping an expired entry, the system verifies `node != nullptr && node->expiry_version == entry.version && node->expire_at_ms <= now`. If the version mismatches or the key was deleted, the entry is safely discarded in $O(1)$ time.
- **Alternatives Considered:**
  - *Heap back-pointers / index map:* Maintaining a `key -> heap_index` hash map enables $O(\log N)$ heap updates, but requires additional memory, bidirectional pointer updates on every swap in `sift_up`/`sift_down`, and extra map lookups.
  - *Linear heap scan:* $O(N)$ scan per mutation degrades write throughput.
- **Tradeoffs:**
  - *Pros:* Zero overhead on mutations ($O(1)$ version increment); zero extra allocations; robust against arbitrary rapid updates, overwrites, and deletes.
  - *Cons:* Stale heap entries remain in the heap until they bubble to the top and are popped, temporarily consuming $(N_{\text{stale}} \times 24)$ bytes of heap capacity.

---

## ADR 012: Dual-Layer Expiry (Lazy Eviction + Bounded Active Min-Heap)

- **Status:** Accepted
- **Context:** Storing keys with expiration deadlines requires an eviction mechanism that prevents memory buildup of expired keys without introducing latency stalls.
- **Decision:** Implement a dual-layer eviction architecture:
  1. *Lazy Expiry:* On every key access (`get`, `contains`, `ttl`), check if `now >= expire_at_ms`. If expired, delete the key immediately and return null.
  2. *Active Expiry:* On each event loop tick, inspect the binary min-heap and pop entries whose timestamp $\le \text{now}$. Eviction is strictly capped at `max_keys = 100` per tick.
- **Alternatives Considered:**
  - *Redis Randomized Sampling:* Real Redis samples 20 random keys with TTL per cycle, expiring those that passed, and repeats if $> 25\%$ expired. Redis chose this to avoid the memory overhead of a heap.
  - *Pure Lazy Expiry:* Never runs background checks; causes permanent memory leaks for keys that are never queried again.
- **Tradeoffs:**
  - *Pros:* Exact temporal ordering guarantees the earliest expiring keys are evicted first; bounded eviction loop prevents event loop stalls during mass expirations.
  - *Cons:* $O(\log N)$ heap operations per TTL write compared to Redis's $O(1)$ probabilistic sampling.

---

## ADR 013: Monotonic Activity Tracking and Idle Descriptor Reaping

- **Status:** Accepted
- **Context:** Long-lived idle TCP connections consume kernel file descriptors, epoll tracking resources, and per-client buffer metadata. Without proactive timeout management, connection leakage or slowloris-style resource exhaustion attacks can exhaust system FD limits (`RLIMIT_NOFILE`).
- **Decision:** Track `last_active_time_ms_` on every socket read or write event using `CLOCK_MONOTONIC`. Periodically reap idle connections in `EventLoop` tick callbacks when idle duration exceeds `idle_timeout_ms_`. Collect expiring FDs into a local detachment buffer before calling `disconnect_client()` to prevent iterator invalidation of the active client map.
- **Alternatives Considered:**
  - *TCP Keepalive (`SO_KEEPALIVE`):* OS kernel level, but timeout intervals (typically 2 hours default) are too slow for application-level connection pool cleanup and lack visibility into application command activity.
  - *Timer wheels or per-connection timers:* Heavy per-connection timer overhead; unnecessary for low-granularity idle timeout sweeps.
- **Tradeoffs:**
  - *Pros:* Completely prevents connection leakage; $O(C)$ sweep across active clients per idle check interval; zero allocation during normal active communication.
  - *Cons:* Requires periodic tick check in event loop.

---

## ADR 014: Coverage-Guided In-Process Protocol Fuzzing (LLVM libFuzzer)

- **Status:** Accepted
- **Context:** Network-facing binary wire protocol parsers are the primary attack surface for buffer overflows, integer wrap-arounds, and denial-of-service hangs.
- **Decision:** Integrate LLVM `libFuzzer` target (`fuzz_frame_parser`) alongside continuous pseudo-random mutated stream fuzzing in unit tests.
- **Alternatives Considered:**
  - *AFL++ / Out-of-process fuzzers:* Requires fork-server overhead, slower iteration rates.
  - *Unit-test-only hand-crafted bad inputs:* Limited mutation search space; cannot discover subtle edge cases in multi-byte fragmented length headers.
- **Tradeoffs:**
  - *Pros:* Explores deep parser state transitions at >250,000 executions/sec; seamlessly integrated with AddressSanitizer and UndefinedBehaviorSanitizer.
  - *Cons:* Requires Clang compiler with `-fsanitize=fuzzer`.
