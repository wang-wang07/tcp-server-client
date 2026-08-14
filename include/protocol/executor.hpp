#pragma once

#include "store.hpp"
#include <string>

struct Command;
class KeyValueStore;

[[nodiscard]]
std::string execute_command(const Command& command, KeyValueStore& store);
