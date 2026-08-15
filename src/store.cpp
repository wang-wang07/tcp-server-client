#include "tcp/store.hpp"


void KeyValueStore::set(const std::string& key, const std::string& value) {
  data_[key] = value;
}

std::optional<std::string>
KeyValueStore::get(const std::string& key) const {
  auto item = data_.find(key);

  if (item == data_.end()) {
    return std::nullopt;
  }

  return item->second;
}

bool KeyValueStore::erase(const std::string& key) {
  return data_.erase(key) > 0;
}

bool KeyValueStore::exists(const std::string& key) const {
  return data_.contains(key);
}

std::size_t KeyValueStore::count() const noexcept{
  return data_.size();
}
