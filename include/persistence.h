#pragma once
#include <string>
namespace persistence {
// PBKDF2-HMAC-SHA256; authentication data is stored in PostgreSQL.
bool make_password_hash(const std::string& password, std::string& salt, std::string& hash);
bool password_matches(const std::string& password, const std::string& salt, const std::string& expected_hash);
}
