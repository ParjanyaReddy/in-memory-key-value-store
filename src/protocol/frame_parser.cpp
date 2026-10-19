#include "protocol/frame_parser.h"

#include <cstring>

namespace redis_clone {
namespace protocol {

ParseStatus FrameParser::parse_one_frame(const uint8_t* data, size_t size, std::string& payload,
                                         size_t& bytes_consumed, std::string& error_message) {
    bytes_consumed = 0;

    // Need at least 4 bytes to inspect the length header
    if (size < kHeaderSize) {
        return ParseStatus::kIncomplete;
    }

    // Little-endian 32-bit length decode
    uint32_t payload_len = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                           (static_cast<uint32_t>(data[2]) << 16) |
                           (static_cast<uint32_t>(data[3]) << 24);

    // Enforce 1 MB limit BEFORE allocating buffer space to prevent memory exhaustion
    if (payload_len > kMaxPayloadSize) {
        error_message = "Frame exceeds maximum allowed size (1 MB)";
        return ParseStatus::kErrorTooLarge;
    }

    // Wait until full payload has arrived
    if (size < kHeaderSize + payload_len) {
        return ParseStatus::kIncomplete;
    }

    payload.assign(reinterpret_cast<const char*>(data + kHeaderSize), payload_len);
    bytes_consumed = kHeaderSize + payload_len;
    return ParseStatus::kComplete;
}

ParseStatus FrameParser::parse_all_frames(std::vector<uint8_t>& buffer,
                                          std::vector<std::string>& frames,
                                          std::string& error_message) {
    size_t offset = 0;

    while (offset < buffer.size()) {
        std::string frame_payload;
        size_t consumed = 0;

        ParseStatus status = parse_one_frame(buffer.data() + offset, buffer.size() - offset,
                                             frame_payload, consumed, error_message);

        if (status == ParseStatus::kComplete) {
            frames.push_back(std::move(frame_payload));
            offset += consumed;
        } else if (status == ParseStatus::kIncomplete) {
            break;  // Partial frame remaining; wait for subsequent network read
        } else {
            // Unrecoverable framing error (e.g. frame exceeds 1 MB)
            if (offset > 0) {
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(offset));
            }
            return status;
        }
    }

    if (offset > 0) {
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<ptrdiff_t>(offset));
    }
    return ParseStatus::kComplete;
}

std::vector<uint8_t> FrameParser::serialize_frame(const uint8_t* data, size_t length) {
    std::vector<uint8_t> frame(kHeaderSize + length);
    frame[0] = static_cast<uint8_t>(length & 0xFF);
    frame[1] = static_cast<uint8_t>((length >> 8) & 0xFF);
    frame[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
    frame[3] = static_cast<uint8_t>((length >> 24) & 0xFF);

    if (length > 0 && data != nullptr) {
        std::memcpy(frame.data() + kHeaderSize, data, length);
    }
    return frame;
}

std::vector<uint8_t> FrameParser::serialize_frame(const std::string& payload) {
    return serialize_frame(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}

}  // namespace protocol
}  // namespace redis_clone
