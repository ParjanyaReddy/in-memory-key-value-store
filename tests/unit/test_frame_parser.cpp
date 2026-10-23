#include <gtest/gtest.h>

#include <random>

#include "protocol/command.h"
#include "protocol/frame_parser.h"
#include "store/hash_table.h"

using namespace redis_clone::protocol;
using namespace redis_clone::store;

TEST(FrameParserTest, BasicSerializationAndParsing) {
    std::string message = "PING";
    std::vector<uint8_t> frame = FrameParser::serialize_frame(message);

    ASSERT_EQ(frame.size(), 4u + 4u);
    EXPECT_EQ(frame[0], 4);
    EXPECT_EQ(frame[1], 0);
    EXPECT_EQ(frame[2], 0);
    EXPECT_EQ(frame[3], 0);

    std::vector<std::string> extracted;
    std::string err;
    ParseStatus status = FrameParser::parse_all_frames(frame, extracted, err);

    EXPECT_EQ(status, ParseStatus::kComplete);
    ASSERT_EQ(extracted.size(), 1u);
    EXPECT_EQ(extracted[0], "PING");
    EXPECT_TRUE(frame.empty());  // Buffer consumed
}

TEST(FrameParserTest, EverySplitPointFragmentation) {
    std::string message = "SET benchmark_key 42_value_payload";
    std::vector<uint8_t> full_frame = FrameParser::serialize_frame(message);

    // Test every single possible split boundary
    for (size_t split = 1; split < full_frame.size(); ++split) {
        std::vector<uint8_t> stream_buf;
        std::vector<std::string> extracted;
        std::string err;

        // Feed first chunk
        stream_buf.insert(stream_buf.end(), full_frame.begin(), full_frame.begin() + split);
        ParseStatus status1 = FrameParser::parse_all_frames(stream_buf, extracted, err);

        EXPECT_EQ(status1, ParseStatus::kComplete);
        EXPECT_EQ(extracted.size(), 0u);
        EXPECT_EQ(stream_buf.size(), split);

        // Feed remaining chunk
        stream_buf.insert(stream_buf.end(), full_frame.begin() + split, full_frame.end());
        ParseStatus status2 = FrameParser::parse_all_frames(stream_buf, extracted, err);

        EXPECT_EQ(status2, ParseStatus::kComplete);
        ASSERT_EQ(extracted.size(), 1u);
        EXPECT_EQ(extracted[0], message);
        EXPECT_TRUE(stream_buf.empty());
    }
}

TEST(FrameParserTest, SingleByteStreamingFragmentation) {
    std::string message = "GET user_session_token_12345";
    std::vector<uint8_t> full_frame = FrameParser::serialize_frame(message);

    std::vector<uint8_t> stream_buf;
    std::vector<std::string> extracted;
    std::string err;

    for (size_t i = 0; i < full_frame.size(); ++i) {
        stream_buf.push_back(full_frame[i]);
        ParseStatus status = FrameParser::parse_all_frames(stream_buf, extracted, err);
        EXPECT_EQ(status, ParseStatus::kComplete);

        if (i < full_frame.size() - 1) {
            EXPECT_EQ(extracted.size(), 0u);
        } else {
            ASSERT_EQ(extracted.size(), 1u);
            EXPECT_EQ(extracted[0], message);
            EXPECT_TRUE(stream_buf.empty());
        }
    }
}

TEST(FrameParserTest, PipelinedMultipleFramesInSingleRead) {
    std::vector<uint8_t> pipelined_buffer;
    std::vector<std::string> expected_messages = {"PING", "SET key1 value1", "GET key1", "DEL key1",
                                                  "PING final_check"};

    for (const auto& msg : expected_messages) {
        auto frame = FrameParser::serialize_frame(msg);
        pipelined_buffer.insert(pipelined_buffer.end(), frame.begin(), frame.end());
    }

    std::vector<std::string> extracted;
    std::string err;
    ParseStatus status = FrameParser::parse_all_frames(pipelined_buffer, extracted, err);

    EXPECT_EQ(status, ParseStatus::kComplete);
    ASSERT_EQ(extracted.size(), expected_messages.size());
    for (size_t i = 0; i < expected_messages.size(); ++i) {
        EXPECT_EQ(extracted[i], expected_messages[i]);
    }
    EXPECT_TRUE(pipelined_buffer.empty());
}

TEST(FrameParserTest, RejectsOversizedHeaderBeforeAllocation) {
    // 1 MB + 1 byte
    uint32_t oversized_len = 1024 * 1024 + 1;
    uint8_t malicious_header[4] = {static_cast<uint8_t>(oversized_len & 0xFF),
                                   static_cast<uint8_t>((oversized_len >> 8) & 0xFF),
                                   static_cast<uint8_t>((oversized_len >> 16) & 0xFF),
                                   static_cast<uint8_t>((oversized_len >> 24) & 0xFF)};

    std::string payload;
    size_t consumed = 0;
    std::string err;

    ParseStatus status = FrameParser::parse_one_frame(malicious_header, sizeof(malicious_header),
                                                      payload, consumed, err);

    EXPECT_EQ(status, ParseStatus::kErrorTooLarge);
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(consumed, 0u);
}

TEST(CommandDispatcherTest, ExecutesBasicCommands) {
    HashTable table;

    // PING
    Command ping_cmd = CommandDispatcher::parse("PING");
    EXPECT_EQ(CommandDispatcher::execute(ping_cmd, table), "+PONG\r\n");

    // PING with message
    Command ping_msg = CommandDispatcher::parse("PING \"custom message\"");
    EXPECT_EQ(CommandDispatcher::execute(ping_msg, table), "$14\r\ncustom message\r\n");

    // SET
    Command set_cmd = CommandDispatcher::parse("SET mykey myval");
    EXPECT_EQ(CommandDispatcher::execute(set_cmd, table), "+OK\r\n");

    // GET
    Command get_cmd = CommandDispatcher::parse("GET mykey");
    EXPECT_EQ(CommandDispatcher::execute(get_cmd, table), "$5\r\nmyval\r\n");

    // DEL
    Command del_cmd = CommandDispatcher::parse("DEL mykey");
    EXPECT_EQ(CommandDispatcher::execute(del_cmd, table), ":1\r\n");

    // GET after DEL (nil)
    EXPECT_EQ(CommandDispatcher::execute(get_cmd, table), "$-1\r\n");

    // Unknown command
    Command unknown_cmd = CommandDispatcher::parse("FOOBAR");
    EXPECT_EQ(CommandDispatcher::execute(unknown_cmd, table), "-ERR unknown command 'FOOBAR'\r\n");
}

TEST(FrameParserTest, FuzzRandomBytesContinuous) {
    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<size_t> len_dist(0, 1024);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    constexpr int kFuzzIterations = 10000;
    std::vector<uint8_t> fuzz_buf;

    for (int iter = 0; iter < kFuzzIterations; ++iter) {
        size_t len = len_dist(rng);
        fuzz_buf.resize(len);
        for (size_t i = 0; i < len; ++i) {
            fuzz_buf[i] = static_cast<uint8_t>(byte_dist(rng));
        }

        std::vector<std::string> frames;
        std::string err;
        FrameParser::parse_all_frames(fuzz_buf, frames, err);

        for (const auto& f : frames) {
            auto cmd = CommandDispatcher::parse(f);
            (void)cmd;
        }
    }
}
