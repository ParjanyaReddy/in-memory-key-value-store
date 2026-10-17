#ifndef REDIS_CLONE_SERVER_CLIENT_H_
#define REDIS_CLONE_SERVER_CLIENT_H_

#include <cstdint>
#include <string>
#include <vector>

namespace redis_clone {
namespace server {

enum class ReadStatus {
    kOk,      // Read some bytes or exhausted socket with EAGAIN
    kClosed,  // Client closed connection cleanly (EOF / read 0 bytes)
    kError    // Socket error encountered
};

enum class WriteStatus {
    kComplete,  // All queued write data transmitted
    kPartial,   // Transmitted partial data; blocked on EAGAIN
    kError      // Socket write error encountered
};

/**
 * CORE LOGIC: understand this file line by line.
 * Represents an active client connection, managing socket I/O and dynamic buffers.
 */
class Client {
public:
    explicit Client(int fd);
    ~Client();

    // Disable copy semantics to guarantee exclusive socket ownership
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Enable move semantics
    Client(Client&& other) noexcept;
    Client& operator=(Client&& other) noexcept;

    int fd() const { return fd_; }

    /**
     * Non-blocking read from socket into incoming read buffer until EAGAIN.
     */
    ReadStatus read_from_socket();

    /**
     * Non-blocking flush of pending write buffer to socket until empty or EAGAIN.
     */
    WriteStatus write_to_socket();

    /**
     * Queue data into client's outgoing write buffer.
     */
    void append_response(const uint8_t* data, size_t length);
    void append_response(const std::string& text);

    /**
     * Direct access to incoming read buffer and consumption.
     */
    const std::vector<uint8_t>& read_buffer() const { return read_buffer_; }
    void consume_read_bytes(size_t count);

    /**
     * Status inspectors.
     */
    bool has_pending_write() const { return write_offset_ < write_buffer_.size(); }
    size_t pending_write_bytes() const { return write_buffer_.size() - write_offset_; }

    /**
     * Close the underlying socket descriptor cleanly.
     */
    void close_socket();

private:
    int fd_{-1};
    std::vector<uint8_t> read_buffer_;
    std::vector<uint8_t> write_buffer_;
    size_t write_offset_{0};
};

}  // namespace server
}  // namespace redis_clone

#endif  // REDIS_CLONE_SERVER_CLIENT_H_
