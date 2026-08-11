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

  int socket_fd = -1;

  for (addrinfo* addr = results; addr != nullptr; addr = addr->ai_next) {
    socket_fd = ::socket(
        addr->ai_family,
        addr->ai_socktype,
        addr->ai_protocol
    );

    if (socket_fd == -1) {
      continue;
    }
    
    if (::connect(socket_fd, addr->ai_addr, addr->ai_addrlen) == 0) {
      break;
    }

    ::close(socket_fd);
    socket_fd = -1;
  }

  ::freeaddrinfo(results);

  if (socket_fd == -1) {
    std::cerr << "connect() failed\n";
    return 1;
  }

  std::cout << "connected to server\n";

  std::string message;
  std::array<char, 4096> buffer{};

  while (std::getline(std::cin, message)) {
    ::send(socket_fd, message.data(), message.size(), 0);

    ssize_t bytes_received = recv(socket_fd, buffer.data(), buffer.size(), 0);

    if (bytes_received > 0) {
      std::cout << "Server: ";
      std::cout.write(buffer.data(), bytes_received);
      std::cout << '\n';
    }
    else if (bytes_received == 0) {
      std::cout << "server disconnected\n";
      break;
    }
    else {
      std::cerr << "recv() failed\n";
      break;
    }
  }

  ::close(socket_fd);
}
