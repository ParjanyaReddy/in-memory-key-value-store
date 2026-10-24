# Protocol Specification: Length-Prefixed Binary Protocol

This document defines the wire protocol used by `redis_clone` for client-server communication over non-blocking TCP sockets.

---

## 1. Frame Wire Format

Each protocol frame consists of a fixed-size 4-byte header followed by a variable-length payload:

```
+------------------------------------+---------------------------------------+
|  Length (4 bytes, Little-Endian)   |               Payload                 |
|            uint32_t                |          (Length raw bytes)           |
+------------------------------------+---------------------------------------+
```

### 1.1 Header Fields
- **Length (32-bit unsigned integer, little-endian):**
  - Specifies the exact number of bytes in the payload.
  - Byte 0: least significant byte; Byte 3: most significant byte.
  - **Maximum Allowed Size:** `1,048,576 bytes` (1 MB). Frames specifying a length larger than 1 MB are rejected immediately with a protocol error without memory allocation.

---

## 2. Command Serialization (Payload)

The payload encodes commands as a sequence of space-separated or newline-delimited strings:

```
<COMMAND> <KEY> [VALUE] [TTL_SECONDS]
```

### Supported Commands

| Command | Arguments | Description | Example Payload |
|---|---|---|---|
| `PING` | `None` | Health check / keepalive | `PING` |
| `GET` | `<key>` | Retrieves the value for `<key>` | `GET session:123` |
| `SET` | `<key> <val>` | Stores `<key>` with value `<val>` | `SET session:123 user_42` |
| `SET` (with TTL) | `<key> <val> EX <sec>` | Stores `<key>` with an expiration deadline | `SET session:123 user_42 EX 60` |
| `DEL` | `<key>` | Deletes `<key>` from the database | `DEL session:123` |
| `EXPIRE` | `<key> <sec>` | Sets or updates expiration on `<key>` | `EXPIRE session:123 120` |

---

## 3. Server Response Format

All responses from the server are formatted using the same length-prefixed binary frame. The payload contains a status indicator:

- **Success / Value:**
  - `+OK\r\n`: Operation succeeded (e.g. `SET`).
  - `+PONG\r\n`: Response to `PING`.
  - `$5\r\nhello\r\n`: Value string returned from `GET`.
  - `$-1\r\n`: Key not found or key expired (nil response).
  - `:1\r\n` or `:0\r\n`: Integer response (e.g. `DEL` returned 1 key deleted).
- **Errors:**
  - `-ERR <reason>\r\n`: General command or syntax error.
  - `-ERR frame too large\r\n`: Frame length header exceeded 1 MB.

---

## 4. Pipelining & Network Semantics

- **Pipelining:** Clients may send multiple consecutive frames without awaiting intermediate responses. The server parses each complete frame in sequence and appends responses to the client's write buffer.
- **Partial Reads:** If the incoming buffer contains fewer than 4 bytes, or fewer than `4 + length` bytes, the parser halts consumption and waits for the next read event.
- **Zero-Allocation Error Path:** If the length header exceeds 1 MB, the server immediately marks the connection for termination and writes an error response, preventing malicious clients from inducing an out-of-memory denial of service.
