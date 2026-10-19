#ifndef REDIS_CLONE_PROTOCOL_FRAME_PARSER_H_
#define REDIS_CLONE_PROTOCOL_FRAME_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace redis_clone {
namespace protocol {

enum class ParseStatus {
    kComplete,       // Successfully extracted a complete frame
    kIncomplete,     // Need more bytes from network stream
    kErrorTooLarge,  // Frame header exceeds maximum allowed size (1 MB)
    kErrorCorrupted  // Malformed frame or protocol violation
};

/**
 * CORE LOGIC: understand this file line by line.
 * Length-prefixed binary stream parser.
 * Wire format: [4-byte Little-Endian uint32_t length] + [raw payload bytes].
 */
class FrameParser {
public:
    static constexpr size_t kHeaderSize = 4;
    static constexpr size_t kMaxPayloadSize = 1024 * 1024;  // 1 MB ceiling

    /**
     * Attempts to parse a single frame from the buffer starting at offset.
     * On kComplete, outputs payload and sets bytes_consumed (4 + payload.size()).
     * On kErrorTooLarge, sets error_message and bytes_consumed is 0.
     */
    static ParseStatus parse_one_frame(const uint8_t* data, size_t size, std::string& payload,
                                       size_t& bytes_consumed, std::string& error_message);

    /**
     * Parses as many complete frames as available from buffer, consuming them in place.
     * Supports TCP pipelining (multiple consecutive frames in a single buffer).
     */
    static ParseStatus parse_all_frames(std::vector<uint8_t>& buffer,
                                        std::vector<std::string>& frames,
                                        std::string& error_message);

    /**
     * Encodes a payload into a length-prefixed binary frame (4-byte LE length + payload).
     */
    static std::vector<uint8_t> serialize_frame(const std::string& payload);
    static std::vector<uint8_t> serialize_frame(const uint8_t* data, size_t length);
};

}  // namespace protocol
}  // namespace redis_clone

#endif  // REDIS_CLONE_PROTOCOL_FRAME_PARSER_H_
