#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>


int main() {
  int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);

  sockaddr_in server_address{};
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(8080);

  if (::inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) != 1) {
    std::cerr << "Invalid IP addres\n";
    ::close(socket_fd);
    return 1;
  }

  if (::connect(socket_fd, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == -1) {

    std::cerr << "connect() failed\n";
    ::close(socket_fd);
    return 1;
  }

  std::cout << "connected to server\n";

  std::string message;
  std::array<char, 4096> buffer{};

  while (std::getline(std::cin, message)) {
    ::send(socket_fd, message.data(), message.size(), 0);
    ssize_t bytes_received = ::recv(socket_fd, buffer.data(), buffer.size(), 0);

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
