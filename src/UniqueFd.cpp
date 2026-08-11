#include "UniqueFd.hpp"

#include <utility>
#include <unistd.h>
#include <iostream>

// constructor
UniqueFd::UniqueFd(int fd) noexcept
  :fd_(fd)
{
  std::cout << "Contructor\n";
}

// destructor

UniqueFd::~UniqueFd() {
  if (fd_ != -1) {
    ::close(fd_);
  }

  std::cout << "destructor\n";
}

// move constructor
UniqueFd::UniqueFd(UniqueFd&& other) noexcept
  : fd_(std::exchange(other.fd_, -1))
{
  std::cout << "move constructor\n";
}

// move assignment
UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
  if (this != &other) {
    if (fd_ != -1) {
      ::close(fd_);
    }

    fd_ = std::exchange(other.fd_, -1);
  }

  std::cout << "move assignemnt\n";
  return *this;
}

// get fd value
int UniqueFd::get() const noexcept {
  return fd_;
}

// valid: checks if get() is -1
bool UniqueFd::valid() const noexcept {
  return fd_ != 1;
}
