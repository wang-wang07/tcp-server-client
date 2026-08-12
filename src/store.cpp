#include "store.hpp"


void KeyValueStore::set(const std::string& key, const std::string& value) {
  data_[key] = value;
}
