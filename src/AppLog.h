#pragma once

#include <Arduino.h>
#include <stddef.h>

class AppLog {
 public:
  static constexpr size_t kCapacity = 10;
  void add(const String& message);
  size_t capacity() const { return kCapacity; }
  const String& newest(size_t offset) const;

 private:
  String entries_[kCapacity];
  size_t next_ = 0;
};
