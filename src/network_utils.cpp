#include "network_utils.h"
#include <cerrno>
#include <cctype>
#include <fcntl.h>
#include <sys/socket.h>

namespace network {
bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

// Used by the small terminal client. The server uses nonblocking output queues.
bool send_all(int fd, const void* data, size_t length) {
  const char* p = static_cast<const char*>(data);
  size_t sent = 0;
  while (sent < length) {
    ssize_t n = send(fd, p + sent, length - sent, MSG_NOSIGNAL);
    if (n > 0) { sent += static_cast<size_t>(n); continue; }
    if (n < 0 && errno == EINTR) continue;
    return false;
  }
  return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == sep) { out.push_back(s.substr(start, i - start)); start = i + 1; }
  }
  out.push_back(s.substr(start));
  return out;
}

// Reject protocol separators, ASCII controls and invalid UTF-8 sequences.
bool valid_text(const std::string& s, size_t limit, bool allow_spaces) {
  if (s.empty() || s.size() > limit) return false;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      if (c < 0x20 || c == 0x7f || c == '|' || (!allow_spaces && c == ' ')) return false;
      ++i;
      continue;
    }
    size_t n = 0;
    if (c >= 0xc2 && c <= 0xdf) n = 2;
    else if (c >= 0xe0 && c <= 0xef) n = 3;
    else if (c >= 0xf0 && c <= 0xf4) n = 4;
    else return false;
    if (n > s.size() - i) return false;
    for (size_t j = 1; j < n; ++j)
      if ((static_cast<unsigned char>(s[i+j]) & 0xc0) != 0x80) return false;
    unsigned char b1 = static_cast<unsigned char>(s[i+1]);
    if (c == 0xe0 && b1 < 0xa0) return false;
    if (c == 0xed && b1 >= 0xa0) return false;
    if (c == 0xf0 && b1 < 0x90) return false;
    if (c == 0xf4 && b1 > 0x8f) return false;
    i += n;
  }
  return true;
}

bool valid_username(const std::string& s) {
  if (s.size() < 3 || s.size() > 24) return false;
  for (unsigned char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

bool valid_password(const std::string& s) {
  if (s.size() < 8 || s.size() > 72) return false;
  for (unsigned char c : s) if (c < 0x21 || c > 0x7e || c == '|') return false;
  return true;
}

bool parse_positive(const std::string& s, int64_t& out, int64_t max_value) {
  if (s.empty() || s.size() > 19 || max_value < 1) return false;
  int64_t result = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    int digit = c - '0';
    if (digit > max_value || result > (max_value - digit) / 10) return false;
    result = result * 10 + digit;
  }
  if (result < 1) return false;
  out = result;
  return true;
}
}
