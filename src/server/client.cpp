#include "server/client.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

#include "util/time_util.h"

namespace redis_clone {
namespace server {

Client::Client(int fd) : fd_(fd), last_active_time_ms_(util::current_time_ms()) {}

Client::~Client() {
    close_socket();
}

Client::Client(Client&& other) noexcept
    : fd_(other.fd_),
      read_buffer_(std::move(other.read_buffer_)),
      write_buffer_(std::move(other.write_buffer_)),
      write_offset_(other.write_offset_),
      last_active_time_ms_(other.last_active_time_ms_) {
    other.fd_ = -1;
    other.write_offset_ = 0;
    other.last_active_time_ms_ = 0;
}

Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        close_socket();
        fd_ = other.fd_;
        read_buffer_ = std::move(other.read_buffer_);
        write_buffer_ = std::move(other.write_buffer_);
        write_offset_ = other.write_offset_;
        last_active_time_ms_ = other.last_active_time_ms_;

        other.fd_ = -1;
        other.write_offset_ = 0;
        other.last_active_time_ms_ = 0;
    }
    return *this;
}

void Client::close_socket() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

ReadStatus Client::read_from_socket() {
    if (fd_ < 0)
        return ReadStatus::kError;

    constexpr size_t kChunkSize = 4096;
    uint8_t buffer[kChunkSize];

    while (true) {
        ssize_t bytes_read = read(fd_, buffer, kChunkSize);
        if (bytes_read > 0) {
            read_buffer_.insert(read_buffer_.end(), buffer, buffer + bytes_read);
            last_active_time_ms_ = util::current_time_ms();
            continue;
        }

        if (bytes_read == 0) {
            return ReadStatus::kClosed;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ReadStatus::kOk;
        }
        if (errno == EINTR) {
            continue;
        }
        return ReadStatus::kError;
    }
}

WriteStatus Client::write_to_socket() {
    if (fd_ < 0)
        return WriteStatus::kError;
    if (!has_pending_write())
        return WriteStatus::kComplete;

    while (write_offset_ < write_buffer_.size()) {
        const uint8_t* ptr = write_buffer_.data() + write_offset_;
        size_t remaining = write_buffer_.size() - write_offset_;

        ssize_t bytes_written = write(fd_, ptr, remaining);
        if (bytes_written > 0) {
            write_offset_ += static_cast<size_t>(bytes_written);
            last_active_time_ms_ = util::current_time_ms();
            continue;
        }

        if (bytes_written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return WriteStatus::kPartial;
            }
            if (errno == EINTR) {
                continue;
            }
            return WriteStatus::kError;
        }
    }

    // Completely flushed all queued bytes
    write_buffer_.clear();
    write_offset_ = 0;
    return WriteStatus::kComplete;
}

void Client::append_response(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0)
        return;

    if (write_offset_ > 0 && write_offset_ == write_buffer_.size()) {
        write_buffer_.clear();
        write_offset_ = 0;
    }
    write_buffer_.insert(write_buffer_.end(), data, data + length);
}

void Client::append_response(const std::string& text) {
    append_response(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void Client::consume_read_bytes(size_t count) {
    if (count == 0)
        return;
    if (count >= read_buffer_.size()) {
        read_buffer_.clear();
    } else {
        read_buffer_.erase(read_buffer_.begin(),
                           read_buffer_.begin() + static_cast<ptrdiff_t>(count));
    }
}

}  // namespace server
}  // namespace redis_clone
