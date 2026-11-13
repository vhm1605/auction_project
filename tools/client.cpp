#include "network_utils.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// Text-based TCP client. Can receive server notifications while keyboard is idle.
int main(int argc, char* argv[]) {
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
  // Avoid indefinite write block for a slow or nonresponsive server.
  timeval send_timeout{3, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
  std::cout << "Connected to " << host << ':' << port_num << "\n"
            << "Type HELP, then commands like REGISTER|alice|Password123\n"
            << "CREATE_ROOM|Room A, ADD_ITEM|Laptop|Used|100000|200000, START, BID|110000\n"
            << "Messages from server appear automatically.\n" << std::flush;
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
        if (bytes[i] == '\n') { std::cout << "[SERVER] " << incoming << '\n' << std::flush; incoming.clear(); }
        else if (incoming.size() < network::MAX_LINE) incoming.push_back(bytes[i]);
        else { std::cerr << "Oversized response\n"; close(fd); return 1; }
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
            if (!keyboard.empty()) {
              if (!network::send_all(fd, keyboard.data(), keyboard.size()) ||
                  !network::send_all(fd, "\n", 1)) {
                std::cerr << "Send failed\n"; close(fd); return 1;
              }
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
