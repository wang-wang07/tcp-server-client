#include "UniqueFD.hpp"

#include <utility>
#include <unistd.h>

// constructor
UniqueFd::UniqueFd(int fd) noexcept
  :fd_(fd)
{
}

// destructor

UniqueFd::~UniqueFd() {
  if (fd_ != -1) {
    ::close(fd_);
  }
}

// move constructor
UniqueFd::UniqueFd(UniqueFd&& other) noexcept
  : fd_(std::exchange(other.fd_, -1))
{
}

// move assignment
UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
  if (this != &other) {
    if (fd_ != -1) {
      ::close(fd_);
    }

    fd_ = std::exchange(other.fd_, -1);
  }

  return *this;
}

// get fd value
int UniqueFd::get() const noexcept {
  return fd_;
}
