#pragma once
#include "server_state.h"
#include <libpq-fe.h>
#include <string>
#include <vector>

namespace persistence {
// Uses libpq parameterized queries. Only one server event loop accesses this object.
class PostgresPersistence {
public:
  PostgresPersistence() = default;
  ~PostgresPersistence();
  PostgresPersistence(const PostgresPersistence&) = delete;
  PostgresPersistence& operator=(const PostgresPersistence&) = delete;

  bool connect_db(const std::string& connection);
  bool load_state(data::State& state);
  bool add_user(const data::User& user);
  bool create_room(const data::Room& room);
  bool create_item(int room_id, const data::Item& item);
  bool withdraw_item(int room_id, int item_id);
  bool activate_item(int room_id, int item_id);
  bool save_bid(int item_id, const std::string& bidder, int64_t amount, int64_t next_notice_ms);
  bool save_notice(int item_id, int notifications, int64_t next_notice_ms);
  bool finish_item(int sold_id, const std::string& buyer, int64_t amount,
                   int room_id, int next_item_id, int notifications);
  bool log_activity(const std::string& action, const std::string& username, const std::string& detail);
  const std::string& last_error() const { return last_error_; }

private:
  PGconn* connection_ = nullptr;
  std::string last_error_;
  bool query(const char* sql, const std::vector<std::string>& params, bool one_row = false);
  bool transaction(const std::vector<std::pair<std::string,std::vector<std::string>>>& statements);
  bool load_table(data::State& state, const char* sql, int table_type);
};
}
