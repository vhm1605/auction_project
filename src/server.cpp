#define _POSIX_C_SOURCE 200809L
#include "server_state.h"
#include "network_utils.h"
#include "protocol.h"
#include "persistence.h"
#include "persistence_pg.h"
#include "logger.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

using Clock = std::chrono::steady_clock;
namespace {
constexpr size_t MAX_CLIENTS = 200;
constexpr size_t MAX_QUEUE = 64 * 1024;
constexpr size_t MAX_ROOMS = 100;
constexpr size_t MAX_ITEMS_PER_ROOM = 100;
constexpr size_t MAX_USERS = 1000;

std::string status_name(data::Status s) {
  switch (s) {
    case data::Status::QUEUED: return "QUEUED";
    case data::Status::ACTIVE: return "ACTIVE";
    case data::Status::SOLD: return "SOLD";
    case data::Status::WITHDRAWN: return "WITHDRAWN";
  }
  return "UNKNOWN";
}

class AuctionServer {
  data::State state_;
  persistence::PostgresPersistence db_;
  std::string connection_string_;
  std::string directory_;
  int bid_timeout_ = 180;
  int notice_gap_ = 30;

  void queue(int fd, protocol::Code code, const std::string& fields = {}) {
    const std::string text = protocol::format(code, fields);
    auto it = state_.clients.find(fd);
    if (it == state_.clients.end() || it->second.drop) return;
    data::Client& client = it->second;
    if (text.size() > network::MAX_LINE || text.size() + 2 > MAX_QUEUE - client.queued_bytes) {
      client.drop = true; // slow client or invalid outgoing line
      return;
    }
    client.output.push_back(text + protocol::CRLF);
    client.queued_bytes += text.size() + 2;
  }

  void broadcast(const data::Room& room, protocol::Code code, const std::string& fields = {}) {
    for (const std::string& name : room.members) {
      auto u = state_.users.find(name);
      if (u != state_.users.end() && u->second.socket_fd >= 0)
        queue(u->second.socket_fd, code, fields);
    }
  }

  data::Room* joined_room(const std::string& name) {
    auto u = state_.users.find(name);
    if (u == state_.users.end() || u->second.room_id <= 0) return nullptr;
    auto r = state_.rooms.find(u->second.room_id);
    if (r == state_.rooms.end() || !r->second.members.count(name)) return nullptr;
    return &r->second;
  }

  data::Item* active_item(data::Room& room) {
    if (room.active_id == 0) return nullptr;
    for (data::Item& item : room.items)
      if (item.id == room.active_id && item.status == data::Status::ACTIVE) return &item;
    return nullptr;
  }

  void leave_room(const std::string& name) {
    data::Room* room = joined_room(name);
    if (!room) {
      auto u = state_.users.find(name);
      if (u != state_.users.end()) u->second.room_id = 0;
      return;
    }
    int id = room->id;
    room->members.erase(name);
    state_.users.at(name).room_id = 0;
    broadcast(*room, protocol::Code::MEMBER_EVENT, std::to_string(id) + "|LEAVE|" + name);
    logger::log_activity(directory_, "LEAVE_ROOM", name, std::to_string(id));
  }

  void disconnect_client(int fd) {
    auto it = state_.clients.find(fd);
    if (it == state_.clients.end()) return;
    std::string name = it->second.username;
    if (!name.empty()) {
      leave_room(name);
      auto u = state_.users.find(name);
      if (u != state_.users.end() && u->second.socket_fd == fd) u->second.socket_fd = -1;
      logger::log_activity(directory_, "DISCONNECT", name);
    }
    close(fd);
    state_.clients.erase(it);
  }

  bool start_next(data::Room& room) {
    if (room.active_id != 0) return true;
    for (data::Item& item : room.items) {
      if (item.status != data::Status::QUEUED) continue;
      if (!db_.activate_item(room.id, item.id)) return false;
      item.status = data::Status::ACTIVE;
      room.active_id = item.id;
      item.highest_price = item.start_price;
      broadcast(room, protocol::Code::ITEM_STARTED, std::to_string(room.id) + "|" +
                std::to_string(item.id) + "|" + item.name + "|" + item.description + "|" +
                std::to_string(item.start_price) + "|" + std::to_string(item.buy_now_price));
      logger::log_activity(directory_, "START_ITEM", item.seller, std::to_string(item.id));
      return true;
    }
    broadcast(room, protocol::Code::QUEUE_EMPTY, std::to_string(room.id));
    return true;
  }

  bool finish_item(data::Room& room, data::Item& item,
                   const std::string& buyer, int64_t amount, const std::string& mode,
                   int final_notices = -1) {
    data::Item* next = nullptr;
    for (auto& candidate : room.items) {
      if (candidate.status == data::Status::QUEUED) { next = &candidate; break; }
    }
    int saved_notices = final_notices >= 0 ? final_notices : item.notifications;
    if (!db_.finish_item(item.id, buyer, amount, room.id, next ? next->id : 0, saved_notices)) {
      std::cerr << "Database settlement failed: " << db_.last_error() << '\n';
      return false;
    }
    item.status = data::Status::SOLD;
    item.buyer = buyer;
    item.final_price = amount;
    item.notifications = saved_notices;
    int sold_id = item.id;
    room.active_id = 0;
    if (mode == "BID" && final_notices == 3) {
      broadcast(room, protocol::Code::CLOSING_NOTICE, std::to_string(room.id) + "|" +
                std::to_string(sold_id) + "|3/3|" + buyer + "|" + std::to_string(amount));
      logger::log_activity(directory_, "NOTICE", buyer, std::to_string(sold_id) + " #3");
    }
    broadcast(room, protocol::Code::ITEM_SOLD, std::to_string(room.id) + "|" +
              std::to_string(sold_id) + "|" + buyer + "|" + std::to_string(amount) + "|" + mode);
    logger::log_activity(directory_, "SOLD_" + mode, buyer, std::to_string(sold_id) + " price=" + std::to_string(amount));
    if (next) {
      next->status = data::Status::ACTIVE;
      next->highest_price = next->start_price;
      room.active_id = next->id;
      broadcast(room, protocol::Code::ITEM_STARTED, std::to_string(room.id) + "|" +
                std::to_string(next->id) + "|" + next->name + "|" + next->description + "|" +
                std::to_string(next->start_price) + "|" + std::to_string(next->buy_now_price));
      logger::log_activity(directory_, "START_ITEM", next->seller, std::to_string(next->id));
    } else {
      broadcast(room, protocol::Code::QUEUE_EMPTY, std::to_string(room.id));
    }
    return true;
  }

  static int64_t deadline_ms(int seconds) {
    using SysClock = std::chrono::system_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               SysClock::now().time_since_epoch()).count() + static_cast<int64_t>(seconds) * 1000;
  }

  void check_timers() {
    Clock::time_point now = Clock::now();
    for (auto& kv : state_.rooms) {
      data::Room& room = kv.second;
      data::Item* item = active_item(room);
      if (!item || item->highest_bidder.empty() || now < item->next_notification) continue;
      int next_count = item->notifications + 1;
      if (next_count == 3) {
        // Commit sale and queue advance as one PostgreSQL transaction.
        std::string bidder = item->highest_bidder;
        int64_t price = item->highest_price;
        if (!finish_item(room, *item, bidder, price, "BID", 3)) continue;
        // finish_item queues NOTICE #3 before SOLD after the DB commit.
      } else {
        int64_t next_epoch = deadline_ms(notice_gap_);
        if (!db_.save_notice(item->id, next_count, next_epoch)) {
          std::cerr << "Database notice failed: " << db_.last_error() << '\n';
          continue;
        }
        item->notifications = next_count;
        item->next_notification = now + std::chrono::seconds(notice_gap_);
        broadcast(room, protocol::Code::CLOSING_NOTICE, std::to_string(room.id) + "|" +
                  std::to_string(item->id) + "|" + std::to_string(next_count) +
                  "/3|" + item->highest_bidder + "|" + std::to_string(item->highest_price));
        logger::log_activity(directory_, "NOTICE", item->highest_bidder,
                             std::to_string(item->id) + " #" + std::to_string(next_count));
      }
    }
  }

  void command(int fd, const std::string& line) {
    std::vector<std::string> v;
    if (!protocol::parse(line, v)) {
      queue(fd, protocol::Code::INVALID_REQUEST, "Sai tên lệnh hoặc số tham số");
      return;
    }
    const std::string& action = v[0];
    if (!protocol::valid_request(action, v.size())) {
      queue(fd, protocol::Code::INVALID_REQUEST, "Sai tên lệnh hoặc số tham số");
      return;
    }
    auto c = state_.clients.find(fd);
    if (c == state_.clients.end()) return;
    std::string username = c->second.username;

    if (action == "HELP" && v.size() == 1) {
      queue(fd, protocol::Code::OK, std::string("HELP|") + "REGISTER|LOGIN|LOGOUT|CREATE_ROOM|LIST_ROOMS|JOIN_ROOM|LEAVE_ROOM|ADD_ITEM|LIST_ITEMS|WITHDRAW_ITEM|START|BID|BUY_NOW|HELP|EXIT");
      return;
    }
    if (action == "EXIT" && v.size() == 1) {
      queue(fd, protocol::Code::OK, "EXIT"); c->second.closing = true; return;
    }
    if (action == "REGISTER" && v.size() == 3) {
      if (!username.empty()) { queue(fd, protocol::Code::AUTH_ERROR, "Phải đăng xuất trước"); return; }
      if (!network::valid_username(v[1]) || !network::valid_password(v[2])) {
        queue(fd, protocol::Code::INVALID_DATA, "Sai định dạng tài khoản"); return;
      }
      if (state_.users.count(v[1])) { queue(fd, protocol::Code::AUTH_ERROR, "Tài khoản đã tồn tại"); return; }
      if (state_.users.size() >= MAX_USERS) { queue(fd, protocol::Code::LIMIT_REACHED, "Đạt giới hạn tài khoản"); return; }
      data::User u;
      u.username = v[1];
      if (!persistence::make_password_hash(v[2], u.salt_hex, u.password_hash_hex) ||
          !db_.add_user(u)) { queue(fd, protocol::Code::STORAGE_FAILED, "Lỗi lưu trữ dữ liệu"); return; }
      std::string key = u.username;
      state_.users.emplace(key, std::move(u));
      queue(fd, protocol::Code::OK, std::string("REGISTER|") + v[1]);
      logger::log_activity(directory_, "REGISTER", v[1]);
      return;
    }
    if (action == "LOGIN" && v.size() == 3) {
      if (!username.empty()) { queue(fd, protocol::Code::AUTH_ERROR, "Phiên đã đăng nhập"); return; }
      auto user = state_.users.find(v[1]);
      // Short circuit on invalid credentials; do not reveal which field failed.
      if (user == state_.users.end() || !network::valid_password(v[2]) ||
          !persistence::password_matches(v[2], user->second.salt_hex, user->second.password_hash_hex)) {
        queue(fd, protocol::Code::AUTH_ERROR, "Sai tài khoản hoặc mật khẩu"); return;
      }
      if (user->second.socket_fd >= 0) { queue(fd, protocol::Code::AUTH_ERROR, "Tài khoản đang đăng nhập nơi khác"); return; }
      user->second.socket_fd = fd;
      user->second.room_id = 0;
      c->second.username = v[1];
      queue(fd, protocol::Code::OK, std::string("LOGIN|") + v[1]);
      logger::log_activity(directory_, "LOGIN", v[1]);
      return;
    }
    if (username.empty()) { queue(fd, protocol::Code::AUTH_ERROR, "Phải đăng nhập trước"); return; }

    if (action == "LOGOUT" && v.size() == 1) {
      leave_room(username);
      auto& user = state_.users.at(username);
      user.socket_fd = -1;
      c->second.username.clear();
      queue(fd, protocol::Code::OK, "LOGOUT");
      logger::log_activity(directory_, "LOGOUT", username);
      return;
    }
    if (action == "CREATE_ROOM" && v.size() == 2) {
      if (joined_room(username)) { queue(fd, protocol::Code::INVALID_STATE, "Đang ở trong phòng"); return; }
      if (!network::valid_text(v[1], 64)) { queue(fd, protocol::Code::INVALID_DATA, "Tên phòng không hợp lệ"); return; }
      if (state_.rooms.size() >= MAX_ROOMS || state_.next_room_id >= 1000000) { queue(fd, protocol::Code::LIMIT_REACHED, "Đạt giới hạn số phòng"); return; }
      data::Room room;
      room.id = state_.next_room_id;
      room.name = v[1];
      room.owner = username;
      room.members.insert(username); // Room creator automatically joins.
      int id = room.id;
      if (!db_.create_room(room)) { queue(fd, protocol::Code::STORAGE_FAILED, "Lỗi lưu trữ dữ liệu"); return; }
      ++state_.next_room_id;
      state_.rooms.emplace(id, std::move(room));
      state_.users.at(username).room_id = id;
      queue(fd, protocol::Code::OK, std::string("CREATE_ROOM|") + std::to_string(id) + "|" + v[1]);
      logger::log_activity(directory_, "CREATE_ROOM", username, std::to_string(id));
      return;
    }
    if (action == "LIST_ROOMS" && v.size() == 1) {
      queue(fd, protocol::Code::OK, std::string("LIST_ROOMS|") + std::to_string(state_.rooms.size()));
      for (const auto& [id, room] : state_.rooms)
        queue(fd, protocol::Code::ROOM_DATA, std::string("LIST_ROOMS|") + std::to_string(id) + "|" + room.name + "|" + room.owner +
                  "|" + std::to_string(room.members.size()));
      return;
    }
    if (action == "JOIN_ROOM" && v.size() == 2) {
      if (joined_room(username)) { queue(fd, protocol::Code::INVALID_STATE, "Đang ở trong phòng"); return; }
      int64_t rid;
      if (!network::parse_positive(v[1], rid, 999999)) { queue(fd, protocol::Code::INVALID_DATA, "ID phòng không hợp lệ"); return; }
      auto r = state_.rooms.find(static_cast<int>(rid));
      if (r == state_.rooms.end()) { queue(fd, protocol::Code::NOT_FOUND, "Không tìm thấy phòng"); return; }
      if (r->second.members.size() >= MAX_CLIENTS) { queue(fd, protocol::Code::LIMIT_REACHED, "Phòng đã đầy"); return; }
      r->second.members.insert(username);
      state_.users.at(username).room_id = static_cast<int>(rid);
      queue(fd, protocol::Code::OK, std::string("JOIN_ROOM|") + v[1] + "|" + r->second.name);
      broadcast(r->second, protocol::Code::MEMBER_EVENT, v[1] + "|JOIN|" + username);
      logger::log_activity(directory_, "JOIN_ROOM", username, v[1]);
      return;
    }
    if (action == "LEAVE_ROOM" && v.size() == 1) {
      if (!joined_room(username)) { queue(fd, protocol::Code::INVALID_STATE, "Chưa tham gia phòng"); return; }
      leave_room(username);
      queue(fd, protocol::Code::OK, "LEAVE_ROOM");
      return;
    }
    data::Room* room = joined_room(username);
    if (!room) { queue(fd, protocol::Code::INVALID_STATE, "Chưa tham gia phòng"); return; }

    if (action == "ADD_ITEM" && v.size() == 5) {
      if (room->owner != username) { queue(fd, protocol::Code::FORBIDDEN, "Chỉ chủ phòng được thêm vật phẩm"); return; }
      if (!network::valid_text(v[1], 80) || !network::valid_text(v[2], 240)) {
        queue(fd, protocol::Code::INVALID_DATA, "Tên hoặc mô tả không hợp lệ"); return;
      }
      int64_t start = 0, buy = 0;
      if (!network::parse_positive(v[3], start) || !network::parse_positive(v[4], buy) ||
          start > network::MAX_PRICE - network::MIN_INCREMENT ||
          buy < start + network::MIN_INCREMENT) { queue(fd, protocol::Code::INVALID_DATA, "Giá không hợp lệ"); return; }
      if (room->items.size() >= MAX_ITEMS_PER_ROOM || state_.next_item_id >= 1000000) {
        queue(fd, protocol::Code::LIMIT_REACHED, "Đạt giới hạn vật phẩm"); return;
      }
      data::Item item;
      item.id = state_.next_item_id;
      item.name = v[1]; item.description = v[2]; item.seller = username;
      item.start_price = start; item.buy_now_price = buy; item.highest_price = start;
      int id = item.id;
      if (!db_.create_item(room->id, item)) { queue(fd, protocol::Code::STORAGE_FAILED, "Lỗi lưu trữ dữ liệu"); return; }
      ++state_.next_item_id;
      room->items.push_back(std::move(item));
      queue(fd, protocol::Code::OK, std::string("ADD_ITEM|") + std::to_string(id));
      broadcast(*room, protocol::Code::ITEM_EVENT, std::to_string(room->id) + "|ADD|" + std::to_string(id) + "|" + v[1]);
      logger::log_activity(directory_, "ADD_ITEM", username, std::to_string(id));
      return;
    }
    if (action == "WITHDRAW_ITEM" && v.size() == 2) {
      if (room->owner != username) { queue(fd, protocol::Code::FORBIDDEN, "Chỉ chủ phòng được rút vật phẩm"); return; }
      int64_t iid;
      if (!network::parse_positive(v[1], iid, 999999)) { queue(fd, protocol::Code::INVALID_DATA, "ID vật phẩm không hợp lệ"); return; }
      for (auto& item : room->items) {
        if (item.id != iid) continue;
        if (item.status != data::Status::QUEUED) { queue(fd, protocol::Code::INVALID_STATE, "Vật phẩm không ở trạng thái chờ"); return; }
        if (!db_.withdraw_item(room->id, item.id)) { queue(fd, protocol::Code::STORAGE_FAILED, "Lỗi lưu trữ dữ liệu"); return; }
        item.status = data::Status::WITHDRAWN;
        queue(fd, protocol::Code::OK, std::string("WITHDRAW_ITEM|") + v[1]);
        broadcast(*room, protocol::Code::ITEM_EVENT, std::to_string(room->id) + "|WITHDRAW|" + v[1]);
        logger::log_activity(directory_, "WITHDRAW_ITEM", username, v[1]);
        return;
      }
      queue(fd, protocol::Code::NOT_FOUND, "Không tìm thấy vật phẩm"); return;
    }
    if (action == "LIST_ITEMS" && v.size() == 1) {
      queue(fd, protocol::Code::OK, std::string("LIST_ITEMS|") + std::to_string(room->items.size()) + "|ACTIVE=" + std::to_string(room->active_id));
      for (const auto& item : room->items)
        queue(fd, protocol::Code::ITEM_DATA, std::string("LIST_ITEMS|") + std::to_string(item.id) + "|" + status_name(item.status) + "|" +
              item.name + "|" + item.description + "|" + std::to_string(item.start_price) + "|" +
              std::to_string(item.buy_now_price) + "|" + std::to_string(item.highest_price) + "|" +
              (item.highest_bidder.empty() ? "NONE" : item.highest_bidder) + "|" +
              (item.buyer.empty() ? "NONE" : item.buyer));
      return;
    }
    if (action == "START" && v.size() == 1) {
      if (room->owner != username) { queue(fd, protocol::Code::FORBIDDEN, "Chỉ chủ phòng được bắt đầu đấu giá"); return; }
      if (room->active_id != 0) { queue(fd, protocol::Code::INVALID_STATE, "Đang có vật phẩm đấu giá"); return; }
      bool has_queued = false;
      for (const auto& item : room->items) if (item.status == data::Status::QUEUED) { has_queued = true; break; }
      if (!has_queued) { queue(fd, protocol::Code::INVALID_STATE, "Không có vật phẩm chờ"); return; }
      if (!start_next(*room)) { queue(fd, protocol::Code::STORAGE_FAILED, "Lỗi lưu trữ dữ liệu"); return; }
      queue(fd, protocol::Code::OK, "START"); return;
    }
    if (action == "BID" && v.size() == 2) {
      data::Item* item = active_item(*room);
      if (!item) { queue(fd, protocol::Code::INVALID_STATE, "Không có vật phẩm đang đấu giá"); return; }
      if (item->seller == username) { queue(fd, protocol::Code::FORBIDDEN, "Người bán không được tự đặt giá"); return; }
      int64_t amount;
      if (!network::parse_positive(v[1], amount) ||
          amount <= item->highest_price ||
          amount - item->highest_price < network::MIN_INCREMENT ||
          amount >= item->buy_now_price) { queue(fd, protocol::Code::INVALID_DATA, "Giá đặt không hợp lệ"); return; }
      if (!db_.save_bid(item->id, username, amount, deadline_ms(bid_timeout_))) {
        queue(fd, protocol::Code::STORAGE_FAILED, "Lỗi lưu trữ dữ liệu"); return;
      }
      item->highest_price = amount;
      item->highest_bidder = username;
      item->notifications = 0;
      item->next_notification = Clock::now() + std::chrono::seconds(bid_timeout_);
      queue(fd, protocol::Code::OK, std::string("BID|") + std::to_string(item->id) + "|" + std::to_string(amount));
      broadcast(*room, protocol::Code::NEW_BID, std::to_string(room->id) + "|" +
                std::to_string(item->id) + "|" + username + "|" + std::to_string(amount));
      logger::log_activity(directory_, "BID", username, std::to_string(item->id) + " amount=" + std::to_string(amount));
      return;
    }
    if (action == "BUY_NOW" && v.size() == 1) {
      data::Item* item = active_item(*room);
      if (!item) { queue(fd, protocol::Code::INVALID_STATE, "Không có vật phẩm đang đấu giá"); return; }
      if (item->seller == username) { queue(fd, protocol::Code::FORBIDDEN, "Người bán không được tự mua"); return; }
      int item_id = item->id;
      int64_t amount = item->buy_now_price;
      if (!finish_item(*room, *item, username, amount, "BUY_NOW")) {
        queue(fd, protocol::Code::STORAGE_FAILED, "Lỗi lưu trữ dữ liệu"); return;
      }
      queue(fd, protocol::Code::OK, std::string("BUY_NOW|") + std::to_string(item_id) + "|" + std::to_string(amount));
      return;
    }
    queue(fd, protocol::Code::INVALID_REQUEST, "Sai tên lệnh hoặc số tham số");
  }

  void receive_from(int fd) {
    char bytes[4096];
    // Bound per-client work so one noisy client cannot starve timers.
    for (int read_batch = 0; read_batch < 4; ++read_batch) {
      ssize_t n = recv(fd, bytes, sizeof(bytes), 0);
      if (n == 0) { state_.clients.at(fd).drop = true; return; }
      if (n < 0) {
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        state_.clients.at(fd).drop = true; return;
      }
      for (ssize_t i = 0; i < n; ++i) {
        auto it = state_.clients.find(fd);
        if (it == state_.clients.end() || it->second.drop || it->second.closing) return;
        data::Client& c = it->second;
        unsigned char ch = static_cast<unsigned char>(bytes[i]);
        if (ch == '\n') {
          if (c.input_line.empty() || c.input_line.back() != '\r') {
            queue(fd, protocol::Code::INVALID_REQUEST, "Sai ký tự kết thúc dòng");
            c.input_line.clear();
            continue;
          }
          c.input_line.pop_back();
          std::string line = std::move(c.input_line);
          c.input_line.clear();
          command(fd, line);
        } else if ((!c.input_line.empty() && c.input_line.back() == '\r') ||
                   (ch < 0x20 && ch != '\r') || ch == 0x7f ||
                   (ch != '\r' && c.input_line.size() >= network::MAX_LINE)) {
          queue(fd, protocol::Code::INVALID_REQUEST, "Message không hợp lệ hoặc quá dài");
          c.closing = true;
          return;
        } else {
          c.input_line.push_back(static_cast<char>(ch));
        }
      }
    }
  }

  void send_to(int fd) {
    auto it = state_.clients.find(fd);
    if (it == state_.clients.end()) return;
    data::Client& c = it->second;
    while (!c.output.empty()) {
      const std::string& msg = c.output.front();
      size_t remaining = msg.size() - c.output_offset;
      ssize_t n = send(fd, msg.data() + c.output_offset, remaining, MSG_NOSIGNAL);
      if (n > 0) {
        c.output_offset += static_cast<size_t>(n);
        c.queued_bytes -= static_cast<size_t>(n);
        if (c.output_offset == msg.size()) { c.output.pop_front(); c.output_offset = 0; }
        continue;
      }
      if (n < 0 && errno == EINTR) continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
      c.drop = true;
      return;
    }
  }

public:
  AuctionServer(std::string directory, std::string connection, int bid_timeout, int notice_gap)
      : connection_string_(std::move(connection)), directory_(std::move(directory)),
        bid_timeout_(bid_timeout), notice_gap_(notice_gap) {}

  int run(int port) {
    if (directory_.empty() || directory_.size() > 1024 ||
        (mkdir(directory_.c_str(), 0700) < 0 && errno != EEXIST)) {
      std::cerr << "Cannot create private log directory\n";
      return 1;
    }
    struct stat dir_stat{};
    if (lstat(directory_.c_str(), &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode) ||
        dir_stat.st_uid != geteuid() || (dir_stat.st_mode & 0077) != 0) {
      std::cerr << "Cannot create private log directory\n";
      return 1;
    }
    if (!db_.connect_db(connection_string_) || !db_.load_state(state_)) {
      std::cerr << "Cannot initialize PostgreSQL: " << db_.last_error() << '\n';
      return 1;
    }
    logger::set_database(&db_);
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(listen_fd, 32) < 0 || !network::set_nonblocking(listen_fd)) {
      perror("bind/listen/nonblocking"); close(listen_fd); return 1;
    }
    if (listen_fd >= FD_SETSIZE) {
      std::cerr << "Listening descriptor exceeds select() FD_SETSIZE limit\n";
      close(listen_fd); return 1;
    }
    std::cout << "Auction server (PostgreSQL) listening on port " << port << "; loaded " << state_.users.size()
              << " accounts, " << state_.rooms.size() << " rooms; bid timeout " << bid_timeout_ << "s; notice gap " << notice_gap_ << "s\n" << std::flush;

    for (;;) {
      check_timers();
      fd_set readfds, writefds;
      FD_ZERO(&readfds); FD_ZERO(&writefds);
      FD_SET(listen_fd, &readfds);
      int max_fd = listen_fd;
      for (const auto& [fd, c] : state_.clients) {
        if (c.drop || (c.closing && c.output.empty())) continue;
        if (!c.closing) FD_SET(fd, &readfds);
        if (!c.output.empty()) FD_SET(fd, &writefds);
        max_fd = std::max(max_fd, fd);
      }
      timeval timeout{};
      timeout.tv_sec = 0;
      timeout.tv_usec = 100000; // check timers at 100ms precision
      int ready = select(max_fd + 1, &readfds, &writefds, nullptr, &timeout);
      if (ready < 0 && errno != EINTR) { perror("select"); break; }
      if (ready > 0 && FD_ISSET(listen_fd, &readfds)) {
        for (int accept_batch = 0; accept_batch < 32; ++accept_batch) {
          sockaddr_in peer{};
          socklen_t peer_len = sizeof(peer);
          int fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
          if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
            break;
          }
          if (fd >= FD_SETSIZE || state_.clients.size() >= MAX_CLIENTS || !network::set_nonblocking(fd)) {
            close(fd); continue;
          }
          data::Client c;
          c.fd = fd;
          state_.clients.emplace(fd, std::move(c));
          queue(fd, protocol::Code::CONNECTED, "Send HELP for commands");
        }
      }
      // Work on a snapshot to avoid iterator invalidation during disconnects.
      std::vector<int> descriptors;
      descriptors.reserve(state_.clients.size());
      for (const auto& entry : state_.clients) descriptors.push_back(entry.first);
      for (int fd : descriptors) {
        auto it = state_.clients.find(fd);
        if (it == state_.clients.end()) continue;
        if (ready > 0 && FD_ISSET(fd, &readfds) && !it->second.closing) receive_from(fd);
        it = state_.clients.find(fd);
        if (it != state_.clients.end() && !it->second.drop && ready > 0 && FD_ISSET(fd, &writefds)) send_to(fd);
      }
      for (int fd : descriptors) {
        auto it = state_.clients.find(fd);
        if (it != state_.clients.end() && (it->second.drop || (it->second.closing && it->second.output.empty())))
          disconnect_client(fd);
      }
    }
    close(listen_fd);
    return 1;
  }
};

bool parse_arg(const char* s, int& result, int max_value) {
  int64_t value = 0;
  if (!network::parse_positive(s, value, max_value)) return false;
  result = static_cast<int>(value);
  return true;
}
}

int main(int argc, char* argv[]) {
  signal(SIGPIPE, SIG_IGN);
  int port = 8080, bid_timeout = 180, notice_gap = 30;
  std::string directory = "./auction_data";
  const char* env_conn = std::getenv("DB_CONN_STR");
  std::string connection = env_conn ? env_conn : "";
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 >= argc) { std::cerr << "Missing option value\n"; return 2; }
    std::string flag = argv[i];
    if (flag == "--port") {
      if (!parse_arg(argv[i+1], port, 65535)) return 2;
    } else if (flag == "--data-dir") {
      directory = argv[i+1];
    } else if (flag == "--db-conn") {
      connection = argv[i+1];
    } else if (flag == "--bid-timeout") {
      if (!parse_arg(argv[i+1], bid_timeout, 3600)) return 2;
    } else if (flag == "--notice-gap") {
      if (!parse_arg(argv[i+1], notice_gap, 3600)) return 2;
    } else { std::cerr << "Unknown flag: " << flag << '\n'; return 2; }
  }
  if (connection.empty()) {
    std::cerr << "Set DB_CONN_STR for PostgreSQL (or pass --db-conn).\n";
    return 2;
  }
  AuctionServer server(directory, connection, bid_timeout, notice_gap);
  return server.run(port);
}
