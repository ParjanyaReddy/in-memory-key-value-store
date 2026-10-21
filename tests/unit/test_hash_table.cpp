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

    // Insert 13th item -> triggers incremental rehash to 32
    table.set("key_12", "val_12");
    EXPECT_EQ(table.capacity(), 32u);
    EXPECT_EQ(table.size(), 13u);

    // Verify all 13 items exist during and after rehash
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

TEST(HashTableTest, IncrementalRehashStepByStepProgress) {
    HashTable table(16);
    table.set_auto_resize(false);  // Manual rehash trigger

    for (int i = 0; i < 16; ++i) {
        table.set("key_" + std::to_string(i), "val_" + std::to_string(i));
    }
    EXPECT_FALSE(table.is_rehashing());
    EXPECT_EQ(table.capacity(), 16u);

    // Initiate incremental rehash to 32
    ASSERT_TRUE(table.start_incremental_rehash(32));
    EXPECT_TRUE(table.is_rehashing());
    EXPECT_EQ(table.table_old_capacity(), 16u);
    EXPECT_EQ(table.table_new_capacity(), 32u);
    EXPECT_EQ(table.rehash_index(), 0u);

    // Step through migration 1 bucket at a time
    size_t prev_index = 0;
    while (table.is_rehashing()) {
        size_t cur_index = table.rehash_index();
        EXPECT_GE(cur_index, prev_index);
        prev_index = cur_index;

        // Verify all 16 items remain accessible at every step
        for (int i = 0; i < 16; ++i) {
            std::string val;
            ASSERT_TRUE(table.get("key_" + std::to_string(i), val));
            EXPECT_EQ(val, "val_" + std::to_string(i));
        }

        table.rehash_step(1, 10);
    }

    EXPECT_FALSE(table.is_rehashing());
    EXPECT_EQ(table.capacity(), 32u);
    EXPECT_EQ(table.size(), 16u);
}

TEST(HashTableTest, IncrementalRehashEmptyBucketVisitBounding) {
    HashTable table(64);
    table.set_auto_resize(false);

    // Insert only 2 keys into a 64-capacity table to create sparse buckets
    table.set("sparse_k1", "v1");
    table.set("sparse_k2", "v2");

    ASSERT_TRUE(table.start_incremental_rehash(128));
    EXPECT_TRUE(table.is_rehashing());

    // With max_empty = 4, a single step should inspect at most 4 empty buckets
    size_t start_idx = table.rehash_index();
    table.rehash_step(1, 4);
    size_t end_idx = table.rehash_index();

    // Verify progress was strictly bounded by empty bucket limit + 1
    EXPECT_LE(end_idx - start_idx, 5u);
    EXPECT_TRUE(table.is_rehashing());

    // Step until complete
    while (table.is_rehashing()) {
        table.rehash_step(8, 80);
    }
    EXPECT_FALSE(table.is_rehashing());
    EXPECT_EQ(table.capacity(), 128u);
    EXPECT_EQ(table.size(), 2u);
}

TEST(HashTableTest, MidMigrationMutations) {
    HashTable table(16);
    table.set_auto_resize(false);

    for (int i = 0; i < 16; ++i) {
        table.set("k_" + std::to_string(i), "v_" + std::to_string(i));
    }

    ASSERT_TRUE(table.start_incremental_rehash(32));
    EXPECT_TRUE(table.is_rehashing());

    // Step 2 buckets
    table.rehash_step(2, 10);

    // 1. Update existing key: should move to table_new_
    EXPECT_FALSE(table.set("k_15", "updated_v_15"));
    std::string val;
    EXPECT_TRUE(table.get("k_15", val));
    EXPECT_EQ(val, "updated_v_15");

    // 2. Insert brand new key: goes into table_new_
    EXPECT_TRUE(table.set("brand_new_key", "brand_new_val"));
    EXPECT_TRUE(table.get("brand_new_key", val));
    EXPECT_EQ(val, "brand_new_val");

    // 3. Delete key from table
    EXPECT_TRUE(table.del("k_14"));
    EXPECT_FALSE(table.get("k_14", val));

    // Finish rehash
    while (table.is_rehashing()) {
        table.rehash_step(4, 40);
    }
    EXPECT_FALSE(table.is_rehashing());

    // Verify final state
    EXPECT_EQ(table.size(), 16u);  // 16 original - 1 deleted + 1 new = 16
    EXPECT_TRUE(table.get("brand_new_key", val));
    EXPECT_EQ(val, "brand_new_val");
    EXPECT_TRUE(table.get("k_15", val));
    EXPECT_EQ(val, "updated_v_15");
    EXPECT_FALSE(table.get("k_14", val));
}

TEST(HashTableTest, IncrementalVsStopTheWorldEquivalence) {
    HashTable table_inc(16);
    table_inc.set_incremental_rehash(true);

    HashTable table_stw(16);
    table_stw.set_incremental_rehash(false);

    constexpr int kCount = 500;
    for (int i = 0; i < kCount; ++i) {
        std::string k = "key_" + std::to_string(i);
        std::string v = "val_" + std::to_string(i);
        table_inc.set(k, v);
        table_stw.set(k, v);
    }

    // Advance any remaining incremental rehash steps
    while (table_inc.is_rehashing()) {
        table_inc.rehash_step(8, 80);
    }

    EXPECT_EQ(table_inc.size(), table_stw.size());
    EXPECT_EQ(table_inc.capacity(), table_stw.capacity());

    for (int i = 0; i < kCount; ++i) {
        std::string k = "key_" + std::to_string(i);
        std::string v_inc;
        std::string v_stw;
        ASSERT_TRUE(table_inc.get(k, v_inc));
        ASSERT_TRUE(table_stw.get(k, v_stw));
        ASSERT_EQ(v_inc, v_stw);
    }
}

TEST(HashTableTest, RandomizedOracleDifferentialTest) {
    HashTable table(16);
    table.set_migration_batch_size(1);  // Migrate 1 bucket per op to maximize rehash interleaving
    table.set_max_empty_buckets_to_visit(5);

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
