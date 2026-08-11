#include "UniqueFd.hpp"

#include <expected>
#include <system_error>

namespace net {

[[nodiscard]]
std::expected<UniqueFd, std::error_code> create_socket(int domain, int type,
                                                       int protocol);
}
