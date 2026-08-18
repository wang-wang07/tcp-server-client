#pragma once

#include "tcp/net/unique_fd.hpp"
#include "tcp/store.hpp"

namespace tcp::server {
  void handle_client(UniqueFd clientFd, KeyValueStore& store);
}
