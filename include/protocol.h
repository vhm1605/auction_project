#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <utility>

namespace protocol {
// Application-layer status codes; request identifiers are uppercase keywords.
// All wire lines terminate with CRLF. Space separates fields; quote text as needed.
inline constexpr char CRLF[] = "\r\n";
enum class Code : int {
  CONNECTED = 100,
  ITEM_STARTED = 110,
  MEMBER_EVENT = 111,
  ITEM_EVENT = 113,
  NEW_BID = 120,
  CLOSING_NOTICE = 121,
  ITEM_SOLD = 130,
  QUEUE_EMPTY = 131,
  OK = 200,
  ROOM_DATA = 201,
  ITEM_DATA = 202,
  INVALID_REQUEST = 400,
  AUTH_ERROR = 401,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  INVALID_STATE = 409,
  INVALID_DATA = 422,
  LIMIT_REACHED = 429,
  STORAGE_FAILED = 501,
};

/** @brief Validate request keyword and number of fields (including keyword). */
inline bool valid_request(const std::string& name, std::size_t count) {
  if (name == "HELP") return count == 1;
  if (name == "EXIT") return count == 1;
  if (name == "REGISTER") return count == 3;
  if (name == "LOGIN") return count == 3;
  if (name == "LOGOUT") return count == 1;
  if (name == "CREATE_ROOM") return count == 2;
  if (name == "LIST_ROOMS") return count == 1;
  if (name == "JOIN_ROOM") return count == 2;
  if (name == "LEAVE_ROOM") return count == 1;
  if (name == "ADD_ITEM") return count == 5;
  if (name == "WITHDRAW_ITEM") return count == 2;
  if (name == "LIST_ITEMS") return count == 1;
  if (name == "START") return count == 1;
  if (name == "BID") return count == 2;
  if (name == "BUY_NOW") return count == 1;
  return false;
}

/** @brief Return the stable symbolic name associated with a status code. */
inline const char* message_name(Code code) {
  switch (code) {
    case Code::CONNECTED: return "CONNECTED";
    case Code::ITEM_STARTED: return "ITEM_STARTED";
    case Code::MEMBER_EVENT: return "MEMBER_EVENT";
    case Code::ITEM_EVENT: return "ITEM_EVENT";
    case Code::NEW_BID: return "NEW_BID";
    case Code::CLOSING_NOTICE: return "CLOSING_NOTICE";
    case Code::ITEM_SOLD: return "ITEM_SOLD";
    case Code::QUEUE_EMPTY: return "QUEUE_EMPTY";
    case Code::OK: return "OK";
    case Code::ROOM_DATA: return "ROOM_DATA";
    case Code::ITEM_DATA: return "ITEM_DATA";
    case Code::INVALID_REQUEST: return "INVALID_REQUEST";
    case Code::AUTH_ERROR: return "AUTH_ERROR";
    case Code::FORBIDDEN: return "FORBIDDEN";
    case Code::NOT_FOUND: return "NOT_FOUND";
    case Code::INVALID_STATE: return "INVALID_STATE";
    case Code::INVALID_DATA: return "INVALID_DATA";
    case Code::LIMIT_REACHED: return "LIMIT_REACHED";
    case Code::STORAGE_FAILED: return "STORAGE_FAILED";
  }
  return "UNKNOWN_STATUS";
}

/** @brief Encode a single wire field. Spaces, quotes and backslashes are escaped. */
inline std::string encode_field(const std::string& field) {
  if (!field.empty() && field.find_first_of(" \"\\") == std::string::npos) return field;
  std::string out = "\"";
  for (char ch : field) {
    if (ch == '"' || ch == '\\') out += '\\';
    out += ch;
  }
  return out + '"';
}

/** @brief Parse space-separated tokens with optional quoted fields. */
inline bool parse(const std::string& line, std::vector<std::string>& fields) {
  fields.clear();
  std::size_t i = 0;
  while (i < line.size()) {
    if (line[i] == ' ') return false;
    std::string field;
    if (line[i] == '"') {
      ++i;
      bool closed = false;
      while (i < line.size()) {
        char ch = line[i++];
        if (ch == '"') { closed = true; break; }
        if (ch == '\\') {
          if (i == line.size() || (line[i] != '\\' && line[i] != '"')) return false;
          ch = line[i++];
        }
        field += ch;
      }
      if (!closed) return false;
    } else {
      while (i < line.size() && line[i] != ' ') {
        if (line[i] == '"' || line[i] == '\\') return false;
        field += line[i++];
      }
    }
    if (field.find('|') != std::string::npos) return false;
    fields.push_back(std::move(field));
    if (i == line.size()) break;
    if (line[i++] != ' ' || i == line.size()) return false;
  }
  return !fields.empty();
}

/** @brief Format response with fixed code and message name; existing internal payload fields are pipe-separated. */
inline std::string format(Code code, const std::string& fields = {}) {
  std::string line = std::to_string(static_cast<int>(code)) + " " + message_name(code);
  if (fields.empty()) return line;
  std::size_t start = 0;
  for (;;) {
    const auto end = fields.find('|', start);
    line += ' ';
    line += encode_field(fields.substr(start, end == std::string::npos ? end : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return line;
}
} // namespace protocol
