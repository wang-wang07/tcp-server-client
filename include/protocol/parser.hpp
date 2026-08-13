#pragma once

#include <optional>
#include <string>

#include "command.hpp"


[[nodiscard]] std::optional<Command> parse_command(const std::string& input);
