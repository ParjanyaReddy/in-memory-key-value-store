#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server/client.h"
#include "util/socket_util.h"

using namespace redis_clone::server;
using namespace redis_clone::util;

class ClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        int fds[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        fd_client_ = fds[0];
        fd_peer_ = fds[1];
        ASSERT_TRUE(set_nonblocking(fd_client_));
        ASSERT_TRUE(set_nonblocking(fd_peer_));

        client_ = std::make_unique<Client>(fd_client_);
    }

    void TearDown() override {
        client_.reset();
        if (fd_peer_ >= 0)
            close(fd_peer_);
    }

    int fd_client_{-1};
    int fd_peer_{-1};
    std::unique_ptr<Client> client_;
};

TEST_F(ClientTest, ReadNonBlockingUntilEagain) {
    // Empty socket should return kOk without reading anything
    EXPECT_EQ(client_->read_from_socket(), ReadStatus::kOk);
    EXPECT_TRUE(client_->read_buffer().empty());

    // Write 5 bytes from peer
    const char msg[] = "hello";
    ASSERT_EQ(write(fd_peer_, msg, 5), 5);

    EXPECT_EQ(client_->read_from_socket(), ReadStatus::kOk);
    ASSERT_EQ(client_->read_buffer().size(), 5u);
    EXPECT_EQ(std::string(client_->read_buffer().begin(), client_->read_buffer().end()), "hello");

    // Consume 2 bytes
    client_->consume_read_bytes(2);
    ASSERT_EQ(client_->read_buffer().size(), 3u);
    EXPECT_EQ(std::string(client_->read_buffer().begin(), client_->read_buffer().end()), "llo");
}

TEST_F(ClientTest, WriteNonBlockingAndFlush) {
    client_->append_response("world");
    EXPECT_TRUE(client_->has_pending_write());
    EXPECT_EQ(client_->pending_write_bytes(), 5u);

    EXPECT_EQ(client_->write_to_socket(), WriteStatus::kComplete);
    EXPECT_FALSE(client_->has_pending_write());

    // Read back from peer
    char recv_buf[16] = {0};
    ssize_t bytes_read = read(fd_peer_, recv_buf, sizeof(recv_buf));
    ASSERT_EQ(bytes_read, 5);
    EXPECT_STREQ(recv_buf, "world");
}

TEST_F(ClientTest, DetectsCleanDisconnect) {
    // Close peer socket
    close(fd_peer_);
    fd_peer_ = -1;

    // Reading from closed socket returns kClosed (0 bytes EOF)
    EXPECT_EQ(client_->read_from_socket(), ReadStatus::kClosed);
}
