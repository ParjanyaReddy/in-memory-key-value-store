#include "util/socket_util.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace redis_clone {
namespace util {

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool set_reuseaddr(int fd) {
    int optval = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == 0;
}

bool set_tcp_nodelay(int fd) {
    int optval = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) == 0;
}

int create_listening_socket(const std::string& host, int port, int backlog) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    if (!set_reuseaddr(fd) || !set_nonblocking(fd)) {
        close(fd);
        return -1;
    }

    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            close(fd);
            return -1;
        }
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

}  // namespace util
}  // namespace redis_clone
