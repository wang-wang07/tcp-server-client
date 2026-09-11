#pragma once

#include "tcp/net/unique_fd.hpp"
#include "tcp/store.hpp"

#include <chrono>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

class KeyValueStore;

namespace tcp::server {
  enum class InputResult {
    Ok,
    InputLimit,
    OutputLimit,
  };

  struct Connection {
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t kMaxInputBytes = 64 * 1024;
    static constexpr std::size_t kMaxResponseBytes = 64 * 1024;

    // hard limit
    static constexpr std::size_t kMaxOutputBytes = 128 * 1024;

    //soft limit
    static constexpr std::size_t kOutputHighWater = 64 * 1024;
    static constexpr std::size_t kOutputLowerWater = 32 * 1024;

    static constexpr std::size_t kCommandBudget = 32; // counts frames (inlucding invalid)

    static_assert(kOutputHighWater + kMaxResponseBytes <= kMaxOutputBytes);

    explicit Connection(UniqueFd socket);

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    [[nodiscard]] InputResult append_input(std::string_view bytes);
    [[nodiscard]] InputResult process_input(std::string_view bytes, KeyValueStore& store);

    [[nodiscard]] std::expected<bool, std::error_code> flush_output();

    [[nodiscard]] std::size_t pending_input_bytes() const noexcept {
      return input_buffer.size() - input_cursor;
    }

    [[nodiscard]] std::size_t pending_output_bytes() const noexcept {
      return output_buffer.size() - output_cursor;;
    }

    [[nodiscard]] bool has_pending_output() const noexcept {
      return pending_output_bytes() != 0;
    }

    [[nodiscard]] bool has_complete_frame() const noexcept {
      return input_buffer.find('\n', input_cursor) != std::string::npos;
    }

    [[nodiscard]] bool has_runnable_input() const noexcept {
      return !output_paused && has_complete_frame();
    }

    [[nodiscard]] bool can_read() const noexcept {
      return !read_eof && !output_paused && !has_complete_frame() && !pending_input_bytes() < kMaxInputBytes;
    }

    void update_backpressure() noexcept {
      const auto pending = pending_output_bytes();

      if (pending >= kOutputHighWater) {
        output_paused = true;
      } else if (pending <= kOutputLowerWater) {
        output_paused = false;
      }
    }

    UniqueFd socket;

    std::string input_buffer;
    std::size_t input_cursor = 0;

    std::string output_buffer;
    std::size_t output_cursor = 0;

    bool read_eof = false;
    bool output_paused = false;
    Clock::time_point last_progress = Clock::now();
  };
}
