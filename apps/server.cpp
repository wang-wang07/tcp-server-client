#include "tcp/net/event_poller.hpp"
#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"
#include "tcp/server/connection.hpp"
#include "tcp/store.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace {
using tcp::server::Connection;
using tcp::server::InputResult;
using Clients = std::unordered_map<int, Connection>;
constexpr std::size_t kMaxClients = 128; // Live owners, not the listen backlog.
constexpr auto kIdleTimeout = std::chrono::seconds{30};
constexpr int kTimerPollMs = 1000; // Wake even if no socket becomes ready.
constexpr bool kLogConnections = false; // Avoid routine logging in measurements.
constexpr int kAcceptBudget = 64;
constexpr int kReadCallBudget = 16;
constexpr std::size_t kReadBufferSize = 4096;

void report_error(std::string_view operation, int error_number) {
  const std::error_code error{error_number, std::generic_category()};
  std::cerr << operation << " failed: " << error.message() << '\n';
}

// Remove registration before erasing the sole owner. The caller must not use
// its iterator or Connection reference after this function returns.
void retire_client(net::EventPoller& poller, Clients& clients,
                   Clients::iterator client_it, std::string_view reason) {
  const int fd = client_it->first; // Save identity before invalidating the entry.
  const auto removed = poller.remove(fd);
  // A failed DEL is diagnostic; closing the sole owner still removes the socket
  // from epoll. There are no duplicated client descriptors in this server.
  if (!removed) {
    report_error("poller remove", removed.error().value());
  }
  clients.erase(client_it);
  // Log only when debugging lifecycle behavior. No erased reference is used.
  if (kLogConnections) {
    std::cout << "closed fd=" << fd << " reason=" << reason
              << " active=" << clients.size() << '\n';
  }
}

// Borrow one connection for a bounded receive/execute/send visit. A default
// event resumes owned input without claiming any kernel read readiness.
// Return a stable string literal to request retirement, or nullptr to retain it.
const char* service_client(net::EventPoller& poller, Connection& connection,
                           net::ReadyEvent event, KeyValueStore& store) {
  const int fd = connection.socket.get(); // Borrow identity; do not close here.
  // Socket errors are terminal. SO_ERROR provides additional diagnostics and
  // may already be zero if another syscall consumed the pending error.
  if (event.error) {
    int socket_error = 0;
    socklen_t size = sizeof(socket_error);
    // -1 reports failure of inspection itself; otherwise inspect its output.
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &size) == -1) {
      report_error("getsockopt SO_ERROR", errno);
    } else if (socket_error != 0) {
      // Nonzero SO_ERROR identifies the underlying socket failure.
      report_error("pending socket error", socket_error);
    }
    return "socket error event";
  }
  // Full hangup is terminal; readable half-close alone takes the recv/EOF path.
  if (event.hangup) {
    return "full hangup";
  }

  // Charge every syscall attempt, including EINTR. Stop admission when a
  // complete frame appears, capacity runs out, EOF occurs, or output pauses.
  for (int attempt = 0;
       attempt < kReadCallBudget && event.readable && connection.can_read(); ++attempt) {
    char buffer[kReadBufferSize];
    const auto room = Connection::kMaxInputBytes - connection.pending_input_bytes();
    const auto requested = std::min(room, sizeof(buffer));
    // can_read() ensures requested is positive. Receive only what can fit,
    // rather than consuming excess TCP bytes and then rejecting valid traffic.
    const ssize_t received = ::recv(fd, buffer, requested, 0);
    // Positive results initialize exactly this many bytes of the stack buffer.
    if (received > 0) {
      connection.last_progress = Connection::Clock::now();
      const auto result = connection.append_input(
          std::string_view{buffer, static_cast<std::size_t>(received)});
      // Capacity calculation should prevent this failure; retain the invariant
      // check in case later admission changes break the relationship.
      if (result != InputResult::Ok) {
        return "input buffer limit";
      }
      continue; // Reevaluate can_read() before admitting another chunk.
    }
    // EOF ends reads, not already-received commands or already-queued replies.
    if (received == 0) {
      connection.read_eof = true;
      break;
    }
    const int error = errno;
    // Interruption moved no bytes; the next attempt still consumes budget.
    if (error == EINTR) {
      continue;
    }
    // No available bytes: preserve the connection and yield, rather than spin.
    if (error == EAGAIN || error == EWOULDBLOCK) {
      break;
    }
    report_error("recv", error);
    return "receive error";
  }

  // One invocation grants one command budget, regardless of recv call count.
  const auto result = connection.process_input("", store);
  // A full unterminated frame cannot be completed within the input allowance.
  if (result == InputResult::InputLimit) {
    return "input buffer limit";
  }
  // A defensive output limit failure is terminal; mutations are not rolled back.
  if (result == InputResult::OutputLimit) {
    return "response buffer limit";
  }
  // Only an unsent suffix needs a bounded flush attempt.
  if (connection.has_pending_output()) {
    const auto flushed = connection.flush_output();
    // Negating expected tests for an ERROR, not its contained boolean value.
    // A contained false simply leaves work pending for a writable event.
    if (!flushed) {
      report_error("send", flushed.error().value());
      return "send error";
    }
  }
  connection.update_backpressure(); // Sending may have crossed low-water.

  // Preserve complete frames after EOF. Only a final incomplete tail is dropped.
  if (connection.read_eof && !connection.has_complete_frame()) {
    connection.input_buffer.clear();
    connection.input_cursor = 0;
    // No more input can arrive; retire only after all queued output drains.
    if (!connection.has_pending_output()) {
      return "read EOF and output drained";
    }
  }
  const auto modified = poller.modify(fd, {
      .read = connection.can_read(),
      .write = connection.has_pending_output(),
  });
  // An incorrect interest mask would strand work or spin. Retire on failure.
  if (!modified) {
    report_error("poller modify", modified.error().value());
    return "interest update failed";
  }
  return nullptr;
}
}

int main() {
  auto socket_result = net::create_socket(AF_INET, SOCK_STREAM, 0);
  // No listener exists on failure; do not dereference the expected result.
  if (!socket_result) {
    report_error("socket", socket_result.error().value());
    return 1;
  }
  UniqueFd listener = std::move(*socket_result);

  const int reuse_address = 1;
  // Configure address reuse before bind; -1 reports failure through errno.
  if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) == -1) {
    report_error("setsockopt", errno);
    return 1;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY); // Bind all IPv4 interfaces.
  address.sin_port = htons(8080); // Socket addresses use network byte order.
  // bind associates the socket with its local address; failure prevents startup.
  if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) == -1) {
    report_error("bind", errno);
    return 1;
  }
  // Backlog 16 bounds the requested pending-accept queue, not retained clients.
  if (::listen(listener.get(), 16) == -1) {
    report_error("listen", errno);
    return 1;
  }
  const auto nonblocking = net::set_nonblocking(listener.get());
  // Without O_NONBLOCK, draining accepts could block the only event-loop thread.
  if (!nonblocking) {
    report_error("listener nonblocking", nonblocking.error().value());
    return 1;
  }

  auto poller_result = net::EventPoller::create();
  // A failed factory has no usable owner to transfer.
  if (!poller_result) {
    report_error("poller create", poller_result.error().value());
    return 1;
  }
  auto poller = std::move(*poller_result);
  const auto registered = poller.add(listener.get(), {.read = true});
  // Listener registration is required for all future accepts.
  if (!registered) {
    report_error("poller listener add", registered.error().value());
    return 1;
  }
  std::cout << "listening on port 8080\n";

  KeyValueStore store; // One shared in-memory state machine, on this thread only.
  Clients clients; // Outlives each event; each entry owns its socket and buffers.
  while (true) {
    bool runnable_remains = false; // True forbids sleeping on buffered commands.
    // At most 128 entries receive one visit each before kernel-event dispatch.
    for (auto it = clients.begin(); it != clients.end();) {
      const auto current = it++; // Advance before possibly erasing this entry.
      Connection& connection = current->second;
      // Elapsed monotonic time counts actual socket progress, not readiness.
      if (Connection::Clock::now() - connection.last_progress >= kIdleTimeout) {
        retire_client(poller, clients, current, "idle timeout");
        continue; // connection was destroyed; never inspect it again.
      }
      // Only complete, unpaused frames can make user-space progress now.
      if (connection.has_runnable_input()) {
        const char* reason = service_client(poller, connection, {}, store);
        // A reason requests erasure after the borrowed reference use ends.
        if (reason != nullptr) {
          retire_client(poller, clients, current, reason);
          continue;
        }
      }
      runnable_remains = runnable_remains || connection.has_runnable_input();
    }

    // Zero polls without sleeping; otherwise finite waiting services timers.
    const int timeout_ms = runnable_remains ? 0 : kTimerPollMs;
    const auto waited = poller.wait_one(timeout_ms);
    // The outer expected distinguishes a syscall error from a successful wait.
    if (!waited) {
      // Return to the OUTER loop so repeated interruptions cannot skip timers.
      if (waited.error().value() == EINTR) {
        continue;
      }
      report_error("poller wait", waited.error().value());
      return 1;
    }
    // The inner optional is empty on timeout; no event identity is valid then.
    if (!waited->has_value()) {
      continue;
    }
    const net::ReadyEvent ready_event = **waited;
    const int ready_fd = ready_event.fd;
    // Dispatch one event immediately: no event batch survives erasure/fd reuse.
    if (ready_fd != listener.get()) {
      const auto client_it = clients.find(ready_fd);
      // Missing ownership is an invariant failure with this one-event design.
      if (client_it == clients.end()) {
        std::cerr << "poller returned an unknown client descriptor\n";
        return 1;
      }
      const char* reason = service_client(poller, client_it->second, ready_event, store);
      // Erase only if service requested it; otherwise leave the owner in the map.
      if (reason != nullptr) {
        retire_client(poller, clients, client_it, reason);
      }
      continue; // Give other buffered clients a turn before another kernel event.
    }
    // Listener readiness must describe accept work, not terminal listener failure.
    if (!ready_event.readable || ready_event.error || ready_event.hangup) {
      std::cerr << "listener reported an unexpected event\n";
      return 1;
    }

    int attempts = 0;
    while (attempts < kAcceptBudget) {
      ++attempts; // Count successes, recoverable errors, and EINTR alike.
      const int accepted_fd = ::accept(listener.get(), nullptr, nullptr);
      // -1 means no socket was returned; errno determines retry versus failure.
      if (accepted_fd == -1) {
        const int error = errno;
        // Interrupted accept consumed a budget slot but no connection owner.
        if (error == EINTR) {
          continue;
        }
        // The pending queue is drained. Level-triggering wakes us on more work.
        if (error == EAGAIN || error == EWOULDBLOCK) {
          break;
        }
        // These Linux pending-connection errors do not invalidate the listener.
        // Other failures (including OS resource exhaustion) stop this server.
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
      UniqueFd client{accepted_fd}; // Adopt before any rejection path can execute.
      // Retained clients are limited independently of the listener backlog.
      if (clients.size() >= kMaxClients) {
        continue; // Local owner closes the rejected connection; no reply promised.
      }
      const auto client_nonblocking = net::set_nonblocking(client.get());
      // Accepted sockets do not automatically inherit the listener's O_NONBLOCK.
      if (!client_nonblocking) {
        report_error("client nonblocking", client_nonblocking.error().value());
        continue; // Configuration failed; local owner closes only this socket.
      }
      const int client_fd = client.get();
      const auto [client_it, inserted] = clients.try_emplace(client_fd, std::move(client));
      // Duplicate fd ownership is impossible under the established invariants.
      if (!inserted) {
        std::cerr << "accepted descriptor already has an owner\n";
        return 1;
      }
      const auto added = poller.add(client_fd, {.read = true});
      // The map now owns the socket. Erase it if no readiness can reach it.
      if (!added) {
        report_error("poller client add", added.error().value());
        clients.erase(client_it);
        continue;
      }
      // Successful lifecycle events are optional to keep measurements useful.
      if (kLogConnections) {
        std::cout << "registered client on fd " << client_fd
                  << ", active=" << clients.size() << '\n';
      }
    }
  }
}
