#pragma once

#include <optional>
#include <string>
#include <unordered_map>

class KeyValueStore {
  public:
    KeyValueStore() noexcept = default;

    void set(const std::string& key, const std::string& value);

    [[nodiscard]]
    std::optional<std::string> get(const std::string& key) const;

    [[nodiscard]]
    bool erase(const std::string& key);

    [[nodiscard]]
    bool exists(const std::string& key) const;

    [[nodiscard]]
    std::size_t count() const noexcept;


  private:
      std::unordered_map<std::string, std::string> data_;
};
