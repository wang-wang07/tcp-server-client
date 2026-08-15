#include "tcp/protocol/executor.hpp"

#include "tcp/protocol/command.hpp"
#include "tcp/store.hpp"

std::string execute_command(const Command& command, KeyValueStore& store) {
  switch (command.type) {
    case CommandType::Set:
    {
      store.set(command.key, command.value);
      return "OK";
    }

    case CommandType::Get:
    {
      auto value = store.get(command.key);

      if (!value) {
        return "NOT_FOUND";
      }

      return *value;
    }

    case CommandType::Erase:
    {
      if (store.erase(command.key)) {
        return "OK";
      }

      return "NOT_FOUND";
    }

    case CommandType::Exists:
    {
      return store.exists(command.key) ? "1" : "0";
    }

    case CommandType::Count:
    {
      return std::to_string(store.count());
    }
  }

  return "ERROR";
}
