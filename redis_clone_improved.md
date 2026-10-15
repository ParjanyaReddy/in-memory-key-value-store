# In-Memory Key-Value Store (Improved Guide)

A Redis-inspired key-value server in C++ built around a **single-threaded event loop**, a hand-written hash table with **incremental rehashing**, and TTL expiry. The goal is to demonstrate systems fundamentals *with measurements*.

**Stack:** C++17, POSIX sockets, `epoll` (Linux) or `poll`, CMake, GoogleTest, ASan/UBSan, Valgrind.

---

## 0. Ground Rules

- **Write the hash table, rehashing, protocol parser, and heap yourself.** AI can scaffold sockets and CMake.
- **Say "single-threaded command execution,"** not "no threads." Real Redis uses background threads for lazy freeing and (optionally) I/O.
- **Don't claim "like Redis does" for the TTL heap.** Real Redis uses lazy expiry plus randomized active sampling. Your heap is a deliberate alternative, so know why it differs.

---

## 1. Architecture

```
clients -> [epoll event loop] -> per-client read buffer -> protocol parser -> command executor
                                                                  |
                                              HashTable (old + new) + TTL min-heap
                                                                  |
                                          per-client write buffer <- response
```

### Event loop
1. `epoll_wait` returns the ready sockets.
2. For readable sockets: `read()` into the client's buffer until `EAGAIN`.
3. Parse as many complete frames as the buffer holds (supports pipelining), execute, append responses to the write buffer.
4. For writable sockets: `write()` until buffer empty or `EAGAIN`. Only register for write-readiness while data is pending.
5. Each iteration: run expiry check and incremental-rehash step.

Handle: client disconnects mid-message, oversized length headers (reject above a max, e.g. 1 MB), idle connection timeouts.

---

## 2. Protocol (TCP is a byte stream)

- Frame = **4-byte little-endian length** + payload.
- Parser state machine: need header? -> have 4 bytes? -> read length -> have full payload? -> execute and consume, else wait.
- Validate length *before* allocating, to avoid memory exhaustion from a malicious header.
- Test with input split into every possible chunking (1 byte at a time, random splits, multiple frames in one read).

---

## 3. Hash Table (separate chaining)

- Array of bucket heads, each a singly linked list. Power-of-two sizes so `hash & (size-1)` replaces modulo.
- Hash: FNV-1a or MurmurHash.
- Load factor threshold to trigger resize: `> 1.0` (configurable, so you can benchmark alternatives).

### Incremental rehashing
Keep `table_old` and `table_new`.

1. On resize trigger: allocate `table_new` at 2x size, set `rehash_index = 0`.
2. Each command (`GET`/`SET`/`DEL`) migrates a small batch of old buckets (e.g. 8), advancing `rehash_index`. Skip empty buckets with a visit cap per step so a sparse table can't cause a long pause.
3. **Lookups** check `table_new` then `table_old`.
4. **Inserts** go to `table_new`. If the key exists in `table_old`, remove it there (or update in place) to avoid duplicates.
5. **Deletes** must check both tables.
6. When `table_old` is empty, free it and make `table_new` the main table.
7. Don't start a new resize while one is in progress.

---

## 4. TTL / Expiry

Two layers:
- **Lazy expiry:** on every access, if the key is past its deadline, delete it and return null.
- **Active expiry (min-heap):** the heap holds `(expire_at, key)`. Each loop iteration, pop entries whose time has passed.

**Edge cases you must handle (interviewers love these):**
- Key deleted or overwritten before expiry -> heap entry is stale. Either store a per-key expiry version and ignore mismatches when popped, or keep a key->heap-index map for O(log N) removal.
- TTL updated -> same issue, same fix.
- Cap work per loop iteration (e.g. expire at most 100 keys) so a mass-expiry event doesn't stall the loop.

**Be ready to compare:** Redis's sampling approach (probabilistic, bounded memory overhead, no heap maintenance) vs. your heap (exact ordering, O(log N) per TTL set, extra memory).

---

## 5. Build Roadmap

| Phase | You write | AI may help with |
|---|---|---|
| 1. Network skeleton | Client struct, buffer handling | socket/epoll boilerplate, CMake |
| 2. Hash table + commands | Entire hash table, GET/SET/DEL | Command string parsing |
| 3. Protocol | Frame parser state machine | Test client |
| 4. Incremental rehash | Migration logic, dual lookup | |
| 5. TTL | Min-heap, stale-entry handling | |
| 6. Tests + benchmarks | Test design, benchmark client | Plot scripts |

---

## 6. Test Plan (source of your metrics)

| Test | Assertion | Metric to record |
|---|---|---|
| Fragmented input: every split point of a frame | Same result as a single read | `[X]` split patterns, 0 failures |
| Fuzz random bytes at the parser (libFuzzer) | No crash, hang, or sanitizer error | `[X]` million inputs |
| Run full suite under ASan + UBSan | Clean | 0 errors, 0 leaks |
| Hash table vs. `std::unordered_map` as oracle on random ops | Identical results | `[X]` random operations |
| Operate during an active rehash (GET/SET/DEL mid-migration) | Always correct | |
| TTL: set, overwrite, delete, update, expire | No resurrected or stale keys | |
| 10,000 concurrent connections | Server stays stable | peak memory `[X]` MB |

### Benchmarks
Hardware, OS, and build flags must be stated. Record:
- Throughput (ops/sec) for GET/SET at `[X]` concurrent clients, pipelined and non-pipelined.
- **p99 and max latency while growing the table to `[N]` million keys, with incremental rehashing vs. a naive stop-the-world resize.** This is your headline result. Plot latency over time showing the spike in the naive version and its absence in yours.
- Optional: compare against real `redis-server` using `redis-benchmark` and report the honest ratio.
- Memory per key (bytes) vs. `std::unordered_map`.

---

## 7. Repo Checklist

- [ ] `cmake .. && make && ./tests` works from a clean clone
- [ ] CI runs tests plus a sanitizer build
- [ ] README: architecture diagram, protocol spec, benchmark graphs, design tradeoffs, known limitations (no persistence, no replication)
- [ ] A results table with hardware specs

---

## 8. Interview Defense

- **Blocking vs non-blocking I/O?** Blocking `read()` suspends the thread until data arrives. Non-blocking returns `EAGAIN` immediately, letting one thread service many sockets.
- **Why is single-threaded fast?** No lock contention or context switching, in-memory ops take nanoseconds, and the bottleneck is usually network I/O, not CPU. Tradeoff: one slow command blocks everyone, and you can't use multiple cores (scale by running multiple instances/sharding).
- **`poll` vs `epoll`?** `poll` is O(n) per call over all fds; `epoll` returns only ready fds, so it scales to many idle connections. Know level- vs edge-triggered.
- **Why separate chaining?** Graceful degradation under load and simple deletion, vs. open addressing's clustering and tombstones. Tradeoff: worse cache locality and per-node allocation overhead. Be able to say that.
- **Why incremental rehash?** A full resize is O(N) in one call, producing a latency spike. Amortizing it keeps every operation's latency bounded. Costs: temporary extra memory and more complex lookups.
- **What if a client sends a 4 GB length header?** Enforce a max frame size before allocating.
- **How do you prevent leaks?** Ownership discipline (RAII or explicit free paths), verified with ASan/Valgrind.
- **What's missing vs real Redis?** Persistence (RDB/AOF), replication, richer data types, memory eviction policies. Mention how you'd add AOF.

---

## 9. Resume Bullets (fill in only measured values)

- Built a single-threaded C++ key-value server on an `epoll` event loop handling **`[X]` concurrent connections at `[Y]` ops/sec** (`[hardware]`), reaching **`[Z]%` of `redis-server` throughput** on `redis-benchmark`.
- Implemented a custom hash table with incremental rehashing, reducing **max resize latency from `[X]` ms to `[Y]` ms at `[N]`M keys** versus a stop-the-world baseline.
- Designed a length-prefixed binary protocol with per-client buffers; validated against **`[X]` fragmentation patterns and `[Y]`M fuzzed inputs** with zero crashes under ASan/UBSan.
- Implemented TTL expiry with a min-heap and lazy deletion, with bounded per-iteration work to avoid event-loop stalls; verified correct under overwrite/delete/update races across `[X]` randomized tests.

> If a number isn't measured, delete it and describe the mechanism instead. Never guess.
