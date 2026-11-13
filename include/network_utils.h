#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace network {
constexpr size_t MAX_LINE = 2048;
constexpr int64_t MAX_PRICE = 9000000000000000LL;
constexpr int64_t MIN_INCREMENT = 10000;
bool set_nonblocking(int fd);
bool send_all(int fd, const void *data, size_t length);
std::vector<std::string> split(const std::string& line, char sep);
bool valid_text(const std::string& s, size_t limit, bool allow_spaces = true);
bool valid_username(const std::string& s);
bool valid_password(const std::string& s);
bool parse_positive(const std::string& s, int64_t& out, int64_t max_value = MAX_PRICE);
}
