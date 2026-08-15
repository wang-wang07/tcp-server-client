#pragma once

#include <optional>
#include <string>

[[nodiscard]] std::optional<std::string> next_message(std::string& buffer);
[[nodiscard]] std::optional<std::string> read_message(int fd, std::string& buffer);
