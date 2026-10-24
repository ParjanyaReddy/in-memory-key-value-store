# Systems & Architecture Glossary

A living glossary of systems programming, networking, and data structures terminology encountered across the implementation of the `redis_clone` in-memory key-value store.

---

### Networking & I/O

- **Non-blocking I/O (`O_NONBLOCK`):** A socket mode where system calls like `read()` and `write()` return immediately rather than suspending the thread. If no data is available to read or the kernel send buffer is full, the call returns -1 and sets `errno` to `EAGAIN` or `EWOULDBLOCK`.
- **`epoll`:** A scalable Linux-native I/O event notification facility. Unlike `poll()` or `select()`, which require passing and scanning an array of $O(N)$ file descriptors on every call, `epoll` monitors an interest list registered in the kernel and returns only the $O(K)$ file descriptors that transitioned to ready, operating in $O(1)$ amortized time.
- **Level-Triggered vs. Edge-Triggered:**
  - *Level-Triggered (LT):* The kernel reports an event as long as the underlying condition holds (e.g. unread bytes remain in the read buffer). Safer and easier to manage with streaming parsers.
  - *Edge-Triggered (ET):* The kernel reports an event only when state transitions (e.g. new data arrives). Requires reading until `EAGAIN` in a tight loop.
- **TCP Stream Framing:** TCP is a byte stream with no message boundaries. Bytes sent in multiple `write()` calls may be coalesced or fragmented arbitrarily across IP packets. Applications must implement an explicit framing layer (e.g. length prefixes or delimiters) to reconstruct messages.
- **Pipelining:** A technique where a client transmits multiple requests across a single connection without waiting for each individual response, drastically reducing network round-trip latency.
- **`SO_REUSEADDR`:** A socket option enabling a server to immediately bind to a local port even if previous connections on that port are in the TCP `TIME_WAIT` state, preventing `EADDRINUSE` errors upon rapid server restarts.
- **`TCP_NODELAY` (Nagle's Algorithm):** Disables Nagle's algorithm, which buffers small outgoing TCP packets to coalesce them into larger frames. In low-latency request-response protocols, disabling Nagle's algorithm ensures responses are flushed immediately over the wire.
- **`EAGAIN` / `EWOULDBLOCK`:** Error numbers returned by non-blocking system calls indicating that an operation (such as `read` or `write`) would block because no data is currently available or buffers are full.
- **`EINTR`:** Error code returned when a blocking or slow system call is interrupted by the delivery of a signal before any data was transferred. In event loops, calls returning `EINTR` must be retried.

---

### Hash Tables & Data Structures

- **Separate Chaining:** A hash collision resolution strategy where each bucket in the hash table heads a linked list (or chain) of entries that hash to the same bucket index.
- **Load Factor ($\alpha$):** The ratio of total items stored ($N$) to the number of buckets ($B$), defined as $\alpha = \frac{N}{B}$. A load factor $> 1.0$ indicates that chains are growing and lookup performance is degrading towards $O(N)$.
- **Incremental Rehashing:** An amortized resizing technique where table expansion is spread across multiple subsequent operations (e.g., migrating 8 buckets per command) rather than halting the entire server for an $O(N)$ stop-the-world migration.
- **FNV-1a Hash:** Fowler–Noll–Vo hash function (variant 1a). A non-cryptographic, fast hash algorithm with excellent distribution properties and low collision rates, implemented using a 64-bit prime and offset basis with XOR-then-multiply steps.
- **Bitwise Bucket Masking:** Replacing the modulo operator `hash % size` with a bitwise AND `hash & (size - 1)`, valid only when `size` is an exact power of two ($2^k$). Bitwise operations take a single CPU cycle, whereas integer division takes 10–40 cycles.

---

### Expiry & Memory Management

- **Time-to-Live (TTL):** A duration or absolute Unix timestamp after which a key-value entry is considered expired and inaccessible.
- **Lazy Expiry:** Checking the expiration timestamp of a key on demand when it is accessed. If expired, it is deleted and a null response is returned. Space is only freed when the key is read.
- **Active Expiry:** Proactively scanning and deleting expired keys in the background or during event loop iterations using a priority queue (min-heap) ordered by expiration timestamp.
- **Stale Min-Heap Entries:** When an existing key with a TTL is deleted, overwritten, or updated with a new TTL, its old entry remains inside the binary min-heap. Rather than performing an expensive $O(N)$ search or complex heap back-pointers, an explicit monotonic *expiry version* is attached to each key. If the version popped from the heap does not match the key's current version, the entry is discarded as stale.
- **RAII (Resource Acquisition Is Initialization):** A C++ idiom where resources (heap memory, socket file descriptors) are tied to object lifetimes. Destructors guarantee release, preventing leaks.
