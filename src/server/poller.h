#ifndef REDIS_CLONE_SERVER_POLLER_H_
#define REDIS_CLONE_SERVER_POLLER_H_

#include <cstdint>
#include <memory>
#include <vector>

namespace redis_clone {
namespace server {

// Poller event bitmask flags
enum EventFlags : uint32_t {
    EVENT_NONE = 0,
    EVENT_READ = 1 << 0,
    EVENT_WRITE = 1 << 1,
    EVENT_ERROR = 1 << 2
};

struct PollEvent {
    int fd;
    uint32_t events;
};

/**
 * CORE LOGIC: understand this file line by line.
 * Abstract interface for I/O multiplexing pollers (epoll on Linux, poll fallback).
 */
class IPoller {
public:
    virtual ~IPoller() = default;

    /**
     * Register interest in events for a file descriptor.
     */
    virtual bool add(int fd, uint32_t events) = 0;

    /**
     * Modify the registered event interest for an existing file descriptor.
     */
    virtual bool modify(int fd, uint32_t events) = 0;

    /**
     * Remove a file descriptor from the poller interest list.
     */
    virtual bool remove(int fd) = 0;

    /**
     * Wait for I/O events with a timeout in milliseconds (-1 for infinite).
     * Populates active_events and returns the number of ready descriptors.
     */
    virtual int poll(std::vector<PollEvent>& active_events, int timeout_ms) = 0;

    /**
     * Create the best available poller for the current host operating system.
     */
    static std::unique_ptr<IPoller> create_default();

    /**
     * Create an explicit epoll poller (Linux-only).
     */
    static std::unique_ptr<IPoller> create_epoll();

    /**
     * Create an explicit poll(2) fallback poller.
     */
    static std::unique_ptr<IPoller> create_poll();
};

}  // namespace server
}  // namespace redis_clone

#endif  // REDIS_CLONE_SERVER_POLLER_H_
