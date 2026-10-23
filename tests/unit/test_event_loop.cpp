#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server/event_loop.h"
#include "util/socket_util.h"

using namespace redis_clone::server;
using namespace redis_clone::util;

class EventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        listen_fd_ = create_listening_socket("127.0.0.1", 0);
        ASSERT_GE(listen_fd_, 0);

        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        ASSERT_EQ(getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len), 0);
        server_port_ = ntohs(addr.sin_port);

        event_loop_ = std::make_unique<EventLoop>();
        ASSERT_TRUE(event_loop_->attach_listener(listen_fd_));
    }

    void TearDown() override { event_loop_.reset(); }

    int connect_test_client() {
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (client_fd < 0)
            return -1;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(server_port_));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(client_fd);
            return -1;
        }
        return client_fd;
    }

    int listen_fd_{-1};
    int server_port_{0};
    std::unique_ptr<EventLoop> event_loop_;
};

TEST_F(EventLoopTest, AcceptMultipleClientsAndEchoData) {
    // Configure echo handler
    event_loop_->set_message_handler([](Client& client) {
        const auto& buf = client.read_buffer();
        client.append_response(buf.data(), buf.size());
        client.consume_read_bytes(buf.size());
    });

    int client1 = connect_test_client();
    int client2 = connect_test_client();
    ASSERT_GE(client1, 0);
    ASSERT_GE(client2, 0);

    // Step loop to accept both connections
    event_loop_->step(50);
    EXPECT_EQ(event_loop_->active_clients_count(), 2u);

    // Send payload from client 1
    const char msg[] = "echo_test_payload";
    ASSERT_EQ(write(client1, msg, sizeof(msg)), static_cast<ssize_t>(sizeof(msg)));

    // Step loop to process read and write
    event_loop_->step(50);

    // Read echoed response on client 1
    char recv_buf[32] = {0};
    ssize_t bytes_read = read(client1, recv_buf, sizeof(recv_buf));
    ASSERT_EQ(bytes_read, static_cast<ssize_t>(sizeof(msg)));
    EXPECT_STREQ(recv_buf, "echo_test_payload");

    // Close client 2 and verify event loop detects clean disconnect
    close(client2);
    event_loop_->step(50);
    EXPECT_EQ(event_loop_->active_clients_count(), 1u);

    close(client1);
    event_loop_->step(50);
    EXPECT_EQ(event_loop_->active_clients_count(), 0u);
}

TEST_F(EventLoopTest, DisconnectsIdleClientsAfterTimeout) {
    // Configure 50ms idle timeout
    event_loop_->set_idle_timeout_ms(50);
    EXPECT_EQ(event_loop_->idle_timeout_ms(), 50u);

    int client1 = connect_test_client();
    int client2 = connect_test_client();
    ASSERT_GE(client1, 0);
    ASSERT_GE(client2, 0);

    event_loop_->step(20);
    EXPECT_EQ(event_loop_->active_clients_count(), 2u);

    // Sleep 60ms so clients become idle past the 50ms threshold
    usleep(60000);

    // Stepping loop should trigger idle reaper and disconnect both clients
    event_loop_->step(20);
    EXPECT_EQ(event_loop_->active_clients_count(), 0u);

    close(client1);
    close(client2);
}
