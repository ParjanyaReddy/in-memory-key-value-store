#ifndef REDIS_CLONE_PROTOCOL_COMMAND_H_
#define REDIS_CLONE_PROTOCOL_COMMAND_H_

#include <string>
#include <vector>

#include "store/database.h"
#include "store/hash_table.h"

namespace redis_clone {
namespace protocol {

enum class CommandType { kPing, kGet, kSet, kDel, kExpire, kPexpire, kTtl, kPttl, kUnknown };

struct Command {
    CommandType type{CommandType::kUnknown};
    std::string name;
    std::vector<std::string> args;
};

/**
 * Parses raw text payloads into structured commands and formats Redis-compatible responses.
 */
class CommandDispatcher {
public:
    /**
     * Parse a raw payload string into a Command struct.
     */
    static Command parse(const std::string& raw_payload);

    /**
     * Execute a command against a Database store (supporting TTL & rehashing).
     */
    static std::string execute(const Command& cmd, store::Database& db);

    /**
     * Overload to execute against a raw HashTable store (backward compatibility).
     */
    static std::string execute(const Command& cmd, store::HashTable& store);

    // Serialization response helpers
    static std::string format_simple_string(const std::string& str);
    static std::string format_bulk_string(const std::string& str);
    static std::string format_null_bulk_string();
    static std::string format_integer(int64_t val);
    static std::string format_error(const std::string& msg);
};

}  // namespace protocol
}  // namespace redis_clone

#endif  // REDIS_CLONE_PROTOCOL_COMMAND_H_
