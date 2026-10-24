#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "protocol/command.h"
#include "protocol/frame_parser.h"
#include "server/event_loop.h"
#include "store/database.h"
#include "util/logging.h"

namespace {
redis_clone::server::EventLoop* g_loop = nullptr;

void handle_signal(int sig) {
    (void)sig;
    if (g_loop) {
        LOG_INFO("Shutdown signal received. Stopping event loop...");
        g_loop->stop();
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    int port = 6379;
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            LOG_ERROR("Invalid port specified: ", argv[1]);
            return 1;
        }
    }

    LOG_INFO("Starting redis_clone server on port ", port, "...");

    redis_clone::server::EventLoop loop;
    if (!loop.init("0.0.0.0", port)) {
        LOG_ERROR("Failed to bind and listen on port ", port);
        return 1;
    }

    redis_clone::store::Database db;

    // Configure protocol frame parser and command dispatcher
    loop.set_message_handler([&db](redis_clone::server::Client& client) {
        std::vector<std::string> frames;
        std::string error_msg;
        auto status = redis_clone::protocol::FrameParser::parse_all_frames(
            client.mutable_read_buffer(), frames, error_msg);

        if (status == redis_clone::protocol::ParseStatus::kErrorTooLarge) {
            std::string err_resp =
                redis_clone::protocol::CommandDispatcher::format_error("ERR frame too large");
            auto out_frame = redis_clone::protocol::FrameParser::serialize_frame(err_resp);
            client.append_response(out_frame.data(), out_frame.size());
            client.write_to_socket();
            client.close_socket();
            return;
        }

        for (const auto& raw_frame : frames) {
            auto cmd = redis_clone::protocol::CommandDispatcher::parse(raw_frame);
            std::string response = redis_clone::protocol::CommandDispatcher::execute(cmd, db);
            auto out_frame = redis_clone::protocol::FrameParser::serialize_frame(response);
            client.append_response(out_frame.data(), out_frame.size());
        }
    });

    // Background iteration tick: steps incremental rehashing and active TTL expiry
    loop.set_tick_callback([&db]() { db.step(); });

    g_loop = &loop;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    LOG_INFO("Server ready. Listening for incoming connections...");
    loop.run();

    LOG_INFO("Server shutting down cleanly.");
    g_loop = nullptr;
    return 0;
}
