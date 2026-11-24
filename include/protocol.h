#pragma once
#include <cstddef>
#include <string>

namespace protocol {
// Application-layer status codes; request identifiers are uppercase keywords.
// All wire lines terminate with CRLF. A space separates code from message;
// a pipe separates fields, preserving spaces in names and descriptions.
inline constexpr char CRLF[] = "\r\n";
enum class Code : int {
  CONNECTED = 100,
  LOGGED_IN = 110,
  REGISTERED = 111,
  PASSWORD_CHANGED = 112,
  SESSION = 113,
  LOGGED_OUT = 130,
  BYE = 131,
  ROOM_CREATED = 140,
  JOINED_ROOM = 141,
  LEFT_ROOM = 142,
  ROOMS = 143,
  ROOM = 144,
  ITEM_QUEUED = 150,
  ITEM_WITHDRAWN = 151,
  ITEMS = 152,
  ITEM = 153,
  AUCTION_STARTED = 160,
  BID_ACCEPTED = 161,
  BOUGHT = 162,
  COMMANDS = 190,
  INVALID_CREDENTIALS = 210,
  USERNAME_EXISTS = 211,
  ALREADY_LOGGED_IN = 213,
  USER_ALREADY_ONLINE = 214,
  LOGOUT_FIRST = 215,
  INVALID_OLD_PASSWORD = 216,
  USER_LIMIT = 217,
  LOGIN_REQUIRED = 221,
  ONLY_OWNER_CAN_SELL = 222,
  ONLY_OWNER_CAN_WITHDRAW = 223,
  ONLY_OWNER_CAN_START = 224,
  SELLER_CANNOT_BID = 225,
  SELLER_CANNOT_BUY = 226,
  ROOM_NOT_FOUND = 230,
  ALREADY_IN_A_ROOM = 231,
  NOT_IN_A_ROOM = 232,
  JOIN_ROOM_FIRST = 233,
  ROOM_LIMIT = 234,
  ROOM_FULL = 235,
  ITEM_NOT_FOUND = 240,
  NO_ACTIVE_ITEM = 241,
  BID_INVALID = 242,
  ITEM_LIMIT = 243,
  ONLY_QUEUED_ITEMS_CAN_BE_WITHDRAWN = 244,
  AUCTION_ALREADY_ACTIVE = 245,
  NO_QUEUED_ITEM = 246,
  UNKNOWN_COMMAND_OR_WRONG_ARGUMENTS = 300,
  BAD_CREDENTIAL_FORMAT = 301,
  INVALID_OR_TOO_LONG_LINE = 302,
  INVALID_LINE_ENDING = 303,
  INVALID_NEW_PASSWORD = 304,
  INVALID_ROOM_NAME = 305,
  INVALID_ROOM_ID = 306,
  INVALID_ITEM_TEXT = 307,
  INVALID_PRICE = 308,
  INVALID_ITEM_ID = 309,
  EVENT_ITEM_STARTED = 400,
  EVENT_MEMBER_JOINED = 401,
  EVENT_MEMBER_LEFT = 402,
  EVENT_ITEM_QUEUED = 403,
  EVENT_ITEM_WITHDRAWN = 404,
  EVENT_NEW_BID = 410,
  EVENT_NOTICE = 411,
  EVENT_SOLD = 420,
  EVENT_QUEUE_EMPTY = 421,
  HASH_FAILED = 500,
  STORAGE_FAILED = 501,
};

/** @brief Validate request keyword and number of fields (including keyword). */
inline bool valid_request(const std::string& name, std::size_t count) {
  if (name == "HELP") return count == 1;
  if (name == "EXIT") return count == 1;
  if (name == "REGISTER") return count == 3;
  if (name == "LOGIN") return count == 3;
  if (name == "LOGOUT") return count == 1;
  if (name == "CHANGE_PASSWORD") return count == 3;
  if (name == "WHOAMI") return count == 1;
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
    case Code::PASSWORD_CHANGED: return "PASSWORD_CHANGED";
    case Code::SESSION: return "SESSION";
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
    case Code::INVALID_OLD_PASSWORD: return "INVALID_OLD_PASSWORD";
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
    case Code::INVALID_NEW_PASSWORD: return "INVALID_NEW_PASSWORD";
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
    case Code::HASH_FAILED: return "HASH_FAILED";
    case Code::STORAGE_FAILED: return "STORAGE_FAILED";
  }
  return "UNKNOWN_STATUS";
}

/** @brief Format one response/event body, excluding the CRLF terminator. */
inline std::string format(Code code, const std::string& fields = {}) {
  std::string line = std::to_string(static_cast<int>(code)) + " " + message_name(code);
  if (!fields.empty()) line += "|" + fields;
  return line;
}
} // namespace protocol
