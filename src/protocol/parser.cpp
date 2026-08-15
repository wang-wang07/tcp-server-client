#include "tcp/protocol/parser.hpp"

#include <sstream>
#include <string>


std::optional<Command> parse_command(const std::string& input) {
  std::istringstream stream(input);

  std::string operation;

  if (!(stream >> operation)) {
    return std::nullopt;
  }

  if (operation == "SET") {
    std::string key;
    std::string value;
    std::string extra;

    if (!(stream >> key >> value)) {
      return std::nullopt;
    }

    if (stream >> extra) {
      return std::nullopt;
    }

    return Command {
      CommandType::Set,
      key,
      value
    };
  }

  if (operation == "GET") {
    std::string key;
    std::string extra;

    if (!(stream >> key)) {
      return std::nullopt;
    }

    if (stream >> extra)  {
      return std::nullopt;
    }

    return Command {
      CommandType::Get,
      key,
      ""
    };
  }

  if (operation == "DELETE") {
    std::string key;
    std::string extra;

    if (!(stream >> key)) {
      return std::nullopt;
    }

    if (stream >> extra) {
      return std::nullopt;
    }

    return Command {
      CommandType::Erase,
      key,
      ""
    };
  }

  if (operation == "EXISTS") {
    std::string key;
    std::string extra;

    if (!(stream >> key)) {
      return std::nullopt;
    }

    if (stream >> extra) {
      return std::nullopt;
    }

    return Command {
      CommandType::Exists,
      key,
      ""
    };
  }

  if (operation == "COUNT") {
    std::string extra;
    if (stream >> extra) {
      return std::nullopt;
    }

    return Command {
      CommandType::Count,
      "",
      "",
    };
  }

  return std::nullopt;
}
