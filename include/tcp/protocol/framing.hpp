#pragma once

#include <optional>
#include <string>
#include <string_view>

[[nodiscard]] std::optional<std::string> next_message(std::string& buffer);
[[nodiscard]] std::optional<std::string> read_message(int fd, std::string& buffer);
[[nodiscard]] bool send_all(int fd, std::string_view message);
