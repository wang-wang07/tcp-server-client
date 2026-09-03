#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"

#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <system_error>
#include <utility>

int main() {
  auto socket_result = net::create_socket(AF_INET, SOCK_STREAM, 0);

  if (!socket_result) {
    std::cerr << "socket failed: " << socket_result.error().message() << '\n';
    return 1;
  }

  UniqueFd listener = std::move(*socket_result);

  const int reuse_address = 1;

  if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) == -1) {
    const std::error_code error {
      errno,
      std::generic_category()
    };

    std::cerr << "setsockopt failed: " << error.message() << '\n';
    return 1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(8080);

  if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), 
             sizeof(address)) == -1) {
    const std::error_code error {
      errno,
      std::generic_category(),
    };

    std::cerr << "bind failed: " << error.message() << '\n';
    return 1;
  }

  if (::listen(listener.get(), 16)  == -1) {
    const std::error_code error {
      errno,
      std::generic_category(),
    };

    std::cerr << "listen failed: " << error.message() << '\n';
    return 1;
  }

  std::cout << "listening on port 8080\n";
  UniqueFd client{::accept(listener.get(), nullptr, nullptr)};

  if (!client.valid()) {
    const std::error_code error {
      errno,
      std::generic_category(),
    };

    std::cerr << "accept failed: " << error.message() << '\n';
    return 1;
  }

  std::cout << "accepted client on fd " << client.get() << '\n';
}
