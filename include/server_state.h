#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace data {
struct User {
  std::string username;
  std::string salt_hex;
  std::string password_hash_hex;
  int socket_fd = -1;
  int room_id = 0;
};

enum class Status { QUEUED, ACTIVE, SOLD, WITHDRAWN };
struct Item {
  int id = 0;
  std::string name, description, seller;
  int64_t start_price = 0, buy_now_price = 0, highest_price = 0, final_price = 0;
  std::string highest_bidder, buyer;
  Status status = Status::QUEUED;
  int notifications = 0;
  std::chrono::steady_clock::time_point next_notification{};
};
struct Room {
  int id = 0;
  std::string name, owner;
  std::set<std::string> members;
  std::vector<Item> items; // FIFO by creation order
  int active_id = 0;
};
struct Client {
  int fd = -1;
  std::string username, input_line;
  std::deque<std::string> output;
  size_t output_offset = 0, queued_bytes = 0;
  bool closing = false, drop = false;
};
struct State {
  std::unordered_map<std::string, User> users;
  std::unordered_map<int, Room> rooms;
  std::unordered_map<int, Client> clients;
  int next_room_id = 1, next_item_id = 1;
};
}
