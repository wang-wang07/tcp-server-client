#pragma once

#include <optional>
#include <string>

[[nodiscard]] std::optional<std::string> next_message(std::string& buffer);

