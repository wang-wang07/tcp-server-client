#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"

#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <system_error>
#include <sys/epoll.h>
#include <utility>
#include <string_view>
#include <unordered_map>

namespace {
  constexpr int kAcceptBudget = 64;

  void report_error(std::string_view operation, int error_number) {
    const std::error_code error {
      error_number,
      std::generic_category(),
    };

    std::cerr << operation << " failed: " << error.message() << '\n';
  }
}
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
    report_error("setsockopt", errno);
    return 1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(8080);

  if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) == -1) {
    report_error("bind", errno);
    return 1;
  }

  if (::listen(listener.get(), 16)  == -1) {
    report_error("listen", errno);
    return 1;
  }

  const auto nonblocking = net::set_nonblocking(listener.get());

  if (!nonblocking) {
    std::cerr << "could not make listerner nonblocking" << nonblocking.error().message();
    return 1;
  }

  UniqueFd epoll_instance {
    ::epoll_create1(EPOLL_CLOEXEC)
  };

  if (!epoll_instance.valid()) {
    report_error("epoll_create1", errno);
    return 1;
  }

  epoll_event listener_event{};
  listener_event.events = EPOLLIN;
  listener_event.data.fd = listener.get();

  if (::epoll_ctl(epoll_instance.get(),
    EPOLL_CTL_ADD,
    listener.get(),
    &listener_event) == -1) {
      report_error("epoll_ctl", errno);
      return 1;
    }

  std::cout << "listening on port 8080\n";

  std::unordered_map<int, UniqueFd> clients;
  while (true) {
    epoll_event ready_event{};
    int ready_count{};

    do  {
      ready_count = ::epoll_wait(epoll_instance.get(), &ready_event, 1, -1);
    } while (ready_count == -1 && errno == EINTR);

    if (ready_count == -1) {
      report_error("epoll_wait", errno);
      return 1;
    }

    const int ready_fd = ready_event.data.fd;

    if (ready_fd != listener.get()) {
      const auto client_it = clients.find(ready_fd);

      if (client_it == clients.end()) {
        std::cerr << "epoll returned an unknown client descriptor\n";
        return 1;
      }

      std::cout << "client event on fd " << ready_fd << ", flags=" << ready_event.events << '\n';

      //temporary below
      clients.erase(client_it);
      continue;
    }

    if ((ready_event.events & EPOLLIN) == 0) {
      std::cerr << "listener reported an unexpected event\n";
      return 1;
    }

    int attempts = 0;
    while (attempts < kAcceptBudget) {
      const int accepted_fd = ::accept(listener.get(), nullptr, nullptr);

      if (accepted_fd == -1) {
        const int error = errno;
        if (error == EINTR) {
          continue;
        }

        if (error == EAGAIN || error == EWOULDBLOCK) {
          break;
        }

        attempts++;

        switch (error) {
          case ECONNABORTED:
          case ENETDOWN:
          case EPROTO:
          case ENOPROTOOPT:
          case EHOSTDOWN:
          case ENONET:
          case EHOSTUNREACH:
          case EOPNOTSUPP:
          case ENETUNREACH:
            report_error("accept", error);
            continue;

          default:
            report_error("accept", error);
            return 1;
        }
      }

      UniqueFd client{accepted_fd};
      attempts++;

      const auto client_nonblocking = net::set_nonblocking(client.get());

      if (!client_nonblocking) {
        std::cerr << "could not make client nonblocking: "
          << client_nonblocking.error().message() << '\n';
        continue;
      }

      const int client_fd = client.get();

      const auto [client_it, inserted] = clients.try_emplace(client_fd, std::move(client));

      if (!inserted) {
        std::cerr << "accepted descriptor already has an owner\n";
        return 1;
      }

      epoll_event client_event{};
      client_event.events = EPOLLIN | EPOLLRDHUP;
      client_event.data.fd = client_fd;

      if (::epoll_ctl(epoll_instance.get(), EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
        report_error("epoll_ctl client add", errno);
        clients.erase(client_it);
        return 1;
      }

      std::cout << "registered client on fd " << client_fd << ", active=" << clients.size() << '\n';
    }
  }
}
