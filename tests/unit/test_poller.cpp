#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server/poller.h"
#include "util/socket_util.h"

using namespace redis_clone::server;
using namespace redis_clone::util;

class PollerTest : public ::testing::TestWithParam<const char*> {
protected:
    void SetUp() override {
        int fds[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        fd_read_ = fds[0];
        fd_write_ = fds[1];
        ASSERT_TRUE(set_nonblocking(fd_read_));
        ASSERT_TRUE(set_nonblocking(fd_write_));

        std::string poller_type = GetParam();
        if (poller_type == "poll") {
            poller_ = IPoller::create_poll();
        } else {
            poller_ = IPoller::create_epoll();
        }
        ASSERT_NE(poller_, nullptr);
    }

    void TearDown() override {
        if (fd_read_ >= 0)
            close(fd_read_);
        if (fd_write_ >= 0)
            close(fd_write_);
    }

    int fd_read_{-1};
    int fd_write_{-1};
    std::unique_ptr<IPoller> poller_;
};

TEST_P(PollerTest, DetectsReadEventOnSocketPair) {
    ASSERT_TRUE(poller_->add(fd_read_, EVENT_READ));

    std::vector<PollEvent> events;
    // Before writing, poll should timeout with 0 events
    int ready = poller_->poll(events, 10);
    EXPECT_EQ(ready, 0);

    // Write bytes to fd_write_
    const char msg[] = "ping";
    ASSERT_EQ(write(fd_write_, msg, 4), 4);

    // Now fd_read_ must be readable
    ready = poller_->poll(events, 50);
    EXPECT_EQ(ready, 1);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].fd, fd_read_);
    EXPECT_TRUE(events[0].events & EVENT_READ);
}

TEST_P(PollerTest, ModifyAndRemoveEvents) {
    ASSERT_TRUE(poller_->add(fd_read_, EVENT_READ));

    // Modify interest to EVENT_WRITE
    ASSERT_TRUE(poller_->modify(fd_read_, EVENT_WRITE));

    std::vector<PollEvent> events;
    int ready = poller_->poll(events, 50);
    EXPECT_GE(ready, 1);

    // Remove fd from poller
    ASSERT_TRUE(poller_->remove(fd_read_));
    ready = poller_->poll(events, 10);
    EXPECT_EQ(ready, 0);
}

#if defined(__linux__)
INSTANTIATE_TEST_SUITE_P(PollAndEpoll, PollerTest, ::testing::Values("poll", "epoll"));
#else
INSTANTIATE_TEST_SUITE_P(PollOnly, PollerTest, ::testing::Values("poll"));
#endif
