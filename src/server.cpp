#include "UniqueFd.hpp"
#include "socket.hpp"

#include "protocol/protocol.hpp"
#include "store.hpp"


#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>


int main() {
  KeyValueStore MASTER_STORE;
  addrinfo hints{};

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo* results = nullptr;

  int status = ::getaddrinfo(nullptr, "8080", &hints, &results);

  if (status != 0) {
    std::cerr << "getaddrinfo() failed: " << ::gai_strerror(status) << '\n';
    return 1;
  }

  UniqueFd server_fd;

  bool bound = false;
  for (addrinfo* addr = results; addr != nullptr; addr = addr->ai_next) {
    auto result = net::create_socket(
        addr->ai_family,
        addr->ai_socktype,
        addr->ai_protocol
    );

    if (!result) {
      continue;
    }

    server_fd = std::move(*result);
    if (::bind(server_fd.get(), addr->ai_addr, addr->ai_addrlen) == 0) {
      bound = true;
      break;
    }
  }

  ::freeaddrinfo(results);

  if (!bound) {
    std::cerr << "could not bind to any address\n";
    return 1;
  }

  if (::listen(server_fd.get(), 10) == -1) {
    std::cerr << "listen() fails\n";
    return 1;
  }

  while (true) {
    UniqueFd client_fd = UniqueFd(::accept(server_fd.get(), nullptr, nullptr));
    if (!client_fd.valid()) {
      std::cerr << "accept() failes\n";
      continue;
    }

    std::cout << "client connected\n";
    std::string message_buffer;
    while (true) {
      char chunk[4096];
      ssize_t bytes_received = ::recv(client_fd.get(), chunk, sizeof(chunk), 0);

      if (bytes_received == 0) {
        std::cout << "client disconnect\n";
        break;
      }

      if (bytes_received < 0) {
        std::cerr << "recv() error\n";
        break;
      }

      message_buffer.append(chunk, bytes_received);

      while (auto message = next_message(message_buffer)) {

        auto command = parse_command(*message);

        if (!command) {
          constexpr std::string_view response = "INVALID COMMAND MESSAGE\n";
          ::send(client_fd.get(), response.data(), response.size(), 0);
          continue;
        }

        auto response = execute_command(*command, MASTER_STORE);
        response += '\n';

        ::send(client_fd.get(), response.data(), response.size(), 0);
      }
    }
  }
}
