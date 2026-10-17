#ifndef REDIS_CLONE_SERVER_EVENT_LOOP_H_
#define REDIS_CLONE_SERVER_EVENT_LOOP_H_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/client.h"
#include "server/poller.h"

namespace redis_clone {
namespace server {

/**
 * CORE LOGIC: understand this file line by line.
 * Single-threaded event loop multiplexing network sockets and dispatching I/O events.
 */
class EventLoop {
public:
    using MessageHandler = std::function<void(Client& client)>;
    using ConnectionHandler = std::function<void(int fd)>;

    explicit EventLoop(std::unique_ptr<IPoller> poller = nullptr);
    ~EventLoop();

    // Prevent copying
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /**
     * Bind listening socket and initialize the poller.
     */
    bool init(const std::string& host, int port);

    /**
     * Attach an externally created listening file descriptor (useful in tests).
     */
    bool attach_listener(int listen_fd);

    /**
     * Execute a single tick of the event loop with a timeout.
     */
    void step(int timeout_ms = 100);

    /**
     * Run the event loop continuously until stop() is invoked.
     */
    void run();

    /**
     * Signal the event loop to stop processing.
     */
    void stop() { running_ = false; }
    bool is_running() const { return running_; }

    /**
     * Register callbacks for message processing and disconnections.
     */
    void set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
    void set_disconnect_handler(ConnectionHandler handler) {
        disconnect_handler_ = std::move(handler);
    }

    /**
     * Access client registry.
     */
    size_t active_clients_count() const { return clients_.size(); }
    Client* get_client(int fd);

    /**
     * Disconnect a client connection cleanly.
     */
    void disconnect_client(int fd);

private:
    void handle_accept();
    void handle_client_read(int fd);
    void handle_client_write(int fd);
    void update_client_poller_interest(int fd);

    int listen_fd_{-1};
    bool running_{false};
    std::unique_ptr<IPoller> poller_;
    std::unordered_map<int, std::unique_ptr<Client>> clients_;
    std::vector<PollEvent> active_events_;

    MessageHandler message_handler_;
    ConnectionHandler disconnect_handler_;
};

}  // namespace server
}  // namespace redis_clone

#endif  // REDIS_CLONE_SERVER_EVENT_LOOP_H_
