#pragma once
#include <string>
namespace persistence { class PostgresPersistence; }
namespace logger {
void set_database(persistence::PostgresPersistence* database);
void log_activity(const std::string& directory, const std::string& action,
                  const std::string& username, const std::string& detail = "");
}
