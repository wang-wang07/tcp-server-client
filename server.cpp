#include <array>
#include <iostream>
#include <netdb.h>
#include <regex>
#include <sys/socket.h>
#include <unistd.h>

int main() {
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

  int server_fd = -1;

  for (addrinfo* addr = results; addr != nullptr; addr = addr->ai_next) {
    server_fd = ::socket(
        addr->ai_family,
        addr->ai_socktype,
        addr->ai_protocol
    );

    if (server_fd == -1) {
      continue;
    }

    if (::bind(server_fd, addr->ai_addr, addr->ai_addrlen) == 0) {
      break;
    }

    ::close(server_fd);
    server_fd = -1;
  }

  ::freeaddrinfo(results);

  if (server_fd == -1) {
    std::cerr << "could not bind to any address\n";
    return 1;
  }

  if (::listen(server_fd, 10) == -1) {
    std::cerr << "listen() failes\n";
    ::close(server_fd);
    return 1;
  }

  while (true) {
    int client_fd = ::accept(server_fd, nullptr, nullptr);
    if (client_fd == -1) {
      std::cerr << "accept() failes\n";
      continue;
    }

    std::cout << "client connected\n";
    std::array<char, 4096> buffer{};

    while (true) {
      ssize_t bytes_received = ::recv(client_fd, buffer.data(), buffer.size(), 0);

      if (bytes_received > 0) {
        ::send(client_fd, buffer.data(), bytes_received, 0);
      }
      else if (bytes_received == 0) {
        std::cout << "client disconnect\n";
      }
      else {
        std::cerr << "recv() failes\n";
        break;
      }
    }

    ::close(client_fd);
  }
}
