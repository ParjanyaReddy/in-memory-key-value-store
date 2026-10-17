#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "server/event_loop.h"

namespace {
redis_clone::server::EventLoop* g_loop = nullptr;

void handle_signal(int sig) {
    (void)sig;
    if (g_loop) {
        g_loop->stop();
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    int port = 6379;
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Invalid port specified: " << argv[1] << std::endl;
            return 1;
        }
    }

    std::cout << "Starting redis_clone echo server on port " << port << "..." << std::endl;

    redis_clone::server::EventLoop loop;
    if (!loop.init("0.0.0.0", port)) {
        std::cerr << "Failed to bind and listen on port " << port << std::endl;
        return 1;
    }

    // Configure Phase 1 echo handler
    loop.set_message_handler([](redis_clone::server::Client& client) {
        const auto& buf = client.read_buffer();
        client.append_response(buf.data(), buf.size());
        client.consume_read_bytes(buf.size());
    });

    g_loop = &loop;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "Server ready. Listening for incoming connections..." << std::endl;
    loop.run();

    std::cout << "Server shutting down cleanly." << std::endl;
    g_loop = nullptr;
    return 0;
}
