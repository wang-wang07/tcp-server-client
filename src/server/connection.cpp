#include "tcp/server/connection.hpp"

#include "tcp/protocol/executor.hpp"
#include "tcp/protocol/parser.hpp"
#include "tcp/store.hpp"

#include <cerrno>
#include <utility>
#include <algorithm>
#include <sys/socket.h>

namespace tcp::server {

  Connection::Connection(UniqueFd socket)
      : socket(std::move(socket)) {}

  std::expected<bool, std::error_code> Connection::flush_output() {
    constexpr int kWriteCallBudget = 8;
    constexpr std::size_t kWriteChunkBytes = 4096;

    for (int attempt = 0; attempt < kWriteCallBudget && has_pending_output(); attempt++) {
      const std::size_t remaining = output_buffer.size() - output_cursor;
      const std::size_t requested = std::min(remaining, kWriteChunkBytes);

      const ssize_t sent = ::send(socket.get(), output_buffer.data() + output_cursor, requested, MSG_NOSIGNAL);
      if (sent > 0) {
        output_cursor += static_cast<std::size_t>(sent);
        last_progress = Clock::now();
        continue;
      }
      if (sent == 0) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
      }

      const int error = errno;
      if (error == EINTR) {
        continue;
      }

      if (error == EAGAIN || error == EWOULDBLOCK) {
        return false;
      }

      return std::unexpected(std::error_code{error, std::generic_category()});
    }

    if (has_pending_output()) {
      return false;
    }


    output_buffer.clear();
    output_cursor = 0;
    return true;
  }

  InputResult Connection::append_input(std::string_view bytes) {
    if (input_cursor != 0) {
      input_buffer.erase(0, input_cursor);
      input_cursor = 0;
    }

    if (bytes.size() > kMaxInputBytes - input_buffer.size()) {
      return InputResult::InputLimit;
    }

    if (!bytes.empty()) {
      input_buffer.append(bytes.data(), bytes.size());
    }

    return InputResult::Ok;
  }

  InputResult Connection::process_input(
      std::string_view bytes, KeyValueStore& store) {
    const auto admitted = append_input(bytes);

    if (admitted != InputResult::Ok) {
      return admitted;
    }

    if (output_cursor != 0) {
      output_buffer.erase(0, output_cursor);
      output_cursor = 0;
    }

    update_backpressure();

    for (std::size_t processed = 0;
         processed < kCommandBudget && !output_paused;
         ++processed) {
      const auto newline = input_buffer.find('\n', input_cursor);

      if (newline == std::string::npos) {
        break;
      }

      const std::string frame =
          input_buffer.substr(input_cursor, newline - input_cursor);

      input_cursor = newline + 1;

      const auto command = parse_command(frame);

      std::string response = command
          ? execute_command(*command, store)
          : "INVALID COMMAND MESSAGE";

      response.push_back('\n');

      if (response.size() > kMaxResponseBytes ||
          response.size() > kMaxOutputBytes - output_buffer.size()) {
        return InputResult::OutputLimit;
      }

      output_buffer.append(response);

      update_backpressure();
    }

    if (input_cursor == input_buffer.size()) {
      input_buffer.clear();
      input_cursor = 0;
    }

    if (pending_input_bytes() == kMaxInputBytes && !has_complete_frame()) {
      return InputResult::InputLimit;
    }

    return InputResult::Ok;
  }
}
