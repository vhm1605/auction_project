#include "persistence_pg.h"
#include "network_utils.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

namespace {
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
std::string num(int64_t n) { return std::to_string(n); }

bool parse_int(const char* text, int64_t& value, int64_t min, int64_t max) {
  if (!text || !*text) return false;
  errno = 0;
  char* end = nullptr;
  long long result = std::strtoll(text, &end, 10);
  if (errno || !end || *end != '\0' || result < min || result > max) return false;
  value = static_cast<int64_t>(result);
  return true;
}
bool query_result(PGconn* connection, const std::string& sql,
                  const std::vector<std::string>& params, Result& result) {
  std::vector<const char*> values;
  values.reserve(params.size());
  for (const auto& p : params) values.push_back(p.c_str());
  result.reset(PQexecParams(connection, sql.c_str(), static_cast<int>(values.size()),
                            nullptr, values.empty() ? nullptr : values.data(),
                            nullptr, nullptr, 0));
  if (!result) return false;
  ExecStatusType status = PQresultStatus(result.get());
  return status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK;
}
}

namespace persistence {
PostgresPersistence::~PostgresPersistence() { if (connection_) PQfinish(connection_); }

bool PostgresPersistence::connect_db(const std::string& connection) {
  if (connection_) PQfinish(connection_);
  connection_ = PQconnectdb(connection.c_str());
  if (!connection_ || PQstatus(connection_) != CONNECTION_OK) {
    last_error_ = connection_ ? PQerrorMessage(connection_) : "Unable to allocate PostgreSQL connection";
    return false;
  }
  // Require UTF-8 text on the wire; keep DB client encoding consistent.
  if (PQsetClientEncoding(connection_, "UTF8") != 0) {
    last_error_ = PQerrorMessage(connection_);
    return false;
  }
  return true;
}

bool PostgresPersistence::query(const char* sql, const std::vector<std::string>& params, bool one_row) {
  if (!connection_ || PQstatus(connection_) != CONNECTION_OK) {
    last_error_ = "PostgreSQL connection is not ready";
    return false;
  }
  Result r(nullptr, &PQclear);
  if (!query_result(connection_, sql, params, r)) {
    last_error_ = PQerrorMessage(connection_);
    return false;
  }
  if (one_row && ((PQresultStatus(r.get()) == PGRES_TUPLES_OK && PQntuples(r.get()) != 1) ||
                   (PQresultStatus(r.get()) == PGRES_COMMAND_OK &&
                    std::string(PQcmdTuples(r.get())) != "1"))) {
    last_error_ = "No row matched or more than one row matched";
    return false;
  }
  return true;
}

bool PostgresPersistence::transaction(
    const std::vector<std::pair<std::string,std::vector<std::string>>>& statements) {
  if (!query("BEGIN", {})) return false;
  for (const auto& stmt : statements) {
    if (!query(stmt.first.c_str(), stmt.second, true)) {
      query("ROLLBACK", {});
      return false;
    }
  }
  if (!query("COMMIT", {})) {
    query("ROLLBACK", {});
    return false;
  }
  return true;
}

bool PostgresPersistence::load_table(data::State& state, const char* sql, int type) {
  Result r(nullptr, &PQclear);
  if (!connection_ || !query_result(connection_, sql, {}, r) || PQresultStatus(r.get()) != PGRES_TUPLES_OK) {
    last_error_ = connection_ ? PQerrorMessage(connection_) : "Not connected";
    return false;
  }
  int rows = PQntuples(r.get());
  for (int row = 0; row < rows; ++row) {
    auto get = [&](int col) -> std::string { return PQgetisnull(r.get(),row,col) ? "" : PQgetvalue(r.get(),row,col); };
    if (type == 0) {
      data::User u; u.username=get(0); u.salt_hex=get(1); u.password_hash_hex=get(2);
      if (!network::valid_username(u.username) || u.salt_hex.size()!=32 || u.password_hash_hex.size()!=64 ||
          state.users.size()>=1000) return false;
      std::string key = u.username;
      if (!state.users.emplace(key, std::move(u)).second) return false;
    } else if (type == 1) {
      int64_t id=0;
      if (!parse_int(PQgetvalue(r.get(),row,0),id,1,999999) || state.rooms.size()>=100 ||
          !network::valid_text(get(1),64) || !state.users.count(get(2))) return false;
      data::Room room; room.id=static_cast<int>(id); room.name=get(1); room.owner=get(2);
      if (!state.rooms.emplace(room.id,std::move(room)).second) return false;
      state.next_room_id=std::max(state.next_room_id,static_cast<int>(id)+1);
    } else {
      // Items may be loaded from multiple historical auctions after a restart.
      int64_t id=0,rid=0,start=0,buy=0,highest=0,final=0,notices=0,deadline=0;
      if (!parse_int(PQgetvalue(r.get(),row,0),id,1,999999) ||
          !parse_int(PQgetvalue(r.get(),row,1),rid,1,999999) ||
          !parse_int(PQgetvalue(r.get(),row,5),start,1,network::MAX_PRICE) ||
          !parse_int(PQgetvalue(r.get(),row,6),buy,1,network::MAX_PRICE) ||
          !parse_int(PQgetvalue(r.get(),row,7),highest,1,network::MAX_PRICE) ||
          !parse_int(PQgetvalue(r.get(),row,10),final,0,network::MAX_PRICE) ||
          !parse_int(PQgetvalue(r.get(),row,12),notices,0,3) ||
          !parse_int(PQgetvalue(r.get(),row,13),deadline,0,std::numeric_limits<int64_t>::max())) return false;
      auto room_it=state.rooms.find(static_cast<int>(rid));
      if (room_it==state.rooms.end() || room_it->second.items.size()>=100 ||
          !network::valid_text(get(2),80) || !network::valid_text(get(3),240) ||
          !state.users.count(get(4)) || (!get(8).empty() && !state.users.count(get(8))) ||
          (!get(9).empty() && !state.users.count(get(9))) || start>network::MAX_PRICE-network::MIN_INCREMENT ||
          buy<start+network::MIN_INCREMENT || highest<start) return false;
      data::Item item; item.id=static_cast<int>(id); item.name=get(2); item.description=get(3);
      item.seller=get(4); item.start_price=start; item.buy_now_price=buy; item.highest_price=highest;
      item.highest_bidder=get(8); item.buyer=get(9); item.final_price=final;
      std::string status=get(11);
      if (status=="QUEUED") item.status=data::Status::QUEUED;
      else if (status=="ACTIVE") item.status=data::Status::ACTIVE;
      else if (status=="SOLD") item.status=data::Status::SOLD;
      else if (status=="WITHDRAWN") item.status=data::Status::WITHDRAWN;
      else return false;
      item.notifications=static_cast<int>(notices);
      if (item.status==data::Status::ACTIVE) {
        if (room_it->second.active_id!=0) return false;
        room_it->second.active_id=item.id;
        if (!item.highest_bidder.empty() && deadline == 0) return false;
        if (deadline>0 && !item.highest_bidder.empty()) {
          using SysClock=std::chrono::system_clock;
          int64_t current=std::chrono::duration_cast<std::chrono::milliseconds>(SysClock::now().time_since_epoch()).count();
          int64_t wait=deadline>current?std::min<int64_t>(deadline-current,3600000):0;
          item.next_notification=std::chrono::steady_clock::now()+std::chrono::milliseconds(wait);
        }
      }
      room_it->second.items.push_back(std::move(item));
      state.next_item_id=std::max(state.next_item_id,static_cast<int>(id)+1);
    }
  }
  return true;
}

bool PostgresPersistence::load_state(data::State& state) {
  state=data::State{};
  if (!load_table(state,"SELECT username,salt_hex,password_hash_hex FROM users ORDER BY username",0) ||
      !load_table(state,"SELECT id,name,owner FROM rooms ORDER BY id",1) ||
      !load_table(state,"SELECT id,room_id,name,description,seller,start_price,buy_now_price,highest_price,"
                       "highest_bidder,buyer,final_price,status,notifications,next_notice_ms FROM items ORDER BY id",2)) {
    if (last_error_.empty()) last_error_="Invalid persistent data, or schema is missing";
    return false;
  }
  return true;
}

bool PostgresPersistence::add_user(const data::User& user) {
  return query("INSERT INTO users(username,salt_hex,password_hash_hex) VALUES($1,$2,$3)",
               {user.username,user.salt_hex,user.password_hash_hex},true);
}
bool PostgresPersistence::create_room(const data::Room& room) {
  return query("INSERT INTO rooms(id,name,owner) VALUES($1,$2,$3)",
               {num(room.id),room.name,room.owner},true);
}
bool PostgresPersistence::create_item(int room_id,const data::Item& item) {
  return query("INSERT INTO items(id,room_id,name,description,seller,start_price,buy_now_price,highest_price) "
               "VALUES($1,$2,$3,$4,$5,$6,$7,$8)",
               {num(item.id),num(room_id),item.name,item.description,item.seller,num(item.start_price),
                num(item.buy_now_price),num(item.highest_price)},true);
}
bool PostgresPersistence::withdraw_item(int room_id,int item_id) {
  return query("UPDATE items SET status='WITHDRAWN' WHERE id=$1 AND room_id=$2 AND status='QUEUED'",
               {num(item_id),num(room_id)},true);
}
bool PostgresPersistence::activate_item(int room_id,int item_id) {
  return query("UPDATE items SET status='ACTIVE',highest_price=start_price,notifications=0,next_notice_ms=0 "
               "WHERE id=$1 AND room_id=$2 AND status='QUEUED'",
               {num(item_id),num(room_id)},true);
}
bool PostgresPersistence::save_bid(int item_id,const std::string& bidder,int64_t amount,int64_t deadline) {
  return transaction({
    {"UPDATE items SET highest_price=$2,highest_bidder=$3,notifications=0,next_notice_ms=$4 "
     "WHERE id=$1 AND status='ACTIVE' AND highest_price<=$2-10000 AND buy_now_price>$2",
     {num(item_id),num(amount),bidder,num(deadline)}},
    {"INSERT INTO bids(item_id,bidder,amount) VALUES($1,$2,$3)",{num(item_id),bidder,num(amount)}}
  });
}
bool PostgresPersistence::save_notice(int item_id,int count,int64_t deadline) {
  return query("UPDATE items SET notifications=$2,next_notice_ms=$3 WHERE id=$1 AND status='ACTIVE'",
               {num(item_id),num(count),num(deadline)},true);
}
bool PostgresPersistence::finish_item(int sold_id,const std::string& buyer,int64_t amount,
                                      int room_id,int next_item_id,int notices) {
  std::vector<std::pair<std::string,std::vector<std::string>>> stmts{
    {"UPDATE items SET status='SOLD',buyer=$2,final_price=$3,notifications=$4,next_notice_ms=0,sold_at=now() "
     "WHERE id=$1 AND room_id=$5 AND status='ACTIVE'",
     {num(sold_id),buyer,num(amount),num(notices),num(room_id)}}
  };
  if (next_item_id>0) {
    stmts.push_back({"UPDATE items SET status='ACTIVE',highest_price=start_price,notifications=0,next_notice_ms=0 "
                     "WHERE id=$1 AND room_id=$2 AND status='QUEUED'",
                     {num(next_item_id),num(room_id)}});
  }
  return transaction(stmts);
}
bool PostgresPersistence::log_activity(const std::string& action,const std::string& username,const std::string& detail) {
  return query("INSERT INTO activity_logs(action,username,detail) VALUES($1,$2,$3)",
               {action,username,detail},true);
}
}
