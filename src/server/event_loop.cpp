#include "server/event_loop.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "util/socket_util.h"

namespace redis_clone {
namespace server {

EventLoop::EventLoop(std::unique_ptr<IPoller> poller)
    : poller_(poller ? std::move(poller) : IPoller::create_default()) {}

EventLoop::~EventLoop() {
    stop();
    clients_.clear();
    if (listen_fd_ >= 0) {
        if (poller_) {
            poller_->remove(listen_fd_);
        }
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

bool EventLoop::init(const std::string& host, int port) {
    listen_fd_ = util::create_listening_socket(host, port);
    if (listen_fd_ < 0) {
        return false;
    }
    return attach_listener(listen_fd_);
}

bool EventLoop::attach_listener(int listen_fd) {
    listen_fd_ = listen_fd;
    if (!poller_) {
        return false;
    }
    return poller_->add(listen_fd_, EVENT_READ);
}

Client* EventLoop::get_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return nullptr;
    }
    return it->second.get();
}

void EventLoop::handle_accept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // All pending connections accepted
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        util::set_nonblocking(client_fd);
        util::set_tcp_nodelay(client_fd);

        clients_[client_fd] = std::make_unique<Client>(client_fd);
        poller_->add(client_fd, EVENT_READ);
    }
}

void EventLoop::handle_client_read(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end())
        return;

    Client& client = *(it->second);
    ReadStatus status = client.read_from_socket();

    if (status == ReadStatus::kClosed || status == ReadStatus::kError) {
        disconnect_client(fd);
        return;
    }

    if (message_handler_ && !client.read_buffer().empty()) {
        message_handler_(client);
    }

    if (client.has_pending_write()) {
        WriteStatus wstatus = client.write_to_socket();
        if (wstatus == WriteStatus::kError) {
            disconnect_client(fd);
            return;
        }
    }

    update_client_poller_interest(fd);
}

void EventLoop::handle_client_write(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end())
        return;

    Client& client = *(it->second);
    WriteStatus status = client.write_to_socket();

    if (status == WriteStatus::kError) {
        disconnect_client(fd);
        return;
    }

    update_client_poller_interest(fd);
}

void EventLoop::update_client_poller_interest(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end())
        return;

    uint32_t events = EVENT_READ;
    if (it->second->has_pending_write()) {
        events |= EVENT_WRITE;
    }
    poller_->modify(fd, events);
}

void EventLoop::disconnect_client(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end())
        return;

    poller_->remove(fd);
    if (disconnect_handler_) {
        disconnect_handler_(fd);
    }
    clients_.erase(it);  // Unique_ptr cleans up Client and closes socket
}

void EventLoop::step(int timeout_ms) {
    int nfds = poller_->poll(active_events_, timeout_ms);
    if (nfds > 0) {
        for (const auto& event : active_events_) {
            if (event.fd == listen_fd_) {
                handle_accept();
            } else {
                if (event.events & (EVENT_READ | EVENT_ERROR)) {
                    handle_client_read(event.fd);
                }
                if (event.events & EVENT_WRITE) {
                    // Confirm client still exists in case read caused disconnect
                    if (clients_.find(event.fd) != clients_.end()) {
                        handle_client_write(event.fd);
                    }
                }
            }
        }
    }

    if (tick_callback_) {
        tick_callback_();
    }
}

void EventLoop::run() {
    running_ = true;
    while (running_) {
        step(100);
    }
}

}  // namespace server
}  // namespace redis_clone
