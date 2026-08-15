#include "protocol/framing.hpp"

#include <array>
#include <sys/socket.h>

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

    buffer.append(chunk.data(), bytes_received);
  }
}
