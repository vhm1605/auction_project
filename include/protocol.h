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
  LOGGED_IN = 210,
  REGISTERED = 211,
  LOGGED_OUT = 230,
  BYE = 231,
  ROOM_CREATED = 240,
  JOINED_ROOM = 241,
  LEFT_ROOM = 242,
  ROOMS = 243,
  ROOM = 244,
  ITEM_QUEUED = 250,
  ITEM_WITHDRAWN = 251,
  ITEMS = 252,
  ITEM = 253,
  AUCTION_STARTED = 260,
  BID_ACCEPTED = 261,
  BOUGHT = 262,
  COMMANDS = 290,
  INVALID_CREDENTIALS = 410,
  USERNAME_EXISTS = 411,
  ALREADY_LOGGED_IN = 413,
  USER_ALREADY_ONLINE = 414,
  LOGOUT_FIRST = 415,
  USER_LIMIT = 417,
  LOGIN_REQUIRED = 421,
  ONLY_OWNER_CAN_SELL = 422,
  ONLY_OWNER_CAN_WITHDRAW = 423,
  ONLY_OWNER_CAN_START = 424,
  SELLER_CANNOT_BID = 425,
  SELLER_CANNOT_BUY = 426,
  ROOM_NOT_FOUND = 430,
  ALREADY_IN_A_ROOM = 431,
  NOT_IN_A_ROOM = 432,
  JOIN_ROOM_FIRST = 433,
  ROOM_LIMIT = 434,
  ROOM_FULL = 435,
  ITEM_NOT_FOUND = 440,
  NO_ACTIVE_ITEM = 441,
  BID_INVALID = 442,
  ITEM_LIMIT = 443,
  ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN = 444,
  AUCTION_ALREADY_ACTIVE = 445,
  NO_QUEUED_ITEM = 446,
  UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS = 450,
  BAD_CREDENTIAL_FORMAT = 451,
  INVALID_OR_TOO_LONG_LINE = 452,
  INVALID_LINE_ENDING = 453,
  INVALID_ROOM_NAME = 455,
  INVALID_ROOM_ID = 456,
  INVALID_ITEM_TEXT = 457,
  INVALID_PRICE = 458,
  INVALID_ITEM_ID = 459,
  EVENT_ITEM_STARTED = 110,
  EVENT_MEMBER_JOINED = 111,
  EVENT_MEMBER_LEFT = 112,
  EVENT_ITEM_QUEUED = 113,
  EVENT_ITEM_WITHDRAWN = 114,
  EVENT_NEW_BID = 120,
  EVENT_NOTICE = 121,
  EVENT_SOLD = 130,
  EVENT_QUEUE_EMPTY = 131,
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
    case Code::LOGGED_IN: return "LOGGED_IN";
    case Code::REGISTERED: return "REGISTERED";
    case Code::LOGGED_OUT: return "LOGGED_OUT";
    case Code::BYE: return "BYE";
    case Code::ROOM_CREATED: return "ROOM_CREATED";
    case Code::JOINED_ROOM: return "JOINED_ROOM";
    case Code::LEFT_ROOM: return "LEFT_ROOM";
    case Code::ROOMS: return "ROOMS";
    case Code::ROOM: return "ROOM";
    case Code::ITEM_QUEUED: return "ITEM_QUEUED";
    case Code::ITEM_WITHDRAWN: return "ITEM_WITHDRAWN";
    case Code::ITEMS: return "ITEMS";
    case Code::ITEM: return "ITEM";
    case Code::AUCTION_STARTED: return "AUCTION_STARTED";
    case Code::BID_ACCEPTED: return "BID_ACCEPTED";
    case Code::BOUGHT: return "BOUGHT";
    case Code::COMMANDS: return "COMMANDS";
    case Code::INVALID_CREDENTIALS: return "INVALID_CREDENTIALS";
    case Code::USERNAME_EXISTS: return "USERNAME_EXISTS";
    case Code::ALREADY_LOGGED_IN: return "ALREADY_LOGGED_IN";
    case Code::USER_ALREADY_ONLINE: return "USER_ALREADY_ONLINE";
    case Code::LOGOUT_FIRST: return "LOGOUT_FIRST";
    case Code::USER_LIMIT: return "USER_LIMIT";
    case Code::LOGIN_REQUIRED: return "LOGIN_REQUIRED";
    case Code::ONLY_OWNER_CAN_SELL: return "ONLY_OWNER_CAN_SELL";
    case Code::ONLY_OWNER_CAN_WITHDRAW: return "ONLY_OWNER_CAN_WITHDRAW";
    case Code::ONLY_OWNER_CAN_START: return "ONLY_OWNER_CAN_START";
    case Code::SELLER_CANNOT_BID: return "SELLER_CANNOT_BID";
    case Code::SELLER_CANNOT_BUY: return "SELLER_CANNOT_BUY";
    case Code::ROOM_NOT_FOUND: return "ROOM_NOT_FOUND";
    case Code::ALREADY_IN_A_ROOM: return "ALREADY_IN_A_ROOM";
    case Code::NOT_IN_A_ROOM: return "NOT_IN_A_ROOM";
    case Code::JOIN_ROOM_FIRST: return "JOIN_ROOM_FIRST";
    case Code::ROOM_LIMIT: return "ROOM_LIMIT";
    case Code::ROOM_FULL: return "ROOM_FULL";
    case Code::ITEM_NOT_FOUND: return "ITEM_NOT_FOUND";
    case Code::NO_ACTIVE_ITEM: return "NO_ACTIVE_ITEM";
    case Code::BID_INVALID: return "BID_INVALID";
    case Code::ITEM_LIMIT: return "ITEM_LIMIT";
    case Code::ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN: return "ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN";
    case Code::AUCTION_ALREADY_ACTIVE: return "AUCTION_ALREADY_ACTIVE";
    case Code::NO_QUEUED_ITEM: return "NO_QUEUED_ITEM";
    case Code::UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS: return "UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS";
    case Code::BAD_CREDENTIAL_FORMAT: return "BAD_CREDENTIAL_FORMAT";
    case Code::INVALID_OR_TOO_LONG_LINE: return "INVALID_OR_TOO_LONG_LINE";
    case Code::INVALID_LINE_ENDING: return "INVALID_LINE_ENDING";
    case Code::INVALID_ROOM_NAME: return "INVALID_ROOM_NAME";
    case Code::INVALID_ROOM_ID: return "INVALID_ROOM_ID";
    case Code::INVALID_ITEM_TEXT: return "INVALID_ITEM_TEXT";
    case Code::INVALID_PRICE: return "INVALID_PRICE";
    case Code::INVALID_ITEM_ID: return "INVALID_ITEM_ID";
    case Code::EVENT_ITEM_STARTED: return "ITEM_STARTED";
    case Code::EVENT_MEMBER_JOINED: return "MEMBER_JOINED";
    case Code::EVENT_MEMBER_LEFT: return "MEMBER_LEFT";
    case Code::EVENT_ITEM_QUEUED: return "ITEM_QUEUED";
    case Code::EVENT_ITEM_WITHDRAWN: return "ITEM_WITHDRAWN";
    case Code::EVENT_NEW_BID: return "NEW_BID";
    case Code::EVENT_NOTICE: return "NOTICE";
    case Code::EVENT_SOLD: return "SOLD";
    case Code::EVENT_QUEUE_EMPTY: return "QUEUE_EMPTY";
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

/** @brief Format numeric response; existing internal payload fields are pipe-separated. */
inline std::string format(Code code, const std::string& fields = {}) {
  std::string line = std::to_string(static_cast<int>(code));
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
