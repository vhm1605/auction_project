#include "logger.h"
#include "persistence_pg.h"
#include <iostream>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

namespace logger {
static persistence::PostgresPersistence* database_ = nullptr;
void set_database(persistence::PostgresPersistence* database) { database_ = database; }
void log_activity(const std::string& directory, const std::string& action,
                  const std::string& username, const std::string& detail) {
  // Audit DB is best effort; don't echo DB credentials or any client passwords.
  if (database_ && !database_->log_activity(action, username, detail))
    std::cerr << "Warning: activity log could not be written to PostgreSQL\n";
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  if (!localtime_r(&t, &tm)) return;
  char date[32];
  if (std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm) == 0) return;
  std::string line = std::string(date) + " | " + action + " | " + username + " | " + detail + "\n";
  // No passwords should ever be passed to this function.
  int fd = open((directory + "/activity.log").c_str(),
                O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return;
  fchmod(fd, 0600);
  size_t off = 0;
  while (off < line.size()) {
    ssize_t written = write(fd, line.data() + off, line.size() - off);
    if (written > 0) off += static_cast<size_t>(written);
    else if (written < 0 && errno == EINTR) continue;
    else break;
  }
  close(fd);
}
}
