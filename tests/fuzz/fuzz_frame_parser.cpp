#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "protocol/command.h"
#include "protocol/frame_parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // 1. Full buffer feeding
    std::vector<uint8_t> buffer(data, data + size);
    std::vector<std::string> frames;
    std::string error_msg;

    redis_clone::protocol::ParseStatus status =
        redis_clone::protocol::FrameParser::parse_all_frames(buffer, frames, error_msg);
    (void)status;

    for (const auto& frame : frames) {
        auto cmd = redis_clone::protocol::CommandDispatcher::parse(frame);
        (void)cmd;
    }

    // 2. Incremental chunk streaming (split at midpoint)
    if (size >= 4) {
        std::vector<uint8_t> stream_buf;
        size_t mid = size / 2;

        stream_buf.insert(stream_buf.end(), data, data + mid);
        redis_clone::protocol::FrameParser::parse_all_frames(stream_buf, frames, error_msg);

        stream_buf.insert(stream_buf.end(), data + mid, data + size);
        redis_clone::protocol::FrameParser::parse_all_frames(stream_buf, frames, error_msg);
    }

    return 0;
}
