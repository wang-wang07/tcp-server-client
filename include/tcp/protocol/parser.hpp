#pragma once

#include <optional>
#include <string>

#include "tcp/protocol/command.hpp"


[[nodiscard]] std::optional<Command> parse_command(const std::string& input);
