#include "tcp/server/client_handler.hpp"
#include "tcp/protocol/framing.hpp"
#include "tcp/protocol/parser.hpp"
#include "tcp/protocol/protocol.hpp"

#include <iostream>
#include <string>

namespace tcp::server {
  void handle_client(UniqueFd clientFd, KeyValueStore &store) {
    std::cout << "client connected\n";

    std::string message_buffer;

    while(true) {
      const auto message = read_message(clientFd.get(), message_buffer);

      if (!message) {
        std::cout << "client disconnected\n";
        return;
      }

      const auto command = parse_command(*message);

      if (!command) {
        if (!send_all(clientFd.get(), "INVALID COMMAND MESSAGE\n")) {
          std::cerr << "send failed\n";
          return;
        }
        continue;
      }

      std::string response = execute_command(*command, store);
      response += '\n';

      if (!send_all(clientFd.get(), response)) {
        std::cerr << "send failed\n";
        return;
      }
    }
  }
}
