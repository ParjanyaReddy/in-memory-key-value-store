#include "server/poller.h"

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <sys/epoll.h>
#endif

namespace redis_clone {
namespace server {

#if defined(__linux__)
/**
 * EpollPoller implements IPoller using Linux epoll(7).
 * It scales efficiently to thousands of connections with O(1) amortized event notification.
 */
class EpollPoller : public IPoller {
public:
    EpollPoller() : epoll_fd_(epoll_create1(EPOLL_CLOEXEC)) {}

    ~EpollPoller() override {
        if (epoll_fd_ >= 0) {
            close(epoll_fd_);
        }
    }

    bool add(int fd, uint32_t events) override {
        if (epoll_fd_ < 0)
            return false;
        epoll_event ev = {};
        ev.data.fd = fd;
        ev.events = to_epoll_events(events);
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
    }

    bool modify(int fd, uint32_t events) override {
        if (epoll_fd_ < 0)
            return false;
        epoll_event ev = {};
        ev.data.fd = fd;
        ev.events = to_epoll_events(events);
        return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
    }

    bool remove(int fd) override {
        if (epoll_fd_ < 0)
            return false;
        return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
    }

    int poll(std::vector<PollEvent>& active_events, int timeout_ms) override {
        active_events.clear();
        if (epoll_fd_ < 0)
            return -1;

        constexpr int kMaxEvents = 1024;
        struct epoll_event events[kMaxEvents];

        int nfds = epoll_wait(epoll_fd_, events, kMaxEvents, timeout_ms);
        if (nfds < 0) {
            if (errno == EINTR)
                return 0;
            return -1;
        }

        active_events.reserve(static_cast<size_t>(nfds));
        for (int i = 0; i < nfds; ++i) {
            uint32_t flags = EVENT_NONE;
            if (events[i].events & EPOLLIN)
                flags |= EVENT_READ;
            if (events[i].events & EPOLLOUT)
                flags |= EVENT_WRITE;
            if (events[i].events & (EPOLLERR | EPOLLHUP))
                flags |= EVENT_ERROR;

            active_events.push_back({events[i].data.fd, flags});
        }
        return nfds;
    }

private:
    static uint32_t to_epoll_events(uint32_t events) {
        uint32_t epoll_ev = 0;
        if (events & EVENT_READ)
            epoll_ev |= EPOLLIN;
        if (events & EVENT_WRITE)
            epoll_ev |= EPOLLOUT;
        return epoll_ev;
    }

    int epoll_fd_{-1};
};
#endif

/**
 * PollPoller implements IPoller using POSIX poll(2).
 * Serves as a portable fallback for non-Linux platforms (e.g. macOS).
 */
class PollPoller : public IPoller {
public:
    PollPoller() = default;
    ~PollPoller() override = default;

    bool add(int fd, uint32_t events) override {
        if (fd_to_index_.find(fd) != fd_to_index_.end()) {
            return false;
        }
        pollfd pfd = {};
        pfd.fd = fd;
        pfd.events = to_poll_events(events);
        pfd.revents = 0;

        fd_to_index_[fd] = pollfds_.size();
        pollfds_.push_back(pfd);
        return true;
    }

    bool modify(int fd, uint32_t events) override {
        auto it = fd_to_index_.find(fd);
        if (it == fd_to_index_.end()) {
            return false;
        }
        pollfds_[it->second].events = to_poll_events(events);
        return true;
    }

    bool remove(int fd) override {
        auto it = fd_to_index_.find(fd);
        if (it == fd_to_index_.end()) {
            return false;
        }
        size_t index = it->second;
        size_t last_index = pollfds_.size() - 1;

        if (index != last_index) {
            pollfds_[index] = pollfds_[last_index];
            fd_to_index_[pollfds_[index].fd] = index;
        }
        pollfds_.pop_back();
        fd_to_index_.erase(it);
        return true;
    }

    int poll(std::vector<PollEvent>& active_events, int timeout_ms) override {
        active_events.clear();
        if (pollfds_.empty()) {
            if (timeout_ms > 0) {
                usleep(static_cast<useconds_t>(timeout_ms) * 1000);
            }
            return 0;
        }

        int ready = ::poll(pollfds_.data(), static_cast<nfds_t>(pollfds_.size()), timeout_ms);
        if (ready < 0) {
            if (errno == EINTR)
                return 0;
            return -1;
        }

        for (const auto& pfd : pollfds_) {
            if (pfd.revents == 0)
                continue;

            uint32_t flags = EVENT_NONE;
            if (pfd.revents & (POLLIN | POLLPRI))
                flags |= EVENT_READ;
            if (pfd.revents & POLLOUT)
                flags |= EVENT_WRITE;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                flags |= EVENT_ERROR;

            active_events.push_back({pfd.fd, flags});
        }
        return static_cast<int>(active_events.size());
    }

private:
    static short to_poll_events(uint32_t events) {
        short poll_ev = 0;
        if (events & EVENT_READ)
            poll_ev |= POLLIN;
        if (events & EVENT_WRITE)
            poll_ev |= POLLOUT;
        return poll_ev;
    }

    std::vector<struct pollfd> pollfds_;
    std::unordered_map<int, size_t> fd_to_index_;
};

std::unique_ptr<IPoller> IPoller::create_default() {
#if defined(__linux__)
    return create_epoll();
#else
    return create_poll();
#endif
}

std::unique_ptr<IPoller> IPoller::create_epoll() {
#if defined(__linux__)
    return std::make_unique<EpollPoller>();
#else
    return nullptr;
#endif
}

std::unique_ptr<IPoller> IPoller::create_poll() {
    return std::make_unique<PollPoller>();
}

}  // namespace server
}  // namespace redis_clone
