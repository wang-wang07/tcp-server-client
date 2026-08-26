#pragma once

#include "tcp/net/unique_fd.hpp"

#include <chrono>
#include <cstddef>
#include <string>

namespace tcp::server {
  struct Connection {
    using Clock = std::chrono::steady_clock;
    explicit Connection(UniqueFd socket);

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection &&) noexcept = default;

    [[nodiscard]]int fd() const noexcept;
    [[nodiscard]]bool has_pending_output() const noexcept;

    UniqueFd socket;

    std::string input_buffer;
    std::size_t input_cursor = 0;

    std::string output_buffer;
    std::size_t output_cursor = 0;

    Clock::time_point last_activity;
  };
}
