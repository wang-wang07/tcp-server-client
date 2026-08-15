#include "tcp/protocol/framing.hpp"

#include <array>
#include <cstddef>
#include <sys/socket.h>
#include <cerrno>

std::optional<std::string> next_message(std::string& buffer) {
  const std::size_t newline = buffer.find('\n');

  if (newline == std::string::npos) {
    return std::nullopt;
  }

  std::string message = buffer.substr(0, newline);

  buffer.erase(0, newline + 1);

  return message;
}

std::optional<std::string> read_message(int fd, std::string& buffer) {
  while (true) {
    if (auto message = next_message(buffer)) {
      return message;
    }

    std::array<char, 4096> chunk{};
    ssize_t bytes_received = ::recv(fd, chunk.data(), chunk.size(), 0);

    if (bytes_received <= 0) {
      return std::nullopt;
    }

    buffer.append(chunk.data(), static_cast<std::size_t>(bytes_received));
  }
}

bool send_all(int fd, std::string_view message) {
  std::size_t total_sent = 0;

  while (total_sent < message.size()) {
    ssize_t bytes_sent = ::send(fd, message.data() + total_sent, message.size() - total_sent, 0);

    if (bytes_sent == -1) {
      if (errno == EINTR) {
        continue;
      }

      return false;
    }

    if (bytes_sent == 0) {
      return false;
    }

    total_sent += static_cast<std::size_t>(bytes_sent);
  }

  return true;
}
