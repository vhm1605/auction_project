#include "persistence.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <array>

namespace {
std::string hex(const unsigned char* input, size_t n) {
  static const char* digits = "0123456789abcdef";
  std::string result;
  result.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    result += digits[input[i] >> 4];
    result += digits[input[i] & 15];
  }
  return result;
}
bool unhex(const std::string& s, unsigned char* out, size_t bytes) {
  if (s.size() != bytes * 2) return false;
  for (size_t i = 0; i < bytes; ++i) {
    auto digit = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };
    int a = digit(s[2*i]), b = digit(s[2*i+1]);
    if (a < 0 || b < 0) return false;
    out[i] = static_cast<unsigned char>(a * 16 + b);
  }
  return true;
}
constexpr int ITERATIONS = 120000;
}

namespace persistence {
bool make_password_hash(const std::string& password, std::string& salt, std::string& hash) {
  std::array<unsigned char, 16> random_salt{};
  std::array<unsigned char, 32> derived{};
  if (RAND_bytes(random_salt.data(), static_cast<int>(random_salt.size())) != 1) return false;
  if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
       random_salt.data(), static_cast<int>(random_salt.size()), ITERATIONS,
       EVP_sha256(), static_cast<int>(derived.size()), derived.data()) != 1) return false;
  salt = hex(random_salt.data(), random_salt.size());
  hash = hex(derived.data(), derived.size());
  OPENSSL_cleanse(derived.data(), derived.size());
  return true;
}

bool password_matches(const std::string& password, const std::string& salt, const std::string& expected_hash) {
  std::array<unsigned char, 16> salt_bytes{};
  std::array<unsigned char, 32> expected{}, actual{};
  if (!unhex(salt, salt_bytes.data(), salt_bytes.size()) ||
      !unhex(expected_hash, expected.data(), expected.size())) return false;
  if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
       salt_bytes.data(), static_cast<int>(salt_bytes.size()), ITERATIONS,
       EVP_sha256(), static_cast<int>(actual.size()), actual.data()) != 1) return false;
  bool match = CRYPTO_memcmp(actual.data(), expected.data(), actual.size()) == 0;
  OPENSSL_cleanse(actual.data(), actual.size());
  return match;
}


}
