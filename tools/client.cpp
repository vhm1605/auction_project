#include "network_utils.h"
#include "protocol.h"
#include <arpa/inet.h>
#include <algorithm>
#include <vector>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>


struct MenuCommand {
    const char* name;
    const char* label;
    std::vector<const char*> fields;
};
static const std::vector<MenuCommand> commands = {
    {"EXIT", "Exit", {}},
    {"REGISTER", "Register", {"Username", "Password"}},
    {"LOGIN", "Login", {"Username", "Password"}},
    {"LOGOUT", "Logout", {}},
    {"CHANGE_PASSWORD", "Change password", {"Old password", "New password"}},
    {"WHOAMI", "Current account", {}},
    {"CREATE_ROOM", "Create room", {"Room name"}},
    {"LIST_ROOMS", "List rooms", {}},
    {"JOIN_ROOM", "Join room", {"Room ID"}},
    {"LEAVE_ROOM", "Leave room", {}},
    {"ADD_ITEM", "Add item", {"Item name", "Description", "Starting price", "Buy now price"}},
    {"LIST_ITEMS", "List items", {}},
    {"WITHDRAW_ITEM", "Withdraw item", {"Item ID"}},
    {"START", "Start auction", {}},
    {"BID", "Place bid", {"Bid amount"}},
    {"BUY_NOW", "Buy now", {}},
    {"HELP", "Help", {}}
};

/**
 * @brief Print the numbered client menu.
 */
static void print_menu() {
    std::cout << "\n===== AUCTION MENU =====\n";
    for (size_t i = 1; i < commands.size(); ++i)
        std::cout << i << ". " << commands[i].label << '\n';
    std::cout << "0. Exit\nChoice: " << std::flush;
}

/**
 * @brief Collect command fields without blocking socket notifications.
 */
class MenuInput {
    size_t choice = 0, field = 0;
    bool collecting = false;
    std::string command;
public:
    void prompt() const {
        std::cout << (collecting ? commands[choice].fields[field] : "Choice")
                  << ": " << std::flush;
    }
    bool accept(const std::string& line, std::string& output) {
        if (!collecting) {
            int64_t number = 0;
            if (line != "0" && !network::parse_positive(line, number,
                    static_cast<int64_t>(commands.size() - 1))) {
                std::cout << "Invalid choice.\n"; prompt(); return false;
            }
            choice = static_cast<size_t>(number);
            field = 0;
            command = commands[choice].name;
            collecting = !commands[choice].fields.empty();
            if (collecting) { prompt(); return false; }
        } else {
            if (line == "/cancel") {
                collecting = false; command.clear(); print_menu(); return false;
            }
            if (line.empty() || line.find('|') != std::string::npos ||
                line.find('\r') != std::string::npos || line.find('\0') != std::string::npos ||
                command.size() + protocol::encode_field(line).size() + 1 > network::MAX_LINE) {
                std::cout << "Invalid input. Retry or enter /cancel.\n"; prompt(); return false;
            }
            command += ' '; command += protocol::encode_field(line);
            if (++field < commands[choice].fields.size()) { prompt(); return false; }
            collecting = false;
        }
        output = command; command.clear(); return true;
    }
};

// Text-based TCP client. Can receive server notifications while keyboard is idle.
int main(int argc, char* argv[]) {
  std::signal(SIGPIPE, SIG_IGN);
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  int64_t port_num = 8080;
  if (argc > 2 && !network::parse_positive(argv[2], port_num, 65535)) {
    std::cerr << "Invalid port\n"; return 1;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { perror("socket"); return 1; }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_num));
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1 ||
      connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("connect"); close(fd); return 1;
  }
  if (fd >= FD_SETSIZE) { close(fd); return 1; }
  // Avoid indefinite write block for a slow or nonresponsive server.
  timeval send_timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
  std::cout << "Connected to " << host << ':' << port_num << "\n";
  std::cout << "Enter /cancel to cancel current input.\n";
  MenuInput menu;
  print_menu();
  std::string keyboard, incoming;
  bool input_open = true;
  while (true) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    if (input_open) FD_SET(STDIN_FILENO, &readfds);
    int max_fd = std::max(fd, STDIN_FILENO);
    int nready = select(max_fd + 1, &readfds, nullptr, nullptr, nullptr);
    if (nready < 0) { if (errno == EINTR) continue; perror("select"); break; }
    if (FD_ISSET(fd, &readfds)) {
      char bytes[4096];
      ssize_t n = recv(fd, bytes, sizeof(bytes), 0);
      if (n <= 0) { std::cout << "Server closed connection\n"; break; }
      for (ssize_t i = 0; i < n; ++i) {
        if (bytes[i] == '\n') {
          if (incoming.empty() || incoming.back() != '\r') {
            std::cerr << "Invalid server line ending\n"; close(fd); return 1;
          }
          incoming.pop_back();
          std::vector<std::string> fields;
          int64_t numeric = 0;
          if (!protocol::parse(incoming, fields) || fields[0].size() != 3 ||
              !network::parse_positive(fields[0], numeric, 599) || numeric < 100 ||
              std::string(protocol::message_name(static_cast<protocol::Code>(numeric))) == "UNKNOWN_STATUS") {
            std::cerr << "Invalid server message code or fields\n"; close(fd); return 1;
          }
          std::cout << "\n[SERVER] " << fields[0] << ' '
                    << protocol::message_name(static_cast<protocol::Code>(numeric));
          for (std::size_t j = 1; j < fields.size(); ++j)
            std::cout << ' ' << protocol::encode_field(fields[j]);
          std::cout << '\n' << std::flush;
          if (numeric == 131) { close(fd); return 0; }
          incoming.clear();
          if (input_open) menu.prompt();
        } else if ((!incoming.empty() && incoming.back() == '\r') ||
                   (bytes[i] != '\r' && incoming.size() >= network::MAX_LINE)) {
          std::cerr << "Invalid or oversized response\n"; close(fd); return 1;
        } else {
          incoming.push_back(bytes[i]);
        }
      }
    }
    if (input_open && FD_ISSET(STDIN_FILENO, &readfds)) {
      char bytes[1024];
      ssize_t n = read(STDIN_FILENO, bytes, sizeof(bytes));
      if (n <= 0) {
        input_open = false;
        shutdown(fd, SHUT_WR);
      } else {
        for (ssize_t i = 0; i < n; ++i) {
          if (bytes[i] == '\n') {
            if (!keyboard.empty() && keyboard.back() == '\r') keyboard.pop_back();
            std::string command;
            if (menu.accept(keyboard, command)) {
              if (!network::send_all(fd, command.data(), command.size()) ||
                  !network::send_all(fd, protocol::CRLF, 2)) {
                std::cerr << "Send failed\n"; close(fd); return 1;
              }
              if (command == "EXIT") { input_open = false; break; }
              print_menu();
            }
            keyboard.clear();
          } else if (keyboard.size() < network::MAX_LINE) keyboard.push_back(bytes[i]);
          else { std::cerr << "Input line too long\n"; close(fd); return 1; }
        }
      }
    }
  }
  close(fd);
  return 0;
}
