#include "tcp/net/socket.hpp"
#include "tcp/net/unique_fd.hpp"
#include "tcp/server/connection.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using ConnectionMap =
    std::unordered_map<int, tcp::server::Connection>;

constexpr std::size_t kMaximumConnections = 1024;
constexpr std::size_t kAcceptBudget = 64;
constexpr std::size_t kReadChunkSize = 4096;
constexpr std::size_t kReadBudget = 64 * 1024;
constexpr std::size_t kMaximumInputBuffer = 128 * 1024;

enum class ReadResult {
  KeepConnection,
  CloseConnection,
};

bool accept_ready_clients(
    int listener,
    std::vector<pollfd>& poll_descriptors,
    ConnectionMap& connections)
{
  std::size_t accepted_this_iteration = 0;

  while (accepted_this_iteration < kAcceptBudget) {
    const int accepted_fd =
        ::accept(listener, nullptr, nullptr);

    if (accepted_fd == -1) {
      // errno may be changed by later library calls, so copy it immediately.
      const int error = errno;

      // A caught signal interrupted accept() before it completed. Nothing was
      // accepted, so retry without charging the accept budget.
      if (error == EINTR) {
        continue;
      }

      // The nonblocking listener's queue is drained. This is not a failure;
      // control should return to poll() until the listener becomes ready again.
      if (error == EAGAIN || error == EWOULDBLOCK) {
        return true;
      }

      // A client abandoned a queued handshake. Skip that one connection while
      // keeping the listening socket and the rest of the server alive.
      if (error == ECONNABORTED) {
        continue;
      }

      // Other accept errors indicate that this listener can no longer be used
      // reliably by this simple tutorial server.
      std::cerr
          << "accept failed: "
          << std::error_code(
                 error,
                 std::generic_category()).message()
          << '\n';

      return false;
    }

    // The budget counts every successfully accepted socket, including one that
    // is immediately rejected because the server is full.
    ++accepted_this_iteration;

    // Adopt the raw descriptor immediately. Every continue/return below then
    // destroys client and closes the descriptor automatically (RAII).
    UniqueFd client{accepted_fd};

    // The kernel accepted the TCP connection, but this process has reached its
    // configured active-client limit. Destroying client rejects it cleanly.
    if (connections.size() >= kMaximumConnections) {
      continue;
    }

    // Accepted sockets are configured separately for portability; code must
    // not assume that they inherit O_NONBLOCK from the listening socket.
    const auto nonblocking =
        net::set_nonblocking(client.get());

    if (!nonblocking) {
      std::cerr
          << "could not make client nonblocking: "
          << nonblocking.error().message()
          << '\n';

      // Registering a blocking client would allow recv() to freeze the entire
      // single-threaded event loop, so this client must be discarded.
      continue;
    }

    // Save the integer before moving client. Moving transfers ownership into
    // Connection and leaves the local UniqueFd invalid.
    const int client_fd = client.get();

    // try_emplace constructs the move-only Connection in place only if the raw
    // descriptor is not already a key in the map.
    const auto insertion =
        connections.try_emplace(
            client_fd,
            std::move(client));

    if (!insertion.second) {
      std::cerr << "duplicate client descriptor\n";
      continue;
    }

    // The map owns the socket. This pollfd is only a non-owning registration
    // saying: "wake the loop when this descriptor can be read."
    poll_descriptors.push_back({
        .fd = client_fd,
        .events = POLLIN,
        .revents = 0,
    });

    std::cout
        << "client connected on fd "
        << client_fd
        << '\n';
  }

  // Reaching the accept budget is also successful. poll() is level-triggered,
  // so it will report the listener again if queued connections remain.
  return true;
}

// Read currently available bytes from one client without allowing that client
// to monopolize the loop or grow its persistent input buffer indefinitely.
ReadResult read_ready_client(
    tcp::server::Connection& connection)
{
  // This temporary array is reused for each recv() in this function call. It
  // is not the persistent message store; input_buffer below fills that role.
  std::array<char, kReadChunkSize> chunk{};
  std::size_t total_read = 0;

  // A readable TCP socket may hold much more than one chunk. Continue until it
  // is drained, closed, fails, fills our buffer, or consumes this pass's budget.
  while (total_read < kReadBudget) {
    // input_buffer persists between readiness events. Since this checkpoint
    // does not parse frames yet, reaching the cap means the peer must be closed.
    if (connection.input_buffer.size() >=
        kMaximumInputBuffer) {
      return ReadResult::CloseConnection;
    }

    // Both subtractions are safe: the while-condition and size check above
    // establish that the left side is at least as large as the right side.
    const std::size_t remaining_budget =
        kReadBudget - total_read;

    const std::size_t remaining_buffer_space =
        kMaximumInputBuffer -
        connection.input_buffer.size();

    // recv() must not exceed any of three independent bounds:
    //   1. the temporary array's physical capacity,
    //   2. this client's remaining fairness budget for this pass, or
    //   3. the persistent input buffer's remaining memory allowance.
    const std::size_t requested_size =
        std::min({
            chunk.size(),
            remaining_budget,
            remaining_buffer_space,
        });

    const ssize_t received =
        ::recv(
            connection.fd(),
            chunk.data(),
            requested_size,
            0);

    if (received > 0) {
      // ssize_t can represent -1 for errors. Convert only after proving the
      // value is positive, making the conversion to size_t safe.
      const auto received_size =
          static_cast<std::size_t>(received);

      // append(pointer, count) preserves exactly count bytes, including '\0'.
      // TCP carries bytes, so treating this as a C string would be incorrect.
      connection.input_buffer.append(
          chunk.data(),
          received_size);

      // Activity time belongs to persistent connection state and will support
      // idle timeouts in a later checkpoint.
      connection.last_activity =
          tcp::server::Connection::Clock::now();

      total_read += received_size;
      continue;
    }

    // For a TCP stream, recv() == 0 is EOF: the peer performed an orderly
    // shutdown and no future bytes will arrive on this connection.
    if (received == 0) {
      return ReadResult::CloseConnection;
    }

    // received == -1 here. Preserve errno before doing any other work.
    const int error = errno;

    // No bytes were transferred because a signal interrupted the call. Retry
    // the same operation without consuming byte budget.
    if (error == EINTR) {
      continue;
    }

    // Readiness is only a snapshot. Another condition may drain/change the
    // socket before recv(), so nonblocking recv() can still report "not now."
    // The descriptor remains healthy and should go back to poll().
    if (error == EAGAIN || error == EWOULDBLOCK) {
      return ReadResult::KeepConnection;
    }

    // Any other read error affects this client, not the listening socket or
    // other clients, so ask the caller to remove only this connection.
    return ReadResult::CloseConnection;
  }

  // This client consumed its per-pass budget. Keep it registered; if unread
  // bytes remain, level-triggered poll() will report it again on the next pass.
  return ReadResult::KeepConnection;
}

// Remove a client from both reactor bookkeeping structures. The listener is
// always at index 0, so callers use this only for indices 1 and above.
void remove_connection(
    std::size_t index,
    std::vector<pollfd>& poll_descriptors,
    ConnectionMap& connections)
{
  const int client_fd =
      poll_descriptors[index].fd;

  // Client ordering has no meaning. Moving the last element into this hole is
  // constant-time, unlike vector::erase(), which shifts all following entries.
  if (index != poll_descriptors.size() - 1) {
    poll_descriptors[index] =
        poll_descriptors.back();
  }

  poll_descriptors.pop_back();

  // Erasing destroys Connection, which destroys UniqueFd, which calls close().
  connections.erase(client_fd);

  std::cout
      << "client disconnected from fd "
      << client_fd
      << '\n';
}

}  // namespace

int main()
{
  // getaddrinfo() accepts hints describing the kind of local address wanted.
  // Zero-initialization is important because the structure has many fields.
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;      // Accept an IPv4 or IPv6 result.
  hints.ai_socktype = SOCK_STREAM; // TCP provides a reliable byte stream.
  hints.ai_flags = AI_PASSIVE;     // Produce wildcard addresses for bind().

  addrinfo* results = nullptr;

  // A null host plus AI_PASSIVE means "listen on local interfaces." Service
  // "8080" selects the TCP port. results becomes a linked list of candidates.
  const int status =
      ::getaddrinfo(nullptr, "8080", &hints, &results);

  if (status != 0) {
    std::cerr
        << "getaddrinfo() failed: "
        << ::gai_strerror(status)
        << '\n';
    return 1;
  }

  // This RAII owner starts invalid and takes ownership of the first socket that
  // successfully binds. Reassignment closes failed earlier candidates.
  UniqueFd server_fd;
  bool bound = false;

  for (addrinfo* address = results;
       address != nullptr;
       address = address->ai_next) {
    auto socket =
        net::create_socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol);

    if (!socket) {
      continue;
    }

    server_fd = std::move(*socket);

    if (::bind(
            server_fd.get(),
            address->ai_addr,
            address->ai_addrlen) == 0) {
      bound = true;
      break;
    }
  }

  // getaddrinfo() allocated the entire candidate list. None of its address
  // pointers are needed after bind(), so release the list on every later path.
  ::freeaddrinfo(results);

  if (!bound) {
    std::cerr << "could not bind to any address\n";
    return 1;
  }

  // Convert the bound socket into a listening socket. The backlog is the
  // kernel's pending-connection queue hint, not the active-client limit.
  if (::listen(server_fd.get(), 10) == -1) {
    std::cerr << "listen() failed\n";
    return 1;
  }

  // The accept loop intentionally calls accept() until EAGAIN. Therefore the
  // listener itself must be nonblocking or the final accept() would hang.
  const auto nonblocking =
      net::set_nonblocking(server_fd.get());

  if (!nonblocking) {
    std::cerr
        << "could not make listener nonblocking: "
        << nonblocking.error().message()
        << '\n';
    return 1;
  }

  // poll() requires contiguous pollfd records. Reserve capacity for every
  // allowed client plus the listener at index 0; reserve does not add elements.
  std::vector<pollfd> poll_descriptors;
  poll_descriptors.reserve(kMaximumConnections + 1);

  // events is our requested interest. revents is output written by poll().
  poll_descriptors.push_back({
      .fd = server_fd.get(),
      .events = POLLIN,
      .revents = 0,
  });

  // The map, rather than poll_descriptors, owns client sockets and buffers.
  ConnectionMap connections;
  connections.reserve(kMaximumConnections);

  while (true) {
    int poll_result;

    // A timeout of -1 sleeps indefinitely until at least one descriptor has an
    // event. Retry if a signal interrupted the wait before events were returned.
    do {
      poll_result =
          ::poll(
              poll_descriptors.data(),
              static_cast<nfds_t>(
                  poll_descriptors.size()),
              -1);
    } while (poll_result == -1 && errno == EINTR);

    if (poll_result == -1) {
      const int error = errno;

      std::cerr
          << "poll failed: "
          << std::error_code(
                 error,
                 std::generic_category()).message()
          << '\n';
      return 1;
    }

    // poll() has filled each record's revents. Scan all records because ready
    // descriptors need not be adjacent and multiple flags can coexist.
    std::size_t index = 0;

    while (index < poll_descriptors.size()) {
      // Copy rather than reference revents. Accepting clients may grow and
      // reallocate the vector, invalidating all references into it.
      const short returned_events =
          poll_descriptors[index].revents;

      if (returned_events == 0) {
        ++index;
        continue;
      }

      // The listener is permanently stored at index 0 and has different event
      // semantics from client sockets: readable means "connections to accept."
      if (index == 0) {
        // These conditions make the listener itself unusable. There is no
        // enclosing supervisor here that could replace it, so stop the server.
        if ((returned_events &
             (POLLERR | POLLHUP | POLLNVAL)) != 0) {
          std::cerr
              << "listener reported a fatal poll event\n";
          return 1;
        }

        if ((returned_events & POLLIN) != 0) {
          if (!accept_ready_clients(
                  server_fd.get(),
                  poll_descriptors,
                  connections)) {
            return 1;
          }
        }

        ++index;
        continue;
      }

      // POLLERR and POLLNVAL make a client unusable. POLLNVAL commonly means
      // the registered integer is not an open descriptor.
      bool should_close =
          (returned_events &
           (POLLERR | POLLNVAL)) != 0;

      // Flags are a bit mask, so use bitwise &, not equality. For example, a
      // peer can produce POLLIN | POLLHUP when it sends final bytes then closes.
      if (!should_close &&
          (returned_events & POLLIN) != 0) {
        const int client_fd =
            poll_descriptors[index].fd;

        const auto connection =
            connections.find(client_fd);

        // The vector and map should always agree. Close the registration if an
        // internal bookkeeping error ever violates that invariant.
        if (connection == connections.end()) {
          should_close = true;
        } else {
          should_close =
              read_ready_client(connection->second) ==
              ReadResult::CloseConnection;
        }
      }

      // Check hangup after POLLIN so final readable bytes are drained first.
      // This checkpoint only buffers them; later parsing can consume them.
      if ((returned_events & POLLHUP) != 0) {
        should_close = true;
      }

      if (should_close) {
        remove_connection(
            index,
            poll_descriptors,
            connections);

        // Swap-and-pop moved a different descriptor into this same index. Do
        // not increment yet or that descriptor's returned events would be skipped.
        continue;
      }

      ++index;
    }
  }
}
