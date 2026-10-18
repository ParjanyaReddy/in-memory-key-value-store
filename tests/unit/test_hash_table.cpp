#include <gtest/gtest.h>

#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "store/hash_table.h"

using namespace redis_clone::store;

TEST(HashTableTest, BasicCrudOperations) {
    HashTable table(16);
    EXPECT_EQ(table.size(), 0u);
    EXPECT_EQ(table.capacity(), 16u);

    // Set new key
    EXPECT_TRUE(table.set("key1", "val1"));
    EXPECT_EQ(table.size(), 1u);

    // Get key
    std::string value;
    EXPECT_TRUE(table.get("key1", value));
    EXPECT_EQ(value, "val1");

    // Overwrite key
    EXPECT_FALSE(table.set("key1", "updated_val"));
    EXPECT_EQ(table.size(), 1u);
    EXPECT_TRUE(table.get("key1", value));
    EXPECT_EQ(value, "updated_val");

    // Delete key
    EXPECT_TRUE(table.del("key1"));
    EXPECT_EQ(table.size(), 0u);
    EXPECT_FALSE(table.get("key1", value));

    // Delete non-existent key
    EXPECT_FALSE(table.del("key1"));
}

TEST(HashTableTest, AutoResizeTriggersAtLoadFactorThreshold) {
    HashTable table(16, 0.75);
    EXPECT_EQ(table.capacity(), 16u);

    // Insert 12 items -> load factor 12/16 = 0.75
    for (int i = 0; i < 12; ++i) {
        table.set("key_" + std::to_string(i), "val_" + std::to_string(i));
    }
    EXPECT_EQ(table.capacity(), 16u);

    // Insert 13th item -> triggers resize to 32
    table.set("key_12", "val_12");
    EXPECT_EQ(table.capacity(), 32u);
    EXPECT_EQ(table.size(), 13u);

    // Verify all 13 items exist after resize
    for (int i = 0; i < 13; ++i) {
        std::string val;
        ASSERT_TRUE(table.get("key_" + std::to_string(i), val));
        EXPECT_EQ(val, "val_" + std::to_string(i));
    }
}

TEST(HashTableTest, ClearDeallocatesAllNodes) {
    HashTable table(16);
    for (int i = 0; i < 100; ++i) {
        table.set("k" + std::to_string(i), "v" + std::to_string(i));
    }
    EXPECT_EQ(table.size(), 100u);

    table.clear();
    EXPECT_EQ(table.size(), 0u);

    std::string val;
    EXPECT_FALSE(table.get("k0", val));
}

TEST(HashTableTest, RandomizedOracleDifferentialTest) {
    HashTable table(16);
    std::unordered_map<std::string, std::string> oracle;

    std::mt19937_64 rng(42);                           // Fixed seed for deterministic testing
    std::uniform_int_distribution<int> op_dist(0, 2);  // 0: SET, 1: GET, 2: DEL
    std::uniform_int_distribution<int> key_dist(0, 1000);

    constexpr int kNumOperations = 100000;

    for (int i = 0; i < kNumOperations; ++i) {
        int op = op_dist(rng);
        std::string key = "key_" + std::to_string(key_dist(rng));

        if (op == 0) {  // SET
            std::string val = "val_" + std::to_string(i);
            bool table_new = table.set(key, val);
            bool oracle_new = (oracle.find(key) == oracle.end());
            oracle[key] = val;

            ASSERT_EQ(table_new, oracle_new);
        } else if (op == 1) {  // GET
            std::string table_val;
            bool table_found = table.get(key, table_val);
            auto it = oracle.find(key);
            bool oracle_found = (it != oracle.end());

            ASSERT_EQ(table_found, oracle_found);
            if (oracle_found) {
                ASSERT_EQ(table_val, it->second);
            }
        } else {  // DEL
            bool table_del = table.del(key);
            auto it = oracle.find(key);
            bool oracle_del = (it != oracle.end());
            if (oracle_del) {
                oracle.erase(it);
            }

            ASSERT_EQ(table_del, oracle_del);
        }

        ASSERT_EQ(table.size(), oracle.size());
    }

    // Final integrity sweep: verify every remaining key in oracle matches table
    for (const auto& [k, v] : oracle) {
        std::string val;
        ASSERT_TRUE(table.get(k, val));
        ASSERT_EQ(val, v);
    }
}
