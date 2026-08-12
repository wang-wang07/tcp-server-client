#pragma once

#include <string>
#include <unordered_map>

class KeyValueStore {
  public:
    KeyValueStore() noexcept = default;
    void set(const std::string& key, const std::string& value);

  private:
      std::unordered_map<std::string, std::string> data_;
};
