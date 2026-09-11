#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"
#include "tcp/server/connection.hpp"
#include "tcp/store.hpp"

#include <asm-generic/socket.h>
#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <system_error>
#include <sys/epoll.h>
#include <utility>
#include <string_view>
#include <unordered_map>
#include <cstddef>

namespace {
  using tcp::server::Connection;
  using tcp::server::InputResult;

  using Clients = std::unordered_map<int, Connection>;

  constexpr int kAcceptBudget = 64;
  constexpr int kReadCallBudget = 16;
  constexpr std::size_t kReadBufferSize = 4096;

  void report_error(std::string_view operation, int error_number) {
    const std::error_code error {
      error_number,
      std::generic_category(),
    };

    std::cerr << operation << " failed: " << error.message() << '\n';
  }

  void retire_client(int epoll_fd, Clients& clients, Clients::iterator client_it, std::string_view reason) {
    const int fd = client_it->first;

    if (::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
      const int error = errno;
      report_error("epoll_ctl client delete", error);
    }

    clients.erase(client_it);

    std::cout << "closed fd=" << fd << " reason=" << reason << " active=" << clients.size() << '\n';
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

  KeyValueStore store;
  Clients clients;
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

      Connection& connection = client_it->second;

      char buffer[kReadBufferSize];
      std::size_t bytes_this_pass = 0;
      const char* close_reason = nullptr;

      const bool read_ready = (ready_event.events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) != 0;

      for (int attempt = 0; read_ready && !connection.read_eof && attempt < kReadCallBudget; attempt++) {
        const ssize_t received = ::recv(ready_fd, buffer, sizeof(buffer), 0);

        if (received > 0) {
          const auto count = static_cast<std::size_t>(received);
          bytes_this_pass += count;

          const auto result = connection.process_input(std::string_view{buffer, count}, store);

          std::cout << "buffered fd=" << ready_fd
                    << " input_pending="
                    << connection.input_buffer.size() - connection.input_cursor
                    << " output_bytes=" << connection.output_buffer.size()
                    << " keys=" << store.count() << '\n';
          if (result == InputResult::InputLimit) {
            close_reason = "input buffer limit";
            break;
          }

          if (result == InputResult::OutputLimit) {
            close_reason = "output buffer limit";
            break;
          }

          continue;
        }

        if (received == 0) {
          connection.read_eof = true;
          connection.input_buffer.clear();
          connection.input_cursor = 0;
          break;
        }

        const int error = errno;
        if (error == EINTR) {
          continue;
        }

        if (error == EAGAIN || error == EWOULDBLOCK) {
          // Do not repeatedly wake on a terminal event that cannot progress.
          if ((ready_event.events & (EPOLLERR | EPOLLHUP)) != 0) {
            close_reason = "terminal event wit hno readable bytes";
          }
          break;
        }

        report_error("recv", error);
        close_reason = "receive error";
        break;
      }

      if (bytes_this_pass != 0) {
        std::cout << "received fd=" << ready_fd << " bytes=" << bytes_this_pass << '\n';
      }

      if (close_reason == nullptr && connection.read_eof &&  (ready_event.events & (EPOLLERR | EPOLLHUP)) != 0) {
        close_reason = "terminal even after read EOF";
      }

      if (close_reason == nullptr && connection.has_pending_output()) {
        const auto flushed = connection.flush_output();
        if (!flushed) {
          report_error("send", flushed.error().value());
          close_reason = "sned error";
        }
      }

      if (close_reason == nullptr && connection.read_eof && !connection.has_pending_output()) {
        close_reason = "read EOF and output drained";
      }

      if (close_reason != nullptr) {
        if ((ready_event.events & EPOLLERR) != 0) {
          int socket_error = 0;
          socklen_t option_size = sizeof(socket_error);
          if (::getsockopt(ready_fd, SOL_SOCKET, SO_ERROR, &socket_error, &option_size) == -1) {
            const int error = errno;
            report_error("getsockopt SO_ERROR", error);
            close_reason = "socket error inspection failed";
          } else if (socket_error != 0) {
            report_error("pending socket error", socket_error);
            close_reason = "socket error";
          } else {
            std::cout << "SO_ERROR fd=" << ready_fd << " value=0\n";
          }
        }

        retire_client(epoll_instance.get(), clients, client_it, close_reason);

        continue;
      }

      epoll_event client_event{};
      client_event.events = 0;
      client_event.data.fd = ready_fd;

      if (!connection.read_eof) {
        client_event.events |= EPOLLIN | EPOLLRDHUP;
      }

      if (connection.has_pending_output()) {
        client_event.events |= EPOLLOUT;
      }

      if (::epoll_ctl(epoll_instance.get(), EPOLL_CTL_MOD, ready_fd, &client_event) == -1) {
        const int error = errno;
        report_error("epoll_ctl client modify", error);

        retire_client(epoll_instance.get(), clients, client_it, "interest update failed");
      }
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
