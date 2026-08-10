#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  // create server fd, ipv4, byte stream socket
  // and check for failure
  int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);

  if (server_fd == -1) {
    std::cerr << "server socket() failed\n";
    return 1;
  }

  //give server adderess
  sockaddr_in address{};

  address.sin_family = AF_INET; //ipv4
  address.sin_addr.s_addr = htonl(INADDR_ANY); // accept connections through an network interface
  address.sin_port = htons(8080); // convert host to network short

  // bind: attatch a socket to address

  if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
    std::cerr << "bind() failed\n";
    ::close(server_fd);
    return 1;
  }

  //listen

  if (::listen(server_fd, 10) == -1) {
    std::cerr << "listen() failed\n";
    ::close(server_fd);
    return 1;
  }

  int client_fd = ::accept(server_fd, nullptr, nullptr);

  while (true) {
    int client_fd = ::accept(server_fd, nullptr, nullptr);

    if (client_fd == -1) {
      std::cerr << "accept() failes\n";
      continue;
    }

    std::cout << "client connects\n";


    std::array<char, 4096> buffer{};

    while(true) {
      ssize_t bytes_received = ::recv(client_fd, buffer.data(), buffer.size(), 0);

      if (bytes_received > 0) {
        ::send(client_fd, buffer.data(), bytes_received, 0);
      }
      else if (bytes_received == 0) {
        std::cout << "client disconnect\n";
        break;
      }
      else {
        std::cerr << "recv() failed\n";
        break;
      }
    }

    ::close(client_fd);
  }


  close(server_fd);
  return 0;
}

