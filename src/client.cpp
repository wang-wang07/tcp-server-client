#include "UniqueFd.hpp"
#include "socket.hpp"
#include "protocol/framing.hpp"

#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>

int main() {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* results = nullptr;

  int status = ::getaddrinfo("localhost", "8080", &hints, &results);

  if (status != 0) {
    std::cerr << "getaddrinfo() faild: " << ::gai_strerror(status) << '\n';
    return 1;
  }

  UniqueFd socket_fd;

  for (addrinfo* addr = results; addr != nullptr; addr = addr->ai_next) {
    auto result = net::create_socket(
        addr->ai_family,
        addr->ai_socktype,
        addr->ai_protocol
    );

    if (!result) {
      continue;
    }
    
    socket_fd = std::move(*result);

    if (::connect(socket_fd.get(), addr->ai_addr, addr->ai_addrlen) == 0) {
      break;
    }
  }

  ::freeaddrinfo(results);

  if (!socket_fd.valid()) {
    std::cerr << "connect() failed\n";
    return 1;
  }

  std::cout << "connected to server\n";

  std::string message;
  std::string response_buffer;

  while (std::getline(std::cin, message)) {
    message += '\n';
    ::send(socket_fd.get(), message.data(), message.size(), 0);

    auto response = read_message(socket_fd.get(), response_buffer);

    if (!response) {
      std::cout << "server disconnected\n";
      break;
    }

    std::cout << "Server: " << *response << '\n';
  }
}
