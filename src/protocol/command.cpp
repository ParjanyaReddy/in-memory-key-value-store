#include "protocol/command.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace redis_clone {
namespace protocol {

namespace {
std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;

    for (char c : input) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}
}  // namespace

Command CommandDispatcher::parse(const std::string& raw_payload) {
    Command cmd;
    std::vector<std::string> tokens = tokenize(raw_payload);
    if (tokens.empty()) {
        return cmd;
    }

    cmd.name = to_upper(tokens[0]);
    cmd.args.assign(tokens.begin() + 1, tokens.end());

    if (cmd.name == "PING") {
        cmd.type = CommandType::kPing;
    } else if (cmd.name == "GET") {
        cmd.type = CommandType::kGet;
    } else if (cmd.name == "SET") {
        cmd.type = CommandType::kSet;
    } else if (cmd.name == "DEL") {
        cmd.type = CommandType::kDel;
    } else {
        cmd.type = CommandType::kUnknown;
    }
    return cmd;
}

std::string CommandDispatcher::format_simple_string(const std::string& str) {
    return "+" + str + "\r\n";
}

std::string CommandDispatcher::format_bulk_string(const std::string& str) {
    return "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
}

std::string CommandDispatcher::format_null_bulk_string() {
    return "$-1\r\n";
}

std::string CommandDispatcher::format_integer(int64_t val) {
    return ":" + std::to_string(val) + "\r\n";
}

std::string CommandDispatcher::format_error(const std::string& msg) {
    return "-" + msg + "\r\n";
}

std::string CommandDispatcher::execute(const Command& cmd, store::HashTable& store) {
    switch (cmd.type) {
        case CommandType::kPing: {
            if (cmd.args.empty()) {
                return format_simple_string("PONG");
            }
            return format_bulk_string(cmd.args[0]);
        }
        case CommandType::kGet: {
            if (cmd.args.size() != 1) {
                return format_error("ERR wrong number of arguments for 'get' command");
            }
            std::string val;
            if (store.get(cmd.args[0], val)) {
                return format_bulk_string(val);
            }
            return format_null_bulk_string();
        }
        case CommandType::kSet: {
            if (cmd.args.size() != 2) {
                return format_error("ERR wrong number of arguments for 'set' command");
            }
            store.set(cmd.args[0], cmd.args[1]);
            return format_simple_string("OK");
        }
        case CommandType::kDel: {
            if (cmd.args.empty()) {
                return format_error("ERR wrong number of arguments for 'del' command");
            }
            int64_t deleted_count = 0;
            for (const auto& key : cmd.args) {
                if (store.del(key)) {
                    deleted_count++;
                }
            }
            return format_integer(deleted_count);
        }
        default:
            return format_error("ERR unknown command '" + cmd.name + "'");
    }
}

}  // namespace protocol
}  // namespace redis_clone
