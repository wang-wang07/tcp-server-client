#include "protocol/framing.hpp"

std::optional<std::string> next_message(std::string& buffer) {
  const std::size_t newline = buffer.find('\n');

  if (newline == std::string::npos) {
    return std::nullopt;
  }

  std::string message = buffer.substr(0, newline);

  buffer.erase(0, newline + 1);

  return message;
}
