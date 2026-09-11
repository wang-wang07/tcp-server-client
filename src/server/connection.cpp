#include "tcp/server/connection.hpp"

#include "tcp/protocol/executor.hpp"
#include "tcp/protocol/parser.hpp"

#include <algorithm>
#include <cerrno>
#include <sys/socket.h>
#include <utility>

namespace tcp::server {

// Move ownership from the caller; it must not close the transferred descriptor.
Connection::Connection(UniqueFd socket) : socket(std::move(socket)) {}

InputResult Connection::append_input(std::string_view bytes) {
  // Remove the consumed prefix before checking retained-input capacity.
  if (input_cursor != 0) {
    input_buffer.erase(0, input_cursor);
    input_cursor = 0;
  }

  // Subtraction avoids overflow from summing sizes. The buffer invariant
  // guarantees input_buffer.size() <= kMaxInputBytes before this operation.
  if (bytes.size() > kMaxInputBytes - input_buffer.size()) {
    return InputResult::InputLimit; // Reject the entire new chunk without appending.
  }

  // Empty scheduler views may have null data pointers: skip append in that case.
  if (!bytes.empty()) {
    input_buffer.append(bytes.data(), bytes.size()); // Copy exact bytes, including NUL.
  }
  return InputResult::Ok;
}

InputResult Connection::process_input(std::string_view bytes, KeyValueStore& store) {
  const auto admitted = append_input(bytes);
  // Terminal admission failure must not trigger further command execution.
  if (admitted != InputResult::Ok) {
    return admitted;
  }

  // Reclaim only the already-sent prefix; preserve unsent response ordering.
  if (output_cursor != 0) {
    output_buffer.erase(0, output_cursor);
    output_cursor = 0;
  }
  update_backpressure(); // Recent send progress may permit execution again.

  // A visit is bounded by frames and output pressure. Either stop condition
  // preserves deferred frames for a later user-space scheduling visit.
  for (std::size_t processed = 0;
       processed < kCommandBudget && !output_paused; ++processed) {
    const auto newline = input_buffer.find('\n', input_cursor);
    // No delimiter means only an incomplete tail remains: wait for more data.
    if (newline == std::string::npos) {
      break;
    }

    // Own the frame separately so string mutation cannot invalidate parser input.
    const std::string frame = input_buffer.substr(input_cursor, newline - input_cursor);
    input_cursor = newline + 1; // Consume exactly once, including the delimiter.
    const auto command = parse_command(frame);
    // Parse success executes against shared state; failure queues a protocol
    // error. Both outcomes consume the same one-frame budget slot.
    std::string response = command ? execute_command(*command, store)
                                   : "INVALID COMMAND MESSAGE";
    response.push_back('\n');

    // Defend the individual response allowance and the hard output bound.
    // Execution already occurred: returning a limit error does not roll it back.
    if (response.size() > kMaxResponseBytes ||
        response.size() > kMaxOutputBytes - output_buffer.size()) {
      return InputResult::OutputLimit;
    }
    output_buffer.append(response);
    update_backpressure(); // Crossing high-water prevents another loop iteration.
  }

  // No unconsumed bytes remain; keep the empty-string/zero-cursor invariant.
  if (input_cursor == input_buffer.size()) {
    input_buffer.clear();
    input_cursor = 0;
  }

  // A full tail without a newline cannot become legal within the cap. A full
  // buffer containing complete frames can instead free space by executing them.
  if (pending_input_bytes() == kMaxInputBytes && !has_complete_frame()) {
    return InputResult::InputLimit;
  }
  return InputResult::Ok; // Complete frames may remain despite a successful result.
}

std::expected<bool, std::error_code> Connection::flush_output() {
  constexpr int kWriteCallBudget = 8; // EINTR and successful calls both consume slots.
  constexpr std::size_t kWriteChunkBytes = 4096; // At most 32 KiB accepted per visit.

  // Stop when the queue drains or the attempt budget is exhausted.
  for (int attempt = 0; attempt < kWriteCallBudget && has_pending_output(); ++attempt) {
    const auto remaining = pending_output_bytes();
    const auto requested = std::min(remaining, kWriteChunkBytes);
    // The pointer borrows the unsent suffix for this syscall only. Nonblocking
    // send may accept a shorter prefix. MSG_NOSIGNAL prevents SIGPIPE termination.
    const ssize_t sent = ::send(socket.get(), output_buffer.data() + output_cursor,
                                requested, MSG_NOSIGNAL);
    // Positive results alone advance the cursor and count as timeout progress.
    if (sent > 0) {
      output_cursor += static_cast<std::size_t>(sent);
      last_progress = Clock::now();
      continue; // Try the remaining suffix if another budget slot exists.
    }
    // A nonempty request that makes zero progress must not cause a retry spin.
    if (sent == 0) {
      return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    const int error = errno; // Preserve immediately after the failed syscall.
    // Interruption moved no bytes; retry within the same finite attempt budget.
    if (error == EINTR) {
      continue;
    }
    // Kernel send space is unavailable. Retain cursor/buffer for EPOLLOUT.
    if (error == EAGAIN || error == EWOULDBLOCK) {
      return false;
    }
    return std::unexpected(std::error_code{error, std::generic_category()});
  }

  // Remaining bytes imply budget exhaustion rather than a drained connection.
  if (has_pending_output()) {
    return false;
  }
  output_buffer.clear();
  output_cursor = 0; // Both resets are needed so future replies start at offset zero.
  return true; // Kernel acceptance, not proof of receipt by the peer application.
}

}
