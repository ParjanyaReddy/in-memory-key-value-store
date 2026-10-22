#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "protocol/command.h"
#include "store/database.h"
#include "store/ttl_heap.h"

using namespace redis_clone::store;
using namespace redis_clone::protocol;

TEST(TtlHeapTest, MinHeapOrdering) {
    TtlHeap heap;
    EXPECT_TRUE(heap.empty());
    EXPECT_EQ(heap.size(), 0u);

    heap.push("k500", 500, 1);
    heap.push("k100", 100, 2);
    heap.push("k300", 300, 3);
    heap.push("k50", 50, 4);
    heap.push("k1000", 1000, 5);

    EXPECT_EQ(heap.size(), 5u);

    TtlEntry peek;
    ASSERT_TRUE(heap.peek_min(peek));
    EXPECT_EQ(peek.key, "k50");
    EXPECT_EQ(peek.expire_at_ms, 50u);

    std::vector<uint64_t> popped_order;
    TtlEntry entry;
    while (heap.pop_min(entry)) {
        popped_order.push_back(entry.expire_at_ms);
    }

    std::vector<uint64_t> expected = {50, 100, 300, 500, 1000};
    EXPECT_EQ(popped_order, expected);
    EXPECT_TRUE(heap.empty());
    EXPECT_FALSE(heap.pop_min(entry));
}

TEST(DatabaseTest, LazyExpiryOnGet) {
    Database db;
    uint64_t mock_time = 1000;
    db.set_clock([&mock_time]() { return mock_time; });

    // Insert key expiring at 1500 ms
    db.set("temp_key", "temp_val", 1500);

    std::string val;
    // Before expiration
    mock_time = 1200;
    EXPECT_TRUE(db.get("temp_key", val));
    EXPECT_EQ(val, "temp_val");

    // At expiration boundary
    mock_time = 1500;
    EXPECT_FALSE(db.get("temp_key", val));  // Lazily evicted!

    // Verify key was removed from the internal hash table
    EXPECT_EQ(db.raw_size(), 0u);
}

TEST(DatabaseTest, ActiveExpireStepBounded) {
    Database db;
    uint64_t mock_time = 1000;
    db.set_clock([&mock_time]() { return mock_time; });

    // Populate 250 keys all expiring at 1200 ms
    for (int i = 0; i < 250; ++i) {
        db.set("k_" + std::to_string(i), "v_" + std::to_string(i), 1200);
    }
    EXPECT_EQ(db.raw_size(), 250u);
    EXPECT_EQ(db.heap_size(), 250u);

    // Advance clock past expiration
    mock_time = 1300;

    // Step 1: evict at most 100 keys
    size_t evicted1 = db.active_expire_step(100);
    EXPECT_EQ(evicted1, 100u);
    EXPECT_EQ(db.raw_size(), 150u);

    // Step 2: evict next 100 keys
    size_t evicted2 = db.active_expire_step(100);
    EXPECT_EQ(evicted2, 100u);
    EXPECT_EQ(db.raw_size(), 50u);

    // Step 3: evict remaining 50 keys
    size_t evicted3 = db.active_expire_step(100);
    EXPECT_EQ(evicted3, 50u);
    EXPECT_EQ(db.raw_size(), 0u);

    // Step 4: nothing left to evict
    size_t evicted4 = db.active_expire_step(100);
    EXPECT_EQ(evicted4, 0u);
}

TEST(DatabaseTest, StaleResolutionOnOverwriteWithoutTtl) {
    Database db;
    uint64_t mock_time = 1000;
    db.set_clock([&mock_time]() { return mock_time; });

    // 1. Set key with TTL at 1200
    db.set("persist_key", "initial_val", 1200);
    EXPECT_EQ(db.heap_size(), 1u);

    // 2. Overwrite key without TTL -> increments version and sets expire_at_ms = 0
    db.set("persist_key", "overwritten_val", 0);
    EXPECT_EQ(db.ttl("persist_key"), -1);

    // 3. Advance past initial TTL
    mock_time = 1300;
    db.active_expire_step(100);

    // Key must NOT be evicted because the heap entry had stale version 1
    std::string val;
    EXPECT_TRUE(db.get("persist_key", val));
    EXPECT_EQ(val, "overwritten_val");
}

TEST(DatabaseTest, StaleResolutionOnOverwriteWithNewTtl) {
    Database db;
    uint64_t mock_time = 1000;
    db.set_clock([&mock_time]() { return mock_time; });

    // 1. Initial short TTL (1200)
    db.set("k", "v1", 1200);

    // 2. Overwrite with longer TTL (2000)
    db.set("k", "v2", 2000);
    EXPECT_EQ(db.heap_size(), 2u);  // Contains version 1 and version 2

    // 3. Advance to 1300 (past v1 expiry, before v2 expiry)
    mock_time = 1300;
    size_t evicted = db.active_expire_step(100);
    EXPECT_EQ(evicted, 0u);  // v1 popped and ignored as stale; v2 remains in heap

    std::string val;
    EXPECT_TRUE(db.get("k", val));
    EXPECT_EQ(val, "v2");

    // 4. Advance past v2 expiry
    mock_time = 2100;
    evicted = db.active_expire_step(100);
    EXPECT_EQ(evicted, 1u);  // v2 expired!

    EXPECT_FALSE(db.get("k", val));
}

TEST(DatabaseTest, StaleResolutionOnExplicitDel) {
    Database db;
    uint64_t mock_time = 1000;
    db.set_clock([&mock_time]() { return mock_time; });

    db.set("doomed", "val", 1200);
    EXPECT_TRUE(db.del("doomed"));

    mock_time = 1500;
    size_t evicted = db.active_expire_step(100);
    EXPECT_EQ(evicted, 0u);  // Stale entry safely discarded
    EXPECT_FALSE(db.contains("doomed"));
}

TEST(DatabaseTest, ExpireAndTtlCommands) {
    Database db;
    uint64_t mock_time = 1000;
    db.set_clock([&mock_time]() { return mock_time; });

    db.set("session", "token123");
    EXPECT_EQ(db.ttl("session"), -1);
    EXPECT_EQ(db.pttl("session"), -1);

    // Set 10 second expiration
    EXPECT_TRUE(db.expire("session", 10));
    EXPECT_EQ(db.ttl("session"), 10);
    EXPECT_EQ(db.pttl("session"), 10000);

    // Advance 3.5 seconds
    mock_time += 3500;
    EXPECT_EQ(db.ttl("session"), 7);
    EXPECT_EQ(db.pttl("session"), 6500);

    // Advance 6.6 seconds (total 10.1s)
    mock_time += 6600;
    EXPECT_EQ(db.ttl("session"), -2);
    EXPECT_EQ(db.pttl("session"), -2);
    EXPECT_FALSE(db.contains("session"));
}

TEST(CommandDispatcherTest, ExecutesTtlCommands) {
    Database db;
    uint64_t mock_time = 1000;
    db.set_clock([&mock_time]() { return mock_time; });

    // SET with EX option
    Command set_ex = CommandDispatcher::parse("SET session_key token_abc EX 5");
    EXPECT_EQ(CommandDispatcher::execute(set_ex, db), "+OK\r\n");
    EXPECT_EQ(db.ttl("session_key"), 5);

    // TTL command
    Command ttl_cmd = CommandDispatcher::parse("TTL session_key");
    EXPECT_EQ(CommandDispatcher::execute(ttl_cmd, db), ":5\r\n");

    // Advance 2 seconds
    mock_time += 2000;
    EXPECT_EQ(CommandDispatcher::execute(ttl_cmd, db), ":3\r\n");

    // EXPIRE update to 10 seconds
    Command exp_cmd = CommandDispatcher::parse("EXPIRE session_key 10");
    EXPECT_EQ(CommandDispatcher::execute(exp_cmd, db), ":1\r\n");
    EXPECT_EQ(CommandDispatcher::execute(ttl_cmd, db), ":10\r\n");

    // Advance 11 seconds -> expired
    mock_time += 11000;
    EXPECT_EQ(CommandDispatcher::execute(ttl_cmd, db), ":-2\r\n");
    Command get_cmd = CommandDispatcher::parse("GET session_key");
    EXPECT_EQ(CommandDispatcher::execute(get_cmd, db), "$-1\r\n");
}
