#ifndef REDIS_CLONE_UTIL_SOCKET_UTIL_H_
#define REDIS_CLONE_UTIL_SOCKET_UTIL_H_

#include <string>

namespace redis_clone {
namespace util {

/**
 * Configure socket file descriptor to non-blocking mode (O_NONBLOCK).
 * Returns true on success, false on failure.
 */
bool set_nonblocking(int fd);

/**
 * Enable SO_REUSEADDR on socket to allow immediate rebinding after restart.
 * Returns true on success, false on failure.
 */
bool set_reuseaddr(int fd);

/**
 * Enable TCP_NODELAY to disable Nagle's algorithm for low latency.
 * Returns true on success, false on failure.
 */
bool set_tcp_nodelay(int fd);

/**
 * Create, configure (non-blocking, reuseaddr, nodelay), bind, and listen
 * on a TCP socket for the specified host and port.
 * Returns the socket file descriptor on success, or -1 on failure.
 */
int create_listening_socket(const std::string& host, int port, int backlog = 1024);

}  // namespace util
}  // namespace redis_clone

#endif  // REDIS_CLONE_UTIL_SOCKET_UTIL_H_
