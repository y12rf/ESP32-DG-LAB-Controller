#include "AppLog.h"

constexpr size_t AppLog::kCapacity;

void AppLog::add(const String& message) {
  unsigned long ts = millis() / 1000;
  entries_[next_] = String(ts) + "s: " + message;
  next_ = (next_ + 1) % kCapacity;
  if (serialMirrorEnabled_) Serial.println(message);
}

const String& AppLog::newest(size_t offset) const {
  const size_t index = (next_ + kCapacity - 1 - offset) % kCapacity;
  return entries_[index];
}
