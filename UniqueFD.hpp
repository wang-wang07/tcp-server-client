#pragma once

class UniqueFd {
public:

  // constructors
  UniqueFd() noexcept = default;
  explicit UniqueFd(int fd) noexcept;

  // desctructor
  ~UniqueFd();

  // copy construction and assignment are NOT ALLOWED
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  // moving
  UniqueFd(UniqueFd&&) noexcept;
  UniqueFd& operator=(UniqueFd&&) noexcept;

  int get() const noexcept;

private:
  int fd_ = -1;
};
