#pragma once

#include "tcp/net/unique_fd.hpp"

#include <chrono>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

class KeyValueStore;

namespace tcp::server {

// Ok means no terminal limit violation, not necessarily that all frames ran.
// The server retires a connection on either limit result; mutations already
// executed by process_input() are not rolled back.
enum class InputResult { Ok, InputLimit, OutputLimit };

// One movable owner for a client socket and its persistent stream state.
// Only the server's single event-loop thread accesses this object.
struct Connection {
  // A monotonic clock makes elapsed-time checks immune to calendar-clock jumps.
  using Clock = std::chrono::steady_clock;

  // A wire frame must fit including its newline. The individual response
  // allowance also includes a newline; bounded SET frames imply bounded values.
  static constexpr std::size_t kMaxInputBytes = 64 * 1024;
  static constexpr std::size_t kMaxResponseBytes = 64 * 1024;

  // Before executing a command, pending output is below high-water. Reserve
  // another maximum response so crossing that watermark cannot overflow output.
  static constexpr std::size_t kMaxOutputBytes = 128 * 1024;
  static constexpr std::size_t kOutputHighWater = 64 * 1024;
  static constexpr std::size_t kOutputLowWater = 32 * 1024;
  static_assert(kOutputHighWater + kMaxResponseBytes <= kMaxOutputBytes);

  // Charge every complete frame, including malformed/empty frames, to prevent
  // invalid traffic from bypassing the work budget.
  static constexpr std::size_t kCommandBudget = 32;

  explicit Connection(UniqueFd socket);
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) noexcept = default;
  Connection& operator=(Connection&&) noexcept = default;

  // Copy external bytes without executing commands. The view must not alias
  // input_buffer: compaction may invalidate pointers into that owned string.
  [[nodiscard]] InputResult append_input(std::string_view bytes);

  // Admit bytes and execute at most one command budget. An empty view resumes
  // owned input without requiring another TCP receive or another client send.
  [[nodiscard]] InputResult process_input(std::string_view bytes, KeyValueStore& store);

  // True = drained; false = still pending; unexpected = terminal send failure.
  // Ownership remains here in every case. The server decides when to retire.
  [[nodiscard]] std::expected<bool, std::error_code> flush_output();

  [[nodiscard]] std::size_t pending_input_bytes() const noexcept {
    // The cursor marks a consumed prefix, so only the suffix requires parsing.
    return input_buffer.size() - input_cursor;
  }

  [[nodiscard]] std::size_t pending_output_bytes() const noexcept {
    // Bytes before the cursor have already been accepted by the kernel.
    return output_buffer.size() - output_cursor;
  }

  [[nodiscard]] bool has_pending_output() const noexcept {
    // Any nonempty unsent suffix needs a future send attempt.
    return pending_output_bytes() != 0;
  }

  [[nodiscard]] bool has_complete_frame() const noexcept {
    // npos means the unconsumed suffix has no delimiter and is only a fragment.
    return input_buffer.find('\n', input_cursor) != std::string::npos;
  }

  [[nodiscard]] bool has_runnable_input() const noexcept {
    // Buffered complete frames need scheduling only if output pressure permits
    // execution. Scheduling a paused connection repeatedly would busy-loop.
    return !output_paused && has_complete_frame();
  }

  [[nodiscard]] bool can_read() const noexcept {
    // All conditions are required: the stream is open, output permits admission,
    // existing complete commands get service first, and at least one byte fits.
    return !read_eof && !output_paused && !has_complete_frame()
        && pending_input_bytes() < kMaxInputBytes;
  }

  void update_backpressure() noexcept {
    const auto pending = pending_output_bytes();
    // Reaching high-water stops command execution and socket reads.
    if (pending >= kOutputHighWater) {
      output_paused = true;
    } else if (pending <= kOutputLowWater) {
      // Resume only after sufficient drainage, not immediately below high-water.
      output_paused = false;
    }
    // Between thresholds retain the prior state: this is hysteresis.
  }

  UniqueFd socket; // The sole owner; destruction closes this client descriptor.
  std::string input_buffer;
  std::size_t input_cursor = 0; // Invariant: cursor <= input_buffer.size().
  std::string output_buffer;
  std::size_t output_cursor = 0; // Invariant: cursor <= output_buffer.size().
  bool read_eof = false; // Stops reads permanently; queued replies may still drain.
  bool output_paused = false; // Stops reads and execution, but permits sends.
  Clock::time_point last_progress = Clock::now(); // Refreshed only by byte movement.
};

}
